#!/usr/bin/env python3
"""Per-engine Sync In flash marginal, run INSIDE the plaits-lab-builder container.

flash-budget.ts's syncInputEngineBytes rows are the extra flash an engine's
NATIVE sample-accurate reset path costs once Sync In is compiled in. A single
on-vs-off difference cannot give that: it also carries syncInputBaseBytes and
every other native engine's path. So each row is a 2x2
difference-in-differences, with and without the engine, each arm built with
Sync In off and on:

    row = (with engine, sync on) - (with engine, sync off)
        - (without engine, sync on) + (without engine, sync off)

The without-arm subtracts the shared base and the sync code of every OTHER
engine present in both arms; the off-vs-on pairing subtracts the engine's
ordinary (non-sync) code. What survives is only that engine's native path.

TWO FACTS THE BASE PALETTE HAS TO RESPECT, both learned the hard way:

  * Speech always comes out. stock-24 carrying both Speech and Sync In does
    not link at all -- it overflows FLASH by ~10 KB -- so an arm that keeps
    Speech fails and the row is unmeasurable. Removing the largest engine is
    what buys room for the sync paths under test. This is why the original
    sweep swapped SPEECH's slot specifically, rather than any other.
  * An engine the base palette ALREADY carries cannot be measured by adding it
    to a spare slot: duplicate slots collapse to one instance, so the arms come
    out identical and the row reads zero. Such an engine is measured by taking
    it OUT instead. Both shapes are handled below; callers do not choose.

    python3 /work/alt_firmwares/plaits_lab_builder/sync_flash_sweep.py acid
    python3 .../sync_flash_sweep.py --all      # every row in flash-budget.ts

Run it the way flash_sweep.py is run, but mount the checkout you want measured
at /workspace as well -- container_server builds from there, not from /work, so
mounting only /work silently measures the image's baked source instead:

    docker run --rm --platform linux/amd64 \\
      -v "$PWD":/work -v "$PWD":/workspace -w /work \\
      -v /path/to/eurorack/stmlib:/work/stmlib:ro \\
      -v /path/to/eurorack/stmlib:/workspace/stmlib:ro \\
      -v /path/to/eurorack/stm_audio_bootloader:/workspace/stm_audio_bootloader:ro \\
      --entrypoint python3 plaits-lab-builder:<tag> \\
      /work/alt_firmwares/plaits_lab_builder/sync_flash_sweep.py --all

A fresh worktree checks plaits/resources.cc out with an mtime older than
plaits/resources/*.py, so make tries to regenerate it and dies on the python2
scipy the image does not carry. `touch plaits/resources.cc` first; the content
is unchanged.

The control below is the check that the pairing held: an engine with NO native
path must difference to 0, because its fallback code lives in the shared base.
A non-zero control means the arms were not comparable and the rows are garbage.
"""
import json
import sys

sys.path.insert(0, '/work/alt_firmwares/plaits_lab_builder')
from flash_sweep import BASE, build_size  # noqa: E402
from generate_engine_config import DEFAULT_CHORD_TABLES  # noqa: E402

SYNC_SCHEMA_VERSION = 22

# NOT flash_sweep's FILLER. That is 'virtual-analog', which stock-24 already
# carries AND which is itself one of the rows measured here -- so "removing" it
# would replace a virtual-analog slot with another virtual-analog slot, the arms
# would be identical, and its row would silently read 0. A fallback-only engine
# is the right filler for this sweep for a second reason too: it contributes no
# native sync code of its own, so it cannot leak into the difference.
FILLER = 'chiptune'
# A fallback-only engine that is NOT in the base palette and NOT the filler:
# it must be measurable by the ADD shape, and if it were the filler its own
# without-arm would be a no-op for the same reason FILLER cannot be measured.
CONTROL = 'gendy'
CONTROL_TOLERANCE = 96

# The committed table, so a run reports DRIFT rather than bare numbers.
PUBLISHED = {
    'virtual-analog': 3_872, 'virtual-analog-dual': 2_592,
    'virtual-analog-crossfade': 2_500, 'waveshaping': 1_148, 'two-op-fm': 1_384,
    'granular-formant': 1_992, 'harmonic': 4_832, 'wavetable': 1_652,
    'swarm': 1_744, 'virtual-analog-vcf': 1_552, 'wave-terrain': 552,
    'acid': 816,
}


def recipe(slots, sync_input):
    r = json.loads(json.dumps(BASE))
    r['schemaVersion'] = SYNC_SCHEMA_VERSION
    r['slots'] = list(slots)
    # Preferences validate as a cumulative PREFIX of PREFERENCE_TIERS, so
    # reaching syncInput (tier 4) means spelling out tiers 0-3 at their
    # defaults. Both arms carry the identical set; only the flag moves.
    r['preferences'] = {
        'navigationMode': 'linear', 'calibration': False,
        'colorBlindMode': False, 'replaceableFmBanks': False,
        'syncInput': bool(sync_input),
    }
    r['initialOptions'] = {
        'lockedFrequencyKnob': 'octaves',
        # Deliberately NOT 'sync': compiling the option in is what pulls the
        # native paths, and starting the module in it would be a second change
        # between the arms.
        'modelInput': 'model', 'levelInput': 'level',
        'auxOutput': 'alternate-model', 'suboscillatorOctave': 0,
        'chordTable': 'original', 'holdOnTrigger': False,
        'attenuverterMode': 'stock',
    }
    r['resources'] = {'chordTables': DEFAULT_CHORD_TABLES}
    return r


# The three DX7 banks share one six-op core and its factory patches -- about
# 24 KB. Some engines cannot be ADDED to the full base at all: the with-arm
# overflows FLASH and the row is unmeasurable. Dropping the banks from BOTH
# arms buys the room, exactly as flash_sweep's --light does. A row measured
# this way is not strictly comparable to a full-context one -- less code is
# present to share, so the marginal reads slightly HIGH -- which is the
# conservative direction for a flash meter. The control must still difference
# to ~0 before such a row is used.
LIGHT_BANKS = ('dx7-bank-a', 'dx7-bank-b', 'dx7-bank-c')
LIGHT = False


def arms(engine_id):
    """(with, without) slot lists for `engine_id`. See the module docstring."""
    slots = list(BASE['slots'])
    speech = slots.index('speech')
    slots[speech] = FILLER            # headroom; see docstring
    if LIGHT:
        for bank in LIGHT_BANKS:
            while bank in slots:
                slots[slots.index(bank)] = FILLER
    if engine_id in slots:
        without = list(slots)
        without[slots.index(engine_id)] = FILLER
        return slots, without
    with_it = list(slots)
    with_it[speech] = engine_id       # the slot Speech vacated
    return with_it, slots


def marginal(engine_id):
    with_it, without = arms(engine_id)
    sizes = {}
    for label, slots in (('with', with_it), ('without', without)):
        for sync in (False, True):
            tag = '%s/%s/%s' % (engine_id, label, 'sync' if sync else 'nosync')
            sizes[(label, sync)] = build_size(tag, recipe(slots, sync))
    if any(v is None for v in sizes.values()):
        return None
    return ((sizes[('with', True)] - sizes[('with', False)])
            - (sizes[('without', True)] - sizes[('without', False)]))


def main():
    global LIGHT
    argv = sys.argv[1:]
    if '--light' in argv:
        LIGHT = True
        argv = [a for a in argv if a != '--light']
        print('--light: the three DX7 banks are dropped from BOTH arms\n', flush=True)
    engines = sorted(PUBLISHED) if argv == ['--all'] else (argv or ['acid'])

    print('control: %s (must difference to ~0)' % CONTROL, flush=True)
    control = marginal(CONTROL)
    if control is None:
        sys.exit('control build failed -- no row is trustworthy')
    print('control marginal: %+d B\n' % control, flush=True)
    if abs(control) > CONTROL_TOLERANCE:
        sys.exit('control differenced to %+d B, past the %d B tolerance: the '
                 'arms are not comparable, so the rows below would be noise.'
                 % (control, CONTROL_TOLERANCE))

    rows, failed = {}, []
    for engine_id in engines:
        row = marginal(engine_id)
        if row is None:
            failed.append(engine_id)
            print('%-26s FAILED' % engine_id, flush=True)
            continue
        rows[engine_id] = row
        published = PUBLISHED.get(engine_id)
        drift = '' if published is None else '  (table %5d, drift %+5d)' % (
            published, published - row)
        print('%-26s %5d B%s' % (engine_id, row, drift), flush=True)

    if rows:
        print('\nsyncInputEngineBytes:', flush=True)
        for engine_id in sorted(rows):
            print('  "%s": %d,' % (engine_id, rows[engine_id]), flush=True)
    if failed:
        print('\nunmeasured: %s' % ', '.join(failed), flush=True)


if __name__ == '__main__':
    main()
