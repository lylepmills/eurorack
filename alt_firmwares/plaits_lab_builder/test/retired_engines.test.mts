import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import {
  approvedEngineIds,
  normalizeRecipe,
  resolveEngineIdIn,
  retiredEngines,
} from "../src/contract.ts";

// catalog.json retiredEngines: a withdrawn engine id builds its replacement once
// the catalog no longer carries it. The Worker's catalog is fixed at import, so
// the removal is exercised through the pure resolver with synthetic sets, and
// normalizeRecipe is checked for the current, both-approved state.

const fixture = JSON.parse(await readFile(new URL("../default_recipe.json", import.meta.url), "utf8"));

test("the Worker carries the catalog's retired-engine map", () => {
  assert.deepEqual(retiredEngines, {
    "virtual-analog-dual": "virtual-analog-variant",
    "virtual-analog-crossfade": "virtual-analog-variant",
  });
  for (const replacement of Object.values(retiredEngines)) {
    assert.ok(approvedEngineIds.includes(replacement), `${replacement} must be approved`);
  }
});

test("resolution: approved ids build as themselves, removed ones as their replacement", () => {
  const retired = { old: "new" };
  const both = new Set(["old", "new"]);
  const afterRemoval = new Set(["new"]);
  assert.equal(resolveEngineIdIn(both, retired, "old"), "old");
  assert.equal(resolveEngineIdIn(afterRemoval, retired, "old"), "new");
  assert.equal(resolveEngineIdIn(afterRemoval, retired, "new"), "new");
  // Nothing to fall back to: unknown ids, and a replacement that isn't approved.
  assert.equal(resolveEngineIdIn(afterRemoval, retired, "other"), undefined);
  assert.equal(resolveEngineIdIn(new Set(["x"]), retired, "old"), undefined);
  // Prototype keys are not ids.
  assert.equal(resolveEngineIdIn(afterRemoval, retired, "toString"), undefined);
});

test("while both are approved, a retired engine still builds as itself", () => {
  const recipe = structuredClone(fixture);
  recipe.slots[0] = "virtual-analog-dual";
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.slots[0], "virtual-analog-dual");
});
