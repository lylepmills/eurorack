import assert from "node:assert/strict";
import test from "node:test";
import { isTransientPlatformError, withTransientRetry } from "../src/transient.ts";

// The exact message production logged on 2026-09-26.
const r2Unavailable = new Error(
  "get: Please look at https://www.cloudflarestatus.com for issues or contact customer support. (10043)");
const doReset = Object.assign(
  new Error("Internal error in Durable Object storage caused object to be reset; reference = d18shjggdi69sppch6mfppeh"),
  { retryable: true });
const noSleep = async () => undefined;

test("R2 internal and unavailable errors are transient", () => {
  assert.equal(isTransientPlatformError(r2Unavailable), true);
  assert.equal(isTransientPlatformError(new Error("get: We encountered an internal error. (10001)")), true);
});

test("other R2 errors are not retried", () => {
  assert.equal(isTransientPlatformError(new Error("put: Your metadata headers exceed the maximum allowed metadata size. (10012)")), false);
  assert.equal(isTransientPlatformError(new Error("put: Rate limit exceeded. (10058)")), false);
});

test("a retryable Durable Object error is transient, an overloaded one is not", () => {
  assert.equal(isTransientPlatformError(doReset), true);
  assert.equal(isTransientPlatformError(Object.assign(new Error("Durable Object is overloaded."), { retryable: true, overloaded: true })), false);
});

test("ordinary errors and non-errors are not transient", () => {
  assert.equal(isTransientPlatformError(new Error("The queued firmware recipe is missing.")), false);
  assert.equal(isTransientPlatformError(new TypeError("fetch failed")), false);
  assert.equal(isTransientPlatformError("(10043)"), false);
  assert.equal(isTransientPlatformError(null), false);
});

test("a transient failure is retried until it succeeds", async () => {
  let calls = 0;
  const delays: number[] = [];
  const result = await withTransientRetry(async () => {
    calls += 1;
    if (calls < 3) throw r2Unavailable;
    return "firmware";
  }, { sleep: async (ms) => { delays.push(ms); } });
  assert.equal(result, "firmware");
  assert.equal(calls, 3);
  assert.deepEqual(delays, [200, 400]);
});

test("retries stop after the attempt limit and rethrow the last error", async () => {
  let calls = 0;
  await assert.rejects(
    withTransientRetry(async () => { calls += 1; throw doReset; }, { sleep: noSleep }),
    (error) => error === doReset);
  assert.equal(calls, 3);
});

test("a non-transient failure is thrown at once", async () => {
  let calls = 0;
  const bug = new Error("The queued firmware recipe is missing.");
  await assert.rejects(
    withTransientRetry(async () => { calls += 1; throw bug; }, { sleep: noSleep }),
    (error) => error === bug);
  assert.equal(calls, 1);
});

test("each retry is reported", async () => {
  const seen: number[] = [];
  let calls = 0;
  await withTransientRetry(async () => {
    calls += 1;
    if (calls === 1) throw r2Unavailable;
  }, { sleep: noSleep, onRetry: (attempt) => seen.push(attempt) });
  assert.deepEqual(seen, [1]);
});
