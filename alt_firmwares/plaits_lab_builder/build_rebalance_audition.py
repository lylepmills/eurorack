#!/usr/bin/env python3
"""Build a firmware for AUDITIONING the non-stock engine rebalance.

Not a release artifact: this exists so the new catalog gains can be heard on
a real module against untouched stock references. Slots 0-3 are stock models
at their original levels; slots 4-23 are the twenty rebalanced engines that
moved the most, loudest boost first, then the cuts.

    python3 /work/alt_firmwares/plaits_lab_builder/build_rebalance_audition.py [out.wav]

Run it the way sync_flash_sweep.py is run -- the checkout under test mounted
at /workspace as well as /work, and plaits/resources.cc touched first.
"""
import shutil
import sys
from pathlib import Path

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
import container_server as cs  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

STOCK_REFERENCES = ['virtual-analog', 'two-op-fm', 'modal-resonator', 'analog-snare']
REBALANCED = [
    'bowed', 'blown', 'metalwork', 'zxpulse48k', 'brass', 'scale-stack', 'question-mark',
    'diatonic-chord', 'undertow', 'saw-swarm', 'skins', 'wave-paraphonic', 'plucked',
    'freshets-formant',
    'virtual-analog-crossfade', 'lockstep', 'virtual-analog-dual', 'wave-scan', 'saw-comb', 'acid',
]

recipe = {
    'schemaVersion': 22,
    'target': 'mutable-instruments-plaits',
    'firmware': 'rubato-plaits',
    'output': 'audio-wav',
    'slots': STOCK_REFERENCES + REBALANCED,
    'preferences': {
        'navigationMode': 'linear',
        'calibration': False,
        'colorBlindMode': False,
        'replaceableFmBanks': False,
        'syncInput': False,
    },
    'initialOptions': {
        'lockedFrequencyKnob': 'octaves',
        'modelInput': 'model',
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

destination = Path(sys.argv[1] if len(sys.argv) > 1 else '/work/rebalance-audition.wav')
artifact, output, metadata = cs.build_firmware({'buildKey': 'b' * 64, 'recipe': recipe})
shutil.copy(artifact, destination)
print('output   %s' % output)
print('revision %s' % metadata.get('sourceRevision', '(unstamped)'))
print('wav      %s (%d bytes)' % (destination, destination.stat().st_size))
elf = next((p for p in artifact.parent.rglob('*.elf')), None)
if elf is not None:
    text, data, bss = cs.parse_size(elf)
    print('flash    %d B text + %d B data (%d B bss)' % (text, data, bss))
