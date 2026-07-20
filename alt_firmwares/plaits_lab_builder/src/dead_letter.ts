// A build that simply fails to compile never reaches the dead-letter queue:
// `processBuild` acks it after five attempts and records `build_unavailable`
// itself. Anything that does arrive here failed *delivery* — an uncaught
// throw, an evicted isolate, a Durable Object write that did not land — so the
// job's last recorded state may be non-terminal and its client still polling.
// This decides what that stranded job should say, given whatever state the
// failed attempt managed to write.

export type DeadLetterPrior = {
  status: "queued" | "building" | "succeeded" | "failed";
  manual?: { status: "pending" | "ready" | "unavailable" };
} | null;

export type DeadLetterAction =
  | { kind: "fail" }
  | { kind: "manual-unavailable" }
  | { kind: "none" };

export function deadLetterAction(prior: DeadLetterPrior): DeadLetterAction {
  // Firmware already in R2 is not undone by a lost message. A manual-only
  // redelivery is the usual way a succeeded job lands here, and the only thing
  // still outstanding is the field guide.
  if (prior?.status === "succeeded") {
    return prior.manual?.status === "pending" ? { kind: "manual-unavailable" } : { kind: "none" };
  }
  // An already-failed job has a more specific cause recorded than this one.
  if (prior?.status === "failed") {
    return { kind: "none" };
  }
  return { kind: "fail" };
}
