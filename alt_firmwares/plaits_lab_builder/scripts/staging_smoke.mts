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
const waveTerrain = publicCatalog.engines.find((engine: { id: string }) => engine.id === "wave-terrain");
assert.ok(originalSpeech, "Original Speech is missing from the public catalog");
assert.ok(speechSounds, "Speech Sounds is missing from the public catalog");
assert.ok(lpcWords, "LPC Words is missing from the public catalog");
assert.ok(waveTerrain, "Wave Terrain is missing from the public catalog");
const reference = (engine: any) => ({
  engine: engine.id,
  package: engine.packageId,
  version: engine.version,
  digest: engine.digest,
});

function canaryTerrainData(seed: number): string {
  const bytes = Buffer.alloc(64 * 64);
  for (let row = 0; row < 64; row += 1) {
    for (let column = 0; column < 64; column += 1) {
      const x = column / 31.5 - 1;
      const y = row / 31.5 - 1;
      const z = 0.55 * Math.sin((seed + 3) * Math.PI * x)
        + 0.35 * Math.cos((seed + 5) * Math.PI * y)
        + 0.1 * x * y;
      const sample = Math.max(-127, Math.min(127, Math.round(z * 127)));
      bytes[row * 64 + column] = sample & 0xff;
    }
  }
  return bytes.toString("base64");
}

function canaryTerrain(
  name: string,
  equation: string,
  seed: number,
  representation?: "native",
) {
  return {
    kind: "custom",
    model: {
      kind: "wave-terrain",
      name,
      equation,
      data: canaryTerrainData(seed),
      ...(representation ? { representation } : {}),
    },
  };
}

const terrainBank = [
  { kind: "factory", id: "factory-1" },
  { kind: "factory", id: "factory-6" },
  canaryTerrain("Linear field", "x + y", 0, "native"),
  canaryTerrain("Soft rings", "sin(10 * r)", 1, "native"),
  canaryTerrain(
    "Lone island",
    "max(0, 1 - 2.2 * sqrt((x + 0.25)^2 + (y - 0.15)^2))",
    2,
    "native",
  ),
  canaryTerrain("Tilted terraces", "round(3 * (x + 0.35 * y)) + 0.4 * y", 3, "native"),
  canaryTerrain("Four chambers", "sign(x * y) * (1 - 0.55 * r) + 0.2 * x", 4, "native"),
  canaryTerrain("Spiral current", "sin(10 * r + 3 * atan2(y, x))", 5, "native"),
  canaryTerrain(
    "Twin pulses",
    "ball(-0.38, -0.22, 0.32) - 0.85 * ball(0.38, 0.27, 0.18)",
    6,
    "native",
  ),
  canaryTerrain(
    "Eight-sine stress",
    "sin(12*x)+sin(13*y)+sin(9*(x+y))+cos(11*(x-y))+sin(15*r)+cos(7*r+2*theta)+sin(17*x*y)+cos(5*x-8*y)",
    7,
  ),
  canaryTerrain(
    "Layered current crater",
    "sin(10*r)+0.7*sin(10*r+3*atan2(y,x))+0.4*sin(7*log(0.14+r))",
    8,
  ),
  canaryTerrain(
    "Rippled twin pulses",
    "ball(-0.38,-0.22,0.32)-0.85*ball(0.38,0.27,0.18)+0.5*sin(10*r)",
    9,
  ),
  canaryTerrain(
    "Rippled diamond",
    "pow(abs(x),0.45)+pow(abs(y),0.45)-1+0.4*sin(11*r)",
    10,
  ),
  canaryTerrain(
    "Terraced log crater",
    "round(3*(x+0.35*y))+0.4*y+0.5*sin(7*log(0.14+r))",
    11,
  ),
  canaryTerrain(
    "Folded crater",
    "max(sin(10*r),0.35*tan(1.1*(x+0.25*sin(4*y))))+0.4*sin(7*log(0.14+r))",
    12,
  ),
  { kind: "factory", id: "factory-8" },
];
assert.equal(terrainBank.length, 16, "hardware canary must exercise the full terrain bank");

const recipe = {
  schemaVersion: 24,
  target: "mutable-instruments-plaits",
  firmware: "rubato-plaits",
  // Keep Original Speech beside both split engines in this hardware gate. All
  // three paths must boot, navigate, and speak before an image is promoted.
  // Wave Terrain adds the full factory/native/prebaked bank that the same exact
  // artifact exercises during the physical hardware gate.
  slots: [
    reference(waveTerrain),
    reference(originalSpeech),
    reference(speechSounds),
    reference(lpcWords),
    ...Array.from({ length: 20 }, () => null),
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
    linearTzfm: false,
    fastFm: false,
    simplifiedPitchRanges: false,
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
    terrainBank,
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
  await writeFile(resolve(output, `wave-terrain-${deploymentEnvironment}-${build.buildId}.wav`), firmware);
  await writeFile(resolve(output, `wave-terrain-${deploymentEnvironment}-${build.buildId}.recipe.json`), `${JSON.stringify(recipe, null, 2)}\n`);
  if (manual) await writeFile(resolve(output, `wave-terrain-${deploymentEnvironment}-${build.buildId}.pdf`), manual);
  console.log(`Saved exact ${deploymentEnvironment} artifacts to ${output}`);
}

console.log(
  `${deploymentEnvironment} smoke passed for `
  + `${build.artifact?.sourceRevision || catalog.sourceRevision} `
  + `(compiler-stamped${build.artifact?.sourceRevision ? "" : " unavailable, showing Worker var"}); `
  + `build ${build.buildId}${build.cacheHit ? " (cache hit)" : ""}`,
);
console.log(`  firmware WAV ${firmware.byteLength} bytes`);
