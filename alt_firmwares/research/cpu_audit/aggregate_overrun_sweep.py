"""Merge the eight on-module overrun-sweep runs of 2026-09-24 into one
verdict per engine (see plaits/overrun_sweep.h, plaits/tools/
overrun_sweep_host.py). Mono comes from the schema-2 groups (1-5), whose
recipe compiled every per-engine stereo path out; stereo comes from the
stereo-compiled groups (6-8) or, for engines whose stereo is always on, from
groups 1-5. Peaks are cost from interrupt entry to end of render, as a
fraction of the 12-sample block period. The line is 0.985: where the
threshold ladder (plaits/threshold_ladder.h) measured production firmware
starting to play stale data (it was 0.90 until that measurement). Settle peaks
exist only where the firmware carried them (groups 4-8): 0 means not
measured, not "no spike".
"""
import json, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'plaits_lab_builder'))
import container_server as cs
# Measured on production's own signal path (threshold_ladder_2026-09-24.json):
# no stale output up to a bracket cost of 0.976, first rare breaks at 0.986.
RED=0.985
runs={'mono':['run-g1v3','run-g2','run-g3','run-g4'],'extra':['run-g5'],'stereo':['run-g6','run-g7','run-g8']}
def load(run):
    p = os.path.join(HERE, f'overrun_sweep_2026-09-24_{run[4:]}.json')
    return json.load(open(p)) if os.path.exists(p) else None
def cond_max(c, pred):
    v=[x['peak'] for x in c if pred(x['condition'])]; return max(v) if v else None
E={}
def upd(name, key, val, mode=max):
    if val is None: return
    d=E.setdefault(name,{})
    d[key]=val if key not in d else mode(d[key],val)
for kind,rs in runs.items():
    for run in rs:
        r=load(run)
        if not r: continue
        for e in r['engines']:
            if 'grid' not in e: continue
            n=e['name']; c=e.get('conditions') or []
            always_stereo = n not in cs.STEREO_MACROS
            if c:
                un=cond_max(c,lambda k:k.startswith('unpatched/regular'))
                mono=cond_max(c,lambda k:'/stereo/' not in k)
                ste=cond_max(c,lambda k:'/stereo/' in k) if e['stereo_capable'] else None
                trig=cond_max(c,lambda k:k.startswith(('triggered/regular','gated/regular')))
                sp=max((x.get('settle_peak',0) for x in c),default=0)
            else:
                un=None; mono=max(e['grid']['peak'],e['random']['peak']); ste=None; trig=None; sp=0
            if kind in ('mono','extra'):
                upd(n,'mono',mono); upd(n,'unpatched',un); upd(n,'trig',trig)
                if always_stereo and ste is not None: upd(n,'stereo',ste)
            if kind=='stereo':
                if ste is not None: upd(n,'stereo',ste)
                upd(n,'mono_in_stereo_build',mono)
            if sp: upd(n,'settle',sp)
            upd(n,'switch',e.get('switch_in_peak'))
            upd(n,'tail',e['tail']['peak'])
            upd(n,'stereo_path', bool(e['stereo_capable']) if (kind=='stereo' or always_stereo) else None, lambda a,b:a or b)
rows=[]
for n,d in E.items():
    mono=max(v for v in (d.get('mono'),d.get('mono_in_stereo_build')) if v is not None)
    ste=d.get('stereo')
    settle=d.get('settle',0)
    issues=[]
    if mono>=RED: issues.append(f"mono {mono:.2f}")
    if ste is not None and ste>=RED: issues.append(f"stereo {ste:.2f}")
    if settle>=1.5: issues.append(f"param-jump spike {settle:.1f}")
    if d.get('switch',0)>=1.0: issues.append(f"model-select {d['switch']:.1f}")
    rows.append((n,mono,ste,settle,d.get('switch',0),d.get('unpatched'),d.get('trig'),issues))
rows.sort(key=lambda r:-max(r[1],r[2] or 0))
json.dump([dict(engine=r[0],mono=r[1],stereo=r[2],settle=r[3],switch=r[4],unpatched=r[5],trig=r[6],issues=r[7]) for r in rows],open(os.path.join(HERE, 'overrun_sweep_2026-09-24.json'), 'w'),indent=1)
print(f"{len(rows)} engines; over red line {RED} (mono or stereo): {sum(1 for r in rows if max(r[1],r[2] or 0)>=RED)}")
for r in rows:
    print(f"{r[0]:24} mono {r[1]:.3f} stereo {'  -  ' if r[2] is None else f'{r[2]:.3f}'} settle {r[3]:6.2f} switch {r[4]:5.2f}  {'; '.join(r[7])}")
