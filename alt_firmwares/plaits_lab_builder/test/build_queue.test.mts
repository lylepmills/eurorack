import assert from "node:assert/strict";
import test from "node:test";

import { buildQueueMessage, recipeArtifactKey } from "../src/build_queue.ts";

test("new firmware jobs queue a small recipe reference", () => {
  const buildId = "a".repeat(64);
  const message = buildQueueMessage(buildId);

  assert.deepEqual(message, { buildId });
  assert.ok(Buffer.byteLength(JSON.stringify(message)) < 1024);
  assert.equal("recipe" in message, false);
});

test("manual backfills use the same reference-only handoff", () => {
  const buildId = "b".repeat(64);

  assert.deepEqual(buildQueueMessage(buildId, true), { buildId, manualOnly: true });
  assert.equal(recipeArtifactKey(buildId), `recipes/${buildId}.json`);
});
