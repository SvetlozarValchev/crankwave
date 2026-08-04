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

function publicState(state) {
  return Object.freeze({ ...state });
}

function endpoint(frame, telemetry) {
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
    signedLoad: null,
    ignitionEnabled: telemetry.ignitionEnabled === true,
    fuelEnabled: telemetry.fuelEnabled === true,
    starterEnabled: telemetry.starterEnabled === true,
    limiterEnabled: telemetry.limiterEnabled === true,
    limiterCutActive: telemetry.limiterCutActive === true,
  };
}

// Converts the authoritative EngineSession timeline into the state clock consumed
// by the derived package follower. It never predicts drivetrain motion. Load state
// follows requested throttle continuously; completed-cycle net torque is a delayed
// result of engine acceleration/load and must not drive responsive source selection.
export class SourceBakedSessionClock {
  #previousEndpoint = null;
  #blockCount = 0;

  acceptBlock(block) {
    if (!block || typeof block !== "object") {
      throw new TypeError("block must be an EngineSession process block");
    }
    if (!Array.isArray(block.telemetry) || block.telemetry.length !== 1) {
      throw new RangeError(
        "responsive package following requires exactly one session telemetry endpoint per block",
      );
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

    const current = endpoint(endFrame, block.telemetry[0]);
    const start =
      this.#previousEndpoint ??
      endpoint(firstFrame, block.telemetry[0]);
    this.#previousEndpoint = current;
    ++this.#blockCount;
    return Object.freeze({
      start: publicState(start),
      end: publicState(current),
    });
  }

  reset() {
    this.#previousEndpoint = null;
    this.#blockCount = 0;
  }

  diagnostics() {
    return Object.freeze({
      blockCount: this.#blockCount,
      nextDeliveryFrame: this.#previousEndpoint?.deliveryFrame ?? null,
    });
  }
}
