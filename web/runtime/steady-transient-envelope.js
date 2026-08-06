const CANONICAL_SAMPLE_RATE_HZ = 192_000;
const THROTTLE_WINDOW_FRAMES = 15_360; // 80 ms
const THROTTLE_HISTORY_MINIMUM_FRAMES = 11_520; // 75% of the window
const REFRACTORY_FRAMES = 48_000; // 250 ms
const OPPOSITE_RETURN_FRAMES = 5_760; // 30 ms through the steady bed

const RISING_PROFILE = Object.freeze({
  direction: "rising",
  maximumGain: 0.75,
  attackFrames: 5_760,
  holdFrames: 17_280,
  releaseFrames: 63_360,
  totalFrames: 86_400,
});

const FALLING_PROFILE = Object.freeze({
  direction: "falling",
  maximumGain: 0.65,
  attackFrames: 3_840,
  holdFrames: 15_360,
  releaseFrames: 80_640,
  totalFrames: 99_840,
});

export const STEADY_TRANSIENT_POLICY = Object.freeze({
  sampleRateHz: CANONICAL_SAMPLE_RATE_HZ,
  throttleWindowFrames: THROTTLE_WINDOW_FRAMES,
  throttleWindowSeconds:
    THROTTLE_WINDOW_FRAMES / CANONICAL_SAMPLE_RATE_HZ,
  throttleDeltaOnset: 0.30,
  throttleDeltaFull: 0.70,
  throttleDeltaRearm: 0.12,
  refractoryFrames: REFRACTORY_FRAMES,
  refractorySeconds: REFRACTORY_FRAMES / CANONICAL_SAMPLE_RATE_HZ,
  oppositeReturnFrames: OPPOSITE_RETURN_FRAMES,
  oppositeReturnSeconds:
    OPPOSITE_RETURN_FRAMES / CANONICAL_SAMPLE_RATE_HZ,
  rising: RISING_PROFILE,
  falling: FALLING_PROFILE,
  diagnosticMacroSlopeWindowFrames: 23_040,
  diagnosticGentleSlopeCeilingRpmPerSecond: 1_500,
  diagnosticSharpSlopeThresholdRpmPerSecond: 2_500,
  diagnosticSharpSlopeConfirmationFrames: 19_200,
});

function fail(message) {
  throw new TypeError(message);
}

function finite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail(`${label} must be finite`);
  }
  return value;
}

function unitInterval(value, label) {
  const result = finite(value, label);
  if (result < 0 || result > 1) {
    fail(`${label} must be in [0, 1]`);
  }
  return result;
}

function canonicalFrame(value, label) {
  if (Number.isSafeInteger(value?.frame) && value.frame >= 0) {
    return value.frame;
  }
  if (
    Number.isSafeInteger(value?.canonicalFrame) &&
    value.canonicalFrame >= 0
  ) {
    return value.canonicalFrame;
  }
  if (typeof value?.timeSeconds === "number" && value.timeSeconds >= 0) {
    const frame = value.timeSeconds * CANONICAL_SAMPLE_RATE_HZ;
    const rounded = Math.round(frame);
    if (Number.isSafeInteger(rounded) && Math.abs(frame - rounded) <= 1e-7) {
      return rounded;
    }
  }
  fail(
    `${label} must provide a nonnegative safe-integer canonical frame ` +
      "or an exactly representable canonical timeSeconds",
  );
}

function endpoint(value, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  const frame = canonicalFrame(value, label);
  const requestedThrottle01 = unitInterval(
    value.requestedThrottle01,
    `${label}.requestedThrottle01`,
  );
  const rpmSlopeRpmPerSecond = value.rpmSlopeRpmPerSecond === undefined
    ? null
    : finite(
        value.rpmSlopeRpmPerSecond,
        `${label}.rpmSlopeRpmPerSecond`,
      );
  return Object.freeze({
    frame,
    timeSeconds: frame / CANONICAL_SAMPLE_RATE_HZ,
    requestedThrottle01,
    rpmSlopeRpmPerSecond,
  });
}

function smoothstep01(amount) {
  const x = Math.max(0, Math.min(1, amount));
  return x * x * (3 - 2 * x);
}

function halfCosineIn(amount) {
  const x = Math.max(0, Math.min(1, amount));
  return 0.5 - 0.5 * Math.cos(Math.PI * x);
}

function halfCosineOut(amount) {
  return 1 - halfCosineIn(amount);
}

function profile(direction) {
  return direction === "rising" ? RISING_PROFILE : FALLING_PROFILE;
}

function directionName(direction) {
  return direction > 0 ? "rising" : "falling";
}

function eventGain(event, frame) {
  const shape = profile(event.direction);
  const age = frame - event.startFrame;
  if (age <= 0 || age >= shape.totalFrames) return 0;
  if (age < shape.attackFrames) {
    return event.strength * shape.maximumGain *
      halfCosineIn(age / shape.attackFrames);
  }
  if (age < shape.attackFrames + shape.holdFrames) {
    return event.strength * shape.maximumGain;
  }
  const releaseAge = age - shape.attackFrames - shape.holdFrames;
  return event.strength * shape.maximumGain *
    halfCosineOut(releaseAge / shape.releaseFrames);
}

function eventSnapshot(event) {
  if (event === null) return null;
  return Object.freeze({
    direction: event.direction,
    strength: event.strength,
    startFrame: event.startFrame,
    startTimeSeconds: event.startFrame / CANONICAL_SAMPLE_RATE_HZ,
    admittedFrame: event.admittedFrame,
    admittedTimeSeconds: event.admittedFrame / CANONICAL_SAMPLE_RATE_HZ,
    throttleDelta80ms: event.throttleDelta80ms,
  });
}

function outputBuffer(value, frameCount, label) {
  if (value === undefined) return new Float32Array(frameCount);
  if (!(value instanceof Float32Array) || value.length < frameCount) {
    fail(`${label} must be a Float32Array with at least frameCount elements`);
  }
  return value.length === frameCount ? value : value.subarray(0, frameCount);
}

// This class intentionally never selects transient material from RPM slope.
// Slope is retained only in diagnostics because it confirms a throttle attack
// roughly 200-260 ms too late and can have the opposite sign at gesture onset.
export class SteadyTransientEnvelope {
  #audible = false;
  #lastEndpoint = null;
  #throttleHistory = [];
  #priorEligibleDirection = 0;
  #lastAdmittedFrame = Number.NEGATIVE_INFINITY;
  #activeEvent = null;
  #returnState = null;
  #pendingEvent = null;
  #lastThrottleDelta80ms = 0;
  #lastRpmSlopeRpmPerSecond = null;
  #maximumAbsoluteRpmSlopeRpmPerSecond = 0;
  #admittedEventCount = 0;
  #suppressedEventCount = 0;
  #oppositeReturnCount = 0;
  #lastAdmittedEvent = null;

  reset() {
    this.#audible = false;
    this.#lastEndpoint = null;
    this.#throttleHistory.length = 0;
    this.#priorEligibleDirection = 0;
    this.#lastAdmittedFrame = Number.NEGATIVE_INFINITY;
    this.#activeEvent = null;
    this.#returnState = null;
    this.#pendingEvent = null;
    this.#lastThrottleDelta80ms = 0;
    this.#lastRpmSlopeRpmPerSecond = null;
    this.#maximumAbsoluteRpmSlopeRpmPerSecond = 0;
    this.#admittedEventCount = 0;
    this.#suppressedEventCount = 0;
    this.#oppositeReturnCount = 0;
    this.#lastAdmittedEvent = null;
  }

  beginAudible(value) {
    const initial = endpoint(value, "initial endpoint");
    this.reset();
    this.#audible = true;
    this.#lastEndpoint = initial;
    this.#throttleHistory.push(initial);
    this.#observeSlope(initial);
    return this.diagnostics();
  }

  processBlock({
    frameCount,
    start,
    end,
    risingGainOutput,
    fallingGainOutput,
  }) {
    if (!this.#audible || this.#lastEndpoint === null) {
      throw new Error("beginAudible() must seed the live gesture history first");
    }
    if (!Number.isSafeInteger(frameCount) || frameCount <= 0) {
      fail("frameCount must be a positive safe integer");
    }
    const normalizedStart = endpoint(start, "start endpoint");
    const normalizedEnd = endpoint(end, "end endpoint");
    if (
      normalizedStart.frame !== this.#lastEndpoint.frame ||
      normalizedStart.requestedThrottle01 !==
        this.#lastEndpoint.requestedThrottle01
    ) {
      throw new RangeError(
        "live envelope blocks must begin at the previously committed endpoint",
      );
    }
    if (normalizedEnd.frame - normalizedStart.frame !== frameCount) {
      throw new RangeError(
        "end endpoint must be exactly frameCount canonical frames after start",
      );
    }

    const rising = outputBuffer(
      risingGainOutput,
      frameCount,
      "risingGainOutput",
    );
    const falling = outputBuffer(
      fallingGainOutput,
      frameCount,
      "fallingGainOutput",
    );
    for (let offset = 0; offset < frameCount; ++offset) {
      const frame = normalizedStart.frame + offset;
      this.#advanceState(frame);
      const gains = this.#gainsAt(frame);
      rising[offset] = Math.fround(gains.rising);
      falling[offset] = Math.fround(gains.falling);
    }
    this.#advanceState(normalizedEnd.frame);
    const admittedEvent = this.#observeEndpoint(normalizedEnd);
    this.#lastEndpoint = normalizedEnd;
    return Object.freeze({
      risingGain: rising,
      fallingGain: falling,
      admittedEvent,
      diagnostics: this.diagnostics(),
    });
  }

  diagnostics() {
    const frame = this.#lastEndpoint?.frame ?? null;
    if (frame !== null) this.#advanceState(frame);
    const gains = frame === null
      ? { rising: 0, falling: 0 }
      : this.#gainsAt(frame);
    return Object.freeze({
      audible: this.#audible,
      frame,
      timeSeconds:
        frame === null ? null : frame / CANONICAL_SAMPLE_RATE_HZ,
      requestedThrottle01:
        this.#lastEndpoint?.requestedThrottle01 ?? null,
      throttleDelta80ms: this.#lastThrottleDelta80ms,
      priorEligibleDirection:
        this.#priorEligibleDirection === 0
          ? null
          : directionName(this.#priorEligibleDirection),
      activeDirection:
        this.#returnState?.direction ?? this.#activeEvent?.direction ?? null,
      pendingDirection: this.#pendingEvent?.direction ?? null,
      returningThroughSteady: this.#returnState !== null,
      risingGain: gains.rising,
      fallingGain: gains.falling,
      lastRpmSlopeRpmPerSecond: this.#lastRpmSlopeRpmPerSecond,
      maximumAbsoluteRpmSlopeRpmPerSecond:
        this.#maximumAbsoluteRpmSlopeRpmPerSecond,
      rpmSlopeAffectsEnvelope: false,
      admittedEventCount: this.#admittedEventCount,
      suppressedEventCount: this.#suppressedEventCount,
      oppositeReturnCount: this.#oppositeReturnCount,
      lastAdmittedEvent: eventSnapshot(this.#lastAdmittedEvent),
    });
  }

  #observeSlope(value) {
    if (value.rpmSlopeRpmPerSecond === null) return;
    this.#lastRpmSlopeRpmPerSecond = value.rpmSlopeRpmPerSecond;
    this.#maximumAbsoluteRpmSlopeRpmPerSecond = Math.max(
      this.#maximumAbsoluteRpmSlopeRpmPerSecond,
      Math.abs(value.rpmSlopeRpmPerSecond),
    );
  }

  #observeEndpoint(value) {
    this.#observeSlope(value);
    this.#throttleHistory.push(value);
    const targetFrame = value.frame - THROTTLE_WINDOW_FRAMES;
    let baseline = this.#throttleHistory[0];
    for (const candidate of this.#throttleHistory) {
      if (candidate.frame > targetFrame) break;
      baseline = candidate;
    }
    while (
      this.#throttleHistory.length > 2 &&
      this.#throttleHistory[1].frame <= targetFrame
    ) {
      this.#throttleHistory.shift();
      baseline = this.#throttleHistory[0];
    }
    if (value.frame - baseline.frame < THROTTLE_HISTORY_MINIMUM_FRAMES) {
      return null;
    }

    const delta = value.requestedThrottle01 - baseline.requestedThrottle01;
    this.#lastThrottleDelta80ms = delta;
    const magnitude = Math.abs(delta);
    const eligibleDirection = delta > STEADY_TRANSIENT_POLICY.throttleDeltaOnset
      ? 1
      : delta < -STEADY_TRANSIENT_POLICY.throttleDeltaOnset
        ? -1
        : 0;
    let admitted = null;
    if (
      eligibleDirection !== 0 &&
      eligibleDirection !== this.#priorEligibleDirection
    ) {
      const strength = smoothstep01(
        (magnitude - STEADY_TRANSIENT_POLICY.throttleDeltaOnset) /
          (STEADY_TRANSIENT_POLICY.throttleDeltaFull -
            STEADY_TRANSIENT_POLICY.throttleDeltaOnset),
      );
      admitted = this.#admitGesture({
        direction: directionName(eligibleDirection),
        strength,
        admittedFrame: value.frame,
        startFrame: value.frame,
        throttleDelta80ms: delta,
      });
    }
    if (magnitude < STEADY_TRANSIENT_POLICY.throttleDeltaRearm) {
      this.#priorEligibleDirection = 0;
    } else if (eligibleDirection !== 0) {
      this.#priorEligibleDirection = eligibleDirection;
    }
    return admitted;
  }

  #admitGesture(candidate) {
    this.#advanceState(candidate.admittedFrame);
    const currentDirection =
      this.#returnState?.direction ?? this.#activeEvent?.direction ?? null;
    const opposite =
      currentDirection !== null && currentDirection !== candidate.direction;

    if (this.#returnState !== null) {
      if (this.#pendingEvent?.direction === candidate.direction) {
        ++this.#suppressedEventCount;
        return null;
      }
      const delayed = {
        ...candidate,
        startFrame: this.#returnState.endFrame,
      };
      this.#pendingEvent = delayed;
      this.#lastAdmittedFrame = candidate.admittedFrame;
      this.#lastAdmittedEvent = delayed;
      ++this.#admittedEventCount;
      return eventSnapshot(delayed);
    }

    if (opposite) {
      const currentGain = eventGain(
        this.#activeEvent,
        candidate.admittedFrame,
      );
      if (currentGain > 0) {
        const returnEndFrame =
          candidate.admittedFrame + OPPOSITE_RETURN_FRAMES;
        this.#returnState = {
          direction: this.#activeEvent.direction,
          startFrame: candidate.admittedFrame,
          endFrame: returnEndFrame,
          startGain: currentGain,
        };
        this.#activeEvent = null;
        this.#pendingEvent = {
          ...candidate,
          startFrame: returnEndFrame,
        };
        ++this.#oppositeReturnCount;
      } else {
        this.#activeEvent = candidate;
      }
      this.#lastAdmittedFrame = candidate.admittedFrame;
      this.#lastAdmittedEvent = this.#pendingEvent ?? candidate;
      ++this.#admittedEventCount;
      return eventSnapshot(this.#lastAdmittedEvent);
    }

    if (
      currentDirection === candidate.direction ||
      candidate.admittedFrame - this.#lastAdmittedFrame < REFRACTORY_FRAMES
    ) {
      ++this.#suppressedEventCount;
      return null;
    }
    this.#activeEvent = candidate;
    this.#lastAdmittedFrame = candidate.admittedFrame;
    this.#lastAdmittedEvent = candidate;
    ++this.#admittedEventCount;
    return eventSnapshot(candidate);
  }

  #advanceState(frame) {
    if (this.#returnState !== null && frame >= this.#returnState.endFrame) {
      const startFrame = this.#returnState.endFrame;
      const pending = this.#pendingEvent;
      this.#returnState = null;
      this.#pendingEvent = null;
      this.#activeEvent = pending === null
        ? null
        : { ...pending, startFrame };
    }
    if (
      this.#activeEvent !== null &&
      frame - this.#activeEvent.startFrame >=
        profile(this.#activeEvent.direction).totalFrames
    ) {
      this.#activeEvent = null;
    }
  }

  #gainsAt(frame) {
    if (this.#returnState !== null) {
      const amount =
        (frame - this.#returnState.startFrame) / OPPOSITE_RETURN_FRAMES;
      const gain = this.#returnState.startGain * halfCosineOut(amount);
      return this.#returnState.direction === "rising"
        ? { rising: gain, falling: 0 }
        : { rising: 0, falling: gain };
    }
    if (this.#activeEvent === null) return { rising: 0, falling: 0 };
    const gain = eventGain(this.#activeEvent, frame);
    return this.#activeEvent.direction === "rising"
      ? { rising: gain, falling: 0 }
      : { rising: 0, falling: gain };
  }
}
