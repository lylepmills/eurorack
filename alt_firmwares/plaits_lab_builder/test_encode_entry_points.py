"""Every encode entry point must survive BOTH synthesis engines.

Speech has two encode entry points -- word banks and Natural Speech -- and they
are separate consumers of the same `preview_artifacts` engine. Natural Speech is
the one that gets forgotten: it read `manifest["publishedVoiceSha256"]`, a field
only Kokoro records, so widening the catalog to Piper broke EVERY Piper voice in
that panel in production, reported as a generic "could not be encoded".

The engines genuinely disagree about manifest shape, so this is not a one-time
slip -- any new entry point can make it again. These tests therefore check the
CONTRACT rather than one call site: both engine shapes go through the shared
helper without raising, and every encode entry point actually uses it.
"""

from __future__ import annotations

import unittest
from pathlib import Path

BUILDER_DIR = Path(__file__).parent

# preview_artifacts pulls numpy, which the structural tests below do not need.
# They are the ones that stop a THIRD entry point repeating the bug, so they
# must run everywhere -- including a bare checkout with no synthesis deps.
# Only the behavioural tests are skipped when numpy is absent.
try:
    from preview_artifacts import (
        SYNTHESIS_PROVENANCE_FIELDS,
        synthesis_provenance,
    )
except ModuleNotFoundError as exc:  # pragma: no cover - env-dependent
    SYNTHESIS_PROVENANCE_FIELDS = ()
    synthesis_provenance = None
    _IMPORT_ERROR: str | None = str(exc)
else:
    _IMPORT_ERROR = None

# Mirrors what each engine records in preview_artifacts.py. The engines are not
# instantiated here on purpose: Piper needs a ~60-120 MB ONNX model on disk, and
# a test that only runs where the models are baked is a test that never runs.
KOKORO_MANIFEST = {
    "engine": "kokoro",
    "repository": "hexgrad/Kokoro-82M",
    "publishedModelSha256": "4" * 64,
    "publishedVoiceSha256": "a" * 64,
}

PIPER_MANIFEST = {
    "engine": "piper",
    "model": "de_DE-thorsten-medium",
    "speaker": 0,
    "publishedModelSha256": "b" * 64,
    "trimTokenEdgesApplied": False,
}


def encode_entry_points() -> list[Path]:
    found = sorted(BUILDER_DIR.glob("encode_*.py"))
    assert found, "no encode_*.py entry points found -- glob is wrong"
    return found


@unittest.skipIf(_IMPORT_ERROR, f"preview_artifacts unavailable: {_IMPORT_ERROR}")
class SynthesisProvenanceTest(unittest.TestCase):
    def test_kokoro_manifest_round_trips(self):
        self.assertEqual(synthesis_provenance(KOKORO_MANIFEST), KOKORO_MANIFEST)

    def test_piper_manifest_round_trips(self):
        """The regression: Piper has no voice hash, and must not raise for it."""
        self.assertEqual(synthesis_provenance(PIPER_MANIFEST), PIPER_MANIFEST)
        self.assertNotIn("publishedVoiceSha256", synthesis_provenance(PIPER_MANIFEST))

    def test_drops_fields_the_manifest_schema_does_not_carry(self):
        noisy = dict(PIPER_MANIFEST, cacheKey="deadbeef", elapsedMs=1234)
        carried = synthesis_provenance(noisy)
        self.assertEqual(carried, PIPER_MANIFEST)
        self.assertNotIn("cacheKey", carried)

    def test_an_empty_manifest_is_survivable(self):
        self.assertEqual(synthesis_provenance({}), {})

    def test_every_declared_field_is_carried_when_present(self):
        every = {k: "x" for k in SYNTHESIS_PROVENANCE_FIELDS}
        self.assertEqual(synthesis_provenance(every), every)


class EncodeEntryPointTest(unittest.TestCase):
    """Structural guards, so a THIRD entry point cannot repeat the bug."""

    def test_every_entry_point_uses_the_shared_helper(self):
        for path in encode_entry_points():
            with self.subTest(entry_point=path.name):
                source = path.read_text(encoding="utf-8")
                if '"synthesis"' not in source:
                    continue  # does not emit a synthesis block; nothing to carry
                # assertTrue on a bool, not assertIn on the source: a failing
                # assertIn prints the whole file, which buries the one line.
                self.assertTrue(
                    "synthesis_provenance(" in source,
                    f"{path.name} builds a synthesis block without calling "
                    "synthesis_provenance() -- it will break on one of the "
                    "two engines, whichever one it did not have in mind")

    def test_no_entry_point_reads_an_engine_specific_field_directly(self):
        """The exact shape of the shipped bug: a Kokoro-only key, subscripted."""
        engine_specific = ("publishedVoiceSha256", "repository", "speaker", "model")
        for path in encode_entry_points():
            source = path.read_text(encoding="utf-8")
            lines = source.splitlines()
            for field in engine_specific:
                subscripts = [f'["{field}"]', f"['{field}']"]
                hits = [
                    f"  line {n}: {line.strip()}"
                    for n, line in enumerate(lines, 1)
                    if any(sub in line for sub in subscripts)
                ]
                with self.subTest(entry_point=path.name, field=field):
                    # Report the offending LINES, not the file: this assertion
                    # fires on an 8 KB module and the location is the whole
                    # point of the message.
                    self.assertFalse(
                        hits,
                        f"{path.name} subscripts {field!r}, which only one "
                        f"engine records -- use synthesis_provenance():\n"
                        + "\n".join(hits))


if __name__ == "__main__":
    unittest.main()
