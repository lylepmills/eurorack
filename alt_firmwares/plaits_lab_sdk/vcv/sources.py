#!/usr/bin/env python3
# Copyright 2026 Matt Policastro.
# SPDX-License-Identifier: MIT
"""Print a makefile fragment that builds one Plaits Lab package into the module.

The SDK already knows a package's translation units, engine class and
post-processing (the same facts `check`, `render` and `dev` use), so the
module asks it instead of keeping its own list.

    python3 sources.py <package directory | built-in catalog id>
"""

import importlib.util
import json
import shlex
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("plaits_lab", HERE.parent / "plaits_lab.py")
plaits_lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plaits_lab)


def main(argument: str) -> None:
    path = Path(argument)
    package = (plaits_lab.load_package(str(path.resolve())) if path.exists()
               else plaits_lab.builtin_package(argument))
    manifest = package["manifest"]
    post = manifest.get("postProcessing", {})
    entry = HERE / "src" / "lab_voice.cc"
    units = [unit for unit in plaits_lab.engine_translation_units(package, entry)
             if Path(unit).resolve() != entry]
    flags = [
        f"-I{Path(package['repo_root']).resolve()}",
        f"-I{Path(package['source_root']).resolve()}",
        f'-DPLAITS_LAB_ENGINE_HEADER="{plaits_lab.engine_header_define(package)}"',
        f"-DPLAITS_LAB_ENGINE_CLASS=plaits::{manifest['source']['className']}",
        f"-DPLAITS_LAB_ENGINE_NAME=\"{manifest['name']}\"",
        f"-DPLAITS_LAB_USER_DATA_BANK={package.get('user_data_bank', -1)}",
        f"-DPLAITS_LAB_ALREADY_ENVELOPED={int(bool(post.get('alreadyEnveloped', False)))}",
        f"-DPLAITS_LAB_OUT_GAIN={float(post.get('outGain', 1.0))!r}f",
        f"-DPLAITS_LAB_AUX_GAIN={float(post.get('auxGain', 1.0))!r}f",
    ]
    # The panel and tooltips use the package's own control names.
    for control in manifest.get("controls", []):
        key = control["id"].upper()
        for suffix, field in (("LABEL", "label"), ("DESCRIPTION", "description")):
            if control.get(field):
                flags.append(f"-DPLAITS_LAB_{suffix}_{key}="
                             + json.dumps(control[field], ensure_ascii=False))
    print("PLAITS_LAB_SOURCES := " + " ".join(str(Path(unit).resolve()) for unit in units))
    quoted = " ".join(shlex.quote(flag) for flag in flags)
    print("PLAITS_LAB_FLAGS := " + quoted.replace("$", "$$").replace("#", "\\#"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1].strip())
    main(sys.argv[1])
