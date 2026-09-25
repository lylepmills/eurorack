#!/usr/bin/env python3
"""Per-source-line instruction counts per block for chosen functions."""
import sys, subprocess, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "plaits_lab_sdk/qemu"))
import estimate, os
WORK = Path(os.environ.get('VOICE_PROFILE_WORK', '/tmp/voice_profile'))
tag, src_root = sys.argv[1], sys.argv[2]
funcs = sys.argv[3].split(",")
out = WORK / tag
plugin = Path(estimate.__file__).parent / "cycles_plugin.so"
_, pa = estimate.run_qemu(out / "h_a.elf", plugin)
_, pb = estimate.run_qemu(out / "h_b.elf", plugin)
syms = []
for line in (out / "symbols.txt").read_text().splitlines():
    p = line.split(None, 3)
    if len(p) == 4 and p[2].lower() in "tw":
        syms.append((int(p[0], 16), int(p[1], 16), p[3]))
ranges = [(s, s + n, name) for s, n, name in syms if any(name.startswith(f) for f in funcs)]
addrs = {}
for a in set(pa) | set(pb):
    d = pb.get(a, (0, 0))[0] - pa.get(a, (0, 0))[0]
    if d > 0 and any(lo <= a < hi for lo, hi, _ in ranges):
        addrs[a] = d / 1000.0
lst = "\n".join(hex(a) for a in sorted(addrs))
(out / "addrs.txt").write_text(lst + "\n")
r = subprocess.run(["docker", "run", "--rm", "--platform", "linux/amd64", "--entrypoint", "sh",
    "-v", f"{out}:/out", "plaits-lab-builder:local", "-c",
    "/usr/local/arm-4.8.3/bin/arm-none-eabi-addr2line -e /out/h_b.elf < /out/addrs.txt"],
    capture_output=True, text=True)
per_line = collections.Counter()
for a, loc in zip(sorted(addrs), r.stdout.splitlines()):
    loc = loc.replace(src_root, "").replace("/workspace/", "")
    per_line[loc] += addrs[a]
total = sum(per_line.values())
print(f"{tag}: {total:.0f} instructions/block in {funcs}")
for loc, n in per_line.most_common(int(sys.argv[4]) if len(sys.argv) > 4 else 45):
    print(f"{n:7.1f}  {loc}")
