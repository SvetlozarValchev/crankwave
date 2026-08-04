import assert from "node:assert/strict";
import test from "node:test";

import { SourceBakedSessionClock } from "../../runtime/source-baked-session-clock.js";

function telemetry(rpm, throttle01) {
  return {
    engineSpeedRpm: rpm,
    requestedThrottle01: throttle01,
    ignitionEnabled: true,
    fuelEnabled: true,
    starterEnabled: false,
    limiterEnabled: true,
    limiterCutActive: false,
  };
}

function block(firstDeliveryFrame, rpm, throttle01, completedCycles = []) {
  return {
    process: {
      firstDeliveryFrame: String(firstDeliveryFrame),
      deliveryFrameCount: 3_840,
    },
    telemetry: [telemetry(rpm, throttle01)],
    completedCycles,
  };
}

function cycle({ torqueNm = 15, includedTerms = "135", omittedTerms = "120" } = {}) {
  return {
    completedCycleOrdinal: "7",
    meanEngineSpeedRpm: 1_500,
    instantaneousNetShaft: {
      cycleMeanTorqueNm: torqueNm,
      availability: 1,
      completeness: 0,
      includedTerms,
      omittedTerms,
    },
  };
}

test("session clock publishes explicit contiguous throttle delivery endpoints", () => {
  const clock = new SourceBakedSessionClock();
  const first = clock.acceptBlock(block(0, 1_200, 0.25));
  assert.deepEqual(
    [first.start.deliveryFrame, first.end.deliveryFrame],
    [0, 3_840],
  );
  assert.equal(first.start.rpm, 1_200);
  assert.equal(first.end.signedLoad, null);

  const second = clock.acceptBlock(block(3_840, 1_400, 0.5));
  assert.deepEqual(
    [second.start.deliveryFrame, second.end.deliveryFrame],
    [3_840, 7_680],
  );
  assert.equal(second.start.rpm, 1_200);
  assert.equal(second.start.throttle01, 0.25);
  assert.equal(second.end.rpm, 1_400);
  assert.equal(second.end.throttle01, 0.5);
  assert.equal(second.start.signedLoad, null);
  assert.equal(second.end.signedLoad, null);
  assert.equal(clock.diagnostics().blockCount, 2);
});

test("completed-cycle net torque never overrides requested throttle", () => {
  const clock = new SourceBakedSessionClock();
  const sourceClock = clock.acceptBlock(block(0, 1_500, 0.45, [cycle()]));
  assert.equal(sourceClock.end.signedLoad, null);
  assert.equal(sourceClock.end.throttle01, 0.45);
  assert.deepEqual(clock.diagnostics(), {
    blockCount: 1,
    nextDeliveryFrame: 3_840,
  });
});

test("session discontinuities and ambiguous telemetry blocks are rejected", () => {
  const clock = new SourceBakedSessionClock();
  clock.acceptBlock(block(0, 1_000, 0.2));
  assert.throws(() => clock.acceptBlock(block(4_000, 1_100, 0.2)), /discontinuity/);
  assert.throws(
    () =>
      new SourceBakedSessionClock().acceptBlock({
        ...block(0, 1_000, 0.2),
        telemetry: [],
      }),
    /exactly one session telemetry endpoint/,
  );
});
