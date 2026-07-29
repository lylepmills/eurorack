#!/usr/bin/env python3
"""Leave-one-out flash sweep, run INSIDE the plaits-lab-builder container.

Method is flash-budget.ts's: build stock-24, then rebuild it with one slot
replaced, and read the text+data delta. Measuring in the FULL stock context is
the point -- code shared with the other engines is already paid for by them,
so the delta is the engine's true full-palette marginal.

Speech is the slot that gets swapped, matching how the existing Lab engines
were measured. The baseline replaces Speech with a DUPLICATE of an engine
already present (duplicate slots collapse to one instance), so the baseline
carries 23 distinct engines and each measurement carries those 23 plus one.
"""
import hashlib
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
import container_server as cs

BASE = json.load(open('/work/alt_firmwares/plaits_lab_builder/default_recipe.json'))
# The slots MUST be the catalog's stock-24 preset, not default_recipe.json's own
# list. They differ: the default recipe carries glisson/gendy/scanned/pulsar
# where stock-24 carries the three DX7 banks and wave-terrain, which is about
# 24.6 KB of six-op core and factory patches. flash-budget.ts's marginals were
# all measured in the stock-24 context, and a marginal only means anything
# against the context it was measured in -- code shared with an engine that is
# present is already paid for, so the same engine measures differently in a
# palette that lacks its neighbours. Measuring in the wrong base is silent: the
# builds succeed and the numbers look plausible.
_CATALOG = json.load(open('/work/alt_firmwares/plaits_lab_catalog/catalog.json'))
BASE['slots'] = list(_CATALOG['presets']['stock'])
SPEECH = BASE['slots'].index('speech')
# The duplicate that stands in for Speech in the baseline.
FILLER = 'virtual-analog'

# Engines to measure. Override from the command line so a re-measure of a
# handful of new engines does not need the image rebuilt:
#   python3 flash_sweep.py brass shakers bytebeat
NEW = sys.argv[1:] or [
    'z-filter', 'toy', 'csaw', 'bowed', 'ring-mod', 'sub-oscillator',
    'digital-modulation', 'saw-comb', 'vowel-fof', 'raw-fm', 'triple']


def measure(tag, engine_id):
    recipe = json.loads(json.dumps(BASE))
    recipe['slots'][SPEECH] = engine_id
    # The build key is a 64-hex content address, not a free-form label.
    key = hashlib.sha256(json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    payload = {'buildKey': key, 'recipe': recipe}
    try:
        elf_dir, _ = cs.build_firmware(payload)
    except Exception as error:  # noqa: BLE001 - report and continue the sweep
        print('%-22s BUILD FAILED %s' % (tag, error), flush=True)
        return None
    elf = None
    for candidate in ('plaits.elf', 'rubato-plaits.elf'):
        path = elf_dir.parent / candidate if elf_dir.is_file() else elf_dir / candidate
        if path.exists():
            elf = path
            break
    if elf is None:
        import pathlib
        found = list(pathlib.Path(elf_dir if elf_dir.is_dir() else elf_dir.parent)
                     .rglob('*.elf'))
        if not found:
            print('%-22s NO ELF in %s' % (tag, elf_dir), flush=True)
            return None
        elf = found[0]
    text, data, _bss = cs.parse_size(elf)
    total = text + data
    print('%-22s %8d' % (tag, total), flush=True)
    return total


# CONTROLS: engines whose marginal flash-budget.ts already records. They are
# measured in the same pair as the new engines, so if they reproduce their
# published values the method and the base line up and the new numbers can be
# trusted as if measured against the deployed builder. This is the same check
# the Helix measurement used (Speech re-measured 23,296 against a published
# 23,312 -- a 16 B agreement). If a control does NOT reproduce, stop: the
# toolchain or the base moved, and every new marginal is being compared against
# a different baseline than the rest of the table.
CONTROLS = {'speech': 23_312, 'reed-pipe': 2_000, 'spectral-spiral': 2_064}

base = measure('BASELINE(dup)', FILLER)
if base is None:
    sys.exit(1)
print('--- controls (expect the published value) ---', flush=True)
drift = False
for engine_id, published in CONTROLS.items():
    total = measure(engine_id, engine_id)
    if total is None:
        drift = True
        continue
    got = total - base
    delta = got - published
    print('  %-18s %7d  published %7d  delta %+d%s'
          % (engine_id, got, published, delta,
             '   <-- DRIFT' if abs(delta) > 64 else ''), flush=True)
    if abs(delta) > 64:
        drift = True

print('--- marginal cost, bytes ---', flush=True)
results = {}
for engine_id in NEW:
    total = measure(engine_id, engine_id)
    if total is not None:
        results[engine_id] = total - base
print(json.dumps(results, indent=2), flush=True)
if drift:
    print('\nA CONTROL DRIFTED. Do not paste these into flash-budget.ts until '
          'the base is reconciled -- the whole table would be inconsistent.',
          flush=True)
    sys.exit(2)
