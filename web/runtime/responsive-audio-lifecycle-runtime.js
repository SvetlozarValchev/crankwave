const SCHEMA = "engine-sim-offline/responsive-audio-lifecycle";
const KIND = "responsive-audio-lifecycle";
const SAMPLE_RATE = 192_000;
const BUS_ID = "master.engine.audition";
const FOUR_STROKE_CYCLE_REVOLUTIONS = 2;
const LOW_LOAD_SETTLE_CYCLES = 2;

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

function finite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail(`${label} must be finite`);
  }
  return value;
}

function nonnegativeInteger(value, label) {
  if (!Number.isSafeInteger(value) || value < 0) {
    fail(`${label} must be a nonnegative integer`);
  }
  return value;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("responsive lifecycle manifest URL must be nonempty");
  }
  return new URL(value, globalThis.location?.href ?? "http://localhost/");
}

function relativeUrl(manifestUrl, value, label) {
  const path = string(value, label);
  if (path.startsWith("/") || path.includes("..") || path.includes("\\")) {
    fail(`${label} must stay beneath its manifest`);
  }
  return new URL(path, manifestUrl);
}

async function fetchBytes(url, fetchImplementation, label) {
  const response = await fetchImplementation(url.href, { cache: "no-store" });
  if (!response?.ok || typeof response.arrayBuffer !== "function") {
    throw new Error(
      `${label} fetch failed: HTTP ${response?.status ?? "?"} for ${url.href}`,
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

async function sha256Hex(bytes, cryptoImplementation, label) {
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error("Web Crypto SHA-256 is required for lifecycle loading");
  }
  return hex(
    new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", bytes)),
  );
}

async function requireSha256(bytes, expected, cryptoImplementation, label) {
  string(expected, `${label}.sha256`);
  const digest = await sha256Hex(bytes, cryptoImplementation, label);
  if (digest !== expected) {
    throw new Error(`${label} SHA-256 does not match its manifest`);
  }
}

function decodeJsonObject(bytes, label) {
  return object(
    JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)),
    label,
  );
}

function decodeFloat32Le(bytes, frameCount, label) {
  if (bytes.byteLength !== frameCount * Float32Array.BYTES_PER_ELEMENT) {
    throw new RangeError(`${label} byte count does not match frame_count`);
  }
  const samples = new Float32Array(frameCount);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let frame = 0; frame < frameCount; ++frame) {
    const sample = view.getFloat32(frame * 4, true);
    if (!Number.isFinite(sample)) {
      throw new RangeError(`${label} contains a non-finite sample at ${frame}`);
    }
    samples[frame] = sample;
  }
  return samples;
}

function checkpointMap(value, requiredKinds, label) {
  const result = new Map();
  for (const [index, itemValue] of array(value, label).entries()) {
    const item = object(itemValue, `${label}[${index}]`);
    const kind = string(item.kind, `${label}[${index}].kind`);
    if (result.has(kind)) fail(`${label} repeats checkpoint ${kind}`);
    result.set(kind, Object.freeze({
      kind,
      frame: nonnegativeInteger(item.frame, `${label}[${index}].frame`),
      rpm: finite(item.rpm, `${label}[${index}].rpm`),
      precision: string(item.precision, `${label}[${index}].precision`),
      method: string(item.method, `${label}[${index}].method`),
    }));
  }
  for (const kind of requiredKinds) {
    if (!result.has(kind)) fail(`${label} is missing ${kind}`);
  }
  return result;
}

function seam(value, target, label) {
  const item = object(value, label);
  if (item.target !== target) fail(`${label}.target must be ${target}`);
  const result = Object.freeze({
    sourceFrame: nonnegativeInteger(item.source_frame, `${label}.source_frame`),
    crossfadeFrames: positiveInteger(
      item.crossfade_frames,
      `${label}.crossfade_frames`,
    ),
    sourceRpm: finite(item.source_rpm, `${label}.source_rpm`),
    targetSourceFrame: nonnegativeInteger(
      item.target_source_frame,
      `${label}.target_source_frame`,
    ),
    targetRpm: finite(item.target_rpm, `${label}.target_rpm`),
    correlation: finite(item.correlation, `${label}.correlation`),
    target,
    targetReference: string(item.target_reference, `${label}.target_reference`),
  });
  if (result.correlation < -1 || result.correlation > 1) {
    fail(`${label}.correlation must lie in [-1, 1]`);
  }
  return result;
}

function unitInterval(value, label) {
  const result = finite(value, label);
  if (result < 0 || result > 1) fail(`${label} must lie in [0, 1]`);
  return result;
}

function startupAdmission(value) {
  const item = object(value, "startup_admission");
  if (
    item.schema !== "engine-sim-offline/continuous-startup-admission-v1" ||
    item.blend !== "constant-power" ||
    item.admission_lane_coordinate !== "authored-throttle-01" ||
    item.pre_floor_progress !== "smoothstep-first-fire-rpm-to-running-floor" ||
    item.completion_progress !== "smoothstep-committed-crank-travel" ||
    item.monotone_ownership !== true
  ) {
    fail("startup_admission does not implement the continuous constant-power contract");
  }
  const lanes = array(item.lanes, "startup_admission.lanes").map(
    (laneValue, index) => {
      const lane = object(laneValue, `startup_admission.lanes[${index}]`);
      return Object.freeze({
        id: string(lane.id, `startup_admission.lanes[${index}].id`),
        throttle01: unitInterval(
          lane.throttle_01,
          `startup_admission.lanes[${index}].throttle_01`,
        ),
        floorRunningGainLinear: unitInterval(
          lane.floor_running_gain_linear,
          `startup_admission.lanes[${index}].floor_running_gain_linear`,
        ),
      });
    },
  );
  if (lanes.length < 2 || new Set(lanes.map(({ id }) => id)).size !== lanes.length) {
    fail("startup_admission requires at least two uniquely named load lanes");
  }
  for (let index = 1; index < lanes.length; ++index) {
    if (!(lanes[index].throttle01 > lanes[index - 1].throttle01)) {
      fail("startup_admission lane throttle coordinates must increase strictly");
    }
  }
  if (lanes[0].throttle01 !== 0 || lanes.at(-1).throttle01 !== 1) {
    fail("startup_admission lane coordinates must span closed throttle through WOT");
  }
  const coastValue = object(
    item.coast_stability,
    "startup_admission.coast_stability",
  );
  const coastLaneId = string(
    coastValue.lane_id,
    "startup_admission.coast_stability.lane_id",
  );
  if (!lanes.some(({ id }) => id === coastLaneId)) {
    fail("startup_admission coast stability lane is absent");
  }
  if (
    coastValue.requires_starter_released !== true ||
    coastValue.clock_law !== "lane-weighted-post-peak-admission"
  ) {
    fail("startup_admission coast stability contract is invalid");
  }
  const evidence = object(item.evidence, "startup_admission.evidence");
  return Object.freeze({
    schema: item.schema,
    runningBedLoadCoordinate: string(
      item.running_bed_load_coordinate,
      "startup_admission.running_bed_load_coordinate",
    ),
    admissionLaneCoordinate: item.admission_lane_coordinate,
    blend: item.blend,
    preFloorProgress: item.pre_floor_progress,
    completionProgress: item.completion_progress,
    monotoneOwnership: true,
    completionCrankTravelRevolutions: finite(
      item.completion_crank_travel_revolutions,
      "startup_admission.completion_crank_travel_revolutions",
    ),
    lanes: Object.freeze(lanes),
    coastStability: Object.freeze({
      laneId: coastLaneId,
      requiresStarterReleased: true,
      postPeakCrankTravelRevolutions: finite(
        coastValue.post_peak_crank_travel_revolutions,
        "startup_admission.coast_stability.post_peak_crank_travel_revolutions",
      ),
      clockLaw: coastValue.clock_law,
    }),
    evidence: Object.freeze({
      method: string(evidence.method, "startup_admission.evidence.method"),
      path: string(evidence.path, "startup_admission.evidence.path"),
      sha256: string(evidence.sha256, "startup_admission.evidence.sha256"),
      correctedHeldManifestSha256: string(
        evidence.corrected_held_manifest_sha256,
        "startup_admission.evidence.corrected_held_manifest_sha256",
      ),
    }),
  });
}

async function artifact(
  value,
  manifestUrl,
  fetchImplementation,
  cryptoImplementation,
  label,
) {
  const item = object(value, label);
  const frameCount = positiveInteger(item.frame_count, `${label}.frame_count`);
  const url = relativeUrl(manifestUrl, item.path, `${label}.path`);
  const bytes = await fetchBytes(url, fetchImplementation, label);
  await requireSha256(bytes, item.sha256, cryptoImplementation, label);
  return Object.freeze({
    url: url.href,
    path: item.path,
    sha256: item.sha256,
    frameCount,
    samples: decodeFloat32Le(bytes, frameCount, label),
  });
}

export async function loadResponsiveAudioLifecycleRuntime(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("responsive lifecycle loading requires fetch");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifestBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "responsive lifecycle manifest",
  );
  const manifestSha256 = await sha256Hex(
    manifestBytes,
    cryptoImplementation,
    "responsive lifecycle manifest",
  );
  const manifest = decodeJsonObject(manifestBytes, "manifest");
  if (manifest.schema !== SCHEMA) fail(`unsupported lifecycle schema ${manifest.schema}`);
  string(manifest.id, "manifest.id");
  string(manifest.engine, "manifest.engine");
  const audio = object(manifest.audio, "manifest.audio");
  if (
    audio.sample_rate_hz !== SAMPLE_RATE ||
    audio.encoding !== "float32le" ||
    audio.channel_layout !== "mono" ||
    audio.bus_id !== BUS_ID
  ) {
    fail("lifecycle audio must be the canonical mono 192 kHz audition master");
  }

  const starterValue = object(manifest.starter, "manifest.starter");
  const startupValue = object(manifest.startup, "manifest.startup");
  const shutdownValue = object(manifest.shutdown, "manifest.shutdown");
  const [starterArtifact, startupArtifact, shutdownArtifact] = await Promise.all([
    artifact(
      starterValue.artifact,
      manifestUrl,
      fetchImplementation,
      cryptoImplementation,
      "starter.artifact",
    ),
    artifact(
      startupValue.artifact,
      manifestUrl,
      fetchImplementation,
      cryptoImplementation,
      "startup.artifact",
    ),
    artifact(
      shutdownValue.artifact,
      manifestUrl,
      fetchImplementation,
      cryptoImplementation,
      "shutdown.artifact",
    ),
  ]);

  const starter = Object.freeze({
    artifact: starterArtifact,
    meanCrankRpm: finite(starterValue.mean_crank_rpm, "starter.mean_crank_rpm"),
    referenceRpm: finite(starterValue.reference_rpm, "starter.reference_rpm"),
    loopStartFrame: nonnegativeInteger(
      starterValue.loop_start_frame,
      "starter.loop_start_frame",
    ),
    loopEndFrame: positiveInteger(
      starterValue.loop_end_frame,
      "starter.loop_end_frame",
    ),
    crossfadeFrames: positiveInteger(
      starterValue.crossfade_frames,
      "starter.crossfade_frames",
    ),
    attackFadeFrames: positiveInteger(
      starterValue.attack_fade_frames,
      "starter.attack_fade_frames",
    ),
    releaseFadeFrames: positiveInteger(
      starterValue.release_fade_frames,
      "starter.release_fade_frames",
    ),
  });
  if (
    starter.meanCrankRpm <= 0 ||
    starter.referenceRpm <= 0 ||
    starter.loopStartFrame >= starter.loopEndFrame ||
    starter.loopEndFrame > starter.artifact.frameCount ||
    starter.crossfadeFrames * 2 >=
      starter.loopEndFrame - starter.loopStartFrame
  ) {
    fail("starter loop geometry is invalid");
  }

  const startupCheckpoints = checkpointMap(
    startupValue.checkpoints,
    ["ignition-on", "first-combustion", "starter-release", "running-floor"],
    "startup.checkpoints",
  );
  const startup = Object.freeze({
    artifact: startupArtifact,
    checkpoints: startupCheckpoints,
    entry: seam(startupValue.entry, "starter", "startup.entry"),
    exit: seam(startupValue.exit, "running", "startup.exit"),
  });
  const admission = startupAdmission(manifest.startup_admission);
  const admissionEvidenceUrl = relativeUrl(
    manifestUrl,
    admission.evidence.path,
    "startup_admission.evidence.path",
  );
  const admissionEvidenceBytes = await fetchBytes(
    admissionEvidenceUrl,
    fetchImplementation,
    "startup admission evidence",
  );
  await requireSha256(
    admissionEvidenceBytes,
    admission.evidence.sha256,
    cryptoImplementation,
    "startup admission evidence",
  );
  const admissionEvidenceDocument = decodeJsonObject(
    admissionEvidenceBytes,
    "startup admission evidence",
  );
  if (
    admissionEvidenceDocument.schema !==
      "engine-sim-offline/startup-admission-floor-evidence" ||
    admissionEvidenceDocument.atlas_load_coordinate !==
      admission.runningBedLoadCoordinate ||
    !(admissionEvidenceDocument.running_floor_rpm > 0)
  ) {
    fail("startup admission evidence does not match its authored load contract");
  }
  const loadedAdmission = Object.freeze({
    ...admission,
    evidence: Object.freeze({
      ...admission.evidence,
      url: admissionEvidenceUrl.href,
      document: admissionEvidenceDocument,
    }),
  });
  if (
    !(admission.completionCrankTravelRevolutions > 0) ||
    !(admission.coastStability.postPeakCrankTravelRevolutions > 0)
  ) {
    fail("startup_admission crank travel values must be positive");
  }
  if (
    startup.entry.sourceFrame >= startupCheckpoints.get("first-combustion").frame ||
    startupCheckpoints.get("first-combustion").frame >= startup.exit.sourceFrame ||
    startup.exit.sourceFrame + startup.exit.crossfadeFrames >
      startup.artifact.frameCount
  ) {
    fail("startup checkpoint/seam ordering is invalid");
  }

  const shutdownCheckpoints = checkpointMap(
    shutdownValue.checkpoints,
    ["settled-idle", "ignition-off", "engine-stopped"],
    "shutdown.checkpoints",
  );
  const shutdown = Object.freeze({
    artifact: shutdownArtifact,
    checkpoints: shutdownCheckpoints,
    entry: seam(shutdownValue.entry, "running", "shutdown.entry"),
    silenceFrame: nonnegativeInteger(
      shutdownValue.silence_frame,
      "shutdown.silence_frame",
    ),
    exitFadeFrames: positiveInteger(
      shutdownValue.exit_fade_frames,
      "shutdown.exit_fade_frames",
    ),
    quietTailFrames: nonnegativeInteger(
      shutdownValue.quiet_tail_frames,
      "shutdown.quiet_tail_frames",
    ),
    quietPeakThreshold: finite(
      shutdownValue.quiet_peak_threshold,
      "shutdown.quiet_peak_threshold",
    ),
    quietRmsThreshold: finite(
      shutdownValue.quiet_rms_threshold,
      "shutdown.quiet_rms_threshold",
    ),
  });
  if (
    shutdown.entry.sourceFrame > shutdownCheckpoints.get("ignition-off").frame ||
    shutdownCheckpoints.get("ignition-off").frame >= shutdown.silenceFrame ||
    shutdown.silenceFrame > shutdown.artifact.frameCount
  ) {
    fail("shutdown checkpoint/seam ordering is invalid");
  }

  const provenance = object(manifest.provenance, "manifest.provenance");
  object(provenance.engine, "manifest.provenance.engine");
  object(provenance.renderer_build, "manifest.provenance.renderer_build");
  if (
    provenance.engine.id !== manifest.engine ||
    provenance.physics_rate_hz !== 10_000 ||
    provenance.delivery_rate_hz !== SAMPLE_RATE
  ) {
    fail("lifecycle provenance does not describe its engine and canonical rates");
  }
  const seenScenarioRoles = new Set();
  const provenanceSources = await Promise.all(
    array(provenance.scenarios, "manifest.provenance.scenarios").map(
      async (scenarioValue, index) => {
        const label = `manifest.provenance.scenarios[${index}]`;
        const scenario = object(scenarioValue, label);
        const role = string(scenario.role, `${label}.role`);
        if (seenScenarioRoles.has(role)) {
          fail(`manifest.provenance.scenarios repeats role ${role}`);
        }
        seenScenarioRoles.add(role);
        const scenarioUrl = relativeUrl(manifestUrl, scenario.path, `${label}.path`);
        const evidenceUrl = relativeUrl(
          manifestUrl,
          scenario.evidence_path,
          `${label}.evidence_path`,
        );
        const [scenarioBytes, evidenceBytes] = await Promise.all([
          fetchBytes(scenarioUrl, fetchImplementation, `${label} scenario`),
          fetchBytes(evidenceUrl, fetchImplementation, `${label} evidence`),
        ]);
        await Promise.all([
          requireSha256(
            scenarioBytes,
            scenario.sha256,
            cryptoImplementation,
            `${label} scenario`,
          ),
          requireSha256(
            evidenceBytes,
            scenario.evidence_sha256,
            cryptoImplementation,
            `${label} evidence`,
          ),
        ]);
        const scenarioDocument = decodeJsonObject(
          scenarioBytes,
          `${label} scenario`,
        );
        const evidenceDocument = decodeJsonObject(
          evidenceBytes,
          `${label} evidence`,
        );
        if (
          scenarioDocument.schema !== "engine-sim-offline/scenario" ||
          evidenceDocument.schema !==
            "engine-sim-offline/lifecycle-capture-evidence"
        ) {
          fail(`${label} documents use unsupported schemas`);
        }
        return Object.freeze({
          role,
          scenarioUrl: scenarioUrl.href,
          evidenceUrl: evidenceUrl.href,
          scenario: scenarioDocument,
          evidence: evidenceDocument,
        });
      },
    ),
  );
  return Object.freeze({
    kind: KIND,
    manifestUrl: manifestUrl.href,
    manifestSha256,
    manifest,
    sampleRate: SAMPLE_RATE,
    busId: BUS_ID,
    engine: manifest.engine,
    starter,
    startup,
    startupAdmission: loadedAdmission,
    shutdown,
    provenanceSources: Object.freeze(provenanceSources),
  });
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function smoothstep(value) {
  const amount = clamp(value, 0, 1);
  return amount * amount * (3 - 2 * amount);
}

function sampleLinear(samples, position) {
  const left = Math.floor(position);
  const amount = position - left;
  const leftSample = samples[Math.max(0, Math.min(samples.length - 1, left))] ?? 0;
  const rightSample = samples[
    Math.max(0, Math.min(samples.length - 1, left + 1))
  ] ?? leftSample;
  return leftSample + (rightSample - leftSample) * amount;
}

function laneWeights(value, label) {
  const seen = new Set();
  const result = array(value, label).map((entryValue, index) => {
    const entry = object(entryValue, `${label}[${index}]`);
    const id = string(entry.id, `${label}[${index}].id`);
    if (seen.has(id)) fail(`${label} repeats lane ${id}`);
    seen.add(id);
    return Object.freeze({
      id,
      weight: unitInterval(entry.weight, `${label}[${index}].weight`),
    });
  });
  const total = result.reduce((sum, { weight }) => sum + weight, 0);
  if (Math.abs(total - 1) > 1e-9) fail(`${label} must sum to one`);
  return Object.freeze(result);
}

function acceptedState(value) {
  object(value, "lifecycle state");
  if (!Number.isSafeInteger(value.frame) || value.frame < 0) {
    fail("lifecycle state.frame must be a nonnegative safe integer");
  }
  if (
    typeof value.starter !== "boolean" ||
    typeof value.ignition !== "boolean" ||
    typeof value.fuel !== "boolean" ||
    typeof value.runningBedReady !== "boolean"
  ) {
    fail("lifecycle state flags must be boolean");
  }
  const torque = value.indicatedGasTorqueNm;
  if (torque !== null && !Number.isFinite(torque)) {
    fail("lifecycle indicatedGasTorqueNm must be finite or null");
  }
  const requestedThrottle01 = finite(
    value.requestedThrottle01,
    "lifecycle state.requestedThrottle01",
  );
  if (requestedThrottle01 < 0 || requestedThrottle01 > 1) {
    fail("lifecycle state.requestedThrottle01 must lie in [0, 1]");
  }
  const manifoldPressurePaAbs = finite(
    value.manifoldPressurePaAbs,
    "lifecycle state.manifoldPressurePaAbs",
  );
  if (manifoldPressurePaAbs <= 0) {
    fail("lifecycle state.manifoldPressurePaAbs must be positive");
  }
  return Object.freeze({
    frame: value.frame,
    starter: value.starter,
    ignition: value.ignition,
    fuel: value.fuel,
    runningBedReady: value.runningBedReady,
    rpm: finite(value.rpm, "lifecycle state.rpm"),
    requestedThrottle01,
    manifoldPressurePaAbs,
    rpmSlopeRpmPerSecond: finite(
      value.rpmSlopeRpmPerSecond,
      "lifecycle state.rpmSlopeRpmPerSecond",
    ),
    admissionLaneWeights: laneWeights(
      value.admissionLaneWeights,
      "lifecycle state.admissionLaneWeights",
    ),
    runningBedLaneWeights: laneWeights(
      value.runningBedLaneWeights,
      "lifecycle state.runningBedLaneWeights",
    ),
    unwrappedCrankRevolutions: finite(
      value.unwrappedCrankRevolutions,
      "lifecycle state.unwrappedCrankRevolutions",
    ),
    indicatedGasTorqueNm: torque,
  });
}

function mono(value, label) {
  if (!(value instanceof Float32Array)) fail(`${label} must be a Float32Array`);
  return value;
}

export class ResponsiveAudioLifecycleCursor {
  #package;
  #startupAdmission;
  #audioLatencyFrames;
  #runningFloorRpm;
  #heldAnchorFloorRpm;
  #atlasLoadLanes;
  #atlasLoadCoordinate;
  #generation = 0;
  #scheduledStates = [];
  #scheduledAudibleStates = [];
  #lastScheduledFrame = -1;
  #lastInputState = null;
  #inputMode = "stopped";
  #events = [];
  #nextEventId = 1;
  #outputFrame = null;
  #state = Object.freeze({
    frame: 0,
    starter: false,
    ignition: false,
    fuel: false,
    runningBedReady: false,
    rpm: 0,
    requestedThrottle01: 0,
    manifoldPressurePaAbs: 1,
    rpmSlopeRpmPerSecond: 0,
    admissionLaneWeights: Object.freeze([]),
    runningBedLaneWeights: Object.freeze([]),
    startupAdmissionProgress: 0,
    unwrappedCrankRevolutions: 0,
    indicatedGasTorqueNm: null,
  });
  #audibleState = null;
  #outputMode = "stopped";
  #activeEvent = null;
  #starterPosition = 0;
  #starterGain = 0;
  #startupCount = 0;
  #shutdownCount = 0;
  #lateStartFrames = 0;
  #renderedFrames = 0;
  #physicalFirstFireFrame = -1;
  #audibleFirstFireFrame = -1;
  #physicalRunningFloorFrame = -1;
  #audibleHandoffStartFrame = -1;
  #audibleHandoffEndFrame = -1;
  #handoffRpm = null;
  #handoffReason = null;
  #handoffStartCrankRevolutions = null;
  #handoffEndCrankRevolutions = null;
  #firstFireRpm = null;
  #admissionProgress = 0;
  #floorCrankRevolutions = null;
  #completionCrankTravelRevolutions = 0;
  #lastAdmissionCrankRevolutions = null;
  #coastStable = false;
  #lastAdmissionLaneWeights = null;
  #lastRunningBedLaneWeights = null;
  #lastFloorRunningGainLinear = 0;
  #peakPostFireRpm = 0;
  #peakPostFireCrankRevolutions = null;

  constructor(
    package_,
    {
      audioLatencyFrames = 0,
      runningFloorRpm,
      heldAnchorFloorRpm,
      atlasLoadLanes,
      atlasLoadCoordinate,
    } = {},
  ) {
    if (package_?.kind !== KIND || package_.sampleRate !== SAMPLE_RATE) {
      fail("ResponsiveAudioLifecycleCursor requires a verified lifecycle package");
    }
    this.#package = package_;
    this.#audioLatencyFrames = nonnegativeInteger(
      audioLatencyFrames,
      "lifecycle audioLatencyFrames",
    );
    this.#runningFloorRpm = finite(
      runningFloorRpm,
      "lifecycle runningFloorRpm",
    );
    if (this.#runningFloorRpm <= 0) {
      fail("lifecycle runningFloorRpm must be positive");
    }
    this.#heldAnchorFloorRpm = finite(
      heldAnchorFloorRpm,
      "lifecycle heldAnchorFloorRpm",
    );
    if (this.#heldAnchorFloorRpm < this.#runningFloorRpm) {
      fail("lifecycle heldAnchorFloorRpm must not precede the composed running floor");
    }
    this.#startupAdmission = package_.startupAdmission;
    this.#atlasLoadLanes = array(
      atlasLoadLanes,
      "lifecycle atlasLoadLanes",
    ).map((laneValue, index) => {
      const lane = object(laneValue, `lifecycle atlasLoadLanes[${index}]`);
      return Object.freeze({
        id: string(lane.id, `lifecycle atlasLoadLanes[${index}].id`),
        throttle01: unitInterval(
          lane.throttle01,
          `lifecycle atlasLoadLanes[${index}].throttle01`,
        ),
      });
    });
    this.#atlasLoadCoordinate = string(
      atlasLoadCoordinate,
      "lifecycle atlasLoadCoordinate",
    );
    if (
      this.#atlasLoadCoordinate !==
        this.#startupAdmission.runningBedLoadCoordinate ||
      this.#atlasLoadLanes.length !== this.#startupAdmission.lanes.length ||
      this.#atlasLoadLanes.some((lane, index) =>
        lane.id !== this.#startupAdmission.lanes[index].id ||
        lane.throttle01 !== this.#startupAdmission.lanes[index].throttle01
      )
    ) {
      fail("lifecycle startup admission does not match the loaded held atlas lanes");
    }
  }

  reset(generation = this.#generation + 1) {
    if (!Number.isSafeInteger(generation) || generation < 0) {
      fail("lifecycle generation must be a nonnegative safe integer");
    }
    this.#generation = generation;
    this.#scheduledStates.length = 0;
    this.#scheduledAudibleStates.length = 0;
    this.#lastScheduledFrame = -1;
    this.#lastInputState = null;
    this.#inputMode = "stopped";
    this.#events.length = 0;
    this.#nextEventId = 1;
    this.#outputFrame = null;
    this.#state = Object.freeze({
      frame: 0,
      starter: false,
      ignition: false,
      fuel: false,
      runningBedReady: false,
      rpm: 0,
      requestedThrottle01: 0,
      manifoldPressurePaAbs: 1,
      rpmSlopeRpmPerSecond: 0,
      admissionLaneWeights: Object.freeze([]),
      runningBedLaneWeights: Object.freeze([]),
      startupAdmissionProgress: 0,
      unwrappedCrankRevolutions: 0,
      indicatedGasTorqueNm: null,
    });
    this.#audibleState = null;
    this.#outputMode = "stopped";
    this.#activeEvent = null;
    this.#starterPosition = 0;
    this.#starterGain = 0;
    this.#startupCount = 0;
    this.#shutdownCount = 0;
    this.#lateStartFrames = 0;
    this.#renderedFrames = 0;
    this.#physicalFirstFireFrame = -1;
    this.#audibleFirstFireFrame = -1;
    this.#physicalRunningFloorFrame = -1;
    this.#audibleHandoffStartFrame = -1;
    this.#audibleHandoffEndFrame = -1;
    this.#handoffRpm = null;
    this.#handoffReason = null;
    this.#handoffStartCrankRevolutions = null;
    this.#handoffEndCrankRevolutions = null;
    this.#firstFireRpm = null;
    this.#admissionProgress = 0;
    this.#floorCrankRevolutions = null;
    this.#completionCrankTravelRevolutions = 0;
    this.#lastAdmissionCrankRevolutions = null;
    this.#coastStable = false;
    this.#lastAdmissionLaneWeights = null;
    this.#lastRunningBedLaneWeights = null;
    this.#lastFloorRunningGainLinear = 0;
    this.#peakPostFireRpm = 0;
    this.#peakPostFireCrankRevolutions = null;
  }

  setState(value) {
    const state = acceptedState(value);
    if (state.frame < this.#lastScheduledFrame) {
      fail("lifecycle states must be scheduled in nondecreasing frame order");
    }
    if (this.#outputFrame !== null && state.frame < this.#outputFrame) {
      fail("lifecycle state arrived after its output frame was rendered");
    }
    this.#validateLaneWeights(state);
    const previous = this.#lastInputState;
    if (this.#outputFrame === null) this.#outputFrame = state.frame;

    if (previous === null) {
      if (state.starter) this.#inputMode = "cranking";
      else if (state.ignition && state.fuel && Math.abs(state.rpm) > 1) {
        this.#inputMode = "running";
      }
    } else {
      const starterRose = !previous.starter && state.starter;
      const starterFell = previous.starter && !state.starter;
      const combustionFell =
        previous.ignition && previous.fuel && !(state.ignition && state.fuel);
      const firstPositiveCombustion =
        this.#inputMode === "cranking" &&
        state.ignition &&
        state.fuel &&
        state.indicatedGasTorqueNm !== null &&
        state.indicatedGasTorqueNm > 0;
      if (starterRose) {
        this.#cancelEventsAt(state.frame);
        this.#inputMode = "cranking";
      }
      if (firstPositiveCombustion) {
        this.#physicalFirstFireFrame = state.frame;
        this.#audibleFirstFireFrame = this.#audibleFrame(state.frame);
        this.#firstFireRpm = Math.abs(state.rpm);
        this.#admissionProgress = 0;
        this.#floorCrankRevolutions = null;
        this.#completionCrankTravelRevolutions = 0;
        this.#lastAdmissionCrankRevolutions = state.unwrappedCrankRevolutions;
        this.#coastStable = false;
        this.#scheduleEvent("startup", this.#audibleFirstFireFrame);
        this.#inputMode = "running";
      } else if (starterFell && this.#inputMode === "cranking") {
        this.#inputMode = "stopped";
      }
      if (
        combustionFell &&
        ["running", "startup"].includes(this.#inputMode) &&
        Math.abs(previous.rpm) > 1
      ) {
        this.#scheduleEvent("shutdown", state.frame);
        this.#inputMode = "shutdown";
      }
      if (
        this.#physicalFirstFireFrame >= 0 &&
        state.ignition &&
        state.fuel &&
        Math.abs(state.rpm) >= this.#peakPostFireRpm
      ) {
        this.#peakPostFireRpm = Math.abs(state.rpm);
        this.#peakPostFireCrankRevolutions = state.unwrappedCrankRevolutions;
      }
      if (
        this.#physicalFirstFireFrame >= 0 &&
        state.runningBedReady &&
        state.ignition &&
        state.fuel
      ) {
        this.#advanceStartupAdmission(state, previous);
      }
    }
    const scheduledState = Object.freeze({
      ...state,
      startupAdmissionProgress: this.#admissionProgress,
    });
    this.#scheduledStates.push(scheduledState);
    this.#scheduledAudibleStates.push(Object.freeze({
      ...scheduledState,
      frame: this.#audibleFrame(state.frame),
    }));
    this.#lastScheduledFrame = state.frame;
    this.#lastInputState = state;
  }

  #audibleFrame(physicalFrame) {
    if (physicalFrame > Number.MAX_SAFE_INTEGER - this.#audioLatencyFrames) {
      fail("lifecycle audible frame overflowed");
    }
    return physicalFrame + this.#audioLatencyFrames;
  }

  #validateLaneWeights(state) {
    for (const [label, weights] of [
      ["admission", state.admissionLaneWeights],
      ["running-bed", state.runningBedLaneWeights],
    ]) {
      if (
        weights.length !== this.#atlasLoadLanes.length ||
        weights.some((entry, index) =>
          entry.id !== this.#atlasLoadLanes[index].id
        )
      ) {
        fail(`lifecycle ${label} lane weights disagree with the held atlas`);
      }
    }
  }

  #floorRunningGain(state) {
    let result = 0;
    for (let index = 0; index < this.#startupAdmission.lanes.length; ++index) {
      result +=
        state.admissionLaneWeights[index].weight *
        this.#startupAdmission.lanes[index].floorRunningGainLinear;
    }
    return clamp(result, 0, 1);
  }

  #advanceStartupAdmission(state, previous) {
    if (this.#admissionProgress >= 1) return;
    const event = this.#events.findLast((candidate) =>
      candidate.kind === "startup" && candidate.cancelFrame === null
    );
    if (event === undefined) {
      fail("lifecycle admission advanced without an active first-fire event");
    }
    const rpm = Math.abs(state.rpm);
    const floorGain = this.#floorRunningGain(state);
    this.#lastAdmissionLaneWeights = state.admissionLaneWeights;
    this.#lastRunningBedLaneWeights = state.runningBedLaneWeights;
    this.#lastFloorRunningGainLinear = floorGain;
    let candidateProgress = this.#admissionProgress;

    if (rpm < this.#runningFloorRpm || this.#floorCrankRevolutions === null) {
      const firstFireRpm = Math.min(
        this.#runningFloorRpm,
        this.#firstFireRpm ?? rpm,
      );
      const preFloorAmount = smoothstep(
        (rpm - firstFireRpm) /
          Math.max(1e-9, this.#runningFloorRpm - firstFireRpm),
      );
      const runningGain = floorGain * preFloorAmount;
      candidateProgress = runningGain * runningGain;
    }

    if (rpm >= this.#runningFloorRpm && this.#floorCrankRevolutions === null) {
      const previousRpm = Math.abs(previous?.rpm ?? rpm);
      const amount = previousRpm < this.#runningFloorRpm && rpm > previousRpm
        ? clamp(
          (this.#runningFloorRpm - previousRpm) / (rpm - previousRpm),
          0,
          1,
        )
        : 1;
      const priorCrank = previous?.unwrappedCrankRevolutions ??
        state.unwrappedCrankRevolutions;
      const priorFrame = previous?.frame ?? state.frame;
      this.#floorCrankRevolutions = priorCrank +
        (state.unwrappedCrankRevolutions - priorCrank) * amount;
      this.#lastAdmissionCrankRevolutions = this.#floorCrankRevolutions;
      this.#physicalRunningFloorFrame = Math.round(
        priorFrame + (state.frame - priorFrame) * amount,
      );
      this.#handoffRpm = this.#runningFloorRpm;
    }

    if (this.#floorCrankRevolutions !== null) {
      const coast = this.#startupAdmission.coastStability;
      const coastIndex = this.#startupAdmission.lanes.findIndex(
        ({ id }) => id === coast.laneId,
      );
      if (
        !this.#coastStable &&
        !state.starter &&
        this.#peakPostFireCrankRevolutions !== null &&
        Math.abs(
          state.unwrappedCrankRevolutions -
            this.#peakPostFireCrankRevolutions,
        ) >= coast.postPeakCrankTravelRevolutions
      ) {
        this.#coastStable = true;
      }
      const coastWeight = state.admissionLaneWeights[coastIndex].weight;
      const clockScale = 1 - coastWeight + (this.#coastStable ? coastWeight : 0);
      const priorCrank = this.#lastAdmissionCrankRevolutions ??
        this.#floorCrankRevolutions;
      const crankDelta = Math.abs(state.unwrappedCrankRevolutions - priorCrank);
      this.#completionCrankTravelRevolutions = Math.min(
        this.#startupAdmission.completionCrankTravelRevolutions,
        this.#completionCrankTravelRevolutions + crankDelta * clockScale,
      );
      this.#lastAdmissionCrankRevolutions = state.unwrappedCrankRevolutions;
      const completionAmount = smoothstep(
        this.#completionCrankTravelRevolutions /
          this.#startupAdmission.completionCrankTravelRevolutions,
      );
      const floorProgress = floorGain * floorGain;
      candidateProgress = Math.max(
        candidateProgress,
        floorProgress + (1 - floorProgress) * completionAmount,
      );
    }

    const priorProgress = this.#admissionProgress;
    this.#admissionProgress = Math.max(
      priorProgress,
      clamp(candidateProgress, 0, 1),
    );
    if (priorProgress === 0 && this.#admissionProgress > 0) {
      const handoffStartFrame = this.#audibleFrame(state.frame);
      event.handoffStartFrame = handoffStartFrame;
      event.handoffStartCrankRevolutions = state.unwrappedCrankRevolutions;
      this.#audibleHandoffStartFrame = handoffStartFrame;
      this.#handoffReason = "continuous-authored-load-lane-constant-power";
      this.#handoffStartCrankRevolutions = state.unwrappedCrankRevolutions;
    }
  }

  #cancelEventsAt(frame) {
    for (const event of this.#events) {
      if (event.cancelFrame === null || event.cancelFrame > frame) {
        event.cancelFrame = frame;
      }
    }
  }

  #scheduleEvent(kind, triggerFrame) {
    const performance = this.#package[kind];
    const checkpointKind = kind === "startup" ? "first-combustion" : "ignition-off";
    const checkpoint = performance.checkpoints.get(checkpointKind);
    const desiredStart = triggerFrame - (checkpoint.frame - performance.entry.sourceFrame);
    const earliest = this.#outputFrame ?? 0;
    const startFrame = Math.max(0, desiredStart, earliest);
    const skippedFrames = startFrame - desiredStart;
    this.#lateStartFrames += skippedFrames;
    const naturalEndSourceFrame = performance.silenceFrame;
    const event = {
      id: this.#nextEventId++,
      kind,
      triggerFrame,
      startFrame,
      sourceFrameAtStart: performance.entry.sourceFrame + skippedFrames,
      entryFadeFrames: Math.max(
        1,
        Math.min(
          performance.entry.crossfadeFrames,
          Math.max(1, triggerFrame - startFrame),
        ),
      ),
      endFrame: kind === "startup"
        ? Number.MAX_SAFE_INTEGER
        : startFrame + Math.max(
          0,
          naturalEndSourceFrame - performance.entry.sourceFrame - skippedFrames,
        ),
      handoffStartFrame: null,
      handoffEndFrame: null,
      handoffStartCrankRevolutions: null,
      cancelFrame: null,
    };
    this.#events.push(event);
    this.#events.sort((left, right) => left.startFrame - right.startFrame);
    if (kind === "startup") ++this.#startupCount;
    else ++this.#shutdownCount;
  }

  #applyStates(frame) {
    let consumed = 0;
    while (
      consumed < this.#scheduledStates.length &&
      this.#scheduledStates[consumed].frame <= frame
    ) {
      const prior = this.#state;
      this.#state = this.#scheduledStates[consumed];
      if (!prior.starter && this.#state.starter && this.#outputMode === "stopped") {
        this.#outputMode = "cranking";
        this.#starterPosition = 0;
      } else if (
        prior.starter &&
        !this.#state.starter &&
        this.#outputMode === "cranking"
      ) {
        this.#outputMode = "stopped";
      } else if (
        this.#outputMode === "stopped" &&
        this.#state.ignition &&
        this.#state.fuel &&
        !this.#state.starter &&
        Math.abs(this.#state.rpm) > 1
      ) {
        this.#outputMode = "running";
      }
      ++consumed;
    }
    if (consumed > 0) this.#scheduledStates.splice(0, consumed);
  }

  #applyAudibleStates(frame) {
    let consumed = 0;
    while (
      consumed < this.#scheduledAudibleStates.length &&
      this.#scheduledAudibleStates[consumed].frame <= frame
    ) {
      this.#audibleState = this.#scheduledAudibleStates[consumed];
      ++consumed;
    }
    if (consumed > 0) this.#scheduledAudibleStates.splice(0, consumed);
  }

  #audibleCrankRevolutionsAt(frame) {
    const current = this.#audibleState;
    if (current === null) return null;
    const next = this.#scheduledAudibleStates[0] ?? null;
    if (next === null || next.frame <= current.frame || frame >= next.frame) {
      return current.unwrappedCrankRevolutions;
    }
    const amount = clamp(
      (frame - current.frame) / (next.frame - current.frame),
      0,
      1,
    );
    return current.unwrappedCrankRevolutions +
      (next.unwrappedCrankRevolutions - current.unwrappedCrankRevolutions) *
        amount;
  }

  #audibleAdmissionProgressAt(frame) {
    const current = this.#audibleState;
    if (current === null) return 0;
    const currentProgress = current.startupAdmissionProgress ?? 0;
    const next = this.#scheduledAudibleStates[0] ?? null;
    if (next === null || next.frame <= current.frame || frame >= next.frame) {
      return currentProgress;
    }
    const amount = clamp(
      (frame - current.frame) / (next.frame - current.frame),
      0,
      1,
    );
    return clamp(
      currentProgress +
        ((next.startupAdmissionProgress ?? currentProgress) - currentProgress) *
          amount,
      0,
      1,
    );
  }

  #eventAt(frame) {
    let result = null;
    for (const event of this.#events) {
      const end = event.cancelFrame === null
        ? event.endFrame
        : Math.min(event.endFrame, event.cancelFrame);
      if (event.startFrame <= frame && frame < end) result = event;
    }
    return result;
  }

  #updateEvent(frame) {
    const event = this.#eventAt(frame);
    if (event?.id === this.#activeEvent?.id) return event;
    if (this.#activeEvent !== null && event === null) {
      this.#outputMode = this.#activeEvent.kind === "startup" ? "running" : "stopped";
    }
    if (event !== null) this.#outputMode = event.kind;
    this.#activeEvent = event;
    return event;
  }

  #starterSample() {
    const starter = this.#package.starter;
    const samples = starter.artifact.samples;
    const seamStart = starter.loopEndFrame - starter.crossfadeFrames;
    let sample;
    if (
      this.#starterPosition >= seamStart &&
      this.#starterPosition < starter.loopEndFrame
    ) {
      const amount = smoothstep(
        (this.#starterPosition - seamStart) / starter.crossfadeFrames,
      );
      const loopPosition = starter.loopStartFrame +
        (this.#starterPosition - seamStart);
      sample = sampleLinear(samples, this.#starterPosition) * (1 - amount) +
        sampleLinear(samples, loopPosition) * amount;
    } else {
      sample = sampleLinear(samples, this.#starterPosition);
    }
    const rate = clamp(
      Math.abs(this.#state.rpm) / starter.referenceRpm,
      0.65,
      1.6,
    );
    this.#starterPosition += rate;
    if (this.#starterPosition >= starter.loopEndFrame) {
      const resume = starter.loopStartFrame + starter.crossfadeFrames;
      const loopLength = starter.loopEndFrame - resume;
      this.#starterPosition = resume +
        (this.#starterPosition - starter.loopEndFrame) % loopLength;
    }
    return sample;
  }

  #renderLifecycle(frame, runningSample) {
    const event = this.#updateEvent(frame);
    const starterWanted =
      this.#state.starter && ["cranking", "startup"].includes(this.#outputMode);
    const fadeFrames = starterWanted
      ? this.#package.starter.attackFadeFrames
      : this.#package.starter.releaseFadeFrames;
    const gainStep = fadeFrames <= 1 ? 1 : 1 / fadeFrames;
    this.#starterGain = starterWanted
      ? Math.min(1, this.#starterGain + gainStep)
      : Math.max(0, this.#starterGain - gainStep);
    const starterSample = this.#starterGain > 0
      ? this.#starterSample() * this.#starterGain
      : 0;

    if (event === null) {
      if (this.#outputMode === "running") return runningSample;
      if (this.#outputMode === "cranking") return starterSample;
      return 0;
    }

    const performance = this.#package[event.kind];
    const sourceFrame = event.sourceFrameAtStart + (frame - event.startFrame);
    if (
      event.kind === "startup" &&
      sourceFrame >= performance.artifact.frameCount
    ) {
      throw new RangeError(
        "first-fire tape ended before the live running bed became authoritative",
      );
    }
    const eventSample = performance.artifact.samples[Math.floor(sourceFrame)] ?? 0;
    if (event.kind === "startup") {
      if (frame < event.startFrame + event.entryFadeFrames) {
        const amount = smoothstep(
          (frame - event.startFrame) / event.entryFadeFrames,
        );
        return starterSample * (1 - amount) + eventSample * amount;
      }
      const admissionProgress = this.#audibleAdmissionProgressAt(frame);
      if (admissionProgress > 0) {
        const audibleCrankRevolutions = this.#audibleCrankRevolutionsAt(frame);
        if (audibleCrankRevolutions === null) {
          throw new RangeError("audible crank phase is unavailable during startup handoff");
        }
        if (admissionProgress >= 1 && event.handoffEndFrame === null) {
          event.handoffEndFrame = frame + 1;
          event.endFrame = event.handoffEndFrame;
          this.#audibleHandoffEndFrame = event.handoffEndFrame;
          this.#handoffEndCrankRevolutions = audibleCrankRevolutions;
        }
        const eventGain = Math.sqrt(Math.max(0, 1 - admissionProgress));
        const runningGain = Math.sqrt(admissionProgress);
        return eventSample * eventGain + runningSample * runningGain;
      }
      return eventSample;
    }

    if (frame < event.startFrame + event.entryFadeFrames) {
      const amount = smoothstep(
        (frame - event.startFrame) / event.entryFadeFrames,
      );
      return runningSample * (1 - amount) + eventSample * amount;
    }
    return sourceFrame < performance.silenceFrame ? eventSample : 0;
  }

  mixPair(sourceBlockValue, runningBakedBlockValue) {
    const sourceBlock = mono(sourceBlockValue, "sourceBlock");
    const runningBakedBlock = mono(runningBakedBlockValue, "runningBakedBlock");
    if (sourceBlock.length !== runningBakedBlock.length) {
      throw new RangeError("lifecycle source/running blocks must have equal lengths");
    }
    if (this.#outputFrame === null) {
      this.#outputFrame = this.#scheduledStates[0]?.frame ?? 0;
    }
    const bakedBlock = new Float32Array(runningBakedBlock.length);
    for (let index = 0; index < bakedBlock.length; ++index) {
      const frame = this.#outputFrame + index;
      this.#applyStates(frame);
      this.#applyAudibleStates(frame);
      const sample = this.#renderLifecycle(frame, runningBakedBlock[index]);
      if (!Number.isFinite(sample)) {
        throw new RangeError(`lifecycle output became non-finite at ${frame}`);
      }
      bakedBlock[index] = sample;
    }
    this.#outputFrame += bakedBlock.length;
    this.#renderedFrames += bakedBlock.length;
    return Object.freeze({ sourceBlock, bakedBlock });
  }

  diagnostics() {
    return Object.freeze({
      schema: SCHEMA,
      id: this.#package.manifest.id,
      engine: this.#package.engine,
      generation: this.#generation,
      outputFrame: this.#outputFrame,
      inputMode: this.#inputMode,
      outputMode: this.#outputMode,
      activeEvent: this.#activeEvent?.kind ?? null,
      queuedStateCount: this.#scheduledStates.length,
      eventCount: this.#events.length,
      startupCount: this.#startupCount,
      shutdownCount: this.#shutdownCount,
      lateStartFrames: this.#lateStartFrames,
      audioLatencyFrames: this.#audioLatencyFrames,
      runningFloorRpm: this.#runningFloorRpm,
      heldAnchorFloorRpm: this.#heldAnchorFloorRpm,
      startupAdmissionSchema: this.#startupAdmission.schema,
      startupAdmissionBlend: this.#startupAdmission.blend,
      startupAdmissionLaneCoordinate:
        this.#startupAdmission.admissionLaneCoordinate,
      runningBedLoadCoordinate: this.#atlasLoadCoordinate,
      admissionProgress: this.#admissionProgress,
      floorRunningGainLinear: this.#lastFloorRunningGainLinear,
      admissionLaneWeights: this.#lastAdmissionLaneWeights,
      runningBedLaneWeights: this.#lastRunningBedLaneWeights,
      authoredCompletionCrankTravelRevolutions:
        this.#startupAdmission.completionCrankTravelRevolutions,
      completionCrankTravelRevolutions:
        this.#completionCrankTravelRevolutions,
      floorCrankRevolutions: this.#floorCrankRevolutions,
      coastStable: this.#coastStable,
      physicalFirstFireFrame: this.#physicalFirstFireFrame,
      audibleFirstFireFrame: this.#audibleFirstFireFrame,
      physicalRunningFloorFrame: this.#physicalRunningFloorFrame,
      audibleHandoffStartFrame: this.#audibleHandoffStartFrame,
      audibleHandoffEndFrame: this.#audibleHandoffEndFrame,
      handoffRpm: this.#handoffRpm,
      handoffReason: this.#handoffReason,
      handoffStartCrankRevolutions: this.#handoffStartCrankRevolutions,
      handoffEndCrankRevolutions: this.#handoffEndCrankRevolutions,
      handoffCrankTravelRevolutions:
        this.#handoffStartCrankRevolutions === null ||
        this.#handoffEndCrankRevolutions === null
          ? null
          : Math.abs(
            this.#handoffEndCrankRevolutions -
              this.#handoffStartCrankRevolutions,
          ),
      peakPostFireRpm: this.#peakPostFireRpm,
      starterSourceFrame: Math.floor(this.#starterPosition),
      starterGain: this.#starterGain,
      renderedFrames: this.#renderedFrames,
    });
  }
}
