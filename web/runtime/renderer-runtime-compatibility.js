const EVIDENCE_SCHEMA =
  "crankwave/renderer-compatibility-parity-report-v1";
const EVIDENCE_VERDICT =
  "all-old-current-pcm-and-normalized-telemetry-byte-identical";

// Compatibility is review-owned code, not package-self-attested metadata. Each
// edge pins one committed evidence file and remains invalid for every future
// renderer source closure.
const VERIFIED_EDGES = Object.freeze([
  Object.freeze({
    captureSourceClosureSha256:
      "5287982ab1fe4846fc6d5008f0415f1901adae0fe4ca06a02c48addcab39b6fb",
    admittedSourceClosureSha256:
      "c3a905712ddf3ae4e506af64e3919f2324df46af13ff4e3dc1a24037b4079d23",
    evidenceByteCount: 18_309,
    evidenceSha256:
      "ee4eed2e02c2177ac487a15d3ecb0c0aed7cf226f5cf6544acc9ddb8f2a9de0a",
    captureWasmEvidenceSha256:
      "d5c0016086cd3d67eb9daf4fd80c90cfd2eacba2cbd36d5da7c68f68c909c033",
    admittedWasmEvidenceSha256:
      "0596bf2535c11368c087474aedcf24da8cfeca67bc266b5729046796b5882963",
    moduleLoaderEvidenceSha256:
      "769c7942e447acf79e155470d84e94c5e1dd2ad896ce3691b81e0fd5a93d86e6",
    caseCount: 21,
    processedBlockCount: 14_315,
    audioFrameCount: 54_942_720,
  }),
]);

function fail(message) {
  throw new TypeError(message);
}

function object(value, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  return value;
}

function array(value, label) {
  if (!Array.isArray(value)) fail(`${label} must be an array`);
  return value;
}

function string(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    fail(`${label} must be a nonempty string`);
  }
  return value;
}

function sha256(value, label) {
  const result = string(value, label);
  if (!/^[0-9a-f]{64}$/.test(result) || /^0{64}$/.test(result)) {
    fail(`${label} must be a nonzero lowercase SHA-256 digest`);
  }
  return result;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function nonnegativeInteger(value, label) {
  if (!Number.isSafeInteger(value) || value < 0) {
    fail(`${label} must be a nonnegative integer`);
  }
  return value;
}

function exactKeys(value, keys, label) {
  const expected = new Set(keys);
  for (const key of Object.keys(value)) {
    if (!expected.has(key)) fail(`${label} has unknown member ${key}`);
  }
  for (const key of expected) {
    if (!Object.hasOwn(value, key)) fail(`${label} is missing member ${key}`);
  }
}

function relativeUrl(manifestUrl, value, label) {
  const path = string(value, label);
  if (
    path.startsWith("/") ||
    path.includes("..") ||
    path.includes("%") ||
    path.includes("\\") ||
    path.includes("?") ||
    path.includes("#")
  ) {
    fail(`${label} must stay beneath its manifest`);
  }
  const manifest = new URL(manifestUrl);
  const base = new URL(".", manifest);
  const resolved = new URL(path, base);
  if (
    !["http:", "https:"].includes(manifest.protocol) ||
    resolved.protocol !== manifest.protocol ||
    resolved.origin !== manifest.origin ||
    resolved.username !== "" ||
    resolved.password !== "" ||
    resolved.search !== "" ||
    resolved.hash !== "" ||
    !resolved.pathname.startsWith(base.pathname)
  ) {
    fail(`${label} must stay beneath its manifest`);
  }
  return resolved;
}

async function fetchBytes(url, fetchImplementation) {
  const response = await fetchImplementation(url.href, {
    cache: "no-store",
    credentials: "same-origin",
    redirect: "error",
  });
  if (
    !response?.ok ||
    response.redirected === true ||
    (typeof response.url === "string" &&
      response.url.length !== 0 &&
      response.url !== url.href) ||
    typeof response.arrayBuffer !== "function"
  ) {
    throw new Error(
      `renderer compatibility evidence fetch failed: HTTP ${
        response?.status ?? "?"
      } for ${url.href}`,
    );
  }
  return new Uint8Array(await response.arrayBuffer());
}

function hex(bytes) {
  return Array.from(
    bytes,
    (value) => value.toString(16).padStart(2, "0"),
  ).join("");
}

async function requireSha256(bytes, expected, cryptoImplementation) {
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error(
      "Web Crypto SHA-256 is required for renderer compatibility evidence",
    );
  }
  const digest = hex(
    new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", bytes)),
  );
  if (digest !== expected) {
    throw new Error(
      "renderer compatibility evidence SHA-256 does not match its descriptor",
    );
  }
}

function findVerifiedEdge(captureSourceClosureSha256, admittedSourceClosureSha256) {
  return VERIFIED_EDGES.find(
    (edge) =>
      edge.captureSourceClosureSha256 === captureSourceClosureSha256 &&
      edge.admittedSourceClosureSha256 === admittedSourceClosureSha256,
  ) ?? null;
}

function validateFailure(value, label) {
  if (value === null) return null;
  const failure = object(value, label);
  exactKeys(
    failure,
    [
      "code",
      "detailCode",
      "diagnostics",
      "message",
      "name",
      "operation",
      "stage",
      "stageName",
      "status",
      "statusName",
    ],
    label,
  );
  nonnegativeInteger(failure.code, `${label}.code`);
  string(failure.detailCode, `${label}.detailCode`);
  array(failure.diagnostics, `${label}.diagnostics`);
  string(failure.message, `${label}.message`);
  string(failure.name, `${label}.name`);
  string(failure.operation, `${label}.operation`);
  nonnegativeInteger(failure.stage, `${label}.stage`);
  string(failure.stageName, `${label}.stageName`);
  nonnegativeInteger(failure.status, `${label}.status`);
  string(failure.statusName, `${label}.statusName`);
  return Object.freeze(failure);
}

function validateCase(value, index, controlProfiles) {
  const label = `renderer compatibility evidence.cases[${index}]`;
  const entry = object(value, label);
  exactKeys(
    entry,
    [
      "audio_frames",
      "byte_identical",
      "control_profile",
      "engine",
      "engine_provenance_sha256",
      "execution_kind",
      "id",
      "matched_process_failure",
      "pcm_sha256",
      "processed_blocks",
      "reached_finite_completion",
      "scenario",
      "telemetry_sha256",
      "transcript_sha256",
    ],
    label,
  );
  const controlProfile = string(
    entry.control_profile,
    `${label}.control_profile`,
  );
  if (!Object.hasOwn(controlProfiles, controlProfile)) {
    fail(`${label}.control_profile is not declared`);
  }
  if (
    entry.byte_identical !== true ||
    typeof entry.reached_finite_completion !== "boolean"
  ) {
    fail(`${label} does not record byte-identical deterministic output`);
  }
  return Object.freeze({
    id: string(entry.id, `${label}.id`),
    engine: string(entry.engine, `${label}.engine`),
    engineProvenanceSha256: sha256(
      entry.engine_provenance_sha256,
      `${label}.engine_provenance_sha256`,
    ),
    scenario: string(entry.scenario, `${label}.scenario`),
    executionKind: string(entry.execution_kind, `${label}.execution_kind`),
    controlProfile,
    processedBlocks: positiveInteger(
      entry.processed_blocks,
      `${label}.processed_blocks`,
    ),
    audioFrames: positiveInteger(entry.audio_frames, `${label}.audio_frames`),
    pcmSha256: sha256(entry.pcm_sha256, `${label}.pcm_sha256`),
    telemetrySha256: sha256(
      entry.telemetry_sha256,
      `${label}.telemetry_sha256`,
    ),
    transcriptSha256: sha256(
      entry.transcript_sha256,
      `${label}.transcript_sha256`,
    ),
    reachedFiniteCompletion: entry.reached_finite_completion,
    matchedProcessFailure: validateFailure(
      entry.matched_process_failure,
      `${label}.matched_process_failure`,
    ),
  });
}

export async function loadRendererRuntimeCompatibility(
  descriptorValue,
  capturedRendererSourceClosureSha256,
  manifestUrl,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (descriptorValue === undefined) return null;
  if (typeof fetchImplementation !== "function") {
    fail("renderer compatibility loading requires fetch");
  }
  const descriptor = object(
    descriptorValue,
    "runtime_renderer_compatibility",
  );
  exactKeys(
    descriptor,
    [
      "capture_source_closure_sha256",
      "admitted_source_closure_sha256",
      "evidence_path",
      "evidence_byte_count",
      "evidence_sha256",
    ],
    "runtime_renderer_compatibility",
  );
  const captureSourceClosureSha256 = sha256(
    descriptor.capture_source_closure_sha256,
    "runtime_renderer_compatibility.capture_source_closure_sha256",
  );
  const admittedSourceClosureSha256 = sha256(
    descriptor.admitted_source_closure_sha256,
    "runtime_renderer_compatibility.admitted_source_closure_sha256",
  );
  if (
    captureSourceClosureSha256 !==
    sha256(
      capturedRendererSourceClosureSha256,
      "captured renderer source closure",
    )
  ) {
    fail("renderer compatibility capture identity must match package provenance");
  }
  if (captureSourceClosureSha256 === admittedSourceClosureSha256) {
    fail("renderer compatibility must describe two distinct source closures");
  }
  const evidenceByteCount = positiveInteger(
    descriptor.evidence_byte_count,
    "runtime_renderer_compatibility.evidence_byte_count",
  );
  const evidenceSha256 = sha256(
    descriptor.evidence_sha256,
    "runtime_renderer_compatibility.evidence_sha256",
  );
  const edge = findVerifiedEdge(
    captureSourceClosureSha256,
    admittedSourceClosureSha256,
  );
  if (
    edge === null ||
    evidenceByteCount !== edge.evidenceByteCount ||
    evidenceSha256 !== edge.evidenceSha256
  ) {
    fail("renderer compatibility edge is not present in the reviewed registry");
  }

  const evidenceUrl = relativeUrl(
    manifestUrl,
    descriptor.evidence_path,
    "runtime_renderer_compatibility.evidence_path",
  );
  const evidenceBytes = await fetchBytes(evidenceUrl, fetchImplementation);
  if (evidenceBytes.byteLength !== edge.evidenceByteCount) {
    throw new RangeError(
      "renderer compatibility evidence byte count does not match its registry",
    );
  }
  await requireSha256(evidenceBytes, edge.evidenceSha256, cryptoImplementation);

  const evidence = JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(evidenceBytes),
  );
  object(evidence, "renderer compatibility evidence");
  exactKeys(
    evidence,
    [
      "aggregate",
      "cases",
      "control_profiles",
      "renderer_pair",
      "schema",
      "verdict",
    ],
    "renderer compatibility evidence",
  );
  if (
    evidence.schema !== EVIDENCE_SCHEMA ||
    evidence.verdict !== EVIDENCE_VERDICT
  ) {
    fail("renderer compatibility evidence has an unsupported contract or verdict");
  }

  const rendererPair = object(
    evidence.renderer_pair,
    "renderer compatibility evidence.renderer_pair",
  );
  exactKeys(
    rendererPair,
    [
      "capture_renderer_source_closure_sha256",
      "capture_wasm_sha256",
      "loader_sha256",
      "running_renderer_source_closure_sha256",
      "running_wasm_sha256",
    ],
    "renderer compatibility evidence.renderer_pair",
  );
  if (
    sha256(
      rendererPair.capture_renderer_source_closure_sha256,
      "renderer compatibility evidence capture closure",
    ) !== edge.captureSourceClosureSha256 ||
    sha256(
      rendererPair.running_renderer_source_closure_sha256,
      "renderer compatibility evidence running closure",
    ) !== edge.admittedSourceClosureSha256 ||
    sha256(
      rendererPair.capture_wasm_sha256,
      "renderer compatibility evidence capture WASM",
    ) !== edge.captureWasmEvidenceSha256 ||
    sha256(
      rendererPair.running_wasm_sha256,
      "renderer compatibility evidence running WASM",
    ) !== edge.admittedWasmEvidenceSha256 ||
    sha256(
      rendererPair.loader_sha256,
      "renderer compatibility evidence module loader",
    ) !== edge.moduleLoaderEvidenceSha256
  ) {
    fail("renderer compatibility evidence does not match its reviewed edge");
  }

  const controlProfiles = object(
    evidence.control_profiles,
    "renderer compatibility evidence.control_profiles",
  );
  if (Object.keys(controlProfiles).length === 0) {
    fail("renderer compatibility evidence must declare control profiles");
  }
  for (const [id, description] of Object.entries(controlProfiles)) {
    string(id, "renderer compatibility evidence control profile ID");
    string(
      description,
      `renderer compatibility evidence.control_profiles.${id}`,
    );
  }

  const cases = array(evidence.cases, "renderer compatibility evidence.cases")
    .map((value, index) => validateCase(value, index, controlProfiles));
  const aggregate = object(
    evidence.aggregate,
    "renderer compatibility evidence.aggregate",
  );
  exactKeys(
    aggregate,
    [
      "all_cases_byte_identical",
      "audio_frames",
      "case_count",
      "engine_count",
      "finite_case_count",
      "finite_cases_completed_within_bound",
      "finite_cases_with_matched_process_failure",
      "open_ended_case_count",
      "processed_blocks",
      "scenario_count",
    ],
    "renderer compatibility evidence.aggregate",
  );
  if (aggregate.all_cases_byte_identical !== true) {
    fail("renderer compatibility evidence does not prove byte identity");
  }
  const caseCount = positiveInteger(
    aggregate.case_count,
    "renderer compatibility evidence.aggregate.case_count",
  );
  const processedBlockCount = positiveInteger(
    aggregate.processed_blocks,
    "renderer compatibility evidence.aggregate.processed_blocks",
  );
  const audioFrameCount = positiveInteger(
    aggregate.audio_frames,
    "renderer compatibility evidence.aggregate.audio_frames",
  );
  const engineCount = positiveInteger(
    aggregate.engine_count,
    "renderer compatibility evidence.aggregate.engine_count",
  );
  const scenarioCount = positiveInteger(
    aggregate.scenario_count,
    "renderer compatibility evidence.aggregate.scenario_count",
  );
  const finiteCaseCount = nonnegativeInteger(
    aggregate.finite_case_count,
    "renderer compatibility evidence.aggregate.finite_case_count",
  );
  const openEndedCaseCount = nonnegativeInteger(
    aggregate.open_ended_case_count,
    "renderer compatibility evidence.aggregate.open_ended_case_count",
  );
  const finiteCompletedCount = nonnegativeInteger(
    aggregate.finite_cases_completed_within_bound,
    "renderer compatibility evidence.aggregate.finite_cases_completed_within_bound",
  );
  const finiteFailureCount = nonnegativeInteger(
    aggregate.finite_cases_with_matched_process_failure,
    "renderer compatibility evidence.aggregate.finite_cases_with_matched_process_failure",
  );
  if (
    caseCount !== edge.caseCount ||
    processedBlockCount !== edge.processedBlockCount ||
    audioFrameCount !== edge.audioFrameCount ||
    cases.length !== caseCount ||
    new Set(cases.map(({ id }) => id)).size !== cases.length ||
    new Set(cases.map(({ engine }) => engine)).size !== engineCount ||
    new Set(cases.map(({ scenario }) => scenario)).size !== scenarioCount ||
    finiteCaseCount + openEndedCaseCount !== caseCount ||
    finiteCompletedCount + finiteFailureCount !== finiteCaseCount ||
    cases.reduce((sum, entry) => sum + entry.processedBlocks, 0) !==
      processedBlockCount ||
    cases.reduce((sum, entry) => sum + entry.audioFrames, 0) !== audioFrameCount
  ) {
    fail("renderer compatibility evidence aggregate does not match its cases");
  }

  return Object.freeze({
    captureSourceClosureSha256,
    admittedSourceClosureSha256,
    evidenceUrl: evidenceUrl.href,
    evidenceSha256,
    caseCount,
    processedBlockCount,
    audioFrameCount,
  });
}
