import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { loadAudioPackage } from "../web/runtime/audio-package-loader.js";
import { SessionExecutionKind } from "../web/runtime/c-api-abi.js";
import { EngineSimCapiClient } from "../web/runtime/c-api-client.js";
import { ResponsiveAudioPackageFollower } from "../web/runtime/responsive-audio-package-follower.js";
import { SourceBakedSessionClock } from "../web/runtime/source-baked-session-clock.js";
import { concatenateFloat32, encodeFloat32Wav } from "../web/runtime/wav.js";

const SAMPLE_RATE = 192_000;
const IDLE_LEAD_FRAMES = SAMPLE_RATE;
const IDLE_TAIL_FRAMES = Math.round(SAMPLE_RATE * 1.5);
const LIFT_RPM = 5_500;
const RETURNED_IDLE_RPM = 800;
const MAXIMUM_AUDIBLE_FRAMES = SAMPLE_RATE * 30;

function usage() {
  return [
    "usage:",
    "  node tools/capture-responsive-ab.mjs \\",
    "    <engine-sim-offline.js> <engine.json> <free-rev-scenario.json> \\",
    "    <package-url> <impulse-response.wav> <accessory-config.json> \\",
    "    <new-output-directory>",
  ].join("\n");
}

function requireFile(value, label) {
  const resolved = path.resolve(value);
  assert.ok(fs.statSync(resolved).isFile(), `${label} is not a file: ${resolved}`);
  return resolved;
}

function finite(value, label) {
  assert.ok(Number.isFinite(value), `${label} must be finite`);
  return value;
}

function signalStatistics(samples, start = 0, end = samples.length) {
  let peak = 0;
  let squareSum = 0;
  let maximumAdjacentDelta = 0;
  let maximumAdjacentFrame = start;
  let prior = start > 0 ? samples[start - 1] : samples[start];
  for (let frame = start; frame < end; ++frame) {
    const sample = finite(samples[frame], `PCM frame ${frame}`);
    peak = Math.max(peak, Math.abs(sample));
    if (frame > start) {
      const delta = Math.abs(sample - prior);
      if (delta > maximumAdjacentDelta) {
        maximumAdjacentDelta = delta;
        maximumAdjacentFrame = frame;
      }
    }
    squareSum += sample * sample;
    prior = sample;
  }
  const frameCount = end - start;
  const rms = frameCount === 0 ? 0 : Math.sqrt(squareSum / frameCount);
  return {
    frameCount,
    peak,
    rms,
    maximumAdjacentDelta,
    maximumAdjacentFrame,
  };
}

function phaseStatistics(source, baked, first, last) {
  const sourceStats = signalStatistics(source, first, last);
  const bakedStats = signalStatistics(baked, first, last);
  return {
    firstFrame: first,
    lastFrameExclusive: last,
    source: sourceStats,
    baked: bakedStats,
    bakedMinusSourceRmsDb:
      sourceStats.rms > 0 && bakedStats.rms > 0
        ? 20 * Math.log10(bakedStats.rms / sourceStats.rms)
        : null,
  };
}

function warmNormalRunning(clock) {
  return clock.start.rpm > 0 && clock.end.rpm > 0 &&
    clock.start.ignitionEnabled && clock.end.ignitionEnabled &&
    clock.start.fuelEnabled && clock.end.fuelEnabled;
}

function control(kind, value, deliveryFrame) {
  return { kind, value, deliveryFrame };
}

async function main() {
  if (process.argv.length !== 9) {
    throw new Error(usage());
  }
  const [
    moduleArgument,
    engineArgument,
    scenarioArgument,
    packageUrl,
    impulseArgument,
    accessoryArgument,
    outputArgument,
  ] = process.argv.slice(2);
  const modulePath = requireFile(moduleArgument, "Emscripten module");
  const enginePath = requireFile(engineArgument, "engine JSON");
  const scenarioPath = requireFile(scenarioArgument, "scenario JSON");
  const impulsePath = requireFile(impulseArgument, "impulse response");
  const accessoryPath = requireFile(accessoryArgument, "accessory configuration");
  const outputDirectory = path.resolve(outputArgument);
  assert.ok(!fs.existsSync(outputDirectory), `output already exists: ${outputDirectory}`);

  const scenario = JSON.parse(fs.readFileSync(scenarioPath, "utf8"));
  scenario.mode.throttle_01.points = [
    { time: { value: 0, unit: "s" }, value: 0 },
  ];
  const client = await EngineSimCapiClient.create(pathToFileURL(modulePath));
  const loadedPackage = await loadAudioPackage(packageUrl);
  const program = client.compile(
    fs.readFileSync(enginePath, "utf8"),
    JSON.stringify(scenario),
    [
      {
        kind: "audio",
        id: "smooth-39",
        bytes: fs.readFileSync(impulsePath),
      },
      {
        kind: "accessory-configuration",
        id: "warm-generic-accessories",
        bytes: fs.readFileSync(accessoryPath),
      },
    ],
    SessionExecutionKind.openEnded,
  );

  const session = program.session;
  const variants = [
    {
      id: "current",
      file: "B-current-baked-idle-wot-5500-lift-idle.wav",
      throttleOnly: false,
      sourceCursorOnly: false,
      follower: new ResponsiveAudioPackageFollower(loadedPackage),
      chunks: [],
    },
    {
      id: "throttle-routing",
      file: "C-throttle-routed-idle-wot-5500-lift-idle.wav",
      throttleOnly: true,
      sourceCursorOnly: false,
      follower: new ResponsiveAudioPackageFollower(loadedPackage),
      chunks: [],
    },
    {
      id: "source-cursor",
      file: "D-source-cursor-idle-wot-5500-lift-idle.wav",
      throttleOnly: false,
      sourceCursorOnly: true,
      follower: new ResponsiveAudioPackageFollower(loadedPackage),
      chunks: [],
    },
    {
      id: "throttle-routing-source-cursor",
      file: "E-throttle-routed-source-cursor-idle-wot-5500-lift-idle.wav",
      throttleOnly: true,
      sourceCursorOnly: true,
      follower: new ResponsiveAudioPackageFollower(loadedPackage),
      chunks: [],
    },
  ];
  const packageClock = new SourceBakedSessionClock();
  const packageBusId = loadedPackage.manifest.buses[0].id;
  let followerAdmitted = false;
  const sourceChunks = [];
  const telemetryTrace = [];
  let capturedFrames = 0;
  let wotGlobalFrame = null;
  let liftGlobalFrame = null;
  let returnedIdleGlobalFrame = null;
  let finalGlobalFrame = null;

  session.enqueueControls([
    control("throttle", 0, session.firstAudibleDeliveryFrame),
    control("ignition", true, session.firstAudibleDeliveryFrame),
    control("fuel", true, session.firstAudibleDeliveryFrame),
    control("limiter", true, session.firstAudibleDeliveryFrame),
  ]);

  try {
    while (capturedFrames < MAXIMUM_AUDIBLE_FRAMES) {
      const block = session.processBlock();
      assert.equal(block.process.kind, "block", "open session completed unexpectedly");
      const clock = packageClock.acceptBlock(block);
      const bakedBlocks = new Map();
      if (warmNormalRunning(clock)) {
        if (!followerAdmitted) {
          for (const variant of variants) {
            variant.follower.reset();
          }
          followerAdmitted = true;
        }
        for (const variant of variants) {
          const variantClock = variant.throttleOnly
            ? {
                start: { ...clock.start, signedLoad: null },
                end: { ...clock.end, signedLoad: null },
              }
            : clock;
          bakedBlocks.set(
            variant.id,
            variant.follower.renderBlock({
              firstDeliveryFrame: block.process.firstDeliveryFrame,
              frameCount: block.process.deliveryFrameCount,
              sourceClock: variantClock,
              completedCycles: variant.sourceCursorOnly
                ? []
                : block.completedCycles,
            }).bus(packageBusId),
          );
        }
      } else if (followerAdmitted) {
        for (const variant of variants) {
          variant.follower.reset();
        }
        followerAdmitted = false;
      }

      if (!block.audible) {
        continue;
      }
      assert.equal(block.bus.channelCount, 1);
      sourceChunks.push(block.samples);
      for (const variant of variants) {
        const bakedBlock = bakedBlocks.get(variant.id) ??
          new Float32Array(block.process.deliveryFrameCount);
        assert.equal(block.samples.length, bakedBlock.length);
        variant.chunks.push(bakedBlock);
      }
      capturedFrames += block.samples.length;
      const endpoint = block.telemetry.at(-1);
      const endGlobalFrame = session.nextDeliveryFrame;

      if (wotGlobalFrame === null && capturedFrames >= IDLE_LEAD_FRAMES) {
        wotGlobalFrame = endGlobalFrame;
        session.enqueueControls([control("throttle", 1, wotGlobalFrame)]);
      } else if (
        wotGlobalFrame !== null &&
        liftGlobalFrame === null &&
        endpoint.engineSpeedRpm >= LIFT_RPM
      ) {
        liftGlobalFrame = endGlobalFrame;
        session.enqueueControls([control("throttle", 0, liftGlobalFrame)]);
      } else if (
        liftGlobalFrame !== null &&
        returnedIdleGlobalFrame === null &&
        endpoint.engineSpeedRpm <= RETURNED_IDLE_RPM
      ) {
        returnedIdleGlobalFrame = endGlobalFrame;
      }

      const followerDiagnostics = variants[0].follower.diagnostics();
      telemetryTrace.push({
        globalEndFrame: endGlobalFrame.toString(),
        captureEndFrame: capturedFrames,
        rpm: endpoint.engineSpeedRpm,
        requestedThrottle01: endpoint.requestedThrottle01,
        signedLoad: clock.end.signedLoad,
        sourceCoordinate01: followerDiagnostics.sourceCoordinate01,
        idleBlend: followerDiagnostics.idleBlend,
        exactReanchorCount: followerDiagnostics.exactReanchorCount,
        sourceCursorTransitionCount: followerDiagnostics.sourceCursorTransitionCount,
        maximumCursorMarkerErrorFrames:
          followerDiagnostics.maximumCursorMarkerErrorFrames,
        runningRow: followerDiagnostics.selectionHistory.at(-1)?.runningRow ?? null,
        targetRpm: followerDiagnostics.selectionHistory.at(-1)?.targetRpm ?? null,
      });

      if (
        returnedIdleGlobalFrame !== null &&
        endGlobalFrame - returnedIdleGlobalFrame >= BigInt(IDLE_TAIL_FRAMES)
      ) {
        finalGlobalFrame = endGlobalFrame;
        break;
      }
    }

    assert.notEqual(wotGlobalFrame, null, "capture never opened the throttle");
    assert.notEqual(liftGlobalFrame, null, `engine never reached ${LIFT_RPM} RPM`);
    assert.notEqual(returnedIdleGlobalFrame, null, "engine never returned to idle");
    assert.notEqual(finalGlobalFrame, null, "capture did not retain its idle tail");

    const source = concatenateFloat32(sourceChunks, capturedFrames);
    const renderedVariants = Object.fromEntries(
      variants.map((variant) => [
        variant.id,
        concatenateFloat32(variant.chunks, capturedFrames),
      ]),
    );
    const captureFirstGlobalFrame =
      BigInt(telemetryTrace[0].globalEndFrame) - BigInt(sourceChunks[0].length);
    const localFrame = (globalFrame) => Number(globalFrame - captureFirstGlobalFrame);
    const wotFrame = localFrame(wotGlobalFrame);
    const liftFrame = localFrame(liftGlobalFrame);
    const returnedIdleFrame = localFrame(returnedIdleGlobalFrame);
    const finalFrame = localFrame(finalGlobalFrame);
    const report = {
      maneuver: "idle -> 100% throttle -> first 5500 RPM crossing -> 0% throttle -> idle",
      sampleRate: SAMPLE_RATE,
      frameCount: capturedFrames,
      durationSeconds: capturedFrames / SAMPLE_RATE,
      actionFrames: { wotFrame, liftFrame, returnedIdleFrame, finalFrame },
      actionSeconds: {
        wot: wotFrame / SAMPLE_RATE,
        lift: liftFrame / SAMPLE_RATE,
        returnedIdle: returnedIdleFrame / SAMPLE_RATE,
        final: finalFrame / SAMPLE_RATE,
      },
      variants: Object.fromEntries(
        variants.map((variant) => {
          const baked = renderedVariants[variant.id];
          return [
            variant.id,
            {
              file: variant.file,
              throttleOnly: variant.throttleOnly,
              sourceCursorOnly: variant.sourceCursorOnly,
              phases: {
                idleBefore: phaseStatistics(source, baked, 0, wotFrame),
                hardRise: phaseStatistics(source, baked, wotFrame, liftFrame),
                rundown: phaseStatistics(
                  source,
                  baked,
                  liftFrame,
                  returnedIdleFrame,
                ),
                idleAfter: phaseStatistics(
                  source,
                  baked,
                  returnedIdleFrame,
                  finalFrame,
                ),
              },
              follower: variant.follower.diagnostics(),
            },
          ];
        }),
      ),
      sourceClock: packageClock.diagnostics(),
      telemetry: telemetryTrace,
    };

    fs.mkdirSync(outputDirectory, { recursive: false });
    fs.writeFileSync(
      path.join(outputDirectory, "A-source-idle-wot-5500-lift-idle.wav"),
      encodeFloat32Wav(source, SAMPLE_RATE, 1),
    );
    for (const variant of variants) {
      fs.writeFileSync(
        path.join(outputDirectory, variant.file),
        encodeFloat32Wav(renderedVariants[variant.id], SAMPLE_RATE, 1),
      );
    }
    fs.writeFileSync(
      path.join(outputDirectory, "report.json"),
      `${JSON.stringify(report, null, 2)}\n`,
    );
    process.stdout.write(
      `${JSON.stringify({
        outputDirectory,
        maneuver: report.maneuver,
        durationSeconds: report.durationSeconds,
        actionSeconds: report.actionSeconds,
        variants: Object.fromEntries(
          Object.entries(report.variants).map(([id, variant]) => [
            id,
            {
              phases: variant.phases,
              follower: {
                exactReanchorCount: variant.follower.exactReanchorCount,
                sourceCursorTransitionCount:
                  variant.follower.sourceCursorTransitionCount,
                maximumCursorMarkerErrorFrames:
                  variant.follower.maximumCursorMarkerErrorFrames,
              },
            },
          ]),
        ),
      }, null, 2)}\n`,
    );
  } finally {
    program.dispose();
    client.dispose();
  }
}

await main();
