#!/usr/bin/env python3
"""Compare a ported engine's render against its Braids reference.

Wave 1 of the Braids port quoted A/B figures ("within 0.05 dB AC RMS, +5
cents", "0.19 / 0.45 / 0.67 dB") from a harness that was never committed, so
none of them can be reproduced or extended. This is that comparison, committed,
and it reports the same three things wave 1 reported:

  * AC RMS, in dB, per file and as a delta. DC is removed first, because
    several Braids models carry a large DC offset that the port's DC blocker
    removes by design -- comparing raw RMS would charge that to the port.
  * f0 by autocorrelation, as a cents delta. This is the metric that catches
    the class of bug R5 exists for: a rate constant transplanted from the wrong
    side of the 96/48 kHz line shows up as a pitch error, not a timbre error.
    Fluted was dropped on exactly this measurement.
  * An energy-weighted spectral difference in dB, plus a per-octave-band
    breakdown so a failure localizes instead of just being a single number.

Both files must share a sample rate. Render the reference with
`braids/test/render_braids_model --rate 48000` when the port runs at 48 kHz;
comparing a 48 kHz port against a 96 kHz reference charges everything above
24 kHz to the port and is meaningless.

No third-party dependencies, matching the rest of the SDK.

Usage:
    python3 ab_compare.py reference.wav port.wav [--bands] [--json]
"""

import argparse
import cmath
import json
import math
import struct
import sys
import wave


def read_wav(path):
    """Return (samples as floats in -1..1, sample_rate). Mono; 16- or 32-bit."""
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        frames = handle.getnframes()
        raw = handle.readframes(frames)

    if width == 2:
        values = struct.unpack("<%dh" % (len(raw) // 2), raw)
        scale = 1.0 / 32768.0
    elif width == 4:
        values = struct.unpack("<%di" % (len(raw) // 4), raw)
        scale = 1.0 / 2147483648.0
    else:
        raise SystemExit("%s: unsupported sample width %d bytes" % (path, width))

    if channels > 1:
        values = values[0::channels]
    return [v * scale for v in values], rate


def remove_dc(samples):
    if not samples:
        return samples
    mean = sum(samples) / len(samples)
    return [s - mean for s in samples]


def rms_db(samples):
    if not samples:
        return float("-inf")
    total = sum(s * s for s in samples)
    if total <= 0.0:
        return float("-inf")
    return 20.0 * math.log10(math.sqrt(total / len(samples)))


def autocorrelation_f0(samples, rate, f_min=20.0, f_max=2000.0):
    """f0 in Hz, or None when the signal has no usable periodicity.

    Runs on a mid-file window so an attack transient does not dominate, and
    rejects the result when the best correlation peak is weak -- noise models
    have no f0 and should report one rather than a spurious number.
    """
    window = min(len(samples), int(rate * 0.25))
    if window < 1024:
        return None
    start = max(0, (len(samples) - window) // 2)
    segment = remove_dc(samples[start:start + window])

    energy = sum(s * s for s in segment)
    if energy <= 1e-12:
        return None

    min_lag = max(2, int(rate / f_max))
    max_lag = min(len(segment) - 1, int(rate / f_min))
    if max_lag <= min_lag:
        return None

    best_lag = None
    best_value = 0.0
    previous = 0.0
    rising = False
    for lag in range(min_lag, max_lag):
        total = 0.0
        for i in range(0, len(segment) - lag, 2):  # stride 2: 2x faster, same peak
            total += segment[i] * segment[i + lag]
        if total > previous:
            rising = True
        elif rising:
            # Local maximum just passed; keep the strongest one.
            if previous > best_value:
                best_value = previous
                best_lag = lag - 1
            rising = False
        previous = total

    if best_lag is None:
        return None
    # Correlation at the peak, normalized against zero-lag energy.
    if best_value / (energy / 2.0) < 0.25:
        return None

    # Parabolic interpolation across the peak. An integer lag quantizes badly
    # at musical pitches -- at 110 Hz the lag is 436.4 samples, so whole-sample
    # lags land 4 cents apart, and every engine measured here reported the same
    # +4.0 cents. A systematic bias that size would mask a real one, and pitch
    # is the metric the R5 rate-constant bugs show up in.
    def correlate(lag):
        return sum(segment[i] * segment[i + lag]
                   for i in range(0, len(segment) - lag, 2))

    lag = best_lag
    if min_lag < lag < max_lag - 1:
        before, centre, after = correlate(lag - 1), correlate(lag), correlate(lag + 1)
        denominator = 2.0 * (2.0 * centre - before - after)
        if denominator != 0.0:
            offset = (after - before) / denominator
            if -1.0 < offset < 1.0:
                return rate / (float(lag) + offset)
    return rate / float(lag)


def cents(f_reference, f_port):
    if not f_reference or not f_port:
        return None
    return 1200.0 * math.log2(f_port / f_reference)


def fft(values):
    """Iterative radix-2 FFT. len(values) must be a power of two."""
    n = len(values)
    if n & (n - 1):
        raise ValueError("FFT length must be a power of two")
    data = list(values)

    # Bit-reversal permutation.
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            data[i], data[j] = data[j], data[i]

    length = 2
    while length <= n:
        angle = -2.0 * math.pi / length
        step = cmath.exp(complex(0.0, angle))
        for start in range(0, n, length):
            w = complex(1.0, 0.0)
            half = length // 2
            for k in range(start, start + half):
                even = data[k]
                odd = data[k + half] * w
                data[k] = even + odd
                data[k + half] = even - odd
                w *= step
        length <<= 1
    return data


def average_spectrum(samples, size=2048):
    """Mean magnitude spectrum over Hann-windowed 50%-overlap frames."""
    if len(samples) < size:
        return None
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (size - 1))
              for i in range(size)]
    bins = size // 2
    accumulator = [0.0] * bins
    frames = 0
    hop = size // 2
    for start in range(0, len(samples) - size + 1, hop):
        frame = [samples[start + i] * window[i] for i in range(size)]
        spectrum = fft([complex(v, 0.0) for v in frame])
        for b in range(bins):
            accumulator[b] += abs(spectrum[b])
        frames += 1
    if not frames:
        return None
    return [value / frames for value in accumulator]


def match_level(reference, port):
    """Scale `port` to the reference's AC RMS. Returns (scaled, gain_db).

    The spectral metric is meant to measure TIMBRE. A flat level offset — which
    is what a catalog out_gain is — would otherwise show up as a uniform
    difference in every bin and swamp the shape comparison. Level is reported
    separately, by the AC RMS figure, where it belongs.
    """
    energy_a = sum(s * s for s in reference)
    energy_b = sum(s * s for s in port)
    if energy_a <= 0.0 or energy_b <= 0.0:
        return port, 0.0
    gain = math.sqrt(energy_a / energy_b)
    return [s * gain for s in port], 20.0 * math.log10(gain)


def spectral_difference(reference, port, rate, size=2048, normalize=True):
    """Energy-weighted mean |dB| difference, and a per-octave-band breakdown."""
    if normalize:
        port, _ = match_level(reference, port)
    spectrum_a = average_spectrum(reference, size)
    spectrum_b = average_spectrum(port, size)
    if spectrum_a is None or spectrum_b is None:
        return None, []

    floor = 1e-9
    total_weight = sum(spectrum_a)
    if total_weight <= 0.0:
        return None, []

    edges = [20.0]
    while edges[-1] * 2.0 < rate / 2.0:
        edges.append(edges[-1] * 2.0)
    edges.append(rate / 2.0)

    bin_hz = rate / float(size)
    bands = []
    for low, high in zip(edges, edges[1:]):
        first = max(1, int(low / bin_hz))
        last = min(len(spectrum_a), int(high / bin_hz) + 1)
        if last <= first:
            continue
        energy_a = sum(spectrum_a[first:last])
        energy_b = sum(spectrum_b[first:last])
        if energy_a <= floor and energy_b <= floor:
            continue
        bands.append({
            "low_hz": round(low, 1),
            "high_hz": round(high, 1),
            "delta_db": round(20.0 * math.log10(
                (energy_b + floor) / (energy_a + floor)), 2),
            "share": round(energy_a / total_weight, 4),
        })

    # The headline number is the energy-weighted mean of the per-BAND deltas,
    # not of the per-bin ones. A per-bin mean is dominated by bins sitting in a
    # spectral null, where the two renders disagree by tens of dB on energy too
    # small to hear: csaw reads 3.94 dB per-bin against 0.35 dB per-band, while
    # its bands are flat to 0.06 dB across 80% of the energy. The band figure is
    # the one that tracks what the engine actually sounds like.
    overall = sum(abs(band["delta_db"]) * band["share"] for band in bands)
    weight = sum(band["share"] for band in bands)
    if weight > 0.0:
        overall /= weight
    return overall, bands


def main():
    parser = argparse.ArgumentParser(
        description="Compare a ported engine against its Braids reference.")
    parser.add_argument("reference", help="Braids reference WAV")
    parser.add_argument("port", help="ported engine WAV")
    parser.add_argument("--bands", action="store_true",
                        help="print the per-octave-band breakdown")
    parser.add_argument("--json", action="store_true",
                        help="emit machine-readable results")
    arguments = parser.parse_args()

    reference, rate_a = read_wav(arguments.reference)
    port, rate_b = read_wav(arguments.port)

    if rate_a != rate_b:
        raise SystemExit(
            "sample rates differ (%d vs %d). Render the reference with "
            "--rate %d." % (rate_a, rate_b, rate_b))

    length = min(len(reference), len(port))
    if length == 0:
        raise SystemExit("one of the files is empty")
    reference = reference[:length]
    port = port[:length]

    reference_ac = remove_dc(reference)
    port_ac = remove_dc(port)

    rms_a = rms_db(reference_ac)
    rms_b = rms_db(port_ac)

    f0_a = autocorrelation_f0(reference, rate_a)
    f0_b = autocorrelation_f0(port, rate_b)
    cents_delta = cents(f0_a, f0_b)

    overall, bands = spectral_difference(reference_ac, port_ac, rate_a)

    results = {
        "sample_rate": rate_a,
        "seconds": round(length / float(rate_a), 3),
        "reference_ac_rms_db": round(rms_a, 3),
        "port_ac_rms_db": round(rms_b, 3),
        "ac_rms_delta_db": round(rms_b - rms_a, 3),
        "reference_f0_hz": round(f0_a, 3) if f0_a else None,
        "port_f0_hz": round(f0_b, 3) if f0_b else None,
        "pitch_delta_cents": round(cents_delta, 2) if cents_delta is not None
                             else None,
        "spectral_delta_db": round(overall, 3) if overall is not None else None,
        "bands": bands,
    }

    if arguments.json:
        json.dump(results, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return

    print("%.3f s at %d Hz" % (results["seconds"], rate_a))
    print("  AC RMS      reference %7.2f dB   port %7.2f dB   delta %+6.2f dB"
          % (rms_a, rms_b, rms_b - rms_a))
    if cents_delta is None:
        print("  pitch       no usable f0 (aperiodic on one or both sides)")
    else:
        print("  pitch       reference %7.2f Hz   port %7.2f Hz   delta %+6.1f "
              "cents" % (f0_a, f0_b, cents_delta))
    if overall is not None:
        print("  spectrum    energy-weighted mean |delta| %6.2f dB" % overall)

    if arguments.bands and bands:
        print("  per band:")
        for band in bands:
            print("    %8.0f - %8.0f Hz  %+6.2f dB  (%4.1f%% of energy)"
                  % (band["low_hz"], band["high_hz"], band["delta_db"],
                     band["share"] * 100.0))


if __name__ == "__main__":
    main()
