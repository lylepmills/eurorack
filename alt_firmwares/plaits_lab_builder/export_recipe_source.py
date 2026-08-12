#!/usr/bin/env python3
"""Export a Plaits Palette recipe as an editable local-build overlay."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path
from typing import Any

from container_server import (
    BUILD_CONTRACT_VERSION,
    TOOLCHAIN_ID,
    _declared_region_count,
    _recipe_is_stereo,
    _stereo_disable_flags,
)
from generate_engine_config import render_config, validate_recipe
from speech_banks import render_speech_config
from user_data_regions import render_linker_script


REPOSITORY_URL = "https://github.com/lylepmills/eurorack"
CONFIGURATION_FORMAT = "rubato-plaits-palette-configuration"
EXPORT_FORMAT = "rubato.plaits-palette-source-export/v1"
BUILDER = Path(__file__).resolve().parent
REPOSITORY = BUILDER.parents[1]
CATALOG_PATH = REPOSITORY / "alt_firmwares/plaits_lab_catalog/catalog.json"
STOCK_LINKER_SCRIPT = (
    REPOSITORY / "stmlib/linker_scripts/stm32f373x_flash_application_large.ld"
)


def _unwrap_recipe(document: Any) -> tuple[dict[str, Any], str | None]:
    """Accept either a bare builder recipe or the browser's named envelope."""
    if not isinstance(document, dict):
        raise ValueError("the input must be a Plaits Palette JSON object")
    if document.get("format") != CONFIGURATION_FORMAT:
        return document, None
    if document.get("version") != 1 or not isinstance(document.get("recipe"), dict):
        raise ValueError("the Plaits Palette configuration file is not supported")
    name = document.get("name")
    return document["recipe"], name if isinstance(name, str) else None


def _git_source_state(repository: Path) -> tuple[str, bool]:
    def git(*arguments: str) -> str:
        result = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()

    try:
        revision = git("rev-parse", "HEAD")
        dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(
            "run this exporter from a Git checkout so it can pin the firmware source revision"
        ) from error
    return revision, dirty


def _selected_models(recipe: Any) -> list[dict[str, Any]]:
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    by_id = {engine["id"]: engine for engine in catalog["engines"]}
    selected: list[dict[str, Any]] = []
    seen: set[str] = set()
    for engine_id in recipe.public_slots:
        if engine_id is None or engine_id in seen:
            continue
        seen.add(engine_id)
        engine = by_id[engine_id]
        source = engine["source"]
        selected.append({
            "id": engine_id,
            "name": engine["name"],
            "author": engine["author"],
            "header": source["header"],
            "files": source.get("files", []),
        })
    return selected


def _source_map(models: list[dict[str, Any]]) -> str:
    sections = [
        "# Selected model sources\n",
        "These are the model-specific files selected by the recipe. Paths are relative "
        "to the pinned eurorack checkout. Their includes lead to any shared DSP code "
        "they use.\n",
    ]
    for model in models:
        paths = [model["header"], *model["files"]]
        sections.append(
            f"## {model['name']} (`{model['id']}`)\n\n"
            f"Author: {model['author']}\n\n"
            + "".join(f"- `{path}`\n" for path in paths)
        )
    return "\n".join(sections)


def _shell_script(recipe: Any, has_linker_script: bool) -> str:
    variables = [
        '"BUILD_ROOT=$build_root"',
        '"ENGINE_CONFIG=$export_dir/engine_config.h"',
        '"SPEECH_CONFIG=$export_dir/speech_config.h"',
        f'"SPEECH_BANKS_ENABLED={1 if recipe.speech_banks is not None else 0}"',
        '"CC=arm-none-eabi-gcc"',
        '"CXX=arm-none-eabi-g++"',
        "DEPS=",
    ]
    if has_linker_script:
        variables.append('"LINKER_SCRIPT=$export_dir/plaits_user_banks.ld"')
    variables.extend(
        shlex.quote(flag)
        for flag in _stereo_disable_flags(
            _recipe_is_stereo(recipe), recipe.stereo_engines
        )
    )
    joined = " \\\n  ".join(variables)
    return f"""#!/bin/sh
set -eu

export_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=${{PLAITS_REPO:-}}
if [ -z "$repo" ]; then
  repo=$(git -C "$export_dir" rev-parse --show-toplevel 2>/dev/null || true)
fi
if [ ! -f "$repo/plaits/makefile" ]; then
  echo "Set PLAITS_REPO to the pinned lylepmills/eurorack checkout." >&2
  exit 2
fi

target=${{1:-wav}}
case "$target" in
  wav|hex|bin) ;;
  *) echo "Usage: $0 [wav|hex|bin]" >&2; exit 2 ;;
esac

build_root=${{PLAITS_BUILD_ROOT:-$export_dir/build/}}
jobs=${{PLAITS_JOBS:-4}}

make -C "$repo" -f plaits/makefile \\
  {joined} \\
  "-j$jobs" "$target"

python3 "$repo/alt_firmwares/plaits_lab_builder/validate_local_build.py" \\
  "$export_dir/engine_config.h" \\
  "${{build_root%/}}/plaits/plaits.elf"
"""


def _readme(
    revision: str,
    dirty: bool,
    has_linker_script: bool,
    recipe_name: str | None,
) -> str:
    dirty_note = (
        "\n> Warning: the exporter was run from a checkout with uncommitted tracked "
        "changes. The commit below is the baseline, but those changes are not contained "
        "in this overlay. Commit or separately preserve them if they matter.\n"
        if dirty else ""
    )
    linker_note = (
        "- `plaits_user_banks.ld` — recipe-specific flash placement for replaceable FM banks and prebaked terrains\n"
        if has_linker_script else ""
    )
    safe_name = " ".join(recipe_name.split()).replace("`", "'") if recipe_name else ""
    name_note = f"Configuration name: `{safe_name}`\n\n" if safe_name else ""
    return f"""# Plaits Palette local source build

{name_note}This folder is the build-specific layer for one Plaits Palette recipe. The shared
firmware and model source stays in the eurorack repository; this overlay pins the
exact starting commit and contains all generated configuration and custom data.

> **Hack at your own risk.** Once you alter any code, you are responsible for
> the result. Modified firmware can brick your module or require a hardware
> programmer to recover it. Rubato Audio is not responsible for damage, data
> loss, or an unbootable module caused by modified firmware.

Source: {REPOSITORY_URL}/tree/{revision}

Commit: `{revision}`
{dirty_note}
## What is here

- `recipe.json` — the complete build recipe
- `engine_config.h` — selected models, layout, options, chord/scale data, and FM banks
- `speech_config.h` — selected and custom LPC Speech-bank data
{linker_note}- `selected-models.md` — the model-specific source files to start hacking
- `build-metadata.json` — source, toolchain, contract, and recipe identity
- `build.sh` — the hosted compiler's recipe flags and post-link safety gates

## 1. Check out the pinned source

```sh
git clone --recurse-submodules {REPOSITORY_URL}.git eurorack
cd eurorack
git checkout {revision}
git submodule update --init stmlib stm_audio_bootloader
```

Keep this export inside that checkout—`build/my-palette-source/` is a convenient
ignored location—or set `PLAITS_REPO` when building from somewhere else.

## 2. Build with the original ARM toolchain

The repository's development container installs the same GCC 4.8.3 toolchain
used by the hosted builder. Build it once from the repository root:

```sh
docker build --platform linux/amd64 \\
  -t mutable-eurorack-dev:local \\
  -f .devcontainer/Dockerfile .
```

If this folder is `build/my-palette-source/`, compile the audio updater with:

```sh
docker run --rm --platform linux/amd64 \\
  -v "$PWD":/workspace -w /workspace \\
  mutable-eurorack-dev:local \\
  sh build/my-palette-source/build.sh wav
```

Use `hex` instead of `wav` for an application-only Intel HEX file. Output lands
under this folder's `build/plaits/` directory.

If `arm-none-eabi-gcc` 4.8.3 is already on `PATH`, Docker is optional:

```sh
PLAITS_REPO=/path/to/eurorack ./build.sh wav
```

## 3. Hack on it

Edit the model and shared firmware sources in the pinned eurorack checkout.
`selected-models.md` points to the model-specific starting files. Edit the
generated headers only when you deliberately want to change this recipe's baked
layout or data; regenerating the export will replace those changes.

`build.sh` uses the generated headers and the recipe's exact stereo, Speech, and
linker settings. After linking, it rejects firmware that exceeds Plaits' flash
or RAM limits, or gives replaceable FM banks an unsafe flash layout. These gates
catch known structural hazards; they cannot prove that altered DSP or control
code is safe. Running `make` directly bypasses them.

The script does not contact Rubato Audio and it does not upload source.

The hosted artifact can still differ if this export was made from a different
source commit or with local changes. For byte comparison, use the commit and
toolchain recorded in `build-metadata.json`.
"""


def export_recipe_source(
    document: Any,
    output: Path,
    *,
    source_revision: str,
    source_dirty: bool,
) -> list[str]:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory is not empty: {output}")

    recipe_document, recipe_name = _unwrap_recipe(document)
    recipe = validate_recipe(recipe_document)
    engine_config = render_config(recipe)
    speech_config = render_speech_config(recipe.speech_banks)
    region_count = _declared_region_count(engine_config)
    linker_script = None
    if region_count:
        linker_script = render_linker_script(
            STOCK_LINKER_SCRIPT.read_text(encoding="utf-8")
        )

    normalized_json = json.dumps(
        recipe_document, indent=2, sort_keys=True, ensure_ascii=False
    ) + "\n"
    recipe_sha256 = hashlib.sha256(
        json.dumps(
            recipe_document, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
    ).hexdigest()
    models = _selected_models(recipe)
    metadata = {
        "format": EXPORT_FORMAT,
        "firmwareRepository": REPOSITORY_URL,
        "sourceRevision": source_revision,
        "sourceTreeDirty": source_dirty,
        "toolchain": TOOLCHAIN_ID,
        "buildContract": BUILD_CONTRACT_VERSION,
        "recipeSchemaVersion": recipe_document["schemaVersion"],
        "recipeSha256": recipe_sha256,
        "selectedEngineIds": [model["id"] for model in models],
    }

    files = {
        "README.md": _readme(
            source_revision, source_dirty, linker_script is not None, recipe_name
        ),
        "recipe.json": normalized_json,
        "engine_config.h": engine_config,
        "speech_config.h": speech_config,
        "selected-models.md": _source_map(models),
        "build-metadata.json": json.dumps(metadata, indent=2) + "\n",
        "build.sh": _shell_script(recipe, linker_script is not None),
    }
    if linker_script is not None:
        files["plaits_user_banks.ld"] = linker_script

    output.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (output / name).write_text(content, encoding="utf-8")
    os.chmod(output / "build.sh", 0o755)
    return sorted(files)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "recipe", type=Path,
        help="a .plaits-palette.json configuration or bare builder recipe",
    )
    parser.add_argument("output", type=Path, help="a new or empty output directory")
    args = parser.parse_args()

    try:
        document = json.loads(args.recipe.read_text(encoding="utf-8"))
        revision, dirty = _git_source_state(REPOSITORY)
        files = export_recipe_source(
            document,
            args.output,
            source_revision=revision,
            source_dirty=dirty,
        )
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))
    print(f"Exported {len(files)} files to {args.output}")


if __name__ == "__main__":
    main()
