#!/usr/bin/env python3
"""Refuse a deploy whose manual contract is behind the renderer it will use.

`computeManualKey` deliberately leaves the firmware source revision out of the
guide cache key, so firmware rollouts reuse cached PDFs. That makes
`PLAITS_MANUAL_CONTRACT` the ONLY signal that the renderer's output changed. Get
it wrong in the quiet direction — ship an image with new prose but leave the
contract alone — and every already-rendered recipe keeps serving the old PDF
under an unchanged key, while recipes nobody has built yet render correctly. The
firmware is right either way, so the symptom is a guide that contradicts the
module, on some configurations and not others, with nothing to tell them apart.

The rule this enforces is NOT "renderer contract == Worker contract". The
renderer is meant to run ahead: the README's contract-14 precedent lands a
renderer and its tests while the Worker stays a version back, because moving the
Worker first would point it at prose the deployed container cannot produce. What
must hold is narrower:

    the Worker's PLAITS_MANUAL_CONTRACT must be at least the MANUAL_CONTRACT
    declared by render_manual.py AT THE COMMIT THE DEPLOYED IMAGE WAS BUILT FROM

which is `PLAITS_SOURCE_REVISION` — the same commit named in the container image
tag. So a renderer that has landed but not shipped constrains nothing, and the
moment the revision moves to a commit carrying it, the contract must move too.

Every environment in wrangler.jsonc is checked independently, since staging and
production advance separately.

Usage:  python3 check_manual_contract.py [--config wrangler.jsonc]
Exit 0 when every environment is coherent, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

BUILDER_DIR = Path(__file__).resolve().parent
RENDERER_REPO_PATH = "alt_firmwares/plaits_lab_builder/render_manual.py"
CONTRACT_PATTERN = re.compile(r"^MANUAL_CONTRACT\s*=\s*(\d+)\s*$", re.MULTILINE)
IMAGE_REVISION_PATTERN = re.compile(r":rev-([0-9a-f]+)\s*$")


class CheckError(Exception):
    """A problem that must stop the deploy."""


def strip_jsonc(text: str) -> str:
    """Drop // and /* */ comments, leaving string literals untouched."""
    out = []
    i = 0
    in_string = False
    while i < len(text):
        char = text[i]
        if in_string:
            out.append(char)
            if char == "\\" and i + 1 < len(text):
                out.append(text[i + 1])
                i += 2
                continue
            if char == '"':
                in_string = False
            i += 1
            continue
        if char == '"':
            in_string = True
            out.append(char)
            i += 1
            continue
        if text.startswith("//", i):
            i = text.find("\n", i)
            if i == -1:
                break
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end == -1 else end + 2
            continue
        out.append(char)
        i += 1
    return "".join(out)


def load_config(path: Path) -> dict:
    return json.loads(strip_jsonc(path.read_text()))


def environments(config: dict) -> list[tuple[str, dict]]:
    """The top-level environment plus each named one, as (label, block)."""
    found = [("production (top level)", config)]
    for name, block in (config.get("env") or {}).items():
        found.append((f"env.{name}", block))
    return found


def image_revisions(block: dict) -> list[str]:
    revisions = []
    for container in block.get("containers") or []:
        image = container.get("image")
        if not isinstance(image, str):
            continue
        match = IMAGE_REVISION_PATTERN.search(image)
        revisions.append(match.group(1) if match else "")
    return revisions


def renderer_contract_at(revision: str, repo_dir: Path = BUILDER_DIR) -> int:
    """MANUAL_CONTRACT declared by render_manual.py at `revision`.

    A revision predating the constant declares no requirement and returns 0, so
    this starts constraining from the commit that introduced it rather than
    retroactively failing every historical rollout.
    """
    try:
        source = subprocess.run(
            ["git", "show", f"{revision}:{RENDERER_REPO_PATH}"],
            cwd=repo_dir,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    except FileNotFoundError as error:  # pragma: no cover - git is always present in practice
        raise CheckError("git is not available, so the deployed renderer cannot be read.") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or "").strip().splitlines()
        raise CheckError(
            f"cannot read {RENDERER_REPO_PATH} at revision {revision}"
            + (f" ({detail[-1]})" if detail else "")
            + ".\n"
            "    That revision is what the deployed image was built from, so it must be a\n"
            "    commit this checkout has. Fetch it (git fetch origin) and try again. Do not\n"
            "    skip this check: an unfetchable revision means nobody here can say what the\n"
            "    running container renders."
        ) from error
    match = CONTRACT_PATTERN.search(source)
    return int(match.group(1)) if match else 0


def check_environment(label: str, block: dict, repo_dir: Path = BUILDER_DIR) -> list[str]:
    problems = []
    variables = block.get("vars") or {}
    revision = variables.get("PLAITS_SOURCE_REVISION")
    contract = variables.get("PLAITS_MANUAL_CONTRACT")

    if not revision or not contract:
        # An env block that overrides neither inherits the top level, which is
        # checked in its own right.
        if revision or contract:
            problems.append(
                f"{label}: sets only one of PLAITS_SOURCE_REVISION / PLAITS_MANUAL_CONTRACT. "
                "They describe one deployment and have to be set together."
            )
        return problems

    # The image tag names the same commit as the var. The README records a
    # rollout where they diverged and every artifact was stamped with a stale
    # revision, so a mismatch here is a known, expensive failure.
    for image_revision in image_revisions(block):
        if not image_revision:
            problems.append(
                f"{label}: a container image tag does not end in :rev-<commit>, so the "
                "revision it carries cannot be verified against PLAITS_SOURCE_REVISION."
            )
        elif image_revision != revision:
            problems.append(
                f"{label}: PLAITS_SOURCE_REVISION is {revision} but the container image is "
                f"built from {image_revision}. The Worker would record a revision the "
                "container never compiled."
            )

    required = renderer_contract_at(revision, repo_dir)
    try:
        deployed = int(contract)
    except ValueError:
        problems.append(f"{label}: PLAITS_MANUAL_CONTRACT {contract!r} is not a number.")
        return problems

    if deployed < required:
        problems.append(
            f"{label}: PLAITS_MANUAL_CONTRACT is {deployed}, but render_manual.py at "
            f"{revision} — the commit this deployment's image is built from — requires "
            f"{required}.\n"
            f"    That renderer prints a guide the contract does not describe, so every "
            "already-rendered\n"
            f"    recipe would keep serving its cached PDF from before the change while new "
            "ones render\n"
            f"    correctly. Set PLAITS_MANUAL_CONTRACT to {required} in this environment."
        )
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", default=str(BUILDER_DIR / "wrangler.jsonc"))
    parser.add_argument("--repo", default=None, help="Checkout to resolve PLAITS_SOURCE_REVISION in (tests).")
    args = parser.parse_args(argv)

    try:
        config = load_config(Path(args.config))
        problems = []
        for label, block in environments(config):
            problems.extend(check_environment(label, block, Path(args.repo or BUILDER_DIR)))
    except CheckError as error:
        print(f"manual-contract check failed: {error}", file=sys.stderr)
        return 1

    if problems:
        print("manual-contract check failed:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("manual-contract check passed: every environment's contract covers its renderer.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
