const KNOWN_STATE_MASK = 0x1f;
const FOUR_STROKE_CYCLE_REVOLUTIONS = 2;
const SOURCE_FRAME_EPSILON = 1e-9;

export const AudioAtlasEngineStateFlag = Object.freeze({
  ignitionEnabled: 1 << 0,
  fuelEnabled: 1 << 1,
  starterEnabled: 1 << 2,
  limiterEnabled: 1 << 3,
  limiterCutActive: 1 << 4,
});

export class ContinuousAudioAtlasCursorError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = "ContinuousAudioAtlasCursorError";
    this.code = code;
  }
}

function fail(code, message, options) {
  throw new ContinuousAudioAtlasCursorError(code, message, options);
}

function finite(value, name) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError(`${name} must be a finite number`);
  }
  return value;
}

function positive(value, name) {
  const result = finite(value, name);
  if (result <= 0) {
    throw new RangeError(`${name} must be positive`);
  }
  return result;
}

function unitLoad(value, name) {
  const result = finite(value, name);
  if (result < -1 || result > 1) {
    throw new RangeError(`${name} must be in [-1, 1]`);
  }
  return result;
}

function audibleStateMask(value, name) {
  if (!Number.isSafeInteger(value) || value < 0 || value > KNOWN_STATE_MASK) {
    throw new RangeError(`${name} must contain only the five atlas audible-state bits`);
  }
  return value;
}

function optionalFinite(value, name) {
  return value === undefined || value === null ? null : finite(value, name);
}

function endpointState(value, name) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new TypeError(`${name} must be an object`);
  }
  const rpm = positive(value.rpm, `${name}.rpm`);
  const rpmSlopeRpmPerSecond = finite(
    value.rpmSlopeRpmPerSecond,
    `${name}.rpmSlopeRpmPerSecond`,
  );
  const stateMask = audibleStateMask(value.stateMask, `${name}.stateMask`);
  const signedLoadCoordinate =
    value.signedLoadCoordinate === undefined ||
    value.signedLoadCoordinate === null
      ? null
      : unitLoad(value.signedLoadCoordinate, `${name}.signedLoadCoordinate`);
  const manifoldPressurePaAbs =
    value.manifoldPressurePaAbs === undefined ||
    value.manifoldPressurePaAbs === null
      ? null
      : positive(value.manifoldPressurePaAbs, `${name}.manifoldPressurePaAbs`);
  if (signedLoadCoordinate === null && manifoldPressurePaAbs === null) {
    throw new TypeError(
      `${name} must provide signedLoadCoordinate or manifoldPressurePaAbs`,
    );
  }
  const unwrappedCrankRevolutions = optionalFinite(
    value.unwrappedCrankRevolutions,
    `${name}.unwrappedCrankRevolutions`,
  );
  return {
    rpm,
    rpmSlopeRpmPerSecond,
    normalizedRpmSlopePerSecond: rpmSlopeRpmPerSecond / rpm,
    stateMask,
    signedLoadCoordinate,
    manifoldPressurePaAbs,
    unwrappedCrankRevolutions,
  };
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function frameBefore(exclusiveFrame) {
  return exclusiveFrame - Math.max(
    1e-6,
    Math.abs(exclusiveFrame) * Number.EPSILON * 2,
  );
}

function sourceStateAt(segment, sourceFrame) {
  const knots = segment.timeline;
  if (
    sourceFrame < knots[0].frame - SOURCE_FRAME_EPSILON ||
    sourceFrame > knots.at(-1).frame + SOURCE_FRAME_EPSILON
  ) {
    fail(
      "continuous-atlas-source-frame-uncovered",
      `source frame ${sourceFrame} lies outside segment ${segment.id}`,
    );
  }
  if (sourceFrame >= knots.at(-1).frame) {
    return { ...knots.at(-1), knotIndex: knots.length - 1 };
  }
  let lower = 0;
  let upper = knots.length - 1;
  while (upper - lower > 1) {
    const middle = (lower + upper) >>> 1;
    if (knots[middle].frame <= sourceFrame) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  const left = knots[lower];
  const right = knots[upper];
  const amount =
    (sourceFrame - left.frame) / Math.max(Number.EPSILON, right.frame - left.frame);
  return {
    frame: sourceFrame,
    rpm: interpolate(left.rpm, right.rpm, amount),
    rpmSlopeRpmPerSecond: interpolate(
      left.rpmSlopeRpmPerSecond,
      right.rpmSlopeRpmPerSecond,
      amount,
    ),
    requestedThrottle01: interpolate(
      left.requestedThrottle01,
      right.requestedThrottle01,
      amount,
    ),
    signedLoadCoordinate: interpolate(
      left.signedLoadCoordinate,
      right.signedLoadCoordinate,
      amount,
    ),
    manifoldPressurePaAbs: interpolate(
      left.manifoldPressurePaAbs,
      right.manifoldPressurePaAbs,
      amount,
    ),
    unwrappedCrankRevolutions: interpolate(
      left.unwrappedCrankRevolutions,
      right.unwrappedCrankRevolutions,
      amount,
    ),
    stateMask: left.stateMask,
    transitionMask: left.transitionMask,
    knotIndex: lower,
  };
}

function sourceFrameAtCrank(segment, crankRevolutions) {
  const knots = segment.timeline;
  if (
    crankRevolutions < knots[0].unwrappedCrankRevolutions ||
    crankRevolutions > knots.at(-1).unwrappedCrankRevolutions
  ) {
    return null;
  }
  let lower = 0;
  let upper = knots.length - 1;
  while (upper - lower > 1) {
    const middle = (lower + upper) >>> 1;
    if (knots[middle].unwrappedCrankRevolutions <= crankRevolutions) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  const left = knots[lower];
  const right = knots[upper];
  const amount =
    (crankRevolutions - left.unwrappedCrankRevolutions) /
    Math.max(
      Number.EPSILON,
      right.unwrappedCrankRevolutions - left.unwrappedCrankRevolutions,
    );
  return interpolate(left.frame, right.frame, amount);
}

function nearestRpmFrame(segment, liveState) {
  const start = segment.usableFrames.begin;
  const end = segment.usableFrames.end;
  const candidateFrames = [start, Math.max(start, frameBefore(end))];
  const knots = segment.timeline;
  for (let index = 0; index + 1 < knots.length; ++index) {
    const left = knots[index];
    const right = knots[index + 1];
    const intervalBegin = Math.max(start, left.frame);
    const intervalEnd = Math.min(end, right.frame);
    if (intervalBegin >= intervalEnd) {
      continue;
    }
    candidateFrames.push(intervalBegin, Math.max(intervalBegin, frameBefore(intervalEnd)));
    const rpmSpan = right.rpm - left.rpm;
    if (rpmSpan !== 0) {
      const amount = (liveState.rpm - left.rpm) / rpmSpan;
      if (amount >= 0 && amount <= 1) {
        const crossing = interpolate(left.frame, right.frame, amount);
        if (crossing >= start && crossing < end) {
          candidateFrames.push(crossing);
        }
      }
    }
  }
  let best = null;
  for (const sourceFrame of candidateFrames) {
    const source = sourceStateAt(segment, sourceFrame);
    if (source.stateMask !== liveState.stateMask || source.transitionMask !== 0) {
      continue;
    }
    const rpmError = Math.abs(source.rpm - liveState.rpm);
    const sourceNormalizedSlope = source.rpmSlopeRpmPerSecond / source.rpm;
    const slopeError = Math.abs(
      sourceNormalizedSlope - liveState.normalizedRpmSlopePerSecond,
    );
    const loadError =
      liveState.signedLoadCoordinate === null
        ? 0
        : Math.abs(source.signedLoadCoordinate - liveState.signedLoadCoordinate);
    const mapError =
      liveState.manifoldPressurePaAbs === null
        ? 0
        : Math.abs(Math.log(source.manifoldPressurePaAbs / liveState.manifoldPressurePaAbs));
    const score =
      rpmError / Math.max(1, segment.handoff.maximumRpmError) +
      slopeError / Math.max(1e-6, segment.handoff.maximumSlopeError) +
      loadError / Math.max(1e-6, segment.handoff.maximumLoadError) +
      mapError;
    if (best === null || score < best.score) {
      best = { sourceFrame, source, score };
    }
  }
  return best;
}

function phaseSeedFrame(segment, baseFrame, liveCrankRevolutions) {
  const usableStartState = sourceStateAt(segment, segment.usableFrames.begin);
  const usableEndState = sourceStateAt(
    segment,
    frameBefore(segment.usableFrames.end),
  );
  const baseState = sourceStateAt(segment, baseFrame);
  const minimumCycle = Math.ceil(
    (usableStartState.unwrappedCrankRevolutions - liveCrankRevolutions) /
      FOUR_STROKE_CYCLE_REVOLUTIONS,
  );
  const maximumCycle = Math.floor(
    (usableEndState.unwrappedCrankRevolutions - liveCrankRevolutions) /
      FOUR_STROKE_CYCLE_REVOLUTIONS,
  );
  if (minimumCycle > maximumCycle) {
    return null;
  }
  const nearestCycle = Math.round(
    (baseState.unwrappedCrankRevolutions - liveCrankRevolutions) /
      FOUR_STROKE_CYCLE_REVOLUTIONS,
  );
  const selectedCycle = Math.max(minimumCycle, Math.min(maximumCycle, nearestCycle));
  const targetCrank =
    liveCrankRevolutions + selectedCycle * FOUR_STROKE_CYCLE_REVOLUTIONS;
  const sourceFrame = sourceFrameAtCrank(segment, targetCrank);
  return sourceFrame !== null &&
    sourceFrame >= segment.usableFrames.begin &&
    sourceFrame < segment.usableFrames.end
    ? sourceFrame
    : null;
}

function phaseDistanceRevolutions(left, right) {
  const difference = left - right;
  return Math.abs(
    difference -
      Math.round(difference / FOUR_STROKE_CYCLE_REVOLUTIONS) *
        FOUR_STROKE_CYCLE_REVOLUTIONS,
  );
}

function candidateForSegment(segment, liveState) {
  const base = nearestRpmFrame(segment, liveState);
  if (base === null) {
    return null;
  }
  let sourceFrame = base.sourceFrame;
  let phaseSeeded = false;
  if (liveState.unwrappedCrankRevolutions !== null) {
    const seeded = phaseSeedFrame(
      segment,
      sourceFrame,
      liveState.unwrappedCrankRevolutions,
    );
    if (seeded === null) {
      return null;
    }
    sourceFrame = seeded;
    phaseSeeded = true;
  }
  const source = sourceStateAt(segment, sourceFrame);
  const rpmError = Math.abs(source.rpm - liveState.rpm);
  const normalizedSlopeError = Math.abs(
    source.rpmSlopeRpmPerSecond / source.rpm -
      liveState.normalizedRpmSlopePerSecond,
  );
  const loadError =
    liveState.signedLoadCoordinate === null
      ? 0
      : Math.abs(source.signedLoadCoordinate - liveState.signedLoadCoordinate);
  const phaseError =
    liveState.unwrappedCrankRevolutions === null
      ? 0
      : phaseDistanceRevolutions(
          source.unwrappedCrankRevolutions,
          liveState.unwrappedCrankRevolutions,
        );
  const mapError =
    liveState.manifoldPressurePaAbs === null
      ? 0
      : Math.abs(Math.log(source.manifoldPressurePaAbs / liveState.manifoldPressurePaAbs));
  if (
    source.stateMask !== liveState.stateMask ||
    source.transitionMask !== 0 ||
    rpmError > segment.handoff.maximumRpmError ||
    normalizedSlopeError > segment.handoff.maximumSlopeError ||
    loadError > segment.handoff.maximumLoadError ||
    mapError > segment.handoff.maximumLoadError ||
    phaseError > segment.handoff.maximumPhaseError
  ) {
    return null;
  }
  return {
    segment,
    sourceFrame,
    phaseSeeded,
    source,
    score:
      rpmError / Math.max(1, segment.handoff.maximumRpmError) +
      normalizedSlopeError / Math.max(1e-6, segment.handoff.maximumSlopeError) +
      loadError / Math.max(1e-6, segment.handoff.maximumLoadError) +
      mapError,
  };
}

function validateLoadedAtlas(value) {
  if (
    value === null ||
    typeof value !== "object" ||
    typeof value.sampleRate !== "number" ||
    !Array.isArray(value.busIds) ||
    value.busIds.length === 0 ||
    !Array.isArray(value.movingSegments) ||
    value.movingSegments.length === 0
  ) {
    throw new TypeError("loadedAtlas must be a validated loaded continuous audio atlas");
  }
  for (const segment of value.movingSegments) {
    if (
      !Array.isArray(segment.timeline) ||
      segment.timeline.length < 2 ||
      typeof segment.bus !== "function"
    ) {
      throw new TypeError("loadedAtlas contains an invalid moving segment view");
    }
    for (const busId of value.busIds) {
      const artifact = segment.bus(busId);
      if (!artifact || !(artifact.pcm instanceof Float32Array)) {
        throw new TypeError(
          `moving segment ${segment.id} has no Float32 artifact for bus ${busId}`,
        );
      }
    }
  }
  return value;
}

function segmentCovers(segment, liveState, atlasDomain) {
  if (
    liveState.rpm < atlasDomain.minimum_rpm ||
    liveState.rpm > atlasDomain.maximum_rpm ||
    liveState.rpm < segment.usableRpm.minimum ||
    liveState.rpm > segment.usableRpm.maximum ||
    liveState.stateMask !== segment.stateMask
  ) {
    return false;
  }
  const rising = liveState.rpmSlopeRpmPerSecond > 0;
  const falling = liveState.rpmSlopeRpmPerSecond < 0;
  if (
    (segment.direction === "rising" && !rising) ||
    (segment.direction === "falling" && !falling)
  ) {
    return false;
  }
  const slope = liveState.normalizedRpmSlopePerSecond;
  if (
    slope < segment.normalizedRpmSlope.minimumPerSecond ||
    slope > segment.normalizedRpmSlope.maximumPerSecond
  ) {
    return false;
  }
  if (liveState.signedLoadCoordinate !== null) {
    if (
      liveState.signedLoadCoordinate < atlasDomain.minimum_load_coordinate ||
      liveState.signedLoadCoordinate > atlasDomain.maximum_load_coordinate ||
      Math.abs(liveState.signedLoadCoordinate - segment.loadCoordinate) >
        segment.handoff.maximumLoadError
    ) {
      return false;
    }
  }
  return true;
}

function validateBlockEndpoints(start, end, activeSegment, atlasDomain) {
  if (
    (start.signedLoadCoordinate === null) !==
      (end.signedLoadCoordinate === null) ||
    (start.manifoldPressurePaAbs === null) !==
      (end.manifoldPressurePaAbs === null)
  ) {
    throw new TypeError(
      "render endpoints must provide the same MAP/load coordinate fields",
    );
  }
  if (start.stateMask !== end.stateMask) {
    fail(
      "continuous-atlas-state-transition-unsupported",
      "a moving-only block cannot contain an audible-state transition",
    );
  }
  if (
    !segmentCovers(activeSegment, start, atlasDomain) ||
    !segmentCovers(activeSegment, end, atlasDomain)
  ) {
    fail(
      "continuous-atlas-active-segment-uncovered",
      `live block leaves active segment ${activeSegment.id}; handoff is not implemented`,
    );
  }
}

function validateOutputBuffers(outputBuffers, tapes, frameCount) {
  const busCount = tapes.length;
  if (!Array.isArray(outputBuffers) || outputBuffers.length !== busCount) {
    throw new TypeError("outputBuffers must follow the atlas bus order exactly");
  }
  for (let bus = 0; bus < outputBuffers.length; ++bus) {
    if (!(outputBuffers[bus] instanceof Float32Array)) {
      throw new TypeError(`outputBuffers[${bus}] must be Float32Array`);
    }
    if (outputBuffers[bus].length < frameCount) {
      throw new RangeError(`outputBuffers[${bus}] is shorter than frameCount`);
    }
    for (let other = 0; other < bus; ++other) {
      if (outputBuffers[bus].buffer === outputBuffers[other].buffer) {
        throw new TypeError("output buffers may not share backing storage");
      }
    }
    for (let tape = 0; tape < tapes.length; ++tape) {
      if (outputBuffers[bus].buffer === tapes[tape].buffer) {
        throw new TypeError("output buffers may not alias immutable atlas PCM");
      }
    }
  }
}

function assertSourceAdmission(
  segment,
  liveRpm,
  liveRpmSlope,
  liveLoad,
  liveMap,
  liveStateMask,
  sourceRpm,
  sourceRpmSlope,
  sourceLoad,
  sourceMap,
  sourceStateMask,
  sourceTransitionMask,
) {
  if (sourceStateMask !== liveStateMask || sourceTransitionMask !== 0) {
    fail(
      "continuous-atlas-source-state-mismatch",
      `active segment ${segment.id} reached an incompatible audible state`,
    );
  }
  if (Math.abs(sourceRpm - liveRpm) > segment.handoff.maximumRpmError) {
    fail(
      "continuous-atlas-source-rpm-mismatch",
      `active segment ${segment.id} exceeded its admitted live/source RPM error`,
    );
  }
  const slopeError = Math.abs(sourceRpmSlope / sourceRpm - liveRpmSlope / liveRpm);
  if (slopeError > segment.handoff.maximumSlopeError) {
    fail(
      "continuous-atlas-source-slope-mismatch",
      `active segment ${segment.id} exceeded its admitted normalized-slope error`,
    );
  }
  if (
    liveLoad !== null &&
    Math.abs(sourceLoad - liveLoad) > segment.handoff.maximumLoadError
  ) {
    fail(
      "continuous-atlas-source-load-mismatch",
      `active segment ${segment.id} exceeded its admitted load-coordinate error`,
    );
  }
  // Until MAP receives its own authored envelope, the alternate positive load
  // coordinate uses the same dimensionless handoff bound in log-pressure space.
  if (
    liveMap !== null &&
    Math.abs(Math.log(sourceMap / liveMap)) > segment.handoff.maximumLoadError
  ) {
    fail(
      "continuous-atlas-source-map-mismatch",
      `active segment ${segment.id} exceeded its admitted manifold-pressure error`,
    );
  }
}

function advanceKnotIndex(knots, knotIndex, sourceFrame) {
  while (
    knotIndex + 1 < knots.length - 1 &&
    sourceFrame >= knots[knotIndex + 1].frame
  ) {
    ++knotIndex;
  }
  return knotIndex;
}

function preflightBlock(
  segment,
  tapes,
  firstSourceFrame,
  firstKnotIndex,
  frameCount,
  start,
  end,
) {
  const knots = segment.timeline;
  const liveRpmStep = (end.rpm - start.rpm) / frameCount;
  const liveSlopeStep =
    (end.rpmSlopeRpmPerSecond - start.rpmSlopeRpmPerSecond) / frameCount;
  const liveLoadStep =
    start.signedLoadCoordinate === null
      ? 0
      : (end.signedLoadCoordinate - start.signedLoadCoordinate) / frameCount;
  const liveMapStep =
    start.manifoldPressurePaAbs === null
      ? 0
      : (end.manifoldPressurePaAbs - start.manifoldPressurePaAbs) / frameCount;
  let sourceFrame = firstSourceFrame;
  let knotIndex = firstKnotIndex;

  for (let frame = 0; frame < frameCount; ++frame) {
    if (
      sourceFrame < segment.usableFrames.begin ||
      sourceFrame >= segment.usableFrames.end
    ) {
      fail(
        "continuous-atlas-active-segment-exhausted",
        `active segment ${segment.id} exhausted its usable chronological tape; handoff is not implemented`,
      );
    }
    knotIndex = advanceKnotIndex(knots, knotIndex, sourceFrame);
    const left = knots[knotIndex];
    const right = knots[knotIndex + 1];
    const amount = (sourceFrame - left.frame) / (right.frame - left.frame);
    const sourceRpm = interpolate(left.rpm, right.rpm, amount);
    const sourceSlope = interpolate(
      left.rpmSlopeRpmPerSecond,
      right.rpmSlopeRpmPerSecond,
      amount,
    );
    const sourceLoad = interpolate(
      left.signedLoadCoordinate,
      right.signedLoadCoordinate,
      amount,
    );
    const sourceMap = interpolate(
      left.manifoldPressurePaAbs,
      right.manifoldPressurePaAbs,
      amount,
    );
    const liveRpm = start.rpm + liveRpmStep * frame;
    const liveSlope = start.rpmSlopeRpmPerSecond + liveSlopeStep * frame;
    const liveLoad =
      start.signedLoadCoordinate === null
        ? null
        : start.signedLoadCoordinate + liveLoadStep * frame;
    const liveMap =
      start.manifoldPressurePaAbs === null
        ? null
        : start.manifoldPressurePaAbs + liveMapStep * frame;
    assertSourceAdmission(
      segment,
      liveRpm,
      liveSlope,
      liveLoad,
      liveMap,
      start.stateMask,
      sourceRpm,
      sourceSlope,
      sourceLoad,
      sourceMap,
      left.stateMask,
      left.transitionMask,
    );

    const pcmLeft = Math.floor(sourceFrame);
    const fractional = sourceFrame - pcmLeft > SOURCE_FRAME_EPSILON;
    for (let bus = 0; bus < tapes.length; ++bus) {
      const tape = tapes[bus];
      if (
        pcmLeft < 0 ||
        pcmLeft >= tape.length ||
        (fractional && pcmLeft + 1 >= tape.length) ||
        !Number.isFinite(tape[pcmLeft]) ||
        (fractional && !Number.isFinite(tape[pcmLeft + 1]))
      ) {
        fail(
          "continuous-atlas-source-pcm-uncovered",
          `active segment ${segment.id} cursor left its finite PCM artifacts`,
        );
      }
    }
    const step = liveRpm / sourceRpm;
    if (!Number.isFinite(step) || step <= 0) {
      fail(
        "continuous-atlas-source-step-invalid",
        "live/source RPM produced an invalid chronological cursor step",
      );
    }
    sourceFrame += step;
  }

  if (sourceFrame > segment.usableFrames.end) {
    fail(
      "continuous-atlas-active-segment-exhausted",
      `active segment ${segment.id} cannot complete this block without a handoff`,
    );
  }
  knotIndex = advanceKnotIndex(knots, knotIndex, sourceFrame);
  const left = knots[knotIndex];
  const right = knots[knotIndex + 1];
  const amount = (sourceFrame - left.frame) / (right.frame - left.frame);
  assertSourceAdmission(
    segment,
    end.rpm,
    end.rpmSlopeRpmPerSecond,
    end.signedLoadCoordinate,
    end.manifoldPressurePaAbs,
    end.stateMask,
    interpolate(left.rpm, right.rpm, amount),
    interpolate(left.rpmSlopeRpmPerSecond, right.rpmSlopeRpmPerSecond, amount),
    interpolate(left.signedLoadCoordinate, right.signedLoadCoordinate, amount),
    interpolate(left.manifoldPressurePaAbs, right.manifoldPressurePaAbs, amount),
    left.stateMask,
    left.transitionMask,
  );
  return { sourceFrame, knotIndex };
}

function renderPcm(
  segment,
  tapes,
  outputBuffers,
  firstSourceFrame,
  firstKnotIndex,
  frameCount,
  start,
  end,
) {
  const knots = segment.timeline;
  const liveRpmStep = (end.rpm - start.rpm) / frameCount;
  let sourceFrame = firstSourceFrame;
  let knotIndex = firstKnotIndex;
  for (let frame = 0; frame < frameCount; ++frame) {
    knotIndex = advanceKnotIndex(knots, knotIndex, sourceFrame);
    const leftKnot = knots[knotIndex];
    const rightKnot = knots[knotIndex + 1];
    const timelineAmount =
      (sourceFrame - leftKnot.frame) / (rightKnot.frame - leftKnot.frame);
    const sourceRpm = interpolate(leftKnot.rpm, rightKnot.rpm, timelineAmount);
    const pcmLeft = Math.floor(sourceFrame);
    const pcmAmount = sourceFrame - pcmLeft;
    for (let bus = 0; bus < tapes.length; ++bus) {
      outputBuffers[bus][frame] =
        pcmAmount <= SOURCE_FRAME_EPSILON
          ? tapes[bus][pcmLeft]
          : interpolate(tapes[bus][pcmLeft], tapes[bus][pcmLeft + 1], pcmAmount);
    }
    sourceFrame += (start.rpm + liveRpmStep * frame) / sourceRpm;
  }
}

// Pure canonical atlas-clock renderer. It owns no AudioContext, worker, device
// resampler, ring buffer, live-cycle scheduler, or handoff voice.
export class ContinuousAudioAtlasCursor {
  #atlas;
  #activeSegment = null;
  #activeTapes = null;
  #sourceFrame = null;
  #timelineKnotIndex = null;
  #renderedFrames = 0;
  #phaseSeeded = false;

  constructor(loadedAtlas) {
    this.#atlas = validateLoadedAtlas(loadedAtlas);
  }

  get sampleRate() {
    return this.#atlas.sampleRate;
  }

  get busIds() {
    return this.#atlas.busIds;
  }

  get activeSegmentId() {
    return this.#activeSegment?.id ?? null;
  }

  get sourceFrame() {
    return this.#sourceFrame;
  }

  reset() {
    this.#activeSegment = null;
    this.#activeTapes = null;
    this.#sourceFrame = null;
    this.#timelineKnotIndex = null;
    this.#renderedFrames = 0;
    this.#phaseSeeded = false;
  }

  initialize(liveStateValue) {
    const liveState = endpointState(liveStateValue, "liveState");
    const domain = this.#atlas.manifest.domain;
    const candidates = [];
    for (const segment of this.#atlas.movingSegments) {
      if (!segmentCovers(segment, liveState, domain)) {
        continue;
      }
      const candidate = candidateForSegment(segment, liveState);
      if (candidate !== null) {
        candidates.push(candidate);
      }
    }
    if (candidates.length === 0) {
      fail(
        "continuous-atlas-no-moving-segment",
        "no moving segment covers the live RPM, direction, state, normalized slope, and MAP/load state",
      );
    }
    candidates.sort((left, right) => left.score - right.score);
    const selected = candidates[0];
    this.#activeSegment = selected.segment;
    this.#activeTapes = this.#atlas.busIds.map(
      (busId) => selected.segment.bus(busId).pcm,
    );
    this.#sourceFrame = selected.sourceFrame;
    this.#timelineKnotIndex = selected.source.knotIndex;
    this.#renderedFrames = 0;
    this.#phaseSeeded = selected.phaseSeeded;
    return Object.freeze({
      segmentId: selected.segment.id,
      sourceFrame: selected.sourceFrame,
      sourceRpm: selected.source.rpm,
      phaseSeeded: selected.phaseSeeded,
    });
  }

  createOutputBuffers(frameCapacity) {
    if (!Number.isSafeInteger(frameCapacity) || frameCapacity <= 0) {
      throw new RangeError("frameCapacity must be a positive safe integer");
    }
    return Object.freeze(
      this.#atlas.busIds.map(() => new Float32Array(frameCapacity)),
    );
  }

  renderBlockInto({ frameCount, start, end = start, outputBuffers }) {
    if (
      this.#activeSegment === null ||
      this.#activeTapes === null ||
      this.#sourceFrame === null ||
      this.#timelineKnotIndex === null
    ) {
      fail(
        "continuous-atlas-cursor-uninitialized",
        "initialize the continuous atlas cursor before rendering",
      );
    }
    if (!Number.isSafeInteger(frameCount) || frameCount <= 0) {
      throw new RangeError("frameCount must be a positive safe integer");
    }
    validateOutputBuffers(outputBuffers, this.#activeTapes, frameCount);
    const startState = endpointState(start, "start");
    const endState = endpointState(end, "end");
    const segment = this.#activeSegment;
    validateBlockEndpoints(startState, endState, segment, this.#atlas.manifest.domain);

    // The first pass validates the complete transaction without touching caller
    // output or public cursor state. The second pass cannot discover new input.
    const firstSourceFrame = this.#sourceFrame;
    const firstKnotIndex = this.#timelineKnotIndex;
    const next = preflightBlock(
      segment,
      this.#activeTapes,
      firstSourceFrame,
      firstKnotIndex,
      frameCount,
      startState,
      endState,
    );
    renderPcm(
      segment,
      this.#activeTapes,
      outputBuffers,
      firstSourceFrame,
      firstKnotIndex,
      frameCount,
      startState,
      endState,
    );
    this.#sourceFrame = next.sourceFrame;
    this.#timelineKnotIndex = next.knotIndex;
    this.#renderedFrames += frameCount;
    return Object.freeze({
      segmentId: segment.id,
      firstSourceFrame,
      nextSourceFrame: next.sourceFrame,
      frameCount,
      outputBuffers,
    });
  }

  // Deliberately named: audition scripts may use this convenience, while the
  // worker hot path must retain and pass createOutputBuffers() storage itself.
  renderBlockAllocated({ frameCount, start, end = start }) {
    const outputBuffers = this.createOutputBuffers(frameCount);
    const result = this.renderBlockInto({
      frameCount,
      start,
      end,
      outputBuffers,
    });
    return Object.freeze({
      ...result,
      buses: Object.freeze(
        this.#atlas.busIds.map((id, index) =>
          Object.freeze({ id, samples: outputBuffers[index] }),
        ),
      ),
      bus: (id) => {
        const index = this.#atlas.busIds.indexOf(id);
        return index === -1 ? null : outputBuffers[index];
      },
    });
  }

  diagnostics() {
    return Object.freeze({
      sampleRate: this.#atlas.sampleRate,
      activeSegmentId: this.#activeSegment?.id ?? null,
      sourceFrame: this.#sourceFrame,
      timelineKnotIndex: this.#timelineKnotIndex,
      renderedFrames: this.#renderedFrames,
      phaseSeeded: this.#phaseSeeded,
      handoffCount: 0,
      timeOwner: "continuous-source-cursor",
    });
  }
}
