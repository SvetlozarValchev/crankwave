export const AUDIO_ATLAS_SCHEMA = "engine-sim-offline/audio-atlas";
export const MAXIMUM_AUDIO_ATLAS_MANIFEST_BYTES = 16 * 1024 * 1024;

const UINT64_MAX = 0xffff_ffff_ffff_ffffn;
const UINT32_MAX = 0xffff_ffff;
const KNOWN_STATE_MASK = 0x1f;
// Keep this byte grammar identical to contract::is_valid_semantic_id(). Native
// deliberately imposes no separate length ceiling on semantic IDs.
const ID_PATTERN = /^[a-z0-9][a-z0-9._\-/]*$/;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;

const ROOT_FIELDS = [
  "schema",
  "id",
  "engine",
  "public_seed",
  "audio",
  "domain",
  "moving_segments",
  "stationary_tiles",
  "transient_performances",
  "lifecycle_performances",
  "artifacts",
  "provenance",
];
const IDENTITY_FIELDS = ["id", "sha256"];
const RANGE_FIELDS = ["begin", "end"];
const RPM_RANGE_FIELDS = ["minimum", "maximum"];
const SLOPE_RANGE_FIELDS = ["minimum_per_second", "maximum_per_second"];
const KNOT_FIELDS = [
  "frame",
  "rpm",
  "rpm_slope_rpm_per_second",
  "requested_throttle_01",
  "signed_load_coordinate",
  "manifold_pressure_pa_abs",
  "unwrapped_crank_revolutions",
  "state_mask",
  "transition_mask",
];

export class AudioAtlasLoadError extends Error {
  constructor(code, path, message, options = {}) {
    super(path ? `${path}: ${message}` : message, options);
    this.name = "AudioAtlasLoadError";
    this.code = code;
    this.path = path;
  }
}

function fail(code, path, message, options) {
  throw new AudioAtlasLoadError(code, path, message, options);
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function record(value, fields, path) {
  if (!isRecord(value)) fail("audio-atlas-invalid-manifest", path, "must be an object");
  for (const field of fields) {
    if (!Object.hasOwn(value, field)) {
      fail("audio-atlas-invalid-manifest", path ? `${path}.${field}` : field,
        "required field is absent");
    }
  }
  for (const field of Object.keys(value)) {
    if (!fields.includes(field)) {
      fail("audio-atlas-invalid-manifest", path ? `${path}.${field}` : field,
        "field is not part of the current audio-atlas contract");
    }
  }
  return value;
}

function array(value, path, { nonempty = false, empty = false } = {}) {
  if (!Array.isArray(value)) fail("audio-atlas-invalid-manifest", path, "must be an array");
  if (nonempty && value.length === 0)
    fail("audio-atlas-invalid-manifest", path, "must be a nonempty array");
  if (empty && value.length !== 0) {
    fail("audio-atlas-invalid-manifest", path,
      "is not admitted by the moving-only audio-atlas slice");
  }
  return value;
}

function string(value, path) {
  if (typeof value !== "string") fail("audio-atlas-invalid-manifest", path, "must be a string");
  return value;
}

function semanticId(value, path) {
  if (!ID_PATTERN.test(string(value, path))) {
    fail("audio-atlas-invalid-manifest", path, "must be a canonical audio-atlas ID");
  }
  return value;
}

function sha256(value, path) {
  if (!SHA256_PATTERN.test(string(value, path)) || /^0{64}$/.test(value)) {
    fail("audio-atlas-invalid-manifest", path,
      "must be a nonzero lowercase SHA-256 digest");
  }
  return value;
}

function finite(value, path) {
  if (typeof value !== "number" || !Number.isFinite(value))
    fail("audio-atlas-invalid-manifest", path, "must be a finite number");
  return value;
}

function positive(value, path) {
  if (finite(value, path) <= 0) fail("audio-atlas-invalid-manifest", path, "must be positive");
  return value;
}

function bounded(value, minimum, maximum, path) {
  finite(value, path);
  if (value < minimum || value > maximum) {
    fail("audio-atlas-invalid-manifest", path, `must be in [${minimum}, ${maximum}]`);
  }
  return value;
}

function integer(value, minimum, maximum, path) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    fail("audio-atlas-invalid-manifest", path,
      `must be an integer in [${minimum}, ${maximum}]`);
  }
  return value;
}

function uint64(value, path) {
  if (typeof value !== "string" || !/^(0|[1-9][0-9]{0,19})$/.test(value)) {
    fail("audio-atlas-invalid-manifest", path,
      "must be a canonical uint64 decimal string");
  }
  const result = BigInt(value);
  if (result > UINT64_MAX) fail("audio-atlas-invalid-manifest", path, "exceeds uint64 range");
  return result;
}

function runtimeFrame(value, path) {
  const parsed = uint64(value, path);
  if (parsed > BigInt(Number.MAX_SAFE_INTEGER)) {
    fail("audio-atlas-invalid-manifest", path,
      "is outside the exact JavaScript frame-address range");
  }
  return Number(parsed);
}

function identity(value, path) {
  record(value, IDENTITY_FIELDS, path);
  semanticId(value.id, `${path}.id`);
  sha256(value.sha256, `${path}.sha256`);
}

function safeRelativePath(value, path) {
  string(value, path);
  const unsafe = value.length === 0 || value.length > 4096 ||
    value.startsWith("/") || value.startsWith("\\") || value.includes("\\") ||
    value.includes("\0") || value.includes("%") || value.includes("?") || value.includes("#");
  if (unsafe) {
    fail("audio-atlas-invalid-artifact-path", path,
      "must be a normalized relative path without URL metacharacters");
  }
  if (value.split("/").some((part) => part === "" || part === "." || part === "..")) {
    fail("audio-atlas-invalid-artifact-path", path, "must remain beneath the atlas directory");
  }
  return value;
}

function frameRange(value, path) {
  record(value, RANGE_FIELDS, path);
  const begin = runtimeFrame(value.begin, `${path}.begin`);
  const end = runtimeFrame(value.end, `${path}.end`);
  if (begin >= end) fail("audio-atlas-invalid-manifest", path, "must be a nonempty range");
  return { begin, end };
}

function rpmRange(value, path) {
  record(value, RPM_RANGE_FIELDS, path);
  const minimum = positive(value.minimum, `${path}.minimum`);
  const maximum = positive(value.maximum, `${path}.maximum`);
  if (minimum >= maximum) fail("audio-atlas-invalid-manifest", path, "must be strictly ordered");
  return { minimum, maximum };
}

function stateMask(value, path) {
  integer(value, 0, KNOWN_STATE_MASK, path);
  if ((value & ~KNOWN_STATE_MASK) !== 0)
    fail("audio-atlas-invalid-manifest", path, "contains unknown state bits");
  return value;
}

function validateAudio(manifest) {
  const audio = record(manifest.audio,
    ["sample_rate_hz", "encoding", "channel_layout", "buses"], "audio");
  integer(audio.sample_rate_hz, 1, UINT32_MAX, "audio.sample_rate_hz");
  if (audio.encoding !== "float32le")
    fail("audio-atlas-invalid-manifest", "audio.encoding", "must be float32le");
  if (audio.channel_layout !== "mono")
    fail("audio-atlas-invalid-manifest", "audio.channel_layout", "must be mono");
  const buses = array(audio.buses, "audio.buses", { nonempty: true });
  const ids = new Set();
  for (let index = 0; index < buses.length; ++index) {
    const path = `audio.buses[${index}]`;
    record(buses[index], ["id"], path);
    const id = semanticId(buses[index].id, `${path}.id`);
    if (ids.has(id)) fail("audio-atlas-invalid-manifest", `${path}.id`, "is duplicated");
    ids.add(id);
  }
  return ids;
}

function validateDomain(manifest) {
  const domain = record(manifest.domain, ["minimum_rpm", "maximum_rpm",
    "minimum_load_coordinate", "maximum_load_coordinate"], "domain");
  const minimumRpm = positive(domain.minimum_rpm, "domain.minimum_rpm");
  const maximumRpm = positive(domain.maximum_rpm, "domain.maximum_rpm");
  if (minimumRpm >= maximumRpm)
    fail("audio-atlas-invalid-manifest", "domain.maximum_rpm",
      "must exceed domain.minimum_rpm");
  const minimumLoad = bounded(domain.minimum_load_coordinate, -1, 1,
    "domain.minimum_load_coordinate");
  const maximumLoad = bounded(domain.maximum_load_coordinate, -1, 1,
    "domain.maximum_load_coordinate");
  if (minimumLoad > maximumLoad)
    fail("audio-atlas-invalid-manifest", "domain.maximum_load_coordinate",
      "must not be below domain.minimum_load_coordinate");
  return { minimumRpm, maximumRpm, minimumLoad, maximumLoad };
}

function validateArtifacts(manifest) {
  const artifacts = array(manifest.artifacts, "artifacts", { nonempty: true });
  const compiled = [];
  const byId = new Map();
  for (let index = 0; index < artifacts.length; ++index) {
    const descriptor = artifacts[index];
    const path = `artifacts[${index}]`;
    record(descriptor,
      ["id", "relative_path", "frame_count", "byte_count", "payload_sha256"], path);
    const id = semanticId(descriptor.id, `${path}.id`);
    if (byId.has(id)) fail("audio-atlas-invalid-manifest", `${path}.id`, "is duplicated");
    safeRelativePath(descriptor.relative_path, `${path}.relative_path`);
    const frameCount64 = uint64(descriptor.frame_count, `${path}.frame_count`);
    const byteCount64 = uint64(descriptor.byte_count, `${path}.byte_count`);
    if (frameCount64 === 0n)
      fail("audio-atlas-invalid-manifest", `${path}.frame_count`, "must be positive");
    if (byteCount64 !== frameCount64 * 4n)
      fail("audio-atlas-invalid-manifest", `${path}.byte_count`,
        "must equal frame_count times four for mono Float32");
    if (
      frameCount64 > BigInt(Number.MAX_SAFE_INTEGER) ||
      byteCount64 > BigInt(Number.MAX_SAFE_INTEGER)
    ) {
      fail("audio-atlas-invalid-manifest", path,
        "artifact is outside the exact JavaScript address range");
    }
    sha256(descriptor.payload_sha256, `${path}.payload_sha256`);
    const item = { descriptor, index, id, frameCount: Number(frameCount64),
      byteCount: Number(byteCount64), useCount: 0 };
    compiled.push(item);
    byId.set(id, item);
  }
  return { compiled, byId };
}

function validateTimeline(segment, capturedFrames, path) {
  const timeline = record(segment.timeline, ["knots"], path);
  const knots = array(timeline.knots, `${path}.knots`, { nonempty: true });
  if (knots.length < 2)
    fail("audio-atlas-invalid-manifest", `${path}.knots`,
      "must contain at least two knots");
  const compiled = [];
  for (let index = 0; index < knots.length; ++index) {
    const knot = knots[index];
    const knotPath = `${path}.knots[${index}]`;
    record(knot, KNOT_FIELDS, knotPath);
    const item = {
      frame: runtimeFrame(knot.frame, `${knotPath}.frame`),
      rpm: positive(knot.rpm, `${knotPath}.rpm`),
      rpmSlopeRpmPerSecond: finite(knot.rpm_slope_rpm_per_second,
        `${knotPath}.rpm_slope_rpm_per_second`),
      requestedThrottle01: bounded(knot.requested_throttle_01, 0, 1,
        `${knotPath}.requested_throttle_01`),
      signedLoadCoordinate: bounded(knot.signed_load_coordinate, -1, 1,
        `${knotPath}.signed_load_coordinate`),
      manifoldPressurePaAbs: positive(knot.manifold_pressure_pa_abs,
        `${knotPath}.manifold_pressure_pa_abs`),
      unwrappedCrankRevolutions: finite(knot.unwrapped_crank_revolutions,
        `${knotPath}.unwrapped_crank_revolutions`),
      stateMask: stateMask(knot.state_mask, `${knotPath}.state_mask`),
      transitionMask: stateMask(
        knot.transition_mask,
        `${knotPath}.transition_mask`,
      ),
    };
    if (index > 0) {
      const previous = compiled[index - 1];
      if (item.frame <= previous.frame) {
        fail("audio-atlas-invalid-manifest", `${knotPath}.frame`,
          "timeline frame addresses must increase strictly");
      }
      if (item.unwrappedCrankRevolutions <= previous.unwrappedCrankRevolutions) {
        fail("audio-atlas-invalid-manifest", `${knotPath}.unwrapped_crank_revolutions`,
          "moving crank position must increase strictly");
      }
      // Instantaneous shaft speed legitimately contains combustion-order ripple.
      // The separately captured macro RPM slope owns segment direction.
    }
    compiled.push(item);
  }
  if (compiled[0].frame !== capturedFrames.begin) {
    fail("audio-atlas-invalid-manifest", `${path}.knots[0].frame`,
      "must equal captured_frames.begin");
  }
  if (compiled.at(-1).frame !== capturedFrames.end) {
    fail("audio-atlas-invalid-manifest", `${path}.knots[${knots.length - 1}].frame`,
      "must be the captured-frame exclusive-end sentinel");
  }
  return compiled;
}

function fractionalFrame(value, path) {
  record(value, ["left_frame", "right_frame", "fraction_from_left_01"], path);
  const leftFrame = runtimeFrame(value.left_frame, `${path}.left_frame`);
  const rightFrame = runtimeFrame(value.right_frame, `${path}.right_frame`);
  const fractionFromLeft01 = bounded(value.fraction_from_left_01, 0, 1,
    `${path}.fraction_from_left_01`);
  const exact = leftFrame === rightFrame && fractionFromLeft01 === 0;
  const fractional = rightFrame === leftFrame + 1 &&
    fractionFromLeft01 > 0 && fractionFromLeft01 < 1;
  if (!exact && !fractional) {
    fail("audio-atlas-invalid-manifest", path,
      "must be an exact frame or a strict fraction between adjacent frames");
  }
  return { leftFrame, rightFrame, fractionFromLeft01,
    value: leftFrame + fractionFromLeft01 };
}

function validateCrankBoundaries(segment, capturedFrames, path) {
  const boundaries = array(segment.crank_boundaries, path, { nonempty: true });
  if (boundaries.length < 2)
    fail("audio-atlas-invalid-manifest", path,
      "must contain at least two complete-cycle boundaries");
  const compiled = [];
  for (let index = 0; index < boundaries.length; ++index) {
    const boundary = boundaries[index];
    const itemPath = `${path}[${index}]`;
    record(boundary, ["completed_cycle_ordinal", "position"], itemPath);
    const completedCycleOrdinal = uint64(boundary.completed_cycle_ordinal,
      `${itemPath}.completed_cycle_ordinal`);
    const position = fractionalFrame(boundary.position, `${itemPath}.position`);
    if (
      position.leftFrame < capturedFrames.begin ||
      position.rightFrame > capturedFrames.end
    ) {
      fail("audio-atlas-invalid-manifest", `${itemPath}.position`,
        "lies outside captured_frames");
    }
    if (index > 0) {
      const previous = compiled[index - 1];
      if (completedCycleOrdinal !== previous.completedCycleOrdinal + 1n) {
        fail("audio-atlas-invalid-manifest", `${itemPath}.completed_cycle_ordinal`,
          "cycle ordinals must be contiguous");
      }
      if (position.value <= previous.position.value) {
        fail("audio-atlas-invalid-manifest", `${itemPath}.position`,
          "cycle-boundary positions must increase strictly");
      }
    }
    compiled.push({ completedCycleOrdinal, position });
  }
  return compiled;
}

function validateHandoff(segment, capturedFrames, usableFrames, path) {
  const handoff = record(segment.handoff, ["transition_frames", "maximum_rpm_error",
    "maximum_normalized_rpm_slope_error_per_second", "maximum_load_error",
    "maximum_crank_phase_error_revolutions"], path);
  const transitionFrames = integer(handoff.transition_frames, 1, UINT32_MAX,
    `${path}.transition_frames`);
  const maximumRpmError = finite(handoff.maximum_rpm_error, `${path}.maximum_rpm_error`);
  const maximumSlopeError = finite(handoff.maximum_normalized_rpm_slope_error_per_second,
    `${path}.maximum_normalized_rpm_slope_error_per_second`);
  const maximumLoadError = bounded(handoff.maximum_load_error, 0, 2,
    `${path}.maximum_load_error`);
  const maximumPhaseError = bounded(handoff.maximum_crank_phase_error_revolutions, 0, 0.5,
    `${path}.maximum_crank_phase_error_revolutions`);
  if (maximumRpmError < 0 || maximumSlopeError < 0) {
    fail("audio-atlas-invalid-manifest", path, "handoff error limits must be nonnegative");
  }
  if (
    usableFrames.begin - capturedFrames.begin < transitionFrames ||
    capturedFrames.end - usableFrames.end < transitionFrames
  ) {
    fail("audio-atlas-invalid-manifest", `${path}.transition_frames`,
      "captured guards must contain the complete handoff duration");
  }
  return {
    transitionFrames,
    maximumRpmError,
    maximumSlopeError,
    maximumLoadError,
    maximumPhaseError,
  };
}

function validateSegments(manifest, buses, domain, artifacts) {
  const segments = array(manifest.moving_segments, "moving_segments", { nonempty: true });
  const ids = new Set();
  const compiled = [];
  for (let index = 0; index < segments.length; ++index) {
    const segment = segments[index];
    const path = `moving_segments[${index}]`;
    record(segment, ["id", "direction", "load_coordinate", "state_mask",
      "normalized_rpm_slope", "captured_frames", "usable_frames", "captured_rpm",
      "usable_rpm", "source_scenario", "capture_configuration", "artifacts",
      "timeline", "crank_boundaries", "handoff"], path);
    const id = semanticId(segment.id, `${path}.id`);
    if (ids.has(id)) fail("audio-atlas-invalid-manifest", `${path}.id`, "is duplicated");
    ids.add(id);
    if (segment.direction !== "rising" && segment.direction !== "falling") {
      fail("audio-atlas-invalid-manifest", `${path}.direction`, "must be rising or falling");
    }
    const loadCoordinate = bounded(segment.load_coordinate, -1, 1, `${path}.load_coordinate`);
    if (loadCoordinate < domain.minimumLoad || loadCoordinate > domain.maximumLoad) {
      fail("audio-atlas-invalid-manifest", `${path}.load_coordinate`,
        "lies outside the atlas load domain");
    }
    const segmentStateMask = stateMask(segment.state_mask, `${path}.state_mask`);
    const slope = record(segment.normalized_rpm_slope, SLOPE_RANGE_FIELDS,
      `${path}.normalized_rpm_slope`);
    const minimumSlope = finite(slope.minimum_per_second,
      `${path}.normalized_rpm_slope.minimum_per_second`);
    const maximumSlope = finite(slope.maximum_per_second,
      `${path}.normalized_rpm_slope.maximum_per_second`);
    if (
      minimumSlope > maximumSlope ||
      (segment.direction === "rising" && minimumSlope <= 0) ||
      (segment.direction === "falling" && maximumSlope >= 0)
    ) {
      fail("audio-atlas-invalid-manifest", `${path}.normalized_rpm_slope`,
        "must be ordered and agree with direction");
    }
    const capturedFrames = frameRange(segment.captured_frames, `${path}.captured_frames`);
    const usableFrames = frameRange(segment.usable_frames, `${path}.usable_frames`);
    if (
      capturedFrames.begin >= usableFrames.begin ||
      usableFrames.end >= capturedFrames.end
    ) {
      fail("audio-atlas-invalid-manifest", `${path}.usable_frames`,
        "must be a strict interior of captured_frames");
    }
    const capturedRpm = rpmRange(segment.captured_rpm, `${path}.captured_rpm`);
    const usableRpm = rpmRange(segment.usable_rpm, `${path}.usable_rpm`);
    if (
      capturedRpm.minimum >= usableRpm.minimum ||
      usableRpm.maximum >= capturedRpm.maximum ||
      usableRpm.minimum < domain.minimumRpm ||
      usableRpm.maximum > domain.maximumRpm
    ) {
      fail("audio-atlas-invalid-manifest", `${path}.usable_rpm`,
        "must be interior to captured_rpm and inside the atlas domain");
    }
    identity(segment.source_scenario, `${path}.source_scenario`);
    identity(segment.capture_configuration, `${path}.capture_configuration`);

    const references = array(segment.artifacts, `${path}.artifacts`, { nonempty: true });
    const referencedBuses = new Set();
    const compiledReferences = [];
    for (let referenceIndex = 0; referenceIndex < references.length; ++referenceIndex) {
      const reference = references[referenceIndex];
      const referencePath = `${path}.artifacts[${referenceIndex}]`;
      record(reference, ["bus_id", "artifact_id"], referencePath);
      const busId = semanticId(reference.bus_id, `${referencePath}.bus_id`);
      const artifactId = semanticId(reference.artifact_id, `${referencePath}.artifact_id`);
      if (!buses.has(busId)) {
        fail("audio-atlas-invalid-manifest", `${referencePath}.bus_id`,
          "references an undeclared bus");
      }
      if (referencedBuses.has(busId)) {
        fail("audio-atlas-invalid-manifest", `${referencePath}.bus_id`,
          "duplicates a bus binding");
      }
      const artifact = artifacts.byId.get(artifactId);
      if (!artifact) {
        fail("audio-atlas-invalid-manifest", `${referencePath}.artifact_id`,
          "references an undeclared artifact");
      }
      if (capturedFrames.end > artifact.frameCount) {
        fail("audio-atlas-invalid-manifest", `${referencePath}.artifact_id`,
          "does not contain the captured frame range");
      }
      ++artifact.useCount;
      referencedBuses.add(busId);
      compiledReferences.push({ busId, artifactId });
    }
    if (referencedBuses.size !== buses.size) {
      fail("audio-atlas-invalid-manifest", `${path}.artifacts`,
        "must bind every declared audio bus exactly once");
    }

    const timeline = validateTimeline(segment, capturedFrames, `${path}.timeline`);
    const crankBoundaries = validateCrankBoundaries(segment, capturedFrames,
      `${path}.crank_boundaries`);
    const handoff = validateHandoff(segment, capturedFrames, usableFrames,
      `${path}.handoff`);
    compiled.push({
      descriptor: segment,
      id,
      direction: segment.direction,
      loadCoordinate,
      stateMask: segmentStateMask,
      normalizedRpmSlope: { minimumPerSecond: minimumSlope, maximumPerSecond: maximumSlope },
      capturedFrames,
      usableFrames,
      capturedRpm,
      usableRpm,
      references: compiledReferences,
      timeline,
      crankBoundaries,
      handoff,
    });
  }
  for (const artifact of artifacts.compiled) {
    if (artifact.useCount !== 1) {
      fail("audio-atlas-invalid-manifest", `artifacts[${artifact.index}]`,
        "must be owned by exactly one moving-segment bus");
    }
  }
  return compiled;
}

function compileManifest(manifest) {
  record(manifest, ROOT_FIELDS, "");
  if (manifest.schema !== AUDIO_ATLAS_SCHEMA) {
    fail("audio-atlas-invalid-manifest", "schema", `must be ${AUDIO_ATLAS_SCHEMA}`);
  }
  semanticId(manifest.id, "id");
  semanticId(manifest.engine, "engine");
  uint64(manifest.public_seed, "public_seed");
  const buses = validateAudio(manifest);
  const domain = validateDomain(manifest);
  array(manifest.stationary_tiles, "stationary_tiles", { empty: true });
  array(manifest.transient_performances, "transient_performances", { empty: true });
  array(manifest.lifecycle_performances, "lifecycle_performances", { empty: true });
  const artifacts = validateArtifacts(manifest);
  const segments = validateSegments(manifest, buses, domain, artifacts);
  const provenance = record(manifest.provenance,
    ["engine", "bake_document", "renderer_build", "source_inputs"], "provenance");
  identity(provenance.engine, "provenance.engine");
  identity(provenance.bake_document, "provenance.bake_document");
  identity(provenance.renderer_build, "provenance.renderer_build");
  identity(provenance.source_inputs, "provenance.source_inputs");
  if (provenance.engine.id !== manifest.engine) {
    fail("audio-atlas-invalid-manifest", "provenance.engine.id",
      "must equal the atlas engine ID");
  }
  return { buses: [...buses], domain, artifacts: artifacts.compiled, segments };
}

export function validateAudioAtlasManifest(manifest) {
  compileManifest(manifest);
  return manifest;
}

function resolveManifestUrl(value) {
  if (value instanceof URL) {
    return new URL(value.href);
  }
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError("audioAtlasManifestUrl must be a nonempty URL or string");
  }
  try {
    return new URL(value, globalThis.location?.href);
  } catch (error) {
    throw new TypeError("audioAtlasManifestUrl must resolve to an absolute URL", {
      cause: error,
    });
  }
}

function resolveArtifactUrl(manifestUrl, artifact) {
  const base = new URL(".", manifestUrl);
  const url = new URL(artifact.descriptor.relative_path, base);
  if (
    url.origin !== base.origin ||
    !url.pathname.startsWith(base.pathname) ||
    url.search !== "" ||
    url.hash !== ""
  ) {
    fail(
      "audio-atlas-invalid-artifact-path",
      `artifacts[${artifact.index}].relative_path`,
      "does not remain beneath the atlas URL",
    );
  }
  return url;
}

async function fetchBytes(url, fetchImplementation, path) {
  let response;
  try {
    response = await fetchImplementation(url.href);
  } catch (error) {
    fail("audio-atlas-fetch-failed", path, `fetch failed for ${url.href}`, {
      cause: error,
    });
  }
  if (!response || response.ok !== true || typeof response.arrayBuffer !== "function") {
    const status = response?.status === undefined ? "invalid response" : `HTTP ${response.status}`;
    fail("audio-atlas-fetch-failed", path, `${status} for ${url.href}`);
  }
  if (typeof response.url === "string" && response.url !== "") {
    let finalUrl;
    try {
      finalUrl = new URL(response.url).href;
    } catch {
      fail("audio-atlas-fetch-failed", path, "response reports an invalid final URL");
    }
    if (finalUrl !== url.href) {
      fail(
        "audio-atlas-fetch-failed",
        path,
        "cross-location redirects are not admitted",
      );
    }
  }
  let body;
  try {
    body = await response.arrayBuffer();
  } catch (error) {
    fail("audio-atlas-fetch-failed", path, "response body could not be read", {
      cause: error,
    });
  }
  if (!(body instanceof ArrayBuffer)) {
    fail("audio-atlas-fetch-failed", path, "response body is not an ArrayBuffer");
  }
  return new Uint8Array(body);
}

async function digestHex(bytes, cryptoImplementation, path) {
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    fail("audio-atlas-crypto-unavailable", path, "Web Crypto SHA-256 is unavailable");
  }
  let digest;
  try {
    digest = new Uint8Array(
      await cryptoImplementation.subtle.digest("SHA-256", bytes),
    );
  } catch (error) {
    fail("audio-atlas-crypto-unavailable", path, "SHA-256 digest failed", {
      cause: error,
    });
  }
  return Array.from(digest, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function decodeManifest(bytes) {
  if (
    bytes.byteLength === 0 ||
    bytes.byteLength > MAXIMUM_AUDIO_ATLAS_MANIFEST_BYTES
  ) {
    fail(
      "audio-atlas-invalid-manifest",
      "atlas.json",
      "manifest must contain between 1 byte and 16 MiB",
    );
  }
  if (
    bytes.byteLength >= 3 &&
    bytes[0] === 0xef &&
    bytes[1] === 0xbb &&
    bytes[2] === 0xbf
  ) {
    fail("audio-atlas-invalid-manifest", "atlas.json", "UTF-8 BOM is not admitted");
  }
  let text;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch (error) {
    fail("audio-atlas-invalid-manifest", "atlas.json", "is not valid UTF-8", {
      cause: error,
    });
  }
  let manifest;
  try {
    manifest = JSON.parse(text);
  } catch (error) {
    fail("audio-atlas-invalid-manifest", "atlas.json", "is not valid JSON", {
      cause: error,
    });
  }
  return { manifest, compiled: compileManifest(manifest) };
}

function decodeFloat32Le(bytes, frameCount, path) {
  if (bytes.byteOffset % Float32Array.BYTES_PER_ELEMENT !== 0) {
    fail("audio-atlas-invalid-pcm", path,
      "Float32 payload is not aligned for its canonical decoded view");
  }
  // Digest verification has already consumed the immutable wire bytes. Decode
  // explicitly as little-endian, then retain this same allocation as the sole PCM
  // representation instead of allocating a second package-sized buffer.
  const samples = new Float32Array(bytes.buffer, bytes.byteOffset, frameCount);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let frame = 0; frame < frameCount; ++frame) {
    const sample = view.getFloat32(frame * 4, true);
    if (!Number.isFinite(sample)) {
      fail(
        "audio-atlas-invalid-pcm",
        path,
        `contains a non-finite Float32 sample at frame ${frame}`,
      );
    }
    samples[frame] = sample;
  }
  return samples;
}

function deepFreezeJson(value) {
  if (value !== null && typeof value === "object" && !Object.isFrozen(value)) {
    for (const child of Object.values(value)) {
      deepFreezeJson(child);
    }
    Object.freeze(value);
  }
  return value;
}

async function loadArtifact(
  manifestUrl,
  artifact,
  sampleRate,
  fetchImplementation,
  cryptoImplementation,
) {
  const path = `artifacts[${artifact.index}]`;
  const url = resolveArtifactUrl(manifestUrl, artifact);
  const bytes = await fetchBytes(url, fetchImplementation, path);
  if (bytes.byteLength !== artifact.byteCount) {
    fail(
      "audio-atlas-artifact-size-mismatch",
      `${path}.byte_count`,
      `expected ${artifact.byteCount} bytes but fetched ${bytes.byteLength}`,
    );
  }
  const actualDigest = await digestHex(bytes, cryptoImplementation, path);
  if (actualDigest !== artifact.descriptor.payload_sha256) {
    fail(
      "audio-atlas-artifact-digest-mismatch",
      `${path}.payload_sha256`,
      `expected ${artifact.descriptor.payload_sha256} but fetched ${actualDigest}`,
    );
  }
  const pcm = decodeFloat32Le(bytes, artifact.frameCount, path);
  return Object.freeze({
    id: artifact.id,
    relativePath: artifact.descriptor.relative_path,
    url: url.href,
    descriptor: artifact.descriptor,
    // PCM is the sole retained payload representation. Keeping the fetched bytes
    // as a second public mutable alias would allow the two views to disagree and
    // would double every loaded artifact's resident payload.
    pcm,
    sampleRate,
    channelCount: 1,
    frameCount: artifact.frameCount,
  });
}

export async function loadAudioAtlas(
  audioAtlasManifestUrl,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    throw new TypeError("audio-atlas loading requires a fetch function");
  }
  const manifestUrl = resolveManifestUrl(audioAtlasManifestUrl);
  const { manifest, compiled } = decodeManifest(
    await fetchBytes(manifestUrl, fetchImplementation, "atlas.json"),
  );
  // The first atlas loader intentionally bounds in-flight payload memory to one
  // artifact. A later concurrency policy can add a small explicit limit without
  // changing the decoded atlas contract.
  const loadedArtifacts = [];
  for (const artifact of compiled.artifacts) {
    loadedArtifacts.push(await loadArtifact(
      manifestUrl,
      artifact,
      manifest.audio.sample_rate_hz,
      fetchImplementation,
      cryptoImplementation,
    ));
  }
  const artifactsById = Object.create(null);
  for (const artifact of loadedArtifacts) {
    artifactsById[artifact.id] = artifact;
  }
  const movingSegments = compiled.segments.map((segment) => {
    const busArtifacts = segment.references.map((reference) =>
      Object.freeze({
        id: reference.busId,
        artifact: artifactsById[reference.artifactId],
      }),
    );
    return Object.freeze({
      ...segment,
      busArtifacts: Object.freeze(busArtifacts),
      bus(id) {
        return busArtifacts.find((bus) => bus.id === id)?.artifact ?? null;
      },
    });
  });
  Object.freeze(loadedArtifacts);
  Object.freeze(artifactsById);
  Object.freeze(movingSegments);
  deepFreezeJson(manifest);
  return Object.freeze({
    manifestUrl: manifestUrl.href,
    manifest,
    sampleRate: manifest.audio.sample_rate_hz,
    busIds: Object.freeze(manifest.audio.buses.map((bus) => bus.id)),
    artifacts: loadedArtifacts,
    artifactsById,
    movingSegments,
    artifact(id) {
      return Object.hasOwn(artifactsById, id) ? artifactsById[id] : null;
    },
    segment(id) {
      return movingSegments.find((segment) => segment.id === id) ?? null;
    },
  });
}
