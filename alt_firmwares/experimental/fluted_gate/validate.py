# Copyright 2026 Lyle Mills.
# SPDX-License-Identifier: MIT
#
# Instrument validation for the §3.11 `fluted` gate -- run this BEFORE trusting
# any number the other scripts print.
#
# Two questions:
#
#   1. Does the probe reproduce Braids' own FLUT? Sweeping COLOR should show
#      the known chaotic detuning the cited study reports (1.50 / 3.47 / 1.01 /
#      2.44 / 2.48). If it does not, the probe is wrong, not the engine.
#   2. Is the port a faithful transcription? Pin the port's reflection corner to
#      Braids' own `lut_flute_body_filter` value and its fixed 1-sample pitch
#      term, then compare the mode each picks across COLOR. A chaotic loop will
#      not match bin for bin (PROGRESS §3.11) -- what has to match is the
#      CHARACTER: same scatter, same order of detuning.
#
# It also derives what Braids' fixed body filter works out to in harmonics of
# the note, which is what the port's MORPH replaces.

import re
import numpy as np

from fluted_probe import REPO, CORRECTED, render, analyse, note_to_hz


def body_filter_table():
    """lut_flute_body_filter, read out of the firmware source."""
    src = open(REPO + "/braids/resources.cc").read()
    m = re.search(r"const uint16_t lut_flute_body_filter\[\] = \{(.*?)\};", src, re.S)
    return [int(v) for v in re.findall(r"-?\d+", m.group(1))]


def corner_in_harmonics(body, note):
    """Braids' fixed corner at `note`, as a multiple of the note's own f0.

    Its one-pole runs at 96 kHz; the 48 kHz equivalent squares the pole, so
    b48 = 1 - (1 - b96)**2. Expressed against ReedPipe's b = 6.2832*h*f idiom
    that gives the harmonic count the port's MORPH has to reproduce.
    """
    b96 = body[note] / 4096.0
    b48 = 1.0 - (1.0 - b96) ** 2
    f = (440.0 / 8.0 / CORRECTED) * 0.25 * 2.0 ** ((note - 9.0) / 12.0)
    return b96, b48, b48 / (6.2832 * f)


def main():
    body = body_filter_table()

    print("1. Braids' body-filter corner in HARMONICS of the note")
    print("   (this is what the port's MORPH replaces -- note how nearly")
    print("   constant it is, which is why 'corner in harmonics' was the right")
    print("   generalisation of it)\n")
    print("   %-6s %8s %8s %10s %10s" % ("note", "lut", "b@96k", "b@48k", "harmonics"))
    for n in [43, 48, 55, 60, 67, 72, 79]:
        b96, b48, h = corner_in_harmonics(body, n)
        print("   %-6d %8d %8.4f %10.4f %10.1f" % (n, body[n], b96, b48, h))

    print("\n2. Does the probe reproduce Braids' FLUT? COLOR sweep at MIDI 60.")
    print("   Expect wide scatter with no relation to f0 -- the cited study")
    print("   reports 1.50 / 3.47 / 1.01 / 2.44 / 2.48.\n")
    f0 = note_to_hz(60)
    _, _, h_equiv = corner_in_harmonics(body, 60)
    morph_equiv = (h_equiv - 2.0) / 38.0
    print("   Braids' MIDI 60 corner == %.1f harmonics == the port at MORPH %.3f"
          % (h_equiv, morph_equiv))
    print("   (note that this is BELOW noon -- the region the claim under test")
    print("   calls reliable, and where Braids itself is famously not)\n")
    print("   %-8s %10s %10s %12s %10s" % (
        "COLOR", "braids", "port", "braids rms", "port rms"))
    for c in [0.0, 0.25, 0.5, 0.75, 1.0]:
        rb = analyse(render(model="braids", note=60, harmonics=c, timbre=0.5,
                            seconds=3.0), f0)
        rp = analyse(render(model="port", note=60, harmonics=c, timbre=0.5,
                            morph=morph_equiv, macro=0.5, seconds=3.0,
                            pitch_term=1.0), f0)
        print("   %-8.2f %10.3f %10.3f %12.4f %10.4f" % (
            c, rb["dom_ratio"], rp["dom_ratio"], rb["rms"], rp["rms"]))

    print("\n   Both sides scatter across multiples of f0 that have nothing to do")
    print("   with the played note. They do not agree take by take, and are not")
    print("   expected to: two coupled nonlinear loops a rate apart settle into")
    print("   different limit cycles (PROGRESS §3.11, same as `bowed`).")


if __name__ == "__main__":
    main()
