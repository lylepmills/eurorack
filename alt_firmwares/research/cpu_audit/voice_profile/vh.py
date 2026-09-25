#!/usr/bin/env python3
"""Whole-Voice instruction counts: link voice_harness.cc against a firmware
build's objects, run A/B block counts in QEMU, attribute the difference.

  vh.py <export dir> --engine N [--h --t --m --note --aux 0|1|2 --trig 0|1]
        [--profile] [--tag name]          # fork build (export_overrun_sweep.py)
  vh.py <upstream tree> --stock --engine N  # upstream Plaits built in place
  lines.py <tag> /b plaits::Voice::Render,plaits::ChannelPostProcessor

The objects are the firmware's own (same flags, same -Os/-O2 split), so the
counts are for exactly the code that ran on the module. Counts are
instructions, not cycles: on the module this glue runs at ~2.5-2.8 cycles per
instruction (flash wait states, branches).
"""
import argparse, subprocess, sys, shlex
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]
QEMU = REPO / "alt_firmwares/plaits_lab_sdk/qemu"
sys.path.insert(0, str(QEMU))
import estimate  # run_qemu, report_profile, ARCH_FLAGS
import os, re
HERE = Path(__file__).resolve().parent
WORK = Path(os.environ.get('VOICE_PROFILE_WORK', '/tmp/voice_profile'))

ap = argparse.ArgumentParser()
ap.add_argument("build", type=Path, help="export dir (with build/plaits/*.o)")
ap.add_argument("--engine", type=int, default=0)
ap.add_argument("--note", type=float, default=60)
ap.add_argument("--h", type=float, default=.5); ap.add_argument("--t", type=float, default=.5)
ap.add_argument("--m", type=float, default=.5)
ap.add_argument("--aux", type=int, default=0)
ap.add_argument("--trig", type=int, default=0)
ap.add_argument("--profile", action="store_true")
ap.add_argument("--tag", default="run")
ap.add_argument("--stock", action="store_true", help="build is the upstream tree (objects in build/plaits)")
a = ap.parse_args()
out = WORK / a.tag; out.mkdir(parents=True, exist_ok=True)
import glob, os
if a.stock:
    names = sorted(os.path.basename(f) for f in glob.glob(str(a.build / "build/plaits/*.o")))
    skip_re = __import__("re").compile(r"^(plaits|ui|settings|user_data_receiver|audio_dac|cv_adc|debug_port|firmware_update_adc|leds|pots_adc|switches|bootloader_utils|system_clock|system_stm32f37x|startup_stm32f37x|stm32f37x_.*)\.o$")
    objs = [f"/b/build/plaits/{n}" for n in names if not skip_re.match(n)]
    keep = ["-I/b", "-DVH_STOCK", "-DGCC_ARMCM4", "-DSTM32F37X", "-DARM_MATH_CM4", "-D__FPU_PRESENT",
            "-DAPPLICATION", "-DF_CPU=72000000L"]
else:
    # Everything the firmware links except the application, UI, drivers and
    # the STM peripheral library: Voice, every engine, resources, stmlib.
    fw_skip = re.compile(r"^(plaits|ui|settings|user_data_receiver|audio_dac|audio_rate_timer|cv_adc|debug_port|firmware_update_adc|leds|pots_adc|switches|sync_input|bootloader_utils|system_clock|system_stm32f37x|startup_stm32f37x|stm32f37x_.*)\.o$")
    names = sorted(os.path.basename(f) for f in glob.glob(str(a.build / "build/plaits/*.o")))
    objs = [f"/b/build/plaits/{n}" for n in names if not fw_skip.match(n)]
    log = (a.build / "build.log").read_text()
    line = [l for l in log.splitlines() if "plaits/dsp/voice.cc -o" in l][0].split()
    keep = []
    for i, t in enumerate(line):
        if t.startswith(("-D", "-I", "-std")) or t in ("-include",) or (i and line[i-1] == "-include"):
            keep.append(t.replace(str(a.build), "/b"))
flags = [*estimate.ARCH_FLAGS, *keep]
cmds = []
for label, n in (("a", 400), ("b", 1400)):
    d = [f"-DVH_BLOCKS={n}", f"-DVH_ENGINE={a.engine}", f"-DVH_NOTE={float(a.note)!r}f", f"-DVH_H={float(a.h)!r}f",
         f"-DVH_T={float(a.t)!r}f", f"-DVH_M={float(a.m)!r}f", f"-DVH_AUX={a.aux}", f"-DVH_TRIG={a.trig}"]
    cmds.append(" ".join(shlex.quote(c) for c in ["/usr/local/arm-4.8.3/bin/arm-none-eabi-g++", *flags, *d,
        "-c", "/vh/voice_harness.cc", "-o", f"/out/h_{label}.o"]))
    cmds.append(" ".join(shlex.quote(c) for c in ["/usr/local/arm-4.8.3/bin/arm-none-eabi-g++", *estimate.ARCH_FLAGS,
        f"/out/h_{label}.o", "/out/startup.o", *objs, "-T", "/qemu/mps2.ld", "-nostartfiles",
        "-Wl,--gc-sections", "-o", f"/out/h_{label}.elf"]))
cmds.insert(0, " ".join(shlex.quote(c) for c in ["/usr/local/arm-4.8.3/bin/arm-none-eabi-gcc", *estimate.ARCH_FLAGS,
    "-c", "/qemu/startup.c", "-o", "/out/startup.o"]))
cmds.append("/usr/local/arm-4.8.3/bin/arm-none-eabi-nm -C -S -n /out/h_a.elf > /out/symbols.txt")
(out / "build.sh").write_text("#!/bin/sh\nset -e\n" + "\n".join(cmds) + "\n")
r = subprocess.run(["docker", "run", "--rm", "--platform", "linux/amd64", "--entrypoint", "sh",
    "-v", f"{(a.build if a.stock else REPO)}:/workspace:ro", "-v", f"{a.build}:/b:ro", "-v", f"{QEMU}:/qemu:ro",
    "-v", f"{HERE}:/vh:ro", "-v", f"{out}:/out", "-w", "/workspace",
    "plaits-lab-builder:local", "/out/build.sh"], capture_output=True, text=True)
if r.returncode:
    sys.exit(r.stderr[-4000:] + r.stdout[-2000:])
plugin = QEMU / "cycles_plugin.so"
ca, pa = estimate.run_qemu(out / "h_a.elf", plugin)
cb, pb = estimate.run_qemu(out / "h_b.elf", plugin)
per_block = (cb[0] - ca[0]) / 1000.0
print(f"{a.tag}: {per_block:.0f} instructions/block ({per_block / 12:.1f}/sample)")
if a.profile:
    estimate.report_profile(out / "symbols.txt", pa, pb, 1000 * 12)
