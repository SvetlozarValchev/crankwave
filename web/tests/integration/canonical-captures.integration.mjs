import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import {
  MotionMode,
  SessionExecutionKind,
} from "../../runtime/c-api-abi.js";
import { CrankwaveCapiClient } from "../../runtime/c-api-client.js";

const canonicalPhysicsRateHz = 20_000;

function usage() {
  return [
    "usage:",
    "  node web/tests/integration/canonical-captures.integration.mjs \\",
    "    <crankwave.js> <engine.json> <crank.json> <load.json> \\",
    "    <shutdown.json> <audio-asset-id> <audio-asset> \\",
    "    <accessory-asset-id> <accessory-asset.json>",
  ].join("\n");
}

function requireFile(filePath, label) {
  const absolute = path.resolve(filePath);
  assert.ok(fs.statSync(absolute).isFile(), `${label} is not a file: ${absolute}`);
  return absolute;
}

function compileProgram(client, engineJson, scenarioJson, assets) {
  return client.compile(
    engineJson,
    scenarioJson,
    assets,
    SessionExecutionKind.finiteScenario,
  );
}

function runFinite(program, label, observe) {
  const { session } = program;
  const { descriptor } = session;
  assert.equal(descriptor.executionKind, "finite-scenario");
  assert.ok(descriptor.totalBlockCount !== null);
  let blockCount = 0n;
  for (;;) {
    const result = session.processBlock();
    if (result.process.kind === "completed") {
      assert.equal(blockCount, descriptor.totalBlockCountBigInt);
      assert.equal(result.process.completedBlockCount, blockCount.toString());
      assert.equal(result.process.liveControlsAccepted, false);
      return;
    }
    assert.equal(result.process.kind, "block");
    assert.equal(result.process.blockOrdinal, blockCount.toString());
    assert.equal(result.telemetry.length, 1, `${label} omitted telemetry`);
    observe(result.telemetry[0], result.process.blockPhase);
    ++blockCount;
  }
}

function testCrank(program) {
  const { descriptor } = program.session;
  assert.equal(descriptor.motionModeCode, MotionMode.freeEngine);
  assert.equal(descriptor.scenarioId,
    "bmw-m52tub28-cleanroom-canonical-crank-only-0rpm");
  let maximumRpm = 0;
  let minimumRunningRpm = Number.POSITIVE_INFINITY;
  runFinite(program, "crank capture", (telemetry) => {
    assert.equal(telemetry.ignitionEnabled, false);
    assert.equal(telemetry.fuelEnabled, false);
    assert.equal(telemetry.starterEnabled, true);
    maximumRpm = Math.max(maximumRpm, telemetry.engineSpeedRpm);
    if (telemetry.engineSpeedRpm > 0) {
      minimumRunningRpm = Math.min(minimumRunningRpm, telemetry.engineSpeedRpm);
    }
  });
  assert.ok(minimumRunningRpm > 0 && minimumRunningRpm < 150);
  assert.ok(maximumRpm > 250 && maximumRpm < 400);
  return { minimumRunningRpm, maximumRpm };
}

function testLoadCycle(program) {
  const { descriptor } = program.session;
  assert.equal(descriptor.motionModeCode, MotionMode.heldDyno);
  assert.equal(
    descriptor.scenarioId,
    "bmw-m52tub28-cleanroom-canonical-loaded-rise-part-load-coast-1500-4500rpm",
  );
  let maximumRpm = 0;
  let finalRpm = 0;
  let sawLoadedRise = false;
  let sawPartLoad = false;
  let sawCoast = false;
  let sawUnforcedFall = false;
  runFinite(program, "loaded capture", (telemetry, phase) => {
    if (phase !== "audible") {
      assert.equal(telemetry.heldDyno, null);
      return;
    }
    assert.ok(telemetry.heldDyno !== null);
    maximumRpm = Math.max(maximumRpm, telemetry.engineSpeedRpm);
    finalRpm = telemetry.engineSpeedRpm;
    sawLoadedRise ||= telemetry.requestedThrottle01 === 1 &&
      telemetry.engineSpeedRpm > 3_000;
    sawPartLoad ||= telemetry.requestedThrottle01 === 0.45 &&
      telemetry.engineSpeedRpm > 4_400;
    sawCoast ||= telemetry.requestedThrottle01 === 0.04 &&
      telemetry.engineSpeedRpm < 3_000;
    sawUnforcedFall ||=
      telemetry.heldDyno.disposition === "driving-torque-limited";
  });
  assert.ok(maximumRpm >= 4_499 && maximumRpm <= 4_501);
  assert.ok(finalRpm > 1_000 && finalRpm < 1_500);
  assert.ok(sawLoadedRise && sawPartLoad && sawCoast && sawUnforcedFall);
  return { maximumRpm, finalRpm };
}

function testShutdown(program) {
  const { descriptor } = program.session;
  assert.equal(descriptor.motionModeCode, MotionMode.freeEngine);
  assert.equal(
    descriptor.scenarioId,
    "bmw-m52tub28-cleanroom-canonical-key-off-shutdown-700rpm",
  );
  let sawRunning = false;
  let sawKeyOff = false;
  let firstStoppedPhysicsStep = null;
  let maximumRpm = 0;
  let finalRpm = Number.NaN;
  runFinite(program, "shutdown capture", (telemetry) => {
    sawRunning ||= telemetry.ignitionEnabled && telemetry.fuelEnabled;
    sawKeyOff ||= !telemetry.ignitionEnabled && !telemetry.fuelEnabled;
    maximumRpm = Math.max(maximumRpm, telemetry.engineSpeedRpm);
    finalRpm = telemetry.engineSpeedRpm;
    if (firstStoppedPhysicsStep === null && telemetry.engineSpeedRpm === 0) {
      firstStoppedPhysicsStep = telemetry.physicsStepEnd;
    }
    assert.equal(telemetry.starterEnabled, false);
    assert.equal(telemetry.requestedExternalResistingTorqueNm, 40);
  });
  assert.ok(sawRunning && sawKeyOff);
  assert.ok(maximumRpm > 650 && maximumRpm < 850);
  assert.equal(finalRpm, 0);
  assert.ok(
    firstStoppedPhysicsStep !== null &&
      firstStoppedPhysicsStep / canonicalPhysicsRateHz <= 2.2,
  );
  return {
    maximumRpm,
    stoppedAtSeconds: firstStoppedPhysicsStep / canonicalPhysicsRateHz,
  };
}

async function main() {
  if (process.argv.length !== 11) {
    throw new Error(usage());
  }
  const [
    moduleArgument,
    engineArgument,
    crankArgument,
    loadArgument,
    shutdownArgument,
    audioAssetId,
    audioArgument,
    accessoryAssetId,
    accessoryArgument,
  ] = process.argv.slice(2);
  assert.ok(audioAssetId.length !== 0, "audio asset ID must not be empty");
  assert.ok(accessoryAssetId.length !== 0, "accessory asset ID must not be empty");

  const modulePath = requireFile(moduleArgument, "Emscripten module");
  const engineJson = fs.readFileSync(requireFile(engineArgument, "engine JSON"), "utf8");
  const scenarioJson = [crankArgument, loadArgument, shutdownArgument].map(
    (argument, index) =>
      fs.readFileSync(requireFile(argument, `scenario JSON ${index + 1}`), "utf8"),
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

  const client = await CrankwaveCapiClient.create(pathToFileURL(modulePath));
  let program = null;
  const observations = [];
  try {
    const tests = [testCrank, testLoadCycle, testShutdown];
    for (let index = 0; index < tests.length; ++index) {
      program = compileProgram(client, engineJson, scenarioJson[index], assets);
      observations.push(tests[index](program));
      program.dispose();
      program = null;
    }
  } finally {
    program?.dispose();
    client.dispose();
  }

  process.stdout.write(`${JSON.stringify({ observations })}\n`);
}

main().catch((error) => {
  process.stderr.write(`canonical-captures integration failure: ${error.stack}\n`);
  process.exitCode = 1;
});
