// Cloudflare's own storage occasionally fails for a moment, and each time a
// user saw a 500 for something a retry would have served. Both cases below
// reached users in production in September 2026: R2 answered one firmware
// download with 10043 four times in a row (the user gave up), and a Durable
// Object storage reset failed one Speech encode.
//
// Only the failures Cloudflare documents as retryable are retried. Everything
// else still surfaces at once, so a real bug cannot hide behind retries.

// R2 puts its error code at the end of the message, e.g.
// "get: Please look at https://www.cloudflarestatus.com ... (10043)".
// 10001 InternalError and 10043 ServiceUnavailable are the two its docs say to
// retry. 10058 (too many writes to one key) is not transient for our reads.
const RETRYABLE_R2_CODES = new Set([10001, 10043]);

export function isTransientPlatformError(error: unknown): boolean {
  if (!(error instanceof Error)) return false;
  // Durable Object errors carry these flags. An overloaded object only gets
  // worse when retried, which is why Cloudflare sets both on that case.
  const flags = error as Error & { retryable?: unknown; overloaded?: unknown };
  if (flags.retryable === true && flags.overloaded !== true) return true;
  const code = /\((\d{5})\)\s*$/.exec(error.message)?.[1];
  return code !== undefined && RETRYABLE_R2_CODES.has(Number(code));
}

export type RetryOptions = {
  attempts?: number;
  baseDelayMs?: number;
  sleep?: (ms: number) => Promise<void>;
  onRetry?: (attempt: number, error: unknown) => void;
};

// Waits 200 ms, then 400 ms: short enough that a user clicking a download link
// does not notice, and R2 recommends backing off exponentially for 10043.
export async function withTransientRetry<T>(
  operation: () => Promise<T>,
  { attempts = 3, baseDelayMs = 200, sleep = defaultSleep, onRetry }: RetryOptions = {},
): Promise<T> {
  for (let attempt = 1; ; attempt += 1) {
    try {
      return await operation();
    } catch (error) {
      if (attempt >= attempts || !isTransientPlatformError(error)) throw error;
      onRetry?.(attempt, error);
      await sleep(baseDelayMs * 2 ** (attempt - 1));
    }
  }
}

function defaultSleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
