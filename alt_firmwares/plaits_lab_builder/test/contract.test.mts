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

test("version 6 carries a validated custom FM bank and hashes its packed bytes", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const publishedTable = structuredClone(chordCatalog.tables[0]);
  const makeBank = () => ({
    id: "warm-keys", packageId: "anon/warm-keys", version: "1.0.0", digest: null,
    name: "Warm Keys", author: "You", license: "CC0-1.0", origin: "Community",
    description: "Custom keys.",
    voices: Array.from({ length: 32 }, (_unused, i) => ({ name: `V${i}`, algorithm: 1, packed: Array(128).fill(0) })),
  });
  const makeRecipe = (userDataBanks: unknown[]) => ({
    ...fixture,
    schemaVersion: 6,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id, holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable], userDataBanks },
  });

  const bank = makeBank();
  const normalized = normalizeRecipe(makeRecipe([{ index: 0, bank }]));
  assert.equal(normalized.schemaVersion, 6);
  assert.equal(normalized.resources.userDataBanks!.length, 1);
  assert.equal(normalized.resources.userDataBanks![0].index, 0);
  assert.equal(normalized.resources.userDataBanks![0].bank.voices.length, 32);

  // The packed bytes are part of the build identity: change one → different build.
  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "6" };
  const first = await computeBuildKey(normalized, identity);
  const changed = makeBank();
  changed.voices[5].packed[10] = 77;
  assert.notEqual(first, await computeBuildKey(normalizeRecipe(makeRecipe([{ index: 0, bank: changed }])), identity));

  // Rejections: out-of-range byte, wrong voice count, duplicate index.
  const badByte = makeBank();
  badByte.voices[0].packed[0] = 200;
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: badByte }])), /7-bit/);
  const short = makeBank();
  short.voices = short.voices.slice(0, 31);
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: short }])), /32 voices/);
  assert.throws(
    () => normalizeRecipe(makeRecipe([{ index: 0, bank: makeBank() }, { index: 0, bank: makeBank() }])),
    /distinct/,
  );
});

test("version 6 accepts a 32-slot fourth-bank recipe with no custom banks", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const publishedTable = structuredClone(chordCatalog.tables[0]);
  const reference = (engineId: string) => {
    const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
    return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
  };
  const fourthBank = [
    "loopback", "lockstep", "tapfield", "phase-weave",
    "sideband-bank", "attractor", "undertow", "reed-pipe",
  ];
  const makeRecipe = (slotIds: string[], schemaVersion = 6) => ({
    ...fixture,
    schemaVersion,
    slots: slotIds.map(reference),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id, holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable], userDataBanks: [] },
  });

  const normalized = normalizeRecipe(makeRecipe([...fixture.slots, ...fourthBank]));
  assert.equal(normalized.schemaVersion, 6);
  assert.equal(normalized.slots.length, 32);
  assert.equal(normalized.resources.userDataBanks!.length, 0);

  // Slot count is part of the build identity.
  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "6" };
  assert.notEqual(
    await computeBuildKey(normalized, identity),
    await computeBuildKey(normalizeRecipe(makeRecipe(fixture.slots)), identity),
  );

  // 32 slots demand schema v6; 24 or 32 are the only counts.
  const v5 = makeRecipe([...fixture.slots, ...fourthBank], 5) as Record<string, unknown>;
  (v5 as { resources: Record<string, unknown> }).resources = { chordTables: [publishedTable] };
  assert.throws(() => normalizeRecipe(v5), /schema version 6/);
  assert.throws(() => normalizeRecipe(makeRecipe([...fixture.slots, "loopback"])), /24 engine slots, or 32/);
});

test("manual keys derive from documentation identity, not build identity", async () => {
  const { computeManualKey } = await import("../src/contract.ts");
  const recipe = normalizeRecipe(fixture);
  const first = await computeManualKey(recipe, "1");
  const second = await computeManualKey(recipe, "1");
  assert.equal(first, second);
  assert.match(first, /^[0-9a-f]{64}$/);

  // Prose/renderer identity changes the manual…
  assert.notEqual(first, await computeManualKey(recipe, "2"));

  // …and so does the layout…
  const reordered = normalizeRecipe(fixture);
  [reordered.slots[0], reordered.slots[1]] = [reordered.slots[1], reordered.slots[0]];
  assert.notEqual(first, await computeManualKey(reordered, "1"));

  // …but firmware options and chord-table edits share the same field guide.
  const optionsChanged = normalizeRecipe(fixture);
  optionsChanged.preferences = { ...optionsChanged.preferences, navigationMode: "banked" };
  optionsChanged.initialOptions = { ...optionsChanged.initialOptions, holdOnTrigger: true };
  assert.equal(first, await computeManualKey(optionsChanged, "1"));
});
