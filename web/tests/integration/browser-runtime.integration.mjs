import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { BrowserEngineRuntime } from "../../runtime/browser-engine-runtime.js";
import { SessionExecutionKind } from "../../runtime/c-api-abi.js";

const EXPECTED_FRAME_COUNT = 7_680;
const EXPECTED_PCM_SHA256 =
  "ede3c34fc94cd25c32329e37ee84cdd80f6093f1f2a3c36e055be71e55dc4610";
const CANONICAL_SAMPLE_RATE = 192_000;
const WAV_HEADER_BYTES = 56;

function usage() {
  return [
    "usage:",
    "  node web/tests/integration/browser-runtime.integration.mjs \\",
    "    <engine-sim-offline.js> <engine.json> <scenario.json> \\",
    "    <audio-asset-id> <audio-asset> \\",
    "    <accessory-asset-id> <accessory-asset.json>",
  ].join("\n");
}

function requireFile(filePath, label) {
  const absolute = path.resolve(filePath);
  const stat = fs.statSync(absolute);
  assert.ok(stat.isFile(), `${label} is not a file: ${absolute}`);
  return absolute;
}

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function ascii(view, offset, length) {
  return new TextDecoder("ascii").decode(
    new Uint8Array(view.buffer, view.byteOffset + offset, length),
  );
}

function assertFloat32WavMatches(message) {
  assert.equal(message.encoding, "ieee-float32-le");
  assert.equal(message.coreIdentical, true);
  assert.equal(message.executionKind, "finite-scenario");
  assert.equal(message.sampleRate, CANONICAL_SAMPLE_RATE);
  assert.equal(message.channelCount, 1);
  assert.equal(message.frameCount, EXPECTED_FRAME_COUNT);
  assert.ok(message.pcmFloat32 instanceof ArrayBuffer);
  assert.ok(message.wav instanceof ArrayBuffer);

  const pcmBytes = new Uint8Array(message.pcmFloat32);
  const wavBytes = new Uint8Array(message.wav);
  const view = new DataView(message.wav);
  assert.equal(pcmBytes.byteLength, EXPECTED_FRAME_COUNT * 4);
  assert.equal(wavBytes.byteLength, WAV_HEADER_BYTES + pcmBytes.byteLength);
  assert.equal(ascii(view, 0, 4), "RIFF");
  assert.equal(view.getUint32(4, true), wavBytes.byteLength - 8);
  assert.equal(ascii(view, 8, 4), "WAVE");
  assert.equal(ascii(view, 12, 4), "fmt ");
  assert.equal(view.getUint32(16, true), 16);
  assert.equal(view.getUint16(20, true), 3);
  assert.equal(view.getUint16(22, true), 1);
  assert.equal(view.getUint32(24, true), CANONICAL_SAMPLE_RATE);
  assert.equal(view.getUint32(28, true), CANONICAL_SAMPLE_RATE * 4);
  assert.equal(view.getUint16(32, true), 4);
  assert.equal(view.getUint16(34, true), 32);
  assert.equal(ascii(view, 36, 4), "fact");
  assert.equal(view.getUint32(40, true), 4);
  assert.equal(view.getUint32(44, true), EXPECTED_FRAME_COUNT);
  assert.equal(ascii(view, 48, 4), "data");
  assert.equal(view.getUint32(52, true), pcmBytes.byteLength);
  assert.deepEqual(wavBytes.subarray(WAV_HEADER_BYTES), pcmBytes);
}

function eventFor(events, type, requestId) {
  return events.findLast(
    (event) => event.type === type && event.requestId === requestId,
  );
}

function waitForEvent(subscribe, description, timeoutMilliseconds = 5_000) {
  let timer;
  return Promise.race([
    new Promise((resolve) => subscribe(resolve)),
    new Promise((_, reject) => {
      timer = setTimeout(
        () => reject(new Error(`timed out waiting for ${description}`)),
        timeoutMilliseconds,
      );
    }),
  ]).finally(() => clearTimeout(timer));
}

async function main() {
  if (process.argv.length !== 9) {
    throw new Error(usage());
  }
  const [
    moduleArgument,
    engineArgument,
    scenarioArgument,
    audioAssetId,
    audioArgument,
    accessoryAssetId,
    accessoryArgument,
  ] = process.argv.slice(2);
  assert.ok(audioAssetId.length !== 0, "audio asset ID must not be empty");
  assert.ok(
    accessoryAssetId.length !== 0,
    "accessory asset ID must not be empty",
  );

  const modulePath = requireFile(moduleArgument, "Emscripten module");
  const enginePath = requireFile(engineArgument, "engine JSON");
  const scenarioPath = requireFile(scenarioArgument, "scenario JSON");
  const audioPath = requireFile(audioArgument, "audio asset");
  const accessoryPath = requireFile(
    accessoryArgument,
    "accessory configuration",
  );
  const engineJson = fs.readFileSync(enginePath, "utf8");
  const scenarioJson = fs.readFileSync(scenarioPath, "utf8");
  const assets = [
    {
      kind: "audio",
      id: audioAssetId,
      bytes: fs.readFileSync(audioPath),
    },
    {
      kind: "accessory-configuration",
      id: accessoryAssetId,
      bytes: fs.readFileSync(accessoryPath),
    },
  ];

  const events = [];
  let telemetryListener = null;
  let audioRingListener = null;
  let completedStateListener = null;
  const runtime = await BrowserEngineRuntime.create({
    moduleUrl: pathToFileURL(modulePath),
    emit(message) {
      events.push(message);
      if (message.type === "telemetry" && telemetryListener !== null) {
        const listener = telemetryListener;
        telemetryListener = null;
        listener(message);
      }
      if (message.type === "audio-ring" && audioRingListener !== null) {
        const listener = audioRingListener;
        audioRingListener = null;
        listener(message);
      }
      if (
        message.type === "state" &&
        message.state === "completed" &&
        completedStateListener !== null
      ) {
        const listener = completedStateListener;
        completedStateListener = null;
        listener(message);
      }
    },
  });

  try {
    runtime.build({
      requestId: "valid-build",
      engineJson,
      scenarioJson,
      assets,
      executionKind: SessionExecutionKind.finiteScenario,
    });
    const validBuild = eventFor(events, "built", "valid-build");
    assert.ok(validBuild, "valid build did not publish its descriptor");
    assert.equal(validBuild.descriptor.executionKind, "finite-scenario");
    assert.equal(validBuild.descriptor.executionKindCode, 1);
    assert.equal(validBuild.descriptor.openEnded, false);
    assert.equal(validBuild.descriptor.motionMode, "inertial-dyno");
    assert.equal(validBuild.descriptor.motionModeCode, 5);
    assert.equal(validBuild.descriptor.forwardGearCount, 0);
    assert.deepEqual(validBuild.descriptor.forwardGears, []);
    assert.equal(validBuild.descriptor.totalBlockCount, "18");
    assert.equal(validBuild.descriptor.totalDeliveryFrames, "69120");
    assert.equal(validBuild.descriptor.liveControlCapabilities, 0b00111);
    assert.deepEqual(
      validBuild.descriptor.controls.map(({ kind }) => kind),
      ["throttle", "ignition", "fuel"],
    );
    const auditionBus = validBuild.descriptor.buses.find(
      (bus) => bus.kind === "engine-audition-master",
    );
    const routeBus = validBuild.descriptor.buses.find(
      (bus) => bus.kind === "source-route-dry",
    );
    assert.ok(auditionBus, "session does not expose an audition master");
    assert.ok(routeBus, "session does not expose a dry exhaust route");
    assert.equal(auditionBus.signalDisposition, "active");
    assert.equal(routeBus.sourceRouteKind, "exhaust-outlet");
    assert.equal(routeBus.signalDisposition, "active");
    assert.equal(
      validBuild.descriptor.selectedBusIndex,
      auditionBus.index,
    );

    for (const [requestId, executionKind] of [
      ["missing-execution-kind", undefined],
      ["zero-execution-kind", 0],
      ["unknown-execution-kind", 99],
    ]) {
      assert.throws(
        () =>
          runtime.build({
            requestId,
            engineJson,
            scenarioJson,
            assets,
            ...(executionKind === undefined ? {} : { executionKind }),
          }),
        /session execution kind must be finiteScenario or openEnded/u,
      );
      assert.equal(eventFor(events, "built", requestId), undefined);
    }

    runtime.build({
      requestId: "malformed-replacement",
      engineJson: "{\"schema\":",
      scenarioJson,
      assets,
      executionKind: SessionExecutionKind.finiteScenario,
    });
    const validation = eventFor(
      events,
      "validation",
      "malformed-replacement",
    );
    assert.ok(validation, "malformed replacement did not return diagnostics");
    assert.equal(validation.ok, false);
    assert.ok(validation.diagnostics.length > 0);
    assert.equal(validation.diagnostics[0].document, "engine");
    assert.equal(validation.error.stageName, "engine-parse");

    runtime.enqueueControls({
      requestId: "live-controls",
      controls: [
        { kind: "throttle", value: 0.5 },
        { kind: "ignition", value: true },
        { kind: "fuel", value: true },
      ],
    });
    const accepted = eventFor(events, "controls-result", "live-controls");
    assert.equal(accepted?.accepted, true);
    assert.deepEqual(
      accepted.controls.map(({ kind }) => kind),
      ["throttle", "ignition", "fuel"],
    );
    assert.equal(
      new Set(accepted.controls.map(({ deliveryFrame }) => deliveryFrame)).size,
      1,
      "one atomic control batch did not resolve one shared default frame",
    );

    const firstTelemetry = waitForEvent(
      (resolve) => {
        telemetryListener = resolve;
      },
      "the first real C-ABI telemetry block",
    );
    runtime.start({
      requestId: "start-before-route-replacement",
      outputSampleRate: 48_000,
      leadFrames: 960,
    });
    const preparationTelemetry = await firstTelemetry;
    assert.ok(preparationTelemetry.frames.length > 0);
    assert.equal(preparationTelemetry.frames.at(-1).heldDyno, null);
    assert.equal(preparationTelemetry.frames.at(-1).freeVehicle, null);
    assert.equal(
      eventFor(events, "audio-ring", "start-before-route-replacement"),
      undefined,
      "the empty preparation ring was published to the AudioWorklet",
    );
    runtime.stop({ requestId: "stop-before-route-replacement" });
    const paused = eventFor(
      events,
      "state",
      "stop-before-route-replacement",
    );
    assert.equal(paused?.state, "paused");
    assert.notEqual(
      paused.nextDeliveryFrame,
      "0",
      "live session did not advance before replacement",
    );
    const preparationStats = events.findLast(
      (event) => event.type === "runtime-stats",
    );
    assert.equal(preparationStats.ring.producerState, 0);
    assert.equal(preparationStats.ring.underrunFrames, 0);
    assert.equal(preparationStats.ring.underrunEvents, 0);

    runtime.selectAudioBus({
      requestId: "select-route",
      busIndex: routeBus.index,
    });
    const routeBuild = eventFor(events, "built", "select-route");
    const routeReady = eventFor(events, "state", "select-route");
    assert.equal(routeBuild?.engineId, validBuild.engineId);
    assert.equal(routeBuild?.scenarioId, validBuild.scenarioId);
    assert.equal(routeBuild?.descriptor.selectedBusIndex, routeBus.index);
    assert.equal(routeReady?.state, "ready");
    assert.equal(
      routeReady?.nextDeliveryFrame,
      "0",
      "route selection did not replace the partially consumed session",
    );

    const exportControls = [
      { kind: "throttle", value: 0.5 },
      { kind: "ignition", value: true },
      { kind: "fuel", value: true },
    ];
    await runtime.exportWav({
      requestId: "route-export",
      controls: exportControls,
    });
    const routeExport = eventFor(events, "wav-export", "route-export");
    assert.ok(routeExport);
    assert.equal(routeExport.executionKind, "finite-scenario");
    assert.equal(routeExport.bus.index, routeBus.index);
    assert.equal(routeExport.frameCount, EXPECTED_FRAME_COUNT);
    assert.notEqual(
      sha256(new Uint8Array(routeExport.pcmFloat32)),
      EXPECTED_PCM_SHA256,
      "route selection returned the audition-master PCM",
    );

    runtime.selectAudioBus({
      requestId: "select-audition",
      busIndex: auditionBus.index,
    });
    assert.equal(
      eventFor(events, "built", "select-audition")?.descriptor
        .selectedBusIndex,
      auditionBus.index,
    );

    const primedAudioRing = waitForEvent(
      (resolve) => {
        audioRingListener = resolve;
      },
      "a primed audition-master PCM ring",
    );
    runtime.start({
      requestId: "primed-audition-start",
      outputSampleRate: 48_000,
      leadFrames: 960,
    });
    assert.equal(
      eventFor(events, "state", "primed-audition-start")?.state,
      "preparing",
    );
    assert.equal(
      eventFor(events, "audio-ring", "primed-audition-start"),
      undefined,
    );
    const ring = await primedAudioRing;
    const header = new Int32Array(ring.sharedBuffer, 0, ring.headerBytes / 4);
    assert.ok(
      Atomics.load(header, 2) >= 960,
      "published PCM ring does not contain its requested lead",
    );
    assert.equal(Atomics.load(header, 3), 0);
    assert.equal(Atomics.load(header, 4), 0);
    assert.equal(Atomics.load(header, 6), 1);
    assert.equal(
      eventFor(events, "state", "primed-audition-start")?.state,
      "running",
    );
    runtime.stop({ requestId: "stop-primed-audition" });

    await runtime.exportWav({
      requestId: "audition-export",
      controls: exportControls,
    });
    const auditionExport = eventFor(
      events,
      "wav-export",
      "audition-export",
    );
    assert.ok(auditionExport);
    assert.equal(auditionExport.bus.index, auditionBus.index);
    assertFloat32WavMatches(auditionExport);
    assert.equal(
      sha256(new Uint8Array(auditionExport.pcmFloat32)),
      EXPECTED_PCM_SHA256,
    );

    const finiteCompletion = waitForEvent(
      (resolve) => {
        completedStateListener = resolve;
      },
      "normal finite live-session completion",
    );
    runtime.restart({
      requestId: "finite-completion",
      outputSampleRate: 48_000,
      leadFrames: 4_096,
    });
    const completed = await finiteCompletion;
    assert.equal(completed.state, "completed");
    assert.equal(
      events.some(
        (event) =>
          event.type === "error" &&
          event.error?.detailCode === "browser-runtime-session-terminal",
      ),
      false,
      "normal finite completion was misreported as a terminal-session fault",
    );

    process.stdout.write(
      `browser runtime integration passed: ${EXPECTED_FRAME_COUNT} frames, ` +
        `${EXPECTED_PCM_SHA256}\n`,
    );
  } finally {
    runtime.dispose({ requestId: "dispose" });
  }
}

main().catch((error) => {
  process.stderr.write(`browser runtime integration failure: ${error.stack}\n`);
  process.exitCode = 1;
});
