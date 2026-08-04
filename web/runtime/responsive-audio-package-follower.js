import { ESO_CANONICAL_SAMPLE_RATE } from "./c-api-abi.js";

const REFERENCE_OVERLAP_FRAMES = 128;
const REFERENCE_OVERLAP_SAMPLE_RATE = 44_100;
const LOAD_ATTACK_MILLISECONDS = 24;
const LOAD_RELEASE_MILLISECONDS = 42;
const SOURCE_TRANSITION_MILLISECONDS = 90;
const IDLE_BLEND_RANGE_RPM = 140;
const NEIGHBOR_RADIUS_ROWS = 3;
const MINIMUM_PLAYBACK_RPM = 1;
const RANDOM_FALLBACK_STATE = 0x6d2b79f5;
const MAXIMUM_SELECTION_HISTORY = 256;
const EDGE_EPSILON_FRAMES = 1e-7;

export const RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS = Object.freeze({
  sampleRate: ESO_CANONICAL_SAMPLE_RATE,
  cycleRevolutions: 2,
  loadAttackMilliseconds: LOAD_ATTACK_MILLISECONDS,
  loadReleaseMilliseconds: LOAD_RELEASE_MILLISECONDS,
  sourceTransitionMilliseconds: SOURCE_TRANSITION_MILLISECONDS,
  idleBlendRangeRpm: IDLE_BLEND_RANGE_RPM,
  neighborRadiusRows: NEIGHBOR_RADIUS_ROWS,
  overlapFrames:
    (REFERENCE_OVERLAP_FRAMES * ESO_CANONICAL_SAMPLE_RATE) /
    REFERENCE_OVERLAP_SAMPLE_RATE,
});

export class ResponsiveAudioFollowerError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = "ResponsiveAudioFollowerError";
    this.code = code;
  }
}

function fail(code, message, options) {
  throw new ResponsiveAudioFollowerError(code, message, options);
}

function clamp(value, minimum, maximum) {
  return Math.min(maximum, Math.max(minimum, value));
}

function smoothstep(value) {
  const unit = clamp(value, 0, 1);
  return unit * unit * (3 - 2 * unit);
}

function raisedCosine(value) {
  const unit = clamp(value, 0, 1);
  return 0.5 - 0.5 * Math.cos(Math.PI * unit);
}

function smoothingCoefficient(milliseconds) {
  return 1 - Math.exp(-1 / (milliseconds * 0.001 * ESO_CANONICAL_SAMPLE_RATE));
}

function finite(value, name) {
  if (!Number.isFinite(value)) {
    throw new TypeError(`${name} must be finite`);
  }
  return value;
}

function safeFrame(value, name) {
  let numeric = value;
  if (typeof value === "bigint") {
    if (value < 0n || value > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new RangeError(`${name} is outside the exact JavaScript frame range`);
    }
    numeric = Number(value);
  } else if (typeof value === "string" && /^(0|[1-9][0-9]*)$/.test(value)) {
    const parsed = BigInt(value);
    if (parsed > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new RangeError(`${name} is outside the exact JavaScript frame range`);
    }
    numeric = Number(parsed);
  }
  if (!Number.isSafeInteger(numeric) || numeric < 0) {
    throw new RangeError(`${name} must be a nonnegative safe integer`);
  }
  return numeric;
}

function clockFrame(value, name) {
  if (typeof value === "string" && value.length > 0) {
    value = Number(value);
  }
  finite(value, name);
  if (value < 0 || value > Number.MAX_SAFE_INTEGER) {
    throw new RangeError(`${name} must be in the exact nonnegative frame range`);
  }
  return value;
}

function exactBoundaryFrame(boundary, name) {
  if (!boundary || typeof boundary !== "object") {
    throw new TypeError(`${name} must be an exact cycle boundary`);
  }
  return finite(boundary.deliveryFrame, `${name}.deliveryFrame`);
}

function sourceBoundaryPosition(boundary) {
  return boundary.left_frame + boundary.fraction_from_left_01;
}

function linearSample(samples, position) {
  if (position < 0 || position >= samples.length - 1) {
    return 0;
  }
  const left = Math.floor(position);
  const amount = position - left;
  return samples[left] + (samples[left + 1] - samples[left]) * amount;
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function interpolateUnitField(units, rpm, field) {
  if (units.length === 1 || rpm <= units[0].canonical_rpm) {
    return units[0][field];
  }
  const last = units.length - 1;
  if (rpm >= units[last].canonical_rpm) {
    return units[last][field];
  }
  let lower = 0;
  let upper = last;
  while (upper - lower > 1) {
    const middle = (lower + upper) >>> 1;
    if (units[middle].canonical_rpm <= rpm) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  const left = units[lower];
  const right = units[upper];
  const amount =
    (rpm - left.canonical_rpm) /
    Math.max(Number.EPSILON, right.canonical_rpm - left.canonical_rpm);
  return interpolate(left[field], right[field], amount);
}

function ordinal(value, name) {
  try {
    const parsed = BigInt(value);
    if (parsed < 0n) {
      throw new RangeError(`${name} must be nonnegative`);
    }
    return parsed;
  } catch (error) {
    if (error instanceof RangeError) {
      throw error;
    }
    throw new TypeError(`${name} must be an unsigned integer`, { cause: error });
  }
}

function endpointState(value, name) {
  if (!value || typeof value !== "object") {
    throw new TypeError(`${name} must be an object`);
  }
  const rpm = finite(value.rpm, `${name}.rpm`);
  if (rpm <= 0) {
    throw new RangeError(
      `${name}.rpm must be positive for the warm-normal-running follower`,
    );
  }
  const hasSignedLoad =
    value.signedLoad !== undefined && value.signedLoad !== null;
  const hasThrottle = value.throttle01 !== undefined && value.throttle01 !== null;
  if (!hasSignedLoad && !hasThrottle) {
    throw new TypeError(`${name} must provide signedLoad or throttle01`);
  }
  let signedLoad = null;
  if (hasSignedLoad) {
    signedLoad = finite(value.signedLoad, `${name}.signedLoad`);
    if (signedLoad < -1 || signedLoad > 1) {
      throw new RangeError(`${name}.signedLoad must be in [-1, 1]`);
    }
  }
  let throttle01 = null;
  if (hasThrottle) {
    throttle01 = finite(value.throttle01, `${name}.throttle01`);
    if (throttle01 < 0 || throttle01 > 1) {
      throw new RangeError(`${name}.throttle01 must be in [0, 1]`);
    }
  }
  return { rpm, signedLoad, throttle01 };
}

function compileLoadedPackage(loadedPackage) {
  const manifest = loadedPackage?.manifest;
  if (!manifest || typeof loadedPackage?.artifact !== "function") {
    throw new TypeError("loadedPackage must be a validated loaded audio package");
  }
  if (
    manifest.audio?.sample_rate?.numerator !== ESO_CANONICAL_SAMPLE_RATE ||
    manifest.audio.sample_rate.denominator !== 1
  ) {
    fail(
      "responsive-follower-sample-rate-mismatch",
      `audio package must use ${ESO_CANONICAL_SAMPLE_RATE} Hz`,
    );
  }
  if (manifest.running?.cycle_revolutions !== 2) {
    fail(
      "responsive-follower-cycle-geometry-invalid",
      "responsive follower requires complete 720-degree units",
    );
  }
  const planes = manifest.running.planes;
  if (!Array.isArray(planes) || planes.length < 3) {
    fail(
      "responsive-follower-plane-registry-invalid",
      "responsive follower requires ordered coast, part-load, and power planes",
    );
  }
  const buses = manifest.buses.map((bus) => ({ id: bus.id }));
  const artifactFor = (references, busId, laneName) => {
    const reference = references.find((item) => item.bus_id === busId);
    const artifact = reference ? loadedPackage.artifact(reference.artifact_id) : null;
    if (!artifact || !(artifact.pcm instanceof Float32Array)) {
      fail(
        "responsive-follower-artifact-missing",
        `${laneName} has no loaded Float32 tape for bus ${busId}`,
      );
    }
    return artifact.pcm;
  };
  const compiledPlanes = planes.map((plane) => ({
    id: plane.id,
    coordinate01: (plane.load_coordinate + 1) * 0.5,
    units: plane.units,
    tapes: buses.map((bus) => artifactFor(plane.artifacts, bus.id, plane.id)),
  }));
  const idle = {
    units: manifest.running.idle.units,
    tapes: buses.map((bus) =>
      artifactFor(manifest.running.idle.artifacts, bus.id, "idle"),
    ),
  };
  const idleMeanRpm =
    idle.units.reduce((sum, unit) => sum + unit.measured_rpm, 0) /
    idle.units.length;
  if (!Number.isFinite(idleMeanRpm) || idleMeanRpm <= 0) {
    fail(
      "responsive-follower-idle-pool-invalid",
      "idle pool has no positive finite natural RPM mean",
    );
  }
  const radius = manifest.running.rpm_grid.neighbor_radius_rows;
  if (radius !== NEIGHBOR_RADIUS_ROWS) {
    fail(
      "responsive-follower-selector-invalid",
      `package selector radius must be exactly ${NEIGHBOR_RADIUS_ROWS} rows`,
    );
  }
  if (
    compiledPlanes.some((plane) => plane.units.length < radius * 2 + 1) ||
    idle.units.length < radius * 2 + 1
  ) {
    fail(
      "responsive-follower-selector-invalid",
      "every running role must retain a complete symmetric selector pool",
    );
  }
  const seed64 = BigInt(manifest.running.selector_seed);
  const seed32 = Number((seed64 ^ (seed64 >> 32n)) & 0xffff_ffffn) >>> 0;
  return {
    manifest,
    buses,
    planes: compiledPlanes,
    idle,
    idleMeanRpm,
    radius,
    randomState: seed32 || RANDOM_FALLBACK_STATE,
    alignmentFrames: manifest.running.cycle_signal_alignment_frames,
    playbackMinimumRpm: manifest.running.rpm_grid.playback_minimum_rpm,
    playbackMaximumRpm: manifest.running.rpm_grid.playback_maximum_rpm,
  };
}

function initialBusStats(buses) {
  return buses.map((bus) => ({
    id: bus.id,
    peak: 0,
    squareSum: 0,
    clipSampleCount: 0,
  }));
}

export function audiblePackageCycleBoundaryFrame(loadedPackage, completedCycle) {
  const alignment = finite(
    loadedPackage?.manifest?.running?.cycle_signal_alignment_frames,
    "running.cycle_signal_alignment_frames",
  );
  return (
    exactBoundaryFrame(completedCycle?.endBoundary, "completedCycle.endBoundary") +
    alignment
  );
}

// Pure canonical-clock normal-running follower. It owns no AudioContext,
// resampler, worklet, ring buffer, or lifecycle-event behavior.
export class ResponsiveAudioPackageFollower {
  #package;
  #nextDeliveryFrame = null;
  #randomState;
  #offsetBag = [];
  #offsetIndex = 0;
  #previousOffset = null;
  #voices = [];
  #scheduledEdges = [];
  #clockSegments = [];
  #lastSubmittedOrdinal = null;
  #lastSubmittedMarkerFrame = null;
  #lastTransitionMarkerFrame = null;
  #selectionSequence = 0;
  #selectionHistory = [];
  #initializedState = false;
  #smoothedLoad01 = 0;
  #sourceCoordinate01 = 0;
  #idleBlend = 0;
  #renderedFrames = 0;
  #exactReanchorCount = 0;
  #sourceCursorTransitionCount = 0;
  #maximumCursorMarkerErrorFrames = 0;
  #stateWarmupFrameCount = 0;
  #uncoveredFrameCount = 0;
  #exactSilentFrameCount = 0;
  #lastAlignedSourceFrame = null;
  #busStats;

  constructor(loadedPackage) {
    this.#package = compileLoadedPackage(loadedPackage);
    this.#randomState = this.#package.randomState;
    this.#busStats = initialBusStats(this.#package.buses);
  }

  get sampleRate() {
    return ESO_CANONICAL_SAMPLE_RATE;
  }

  get busIds() {
    return Object.freeze(this.#package.buses.map((bus) => bus.id));
  }

  // Lifecycle events are intentionally outside this first follower slice.
  // The caller must reset on leaving admitted warm normal running and may then
  // feed silence (or a future native event asset) until a new warm run begins.
  reset() {
    this.#nextDeliveryFrame = null;
    this.#randomState = this.#package.randomState;
    this.#offsetBag = [];
    this.#offsetIndex = 0;
    this.#previousOffset = null;
    this.#voices = [];
    this.#scheduledEdges = [];
    this.#clockSegments = [];
    this.#lastSubmittedOrdinal = null;
    this.#lastSubmittedMarkerFrame = null;
    this.#lastTransitionMarkerFrame = null;
    this.#selectionSequence = 0;
    this.#selectionHistory = [];
    this.#initializedState = false;
    this.#smoothedLoad01 = 0;
    this.#sourceCoordinate01 = 0;
    this.#idleBlend = 0;
    this.#renderedFrames = 0;
    this.#exactReanchorCount = 0;
    this.#sourceCursorTransitionCount = 0;
    this.#maximumCursorMarkerErrorFrames = 0;
    this.#stateWarmupFrameCount = 0;
    this.#uncoveredFrameCount = 0;
    this.#exactSilentFrameCount = 0;
    this.#lastAlignedSourceFrame = null;
    this.#busStats = initialBusStats(this.#package.buses);
  }

  renderBlock({
    firstDeliveryFrame,
    frameCount,
    sourceClock,
    completedCycles = [],
  }) {
    const firstFrame = safeFrame(firstDeliveryFrame, "firstDeliveryFrame");
    const frames = safeFrame(frameCount, "frameCount");
    if (frames === 0) {
      throw new RangeError("frameCount must be positive");
    }
    if (
      this.#nextDeliveryFrame !== null &&
      firstFrame !== this.#nextDeliveryFrame
    ) {
      fail(
        "responsive-follower-clock-discontinuity",
        `expected delivery frame ${this.#nextDeliveryFrame} but received ${firstFrame}`,
      );
    }
    const clockSegment = this.#validateSourceClock(sourceClock);
    if (!Array.isArray(completedCycles)) {
      throw new TypeError("completedCycles must be an array");
    }
    const cycleBatch = this.#validateCompletedCycles(completedCycles, firstFrame);

    // Commit input only after the entire block contract has validated.
    this.#clockSegments.push(clockSegment);
    this.#scheduledEdges.push(...cycleBatch.edges);
    const output = this.#package.buses.map(() => new Float32Array(frames));
    for (let offset = 0; offset < frames; ++offset) {
      const frame = firstFrame + offset;
      const state = this.#stateAtAudibleFrame(frame);
      this.#updateState(state.rpm, state.signedLoad, state.throttle01);
      this.#renderFrame(frame, state.rpm, output, offset);
    }

    this.#nextDeliveryFrame = firstFrame + frames;
    this.#renderedFrames += frames;
    this.#lastSubmittedOrdinal = cycleBatch.lastOrdinal;
    this.#lastSubmittedMarkerFrame = cycleBatch.lastMarkerFrame;
    this.#pruneClockHistory();
    const buses = this.#package.buses.map((bus, index) =>
      Object.freeze({ id: bus.id, samples: output[index] }),
    );
    return Object.freeze({
      firstDeliveryFrame: firstFrame,
      frameCount: frames,
      buses: Object.freeze(buses),
      bus(id) {
        return buses.find((bus) => bus.id === id)?.samples ?? null;
      },
    });
  }

  diagnostics() {
    const buses = this.#busStats.map((stats) =>
      Object.freeze({
        id: stats.id,
        peak: stats.peak,
        rms:
          this.#renderedFrames === 0
            ? 0
            : Math.sqrt(stats.squareSum / this.#renderedFrames),
        clipSampleCount: stats.clipSampleCount,
        finite: true,
      }),
    );
    return Object.freeze({
      sampleRate: ESO_CANONICAL_SAMPLE_RATE,
      scope: "warm-normal-running-only",
      nextDeliveryFrame: this.#nextDeliveryFrame,
      overlapFrames: RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames,
      idleMeanRpm: this.#package.idleMeanRpm,
      smoothedLoad01: this.#smoothedLoad01,
      sourceCoordinate01: this.#sourceCoordinate01,
      idleBlend: this.#idleBlend,
      activeVoiceCount: this.#voices.length,
      exactReanchorCount: this.#exactReanchorCount,
      sourceCursorTransitionCount: this.#sourceCursorTransitionCount,
      maximumCursorMarkerErrorFrames: this.#maximumCursorMarkerErrorFrames,
      stateWarmupFrameCount: this.#stateWarmupFrameCount,
      uncoveredFrameCount: this.#uncoveredFrameCount,
      exactSilentFrameCount: this.#exactSilentFrameCount,
      lastAlignedSourceFrame: this.#lastAlignedSourceFrame,
      lastTransitionMarkerFrame: this.#lastTransitionMarkerFrame,
      selectionHistory: Object.freeze(
        this.#selectionHistory.map((selection) => Object.freeze({ ...selection })),
      ),
      buses: Object.freeze(buses),
    });
  }

  #validateSourceClock(sourceClock) {
    if (!sourceClock || typeof sourceClock !== "object") {
      throw new TypeError(
        "sourceClock must provide explicit delivery-frame endpoints and states",
      );
    }
    const startDeliveryFrame = clockFrame(
      sourceClock.start?.deliveryFrame,
      "sourceClock.start.deliveryFrame",
    );
    const endDeliveryFrame = clockFrame(
      sourceClock.end?.deliveryFrame,
      "sourceClock.end.deliveryFrame",
    );
    if (endDeliveryFrame <= startDeliveryFrame) {
      throw new RangeError(
        "sourceClock.endDeliveryFrame must be after startDeliveryFrame",
      );
    }
    const start = endpointState(sourceClock.start, "sourceClock.start");
    const end = endpointState(sourceClock.end, "sourceClock.end");
    const completeSignedLoad =
      start.signedLoad !== null && end.signedLoad !== null;
    const completeThrottle =
      start.throttle01 !== null && end.throttle01 !== null;
    if (!completeSignedLoad && !completeThrottle) {
      throw new TypeError(
        "sourceClock interval must provide signedLoad or requested throttle at both endpoints",
      );
    }
    const previous = this.#clockSegments.at(-1);
    if (
      previous &&
      Math.abs(previous.endDeliveryFrame - startDeliveryFrame) >
        EDGE_EPSILON_FRAMES
    ) {
      fail(
        "responsive-follower-source-clock-discontinuity",
        `expected source-clock frame ${previous.endDeliveryFrame} but received ${startDeliveryFrame}`,
      );
    }
    return {
      startDeliveryFrame,
      endDeliveryFrame,
      start,
      end,
    };
  }

  #stateAtAudibleFrame(audibleFrame) {
    const sourceFrame = audibleFrame - this.#package.alignmentFrames;
    this.#lastAlignedSourceFrame = sourceFrame;
    if (sourceFrame < this.#clockSegments[0].startDeliveryFrame) {
      ++this.#stateWarmupFrameCount;
      return this.#clockSegments[0].start;
    }
    let selected = null;
    // Search from newest to oldest so a deliberate control step at a shared
    // endpoint belongs to the new half-open segment.
    for (let index = this.#clockSegments.length - 1; index >= 0; --index) {
      const segment = this.#clockSegments[index];
      if (
        sourceFrame + EDGE_EPSILON_FRAMES >= segment.startDeliveryFrame &&
        sourceFrame < segment.endDeliveryFrame + EDGE_EPSILON_FRAMES
      ) {
        selected = segment;
        break;
      }
    }
    if (!selected) {
      fail(
        "responsive-follower-source-clock-gap",
        `no source-clock state covers aligned frame ${sourceFrame}`,
      );
    }
    const amount = clamp(
      (sourceFrame - selected.startDeliveryFrame) /
        (selected.endDeliveryFrame - selected.startDeliveryFrame),
      0,
      1,
    );
    const signedLoad =
      selected.start.signedLoad !== null && selected.end.signedLoad !== null
        ? interpolate(
            selected.start.signedLoad,
            selected.end.signedLoad,
            amount,
          )
        : null;
    const throttle01 =
      selected.start.throttle01 !== null && selected.end.throttle01 !== null
        ? interpolate(
            selected.start.throttle01,
            selected.end.throttle01,
            amount,
          )
        : null;
    if (signedLoad === null && throttle01 === null) {
      fail(
        "responsive-follower-load-state-unavailable",
        "aligned source-clock interval has neither complete signed load nor requested throttle",
      );
    }
    return {
      rpm: interpolate(selected.start.rpm, selected.end.rpm, amount),
      signedLoad,
      throttle01,
    };
  }

  #pruneClockHistory() {
    if (this.#nextDeliveryFrame === null) {
      return;
    }
    const earliestNeeded =
      this.#nextDeliveryFrame - this.#package.alignmentFrames - 1;
    while (
      this.#clockSegments.length > 1 &&
      this.#clockSegments[1].endDeliveryFrame < earliestNeeded
    ) {
      this.#clockSegments.shift();
    }
  }

  #validateCompletedCycles(completedCycles, firstFrame) {
    const edges = [];
    let previousOrdinal = this.#lastSubmittedOrdinal;
    let previousMarker =
      this.#scheduledEdges.length === 0
        ? this.#lastSubmittedMarkerFrame
        : this.#scheduledEdges[this.#scheduledEdges.length - 1].markerFrame;
    for (let index = 0; index < completedCycles.length; ++index) {
      const cycle = completedCycles[index];
      const cycleOrdinal = ordinal(
        cycle?.completedCycleOrdinal,
        `completedCycles[${index}].completedCycleOrdinal`,
      );
      if (previousOrdinal !== null && cycleOrdinal !== previousOrdinal + 1n) {
        fail(
          "responsive-follower-cycle-order-invalid",
          "completed cycle ordinals must be contiguous and strictly increasing",
        );
      }
      const markerFrame = audiblePackageCycleBoundaryFrame(
        { manifest: this.#package.manifest },
        cycle,
      );
      const onsetFrame =
        markerFrame - RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames;
      if (markerFrame < 0 || onsetFrame < firstFrame - EDGE_EPSILON_FRAMES) {
        fail(
          "responsive-follower-cycle-evidence-late",
          `cycle ${cycleOrdinal} arrived after its exact OLA onset`,
        );
      }
      if (previousMarker !== null && markerFrame <= previousMarker) {
        fail(
          "responsive-follower-cycle-order-invalid",
          "completed cycle audible boundaries must be strictly increasing",
        );
      }
      edges.push({ cycleOrdinal, markerFrame });
      previousOrdinal = cycleOrdinal;
      previousMarker = markerFrame;
    }
    return {
      edges,
      lastOrdinal:
        edges.length > 0
          ? edges[edges.length - 1].cycleOrdinal
          : this.#lastSubmittedOrdinal,
      lastMarkerFrame:
        edges.length > 0
          ? edges[edges.length - 1].markerFrame
          : this.#lastSubmittedMarkerFrame,
    };
  }

  #updateState(rpm, signedLoad, throttle01) {
    const targetLoad01 =
      signedLoad === null
        ? this.#throttleLoadCoordinate(rpm, throttle01)
        : (signedLoad + 1) * 0.5;
    if (!this.#initializedState) {
      this.#smoothedLoad01 = targetLoad01;
      this.#sourceCoordinate01 = targetLoad01;
      const idleByRpm = 1 - smoothstep(
        (rpm - this.#package.idleMeanRpm) / IDLE_BLEND_RANGE_RPM,
      );
      this.#idleBlend = (1 - targetLoad01) * idleByRpm;
      this.#initializedState = true;
      return;
    }
    const loadMilliseconds =
      targetLoad01 > this.#smoothedLoad01
        ? LOAD_ATTACK_MILLISECONDS
        : LOAD_RELEASE_MILLISECONDS;
    this.#smoothedLoad01 +=
      (targetLoad01 - this.#smoothedLoad01) *
      smoothingCoefficient(loadMilliseconds);
    this.#sourceCoordinate01 +=
      (this.#smoothedLoad01 - this.#sourceCoordinate01) *
      smoothingCoefficient(SOURCE_TRANSITION_MILLISECONDS);
    const idleByRpm = 1 - smoothstep(
      (rpm - this.#package.idleMeanRpm) / IDLE_BLEND_RANGE_RPM,
    );
    const idleTarget = (1 - this.#sourceCoordinate01) * idleByRpm;
    this.#idleBlend +=
      (idleTarget - this.#idleBlend) *
      smoothingCoefficient(SOURCE_TRANSITION_MILLISECONDS);
  }

  #throttleLoadCoordinate(rpm, throttle01) {
    const coordinates = this.#package.planes.map((plane) => ({
      coordinate01: plane.coordinate01,
      throttle01: interpolateUnitField(
        plane.units,
        rpm,
        "average_requested_throttle_01",
      ),
    }));
    if (throttle01 <= coordinates[0].throttle01) {
      return coordinates[0].coordinate01;
    }
    const last = coordinates.length - 1;
    if (throttle01 >= coordinates[last].throttle01) {
      return coordinates[last].coordinate01;
    }
    for (let index = 1; index < coordinates.length; ++index) {
      const right = coordinates[index];
      if (throttle01 > right.throttle01) {
        continue;
      }
      const left = coordinates[index - 1];
      const span = right.throttle01 - left.throttle01;
      if (!(span > 1e-9)) {
        continue;
      }
      const amount = clamp((throttle01 - left.throttle01) / span, 0, 1);
      return interpolate(left.coordinate01, right.coordinate01, amount);
    }
    // A malformed/non-monotone throttle calibration cannot silently invent
    // another nonlinear routing law. The package's normalized endpoints are
    // the deterministic fallback when signed load is unavailable.
    return clamp(throttle01, 0, 1);
  }

  #nextRandom() {
    let state = this.#randomState;
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    this.#randomState = state >>> 0 || RANDOM_FALLBACK_STATE;
    return this.#randomState / 0x1_0000_0000;
  }

  #nextOffset() {
    const radius = this.#package.radius;
    if (this.#offsetIndex >= this.#offsetBag.length) {
      const offsets = Array.from(
        { length: radius * 2 + 1 },
        (_unused, index) => index - radius,
      );
      for (let index = offsets.length - 1; index > 0; --index) {
        const swapIndex = Math.floor(this.#nextRandom() * (index + 1));
        const swap = offsets[index];
        offsets[index] = offsets[swapIndex];
        offsets[swapIndex] = swap;
      }
      if (offsets.length > 1 && offsets[0] === this.#previousOffset) {
        const swapIndex = offsets.findIndex(
          (offset) => offset !== this.#previousOffset,
        );
        const swap = offsets[0];
        offsets[0] = offsets[swapIndex];
        offsets[swapIndex] = swap;
      }
      this.#offsetBag = offsets;
      this.#offsetIndex = 0;
    }
    const offset = this.#offsetBag[this.#offsetIndex++];
    this.#previousOffset = offset;
    return offset;
  }

  #nearestCompleteIndex(units, rpm) {
    const radius = this.#package.radius;
    let nearest = radius;
    let distance = Number.POSITIVE_INFINITY;
    for (let index = radius; index < units.length - radius; ++index) {
      const currentDistance = Math.abs(units[index].canonical_rpm - rpm);
      if (currentDistance < distance) {
        nearest = index;
        distance = currentDistance;
      }
    }
    return nearest;
  }

  #spawnVoice(
    onsetFrame,
    markerFrame,
    rpm,
    exact,
    initiallyFull = false,
    inheritedSeamPhaseCycles = null,
  ) {
    const targetRpm = Math.max(MINIMUM_PLAYBACK_RPM, rpm);
    const targetPeriodFrames =
      (2 * 60 * ESO_CANONICAL_SAMPLE_RATE) / targetRpm;
    const seamPhaseCycles =
      inheritedSeamPhaseCycles ??
      -RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames / targetPeriodFrames;
    const offset = this.#nextOffset();
    const runningBase = this.#nearestCompleteIndex(
      this.#package.planes[0].units,
      clamp(
        targetRpm,
        this.#package.playbackMinimumRpm,
        this.#package.playbackMaximumRpm,
      ),
    );
    const runningIndex = runningBase + offset;
    const idleBase = this.#nearestCompleteIndex(this.#package.idle.units, targetRpm);
    const idleIndex = idleBase + offset;
    const elapsed = Math.max(0, Math.ceil(onsetFrame) - onsetFrame);
    const planeStates = this.#package.planes.map((plane) => {
      const unit = plane.units[runningIndex];
      const start = sourceBoundaryPosition(unit.start);
      const end = sourceBoundaryPosition(unit.end);
      const step = targetRpm / unit.measured_rpm;
      const seamPosition = start + seamPhaseCycles * (end - start);
      return {
        unit,
        position: initiallyFull
          ? start
          : seamPosition + elapsed * step,
        seamPosition,
        sourceRpm: unit.measured_rpm,
        period: end - start,
      };
    });
    const idleUnit = this.#package.idle.units[idleIndex];
    const idleStart = sourceBoundaryPosition(idleUnit.start);
    const idleEnd = sourceBoundaryPosition(idleUnit.end);
    const idleStep = targetRpm / idleUnit.measured_rpm;
    const power = planeStates[planeStates.length - 1];
    const voice = {
      onsetFrame,
      markerFrame,
      seamPhaseCycles,
      initiallyFull,
      fadeOnsetFrame: null,
      planeStates,
      idleState: {
        unit: idleUnit,
        position: initiallyFull
          ? idleStart
          : idleStart + seamPhaseCycles * (idleEnd - idleStart) +
            elapsed * idleStep,
        sourceRpm: idleUnit.measured_rpm,
      },
      powerTransitionPosition: power.seamPosition + power.period,
      lastPowerStep: targetRpm / power.sourceRpm,
    };
    for (const outgoing of this.#voices) {
      if (outgoing.fadeOnsetFrame === null) {
        outgoing.fadeOnsetFrame = onsetFrame;
      }
    }
    this.#voices.push(voice);
    this.#lastTransitionMarkerFrame = markerFrame;
    const history = {
      sequence: ++this.#selectionSequence,
      exact,
      onsetFrame,
      markerFrame,
      seamPhaseCycles,
      sourceSpanCycles:
        (voice.powerTransitionPosition - power.seamPosition) / power.period,
      targetRpm,
      variationOffset: offset,
      runningRow: runningIndex,
      idleRow: idleIndex,
    };
    this.#selectionHistory.push(history);
    if (this.#selectionHistory.length > MAXIMUM_SELECTION_HISTORY) {
      this.#selectionHistory.shift();
    }
    return voice;
  }

  #processScheduledEdges(frame, rpm) {
    while (this.#scheduledEdges.length > 0) {
      const edge = this.#scheduledEdges[0];
      const authoritative = this.#voices.find(
        (voice) => voice.fadeOnsetFrame === null,
      );
      const targetPeriodFrames =
        (2 * 60 * ESO_CANONICAL_SAMPLE_RATE) /
        Math.max(MINIMUM_PLAYBACK_RPM, rpm);
      const seamPhaseCycles =
        authoritative?.seamPhaseCycles ??
        -RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames /
          targetPeriodFrames;
      const onsetFrame =
        edge.markerFrame + seamPhaseCycles * targetPeriodFrames;
      if (onsetFrame > frame + EDGE_EPSILON_FRAMES) {
        return;
      }
      this.#scheduledEdges.shift();
      if (authoritative) {
        const remaining =
          authoritative.powerTransitionPosition -
          authoritative.planeStates[authoritative.planeStates.length - 1].position;
        const predictedOnset =
          frame + remaining / Math.max(1e-12, authoritative.lastPowerStep);
        this.#maximumCursorMarkerErrorFrames = Math.max(
          this.#maximumCursorMarkerErrorFrames,
          Math.abs(
            predictedOnset - seamPhaseCycles * targetPeriodFrames -
              edge.markerFrame,
          ),
        );
      }
      this.#spawnVoice(
        onsetFrame,
        edge.markerFrame,
        rpm,
        true,
        false,
        seamPhaseCycles,
      );
      ++this.#exactReanchorCount;
    }
  }

  #processSourceCursorEdge(frame, rpm) {
    const authoritative = this.#voices.find(
      (voice) => voice.fadeOnsetFrame === null,
    );
    if (!authoritative) {
      return;
    }
    // Once exact contiguous native evidence is queued, it is the sole owner of
    // the next edge. The source cursor remains the fallback only across a real
    // evidence gap; it never emits a competing shortened unit.
    if (this.#scheduledEdges.length > 0) {
      return;
    }
    const power = authoritative.planeStates[authoritative.planeStates.length - 1];
    if (power.position + 1e-9 < authoritative.powerTransitionPosition) {
      return;
    }
    const overshoot = power.position - authoritative.powerTransitionPosition;
    const onsetFrame =
      frame - overshoot / Math.max(1e-12, authoritative.lastPowerStep);
    const targetPeriodFrames =
      (2 * 60 * ESO_CANONICAL_SAMPLE_RATE) / Math.max(MINIMUM_PLAYBACK_RPM, rpm);
    const markerFrame =
      onsetFrame - authoritative.seamPhaseCycles * targetPeriodFrames;
    this.#spawnVoice(
      onsetFrame,
      markerFrame,
      rpm,
      false,
      false,
      authoritative.seamPhaseCycles,
    );
    ++this.#sourceCursorTransitionCount;
  }

  #directionalWeights() {
    const coordinate = clamp(this.#sourceCoordinate01, 0, 1);
    const planes = this.#package.planes;
    if (coordinate <= planes[0].coordinate01) {
      return planes.map((_plane, index) => (index === 0 ? 1 : 0));
    }
    const last = planes.length - 1;
    if (coordinate >= planes[last].coordinate01) {
      return planes.map((_plane, index) => (index === last ? 1 : 0));
    }
    const weights = planes.map(() => 0);
    for (let index = 1; index < planes.length; ++index) {
      const right = planes[index];
      if (coordinate > right.coordinate01) {
        continue;
      }
      const left = planes[index - 1];
      const amount = clamp(
        (coordinate - left.coordinate01) /
          Math.max(Number.EPSILON, right.coordinate01 - left.coordinate01),
        0,
        1,
      );
      weights[index - 1] = 1 - amount;
      weights[index] = amount;
      return weights;
    }
    return weights;
  }

  #voiceWindow(voice, frame) {
    const rise = voice.initiallyFull
      ? 1
      : raisedCosine(
          (frame - voice.onsetFrame) /
            RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames,
        );
    const fade =
      voice.fadeOnsetFrame === null
        ? 1
        : raisedCosine(
            (voice.fadeOnsetFrame +
              RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames -
              frame) /
              RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames,
          );
    return Math.min(rise, fade);
  }

  #renderFrame(frame, rpm, output, offset) {
    if (rpm <= 0) {
      return;
    }
    if (this.#voices.length === 0) {
      this.#spawnVoice(frame, frame, rpm, false, true);
    }
    this.#processScheduledEdges(frame, rpm);
    this.#processSourceCursorEdge(frame, rpm);

    const directionalWeights = this.#directionalWeights();
    const idleAngle = clamp(this.#idleBlend, 0, 1) * Math.PI * 0.5;
    const directionalIdleWeight = Math.cos(idleAngle);
    const idleWeight = Math.sin(idleAngle);
    let normalization = 0;
    const sums = this.#package.buses.map(() => 0);
    for (const voice of this.#voices) {
      const window = this.#voiceWindow(voice, frame);
      if (window <= 0) {
        continue;
      }
      normalization += window;
      for (let busIndex = 0; busIndex < sums.length; ++busIndex) {
        let directional = 0;
        for (
          let planeIndex = 0;
          planeIndex < this.#package.planes.length;
          ++planeIndex
        ) {
          directional +=
            linearSample(
              this.#package.planes[planeIndex].tapes[busIndex],
              voice.planeStates[planeIndex].position,
            ) * directionalWeights[planeIndex];
        }
        const idle = linearSample(
          this.#package.idle.tapes[busIndex],
          voice.idleState.position,
        );
        sums[busIndex] +=
          (directional * directionalIdleWeight + idle * idleWeight) * window;
      }
    }
    const divisor = Math.max(1, normalization);
    if (normalization <= 0) {
      ++this.#uncoveredFrameCount;
    }
    let exactSilent = true;
    for (let busIndex = 0; busIndex < output.length; ++busIndex) {
      const sample = sums[busIndex] / divisor;
      if (!Number.isFinite(sample)) {
        fail(
          "responsive-follower-nonfinite-output",
          `bus ${this.#package.buses[busIndex].id} produced a non-finite sample at frame ${frame}`,
        );
      }
      output[busIndex][offset] = sample;
      exactSilent &&= sample === 0;
      const magnitude = Math.abs(sample);
      const stats = this.#busStats[busIndex];
      stats.peak = Math.max(stats.peak, magnitude);
      stats.squareSum += sample * sample;
      if (magnitude > 1) {
        ++stats.clipSampleCount;
      }
    }
    if (exactSilent) {
      ++this.#exactSilentFrameCount;
    }

    const targetRpm = Math.max(MINIMUM_PLAYBACK_RPM, rpm);
    for (const voice of this.#voices) {
      for (const plane of voice.planeStates) {
        const step = targetRpm / Math.max(MINIMUM_PLAYBACK_RPM, plane.sourceRpm);
        plane.position += step;
      }
      voice.lastPowerStep =
        targetRpm /
        Math.max(
          MINIMUM_PLAYBACK_RPM,
          voice.planeStates[voice.planeStates.length - 1].sourceRpm,
        );
      voice.idleState.position +=
        targetRpm /
        Math.max(MINIMUM_PLAYBACK_RPM, voice.idleState.sourceRpm);
    }
    this.#voices = this.#voices.filter(
      (voice) =>
        voice.fadeOnsetFrame === null ||
        frame <
          voice.fadeOnsetFrame +
            RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames,
    );
  }
}
