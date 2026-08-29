import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL("../wrangler.jsonc", import.meta.url), "utf8");
const config = JSON.parse(source.replace(/^\s*\/\/.*$/gm, ""));
const staging = config.env.staging;
const smoke = await readFile(new URL("../scripts/staging_smoke.mts", import.meta.url), "utf8");
const packageJson = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));

test("production deploy explicitly targets the root Wrangler environment", () => {
  assert.match(packageJson.scripts.deploy, /wrangler deploy --env=(?:""|\\"\\")$/);
});

test("staging promotes the same immutable release identity and container image", () => {
  for (const key of [
    "PLAITS_SOURCE_REVISION",
    "PLAITS_TOOLCHAIN_ID",
    "PLAITS_BUILD_CONTRACT",
    "PLAITS_MANUAL_CONTRACT",
  ]) {
    assert.equal(staging.vars[key], config.vars[key], key);
  }
  assert.equal(staging.containers[0].image, config.containers[0].image);
});

test("staging cannot consume production jobs, artifacts, rate limits, or dead letters", () => {
  assert.notEqual(staging.queues.producers[0].queue, config.queues.producers[0].queue);
  assert.notEqual(staging.r2_buckets[0].bucket_name, config.r2_buckets[0].bucket_name);
  assert.notEqual(staging.ratelimits[0].namespace_id, config.ratelimits[0].namespace_id);
  assert.notEqual(staging.vars.DEAD_LETTER_QUEUE, config.vars.DEAD_LETTER_QUEUE);
  assert.equal(staging.vars.DEPLOYMENT_ENVIRONMENT, "staging");
  assert.equal(config.vars.DEPLOYMENT_ENVIRONMENT, "production");
});

test("every environment's queue consumer agrees with its dead-letter variable", () => {
  assert.equal(config.queues.consumers[0].dead_letter_queue, config.vars.DEAD_LETTER_QUEUE);
  assert.equal(staging.queues.consumers[0].dead_letter_queue, staging.vars.DEAD_LETTER_QUEUE);
});

test("the hardware smoke carries Speech plus the full Wave Terrain release bank", () => {
  assert.match(smoke, /engine\.id === "speech"/);
  assert.match(smoke, /engine\.id === "formant-speech"/);
  assert.match(smoke, /engine\.id === "lpc-speech"/);
  assert.match(smoke, /engine\.id === "wave-terrain"/);
  assert.match(
    smoke,
    /slots:\s*\[\s*reference\(waveTerrain\),\s*reference\(originalSpeech\),\s*reference\(speechSounds\),\s*reference\(lpcWords\),/s,
  );
  assert.match(smoke, /assert\.equal\(terrainBank\.length, 16/);
  assert.match(smoke, /"native"/);
});

// The 2026-08-21 octave-fix rollout passed this gate against the PREVIOUS
// firmware. The build key is derived from the Worker's PLAITS_SOURCE_REVISION
// var, and the var updates the instant `deploy:staging` returns, while the
// Container serving compiles keeps running the old image until its rollout
// finishes. So the smoke asserted a revision the Worker merely advertised and
// saved a pre-fix WAV as the artifact for the hardware audition.
test("the release gate checks the revision the compiler stamped, not the Worker's var", () => {
  // The catalog var alone must never be the only revision assertion.
  assert.match(smoke, /build\.artifact\?\.sourceRevision/);
  assert.match(
    smoke,
    /assert\.equal\(\s*build\.artifact\?\.sourceRevision,\s*expectedRevision/s,
    "the smoke must assert the artifact's stamped revision against the expected one",
  );
});

test("a stale-image gate failure explains that the Container is still rolling", () => {
  // The failure is indistinguishable from a bad build unless the message says
  // so, and it also leaves a mis-keyed artifact behind that must be purged.
  assert.match(smoke, /still rolling/);
  assert.match(smoke, /purge/i);
});
