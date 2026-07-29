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

Two modes:

  (no args)          the MONO marginal sweep above -- engineFlashBytes.
  --stereo [ids...]  the per-engine STEREO delta -- engineStereoBytes.

The stereo mode differences two builds that are identical except for the
recipe's `stereoEngines` list, so what it isolates is exactly the engine's
PLAITS_STEREO_<MACRO> render code. Both arms set auxOutput to "stereo", so
whatever the stereo aux option itself costs cancels -- which is what
flash-budget.ts's model wants, since it carries that in flashBaseBytes and
adds engineStereoBytes strictly per enabled engine.

Per-engine stereo needs recipe schemaVersion 10, so the stereo arms carry the
explicit configuration + chordTables that schema requires; the mono sweep
stays on the default recipe's schema 2 and its implicit defaults.
"""
import hashlib
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
import container_server as cs
from generate_engine_config import DEFAULT_CHORD_TABLES

BASE = json.load(open('/work/alt_firmwares/plaits_lab_builder/default_recipe.json'))
SPEECH = BASE['slots'].index('speech')
# The duplicate that stands in for Speech in the baseline.
FILLER = 'virtual-analog'

NEW = ['z-filter', 'toy', 'csaw', 'bowed', 'ring-mod', 'sub-oscillator',
       'digital-modulation', 'saw-comb', 'vowel-fof', 'raw-fm', 'triple']

# Engines to difference in --stereo mode. `toy` is the one Pattern-B Braids
# port (a second sample-and-hold clock behind PLAITS_STEREO_TOY); the other ten
# are Pattern A -- always a stereo pair at ~0 extra -- and so want no entry.
#
# harmonic and glisson are CONTROLS: both sit in the base palette and both are
# already in the website's engineStereoBytes, so reproducing them validates the
# harness before a new engine's number is trusted. Expect agreement to within
# a few tens of bytes, not exactness -- this is a LOCAL container on a branch
# head, against a table measured on the deployed builder.
#
# KEEP THESE IN SYNC with website/src/components/plaits-palette/flash-budget.ts.
# They are a snapshot of someone else's table, so they rot on every flash-meter
# re-calibration; a control that silently drifts is worse than no control. These
# are the rev-94e84165 values (2026-07-27). If a run reports a large control
# gap, re-read that file before believing the engine you actually came to
# measure.
STEREO_DEFAULT = ['toy', 'harmonic', 'glisson']
STEREO_CONTROLS = {'harmonic': 2_560, 'glisson': 432}


def build_size(tag, recipe):
    """Build one recipe and return its text+data bytes (None if it failed)."""
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


def measure(tag, engine_id):
    recipe = json.loads(json.dumps(BASE))
    recipe['slots'][SPEECH] = engine_id
    return build_size(tag, recipe)


def stereo_recipe(slot_engine_id, stereo_engines):
    """The base palette with `slot_engine_id` in Speech's slot, aux = stereo.

    schemaVersion 10 is what carries `stereoEngines`, and it requires the
    configuration and chordTables to be spelled out rather than defaulted.
    """
    recipe = json.loads(json.dumps(BASE))
    recipe['schemaVersion'] = 10
    recipe['slots'][SPEECH] = slot_engine_id
    recipe['preferences'] = {'navigationMode': 'linear'}
    recipe['initialOptions'] = {
        'lockedFrequencyKnob': 'octaves',
        'modelInput': 'model',
        'levelInput': 'level',
        'auxOutput': 'stereo',
        'suboscillatorOctave': 0,
        'chordTable': 'original',
        'holdOnTrigger': False,
    }
    recipe['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
    recipe['stereoEngines'] = list(stereo_engines)
    return recipe


def mono_sweep():
    base = measure('BASELINE(dup)', FILLER)
    if base is None:
        return 1
    print('--- marginal cost, bytes ---', flush=True)
    results = {}
    for engine_id in NEW:
        total = measure(engine_id, engine_id)
        if total is not None:
            results[engine_id] = total - base
    print(json.dumps(results, indent=2), flush=True)
    return 0


def stereo_sweep(engine_ids):
    # Every arm holds the palette fixed with `toy` in Speech's slot, so `toy`
    # is present to have its stereo path enabled and the controls are measured
    # against the identical 24 engines. The all-mono arm is the shared baseline.
    slot = 'toy'
    base = build_size('STEREO-BASE(none)', stereo_recipe(slot, []))
    if base is None:
        return 1
    print('--- per-engine stereo delta, bytes ---', flush=True)
    results = {}
    for engine_id in engine_ids:
        total = build_size(engine_id, stereo_recipe(slot, [engine_id]))
        if total is None:
            continue
        results[engine_id] = total - base
        expected = STEREO_CONTROLS.get(engine_id)
        if expected is not None:
            print('%-22s control: published %d, measured %d (%+d)'
                  % ('', expected, results[engine_id], results[engine_id] - expected),
                  flush=True)
    print(json.dumps(results, indent=2), flush=True)
    return 0


if __name__ == '__main__':
    argv = sys.argv[1:]
    if argv and argv[0] == '--stereo':
        sys.exit(stereo_sweep(argv[1:] or STEREO_DEFAULT))
    sys.exit(mono_sweep())
