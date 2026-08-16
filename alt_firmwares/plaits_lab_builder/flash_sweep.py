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
  --light [ids...]   the same sweep with all three DX7 banks removed from both
                     arms, for engines too large to link in the stock context.
                     The controls must still match before using the result.
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

ARGV = sys.argv[1:]
LIGHT = bool(ARGV and ARGV[0] == '--light')
if LIGHT:
    ARGV = ARGV[1:]
    # The three banks share one six-op core and factory patch resources. Drop
    # all three in both measurement arms to make enough room for an oversized
    # candidate without changing any source shared by the Braids ports.
    for bank in ('dx7-bank-a', 'dx7-bank-b', 'dx7-bank-c'):
        BASE['slots'][BASE['slots'].index(bank)] = FILLER

# Engines to measure. Override from the command line so a re-measure of a
# handful of new engines does not need the image rebuilt:
#   python3 flash_sweep.py brass shakers bytebeat
NEW = ARGV or [
    'z-filter', 'toy', 'csaw', 'bowed', 'ring-mod', 'sub-oscillator',
    'digital-modulation', 'saw-comb', 'vowel-fof', 'raw-fm', 'triple']

# Engines to difference in --stereo mode: engines with a real second render
# path behind a PLAITS_STEREO_<X> gate. `toy`
# shipped as the only one (a second sample-and-hold clock); bowed, csaw,
# ring-mod and vowel-fof joined it in c84a06a, which gave each a distinct mono
# AUX voice. Clap and Freshets Formant joined as community engines in 2026-08.
# The other Braids ports are Pattern A -- always a stereo pair at ~0 extra --
# and so want no entry. Keep this list matching STEREO_MACROS below in
# container_server.py.
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
# are the d554b7f46dc0 values (2026-08-05). If a run reports a large control
# gap, re-read that file before believing the engine you actually came to
# measure.
STEREO_DEFAULT = ['toy', 'bowed', 'csaw', 'ring-mod', 'vowel-fof',
                  'digital-modulation', 'clap', 'analog-percussion',
                  'freshets-formant',
                  'harmonic', 'glisson']
STEREO_CONTROLS = {'harmonic': 2_512, 'glisson': 432}

# The MONO sweep's controls, same idea: engines whose marginal flash-budget.ts
# already records, measured in the same pair as the new engines. If they
# reproduce their published values the method and the base line up and the new
# numbers can be trusted as if measured against the deployed builder -- the same
# check the Helix measurement used. If a control does NOT reproduce, stop: the toolchain
# or the base moved, and every new marginal is being compared against a different
# baseline than the rest of the table.
CONTROLS = {'speech': 23_200, 'reed-pipe': 2_000, 'spectral-spiral': 2_032}


def build_size(tag, recipe):
    """Build one recipe and return its text+data bytes (None if it failed)."""
    # The build key is a 64-hex content address, not a free-form label.
    key = hashlib.sha256(json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    payload = {'buildKey': key, 'recipe': recipe}
    try:
        artifact_path, _output, _metadata = cs.build_firmware(payload)
    except Exception as error:  # noqa: BLE001 - report and continue the sweep
        print('%-22s BUILD FAILED %s' % (tag, error), flush=True)
        detail = getattr(error, 'detail', '')
        if detail:
            print(detail[-4000:], flush=True)
        return None
    elf = None
    for candidate in ('plaits.elf', 'rubato-plaits.elf'):
        path = artifact_path.parent / candidate
        if path.exists():
            elf = path
            break
    if elf is None:
        import pathlib
        found = list(artifact_path.parent.rglob('*.elf'))
        if not found:
            print('%-22s NO ELF beside %s' % (tag, artifact_path), flush=True)
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


# A control that misses by more than this means the base moved under you.
DRIFT_TOLERANCE = 64

DRIFT_WARNING = (
    'A CONTROL DRIFTED. Do not paste these into flash-budget.ts until the base '
    'is reconciled -- the whole table would be inconsistent.'
)


def report_control(engine_id, got, published):
    """Print one control line; return True if it DRIFTED."""
    delta = got - published
    drifted = abs(delta) > DRIFT_TOLERANCE
    print('  %-18s %7d  published %7d  delta %+d%s'
          % (engine_id, got, published, delta, '   <-- DRIFT' if drifted else ''),
          flush=True)
    return drifted


def mono_sweep():
    base = measure('BASELINE(dup)', FILLER)
    if base is None:
        return 1
    print('--- controls (expect the published value) ---', flush=True)
    drift = False
    for engine_id, published in CONTROLS.items():
        total = measure(engine_id, engine_id)
        if total is None:
            drift = True
            continue
        drift |= report_control(engine_id, total - base, published)

    print('--- marginal cost, bytes ---', flush=True)
    results = {}
    for engine_id in NEW:
        total = measure(engine_id, engine_id)
        if total is not None:
            results[engine_id] = total - base
    print(json.dumps(results, indent=2), flush=True)
    if drift:
        print('\n' + DRIFT_WARNING, flush=True)
        return 2
    return 0


def stereo_sweep(engine_ids):
    # TWO builds per engine, both with THAT engine in Speech's slot, differing
    # only in whether its stereo macro is on.
    #
    # The engine has to be IN the palette or the measurement is silently
    # meaningless: enabling PLAITS_STEREO_<X> for an engine no slot references
    # links no object, so the delta is exactly 0 and looks like a real "costs
    # nothing" result. An earlier version of this pinned one engine in the slot
    # and swept `stereoEngines` against it, which reported a clean 0 for four
    # engines that simply were not in the build. A shared baseline is not worth
    # that failure mode -- the extra build per engine is ~10 s and cached.
    print('--- per-engine stereo delta, bytes ---', flush=True)
    results = {}
    drift = False
    for engine_id in engine_ids:
        mono = build_size('%s (mono)' % engine_id, stereo_recipe(engine_id, []))
        if mono is None:
            continue
        stereo = build_size('%s (stereo)' % engine_id,
                            stereo_recipe(engine_id, [engine_id]))
        if stereo is None:
            continue
        results[engine_id] = stereo - mono
        published = STEREO_CONTROLS.get(engine_id)
        if published is not None:
            drift |= report_control(engine_id, results[engine_id], published)
    print(json.dumps(results, indent=2), flush=True)
    if drift:
        print('\n' + DRIFT_WARNING, flush=True)
        return 2
    return 0


if __name__ == '__main__':
    if ARGV and ARGV[0] == '--stereo':
        sys.exit(stereo_sweep(ARGV[1:] or STEREO_DEFAULT))
    sys.exit(mono_sweep())
