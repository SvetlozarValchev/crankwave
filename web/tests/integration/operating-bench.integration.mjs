import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import {
  MotionMode,
  SessionExecutionKind,
} from "../../runtime/c-api-abi.js";
import { EngineSimCapiClient } from "../../runtime/c-api-client.js";

const HELD_DYNO_CAPABILITIES = 455;
const FREE_VEHICLE_CAPABILITIES = 3_631;
const EXPECTED_GEAR_RATIOS = Object.freeze([4.21, 2.49, 1.66, 1.24, 1]);

function usage() {
  return [
    "usage:",
    "  node web/tests/integration/operating-bench.integration.mjs \\",
    "    <engine-sim-offline.js> <engine.json> <held-dyno.json> \\",
    "    <free-vehicle.json> <audio-asset-id> <audio-asset> \\",
    "    <accessory-asset-id> <accessory-asset.json>",
  ].join("\n");
}

function requireFile(filePath, label) {
  const absolute = path.resolve(filePath);
  assert.ok(fs.statSync(absolute).isFile(), `${label} is not a file: ${absolute}`);
  return absolute;
}

function assertPreparationBlock(block, label) {
  assert.equal(block.process.kind, "block", `${label} completed during preparation`);
  assert.equal(block.process.blockPhase, "preparation");
  assert.equal(block.audible, false);
  assert.equal(block.telemetry.length, 1);
  assert.equal(block.telemetry[0].heldDyno, null);
  assert.equal(block.telemetry[0].freeVehicle, null);
}

function processToFirstAudibleBlock(session, label) {
  const preparationBlocks = session.descriptor.preparationBlockCountBigInt;
  assert.ok(preparationBlocks > 0n, `${label} did not declare preparation`);
  for (let block = 0n; block < preparationBlocks; ++block) {
    assertPreparationBlock(session.processBlock(), label);
  }

  const released = session.processBlock();
  assert.equal(released.process.kind, "block", `${label} completed at release`);
  assert.equal(released.process.blockPhase, "audible");
  assert.equal(released.audible, true);
  assert.equal(released.telemetry.length, 1);
  return released.telemetry[0];
}

function compileProgram(client, engineJson, scenarioJson, assets) {
  return client.compile(
    engineJson,
    scenarioJson,
    assets,
    SessionExecutionKind.openEnded,
  );
}

function testHeldDyno(program) {
  const { session } = program;
  const descriptor = session.descriptor;
  assert.equal(descriptor.executionKind, "open-ended");
  assert.equal(descriptor.totalBlockCount, null);
  assert.equal(descriptor.motionModeCode, MotionMode.heldDyno);
  assert.equal(descriptor.motionMode, "held-dyno");
  assert.equal(descriptor.liveControlCapabilities, HELD_DYNO_CAPABILITIES);
  assert.equal(descriptor.forwardGearCount, 0);
  assert.deepEqual(session.forwardGears, []);

  const deliveryFrame = session.firstAudibleDeliveryFrame;
  const controls = [
    {
      kind: "held-dyno-target-engine-speed",
      value: 1_750,
      deliveryFrame,
    },
    {
      kind: "held-dyno-maximum-absorbing-torque",
      value: 333,
      deliveryFrame,
    },
    {
      kind: "held-dyno-maximum-driving-torque",
      value: 17,
      deliveryFrame,
    },
  ];
  assert.equal(new Set(controls.map((control) => control.deliveryFrame)).size, 1);
  assert.deepEqual(session.enqueueControls(controls), { accepted: 3 });

  const telemetry = processToFirstAudibleBlock(session, "held-dyno session");
  assert.equal(telemetry.freeVehicle, null);
  assert.ok(telemetry.heldDyno !== null);
  assert.equal(telemetry.heldDyno.targetEngineSpeedRpm, 1_750);
  assert.equal(telemetry.heldDyno.maximumAbsorbingTorqueNm, 333);
  assert.equal(telemetry.heldDyno.maximumDrivingTorqueNm, 17);
  assert.ok(Number.isFinite(telemetry.heldDyno.requiredActuatorTorqueNm));
  assert.ok(Number.isFinite(telemetry.heldDyno.appliedActuatorTorqueNm));
  assert.ok(
    [
      "tracking",
      "absorbing-torque-limited",
      "driving-torque-limited",
    ].includes(telemetry.heldDyno.disposition),
  );
}

function testFreeVehicle(program) {
  const { session } = program;
  const descriptor = session.descriptor;
  assert.equal(descriptor.executionKind, "open-ended");
  assert.equal(descriptor.totalBlockCount, null);
  assert.equal(descriptor.motionModeCode, MotionMode.freeVehicle);
  assert.equal(descriptor.motionMode, "free-vehicle");
  assert.equal(descriptor.liveControlCapabilities, FREE_VEHICLE_CAPABILITIES);
  assert.equal(descriptor.forwardGearCount, 5);
  assert.equal(session.forwardGears.length, 5);
  assert.deepEqual(
    session.forwardGears.map(({ authoredOrdinal }) => authoredOrdinal),
    [1, 2, 3, 4, 5],
  );
  assert.deepEqual(
    session.forwardGears.map(({ semanticId }) => semanticId),
    ["gear-1", "gear-2", "gear-3", "gear-4", "gear-5"],
  );
  assert.deepEqual(
    session.forwardGears.map(({ ratio }) => ratio),
    EXPECTED_GEAR_RATIOS,
  );
  const stableGearIds = session.forwardGears.map(({ gearId }) => gearId);
  assert.ok(stableGearIds.every((gearId) => gearId !== 0));
  assert.equal(new Set(stableGearIds).size, stableGearIds.length);

  const deliveryFrame = session.firstAudibleDeliveryFrame;
  assert.throws(
    () =>
      session.enqueueControls([
        { kind: "throttle", value: 0.91, deliveryFrame },
        {
          kind: "vehicle-selected-forward-gear",
          value: descriptor.forwardGearCount + 1,
          deliveryFrame,
        },
      ]),
    /published authored ordinal/u,
    "an out-of-inventory gear did not reject its complete batch",
  );

  const controls = [
    {
      kind: "vehicle-selected-forward-gear",
      value: 2,
      deliveryFrame,
    },
    {
      kind: "vehicle-clutch-engagement",
      value: 0.5,
      deliveryFrame,
    },
    {
      kind: "vehicle-service-brake-application",
      value: 0.5,
      deliveryFrame,
    },
  ];
  assert.equal(new Set(controls.map((control) => control.deliveryFrame)).size, 1);
  assert.deepEqual(session.enqueueControls(controls), { accepted: 3 });

  const telemetry = processToFirstAudibleBlock(session, "FreeVehicle session");
  assert.equal(telemetry.heldDyno, null);
  assert.ok(telemetry.freeVehicle !== null);
  assert.equal(telemetry.requestedThrottle01, 0.1);
  assert.equal(telemetry.freeVehicle.selectedForwardGearOrdinal, 2);
  assert.equal(telemetry.freeVehicle.clutchEngagement01, 0.5);
  assert.equal(telemetry.freeVehicle.serviceBrakeApplication01, 0.5);
  assert.ok(Number.isFinite(telemetry.freeVehicle.vehicleSpeedMS));
  assert.ok(Number.isFinite(telemetry.freeVehicle.vehicleDistanceM));
  assert.ok(Number.isFinite(telemetry.freeVehicle.clutchTorqueCapacityNm));
  assert.ok(
    Number.isFinite(
      telemetry.freeVehicle.appliedAverageClutchTorqueOnEngineNm,
    ),
  );
  assert.ok(Number.isFinite(telemetry.freeVehicle.finalClutchSlipRadS));
  assert.ok(
    [
      "neutral",
      "disengaged",
      "engine-driving-torque-limited",
      "vehicle-backdrive-torque-limited",
      "tracking",
    ].includes(telemetry.freeVehicle.clutchDisposition),
  );
  assert.ok(
    ["moving", "stopped-within-step", "held-at-rest"].includes(
      telemetry.freeVehicle.roadLoadDisposition,
    ),
  );
}

async function main() {
  if (process.argv.length !== 10) {
    throw new Error(usage());
  }
  const [
    moduleArgument,
    engineArgument,
    heldDynoArgument,
    freeVehicleArgument,
    audioAssetId,
    audioArgument,
    accessoryAssetId,
    accessoryArgument,
  ] = process.argv.slice(2);
  assert.ok(audioAssetId.length !== 0, "audio asset ID must not be empty");
  assert.ok(accessoryAssetId.length !== 0, "accessory asset ID must not be empty");

  const modulePath = requireFile(moduleArgument, "Emscripten module");
  const engineJson = fs.readFileSync(requireFile(engineArgument, "engine JSON"), "utf8");
  const heldDynoJson = fs.readFileSync(
    requireFile(heldDynoArgument, "held-dyno scenario JSON"),
    "utf8",
  );
  const freeVehicleJson = fs.readFileSync(
    requireFile(freeVehicleArgument, "FreeVehicle scenario JSON"),
    "utf8",
  );
  const assets = [
    {
      kind: "audio",
      id: audioAssetId,
      bytes: fs.readFileSync(requireFile(audioArgument, "audio asset")),
    },
    {
      kind: "accessory-configuration",
      id: accessoryAssetId,
      bytes: fs.readFileSync(
        requireFile(accessoryArgument, "accessory configuration"),
      ),
    },
  ];

  const client = await EngineSimCapiClient.create(pathToFileURL(modulePath));
  let program = null;
  try {
    program = compileProgram(client, engineJson, heldDynoJson, assets);
    testHeldDyno(program);
    program.dispose();
    program = null;

    program = compileProgram(client, engineJson, freeVehicleJson, assets);
    testFreeVehicle(program);
    program.dispose();
    program = null;
  } finally {
    program?.dispose();
    client.dispose();
  }

  process.stdout.write(
    `${JSON.stringify({
      heldDynoCapabilities: HELD_DYNO_CAPABILITIES,
      freeVehicleCapabilities: FREE_VEHICLE_CAPABILITIES,
      forwardGearCount: EXPECTED_GEAR_RATIOS.length,
      atomicControlBatchSize: 3,
    })}\n`,
  );
}

main().catch((error) => {
  process.stderr.write(`operating-bench integration failure: ${error.stack}\n`);
  process.exitCode = 1;
});
