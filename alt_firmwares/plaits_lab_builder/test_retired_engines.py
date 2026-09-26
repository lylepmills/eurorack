"""Retired engine ids build their replacement (catalog.json retiredEngines).

While a retired engine is still in the catalog it builds as itself; once it is
removed, a recipe that still names it builds the replacement instead of being
rejected. The removal is simulated here by taking the engine out of the loaded
catalog.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from unittest import mock

import generate_engine_config as gec


FIXTURES = Path(__file__).parent
RETIRED = "virtual-analog-dual"
REPLACEMENT = "virtual-analog-variant"


def without(engine_id: str):
    catalog = {k: v for k, v in gec.CATALOG.items() if k != engine_id}
    public = {k: v for k, v in gec.PUBLIC_ENGINES.items() if k != engine_id}
    return (mock.patch.object(gec, "CATALOG", catalog),
            mock.patch.object(gec, "PUBLIC_ENGINES", public))


class RetiredEnginesTest(unittest.TestCase):
    def test_the_catalog_maps_both_va_models_to_the_variant(self) -> None:
        self.assertEqual(gec.RETIRED_ENGINES, {
            "virtual-analog-dual": REPLACEMENT,
            "virtual-analog-crossfade": REPLACEMENT,
        })

    def test_a_retired_engine_still_in_the_catalog_builds_as_itself(self) -> None:
        self.assertEqual(gec.normalize_slots([RETIRED], 7), [RETIRED])

    def test_a_removed_engine_builds_its_replacement(self) -> None:
        catalog, public = without(RETIRED)
        with catalog, public:
            self.assertEqual(gec.normalize_slots([RETIRED, None], 7), [REPLACEMENT, None])
            self.assertEqual(gec.normalize_slots([RETIRED], 2), [REPLACEMENT])
            reference = {"engine": RETIRED, "package": "stale.package",
                         "version": "1.0.0", "digest": "sha256:" + "0" * 64}
            self.assertEqual(gec.normalize_slots([reference], 12), [REPLACEMENT])

    def test_an_unknown_engine_is_still_rejected(self) -> None:
        with self.assertRaises(ValueError):
            gec.normalize_slots(["no-such-engine"], 7)
        with self.assertRaises(ValueError):
            gec.normalize_slots(["no-such-engine"], 2)

    def test_a_whole_recipe_naming_a_removed_engine_renders(self) -> None:
        recipe = json.loads((FIXTURES / "default_recipe.json").read_text(encoding="utf-8"))
        recipe["slots"][0] = RETIRED
        catalog, public = without(RETIRED)
        with catalog, public:
            config = gec.render_config(gec.validate_recipe(recipe))
        self.assertIn(gec.CATALOG[REPLACEMENT].class_name, config)


if __name__ == "__main__":
    unittest.main()
