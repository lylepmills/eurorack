#!/usr/bin/env python3
"""Rebuild the flash meter's ANCHOR palette, run INSIDE the builder container.

flash-budget.ts's base term is only meaningful against the revision production
actually runs, and it has silently outlived its anchor twice. The anchor is the
website's stock-24 recipe with the four pre-loaded chord tables; re-measuring it
at a new revision says whether flashBaseBytes still holds or has to move.

    python3 /work/alt_firmwares/plaits_lab_builder/anchor_build.py
"""
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
from flash_sweep import BASE, build_size  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

# The builder's OWN four-table stock-24 figure, measured at 1d0de19a4df6 and
# again at d1cab7e03e84. Deliberately NOT the 229,204 in flash-budget.ts's
# release-anchor note: that is the WEBSITE's four default chord tables, a
# different set, and differencing the two suggested moving the base 80 B in the
# direction that would make the meter under-report.
PUBLISHED = 229_124

recipe = json.loads(json.dumps(BASE))
recipe['schemaVersion'] = 10
recipe['preferences'] = {'navigationMode': 'linear'}
recipe['initialOptions'] = {
    'lockedFrequencyKnob': 'octaves',
    'modelInput': 'model',
    'levelInput': 'level',
    'auxOutput': 'alternate-model',
    'suboscillatorOctave': 0,
    'chordTable': 'original',
    'holdOnTrigger': False,
}
recipe['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
# Required from schema 10 on; empty is the mono anchor.
recipe['stereoEngines'] = []

total = build_size('anchor stock-24 (4 tables)', recipe)
if total is None:
    sys.exit('anchor build failed')
print('anchor %d  published %d  delta %+d' % (total, PUBLISHED, total - PUBLISHED),
      flush=True)
