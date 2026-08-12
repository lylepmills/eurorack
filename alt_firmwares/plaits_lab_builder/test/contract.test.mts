import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import {
  approvedEngineIds,
  computeBuildKey,
  computeManualKey,
  isCompatiblePackageUpgrade,
  maxRecipeSchemaVersion,
  minRecipeSchemaVersion,
  normalizeRecipe,
} from "../src/contract.ts";

const fixture = JSON.parse(await readFile(new URL("../default_recipe.json", import.meta.url), "utf8"));
const scaleBank = [
  {
    id: "major",
    name: "Major",
    description: "The familiar seven-note major scale.",
    pitches: [0, 256, 512, 640, 896, 1152, 1408],
    tuning: "12-TET",
    source: "Shipped",
  },
  {
    id: "just-five",
    name: "Just five",
    description: "A local microtonal five-note scale.",
    pitches: [0, 261, 637, 899, 1393],
    tuning: "Microtonal",
    source: "Local",
  },
];

test("the Worker and compiler catalogs contain the same approved IDs", async () => {
  const compilerCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/catalog.json", import.meta.url),
    "utf8",
  ));
  assert.deepEqual(
    approvedEngineIds,
    compilerCatalog.engines.map((engine: { id: string }) => engine.id),
  );
  assert.equal(approvedEngineIds.length, 88);
});

test("schema 21 carries distinct per-slot Wave Terrain and Wavetable data", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: any) => [engine.id, engine]),
  );
  const recipe = structuredClone(fixture) as any;
  recipe.schemaVersion = 21;
  recipe.slots[0] = "wave-terrain";
  recipe.slots[1] = "wavetable";
  recipe.slots = recipe.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as any;
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  recipe.preferences = { navigationMode: "linear" };
  recipe.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  const terrainData = Buffer.alloc(4096, 17).toString("base64");
  const wavetableData = Buffer.alloc(4096, 29).toString("base64");
  recipe.resources = {
    chordTables: chordCatalog.tables,
    customModelData: [
      { slot: 0, model: { kind: "wave-terrain", name: "Terrain A", equation: "x + y", data: terrainData } },
      { slot: 1, model: { kind: "wavetable", name: "Table B", equation: "sin(phi)", data: wavetableData } },
    ],
  };

  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 21);
  assert.deepEqual(normalized.resources.customModelData, recipe.resources.customModelData);

  const wrongKind = structuredClone(recipe);
  wrongKind.resources.customModelData[0].model.kind = "wavetable";
  assert.throws(() => normalizeRecipe(wrongKind), /match a Wave Terrain or Wavetable slot/);

  const shortData = structuredClone(recipe);
  shortData.resources.customModelData[0].model.data = Buffer.alloc(4095).toString("base64");
  assert.throws(() => normalizeRecipe(shortData), /exactly 4096 bytes/);

  const wrongSlot = structuredClone(recipe);
  wrongSlot.resources.customModelData[0].slot = 2;
  assert.throws(() => normalizeRecipe(wrongSlot), /match a Wave Terrain or Wavetable slot/);
});

test("schema 23 carries one ordered shared terrain bank", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(publicCatalog.engines.map((engine: any) => [engine.id, engine]));
  const recipe = structuredClone(fixture) as any;
  recipe.schemaVersion = 23;
  recipe.slots[0] = "wave-terrain";
  recipe.slots = recipe.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as any;
    return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
  });
  recipe.preferences = {
    navigationMode: "linear", calibration: false, colorBlindMode: false,
    replaceableFmBanks: false, syncInput: false,
  };
  recipe.initialOptions = {
    lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
    auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: "original",
    holdOnTrigger: false, attenuverterMode: "stock",
  };
  const model = {
    kind: "wave-terrain", name: "Folded basin", equation: "sin(x * y)",
    data: Buffer.alloc(4096, 41).toString("base64"),
  };
  recipe.resources = {
    chordTables: chordCatalog.tables,
    terrainBank: [
      { kind: "factory", id: "factory-8" },
      { kind: "custom", model },
      { kind: "factory", id: "factory-2" },
    ],
  };

  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 23);
  assert.deepEqual(normalized.resources.terrainBank, recipe.resources.terrainBank);

  const native = structuredClone(recipe);
  native.schemaVersion = 24;
  native.resources.terrainBank[1].model.representation = "native";
  const normalizedNative = normalizeRecipe(native);
  assert.equal(normalizedNative.schemaVersion, 24);
  assert.equal(normalizedNative.resources.terrainBank?.[1].kind, "custom");
  assert.equal((normalizedNative.resources.terrainBank?.[1] as any).model.representation, "native");

  const nativeOnV23 = structuredClone(native);
  nativeOnV23.schemaVersion = 23;
  assert.throws(() => normalizeRecipe(nativeOnV23), /require recipe schema 24/);

  const duplicate = structuredClone(recipe);
  duplicate.resources.terrainBank.push({ kind: "factory", id: "factory-8" });
  assert.throws(() => normalizeRecipe(duplicate), /at most once/);

  const wrongPalette = structuredClone(recipe);
  wrongPalette.slots[0] = wrongPalette.slots[1];
  assert.throws(() => normalizeRecipe(wrongPalette), /requires Wave Terrain/);

  const legacySlotShape = structuredClone(recipe);
  legacySlotShape.resources.customModelData = [{ slot: 0, model }];
  assert.throws(() => normalizeRecipe(legacySlotShape), /shared terrain bank/);
});

test("normalization removes nondeterministic manifest fields", () => {
  const normalized = normalizeRecipe({ ...fixture, createdAt: "2099-01-01T00:00:00Z" });
  assert.equal("createdAt" in normalized, false);
  assert.equal(normalized.schemaVersion, 5);
  assert.deepEqual(normalized.slots, fixture.slots);
  assert.equal(normalized.preferences.navigationMode, "linear");
  assert.equal(normalized.initialOptions.lockedFrequencyKnob, "octaves");
  assert.equal(normalized.initialOptions.attenuverterMode, "stock");
});

test("schema 18 carries the unpatched attenuverter starting mode", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const recipe = structuredClone(fixture) as any;
  recipe.schemaVersion = 18;
  recipe.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  recipe.preferences = { navigationMode: "linear" };
  recipe.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "drift",
  };
  recipe.resources = { chordTables: chordCatalog.tables };

  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 18);
  assert.equal(normalized.initialOptions.attenuverterMode, "drift");

  const missing = structuredClone(recipe);
  delete missing.initialOptions.attenuverterMode;
  assert.throws(() => normalizeRecipe(missing), /schema version 18/);
  assert.throws(
    () => normalizeRecipe({ ...recipe, schemaVersion: 17 }),
    /schema version 18/,
  );
});

test("schema 19 carries triggered and gated locked-frequency envelopes", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const recipe = structuredClone(fixture) as any;
  recipe.schemaVersion = 19;
  recipe.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  recipe.preferences = { navigationMode: "linear" };
  recipe.initialOptions = {
    lockedFrequencyKnob: "triggered-envelope",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  recipe.resources = { chordTables: chordCatalog.tables };

  assert.equal(normalizeRecipe(recipe).schemaVersion, 19);
  assert.equal(
    normalizeRecipe(recipe).initialOptions.lockedFrequencyKnob,
    "triggered-envelope",
  );
  recipe.initialOptions.lockedFrequencyKnob = "gated-envelope";
  assert.equal(normalizeRecipe(recipe).schemaVersion, 19);
  assert.equal(
    normalizeRecipe(recipe).initialOptions.lockedFrequencyKnob,
    "gated-envelope",
  );

  assert.throws(
    () => normalizeRecipe({ ...recipe, schemaVersion: 18 }),
    /schema version 19/,
  );
});

test("schema 21 carries the experimental MODEL-input Sync In option", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const recipe = structuredClone(fixture) as any;
  recipe.schemaVersion = 21;
  recipe.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  recipe.preferences = {
    navigationMode: "linear",
    calibration: false,
    colorBlindMode: false,
    replaceableFmBanks: false,
    syncInput: true,
  };
  recipe.schemaVersion = 22;
  recipe.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "sync-in",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  recipe.resources = { chordTables: chordCatalog.tables };

  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 22);
  assert.equal(normalized.initialOptions.modelInput, "sync-in");
  assert.equal(normalized.preferences.syncInput, true);
  assert.throws(
    () => normalizeRecipe({ ...recipe, schemaVersion: 21 }),
    /schema version 22/,
  );

  // Since v22 the starting value no longer implies the capability: without the
  // preference the module would boot with MODEL past the end of its own menu.
  const startsWithoutPreference = structuredClone(recipe);
  startsWithoutPreference.preferences.syncInput = false;
  assert.throws(
    () => normalizeRecipe(startsWithoutPreference),
    /requires the syncInput preference/,
  );

  // The preference alone is legitimate — that is the whole point of making it
  // opt-in: pay Sync In's flash so it can be reached later from the menu.
  const preferenceOnly = structuredClone(recipe);
  preferenceOnly.initialOptions.modelInput = "model";
  assert.equal(normalizeRecipe(preferenceOnly).schemaVersion, 22);
  assert.equal(normalizeRecipe(preferenceOnly).preferences.syncInput, true);
});

test("schema 23 carries independent experimental TZFM and Fast FM preferences", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const base = structuredClone(fixture) as any;
  base.schemaVersion = 23;
  base.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  base.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  base.resources = { chordTables: chordCatalog.tables };

  for (const linearTzfm of [false, true]) {
    for (const fastFm of [false, true]) {
      const recipe = structuredClone(base);
      recipe.preferences = {
        navigationMode: "linear",
        calibration: false,
        colorBlindMode: false,
        replaceableFmBanks: false,
        syncInput: false,
        linearTzfm,
        fastFm,
      };
      const normalized = normalizeRecipe(recipe);
      assert.equal(normalized.preferences.linearTzfm, linearTzfm);
      assert.equal(normalized.preferences.fastFm, fastFm);
      // The explicit attenuverterMode field already requires v18; either FM
      // experiment raises that normalized floor to v23.
      assert.equal(normalized.schemaVersion, linearTzfm || fastFm ? 23 : 18);
    }
  }

  const tooOld = structuredClone(base);
  tooOld.schemaVersion = 22;
  tooOld.preferences = {
    navigationMode: "linear",
    calibration: false,
    colorBlindMode: false,
    replaceableFmBanks: false,
    syncInput: false,
    linearTzfm: true,
    fastFm: false,
  };
  assert.throws(() => normalizeRecipe(tooOld), /schema version 23/);

  const nonBoolean = structuredClone(base);
  nonBoolean.preferences = { ...tooOld.preferences, linearTzfm: "yes" };
  assert.throws(
    () => normalizeRecipe(nonBoolean),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );
});

test("every historical preference shape still loads, and nothing else does", async () => {
  // The tier list is only safe if its cumulative prefixes are exactly the shapes
  // released editors have ever written. Walk them all. The schema version is
  // held fixed on purpose: the key set is validated independently of it (the
  // per-preference floors fire only on a `true` value, and every flag here is
  // false), so this isolates the shape from the version.
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url), "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url), "utf8",
  ));
  const fixture = JSON.parse(await readFile(
    new URL("../default_recipe.json", import.meta.url), "utf8",
  ));
  const engines = new Map<string, unknown>(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const base = structuredClone(fixture) as any;
  base.schemaVersion = 24;
  base.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string; version: string; digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  base.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  base.resources = { chordTables: chordCatalog.tables };

  // Must mirror the tier order in src/contract.ts.
  const tiers = [
    ["navigationMode"],
    ["calibration"],
    ["colorBlindMode"],
    ["replaceableFmBanks"],
    ["syncInput"],
    ["linearTzfm", "fastFm"],
    ["simplifiedPitchRanges"],
  ];

  const cumulative: string[] = [];
  for (const tier of tiers) {
    cumulative.push(...tier);
    const recipe = structuredClone(base);
    recipe.preferences = Object.fromEntries(cumulative.map((key) => [
      key, key === "navigationMode" ? "linear" : false,
    ]));
    const normalized = normalizeRecipe(recipe);
    // Present flags read false; absent ones must not invent a value either.
    for (const key of ["calibration", "colorBlindMode", "replaceableFmBanks",
      "syncInput", "linearTzfm", "fastFm", "simplifiedPitchRanges"]) {
      assert.equal(
        normalized.preferences[key as "calibration"], false,
        `${key} should be false for the {${cumulative.join(",")}} shape`,
      );
    }
  }

  // A gap in the middle of the prefix is not a shape any editor produced.
  const gapped = structuredClone(base);
  gapped.preferences = {
    navigationMode: "linear", calibration: false, syncInput: false,
  };
  assert.throws(
    () => normalizeRecipe(gapped),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );

  // Neither is an unknown key riding along with a valid prefix.
  const extra = structuredClone(base);
  extra.preferences = {
    navigationMode: "linear", calibration: false, colorBlindMode: false,
    replaceableFmBanks: false, syncInput: false, somethingElse: false,
  };
  assert.throws(
    () => normalizeRecipe(extra),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );
});

test("every declared preference is registered in the tier list", async () => {
  // The structural guarantee: a preference added to the type but not to
  // preferenceTiers would be silently rejected by the closed key-set check, and
  // one added to the tiers but never derived would silently read false. Parse
  // both out of the source and require they agree.
  const source = await readFile(new URL("../src/contract.ts", import.meta.url), "utf8");
  const tierBlock = source.match(
    /const preferenceTiers: readonly \(readonly string\[\]\)\[\] = \[([\s\S]*?)\];/,
  );
  assert.ok(tierBlock, "expected a preferenceTiers table in contract.ts");
  const tierKeys = [...tierBlock[1].matchAll(/"([a-zA-Z0-9]+)"/g)].map((match) => match[1]);

  const typeBlock = source.match(/preferences: \{([\s\S]*?)\n  \};/);
  assert.ok(typeBlock, "expected a preferences type on the recipe");
  const typeKeys = [...typeBlock[1].matchAll(/^\s{4}([a-zA-Z0-9]+)\??:/gm)]
    .map((match) => match[1]);

  assert.deepEqual(
    [...tierKeys].sort(), [...typeKeys].sort(),
    "preferenceTiers and the preferences type must declare the same keys",
  );

  // And each one must actually be derived, or it normalizes to false forever.
  for (const key of tierKeys) {
    if (key === "navigationMode") continue;
    assert.match(
      source,
      new RegExp(`const ${key} = preferenceValues\\.${key} === true;`),
      `${key} is registered but never derived`,
    );
  }
});

test("the simplified pitch-range preference requires and normalizes to v24", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const fixture = JSON.parse(await readFile(
    new URL("../default_recipe.json", import.meta.url), "utf8",
  ));
  const engines = new Map<string, unknown>(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const base = structuredClone(fixture) as any;
  base.schemaVersion = 24;
  base.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  base.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  };
  base.resources = { chordTables: chordCatalog.tables };

  const preferences = (simplifiedPitchRanges: unknown) => ({
    navigationMode: "linear",
    calibration: false,
    colorBlindMode: false,
    replaceableFmBanks: false,
    syncInput: false,
    linearTzfm: false,
    fastFm: false,
    simplifiedPitchRanges,
  });

  for (const simplifiedPitchRanges of [false, true]) {
    const recipe = structuredClone(base);
    recipe.preferences = preferences(simplifiedPitchRanges);
    const normalized = normalizeRecipe(recipe);
    assert.equal(
      normalized.preferences.simplifiedPitchRanges, simplifiedPitchRanges);
    // attenuverterMode already floors this at v18; opting in raises it to v24.
    assert.equal(normalized.schemaVersion, simplifiedPitchRanges ? 24 : 18);
  }

  // A recipe that predates v24 must not carry it.
  const tooOld = structuredClone(base);
  tooOld.schemaVersion = 23;
  tooOld.preferences = preferences(true);
  assert.throws(() => normalizeRecipe(tooOld), /schema version 24/);

  const nonBoolean = structuredClone(base);
  nonBoolean.preferences = preferences("yes");
  assert.throws(
    () => normalizeRecipe(nonBoolean),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );

  // Every preference must survive normalization at the newest key set. The
  // derivations use the same exact-key-set matching as the schema floors, so a
  // future preference that adds its own set without extending these ORs makes
  // every OLDER one derive to false -- a recipe's calibration or Sync In quietly
  // becoming absent from the firmware it builds. That is not hypothetical: the
  // website copy of this logic shipped exactly that bug for `calibration` when
  // v24 landed, caught only by adding this assertion.
  const allOn = structuredClone(base);
  allOn.preferences = {
    ...preferences(true),
    calibration: true,
    colorBlindMode: true,
    replaceableFmBanks: true,
    syncInput: true,
    linearTzfm: true,
    fastFm: true,
  };
  const normalizedAllOn = normalizeRecipe(allOn);
  for (const key of [
    "calibration", "colorBlindMode", "replaceableFmBanks", "syncInput",
    "linearTzfm", "fastFm", "simplifiedPitchRanges",
  ] as const) {
    assert.equal(
      normalizedAllOn.preferences[key], true,
      `preference ${key} did not survive normalization`,
    );
  }
});

test("automatic LEVEL routing is carried only by schema 16", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const engines = new Map(
    publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]),
  );
  const recipe = structuredClone(fixture);
  recipe.schemaVersion = 16;
  recipe.slots = fixture.slots.map((engineId: string) => {
    const engine = engines.get(engineId) as {
      packageId: string;
      version: string;
      digest: string;
    };
    return {
      engine: engineId,
      package: engine.packageId,
      version: engine.version,
      digest: engine.digest,
    };
  });
  recipe.preferences = { navigationMode: "linear" };
  recipe.initialOptions = {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "auto",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
  };
  recipe.resources = { chordTables: chordCatalog.tables };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 16);
  assert.equal(normalized.initialOptions.levelInput, "auto");

  recipe.schemaVersion = 15;
  assert.throws(
    () => normalizeRecipe(recipe),
    (error: { code?: string }) => error.code === "unsupported_schema",
  );
});

test("WAV and application-only Intel HEX are the only output formats", async () => {
  const wav = normalizeRecipe(fixture);
  const hex = normalizeRecipe({ ...fixture, output: "intel-hex" });
  assert.equal(wav.output, "audio-wav");
  assert.equal(hex.output, "intel-hex");
  assert.throws(
    () => normalizeRecipe({ ...fixture, output: "raw-binary" }),
    (error: { code?: string }) => error.code === "unsupported_output",
  );

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "15" };
  assert.notEqual(await computeBuildKey(wav, identity), await computeBuildKey(hex, identity));
  // File format changes no controls or documentation, so both downloads share
  // the exact same field-guide PDF.
  assert.equal(await computeManualKey(wav, "9"), await computeManualKey(hex, "9"));
});

// The calibration procedure (v14). It is a firmware PREFERENCE, not a stored
// option, so the whole contract for it is: absent means off, asking for it lifts
// the recipe to v14, and an older declared version cannot smuggle it in.
test("a recipe without the calibration key means calibration off", () => {
  const normalized = normalizeRecipe(fixture);
  assert.equal(normalized.preferences.calibration, false);
  assert.equal(normalized.schemaVersion, 5);
});

test("calibration off is accepted at any version and changes nothing", () => {
  const normalized = normalizeRecipe({
    ...fixture,
    preferences: { ...fixture.preferences, calibration: false },
  });
  assert.equal(normalized.preferences.calibration, false);
  // Still v5: an explicit false asks for nothing a v5 builder cannot do.
  assert.equal(normalized.schemaVersion, 5);
});

test("calibration is carried, version-gated, and type-checked", async () => {
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
  const makeRecipe = (schemaVersion: number, calibration: unknown) => ({
    ...fixture,
    schemaVersion,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear", calibration },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable] },
  });

  // Asking for it lifts the recipe to v14. Its feature says nothing about
  // resources, so unlike v12/v13 it must not demand a userDataBanks list; later
  // resource-independent schemas inherit that behavior.
  const on = normalizeRecipe(makeRecipe(14, true));
  assert.equal(on.preferences.calibration, true);
  assert.equal(on.schemaVersion, 14);
  assert.equal("userDataBanks" in on.resources, false);

  // An explicit false asks for nothing a v5 builder cannot do, so it stays v5.
  const off = normalizeRecipe(makeRecipe(14, false));
  assert.equal(off.preferences.calibration, false);
  assert.equal(off.schemaVersion, 5);

  // A pre-v14 declared version cannot smuggle it in. (Declared v11 rather than
  // v13: a v13 recipe is required to carry a userDataBanks list, so it would
  // trip the resources guard before ever reaching this one.)
  assert.throws(
    () => normalizeRecipe(makeRecipe(11, true)),
    (error: { code?: string }) => error.code === "unsupported_schema",
  );
  // ...and it must be a boolean.
  assert.throws(
    () => normalizeRecipe(makeRecipe(14, "yes")),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );

  // Turning it on changes the build identity, so a cached mono build cannot be
  // served to someone who asked for the procedure.
  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "14" };
  assert.notEqual(await computeBuildKey(on, identity), await computeBuildKey(off, identity));
});

test("Ro'Ved is a schema-15 hardware target and changes build/manual identity", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: {
    id: string; packageId: string; version: string; digest: string;
  }) => [engine.id, engine]));
  const recipe = {
    ...fixture,
    schemaVersion: 15,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId)!;
      return {
        engine: engineId,
        package: engine.packageId,
        version: engine.version,
        digest: engine.digest,
      };
    }),
    preferences: { navigationMode: "linear", calibration: false },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0,
      chordTable: chordCatalog.tables[0].id, holdOnTrigger: false,
    },
    resources: { chordTables: [structuredClone(chordCatalog.tables[0])] },
  };
  const plaits = normalizeRecipe(recipe);
  const roved = normalizeRecipe({
    ...recipe,
    target: "plum-audio-roved",
  });
  assert.equal(plaits.schemaVersion, 5);
  assert.equal(plaits.target, "mutable-instruments-plaits");
  assert.equal(roved.schemaVersion, 15);
  assert.equal(roved.target, "plum-audio-roved");
  assert.throws(
    () => normalizeRecipe({ ...recipe, schemaVersion: 14, target: "plum-audio-roved" }),
    (error: { code?: string }) => error.code === "unsupported_schema",
  );

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "15" };
  assert.notEqual(await computeBuildKey(plaits, identity), await computeBuildKey(roved, identity));
  assert.notEqual(await computeManualKey(plaits, "8"), await computeManualKey(roved, "8"));
});

test("the color-blind bank display is baked, version-gated, and type-checked", async () => {
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
  const makeRecipe = (schemaVersion: number, colorBlindMode: unknown, calibration = false) => ({
    ...fixture,
    schemaVersion,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear", calibration, colorBlindMode },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable] },
  });

  const on = normalizeRecipe(makeRecipe(15, true, true));
  assert.equal(on.preferences.colorBlindMode, true);
  assert.equal(on.preferences.calibration, true);
  assert.equal(on.schemaVersion, 15);

  const roved = normalizeRecipe({
    ...makeRecipe(15, true, true),
    target: "plum-audio-roved",
  });
  assert.equal(roved.target, "plum-audio-roved");
  assert.equal(roved.preferences.colorBlindMode, true);
  assert.equal(roved.preferences.calibration, true);
  assert.equal(roved.schemaVersion, 15);

  const off = normalizeRecipe(makeRecipe(15, false));
  assert.equal(off.preferences.colorBlindMode, false);
  assert.equal(off.schemaVersion, 5);

  assert.throws(
    () => normalizeRecipe(makeRecipe(14, true)),
    (error: { code?: string }) => error.code === "unsupported_schema",
  );
  assert.throws(
    () => normalizeRecipe(makeRecipe(15, "yes")),
    (error: { code?: string }) => error.code === "invalid_preferences",
  );

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "15" };
  assert.notEqual(await computeBuildKey(on, identity), await computeBuildKey(off, identity));
  assert.notEqual(await computeBuildKey(on, identity), await computeBuildKey(roved, identity));
  assert.notEqual(await computeManualKey(on, "9"), await computeManualKey(roved, "9"));
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

test("package-version compatibility only moves forward within a stable API line", () => {
  assert.equal(isCompatiblePackageUpgrade("1.0.0", "1.0.0"), true);
  assert.equal(isCompatiblePackageUpgrade("1.2.3", "1.4.0"), true);
  assert.equal(isCompatiblePackageUpgrade("0.4.2", "0.4.9"), true);
  assert.equal(isCompatiblePackageUpgrade("1.4.0", "1.2.3"), false);
  assert.equal(isCompatiblePackageUpgrade("1.9.0", "2.0.0"), false);
  assert.equal(isCompatiblePackageUpgrade("0.3.9", "0.4.0"), false);
  assert.equal(isCompatiblePackageUpgrade("1.0", "1.0.0"), false);
});

test("versioned package references migrate stale digests but reject incompatible identities", async () => {
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

  // The selected engine/package is unchanged. The current builder owns the
  // source and build revision, so an older digest resolves to today's engine.
  versioned.slots[0].digest = "sha256:" + "0".repeat(64);
  assert.deepEqual(normalizeRecipe(versioned).slots, fixture.slots);

  versioned.slots[0].digest = "not-a-digest";
  assert.throws(() => normalizeRecipe(versioned), /unavailable package version/);

  versioned.slots[0].digest = "sha256:" + "0".repeat(64);
  versioned.slots[0].package = "somewhere-else/virtual-analog";
  assert.throws(() => normalizeRecipe(versioned), /unavailable package version/);

  const currentVersion = (byId.get(fixture.slots[0]) as { version: string }).version;
  const [major, minor, patch] = currentVersion.split(".").map(Number);
  versioned.slots[0].package = (byId.get(fixture.slots[0]) as { packageId: string }).packageId;
  versioned.slots[0].version = `${major}.${minor}.${patch + 1}`;
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

test("version 16 validates, normalizes, and hashes the scale bank", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: {
    id: string; packageId: string; version: string; digest: string;
  }) => [engine.id, engine]));
  const recipe = {
    ...fixture,
    schemaVersion: 16,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId)!;
      return {
        engine: engineId,
        package: engine.packageId,
        version: engine.version,
        digest: engine.digest,
      };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0,
      chordTable: chordCatalog.tables[0].id, holdOnTrigger: false,
    },
    resources: {
      chordTables: [structuredClone(chordCatalog.tables[0])],
      scaleBank: structuredClone(scaleBank),
    },
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 16);
  assert.deepEqual(normalized.resources.scaleBank, scaleBank);

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "16" };
  const changed = structuredClone(recipe);
  changed.resources.scaleBank[1].pitches[1] += 1;
  assert.notEqual(
    await computeBuildKey(normalized, identity),
    await computeBuildKey(normalizeRecipe(changed), identity),
  );

  const missing = structuredClone(recipe);
  delete (missing.resources as { scaleBank?: unknown }).scaleBank;
  assert.equal(normalizeRecipe(missing).resources.scaleBank, undefined);

  const duplicate = structuredClone(recipe);
  duplicate.resources.scaleBank[1].id = duplicate.resources.scaleBank[0].id;
  assert.throws(() => normalizeRecipe(duplicate), /strictly ascending pitches|invalid scale/i);

  const mismatched = structuredClone(recipe);
  mismatched.resources.scaleBank[1].tuning = "12-TET";
  assert.throws(() => normalizeRecipe(mismatched), /tuning label/);
});

test("version 17 carries bounded Speech banks and hashes their LPC frames", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: {
    id: string; packageId: string; version: string; digest: string;
  }) => [engine.id, engine]));
  const frame = Buffer.alloc(14);
  frame[0] = 12;
  frame[1] = 80;
  const speechBanks = {
    stockBankIds: [0, 3],
    customBanks: [{
      words: ["hello"],
      wordBoundaries: [0, 1],
      frameData: frame.toString("base64"),
    }],
  };
  const recipe = {
    ...structuredClone(fixture),
    schemaVersion: 17,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId)!;
      return {
        engine: engineId,
        package: engine.packageId,
        version: engine.version,
        digest: engine.digest,
      };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves",
      modelInput: "model",
      levelInput: "level",
      auxOutput: "alternate-model",
      suboscillatorOctave: 0,
      chordTable: chordCatalog.tables[0].id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [structuredClone(chordCatalog.tables[0])], speechBanks },
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 17);
  assert.deepEqual(normalized.resources.speechBanks, speechBanks);

  const thirtyTwoWords = structuredClone(recipe);
  thirtyTwoWords.resources.speechBanks.customBanks[0] = {
    words: Array.from({ length: 32 }, (_unused, index) => `word ${index + 1}`),
    wordBoundaries: Array.from({ length: 33 }, (_unused, index) => index),
    frameData: Buffer.alloc(14 * 32).toString("base64"),
  };
  assert.equal(
    normalizeRecipe(thirtyTwoWords).resources.speechBanks?.customBanks[0].words.length,
    32,
  );
  const thirtyThreeWords = structuredClone(thirtyTwoWords);
  thirtyThreeWords.resources.speechBanks.customBanks[0].words.push("word 33");
  thirtyThreeWords.resources.speechBanks.customBanks[0].wordBoundaries.push(33);
  thirtyThreeWords.resources.speechBanks.customBanks[0].frameData = Buffer.alloc(14 * 33).toString("base64");
  assert.throws(() => normalizeRecipe(thirtyThreeWords), /invalid custom Speech bank/);

  const lpcWordsOnly = structuredClone(recipe);
  const lpcWords = byId.get("lpc-speech")!;
  lpcWordsOnly.slots = lpcWordsOnly.slots.map(
    (slot: { engine: string }) => slot.engine === "speech"
      ? {
        engine: "lpc-speech",
        package: lpcWords.packageId,
        version: lpcWords.version,
        digest: lpcWords.digest,
      }
      : slot,
  );
  assert.deepEqual(
    normalizeRecipe(lpcWordsOnly).resources.speechBanks,
    speechBanks,
  );

  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "17" };
  const changed = structuredClone(recipe);
  changed.resources.speechBanks.customBanks[0].frameData = Buffer.from([
    13, ...frame.subarray(1),
  ]).toString("base64");
  assert.notEqual(
    await computeBuildKey(normalized, identity),
    await computeBuildKey(normalizeRecipe(changed), identity),
  );

  const tooOld = structuredClone(recipe);
  tooOld.schemaVersion = 16;
  assert.throws(
    () => normalizeRecipe(tooOld),
    (error: { code?: string }) => error.code === "invalid_resources",
  );

  const withoutSpeechEngine = structuredClone(lpcWordsOnly);
  withoutSpeechEngine.slots = withoutSpeechEngine.slots.map(
    (slot: { engine: string }) => slot.engine === "speech" || slot.engine === "lpc-speech"
      ? structuredClone(withoutSpeechEngine.slots[0])
      : slot,
  );
  assert.throws(
    () => normalizeRecipe(withoutSpeechEngine),
    /require.*Speech or LPC Words model/,
  );
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

  // Rejections: out-of-range byte, wrong packed length, duplicate index.
  const badByte = makeBank();
  badByte.voices[0].packed[0] = 200;
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: badByte }])), /7-bit/);
  const badPacked = makeBank();
  badPacked.voices[0].packed = Array(127).fill(0);
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: badPacked }])), /7-bit/);
  // A short (< 32-voice) bank is now well-formed, but needs a v13 builder; a v6
  // recipe carrying one is rejected with the schema gate, not a shape error.
  const short = makeBank();
  short.voices = short.voices.slice(0, 4);
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: short }])), /schema version 13/);
  // An empty (0-voice) bank is malformed at any schema.
  const empty = makeBank();
  empty.voices = [];
  assert.throws(() => normalizeRecipe(makeRecipe([{ index: 0, bank: empty }])), /1–32 voices/);
  assert.throws(
    () => normalizeRecipe(makeRecipe([{ index: 0, bank: makeBank() }, { index: 0, bank: makeBank() }])),
    /distinct/,
  );

  // v12: per-slot custom banks normalize to schemaVersion 12, keyed by slot (not
  // index) — two banks on distinct slots both survive.
  const v12Recipe = {
    ...makeRecipe([]),
    schemaVersion: 12,
    resources: {
      chordTables: [publishedTable],
      userDataBanks: [{ slot: 0, bank: makeBank() }, { slot: 5, bank: makeBank() }],
    },
  };
  const v12 = normalizeRecipe(v12Recipe);
  assert.equal(v12.schemaVersion, 12);
  assert.equal(v12.resources.userDataBanks!.length, 2);
  assert.equal((v12.resources.userDataBanks![0] as { slot: number }).slot, 0);
  assert.equal((v12.resources.userDataBanks![1] as { slot: number }).slot, 5);
  // A bank on a slot that does not exist is rejected.
  assert.throws(
    () => normalizeRecipe({
      ...v12Recipe,
      resources: { chordTables: [publishedTable], userDataBanks: [{ slot: 99, bank: makeBank() }] },
    }),
    /distinct palette slot/,
  );

  // v13: a per-slot bank with fewer than 32 patches (a "short" FM bank) normalizes
  // to schemaVersion 13 — the firmware then sizes its Harmonics dial to the real
  // patch count. A full-length per-slot bank stays v12.
  const shortSlotBank = makeBank();
  shortSlotBank.voices = shortSlotBank.voices.slice(0, 5);
  const v13Recipe = {
    ...v12Recipe,
    schemaVersion: 13,
    resources: { chordTables: [publishedTable], userDataBanks: [{ slot: 0, bank: shortSlotBank }] },
  };
  const v13 = normalizeRecipe(v13Recipe);
  assert.equal(v13.schemaVersion, 13);
  assert.equal(v13.resources.userDataBanks![0].bank.voices.length, 5);
  // The same short bank declared as v12 is rejected (needs the v13 quantizer).
  assert.throws(
    () => normalizeRecipe({ ...v13Recipe, schemaVersion: 12 }),
    /schema version 13/,
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
  assert.throws(() => normalizeRecipe(v5), /schema versions 6 through/);
  assert.throws(() => normalizeRecipe(makeRecipe([...fixture.slots, "loopback"])), /24 engine slots, or 32/);
});

test("version 7 accepts short banks (empty slots) and stays v7", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url), "utf8"));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url), "utf8"));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const publishedTable = structuredClone(chordCatalog.tables[0]);
  const reference = (engineId: string) => {
    const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
    return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
  };
  const slot = (id: string | null) => (id === null ? null : reference(id));
  const makeRecipe = (slotIds: (string | null)[], schemaVersion = 7) => ({
    ...fixture,
    schemaVersion,
    slots: slotIds.map(slot),
    preferences: { navigationMode: "banked" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id, holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable] },
  });

  // Public layout green (0-7), red (8-15), amber (16-23); shorten red to three
  // engines by emptying its last five slots.
  const ids: (string | null)[] = [
    ...fixture.slots.slice(0, 8),
    fixture.slots[8], fixture.slots[9], fixture.slots[10], null, null, null, null, null,
    ...fixture.slots.slice(16, 24),
  ];
  const normalized = normalizeRecipe(makeRecipe(ids));
  assert.equal(normalized.schemaVersion, 7);  // stays v7 so the compiler shortens the bank
  assert.equal(normalized.slots.length, 24);
  assert.deepEqual(normalized.slots.slice(11, 16), [null, null, null, null, null]);
  assert.equal(normalized.slots.filter((s) => s === null).length, 5);
  assert.equal(normalized.resources.userDataBanks, undefined);

  // Emptying a slot in the middle of a bank (a hole) is rejected.
  const holed = [...ids];
  holed[1] = null;  // green: filled, empty, filled -> not contiguous
  assert.throws(() => normalizeRecipe(makeRecipe(holed)), /contiguous/);

  // An all-empty palette is rejected.
  assert.throws(() => normalizeRecipe(makeRecipe(new Array(24).fill(null))), /at least one engine/);

  // Empty slots require v7; the same slots under v6 are refused.
  assert.throws(() => normalizeRecipe(makeRecipe(ids, 6)), /schema versions 7 through/);

  // Short banks change the build identity vs. the same layout fully filled.
  const identity = { sourceRevision: "source", toolchain: "toolchain", contract: "7" };
  const full = normalizeRecipe(makeRecipe(fixture.slots));
  assert.notEqual(await computeBuildKey(normalized, identity), await computeBuildKey(full, identity));
});

test("version 11 accepts a sparse bank (a kept mid-bank gap) and normalizes to v11", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url), "utf8"));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url), "utf8"));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const publishedTable = structuredClone(chordCatalog.tables[0]);
  const reference = (engineId: string) => {
    const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
    return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
  };
  const slot = (id: string | null) => (id === null ? null : reference(id));
  const makeRecipe = (slotIds: (string | null)[], schemaVersion = 11) => ({
    ...fixture,
    schemaVersion,
    slots: slotIds.map(slot),
    preferences: { navigationMode: "banked" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id, holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable] },
  });

  // Green bank keeps a gap in place (slot 1 empty, slots 2+ filled) — the whole
  // point of a sparse bank.
  const sparse: (string | null)[] = [
    fixture.slots[0], null, ...fixture.slots.slice(2, 8),
    ...fixture.slots.slice(8, 16),
    ...fixture.slots.slice(16, 24),
  ];
  const normalized = normalizeRecipe(makeRecipe(sparse));
  assert.equal(normalized.schemaVersion, 11);  // sparse dominates and needs a v11 builder
  assert.equal(normalized.slots.length, 24);
  assert.equal(normalized.slots[1], null);           // the gap is preserved in place
  assert.notEqual(normalized.slots[2], null);        // engines still follow it

  // The same sparse layout is rejected below v11 (v7 and v10).
  assert.throws(() => normalizeRecipe(makeRecipe(sparse, 7)), /contiguous/);
  assert.throws(() => normalizeRecipe(makeRecipe(sparse, 10)), /contiguous/);

  // A fully-filled v11 recipe normalizes DOWN — v11 is only forced by an actual
  // gap, so a contiguous palette keeps its lower feature version.
  assert.equal(normalizeRecipe(makeRecipe(fixture.slots)).schemaVersion, 5);
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

  // …but unprinted firmware options do not.
  const optionsChanged = normalizeRecipe(fixture);
  optionsChanged.preferences = { ...optionsChanged.preferences, navigationMode: "banked" };
  optionsChanged.initialOptions = { ...optionsChanged.initialOptions, holdOnTrigger: true };
  assert.equal(first, await computeManualKey(optionsChanged, "1"));

  // The locked-frequency assignment controls whether the guide adds the
  // right-button + MORPH octave shortcut callout.
  const lockedFrequencyChanged = normalizeRecipe(fixture);
  lockedFrequencyChanged.initialOptions = {
    ...lockedFrequencyChanged.initialOptions,
    lockedFrequencyKnob: "decay",
  };
  assert.notEqual(first, await computeManualKey(lockedFrequencyChanged, "1"));

  // Sync In adds a recipe-specific warning to the options page.
  const syncInputChanged = structuredClone(recipe);
  syncInputChanged.schemaVersion = 21;
  syncInputChanged.initialOptions = {
    ...syncInputChanged.initialOptions,
    modelInput: "sync-in",
  };
  assert.notEqual(first, await computeManualKey(syncInputChanged, "1"));

  // The accessible bank display IS printed beside the bank map.
  const displayChanged = normalizeRecipe(fixture);
  displayChanged.preferences = { ...displayChanged.preferences, colorBlindMode: true };
  assert.notEqual(first, await computeManualKey(displayChanged, "1"));

  // Active experimental-FM badges and their legend are printed too.
  const experimentalFmChanged = structuredClone(recipe);
  experimentalFmChanged.schemaVersion = 23;
  experimentalFmChanged.preferences = {
    ...experimentalFmChanged.preferences,
    linearTzfm: true,
    fastFm: true,
  };
  assert.notEqual(first, await computeManualKey(experimentalFmChanged, "1"));

  // The options-menu page lists LIGHT 1's chord tables by name, so a table swap
  // is a different guide. (It shared a key until 2026-07, which served the wrong
  // LIGHT 1 row from cache.)
  const tablesChanged = normalizeRecipe(fixture);
  tablesChanged.resources.chordTables = tablesChanged.resources.chordTables.map((table, index) =>
    index === 0 ? { ...table, name: "Renamed Table" } : table);
  assert.notEqual(first, await computeManualKey(tablesChanged, "1"));
});

test("manual keys separate recipes by their custom FM bank credits", async () => {
  const { computeManualKey, normalizeRecipe } = await import("../src/contract.ts");
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
  const bankDocument = (overrides: Record<string, unknown> = {}) => ({
    id: "warm-keys", packageId: "anon/warm-keys", version: "1.0.0", digest: null,
    name: "Warm Keys", author: "T. Contributor", license: "CC0-1.0",
    origin: "Community", description: "Eight electric pianos.",
    voices: Array.from({ length: 32 }, (_unused, index) => ({
      name: `V${index}`, algorithm: 1, packed: Array(128).fill(0) as number[],
    })),
    ...overrides,
  });
  const recipeWith = (userDataBanks: unknown[], schemaVersion = 12) => ({
    ...fixture,
    schemaVersion,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "alternate-model", suboscillatorOctave: 0, chordTable: publishedTable.id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [publishedTable], userDataBanks },
  });
  const banked = (bank: Record<string, unknown>, slot = 0) =>
    normalizeRecipe(recipeWith([{ slot, bank }], bank.voices instanceof Array && bank.voices.length < 32 ? 13 : 12));

  const base = await computeManualKey(banked(bankDocument()), "1");
  assert.match(base, /^[0-9a-f]{64}$/);
  assert.equal(base, await computeManualKey(banked(bankDocument()), "1"));

  // Every credited field the guide prints moves the key…
  for (const overrides of [
    { name: "Cold Bells" },
    { author: "Someone Else" },
    { origin: "Local" },
    { description: "Different prose." },
    { voices: bankDocument().voices.slice(0, 6) },  // a short bank: the patch count is printed
  ]) {
    assert.notEqual(base, await computeManualKey(banked(bankDocument(overrides)), "1"));
  }

  // …as does moving the same bank to another slot, since the guide credits it
  // against that slot's model entry.
  assert.notEqual(base, await computeManualKey(banked(bankDocument(), 1), "1"));

  // But the packed patch bytes do not: the guide credits a bank rather than
  // listing its patches, so those recipes really do render the same PDF.
  const edited = bankDocument();
  edited.voices[0].packed[0] = 42;
  assert.equal(base, await computeManualKey(banked(edited), "1"));

  // A stock recipe and a customized one are never the same guide.
  assert.notEqual(base, await computeManualKey(normalizeRecipe(recipeWith([])), "1"));
});

test("version 8 accepts up to nine chord tables and normalizes to v8", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const localTable = (n: number) => ({
    ...structuredClone(chordCatalog.tables[0]),
    id: `filler-${n}`,
    packageId: `local/filler-${n}`,
    version: "draft",
    digest: null,
    name: `Filler ${n}`,
    author: "You",
    origin: "Local",
    description: "A local table.",
  });
  const nineTables = Array.from({ length: 9 }, (_, i) => localTable(i + 1));
  const recipe = {
    ...fixture,
    schemaVersion: 8,
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
      chordTable: "filler-1",
      holdOnTrigger: false,
    },
    resources: { chordTables: nineTables },
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 8);
  assert.equal(normalized.resources.chordTables.length, 9);

  // A tenth table is rejected.
  recipe.resources.chordTables = [...nineTables, localTable(10)];
  assert.throws(() => normalizeRecipe(recipe), /between one and 9 chord tables/);

  // Boundary: six tables need no fast-blink tier, so they stay slot-derived (v5).
  recipe.schemaVersion = 5;
  recipe.resources.chordTables = nineTables.slice(0, 6);
  assert.equal(normalizeRecipe(recipe).schemaVersion, 5);
});

test("the stereo aux-output mode requires and normalizes to v9", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const table = structuredClone(chordCatalog.tables[0]);
  const recipe = {
    ...fixture,
    schemaVersion: 9,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves",
      modelInput: "model",
      levelInput: "level",
      auxOutput: "stereo",
      suboscillatorOctave: 0,
      chordTable: table.id,
      holdOnTrigger: false,
    },
    resources: { chordTables: [table] },
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.initialOptions.auxOutput, "stereo");
  assert.equal(normalized.schemaVersion, 9);

  // Stereo dominates lower feature versions: even a plain 24-slot recipe with a
  // single chord table lifts to v9 the moment stereo is selected.
  assert.equal(normalized.slots.length, 24);

  // Switching aux back off drops the recipe to its slot-derived version.
  recipe.initialOptions.auxOutput = "alternate-model";
  assert.equal(normalizeRecipe(recipe).schemaVersion, 5);

  // An unknown aux value is still rejected.
  recipe.initialOptions.auxOutput = "quadraphonic";
  assert.throws(() => normalizeRecipe(recipe), /unsupported firmware option/);
});

test("per-engine stereo (stereoEngines) requires and normalizes to v10", async () => {
  const publicCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url),
    "utf8",
  ));
  const chordCatalog = JSON.parse(await readFile(
    new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url),
    "utf8",
  ));
  const byId = new Map(publicCatalog.engines.map((engine: { id: string }) => [engine.id, engine]));
  const table = structuredClone(chordCatalog.tables[0]);
  const recipe: Record<string, unknown> = {
    ...fixture,
    schemaVersion: 10,
    slots: fixture.slots.map((engineId: string) => {
      const engine = byId.get(engineId) as { packageId: string; version: string; digest: string };
      return { engine: engineId, package: engine.packageId, version: engine.version, digest: engine.digest };
    }),
    preferences: { navigationMode: "linear" },
    initialOptions: {
      lockedFrequencyKnob: "octaves", modelInput: "model", levelInput: "level",
      auxOutput: "stereo", suboscillatorOctave: 0, chordTable: table.id, holdOnTrigger: false,
    },
    resources: { chordTables: [table] },
    stereoEngines: ["chiptune", "modal-resonator", "chiptune"],
  };
  const normalized = normalizeRecipe(recipe);
  assert.equal(normalized.schemaVersion, 10);
  assert.deepEqual(normalized.stereoEngines, ["chiptune", "modal-resonator"]); // deduped

  // A v10 recipe must carry the list.
  const missing = { ...recipe }; delete missing.stereoEngines;
  assert.throws(() => normalizeRecipe(missing), /must carry a stereoEngines/);

  // stereoEngines is only valid with the stereo aux option.
  const monoAux = { ...recipe, initialOptions: { ...(recipe.initialOptions as object), auxOutput: "alternate-model" } };
  assert.throws(() => normalizeRecipe(monoAux), /only valid with the stereo aux/);

  // Unknown engine ids are rejected.
  assert.throws(() => normalizeRecipe({ ...recipe, stereoEngines: ["not-an-engine"] }), /approved engine ids/);

  // A schema-9 recipe with a stereoEngines list is refused (needs v10).
  assert.throws(() => normalizeRecipe({ ...recipe, schemaVersion: 9 }), /requires recipe schema version 10/);

  // Every later supported schema inherits v10's feature. This loop grows
  // automatically with the ceiling, so a future upper whitelist cannot silently
  // strand stereoEngines again.
  for (let version = 10; version <= maxRecipeSchemaVersion; version += 1) {
    const laterRecipe = {
      ...recipe,
      schemaVersion: version,
      initialOptions: version >= 18
        ? { ...(recipe.initialOptions as object), attenuverterMode: "stock" }
        : recipe.initialOptions,
      resources: version === 12 || version === 13
        ? { chordTables: [table], userDataBanks: [] }
        : version === 16
          ? { chordTables: [table], scaleBank: structuredClone(scaleBank) }
          : { chordTables: [table] },
    };
    assert.deepEqual(
      normalizeRecipe(laterRecipe).stereoEngines,
      ["chiptune", "modal-resonator"],
      `schema version ${version} must inherit per-engine stereo`,
    );
  }
});

// The rejection message is what a plaits-api.rubato.audio caller reads, so it
// must name the range the guard actually accepts — it once still said "2
// through 11" after v12 (per-slot custom FM banks) had been accepted.
test("the unsupported-schema message names the range the guard accepts", () => {
  const highest = maxRecipeSchemaVersion;
  // Reports the schema code a version is rejected with, or null if the version
  // itself was accepted (a later invalid_slots / unapproved_package complaint
  // about the fixture's shape means the version passed this guard).
  const rejection = (version: number): string | null => {
    try {
      normalizeRecipe({ ...fixture, schemaVersion: version });
      return null;
    } catch (error) {
      const { code, message } = error as { code?: string; message: string };
      return code === "unsupported_schema" ? message : null;
    }
  };
  // One past the top is refused, and the message names the true upper bound.
  const refused = rejection(highest + 1);
  assert.match(
    String(refused),
    new RegExp(`versions ${minRecipeSchemaVersion} through ${highest} can be built`),
  );
  // ...and the guard really does accept every version that message claims.
  for (let version = minRecipeSchemaVersion; version <= highest; version += 1) {
    assert.equal(rejection(version), null, `schema version ${version} must not be rejected as unsupported`);
  }
});
