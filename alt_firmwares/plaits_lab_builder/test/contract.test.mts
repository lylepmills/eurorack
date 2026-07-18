import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { approvedEngineIds, computeBuildKey, normalizeRecipe } from "../src/contract.ts";

const fixture = JSON.parse(await readFile(new URL("../default_recipe.json", import.meta.url), "utf8"));

test("the Worker and compiler catalogs contain the same approved IDs", async () => {
  const compilerCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/catalog.json", import.meta.url),
    "utf8",
  ));
  assert.deepEqual(
    approvedEngineIds,
    compilerCatalog.engines.map((engine: { id: string }) => engine.id),
  );
  assert.equal(approvedEngineIds.length, 39);
});

test("normalization removes nondeterministic manifest fields", () => {
  const normalized = normalizeRecipe({ ...fixture, createdAt: "2099-01-01T00:00:00Z" });
  assert.equal("createdAt" in normalized, false);
  assert.equal(normalized.schemaVersion, 5);
  assert.deepEqual(normalized.slots, fixture.slots);
  assert.equal(normalized.preferences.navigationMode, "linear");
  assert.equal(normalized.initialOptions.lockedFrequencyKnob, "octaves");
});

test("build keys are stable and include the build identity", async () => {
  const recipe = normalizeRecipe(fixture);
  const identity = { sourceRevision: "source-a", toolchain: "toolchain-a", contract: "1" };
  const first = await computeBuildKey(recipe, identity);
  const second = await computeBuildKey(recipe, identity);
  const changed = await computeBuildKey(recipe, { ...identity, sourceRevision: "source-b" });
  assert.equal(first, second);
  assert.match(first, /^[0-9a-f]{64}$/);
  assert.notEqual(first, changed);
});

test("unknown engine IDs never reach the queue", () => {
  const input = structuredClone(fixture);
  input.slots[3] = "../../untrusted-source";
  assert.throws(() => normalizeRecipe(input), /not approved/);
});

test("versioned package references are checked against immutable digests", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const versioned = {
    ...fixture,
    schemaVersion: 3,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
  };
  const normalized = normalizeRecipe(versioned);
  assert.equal(normalized.schemaVersion, 5);
  assert.deepEqual(normalized.slots, fixture.slots);
  versioned.slots[0].digest = "sha256:" + "0".repeat(64);
  assert.throws(() => normalizeRecipe(versioned), /unavailable package version/);
});

test("version 4 firmware preferences are validated and affect build identity", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const configured = {
    ...fixture,
    schemaVersion: 4,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "banked" },
    initialOptions: {
      lockedFrequencyKnob: "macro-4",
      modelInput: "aux-crossfade",
      levelInput: "decay",
      auxOutput: "square-subosc",
      suboscillatorOctave: -2,
      chordTable: "jon-butler",
      holdOnTrigger: true,
    },
  };
  const normalized = normalizeRecipe(configured);
  assert.equal(normalized.preferences.navigationMode, "banked");
  assert.equal(normalized.initialOptions.suboscillatorOctave, -2);

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "2" };
  const customKey = await computeBuildKey(normalized, identity);
  const defaultKey = await computeBuildKey(normalizeRecipe(fixture), identity);
  assert.notEqual(customKey, defaultKey);

  configured.preferences.navigationMode = "sideways";
  assert.throws(() => normalizeRecipe(configured), /unsupported firmware option/);
  configured.preferences.navigationMode = "banked";
  Object.assign(configured.preferences, { compilerFlag: "-DUNTRUSTED" });
  assert.throws(() => normalizeRecipe(configured), /unsupported firmware option/);
});

test("version 5 accepts bounded local chord-table edits and hashes their musical data", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const localTable = structuredClone(chordCatalog.tables[0]);
  Object.assign(localTable, {
    id: "original-draft-1",
    packageId: "local/original-draft-1",
    version: "draft",
    digest: null,
    name: "Original edit",
    author: "You",
    origin: "Local",
    description: "A local table edit.",
  });
  const recipe = {
    ...fixture,
    schemaVersion: 5,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves",
      modelInput: "model",
      levelInput: "level",
      auxOutput: "alternate-model",
      suboscillatorOctave: 0,
      chordTable: localTable.id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [localTable] },
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.resources.chordTables[0].chords[0].voices[1], 1);
  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "3" };
  const first = await computeBuildKey(normalized, identity);
  recipe.resources.chordTables[0].chords[0].voices[1] = 9;
  const changed = await computeBuildKey(normalizeRecipe(recipe), identity);
  assert.notEqual(first, changed);

  recipe.resources.chordTables[0].chords[0].voices[1] = 9000;
  assert.throws(() => normalizeRecipe(recipe), /bounded cent offsets/);
});
