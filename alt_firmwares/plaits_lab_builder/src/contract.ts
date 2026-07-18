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

export type NormalizedRecipe = {
  schemaVersion: 5;
  target: "mutable-instruments-plaits";
  firmware: "rubato-plaits";
  slots: string[];
  preferences: {
    navigationMode: "linear" | "banked";
  };
  initialOptions: {
    lockedFrequencyKnob: "octaves" | "decay" | "aux-crossfade" | "macro-4";
    modelInput: "model" | "lpg-colour" | "aux-crossfade";
    levelInput: "level" | "decay";
    auxOutput: "alternate-model" | "square-subosc" | "sine-subosc";
    suboscillatorOctave: 0 | -1 | -2;
    chordTable: string;
    holdOnTrigger: boolean;
  };
  resources: {
    chordTables: NormalizedChordTable[];
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
      || !isOneOf(optionValues.levelInput, ["level", "decay"] as const)
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
  if (![2, 3, 4, 5].includes(Number(candidate.schemaVersion))) {
    throw new ContractError("unsupported_schema", "Only Plaits Lab recipe schema versions 2 through 5 can be built.");
  }
  if (candidate.target !== "mutable-instruments-plaits" || candidate.firmware !== "rubato-plaits") {
    throw new ContractError("unsupported_target", "That recipe targets a different firmware family.");
  }
  if (candidate.output !== "audio-wav") {
    throw new ContractError("unsupported_output", "Only audio-installable WAV firmware is supported.");
  }
  if (!Array.isArray(candidate.slots) || candidate.slots.length !== 24) {
    throw new ContractError("invalid_slots", "A firmware recipe must contain exactly 24 engine slots.");
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
  if (candidate.schemaVersion === 5) {
    const resources = candidate.resources;
    if (!resources || typeof resources !== "object"
        || !hasExactKeys(resources as Record<string, unknown>, ["chordTables"])) {
      throw new ContractError("invalid_resources", "The recipe must contain only supported firmware resources.");
    }
    chordTables = normalizeChordTables((resources as Record<string, unknown>).chordTables);
  } else {
    chordTables = normalizeChordTables(structuredClone(chordCatalog.tables));
  }
  const configuration = candidate.schemaVersion === 4 || candidate.schemaVersion === 5
    ? normalizeConfiguration(candidate, chordTables)
    : defaultConfiguration;
  return {
    schemaVersion: 5,
    target: "mutable-instruments-plaits",
    firmware: "rubato-plaits",
    slots,
    preferences: { ...configuration.preferences },
    initialOptions: { ...configuration.initialOptions },
    resources: { chordTables },
    output: "audio-wav",
  };
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
