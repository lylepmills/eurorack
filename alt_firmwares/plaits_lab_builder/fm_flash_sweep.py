#!/usr/bin/env python3
"""Leave-one-out EXPERIMENTAL FM flash sweep, run INSIDE the builder container.

flash-budget.ts's `experimentalFmEngineBytes` needs a row for every catalog
engine -- a missing row falls back to zero and silently under-estimates -- but
the original table was produced by linked-symbol deltas across five reference
palettes, a method with no script. This is the palette-differential equivalent,
so a newly added engine can be measured without reproducing that analysis.

Turning the experimental FM options on adds shared code plus a per-engine
marginal, so one build pair cannot separate them. This differences two pairs:

    marginal(E) = [on(E) - off(E)] - [on(FILLER) - off(FILLER)]

The baseline arm puts a DUPLICATE of an already-present engine in Speech's
slot, so it contributes no engine of its own and the shared FM cost, plus the
other 23 engines' marginals, cancel out of the subtraction.

Both options are enabled together, matching the "off-vs-both" reference
palettes the published table was measured from.

Usage (inside the container, repo bind-mounted at /work):

    python3 /work/alt_firmwares/plaits_lab_builder/fm_flash_sweep.py natural-speech

Controls run first and must reproduce their published values. A palette
differential resolves at link granularity where the published numbers came from
symbol deltas, so exact agreement is not expected -- but a control off by more
than DRIFT_TOLERANCE means the base or the method has moved and the new number
should not be trusted.
"""
import hashlib
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
from flash_sweep import BASE, SPEECH, FILLER, build_size  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

# v23 is the first schema carrying linearTzfm / fastFm, and it wants the
# configuration spelled out rather than defaulted (as the stereo arms do).
FM_SCHEMA_VERSION = 23
DRIFT_TOLERANCE = 64

# Stock-24 with both experimental FM options ON DOES NOT LINK -- it overflows
# flash by 9,936 B. So unlike the mono and stereo sweeps this one cannot use the
# stock context, which is presumably why the published table came from smaller
# reference palettes. Drop the three DX7 banks from BOTH arms, exactly as
# flash_sweep's --light mode does: they share one six-op core and ~24.6 KB of
# factory patches, which is more than enough room, and dropping them changes no
# source shared with the engine under test. The measurement is still a
# difference of differences, so the lighter context cancels -- but the controls
# below are what actually establish that.
for _bank in ('dx7-bank-a', 'dx7-bank-b', 'dx7-bank-c'):
    BASE['slots'][BASE['slots'].index(_bank)] = FILLER

# Published experimentalFmEngineBytes, the two speech engines nearest the
# engine this script was written to measure. KEEP IN SYNC with
# website/src/components/plaits-palette/flash-budget.ts -- these are a snapshot
# of someone else's table and rot on every FM re-calibration.
CONTROLS = {'speech': 418, 'lpc-speech': 404}

ARGV = sys.argv[1:]
NEW = ARGV or ['natural-speech']


def fm_recipe(slot_engine_id, fm_on):
    recipe = json.loads(json.dumps(BASE))
    recipe['schemaVersion'] = FM_SCHEMA_VERSION
    recipe['slots'][SPEECH] = slot_engine_id
    # Accepted preference shapes are cumulative PREFIXES of PREFERENCE_TIERS,
    # so reaching the experimental-FM tier means spelling out every tier before
    # it. Everything but the two FM switches is held off in both arms, so it
    # cancels out of the differential.
    recipe['preferences'] = {
        'navigationMode': 'linear',
        'calibration': False,
        'colorBlindMode': False,
        'replaceableFmBanks': False,
        'syncInput': False,
        'linearTzfm': bool(fm_on),
        'fastFm': bool(fm_on),
    }
    recipe['initialOptions'] = {
        'lockedFrequencyKnob': 'octaves',
        'modelInput': 'model',
        'levelInput': 'level',
        'auxOutput': 'alternate-model',
        'suboscillatorOctave': 0,
        'chordTable': 'original',
        'holdOnTrigger': False,
        # Schema 18 on requires the unpatched-attenuverter starting mode.
        # 'stock' is the legacy-equivalent no-op, and it is identical in
        # both arms, so it cancels out of the differential.
        'attenuverterMode': 'stock',
    }
    recipe['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
    # Required from schema 10 on; empty keeps both arms mono.
    recipe['stereoEngines'] = []
    return recipe


def fm_delta(engine_id):
    """on - off for a palette carrying engine_id in Speech's slot."""
    off = build_size('%s off' % engine_id, fm_recipe(engine_id, False))
    on = build_size('%s on' % engine_id, fm_recipe(engine_id, True))
    if off is None or on is None:
        return None
    return on - off


print('--- baseline (duplicate in Speech slot) ---', flush=True)
base_delta = fm_delta(FILLER)
if base_delta is None:
    sys.exit('baseline build failed; nothing can be measured')
print('BASELINE fm delta %8d' % base_delta, flush=True)

print('--- controls (expect the published value) ---', flush=True)
drifted = []
unbuildable = []
for engine_id, published in CONTROLS.items():
    delta = fm_delta(engine_id)
    if delta is None:
        # A control that will not LINK is not a control that DRIFTED. The FM
        # path is large enough that a big engine plus both switches overflows
        # even the lightened palette, which says nothing about whether the
        # method is sound -- so report it separately instead of poisoning the
        # run. As long as one control still reproduces, the method holds.
        unbuildable.append(engine_id)
        continue
    marginal = delta - base_delta
    gap = marginal - published
    print('  %-18s %6d  published %6d  delta %+d'
          % (engine_id, marginal, published, gap), flush=True)
    if abs(gap) > DRIFT_TOLERANCE:
        drifted.append(engine_id)

if unbuildable:
    print('CONTROLS UNBUILDABLE (too large with FM on, not a drift signal): %s'
          % ', '.join(unbuildable), flush=True)
if drifted:
    print('CONTROLS DRIFTED: %s -- do not trust the numbers below'
          % ', '.join(drifted), flush=True)
if not drifted and len(unbuildable) < len(CONTROLS):
    print('method validated by the control(s) that built', flush=True)

print('--- experimental FM marginal, bytes ---', flush=True)
for engine_id in NEW:
    delta = fm_delta(engine_id)
    if delta is None:
        continue
    print('  %-18s %6d' % (engine_id, delta - base_delta), flush=True)
