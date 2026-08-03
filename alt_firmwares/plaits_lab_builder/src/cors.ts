export function corsHeaders(request: Request, configuredOrigins: string): Headers {
  const origin = request.headers.get("Origin");
  const allowedOrigins = configuredOrigins
    .split(",")
    .map((candidate) => candidate.trim())
    .filter(Boolean);
  const headers = new Headers({ Vary: "Origin" });
  if (origin !== null && allowedOrigins.includes(origin)) {
    headers.set("Access-Control-Allow-Origin", origin);
    headers.set("Access-Control-Allow-Headers", "Content-Type");
    headers.set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  }
  return headers;
}
