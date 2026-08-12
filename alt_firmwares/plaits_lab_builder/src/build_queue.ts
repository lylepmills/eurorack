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
