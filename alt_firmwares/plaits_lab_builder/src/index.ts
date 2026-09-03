import { Container, getContainer } from "@cloudflare/containers";
import { DurableObject } from "cloudflare:workers";
import {
  ContractError,
  approvedChordTables,
  approvedEngineIds,
  computeBuildKey,
  computeManualKey,
  isBuildKey,
  maxRecipeSchemaVersion,
  normalizeRecipe,
  type FirmwareOutput,
  type NormalizedRecipe,
} from "./contract";
import { deadLetterAction } from "./dead_letter";
import { corsHeaders } from "./cors";
import { buildQueueMessage, queuedRecipe, recipeArtifactKey, type BuildMessage } from "./build_queue";

type JobStatus = "queued" | "building" | "succeeded" | "failed";

type ManualState = {
  status: "pending" | "ready" | "unavailable";
  manualKey: string;
  sha256?: string;
};

type JobState = {
  buildId: string;
  status: JobStatus;
  createdAt: string;
  updatedAt: string;
  cacheHit: boolean;
  // Missing only on WAV jobs stored before selectable output formats shipped.
  output?: FirmwareOutput;
  artifact?: {
    bytes: number;
    wavSha256: string;
    hexSha256: string;
    binarySha256: string;
    textBytes: number;
    dataBytes: number;
    bssBytes: number;
    // The revision the COMPILER stamped, which is the only trustworthy record of
    // what actually built these bytes. It is not the Worker's
    // PLAITS_SOURCE_REVISION var: the two disagree for the length of a Container
    // rollout, and the build key is derived from the var, so during that window
    // an artifact is keyed as the new revision while the old image compiles it.
    // Empty for artifacts stored before this field existed.
    sourceRevision: string;
  };
  manual?: ManualState;
  error?: { code: string; message: string };
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

function outputDetails(output: FirmwareOutput): {
  extension: "wav" | "hex";
  contentType: "audio/wav" | "application/octet-stream";
} {
  return output === "intel-hex"
    ? { extension: "hex", contentType: "application/octet-stream" }
    : { extension: "wav", contentType: "audio/wav" };
}

function stateOutput(state: JobState): FirmwareOutput {
  return state.output ?? "audio-wav";
}

function artifactKey(buildId: string, output: FirmwareOutput): string {
  return `firmware/${buildId}.${outputDetails(output).extension}`;
}

// Manuals are keyed by DOCUMENTATION identity, not build identity — many
// builds (same layout, different options/chord tables/source revisions)
// share one immutable PDF.
function manualArtifactKey(manualKey: string): string {
  return `manuals/${manualKey}.pdf`;
}

async function storeQueuedRecipe(env: Env, buildId: string, recipe: NormalizedRecipe): Promise<void> {
  await env.ARTIFACTS.put(recipeArtifactKey(buildId), JSON.stringify(recipe), {
    httpMetadata: { contentType: "application/json" },
  });
}

async function loadQueuedRecipe(message: BuildMessage, env: Env): Promise<NormalizedRecipe> {
  // Drain jobs produced by the prior deployment without invalidating them.
  if (message.recipe !== undefined) return queuedRecipe(message.recipe);

  const stored = await env.ARTIFACTS.get(recipeArtifactKey(message.buildId));
  if (!stored) throw new Error("The queued firmware recipe is missing.");
  return queuedRecipe(await stored.json<unknown>());
}

// The name is the Durable Object identity, not just a label. Bump it when the
// compiler image's baked speech dependencies change so a long-lived encoder
// instance cannot keep serving the previous image after a container rollout.
// Rotated to v8 for the Piper voice expansion. A named Speech container
// attaches to whichever image was live when it started and keeps serving
// it after the pool advances, so a bumped catalog reaches only some
// requests until the identity changes: with v7 still attached to the old
// image, German encoded while Bulgarian returned "Choose a supported voice
// for the selected language" from the same deployment. Rotate only once the
// new image reports starting == 0.
// v8 was rotated in the same deploy that changed the image, so its first
// instance attached to the OLD image during the ~15 minute gradual rollout
// and kept serving it: production returned 400 for every Piper voice while
// reporting the new revision. Rotating AFTER the pool has fully advanced is
// what the README means by waiting for starting == 0 — the wait applies to
// the rotation, not just the deploy.
const SPEECH_ENCODER_CONTAINER = "speech-encoder-v21";

async function sha256Hex(bytes: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((value) => value.toString(16).padStart(2, "0")).join("");
}

function speechResponse(body: BodyInit | null, init: ResponseInit = {}): Response {
  const headers = new Headers(init.headers);
  headers.set("Cache-Control", "no-store");
  return new Response(body, { ...init, headers });
}

async function speechRateLimit(request: Request, env: Env): Promise<Response | null> {
  const clientIp = request.headers.get("CF-Connecting-IP") ?? "local-development";
  const { success } = await env.BUILD_RATE_LIMITER.limit({ key: `speech:${clientIp}` });
  return success ? null : json(
    { error: "Too many new Speech encodings were requested. Please wait a minute and try again." },
    { status: 429, headers: { "Retry-After": "60" } },
  );
}

/** What a container reports about itself, or why it could not be asked. */
type ContainerIdentity = {
  reachable: boolean;
  sourceRevision?: string;
  buildContract?: string;
  toolchain?: string;
  error?: string;
};

async function askContainer(
  env: Env,
  name: string,
  destroyAfter: boolean,
): Promise<ContainerIdentity> {
  const container = getContainer(env.FIRMWARE_BUILDER, name);
  try {
    const response = await container.fetch("http://container/ping");
    if (!response.ok) {
      return { reachable: false, error: `ping returned ${response.status}` };
    }
    const body = await response.json() as {
      sourceRevision?: string; buildContract?: string; toolchain?: string;
    };
    return {
      reachable: true,
      sourceRevision: body.sourceRevision,
      buildContract: body.buildContract,
      toolchain: body.toolchain,
    };
  } catch (error) {
    // With max_instances at 2 a probe can legitimately find no free slot. That
    // is INCONCLUSIVE, not a failure -- a deploy gate that treated it as a
    // mismatch would block on load rather than on staleness.
    return { reachable: false, error: error instanceof Error ? error.message : String(error) };
  } finally {
    if (destroyAfter) {
      try {
        await container.destroy();
      } catch {
        // Best effort: a probe container that outlives this request idles out
        // on its own, and failing to reap one must not fail the health check.
      }
    }
  }
}

type ContainerVoices = {
  reachable: boolean;
  sourceRevision?: string;
  languages?: Record<string, string[]>;
  error?: string;
};

async function askContainerVoices(env: Env): Promise<ContainerVoices> {
  const container = getContainer(env.FIRMWARE_BUILDER, SPEECH_ENCODER_CONTAINER);
  try {
    const response = await container.fetch("http://container/voices");
    if (!response.ok) {
      // An image predating this endpoint 404s. Say so plainly rather than
      // reporting an empty voice list, which a consumer could read as "this
      // builder speaks nothing" and act on.
      return { reachable: false, error: `voices returned ${response.status}` };
    }
    return { reachable: true, ...(await response.json() as Omit<ContainerVoices, "reachable">) };
  } catch (error) {
    return { reachable: false, error: error instanceof Error ? error.message : String(error) };
  }
}

async function proxySpeechJson(
  request: Request,
  env: Env,
  containerPath: string,
  options: { cacheNamespace?: string; maxBytes: number },
): Promise<Response> {
  const bytes = await request.arrayBuffer();
  if (bytes.byteLength === 0 || bytes.byteLength > options.maxBytes) {
    return json({ error: "The Speech request is too large." }, { status: 413 });
  }
  const cacheKey = options.cacheNamespace
    ? `speech/${options.cacheNamespace}/${await sha256Hex(bytes)}.json`
    : null;
  if (cacheKey) {
    const cached = await env.ARTIFACTS.get(cacheKey);
    if (cached) {
      return speechResponse(cached.body, {
        status: 200,
        headers: { "Content-Type": "application/json; charset=utf-8", "X-Speech-Cache": "hit" },
      });
    }
  }
  const limited = await speechRateLimit(request, env);
  if (limited) return limited;
  const container = getContainer(env.FIRMWARE_BUILDER, SPEECH_ENCODER_CONTAINER);
  const response = await container.fetch(`http://container${containerPath}`, {
    method: "POST",
    headers: { "Content-Type": request.headers.get("Content-Type") ?? "application/json" },
    body: bytes,
  });
  const result = await response.arrayBuffer();
  const contentType = response.headers.get("Content-Type") ?? "application/json; charset=utf-8";
  if (response.ok && cacheKey) {
    await env.ARTIFACTS.put(cacheKey, result, { httpMetadata: { contentType: "application/json" } });
  }
  return speechResponse(result, {
    status: response.status,
    headers: { "Content-Type": contentType, "X-Speech-Cache": response.ok ? "miss" : "error" },
  });
}

async function proxySpeechAudio(
  request: Request,
  env: Env,
  containerPath: string,
  cacheKey: string,
): Promise<Response> {
  const cached = await env.ARTIFACTS.get(cacheKey);
  if (cached) {
    return speechResponse(cached.body, {
      status: 200,
      headers: { "Content-Type": "audio/wav", "X-Speech-Cache": "hit" },
    });
  }
  const limited = await speechRateLimit(request, env);
  if (limited) return limited;
  const container = getContainer(env.FIRMWARE_BUILDER, SPEECH_ENCODER_CONTAINER);
  const response = await container.fetch(`http://container${containerPath}`);
  const result = await response.arrayBuffer();
  if (response.ok && response.headers.get("Content-Type") === "audio/wav") {
    await env.ARTIFACTS.put(cacheKey, result, { httpMetadata: { contentType: "audio/wav" } });
  }
  return speechResponse(result, {
    status: response.status,
    headers: { "Content-Type": response.headers.get("Content-Type") ?? "application/json", "X-Speech-Cache": response.ok ? "miss" : "error" },
  });
}

function artifactSummary(artifact: R2Object): NonNullable<JobState["artifact"]> {
  return {
    bytes: artifact.size,
    wavSha256: artifact.customMetadata?.wavSha256 ?? "",
    hexSha256: artifact.customMetadata?.hexSha256 ?? "",
    binarySha256: artifact.customMetadata?.binarySha256 ?? "",
    textBytes: Number(artifact.customMetadata?.textBytes ?? 0),
    dataBytes: Number(artifact.customMetadata?.dataBytes ?? 0),
    bssBytes: Number(artifact.customMetadata?.bssBytes ?? 0),
    sourceRevision: artifact.customMetadata?.sourceRevision ?? "",
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
    output: stateOutput(state),
    artifact: state.artifact,
    manual: state.manual
      ? {
          status: state.manual.status,
          downloadUrl: state.manual.status === "ready" ? `/v1/builds/${state.buildId}/manual` : undefined,
        }
      : undefined,
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
    const manualKey = await computeManualKey(recipe, env.PLAITS_MANUAL_CONTRACT);
    const job = env.BUILD_JOBS.getByName(buildId);
    const [existingArtifact, existingManual, previousState] = await Promise.all([
      env.ARTIFACTS.head(artifactKey(buildId, recipe.output)),
      env.ARTIFACTS.head(manualArtifactKey(manualKey)),
      job.getState(),
    ]);
    const now = new Date().toISOString();

    if (existingArtifact) {
      let manual: ManualState;
      if (existingManual) {
        manual = { status: "ready", manualKey, sha256: existingManual.customMetadata?.manualSha256 };
      } else if (previousState?.manual?.status === "pending" && previousState.manual.manualKey === manualKey) {
        // A backfill for this exact manual is already queued.
        manual = previousState.manual;
      } else {
        // Cached firmware predating the field guide: backfill only the PDF.
        manual = { status: "pending", manualKey };
        try {
          await storeQueuedRecipe(env, buildId, recipe);
          await env.BUILD_QUEUE.send(buildQueueMessage(buildId, true), { contentType: "json" });
        } catch (error) {
          console.error(JSON.stringify({ message: "manual backfill queue send failed", buildId, error: String(error) }));
          manual = { status: "unavailable", manualKey };
        }
      }
      const state: JobState = {
        buildId,
        status: "succeeded",
        createdAt: previousState?.createdAt ?? now,
        updatedAt: now,
        cacheHit: true,
        output: recipe.output,
        artifact: artifactSummary(existingArtifact),
        manual,
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
      output: recipe.output,
      manual: { status: "pending", manualKey },
    };
    await job.setState(state);
    try {
      // Custom FM banks can make a valid recipe hundreds of kilobytes large.
      // Persist it before publishing the small queue reference so the consumer
      // can never observe a message whose recipe is not available yet.
      await storeQueuedRecipe(env, buildId, recipe);
      await env.BUILD_QUEUE.send(buildQueueMessage(buildId), { contentType: "json" });
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
  const output = stateOutput(state);
  const details = outputDetails(output);
  const artifact = await env.ARTIFACTS.get(artifactKey(state.buildId, output));
  if (!artifact) {
    return json({ error: { code: "artifact_not_found", message: "That firmware artifact is not available." } }, { status: 404 });
  }
  const headers = new Headers();
  artifact.writeHttpMetadata(headers);
  headers.set("ETag", artifact.httpEtag);
  headers.set("Content-Length", String(artifact.size));
  headers.set("Content-Disposition", `attachment; filename="rubato-plaits-firmware.${details.extension}"`);
  headers.set("Cache-Control", "public, max-age=3600, immutable");
  return new Response(artifact.body, { headers });
}

async function downloadManual(buildId: string, env: Env): Promise<Response> {
  if (!isBuildKey(buildId)) {
    return json({ error: { code: "invalid_build_key", message: "The build key is invalid." } }, { status: 400 });
  }
  const state = await getStoredBuild(buildId, env);
  if (!state || state.status !== "succeeded" || state.manual?.status !== "ready") {
    return json({ error: { code: "manual_not_found", message: "That field guide is not available." } }, { status: 404 });
  }
  const manual = await env.ARTIFACTS.get(manualArtifactKey(state.manual.manualKey));
  if (!manual) {
    return json({ error: { code: "manual_not_found", message: "That field guide is not available." } }, { status: 404 });
  }
  const headers = new Headers();
  manual.writeHttpMetadata(headers);
  headers.set("ETag", manual.httpEtag);
  headers.set("Content-Length", String(manual.size));
  headers.set("Content-Disposition", 'attachment; filename="plaits-palette-field-guide.pdf"');
  headers.set("Cache-Control", "public, max-age=3600, immutable");
  return new Response(manual.body, { headers });
}

async function renderManualInContainer(
  container: ReturnType<typeof getContainer<FirmwareBuilder>>,
  manualKey: string,
  recipe: NormalizedRecipe,
  manualContract: string,
): Promise<{ bytes: ArrayBuffer; sha256: string }> {
  // The contract travels with the request because it is ours, not the image's:
  // it seeds the manual key here, and the container only echoes it back on
  // X-Plaits-Manual-Contract so that header describes the render that happened.
  const response = await container.fetch("http://container/manual", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ manualKey, recipe, manualContract }),
  });
  if (!response.ok || response.headers.get("Content-Type") !== "application/pdf") {
    const failure = await response.text().catch(() => "");
    throw new Error(`Manual renderer failed (${response.status}): ${failure.slice(0, 400)}`);
  }
  const bytes = await response.arrayBuffer();
  if (bytes.byteLength === 0 || bytes.byteLength > 8 * 1024 * 1024) {
    throw new Error("Manual renderer returned an invalid PDF size");
  }
  return { bytes, sha256: response.headers.get("X-Plaits-Manual-Sha256") ?? "" };
}

// Render + store the field guide, reusing a cached PDF when the manual key
// already exists. Never throws into the firmware path: the caller decides
// whether a failure retries (manual-only messages) or degrades (post-build).
async function completeManual(
  env: Env,
  container: ReturnType<typeof getContainer<FirmwareBuilder>>,
  manualKey: string,
  recipe: NormalizedRecipe,
): Promise<ManualState> {
  const existing = await env.ARTIFACTS.head(manualArtifactKey(manualKey));
  if (existing) {
    return { status: "ready", manualKey, sha256: existing.customMetadata?.manualSha256 };
  }
  const { bytes, sha256 } = await renderManualInContainer(container, manualKey, recipe, env.PLAITS_MANUAL_CONTRACT);
  await env.ARTIFACTS.put(manualArtifactKey(manualKey), bytes, {
    httpMetadata: { contentType: "application/pdf" },
    customMetadata: { manualSha256: sha256, manualContract: env.PLAITS_MANUAL_CONTRACT },
  });
  return { status: "ready", manualKey, sha256 };
}

async function processBuild(message: Message<BuildMessage>, env: Env): Promise<void> {
  const { buildId } = message.body;
  const job = env.BUILD_JOBS.getByName(buildId);
  const prior = await job.getState();
  const now = new Date().toISOString();
  let recipe: NormalizedRecipe;
  try {
    recipe = await loadQueuedRecipe(message.body, env);
  } catch (error) {
    console.error(JSON.stringify({ message: "queued firmware recipe unavailable", buildId, error: String(error) }));
    if (message.body.manualOnly && prior?.status === "succeeded" && prior.manual) {
      await job.setState({
        ...prior,
        updatedAt: now,
        manual: { ...prior.manual, status: "unavailable" },
      });
    } else {
      await job.setState({
        buildId,
        status: "failed",
        createdAt: prior?.createdAt ?? now,
        updatedAt: now,
        cacheHit: prior?.cacheHit ?? false,
        output: prior?.output,
        error: { code: "recipe_unavailable", message: "The queued firmware recipe is no longer available. Please submit it again." },
      });
    }
    message.ack();
    return;
  }
  const baseState = {
    buildId,
    createdAt: prior?.createdAt ?? now,
    cacheHit: false,
    output: recipe.output,
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
  const manualKey = await computeManualKey(recipe, env.PLAITS_MANUAL_CONTRACT);
  const existingArtifact = await env.ARTIFACTS.head(artifactKey(buildId, recipe.output));

  if (message.body.manualOnly && !existingArtifact) {
    // A manual backfill for firmware that no longer exists documents nothing.
    await job.setState({
      ...(prior ?? { ...baseState, status: "failed" as const, error: { code: "build_not_found", message: "That firmware build does not exist." } }),
      manual: { status: "unavailable", manualKey },
      updatedAt: new Date().toISOString(),
    });
    message.ack();
    return;
  }

  if (existingArtifact) {
    // Firmware is cached — only the field guide may be missing.
    const container = getContainer(env.FIRMWARE_BUILDER, buildId);
    const succeeded: JobState = {
      ...baseState,
      status: "succeeded",
      updatedAt: new Date().toISOString(),
      cacheHit: prior?.cacheHit ?? true,
      artifact: artifactSummary(existingArtifact),
    };
    try {
      const manual = await completeManual(env, container, manualKey, recipe);
      await job.setState({ ...succeeded, manual });
      message.ack();
    } catch (error) {
      console.error(JSON.stringify({ message: "manual render will retry", buildId, manualKey, error: String(error) }));
      if (message.attempts >= 5) {
        await job.setState({ ...succeeded, manual: { status: "unavailable", manualKey } });
        message.ack();
      } else {
        await job.setState({ ...succeeded, manual: { status: "pending", manualKey } });
        message.retry({ delaySeconds: Math.min(300, 60 * message.attempts) });
      }
    } finally {
      await container.destroy().catch(() => undefined);
    }
    return;
  }

  await job.setState({
    ...baseState,
    status: "building",
    updatedAt: now,
    manual: { status: "pending", manualKey },
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
    const details = outputDetails(recipe.output);
    if (!response.body || response.headers.get("Content-Type") !== details.contentType) {
      throw new Error("Compiler returned an invalid artifact");
    }

    const artifactBytes = await response.arrayBuffer();
    if (artifactBytes.byteLength === 0 || artifactBytes.byteLength > 32 * 1024 * 1024) {
      throw new Error("Compiler returned an invalid artifact size");
    }
    const metadata = {
      wavSha256: response.headers.get("X-Plaits-Wav-Sha256") ?? "",
      hexSha256: response.headers.get("X-Plaits-Hex-Sha256") ?? "",
      binarySha256: response.headers.get("X-Plaits-Binary-Sha256") ?? "",
      textBytes: response.headers.get("X-Plaits-Text-Bytes") ?? "0",
      dataBytes: response.headers.get("X-Plaits-Data-Bytes") ?? "0",
      bssBytes: response.headers.get("X-Plaits-Bss-Bytes") ?? "0",
      sourceRevision: response.headers.get("X-Plaits-Source-Revision") ?? env.PLAITS_SOURCE_REVISION,
      toolchain: response.headers.get("X-Plaits-Toolchain") ?? env.PLAITS_TOOLCHAIN_ID,
      buildContract: response.headers.get("X-Plaits-Build-Contract") ?? env.PLAITS_BUILD_CONTRACT,
    };
    console.log(JSON.stringify({ message: "compiler artifact buffered", buildId, bytes: artifactBytes.byteLength }));
    const artifact = await env.ARTIFACTS.put(artifactKey(buildId, recipe.output), artifactBytes, {
      httpMetadata: { contentType: details.contentType },
      customMetadata: metadata,
    });
    await env.ARTIFACTS.put(
      `manifests/${buildId}.json`,
      JSON.stringify({ buildKey: buildId, recipe, metadata }, null, 2) + "\n",
      { httpMetadata: { contentType: "application/json" } },
    );
    // The manual is a bonus artifact: its failure never fails the firmware
    // build. A pending manual retries this message, which then takes the
    // cached-firmware manual-only path above.
    let manual: ManualState;
    try {
      manual = await completeManual(env, container, manualKey, recipe);
    } catch (manualError) {
      console.error(JSON.stringify({ message: "manual render failed after build", buildId, manualKey, error: String(manualError) }));
      manual = { status: message.attempts >= 5 ? "unavailable" : "pending", manualKey };
    }
    await job.setState({
      ...baseState,
      status: "succeeded",
      updatedAt: new Date().toISOString(),
      artifact: {
        bytes: artifact.size,
        wavSha256: metadata.wavSha256,
        hexSha256: metadata.hexSha256,
        binarySha256: metadata.binarySha256,
        textBytes: Number(metadata.textBytes),
        dataBytes: Number(metadata.dataBytes),
        bssBytes: Number(metadata.bssBytes),
        sourceRevision: metadata.sourceRevision,
      },
      manual,
    });
    if (manual.status === "pending") {
      message.retry({ delaySeconds: 60 });
      return;
    }
    message.ack();
  } catch (error) {
    if (message.attempts >= 5) {
      console.error(JSON.stringify({
        message: "firmware build retries exhausted",
        deploymentEnvironment: env.DEPLOYMENT_ENVIRONMENT,
        buildId,
        errorCode: "compiler_retry_exhausted",
        error: String(error),
      }));
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
      manual: { status: "pending", manualKey },
      error: { code: "retrying", message: "The compiler is retrying this build." },
    });
    console.error(JSON.stringify({
      message: "firmware build will retry",
      deploymentEnvironment: env.DEPLOYMENT_ENVIRONMENT,
      buildId,
      errorCode: "compiler_retry",
      error: String(error),
    }));
    message.retry({ delaySeconds: Math.min(300, 60 * message.attempts) });
  } finally {
    await container.destroy().catch(() => undefined);
  }
}

// Drains the dead-letter queue. Without a consumer these messages expire
// unseen inside the retention window, which is the one failure mode nobody
// would otherwise hear about: the compiler's own failures ack themselves with
// a recorded cause, so arriving here means the delivery broke.
async function recordDeadLetter(message: Message<BuildMessage>, env: Env): Promise<void> {
  const { buildId } = message.body;
  console.error(JSON.stringify({
    message: "build message dead-lettered",
    buildId,
    manualOnly: message.body.manualOnly ?? false,
    attempts: message.attempts,
  }));
  try {
    const job = env.BUILD_JOBS.getByName(buildId);
    const prior = await job.getState();
    const action = deadLetterAction(prior);
    const now = new Date().toISOString();
    if (action.kind === "fail") {
      await job.setState({
        buildId,
        status: "failed",
        createdAt: prior?.createdAt ?? now,
        updatedAt: now,
        cacheHit: prior?.cacheHit ?? false,
        error: { code: "build_unavailable", message: "The compiler could not complete this build after several attempts." },
      });
    } else if (action.kind === "manual-unavailable" && prior?.manual) {
      await job.setState({ ...prior, manual: { ...prior.manual, status: "unavailable" }, updatedAt: now });
    }
  } catch (error) {
    // The job state is a courtesy to a polling client; the log above is the
    // signal that matters, so never fail the batch over it.
    console.error(JSON.stringify({ message: "dead-letter state write failed", buildId, error: String(error) }));
  }
  message.ack();
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    const cors = corsHeaders(request, env.PUBLIC_ORIGINS);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors });

    let response: Response;
    try {
      if (request.method === "GET" && url.pathname === "/v1/voices") {
        // What the DEPLOYED image can actually speak. Engines get this for
        // free through approvedEngineIds, which is why an unsupported engine
        // shows "needs a builder update" while an unsupported VOICE used to
        // fail only once a user picked it. Served from the container rather
        // than a mirrored list so it cannot drift from the image answering
        // requests.
        const voices = await askContainerVoices(env);
        response = json(voices, { status: voices.reachable ? 200 : 503 });
      } else if (request.method === "GET" && url.pathname === "/v1/health") {
        // Answers "has the container rollout actually landed?" in ~2 s, which
        // previously required a queued multi-minute firmware compile and was
        // therefore never run during a rollout.
        //
        // Two probes, because two different things go stale and they need
        // different remedies. A FRESH uniquely-named Durable Object has no
        // warm container, so it starts on whatever image the application is
        // configured with -- that reports whether the POOL has rolled. The
        // speech singleton is a FIXED name kept warm by ordinary traffic, so it
        // can keep serving a previous image long after the pool moved -- that
        // reports whether the endpoint you actually hit is current.
        //
        // pool stale            -> wait, the rollout is still going
        // pool current, singleton stale -> rotate SPEECH_ENCODER_CONTAINER
        const worker = env.PLAITS_SOURCE_REVISION;
        const [pool, singleton] = await Promise.all([
          askContainer(env, `health-probe-${worker}-${Date.now()}`, true),
          askContainer(env, SPEECH_ENCODER_CONTAINER, false),
        ]);
        const agrees = (identity: ContainerIdentity) =>
          identity.reachable && identity.sourceRevision === worker;
        response = json({
          worker,
          deploymentEnvironment: env.DEPLOYMENT_ENVIRONMENT,
          pool,
          speechEncoder: { name: SPEECH_ENCODER_CONTAINER, ...singleton },
          poolMatches: agrees(pool),
          speechEncoderMatches: agrees(singleton),
          match: agrees(pool) && agrees(singleton),
        });
      } else if (request.method === "GET" && url.pathname === "/v1/catalog") {
        response = json({
          schemaVersion: 2,
          recipeSchemaVersion: maxRecipeSchemaVersion,
          deploymentEnvironment: env.DEPLOYMENT_ENVIRONMENT,
          sourceRevision: env.PLAITS_SOURCE_REVISION,
          approvedEngineIds,
          chordTables: approvedChordTables,
          // userDataBanks: v12 keys banks per slot, so the ceiling is the slot
          // count (32); the flash budget is the real limit the ARM build enforces.
          limits: {
            chordTables: 16,
            chordsPerTable: 24,
            scales: 16,
            degreesPerScale: 7,
            userDataBanks: 32,
          },
          buildContract: env.PLAITS_BUILD_CONTRACT,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/segment") {
        response = await proxySpeechJson(request, env, "/speech/segment", {
          cacheNamespace: "segments-v1",
          maxBytes: 64 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/encode") {
        response = await proxySpeechJson(request, env, "/speech/encode", {
          cacheNamespace: "banks-v3",
          maxBytes: 64 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/encode-natural") {
        // Its own cache namespace: the request bodies are shaped alike but the
        // encoders are different, so sharing one would let an LPC bank answer a
        // Natural Speech request.
        response = await proxySpeechJson(request, env, "/speech/encode-natural", {
          cacheNamespace: "natural-banks-v1",
          maxBytes: 64 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/render-bank-natural") {
        response = await proxySpeechJson(request, env, "/speech/render-bank-natural", {
          // Saved-bank WAVs are renderer artifacts. Include the immutable image
          // revision here as well as in the container's own job key, otherwise
          // R2 can answer with an obsolete rendering before the request ever
          // reaches the updated container.
          cacheNamespace: `rendered-natural-banks-v2-${env.PLAITS_SOURCE_REVISION}`,
          maxBytes: 64 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/render-bank") {
        response = await proxySpeechJson(request, env, "/speech/render-bank", {
          cacheNamespace: "rendered-banks-v2",
          maxBytes: 64 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/encode-recording-natural") {
        response = await proxySpeechJson(request, env, "/speech/encode-recording-natural", {
          maxBytes: 4 * 1024 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/speech/encode-recording") {
        response = await proxySpeechJson(request, env, "/speech/encode-recording", {
          maxBytes: 4 * 1024 * 1024,
        });
      } else if (request.method === "POST" && url.pathname === "/v1/builds") {
        response = await createBuild(request, env);
      } else {
        const voiceMatch = url.pathname.match(/^\/v1\/speech\/voice-preview\/([^/]+)\/([^/]+)\.wav$/);
        const stockMatch = url.pathname.match(/^\/v1\/speech\/stock\/bank-([1-5])-(natural|flat)\.wav$/);
        const match = url.pathname.match(/^\/v1\/builds\/([0-9a-f]+)(\/firmware|\/manual)?$/);
        if (request.method === "GET" && voiceMatch) {
          response = await proxySpeechAudio(
            request,
            env,
            `/speech/voice-preview/${voiceMatch[1]}/${voiceMatch[2]}.wav`,
            `speech/voices-v1/${voiceMatch[1]}/${voiceMatch[2]}.wav`,
          );
        } else if (request.method === "GET" && stockMatch) {
          response = await proxySpeechAudio(
            request,
            env,
            `/speech/stock/bank-${stockMatch[1]}-${stockMatch[2]}.wav`,
            `speech/stock-v1/bank-${stockMatch[1]}-${stockMatch[2]}.wav`,
          );
        } else if (request.method === "GET" && match?.[2] === "/firmware") response = await downloadFirmware(match[1], env);
        else if (request.method === "GET" && match?.[2] === "/manual") response = await downloadManual(match[1], env);
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
    if (!response.ok) {
      let errorCode = "unknown_error";
      try {
        const payload = await response.clone().json() as { error?: string | { code?: string } };
        errorCode = typeof payload.error === "object" && payload.error?.code
          ? payload.error.code
          : typeof payload.error === "string" ? "speech_request_failed" : errorCode;
      } catch { /* A non-JSON failure still gets a status/path event. */ }
      console.warn(JSON.stringify({
        message: "build-service request failed",
        environment: env.DEPLOYMENT_ENVIRONMENT,
        method: request.method,
        path: url.pathname,
        status: response.status,
        errorCode,
        ray: request.headers.get("CF-Ray"),
      }));
    }
    cors.forEach((value, name) => response.headers.set(name, value));
    return response;
  },

  async queue(batch: MessageBatch<BuildMessage>, env: Env): Promise<void> {
    if (batch.queue === env.DEAD_LETTER_QUEUE) {
      await Promise.all(batch.messages.map((message) => recordDeadLetter(message, env)));
      return;
    }
    await Promise.all(batch.messages.map((message) => processBuild(message, env)));
  },
} satisfies ExportedHandler<Env, BuildMessage>;
