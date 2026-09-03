#!/usr/bin/env python3
"""Per-engine Sync In flash marginal, run INSIDE the plaits-lab-builder container.

flash-budget.ts's syncInputEngineBytes rows are the extra flash an engine's
NATIVE sample-accurate reset path costs once Sync In is compiled in. A single
on-vs-off difference cannot give that: it also carries syncInputBaseBytes and
every other native engine's path. So this is a 2x2 difference-in-differences in
flash_sweep's stock-24 context, with the engine under test in Speech's slot:

    row = (engine, sync on) - (engine, sync off)
        - (filler, sync on) + (filler, sync off)

The filler arm subtracts the shared base and the sync code of the 23 engines
that are present in both arms, and the off-vs-on pairing subtracts the engine's
ordinary (non-sync) code. What survives is only that engine's native path.

Run it the way flash_sweep.py is run, but mount the checkout you want measured
at /workspace as well -- container_server builds from there, not from /work, so
mounting only /work silently measures the image's baked source instead:

    docker run --rm --platform linux/amd64 \
      -v "$PWD":/work -v "$PWD":/workspace -w /work \
      -v /path/to/eurorack/stmlib:/work/stmlib:ro \
      -v /path/to/eurorack/stmlib:/workspace/stmlib:ro \
      -v /path/to/eurorack/stm_audio_bootloader:/workspace/stm_audio_bootloader:ro \
      --entrypoint python3 plaits-lab-builder:<tag> \
      /work/alt_firmwares/plaits_lab_builder/sync_flash_sweep.py acid

A fresh worktree checks plaits/resources.cc out with an mtime older than
plaits/resources/*.py, so make tries to regenerate it and dies on the python2
scipy the image does not carry. `touch plaits/resources.cc` first; the content
is unchanged.

The control below is the check that the pairing held: an engine with NO native
path must difference to 0, because its fallback code lives in the shared base.
A non-zero control means the arms were not comparable and the row is garbage.
"""
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
from flash_sweep import BASE, FILLER, SPEECH, build_size  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

# Sync In is a schema-22 preference, and that schema wants the configuration and
# chord tables spelled out rather than defaulted -- the same requirement the
# stereo arms carry.
SYNC_SCHEMA_VERSION = 22

# A fallback-only engine. It must difference to zero; see the module docstring.
CONTROL = 'chiptune'

# Slack for link-layout noise (alignment, literal-pool placement). The stereo
# sweep uses 64 B against whole-palette totals; a difference-of-differences
# accumulates four builds' worth, so allow a little more.
CONTROL_TOLERANCE = 96


def sync_recipe(slot_engine_id, sync_input):
    recipe = json.loads(json.dumps(BASE))
    recipe['schemaVersion'] = SYNC_SCHEMA_VERSION
    recipe['slots'][SPEECH] = slot_engine_id
    # Preferences are validated as a cumulative PREFIX of PREFERENCE_TIERS, so
    # reaching syncInput (tier 4) means spelling out tiers 0-3 at their
    # defaults too. Both arms carry the identical set; only the flag moves.
    recipe['preferences'] = {
        'navigationMode': 'linear',
        'calibration': False,
        'colorBlindMode': False,
        'replaceableFmBanks': False,
        'syncInput': bool(sync_input),
    }
    recipe['initialOptions'] = {
        'lockedFrequencyKnob': 'octaves',
        # Deliberately NOT 'sync': compiling the option in is what pulls the
        # native paths, and starting the module in it would be a second change
        # between the arms.
        'modelInput': 'model',
        'levelInput': 'level',
        'auxOutput': 'alternate-model',
        'suboscillatorOctave': 0,
        'chordTable': 'original',
        'holdOnTrigger': False,
        # Starting options match INITIAL_OPTION_TIERS the same prefix way, and
        # schema 22 is past the v18 gate that makes attenuverterMode required.
        'attenuverterMode': 'stock',
    }
    recipe['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
    return recipe


def marginal(engine_id):
    """The 2x2 for one engine. None if any of the four arms failed to build."""
    sizes = {}
    for slot, label in ((engine_id, engine_id), (FILLER, 'filler')):
        for sync in (False, True):
            tag = '%s/%s' % (label, 'sync' if sync else 'nosync')
            sizes[(label, sync)] = build_size(tag, sync_recipe(slot, sync))
    if any(value is None for value in sizes.values()):
        return None
    engine_delta = sizes[(engine_id, True)] - sizes[(engine_id, False)]
    filler_delta = sizes[('filler', True)] - sizes[('filler', False)]
    print('  %-18s sync delta %+7d' % (engine_id, engine_delta), flush=True)
    print('  %-18s sync delta %+7d' % ('filler (' + FILLER + ')', filler_delta),
          flush=True)
    return engine_delta - filler_delta


def main():
    engines = sys.argv[1:] or ['acid']
    print('control: %s (must difference to ~0)' % CONTROL, flush=True)
    control = marginal(CONTROL)
    if control is None:
        sys.exit('control build failed -- no row is trustworthy')
    print('control marginal: %+d B\n' % control, flush=True)
    if abs(control) > CONTROL_TOLERANCE:
        sys.exit(
            'control differenced to %+d B, past the %d B tolerance: the arms '
            'are not comparable, so the rows below would be noise.'
            % (control, CONTROL_TOLERANCE))

    for engine_id in engines:
        print('measuring %s' % engine_id, flush=True)
        row = marginal(engine_id)
        if row is None:
            print('%-22s FAILED' % engine_id, flush=True)
            continue
        print('\n  "%s": %d,   // syncInputEngineBytes row\n' % (engine_id, row),
              flush=True)


if __name__ == '__main__':
    main()
