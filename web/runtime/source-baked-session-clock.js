function finite(value, name) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError(`${name} must be finite`);
  }
  return value;
}

function safeFrame(value, name) {
  let parsed;
  try {
    parsed = BigInt(value);
  } catch (error) {
    throw new TypeError(`${name} must be an unsigned frame`, { cause: error });
  }
  if (parsed < 0n || parsed > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new RangeError(`${name} is outside the exact JavaScript frame range`);
  }
  return Number(parsed);
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function interpolateUnitField(units, rpm, field) {
  if (rpm <= units[0].canonical_rpm) {
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

function publicState(state) {
  return Object.freeze({ ...state });
}

function evidenceMatchesCalibration(evidence, calibration) {
  if (!evidence || typeof evidence !== "object") {
    return false;
  }
  const expectedCompleteness = calibration.completeness === "complete" ? 1 : 0;
  return (
    evidence.availability === 1 &&
    evidence.completeness === expectedCompleteness &&
    evidence.includedTerms === calibration.included_terms &&
    evidence.omittedTerms === calibration.omitted_terms &&
    Number.isFinite(evidence.cycleMeanTorqueNm)
  );
}

function torqueCoordinate(planes, rpm, torqueNm) {
  const knots = planes.map((plane) => ({
    coordinate: plane.load_coordinate,
    torqueNm: interpolateUnitField(
      plane.units,
      rpm,
      "average_net_torque_nm",
    ),
  }));
  if (torqueNm <= knots[0].torqueNm) {
    return knots[0].coordinate;
  }
  const last = knots.length - 1;
  if (torqueNm >= knots[last].torqueNm) {
    return knots[last].coordinate;
  }
  for (let index = 1; index < knots.length; ++index) {
    const right = knots[index];
    if (torqueNm > right.torqueNm) {
      continue;
    }
    const left = knots[index - 1];
    const span = right.torqueNm - left.torqueNm;
    if (!(span > 0)) {
      continue;
    }
    return interpolate(
      left.coordinate,
      right.coordinate,
      (torqueNm - left.torqueNm) / span,
    );
  }
  return null;
}

function endpoint(frame, telemetry, signedLoad) {
  if (!telemetry || typeof telemetry !== "object") {
    throw new TypeError("session telemetry endpoint must be an object");
  }
  const rpm = finite(telemetry.engineSpeedRpm, "telemetry.engineSpeedRpm");
  const throttle01 = finite(
    telemetry.requestedThrottle01,
    "telemetry.requestedThrottle01",
  );
  if (rpm < 0 || throttle01 < 0 || throttle01 > 1) {
    throw new RangeError("session RPM/throttle endpoint is outside its domain");
  }
  return {
    deliveryFrame: frame,
    rpm,
    throttle01,
    signedLoad,
    ignitionEnabled: telemetry.ignitionEnabled === true,
    fuelEnabled: telemetry.fuelEnabled === true,
    starterEnabled: telemetry.starterEnabled === true,
    limiterEnabled: telemetry.limiterEnabled === true,
    limiterCutActive: telemetry.limiterCutActive === true,
  };
}

// Converts the authoritative EngineSession timeline into the state clock consumed
// by the derived package follower. It never predicts drivetrain motion. Exact
// cycle-mean torque is admitted only when its accounting tuple exactly matches the
// package calibration; all other cases deliberately fall back to requested throttle.
export class SourceBakedSessionClock {
  #manifest;
  #previousEndpoint = null;
  #latestSignedLoad = null;
  #latestLoadOrdinal = null;
  #blockCount = 0;
  #torqueLoadBlockCount = 0;
  #throttleFallbackBlockCount = 0;

  constructor(packageManifest) {
    const running = packageManifest?.running;
    if (
      !running ||
      !Array.isArray(running.planes) ||
      running.planes.length < 3 ||
      !running.load_calibration
    ) {
      throw new TypeError(
        "packageManifest must be a validated responsive audio package manifest",
      );
    }
    this.#manifest = packageManifest;
  }

  acceptBlock(block) {
    if (!block || typeof block !== "object") {
      throw new TypeError("block must be an EngineSession process block");
    }
    if (!Array.isArray(block.telemetry) || block.telemetry.length !== 1) {
      throw new RangeError(
        "responsive package following requires exactly one session telemetry endpoint per block",
      );
    }
    if (!Array.isArray(block.completedCycles)) {
      throw new TypeError("block.completedCycles must be an array");
    }
    const firstFrame = safeFrame(
      block.process?.firstDeliveryFrame,
      "block.process.firstDeliveryFrame",
    );
    const frameCount = safeFrame(
      block.process?.deliveryFrameCount,
      "block.process.deliveryFrameCount",
    );
    if (frameCount === 0) {
      throw new RangeError("session process blocks must contain delivery frames");
    }
    const endFrame = firstFrame + frameCount;
    if (!Number.isSafeInteger(endFrame)) {
      throw new RangeError("session delivery-frame endpoint overflowed");
    }
    if (
      this.#previousEndpoint !== null &&
      this.#previousEndpoint.deliveryFrame !== firstFrame
    ) {
      throw new RangeError(
        `session clock discontinuity: expected ${this.#previousEndpoint.deliveryFrame}, received ${firstFrame}`,
      );
    }

    const calibration = this.#manifest.running.load_calibration;
    for (const cycle of block.completedCycles) {
      const evidence = cycle?.instantaneousNetShaft;
      if (!evidenceMatchesCalibration(evidence, calibration)) {
        this.#latestSignedLoad = null;
        this.#latestLoadOrdinal = null;
        continue;
      }
      const coordinate = torqueCoordinate(
        this.#manifest.running.planes,
        finite(cycle.meanEngineSpeedRpm, "completedCycle.meanEngineSpeedRpm"),
        evidence.cycleMeanTorqueNm,
      );
      this.#latestSignedLoad = coordinate;
      this.#latestLoadOrdinal = cycle.completedCycleOrdinal;
    }

    const current = endpoint(
      endFrame,
      block.telemetry[0],
      this.#latestSignedLoad,
    );
    const start =
      this.#previousEndpoint ??
      endpoint(firstFrame, block.telemetry[0], this.#latestSignedLoad);
    this.#previousEndpoint = current;
    ++this.#blockCount;
    if (current.signedLoad === null) {
      ++this.#throttleFallbackBlockCount;
    } else {
      ++this.#torqueLoadBlockCount;
    }
    return Object.freeze({
      start: publicState(start),
      end: publicState(current),
    });
  }

  reset() {
    this.#previousEndpoint = null;
    this.#latestSignedLoad = null;
    this.#latestLoadOrdinal = null;
    this.#blockCount = 0;
    this.#torqueLoadBlockCount = 0;
    this.#throttleFallbackBlockCount = 0;
  }

  diagnostics() {
    return Object.freeze({
      blockCount: this.#blockCount,
      torqueLoadBlockCount: this.#torqueLoadBlockCount,
      throttleFallbackBlockCount: this.#throttleFallbackBlockCount,
      latestLoadOrdinal: this.#latestLoadOrdinal,
      latestSignedLoad: this.#latestSignedLoad,
      nextDeliveryFrame: this.#previousEndpoint?.deliveryFrame ?? null,
    });
  }
}

