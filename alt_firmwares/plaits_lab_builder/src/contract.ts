import catalog from "../../plaits_lab_catalog/public_catalog.json" with { type: "json" };
import chordCatalog from "../../plaits_lab_chord_tables/catalog.json" with { type: "json" };

export const approvedEngineIds: readonly string[] = catalog.engines.map((engine) => engine.id);
const approvedEngines = new Map(catalog.engines.map((engine) => [engine.id, engine]));
const approvedEngineIdSet = new Set<string>(approvedEngineIds);
export const maxChordTables = 9;
// The six states the original LED scheme could show (three colors x
// solid/blink). Tables 7-9 need the fast-blink LED tier, which only a v8+
// builder supports — so a recipe with more than this normalizes to schema v8.
export const maxLegacyChordTables = 6;
export const approvedChordTables = chordCatalog.tables;

export type NormalizedChord = {
  id: string;
  name: string;
  voices: [number, number, number, number];
  arpLength: 1 | 2 | 3 | 4;
};

export type NormalizedChordTable = {
  id: string;
  packageId: string;
  version: string;
  digest: string | null;
  name: string;
  author: string;
  license: string;
  origin: "Mutable Instruments" | "Community" | "Local";
  description: string;
  chords: NormalizedChord[];
};

// Recipe schemas are monotonic: a later schema inherits every earlier feature.
// Keep one deliberate supported range for rejecting genuinely unknown formats;
// feature gates below use minimums so a newly supported schema cannot get
// stranded behind an old "10, 11, 12..." whitelist.
export const minRecipeSchemaVersion = 2;
export const maxRecipeSchemaVersion = 15;
const configurationMinSchemaVersion = 4;
const resourcesMinSchemaVersion = 5;
const fourBankMinSchemaVersion = 6;       // 32 slots
const sparseSlotMinSchemaVersion = 7;     // empty slots
const stereoEngineMinSchemaVersion = 10; // stereoEngines list
const sparseBankMinSchemaVersion = 11;    // gaps inside a bank
const slotBankMinSchemaVersion = 12;      // banks keyed by palette slot
const shortBankMinSchemaVersion = 13;     // fewer than 32 FM voices
const calibrationMinSchemaVersion = 14;   // CV calibration procedure
const rovedMinSchemaVersion = 15;         // four-knob Ro'Ved panel
const colorBlindModeMinSchemaVersion = 15; // brightness-coded banks

function isSupportedSchemaVersion(value: unknown): value is number {
  return Number.isInteger(value)
    && Number(value) >= minRecipeSchemaVersion
    && Number(value) <= maxRecipeSchemaVersion;
}

function describeSchemaRange(minimum: number, maximum = maxRecipeSchemaVersion): string {
  return `${minimum} through ${maximum}`;
}

export const maxUserDataBanks = 3;
export const patchesPerBank = 32;
export const packedPatchSize = 128;

export type NormalizedBankVoice = {
  name: string;
  algorithm: number;
  packed: number[];
};

export type NormalizedUserDataBank = {
  index: number;
  bank: {
    id: string;
    packageId: string;
    version: string;
    digest: string | null;
    name: string;
    author: string;
    license: string;
    origin: "Mutable Instruments" | "Community" | "Local";
    description: string;
    voices: NormalizedBankVoice[];
  };
};

// v12: a custom bank keyed by the palette slot it belongs to.
export type NormalizedSlotBank = {
  slot: number;
  bank: NormalizedUserDataBank["bank"];
};

export type NormalizedRecipe = {
  schemaVersion: 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15;
  target: "mutable-instruments-plaits" | "plum-audio-roved";
  firmware: "rubato-plaits";
  // A null entry is an empty slot (v7 short banks); filled slots are engine ids.
  slots: (string | null)[];
  preferences: {
    navigationMode: "linear" | "banked";
    // Include the module's CV calibration procedure (v14). Off by default: the
    // calibration it would produce already survives in its own flash chunk, so
    // this is only for a module that has never been calibrated or was erased
    // over SWD — and it costs flash a full palette does not have to spare.
    calibration: boolean;
    // Bake an accessible model-bank display into the firmware (v15): all model
    // lights use yellow, with one steady PWM brightness per bank. It deliberately
    // has no power-up gesture, leaving right-button boot available to calibration.
    colorBlindMode: boolean;
  };
  initialOptions: {
    lockedFrequencyKnob: "octaves" | "decay" | "aux-crossfade" | "macro-4";
    modelInput: "model" | "lpg-colour" | "aux-crossfade" | "macro-4";
    levelInput: "level" | "decay";
    auxOutput: "alternate-model" | "square-subosc" | "sine-subosc" | "stereo";
    suboscillatorOctave: 0 | -1 | -2;
    chordTable: string;
    holdOnTrigger: boolean;
  };
  resources: {
    chordTables: NormalizedChordTable[];
    // v6 carries index-keyed banks; v12 carries per-slot banks. (The empty
    // fourth-bank marker on a 32-slot v7-v11 recipe is an empty array.)
    userDataBanks?: NormalizedUserDataBank[] | NormalizedSlotBank[];
  };
  // Catalog ids of the engines built with the stereo render path (introduced in
  // schema 10). Absent on schema <= 9 (the global-stereo recipes, which the
  // builder treats as all stereo-capable engines when the aux option is stereo).
  stereoEngines?: string[];
  output: "audio-wav";
};

const defaultConfiguration: Pick<NormalizedRecipe, "preferences" | "initialOptions"> = {
  preferences: { navigationMode: "linear", calibration: false, colorBlindMode: false },
  initialOptions: {
    lockedFrequencyKnob: "octaves",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
  },
};

const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const digestPattern = /^sha256:[0-9a-f]{64}$/;
const approvedChordTablesById = new Map(chordCatalog.tables.map((table) => [table.id, table]));

function shortText(value: unknown, maximum: number): value is string {
  return typeof value === "string" && value.trim().length > 0 && value.length <= maximum;
}

function normalizeChordTables(value: unknown): NormalizedChordTable[] {
  if (!Array.isArray(value) || value.length < 1 || value.length > maxChordTables) {
    throw new ContractError("invalid_chord_tables", `A firmware must contain between one and ${maxChordTables} chord tables.`);
  }
  const tableIds = new Set<string>();
  return value.map((item) => {
    if (!item || typeof item !== "object") {
      throw new ContractError("invalid_chord_table", "The recipe contains an invalid chord table.");
    }
    const table = item as Record<string, unknown>;
    if (!shortText(table.id, 80) || !idPattern.test(table.id) || tableIds.has(table.id)
        || !shortText(table.packageId, 120) || !shortText(table.version, 32)
        || !(table.digest === null || (typeof table.digest === "string" && digestPattern.test(table.digest)))
        || !shortText(table.name, 80) || !shortText(table.author, 80)
        || !shortText(table.license, 32)
        || !["Mutable Instruments", "Community", "Local"].includes(String(table.origin))
        || !shortText(table.description, 240)
        || !Array.isArray(table.chords) || table.chords.length < 1 || table.chords.length > 24) {
      throw new ContractError("invalid_chord_table", "A chord table contains unsupported metadata.");
    }
    tableIds.add(table.id);
    const chordIds = new Set<string>();
    const chords = table.chords.map((item) => {
      if (!item || typeof item !== "object") {
        throw new ContractError("invalid_chord", "A chord-table position is invalid.");
      }
      const chord = item as Record<string, unknown>;
      if (!shortText(chord.id, 80) || !idPattern.test(chord.id) || chordIds.has(chord.id)
          || !shortText(chord.name, 80)
          || !Array.isArray(chord.voices) || chord.voices.length !== 4
          || chord.voices.some((voice) => !Number.isInteger(voice) || Number(voice) < -4800 || Number(voice) > 7200)
          || ![1, 2, 3, 4].includes(Number(chord.arpLength))) {
        throw new ContractError("invalid_chord", "A chord must contain four bounded cent offsets.");
      }
      chordIds.add(chord.id);
      return {
        id: chord.id,
        name: chord.name,
        voices: [...chord.voices] as NormalizedChord["voices"],
        arpLength: chord.arpLength as NormalizedChord["arpLength"],
      };
    });
    const normalized: NormalizedChordTable = {
      id: table.id,
      packageId: table.packageId,
      version: table.version,
      digest: table.digest as string | null,
      name: table.name,
      author: table.author,
      license: table.license,
      origin: table.origin as NormalizedChordTable["origin"],
      description: table.description,
      chords,
    };
    if (normalized.digest !== null) {
      const approved = approvedChordTablesById.get(normalized.id);
      if (!approved || JSON.stringify(normalized) !== JSON.stringify(approved)) {
        throw new ContractError("unapproved_chord_table", "A published chord table does not match its immutable catalog version.");
      }
    } else if (normalized.origin !== "Local" || !normalized.packageId.startsWith("local/")) {
      throw new ContractError("invalid_chord_table", "Editable chord tables must be device-local drafts.");
    }
    return normalized;
  });
}

// Validate one custom bank's metadata + 32 voices. License is intentionally NOT
// constrained to a share-safe set: baking a bank into one's OWN firmware is a
// private act; the share-license gate lives in the contributor pipeline.
function normalizeBankDocument(value: unknown): NormalizedUserDataBank["bank"] {
  const bank = value as Record<string, unknown>;
  if (!bank || typeof bank !== "object"
      || !shortText(bank.id, 80) || !idPattern.test(bank.id)
      || !shortText(bank.packageId, 120) || !shortText(bank.version, 32)
      || !(bank.digest === null || (typeof bank.digest === "string" && digestPattern.test(bank.digest)))
      || !shortText(bank.name, 80) || !shortText(bank.author, 80) || !shortText(bank.license, 32)
      || !["Mutable Instruments", "Community", "Local"].includes(String(bank.origin))
      || !shortText(bank.description, 240)
      || !Array.isArray(bank.voices) || bank.voices.length < 1 || bank.voices.length > patchesPerBank) {
    throw new ContractError("invalid_user_data_bank", "A custom bank contains unsupported metadata or is not 1–32 voices.");
  }
  const voices: NormalizedBankVoice[] = bank.voices.map((raw) => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_user_data_bank", "A custom-bank voice is invalid.");
    }
    const voice = raw as Record<string, unknown>;
    if (typeof voice.name !== "string" || voice.name.length > 16
        || !Number.isInteger(voice.algorithm) || Number(voice.algorithm) < 1 || Number(voice.algorithm) > 32
        || !Array.isArray(voice.packed) || voice.packed.length !== packedPatchSize
        || voice.packed.some((byte) => !Number.isInteger(byte) || Number(byte) < 0 || Number(byte) > 127)) {
      throw new ContractError("invalid_user_data_bank", "A custom-bank voice must have 128 packed 7-bit bytes.");
    }
    return { name: voice.name, algorithm: Number(voice.algorithm), packed: [...(voice.packed as number[])] };
  });
  return {
    id: bank.id as string, packageId: bank.packageId as string, version: bank.version as string,
    digest: bank.digest as string | null, name: bank.name as string, author: bank.author as string,
    license: bank.license as string, origin: bank.origin as NormalizedUserDataBank["bank"]["origin"],
    description: bank.description as string, voices,
  };
}

// v6: custom banks keyed by a built-in bank index (0-2).
function normalizeUserDataBanks(value: unknown): NormalizedUserDataBank[] {
  if (!Array.isArray(value) || value.length > maxUserDataBanks) {
    throw new ContractError("invalid_user_data_banks", `A firmware may override between zero and ${maxUserDataBanks} FM banks.`);
  }
  const indices = new Set<number>();
  return value.map((item) => {
    if (!item || typeof item !== "object") {
      throw new ContractError("invalid_user_data_bank", "The recipe contains an invalid custom bank.");
    }
    const entry = item as Record<string, unknown>;
    if (!hasExactKeys(entry, ["index", "bank"])
        || !Number.isInteger(entry.index) || Number(entry.index) < 0 || Number(entry.index) >= maxUserDataBanks
        || indices.has(Number(entry.index))) {
      throw new ContractError("invalid_user_data_bank", "A custom bank must target a distinct built-in FM bank (0–2).");
    }
    indices.add(Number(entry.index));
    return { index: Number(entry.index), bank: normalizeBankDocument(entry.bank) };
  });
}

// v12: custom banks keyed by palette SLOT — one per customized FM slot, so the
// only bound is one bank per slot (the flash budget, enforced by the ARM build).
function normalizeSlotBanks(value: unknown, numSlots: number): NormalizedSlotBank[] {
  if (!Array.isArray(value) || value.length > numSlots) {
    throw new ContractError("invalid_user_data_banks", "The recipe contains an unsupported set of custom banks.");
  }
  const slots = new Set<number>();
  return value.map((item) => {
    if (!item || typeof item !== "object") {
      throw new ContractError("invalid_user_data_bank", "The recipe contains an invalid custom bank.");
    }
    const entry = item as Record<string, unknown>;
    if (!hasExactKeys(entry, ["slot", "bank"])
        || !Number.isInteger(entry.slot) || Number(entry.slot) < 0 || Number(entry.slot) >= numSlots
        || slots.has(Number(entry.slot))) {
      throw new ContractError("invalid_user_data_bank", "A custom bank must target a distinct palette slot.");
    }
    slots.add(Number(entry.slot));
    return { slot: Number(entry.slot), bank: normalizeBankDocument(entry.bank) };
  });
}

export class ContractError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

function isOneOf<T extends string | number>(value: unknown, choices: readonly T[]): value is T {
  return choices.includes(value as T);
}

function hasExactKeys(value: Record<string, unknown>, keys: string[]): boolean {
  const actual = Object.keys(value).sort();
  const expected = [...keys].sort();
  return actual.length === expected.length && actual.every((key, index) => key === expected[index]);
}

// A bank is "sparse" when an empty slot has a filled slot after it in the same
// 8-slot bank — a gap the user kept in place rather than compacting to the front.
function hasSparseBank(slots: (string | null)[]): boolean {
  for (let start = 0; start < slots.length; start += 8) {
    let seenEmpty = false;
    for (const slot of slots.slice(start, start + 8)) {
      if (slot === null) {
        seenEmpty = true;
      } else if (seenEmpty) {
        return true;
      }
    }
  }
  return false;
}

function validateBankShape(slots: (string | null)[], schemaVersion: number): void {
  if (slots.every((slot) => slot === null)) {
    throw new ContractError("invalid_slots", "A firmware recipe must contain at least one engine.");
  }
  // A bank's engines may be sparse (a gap kept in place) only on a v11-or-later
  // recipe — the firmware then lights each engine on its own physical row. Older
  // recipes must keep each 8-slot bank's engines contiguous at its front, the
  // shape the pre-sparse per-bank navigation assumed.
  if (hasSparseBank(slots) && schemaVersion < sparseBankMinSchemaVersion) {
    throw new ContractError(
      "invalid_slots",
      "A bank's engines must be contiguous, with empty slots only at the end, unless the recipe uses"
        + ` schema version ${sparseBankMinSchemaVersion} or later.`,
    );
  }
}

function normalizeConfiguration(
  candidate: Record<string, unknown>,
  chordTables: NormalizedChordTable[],
): Pick<NormalizedRecipe, "preferences" | "initialOptions"> {
  const preferences = candidate.preferences;
  const initialOptions = candidate.initialOptions;
  if (!preferences || typeof preferences !== "object"
      || !initialOptions || typeof initialOptions !== "object") {
    throw new ContractError("invalid_preferences", "The recipe must contain firmware preferences and starting options.");
  }
  const preferenceValues = preferences as Record<string, unknown>;
  const optionValues = initialOptions as Record<string, unknown>;
  // Compile-time preferences arrived incrementally: pre-v14 recipes carry only
  // navigationMode; v14 adds calibration; v15 adds colorBlindMode. Missing keys
  // mean false, while every other shape remains closed and rejected.
  const legacyPreferences = hasExactKeys(preferenceValues, ["navigationMode"]);
  const carriesCalibration = hasExactKeys(preferenceValues, ["calibration", "navigationMode"]);
  const carriesColorBlindMode = hasExactKeys(
    preferenceValues,
    ["calibration", "colorBlindMode", "navigationMode"],
  );
  if ((!legacyPreferences && !carriesCalibration && !carriesColorBlindMode)
      || ((carriesCalibration || carriesColorBlindMode)
        && typeof preferenceValues.calibration !== "boolean")
      || (carriesColorBlindMode && typeof preferenceValues.colorBlindMode !== "boolean")
      || !hasExactKeys(optionValues, [
        "auxOutput", "chordTable", "holdOnTrigger", "levelInput",
        "lockedFrequencyKnob", "modelInput", "suboscillatorOctave",
      ])
      || !isOneOf(preferenceValues.navigationMode, ["linear", "banked"] as const)
      || !isOneOf(optionValues.lockedFrequencyKnob, ["octaves", "decay", "aux-crossfade", "macro-4"] as const)
      || !isOneOf(optionValues.modelInput, ["model", "lpg-colour", "aux-crossfade", "macro-4"] as const)
      || !isOneOf(optionValues.levelInput, ["level", "decay"] as const)
      || !isOneOf(optionValues.auxOutput, ["alternate-model", "square-subosc", "sine-subosc", "stereo"] as const)
      || !isOneOf(optionValues.suboscillatorOctave, [0, -1, -2] as const)
      || typeof optionValues.chordTable !== "string"
      || !chordTables.some((table) => table.id === optionValues.chordTable)
      || typeof optionValues.holdOnTrigger !== "boolean") {
    throw new ContractError("invalid_preferences", "The recipe contains an unsupported firmware option.");
  }
  const calibration = (carriesCalibration || carriesColorBlindMode)
    && preferenceValues.calibration === true;
  const colorBlindMode = carriesColorBlindMode && preferenceValues.colorBlindMode === true;
  // Same shape as the sparse-bank and short-FM-bank gates: a recipe may only ask
  // for a feature its declared schema version covers, so a client that has not
  // been told this builder understands calibration cannot slip it in under an
  // older version number.
  if (calibration && Number(candidate.schemaVersion) < calibrationMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `The calibration procedure requires recipe schema version ${calibrationMinSchemaVersion}.`,
    );
  }
  if (colorBlindMode && Number(candidate.schemaVersion) < colorBlindModeMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `The color-blind bank display requires recipe schema version ${colorBlindModeMinSchemaVersion}.`,
    );
  }
  return {
    preferences: {
      navigationMode: preferenceValues.navigationMode,
      calibration,
      colorBlindMode,
    },
    initialOptions: {
      lockedFrequencyKnob: optionValues.lockedFrequencyKnob,
      modelInput: optionValues.modelInput,
      levelInput: optionValues.levelInput,
      auxOutput: optionValues.auxOutput,
      suboscillatorOctave: optionValues.suboscillatorOctave,
      chordTable: optionValues.chordTable,
      holdOnTrigger: optionValues.holdOnTrigger,
    },
  };
}

// Per-engine stereo (introduced in schema 10). A stereoEngines list names the
// approved engines built with the stereo render path; it is only meaningful with
// the stereo aux option. Returns the deduped list, or undefined for a schema <= 9
// recipe (which the builder treats as all stereo-capable engines when aux is
// stereo).
function normalizeStereoEngines(
  candidate: Record<string, unknown>,
  schemaVersion: number,
  auxOutput: string,
): string[] | undefined {
  const present = "stereoEngines" in candidate;
  if (schemaVersion === stereoEngineMinSchemaVersion && !present) {
    throw new ContractError("invalid_recipe", "A version 10 recipe must carry a stereoEngines list.");
  }
  if (!present) return undefined;
  // v10's defining feature; every later supported schema inherits it and may
  // carry the list when its aux output is stereo.
  if (schemaVersion < stereoEngineMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `stereoEngines requires recipe schema version ${stereoEngineMinSchemaVersion} or newer.`,
    );
  }
  if (auxOutput !== "stereo") {
    throw new ContractError("invalid_recipe", "stereoEngines is only valid with the stereo aux output.");
  }
  const raw = candidate.stereoEngines;
  if (!Array.isArray(raw)
      || !raw.every((id) => typeof id === "string" && approvedEngineIdSet.has(id))) {
    throw new ContractError("invalid_recipe", "stereoEngines must list approved engine ids.");
  }
  return [...new Set(raw as string[])];
}

export function normalizeRecipe(value: unknown): NormalizedRecipe {
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_recipe", "The build recipe must be a JSON object.");
  }
  const candidate = value as Record<string, unknown>;
  if (!isSupportedSchemaVersion(candidate.schemaVersion)) {
    throw new ContractError(
      "unsupported_schema",
      `Only Plaits Palette recipe schema versions ${describeSchemaRange(minRecipeSchemaVersion)} can be built.`,
    );
  }
  const schemaVersion = candidate.schemaVersion;
  if (!["mutable-instruments-plaits", "plum-audio-roved"].includes(String(candidate.target))
      || candidate.firmware !== "rubato-plaits") {
    throw new ContractError("unsupported_target", "That recipe targets a different firmware family.");
  }
  if (candidate.target === "plum-audio-roved"
      && Number(candidate.schemaVersion) < rovedMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `Ro'Ved builds require recipe schema version ${rovedMinSchemaVersion}.`,
    );
  }
  if (candidate.output !== "audio-wav") {
    throw new ContractError("unsupported_output", "Only audio-installable WAV firmware is supported.");
  }
  if (!Array.isArray(candidate.slots) || (candidate.slots.length !== 24 && candidate.slots.length !== 32)) {
    throw new ContractError("invalid_slots", "A firmware recipe must contain 24 engine slots, or 32 for a four-bank build.");
  }
  if (candidate.slots.length === 32 && schemaVersion < fourBankMinSchemaVersion) {
    throw new ContractError(
      "invalid_slots",
      `32-slot recipes require recipe schema versions ${describeSchemaRange(fourBankMinSchemaVersion)}.`,
    );
  }
  const slots: (string | null)[] = schemaVersion === 2
    ? candidate.slots.map((id) => {
        if (typeof id !== "string" || !approvedEngineIdSet.has(id)) {
          throw new ContractError("unapproved_engine", "The recipe contains an engine that is not approved for builds.");
        }
        return id;
      })
    : candidate.slots.map((value) => {
        if (value === null) {
          // An empty slot — only short-bank / sparse (v7+) recipes may carry them.
          if (schemaVersion < sparseSlotMinSchemaVersion) {
            throw new ContractError(
              "invalid_slots",
              `Empty slots require recipe schema versions ${describeSchemaRange(sparseSlotMinSchemaVersion)}.`,
            );
          }
          return null;
        }
        if (!value || typeof value !== "object") {
          throw new ContractError("invalid_package", "The recipe contains an invalid package reference.");
        }
        const reference = value as Record<string, unknown>;
        const approved = typeof reference.engine === "string" ? approvedEngines.get(reference.engine) : undefined;
        if (!approved
            || reference.package !== approved.packageId
            || reference.version !== approved.version
            || reference.digest !== approved.digest) {
          throw new ContractError("unapproved_package", "The recipe contains an unavailable package version.");
        }
        return approved.id;
      });
  validateBankShape(slots, schemaVersion);
  let chordTables: NormalizedChordTable[];
  let userDataBanks: NormalizedUserDataBank[] | undefined;   // v6 index-keyed
  let slotBanks: NormalizedSlotBank[] | undefined;           // v12 slot-keyed
  if (schemaVersion >= resourcesMinSchemaVersion) {
    const resources = candidate.resources;
    // v6 always carries index-keyed banks; v12 always carries per-slot banks (its
    // defining feature, 24 or 32 slots). v7-v11 mirror the editor: userDataBanks
    // only for a 32-slot (fourth-bank) recipe; a 24-slot v7-v11 carries chord
    // tables only, like v5.
    const expectsUserDataBanks = schemaVersion === fourBankMinSchemaVersion
      || schemaVersion === slotBankMinSchemaVersion
      || schemaVersion === shortBankMinSchemaVersion
      || (schemaVersion >= sparseSlotMinSchemaVersion
        && schemaVersion < slotBankMinSchemaVersion && candidate.slots.length === 32);
    // v14+ compile-time features say nothing about resources: calibration,
    // the Ro'Ved panel, and the color-blind display can each compose with any
    // palette, with or without custom FM banks. Banks still validate exactly
    // as under v13 when present.
    if (!resources || typeof resources !== "object") {
      throw new ContractError("invalid_resources", "The recipe must contain only supported firmware resources.");
    }
    const resourceValues = resources as Record<string, unknown>;
    const carriesUserDataBanks = expectsUserDataBanks
      || (schemaVersion >= calibrationMinSchemaVersion
        && hasExactKeys(resourceValues, ["chordTables", "userDataBanks"]));
    const expectedKeys = carriesUserDataBanks ? ["chordTables", "userDataBanks"] : ["chordTables"];
    if (!hasExactKeys(resourceValues, expectedKeys)) {
      throw new ContractError("invalid_resources", "The recipe must contain only supported firmware resources.");
    }
    chordTables = normalizeChordTables(resourceValues.chordTables);
    if (carriesUserDataBanks) {
      const rawBanks = resourceValues.userDataBanks;
      if (schemaVersion >= slotBankMinSchemaVersion) {
        slotBanks = normalizeSlotBanks(rawBanks, candidate.slots.length);
      } else {
        userDataBanks = normalizeUserDataBanks(rawBanks);
      }
      // A bank with fewer than 32 patches needs the firmware's variable-length
      // Harmonics quantizer, advertised as schema version 13. An older builder
      // would bake it but keep the fixed 32-step dial, so gate it here — mirrors
      // the container-side generator.
      const banks = [...(userDataBanks ?? []), ...(slotBanks ?? [])];
      if (banks.some((b) => b.bank.voices.length < patchesPerBank)
          && schemaVersion < shortBankMinSchemaVersion) {
        throw new ContractError(
          "unsupported_schema",
          `Short FM banks require recipe schema version ${shortBankMinSchemaVersion}.`,
        );
      }
    }
  } else {
    chordTables = normalizeChordTables(structuredClone(chordCatalog.tables));
  }
  const configuration = schemaVersion >= configurationMinSchemaVersion
    ? normalizeConfiguration(candidate, chordTables)
    : defaultConfiguration;
  // Per-engine stereo (introduced in schema 10): a stereoEngines list names the
  // engines built with the stereo render path. Only valid when the aux option is
  // stereo.
  const stereoEngines = normalizeStereoEngines(
    candidate,
    schemaVersion,
    configuration.initialOptions.auxOutput,
  );
  return {
    // Newest first: Ro'Ved's four-clickable-knob UI and the accessible display
    // each require v15 and dominate all lower feature rungs. Then the optional
    // calibration procedure needs v14. All three say nothing about the recipe's
    // resource shape. A per-slot custom bank with fewer than 32 patches (a
    // "short" FM bank) needs the firmware's variable-length Harmonics quantizer,
    // v13. Any other per-slot custom bank needs a v12 builder (only v12
    // keys banks by slot). Then a sparse bank (a gap kept in place) needs v11;
    // per-engine stereo (a stereoEngines list) needs v10; the global stereo aux
    // mode needs v9; more than six chord tables needs the fast-blink LED tier
    // (v8); a short-bank recipe (a trailing empty slot) stays v7; a candidate that
    // carried v6 resources (even an empty custom-bank list, e.g. a 32-slot recipe)
    // stays v6; else v5.
    schemaVersion: candidate.target === "plum-audio-roved"
        || configuration.preferences.colorBlindMode ? 15
      : configuration.preferences.calibration ? 14
      : slotBanks !== undefined
        ? (slotBanks.some((b) => b.bank.voices.length < patchesPerBank) ? 13 : 12)
      : hasSparseBank(slots) ? 11
      : stereoEngines !== undefined ? 10
      : configuration.initialOptions.auxOutput === "stereo" ? 9
      : chordTables.length > maxLegacyChordTables ? 8
      : slots.some((slot) => slot === null) ? 7
      : (userDataBanks !== undefined ? 6 : 5),
    target: candidate.target as NormalizedRecipe["target"],
    firmware: "rubato-plaits",
    slots,
    preferences: { ...configuration.preferences },
    initialOptions: { ...configuration.initialOptions },
    ...(stereoEngines !== undefined ? { stereoEngines } : {}),
    resources: (userDataBanks ?? slotBanks)
      ? { chordTables, userDataBanks: userDataBanks ?? slotBanks }
      : { chordTables },
    output: "audio-wav",
  };
}

// A manual's identity is everything the PDF PRINTS: the slot layout, each
// selected engine's DOCUMENTATION digest, the chord tables the options-menu page
// lists, and the credit each custom FM bank contributes. Deliberately NOT the
// firmware source revision or toolchain, so firmware-only rollouts keep reusing
// cached manuals and prose-only edits never invalidate firmware — and not the
// packed patch bytes either, since the guide credits a bank rather than listing
// its patches, so two banks with identical credits DO render the same guide.
// (Anything the renderer starts printing has to be added here and the manual
// contract bumped, or the cache serves a guide describing a different recipe.)
export async function computeManualKey(
  recipe: NormalizedRecipe,
  manualContract: string,
): Promise<string> {
  const documentation = [...new Set(recipe.slots)]
    .filter((engineId): engineId is string => engineId !== null)
    .sort()
    .map((engineId) => {
      const engine = approvedEngines.get(engineId);
      if (!engine) throw new ContractError("unapproved_engine", "The recipe contains an engine that is not approved for builds.");
      return [engineId, engine.documentationDigest];
    });
  // Both bank shapes carry their key (v6 `index`, v12/v13 `slot`) into the fold,
  // so a bank that moves between slots — or from one factory bank to another —
  // is a different guide even when its credit is unchanged.
  const banks: (NormalizedUserDataBank | NormalizedSlotBank)[] = recipe.resources.userDataBanks ?? [];
  const customBanks = banks.map((entry) => [
    "slot" in entry ? `slot:${entry.slot}` : `index:${entry.index}`,
    entry.bank.name,
    entry.bank.author,
    entry.bank.origin,
    entry.bank.description,
    entry.bank.voices.length,
  ]);
  const canonical = JSON.stringify({
    manualContract,
    slots: recipe.slots,
    documentation,
    chordTables: recipe.resources.chordTables.map((table) => table.name),
    customBanks,
    // The control instructions differ completely: Plaits has two buttons;
    // Ro'Ved has four clickable knobs. Never share a cached guide between them.
    target: recipe.target,
    // Both compile-time display/procedure preferences change what the guide
    // prints, so they must change its cache identity.
    calibration: recipe.preferences.calibration,
    colorBlindMode: recipe.preferences.colorBlindMode,
  });
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(canonical));
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function computeBuildKey(
  recipe: NormalizedRecipe,
  buildIdentity: { sourceRevision: string; toolchain: string; contract: string },
): Promise<string> {
  const canonical = JSON.stringify({
    contract: buildIdentity.contract,
    sourceRevision: buildIdentity.sourceRevision,
    toolchain: buildIdentity.toolchain,
    recipe,
  });
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(canonical));
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function isBuildKey(value: string): boolean {
  return /^[0-9a-f]{64}$/.test(value);
}
