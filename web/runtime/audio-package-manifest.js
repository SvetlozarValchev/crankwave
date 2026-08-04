import { ESO_CANONICAL_SAMPLE_RATE } from "./c-api-abi.js";

export const AUDIO_PACKAGE_SCHEMA = "engine-sim-offline/audio-package";
export const MAXIMUM_AUDIO_PACKAGE_MANIFEST_BYTES = 16 * 1024 * 1024;

const MAXIMUM_RUNTIME_INTEGER = 2 ** 53 - 1;
const MAXIMUM_UINT32 = 0xffff_ffff;
const MAXIMUM_UINT64_DECIMAL = 18_446_744_073_709_551_615n;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;
const SEMANTIC_ID_PATTERN = /^[a-z0-9][a-z0-9._/-]*$/;
const ARTIFACT_PATH_PATTERN = /^[A-Za-z0-9._/-]+$/;
const CONTENT_IDENTITY_FIELDS = ["id", "sha256"];
const BOUNDARY_FIELDS = [
  "left_frame",
  "right_frame",
  "fraction_from_left_01",
];
const UNIT_FIELDS = [
  "completed_cycle_ordinal",
  "start",
  "end",
  "canonical_rpm",
  "measured_rpm",
  "average_signed_load",
  "average_net_torque_nm",
  "average_requested_throttle_01",
  "average_resolved_throttle_01",
  "state_mask",
  "transition_mask",
];

export class AudioPackageLoadError extends Error {
  constructor(code, path, message, options = {}) {
    super(path ? `${path}: ${message}` : message, options);
    this.name = "AudioPackageLoadError";
    this.code = code;
    this.path = path;
  }
}

function fail(code, path, message, options) {
  throw new AudioPackageLoadError(code, path, message, options);
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function requireRecord(value, fields, path) {
  if (!isRecord(value)) {
    fail("audio-package-invalid-manifest", path, "must be an object");
  }
  const keys = Object.keys(value);
  for (const field of fields) {
    if (!Object.hasOwn(value, field)) {
      fail(
        "audio-package-invalid-manifest",
        path ? `${path}.${field}` : field,
        "required field is absent",
      );
    }
  }
  for (const key of keys) {
    if (!fields.includes(key)) {
      fail(
        "audio-package-invalid-manifest",
        path ? `${path}.${key}` : key,
        "field is not part of the current audio-package contract",
      );
    }
  }
  if (keys.length !== fields.length) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "object does not have the exact current-contract shape",
    );
  }
  return value;
}

function requireArray(value, path, { nonempty = false } = {}) {
  if (!Array.isArray(value) || (nonempty && value.length === 0)) {
    fail(
      "audio-package-invalid-manifest",
      path,
      nonempty ? "must be a nonempty array" : "must be an array",
    );
  }
  return value;
}

function requireString(value, path) {
  if (typeof value !== "string") {
    fail("audio-package-invalid-manifest", path, "must be a string");
  }
  return value;
}

function requireSemanticId(value, path) {
  requireString(value, path);
  if (!SEMANTIC_ID_PATTERN.test(value)) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "must be a canonical semantic ID",
    );
  }
  return value;
}

function requireSha256(value, path) {
  requireString(value, path);
  if (!SHA256_PATTERN.test(value) || /^0{64}$/.test(value)) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "must be a nonzero lowercase SHA-256 digest",
    );
  }
  return value;
}

function requireCanonicalFinite(value, path) {
  if (!Number.isFinite(value) || Object.is(value, -0)) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "must be a finite canonical JSON number",
    );
  }
  return value;
}

function requireRuntimeInteger(value, path, maximum = MAXIMUM_RUNTIME_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    fail(
      "audio-package-invalid-manifest",
      path,
      `must be an unsigned integer no greater than ${maximum}`,
    );
  }
  return value;
}

function requireContentIdentity(value, path) {
  requireRecord(value, CONTENT_IDENTITY_FIELDS, path);
  requireSemanticId(value.id, `${path}.id`);
  requireSha256(value.sha256, `${path}.sha256`);
}

function requireSelectorSeed(value, path) {
  requireString(value, path);
  if (!/^(0|[1-9][0-9]*)$/.test(value)) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "must be a canonical unsigned decimal string",
    );
  }
  let parsed;
  try {
    parsed = BigInt(value);
  } catch {
    fail("audio-package-invalid-manifest", path, "is not an unsigned integer");
  }
  if (parsed > MAXIMUM_UINT64_DECIMAL) {
    fail("audio-package-invalid-manifest", path, "exceeds uint64 range");
  }
}

function nearlyEqual(left, right) {
  if (!Number.isFinite(left) || !Number.isFinite(right)) {
    return false;
  }
  const scale = Math.max(1, Math.abs(left), Math.abs(right));
  const tolerance = Math.max(1e-12, 16 * Number.EPSILON * scale);
  return Math.abs(left - right) <= tolerance;
}

function boundaryLess(left, right) {
  return (
    left.left_frame < right.left_frame ||
    (left.left_frame === right.left_frame &&
      left.fraction_from_left_01 < right.fraction_from_left_01)
  );
}

function boundaryLessEqual(left, right) {
  return !boundaryLess(right, left);
}

function validateBoundary(boundary, sourceFrameCount, edgeGuardFrames, path) {
  requireRecord(boundary, BOUNDARY_FIELDS, path);
  const left = requireRuntimeInteger(boundary.left_frame, `${path}.left_frame`);
  const right = requireRuntimeInteger(boundary.right_frame, `${path}.right_frame`);
  const fraction = requireCanonicalFinite(
    boundary.fraction_from_left_01,
    `${path}.fraction_from_left_01`,
  );
  const exact = left === right && fraction === 0;
  const interpolated =
    left < MAXIMUM_RUNTIME_INTEGER &&
    right === left + 1 &&
    fraction > 0 &&
    fraction < 1;
  if (!exact && !interpolated) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "must be an exact frame or a strict fraction between adjacent frames",
    );
  }
  if (right >= sourceFrameCount) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.right_frame`,
      "boundary bracket lies outside its source tape",
    );
  }
  if (left < edgeGuardFrames) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.left_frame`,
      "boundary lacks declared left-edge source context",
    );
  }
  if (sourceFrameCount - right - 1 < edgeGuardFrames) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.right_frame`,
      "boundary lacks declared right-edge source context",
    );
  }
}

function validateUnit(unit, sourceFrameCount, edgeGuardFrames, path) {
  requireRecord(unit, UNIT_FIELDS, path);
  requireRuntimeInteger(unit.completed_cycle_ordinal, `${path}.completed_cycle_ordinal`);
  validateBoundary(unit.start, sourceFrameCount, edgeGuardFrames, `${path}.start`);
  validateBoundary(unit.end, sourceFrameCount, edgeGuardFrames, `${path}.end`);
  if (!boundaryLess(unit.start, unit.end)) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.end`,
      "cycle unit must have positive source duration",
    );
  }
  if (requireCanonicalFinite(unit.canonical_rpm, `${path}.canonical_rpm`) <= 0) {
    fail("audio-package-invalid-manifest", `${path}.canonical_rpm`, "must be positive");
  }
  if (requireCanonicalFinite(unit.measured_rpm, `${path}.measured_rpm`) <= 0) {
    fail("audio-package-invalid-manifest", `${path}.measured_rpm`, "must be positive");
  }
  const load = requireCanonicalFinite(
    unit.average_signed_load,
    `${path}.average_signed_load`,
  );
  if (load < -1 || load > 1) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.average_signed_load`,
      "must be in [-1, 1]",
    );
  }
  requireCanonicalFinite(
    unit.average_net_torque_nm,
    `${path}.average_net_torque_nm`,
  );
  for (const field of [
    "average_requested_throttle_01",
    "average_resolved_throttle_01",
  ]) {
    const throttle = requireCanonicalFinite(unit[field], `${path}.${field}`);
    if (throttle < 0 || throttle > 1) {
      fail("audio-package-invalid-manifest", `${path}.${field}`, "must be in [0, 1]");
    }
  }
  requireRuntimeInteger(unit.state_mask, `${path}.state_mask`, MAXIMUM_UINT32);
  requireRuntimeInteger(
    unit.transition_mask,
    `${path}.transition_mask`,
    MAXIMUM_UINT32,
  );
  if (unit.transition_mask !== 0) {
    fail(
      "audio-package-invalid-manifest",
      `${path}.transition_mask`,
      "normal-running cycle units must have one stable state",
    );
  }
}

function validateUnits(
  units,
  sourceFrameCount,
  edgeGuardFrames,
  direction,
  path,
) {
  requireArray(units, path, { nonempty: true });
  const ordinals = new Set();
  for (let index = 0; index < units.length; ++index) {
    const itemPath = `${path}[${index}]`;
    const unit = units[index];
    validateUnit(unit, sourceFrameCount, edgeGuardFrames, itemPath);
    if (ordinals.has(unit.completed_cycle_ordinal)) {
      fail(
        "audio-package-invalid-manifest",
        `${itemPath}.completed_cycle_ordinal`,
        "cycle ordinal is duplicated within the lane",
      );
    }
    ordinals.add(unit.completed_cycle_ordinal);
    if (index === 0) {
      continue;
    }
    const previous = units[index - 1];
    if (direction === "rising") {
      if (previous.completed_cycle_ordinal >= unit.completed_cycle_ordinal) {
        fail(
          "audio-package-invalid-manifest",
          `${itemPath}.completed_cycle_ordinal`,
          "rising rows do not preserve increasing source-cycle order",
        );
      }
      if (!boundaryLessEqual(previous.end, unit.start)) {
        fail(
          "audio-package-invalid-manifest",
          `${itemPath}.start`,
          "rising rows overlap source frames",
        );
      }
    } else {
      if (previous.completed_cycle_ordinal <= unit.completed_cycle_ordinal) {
        fail(
          "audio-package-invalid-manifest",
          `${itemPath}.completed_cycle_ordinal`,
          "falling rows do not preserve decreasing source-cycle order",
        );
      }
      if (!boundaryLessEqual(unit.end, previous.start)) {
        fail(
          "audio-package-invalid-manifest",
          `${itemPath}.end`,
          "falling rows overlap source frames",
        );
      }
    }
  }
}

export function validateAudioPackageArtifactPath(value, path) {
  requireString(value, path);
  if (
    value.length === 0 ||
    value.startsWith("/") ||
    value.endsWith("/") ||
    value.includes("\\") ||
    value.includes("?") ||
    value.includes("#") ||
    value.includes(":") ||
    !ARTIFACT_PATH_PATTERN.test(value)
  ) {
    fail(
      "audio-package-invalid-artifact-path",
      path,
      "must be a conservative package-relative path",
    );
  }
  const components = value.split("/");
  if (components.some((component) => component === "" || component === "." || component === "..")) {
    fail(
      "audio-package-invalid-artifact-path",
      path,
      "must not contain empty or traversal components",
    );
  }
}

function validateArtifacts(artifacts) {
  requireArray(artifacts, "artifacts", { nonempty: true });
  const registry = new Map();
  const portablePaths = new Set();
  for (let index = 0; index < artifacts.length; ++index) {
    const path = `artifacts[${index}]`;
    const artifact = requireRecord(
      artifacts[index],
      ["id", "relative_path", "frame_count", "byte_count", "payload_sha256"],
      path,
    );
    requireSemanticId(artifact.id, `${path}.id`);
    if (registry.has(artifact.id)) {
      fail("audio-package-invalid-manifest", `${path}.id`, "artifact ID is duplicated");
    }
    if (index > 0 && artifacts[index - 1].id >= artifact.id) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.id`,
        "artifacts must be strictly ordered by ID",
      );
    }
    validateAudioPackageArtifactPath(
      artifact.relative_path,
      `${path}.relative_path`,
    );
    const portablePath = artifact.relative_path.toLowerCase();
    if (portablePaths.has(portablePath)) {
      fail(
        "audio-package-invalid-artifact-path",
        `${path}.relative_path`,
        "artifact path is not unique under portable case folding",
      );
    }
    portablePaths.add(portablePath);
    if (requireRuntimeInteger(artifact.frame_count, `${path}.frame_count`) === 0) {
      fail("audio-package-invalid-manifest", `${path}.frame_count`, "must be positive");
    }
    if (requireRuntimeInteger(artifact.byte_count, `${path}.byte_count`) === 0) {
      fail("audio-package-invalid-manifest", `${path}.byte_count`, "must be positive");
    }
    requireSha256(artifact.payload_sha256, `${path}.payload_sha256`);
    registry.set(artifact.id, artifact);
  }
  return registry;
}

function validateLaneArtifacts(references, buses, artifactRegistry, referencedIds, path) {
  requireArray(references, path);
  if (references.length !== buses.length) {
    fail(
      "audio-package-invalid-manifest",
      path,
      "lane must reference exactly one artifact per package bus",
    );
  }
  let sharedFrameCount = 0;
  for (let index = 0; index < references.length; ++index) {
    const itemPath = `${path}[${index}]`;
    const reference = requireRecord(
      references[index],
      ["bus_id", "artifact_id"],
      itemPath,
    );
    requireSemanticId(reference.bus_id, `${itemPath}.bus_id`);
    requireSemanticId(reference.artifact_id, `${itemPath}.artifact_id`);
    if (reference.bus_id !== buses[index].id) {
      fail(
        "audio-package-invalid-manifest",
        `${itemPath}.bus_id`,
        "lane references do not exactly follow package bus order",
      );
    }
    const artifact = artifactRegistry.get(reference.artifact_id);
    if (!artifact) {
      fail(
        "audio-package-invalid-manifest",
        `${itemPath}.artifact_id`,
        "artifact reference is absent from the package registry",
      );
    }
    if (referencedIds.has(reference.artifact_id)) {
      fail(
        "audio-package-invalid-manifest",
        `${itemPath}.artifact_id`,
        "an artifact may belong to only one operating lane",
      );
    }
    referencedIds.add(reference.artifact_id);
    if (sharedFrameCount === 0) {
      sharedFrameCount = artifact.frame_count;
    } else if (sharedFrameCount !== artifact.frame_count) {
      fail(
        "audio-package-invalid-manifest",
        `${itemPath}.artifact_id`,
        "all bus tapes in a lane must share one exact frame clock",
      );
    }
  }
  return sharedFrameCount;
}

function validateBuses(buses) {
  requireArray(buses, "buses", { nonempty: true });
  const busIds = new Set();
  const routeIds = new Set();
  for (let index = 0; index < buses.length; ++index) {
    const path = `buses[${index}]`;
    const bus = requireRecord(
      buses[index],
      ["id", "kind", "disposition", "source_route"],
      path,
    );
    requireSemanticId(bus.id, `${path}.id`);
    if (busIds.has(bus.id)) {
      fail("audio-package-invalid-manifest", `${path}.id`, "bus ID is duplicated");
    }
    busIds.add(bus.id);
    const master = bus.kind === "master_engine_audition";
    const route = bus.kind === "source_route";
    if (!master && !route) {
      fail("audio-package-invalid-manifest", `${path}.kind`, "unsupported bus kind");
    }
    if (master) {
      if (bus.disposition !== "monitor_mix" || bus.source_route !== null) {
        fail(
          "audio-package-invalid-manifest",
          path,
          "master buses must be monitor mixes without a source route",
        );
      }
      continue;
    }
    if (bus.disposition !== "positional_emitter") {
      fail(
        "audio-package-invalid-manifest",
        `${path}.disposition`,
        "source-route buses must be positional emitters",
      );
    }
    const sourceRoute = requireRecord(
      bus.source_route,
      ["kind", "semantic_id", "emitter_anchor_id"],
      `${path}.source_route`,
    );
    if (
      ![
        "exhaust_outlet",
        "intake_inlet",
        "mechanical_engine",
        "mechanical_starter",
      ].includes(sourceRoute.kind)
    ) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.source_route.kind`,
        "unsupported source-route kind",
      );
    }
    requireSemanticId(sourceRoute.semantic_id, `${path}.source_route.semantic_id`);
    requireSemanticId(
      sourceRoute.emitter_anchor_id,
      `${path}.source_route.emitter_anchor_id`,
    );
    if (routeIds.has(sourceRoute.semantic_id)) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.source_route.semantic_id`,
        "source-route semantic ID is duplicated",
      );
    }
    routeIds.add(sourceRoute.semantic_id);
  }
}

function validateAudio(audio) {
  requireRecord(
    audio,
    ["sample_rate", "container", "encoding", "channel_layout"],
    "audio",
  );
  requireRecord(audio.sample_rate, ["numerator", "denominator"], "audio.sample_rate");
  requireRuntimeInteger(audio.sample_rate.numerator, "audio.sample_rate.numerator");
  requireRuntimeInteger(audio.sample_rate.denominator, "audio.sample_rate.denominator");
  if (
    audio.sample_rate.numerator !== ESO_CANONICAL_SAMPLE_RATE ||
    audio.sample_rate.denominator !== 1
  ) {
    fail(
      "audio-package-unsupported-audio-format",
      "audio.sample_rate",
      `must be exactly ${ESO_CANONICAL_SAMPLE_RATE}/1 Hz`,
    );
  }
  if (
    audio.container !== "wav" ||
    audio.encoding !== "float32le" ||
    audio.channel_layout !== "mono"
  ) {
    fail(
      "audio-package-unsupported-audio-format",
      "audio",
      "must be mono Float32 little-endian WAVE",
    );
  }
}

export function validateAudioPackageManifest(manifest) {
  requireRecord(
    manifest,
    ["schema", "identity", "provenance", "audio", "buses", "running", "events", "artifacts"],
    "",
  );
  if (manifest.schema !== AUDIO_PACKAGE_SCHEMA) {
    fail(
      "audio-package-unsupported-schema",
      "schema",
      `must be ${AUDIO_PACKAGE_SCHEMA}`,
    );
  }
  requireRecord(manifest.identity, ["package_id", "engine", "bake_plan"], "identity");
  requireSemanticId(manifest.identity.package_id, "identity.package_id");
  requireContentIdentity(manifest.identity.engine, "identity.engine");
  requireContentIdentity(manifest.identity.bake_plan, "identity.bake_plan");
  requireRecord(
    manifest.provenance,
    ["renderer_build", "source_inputs"],
    "provenance",
  );
  requireContentIdentity(manifest.provenance.renderer_build, "provenance.renderer_build");
  requireContentIdentity(manifest.provenance.source_inputs, "provenance.source_inputs");
  validateAudio(manifest.audio);
  validateBuses(manifest.buses);
  const artifactRegistry = validateArtifacts(manifest.artifacts);

  const running = requireRecord(
    manifest.running,
    [
      "cycle_revolutions",
      "selector_seed",
      "cycle_signal_alignment_frames",
      "rpm_grid",
      "planes",
      "idle",
    ],
    "running",
  );
  if (
    requireRuntimeInteger(
      running.cycle_revolutions,
      "running.cycle_revolutions",
      MAXIMUM_UINT32,
    ) !== 2
  ) {
    fail(
      "audio-package-invalid-manifest",
      "running.cycle_revolutions",
      "four-stroke package units must span exactly two revolutions",
    );
  }
  requireSelectorSeed(running.selector_seed, "running.selector_seed");
  if (
    requireCanonicalFinite(
      running.cycle_signal_alignment_frames,
      "running.cycle_signal_alignment_frames",
    ) < 0
  ) {
    fail(
      "audio-package-invalid-manifest",
      "running.cycle_signal_alignment_frames",
      "must be nonnegative",
    );
  }
  const grid = requireRecord(
    running.rpm_grid,
    [
      "minimum_rpm",
      "playback_minimum_rpm",
      "playback_maximum_rpm",
      "maximum_rpm",
      "spacing_rpm",
      "padding_rows_per_side",
      "neighbor_radius_rows",
      "edge_guard_frames",
      "maximum_assignment_error_rpm",
    ],
    "running.rpm_grid",
  );
  for (const field of [
    "minimum_rpm",
    "playback_minimum_rpm",
    "playback_maximum_rpm",
    "maximum_rpm",
    "spacing_rpm",
  ]) {
    if (requireCanonicalFinite(grid[field], `running.rpm_grid.${field}`) <= 0) {
      fail("audio-package-invalid-manifest", `running.rpm_grid.${field}`, "must be positive");
    }
  }
  if (
    !(
      grid.minimum_rpm < grid.playback_minimum_rpm &&
      grid.playback_minimum_rpm < grid.playback_maximum_rpm &&
      grid.playback_maximum_rpm < grid.maximum_rpm
    )
  ) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid",
      "padded and playback RPM bounds are not strictly ordered",
    );
  }
  for (const field of [
    "padding_rows_per_side",
    "neighbor_radius_rows",
    "edge_guard_frames",
  ]) {
    if (requireRuntimeInteger(grid[field], `running.rpm_grid.${field}`, MAXIMUM_UINT32) === 0) {
      fail("audio-package-invalid-manifest", `running.rpm_grid.${field}`, "must be positive");
    }
  }
  if (grid.padding_rows_per_side < grid.neighbor_radius_rows) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid.padding_rows_per_side",
      "must cover the neighboring-row radius",
    );
  }
  const paddingRpm = grid.spacing_rpm * grid.padding_rows_per_side;
  if (!nearlyEqual(grid.minimum_rpm, grid.playback_minimum_rpm - paddingRpm)) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid.minimum_rpm",
      "does not close over the declared lower padding",
    );
  }
  if (!nearlyEqual(grid.maximum_rpm, grid.playback_maximum_rpm + paddingRpm)) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid.maximum_rpm",
      "does not close over the declared upper padding",
    );
  }
  const maximumError = requireCanonicalFinite(
    grid.maximum_assignment_error_rpm,
    "running.rpm_grid.maximum_assignment_error_rpm",
  );
  if (maximumError < 0 || maximumError > grid.spacing_rpm * 0.5) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid.maximum_assignment_error_rpm",
      "must be between zero and half one grid step",
    );
  }

  requireArray(running.planes, "running.planes");
  if (running.planes.length < 3) {
    fail(
      "audio-package-invalid-manifest",
      "running.planes",
      "normal running requires coast, part-load, and power planes",
    );
  }
  const planeIds = new Set();
  const scenarioIds = new Set();
  const referencedArtifactIds = new Set();
  let gridUnitCount = 0;
  for (let planeIndex = 0; planeIndex < running.planes.length; ++planeIndex) {
    const path = `running.planes[${planeIndex}]`;
    const plane = requireRecord(
      running.planes[planeIndex],
      ["id", "load_coordinate", "direction", "source_scenario", "artifacts", "units"],
      path,
    );
    requireSemanticId(plane.id, `${path}.id`);
    if (planeIds.has(plane.id)) {
      fail("audio-package-invalid-manifest", `${path}.id`, "plane ID is duplicated");
    }
    planeIds.add(plane.id);
    const load = requireCanonicalFinite(plane.load_coordinate, `${path}.load_coordinate`);
    if (load < -1 || load > 1) {
      fail("audio-package-invalid-manifest", `${path}.load_coordinate`, "must be in [-1, 1]");
    }
    if (planeIndex > 0 && running.planes[planeIndex - 1].load_coordinate >= load) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.load_coordinate`,
        "planes must be strictly ordered by load",
      );
    }
    if (planeIndex === 0 && load !== -1) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.load_coordinate`,
        "first plane must own -1 coast",
      );
    }
    if (planeIndex + 1 === running.planes.length && load !== 1) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.load_coordinate`,
        "last plane must own +1 power",
      );
    }
    if (plane.direction !== "rising" && plane.direction !== "falling") {
      fail("audio-package-invalid-manifest", `${path}.direction`, "unsupported running direction");
    }
    requireContentIdentity(plane.source_scenario, `${path}.source_scenario`);
    if (scenarioIds.has(plane.source_scenario.id)) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.source_scenario.id`,
        "each lane must own an independent source scenario",
      );
    }
    scenarioIds.add(plane.source_scenario.id);
    const sourceFrameCount = validateLaneArtifacts(
      plane.artifacts,
      manifest.buses,
      artifactRegistry,
      referencedArtifactIds,
      `${path}.artifacts`,
    );
    validateUnits(
      plane.units,
      sourceFrameCount,
      grid.edge_guard_frames,
      plane.direction,
      `${path}.units`,
    );
    if (planeIndex === 0) {
      gridUnitCount = plane.units.length;
      if (gridUnitCount < 2) {
        fail("audio-package-invalid-manifest", `${path}.units`, "grid requires at least two rows");
      }
    } else if (plane.units.length !== gridUnitCount) {
      fail(
        "audio-package-invalid-manifest",
        `${path}.units`,
        "all planes must share one RPM grid shape",
      );
    }
    for (let unitIndex = 0; unitIndex < plane.units.length; ++unitIndex) {
      const unit = plane.units[unitIndex];
      const unitPath = `${path}.units[${unitIndex}]`;
      const expectedRpm = grid.minimum_rpm + grid.spacing_rpm * unitIndex;
      if (!nearlyEqual(unit.canonical_rpm, expectedRpm)) {
        fail(
          "audio-package-invalid-manifest",
          `${unitPath}.canonical_rpm`,
          "does not occupy the uniform RPM grid",
        );
      }
      if (!nearlyEqual(unit.average_signed_load, load)) {
        fail(
          "audio-package-invalid-manifest",
          `${unitPath}.average_signed_load`,
          "does not equal its plane load coordinate",
        );
      }
      if (Math.abs(unit.measured_rpm - unit.canonical_rpm) > maximumError) {
        fail(
          "audio-package-invalid-manifest",
          `${unitPath}.measured_rpm`,
          "exceeds row assignment error",
        );
      }
      if (planeIndex > 0) {
        const previousPlane = running.planes[planeIndex - 1];
        if (!nearlyEqual(unit.canonical_rpm, running.planes[0].units[unitIndex].canonical_rpm)) {
          fail(
            "audio-package-invalid-manifest",
            `${unitPath}.canonical_rpm`,
            "does not match the shared RPM grid",
          );
        }
        if (previousPlane.units[unitIndex].average_net_torque_nm >= unit.average_net_torque_nm) {
          fail(
            "audio-package-invalid-manifest",
            `${unitPath}.average_net_torque_nm`,
            "must increase with adjacent plane load",
          );
        }
      }
    }
  }
  const representedMaximum = grid.minimum_rpm + grid.spacing_rpm * (gridUnitCount - 1);
  if (!nearlyEqual(representedMaximum, grid.maximum_rpm)) {
    fail(
      "audio-package-invalid-manifest",
      "running.rpm_grid.maximum_rpm",
      "grid bounds, spacing, and row count do not close",
    );
  }

  const idle = requireRecord(
    running.idle,
    ["source_scenario", "artifacts", "units"],
    "running.idle",
  );
  requireContentIdentity(idle.source_scenario, "running.idle.source_scenario");
  if (scenarioIds.has(idle.source_scenario.id)) {
    fail(
      "audio-package-invalid-manifest",
      "running.idle.source_scenario.id",
      "idle must own an independent source scenario",
    );
  }
  const idleFrameCount = validateLaneArtifacts(
    idle.artifacts,
    manifest.buses,
    artifactRegistry,
    referencedArtifactIds,
    "running.idle.artifacts",
  );
  validateUnits(
    idle.units,
    idleFrameCount,
    grid.edge_guard_frames,
    "rising",
    "running.idle.units",
  );
  requireArray(manifest.events, "events");
  if (manifest.events.length !== 0) {
    fail(
      "audio-package-invalid-manifest",
      "events",
      "lifecycle events are outside the current package contract",
    );
  }
  if (referencedArtifactIds.size !== artifactRegistry.size) {
    const dangling = manifest.artifacts.find(
      (artifact) => !referencedArtifactIds.has(artifact.id),
    );
    fail(
      "audio-package-invalid-manifest",
      "artifacts",
      `artifact ${dangling?.id ?? "<unknown>"} is not owned by an operating lane`,
    );
  }
}
