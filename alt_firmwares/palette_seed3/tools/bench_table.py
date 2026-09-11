#!/usr/bin/env python3
"""Turn bench CSVs from the Seed3 into the per-engine table for the plan.

    python3 tools/bench_table.py qspi.csv [sram.csv] [notables.csv] > table.md

Each CSV is what tools/capture_bench.sh recorded from one image. Columns are
what src/main.cc prints:
  variant,set,engine,voices,stereo,render_cyc_per_voice_block,render_max_cyc,
  cb_mean_cyc,cb_max_cyc,cb_mean_pct_x100,cb_max_pct_x100,callbacks
Percentages are of the 1 ms callback budget at the printed sysclk (480 MHz:
480,000 cycles per 48-frame callback). The table shows, per engine, the
one-voice and four-voice mean callback load and the worst callback, for each
variant present, plus the QSPI-minus-SRAM delta where both have the engine.
"""
import csv, sys
from collections import OrderedDict


def load(path):
    rows = OrderedDict()
    meta = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                meta.append(line)
                continue
            p = line.split(',')
            if len(p) < 12:
                continue
            key = (p[2], int(p[3]))
            rows[key] = dict(variant=p[0], set=p[1], engine=p[2], voices=int(p[3]), stereo=int(p[4]),
                             render=int(p[5]), render_max=int(p[6]), cb_mean=int(p[7]), cb_max=int(p[8]),
                             mean_pct=int(p[9]) / 100.0, max_pct=int(p[10]) / 100.0, n=int(p[11]))
    return meta, rows


def main():
    files = sys.argv[1:]
    if not files:
        print(__doc__); sys.exit(2)
    data = [load(f) for f in files]
    variants = []
    for meta, rows in data:
        v = next(iter(rows.values()))['variant'] if rows else '?'
        variants.append(v)
        for m in meta:
            print(f'<!-- {v}: {m} -->')
    engines = []
    for _, rows in data:
        for (e, n) in rows:
            if e not in engines:
                engines.append(e)
    head = ['engine', 'stereo']
    for v in variants:
        head += [f'{v} 1v mean %', f'{v} 1v max %', f'{v} 4v mean %', f'{v} 4v max %', f'{v} render cyc/blk']
    if len(variants) >= 2:
        head += [f'{variants[0]}−{variants[1]} 1v Δ%', f'{variants[0]}−{variants[1]} 4v Δ%']
    print('| ' + ' | '.join(head) + ' |')
    print('|' + '---|' * len(head))
    for e in engines:
        cells = [e]
        st = None
        vals = []
        for _, rows in data:
            r1 = rows.get((e, 1)); r4 = rows.get((e, 4))
            if r1 and st is None:
                st = 'yes' if r1['stereo'] else 'no'
            if r1 or r4:
                cells_v = [f"{r1['mean_pct']:.1f}" if r1 else '', f"{r1['max_pct']:.1f}" if r1 else '',
                           f"{r4['mean_pct']:.1f}" if r4 else '', f"{r4['max_pct']:.1f}" if r4 else '',
                           f"{r1['render']}" if r1 else '']
                vals.append((r1, r4))
            else:
                cells_v = [''] * 5
                vals.append((None, None))
            cells.extend(cells_v)
        cells.insert(1, st or '')
        if len(variants) >= 2:
            (a1, a4), (b1, b4) = vals[0], vals[1]
            cells.append(f"{a1['mean_pct'] - b1['mean_pct']:+.1f}" if a1 and b1 else '')
            cells.append(f"{a4['mean_pct'] - b4['mean_pct']:+.1f}" if a4 and b4 else '')
        print('| ' + ' | '.join(cells) + ' |')


if __name__ == '__main__':
    main()
