import assert from "node:assert/strict";
import test from "node:test";

import { corsHeaders } from "../src/cors.ts";

const configuredOrigins = "https://rubato.audio, https://www.rubato.audio";

for (const origin of ["https://rubato.audio", "https://www.rubato.audio"]) {
  test(`CORS permits the production origin ${origin}`, () => {
    const headers = corsHeaders(
      new Request("https://plaits-api.rubato.audio/v1/catalog", {
        headers: { Origin: origin },
      }),
      configuredOrigins,
    );

    assert.equal(headers.get("Access-Control-Allow-Origin"), origin);
    assert.equal(headers.get("Access-Control-Allow-Headers"), "Content-Type");
    assert.equal(headers.get("Access-Control-Allow-Methods"), "GET, POST, OPTIONS");
    assert.equal(headers.get("Vary"), "Origin");
  });
}

test("CORS does not permit untrusted origins", () => {
  const headers = corsHeaders(
    new Request("https://plaits-api.rubato.audio/v1/catalog", {
      headers: { Origin: "https://example.com" },
    }),
    configuredOrigins,
  );

  assert.equal(headers.get("Access-Control-Allow-Origin"), null);
  assert.equal(headers.get("Access-Control-Allow-Headers"), null);
  assert.equal(headers.get("Access-Control-Allow-Methods"), null);
  assert.equal(headers.get("Vary"), "Origin");
});
