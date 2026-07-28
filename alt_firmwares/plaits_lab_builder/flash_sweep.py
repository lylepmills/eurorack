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
SPEECH = BASE['slots'].index('speech')
# The duplicate that stands in for Speech in the baseline.
FILLER = 'virtual-analog'

NEW = ['z-filter', 'toy', 'csaw', 'bowed', 'ring-mod', 'sub-oscillator',
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


base = measure('BASELINE(dup)', FILLER)
if base is None:
    sys.exit(1)
print('--- marginal cost, bytes ---', flush=True)
results = {}
for engine_id in NEW:
    total = measure(engine_id, engine_id)
    if total is not None:
        results[engine_id] = total - base
print(json.dumps(results, indent=2), flush=True)
