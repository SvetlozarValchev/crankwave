import assert from "node:assert/strict";
import test from "node:test";

import {
  ControlCapability,
  SessionExecutionKind,
} from "../../runtime/c-api-abi.js";
import { liveExecutionKindForScenarioJson } from "../../runtime/browser-engine-runtime.js";
import {
  LIVE_CONTROL_CAPABILITIES,
  WORKER_PROTOCOL_ID,
  liveControlCapability,
  publicDescriptor,
} from "../../runtime/protocol.js";

test("dynamic operating-bench modes use open-ended live execution", () => {
  for (const type of ["free_engine", "held_dyno", "free_vehicle"]) {
    assert.equal(
      liveExecutionKindForScenarioJson(JSON.stringify({ mode: { type } })),
      SessionExecutionKind.openEnded,
    );
  }
});

test("authored capture-only modes remain finite", () => {
  for (const type of [
    "held_speed",
    "prescribed_kinematic_sweep",
    "load_target_held_capture",
    "inertial_dyno",
  ]) {
    assert.equal(
      liveExecutionKindForScenarioJson(
        JSON.stringify({ mode: { type } }),
      ),
      SessionExecutionKind.finiteScenario,
    );
  }
});

test("malformed JSON remains on the native diagnostic path", () => {
  assert.equal(
    liveExecutionKindForScenarioJson('{"mode":'),
    SessionExecutionKind.finiteScenario,
  );
});

test("execution selection requires JSON text", () => {
  assert.throws(
    () => liveExecutionKindForScenarioJson(null),
    /scenarioJson must be an exact JSON string/u,
  );
});

test("Worker protocol v2 publishes the complete typed control vocabulary", () => {
  assert.equal(WORKER_PROTOCOL_ID, "engine-sim-offline/browser-worker-v2");
  assert.deepEqual(
    LIVE_CONTROL_CAPABILITIES.map(({ kind }) => kind),
    [
      "throttle",
      "ignition",
      "fuel",
      "starter",
      "limiter",
      "external-resisting-torque",
      "held-dyno-target-engine-speed",
      "held-dyno-maximum-absorbing-torque",
      "held-dyno-maximum-driving-torque",
      "vehicle-selected-forward-gear",
      "vehicle-clutch-engagement",
      "vehicle-service-brake-application",
    ],
  );
  assert.equal(
    liveControlCapability("vehicle-selected-forward-gear").mask,
    ControlCapability.vehicleSelectedForwardGear,
  );
  assert.equal(liveControlCapability("not-a-control"), null);
});

test("public descriptor carries explicit motion and forward-gear inventory", () => {
  const forwardGear = {
    gearId: 17,
    authoredOrdinal: 1,
    semanticId: "gear-1",
    ratio: 4.21,
  };
  const program = {
    session: {
      descriptor: {
        totalBlockCountBigInt: 12n,
        preparationBlockCountBigInt: 2n,
        deliveryFramesPerBlock: 3_840,
        executionKind: "finite-scenario",
        executionKindCode: SessionExecutionKind.finiteScenario,
        motionMode: "free-vehicle",
        motionModeCode: 7,
        liveControlCapabilities:
          ControlCapability.throttle |
          ControlCapability.vehicleSelectedForwardGear,
        forwardGearCount: 1,
        forwardGears: [forwardGear],
        deliveryRateHz: 192_000,
        maximumDeliveryFramesPerProcessCall: 3_840,
        totalBlockCount: "12",
        preparationBlockCount: "2",
        engineId: "engine",
        scenarioId: "scenario",
      },
      buses: [],
    },
  };

  const descriptor = publicDescriptor(program, 0);
  assert.equal(descriptor.motionMode, "free-vehicle");
  assert.equal(descriptor.motionModeCode, 7);
  assert.equal(descriptor.forwardGearCount, 1);
  assert.deepEqual(descriptor.forwardGears, [forwardGear]);
  assert.notEqual(descriptor.forwardGears[0], forwardGear);
  assert.equal(descriptor.totalDeliveryFrames, "46080");
  assert.equal(descriptor.preparationDeliveryFrames, "7680");
  assert.deepEqual(
    descriptor.controls.map(({ kind }) => kind),
    ["throttle", "vehicle-selected-forward-gear"],
  );
});
