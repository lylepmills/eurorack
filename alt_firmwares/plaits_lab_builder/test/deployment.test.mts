import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL("../wrangler.jsonc", import.meta.url), "utf8");
const config = JSON.parse(source.replace(/^\s*\/\/.*$/gm, ""));
const staging = config.env.staging;
const smoke = await readFile(new URL("../scripts/staging_smoke.mts", import.meta.url), "utf8");

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

test("the hardware smoke carries both shared-bank Speech models", () => {
  assert.match(smoke, /engine\.id === "speech"/);
  assert.match(smoke, /engine\.id === "lpc-speech"/);
  assert.match(smoke, /slots: \[reference\(speech\), reference\(lpcSpeech\)/);
});
