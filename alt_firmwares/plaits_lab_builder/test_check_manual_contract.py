#!/usr/bin/env python3
"""Tests for the manual-contract deploy gate.

Each test builds a throwaway git repo holding render_manual.py at the path the
checker reads, so the "what did the deployed image's renderer require" question
is answered from real commits rather than a stub.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_manual_contract as guard

RENDERER_DIR = "alt_firmwares/plaits_lab_builder"


def make_repo(directory: Path, contracts: list[int | None]) -> list[str]:
    """Commit render_manual.py once per entry; None means the constant is absent.

    Returns the resulting commit SHAs, so a test can point a config at the exact
    revision whose renderer it wants to check against.
    """
    run = lambda *args: subprocess.run(  # noqa: E731 - terse on purpose in a fixture
        args, cwd=directory, check=True, capture_output=True, text=True)
    run("git", "init", "-q")
    run("git", "config", "user.email", "test@example.com")
    run("git", "config", "user.name", "Test")
    renderer = directory / RENDERER_DIR / "render_manual.py"
    renderer.parent.mkdir(parents=True, exist_ok=True)
    shas = []
    for index, contract in enumerate(contracts):
        # The index keeps consecutive commits distinct even when they declare the
        # same contract, which is the ordinary case: prose changes far more often
        # than the contract does.
        body = f'"""Renderer, revision {index}."""\n\nBANKS = ()\n'
        if contract is not None:
            body += f"\nMANUAL_CONTRACT = {contract}\n"
        renderer.write_text(body)
        run("git", "add", "-A")
        run("git", "commit", "-q", "-m", f"rev {index}")
        shas.append(run("git", "rev-parse", "HEAD").stdout.strip())
    return shas


def config(revision: str, contract: str, *, image_revision: str | None = None,
           staging: tuple[str, str] | None = None) -> dict:
    image = image_revision if image_revision is not None else revision
    block = {
        "vars": {"PLAITS_SOURCE_REVISION": revision, "PLAITS_MANUAL_CONTRACT": contract},
        "containers": [{"image": f"registry.example.com/acct/builder:rev-{image}"}],
    }
    if staging:
        block["env"] = {"staging": {
            "vars": {"PLAITS_SOURCE_REVISION": staging[0], "PLAITS_MANUAL_CONTRACT": staging[1]},
            "containers": [{"image": f"registry.example.com/acct/builder:rev-{staging[0]}"}],
        }}
    return block


class ManualContractGateTest(unittest.TestCase):
    def check(self, repo: Path, cfg: dict) -> list[str]:
        problems = []
        for label, block in guard.environments(cfg):
            problems.extend(guard.check_environment(label, block, repo))
        return problems

    def test_contract_below_the_deployed_renderer_is_refused(self) -> None:
        """The failure this gate exists for: new prose shipped, contract left behind."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            problems = self.check(repo, config(shas[0], "20"))
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("requires 21", problems[0])
            self.assertIn("Set PLAITS_MANUAL_CONTRACT to 21", problems[0])

    def test_contract_matching_the_deployed_renderer_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            self.assertEqual(self.check(repo, config(shas[0], "21")), [])

    def test_a_contract_ahead_of_the_renderer_passes(self) -> None:
        """Running the Worker ahead is not this gate's business — only behind is."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            self.assertEqual(self.check(repo, config(shas[0], "22")), [])

    def test_a_landed_but_unshipped_renderer_constrains_nothing(self) -> None:
        """The contract-14 precedent: renderer at 21 in the tree, image still on the
        commit that required 20, Worker on 20. That is the intended waiting state and
        must deploy cleanly — an equality check would wrongly block it."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [20, 21])  # HEAD requires 21; the deployed image does not
            self.assertEqual(self.check(repo, config(shas[0], "20")), [])

    def test_a_revision_predating_the_constant_requires_nothing(self) -> None:
        """Historical rollouts must not fail retroactively."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [None])
            self.assertEqual(self.check(repo, config(shas[0], "20")), [])

    def test_staging_and_production_are_checked_independently(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [20, 21])
            # Production is fine on the older image; staging moved ahead and did not
            # bring its contract with it.
            problems = self.check(repo, config(shas[0], "20", staging=(shas[1], "20")))
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("env.staging", problems[0])
            self.assertIn("requires 21", problems[0])

    def test_an_image_tag_naming_another_commit_is_refused(self) -> None:
        """The README's stamped-stale-revision incident, caught before it ships."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21, 21])
            problems = self.check(repo, config(shas[1], "21", image_revision=shas[0]))
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("container image is built from", problems[0])

    def test_an_untagged_image_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            cfg = config(shas[0], "21")
            cfg["containers"][0]["image"] = "registry.example.com/acct/builder:latest"
            problems = self.check(repo, cfg)
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("does not end in :rev-", problems[0])

    def test_an_unresolvable_revision_stops_the_deploy(self) -> None:
        """Fail closed: an unfetchable revision means nobody can say what runs."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            make_repo(repo, [21])
            with self.assertRaises(guard.CheckError) as caught:
                self.check(repo, config("0" * 40, "21"))
            self.assertIn("cannot read", str(caught.exception))

    def test_half_configured_environment_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            cfg = config(shas[0], "21")
            del cfg["vars"]["PLAITS_MANUAL_CONTRACT"]
            problems = self.check(repo, cfg)
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("set together", problems[0])

    def test_a_non_numeric_contract_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            shas = make_repo(repo, [21])
            problems = self.check(repo, config(shas[0], "twenty-one"))
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("is not a number", problems[0])


class JsoncParsingTest(unittest.TestCase):
    """wrangler.jsonc carries comments, and its values contain // and /* */."""

    def test_comments_are_stripped_without_touching_strings(self) -> None:
        text = """{
          // a line comment
          "a": "https://example.com/path", /* trailing block */
          "b": "keeps /* this */ and // this",
          "c": "a \\" quote then // not a comment"
        }"""
        parsed = json.loads(guard.strip_jsonc(text))
        self.assertEqual(parsed["a"], "https://example.com/path")
        self.assertEqual(parsed["b"], "keeps /* this */ and // this")
        self.assertEqual(parsed["c"], 'a " quote then // not a comment')

    def test_the_real_config_parses_and_has_both_environments(self) -> None:
        cfg = guard.load_config(guard.BUILDER_DIR / "wrangler.jsonc")
        labels = [label for label, _ in guard.environments(cfg)]
        self.assertIn("production (top level)", labels)
        self.assertIn("env.staging", labels)


class DeclaredContractTest(unittest.TestCase):
    def test_the_renderer_declares_a_contract(self) -> None:
        """Without this constant the gate silently permits everything."""
        source = (guard.BUILDER_DIR / "render_manual.py").read_text()
        match = guard.CONTRACT_PATTERN.search(source)
        self.assertIsNotNone(match, "render_manual.py must declare MANUAL_CONTRACT")
        # A floor, not a pin: it only ever rises. 22 is the LIGHT 4 TRIG
        # articulation prose, which is the newest change the guide prints.
        self.assertGreaterEqual(int(match.group(1)), 22)


if __name__ == "__main__":
    unittest.main()
