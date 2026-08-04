import assert from "node:assert/strict";
import test from "node:test";

import {
  ControlCapability,
  SessionExecutionKind,
} from "../../runtime/c-api-abi.js";
import { BrowserEngineRuntime } from "../../runtime/browser-engine-runtime.js";
import {
  LIVE_CONTROL_CAPABILITIES,
  WORKER_PROTOCOL_ID,
  liveControlCapability,
  publicDescriptor,
} from "../../runtime/protocol.js";

test("browser build forwards explicit execution kind without mode inference", () => {
  const observed = [];
  const sentinel = new Error("stop after compile boundary");
  const runtime = new BrowserEngineRuntime(
    {
      compile(engineJson, scenarioJson, assets, executionKind) {
        observed.push({ engineJson, scenarioJson, assets, executionKind });
        throw sentinel;
      },
    },
    () => {},
  );
  const cases = [
    {
      scenarioJson: '{"mode":{"type":"free_engine"}}',
      executionKind: SessionExecutionKind.finiteScenario,
    },
    {
      scenarioJson: '{"mode":{"type":"held_speed"}}',
      executionKind: SessionExecutionKind.openEnded,
    },
    {
      scenarioJson: '{"mode":',
      executionKind: SessionExecutionKind.openEnded,
    },
  ];

  for (const [index, entry] of cases.entries()) {
    assert.throws(
      () =>
        runtime.build({
          requestId: `build-${index}`,
          engineJson: "{}",
          scenarioJson: entry.scenarioJson,
          assets: [],
          executionKind: entry.executionKind,
        }),
      (error) => error === sentinel,
    );
  }

  assert.deepEqual(
    observed.map(({ scenarioJson, executionKind }) => ({
      scenarioJson,
      executionKind,
    })),
    cases,
  );
});

test("Worker protocol v4 publishes the complete typed control vocabulary", () => {
  assert.equal(WORKER_PROTOCOL_ID, "engine-sim-offline/browser-worker-v4");
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
