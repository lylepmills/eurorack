#!/usr/bin/env python3
"""Build a focused firmware for AUDITIONING Acid's native hard sync.

Not a release artifact and not a measurement: this exists so the change can be
heard on a real module before it ships. The palette is arranged as a three-way
A/B/C on the module's first three model positions, because the interesting
question is not "does Acid react to sync" (a host test already answers that)
but "does it react the way a native engine should, rather than the way the
bounded fallback does".

  slot 0  acid            the engine under test, now native
  slot 1  virtual-analog  reference: native since the original sync pass
  slot 2  chiptune        contrast: fallback-only, one reset per block

Slots 3-23 repeat Acid so no other engine is compiled in (smaller image,
faster build) and so any model-knob position lands somewhere useful.

MODEL starts as SYNC IN, so the jack is live at power-on with no menu diving.

    python3 /work/alt_firmwares/plaits_lab_builder/build_acid_sync_test.py [out.wav]

Run it the way sync_flash_sweep.py is run -- the checkout under test must be
mounted at /workspace as well as /work, and plaits/resources.cc touched first.
"""
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
import container_server as cs  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

UNDER_TEST = 'acid'
NATIVE_REFERENCE = 'virtual-analog'
FALLBACK_CONTRAST = 'chiptune'

recipe = {
    'schemaVersion': 22,
    'target': 'mutable-instruments-plaits',
    'firmware': 'rubato-plaits',
    'output': 'audio-wav',
    'slots': (
        [UNDER_TEST, NATIVE_REFERENCE, FALLBACK_CONTRAST] + [UNDER_TEST] * 21
    ),
    'preferences': {
        'navigationMode': 'linear',
        'calibration': False,
        'colorBlindMode': False,
        'replaceableFmBanks': False,
        'syncInput': True,
    },
    'initialOptions': {
        'lockedFrequencyKnob': 'octaves',
        # The whole point: MODEL is the sync input from power-on.
        'modelInput': 'sync-in',
        'levelInput': 'level',
        'auxOutput': 'alternate-model',
        'suboscillatorOctave': 0,
        'chordTable': 'original',
        'holdOnTrigger': False,
        'attenuverterMode': 'stock',
    },
    'resources': {'chordTables': DEFAULT_CHORD_TABLES},
}

assert len(recipe['slots']) == 24, len(recipe['slots'])

destination = Path(sys.argv[1] if len(sys.argv) > 1 else '/work/acid-sync-test.wav')
artifact, output, metadata = cs.build_firmware(
    {'buildKey': 'a' * 64, 'recipe': recipe})
shutil.copy(artifact, destination)
print('output   %s' % output)
print('revision %s' % metadata.get('sourceRevision', '(unstamped)'))
print('wav      %s (%d bytes)' % (destination, destination.stat().st_size))

elf = next((p for p in artifact.parent.rglob('*.elf')), None)
if elf is not None:
    text, data, bss = cs.parse_size(elf)
    print('flash    %d B text + %d B data (%d B bss)' % (text, data, bss))
