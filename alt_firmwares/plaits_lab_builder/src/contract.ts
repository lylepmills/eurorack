import catalog from "../../plaits_lab_catalog/public_catalog.json" with { type: "json" };
import chordCatalog from "../../plaits_lab_chord_tables/catalog.json" with { type: "json" };

export const approvedEngineIds: readonly string[] = catalog.engines.map((engine) => engine.id);
const approvedEngines = new Map(catalog.engines.map((engine) => [engine.id, engine]));
const approvedEngineIdSet = new Set<string>(approvedEngineIds);
export const maxChordTables = 16;
const maxPreGestureChordTables = 9;
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
export const maxRecipeSchemaVersion = 28;
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
const replaceableFmBanksMinSchemaVersion = 20; // FM banks replaceable over TIMBRE
const syncInputMinSchemaVersion = 21; // audio-rate hard sync on MODEL
// v22 moves Sync In's compile-time switch onto its own preference, so a module
// can reach the mode at runtime without its owner having pre-selected it.
const syncInputPreferenceMinSchemaVersion = 22;
const experimentalFmMinSchemaVersion = 23; // independent TZFM law / fast ADC
// v24 reduces the FREQUENCY range selector to its three most clockwise
// positions. Compile-time only, and it changes no stored state, so a module
// moves between the two layouts keeping its tuned root and locked octave.
const simplifiedPitchRangesMinSchemaVersion = 24;
const gateArticulationMinSchemaVersion = 27;
const wavetableWaveLinesMinSchemaVersion = 28;
const scaleBankMinSchemaVersion = 16;      // recipe-driven scale bank
export const levelAutoMinSchemaVersion = 16; // engine-aware LEVEL routing
const speechBanksMinSchemaVersion = 17;      // selectable/custom Speech LPC banks
const attenuverterModeMinSchemaVersion = 18; // recipe-driven LIGHT 8 starting mode
const oneKnobEnvelopeMinSchemaVersion = 19;  // triggered/gated FREQUENCY contours
const customModelDataMinSchemaVersion = 24;  // per-slot Wavetable data
const terrainBankMinSchemaVersion = 24;       // shared ordered Wave Terrain bank
// v25 Natural Speech word banks. Unlike Speech there is no stock-bank list to
// merge with: the engine's demo banks are a compile-time fallback, so a recipe
// carrying its own replaces them entirely.
const naturalSpeechBanksMinSchemaVersion = 25;
const nativeTerrainMinSchemaVersion = 24;      // compiled custom equations
const wavetableBankMinSchemaVersion = 26;      // shared ordered Wavetable bank
const nativeWavetableMinSchemaVersion = 26;    // compiled custom equations
const customModelDataBytes = 4096;
const wavetableBankDataBytes = 8192;
export const maxTerrainBankSize = 16;
export const maxMirroredWavetableBankSize = 8;
export const maxOneWayWavetableBankSize = 16;
const factoryTerrainIds = [
  "factory-1", "factory-2", "factory-3", "factory-4",
  "factory-5", "factory-6", "factory-7", "factory-8",
] as const;
const factoryTerrainIdSet = new Set<string>(factoryTerrainIds);
const factoryWavetableIds = ["mutable-1", "mutable-2", "mutable-3"] as const;
const factoryWavetableIdSet = new Set<string>(factoryWavetableIds);
const factoryTerrainWaveSources = new Map<string, string>([
  ["factory-6", "mutable-3"],
  ["factory-7", "mutable-2"],
  ["factory-8", "mutable-1"],
]);
const sharedWaveLibraryEngineIds = new Set([
  "wavetable",
  "wave-terrain",
  "chords",
  "wave-paraphonic",
  "wavetable-chord",
  "wavetable-scale-stack",
]);

export const minScaleBankSize = 1;
export const maxScaleBankSize = 16;
export const minScaleDegrees = 2;
export const maxScaleDegrees = 7;
export const scaleUnitsPerSemitone = 128;
export const scaleUnitsPerOctave = 12 * scaleUnitsPerSemitone;

export type NormalizedScale = {
  id: string;
  name: string;
  description: string;
  pitches: number[];
  tuning: "12-TET" | "Microtonal";
  source: "Shipped" | "Braids" | "Rubato" | "Local";
};

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
export type FirmwareOutput = "audio-wav" | "intel-hex";

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

export type NormalizedSpeechBank = {
  words: string[];
  wordBoundaries: number[];
  frameData: string;
};

export type NormalizedSpeechBanks = {
  stockBankIds: number[];
  customBanks: NormalizedSpeechBank[];
};

export type NormalizedNaturalSpeechBanks = {
  customBanks: NormalizedSpeechBank[];
};

export type NormalizedCustomModelData = {
  slot: number;
  model: {
    kind: "wave-terrain" | "wavetable";
    name: string;
    equation: string;
    data: string;
    representation?: "native";
  };
};

export type NormalizedTerrainBankEntry = {
  kind: "factory";
  id: typeof factoryTerrainIds[number];
} | {
  kind: "custom";
  model: NormalizedCustomModelData["model"] & { kind: "wave-terrain" };
};

export type NormalizedWavetableBankEntry = {
  kind: "factory";
  id: typeof factoryWavetableIds[number];
} | {
  kind: "custom";
  model: NormalizedCustomModelData["model"] & { kind: "wavetable" };
};

export type NormalizedWavetableBank = {
  mirrored: boolean;
  entries: NormalizedWavetableBankEntry[];
  waveLines?: {
    chords: NormalizedWaveLinePoint[];
    braids: NormalizedWaveLinePoint[];
  };
};

export type NormalizedWaveLinePoint = {
  bank: number;
  frame: number;
  gain?: number;
};

export type NormalizedRecipe = {
  schemaVersion: 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28;
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
    // Let every 6-Op FM bank be replaced over the TIMBRE input (v20). Opt-in:
    // page-aligning the banks costs ~816 bytes, more than the stock preset has
    // spare, so an un-flagged build keeps the historical single-region layout.
    replaceableFmBanks?: boolean;
    // Compile Sync In in, making it selectable as a MODEL-input mode at RUNTIME
    // (v22). Before v22 this was derived from the starting value, so a user who
    // did not pick it at build time could never reach it on the module.
    syncInput?: boolean;
    // Experimental (v23): on supporting engines, use the FM attenuverter's
    // CCW side for linear through-zero FM and retain exponential FM on CW.
    linearTzfm?: boolean;
    // Experimental (v23): digitize FM continuously at audio rate. FM and LEVEL
    // share a converter, so this makes LEVEL CV unavailable in the whole build.
    fastFm?: boolean;
    // Keep only the three most clockwise FREQUENCY ranges -- octave switching,
    // fine tuning and coarse (v24). Drops the eight +/-7-semitone ranges and the
    // sub-audio LFO range, which are redundant for a coarse-then-fine-then-lock
    // workflow, and gives each surviving mode a third of the selector travel.
    // Leave it off to use Plaits as an LFO or to jump octaves in one gesture.
    simplifiedPitchRanges?: boolean;
    // Compile the optional one-knob envelope contour. Its runtime one-shot or
    // gated behavior is selected independently by trigResponse.
    envelopeContour?: boolean;
  };
  initialOptions: {
    lockedFrequencyKnob: "octaves" | "decay" | "aux-crossfade" | "macro-4"
      | "triggered-envelope" | "gated-envelope" | "envelope-contour";
    trigResponse?: "trigger" | "gate" | "velocity-trigger" | "velocity-gate";
    modelInput: "model" | "lpg-colour" | "aux-crossfade" | "macro-4" | "sync-in";
    levelInput: "level" | "decay" | "auto";
    auxOutput: "alternate-model" | "square-subosc" | "sine-subosc" | "stereo";
    suboscillatorOctave: 0 | -1 | -2;
    chordTable: string;
    holdOnTrigger: boolean;
    attenuverterMode: "stock" | "drift" | "step";
  };
  resources: {
    chordTables: NormalizedChordTable[];
    scaleBank?: NormalizedScale[];
    // v6 carries index-keyed banks; v12 carries per-slot banks. (The empty
    // fourth-bank marker on a 32-slot v7-v11 recipe is an empty array.)
    userDataBanks?: NormalizedUserDataBank[] | NormalizedSlotBank[];
    // v17: selected shipped LPC banks followed by custom decoded-frame banks.
    speechBanks?: NormalizedSpeechBanks;
    naturalSpeechBanks?: NormalizedNaturalSpeechBanks;
    // v21: equation metadata plus the sampled 4 KB data for one terrain/table slot.
    customModelData?: NormalizedCustomModelData[];
    // v23: the shared ordered bank swept by every Wave Terrain slot's HARMONICS.
    terrainBank?: NormalizedTerrainBankEntry[];
    // v26: shared ordered Wavetable bank, optionally folded back through
    // HARMONICS. Sampled custom banks retain all 64 128-sample frames.
    wavetableBank?: NormalizedWavetableBank;
  };
  // Catalog ids of the engines built with the stereo render path (introduced in
  // schema 10). Absent on schema <= 9 (the global-stereo recipes, which the
  // builder treats as all stereo-capable engines when the aux option is stereo).
  stereoEngines?: string[];
  output: FirmwareOutput;
};

const defaultConfiguration: Pick<NormalizedRecipe, "preferences" | "initialOptions"> = {
  preferences: { navigationMode: "linear", calibration: false, colorBlindMode: false },
  initialOptions: {
    lockedFrequencyKnob: "octaves",
    trigResponse: "trigger",
    modelInput: "model",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: "original",
    holdOnTrigger: false,
    attenuverterMode: "stock",
  },
};

const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const digestPattern = /^sha256:[0-9a-f]{64}$/;
const packageVersionPattern = /^\d+\.\d+\.\d+$/;
const approvedChordTablesById = new Map(chordCatalog.tables.map((table) => [table.id, table]));

function shortText(value: unknown, maximum: number): value is string {
  return typeof value === "string" && value.trim().length > 0 && value.length <= maximum;
}

type PackageVersion = readonly [major: number, minor: number, patch: number];

function parsePackageVersion(value: unknown): PackageVersion | null {
  if (!shortText(value, 32) || !packageVersionPattern.test(value)) return null;
  const parts = value.split(".").map(Number);
  if (!parts.every(Number.isSafeInteger)) return null;
  return parts as unknown as PackageVersion;
}

// A recipe identifies the model the user chose; the hosted builder always
// compiles that model from its current approved source image. Accept an older
// package reference only when semantic versioning says the current package is a
// compatible forward update. Versions >=1 stay compatible within a major;
// 0.x packages stay compatible within a minor. Never let a recipe from a newer
// catalog silently compile an older implementation.
export function isCompatiblePackageUpgrade(from: unknown, to: unknown): boolean {
  const source = parsePackageVersion(from);
  const target = parsePackageVersion(to);
  if (!source || !target) return false;
  const [sourceMajor, sourceMinor, sourcePatch] = source;
  const [targetMajor, targetMinor, targetPatch] = target;
  const compatible = targetMajor === 0
    ? sourceMajor === 0 && sourceMinor === targetMinor
    : sourceMajor === targetMajor;
  if (!compatible) return false;
  return sourceMajor < targetMajor
    || (sourceMajor === targetMajor && sourceMinor < targetMinor)
    || (sourceMajor === targetMajor && sourceMinor === targetMinor && sourcePatch <= targetPatch);
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

function normalizeScaleBank(value: unknown): NormalizedScale[] {
  if (!Array.isArray(value)
      || value.length < minScaleBankSize
      || value.length > maxScaleBankSize) {
    throw new ContractError(
      "invalid_scale_bank",
      `A firmware must contain between ${minScaleBankSize} and ${maxScaleBankSize} scales.`,
    );
  }
  const scaleIds = new Set<string>();
  return value.map((item) => {
    if (!item || typeof item !== "object") {
      throw new ContractError("invalid_scale", "The recipe contains an invalid scale.");
    }
    const scale = item as Record<string, unknown>;
    const rawPitches = scale.pitches;
    if (!shortText(scale.id, 80) || !idPattern.test(scale.id) || scaleIds.has(scale.id)
        || !shortText(scale.name, 80) || !shortText(scale.description, 240)
        || !["12-TET", "Microtonal"].includes(String(scale.tuning))
        || !["Shipped", "Braids", "Rubato", "Local"].includes(String(scale.source))
        || !Array.isArray(rawPitches)
        || rawPitches.length < minScaleDegrees
        || rawPitches.length > maxScaleDegrees
        || rawPitches.some((pitch) => !Number.isInteger(pitch))
        || rawPitches[0] !== 0
        || rawPitches.some((pitch, index) =>
          index > 0 && Number(pitch) <= Number(rawPitches[index - 1]))
        || Number(rawPitches.at(-1)) >= scaleUnitsPerOctave) {
      throw new ContractError(
        "invalid_scale",
        `A scale must contain ${minScaleDegrees} to ${maxScaleDegrees} strictly ascending pitches below the octave.`,
      );
    }
    const pitches = [...rawPitches] as number[];
    const tuning = pitches.every((pitch) => pitch % scaleUnitsPerSemitone === 0)
      ? "12-TET" : "Microtonal";
    if (scale.tuning !== tuning) {
      throw new ContractError(
        "invalid_scale",
        "A scale's tuning label does not match its pitches.",
      );
    }
    scaleIds.add(scale.id);
    return {
      id: scale.id,
      name: scale.name,
      description: scale.description,
      pitches,
      tuning,
      source: scale.source as NormalizedScale["source"],
    };
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

const lpcFrameBytes = 14;
const maxSpeechBanks = 8;
const maxSpeechWords = 32;
const maxSpeechFrames = 1024;
const naturalSpeechFrameBytes = 23;   // NSH1
const maxNaturalSpeechBanks = 8;
const maxNaturalSpeechWords = 32;
const maxNaturalSpeechFrames = 1024;
const canonicalBase64Pattern = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;

function normalizeNaturalSpeechBanks(value: unknown): NormalizedNaturalSpeechBanks {
  // The sibling of normalizeSpeechBanks, with two differences that come from
  // the format: frames are 23 bytes (NSH1) rather than 14, and there is no
  // stockBankIds list -- Natural Speech's demo banks are compiled out when a
  // recipe supplies its own, so custom banks replace rather than extend.
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_natural_speech_banks", "The recipe contains invalid Natural Speech word banks.");
  }
  const candidate = value as Record<string, unknown>;
  if (!hasExactKeys(candidate, ["customBanks"]) || !Array.isArray(candidate.customBanks)) {
    throw new ContractError("invalid_natural_speech_banks", "The recipe contains invalid Natural Speech word banks.");
  }
  if (candidate.customBanks.length < 1 || candidate.customBanks.length > maxNaturalSpeechBanks) {
    throw new ContractError(
      "invalid_natural_speech_banks",
      `Natural Speech must contain between one and ${maxNaturalSpeechBanks} banks.`,
    );
  }
  const customBanks = candidate.customBanks.map((raw): NormalizedSpeechBank => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_natural_speech_bank", "The recipe contains an invalid Natural Speech bank.");
    }
    const bank = raw as Record<string, unknown>;
    if (!hasExactKeys(bank, ["words", "wordBoundaries", "frameData"])
        || !Array.isArray(bank.words)
        || bank.words.length < 1 || bank.words.length > maxNaturalSpeechWords
        || bank.words.some((word) => typeof word !== "string" || !word.trim() || word.length > 80)
        || !Array.isArray(bank.wordBoundaries)
        || typeof bank.frameData !== "string"
        || !canonicalBase64Pattern.test(bank.frameData)) {
      throw new ContractError("invalid_natural_speech_bank", "The recipe contains an invalid Natural Speech bank.");
    }
    const packedBytes = (bank.frameData.length / 4) * 3
      - (bank.frameData.endsWith("==") ? 2 : bank.frameData.endsWith("=") ? 1 : 0);
    const frameCount = packedBytes / naturalSpeechFrameBytes;
    const boundaries = bank.wordBoundaries;
    if (packedBytes <= 0 || !Number.isInteger(frameCount) || frameCount > maxNaturalSpeechFrames
        || boundaries.length !== bank.words.length + 1
        || boundaries.some((item) => !Number.isInteger(item))
        || boundaries[0] !== 0 || boundaries.at(-1) !== frameCount
        || boundaries.slice(1).some((item, index) => Number(item) <= Number(boundaries[index]))) {
      throw new ContractError("invalid_natural_speech_bank", "A Natural Speech bank contains invalid word boundaries.");
    }
    return {
      words: bank.words.map((word) => String(word).trim()),
      wordBoundaries: boundaries.map(Number),
      frameData: bank.frameData,
    };
  });
  return { customBanks };
}

function normalizeSpeechBanks(value: unknown): NormalizedSpeechBanks {
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_speech_banks", "The recipe contains invalid Speech word banks.");
  }
  const candidate = value as Record<string, unknown>;
  if (!hasExactKeys(candidate, ["stockBankIds", "customBanks"])
      || !Array.isArray(candidate.stockBankIds)
      || !Array.isArray(candidate.customBanks)) {
    throw new ContractError("invalid_speech_banks", "The recipe contains invalid Speech word banks.");
  }
  const stockBankIds = candidate.stockBankIds;
  if (stockBankIds.some((id) => !Number.isInteger(id) || Number(id) < 0 || Number(id) > 4)
      || new Set(stockBankIds).size !== stockBankIds.length
      || stockBankIds.length + candidate.customBanks.length < 1
      || stockBankIds.length + candidate.customBanks.length > maxSpeechBanks) {
    throw new ContractError(
      "invalid_speech_banks",
      `Speech must contain between one and ${maxSpeechBanks} total banks.`,
    );
  }
  const customBanks = candidate.customBanks.map((raw): NormalizedSpeechBank => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_speech_bank", "The recipe contains an invalid custom Speech bank.");
    }
    const bank = raw as Record<string, unknown>;
    if (!hasExactKeys(bank, ["words", "wordBoundaries", "frameData"])
        || !Array.isArray(bank.words)
        || bank.words.length < 1 || bank.words.length > maxSpeechWords
        || bank.words.some((word) => typeof word !== "string" || !word.trim() || word.length > 80)
        || !Array.isArray(bank.wordBoundaries)
        || typeof bank.frameData !== "string"
        || !canonicalBase64Pattern.test(bank.frameData)) {
      throw new ContractError("invalid_speech_bank", "The recipe contains an invalid custom Speech bank.");
    }
    const packedBytes = (bank.frameData.length / 4) * 3
      - (bank.frameData.endsWith("==") ? 2 : bank.frameData.endsWith("=") ? 1 : 0);
    const frameCount = packedBytes / lpcFrameBytes;
    const boundaries = bank.wordBoundaries;
    if (packedBytes <= 0 || !Number.isInteger(frameCount) || frameCount > maxSpeechFrames
        || boundaries.length !== bank.words.length + 1
        || boundaries.some((item) => !Number.isInteger(item))
        || boundaries[0] !== 0 || boundaries.at(-1) !== frameCount
        || boundaries.slice(1).some((item, index) => Number(item) <= Number(boundaries[index]))) {
      throw new ContractError("invalid_speech_bank", "A custom Speech bank contains invalid LPC word boundaries.");
    }
    return {
      words: bank.words.map((word) => String(word).trim()),
      wordBoundaries: boundaries.map(Number),
      frameData: bank.frameData,
    };
  });
  return { stockBankIds: stockBankIds.map(Number), customBanks };
}

function normalizeCustomModelData(
  value: unknown,
  slots: (string | null)[],
): NormalizedCustomModelData[] {
  if (!Array.isArray(value) || value.length > slots.length) {
    throw new ContractError(
      "invalid_custom_model_data",
      "The recipe contains an unsupported set of custom model data.",
    );
  }
  const seen = new Set<number>();
  return value.map((raw) => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_custom_model_data", "A custom model-data assignment is invalid.");
    }
    const entry = raw as Record<string, unknown>;
    const slot = Number(entry.slot);
    if (!hasExactKeys(entry, ["slot", "model"])
        || !Number.isInteger(entry.slot) || slot < 0 || slot >= slots.length
        || seen.has(slot) || !entry.model || typeof entry.model !== "object") {
      throw new ContractError(
        "invalid_custom_model_data",
        "Custom model data must target a distinct palette slot.",
      );
    }
    const model = entry.model as Record<string, unknown>;
    const kind = slots[slot] === "wave-terrain" ? "wave-terrain"
      : slots[slot] === "wavetable" ? "wavetable" : undefined;
    if (!kind || !hasExactKeys(model, ["kind", "name", "equation", "data"])
        || model.kind !== kind || !shortText(model.name, 80)
        || !shortText(model.equation, 500) || typeof model.data !== "string"
        || !canonicalBase64Pattern.test(model.data)) {
      throw new ContractError(
        "invalid_custom_model_data",
        "Custom model data must match a Wave Terrain or Wavetable slot.",
      );
    }
    let decoded: string;
    try {
      decoded = atob(model.data);
    } catch {
      throw new ContractError("invalid_custom_model_data", "Custom model data is not valid base64.");
    }
    if (decoded.length !== customModelDataBytes || btoa(decoded) !== model.data) {
      throw new ContractError(
        "invalid_custom_model_data",
        `Custom model data must contain exactly ${customModelDataBytes} bytes.`,
      );
    }
    seen.add(slot);
    return {
      slot,
      model: {
        kind,
        name: String(model.name).trim(),
        equation: String(model.equation).trim(),
        data: model.data,
      },
    };
  });
}

function normalizeTerrainBank(value: unknown, schemaVersion: number): NormalizedTerrainBankEntry[] {
  if (!Array.isArray(value) || value.length < 1 || value.length > maxTerrainBankSize) {
    throw new ContractError(
      "invalid_terrain_bank",
      `A terrain bank must contain between one and ${maxTerrainBankSize} terrains.`,
    );
  }
  const seenFactories = new Set<string>();
  return value.map((raw) => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_terrain_bank", "A terrain-bank entry is invalid.");
    }
    const entry = raw as Record<string, unknown>;
    if (entry.kind === "factory") {
      if (!hasExactKeys(entry, ["kind", "id"])
          || typeof entry.id !== "string" || !factoryTerrainIdSet.has(entry.id)
          || seenFactories.has(entry.id)) {
        throw new ContractError(
          "invalid_terrain_bank",
          "Each Mutable Instruments factory terrain may appear at most once.",
        );
      }
      seenFactories.add(entry.id);
      return { kind: "factory", id: entry.id as typeof factoryTerrainIds[number] };
    }
    if (entry.kind !== "custom" || !hasExactKeys(entry, ["kind", "model"])
        || !entry.model || typeof entry.model !== "object") {
      throw new ContractError("invalid_terrain_bank", "A terrain-bank entry is invalid.");
    }
    const model = entry.model as Record<string, unknown>;
    const sampledKeys = hasExactKeys(model, ["kind", "name", "equation", "data"]);
    const nativeKeys = hasExactKeys(model, ["kind", "name", "equation", "data", "representation"]);
    if ((!sampledKeys && !nativeKeys)
        || model.kind !== "wave-terrain" || !shortText(model.name, 80)
        || !shortText(model.equation, 500) || typeof model.data !== "string"
        || !canonicalBase64Pattern.test(model.data)) {
      throw new ContractError(
        "invalid_terrain_bank",
        "A custom terrain must contain a name, equation, and canonical 4 KB sample grid.",
      );
    }
    if (nativeKeys && (model.representation !== "native"
        || schemaVersion < nativeTerrainMinSchemaVersion)) {
      throw new ContractError(
        "invalid_terrain_bank",
        `Native terrain equations require recipe schema ${nativeTerrainMinSchemaVersion}.`,
      );
    }
    let decoded: string;
    try {
      decoded = atob(model.data);
    } catch {
      throw new ContractError("invalid_terrain_bank", "Custom terrain data is not valid base64.");
    }
    if (decoded.length !== customModelDataBytes || btoa(decoded) !== model.data) {
      throw new ContractError(
        "invalid_terrain_bank",
        `Custom terrain data must contain exactly ${customModelDataBytes} bytes.`,
      );
    }
    return {
      kind: "custom",
      model: {
        kind: "wave-terrain",
        name: String(model.name).trim(),
        equation: String(model.equation).trim(),
        data: model.data,
        ...(model.representation === "native" ? { representation: "native" as const } : {}),
      },
    };
  });
}

function normalizeWaveLine(
  value: unknown,
  name: string,
  size: number,
  bankCount: number,
): NormalizedWaveLinePoint[] {
  if (!Array.isArray(value) || value.length !== size) {
    throw new ContractError("invalid_wavetable_bank", `${name} must contain exactly ${size} stops.`);
  }
  return value.map((raw): NormalizedWaveLinePoint => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_wavetable_bank", `${name} contains an invalid stop.`);
    }
    const point = raw as Record<string, unknown>;
    if (!hasExactKeys(point, point.gain === undefined ? ["bank", "frame"] : ["bank", "frame", "gain"])
        || !Number.isInteger(point.bank) || Number(point.bank) < 0 || Number(point.bank) >= bankCount
        || !Number.isInteger(point.frame) || Number(point.frame) < 0 || Number(point.frame) >= 64
        || (point.gain !== undefined && (typeof point.gain !== "number"
          || !Number.isFinite(point.gain) || point.gain < 0 || point.gain > 2))) {
      throw new ContractError("invalid_wavetable_bank", `${name} contains an invalid stop.`);
    }
    return {
      bank: Number(point.bank),
      frame: Number(point.frame),
      ...(point.gain === undefined ? {} : { gain: Number(point.gain) }),
    };
  });
}

function normalizeWaveLines(value: unknown, bankCount: number): NonNullable<NormalizedWavetableBank["waveLines"]> {
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_wavetable_bank", "The engine wave lines are invalid.");
  }
  const lines = value as Record<string, unknown>;
  if (!hasExactKeys(lines, ["chords", "braids"])) {
    throw new ContractError("invalid_wavetable_bank", "The engine wave lines must contain Chords and Braids mappings.");
  }
  return {
    chords: normalizeWaveLine(lines.chords, "The Chords wave line", 15, bankCount),
    braids: normalizeWaveLine(lines.braids, "The Braids wave line", 33, bankCount),
  };
}

function normalizeWavetableBank(value: unknown, schemaVersion: number): NormalizedWavetableBank {
  if (!value || typeof value !== "object") {
    throw new ContractError("invalid_wavetable_bank", "A wavetable bank is invalid.");
  }
  const bank = value as Record<string, unknown>;
  const expectedKeys = schemaVersion >= wavetableWaveLinesMinSchemaVersion
    ? ["mirrored", "entries", "waveLines"] : ["mirrored", "entries"];
  if (!hasExactKeys(bank, expectedKeys) || typeof bank.mirrored !== "boolean"
      || !Array.isArray(bank.entries)) {
    throw new ContractError(
      "invalid_wavetable_bank",
      "A wavetable bank must contain a mirror setting and an ordered list of entries.",
    );
  }
  const maximum = bank.mirrored ? maxMirroredWavetableBankSize : maxOneWayWavetableBankSize;
  if (bank.entries.length < 1 || bank.entries.length > maximum) {
    throw new ContractError(
      "invalid_wavetable_bank",
      `This wavetable path must contain between one and ${maximum} banks.`,
    );
  }
  const seenFactories = new Set<string>();
  const entries = bank.entries.map((raw): NormalizedWavetableBankEntry => {
    if (!raw || typeof raw !== "object") {
      throw new ContractError("invalid_wavetable_bank", "A wavetable-bank entry is invalid.");
    }
    const entry = raw as Record<string, unknown>;
    if (entry.kind === "factory") {
      if (!hasExactKeys(entry, ["kind", "id"])
          || typeof entry.id !== "string" || !factoryWavetableIdSet.has(entry.id)
          || seenFactories.has(entry.id)) {
        throw new ContractError(
          "invalid_wavetable_bank",
          "Each Mutable Instruments factory wavetable may appear at most once.",
        );
      }
      seenFactories.add(entry.id);
      return { kind: "factory", id: entry.id as typeof factoryWavetableIds[number] };
    }
    if (entry.kind !== "custom" || !hasExactKeys(entry, ["kind", "model"])
        || !entry.model || typeof entry.model !== "object") {
      throw new ContractError("invalid_wavetable_bank", "A wavetable-bank entry is invalid.");
    }
    const model = entry.model as Record<string, unknown>;
    const sampledKeys = hasExactKeys(model, ["kind", "name", "equation", "data"]);
    const nativeKeys = hasExactKeys(model, ["kind", "name", "equation", "data", "representation"]);
    if ((!sampledKeys && !nativeKeys)
        || model.kind !== "wavetable" || !shortText(model.name, 80)
        || !shortText(model.equation, 500) || typeof model.data !== "string"
        || !canonicalBase64Pattern.test(model.data)) {
      throw new ContractError(
        "invalid_wavetable_bank",
        "A custom wavetable must contain a name, equation, and canonical 64-frame sample bank.",
      );
    }
    if (nativeKeys && (model.representation !== "native"
        || schemaVersion < nativeWavetableMinSchemaVersion)) {
      throw new ContractError(
        "invalid_wavetable_bank",
        `Native wavetable equations require recipe schema ${nativeWavetableMinSchemaVersion}.`,
      );
    }
    let decoded: string;
    try {
      decoded = atob(model.data);
    } catch {
      throw new ContractError("invalid_wavetable_bank", "Custom wavetable data is not valid base64.");
    }
    if (decoded.length !== wavetableBankDataBytes || btoa(decoded) !== model.data) {
      throw new ContractError(
        "invalid_wavetable_bank",
        `Custom wavetable data must contain exactly ${wavetableBankDataBytes} bytes.`,
      );
    }
    return {
      kind: "custom",
      model: {
        kind: "wavetable",
        name: String(model.name).trim(),
        equation: String(model.equation).trim(),
        data: model.data,
        ...(model.representation === "native" ? { representation: "native" as const } : {}),
      },
    };
  });
  return {
    mirrored: bank.mirrored,
    entries,
    ...(schemaVersion >= wavetableWaveLinesMinSchemaVersion
      ? { waveLines: normalizeWaveLines(bank.waveLines, entries.length) }
      : {}),
  };
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

// Matches an object against the cumulative prefixes of an ordered tier list,
// returning the matched tier index or -1. Exact match per prefix, so an unknown
// or partial key set is still rejected outright. Shared by the preference and
// starting-option tiers below -- both grew the same way and fail the same way.
function matchKeySetTier(
  value: Record<string, unknown>,
  tiers: readonly (readonly string[])[],
): number {
  const present = Object.keys(value).sort();
  const cumulative: string[] = [];
  for (let tier = 0; tier < tiers.length; tier += 1) {
    cumulative.push(...tiers[tier]);
    const expected = [...cumulative].sort();
    if (expected.length === present.length
        && expected.every((key, index) => key === present[index])) {
      return tier;
    }
  }
  return -1;
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
  // Compile-time preferences arrived incrementally, and every shape accepted
  // here is a cumulative PREFIX of the tiers below: a pre-v14 recipe carries
  // navigationMode alone, v14 adds calibration, v15 colorBlindMode, and so on.
  // Adding a preference is one row -- presence, type-checking and the derived
  // value all follow from it.
  //
  // This replaced one hasExactKeys boolean per tier plus a hand-maintained OR of
  // every LATER tier inside each type-check and each derivation. That shape was
  // quadratic to maintain and, worse, failed SILENTLY: a missed OR made an older
  // preference derive to false, so a saved recipe's calibration or Sync In
  // simply vanished from the firmware it built, with nothing reporting a
  // problem. It shipped exactly once, for calibration, when v24 landed.
  //
  // The preference set stays CLOSED, so a new key must be added here as well as
  // version-gated below -- bumping maxRecipeSchemaVersion alone leaves the
  // Worker rejecting the field before the container sees it, the Worker/
  // container contract split that failed the rev-4749aec727af canary.
  const preferenceTiers: readonly (readonly string[])[] = [
    ["navigationMode"],
    ["calibration"],
    ["colorBlindMode"],
    ["replaceableFmBanks"],
    ["syncInput"],
    ["linearTzfm", "fastFm"],
    ["simplifiedPitchRanges"],
    ["envelopeContour"],
  ];
  // The index of the cumulative prefix the recipe's keys match exactly, or -1
  // for a shape no released editor ever produced.
  const preferenceTier = matchKeySetTier(preferenceValues, preferenceTiers);
  // Every preference but navigationMode is a boolean flag.
  const booleanPreferenceKeys = preferenceTiers
    .slice(1, preferenceTier + 1)
    .flatMap((keys) => keys);
  // Starting options grew the same way, so they are matched the same way: every
  // shape accepted here is a cumulative PREFIX of these tiers -- every recipe
  // ever written carries the seven original options, and a v18-or-later one adds
  // attenuverterMode. Adding an option is one row; presence, type-checking and
  // the derived value all follow from it.
  //
  // This replaced one hasExactKeys boolean per shape, which needed a
  // hand-maintained OR of every LATER shape at each use. That is the shape that
  // failed silently for the preferences: a missed OR made an older setting
  // derive to its default, so a saved recipe's option simply vanished from the
  // firmware it built. With two shapes it was still tractable; the third would
  // have recreated the bug.
  //
  // The option set stays CLOSED, so a new key must be added here as well as
  // version-gated below. Keep this list -- content AND order, since the accepted
  // shapes are its prefixes -- in step with INITIAL_OPTION_TIERS in
  // generate_engine_config.py and initialOptionTiers in the editor's manifest.ts.
  const initialOptionTiers: readonly (readonly string[])[] = [
    ["auxOutput", "chordTable", "holdOnTrigger", "levelInput",
      "lockedFrequencyKnob", "modelInput", "suboscillatorOctave"],
    ["attenuverterMode"],
    ["trigResponse"],
  ];
  const initialOptionTier = matchKeySetTier(optionValues, initialOptionTiers);
  const carriesAttenuverterMode = initialOptionTier >= 1;
  const carriesGateArticulation = initialOptionTier >= 2;
  if (preferenceTier < 0
      || booleanPreferenceKeys.some((key) => typeof preferenceValues[key] !== "boolean")
      || initialOptionTier < 0
      || !isOneOf(preferenceValues.navigationMode, ["linear", "banked"] as const)
      || !isOneOf(optionValues.lockedFrequencyKnob, [
        "octaves", "decay", "aux-crossfade", "macro-4",
        "triggered-envelope", "gated-envelope", "envelope-contour",
      ] as const)
      || (carriesGateArticulation
        && !isOneOf(optionValues.trigResponse, [
          "trigger", "gate", "velocity-trigger", "velocity-gate",
        ] as const))
      || !isOneOf(optionValues.modelInput, ["model", "lpg-colour", "aux-crossfade", "macro-4", "sync-in"] as const)
      || !isOneOf(optionValues.levelInput, ["level", "decay", "auto"] as const)
      || !isOneOf(optionValues.auxOutput, ["alternate-model", "square-subosc", "sine-subosc", "stereo"] as const)
      || !isOneOf(optionValues.suboscillatorOctave, [0, -1, -2] as const)
      || typeof optionValues.chordTable !== "string"
      || !chordTables.some((table) => table.id === optionValues.chordTable)
      || typeof optionValues.holdOnTrigger !== "boolean") {
    throw new ContractError("invalid_preferences", "The recipe contains an unsupported firmware option.");
  }
  if ((Number(candidate.schemaVersion) >= attenuverterModeMinSchemaVersion) !== carriesAttenuverterMode
      || (carriesAttenuverterMode
        && !isOneOf(optionValues.attenuverterMode, ["stock", "drift", "step"] as const))) {
    throw new ContractError(
      "unsupported_schema",
      `Unpatched attenuverter starting mode requires recipe schema version ${attenuverterModeMinSchemaVersion}.`,
    );
  }
  // Absent keys are undefined and so never `true`, which is what makes the OR
  // chains unnecessary -- and their omission impossible.
  const calibration = preferenceValues.calibration === true;
  const colorBlindMode = preferenceValues.colorBlindMode === true;
  const replaceableFmBanks = preferenceValues.replaceableFmBanks === true;
  const syncInput = preferenceValues.syncInput === true;
  const linearTzfm = preferenceValues.linearTzfm === true;
  const fastFm = preferenceValues.fastFm === true;
  const simplifiedPitchRanges = preferenceValues.simplifiedPitchRanges === true;
  const legacyContour = optionValues.lockedFrequencyKnob === "triggered-envelope"
    || optionValues.lockedFrequencyKnob === "gated-envelope";
  const envelopeContour = preferenceValues.envelopeContour === true;
  const hasEnvelopeContour = envelopeContour || legacyContour;
  const trigResponse = carriesGateArticulation
    ? optionValues.trigResponse as NonNullable<NormalizedRecipe["initialOptions"]["trigResponse"]>
    : optionValues.lockedFrequencyKnob === "gated-envelope" ? "gate" : "trigger";
  const usesGateArticulation = carriesGateArticulation
    || optionValues.lockedFrequencyKnob === "envelope-contour"
    || envelopeContour;
  if ((Number(candidate.schemaVersion) >= gateArticulationMinSchemaVersion && !carriesGateArticulation)
      || (usesGateArticulation
        && Number(candidate.schemaVersion) < gateArticulationMinSchemaVersion)) {
    throw new ContractError(
      "unsupported_schema",
      `TRIG response requires recipe schema version ${gateArticulationMinSchemaVersion}.`,
    );
  }
  if (optionValues.lockedFrequencyKnob === "envelope-contour" && !hasEnvelopeContour) {
    throw new ContractError(
      "invalid_recipe",
      "Starting with Envelope contour requires the envelopeContour preference.",
    );
  }
  if (simplifiedPitchRanges
      && Number(candidate.schemaVersion) < simplifiedPitchRangesMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `The simplified pitch-range preference requires recipe schema version ${simplifiedPitchRangesMinSchemaVersion}.`,
    );
  }
  if ((linearTzfm || fastFm)
      && Number(candidate.schemaVersion) < experimentalFmMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `Experimental FM preferences require recipe schema version ${experimentalFmMinSchemaVersion}.`,
    );
  }
  if (syncInput
      && Number(candidate.schemaVersion) < syncInputPreferenceMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `The Sync In preference requires recipe schema version ${syncInputPreferenceMinSchemaVersion}.`,
    );
  }
  if (replaceableFmBanks
      && Number(candidate.schemaVersion) < replaceableFmBanksMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `Replaceable FM banks require recipe schema version ${replaceableFmBanksMinSchemaVersion}.`,
    );
  }
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
  if (optionValues.levelInput === "auto"
      && Number(candidate.schemaVersion) < levelAutoMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `Automatic LEVEL routing requires recipe schema version ${levelAutoMinSchemaVersion}.`,
    );
  }
  if ((optionValues.lockedFrequencyKnob === "triggered-envelope"
      || optionValues.lockedFrequencyKnob === "gated-envelope")
      && Number(candidate.schemaVersion) < oneKnobEnvelopeMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `One-knob envelopes require recipe schema version ${oneKnobEnvelopeMinSchemaVersion}.`,
    );
  }
  if (optionValues.modelInput === "sync-in"
      && Number(candidate.schemaVersion) < syncInputMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `Sync In requires recipe schema version ${syncInputMinSchemaVersion}.`,
    );
  }
  if (optionValues.modelInput === "sync-in" && !syncInput) {
    // Since v22 the capability comes from the preference alone. Starting in a
    // mode whose code was never compiled would leave MODEL pointing past the
    // end of its own menu, so reject the pair here rather than let the builder
    // discover it. generate_engine_config.py refuses the same pair.
    throw new ContractError(
      "invalid_recipe",
      "Starting in Sync In requires the syncInput preference.",
    );
  }
  return {
    preferences: {
      navigationMode: preferenceValues.navigationMode,
      calibration,
      colorBlindMode,
      replaceableFmBanks,
      syncInput,
      linearTzfm,
      fastFm,
      simplifiedPitchRanges,
      envelopeContour: hasEnvelopeContour,
    },
    initialOptions: {
      // Preserve the two v19 spellings at the private Worker/container boundary.
      // The generator translates them to the new orthogonal contour + TRIG
      // settings, while their old schema and cache identity remain valid.
      lockedFrequencyKnob: optionValues.lockedFrequencyKnob,
      trigResponse,
      modelInput: optionValues.modelInput,
      levelInput: optionValues.levelInput,
      auxOutput: optionValues.auxOutput,
      suboscillatorOctave: optionValues.suboscillatorOctave,
      chordTable: optionValues.chordTable,
      holdOnTrigger: optionValues.holdOnTrigger,
      // As with the preferences above, an absent key is undefined, so the
      // option falls to its legacy default with no reference to which tier
      // matched -- which is what makes forgetting to extend a condition here
      // impossible rather than merely unlikely.
      attenuverterMode: (optionValues.attenuverterMode
        ?? "stock") as NormalizedRecipe["initialOptions"]["attenuverterMode"],
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
  if (candidate.output !== "audio-wav" && candidate.output !== "intel-hex") {
    throw new ContractError(
      "unsupported_output",
      "Firmware output must be an audio-installable WAV or an application-only Intel HEX file.",
    );
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
        // Package digests are provenance, not caller-supplied source: the
        // compiler image contains the only code a build can use. Re-pin a stale
        // but compatible reference to that image's current approved engine.
        // Package identity and semantic-version compatibility remain the hard
        // boundary, so renamed/removed packages and breaking upgrades fail.
        if (!approved
            || reference.package !== approved.packageId
            || !isCompatiblePackageUpgrade(reference.version, approved.version)
            || typeof reference.digest !== "string"
            || !digestPattern.test(reference.digest)) {
          throw new ContractError("unapproved_package", "The recipe contains an unavailable package version.");
        }
        return approved.id;
      });
  validateBankShape(slots, schemaVersion);
  let chordTables: NormalizedChordTable[];
  let scaleBank: NormalizedScale[] | undefined;
  let userDataBanks: NormalizedUserDataBank[] | undefined;   // v6 index-keyed
  let slotBanks: NormalizedSlotBank[] | undefined;           // v12 slot-keyed
  let speechBanks: NormalizedSpeechBanks | undefined;        // v17
  let naturalSpeechBanks: NormalizedNaturalSpeechBanks | undefined; // v25
  let customModelData: NormalizedCustomModelData[] | undefined; // v24
  let terrainBank: NormalizedTerrainBankEntry[] | undefined; // v24
  let wavetableBank: NormalizedWavetableBank | undefined; // v26
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
    // Schema 16 can be reached by automatic LEVEL routing without a custom
    // scale bank. In that case scaleBank stays absent and the container compiles
    // its shipped default bank.
    const carriesScaleBank = schemaVersion >= scaleBankMinSchemaVersion
      && Object.hasOwn(resourceValues, "scaleBank");
    const carriesSpeechBanks = schemaVersion >= speechBanksMinSchemaVersion
      && Object.hasOwn(resourceValues, "speechBanks");
    const carriesCustomModelData = schemaVersion >= customModelDataMinSchemaVersion
      && Object.hasOwn(resourceValues, "customModelData");
    const carriesTerrainBank = schemaVersion >= terrainBankMinSchemaVersion
      && Object.hasOwn(resourceValues, "terrainBank");
    const carriesNaturalSpeechBanks = schemaVersion >= naturalSpeechBanksMinSchemaVersion
      && Object.hasOwn(resourceValues, "naturalSpeechBanks");
    const carriesWavetableBank = schemaVersion >= wavetableBankMinSchemaVersion
      && Object.hasOwn(resourceValues, "wavetableBank");
    const baseKeys = [
      "chordTables",
      ...(carriesScaleBank ? ["scaleBank"] : []),
      ...(carriesSpeechBanks ? ["speechBanks"] : []),
      ...(carriesCustomModelData ? ["customModelData"] : []),
      ...(carriesTerrainBank ? ["terrainBank"] : []),
      ...(carriesNaturalSpeechBanks ? ["naturalSpeechBanks"] : []),
      ...(carriesWavetableBank ? ["wavetableBank"] : []),
    ];
    const carriesUserDataBanks = expectsUserDataBanks
      || (schemaVersion >= calibrationMinSchemaVersion
        && hasExactKeys(resourceValues, [...baseKeys, "userDataBanks"]));
    const expectedKeys = carriesUserDataBanks
      ? [...baseKeys, "userDataBanks"] : baseKeys;
    if (!hasExactKeys(resourceValues, expectedKeys)) {
      throw new ContractError("invalid_resources", "The recipe must contain only supported firmware resources.");
    }
    chordTables = normalizeChordTables(resourceValues.chordTables);
    if (carriesScaleBank) {
      scaleBank = normalizeScaleBank(resourceValues.scaleBank);
    }
    if (carriesSpeechBanks) {
      if (!slots.includes("speech") && !slots.includes("lpc-speech")) {
        throw new ContractError(
          "invalid_speech_banks",
          "Speech word banks require the Speech or LPC Words model in the palette.",
        );
      }
      speechBanks = normalizeSpeechBanks(resourceValues.speechBanks);
    }
    if (carriesNaturalSpeechBanks) {
      if (!slots.includes("natural-speech")) {
        throw new ContractError(
          "invalid_natural_speech_banks",
          "Natural Speech word banks require the Natural Speech model in the palette.",
        );
      }
      naturalSpeechBanks = normalizeNaturalSpeechBanks(resourceValues.naturalSpeechBanks);
    }
    if (carriesCustomModelData) {
      customModelData = normalizeCustomModelData(resourceValues.customModelData, slots);
      if (schemaVersion >= terrainBankMinSchemaVersion
          && customModelData.some((entry) => entry.model.kind === "wave-terrain")) {
        throw new ContractError(
          "invalid_custom_model_data",
          "Schema 24 terrain data belongs in the shared terrain bank, not on a palette slot.",
        );
      }
    }
    if (carriesTerrainBank) {
      if (!slots.includes("wave-terrain")) {
        throw new ContractError(
          "invalid_terrain_bank",
          "A terrain bank requires Wave Terrain in the palette.",
        );
      }
      terrainBank = normalizeTerrainBank(resourceValues.terrainBank, schemaVersion);
    }
    if (carriesWavetableBank) {
      if (!(schemaVersion >= wavetableWaveLinesMinSchemaVersion
        ? slots.some((id) => id !== null && sharedWaveLibraryEngineIds.has(id))
        : slots.includes("wavetable"))) {
        throw new ContractError(
          "invalid_wavetable_bank",
          schemaVersion >= wavetableWaveLinesMinSchemaVersion
            ? "A shared wave library requires a compatible model in the palette."
            : "A wavetable bank requires Wavetable in the palette.",
        );
      }
      wavetableBank = normalizeWavetableBank(resourceValues.wavetableBank, schemaVersion);
    }
    const retainedFactoryWaves = new Set<string>(wavetableBank?.entries.flatMap((entry) => (
      entry.kind === "factory" ? [entry.id] : []
    )) ?? []);
    if (schemaVersion >= wavetableWaveLinesMinSchemaVersion
        && wavetableBank && slots.includes("wave-terrain") && !carriesTerrainBank
        && factoryWavetableIds.some((id) => !retainedFactoryWaves.has(id))) {
      throw new ContractError(
        "invalid_terrain_bank",
        "A shared wave library missing a factory source requires an explicit Wave Terrain bank.",
      );
    }
    if (schemaVersion >= wavetableWaveLinesMinSchemaVersion && terrainBank && wavetableBank) {
      const hasMissingFactorySource = terrainBank.some((entry) => (
        entry.kind === "factory"
        && factoryTerrainWaveSources.has(entry.id)
        && !retainedFactoryWaves.has(factoryTerrainWaveSources.get(entry.id)!)
      ));
      if (hasMissingFactorySource) {
        throw new ContractError(
          "invalid_terrain_bank",
          "A factory wavetable terrain requires its source bank in the shared wave library.",
        );
      }
    }
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
  if (chordTables.length > maxPreGestureChordTables
      && schemaVersion < gateArticulationMinSchemaVersion) {
    throw new ContractError(
      "unsupported_schema",
      `More than ${maxPreGestureChordTables} chord tables require recipe schema version ${gateArticulationMinSchemaVersion}.`,
    );
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
  const legacyContour = configuration.initialOptions.lockedFrequencyKnob === "triggered-envelope"
    || configuration.initialOptions.lockedFrequencyKnob === "gated-envelope";
  return {
    // Newest first: the Sync In compile switch is a preference of its own as of
    // v22 — a recipe that merely STARTS in Sync In no longer implies it (and is
    // rejected without it), so the starting value never sets the version alone.
    // A recipe-driven scale bank or engine-aware automatic LEVEL
    // routing requires v16. Ro'Ved's four-clickable-knob UI and the accessible
    // display each require v15, followed by the optional v14 calibration
    // procedure. These say nothing about the recipe's resource shape. A
    // per-slot custom bank with fewer than 32 patches (a
    // "short" FM bank) needs the firmware's variable-length Harmonics quantizer,
    // v13. Any other per-slot custom bank needs a v12 builder (only v12
    // keys banks by slot). Then a sparse bank (a gap kept in place) needs v11;
    // per-engine stereo (a stereoEngines list) needs v10; the global stereo aux
    // mode needs v9; more than six chord tables needs the fast-blink LED tier
    // (v8); a short-bank recipe (a trailing empty slot) stays v7; a candidate that
    // carried v6 resources (even an empty custom-bank list, e.g. a 32-slot recipe)
    // stays v6; else v5.
    schemaVersion: chordTables.length > maxPreGestureChordTables
      || (!legacyContour && (
        configuration.preferences.envelopeContour
        || configuration.initialOptions.trigResponse !== "trigger"
        || configuration.initialOptions.lockedFrequencyKnob === "envelope-contour"
      )) ? 27
      : wavetableBank?.waveLines !== undefined ? 28
      : wavetableBank !== undefined ? 26
      : naturalSpeechBanks !== undefined ? 25
      : configuration.preferences.simplifiedPitchRanges
      || terrainBank !== undefined
      || customModelData !== undefined ? 24
      : configuration.preferences.linearTzfm
      || configuration.preferences.fastFm ? 23
      : configuration.preferences.syncInput ? 22
      : configuration.preferences.replaceableFmBanks ? 20
      : legacyContour ? 19
      : schemaVersion >= attenuverterModeMinSchemaVersion ? 18
      : speechBanks !== undefined ? 17
      : scaleBank !== undefined
        || configuration.initialOptions.levelInput === "auto" ? 16
      : candidate.target === "plum-audio-roved"
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
    resources: {
      chordTables,
      ...(scaleBank !== undefined ? { scaleBank } : {}),
      ...(speechBanks !== undefined ? { speechBanks } : {}),
      ...(customModelData !== undefined ? { customModelData } : {}),
      ...(terrainBank !== undefined ? { terrainBank } : {}),
      ...(naturalSpeechBanks !== undefined ? { naturalSpeechBanks } : {}),
      ...(wavetableBank !== undefined ? { wavetableBank } : {}),
      ...((userDataBanks ?? slotBanks)
        ? { userDataBanks: userDataBanks ?? slotBanks }
        : {}),
    },
    output: candidate.output,
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
  // The field guide prints active TZ / 50k badges beside each selected model.
  // Fold the selected engines' current capability policy into the cache key so
  // changing that policy cannot serve a stale, differently badged PDF even
  // when the engine documentation itself did not change.
  const selectedEngineIds = documentation.map(([engineId]) => engineId);
  const linearTzfmEngineIds = new Set(catalog.fmCapabilities.linearTzfm);
  const fastFmEngineIds = new Set(catalog.fmCapabilities.fastFm);
  const fmBadges = selectedEngineIds.map((engineId) => [
    engineId,
    recipe.preferences.linearTzfm && linearTzfmEngineIds.has(engineId),
    recipe.preferences.fastFm && fastFmEngineIds.has(engineId),
  ]);
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
    fmBadges,
    chordTables: recipe.resources.chordTables.map((table) => table.name),
    scaleBank: recipe.resources.scaleBank?.map((scale) => scale.name) ?? [],
    customBanks,
    customModelData: recipe.resources.customModelData?.map((entry) => [
      entry.slot,
      entry.model.kind,
      entry.model.name,
    ]) ?? [],
    terrainBank: recipe.resources.terrainBank?.map((entry) => entry.kind === "factory"
      ? [entry.kind, entry.id]
      : [entry.kind, entry.model.name, entry.model.representation ?? "prebaked"]) ?? [],
    wavetableBank: recipe.resources.wavetableBank
      ? {
        mirrored: recipe.resources.wavetableBank.mirrored,
        entries: recipe.resources.wavetableBank.entries.map((entry) => entry.kind === "factory"
          ? [entry.kind, entry.id]
          : [entry.kind, entry.model.name, entry.model.representation ?? "prebaked"]),
        waveLines: recipe.resources.wavetableBank.waveLines ?? null,
      }
      : null,
    // The control instructions differ completely: Plaits has two buttons;
    // Ro'Ved has four clickable knobs. Never share a cached guide between them.
    target: recipe.target,
    // Both compile-time display/procedure preferences change what the guide
    // prints, so they must change its cache identity.
    calibration: recipe.preferences.calibration,
    colorBlindMode: recipe.preferences.colorBlindMode,
    linearTzfm: recipe.preferences.linearTzfm,
    fastFm: recipe.preferences.fastFm,
    // A non-Octaves starting assignment adds the locked-octave shortcut callout.
    lockedFrequencyKnob: recipe.initialOptions.lockedFrequencyKnob,
    // A Sync-enabled guide adds the fifth MODEL-input setting and its warning.
    modelInput: recipe.initialOptions.modelInput,
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
