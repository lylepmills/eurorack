import { Container, getContainer } from "@cloudflare/containers";
import { DurableObject } from "cloudflare:workers";
import {
  ContractError,
  approvedChordTables,
  approvedEngineIds,
  computeBuildKey,
  isBuildKey,
  normalizeRecipe,
  type NormalizedRecipe,
} from "./contract";

type JobStatus = "queued" | "building" | "succeeded" | "failed";

type JobState = {
  buildId: string;
  status: JobStatus;
  createdAt: string;
  updatedAt: string;
  cacheHit: boolean;
  artifact?: {
    bytes: number;
    wavSha256: string;
    binarySha256: string;
    textBytes: number;
    dataBytes: number;
    bssBytes: number;
  };
  error?: { code: string; message: string };
};

type BuildMessage = {
  buildId: string;
  recipe: NormalizedRecipe;
};

export class FirmwareBuilder extends Container<Env> {
  defaultPort = 8080;
  requiredPorts = [8080];
  sleepAfter = "15m";
  enableInternet = false;
}

export class BuildJob extends DurableObject<Env> {
  async getState(): Promise<JobState | null> {
    return (await this.ctx.storage.get<JobState>("state")) ?? null;
  }

  async setState(state: JobState): Promise<void> {
    await this.ctx.storage.put("state", state);
  }
}

function json(value: unknown, init: ResponseInit = {}): Response {
  const headers = new Headers(init.headers);
  headers.set("Content-Type", "application/json; charset=utf-8");
  headers.set("Cache-Control", "no-store");
  return Response.json(value, { ...init, headers });
}

function corsHeaders(request: Request, env: Env): Headers {
  const origin = request.headers.get("Origin");
  const headers = new Headers({ Vary: "Origin" });
  if (origin === env.PUBLIC_ORIGIN) {
    headers.set("Access-Control-Allow-Origin", origin);
    headers.set("Access-Control-Allow-Headers", "Content-Type");
    headers.set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  }
  return headers;
}

function artifactKey(buildId: string): string {
  return `firmware/${buildId}.wav`;
}

function artifactSummary(artifact: R2Object): NonNullable<JobState["artifact"]> {
  return {
    bytes: artifact.size,
    wavSha256: artifact.customMetadata?.wavSha256 ?? "",
    binarySha256: artifact.customMetadata?.binarySha256 ?? "",
    textBytes: Number(artifact.customMetadata?.textBytes ?? 0),
    dataBytes: Number(artifact.customMetadata?.dataBytes ?? 0),
    bssBytes: Number(artifact.customMetadata?.bssBytes ?? 0),
  };
}

function jobResponse(state: JobState): Record<string, unknown> {
  return {
    buildId: state.buildId,
    buildKey: state.buildId,
    status: state.status,
    createdAt: state.createdAt,
    updatedAt: state.updatedAt,
    cacheHit: state.cacheHit,
    artifact: state.artifact,
    error: state.error,
    statusUrl: `/v1/builds/${state.buildId}`,
    downloadUrl: state.status === "succeeded" ? `/v1/builds/${state.buildId}/firmware` : undefined,
  };
}

async function createBuild(request: Request, env: Env): Promise<Response> {
  let input: unknown;
  try {
    input = await request.json();
  } catch {
    return json({ error: { code: "invalid_json", message: "The request body is not valid JSON." } }, { status: 400 });
  }

  try {
    const recipe = normalizeRecipe(input);
    const buildId = await computeBuildKey(recipe, {
      sourceRevision: env.PLAITS_SOURCE_REVISION,
      toolchain: env.PLAITS_TOOLCHAIN_ID,
      contract: env.PLAITS_BUILD_CONTRACT,
    });
    const job = env.BUILD_JOBS.getByName(buildId);
    const [existingArtifact, previousState] = await Promise.all([
      env.ARTIFACTS.head(artifactKey(buildId)),
      job.getState(),
    ]);
    const now = new Date().toISOString();

    if (existingArtifact) {
      const state: JobState = {
        buildId,
        status: "succeeded",
        createdAt: previousState?.createdAt ?? now,
        updatedAt: now,
        cacheHit: true,
        artifact: artifactSummary(existingArtifact),
      };
      await job.setState(state);
      return json(jobResponse(state), { status: 200 });
    }

    if (previousState?.status === "queued" || previousState?.status === "building") {
      return json(jobResponse(previousState), { status: 202 });
    }

    const clientIp = request.headers.get("CF-Connecting-IP") ?? "local-development";
    const { success } = await env.BUILD_RATE_LIMITER.limit({ key: clientIp });
    if (!success) {
      return json(
        {
          error: {
            code: "build_rate_limited",
            message: "Too many new firmware builds were requested from this connection. Please wait a minute and try again.",
          },
        },
        { status: 429, headers: { "Retry-After": "60" } },
      );
    }

    const state: JobState = {
      buildId,
      status: "queued",
      createdAt: previousState?.createdAt ?? now,
      updatedAt: now,
      cacheHit: false,
    };
    await job.setState(state);
    try {
      await env.BUILD_QUEUE.send({ buildId, recipe }, { contentType: "json" });
    } catch (error) {
      await job.setState({
        ...state,
        status: "failed",
        updatedAt: new Date().toISOString(),
        error: { code: "queue_unavailable", message: "The firmware build could not be queued. Please try again." },
      });
      console.error(JSON.stringify({ message: "firmware queue send failed", buildId, error: String(error) }));
      return json({ error: { code: "queue_unavailable", message: "The firmware build could not be queued. Please try again." } }, { status: 503 });
    }
    return json(jobResponse(state), { status: 202 });
  } catch (error) {
    if (error instanceof ContractError) {
      return json({ error: { code: error.code, message: error.message } }, { status: 400 });
    }
    throw error;
  }
}

async function getStoredBuild(buildId: string, env: Env): Promise<JobState | null> {
  if (!isBuildKey(buildId)) return null;
  return env.BUILD_JOBS.getByName(buildId).getState();
}

async function getBuild(buildId: string, env: Env): Promise<Response> {
  if (!isBuildKey(buildId)) {
    return json({ error: { code: "invalid_build_key", message: "The build key is invalid." } }, { status: 400 });
  }
  const state = await getStoredBuild(buildId, env);
  return state
    ? json(jobResponse(state))
    : json({ error: { code: "build_not_found", message: "That firmware build does not exist." } }, { status: 404 });
}

async function downloadFirmware(buildId: string, env: Env): Promise<Response> {
  if (!isBuildKey(buildId)) {
    return json({ error: { code: "invalid_build_key", message: "The build key is invalid." } }, { status: 400 });
  }
  const state = await getStoredBuild(buildId, env);
  if (!state || state.status !== "succeeded") {
    return json({ error: { code: "build_not_found", message: "That firmware build does not exist." } }, { status: 404 });
  }
  const artifact = await env.ARTIFACTS.get(artifactKey(state.buildId));
  if (!artifact) {
    return json({ error: { code: "artifact_not_found", message: "That firmware artifact is not available." } }, { status: 404 });
  }
  const headers = new Headers();
  artifact.writeHttpMetadata(headers);
  headers.set("ETag", artifact.httpEtag);
  headers.set("Content-Length", String(artifact.size));
  headers.set("Content-Disposition", 'attachment; filename="rubato-plaits-firmware.wav"');
  headers.set("Cache-Control", "public, max-age=3600, immutable");
  return new Response(artifact.body, { headers });
}

async function processBuild(message: Message<BuildMessage>, env: Env): Promise<void> {
  const { buildId, recipe } = message.body;
  const job = env.BUILD_JOBS.getByName(buildId);
  const prior = await job.getState();
  const now = new Date().toISOString();
  const baseState = {
    buildId,
    createdAt: prior?.createdAt ?? now,
    cacheHit: false,
  };
  const expectedBuildKey = await computeBuildKey(recipe, {
    sourceRevision: env.PLAITS_SOURCE_REVISION,
    toolchain: env.PLAITS_TOOLCHAIN_ID,
    contract: env.PLAITS_BUILD_CONTRACT,
  });
  if (expectedBuildKey !== buildId) {
    await job.setState({
      ...baseState,
      status: "failed",
      updatedAt: now,
      error: { code: "stale_build", message: "The firmware source changed while this build was queued. Please submit it again." },
    });
    message.ack();
    return;
  }
  const existingArtifact = await env.ARTIFACTS.head(artifactKey(buildId));
  if (existingArtifact) {
    await job.setState({
      ...baseState,
      status: "succeeded",
      updatedAt: now,
      cacheHit: true,
      artifact: artifactSummary(existingArtifact),
    });
    message.ack();
    return;
  }

  await job.setState({
    ...baseState,
    status: "building",
    updatedAt: now,
  });

  const container = getContainer(env.FIRMWARE_BUILDER, buildId);
  try {
    let response = await container.fetch("http://container/build", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ buildKey: buildId, recipe }),
    });
    const pollDeadline = Date.now() + 13 * 60 * 1000;
    while (response.status === 202) {
      await response.arrayBuffer();
      if (Date.now() >= pollDeadline) throw new Error("Compiler container polling timed out");
      await new Promise((resolve) => setTimeout(resolve, 5000));
      response = await container.fetch(`http://container/build/${buildId}`);
    }
    if (!response.ok) {
      const failure: { error?: { code?: string; message?: string; detail?: string } } = await response
        .json<{ error?: { code?: string; message?: string; detail?: string } }>()
        .catch(() => ({}));
      if (response.status >= 500) throw new Error(failure.error?.message ?? "Compiler container failed");
      console.error(JSON.stringify({
        message: "compiler rejected firmware build",
        buildId,
        code: failure.error?.code ?? "compiler_failed",
        detail: failure.error?.detail ?? "",
      }));
      await job.setState({
        ...baseState,
        status: "failed",
        updatedAt: new Date().toISOString(),
        error: {
          code: failure.error?.code ?? "compiler_failed",
          message: failure.error?.message ?? "The recipe did not produce a valid firmware build.",
        },
      });
      message.ack();
      return;
    }
    if (!response.body || response.headers.get("Content-Type") !== "audio/wav") {
      throw new Error("Compiler returned an invalid artifact");
    }

    const artifactBytes = await response.arrayBuffer();
    if (artifactBytes.byteLength === 0 || artifactBytes.byteLength > 32 * 1024 * 1024) {
      throw new Error("Compiler returned an invalid artifact size");
    }
    const metadata = {
      wavSha256: response.headers.get("X-Plaits-Wav-Sha256") ?? "",
      binarySha256: response.headers.get("X-Plaits-Binary-Sha256") ?? "",
      textBytes: response.headers.get("X-Plaits-Text-Bytes") ?? "0",
      dataBytes: response.headers.get("X-Plaits-Data-Bytes") ?? "0",
      bssBytes: response.headers.get("X-Plaits-Bss-Bytes") ?? "0",
      sourceRevision: response.headers.get("X-Plaits-Source-Revision") ?? env.PLAITS_SOURCE_REVISION,
      toolchain: response.headers.get("X-Plaits-Toolchain") ?? env.PLAITS_TOOLCHAIN_ID,
      buildContract: response.headers.get("X-Plaits-Build-Contract") ?? env.PLAITS_BUILD_CONTRACT,
    };
    console.log(JSON.stringify({ message: "compiler artifact buffered", buildId, bytes: artifactBytes.byteLength }));
    const artifact = await env.ARTIFACTS.put(artifactKey(buildId), artifactBytes, {
      httpMetadata: { contentType: "audio/wav" },
      customMetadata: metadata,
    });
    await env.ARTIFACTS.put(
      `manifests/${buildId}.json`,
      JSON.stringify({ buildKey: buildId, recipe, metadata }, null, 2) + "\n",
      { httpMetadata: { contentType: "application/json" } },
    );
    await job.setState({
      ...baseState,
      status: "succeeded",
      updatedAt: new Date().toISOString(),
      artifact: {
        bytes: artifact.size,
        wavSha256: metadata.wavSha256,
        binarySha256: metadata.binarySha256,
        textBytes: Number(metadata.textBytes),
        dataBytes: Number(metadata.dataBytes),
        bssBytes: Number(metadata.bssBytes),
      },
    });
    message.ack();
  } catch (error) {
    if (message.attempts >= 5) {
      await job.setState({
        ...baseState,
        status: "failed",
        updatedAt: new Date().toISOString(),
        error: { code: "build_unavailable", message: "The compiler could not complete this build after several attempts." },
      });
      message.ack();
      return;
    }
    await job.setState({
      ...baseState,
      status: "queued",
      updatedAt: new Date().toISOString(),
      error: { code: "retrying", message: "The compiler is retrying this build." },
    });
    console.error(JSON.stringify({ message: "firmware build will retry", buildId, error: String(error) }));
    message.retry({ delaySeconds: Math.min(300, 60 * message.attempts) });
  } finally {
    await container.destroy().catch(() => undefined);
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    const cors = corsHeaders(request, env);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors });

    let response: Response;
    try {
      if (request.method === "GET" && url.pathname === "/v1/catalog") {
        response = json({
          schemaVersion: 2,
          recipeSchemaVersion: 5,
          approvedEngineIds,
          chordTables: approvedChordTables,
          limits: { chordTables: 6, chordsPerTable: 24 },
          buildContract: env.PLAITS_BUILD_CONTRACT,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/builds") {
        response = await createBuild(request, env);
      } else {
        const match = url.pathname.match(/^\/v1\/builds\/([0-9a-f]+)(\/firmware)?$/);
        if (request.method === "GET" && match?.[2]) response = await downloadFirmware(match[1], env);
        else if (request.method === "GET" && match) response = await getBuild(match[1], env);
        else response = json({ error: { code: "not_found", message: "Route not found." } }, { status: 404 });
      }
    } catch (error) {
      console.error(JSON.stringify({
        message: "unhandled build-service request error",
        path: url.pathname,
        error: error instanceof Error ? error.message : String(error),
      }));
      response = json({ error: { code: "internal_error", message: "The firmware service could not complete this request." } }, { status: 500 });
    }
    cors.forEach((value, name) => response.headers.set(name, value));
    return response;
  },

  async queue(batch: MessageBatch<BuildMessage>, env: Env): Promise<void> {
    await Promise.all(batch.messages.map((message) => processBuild(message, env)));
  },
} satisfies ExportedHandler<Env, BuildMessage>;
