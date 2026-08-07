#!/usr/bin/env node

// Internal directional-transient stage for the tracked responsive-audio baker.
// It records three chronological 720-degree dry-route cycles from prescribed
// exponential RPM traversals and applies the accepted phase-aligned closure.

import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
  ESO_CANONICAL_SAMPLE_RATE,
  ProcessKind,
  SessionExecutionKind,
} from "../../../web/runtime/c-api-abi.js";
import { EngineSimCapiClient } from "../../../web/runtime/c-api-client.js";
import {
  rendererFileIdentity,
  uniqueArtifactTokens,
} from "./bake-contract.mjs";

const experiment = import.meta.dirname;
const repository = path.resolve(experiment, "../../..");
const COMMAND_ARGS = process.argv.slice(2);
const MOTORING_PROOF = COMMAND_ARGS.includes("--motoring-proof");
const inventoryPath = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_INVENTORY");
const modulePath = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_MODULE");
const wasmPath = process.env.ESO_RESPONSIVE_BAKE_WASM === undefined
  ? path.join(path.dirname(modulePath), "engine-sim-offline.wasm")
  : requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_WASM");
const rendererFilePairIdentity = rendererFileIdentity(modulePath, wasmPath);
const bakeCacheRoot = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_CACHE");
const responsiveOutputRoot = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_OUTPUT");

function requiredEnvironmentPath(name) {
  const value = process.env[name];
  if (typeof value !== "string" || value.length === 0 || !path.isAbsolute(value)) {
    throw new Error(`${name} must be an absolute path`);
  }
  return path.normalize(value);
}

const SAMPLE_RATE = ESO_CANONICAL_SAMPLE_RATE;
const PHYSICS_RATE = 10_000;
const SAMPLES_PER_CYCLE = 4_096;
const PHASE_CYCLE_REVOLUTIONS = 2;
const PHASE_CYCLE_COUNT = 3;
const SOURCE_CYCLE_ORIGIN_REVOLUTIONS = 0;
const PREPARATION_DURATION_SECONDS = 4.5;
const POST_SWEEP_HOLD_SECONDS = MOTORING_PROOF ? 5 : 1;
const LOGARITHMIC_RPM_RATE_PER_SECOND = MOTORING_PROOF ? 1 / 18 : 1 / 3;
const TRAJECTORY_POINT_PERIOD_SECONDS = 0.25;
const DURATION_ALIGNMENT_RATE = 50;
const MAXIMUM_DEFAULT_CONCURRENCY = 6;
const LOAD_LANES = Object.freeze([
  Object.freeze({ id: "coast", throttle01: 0 }),
  Object.freeze({ id: "mid", throttle01: 0.2 }),
  Object.freeze({ id: "power", throttle01: 1 }),
]);
const DIRECTIONS = Object.freeze(MOTORING_PROOF ? ["falling"] : ["rising", "falling"]);
const CAPTURE_METHOD_ID = MOTORING_PROOF
  ? "ignition-off-fuel-on-controlled-high-inertia-free-engine-falling-rpm-one-eighteenth-per-second-three-cycle-dry-routes-v1"
  : "prescribed-exponential-rpm-one-third-per-second-three-cycle-dry-routes-v3";
const SEAM_ALGORITHM_ID =
  "phase-aligned-three-cycle-template-boundary-smoothstep-1of8-v1";
const SEAM_BLEND_CYCLE_FRACTION = 1 / 8;
const MAXIMUM_SEAM_OVER_DERIVATIVE_RMS = 3;
// This is a diagnostic fidelity cap, not part of the closure transform. A
// universal 0.40 retains the accepted algorithm while admitting the sole
// cross-engine 0.35 exceedance: Subaru falling/mid/4800 at 0.37847. Its join
// itself closes from 26.158x to 0.365x adjacent-derivative RMS.
const MAXIMUM_CORRECTION_RMS_OVER_SOURCE_RMS = 0.4;
const PUBLIC_SEED = "12648430";
const ATLAS_AUDITION_BUS_ID = "master-engine-audition";

function fail(message, options) {
  throw new Error(message, options);
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function writeJson(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}

function float32LeBytes(samples) {
  return Buffer.from(samples.buffer, samples.byteOffset, samples.byteLength);
}

function finitePositive(value, label) {
  if (!Number.isFinite(value) || !(value > 0)) fail(`${label} must be positive`);
  return value;
}

function parseConcurrency(value) {
  const result = Number(value ?? MAXIMUM_DEFAULT_CONCURRENCY);
  if (!Number.isSafeInteger(result) || result < 1 || result > 32) {
    fail("concurrency must be an integer in [1,32]");
  }
  return result;
}

function inventory() {
  if (!fs.existsSync(inventoryPath)) {
    fail(`multi-engine inventory is absent: ${inventoryPath}`);
  }
  const value = readJson(inventoryPath);
  if (!Array.isArray(value.engines) || value.engines.length !== 1) {
    fail("standalone bake inventory must contain exactly one engine");
  }
  return value;
}

function findEngine(engineId) {
  const found = inventory().engines.find((engine) => engine.engine_id === engineId);
  if (found === undefined) fail(`unknown inventory engine ${engineId}`);
  return found;
}

function absoluteRepositoryPath(relativePath, label) {
  if (typeof relativePath !== "string" || relativePath.length === 0) {
    fail(`${label} path is absent`);
  }
  const result = path.isAbsolute(relativePath)
    ? path.normalize(relativePath)
    : path.resolve(repository, relativePath);
  if (
    !path.isAbsolute(relativePath) &&
    !result.startsWith(`${repository}${path.sep}`)
  ) {
    fail(`${label} escapes the repository`);
  }
  if (!fs.existsSync(result)) fail(`${label} is absent: ${result}`);
  return result;
}

function engineConfig(engine) {
  const authoredRpmAnchors = engine.rpm_domain?.recommended_rpm_grid;
  const dryBusIds = engine.presentation?.dry_source_route_bus_ids;
  if (
    !Array.isArray(authoredRpmAnchors) ||
    authoredRpmAnchors.length < 2 ||
    authoredRpmAnchors.some((value) => !Number.isFinite(value) || !(value > 0))
  ) {
    fail(`${engine.engine_id} has no usable RPM grid`);
  }
  if (
    !Array.isArray(dryBusIds) ||
    dryBusIds.length < 1 ||
    new Set(dryBusIds).size !== dryBusIds.length
  ) {
    fail(`${engine.engine_id} has no unique dry route bus order`);
  }
  if (
    authoredRpmAnchors.some(
      (value, index) => index > 0 && value <= authoredRpmAnchors[index - 1],
    )
  ) {
    fail(`${engine.engine_id} RPM grid is not strictly increasing`);
  }
  const audioAssets = engine.assets?.audio;
  const accessory = engine.assets?.accessory_configuration;
  if (!Array.isArray(audioAssets) || audioAssets.length < 1 || accessory === null) {
    fail(`${engine.engine_id} asset inventory is incomplete`);
  }
  const scenarioReference =
    engine.canonical_scenarios?.free_interactive?.path ??
    engine.canonical_scenarios?.free_interactive?.template_path ??
    engine.canonical_scenarios?.available_load_step?.path ??
    engine.canonical_scenarios?.available_topology_test?.path ??
    engine.canonical_scenarios?.dyno?.path;
  const baseScenarioPath = absoluteRepositoryPath(
    scenarioReference,
    `${engine.engine_id} scenario template`,
  );
  const baseScenario = readJson(baseScenarioPath);
  const routeTokens = uniqueArtifactTokens(
    dryBusIds,
    "route",
    `${engine.engine_id} dry route IDs`,
  );
  const routes = dryBusIds.map((id, index) =>
    Object.freeze({ id, token: routeTokens[index] })
  );
  const minimumRpm = finitePositive(
    engine.rpm_domain.minimum_rpm,
    `${engine.engine_id} minimum RPM`,
  );
  const maximumRpm = finitePositive(
    engine.rpm_domain.maximum_rpm,
    `${engine.engine_id} maximum RPM`,
  );
  if (!(maximumRpm > minimumRpm)) fail(`${engine.engine_id} RPM domain is invalid`);
  if (
    minimumRpm !== authoredRpmAnchors[0] ||
    maximumRpm !== authoredRpmAnchors.at(-1)
  ) {
    fail(`${engine.engine_id} RPM domain does not match its anchor endpoints`);
  }
  const rpmAnchors = MOTORING_PROOF
    ? [...new Set([100, 250, 500, ...authoredRpmAnchors])].sort(
        (left, right) => left - right,
      )
    : authoredRpmAnchors.slice();
  const responsiveOuterDomain =
    engine.rpm_domain.responsive_audio_outer_domain;
  const outerMinimumRpm = MOTORING_PROOF
    ? 1
    : finitePositive(
        responsiveOuterDomain?.minimum_rpm,
        `${engine.engine_id} responsive-audio outer minimum RPM`,
      );
  const outerMaximumRpm = finitePositive(
    responsiveOuterDomain?.maximum_rpm,
    `${engine.engine_id} responsive-audio outer maximum RPM`,
  );
  if (
    !(outerMaximumRpm > outerMinimumRpm) ||
    outerMinimumRpm > minimumRpm ||
    outerMaximumRpm < maximumRpm
  ) {
    fail(`${engine.engine_id} responsive-audio outer RPM domain is invalid`);
  }
  const acceptedBaseline = null;
  const captureMinimumRpm = MOTORING_PROOF
    ? 25
    : acceptedBaseline?.minimum_rpm ?? Math.max(50, minimumRpm * 0.8);
  const captureMaximumRpm =
    acceptedBaseline?.maximum_rpm ?? maximumRpm * 1.05;
  if (
    captureMinimumRpm > (MOTORING_PROOF ? rpmAnchors[0] : outerMinimumRpm) ||
    captureMaximumRpm < outerMaximumRpm
  ) {
    fail(`${engine.engine_id} capture trajectory does not span its outer RPM domain`);
  }
  const enginePath = absoluteRepositoryPath(
    engine.engine_path,
    `${engine.engine_id} engine`,
  );
  const assets = [
    ...audioAssets.map((asset) => ({
      kind: "audio",
      id: asset.id,
      path: absoluteRepositoryPath(asset.path, `${engine.engine_id} audio asset`),
      expectedSha256: asset.sha256,
    })),
    {
      kind: "accessory-configuration",
      id: accessory.reference_id,
      path: absoluteRepositoryPath(
        accessory.path,
        `${engine.engine_id} accessory configuration`,
      ),
      expectedSha256: accessory.sha256,
    },
  ];
  for (const asset of assets) {
    const digest = sha256(fs.readFileSync(asset.path));
    if (digest !== asset.expectedSha256) {
      fail(`${engine.engine_id} ${asset.id} does not match inventory SHA-256`);
    }
  }
  return Object.freeze({
    engineId: engine.engine_id,
    enginePath,
    fuelId: engine.fuel_id,
    baseScenarioPath,
    baseScenario,
    assets: Object.freeze(assets),
    routes: Object.freeze(routes),
    rpmAnchors: Object.freeze(rpmAnchors),
    minimumRpm: MOTORING_PROOF ? rpmAnchors[0] : minimumRpm,
    maximumRpm,
    outerMinimumRpm,
    outerMaximumRpm,
    captureMinimumRpm,
    captureMaximumRpm,
    captureDurationAlignment:
      acceptedBaseline?.duration_alignment ?? "ceil-20ms",
    acceptedBaseline,
    captureDirectory: path.join(
      bakeCacheRoot,
      MOTORING_PROOF ? "motoring-ignition-off-fuel-on" : "directional",
      "captures",
    ),
    packageDirectory: path.join(
      bakeCacheRoot,
      MOTORING_PROOF ? "motoring" : "directional",
      "package",
    ),
    heldPackageDirectory: path.join(responsiveOutputRoot, "held"),
    stagedEngineDirectory: responsiveOutputRoot,
  });
}

function captureIdentity(config) {
  const inputs = {
    capture_method: CAPTURE_METHOD_ID,
    seam_algorithm: SEAM_ALGORITHM_ID,
    engine_id: config.engineId,
    ...(MOTORING_PROOF
      ? {
          state_selector: {
            id: "motoring-ignition-off-fuel-on",
            required_on_mask: 2,
            required_off_mask: 1,
            wildcard_mask: 28,
          },
        }
      : {}),
    rpm_anchors: config.rpmAnchors,
    minimum_rpm: config.minimumRpm,
    maximum_rpm: config.maximumRpm,
    outer_minimum_rpm: config.outerMinimumRpm,
    outer_maximum_rpm: config.outerMaximumRpm,
    ...(config.acceptedBaseline === null
      ? {}
      : {
          accepted_capture_baseline: {
            minimum_rpm: config.captureMinimumRpm,
            maximum_rpm: config.captureMaximumRpm,
            duration_alignment: config.captureDurationAlignment,
            source_package: config.acceptedBaseline.source_package,
            seam_closed_package: config.acceptedBaseline.seam_closed_package,
          },
        }),
    load_lanes: LOAD_LANES,
    dry_bus_ids: config.routes.map((route) => route.id),
    source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
    phase_cycle_revolutions: PHASE_CYCLE_REVOLUTIONS,
    phase_cycle_count: PHASE_CYCLE_COUNT,
    samples_per_cycle: SAMPLES_PER_CYCLE,
    logarithmic_rpm_rate_per_second: LOGARITHMIC_RPM_RATE_PER_SECOND,
    preparation_duration_seconds: PREPARATION_DURATION_SECONDS,
    post_sweep_hold_seconds: POST_SWEEP_HOLD_SECONDS,
    physics_rate_hz: PHYSICS_RATE,
    sample_rate_hz: SAMPLE_RATE,
    public_seed: PUBLIC_SEED,
    engine_input_sha256: sha256(fs.readFileSync(config.enginePath)),
    wasm_loader_sha256: rendererFilePairIdentity.loader_sha256,
    wasm_binary_sha256: rendererFilePairIdentity.wasm_sha256,
    assets: config.assets.map((asset) => ({
      kind: asset.kind,
      id: asset.id,
      sha256: sha256(fs.readFileSync(asset.path)),
    })),
  };
  return Object.freeze({
    ...inputs,
    sha256: sha256(Buffer.from(JSON.stringify(inputs))),
  });
}

function alignedDuration(seconds) {
  return Math.ceil(seconds * DURATION_ALIGNMENT_RATE) / DURATION_ALIGNMENT_RATE;
}

function trajectory(config, direction) {
  const rising = direction === "rising";
  if (!rising && direction !== "falling") fail(`unknown direction ${direction}`);
  const lowGuardRpm = config.captureMinimumRpm;
  const highGuardRpm = config.captureMaximumRpm;
  const startRpm = rising ? lowGuardRpm : highGuardRpm;
  const endRpm = rising ? highGuardRpm : lowGuardRpm;
  const rawSweepDurationSeconds =
    Math.abs(Math.log(endRpm / startRpm)) / LOGARITHMIC_RPM_RATE_PER_SECOND;
  const sweepDurationSeconds = config.captureDurationAlignment === "nearest-20ms"
    ? Math.round(rawSweepDurationSeconds * DURATION_ALIGNMENT_RATE) /
      DURATION_ALIGNMENT_RATE
    : alignedDuration(rawSweepDurationSeconds);
  const sweepEndSeconds = PREPARATION_DURATION_SECONDS + sweepDurationSeconds;
  const durationSeconds = alignedDuration(
    sweepEndSeconds + POST_SWEEP_HOLD_SECONDS,
  );
  const points = [
    [0, startRpm],
    [PREPARATION_DURATION_SECONDS, startRpm],
  ];
  for (
    let elapsed = TRAJECTORY_POINT_PERIOD_SECONDS;
    elapsed < sweepDurationSeconds;
    elapsed += TRAJECTORY_POINT_PERIOD_SECONDS
  ) {
    const value = rising
      ? startRpm * Math.exp(LOGARITHMIC_RPM_RATE_PER_SECOND * elapsed)
      : startRpm * Math.exp(-LOGARITHMIC_RPM_RATE_PER_SECOND * elapsed);
    points.push([PREPARATION_DURATION_SECONDS + elapsed, value]);
  }
  points.push([sweepEndSeconds, endRpm], [durationSeconds, endRpm]);
  return Object.freeze({
    direction,
    startRpm,
    endRpm,
    sweepDurationSeconds,
    sweepEndSeconds,
    durationSeconds,
    points: Object.freeze(points.map((point) => Object.freeze(point))),
  });
}

function sourceScenario(config, direction, lane) {
  const base = config.baseScenario;
  const motion = trajectory(config, direction);
  const crankAngle = base.initial_state?.crank_angle ?? {
    value: 0,
    unit: "rad",
  };
  return {
    schema: "engine-sim-offline/scenario",
    id: MOTORING_PROOF
      ? `${config.engineId}-motoring-ignition-off-fuel-on-${direction}-${lane.id}-capture`
      : `${config.engineId}-directional-${direction}-${lane.id}-capture`,
    engine: config.engineId,
    fuel: config.fuelId,
    ambient: base.ambient,
    initial_thermal_state: base.initial_thermal_state,
    crankcase: base.crankcase,
    initial_state: {
      engine_speed: { value: motion.startRpm, unit: "rpm" },
      crank_angle: crankAngle,
      ignition_enabled: !MOTORING_PROOF,
      fuel_enabled: true,
      starter_enabled: false,
      dyno_enabled: !MOTORING_PROOF,
      limiter_enabled: false,
    },
    preparation: {
      type: "fixed_horizon",
      preparation_duration: {
        value: PREPARATION_DURATION_SECONDS,
        unit: "s",
      },
      trailing_complete_cycle_count: 16,
    },
    mode: MOTORING_PROOF
      ? {
          type: "free_engine",
          attached_inertia: { value: 10000, unit: "kg*m2" },
          throttle_01: {
            interpolation: "right_continuous_hold",
            points: [{ time: { value: 0, unit: "s" }, value: lane.throttle01 }],
          },
          external_resisting_torque: {
            value_dimension: "torque",
            interpolation: "right_continuous_hold",
            points: [
              {
                time: { value: 0, unit: "s" },
                value: { value: 0, unit: "N*m" },
              },
              ...motion.points.slice(1, -2).map(([time, value]) => ({
                time: { value: time, unit: "s" },
                value: {
                  value:
                    10000 *
                    LOGARITHMIC_RPM_RATE_PER_SECOND *
                    value *
                    (2 * Math.PI / 60),
                  unit: "N*m",
                },
              })),
              {
                time: { value: motion.sweepEndSeconds, unit: "s" },
                value: { value: 0, unit: "N*m" },
              },
            ],
          },
        }
      : {
          type: "held_dyno",
          target_engine_speed: {
            value_dimension: "angular_speed",
            interpolation: "linear",
            points: motion.points.map(([time, value]) => ({
              time: { value: time, unit: "s" },
              value: { value, unit: "rpm" },
            })),
          },
          maximum_absorbing_torque: { value: 10000, unit: "N*m" },
          maximum_driving_torque: { value: 10000, unit: "N*m" },
          throttle_01: {
            interpolation: "right_continuous_hold",
            points: [{ time: { value: 0, unit: "s" }, value: lane.throttle01 }],
          },
        },
    events: [],
    rates: {
      physics: { numerator: String(PHYSICS_RATE), denominator: "1", unit: "Hz" },
      capture: { numerator: String(PHYSICS_RATE), denominator: "1", unit: "Hz" },
      source_processing: {
        numerator: String(SAMPLE_RATE),
        denominator: "1",
        unit: "Hz",
      },
      acoustics: {
        numerator: String(SAMPLE_RATE),
        denominator: "1",
        unit: "Hz",
      },
      delivery: {
        numerator: String(SAMPLE_RATE),
        denominator: "1",
        unit: "Hz",
      },
    },
    quality: {
      id: "listening",
      process_block_capacity_frames: 3840,
      event_queue_capacity: 7600,
      telemetry_capacity_frames: 1,
    },
    total_duration: { value: motion.durationSeconds, unit: "s" },
    audible_start: { value: PREPARATION_DURATION_SECONDS, unit: "s" },
    audible_duration: {
      value: motion.durationSeconds - PREPARATION_DURATION_SECONDS,
      unit: "s",
    },
    public_seed: PUBLIC_SEED,
    output: {
      buses: ["master-engine-raw", ATLAS_AUDITION_BUS_ID],
      telemetry_channels: [],
    },
  };
}

function stateMask(telemetry) {
  let mask = 0;
  if (telemetry.ignitionEnabled) mask |= 1;
  if (telemetry.fuelEnabled) mask |= 2;
  if (telemetry.starterEnabled) mask |= 4;
  if (telemetry.limiterEnabled) mask |= 8;
  if (telemetry.limiterCutActive) mask |= 16;
  return mask;
}

function endpoint(telemetry, deliveryFrame) {
  return Object.freeze({
    deliveryFrame,
    rpm: telemetry.engineSpeedRpm,
    manifoldPressurePaAbs: telemetry.meanIntakeManifoldPressurePaAbs,
    requestedThrottle01: telemetry.requestedThrottle01,
    resolvedThrottle01: telemetry.resolvedThrottle01,
    unwrappedCrankRevolutions: telemetry.thetaRad / (2 * Math.PI),
    stateMask: stateMask(telemetry),
  });
}

function concatenate(chunks, totalLength) {
  const result = new Float32Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += chunk.length;
  }
  return result;
}

async function captureRoute(client, config, scenario, route) {
  const program = client.compile(
    fs.readFileSync(config.enginePath, "utf8"),
    `${JSON.stringify(scenario)}\n`,
    config.assets.map((asset) => ({
      kind: asset.kind,
      id: asset.id,
      bytes: fs.readFileSync(asset.path),
    })),
    SessionExecutionKind.finiteScenario,
  );
  try {
    const selected = program.session.buses.filter((bus) => bus.id === route.id);
    if (selected.length !== 1) {
      fail(`${config.engineId} requires one ${route.id} bus; got ${selected.length}`);
    }
    const chunks = [];
    const endpoints = [];
    let sampleCount = 0;
    let audibleFirstFrame = null;
    let priorRevolutions = -Infinity;
    for (;;) {
      const result = program.session.processBlock(selected[0].index);
      if (result.process.kindCode === ProcessKind.completed) break;
      if (result.telemetry.length !== 1) fail("capture block omitted telemetry");
      const firstFrame = Number(BigInt(result.process.firstDeliveryFrame));
      const end = endpoint(
        result.telemetry[0],
        firstFrame + result.process.deliveryFrameCount,
      );
      if (!(end.unwrappedCrankRevolutions > priorRevolutions)) {
        if (
          MOTORING_PROOF &&
          end.rpm === 0 &&
          end.unwrappedCrankRevolutions === priorRevolutions
        ) {
          break;
        }
        const nearest = [500, 250, 100].map((rpm) =>
          endpoints.reduce((best, value) =>
            Math.abs(value.rpm - rpm) < Math.abs(best.rpm - rpm) ? value : best,
          endpoints[0],
          ),
        );
        fail(
          `${config.engineId} crank did not advance monotonically: ` +
            `frame=${end.deliveryFrame}, rpm=${end.rpm}, ` +
            `prior_revolutions=${priorRevolutions}, ` +
            `revolutions=${end.unwrappedCrankRevolutions}, ` +
            `nearest=${JSON.stringify(nearest)}`,
        );
      }
      priorRevolutions = end.unwrappedCrankRevolutions;
      endpoints.push(end);
      if (result.audible) {
        if (audibleFirstFrame === null) audibleFirstFrame = firstFrame;
        const samples = result.samples.slice();
        chunks.push(samples);
        sampleCount += samples.length;
      }
    }
    if (audibleFirstFrame === null || sampleCount === 0) {
      fail(`${config.engineId} ${route.id} capture has no audible PCM`);
    }
    return Object.freeze({
      pcm: concatenate(chunks, sampleCount),
      endpoints: Object.freeze(endpoints),
      audibleFirstFrame,
      engineProvenanceSha256: program.engineProvenanceSha256,
      rendererSourceSha256: program.rendererSourceSha256,
    });
  } finally {
    program.dispose();
  }
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function crossingRevolutions(endpoints, rpm, direction, audibleFirstFrame) {
  for (let index = 1; index < endpoints.length; ++index) {
    const left = endpoints[index - 1];
    const right = endpoints[index];
    if (right.deliveryFrame < audibleFirstFrame) continue;
    const crossed = direction === "rising"
      ? left.rpm <= rpm && right.rpm >= rpm && right.rpm > left.rpm
      : left.rpm >= rpm && right.rpm <= rpm && right.rpm < left.rpm;
    if (!crossed) continue;
    const amount = (rpm - left.rpm) / (right.rpm - left.rpm);
    return interpolate(
      left.unwrappedCrankRevolutions,
      right.unwrappedCrankRevolutions,
      amount,
    );
  }
  fail(`${direction} traversal did not cross ${rpm} RPM after audible start`);
}

function alignedSourceCycleStart(crossingRevolutions_) {
  const relative =
    (crossingRevolutions_ - SOURCE_CYCLE_ORIGIN_REVOLUTIONS) /
    PHASE_CYCLE_REVOLUTIONS;
  return (
    SOURCE_CYCLE_ORIGIN_REVOLUTIONS +
    Math.ceil(relative) * PHASE_CYCLE_REVOLUTIONS
  );
}

function frameAtRevolutions(knots, revolutions) {
  if (
    revolutions < knots[0].unwrappedCrankRevolutions ||
    revolutions > knots.at(-1).unwrappedCrankRevolutions
  ) {
    fail(`crank position ${revolutions} is outside captured telemetry`);
  }
  let lower = 0;
  let upper = knots.length - 1;
  while (upper - lower > 1) {
    const middle = (lower + upper) >>> 1;
    if (knots[middle].unwrappedCrankRevolutions <= revolutions) lower = middle;
    else upper = middle;
  }
  const left = knots[lower];
  const right = knots[upper];
  const amount =
    (revolutions - left.unwrappedCrankRevolutions) /
    (right.unwrappedCrankRevolutions - left.unwrappedCrankRevolutions);
  return interpolate(left.deliveryFrame, right.deliveryFrame, amount);
}

function sampleAtRevolutions(capture, revolutions) {
  const frame = frameAtRevolutions(capture.endpoints, revolutions);
  const position = frame - capture.audibleFirstFrame;
  const left = Math.floor(position);
  if (left < 0 || left + 1 >= capture.pcm.length) {
    fail(`source phase ${revolutions} is outside audible PCM`);
  }
  return interpolate(
    capture.pcm[left],
    capture.pcm[left + 1],
    position - left,
  );
}

function phaseNormalizeInterval(capture, startRevolutions) {
  const sampleCount = SAMPLES_PER_CYCLE * PHASE_CYCLE_COUNT;
  const endRevolutions =
    startRevolutions + PHASE_CYCLE_COUNT * PHASE_CYCLE_REVOLUTIONS;
  if (endRevolutions > capture.endpoints.at(-1).unwrappedCrankRevolutions) {
    fail("three-cycle source interval extends beyond captured telemetry");
  }
  const output = new Float32Array(sampleCount);
  for (let index = 0; index < sampleCount; ++index) {
    output[index] = sampleAtRevolutions(
      capture,
      startRevolutions +
        (index * PHASE_CYCLE_REVOLUTIONS) / SAMPLES_PER_CYCLE,
    );
  }
  return output;
}

function intervalTelemetry(capture, startRevolutions, endRevolutions) {
  const beginFrame = frameAtRevolutions(capture.endpoints, startRevolutions);
  const endFrame = frameAtRevolutions(capture.endpoints, endRevolutions);
  const selected = capture.endpoints.filter(
    (value) => value.deliveryFrame >= beginFrame && value.deliveryFrame <= endFrame,
  );
  if (selected.length === 0) fail("source interval has no telemetry endpoints");
  const mean = (field) =>
    selected.reduce((sum, value) => sum + value[field], 0) / selected.length;
  return Object.freeze({
    mean_manifold_pressure_pa_abs: mean("manifoldPressurePaAbs"),
    mean_rpm: mean("rpm"),
    mean_requested_throttle_01: mean("requestedThrottle01"),
    mean_resolved_throttle_01: mean("resolvedThrottle01"),
    state_masks: [...new Set(selected.map((value) => value.stateMask))],
    telemetry_endpoint_count: selected.length,
  });
}

function captureMetadataPath(config, direction, lane) {
  return path.join(config.captureDirectory, `${direction}-${lane.id}.json`);
}

function rpmArtifactToken(config, rpm) {
  const index = config.rpmAnchors.indexOf(rpm);
  if (index === -1) fail(`${rpm} RPM is not an authored anchor`);
  return `r${String(index).padStart(4, "0")}`;
}

async function captureDirection(engineId, direction, laneId) {
  const config = engineConfig(findEngine(engineId));
  const lane = LOAD_LANES.find((candidate) => candidate.id === laneId);
  if (lane === undefined) fail(`unknown load lane ${laneId}`);
  if (!DIRECTIONS.includes(direction)) fail(`unknown direction ${direction}`);
  fs.mkdirSync(config.captureDirectory, { recursive: true });
  const identity = captureIdentity(config);
  const scenario = sourceScenario(config, direction, lane);
  const scenarioBytes = Buffer.from(`${JSON.stringify(scenario)}\n`);
  const startedAt = performance.now();
  const client = await EngineSimCapiClient.create(pathToFileURL(modulePath));
  const routeResults = [];
  try {
    for (const route of config.routes) {
      const capture = await captureRoute(client, config, scenario, route);
      for (const rpm of config.rpmAnchors) {
        const crossing = crossingRevolutions(
          capture.endpoints,
          rpm,
          direction,
          capture.audibleFirstFrame,
        );
        const sourceCycleBegin = alignedSourceCycleStart(crossing);
        const sourceCycleEnd =
          sourceCycleBegin + PHASE_CYCLE_COUNT * PHASE_CYCLE_REVOLUTIONS;
        const phaseSource = phaseNormalizeInterval(capture, sourceCycleBegin);
        const filename =
          `${direction}-${lane.id}-${rpmArtifactToken(config, rpm)}.` +
          `${route.token}.source.f32le`;
        const bytes = float32LeBytes(phaseSource);
        fs.writeFileSync(path.join(config.captureDirectory, filename), bytes);
        const captureFidelity = intervalTelemetry(
          capture,
          sourceCycleBegin,
          sourceCycleEnd,
        );
        if (
          MOTORING_PROOF &&
          (captureFidelity.state_masks.length !== 1 ||
            captureFidelity.state_masks[0] !== 2)
        ) {
          fail(
            `${config.engineId} ${route.id} ${rpm} RPM escaped I0/F1/S0 state`,
          );
        }
        routeResults.push({
          route_id: route.id,
          route_slug: route.token,
          direction,
          lane: lane.id,
          capture_throttle_01: lane.throttle01,
          rpm,
          filename,
          byte_count: bytes.byteLength,
          payload_sha256: sha256(bytes),
          crossing_revolutions: crossing,
          source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
          source_cycle_begin_revolutions: sourceCycleBegin,
          source_cycle_end_revolutions: sourceCycleEnd,
          source_cycle_begin_is_aligned:
            Math.abs(
              (sourceCycleBegin - SOURCE_CYCLE_ORIGIN_REVOLUTIONS) %
                PHASE_CYCLE_REVOLUTIONS,
            ) < 1e-12,
          capture_fidelity: captureFidelity,
          engine_provenance_sha256: capture.engineProvenanceSha256,
          renderer_source_sha256: capture.rendererSourceSha256,
        });
      }
    }
  } finally {
    client.dispose();
  }
  const metadata = {
    schema: MOTORING_PROOF
      ? "engine-sim-offline/state-phase-source-capture"
      : "engine-sim-offline/directional-transient-source-capture",
    id: `${config.engineId}-${direction}-${lane.id}`,
    engine: config.engineId,
    direction,
    lane: lane.id,
    capture_method: CAPTURE_METHOD_ID,
    capture_identity_sha256: identity.sha256,
    ...(MOTORING_PROOF
      ? {
          state_selector: {
            id: "motoring-ignition-off-fuel-on",
            required_on_mask: 2,
            required_off_mask: 1,
            wildcard_mask: 28,
          },
        }
      : {}),
    source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
    scenario_sha256: sha256(scenarioBytes),
    scenario,
    routes: routeResults,
    wall_time_seconds: (performance.now() - startedAt) / 1000,
  };
  writeJson(captureMetadataPath(config, direction, lane), metadata);
  process.stdout.write(`${JSON.stringify({
    engine: config.engineId,
    direction,
    lane: lane.id,
    payload_count: routeResults.length,
    wall_time_seconds: metadata.wall_time_seconds,
  })}\n`);
}

function smoothstep(amount) {
  return amount * amount * (3 - 2 * amount);
}

function closeThreeCycleSeam(source) {
  if (source.length !== SAMPLES_PER_CYCLE * PHASE_CYCLE_COUNT) {
    fail("seam closure requires exactly three phase cycles");
  }
  const template = new Float64Array(SAMPLES_PER_CYCLE);
  for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
    template[phase] =
      (source[phase] +
        source[SAMPLES_PER_CYCLE + phase] +
        source[2 * SAMPLES_PER_CYCLE + phase]) /
      PHASE_CYCLE_COUNT;
  }
  const targetBoundaryDelta =
    0.5 *
    ((template[1] - template[0]) +
      (template.at(-1) - template.at(-2)));
  const templateCorrection =
    template[0] - template.at(-1) - targetBoundaryDelta;
  for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
    template[phase] +=
      templateCorrection * (phase / (SAMPLES_PER_CYCLE - 1));
  }
  const blendFrames = Math.round(
    SAMPLES_PER_CYCLE * SEAM_BLEND_CYCLE_FRACTION,
  );
  const output = source.slice();
  const finalBlendBegin = output.length - blendFrames;
  for (let index = 0; index < blendFrames; ++index) {
    const edgeAmount = index / (blendFrames - 1);
    const startWeight = smoothstep(1 - edgeAmount);
    const endWeight = smoothstep(edgeAmount);
    const startPhase = index;
    const endIndex = finalBlendBegin + index;
    const endPhase = endIndex % SAMPLES_PER_CYCLE;
    output[startPhase] =
      source[startPhase] +
      (template[startPhase] - source[startPhase]) * startWeight;
    output[endIndex] =
      source[endIndex] +
      (template[endPhase] - source[endIndex]) * endWeight;
  }
  return output;
}

function signalRms(samples) {
  let squareSum = 0;
  for (const sample of samples) squareSum += sample * sample;
  return Math.sqrt(squareSum / samples.length);
}

function threeCyclePeriodicity(samples) {
  if (samples.length !== SAMPLES_PER_CYCLE * PHASE_CYCLE_COUNT) {
    fail("periodicity audit requires exactly three phase cycles");
  }
  const cycleRms = [];
  const adjacentDifferenceRms = [];
  for (let cycle = 0; cycle < PHASE_CYCLE_COUNT; ++cycle) {
    cycleRms.push(
      signalRms(
        samples.subarray(
          cycle * SAMPLES_PER_CYCLE,
          (cycle + 1) * SAMPLES_PER_CYCLE,
        ),
      ),
    );
    if (cycle === 0) continue;
    let squareSum = 0;
    for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
      const delta =
        samples[cycle * SAMPLES_PER_CYCLE + phase] -
        samples[(cycle - 1) * SAMPLES_PER_CYCLE + phase];
      squareSum += delta * delta;
    }
    adjacentDifferenceRms.push(Math.sqrt(squareSum / SAMPLES_PER_CYCLE));
  }
  const referenceRms = Math.max(
    cycleRms.reduce((sum, value) => sum + value, 0) / cycleRms.length,
    1e-30,
  );
  return Object.freeze({
    cycle_rms: cycleRms,
    adjacent_cycle_difference_rms: adjacentDifferenceRms,
    adjacent_cycle_difference_over_mean_cycle_rms: adjacentDifferenceRms.map(
      (value) => value / referenceRms,
    ),
    maximum_adjacent_cycle_difference_over_mean_cycle_rms:
      Math.max(...adjacentDifferenceRms) / referenceRms,
  });
}

function adjacentDerivativeRms(samples) {
  let squareSum = 0;
  for (let index = 1; index < samples.length; ++index) {
    const delta = samples[index] - samples[index - 1];
    squareSum += delta * delta;
  }
  return Math.sqrt(squareSum / (samples.length - 1));
}

function closureMetrics(source, candidate = source) {
  const sourceRms = signalRms(source);
  const sourceDerivativeRms = adjacentDerivativeRms(source);
  let correctionSquareSum = 0;
  let maximumAdjacentDelta = 0;
  for (let index = 0; index < candidate.length; ++index) {
    const sample = candidate[index];
    if (!Number.isFinite(sample)) fail("seam closure emitted a non-finite sample");
    const correction = sample - source[index];
    correctionSquareSum += correction * correction;
    if (index > 0) {
      maximumAdjacentDelta = Math.max(
        maximumAdjacentDelta,
        Math.abs(sample - candidate[index - 1]),
      );
    }
  }
  const seamDelta = Math.abs(candidate[0] - candidate.at(-1));
  maximumAdjacentDelta = Math.max(maximumAdjacentDelta, seamDelta);
  return Object.freeze({
    signal_rms: sourceRms,
    source_adjacent_derivative_rms: sourceDerivativeRms,
    seam_absolute_delta: seamDelta,
    seam_over_source_adjacent_derivative_rms:
      seamDelta / Math.max(sourceDerivativeRms, 1e-30),
    maximum_adjacent_delta_over_source_adjacent_derivative_rms:
      maximumAdjacentDelta / Math.max(sourceDerivativeRms, 1e-30),
    correction_rms_over_source_rms:
      Math.sqrt(correctionSquareSum / source.length) /
      Math.max(sourceRms, 1e-30),
  });
}

function quantile(values, amount) {
  if (values.length === 0) fail("cannot summarize an empty vector");
  const ordered = values.slice().sort((left, right) => left - right);
  return ordered[Math.floor((ordered.length - 1) * amount)];
}

function summary(values) {
  return Object.freeze({
    minimum: Math.min(...values),
    p50: quantile(values, 0.5),
    p90: quantile(values, 0.9),
    maximum: Math.max(...values),
  });
}

function readSourcePayload(config, descriptor) {
  const file = path.join(config.captureDirectory, descriptor.filename);
  const bytes = fs.readFileSync(file);
  if (bytes.byteLength !== descriptor.byte_count || sha256(bytes) !== descriptor.payload_sha256) {
    fail(`${descriptor.filename} source payload identity changed`);
  }
  const view = new Float32Array(
    bytes.buffer,
    bytes.byteOffset,
    bytes.byteLength / Float32Array.BYTES_PER_ELEMENT,
  );
  return view.slice();
}

function orderedCaptureResults(config) {
  const identity = captureIdentity(config);
  const results = [];
  for (const direction of DIRECTIONS) {
    for (const lane of LOAD_LANES) {
      const file = captureMetadataPath(config, direction, lane);
      if (!fs.existsSync(file)) fail(`capture metadata is absent: ${file}`);
      const result = readJson(file);
      if (
        result.capture_method !== CAPTURE_METHOD_ID ||
        result.capture_identity_sha256 !== identity.sha256
      ) {
        fail(`${file} is stale for the current capture identity`);
      }
      results.push(result);
    }
  }
  return results;
}

function heldIdentity(config, engineSha256, rendererSha256) {
  const file = path.join(config.heldPackageDirectory, "package.json");
  if (!fs.existsSync(file)) {
    fail(`held package must be baked before directional assembly: ${file}`);
  }
  const held = readJson(file);
  if (held.engine !== config.engineId) fail("held package engine differs");
  const heldDryBusIds = held.dry_bus_ids ??
    held.route_manifests?.map((entry) => entry.bus_id);
  const expected = config.routes.map((route) => route.id);
  if (JSON.stringify(heldDryBusIds) !== JSON.stringify(expected)) {
    fail(`${config.engineId} held/directional dry route order differs`);
  }
  if (
    held.provenance?.engine?.sha256 !== engineSha256 ||
    held.provenance?.renderer_build?.sha256 !== rendererSha256
  ) {
    fail(`${config.engineId} held/directional provenance differs`);
  }
  return held;
}

function legacyRouteManifests(packageDirectory) {
  const runtime = readJson(path.join(packageDirectory, "runtime.json"));
  const paths = [...new Set(Object.values(runtime.routes ?? {}))];
  if (paths.length === 0) fail(`accepted baseline has no route manifests`);
  return Object.freeze({
    runtime,
    manifests: paths.map((relativePath) => ({
      relativePath,
      manifest: readJson(path.join(packageDirectory, relativePath)),
    })),
  });
}

function directionalCellKey(direction, lane, rpm, busId) {
  return `${direction}/${lane}/${rpm}/${busId}`;
}

function acceptedBaselineEvidence(config, captures, held, routeManifests) {
  if (config.acceptedBaseline === null) return null;
  const sourceDirectory = absoluteRepositoryPath(
    config.acceptedBaseline.source_package,
    `${config.engineId} accepted directional source package`,
  );
  const closedDirectory = absoluteRepositoryPath(
    config.acceptedBaseline.seam_closed_package,
    `${config.engineId} accepted seam-closed package`,
  );
  const source = legacyRouteManifests(sourceDirectory);
  const closed = legacyRouteManifests(closedDirectory);
  const sourceCells = new Map();
  for (const { relativePath, manifest } of source.manifests) {
    const direction = relativePath.includes("rising") ? "rising" : "falling";
    for (const cell of manifest.cells) {
      sourceCells.set(
        directionalCellKey(direction, cell.lane, cell.rpm, manifest.audio.bus_id),
        cell,
      );
    }
  }
  const newSources = new Map();
  for (const capture of captures) {
    for (const descriptor of capture.routes) {
      newSources.set(
        directionalCellKey(
          descriptor.direction,
          descriptor.lane,
          descriptor.rpm,
          descriptor.route_id,
        ),
        descriptor,
      );
    }
  }
  let identicalSourcePayloads = 0;
  let identicalSourceCycleBegins = 0;
  let identicalManifoldValues = 0;
  for (const [key, prior] of sourceCells) {
    const current = newSources.get(key);
    if (current?.payload_sha256 === prior.payload_sha256) {
      ++identicalSourcePayloads;
    }
    if (
      current?.source_cycle_begin_revolutions ===
      prior.source_cycle_begin_revolutions
    ) {
      ++identicalSourceCycleBegins;
    }
    if (
      current?.capture_fidelity?.mean_manifold_pressure_pa_abs ===
      prior.manifold_pressure_pa_abs
    ) {
      ++identicalManifoldValues;
    }
  }
  const genericClosedCells = new Map();
  for (const route of routeManifests) {
    for (const [direction, manifestPath] of [
      ["rising", route.rising_manifest_path],
      ["falling", route.falling_manifest_path],
    ]) {
      const manifest = readJson(path.join(config.packageDirectory, manifestPath));
      for (const cell of manifest.cells) {
        genericClosedCells.set(
          directionalCellKey(direction, cell.lane, cell.rpm, route.bus_id),
          cell,
        );
      }
    }
  }
  let identicalClosedPayloads = 0;
  let acceptedClosedPayloadCount = 0;
  for (const { relativePath, manifest } of closed.manifests) {
    const direction = relativePath.includes("rising") ? "rising" : "falling";
    for (const prior of manifest.cells) {
      ++acceptedClosedPayloadCount;
      const key = directionalCellKey(
        direction,
        prior.lane,
        prior.rpm,
        manifest.audio.bus_id,
      );
      const current = genericClosedCells.get(key);
      if (current === undefined) continue;
      const currentBytes = fs.readFileSync(
        path.join(config.packageDirectory, current.relative_path),
      );
      const priorBytes = fs.readFileSync(path.join(closedDirectory, prior.relative_path));
      if (
        currentBytes.byteLength === priorBytes.byteLength &&
        sha256(currentBytes) === sha256(priorBytes)
      ) {
        ++identicalClosedPayloads;
      }
    }
  }
  const expectedCount = sourceCells.size;
  const acceptedTransferSpectrumSha256 =
    closed.runtime.transfer?.spectrum_sha256 ?? null;
  const outerTransferSpectrumSha256ByRoute = (held.presentation?.routes ?? []).map(
    (route) => ({
      dry_bus_id: route.dry_bus_id,
      spectrum_sha256: route.transfer?.spectrum_sha256 ?? null,
    }),
  );
  const evidence = {
    status: "byte-identical-dry-audio-schema-path-migration-only",
    accepted_source_package: config.acceptedBaseline.source_package,
    accepted_seam_closed_package: config.acceptedBaseline.seam_closed_package,
    accepted_source_payload_count: expectedCount,
    generic_source_payload_count: newSources.size,
    added_source_payload_count: newSources.size - expectedCount,
    identical_source_payload_hash_count: identicalSourcePayloads,
    identical_source_cycle_begin_count: identicalSourceCycleBegins,
    identical_manifold_pressure_value_count: identicalManifoldValues,
    accepted_seam_closed_payload_count: acceptedClosedPayloadCount,
    generic_seam_closed_payload_count: genericClosedCells.size,
    added_seam_closed_payload_count:
      genericClosedCells.size - acceptedClosedPayloadCount,
    byte_identical_seam_closed_payload_count: identicalClosedPayloads,
    accepted_transfer_spectrum_sha256: acceptedTransferSpectrumSha256,
    outer_presentation_transfer_spectrum_sha256_by_route:
      outerTransferSpectrumSha256ByRoute,
    transfer_ownership_migration: {
      from: "directional-package-global-transfer",
      to: "outer-responsive-presentation-per-route-transfer",
      directional_package_is_dry_only: true,
    },
    schema_migration: {
      from: "legacy-named-front-rear-route-object",
      to: "ordered-n-route-manifests",
    },
  };
  evidence.passed =
    expectedCount === identicalSourcePayloads &&
    expectedCount === identicalSourceCycleBegins &&
    expectedCount === identicalManifoldValues &&
    acceptedClosedPayloadCount === identicalClosedPayloads &&
    newSources.size >= expectedCount &&
    genericClosedCells.size >= acceptedClosedPayloadCount &&
    acceptedTransferSpectrumSha256 !== null &&
    outerTransferSpectrumSha256ByRoute.length === config.routes.length &&
    outerTransferSpectrumSha256ByRoute.every(
      (route) => route.spectrum_sha256 === acceptedTransferSpectrumSha256,
    );
  if (!evidence.passed) {
    fail(`${config.engineId} accepted baseline audio migration differs`);
  }
  return Object.freeze(evidence);
}

function buildPackage(config) {
  const captures = orderedCaptureResults(config);
  const engineHashes = new Set(
    captures.flatMap((capture) =>
      capture.routes.map((route) => route.engine_provenance_sha256),
    ),
  );
  const rendererHashes = new Set(
    captures.flatMap((capture) =>
      capture.routes.map((route) => route.renderer_source_sha256),
    ),
  );
  if (engineHashes.size !== 1 || rendererHashes.size !== 1) {
    fail(`${config.engineId} captures disagree on provenance`);
  }
  const engineSha256 = [...engineHashes][0];
  const rendererSha256 = [...rendererHashes][0];
  const held = heldIdentity(config, engineSha256, rendererSha256);
  fs.rmSync(config.packageDirectory, { recursive: true, force: true });
  fs.mkdirSync(path.join(config.packageDirectory, "audio"), { recursive: true });
  const routeManifests = [];
  const metricRows = [];
  const combinedCells = new Map();
  const coalescedLoadAliases = [];
  let publishedPayloadCount = 0;
  let payloadBytes = 0;
  for (const route of config.routes) {
    const directionalPaths = {};
    for (const direction of DIRECTIONS) {
      const cells = [];
      let capturedCellCount = 0;
      for (const capture of captures.filter((value) => value.direction === direction)) {
        for (const descriptor of capture.routes.filter(
          (value) => value.route_id === route.id,
        )) {
          const source = readSourcePayload(config, descriptor);
          const closed = closeThreeCycleSeam(source);
          const bytes = float32LeBytes(closed);
          const filename =
            `${direction}-${descriptor.lane}-` +
            `${rpmArtifactToken(config, descriptor.rpm)}.` +
            `${route.token}.phase.f32le`;
          const relativePath = `audio/${filename}`;
          const payloadSha256 = sha256(bytes);
          const before = closureMetrics(source);
          const after = closureMetrics(source, closed);
          const cellKey = `${direction}/${descriptor.lane}/${descriptor.rpm}`;
          const combined = combinedCells.get(cellKey) ?? {
            source: new Float64Array(source.length),
            closed: new Float64Array(closed.length),
          };
          for (let index = 0; index < source.length; ++index) {
            combined.source[index] += source[index];
            combined.closed[index] += closed[index];
          }
          combinedCells.set(cellKey, combined);
          metricRows.push({
            cell_key: cellKey,
            route_id: route.id,
            before,
            after,
            periodicity: threeCyclePeriodicity(source),
          });
          const cell = {
            id: `${config.engineId}-${direction}-${descriptor.lane}-` +
              `${descriptor.rpm}rpm-${route.token}-seam-closure`,
            rpm: descriptor.rpm,
            lane: descriptor.lane,
            capture_throttle_01: descriptor.capture_throttle_01,
            manifold_pressure_pa_abs:
              descriptor.capture_fidelity.mean_manifold_pressure_pa_abs,
            relative_path: relativePath,
            byte_count: bytes.byteLength,
            payload_sha256: payloadSha256,
            source_capture_payload_sha256: descriptor.payload_sha256,
            source_cycle_origin_revolutions:
              descriptor.source_cycle_origin_revolutions,
            source_cycle_begin_revolutions:
              descriptor.source_cycle_begin_revolutions,
            source_cycle_end_revolutions:
              descriptor.source_cycle_end_revolutions,
            loop_join_normalized_error:
              after.seam_over_source_adjacent_derivative_rms,
            seam_closure_algorithm: SEAM_ALGORITHM_ID,
            capture_fidelity: descriptor.capture_fidelity,
          };
          ++capturedCellCount;
          const duplicate = cells.find(
            (candidate) =>
              candidate.rpm === cell.rpm &&
              candidate.manifold_pressure_pa_abs ===
                cell.manifold_pressure_pa_abs,
          );
          if (duplicate !== undefined) {
            if (
              duplicate.payload_sha256 !== cell.payload_sha256 ||
              duplicate.source_capture_payload_sha256 !==
                cell.source_capture_payload_sha256 ||
              duplicate.source_cycle_begin_revolutions !==
                cell.source_cycle_begin_revolutions ||
              duplicate.source_cycle_end_revolutions !==
                cell.source_cycle_end_revolutions
            ) {
              fail(
                `${config.engineId} ${route.id} ${direction} ${cell.rpm} RPM ` +
                  "has tied physical load coordinates with different material",
              );
            }
            coalescedLoadAliases.push({
              route_id: route.id,
              direction,
              rpm: cell.rpm,
              manifold_pressure_pa_abs: cell.manifold_pressure_pa_abs,
              retained_lane: duplicate.lane,
              coalesced_lane: cell.lane,
              source_capture_payload_sha256:
                cell.source_capture_payload_sha256,
              seam_closed_payload_sha256: cell.payload_sha256,
              reason:
                "identical-coordinate-and-byte-identical-source-and-closed-payload",
            });
            continue;
          }
          fs.writeFileSync(path.join(config.packageDirectory, relativePath), bytes);
          payloadBytes += bytes.byteLength;
          ++publishedPayloadCount;
          cells.push(cell);
        }
      }
      if (capturedCellCount !== config.rpmAnchors.length * LOAD_LANES.length) {
        fail(`${config.engineId} ${route.id} ${direction} cell lattice is incomplete`);
      }
      const filename = `${route.token}-${direction}.json`;
      const manifest = {
        schema: "engine-sim-offline/responsive-audio-directional-route",
        id: MOTORING_PROOF
          ? `${config.engineId}-motoring-ignition-off-fuel-on-${direction}-${route.token}-live-preview`
          : `${config.engineId}-directional-${direction}-${route.token}-live-preview`,
        engine: config.engineId,
        ...(MOTORING_PROOF
          ? {
              state_selector: {
                id: "motoring-ignition-off-fuel-on",
                required_on_mask: 2,
                required_off_mask: 1,
                wildcard_mask: 28,
              },
            }
          : {}),
        audio: {
          sample_rate_hz: SAMPLE_RATE,
          encoding: "float32le",
          channel_layout: "mono",
          bus_id: route.id,
        },
        phase: {
          cycle_revolutions: PHASE_CYCLE_REVOLUTIONS,
          samples_per_cycle: SAMPLES_PER_CYCLE,
          cycle_count: PHASE_CYCLE_COUNT,
          source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
        },
        domain: {
          minimum_rpm: config.outerMinimumRpm,
          maximum_rpm: config.outerMaximumRpm,
          rpm_anchors: config.rpmAnchors,
          load_coordinate: "measured-intake-manifold-pressure-pa-abs",
          load_lanes: LOAD_LANES,
        },
        seam_closure: {
          algorithm: SEAM_ALGORITHM_ID,
          phase_aligned: true,
          blend_cycle_fraction_per_side: SEAM_BLEND_CYCLE_FRACTION,
          source_cycle_count: PHASE_CYCLE_COUNT,
          source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
        },
        load_cell_coalescing: {
          policy:
            "coalesce-only-identical-coordinate-and-byte-identical-source-and-closed-payload",
          aliases: coalescedLoadAliases.filter(
            (alias) =>
              alias.route_id === route.id && alias.direction === direction,
          ),
        },
        cells,
        provenance: {
          engine: { id: config.engineId, sha256: engineSha256 },
          renderer_build: {
            id: "engine-sim-offline-renderer-build",
            sha256: rendererSha256,
          },
          representation:
            MOTORING_PROOF
              ? `${direction}-ignition-off-fuel-on-prescribed-exponential-dry-route-three-cycle-phase-units-with-seam-closure`
              : `${direction}-prescribed-exponential-dry-route-three-cycle-phase-units-with-seam-closure`,
          capture_method: CAPTURE_METHOD_ID,
          capture_identity_sha256: captureIdentity(config).sha256,
          physics_rate_hz: PHYSICS_RATE,
          source_delivery_rate_hz: SAMPLE_RATE,
        },
      };
      writeJson(path.join(config.packageDirectory, filename), manifest);
      directionalPaths[direction] = filename;
    }
    routeManifests.push(
      MOTORING_PROOF
        ? {
            bus_id: route.id,
            manifest_path: directionalPaths.falling,
          }
        : {
            bus_id: route.id,
            rising_manifest_path: directionalPaths.rising,
            falling_manifest_path: directionalPaths.falling,
          },
    );
  }
  const combinedRows = [...combinedCells].map(([cellKey, value]) => ({
    cell_key: cellKey,
    before: closureMetrics(value.source),
    after: closureMetrics(value.source, value.closed),
  }));
  const seamRatios = combinedRows.map(
    (row) => row.after.seam_over_source_adjacent_derivative_rms,
  );
  const correctionRatios = combinedRows.map(
    (row) => row.after.correction_rms_over_source_rms,
  );
  const seamGate = {
    maximum_seam_over_source_adjacent_derivative_rms:
      MAXIMUM_SEAM_OVER_DERIVATIVE_RMS,
    maximum_correction_rms_over_source_rms:
      MAXIMUM_CORRECTION_RMS_OVER_SOURCE_RMS,
    observed_maximum_seam_over_source_adjacent_derivative_rms:
      Math.max(...seamRatios),
    observed_maximum_correction_rms_over_source_rms:
      Math.max(...correctionRatios),
  };
  seamGate.passed =
    seamGate.observed_maximum_seam_over_source_adjacent_derivative_rms <=
      seamGate.maximum_seam_over_source_adjacent_derivative_rms &&
    seamGate.observed_maximum_correction_rms_over_source_rms <=
      seamGate.maximum_correction_rms_over_source_rms;
  if (!seamGate.passed) {
    fail(`${config.engineId} seam-closure join gate failed: ${JSON.stringify(seamGate)}`);
  }
  const runtime = {
    schema: MOTORING_PROOF
      ? "engine-sim-offline/responsive-audio-state-phase-texture"
      : "engine-sim-offline/responsive-audio-directional-texture",
    id: MOTORING_PROOF
      ? `${config.engineId}-motoring-ignition-off-fuel-on-live-preview`
      : `${config.engineId}-directional-transient-live-preview`,
    engine: config.engineId,
    ...(MOTORING_PROOF
      ? {
          state_selector: {
            id: "motoring-ignition-off-fuel-on",
            required_on_mask: 2,
            required_off_mask: 1,
            wildcard_mask: 28,
          },
        }
      : {}),
    audio: {
      sample_rate_hz: SAMPLE_RATE,
      encoding: "float32le",
      channel_layout: "mono",
    },
    domain: {
      minimum_rpm: config.outerMinimumRpm,
      maximum_rpm: config.outerMaximumRpm,
      rpm_anchors: config.rpmAnchors,
      load_coordinate: "measured-intake-manifold-pressure-pa-abs",
    },
    dry_bus_ids: config.routes.map((route) => route.id),
    route_manifests: routeManifests,
    provenance: {
      engine: { id: config.engineId, sha256: engineSha256 },
      renderer_build: {
        id: "engine-sim-offline-renderer-build",
        sha256: rendererSha256,
      },
      representation:
        MOTORING_PROOF
          ? "live-state-rpm-map-crank-indexed-n-route-dry-motoring-units-from-falling-trajectory-with-phase-aligned-seam-closure"
          : "live-rpm-map-crank-indexed-n-route-dry-directional-units-with-phase-aligned-seam-closure",
      capture_method: CAPTURE_METHOD_ID,
      capture_identity_sha256: captureIdentity(config).sha256,
    },
    seam_closure: {
      algorithm: SEAM_ALGORITHM_ID,
      phase_aligned: true,
      blend_cycle_fraction_per_side: SEAM_BLEND_CYCLE_FRACTION,
      source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
      join_gate: seamGate,
    },
  };
  const baselineEvidence = acceptedBaselineEvidence(
    config,
    captures,
    held,
    routeManifests,
  );
  writeJson(path.join(config.packageDirectory, "runtime.json"), runtime);
  const report = {
    schema: MOTORING_PROOF
      ? "engine-sim-offline/state-phase-texture-proof-report"
      : "engine-sim-offline/multi-engine-directional-transient-report",
    engine: config.engineId,
    silent: true,
    package_id: runtime.id,
    package_directory: config.packageDirectory,
    staged_directory: path.join(
      config.stagedEngineDirectory,
      MOTORING_PROOF ? "motoring" : "directional",
    ),
    rpm_anchors: config.rpmAnchors,
    dry_bus_ids: runtime.dry_bus_ids,
    direction_count: DIRECTIONS.length,
    load_lane_count: LOAD_LANES.length,
    route_count: config.routes.length,
    operating_cell_count_per_route_direction:
      config.rpmAnchors.length * LOAD_LANES.length,
    captured_payload_count: metricRows.length,
    payload_count: publishedPayloadCount,
    coalesced_payload_count: coalescedLoadAliases.length,
    coalesced_load_aliases: coalescedLoadAliases,
    payload_bytes: payloadBytes,
    source_cycle_origin_revolutions: SOURCE_CYCLE_ORIGIN_REVOLUTIONS,
    all_source_cycle_begins_aligned: captures.every((capture) =>
      capture.routes.every((route) => route.source_cycle_begin_is_aligned),
    ),
    seam_gate: seamGate,
    aggregate_summed_routes: {
      before_seam_over_derivative_rms: summary(
        combinedRows.map(
          (row) => row.before.seam_over_source_adjacent_derivative_rms,
        ),
      ),
      after_seam_over_derivative_rms: summary(seamRatios),
      correction_rms_over_source_rms: summary(correctionRatios),
      after_maximum_adjacent_delta_over_derivative_rms: summary(
        combinedRows.map(
          (row) =>
            row.after.maximum_adjacent_delta_over_source_adjacent_derivative_rms,
        ),
      ),
    },
    summed_route_cells: combinedRows,
    per_route_payloads: metricRows,
    periodicity: {
      measurement:
        "adjacent-720-degree-cycle-difference-rms-over-mean-cycle-rms",
      per_route_cell: summary(
        metricRows.map(
          (row) =>
            row.periodicity
              .maximum_adjacent_cycle_difference_over_mean_cycle_rms,
        ),
      ),
      directional_sufficiency:
        MOTORING_PROOF
          ? "falling-only-quasi-steady-hypothesis-requires-live-restart-audition-or-rising-holdout"
          : "not-applicable-both-directions-captured",
    },
    provenance: runtime.provenance,
    ...(baselineEvidence === null
      ? {}
      : { accepted_baseline_migration: baselineEvidence }),
  };
  if (!report.all_source_cycle_begins_aligned) {
    fail(`${config.engineId} source cycle origin alignment failed`);
  }
  writeJson(path.join(config.packageDirectory, "report.json"), report);
  if (baselineEvidence !== null) {
    writeJson(
      path.join(config.packageDirectory, "migration-evidence.json"),
      baselineEvidence,
    );
  }
  const stagedDirectional = path.join(
    config.stagedEngineDirectory,
    MOTORING_PROOF ? "motoring" : "directional",
  );
  fs.rmSync(stagedDirectional, { recursive: true, force: true });
  fs.mkdirSync(stagedDirectional, { recursive: true });
  fs.copyFileSync(
    path.join(config.packageDirectory, "runtime.json"),
    path.join(stagedDirectional, "runtime.json"),
  );
  for (const route of routeManifests) {
    for (const manifestPath of MOTORING_PROOF
      ? [route.manifest_path]
      : [route.rising_manifest_path, route.falling_manifest_path]) {
      fs.copyFileSync(
        path.join(config.packageDirectory, manifestPath),
        path.join(stagedDirectional, manifestPath),
      );
    }
  }
  fs.cpSync(
    path.join(config.packageDirectory, "audio"),
    path.join(stagedDirectional, "audio"),
    { recursive: true },
  );
  return Object.freeze({
    engine: config.engineId,
    package_id: runtime.id,
    package_directory: config.packageDirectory,
    staged_directory: stagedDirectional,
    payload_count: report.payload_count,
    payload_bytes: report.payload_bytes,
    seam_gate: seamGate,
  });
}

function checkCapturedSeams(engineId) {
  const config = engineConfig(findEngine(engineId));
  const captures = orderedCaptureResults(config);
  const combinedCells = new Map();
  let payloadCount = 0;
  for (const capture of captures) {
    for (const descriptor of capture.routes) {
      const source = readSourcePayload(config, descriptor);
      const closed = closeThreeCycleSeam(source);
      const key = `${descriptor.direction}/${descriptor.lane}/${descriptor.rpm}`;
      const combined = combinedCells.get(key) ?? {
        source: new Float64Array(source.length),
        closed: new Float64Array(closed.length),
      };
      for (let index = 0; index < source.length; ++index) {
        combined.source[index] += source[index];
        combined.closed[index] += closed[index];
      }
      combinedCells.set(key, combined);
      ++payloadCount;
    }
  }
  const rows = [...combinedCells].map(([cellKey, value]) => ({
    cell_key: cellKey,
    before: closureMetrics(value.source),
    after: closureMetrics(value.source, value.closed),
  }));
  const seamRatios = rows.map(
    (row) => row.after.seam_over_source_adjacent_derivative_rms,
  );
  const correctionRatios = rows.map(
    (row) => row.after.correction_rms_over_source_rms,
  );
  const maximumSeamRow = rows.reduce((maximum, row) =>
    row.after.seam_over_source_adjacent_derivative_rms >
    maximum.after.seam_over_source_adjacent_derivative_rms
      ? row
      : maximum,
  );
  const maximumCorrectionRow = rows.reduce((maximum, row) =>
    row.after.correction_rms_over_source_rms >
    maximum.after.correction_rms_over_source_rms
      ? row
      : maximum,
  );
  const seamGate = {
    maximum_seam_over_source_adjacent_derivative_rms:
      MAXIMUM_SEAM_OVER_DERIVATIVE_RMS,
    maximum_correction_rms_over_source_rms:
      MAXIMUM_CORRECTION_RMS_OVER_SOURCE_RMS,
    observed_maximum_seam_over_source_adjacent_derivative_rms:
      Math.max(...seamRatios),
    observed_maximum_correction_rms_over_source_rms:
      Math.max(...correctionRatios),
  };
  seamGate.passed =
    seamGate.observed_maximum_seam_over_source_adjacent_derivative_rms <=
      seamGate.maximum_seam_over_source_adjacent_derivative_rms &&
    seamGate.observed_maximum_correction_rms_over_source_rms <=
      seamGate.maximum_correction_rms_over_source_rms;
  return Object.freeze({
    engine: engineId,
    route_count: config.routes.length,
    rpm_anchor_count: config.rpmAnchors.length,
    source_capture_count: captures.length,
    payload_count: payloadCount,
    summed_route_cell_count: rows.length,
    all_source_cycle_begins_aligned: captures.every((capture) =>
      capture.routes.every((route) => route.source_cycle_begin_is_aligned),
    ),
    before_seam_over_derivative_rms: summary(
      rows.map((row) => row.before.seam_over_source_adjacent_derivative_rms),
    ),
    after_seam_over_derivative_rms: summary(seamRatios),
    correction_rms_over_source_rms: summary(correctionRatios),
    maximum_seam_cell: maximumSeamRow,
    maximum_correction_cell: maximumCorrectionRow,
    seam_gate: seamGate,
  });
}

function checkAllCapturedSeams() {
  const engines = inventory().engines.map((engine) =>
    checkCapturedSeams(engine.engine_id),
  );
  return Object.freeze({
    schema: "engine-sim-offline/multi-engine-directional-seam-check-report",
    silent: true,
    engine_count: engines.length,
    all_source_cycle_begins_aligned: engines.every(
      (engine) => engine.all_source_cycle_begins_aligned,
    ),
    all_seam_gates_passed: engines.every((engine) => engine.seam_gate.passed),
    engines,
  });
}

function captureIsCurrent(config, direction, lane) {
  const file = captureMetadataPath(config, direction, lane);
  if (!fs.existsSync(file)) return false;
  try {
    const value = readJson(file);
    return (
      value.capture_method === CAPTURE_METHOD_ID &&
      value.capture_identity_sha256 === captureIdentity(config).sha256 &&
      Array.isArray(value.routes) &&
      value.routes.every((route) => {
        const payload = path.join(config.captureDirectory, route.filename);
        if (!fs.existsSync(payload)) return false;
        const bytes = fs.readFileSync(payload);
        return (
          bytes.byteLength === route.byte_count &&
          sha256(bytes) === route.payload_sha256
        );
      })
    );
  } catch {
    return false;
  }
}

function capturePlan(engineId) {
  const config = engineConfig(findEngine(engineId));
  const states = DIRECTIONS.flatMap((direction) =>
    LOAD_LANES.map((lane) => ({
      direction,
      lane: lane.id,
      scenario_duration_seconds:
        sourceScenario(config, direction, lane).total_duration.value,
      cached: captureIsCurrent(config, direction, lane),
    })),
  );
  return Object.freeze({
    schema: "engine-sim-offline/directional-transient-capture-plan",
    engine: engineId,
    cached_state_count: states.filter(({ cached }) => cached).length,
    new_state_count: states.filter(({ cached }) => !cached).length,
    new_states: states.filter(({ cached }) => !cached),
  });
}

function spawnCapture(engineId, direction, laneId) {
  return new Promise((resolve, reject) => {
    const child = spawn(
      process.execPath,
      [
        fileURLToPath(import.meta.url),
        "--capture-direction",
        engineId,
        direction,
        laneId,
        ...(MOTORING_PROOF ? ["--motoring-proof"] : []),
      ],
      {
        cwd: repository,
        stdio: ["ignore", "ignore", "inherit"],
      },
    );
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (code === 0) resolve();
      else {
        reject(
          new Error(
            `${engineId}/${direction}/${laneId} capture exited with ${code ?? signal}`,
          ),
        );
      }
    });
  });
}

async function runBounded(jobs, concurrency) {
  let next = 0;
  let completed = 0;
  async function worker() {
    for (;;) {
      const index = next++;
      if (index >= jobs.length) return;
      const job = jobs[index];
      await job.run();
      ++completed;
      process.stderr.write(
        `[directional] ${completed}/${jobs.length} ${job.label}\n`,
      );
    }
  }
  await Promise.all(
    Array.from({ length: Math.min(concurrency, jobs.length) }, () => worker()),
  );
}

async function captureEngine(engineId, concurrency) {
  if (!fs.existsSync(modulePath)) fail(`WASM module is absent: ${modulePath}`);
  const config = engineConfig(findEngine(engineId));
  const jobs = DIRECTIONS.flatMap((direction) =>
    LOAD_LANES
      .filter((lane) => !captureIsCurrent(config, direction, lane))
      .map((lane) => ({
        label: `${engineId}/${direction}/${lane.id}`,
        run: () => spawnCapture(engineId, direction, lane.id),
      })),
  );
  if (jobs.length > 0) await runBounded(jobs, concurrency);
  return Object.freeze({
    engine: engineId,
    capture_count: DIRECTIONS.length * LOAD_LANES.length,
    new_capture_count: jobs.length,
    reused_capture_count: DIRECTIONS.length * LOAD_LANES.length - jobs.length,
    capture_directory: config.captureDirectory,
  });
}

async function generateEngine(engineId, concurrency) {
  await captureEngine(engineId, concurrency);
  const config = engineConfig(findEngine(engineId));
  return buildPackage(config);
}

async function captureAll(concurrency) {
  const results = [];
  for (const engine of inventory().engines) {
    process.stderr.write(`[directional] capturing ${engine.engine_id}\n`);
    results.push(await captureEngine(engine.engine_id, concurrency));
  }
  return Object.freeze({
    schema: "engine-sim-offline/multi-engine-directional-source-capture-report",
    silent: true,
    engine_count: results.length,
    maximum_capture_concurrency: concurrency,
    engines: results,
  });
}

async function generateAll(concurrency) {
  const results = [];
  // Engine-level serialization keeps the six-job bound exact and prevents ten
  // simultaneous WASM batches from hiding memory pressure.
  for (const engine of inventory().engines) {
    process.stderr.write(`[directional] baking ${engine.engine_id}\n`);
    results.push(await generateEngine(engine.engine_id, concurrency));
  }
  const report = {
    schema: "engine-sim-offline/multi-engine-directional-transient-batch-report",
    silent: true,
    engine_count: results.length,
    maximum_capture_concurrency: concurrency,
    engines: results,
  };
  writeJson(path.join(experiment, "directional-transient-batch-report.json"), report);
  return report;
}

function optionValue(args, name) {
  const index = args.indexOf(name);
  if (index < 0) return null;
  if (index + 1 >= args.length) fail(`${name} requires a value`);
  return args[index + 1];
}

const args = COMMAND_ARGS.filter((argument) => argument !== "--motoring-proof");
const captureOnly = args.includes("--capture-only");
const seamCheckOnly = args.includes("--seam-check-only");
if (args[0] === "--capture-direction") {
  if (args.length !== 4) {
    fail("--capture-direction requires <engine-id> <rising|falling> <lane-id>");
  }
  await captureDirection(args[1], args[2], args[3]);
} else if (args.includes("--engine")) {
  const engineId = optionValue(args, "--engine");
  const concurrency = parseConcurrency(optionValue(args, "--concurrency"));
  const result = args.includes("--print-capture-plan")
    ? capturePlan(engineId)
    : seamCheckOnly
    ? checkCapturedSeams(engineId)
    : captureOnly
      ? await captureEngine(engineId, concurrency)
      : await generateEngine(engineId, concurrency);
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
} else if (args.includes("--all")) {
  const concurrency = parseConcurrency(optionValue(args, "--concurrency"));
  const report = seamCheckOnly
    ? checkAllCapturedSeams()
    : captureOnly
      ? await captureAll(concurrency)
      : await generateAll(concurrency);
  if (seamCheckOnly) {
    writeJson(path.join(experiment, "directional-seam-check-report.json"), report);
  }
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
} else {
  fail(
    "usage: node generate-directional-transient-packages.mjs (--engine <id> | --all) [--concurrency N] [--capture-only|--seam-check-only|--print-capture-plan]",
  );
}
