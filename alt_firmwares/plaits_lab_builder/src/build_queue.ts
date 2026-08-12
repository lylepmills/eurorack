import type { NormalizedRecipe } from "./contract";

// `recipe` is optional only for deployment compatibility: jobs submitted by
// older Workers carried the whole recipe in the queue message. New jobs store
// it in R2 and enqueue only this small reference, keeping large custom-bank
// recipes below Cloudflare Queues' per-message limit.
export type BuildMessage = {
  buildId: string;
  recipe?: NormalizedRecipe;
  manualOnly?: boolean;
};

export function recipeArtifactKey(buildId: string): string {
  return `recipes/${buildId}.json`;
}

export function buildQueueMessage(buildId: string, manualOnly = false): BuildMessage {
  return manualOnly ? { buildId, manualOnly: true } : { buildId };
}

export function queuedRecipe(value: unknown): NormalizedRecipe {
  // Queue messages and R2 objects are internal artifacts written only after
  // normalizeRecipe has accepted the browser manifest. Do not feed that
  // normalized representation back through the external-manifest parser:
  // normalized slots are engine-id strings, while browser manifests carry
  // versioned package-reference objects. processBuild recomputes the build key
  // before compiling, which verifies this exact stored value against the key
  // produced when it was accepted.
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error("The queued firmware recipe is invalid.");
  }
  return value as NormalizedRecipe;
}
