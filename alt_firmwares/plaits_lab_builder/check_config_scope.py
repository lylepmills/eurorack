#!/usr/bin/env python3
"""Guard for the hosted builder's ccache scoping.

The hosted build force-includes the generated engine_config.h into only the
objects listed as RECIPE_CONFIG_OBJS in plaits/makefile, so every other object
stays recipe-independent and reusable from ccache. That list must cover every
built translation unit whose compilation actually reads the recipe's macros —
i.e. one that reaches engine_config.h or build_config.h through its includes.

If a source change makes another unit include those headers (directly or
transitively) without adding it to the list, that unit would silently compile
with build_config.h's *default* options instead of the recipe's. This script
fails the build in that case. Run from the repository root.
"""
import os
import re
import sys

# Config headers whose macros are recipe-driven. A unit reaching any of these
# must be force-included with the generated config.
COUPLING = {
    "plaits/build_config.h",
    "plaits/dsp/engine_config.h",
    "plaits/dsp/stock_engine_config.h",
}
# Built packages that can reach plaits config headers (from plaits/makefile's
# PACKAGES, minus stmlib / stm_audio_bootloader, which never include them).
SOURCE_DIRS = [
    "plaits", "plaits/drivers", "plaits/dsp", "plaits/dsp/fm", "plaits/dsp/fx",
    "plaits/dsp/chords", "plaits/dsp/drum_modelling", "plaits/dsp/engine",
    "plaits/dsp/engine2", "plaits/dsp/physical_modelling", "plaits/dsp/speech",
]
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def includes(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return INCLUDE_RE.findall(handle.read())
    except FileNotFoundError:
        return []


def reaches_coupling(start, cache):
    """True if `start` transitively includes a coupling header (quoted includes,
    resolved relative to the repo root as -I. does)."""
    if start in cache:
        return cache[start]
    cache[start] = False  # break cycles
    result = False
    for inc in includes(start):
        if inc in COUPLING or reaches_coupling(inc, cache):
            result = True
            break
    cache[start] = result
    return result


def expected_objs(makefile="plaits/makefile"):
    text = open(makefile, encoding="utf-8").read()
    match = re.search(r"^RECIPE_CONFIG_OBJS\s*=\s*(.+)$", text, re.MULTILINE)
    if not match:
        sys.exit("check_config_scope: RECIPE_CONFIG_OBJS not found in plaits/makefile")
    return {tok for tok in match.group(1).split()}


def main():
    cache = {}
    required = set()
    for directory in SOURCE_DIRS:
        if not os.path.isdir(directory):
            continue
        for name in os.listdir(directory):
            if name.endswith(".cc") and reaches_coupling(f"{directory}/{name}", cache):
                required.add(name[:-3] + ".o")
    declared = expected_objs()
    missing = required - declared
    if missing:
        print("check_config_scope: FAIL — these built units read the recipe config "
              "but are not force-included (they would compile with default options):",
              file=sys.stderr)
        for obj in sorted(missing):
            print(f"  {obj}", file=sys.stderr)
        print("Add them to RECIPE_CONFIG_OBJS in plaits/makefile.", file=sys.stderr)
        return 1
    extra = declared - required
    note = f" ({len(extra)} declared but not strictly required: {sorted(extra)})" if extra else ""
    print(f"check_config_scope: OK — {len(required)} recipe-config units, all force-included{note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
