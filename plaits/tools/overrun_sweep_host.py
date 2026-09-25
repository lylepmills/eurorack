#!/usr/bin/env python3
"""Host side of the on-module overrun sweep (plaits/overrun_sweep.h).

Patch (ES-8 numbering, 1-based):
  Plaits OUT  -> ES-8 input 1     (pilot sine: the stale-block detector)
  Plaits AUX  -> ES-8 input 2     (FSK packets: the module's own results)
  ES-8 out 3  -> Plaits MODEL     (firmware WAV into the audio bootloader)

Commands:
  flash FIRMWARE.wav          play a firmware WAV on one ES-8 output only
  capture OUT.wav --seconds N record ES-8 inputs 1-2
  decode CAPTURE.wav          decode packets, find pilot glitches, report
  run --manifest M --out DIR  capture a whole group sweep, then decode

Needs numpy + soundfile (decode) and sounddevice (flash/capture).
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

# ---- Schedule, mirrored from overrun_sweep.h --------------------------------

MODULE_RATE = 47872.34
BLOCK = 12
BLOCKS_PER_SECOND = 3989
PARAM_STATES = 81
PITCHES = 5
OUTPUTS = 3
TRIGGERS = 3
GRID_STATES = PARAM_STATES * PITCHES * OUTPUTS * TRIGGERS
STATE_BLOCKS = 48
SETTLE_BLOCKS = 8
RANDOM_STATES = 400
TAIL_CASES = 6
START_BLOCKS = 2 * BLOCKS_PER_SECOND
LADDER_STEPS = 21
LADDER_SETTLE_BLOCKS = BLOCKS_PER_SECOND // 5
LADDER_BLOCKS = BLOCKS_PER_SECOND
SWITCH_BLOCKS = 9 * BLOCKS_PER_SECOND // 2
BAUD = 1200
MARK_HZ = 2400.0
SPACE_HZ = 4800.0
LEAD_IN_BITS = 24

PACKET_HELLO = 1
PACKET_LADDER = 2
PACKET_ENGINE_START = 3
PACKET_ENGINE_RESULT = 4
PACKET_END = 5
PACKET_CONDITIONS = 6
PACKET_SETTLE = 7
PACKET_THRESHOLD = 8
PACKET_SCENE = 9
PACKET_SECTIONS = 10
# Scene-check sections, by how many the firmware reports: version 1 split the
# block in four; version 2 at every mark in plaits/section_marks.h.
SECTION_NAMES = {
    4: ["ui", "voice", "engine", "post"],
    12: ["ui inputs", "ui task", "ui adc/pitch", "prepare", "select",
         "envelopes", "modulation", "engine", "subosc", "lpg env",
         "out post", "aux post"],
}

OUTPUT_NAMES = ["regular", "stereo", "sub-osc"]
TRIGGER_NAMES = ["unpatched", "triggered", "gated"]
LEVELS = [0.0, 0.5, 1.0]
CONDITIONS = TRIGGERS * OUTPUTS * PITCHES


def condition_name(index: int) -> str:
    pitch = index % PITCHES
    output = (index // PITCHES) % OUTPUTS
    trigger = index // (PITCHES * OUTPUTS)
    return (f"{TRIGGER_NAMES[trigger]}/{OUTPUT_NAMES[output]}/"
            f"note {12 + 24 * pitch}")


def ladder_load(step: int) -> float:
    return 0.8 + 0.02 * step


def grid_state(index: int) -> dict:
    param = index % PARAM_STATES
    pitch = (index // PARAM_STATES) % PITCHES
    output = (index // (PARAM_STATES * PITCHES)) % OUTPUTS
    trigger = index // (PARAM_STATES * PITCHES * OUTPUTS)
    return {
        "harmonics": LEVELS[param % 3],
        "timbre": LEVELS[(param // 3) % 3],
        "morph": LEVELS[(param // 9) % 3],
        "twist": LEVELS[param // 27],
        "note": 12 + 24 * pitch,
        "output": OUTPUT_NAMES[output],
        "trigger": TRIGGER_NAMES[trigger],
    }


def _lcg(x: int) -> int:
    return (x * 1664525 + 1013904223) & 0xFFFFFFFF


def random_state(engine: int, index: int) -> dict:
    x = (0x9E3779B9 * (engine + 1) + 0x85EBCA6B * (index + 1)) & 0xFFFFFFFF
    x = _lcg(x)
    values = []
    for _ in range(5):
        x = _lcg(x)
        values.append((x >> 8) * (1.0 / 16777215.0))
    x = _lcg(x)
    output = (x >> 16) % OUTPUTS
    x = _lcg(x)
    trigger = (x >> 16) % TRIGGERS
    return {
        "harmonics": round(values[0], 3),
        "timbre": round(values[1], 3),
        "morph": round(values[2], 3),
        "twist": round(values[3], 3),
        "note": round(12 + 96 * values[4], 1),
        "output": OUTPUT_NAMES[output],
        "trigger": TRIGGER_NAMES[trigger],
    }


def tail_state(index: int) -> dict:
    return {
        "harmonics": 0.5, "timbre": 0.5, "twist": 0.5,
        "morph": LEVELS[index % 3], "note": 36,
        "output": "regular" if index < 3 else "stereo",
        "trigger": "one strike, then ring-out",
    }


def grid_sequence(stereo_capable: bool) -> list[int]:
    """Grid state indices in the order the module runs them."""
    if stereo_capable:
        return list(range(GRID_STATES))
    return [i for i in range(GRID_STATES)
            if (i // (PARAM_STATES * PITCHES)) % OUTPUTS != 1]


def engine_phases(stereo_capable: bool, tail_seconds: int) -> list[tuple]:
    """(name, module samples) for one engine's sweep, in order."""
    grid = len(grid_sequence(stereo_capable))
    tails = TAIL_CASES if stereo_capable else TAIL_CASES // 2
    tail_blocks = tail_seconds * BLOCKS_PER_SECOND
    return [
        ("switch", SWITCH_BLOCKS * BLOCK),
        ("grid", grid * STATE_BLOCKS * BLOCK),
        ("random", RANDOM_STATES * STATE_BLOCKS * BLOCK),
        ("tail", tails * tail_blocks * BLOCK),
    ]


# ---- FSK + packets ----------------------------------------------------------

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def _moving_average(x: np.ndarray, width: int) -> np.ndarray:
    kernel = np.ones(width) / width
    return np.convolve(x, kernel, mode="same")


def demodulate_uart(x: np.ndarray, sr: float, offset: int,
                    accept_until: int) -> list[tuple[int, int]]:
    """UART bytes (absolute start-bit sample, value) found in `x`.

    Only start bits before `accept_until` (chunk-relative) are kept, so
    overlapping chunks never report a byte twice.
    """
    spb = sr / BAUD
    width = max(4, int(round(spb / 2)))
    n = np.arange(len(x))
    mark = np.abs(_moving_average(
        x * np.exp(-2j * np.pi * MARK_HZ * n / sr), width))
    space = np.abs(_moving_average(
        x * np.exp(-2j * np.pi * SPACE_HZ * n / sr), width))
    rms = np.sqrt(_moving_average(x * x, width)) + 1e-9
    present = (mark + space) > 0.45 * rms
    present &= rms > 1e-3
    bit = mark > space
    candidates = np.flatnonzero(bit[:-1] & ~bit[1:] & present[1:]) + 1
    out: list[tuple[int, int]] = []
    limit = len(x) - int(10 * spb) - 1
    next_allowed = 0
    for i in candidates:
        if i < next_allowed or i > limit or i >= accept_until:
            continue
        centres = [int(round(i + (k + 0.5) * spb)) for k in range(10)]
        if bit[centres[0]] or not bit[centres[9]]:
            continue
        if not all(present[c] for c in centres):
            continue
        value = 0
        for k in range(8):
            if bit[centres[k + 1]]:
                value |= 1 << k
        out.append((offset + int(i), value))
        next_allowed = int(i + 9.5 * spb)
    return out


@dataclass
class Packet:
    type: int
    payload: bytes
    time: int          # host sample of the first byte's start bit
    burst: int = 0     # host sample at which the burst's lead-in began


def parse_packets(stream: list[tuple[int, int]], sr: float) -> list[Packet]:
    byte_time = 10 * sr / BAUD
    # Bursts: runs of bytes with no gap longer than ~1.5 byte times.
    burst_start = None
    previous = None
    burst_of: list[int] = []
    for t, _ in stream:
        if previous is None or t - previous > 1.5 * byte_time:
            burst_start = t - LEAD_IN_BITS * sr / BAUD
        burst_of.append(int(round(burst_start)))
        previous = t
    packets: list[Packet] = []
    values = [v for _, v in stream]
    i = 0
    while i + 5 < len(values):
        if values[i] == 0x7E and values[i + 1] == 0xA5:
            ptype = values[i + 2]
            length = values[i + 3]
            end = i + 4 + length + 2
            if end <= len(values):
                body = bytes(values[i + 2:i + 4 + length])
                crc = values[end - 2] | (values[end - 1] << 8)
                if crc16(body) == crc:
                    packets.append(Packet(
                        ptype, bytes(values[i + 4:i + 4 + length]),
                        stream[i][0] - 2 * int(byte_time), burst_of[i]))
                    i = end
                    continue
        i += 1
    return packets


def _u16(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<H", payload, offset)[0]


def decode_stats(payload: bytes, offset: int) -> dict:
    fields = struct.unpack_from("<5H", payload, offset)
    return {
        "peak": fields[0] / 1000.0,
        "late": fields[1],
        "late_states": fields[2],
        "worst_state": None if fields[3] == 0xFFFF else fields[3],
        "over_ninety_blocks": fields[4],
    }


def decode_packet(packet: Packet) -> dict:
    p = packet.payload
    if packet.type == PACKET_HELLO:
        return {"type": "hello", "version": p[0], "group": p[1],
                "engines": p[2], "ladder_steps": p[3],
                "grid_states": _u16(p, 4), "random_states": _u16(p, 6)}
    if packet.type == PACKET_LADDER:
        steps = []
        for k in range(len(p) // 6):
            peak, late, late_overhead = struct.unpack_from("<3H", p, 6 * k)
            steps.append({"load": round(ladder_load(k), 2), "peak": peak / 1000.0,
                          "late": late, "late_with_overhead": late_overhead})
        return {"type": "ladder", "steps": steps}
    if packet.type == PACKET_ENGINE_START:
        return {"type": "engine_start", "engine": p[0]}
    if packet.type == PACKET_ENGINE_RESULT:
        result = {"type": "engine_result", "engine": p[0],
                  "stereo_capable": bool(p[1]),
                  "grid": decode_stats(p, 2),
                  "random": decode_stats(p, 12),
                  "tail": decode_stats(p, 22)}
        (result["tail_worst_block"], result["switch_in_peak"],
         result["switch_in_late"], result["double_pending"],
         result["late_with_overhead"]) = struct.unpack_from("<5H", p, 32)
        if len(p) >= 44:
            result["dropped_states"] = _u16(p, 42)
        result["switch_in_peak"] /= 1000.0
        return result
    if packet.type == PACKET_CONDITIONS:
        conditions = []
        for c in range(CONDITIONS):
            peak, late = struct.unpack_from("<2H", p, 1 + 4 * c)
            conditions.append({"condition": condition_name(c),
                               "peak": peak / 1000.0, "late": late})
        tails = []
        base = 1 + 4 * CONDITIONS
        for t in range(TAIL_CASES):
            peak, late, final = struct.unpack_from("<3H", p, base + 6 * t)
            tails.append({"case": t, "peak": peak / 1000.0, "late": late,
                          "final_second_peak": final / 1000.0})
        return {"type": "conditions", "engine": p[0],
                "conditions": conditions, "tails": tails}
    if packet.type == PACKET_SETTLE:
        return {"type": "settle", "engine": p[0],
                "settle_peaks": [_u16(p, 1 + 2 * c) / 1000.0
                                 for c in range(CONDITIONS)]}
    if packet.type == PACKET_END:
        blocks = _u16(p, 1) | (_u16(p, 3) << 16)
        end = {"type": "end", "failure_mask": p[0], "blocks": blocks,
               "max_sweep_overhead": _u16(p, 5) / 1000.0,
               "max_entry_lag_frames": _u16(p, 7)}
        if len(p) >= 11:
            end["mean_sweep_overhead"] = _u16(p, 9) / 1000.0
        return end
    return {"type": f"unknown-{packet.type}"}


# ---- Pilot glitch detection -------------------------------------------------

def pilot_glitches(x: np.ndarray, sr: float, offset: int,
                   accept_until: int, threshold: float = 0.02) -> list[int]:
    """Host samples where OUT's pilot sine breaks its own recursion.

    A clean sine obeys x[n] = 2cos(w) x[n-1] - x[n-2]; a stale or partially
    written half-block does not. The residual is normalised by the local
    amplitude, so a silent OUT (the final report) finds nothing.
    """
    if len(x) < 4096:
        return []
    # Local amplitude, so a chunk that is partly silent (the final report)
    # neither dilutes the normalisation nor hides a real glitch.
    envelope = np.sqrt(2.0 * _moving_average(x * x, 512))
    reference = float(np.percentile(envelope, 95))
    # The pilot is ~0.25 FS on the ES-8; a silent OUT still carries a small DC
    # offset, which is not a sine and must not read as glitches.
    if reference < 0.05:
        return []
    present = (envelope > 0.5 * reference) & (envelope > 0.05)
    if np.count_nonzero(present) < 4096:
        return []
    # Recursion coefficient from the median of per-sample estimates
    # (x[n] + x[n-2]) / 2x[n-1], taken where x[n-1] is well away from zero:
    # exact for a clean sine whatever its frequency, and robust even when half
    # the blocks are stale (the top of the calibration ladder). Estimated per
    # half second, not per chunk: a chunk can also hold FSK data, whose tones
    # would otherwise win the median and make the sine read as wrong
    # everywhere (one long merged "event" instead of real breaks).
    mask = present[1:-1]
    middle = x[1:-1]
    usable = mask & (np.abs(middle) > 0.5 * envelope[1:-1])
    ratio = np.zeros_like(middle)
    ratio[usable] = (x[2:] + x[:-2])[usable] / (2.0 * middle[usable])
    window = max(1024, int(sr / 2))
    c_local = np.full(len(middle), np.nan)
    for start in range(0, len(middle), window):
        sel = usable[start:start + window]
        if np.count_nonzero(sel) >= 256:
            c_local[start:start + window] = float(
                np.median(ratio[start:start + window][sel]))
    valid = ~np.isnan(c_local)
    if not np.any(valid):
        return []
    c_local[~valid] = 1.0
    mask = mask & valid
    residual = np.abs(x[2:] - 2.0 * c_local * x[1:-1] + x[:-2])
    residual /= np.maximum(envelope[1:-1], 1e-6)
    hits = np.flatnonzero((residual > threshold) & mask) + 1
    events: list[int] = []
    last = -10**9
    for h in hits:
        if h >= accept_until:
            break
        if h - last > 6:
            events.append(offset + int(h))
        last = h
    return events


# ---- Decode a capture -------------------------------------------------------

def decode_capture(path: Path, main_channel: int = 0, aux_channel: int = 1,
                   chunk_seconds: float = 30.0) -> dict:
    import soundfile as sf

    info = sf.info(str(path))
    sr = float(info.samplerate)
    chunk = int(chunk_seconds * sr)
    overlap = int(1.0 * sr)
    stream: list[tuple[int, int]] = []
    glitches: list[int] = []
    position = 0
    with sf.SoundFile(str(path)) as f:
        while position < info.frames:
            f.seek(position)
            data = f.read(chunk + overlap, dtype="float64", always_2d=True)
            if not len(data):
                break
            accept = min(chunk, len(data))
            if position + len(data) >= info.frames:
                accept = len(data)
            stream.extend(demodulate_uart(
                data[:, aux_channel], sr, position, accept))
            glitches.extend(pilot_glitches(
                data[:, main_channel], sr, position, accept))
            position += chunk
    packets = parse_packets(stream, sr)
    return {"sample_rate": sr, "frames": info.frames,
            "packets": packets, "glitches": glitches}


def build_report(decoded: dict, manifest: dict | None,
                 tail_seconds: int = 4) -> dict:
    sr = decoded["sample_rate"]
    packets = decoded["packets"]
    # A late block's stale frames are its FIRST frames, so its glitch sits
    # right on the block boundary, and burst times are only good to a few
    # samples. Shifting every glitch half a block later attributes it to the
    # block (and so the state and phase) that was actually late.
    glitches = np.array(decoded["glitches"], dtype=np.int64) + BLOCK // 2
    names = (manifest or {}).get("engines", [])
    nominal = sr / MODULE_RATE  # host samples per module sample

    def count(lo: float, hi: float) -> int:
        return int(np.count_nonzero((glitches >= lo) & (glitches < hi)))

    # Timing comes from the FIRST copy of each packet (sent live, at a phase
    # boundary). Values come from the LAST copy (the repeating final report
    # carries the settled totals).
    first: dict = {}
    last: dict = {}
    conditions: dict = {}
    settles: dict = {}
    for p in packets:
        key = (p.type, p.payload[0] if p.type in (
            PACKET_ENGINE_START, PACKET_ENGINE_RESULT) else None)
        first.setdefault(key, p)
        last[key] = p
        if p.type == PACKET_CONDITIONS:
            conditions.setdefault(p.payload[0], decode_packet(p))
        if p.type == PACKET_SETTLE:
            settles.setdefault(p.payload[0], decode_packet(p))

    hello = last.get((PACKET_HELLO, None))
    report: dict = {"sample_rate": sr, "packets": len(packets),
                    "hello": decode_packet(hello) if hello else None}
    engine_count = report["hello"]["engines"] if hello else len(names)
    hellos = sorted({p.burst for p in packets if p.type == PACKET_HELLO})
    end_first = first.get((PACKET_END, None))
    report["restarts"] = len([t for t in hellos
                              if end_first and t < end_first.burst]) - 1 \
        if end_first else None

    # Engine timeline: each engine's SWITCH phase starts at its ENGINE_START
    # burst. Missing anchors are filled from the schedule.
    results = {e: decode_packet(last[(PACKET_ENGINE_RESULT, e)])
               for e in range(engine_count)
               if (PACKET_ENGINE_RESULT, e) in last}
    durations = []
    for e in range(engine_count):
        stereo = results.get(e, {}).get("stereo_capable", True)
        durations.append(engine_phases(stereo, tail_seconds))
    anchors = {e: first[(PACKET_ENGINE_START, e)].burst
               for e in range(engine_count)
               if (PACKET_ENGINE_START, e) in first}
    if end_first:
        anchors[engine_count] = end_first.burst
    starts: list = [None] * (engine_count + 1)
    for e in sorted(anchors):
        starts[e] = anchors[e]
    for e in range(1, engine_count + 1):
        if starts[e] is None and starts[e - 1] is not None:
            starts[e] = starts[e - 1] + sum(
                n for _, n in durations[e - 1]) * nominal
    for e in range(engine_count - 1, -1, -1):
        if starts[e] is None and starts[e + 1] is not None:
            starts[e] = starts[e + 1] - sum(
                n for _, n in durations[e]) * nominal

    engines = []
    for e in range(engine_count):
        entry: dict = {"engine": e,
                       "name": names[e] if e < len(names) else None}
        entry.update(results.get(e, {}))
        entry.pop("type", None)
        if e in conditions:
            entry["conditions"] = conditions[e]["conditions"]
            entry["tails"] = conditions[e]["tails"]
            if e in settles:
                for c, peak in zip(entry["conditions"],
                                   settles[e]["settle_peaks"]):
                    c["settle_peak"] = peak
        lo, hi = starts[e], starts[e + 1]
        if lo is not None and hi is not None:
            total = sum(n for _, n in durations[e])
            scale = (hi - lo) / total
            entry["stretch"] = scale / nominal
            entry["anchored"] = e in anchors and (e + 1) in anchors
            host = {}
            cursor = lo
            for name, n in durations[e]:
                host[name] = count(cursor, cursor + n * scale)
                cursor += n * scale
            entry["host_glitches"] = host
        engines.append(entry)
    report["engines"] = engines

    # Ladder: windows run backwards from the ladder packet (the end of the
    # last step); the start of the capture may have missed the HELLO.
    ladder_first = first.get((PACKET_LADDER, None))
    ladder_last = last.get((PACKET_LADDER, None))
    if ladder_first and ladder_last:
        steps = decode_packet(ladder_last)["steps"]
        # Clock ratio measured over the anchored engines (crystal tolerance
        # moves a nominal-rate boundary by tens of samples over 25 s).
        stretches = [e["stretch"] for e in engines if e.get("anchored")]
        measured = nominal * (float(np.median(stretches)) if stretches else 1.0)
        report["clock_ratio"] = measured
        step_samples = (LADDER_SETTLE_BLOCKS + LADDER_BLOCKS) * BLOCK * measured
        t_end = ladder_first.burst
        for k, step in enumerate(steps):
            lo = t_end - (LADDER_STEPS - k) * step_samples
            step["host_glitches"] = count(lo, lo + step_samples) \
                if lo >= 0 else None
        report["ladder"] = steps

    end_last = last.get((PACKET_END, None))
    if end_last:
        report["end"] = decode_packet(end_last)
    return report


def verdict(entry: dict) -> str:
    if "grid" not in entry:
        return "no result"
    late = sum(entry[k]["late"] for k in ("grid", "random", "tail"))
    late += entry.get("switch_in_late", 0)
    peak = max(entry[k]["peak"] for k in ("grid", "random", "tail"))
    if late:
        return "OVERRUN"
    if peak >= 0.9:
        return "tight"
    return "ok"


def print_report(report: dict) -> None:
    hello = report.get("hello")
    if hello:
        print(f"group {hello['group']}: {hello['engines']} engines, "
              f"firmware v{hello['version']}")
    if report.get("restarts"):
        print(f"!! module restarted {report['restarts']} time(s) during capture")
    if "end" in report:
        end = report["end"]
        print(f"sweep's own post-render work: mean "
              f"{end.get('mean_sweep_overhead', float('nan')):.3f}, max "
              f"{end['max_sweep_overhead']:.3f} of a period; max entry lag "
              f"{end['max_entry_lag_frames']} frames")
    if "ladder" in report:
        print("\ncalibration ladder (cheap engine + busy-wait to a known load)")
        print("  load   peak   late  late+sweep  host glitches")
        for st in report["ladder"]:
            host = st.get("host_glitches")
            print(f"  {st['load']:.2f}  {st['peak']:.3f}  {st['late']:5d}  "
                  f"{st['late_with_overhead']:10d}  "
                  f"{'-' if host is None else host:>13}")
    print("\nengines: peak = cost to end of render / block period; late = blocks"
          " that missed the output deadline")
    print(f"  {'#':>2} {'engine':20} {'st':2} {'grid':>6} {'rand':>6} "
          f"{'tail':>6} {'late':>6} {'+sweep':>6} {'switch':>6} "
          f"{'stretch':>7} {'host':>6}  verdict")
    for e in report["engines"]:
        if "grid" not in e:
            print(f"  {e['engine']:2d} {str(e['name'])[:20]:20} (no result)")
            continue
        late = e["grid"]["late"] + e["random"]["late"] + e["tail"]["late"]
        host = sum((e.get("host_glitches") or {}).get(k, 0)
                   for k in ("grid", "random", "tail"))
        stretch = e.get("stretch")
        print(f"  {e['engine']:2d} {str(e['name'])[:20]:20} "
              f"{'S' if e['stereo_capable'] else ' ':2} "
              f"{e['grid']['peak']:6.3f} {e['random']['peak']:6.3f} "
              f"{e['tail']['peak']:6.3f} {late:6d} "
              f"{e['late_with_overhead']:6d} {e['switch_in_late']:6d} "
              f"{'-' if stretch is None else f'{stretch:.3f}':>7} "
              f"{host:6d}  {verdict(e)}")
        if e.get("dropped_states"):
            print(f"       {e['dropped_states']} state summaries dropped "
                  f"(main loop starved: far over budget)")
        if e["grid"]["worst_state"] is not None and e["grid"]["peak"] >= 0.85:
            print(f"       worst grid state: {grid_state(e['grid']['worst_state'])}")
        bad = sorted((c for c in e.get("conditions", []) if c["late"]),
                     key=lambda c: -c["late"])
        for c in bad[:8]:
            print(f"       late {c['late']:5d}  peak {c['peak']:.3f}  "
                  f"{c['condition']}")
        if len(bad) > 8:
            print(f"       ... {len(bad) - 8} more late conditions")
        for t in e.get("tails", []):
            if t["late"] or t["final_second_peak"] > t["peak"] * 0.98 > 0.85:
                print(f"       tail {t['case']} ({tail_state(t['case'])['morph']} "
                      f"morph, {tail_state(t['case'])['output']}): late "
                      f"{t['late']}, peak {t['peak']:.3f}, final second "
                      f"{t['final_second_peak']:.3f}")
    if "end" in report:
        mask = report["end"]["failure_mask"]
        print(f"\nfailure mask {mask} "
              f"({'headroom<10% ' if mask & 1 else ''}"
              f"{'missed-deadline ' if mask & 2 else ''}"
              f"{'PASS' if not mask else ''})")
    else:
        print("\n(no END packet: sweep incomplete or not captured)")


# ---- Threshold ladder (plaits/threshold_ladder.h) ---------------------------

THRESHOLD_STEPS = 31
THRESHOLD_PASSES = 3
THRESHOLD_STEP_BLOCKS = 3 * BLOCKS_PER_SECOND
THRESHOLD_SETTLE_BLOCKS = BLOCKS_PER_SECOND // 4
THRESHOLD_START_BLOCKS = 3 * BLOCKS_PER_SECOND


def threshold_report(path: Path) -> dict:
    """Decode a threshold-ladder capture. Everything is on AUX (input 2):
    the production sub-oscillator sine during the ladder, FSK before and
    after it. The ladder's end is the first report burst; its start is the
    HELLO burst plus the start phase, which also measures the clock ratio."""
    decoded = decode_capture(path, main_channel=1, aux_channel=1)
    sr = decoded["sample_rate"]
    packets = decoded["packets"]
    hello = [p for p in packets if p.type == PACKET_HELLO]
    reports = [p for p in packets if p.type == PACKET_THRESHOLD]
    if not hello or not reports:
        raise SystemExit("capture lacks the HELLO or the report packets")
    ladder_blocks = THRESHOLD_STEPS * THRESHOLD_PASSES * THRESHOLD_STEP_BLOCKS
    t_start = hello[0].burst + THRESHOLD_START_BLOCKS * BLOCK * sr / MODULE_RATE
    t_end = reports[0].burst
    ratio = (t_end - t_start) / (ladder_blocks * BLOCK)
    glitches = np.array(decoded["glitches"], dtype=np.int64) + BLOCK // 2
    stats = {}
    for p in packets:
        if p.type != PACKET_THRESHOLD:
            continue
        pass_index = p.payload[0]
        for k in range(THRESHOLD_STEPS):
            target, peak, mean, late = struct.unpack_from(
                "<4H", p.payload, 1 + 8 * k)
            stats[(pass_index, k)] = (target / 1000.0, peak / 1000.0,
                                      mean / 1000.0, late)
    rows = []
    for pass_index in range(THRESHOLD_PASSES):
        for k in range(THRESHOLD_STEPS):
            n = pass_index * THRESHOLD_STEPS + k
            lo = t_start + n * THRESHOLD_STEP_BLOCKS * BLOCK * ratio
            measured_lo = lo + THRESHOLD_SETTLE_BLOCKS * BLOCK * ratio
            hi = lo + THRESHOLD_STEP_BLOCKS * BLOCK * ratio
            host = int(np.count_nonzero((glitches >= measured_lo) &
                                        (glitches < hi)))
            target, peak, mean, late = stats.get((pass_index, k),
                                                 (0.8 + 0.01 * k, 0, 0, 0))
            rows.append({"pass": pass_index, "target": round(target, 3),
                         "peak": peak, "mean": mean, "late": late,
                         "host_breaks": host})
    return {"clock_ratio": ratio, "rows": rows}


def print_threshold(report: dict) -> None:
    rows = report["rows"]
    print("load  | measured cost (mean/peak), DMA-late blocks, host breaks per pass")
    for k in range(THRESHOLD_STEPS):
        cells = [r for i, r in enumerate(rows) if i % THRESHOLD_STEPS == k]
        line = "  ".join(f"{r['mean']:.3f}/{r['peak']:.3f} {r['late']:5d} "
                         f"{r['host_breaks']:5d}" for r in cells)
        print(f"{cells[0]['target']:.2f}  | {line}")
    per_pass = {}
    for r in rows:
        if r["host_breaks"] and r["pass"] not in per_pass:
            per_pass[r["pass"]] = r
    for p, r in sorted(per_pass.items()):
        print(f"pass {p}: first host break at target {r['target']:.2f} "
              f"(measured mean {r['mean']:.3f}, peak {r['peak']:.3f})")


# ---- Scene check (plaits/scene_check.h) -------------------------------------

SCENE_BLOCKS = 20 * BLOCKS_PER_SECOND
SCENE_START_BLOCKS = 3 * BLOCKS_PER_SECOND
SCENE_SWITCH_BLOCKS = BLOCKS_PER_SECOND // 2


def scene_report(path: Path, scenes: list[dict], clips: Path | None) -> dict:
    """Per scene: the firmware's cost and DMA-late counts, the host's stale-
    block count on the sub-oscillator sine (mono scenes only: in stereo
    scenes AUX is the engine's right channel), and an audio clip."""
    import soundfile as sf

    decoded = decode_capture(path, main_channel=1, aux_channel=1)
    sr = decoded["sample_rate"]
    packets = decoded["packets"]
    hello = [p for p in packets if p.type == PACKET_HELLO]
    reports = [p for p in packets if p.type == PACKET_SCENE]
    if not hello or not reports:
        raise SystemExit("capture lacks the HELLO or the scene report")
    t_start = hello[0].burst + SCENE_START_BLOCKS * BLOCK * sr / MODULE_RATE
    t_end = reports[0].burst
    ratio = (t_end - t_start) / (len(scenes) * SCENE_BLOCKS * BLOCK)
    glitches = np.array(decoded["glitches"], dtype=np.int64) + BLOCK // 2
    firmware = {}
    sections = {}
    for p in packets:
        if p.type == PACKET_SECTIONS:
            count = (len(p.payload) - 1) // 4
            names = SECTION_NAMES.get(count) or [str(k) for k in range(count)]
            values = struct.unpack_from(f"<{2 * count}H", p.payload, 1)
            sections[p.payload[0]] = {
                name: {"mean": values[2 * k] / 1000.0,
                       "max": values[2 * k + 1] / 1000.0}
                for k, name in enumerate(names)}
    for p in reports:
        (scene, _engine, peak, mean, late, switch_peak, switch_late,
         blocks16) = struct.unpack_from("<BBHHHHHH", p.payload)
        firmware[scene] = {"peak": peak / 1000.0, "mean": mean / 1000.0,
                           "late": late, "switch_peak": switch_peak / 1000.0,
                           "switch_late": switch_late, "blocks": blocks16 * 16}
    if clips:
        clips.mkdir(parents=True, exist_ok=True)
    rows = []
    for k, scene in enumerate(scenes):
        lo = t_start + k * SCENE_BLOCKS * BLOCK * ratio
        measured = lo + SCENE_SWITCH_BLOCKS * BLOCK * ratio
        hi = lo + SCENE_BLOCKS * BLOCK * ratio
        row = {"scene": k, "label": scene.get("label", str(k)),
               "aux": scene["aux"], **firmware.get(k, {})}
        if k in sections:
            row["sections"] = sections[k]
        if scene["aux"] == "subosc-sine":
            row["host_breaks"] = int(np.count_nonzero(
                (glitches >= measured) & (glitches < hi)))
        if clips:
            data, _ = sf.read(str(path), start=int(measured), stop=int(hi),
                              dtype="float32", always_2d=True)
            name = f"{k:02d} {row['label'].replace(',', '').replace(' ', '_')}.wav"
            sf.write(str(clips / name), data, int(sr), subtype="PCM_24")
            row["clip"] = name
        rows.append(row)
    return {"clock_ratio": ratio, "rows": rows}


def print_scenes(report: dict) -> None:
    print(f"{'scene':28} {'aux':12} {'mean':>6} {'peak':>6} {'late':>6} "
          f"{'host':>6}  model-select peak/late")
    for r in report["rows"]:
        host = r.get("host_breaks")
        print(f"{r['label'][:28]:28} {r['aux']:12} {r.get('mean', 0):6.3f} "
              f"{r.get('peak', 0):6.3f} {r.get('late', 0):6d} "
              f"{'  n/a' if host is None else f'{host:6d}'}  "
              f"{r.get('switch_peak', 0):.3f}/{r.get('switch_late', 0)}")
    rows = [r for r in report["rows"] if r.get("sections")]
    if not rows:
        return
    names = list(rows[0]["sections"])
    print("\nwhere the time goes (mean per block, fraction of the period; "
          "max below)")
    for stat in ("mean", "max"):
        print(f"{stat:28} " + " ".join(f"{n[:7]:>7}" for n in names))
        for r in rows:
            s = r["sections"]
            print(f"{r['label'][:28]:28} " + " ".join(
                f"{s[n][stat]:7.3f}" for n in names))


# ---- ES-8 I/O ---------------------------------------------------------------

def find_device(name: str) -> int:
    import sounddevice as sd
    for i, d in enumerate(sd.query_devices()):
        if name.lower() in d["name"].lower():
            return i
    raise SystemExit(f"audio device matching {name!r} not found")


def flash(path: Path, device: str, channel: int, peak: float) -> None:
    import sounddevice as sd
    import soundfile as sf

    data, sr = sf.read(str(path), dtype="float32", always_2d=True)
    mono = data[:, 0]
    scale = peak / max(1e-9, float(np.max(np.abs(mono))))
    dev = find_device(device)
    channels = sd.query_devices(dev)["max_output_channels"]
    if channel > channels:
        raise SystemExit(f"device has {channels} outputs")
    out = np.zeros((len(mono), channel), dtype="float32")
    out[:, channel - 1] = mono * scale
    print(f"playing {path.name} ({len(mono) / sr:.1f} s @ {sr} Hz) on "
          f"output {channel}, peak {peak:.2f} FS")
    sd.play(out, samplerate=sr, device=dev, blocking=True)


def capture(path: Path, seconds: float, device: str,
            channels: tuple[int, int] = (1, 2),
            firmware: Path | None = None, out_channel: int = 3,
            peak: float = 0.3) -> None:
    """Record ES-8 inputs for `seconds` (counted after any firmware plays).

    With `firmware`, one full-duplex stream plays it on `out_channel` while
    recording, so the capture cannot miss the module's boot. (A separate
    output stream opened while an input stream is live on the same CoreAudio
    device fails with AUHAL -10863 and stalls both.)
    """
    import sounddevice as sd
    import soundfile as sf

    dev = find_device(device)
    sr = 48000
    needed = max(channels)
    playback = np.zeros(0, dtype="float32")
    if firmware is not None:
        data, fw_rate = sf.read(str(firmware), dtype="float32", always_2d=True)
        if fw_rate != sr:
            raise SystemExit(f"firmware WAV is {fw_rate} Hz, expected {sr}")
        playback = data[:, 0] * (peak / max(1e-9, float(np.max(np.abs(data)))))
        print(f"flashing {firmware.name} ({len(playback) / sr:.1f} s) on output "
              f"{out_channel}, peak {peak:.2f} FS", flush=True)
    import queue
    import threading

    cursor = [0]
    playback_flags = []
    capture_flags = []
    pending: "queue.Queue" = queue.Queue()
    picks = [channels[0] - 1, channels[1] - 1]

    def callback(indata, outdata, frames, _time, status):
        # Real-time path: copy and hand off only. Disk I/O here causes output
        # dropouts, and a dropout mid-transfer fails the bootloader's CRC.
        if status:
            (playback_flags if cursor[0] < len(playback)
             else capture_flags).append(str(status))
        pending.put(indata[:, picks].copy())
        outdata.fill(0)
        start = cursor[0]
        chunk = playback[start:start + frames]
        outdata[:len(chunk), out_channel - 1] = chunk
        cursor[0] = start + frames

    with sf.SoundFile(str(path), "w", samplerate=sr, channels=2,
                      subtype="PCM_24") as f:
        stop = threading.Event()

        def writer():
            while not stop.is_set() or not pending.empty():
                try:
                    f.write(pending.get(timeout=0.2))
                except queue.Empty:
                    pass

        thread = threading.Thread(target=writer, daemon=True)
        thread.start()
        with sd.Stream(device=(dev, dev), channels=(needed, out_channel),
                       samplerate=sr, dtype="float32", callback=callback,
                       blocksize=1024, latency="high"):
            while cursor[0] < len(playback):
                time.sleep(0.5)
            if firmware is not None:
                if playback_flags:
                    print(f"!! {len(playback_flags)} stream dropouts during the "
                          f"flash (first: {playback_flags[0]}); the transfer "
                          f"is probably corrupt", flush=True)
                else:
                    print("firmware sent cleanly (no stream dropouts); "
                          "capturing the sweep", flush=True)
            end = time.time() + seconds
            while time.time() < end:
                time.sleep(min(5.0, max(0.0, end - time.time())))
        stop.set()
        thread.join()
    if capture_flags:
        print(f"capture stream flags: {len(capture_flags)} "
              f"(first: {capture_flags[0]})", flush=True)


def expected_seconds(manifest: dict, tail_seconds: int) -> float:
    total = START_BLOCKS * BLOCK + LADDER_STEPS * (
        LADDER_SETTLE_BLOCKS + LADDER_BLOCKS) * BLOCK
    # Upper bound: whether an engine has a stereo path is only known on the
    # module, so budget every engine for the longer stereo schedule.
    for _ in manifest["engines"]:
        total += sum(n for _, n in engine_phases(True, tail_seconds))
    return total / MODULE_RATE


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("flash")
    p.add_argument("firmware", type=Path)
    p.add_argument("--device", default="ES-8")
    p.add_argument("--channel", type=int, default=3)
    p.add_argument("--peak", type=float, default=0.3)

    p = sub.add_parser("capture")
    p.add_argument("output", type=Path)
    p.add_argument("--seconds", type=float, required=True)
    p.add_argument("--device", default="ES-8")

    p = sub.add_parser("decode")
    p.add_argument("capture", type=Path)
    p.add_argument("--manifest", type=Path)
    p.add_argument("--json", type=Path)
    p.add_argument("--tail-seconds", type=int, default=4)

    p = sub.add_parser("scenes")
    p.add_argument("capture", type=Path)
    p.add_argument("--scenes", type=Path, required=True)
    p.add_argument("--clips", type=Path)
    p.add_argument("--json", type=Path)

    p = sub.add_parser("scenes-run")
    p.add_argument("--flash", type=Path, required=True)
    p.add_argument("--scenes", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--device", default="ES-8")
    p.add_argument("--channel", type=int, default=3)
    p.add_argument("--peak", type=float, default=0.3)

    p = sub.add_parser("threshold")
    p.add_argument("capture", type=Path)
    p.add_argument("--json", type=Path)

    p = sub.add_parser("threshold-run")
    p.add_argument("--flash", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--device", default="ES-8")
    p.add_argument("--channel", type=int, default=3)
    p.add_argument("--peak", type=float, default=0.3)

    p = sub.add_parser("run")
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--device", default="ES-8")
    p.add_argument("--tail-seconds", type=int, default=4)
    p.add_argument("--margin", type=float, default=45.0)
    p.add_argument("--flash", type=Path,
                   help="play this firmware WAV (output --channel) after the "
                        "capture starts; the module must be in its bootloader")
    p.add_argument("--channel", type=int, default=3)
    p.add_argument("--peak", type=float, default=0.3)

    args = parser.parse_args()
    if args.command == "flash":
        flash(args.firmware, args.device, args.channel, args.peak)
    elif args.command in ("scenes", "scenes-run"):
        scenes = json.loads(args.scenes.read_text())
        if args.command == "scenes-run":
            args.out.mkdir(parents=True, exist_ok=True)
            seconds = ((SCENE_START_BLOCKS + len(scenes) * SCENE_BLOCKS) *
                       BLOCK / MODULE_RATE * 1.1 + 15.0)
            wav = args.out / "capture.wav"
            print(f"capturing {seconds / 60:.1f} min to {wav}", flush=True)
            capture(wav, seconds, args.device, firmware=args.flash,
                    out_channel=args.channel, peak=args.peak)
            capture_path, clips = wav, args.out / "clips"
            json_path = args.out / "scenes.json"
        else:
            capture_path, clips, json_path = args.capture, args.clips, args.json
        report = scene_report(capture_path, scenes, clips)
        print_scenes(report)
        if json_path:
            json_path.write_text(json.dumps(report, indent=1) + "\n")
    elif args.command in ("threshold", "threshold-run"):
        if args.command == "threshold-run":
            args.out.mkdir(parents=True, exist_ok=True)
            seconds = ((THRESHOLD_START_BLOCKS + THRESHOLD_STEPS *
                        THRESHOLD_PASSES * THRESHOLD_STEP_BLOCKS) * BLOCK /
                       MODULE_RATE * 1.1 + 20.0)
            wav = args.out / "capture.wav"
            print(f"capturing {seconds / 60:.1f} min to {wav}", flush=True)
            capture(wav, seconds, args.device, firmware=args.flash,
                    out_channel=args.channel, peak=args.peak)
            capture_path, json_path = wav, args.out / "threshold.json"
        else:
            capture_path, json_path = args.capture, args.json
        report = threshold_report(capture_path)
        print_threshold(report)
        if json_path:
            json_path.write_text(json.dumps(report, indent=1) + "\n")
    elif args.command == "capture":
        capture(args.output, args.seconds, args.device)
    elif args.command in ("decode", "run"):
        manifest = (json.loads(args.manifest.read_text())
                    if args.manifest else None)
        if args.command == "run":
            args.out.mkdir(parents=True, exist_ok=True)
            # Overrunning engines lose callbacks and run slower than their
            # schedule (up to ~20% in practice), so budget for it.
            seconds = (expected_seconds(manifest, args.tail_seconds) * 1.2
                       + args.margin)
            wav = args.out / "capture.wav"
            print(f"capturing {seconds / 60:.1f} min to {wav}", flush=True)
            capture(wav, seconds, args.device, firmware=args.flash,
                    out_channel=args.channel, peak=args.peak)
            capture_path = wav
            json_path = args.out / "report.json"
        else:
            capture_path = args.capture
            json_path = args.json
        decoded = decode_capture(capture_path)
        report = build_report(decoded, manifest, args.tail_seconds)
        print_report(report)
        if json_path:
            json_path.write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
