#!/usr/bin/env python3
"""Guard for the hosted builder's ccache scoping.

The hosted build force-includes the generated engine_config.h into only the
objects listed as RECIPE_CONFIG_OBJS in plaits/makefile, so every other object
stays recipe-independent and reusable from ccache. That list must cover every
built translation unit whose compilation actually reads the recipe's macros.

A unit reads them in either of two ways, and both count:

  * it reaches a config header (engine_config.h, build_config.h) through its
    includes, directly or transitively;
  * it, or any header it reaches, USES a recipe macro without including a
    config header at all. plaits/dsp/engine/engine.h is the case that matters:
    EngineParameters and the Engine vtable change shape with
    PLAITS_BUILD_ENABLE_SYNC_INPUT and the FM options, yet engine.h only
    `#if`s on them (an undefined macro reads as 0). An engine unit outside the
    list compiles a different struct than voice.cc writes — measured
    2026-10-07: 40 bytes with `stereo` at offset 35 against voice.cc's 44 with
    `stereo` at 41, so Supersaw Chords read an unrelated byte as its stereo
    flag. Counting includes alone reported every such engine as "declared but
    not strictly required", which is how one was taken off the list.

A missing unit silently compiles with default options instead of the recipe's,
so this script fails the build in that case. Run from the repository root (or
pass the root as the only argument).
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
# Where the generated engine_config.h comes from; every macro it can #define is
# recipe-driven even when no checked-in header defines it.
GENERATOR = "alt_firmwares/plaits_lab_builder/generate_engine_config.py"
# Built packages that can reach plaits config headers (from plaits/makefile's
# PACKAGES, minus stmlib / stm_audio_bootloader, which never include them).
SOURCE_DIRS = [
    "plaits", "plaits/drivers", "plaits/dsp", "plaits/dsp/fm", "plaits/dsp/fx",
    "plaits/dsp/chords", "plaits/dsp/drum_modelling", "plaits/dsp/engine",
    "plaits/dsp/engine2", "plaits/dsp/physical_modelling", "plaits/dsp/speech",
]
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
DEFINE_RE = re.compile(r"#\s*define\s+([A-Z][A-Z0-9_]*)")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
IDENT_RE = re.compile(r"\b[A-Z][A-Z0-9_]*\b")
MACRO_DEF_RE = re.compile(r"^\s*#\s*define\s+([A-Z][A-Z0-9_]*)\b(.*)$", re.MULTILINE)


def read(root, path):
    try:
        with open(os.path.join(root, path), encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except FileNotFoundError:
        return None


def recipe_macros(root):
    """Every macro a recipe can change: what the generator writes into
    engine_config.h, closed over any checked-in `#define` derived from one (e.g.
    PLAITS_BUILD_FREQUENCY_OFFSET_FM, computed from the FM options). Options the
    generator never emits — Palette-only PLAITS_BUILD_EXTENDED_TZFM, diagnostic
    builds — are deliberately absent: every hosted recipe sees their defaults,
    so a unit reading only those compiles the same either way."""
    text = read(root, GENERATOR)
    if text is None:
        sys.exit(f"check_config_scope: {GENERATOR} not found under {root}")
    macros = {name for name in DEFINE_RE.findall(text) if not name.endswith("_H_")}
    definitions = []
    for path in sorted(COUPLING) + headers(root):
        source = read(root, path)
        if source is None:
            continue
        source = COMMENT_RE.sub(" ", source.replace("\\\n", " "))
        definitions += MACRO_DEF_RE.findall(source)
    changed = True
    while changed:
        changed = False
        for name, body in definitions:
            if name not in macros and macros.intersection(IDENT_RE.findall(body)):
                macros.add(name)
                changed = True
    return macros


def headers(root):
    found = []
    for directory in SOURCE_DIRS:
        full = os.path.join(root, directory)
        if os.path.isdir(full):
            found += sorted(f"{directory}/{name}" for name in os.listdir(full)
                            if name.endswith(".h"))
    return found


class Scope:
    def __init__(self, root, macros=None):
        self.root = root
        self.macros = recipe_macros(root) if macros is None else macros
        self.cache = {}

    def uses_macro(self, path):
        text = read(self.root, path)
        if text is None:
            return False
        return bool(self.macros.intersection(IDENT_RE.findall(COMMENT_RE.sub(" ", text))))

    def reaches(self, start):
        """True if `start` is a config header, uses a recipe macro itself, or
        transitively includes a file that does (quoted includes, resolved
        relative to the repo root as -I. does)."""
        if start in self.cache:
            return self.cache[start]
        self.cache[start] = False  # break cycles
        text = read(self.root, start)
        result = start in COUPLING or self.uses_macro(start)
        if not result and text is not None:
            result = any(self.reaches(inc) for inc in INCLUDE_RE.findall(text))
        self.cache[start] = result
        return result

    def required_objs(self):
        required = set()
        for directory in SOURCE_DIRS:
            full = os.path.join(self.root, directory)
            if not os.path.isdir(full):
                continue
            for name in os.listdir(full):
                if name.endswith(".cc") and self.reaches(f"{directory}/{name}"):
                    required.add(name[:-3] + ".o")
        return required


def declared_objs(root, makefile="plaits/makefile"):
    text = read(root, makefile)
    if text is None:
        sys.exit(f"check_config_scope: {makefile} not found under {root}")
    match = re.search(r"^RECIPE_CONFIG_OBJS\s*=\s*(.+)$", text, re.MULTILINE)
    if not match:
        sys.exit("check_config_scope: RECIPE_CONFIG_OBJS not found in plaits/makefile")
    return set(match.group(1).split())


def check(root="."):
    """Returns (missing, extra, required)."""
    required = Scope(root).required_objs()
    declared = declared_objs(root)
    return required - declared, declared - required, required


def main(argv):
    root = argv[1] if len(argv) > 1 else "."
    missing, extra, required = check(root)
    if missing:
        print("check_config_scope: FAIL — these built units read the recipe config "
              "but are not force-included (they would compile with default options):",
              file=sys.stderr)
        for obj in sorted(missing):
            print(f"  {obj}", file=sys.stderr)
        print("Add them to RECIPE_CONFIG_OBJS in plaits/makefile.", file=sys.stderr)
        return 1
    note = f" ({len(extra)} declared but not strictly required: {sorted(extra)})" if extra else ""
    print(f"check_config_scope: OK — {len(required)} recipe-config units, all force-included{note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
