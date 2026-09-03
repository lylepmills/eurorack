"""Measure the Sync In delta of flash-budget.ts's 24-model reference palette.

That palette's +19,552 B anchor was a paired ARM build at the schema-21 release
candidate (August 2026). This re-measures the SAME slot list at the current
revision so a moved anchor can be attributed to real firmware change rather
than re-pinned to whatever the model now outputs.
"""
import json, sys
sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
from flash_sweep import BASE, build_size
from generate_engine_config import DEFAULT_CHORD_TABLES

REFERENCE = [
    'virtual-analog', 'waveshaping', 'two-op-fm', 'granular-formant',
    'harmonic', 'wavetable', 'chords', 'speech', 'swarm', 'filtered-noise',
    'particle-noise', 'inharmonic-string', 'modal-resonator', 'analog-bass-drum',
    'analog-snare', 'analog-hi-hat', 'virtual-analog-vcf', 'phase-distortion',
    'glisson', 'gendy', 'scanned', 'pulsar', 'string-machine', 'chiptune',
]

def recipe(sync):
    r = json.loads(json.dumps(BASE)); r['schemaVersion'] = 22
    r['slots'] = list(REFERENCE)
    r['preferences'] = {'navigationMode':'linear','calibration':False,
        'colorBlindMode':False,'replaceableFmBanks':False,'syncInput':bool(sync)}
    r['initialOptions'] = {'lockedFrequencyKnob':'octaves','modelInput':'model',
        'levelInput':'level','auxOutput':'alternate-model','suboscillatorOctave':0,
        'chordTable':'original','holdOnTrigger':False,'attenuverterMode':'stock'}
    r['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
    return r

off = build_size('reference/nosync', recipe(False))
on  = build_size('reference/sync',   recipe(True))
if off and on:
    print('\n24-model reference palette at this revision')
    print('  nosync %d, sync %d' % (off, on))
    print('  measured Sync In delta : %d B' % (on - off))
    print('  August anchor          : 19552 B (model pinned 19584)')
    print('  drift since August     : %+d B' % ((on - off) - 19552))
