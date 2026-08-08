import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";

const apiBase = process.env.PLAITS_STAGING_API ?? "https://plaits-api-staging.rubato.audio";
const siteBase = process.env.PLAITS_STAGING_SITE ?? "https://rubato-audio-staging.pages.dev";
const artifactDir = process.env.PLAITS_STAGING_ARTIFACT_DIR;
const expectedRevision = process.env.PLAITS_EXPECTED_SOURCE_REVISION;
const origin = new URL(siteBase).origin;
const headers = { Accept: "application/json", Origin: origin };

async function fetchEventually(
  input: URL,
  init: RequestInit,
  label: string,
  attempts = 30,
): Promise<Response> {
  for (let attempt = 1; attempt <= attempts; attempt += 1) {
    const response = await fetch(input, init);
    if (response.status !== 503 && response.status !== 522) return response;
    if (attempt === attempts) return response;
    console.log(`${label} is still provisioning (${response.status}); retrying in 10 seconds`);
    await response.body?.cancel();
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 10_000));
  }
  throw new Error(`${label} retry loop ended unexpectedly`);
}

async function json(response: Response, label: string): Promise<any> {
  const payload = await response.json();
  assert.equal(response.ok, true, `${label} returned ${response.status}: ${JSON.stringify(payload)}`);
  return payload;
}

async function getBinary(path: string, contentType: RegExp, label: string): Promise<Uint8Array> {
  const response = await fetchEventually(
    new URL(path, apiBase), { headers, cache: "no-store" }, label,
  );
  assert.equal(response.ok, true, `${label} returned ${response.status}`);
  assert.match(response.headers.get("content-type") ?? "", contentType, `${label} content type`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  assert.ok(bytes.byteLength > 44, `${label} was unexpectedly empty`);
  return bytes;
}

console.log(`Checking staging site ${siteBase}`);
const page = await fetchEventually(
  new URL("/plaits-palette/", siteBase), { cache: "no-store" }, "staging site",
);
assert.equal(page.ok, true, `staging Plaits Palette returned ${page.status}`);
assert.match(page.headers.get("x-robots-tag") ?? "", /noindex/i, "staging must be noindex");
assert.match(await page.text(), /PlaitsEditor\.[A-Za-z0-9_-]+\.js/, "staging page must hydrate the editor");

console.log(`Checking staging builder ${apiBase}`);
const catalogResponse = await fetch(new URL("/v1/catalog", apiBase), { headers, cache: "no-store" });
const catalog = await json(catalogResponse, "catalog");
assert.equal(catalogResponse.headers.get("access-control-allow-origin"), origin, "staging CORS origin");
assert.equal(catalog.deploymentEnvironment, "staging");
assert.ok(catalog.recipeSchemaVersion >= 21, "staging builder must support Sync In");
if (expectedRevision) assert.equal(catalog.sourceRevision, expectedRevision);

await getBinary("/v1/speech/voice-preview/en-US/af_heart.wav", /audio\/wav/, "voice preview");
await getBinary("/v1/speech/stock/bank-1-natural.wav", /audio\/wav/, "stock-bank preview");

const words = ["staging", "speech", "hardware", "check"];
const encodeRequest = {
  format: "rubato.plaits-lpc-word-bank/v1",
  name: "Staging speech hardware check",
  sourceText: words.join(" "),
  language: "en-US",
  entries: words.map((word) => ({ word })),
  synthesis: {
    voice: "af_heart",
    pitchContour: "flat-to-natural",
    referencePitchHz: 100,
  },
};
console.log("Encoding representative custom Speech bank");
const encoded = await json(await fetchEventually(new URL("/v1/speech/encode", apiBase), {
  method: "POST",
  headers: { ...headers, "Content-Type": "application/json" },
  body: JSON.stringify(encodeRequest),
}, "Speech encoder"), "Speech encoder");
assert.equal(encoded.entries.length, words.length);
assert.equal(encoded.wordBoundaries.length, words.length + 1);
assert.equal(typeof encoded.frameData, "string");

const restored = await json(await fetchEventually(new URL("/v1/speech/render-bank", apiBase), {
  method: "POST",
  headers: { ...headers, "Content-Type": "application/json" },
  body: JSON.stringify({
    format: "rubato.plaits-lpc-bank-preview/v1",
    bank: { words, wordBoundaries: encoded.wordBoundaries, frameData: encoded.frameData },
  }),
}, "saved-bank preview restoration"), "saved-bank preview restoration");
assert.match(restored.bankAudio.natural, /^data:audio\/wav;base64,/);
assert.match(restored.bankAudio.flat, /^data:audio\/wav;base64,/);

const publicCatalog = JSON.parse(await readFile(
  new URL("../../plaits_lab_catalog/public_catalog.json", import.meta.url), "utf8",
));
const chordCatalog = JSON.parse(await readFile(
  new URL("../../plaits_lab_chord_tables/catalog.json", import.meta.url), "utf8",
));
const originalSpeech = publicCatalog.engines.find((engine: { id: string }) => engine.id === "speech");
const speechSounds = publicCatalog.engines.find((engine: { id: string }) => engine.id === "formant-speech");
const lpcWords = publicCatalog.engines.find((engine: { id: string }) => engine.id === "lpc-speech");
assert.ok(originalSpeech, "Original Speech is missing from the public catalog");
assert.ok(speechSounds, "Speech Sounds is missing from the public catalog");
assert.ok(lpcWords, "LPC Words is missing from the public catalog");
const reference = (engine: any) => ({
  engine: engine.id,
  package: engine.packageId,
  version: engine.version,
  digest: engine.digest,
});
const recipe = {
  schemaVersion: 21,
  target: "mutable-instruments-plaits",
  firmware: "rubato-plaits",
  // Keep Original Speech beside both split engines in this hardware gate. All
  // three paths must boot, navigate, and speak before an image is promoted.
  // They also exercise the bounded Sync fallback in a compact build that
  // leaves enough flash headroom for an honest release canary.
  slots: [
    reference(originalSpeech),
    reference(speechSounds),
    reference(lpcWords),
    ...Array.from({ length: 21 }, () => null),
  ],
  output: "audio-wav",
  preferences: { navigationMode: "linear" },
  initialOptions: {
    lockedFrequencyKnob: "octaves",
    modelInput: "sync-in",
    levelInput: "level",
    auxOutput: "alternate-model",
    suboscillatorOctave: 0,
    chordTable: chordCatalog.tables[0].id,
    holdOnTrigger: false,
    attenuverterMode: "stock",
  },
  resources: {
    chordTables: [chordCatalog.tables[0]],
    speechBanks: {
      // Deliberately remove three factory banks: this covers the flash/resource
      // path that failed during the first Speech rollout.
      stockBankIds: [0, 3],
      customBanks: [{ words, wordBoundaries: encoded.wordBoundaries, frameData: encoded.frameData }],
    },
  },
};

console.log("Submitting representative firmware build (repeat runs should hit staging cache)");
let build = await json(await fetch(new URL("/v1/builds", apiBase), {
  method: "POST",
  headers: { ...headers, "Content-Type": "application/json" },
  body: JSON.stringify(recipe),
}), "firmware submission");
assert.equal(typeof build.buildId, "string");
const deadline = Date.now() + 20 * 60 * 1000;
while (build.status === "queued" || build.status === "building") {
  assert.ok(Date.now() < deadline, `firmware build ${build.buildId} timed out`);
  await new Promise((resolveDelay) => setTimeout(resolveDelay, 5000));
  build = await json(await fetch(new URL(`/v1/builds/${build.buildId}`, apiBase), {
    headers, cache: "no-store",
  }), "firmware status");
  console.log(`Build ${build.buildId}: ${build.status}`);
}
assert.equal(build.status, "succeeded", JSON.stringify(build.error ?? build));

const firmware = await getBinary(build.downloadUrl, /audio\/wav/, "firmware download");
assert.equal(Buffer.from(firmware.subarray(0, 4)).toString("ascii"), "RIFF");
let manual: Uint8Array | undefined;
if (build.manual?.downloadUrl) {
  manual = await getBinary(build.manual.downloadUrl, /application\/pdf/, "field guide");
  assert.equal(Buffer.from(manual.subarray(0, 4)).toString("ascii"), "%PDF");
}

if (artifactDir) {
  const output = resolve(artifactDir);
  await mkdir(output, { recursive: true });
  await writeFile(resolve(output, `sync-speech-staging-${build.buildId}.wav`), firmware);
  await writeFile(resolve(output, `sync-speech-staging-${build.buildId}.recipe.json`), `${JSON.stringify(recipe, null, 2)}\n`);
  if (manual) await writeFile(resolve(output, `sync-speech-staging-${build.buildId}.pdf`), manual);
  console.log(`Saved exact hardware-gate artifacts to ${output}`);
}

console.log(`Staging smoke passed for ${catalog.sourceRevision}; build ${build.buildId}${build.cacheHit ? " (cache hit)" : ""}`);
