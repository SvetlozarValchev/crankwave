#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { createHash, webcrypto } from "node:crypto";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
  ESO_CANONICAL_SAMPLE_RATE,
  ProcessKind,
  SessionExecutionKind,
} from "../web/runtime/c-api-abi.js";
import { EngineSimCapiClient } from "../web/runtime/c-api-client.js";
import { loadAudioAtlas } from "../web/runtime/audio-atlas-loader.js";
import {
  AudioAtlasEngineStateFlag,
  ContinuousAudioAtlasCursor,
} from "../web/runtime/continuous-audio-atlas-cursor.js";
import {
  concatenateFloat32,
  encodeFloat32Wav,
} from "../web/runtime/wav.js";

const SOURCE_OUTPUT_NAME = "A-source-fifth-gear-moving-window.wav";
const BAKED_OUTPUT_NAME = "B-atlas-fifth-gear-moving-window.wav";
const REPORT_OUTPUT_NAME = "report.json";
const SOURCE_AUDITION_BUS_KIND = "engine-audition-master";
const ATLAS_AUDITION_BUS_ID = "master-engine-audition";
const MINIMUM_WINDOW_RPM = 1_600;
const MAXIMUM_WINDOW_RPM = 4_000;
const WOT_EPSILON = 1e-12;
const TIME_EPSILON_S = 1e-12;
const EXPECTED_NORMAL_STATE_MASK =
  AudioAtlasEngineStateFlag.ignitionEnabled |
  AudioAtlasEngineStateFlag.fuelEnabled |
  AudioAtlasEngineStateFlag.limiterEnabled;

function usage() {
  return [
    "usage:",
    "  node tools/capture-bmw-continuous-atlas-ab.mjs \\",
    "    --module <engine-sim-offline.js> \\",
    "    --engine <engine.json> \\",
    "    --scenario <held-out-fifth-gear-scenario.json> \\",
    "    --atlas <atlas.json URL-or-path> \\",
    "    --impulse-response <BMW impulse-response.wav> \\",
    "    --accessory <BMW accessory-configuration.json> \\",
    "    --output <new-output-directory>",
  ].join("\n");
}

function fail(message, options) {
  throw new Error(message, options);
}

function parseArguments(argv) {
  if (argv.includes("--help") || argv.includes("-h")) {
    process.stdout.write(`${usage()}\n`);
    process.exit(0);
  }
  const admitted = new Set([
    "--module",
    "--engine",
    "--scenario",
    "--atlas",
    "--impulse-response",
    "--accessory",
    "--output",
  ]);
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!admitted.has(option)) {
      fail(`unknown option ${String(option)}\n\n${usage()}`);
    }
    if (typeof value !== "string" || value.length === 0 || value.startsWith("--")) {
      fail(`${option} requires one nonempty value\n\n${usage()}`);
    }
    if (values.has(option)) {
      fail(`${option} may be supplied only once`);
    }
    values.set(option, value);
  }
  if (argv.length !== admitted.size * 2 || values.size !== admitted.size) {
    fail(`all capture inputs are required\n\n${usage()}`);
  }
  return Object.freeze({
    module: values.get("--module"),
    engine: values.get("--engine"),
    scenario: values.get("--scenario"),
    atlas: values.get("--atlas"),
    impulseResponse: values.get("--impulse-response"),
    accessory: values.get("--accessory"),
    output: values.get("--output"),
  });
}

function requireFile(input, label) {
  const resolved = path.resolve(input);
  let metadata;
  try {
    metadata = fs.statSync(resolved);
  } catch (error) {
    fail(`${label} does not exist: ${resolved}`, { cause: error });
  }
  if (!metadata.isFile()) {
    fail(`${label} is not a file: ${resolved}`);
  }
  return resolved;
}

function requireNewOutputDirectory(input) {
  const resolved = path.resolve(input);
  if (fs.existsSync(resolved)) {
    fail(`output directory already exists; a new directory is required: ${resolved}`);
  }
  const parent = path.dirname(resolved);
  let metadata;
  try {
    metadata = fs.statSync(parent);
  } catch (error) {
    fail(`output parent does not exist: ${parent}`, { cause: error });
  }
  if (!metadata.isDirectory()) {
    fail(`output parent is not a directory: ${parent}`);
  }
  return resolved;
}

function parseJson(text, label) {
  try {
    return JSON.parse(text);
  } catch (error) {
    fail(`${label} is not valid JSON`, { cause: error });
  }
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function fileIdentity(filePath) {
  const bytes = fs.readFileSync(filePath);
  return Object.freeze({
    path: filePath,
    sha256: sha256(bytes),
    bytes,
  });
}

function resolveAtlasUrl(input) {
  try {
    const url = new URL(input);
    if (url.protocol !== "file:" && url.protocol !== "http:" && url.protocol !== "https:") {
      fail(`atlas URL protocol is not supported: ${url.protocol}`);
    }
    return url;
  } catch (error) {
    if (error instanceof Error && error.message.startsWith("atlas URL protocol")) {
      throw error;
    }
    return pathToFileURL(requireFile(input, "atlas manifest"));
  }
}

function exactArrayBuffer(bytes) {
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

async function atlasFetch(input) {
  const url = new URL(input);
  if (url.protocol !== "file:") {
    return globalThis.fetch(url);
  }
  let bytes;
  try {
    bytes = await fs.promises.readFile(fileURLToPath(url));
  } catch (error) {
    return {
      ok: false,
      status: error?.code === "ENOENT" ? 404 : 500,
      url: url.href,
      async arrayBuffer() {
        return new ArrayBuffer(0);
      },
    };
  }
  return {
    ok: true,
    status: 200,
    url: url.href,
    async arrayBuffer() {
      return exactArrayBuffer(bytes);
    },
  };
}

function inferAssetIds(engineDocument) {
  const impulseResponses = engineDocument?.presentation?.assets?.filter(
    (asset) => asset?.kind === "impulse_response",
  );
  if (!Array.isArray(impulseResponses) || impulseResponses.length !== 1) {
    fail("focused BMW capture requires exactly one authored impulse-response asset");
  }
  const impulseResponseId = impulseResponses[0]?.id;
  const accessoryId = engineDocument?.engine?.losses?.accessory_configuration_id;
  const accessoryDeclarations = engineDocument?.engine?.accessory_configurations;
  if (
    typeof impulseResponseId !== "string" ||
    impulseResponseId.length === 0 ||
    typeof accessoryId !== "string" ||
    accessoryId.length === 0 ||
    !Array.isArray(accessoryDeclarations) ||
    accessoryDeclarations.filter((asset) => asset?.id === accessoryId).length !== 1
  ) {
    fail("focused BMW capture could not resolve its authored IR/accessory asset IDs");
  }
  return Object.freeze({ impulseResponseId, accessoryId });
}

function seconds(quantity, label) {
  if (
    quantity === null ||
    typeof quantity !== "object" ||
    typeof quantity.value !== "number" ||
    !Number.isFinite(quantity.value) ||
    quantity.value < 0 ||
    quantity.unit !== "s"
  ) {
    fail(`${label} must be a finite nonnegative quantity in seconds`);
  }
  return quantity.value;
}

function authoredWotInterval(scenarioDocument) {
  const curve = scenarioDocument?.mode?.throttle_01;
  if (curve?.interpolation !== "right_continuous_hold" || !Array.isArray(curve.points)) {
    fail("held-out scenario must author a right-continuous throttle curve");
  }
  for (let index = 0; index + 1 < curve.points.length; ++index) {
    const point = curve.points[index];
    if (Math.abs(point?.value - 1) > WOT_EPSILON) {
      continue;
    }
    const next = curve.points[index + 1];
    if (Math.abs(next?.value - 1) <= WOT_EPSILON) {
      continue;
    }
    const beginS = seconds(point.time, `mode.throttle_01.points[${index}].time`);
    const endS = seconds(next.time, `mode.throttle_01.points[${index + 1}].time`);
    if (!(endS > beginS)) {
      fail("authored WOT interval is empty");
    }
    return Object.freeze({ beginS, endS });
  }
  fail("held-out scenario has no WOT interval followed by an authored lift");
}

function audibleStateMask(telemetry) {
  let result = 0;
  if (telemetry.ignitionEnabled) {
    result |= AudioAtlasEngineStateFlag.ignitionEnabled;
  }
  if (telemetry.fuelEnabled) {
    result |= AudioAtlasEngineStateFlag.fuelEnabled;
  }
  if (telemetry.starterEnabled) {
    result |= AudioAtlasEngineStateFlag.starterEnabled;
  }
  if (telemetry.limiterEnabled) {
    result |= AudioAtlasEngineStateFlag.limiterEnabled;
  }
  if (telemetry.limiterCutActive) {
    result |= AudioAtlasEngineStateFlag.limiterCutActive;
  }
  return result;
}

function requireFiniteTelemetry(telemetry, label) {
  for (const [field, value] of [
    ["engineSpeedRpm", telemetry.engineSpeedRpm],
    ["requestedThrottle01", telemetry.requestedThrottle01],
    ["meanIntakeManifoldPressurePaAbs", telemetry.meanIntakeManifoldPressurePaAbs],
    ["thetaRad", telemetry.thetaRad],
  ]) {
    if (typeof value !== "number" || !Number.isFinite(value)) {
      fail(`${label}.${field} is not finite`);
    }
  }
  if (telemetry.engineSpeedRpm <= 0) {
    fail(`${label}.engineSpeedRpm must be positive for this moving capture`);
  }
  if (telemetry.meanIntakeManifoldPressurePaAbs <= 0) {
    fail(`${label}.meanIntakeManifoldPressurePaAbs must be positive`);
  }
}

function asSafeFrame(value, label) {
  let parsed;
  try {
    parsed = BigInt(value);
  } catch (error) {
    fail(`${label} is not an exact integer frame`, { cause: error });
  }
  if (parsed < 0n || parsed > BigInt(Number.MAX_SAFE_INTEGER)) {
    fail(`${label} is outside the exact JavaScript frame range`);
  }
  return Number(parsed);
}

function endpointFromTelemetry(telemetry, deliveryFrame, sampleRate, phase, ordinal) {
  requireFiniteTelemetry(telemetry, `telemetry[${ordinal}]`);
  return {
    deliveryFrame,
    timeS: deliveryFrame / sampleRate,
    phase,
    blockOrdinal: ordinal,
    telemetry,
    rpmSlopeRpmPerSecond: Number.NaN,
    stateMask: audibleStateMask(telemetry),
    unwrappedCrankRevolutions: telemetry.thetaRad / (2 * Math.PI),
  };
}

// This is the same centered local secant policy as
// src/atlas/moving_lane_capture.cpp: each endpoint searches at most 0.25 s in
// each direction and falls back to its sole adjacent endpoint at an edge.
function deriveMacroRpmSlopes(endpoints, sampleRate) {
  if (endpoints.length < 2) {
    fail("finite source session did not publish enough telemetry endpoints");
  }
  const quarterSecondFrames = sampleRate / 4;
  for (let index = 0; index < endpoints.length; ++index) {
    const center = endpoints[index].deliveryFrame;
    let left = index;
    while (
      left > 0 &&
      center - endpoints[left - 1].deliveryFrame <= quarterSecondFrames
    ) {
      --left;
    }
    let right = index;
    while (
      right + 1 < endpoints.length &&
      endpoints[right + 1].deliveryFrame - center <= quarterSecondFrames
    ) {
      ++right;
    }
    if (left === right) {
      if (right + 1 < endpoints.length) {
        ++right;
      } else if (left > 0) {
        --left;
      } else {
        fail("macro RPM slope has no adjacent endpoint");
      }
    }
    const frameDelta = endpoints[right].deliveryFrame - endpoints[left].deliveryFrame;
    const durationS = frameDelta / sampleRate;
    const slope =
      (endpoints[right].telemetry.engineSpeedRpm -
        endpoints[left].telemetry.engineSpeedRpm) /
      durationS;
    if (!Number.isFinite(slope)) {
      fail(`macro RPM slope is invalid at telemetry endpoint ${index}`);
    }
    endpoints[index].rpmSlopeRpmPerSecond = slope;
  }
}

function isWot(endpoint) {
  return Math.abs(endpoint.telemetry.requestedThrottle01 - 1) <= WOT_EPSILON;
}

function isNormalAudibleState(endpoint) {
  return endpoint.stateMask === EXPECTED_NORMAL_STATE_MASK;
}

function segmentCoversEndpoint(segment, endpoint) {
  const rpm = endpoint.telemetry.engineSpeedRpm;
  const normalizedSlope = endpoint.rpmSlopeRpmPerSecond / rpm;
  return (
    segment.direction === "rising" &&
    segment.stateMask === endpoint.stateMask &&
    Math.abs(segment.loadCoordinate - 1) <= WOT_EPSILON &&
    rpm >= segment.usableRpm.minimum &&
    rpm <= segment.usableRpm.maximum &&
    normalizedSlope >= segment.normalizedRpmSlope.minimumPerSecond &&
    normalizedSlope <= segment.normalizedRpmSlope.maximumPerSecond
  );
}

function isWindowBlock(block, authoredWot, segment) {
  const { start, end } = block;
  if (!block.audible || start === null) {
    return false;
  }
  if (
    start.timeS + TIME_EPSILON_S < authoredWot.beginS ||
    end.timeS - TIME_EPSILON_S > authoredWot.endS
  ) {
    return false;
  }
  if (!isWot(start) || !isWot(end)) {
    return false;
  }
  if (!isNormalAudibleState(start) || !isNormalAudibleState(end)) {
    return false;
  }
  if (
    start.telemetry.engineSpeedRpm < MINIMUM_WINDOW_RPM ||
    start.telemetry.engineSpeedRpm > MAXIMUM_WINDOW_RPM ||
    end.telemetry.engineSpeedRpm < MINIMUM_WINDOW_RPM ||
    end.telemetry.engineSpeedRpm > MAXIMUM_WINDOW_RPM
  ) {
    return false;
  }
  return (
    start.rpmSlopeRpmPerSecond > 0 &&
    end.rpmSlopeRpmPerSecond > 0 &&
    segmentCoversEndpoint(segment, start) &&
    segmentCoversEndpoint(segment, end)
  );
}

function contiguousRuns(blocks, authoredWot, segment) {
  const runs = [];
  let current = [];
  for (const block of blocks) {
    const prior = current.at(-1);
    const contiguous =
      prior === undefined ||
      block.firstDeliveryFrame ===
        prior.firstDeliveryFrame + prior.deliveryFrameCount;
    if (!isWindowBlock(block, authoredWot, segment) || !contiguous) {
      if (current.length !== 0) {
        runs.push(current);
      }
      current = isWindowBlock(block, authoredWot, segment) ? [block] : [];
      continue;
    }
    current.push(block);
  }
  if (current.length !== 0) {
    runs.push(current);
  }
  return runs;
}

function selectComparisonWindow(blocks, authoredWot, segment) {
  const runs = contiguousRuns(blocks, authoredWot, segment);
  if (runs.length === 0) {
    fail("held-out session has no clean WOT rising block run inside 1600..4000 RPM");
  }
  runs.sort((left, right) => {
    const leftFrames = left.reduce((sum, block) => sum + block.deliveryFrameCount, 0);
    const rightFrames = right.reduce((sum, block) => sum + block.deliveryFrameCount, 0);
    return rightFrames - leftFrames;
  });
  return runs[0];
}

function cursorEndpoint(endpoint) {
  return Object.freeze({
    rpm: endpoint.telemetry.engineSpeedRpm,
    rpmSlopeRpmPerSecond: endpoint.rpmSlopeRpmPerSecond,
    // This is the authored full-load lane coordinate. It is deliberately not
    // copied from throttle; measured MAP remains an independent admission input.
    signedLoadCoordinate: 1,
    manifoldPressurePaAbs: endpoint.telemetry.meanIntakeManifoldPressurePaAbs,
    stateMask: endpoint.stateMask,
    unwrappedCrankRevolutions: endpoint.unwrappedCrankRevolutions,
  });
}

function signalStatistics(samples) {
  let peak = 0;
  let squareSum = 0;
  let clipSampleCount = 0;
  for (const sample of samples) {
    if (!Number.isFinite(sample)) {
      fail("captured signal contains a non-finite sample");
    }
    const magnitude = Math.abs(sample);
    peak = Math.max(peak, magnitude);
    squareSum += sample * sample;
    if (magnitude >= 1) {
      ++clipSampleCount;
    }
  }
  return Object.freeze({
    peak,
    rms: samples.length === 0 ? 0 : Math.sqrt(squareSum / samples.length),
    clipSampleCount,
    frameCount: samples.length,
  });
}

function minimumMaximum(values) {
  return Object.freeze({
    minimum: Math.min(...values),
    maximum: Math.max(...values),
  });
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const modulePath = requireFile(options.module, "Emscripten module");
  const moduleWasmPath = requireFile(
    modulePath.replace(/\.[^.]+$/, ".wasm"),
    "Emscripten WASM payload",
  );
  const engineIdentity = fileIdentity(requireFile(options.engine, "engine JSON"));
  const scenarioIdentity = fileIdentity(requireFile(options.scenario, "held-out scenario JSON"));
  const impulseResponseIdentity = fileIdentity(
    requireFile(options.impulseResponse, "BMW impulse response"),
  );
  const accessoryIdentity = fileIdentity(
    requireFile(options.accessory, "BMW accessory configuration"),
  );
  const outputDirectory = requireNewOutputDirectory(options.output);
  const atlasUrl = resolveAtlasUrl(options.atlas);
  const engineJson = engineIdentity.bytes.toString("utf8");
  const scenarioJson = scenarioIdentity.bytes.toString("utf8");
  const engineDocument = parseJson(engineJson, "engine JSON");
  const scenarioDocument = parseJson(scenarioJson, "held-out scenario JSON");
  const assetIds = inferAssetIds(engineDocument);
  const wotInterval = authoredWotInterval(scenarioDocument);

  const loadedAtlas = await loadAudioAtlas(atlasUrl, {
    fetch: atlasFetch,
    crypto: globalThis.crypto ?? webcrypto,
  });
  if (loadedAtlas.sampleRate !== ESO_CANONICAL_SAMPLE_RATE) {
    fail(`atlas sample rate is ${loadedAtlas.sampleRate}, not canonical 192 kHz`);
  }
  const comparisonSegments = loadedAtlas.movingSegments.filter(
    (segment) =>
      segment.direction === "rising" &&
      segment.stateMask === EXPECTED_NORMAL_STATE_MASK &&
      Math.abs(segment.loadCoordinate - 1) <= WOT_EPSILON &&
      segment.usableRpm.minimum <= MINIMUM_WINDOW_RPM &&
      segment.usableRpm.maximum >= MAXIMUM_WINDOW_RPM,
  );
  if (comparisonSegments.length !== 1) {
    fail(
      `focused held-out capture requires exactly one full-load rising 1600..4000 RPM segment; found ${comparisonSegments.length}`,
    );
  }
  const comparisonSegment = comparisonSegments[0];
  const capturedSource = comparisonSegment.descriptor.source_scenario;
  if (
    capturedSource.id === scenarioDocument.id ||
    capturedSource.sha256 === scenarioIdentity.sha256 ||
    String(loadedAtlas.manifest.public_seed) === String(scenarioDocument.public_seed)
  ) {
    fail(
      "held-out scenario must differ from the atlas capture scenario by ID, content, and public seed",
    );
  }

  const client = await EngineSimCapiClient.create(pathToFileURL(modulePath));
  let program = null;
  const captureStarted = process.hrtime.bigint();
  try {
    program = client.compile(
      engineJson,
      scenarioJson,
      [
        {
          kind: "audio",
          id: assetIds.impulseResponseId,
          bytes: impulseResponseIdentity.bytes,
        },
        {
          kind: "accessory-configuration",
          id: assetIds.accessoryId,
          bytes: accessoryIdentity.bytes,
        },
      ],
      SessionExecutionKind.finiteScenario,
    );
    const { session } = program;
    const { descriptor } = session;
    if (
      descriptor.executionKindCode !== SessionExecutionKind.finiteScenario ||
      descriptor.totalBlockCountBigInt === null
    ) {
      fail("held-out comparison requires a finite authored scenario session");
    }
    if (descriptor.engineId !== loadedAtlas.manifest.engine) {
      fail(
        `atlas engine ${loadedAtlas.manifest.engine} does not match held-out engine ${descriptor.engineId}`,
      );
    }
    if (
      program.engineProvenanceSha256 !==
      loadedAtlas.manifest.provenance.engine.sha256
    ) {
      fail(
        "atlas engine provenance does not match the exact compiled held-out engine",
      );
    }
    if (
      program.rendererSourceSha256 !==
      loadedAtlas.manifest.provenance.renderer_build.sha256
    ) {
      fail(
        "atlas renderer provenance does not match the exact held-out renderer build",
      );
    }
    if (
      descriptor.engineId !== engineDocument?.engine?.identity?.id ||
      descriptor.scenarioId !== scenarioDocument?.id
    ) {
      fail("compiled session identities do not match the untouched JSON inputs");
    }
    if (descriptor.deliveryRateHz !== ESO_CANONICAL_SAMPLE_RATE) {
      fail("held-out source session is not canonical 192 kHz");
    }
    const auditionBuses = session.buses.filter(
      (bus) => bus.kind === SOURCE_AUDITION_BUS_KIND,
    );
    if (
      auditionBuses.length !== 1 ||
      auditionBuses[0].channelCount !== 1 ||
      auditionBuses[0].sampleRateHz !== ESO_CANONICAL_SAMPLE_RATE
    ) {
      fail("held-out session must expose exactly one mono canonical audition master");
    }
    const auditionBus = auditionBuses[0];
    const endpoints = [];
    const blocks = [];
    let priorEndpoint = null;
    let processedBlocks = 0n;
    let preparationTelemetryCount = 0;
    let audibleTelemetryCount = 0;

    for (;;) {
      const result = session.processBlock(auditionBus.index);
      if (result.process.kindCode === ProcessKind.completed) {
        if (processedBlocks !== descriptor.totalBlockCountBigInt) {
          fail("session completion block count disagrees with its descriptor");
        }
        break;
      }
      if (result.process.kindCode !== ProcessKind.block || result.telemetry.length !== 1) {
        fail("source session omitted its exact per-block telemetry endpoint");
      }
      const firstDeliveryFrame = asSafeFrame(
        result.process.firstDeliveryFrame,
        `block ${result.process.blockOrdinal} firstDeliveryFrame`,
      );
      const deliveryFrameCount = result.process.deliveryFrameCount;
      if (
        !Number.isSafeInteger(deliveryFrameCount) ||
        deliveryFrameCount !== descriptor.deliveryFramesPerBlock ||
        result.samples.length !== deliveryFrameCount
      ) {
        fail(`block ${result.process.blockOrdinal} has an invalid source audio extent`);
      }
      const end = endpointFromTelemetry(
        result.telemetry[0],
        firstDeliveryFrame + deliveryFrameCount,
        descriptor.deliveryRateHz,
        result.process.blockPhase,
        result.process.blockOrdinal,
      );
      endpoints.push(end);
      if (result.audible) {
        ++audibleTelemetryCount;
      } else {
        ++preparationTelemetryCount;
      }
      blocks.push({
        blockOrdinal: result.process.blockOrdinal,
        firstDeliveryFrame,
        deliveryFrameCount,
        audible: result.audible,
        phase: result.process.blockPhase,
        start: priorEndpoint,
        end,
        sourceSamples: result.samples,
      });
      priorEndpoint = end;
      ++processedBlocks;
    }

    deriveMacroRpmSlopes(endpoints, descriptor.deliveryRateHz);
    const selectedBlocks = selectComparisonWindow(
      blocks,
      wotInterval,
      comparisonSegment,
    );
    const sourceChunks = selectedBlocks.map((block) => block.sourceSamples);
    const frameCount = selectedBlocks.reduce(
      (sum, block) => sum + block.deliveryFrameCount,
      0,
    );
    const sourcePcm = concatenateFloat32(sourceChunks, frameCount);

    const cursor = new ContinuousAudioAtlasCursor(loadedAtlas);
    const initialization = cursor.initialize(cursorEndpoint(selectedBlocks[0].start));
    if (initialization.segmentId !== comparisonSegment.id) {
      fail(
        `atlas cursor initialized segment ${initialization.segmentId}, not audited segment ${comparisonSegment.id}`,
      );
    }
    const atlasBusIndex = cursor.busIds.indexOf(ATLAS_AUDITION_BUS_ID);
    if (atlasBusIndex === -1) {
      fail(`atlas has no ${ATLAS_AUDITION_BUS_ID} bus`);
    }
    const outputBuffers = cursor.createOutputBuffers(descriptor.deliveryFramesPerBlock);
    const bakedChunks = [];
    for (const block of selectedBlocks) {
      try {
        cursor.renderBlockInto({
          frameCount: block.deliveryFrameCount,
          start: cursorEndpoint(block.start),
          end: cursorEndpoint(block.end),
          outputBuffers,
        });
      } catch (error) {
        fail(
          `atlas replay rejected held-out block ${block.blockOrdinal} ` +
            `(${block.start.telemetry.engineSpeedRpm.toFixed(3)}..` +
            `${block.end.telemetry.engineSpeedRpm.toFixed(3)} RPM, normalized slopes ` +
            `${(block.start.rpmSlopeRpmPerSecond / block.start.telemetry.engineSpeedRpm).toFixed(6)}..` +
            `${(block.end.rpmSlopeRpmPerSecond / block.end.telemetry.engineSpeedRpm).toFixed(6)} /s)`,
          { cause: error },
        );
      }
      bakedChunks.push(outputBuffers[atlasBusIndex].slice(0, block.deliveryFrameCount));
    }
    const bakedPcm = concatenateFloat32(bakedChunks, frameCount);
    const captureEnded = process.hrtime.bigint();

    const sourceWav = encodeFloat32Wav(
      sourcePcm,
      ESO_CANONICAL_SAMPLE_RATE,
      1,
    );
    const bakedWav = encodeFloat32Wav(
      bakedPcm,
      ESO_CANONICAL_SAMPLE_RATE,
      1,
    );
    const firstBlock = selectedBlocks[0];
    const finalBlock = selectedBlocks.at(-1);
    const windowEndpoints = [
      firstBlock.start,
      ...selectedBlocks.map((block) => block.end),
    ];
    const report = {
      schema: "engine-sim-offline/continuous-audio-atlas-ab-report",
      source: "held-out-source-a",
      baked: "chronological-continuous-atlas-b",
      inputs: {
        renderer: {
          javascript: {
            path: modulePath,
            sha256: sha256(fs.readFileSync(modulePath)),
          },
          wasm: {
            path: moduleWasmPath,
            sha256: sha256(fs.readFileSync(moduleWasmPath)),
          },
          source_closure_sha256: program.rendererSourceSha256,
        },
        engine: {
          path: engineIdentity.path,
          id: descriptor.engineId,
          sha256: engineIdentity.sha256,
          compiled_provenance_sha256: program.engineProvenanceSha256,
        },
        scenario: {
          path: scenarioIdentity.path,
          id: descriptor.scenarioId,
          sha256: scenarioIdentity.sha256,
          public_seed: scenarioDocument.public_seed,
          untouched_json: true,
        },
        atlas: {
          manifest_url: loadedAtlas.manifestUrl,
          id: loadedAtlas.manifest.id,
          engine: loadedAtlas.manifest.engine,
          public_seed: loadedAtlas.manifest.public_seed,
          provenance: loadedAtlas.manifest.provenance,
        },
        impulse_response: {
          path: impulseResponseIdentity.path,
          id: assetIds.impulseResponseId,
          sha256: impulseResponseIdentity.sha256,
        },
        accessory_configuration: {
          path: accessoryIdentity.path,
          id: assetIds.accessoryId,
          sha256: accessoryIdentity.sha256,
        },
      },
      capture: {
        source_bus_id: auditionBus.id,
        sample_rate_hz: descriptor.deliveryRateHz,
        physics_rate_hz: descriptor.physicsRateHz,
        delivery_frames_per_block: descriptor.deliveryFramesPerBlock,
        total_blocks: descriptor.totalBlockCount,
        preparation_blocks: descriptor.preparationBlockCount,
        telemetry_endpoints_retained: endpoints.length,
        preparation_telemetry_endpoints_retained: preparationTelemetryCount,
        audible_telemetry_endpoints_retained: audibleTelemetryCount,
        elapsed_seconds: Number(captureEnded - captureStarted) / 1e9,
      },
      macro_rpm_slope: {
        source: "measured-rpm-centered-half-second-secant",
        angular_acceleration_used: false,
        left_and_right_radius_seconds: 0.25,
        range_rpm_per_second: minimumMaximum(
          windowEndpoints.map((endpoint) => endpoint.rpmSlopeRpmPerSecond),
        ),
      },
      window: {
        authored_wot_begin_s: wotInterval.beginS,
        authored_lift_s: wotInterval.endS,
        first_block_ordinal: firstBlock.blockOrdinal,
        last_block_ordinal: finalBlock.blockOrdinal,
        first_delivery_frame: firstBlock.firstDeliveryFrame,
        end_delivery_frame:
          finalBlock.firstDeliveryFrame + finalBlock.deliveryFrameCount,
        frame_count: frameCount,
        duration_seconds: frameCount / descriptor.deliveryRateHz,
        start_rpm: firstBlock.start.telemetry.engineSpeedRpm,
        end_rpm: finalBlock.end.telemetry.engineSpeedRpm,
        requested_throttle_01: 1,
        state_mask: EXPECTED_NORMAL_STATE_MASK,
        complete_blocks_only: true,
        before_authored_lift: true,
      },
      atlas_replay: {
        initialization,
        selected_segment_id: comparisonSegment.id,
        admitted_normalized_rpm_slope_per_second:
          comparisonSegment.normalizedRpmSlope,
        selected_bus_id: ATLAS_AUDITION_BUS_ID,
        signed_load_coordinate: 1,
        load_coordinate_source: "authored-full-load-lane-not-throttle",
        manifold_pressure_source: "measured-source-a-telemetry",
        crank_phase_source: "measured-unwrapped-theta-rad-divided-by-two-pi",
        cursor: cursor.diagnostics(),
      },
      signal_statistics: {
        source_a: signalStatistics(sourcePcm),
        baked_b: signalStatistics(bakedPcm),
      },
      outputs: {
        source_a: SOURCE_OUTPUT_NAME,
        baked_b: BAKED_OUTPUT_NAME,
        combined_ab: null,
      },
    };

    fs.mkdirSync(outputDirectory);
    fs.writeFileSync(path.join(outputDirectory, SOURCE_OUTPUT_NAME), sourceWav);
    fs.writeFileSync(path.join(outputDirectory, BAKED_OUTPUT_NAME), bakedWav);
    fs.writeFileSync(
      path.join(outputDirectory, REPORT_OUTPUT_NAME),
      `${JSON.stringify(report, null, 2)}\n`,
      "utf8",
    );
    process.stdout.write(
      `${JSON.stringify({
        outputDirectory,
        source: path.join(outputDirectory, SOURCE_OUTPUT_NAME),
        baked: path.join(outputDirectory, BAKED_OUTPUT_NAME),
        report: path.join(outputDirectory, REPORT_OUTPUT_NAME),
        frameCount,
        durationSeconds: frameCount / descriptor.deliveryRateHz,
        cursor: cursor.diagnostics(),
      })}\n`,
    );
  } finally {
    program?.dispose();
    client.dispose();
  }
}

main().catch((error) => {
  process.stderr.write(`continuous atlas A/B capture failed: ${error.stack ?? error}\n`);
  process.exitCode = 1;
});
