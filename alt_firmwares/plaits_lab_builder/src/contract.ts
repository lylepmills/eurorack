import catalog from "../../plaits_lab_catalog/public_catalog.json" with { type: "json" };
import chordCatalog from "../../plaits_lab_chord_tables/catalog.json" with { type: "json" };

export const approvedEngineIds: readonly string[] = catalog.engines.map((engine) => engine.id);
const approvedEngines = new Map(catalog.engines.map((engine) => [engine.id, engine]));
const approvedEngineIdSet = new Set<string>(approvedEngineIds);
export const maxChordTables = 6;
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

export type NormalizedRecipe = {
  schemaVersion: 5 | 6;
  target: "mutable-instruments-plaits";
  firmware: "rubato-plaits";
  slots: string[];
  preferences: {
    navigationMode: "linear" | "banked";
  };
  initialOptions: {
    lockedFrequencyKnob: "octaves" | "decay" | "aux-crossfade" | "macro-4";
    modelInput: "model" | "lpg-colour" | "aux-crossfade";
    levelInput: "level" | "decay" | "macro-4";
    auxOutput: "alternate-model" | "square-subosc" | "sine-subosc";
    suboscillatorOctave: 0 | -1 | -2;
    chordTable: string;
    holdOnTrigger: boolean;
  };
  resources: {
    chordTables: NormalizedChordTable[];
    // Present only on a v6 recipe (at least one custom bank).
    userDataBanks?: NormalizedUserDataBank[];
  };
  output: "audio-wav";
};

const defaultConfiguration: Pick<NormalizedRecipe, "preferences" | "initialOptions"> = {
  preferences: { navigationMode: "linear" },
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
    const bank = entry.bank as Record<string, unknown>;
    // License is intentionally NOT constrained to a share-safe set: baking a bank
    // into one's OWN firmware is a private act; the share-license gate lives in the
    // contributor pipeline, not the builder.
    if (!bank || typeof bank !== "object"
        || !shortText(bank.id, 80) || !idPattern.test(bank.id)
        || !shortText(bank.packageId, 120) || !shortText(bank.version, 32)
        || !(bank.digest === null || (typeof bank.digest === "string" && digestPattern.test(bank.digest)))
        || !shortText(bank.name, 80) || !shortText(bank.author, 80) || !shortText(bank.license, 32)
        || !["Mutable Instruments", "Community", "Local"].includes(String(bank.origin))
        || !shortText(bank.description, 240)
        || !Array.isArray(bank.voices) || bank.voices.length !== patchesPerBank) {
      throw new ContractError("invalid_user_data_bank", "A custom bank contains unsupported metadata or is not 32 voices.");
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
      index: Number(entry.index),
      bank: {
        id: bank.id as string, packageId: bank.packageId as string, version: bank.version as string,
        digest: bank.digest as string | null, name: bank.name as string, author: bank.author as string,
        license: bank.license as string, origin: bank.origin as NormalizedUserDataBank["bank"]["origin"],
        description: bank.description as string, voices,
      },
    };
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
  if (!hasExactKeys(preferenceValues, ["navigationMode"])
      || !hasExactKeys(optionValues, [
        "auxOutput", "chordTable", "holdOnTrigger", "levelInput",
        "lockedFrequencyKnob", "modelInput", "suboscillatorOctave",
      ])
      || !isOneOf(preferenceValues.navigationMode, ["linear", "banked"] as const)
      || !isOneOf(optionValues.lockedFrequencyKnob, ["octaves", "decay", "aux-crossfade", "macro-4"] as const)
      || !isOneOf(optionValues.modelInput, ["model", "lpg-colour", "aux-crossfade"] as const)
      || !isOneOf(optionValues.levelInput, ["level", "decay", "macro-4"] as const)
      || !isOneOf(optionValues.auxOutput, ["alternate-model", "square-subosc", "sine-subosc"] as const)
      || !isOneOf(optionValues.suboscillatorOctave, [0, -1, -2] as const)
      || typeof optionValues.chordTable !== "string"
      || !chordTables.some((table) => table.id === optionValues.chordTable)
      || typeof optionValues.holdOnTrigger !== "boolean") {
    throw new ContractError("invalid_preferences", "The recipe contains an unsupported firmware option.");
  }
  return {
    preferences: { navigationMode: preferenceValues.navigationMode },
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

export function normalizeRecipe(value: unknown): NormalizedRecipe {
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_recipe", "The build recipe must be a JSON object.");
  }
  const candidate = value as Record<string, unknown>;
  if (![2, 3, 4, 5, 6].includes(Number(candidate.schemaVersion))) {
    throw new ContractError("unsupported_schema", "Only Plaits Palette recipe schema versions 2 through 6 can be built.");
  }
  if (candidate.target !== "mutable-instruments-plaits" || candidate.firmware !== "rubato-plaits") {
    throw new ContractError("unsupported_target", "That recipe targets a different firmware family.");
  }
  if (candidate.output !== "audio-wav") {
    throw new ContractError("unsupported_output", "Only audio-installable WAV firmware is supported.");
  }
  if (!Array.isArray(candidate.slots) || (candidate.slots.length !== 24 && candidate.slots.length !== 32)) {
    throw new ContractError("invalid_slots", "A firmware recipe must contain 24 engine slots, or 32 for a four-bank build.");
  }
  if (candidate.slots.length === 32 && Number(candidate.schemaVersion) !== 6) {
    throw new ContractError("invalid_slots", "32-slot recipes require recipe schema version 6.");
  }
  const slots = candidate.schemaVersion === 2
    ? candidate.slots.map((id) => {
        if (typeof id !== "string" || !approvedEngineIdSet.has(id)) {
          throw new ContractError("unapproved_engine", "The recipe contains an engine that is not approved for builds.");
        }
        return id;
      })
    : candidate.slots.map((value) => {
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
  let chordTables: NormalizedChordTable[];
  let userDataBanks: NormalizedUserDataBank[] | undefined;
  if (candidate.schemaVersion === 5 || candidate.schemaVersion === 6) {
    const resources = candidate.resources;
    const expectedKeys = candidate.schemaVersion === 6 ? ["chordTables", "userDataBanks"] : ["chordTables"];
    if (!resources || typeof resources !== "object"
        || !hasExactKeys(resources as Record<string, unknown>, expectedKeys)) {
      throw new ContractError("invalid_resources", "The recipe must contain only supported firmware resources.");
    }
    chordTables = normalizeChordTables((resources as Record<string, unknown>).chordTables);
    if (candidate.schemaVersion === 6) {
      userDataBanks = normalizeUserDataBanks((resources as Record<string, unknown>).userDataBanks);
    }
  } else {
    chordTables = normalizeChordTables(structuredClone(chordCatalog.tables));
  }
  const configuration = candidate.schemaVersion === 4 || candidate.schemaVersion === 5 || candidate.schemaVersion === 6
    ? normalizeConfiguration(candidate, chordTables)
    : defaultConfiguration;
  return {
    // A candidate that carried v6 resources (even an empty custom-bank list,
    // e.g. a 32-slot recipe) stays v6 so the compiler applies v6 rules.
    schemaVersion: userDataBanks !== undefined ? 6 : 5,
    target: "mutable-instruments-plaits",
    firmware: "rubato-plaits",
    slots,
    preferences: { ...configuration.preferences },
    initialOptions: { ...configuration.initialOptions },
    resources: userDataBanks ? { chordTables, userDataBanks } : { chordTables },
    output: "audio-wav",
  };
}

// A manual's identity is the slot layout plus each selected engine's
// DOCUMENTATION digest (and the renderer contract) — deliberately NOT the
// firmware source revision or toolchain, so firmware-only rollouts keep
// reusing cached manuals and prose-only edits never invalidate firmware.
export async function computeManualKey(
  recipe: NormalizedRecipe,
  manualContract: string,
): Promise<string> {
  const documentation = [...new Set(recipe.slots)].sort().map((engineId) => {
    const engine = approvedEngines.get(engineId);
    if (!engine) throw new ContractError("unapproved_engine", "The recipe contains an engine that is not approved for builds.");
    return [engineId, engine.documentationDigest];
  });
  const canonical = JSON.stringify({
    manualContract,
    slots: recipe.slots,
    documentation,
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
