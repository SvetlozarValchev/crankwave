import assert from "node:assert/strict";
import test from "node:test";

import { SourceBakedSessionClock } from "../../runtime/source-baked-session-clock.js";

function units(coordinate, torqueOffset) {
  return [1_000, 2_000].map((rpm) => ({
    canonical_rpm: rpm,
    average_net_torque_nm: torqueOffset + rpm / 100,
    average_signed_load: coordinate,
  }));
}

function manifest() {
  return {
    running: {
      load_calibration: {
        signal: "cycle-mean-integrated-instantaneous-net-shaft",
        completeness: "incomplete",
        included_terms: "135",
        omitted_terms: "120",
      },
      planes: [
        { load_coordinate: -1, units: units(-1, -100) },
        { load_coordinate: 0, units: units(0, 0) },
        { load_coordinate: 1, units: units(1, 100) },
      ],
    },
  };
}

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

test("session clock publishes explicit contiguous physical delivery endpoints", () => {
  const clock = new SourceBakedSessionClock(manifest());
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
  assert.equal(second.end.rpm, 1_400);
  assert.equal(clock.diagnostics().throttleFallbackBlockCount, 2);
});

test("matching cycle-mean torque accounting maps through package plane knots", () => {
  const clock = new SourceBakedSessionClock(manifest());
  const sourceClock = clock.acceptBlock(block(0, 1_500, 0.9, [cycle()]));
  assert.equal(sourceClock.end.signedLoad, 0);
  assert.equal(clock.diagnostics().latestLoadOrdinal, "7");
  assert.equal(clock.diagnostics().torqueLoadBlockCount, 1);

  clock.reset();
  const halfway = clock.acceptBlock(
    block(0, 1_500, 0.9, [cycle({ torqueNm: 65 })]),
  );
  assert.equal(halfway.end.signedLoad, 0.5);
});

test("mismatched or unavailable torque metadata falls back to requested throttle", () => {
  const clock = new SourceBakedSessionClock(manifest());
  const sourceClock = clock.acceptBlock(
    block(0, 1_500, 0.45, [cycle({ includedTerms: "255", omittedTerms: "0" })]),
  );
  assert.equal(sourceClock.end.signedLoad, null);
  assert.equal(sourceClock.end.throttle01, 0.45);
  assert.equal(clock.diagnostics().throttleFallbackBlockCount, 1);
});

test("session discontinuities and ambiguous telemetry blocks are rejected", () => {
  const clock = new SourceBakedSessionClock(manifest());
  clock.acceptBlock(block(0, 1_000, 0.2));
  assert.throws(() => clock.acceptBlock(block(4_000, 1_100, 0.2)), /discontinuity/);
  assert.throws(
    () =>
      new SourceBakedSessionClock(manifest()).acceptBlock({
        ...block(0, 1_000, 0.2),
        telemetry: [],
      }),
    /exactly one session telemetry endpoint/,
  );
});
