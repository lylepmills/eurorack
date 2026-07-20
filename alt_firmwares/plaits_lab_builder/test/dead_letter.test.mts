import assert from "node:assert/strict";
import test from "node:test";
import { deadLetterAction } from "../src/dead_letter.ts";

test("a job with no recorded state fails terminally", () => {
  assert.deepEqual(deadLetterAction(null), { kind: "fail" });
});

test("a job stranded mid-flight fails terminally", () => {
  assert.deepEqual(deadLetterAction({ status: "queued" }), { kind: "fail" });
  assert.deepEqual(deadLetterAction({ status: "building" }), { kind: "fail" });
});

test("an already-failed job keeps its recorded cause", () => {
  assert.deepEqual(deadLetterAction({ status: "failed" }), { kind: "none" });
});

test("a succeeded build is never downgraded, only its pending manual", () => {
  assert.deepEqual(
    deadLetterAction({ status: "succeeded", manual: { status: "pending" } }),
    { kind: "manual-unavailable" },
  );
  assert.deepEqual(
    deadLetterAction({ status: "succeeded", manual: { status: "ready" } }),
    { kind: "none" },
  );
  assert.deepEqual(deadLetterAction({ status: "succeeded" }), { kind: "none" });
});
