import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";

const apiBase = process.env.PLAITS_STAGING_API ?? "https://plaits-api-staging.rubato.audio";
const siteBase = process.env.PLAITS_STAGING_SITE ?? "https://rubato-audio-staging.pages.dev";
const artifactDir = process.env.PLAITS_STAGING_ARTIFACT_DIR;
const expectedRevision = process.env.PLAITS_EXPECTED_SOURCE_REVISION;
// The same recipe is both the staging release gate and the production canary the
// README requires after a rollout. Defaults target staging, so an unset
// environment runs the gate exactly as before; the canary opts in explicitly.
const deploymentEnvironment = process.env.PLAITS_DEPLOYMENT_ENVIRONMENT ?? "staging";
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

console.log(`Checking ${deploymentEnvironment} site ${siteBase}`);
const page = await fetchEventually(
  new URL("/plaits-palette/", siteBase), { cache: "no-store" }, "staging site",
);
assert.equal(page.ok, true, `staging Plaits Palette returned ${page.status}`);
// Staging must never be indexable; production must never be accidentally
// noindexed. Same header, opposite expectation — checking only one direction
// would let a bad robots rule reach the public site unnoticed.
if (deploymentEnvironment === "staging") {
  assert.match(page.headers.get("x-robots-tag") ?? "", /noindex/i, "staging must be noindex");
} else {
  assert.doesNotMatch(
    page.headers.get("x-robots-tag") ?? "", /noindex/i, "production must stay indexable",
  );
}
assert.match(await page.text(), /PlaitsEditor\.[A-Za-z0-9_-]+\.js/, "staging page must hydrate the editor");

console.log(`Checking ${deploymentEnvironment} builder ${apiBase}`);
const catalogResponse = await fetch(new URL("/v1/catalog", apiBase), { headers, cache: "no-store" });
const catalog = await json(catalogResponse, "catalog");
assert.equal(catalogResponse.headers.get("access-control-allow-origin"), origin, "CORS origin");
assert.equal(catalog.deploymentEnvironment, deploymentEnvironment);
assert.ok(catalog.recipeSchemaVersion >= 22, "builder must support the Sync In preference");
// The catalog reports the WORKER's var. It proves the Worker deployed; it does
// NOT prove the Container serving compiles is the matching image, because the
// two disagree for the length of a Container rollout. The authoritative check is
// against the artifact's own stamped revision, below.
if (expectedRevision) assert.equal(catalog.sourceRevision, expectedRevision, "Worker catalog revision");

await getBinary("/v1/speech/voice-preview/en-US/af_heart.wav", /audio\/wav/, "voice preview");
await getBinary("/v1/speech/stock/bank-1-natural.wav", /audio\/wav/, "stock-bank preview");

const words = [
  "be", "go", "cat", "dog", "red", "blue", "up", "down",
  "left", "right", "one", "two", "three", "four", "five", "six",
  "sun", "moon", "star", "sky", "day", "night", "hot", "cold",
  "yes", "no", "in", "out", "on", "off", "high", "low",
];
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
  schemaVersion: 22,
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
  // Sync In's compile switch moved onto its own preference in v22. The starting
  // value alone no longer enables it — and is now rejected without it, since that
  // pair would start the module past the end of its own MODEL menu.
  preferences: {
    navigationMode: "linear",
    calibration: false,
    colorBlindMode: false,
    replaceableFmBanks: false,
    syncInput: true,
  },
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

console.log(`Submitting representative firmware build (repeat runs should hit the ${deploymentEnvironment} cache)`);
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

// THE gate this script exists for: the revision the compiler stamped into these
// exact bytes. Checking the catalog var instead let the 2026-08-21 octave-fix
// rollout pass while the staging Container was still serving the previous image,
// so the gate validated -- and saved for the hardware audition -- pre-fix
// firmware. A cache hit is also covered: the stored artifact reports the
// revision that built it, not the current var.
if (expectedRevision) {
  assert.equal(
    build.artifact?.sourceRevision,
    expectedRevision,
    `compiler stamped ${build.artifact?.sourceRevision || "(nothing)"} but this gate expects `
    + `${expectedRevision}. If these differ, the Container is still rolling: wait until `
    + "`wrangler containers info <app-id>` shows configuration.image on the intended tag "
    + "with starting == 0, then re-run. NOTE the artifact just built is cached under a key "
    + "claiming the wrong revision, so purge it before re-gating this revision.",
  );
}

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
  await writeFile(resolve(output, `sync-speech-${deploymentEnvironment}-${build.buildId}.wav`), firmware);
  await writeFile(resolve(output, `sync-speech-${deploymentEnvironment}-${build.buildId}.recipe.json`), `${JSON.stringify(recipe, null, 2)}\n`);
  if (manual) await writeFile(resolve(output, `sync-speech-${deploymentEnvironment}-${build.buildId}.pdf`), manual);
  console.log(`Saved exact ${deploymentEnvironment} artifacts to ${output}`);
}

console.log(
  `${deploymentEnvironment} smoke passed for `
  + `${build.artifact?.sourceRevision || catalog.sourceRevision} `
  + `(compiler-stamped${build.artifact?.sourceRevision ? "" : " unavailable, showing Worker var"}); `
  + `build ${build.buildId}${build.cacheHit ? " (cache hit)" : ""}`,
);
console.log(`  firmware WAV ${firmware.byteLength} bytes`);
