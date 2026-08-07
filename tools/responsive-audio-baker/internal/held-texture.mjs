#!/usr/bin/env node

// Internal exact-held texture stage for the tracked responsive-audio baker.
// The public entry point supplies every path through a bounded environment.

import { createHash } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
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
  compareCodeUnits,
  portableArtifactToken,
  rendererFileIdentity,
  reusablePriorPhaseAlignment,
  uniqueArtifactTokens,
} from "./bake-contract.mjs";

const experiment = import.meta.dirname;
const repository = path.resolve(experiment, "../../..");
const inventoryPath = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_INVENTORY");
const bakeCacheRoot = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_CACHE");
const responsiveOutputRoot = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_OUTPUT");
const args = process.argv.slice(2);

function requiredEnvironmentPath(name) {
  const value = process.env[name];
  if (typeof value !== "string" || value.length === 0 || !path.isAbsolute(value)) {
    throw new Error(`${name} must be an absolute path`);
  }
  return path.normalize(value);
}

function option(name) {
  const index = args.indexOf(name);
  if (index === -1) return null;
  if (index + 1 >= args.length || args[index + 1].startsWith("--")) {
    fail(`${name} requires a value`);
  }
  return args[index + 1];
}

function resolveInventoryPath(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    fail(`${label} path is absent`);
  }
  const resolved = path.isAbsolute(value)
    ? path.normalize(value)
    : path.resolve(repository, value);
  if (
    !path.isAbsolute(value) &&
    !resolved.startsWith(`${repository}${path.sep}`)
  ) {
    fail(`${label} escapes the repository`);
  }
  if (!fs.existsSync(resolved)) fail(`${label} is absent: ${resolved}`);
  return resolved;
}

if (!fs.existsSync(inventoryPath)) {
  fail(`multi-engine inventory is absent: ${inventoryPath}`);
}
const inventory = readJson(inventoryPath);
if (
  inventory?.schema_id !== "engine-sim-offline.multi-engine-bake-inventory.v1" ||
  !Array.isArray(inventory.engines) ||
  inventory.engines.length === 0
) {
  fail("multi-engine inventory has an unsupported shape");
}
const requestedEngineId = option("--engine");
const selectedInventory = requestedEngineId === null && args.includes("--all")
  ? inventory.engines[0]
  : inventory.engines.find((entry) => entry.engine_id === requestedEngineId);
if (selectedInventory === undefined) {
  fail("usage requires --engine <inventory-engine-id> or --all");
}
const audioAsset = selectedInventory.assets?.audio?.[0];
const accessoryAsset = selectedInventory.assets?.accessory_configuration;
if (
  audioAsset?.verified !== true ||
  accessoryAsset?.verified !== true ||
  !Array.isArray(selectedInventory.presentation?.dry_source_route_bus_ids) ||
  selectedInventory.presentation.dry_source_route_bus_ids.length === 0 ||
  !Array.isArray(selectedInventory.rpm_domain?.recommended_rpm_grid) ||
  selectedInventory.rpm_domain.recommended_rpm_grid.length < 2
) {
  fail(`${selectedInventory.engine_id} inventory is incomplete or unverified`);
}
const ENGINE_ID = selectedInventory.engine_id;
const FUEL_ID = selectedInventory.fuel_id;
const AUDIO_ASSET_ID = audioAsset.id;
const ACCESSORY_ASSET_ID = accessoryAsset.reference_id;
const captureDirectory = path.join(bakeCacheRoot, "held", "captures");
const packageDirectory = path.join(bakeCacheRoot, "held", "package");
const reportPath = path.join(packageDirectory, "report.json");
const stagingEngineDirectory = responsiveOutputRoot;
const stagingHeldDirectory = path.join(stagingEngineDirectory, "held");
const modulePath = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_MODULE");
const wasmPath = process.env.ESO_RESPONSIVE_BAKE_WASM === undefined
  ? path.join(path.dirname(modulePath), "engine-sim-offline.wasm")
  : requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_WASM");
const rendererFilePairIdentity = rendererFileIdentity(modulePath, wasmPath);
const enginePath = resolveInventoryPath(
  selectedInventory.engine_path,
  `${ENGINE_ID}.engine_path`,
);
const engineDocument = readJson(enginePath);
const outputCrankshaftId = engineDocument.engine?.output_crankshaft;
const outputCrankshaft = engineDocument.engine?.crankshafts?.find(
  (crankshaft) => crankshaft.id === outputCrankshaftId,
);
if (outputCrankshaft?.tdc_reference_angle === undefined) {
  fail(`${ENGINE_ID} omits the output crankshaft TDC reference angle`);
}
const preferredScenarioPath = selectedInventory.canonical_scenarios
  ?.free_interactive?.path;
const scenarioDirectory = path.join(path.dirname(enginePath), "scenarios");
const availableScenarioPaths = preferredScenarioPath ||
    !fs.existsSync(scenarioDirectory)
  ? []
  : fs
      .readdirSync(scenarioDirectory, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
      .map((entry) => path.join(scenarioDirectory, entry.name))
      .sort();
const crankReferenceScenarioPath = preferredScenarioPath
  ? resolveInventoryPath(
      preferredScenarioPath,
      `${ENGINE_ID}.free_interactive.path`,
    )
  : availableScenarioPaths[0];
if (crankReferenceScenarioPath === undefined) {
  fail(`${ENGINE_ID} has no scenario from which to preserve its exact TDC angle`);
}
const INITIAL_CRANK_ANGLE = readJson(crankReferenceScenarioPath).initial_state
  ?.crank_angle;
if (INITIAL_CRANK_ANGLE === undefined) {
  fail(`${ENGINE_ID} crank-reference scenario omits initial_state.crank_angle`);
}
const activeThrottleController = engineDocument.engine?.throttle_controllers?.find(
  (controller) => controller.id === engineDocument.engine?.throttle_controller,
);
if (activeThrottleController === undefined) {
  fail(`${ENGINE_ID} active throttle controller is absent`);
}
const requiresHeldDynoCapture =
  activeThrottleController.type !== "direct" ||
  engineDocument.engine?.journals?.some(
    (journal) => journal.type === "master_rod",
  );
const CAPTURE_OPERATING_MODE = requiresHeldDynoCapture
  ? "held-dyno-constant-speed"
  : "held-speed";
const impulseResponsePath = resolveInventoryPath(
  audioAsset.path,
  `${ENGINE_ID}.audio.path`,
);
const accessoryPath = resolveInventoryPath(
  accessoryAsset.path,
  `${ENGINE_ID}.accessory.path`,
);
const irSpectrumDumper = requiredEnvironmentPath(
  "ESO_RESPONSIVE_BAKE_IR_DUMPER",
);

const authoredPresentation = engineDocument.presentation;
if (
  !Array.isArray(authoredPresentation?.routes) ||
  !Array.isArray(authoredPresentation?.buses) ||
  !Array.isArray(authoredPresentation?.audition?.buses) ||
  authoredPresentation.audition.buses.length !== 1
) {
  fail(`${ENGINE_ID} presentation is outside the preview baker contract`);
}
const PRESENTATION_ROUTES = Object.freeze(
  selectedInventory.presentation.dry_source_route_bus_ids.map((dryBusId) => {
    if (!dryBusId.endsWith(".dry")) {
      fail(`${ENGINE_ID} dry bus ${dryBusId} lacks the .dry role suffix`);
    }
    const sourceRouteId = dryBusId.slice(0, -".dry".length);
    const authored = authoredPresentation.routes.find(
      (route) => route.route === sourceRouteId,
    );
    if (authored === undefined) {
      fail(`${ENGINE_ID} presentation omits source route ${sourceRouteId}`);
    }
    if (authored.impulse_response !== AUDIO_ASSET_ID) {
      fail(
        `${ENGINE_ID} route ${sourceRouteId} uses unsupported asset ${authored.impulse_response}`,
      );
    }
    if (
      !(authored.impulse_response_gain_linear >= 0) ||
      !(authored.wet_mix_01 >= 0 && authored.wet_mix_01 <= 1)
    ) {
      fail(`${ENGINE_ID} route ${sourceRouteId} has invalid transfer calibration`);
    }
    return Object.freeze({
      busId: dryBusId,
      sourceRouteId,
      impulseResponseAssetId: authored.impulse_response,
      impulseResponseGainLinear: authored.impulse_response_gain_linear,
      wetMix01: authored.wet_mix_01,
    });
  }),
);
const auditionBusId = authoredPresentation.audition.buses[0];
const auditionBus = authoredPresentation.buses.find(
  (bus) => bus.id === auditionBusId,
);
if (
  auditionBus === undefined ||
  !Array.isArray(auditionBus.routes) ||
  auditionBus.routes.length !== PRESENTATION_ROUTES.length
) {
  fail(`${ENGINE_ID} audition bus is absent or does not select every route`);
}
const AUDITION_DRY_BUS_IDS = Object.freeze(
  auditionBus.routes.map((routeId) => `${routeId}.dry`),
);
if (
  new Set(AUDITION_DRY_BUS_IDS).size !== PRESENTATION_ROUTES.length ||
  AUDITION_DRY_BUS_IDS.some(
    (busId) => !PRESENTATION_ROUTES.some((route) => route.busId === busId),
  )
) {
  fail(`${ENGINE_ID} audition route order disagrees with its dry route set`);
}
const CAPTURED_TO_SOURCE_SCALE =
  1 / authoredPresentation.publication_gain_linear;
const MASTER_VOLUME_LINEAR = authoredPresentation.audition.volume_linear;
if (
  !(authoredPresentation.publication_gain_linear > 0) ||
  !Number.isFinite(CAPTURED_TO_SOURCE_SCALE) ||
  !(MASTER_VOLUME_LINEAR > 0) ||
  auditionBus.gain_linear !== 1
) {
  fail(`${ENGINE_ID} publication or mastering calibration is invalid`);
}

const RPM_ANCHORS = Object.freeze(
  selectedInventory.rpm_domain.recommended_rpm_grid.slice(),
);
const RESPONSIVE_AUDIO_OUTER_DOMAIN = Object.freeze({
  minimumRpm:
    selectedInventory.rpm_domain.responsive_audio_outer_domain?.minimum_rpm,
  maximumRpm:
    selectedInventory.rpm_domain.responsive_audio_outer_domain?.maximum_rpm,
});
if (
  !(RESPONSIVE_AUDIO_OUTER_DOMAIN.minimumRpm > 0) ||
  !(RESPONSIVE_AUDIO_OUTER_DOMAIN.maximumRpm >
    RESPONSIVE_AUDIO_OUTER_DOMAIN.minimumRpm) ||
  RESPONSIVE_AUDIO_OUTER_DOMAIN.minimumRpm > RPM_ANCHORS[0] ||
  RESPONSIVE_AUDIO_OUTER_DOMAIN.maximumRpm < RPM_ANCHORS.at(-1)
) {
  fail(`${ENGINE_ID} has an invalid responsive-audio outer RPM domain`);
}
const LOAD_LANES = Object.freeze([
  Object.freeze({ id: "coast", throttle01: 0 }),
  Object.freeze({ id: "mid", throttle01: 0.2 }),
  Object.freeze({ id: "power", throttle01: 1 }),
]);
const routeIds = selectedInventory.presentation.dry_source_route_bus_ids;
const routeTokens = uniqueArtifactTokens(
  routeIds,
  "route",
  `${ENGINE_ID} dry route IDs`,
);
const ROUTES = Object.freeze(routeIds.map((id, index) =>
  Object.freeze({ id, token: routeTokens[index] })
));
const SAMPLE_RATE = ESO_CANONICAL_SAMPLE_RATE;
const PHYSICS_RATE = 10_000;
const DURATION_ALIGNMENT_RATE = 50;
const SAMPLES_PER_CYCLE = 4_096;
const CAPTURED_CYCLE_COUNT = 48;
const CYCLE_REVOLUTIONS = 2;
const CAPTURE_PREPARATION_FLOOR_SECONDS =
  selectedInventory.rpm_domain.held_capture_preparation_floor_seconds;
const CAPTURE_PREPARATION_EXTENSION_BELOW_RPM =
  selectedInventory.rpm_domain.held_capture_extend_preparation_below_rpm;
if (
  !Number.isFinite(CAPTURE_PREPARATION_FLOOR_SECONDS) ||
  CAPTURE_PREPARATION_FLOOR_SECONDS < 3 ||
  CAPTURE_PREPARATION_FLOOR_SECONDS * DURATION_ALIGNMENT_RATE !==
    Math.round(
      CAPTURE_PREPARATION_FLOOR_SECONDS * DURATION_ALIGNMENT_RATE,
    ) ||
  !Number.isFinite(CAPTURE_PREPARATION_EXTENSION_BELOW_RPM) ||
  CAPTURE_PREPARATION_EXTENSION_BELOW_RPM <= 0
) {
  fail(`${ENGINE_ID} has an invalid held capture preparation floor`);
}
// Existing accepted cells retain their exact preparation horizon and capture
// identity. Only a newly added slower anchor lengthens its own fixed horizon
// enough for the executor's conservative 16-complete-cycle requirement.
function preparationDurationSeconds(rpm) {
  if (rpm >= CAPTURE_PREPARATION_EXTENSION_BELOW_RPM) {
    return CAPTURE_PREPARATION_FLOOR_SECONDS;
  }
  return Math.max(
      CAPTURE_PREPARATION_FLOOR_SECONDS,
      Math.ceil((18 * 120 * DURATION_ALIGNMENT_RATE) / rpm) /
        DURATION_ALIGNMENT_RATE,
    );
}
const GUARD_CYCLE_COUNT_BEFORE = 4;
const GUARD_CYCLE_COUNT_AFTER = 4;
const DURATION_MARGIN_SECONDS = 0.5;
const RESIDUAL_TAPER_FRACTION_PER_EDGE = 1 / 16;
const MAXIMUM_CAPTURE_CONCURRENCY = Number(
  process.env.ESO_HELD_CAPTURE_CONCURRENCY ?? 3,
);
const MAXIMUM_ENGINE_CONCURRENCY = Number(
  option("--concurrency") ?? process.env.ESO_HELD_ENGINE_CONCURRENCY ?? 2,
);
for (const [label, value] of [
  ["held capture concurrency", MAXIMUM_CAPTURE_CONCURRENCY],
  ["held engine concurrency", MAXIMUM_ENGINE_CONCURRENCY],
]) {
  if (!Number.isSafeInteger(value) || value < 1 || value > 32) {
    fail(`${label} must be an integer in [1, 32]`);
  }
}
const CAPTURE_METHOD_ID =
  "exact-held-rpm-48-cycle-20ms-exact-sum-horizon-v8";
const DECOMPOSITION_METHOD_ID =
  "cyclic-mean-plus-boundary-zero-smoothstep-residual-bank-v1";
const PUBLIC_SEED = "12648430";

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
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}

function spawnSyncChecked(executable, childArguments) {
  const result = spawnSync(executable, childArguments, {
    cwd: repository,
    stdio: ["ignore", "ignore", "pipe"],
    encoding: "utf8",
  });
  if (result.error !== undefined) {
    fail(`failed to start ${executable}`, { cause: result.error });
  }
  if (result.status !== 0) {
    fail(
      `${path.basename(executable)} exited with ${result.status}: ${result.stderr.trim()}`,
    );
  }
  return result.status;
}

function float32LeBytes(samples) {
  return Buffer.from(samples.buffer, samples.byteOffset, samples.byteLength);
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

function smoothstep(amount) {
  return amount * amount * (3 - 2 * amount);
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
    unwrappedCrankRevolutions: telemetry.thetaRad / (2 * Math.PI),
    stateMask: stateMask(telemetry),
  });
}

function captureIdentity(rpm) {
  if (!RPM_ANCHORS.includes(rpm)) {
    fail(`${ENGINE_ID} capture identity requires an authored RPM anchor`);
  }
  const preparationDurationSecondsForState = preparationDurationSeconds(rpm);
  const inputs = {
    engine_id: ENGINE_ID,
    fidelity_label: "10-khz-live-preview-not-canonical-offline-bake",
    ...(CAPTURE_OPERATING_MODE === "held-speed"
      ? {}
      : { capture_operating_mode: CAPTURE_OPERATING_MODE }),
    capture_method: CAPTURE_METHOD_ID,
    decomposition_method: DECOMPOSITION_METHOD_ID,
    load_lanes: LOAD_LANES,
    routes: ROUTES,
    sample_rate_hz: SAMPLE_RATE,
    physics_rate_hz: PHYSICS_RATE,
    duration_alignment_rate_hz: DURATION_ALIGNMENT_RATE,
    samples_per_cycle: SAMPLES_PER_CYCLE,
    captured_cycle_count: CAPTURED_CYCLE_COUNT,
    preparation_duration_seconds: preparationDurationSecondsForState,
    guard_cycle_count_before: GUARD_CYCLE_COUNT_BEFORE,
    guard_cycle_count_after: GUARD_CYCLE_COUNT_AFTER,
    duration_margin_seconds: DURATION_MARGIN_SECONDS,
    duration_quantization:
      "ceil-to-first-20ms-block-count-whose-binary-start-plus-duration-equals-total",
    residual_taper_fraction_per_edge: RESIDUAL_TAPER_FRACTION_PER_EDGE,
    public_seed: PUBLIC_SEED,
    engine_input_sha256: sha256(fs.readFileSync(enginePath)),
    wasm_loader_sha256: rendererFilePairIdentity.loader_sha256,
    wasm_binary_sha256: rendererFilePairIdentity.wasm_sha256,
    impulse_response_sha256: sha256(fs.readFileSync(impulseResponsePath)),
    accessory_configuration_sha256: sha256(fs.readFileSync(accessoryPath)),
  };
  return Object.freeze({
    ...inputs,
    sha256: sha256(Buffer.from(JSON.stringify(inputs))),
  });
}

function gridCaptureIdentity() {
  const inputs = {
    engine_id: ENGINE_ID,
    capture_method: CAPTURE_METHOD_ID,
    decomposition_method: DECOMPOSITION_METHOD_ID,
    state_capture_identities: RPM_ANCHORS.map((rpm) => ({
      rpm,
      sha256: captureIdentity(rpm).sha256,
    })),
  };
  return Object.freeze({
    ...inputs,
    sha256: sha256(Buffer.from(JSON.stringify(inputs))),
  });
}

function scenario(rpm, lane) {
  const preparationDurationSecondsForState = preparationDurationSeconds(rpm);
  const guardedCycleCount =
    CAPTURED_CYCLE_COUNT +
    GUARD_CYCLE_COUNT_BEFORE +
    GUARD_CYCLE_COUNT_AFTER;
  const requestedAudibleDurationSeconds =
    (guardedCycleCount * 120) / rpm + DURATION_MARGIN_SECONDS;
  let audibleDurationQuanta = Math.ceil(
    requestedAudibleDurationSeconds * DURATION_ALIGNMENT_RATE,
  );
  const preparationQuanta =
    preparationDurationSecondsForState * DURATION_ALIGNMENT_RATE;
  while (
    preparationDurationSecondsForState +
      audibleDurationQuanta / DURATION_ALIGNMENT_RATE !==
    (preparationQuanta + audibleDurationQuanta) / DURATION_ALIGNMENT_RATE
  ) {
    ++audibleDurationQuanta;
  }
  const audibleDurationSeconds =
    audibleDurationQuanta / DURATION_ALIGNMENT_RATE;
  const totalDurationQuanta = preparationQuanta + audibleDurationQuanta;
  const totalDurationSeconds =
    totalDurationQuanta / DURATION_ALIGNMENT_RATE;
  return {
    schema: "engine-sim-offline/scenario",
    id: `${ENGINE_ID}-held-texture-live-preview-${rpm}rpm-${lane.id}`,
    engine: ENGINE_ID,
    fuel: FUEL_ID,
    ambient: {
      pressure: { value: 101325, unit: "Pa" },
      temperature: { value: 298.15, unit: "K" },
      relative_humidity_01: 0,
    },
    initial_thermal_state: {
      gas_temperature: { value: 298.15, unit: "K" },
      wall_temperature: { value: 363.15, unit: "K" },
      coolant_temperature: { value: 363.15, unit: "K" },
      oil_temperature: { value: 363.15, unit: "K" },
    },
    crankcase: {
      pressure: { value: 101325, unit: "Pa" },
      temperature: { value: 298.15, unit: "K" },
    },
    initial_state: {
      engine_speed: { value: rpm, unit: "rpm" },
      crank_angle: INITIAL_CRANK_ANGLE,
      ignition_enabled: true,
      fuel_enabled: true,
      starter_enabled: false,
      dyno_enabled: true,
      limiter_enabled: false,
    },
    preparation: {
      type: "fixed_horizon",
      preparation_duration: {
        value: preparationDurationSecondsForState,
        unit: "s",
      },
      trailing_complete_cycle_count: 16,
    },
    mode: CAPTURE_OPERATING_MODE === "held-speed"
      ? {
          type: "held_speed",
          target_engine_speed: { value: rpm, unit: "rpm" },
          throttle_01: {
            interpolation: "right_continuous_hold",
            points: [{ time: { value: 0, unit: "s" }, value: lane.throttle01 }],
          },
        }
      : {
          type: "held_dyno",
          target_engine_speed: {
            value_dimension: "angular_speed",
            interpolation: "linear",
            points: [
              { time: { value: 0, unit: "s" }, value: { value: rpm, unit: "rpm" } },
              {
                time: { value: totalDurationSeconds, unit: "s" },
                value: { value: rpm, unit: "rpm" },
              },
            ],
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
    total_duration: { value: totalDurationSeconds, unit: "s" },
    audible_start: {
      value: preparationDurationSecondsForState,
      unit: "s",
    },
    audible_duration: { value: audibleDurationSeconds, unit: "s" },
    public_seed: PUBLIC_SEED,
    output: {
      buses: ["master-engine-raw", "master-engine-audition"],
      telemetry_channels: [],
    },
  };
}

async function captureRoute(client, sourceScenario, route) {
  const engineBytes = fs.readFileSync(enginePath);
  let program = null;
  try {
    program = client.compile(
      engineBytes.toString("utf8"),
      `${JSON.stringify(sourceScenario)}\n`,
      [
        {
          kind: "audio",
          id: AUDIO_ASSET_ID,
          bytes: fs.readFileSync(impulseResponsePath),
        },
        {
          kind: "accessory-configuration",
          id: ACCESSORY_ASSET_ID,
          bytes: fs.readFileSync(accessoryPath),
        },
      ],
      SessionExecutionKind.finiteScenario,
    );
    const buses = program.session.buses.filter((bus) => bus.id === route.id);
    if (buses.length !== 1) {
      fail(`capture requires one ${route.id} bus; got ${buses.length}`);
    }
    const chunks = [];
    const endpoints = [];
    let audibleFirstFrame = null;
    let sampleCount = 0;
    for (;;) {
      const result = program.session.processBlock(buses[0].index);
      if (result.process.kindCode === ProcessKind.completed) break;
      if (result.telemetry.length !== 1) {
        fail(`${route.id} capture block omitted telemetry`);
      }
      const firstFrame = Number(BigInt(result.process.firstDeliveryFrame));
      endpoints.push(
        endpoint(
          result.telemetry[0],
          firstFrame + result.process.deliveryFrameCount,
        ),
      );
      if (result.audible) {
        if (audibleFirstFrame === null) audibleFirstFrame = firstFrame;
        const samples = result.samples.slice();
        chunks.push(samples);
        sampleCount += samples.length;
      }
    }
    if (audibleFirstFrame === null || sampleCount === 0) {
      fail(`${route.id} capture produced no audible PCM`);
    }
    return Object.freeze({
      pcm: concatenate(chunks, sampleCount),
      endpoints,
      audibleFirstFrame,
      engineProvenanceSha256: program.engineProvenanceSha256,
      rendererSourceSha256: program.rendererSourceSha256,
    });
  } finally {
    program?.dispose();
  }
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
    if (knots[middle].unwrappedCrankRevolutions <= revolutions) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  const left = knots[lower];
  const right = knots[upper];
  const amount =
    (revolutions - left.unwrappedCrankRevolutions) /
    (right.unwrappedCrankRevolutions - left.unwrappedCrankRevolutions);
  return left.deliveryFrame + (right.deliveryFrame - left.deliveryFrame) * amount;
}

function sampleAtRevolutions(capture, revolutions) {
  const deliveryFrame = frameAtRevolutions(capture.endpoints, revolutions);
  const position = deliveryFrame - capture.audibleFirstFrame;
  const left = Math.floor(position);
  if (left < 0 || left + 1 >= capture.pcm.length) {
    fail(`phase sample at ${revolutions} revolutions is outside audible PCM`);
  }
  return (
    capture.pcm[left] +
    (capture.pcm[left + 1] - capture.pcm[left]) * (position - left)
  );
}

function chooseCapturedInterval(capture) {
  const firstAudible = capture.endpoints.find(
    (value) => value.deliveryFrame >= capture.audibleFirstFrame,
  );
  if (firstAudible === undefined) fail("capture has no audible telemetry endpoint");
  const start =
    Math.ceil(
      (firstAudible.unwrappedCrankRevolutions +
        GUARD_CYCLE_COUNT_BEFORE * CYCLE_REVOLUTIONS) /
        CYCLE_REVOLUTIONS,
    ) * CYCLE_REVOLUTIONS;
  const end = start + CAPTURED_CYCLE_COUNT * CYCLE_REVOLUTIONS;
  const requiredTail = end + GUARD_CYCLE_COUNT_AFTER * CYCLE_REVOLUTIONS;
  if (requiredTail > capture.endpoints.at(-1).unwrappedCrankRevolutions) {
    fail(
      `capture ended at ${capture.endpoints.at(-1).unwrappedCrankRevolutions} revolutions; required ${requiredTail}`,
    );
  }
  return Object.freeze({ start, end });
}

function phaseNormalize(capture, interval) {
  const result = new Float32Array(
    CAPTURED_CYCLE_COUNT * SAMPLES_PER_CYCLE,
  );
  for (let index = 0; index < result.length; ++index) {
    result[index] = sampleAtRevolutions(
      capture,
      interval.start + (index * CYCLE_REVOLUTIONS) / SAMPLES_PER_CYCLE,
    );
  }
  return result;
}

function signalRms(samples) {
  let squareSum = 0;
  for (const sample of samples) squareSum += sample * sample;
  return Math.sqrt(squareSum / samples.length);
}

function errorRms(reference, candidate) {
  if (reference.length !== candidate.length) fail("metric vectors differ in size");
  let squareSum = 0;
  for (let index = 0; index < reference.length; ++index) {
    const error = reference[index] - candidate[index];
    squareSum += error * error;
  }
  return Math.sqrt(squareSum / reference.length);
}

function derivativeRms(samples) {
  let squareSum = 0;
  for (let index = 1; index < samples.length; ++index) {
    const delta = samples[index] - samples[index - 1];
    squareSum += delta * delta;
  }
  return Math.sqrt(squareSum / (samples.length - 1));
}

function decomposeTexture(source) {
  if (source.length !== CAPTURED_CYCLE_COUNT * SAMPLES_PER_CYCLE) {
    fail("held texture source has an unexpected phase length");
  }
  const rawMean = new Float64Array(SAMPLES_PER_CYCLE);
  for (let cycle = 0; cycle < CAPTURED_CYCLE_COUNT; ++cycle) {
    const cycleOffset = cycle * SAMPLES_PER_CYCLE;
    for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
      rawMean[phase] += source[cycleOffset + phase];
    }
  }
  for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
    rawMean[phase] /= CAPTURED_CYCLE_COUNT;
  }

  const targetBoundaryDelta =
    0.5 *
    ((rawMean[1] - rawMean[0]) +
      (rawMean.at(-1) - rawMean.at(-2)));
  const closureCorrection =
    rawMean[0] - rawMean.at(-1) - targetBoundaryDelta;
  const mean = new Float32Array(SAMPLES_PER_CYCLE);
  for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
    mean[phase] =
      rawMean[phase] +
      closureCorrection * (phase / (SAMPLES_PER_CYCLE - 1));
  }

  const taperFrames = Math.round(
    SAMPLES_PER_CYCLE * RESIDUAL_TAPER_FRACTION_PER_EDGE,
  );
  const residuals = new Float32Array(source.length);
  const reconstructed = new Float32Array(source.length);
  const rawResidual = new Float32Array(source.length);
  let maximumResidualBoundaryMagnitude = 0;
  for (let cycle = 0; cycle < CAPTURED_CYCLE_COUNT; ++cycle) {
    const cycleOffset = cycle * SAMPLES_PER_CYCLE;
    for (let phase = 0; phase < SAMPLES_PER_CYCLE; ++phase) {
      let taper = 1;
      if (phase < taperFrames) {
        taper = smoothstep(phase / (taperFrames - 1));
      } else if (phase >= SAMPLES_PER_CYCLE - taperFrames) {
        taper = smoothstep(
          (SAMPLES_PER_CYCLE - 1 - phase) / (taperFrames - 1),
        );
      }
      const index = cycleOffset + phase;
      rawResidual[index] = source[index] - mean[phase];
      residuals[index] = rawResidual[index] * taper;
      reconstructed[index] = mean[phase] + residuals[index];
    }
    maximumResidualBoundaryMagnitude = Math.max(
      maximumResidualBoundaryMagnitude,
      Math.abs(residuals[cycleOffset]),
      Math.abs(residuals[cycleOffset + SAMPLES_PER_CYCLE - 1]),
    );
  }

  const sourceRms = signalRms(source);
  const meanDerivativeRms = derivativeRms(mean);
  return Object.freeze({
    mean,
    residuals,
    metrics: Object.freeze({
      source_rms: sourceRms,
      mean_rms: signalRms(mean),
      raw_residual_rms_over_source_rms:
        signalRms(rawResidual) / Math.max(sourceRms, 1e-30),
      tapered_residual_rms_over_source_rms:
        signalRms(residuals) / Math.max(sourceRms, 1e-30),
      reconstruction_error_rms_over_source_rms:
        errorRms(source, reconstructed) / Math.max(sourceRms, 1e-30),
      mean_seam_absolute_delta: Math.abs(mean[0] - mean.at(-1)),
      mean_seam_over_mean_derivative_rms:
        Math.abs(mean[0] - mean.at(-1)) /
        Math.max(meanDerivativeRms, 1e-30),
      mean_closure_correction_absolute: Math.abs(closureCorrection),
      mean_closure_correction_over_source_rms:
        Math.abs(closureCorrection) / Math.max(sourceRms, 1e-30),
      maximum_residual_boundary_magnitude:
        maximumResidualBoundaryMagnitude,
      residual_taper_frames_per_edge: taperFrames,
    }),
  });
}

function intervalTelemetry(capture, interval, targetRpm) {
  const begin = frameAtRevolutions(capture.endpoints, interval.start);
  const end = frameAtRevolutions(capture.endpoints, interval.end);
  const selected = capture.endpoints.filter(
    (value) => value.deliveryFrame >= begin && value.deliveryFrame <= end,
  );
  if (selected.length === 0) fail("captured interval has no telemetry");
  let mapSum = 0;
  let rpmErrorSquareSum = 0;
  let maximumRpmError = 0;
  for (const value of selected) {
    mapSum += value.manifoldPressurePaAbs;
    const error = value.rpm - targetRpm;
    rpmErrorSquareSum += error * error;
    maximumRpmError = Math.max(maximumRpmError, Math.abs(error));
  }
  return Object.freeze({
    mean_manifold_pressure_pa_abs: mapSum / selected.length,
    rpm_error_rms: Math.sqrt(rpmErrorSquareSum / selected.length),
    maximum_absolute_rpm_error: maximumRpmError,
    telemetry_endpoint_count: selected.length,
  });
}

function stateId(rpm, lane) {
  return `${rpm}rpm-${lane.id}`;
}

function stateArtifactStem(rpm, lane) {
  const index = RPM_ANCHORS.indexOf(rpm);
  if (index === -1) fail(`${rpm} RPM is not an authored anchor`);
  return `r${String(index).padStart(4, "0")}-${lane.id}`;
}

function metadataPath(rpm, lane) {
  return path.join(captureDirectory, `${stateId(rpm, lane)}.json`);
}

function captureRoutesAreIdentical(left, right) {
  if (left.routes.length !== right.routes.length) return false;
  for (const leftRoute of left.routes) {
    const rightRoute = right.routes.find(
      (candidate) => candidate.route_id === leftRoute.route_id,
    );
    if (
      rightRoute === undefined ||
      leftRoute.telemetry.mean_manifold_pressure_pa_abs !==
        rightRoute.telemetry.mean_manifold_pressure_pa_abs ||
      leftRoute.source_cycle_begin_revolutions !==
        rightRoute.source_cycle_begin_revolutions ||
      leftRoute.source_cycle_end_revolutions !==
        rightRoute.source_cycle_end_revolutions ||
      leftRoute.mean_sha256 !== rightRoute.mean_sha256 ||
      leftRoute.residual_sha256 !== rightRoute.residual_sha256
    ) {
      return false;
    }
  }
  return true;
}

function coalesceDuplicateLoadCaptures(captures) {
  const result = [];
  for (const capture of captures) {
    const map = capture.routes[0].telemetry.mean_manifold_pressure_pa_abs;
    const existingIndex = result.findIndex(
      (candidate) =>
        candidate.rpm === capture.rpm &&
        candidate.routes[0].telemetry.mean_manifold_pressure_pa_abs === map,
    );
    if (existingIndex === -1) {
      result.push({
        ...capture,
        coalesced_authored_lanes: [capture.lane],
        coalesced_capture_throttles_01: [capture.throttle_01],
      });
      continue;
    }
    const existing = result[existingIndex];
    if (!captureRoutesAreIdentical(existing, capture)) {
      fail(
        `${capture.rpm} RPM has distinct held audio at the same ` +
          "manifold-pressure coordinate; measured MAP is insufficient for " +
          "this engine",
      );
    }
    result[existingIndex] = {
      ...existing,
      coalesced_authored_lanes: [
        ...existing.coalesced_authored_lanes,
        capture.lane,
      ],
      coalesced_capture_throttles_01: [
        ...existing.coalesced_capture_throttles_01,
        capture.throttle_01,
      ],
    };
  }
  return result;
}

async function captureState(rpm, lane) {
  const startedAt = performance.now();
  fs.mkdirSync(captureDirectory, { recursive: true });
  const identity = captureIdentity(rpm);
  const sourceScenario = scenario(rpm, lane);
  const scenarioBytes = Buffer.from(`${JSON.stringify(sourceScenario)}\n`);
  const client = await EngineSimCapiClient.create(pathToFileURL(modulePath));
  const routeResults = [];
  try {
    for (const route of ROUTES) {
      const captured = await captureRoute(client, sourceScenario, route);
      const interval = chooseCapturedInterval(captured);
      const phaseSource = phaseNormalize(captured, interval);
      const texture = decomposeTexture(phaseSource);
      const meanFilename =
        `${stateArtifactStem(rpm, lane)}.${route.token}.mean.f32le`;
      const residualFilename =
        `${stateArtifactStem(rpm, lane)}.${route.token}.residuals.f32le`;
      const meanBytes = float32LeBytes(texture.mean);
      const residualBytes = float32LeBytes(texture.residuals);
      fs.writeFileSync(path.join(captureDirectory, meanFilename), meanBytes);
      fs.writeFileSync(
        path.join(captureDirectory, residualFilename),
        residualBytes,
      );
      routeResults.push({
        route_id: route.id,
        route_slug: route.token,
        source_cycle_begin_revolutions: interval.start,
        source_cycle_end_revolutions: interval.end,
        mean_filename: meanFilename,
        mean_byte_count: meanBytes.byteLength,
        mean_sha256: sha256(meanBytes),
        residual_filename: residualFilename,
        residual_byte_count: residualBytes.byteLength,
        residual_sha256: sha256(residualBytes),
        telemetry: intervalTelemetry(captured, interval, rpm),
        metrics: texture.metrics,
        engine_provenance_sha256: captured.engineProvenanceSha256,
        renderer_source_sha256: captured.rendererSourceSha256,
      });
    }
  } finally {
    client.dispose();
  }
  const result = {
    schema: "engine-sim-offline/held-phase-texture-capture",
    id: stateId(rpm, lane),
    rpm,
    lane: lane.id,
    throttle_01: lane.throttle01,
    capture_method: CAPTURE_METHOD_ID,
    decomposition_method: DECOMPOSITION_METHOD_ID,
    capture_identity_sha256: identity.sha256,
    scenario_sha256: sha256(scenarioBytes),
    scenario_duration_seconds: sourceScenario.total_duration.value,
    audible_duration_seconds: sourceScenario.audible_duration.value,
    wall_time_seconds: (performance.now() - startedAt) / 1000,
    routes: routeResults,
  };
  writeJson(metadataPath(rpm, lane), result);
  process.stdout.write(`${JSON.stringify({
    id: result.id,
    wallTimeSeconds: result.wall_time_seconds,
  })}\n`);
}

function cachedCaptureIsCurrent(rpm, lane, identity) {
  const file = metadataPath(rpm, lane);
  if (!fs.existsSync(file)) return false;
  try {
    const metadata = readJson(file);
    if (metadata.capture_identity_sha256 !== identity.sha256) return false;
    if (!Array.isArray(metadata.routes) || metadata.routes.length !== ROUTES.length) {
      return false;
    }
    for (const route of metadata.routes) {
      for (const [filenameKey, byteCountKey, hashKey] of [
        ["mean_filename", "mean_byte_count", "mean_sha256"],
        ["residual_filename", "residual_byte_count", "residual_sha256"],
      ]) {
        const payload = path.join(captureDirectory, route[filenameKey]);
        if (!fs.existsSync(payload)) return false;
        const bytes = fs.readFileSync(payload);
        if (
          bytes.byteLength !== route[byteCountKey] ||
          sha256(bytes) !== route[hashKey]
        ) {
          return false;
        }
      }
    }
    return true;
  } catch {
    return false;
  }
}

function captureArtifactFingerprint(rpm, lane) {
  const metadataFile = metadataPath(rpm, lane);
  const metadataBytes = fs.readFileSync(metadataFile);
  const metadata = JSON.parse(metadataBytes);
  const payloads = metadata.routes.flatMap((route) =>
    [route.mean_filename, route.residual_filename].map((filename) => {
      const bytes = fs.readFileSync(path.join(captureDirectory, filename));
      return Object.freeze({
        filename,
        byte_count: bytes.byteLength,
        sha256: sha256(bytes),
      });
    })
  );
  const value = {
    id: metadata.id,
    metadata_byte_count: metadataBytes.byteLength,
    metadata_sha256: sha256(metadataBytes),
    payloads,
  };
  return Object.freeze({
    ...value,
    aggregate_sha256: sha256(Buffer.from(JSON.stringify(value))),
  });
}

function runCaptureChild(rpm, lane) {
  return new Promise((resolve, reject) => {
    const child = spawn(
      process.execPath,
      [
        fileURLToPath(import.meta.url),
        "--engine",
        ENGINE_ID,
        "--capture-state",
        String(rpm),
        lane.id,
      ],
      {
        cwd: repository,
        stdio: ["ignore", "ignore", "inherit"],
      },
    );
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(`${rpm} RPM ${lane.id} capture exited with ${code ?? signal}`));
    });
  });
}

async function runCaptureQueue(jobs) {
  let next = 0;
  let completed = 0;
  const startedAt = performance.now();
  const worker = async () => {
    for (;;) {
      const index = next++;
      if (index >= jobs.length) return;
      const job = jobs[index];
      await runCaptureChild(job.rpm, job.lane);
      ++completed;
      process.stderr.write(
        `held texture capture ${completed}/${jobs.length}: ${job.rpm} RPM ${job.lane.id}\n`,
      );
    }
  };
  await Promise.all(
    Array.from(
      { length: Math.min(MAXIMUM_CAPTURE_CONCURRENCY, jobs.length) },
      worker,
    ),
  );
  return (performance.now() - startedAt) / 1000;
}

function runEngineChild(engineId) {
  return new Promise((resolve, reject) => {
    const childArguments = [
      fileURLToPath(import.meta.url),
      "--engine",
      engineId,
    ];
    if (args.includes("--held-only")) childArguments.push("--held-only");
    const child = spawn(
      process.execPath,
      childArguments,
      {
        cwd: repository,
        stdio: ["ignore", "ignore", "inherit"],
        env: process.env,
      },
    );
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (code === 0) resolve();
      else {
        reject(
          new Error(`${engineId} held bake exited with ${code ?? signal}`),
        );
      }
    });
  });
}

async function runAllEngines() {
  if (
    !Number.isSafeInteger(MAXIMUM_ENGINE_CONCURRENCY) ||
    MAXIMUM_ENGINE_CONCURRENCY <= 0 ||
    MAXIMUM_ENGINE_CONCURRENCY > 4
  ) {
    fail("--concurrency must be an integer from 1 through 4");
  }
  let next = 0;
  let completed = 0;
  const engines = inventory.engines.map((entry) => entry.engine_id);
  const worker = async () => {
    for (;;) {
      const index = next++;
      if (index >= engines.length) return;
      const engineId = engines[index];
      await runEngineChild(engineId);
      ++completed;
      process.stderr.write(
        `held texture engine ${completed}/${engines.length}: ${engineId}\n`,
      );
    }
  };
  await Promise.all(
    Array.from(
      { length: Math.min(MAXIMUM_ENGINE_CONCURRENCY, engines.length) },
      worker,
    ),
  );
}

function quantile(values, amount) {
  const ordered = values.slice().sort((left, right) => left - right);
  return ordered[Math.floor((ordered.length - 1) * amount)];
}

function summary(values) {
  return {
    minimum: Math.min(...values),
    p50: quantile(values, 0.5),
    p90: quantile(values, 0.9),
    maximum: Math.max(...values),
  };
}

function readFloat32Le(file, expectedSampleCount) {
  const bytes = fs.readFileSync(file);
  if (bytes.byteLength !== expectedSampleCount * Float32Array.BYTES_PER_ELEMENT) {
    fail(`${file} has an unexpected Float32 sample count`);
  }
  return new Float32Array(
    bytes.buffer,
    bytes.byteOffset,
    expectedSampleCount,
  ).slice();
}

function fft(real, imaginary, inverse = false) {
  const count = real.length;
  if (
    imaginary.length !== count ||
    count < 2 ||
    (count & (count - 1)) !== 0
  ) {
    fail("phase-alignment FFT requires equal power-of-two vectors");
  }
  for (let index = 1, reversed = 0; index < count; ++index) {
    let bit = count >> 1;
    for (; (reversed & bit) !== 0; bit >>= 1) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) {
      const realSwap = real[index];
      real[index] = real[reversed];
      real[reversed] = realSwap;
      const imaginarySwap = imaginary[index];
      imaginary[index] = imaginary[reversed];
      imaginary[reversed] = imaginarySwap;
    }
  }
  for (let width = 2; width <= count; width <<= 1) {
    const angle = (inverse ? 2 : -2) * Math.PI / width;
    const rootReal = Math.cos(angle);
    const rootImaginary = Math.sin(angle);
    for (let begin = 0; begin < count; begin += width) {
      let twiddleReal = 1;
      let twiddleImaginary = 0;
      for (let offset = 0; offset < width / 2; ++offset) {
        const even = begin + offset;
        const odd = even + width / 2;
        const oddReal =
          real[odd] * twiddleReal - imaginary[odd] * twiddleImaginary;
        const oddImaginary =
          real[odd] * twiddleImaginary + imaginary[odd] * twiddleReal;
        const evenReal = real[even];
        const evenImaginary = imaginary[even];
        real[even] = evenReal + oddReal;
        imaginary[even] = evenImaginary + oddImaginary;
        real[odd] = evenReal - oddReal;
        imaginary[odd] = evenImaginary - oddImaginary;
        const nextTwiddleReal =
          twiddleReal * rootReal - twiddleImaginary * rootImaginary;
        twiddleImaginary =
          twiddleReal * rootImaginary + twiddleImaginary * rootReal;
        twiddleReal = nextTwiddleReal;
      }
    }
  }
  if (inverse) {
    for (let index = 0; index < count; ++index) {
      real[index] /= count;
      imaginary[index] /= count;
    }
  }
}

function circularCorrelation(left, right) {
  if (left.length !== right.length) fail("correlation vectors differ in size");
  const count = left.length;
  const leftReal = new Float64Array(count);
  const leftImaginary = new Float64Array(count);
  const rightReal = new Float64Array(count);
  const rightImaginary = new Float64Array(count);
  let leftMean = 0;
  let rightMean = 0;
  for (let index = 0; index < count; ++index) {
    leftMean += left[index];
    rightMean += right[index];
  }
  leftMean /= count;
  rightMean /= count;
  let leftEnergy = 0;
  let rightEnergy = 0;
  for (let index = 0; index < count; ++index) {
    leftReal[index] = left[index] - leftMean;
    rightReal[index] = right[index] - rightMean;
    leftEnergy += leftReal[index] * leftReal[index];
    rightEnergy += rightReal[index] * rightReal[index];
  }
  fft(leftReal, leftImaginary);
  fft(rightReal, rightImaginary);
  for (let index = 0; index < count; ++index) {
    // conj(left) * right: inverse bin k is sum_n left[n] right[n+k].
    const real =
      leftReal[index] * rightReal[index] +
      leftImaginary[index] * rightImaginary[index];
    const imaginary =
      leftReal[index] * rightImaginary[index] -
      leftImaginary[index] * rightReal[index];
    leftReal[index] = real;
    leftImaginary[index] = imaginary;
  }
  fft(leftReal, leftImaginary, true);
  const normalization = Math.sqrt(leftEnergy * rightEnergy);
  if (!(normalization > 0)) fail("phase-alignment cell has zero correlation energy");
  for (let index = 0; index < count; ++index) {
    leftReal[index] /= normalization;
  }
  return leftReal;
}

function signedCircularIndex(index, count) {
  let result = index;
  while (result >= count / 2) result -= count;
  while (result < -count / 2) result += count;
  return result;
}

function adjacentAlignment(left, right, maximumShiftSamples) {
  const correlation = circularCorrelation(left, right);
  const count = correlation.length;
  let bestIndex = 0;
  let bestValue = Number.NEGATIVE_INFINITY;
  let unconstrainedIndex = 0;
  let unconstrainedValue = Number.NEGATIVE_INFINITY;
  for (let index = 0; index < count; ++index) {
    const value = correlation[index];
    if (value > unconstrainedValue) {
      unconstrainedValue = value;
      unconstrainedIndex = index;
    }
    const signed = signedCircularIndex(index, count);
    if (Math.abs(signed) <= maximumShiftSamples && value > bestValue) {
      bestValue = value;
      bestIndex = index;
    }
  }
  const before = correlation[(bestIndex - 1 + count) % count];
  const center = correlation[bestIndex];
  const after = correlation[(bestIndex + 1) % count];
  const curvature = before - 2 * center + after;
  const subSampleOffset = Math.abs(curvature) < 1e-15
    ? 0
    : Math.max(-0.5, Math.min(0.5, 0.5 * (before - after) / curvature));
  const correlationOffset =
    signedCircularIndex(bestIndex, count) + subSampleOffset;
  // Correlation bin k compares left[n] to right[n+k]. A positive runtime
  // circular shift samples right[n-shift], hence shift = -k.
  const shiftRightToLeft = -correlationOffset;
  let secondValue = Number.NEGATIVE_INFINITY;
  for (let index = 0; index < count; ++index) {
    const distance = Math.abs(
      signedCircularIndex(index - bestIndex, count),
    );
    if (distance > 32) secondValue = Math.max(secondValue, correlation[index]);
  }
  const ambiguityMargin = center - secondValue;
  return Object.freeze({
    shiftRightToLeft,
    peakCorrelation: center,
    ambiguityMargin,
    unconstrainedShiftRightToLeft:
      -signedCircularIndex(unconstrainedIndex, count),
    unconstrainedPeakCorrelation: unconstrainedValue,
    weight:
      Math.max(1e-4, center * center) *
      Math.max(0.02, Math.min(1, ambiguityMargin * 20)),
  });
}

function solveLinearSystem(matrix, vector) {
  const count = vector.length;
  const augmented = matrix.map((row, index) => [...row, vector[index]]);
  for (let column = 0; column < count; ++column) {
    let pivot = column;
    for (let row = column + 1; row < count; ++row) {
      if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) {
        pivot = row;
      }
    }
    if (Math.abs(augmented[pivot][column]) < 1e-12) {
      fail("phase-alignment graph system is singular");
    }
    [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
    const divisor = augmented[column][column];
    for (let index = column; index <= count; ++index) {
      augmented[column][index] /= divisor;
    }
    for (let row = 0; row < count; ++row) {
      if (row === column) continue;
      const factor = augmented[row][column];
      for (let index = column; index <= count; ++index) {
        augmented[row][index] -= factor * augmented[column][index];
      }
    }
  }
  return augmented.map((row) => row[count]);
}

function periodicSample(samples, position) {
  const count = samples.length;
  const wrapped = ((position % count) + count) % count;
  const left = Math.floor(wrapped);
  const right = (left + 1) % count;
  return samples[left] + (samples[right] - samples[left]) * (wrapped - left);
}

function circularShift(samples, shiftSamples) {
  const output = new Float64Array(samples.length);
  for (let index = 0; index < samples.length; ++index) {
    output[index] = periodicSample(samples, index - shiftSamples);
  }
  return output;
}

function summedMean(capture) {
  const routes = ROUTES.map((route) => {
    const metadata = capture.routes.find((value) => value.route_id === route.id);
    if (metadata === undefined) fail(`${capture.id} omitted ${route.id}`);
    return readFloat32Le(
      path.join(captureDirectory, metadata.mean_filename),
      SAMPLES_PER_CYCLE,
    );
  });
  return Float64Array.from(routes[0], (_, index) => {
    let sum = 0;
    for (const route of routes) sum += route[index];
    return sum;
  });
}

function buildPhaseAlignment(captures, priorPhaseAlignment = null) {
  const nodes = captures.map((capture) => capture.id);
  const nodeIndex = new Map(nodes.map((id, index) => [id, index]));
  const fixedShifts = new Map();
  if (priorPhaseAlignment !== null) {
    if (
      !Array.isArray(priorPhaseAlignment.cells) ||
      typeof priorPhaseAlignment.reference_cell_id !== "string"
    ) {
      fail("prior held package has an invalid phase-alignment contract");
    }
    for (const cell of priorPhaseAlignment.cells) {
      if (
        nodeIndex.has(cell.id) &&
        Number.isFinite(cell.shift_to_canonical_samples)
      ) {
        fixedShifts.set(cell.id, cell.shift_to_canonical_samples);
      }
    }
  }
  const referenceRpm = RPM_ANCHORS.reduce((best, rpm) =>
    Math.abs(rpm - 3000) < Math.abs(best - 3000) ? rpm : best
  );
  const defaultReferenceCellId = `${referenceRpm}rpm-power`;
  const referenceCellId = fixedShifts.has(
      priorPhaseAlignment?.reference_cell_id,
    )
    ? priorPhaseAlignment.reference_cell_id
    : defaultReferenceCellId;
  if (fixedShifts.size === 0) fixedShifts.set(referenceCellId, 0);
  const signals = new Map(captures.map((capture) => [capture.id, summedMean(capture)]));
  const byCoordinate = new Map(
    captures.map((capture) => [`${capture.rpm}:${capture.lane}`, capture]),
  );
  const edges = [];
  const addEdge = (left, right, axis) => {
    // RPM changes alter a fixed acoustic delay's crank-angle phase, so its
    // adjacent optimum legitimately spans the whole 720-degree cycle. Load
    // changes at a held RPM should remain on the local pulse identity; the
    // narrower window rejects an occasional neighbouring-cylinder alias.
    const maximumShiftSamples = axis === "rpm"
      ? SAMPLES_PER_CYCLE / 2
      : SAMPLES_PER_CYCLE / 8;
    const alignment = adjacentAlignment(
      signals.get(left.id),
      signals.get(right.id),
      maximumShiftSamples,
    );
    edges.push({
      left: left.id,
      right: right.id,
      axis,
      ...alignment,
    });
  };
  for (const lane of LOAD_LANES) {
    for (let index = 0; index + 1 < RPM_ANCHORS.length; ++index) {
      addEdge(
        byCoordinate.get(`${RPM_ANCHORS[index]}:${lane.id}`),
        byCoordinate.get(`${RPM_ANCHORS[index + 1]}:${lane.id}`),
        "rpm",
      );
    }
  }
  for (const rpm of RPM_ANCHORS) {
    for (let index = 0; index + 1 < LOAD_LANES.length; ++index) {
      addEdge(
        byCoordinate.get(`${rpm}:${LOAD_LANES[index].id}`),
        byCoordinate.get(`${rpm}:${LOAD_LANES[index + 1].id}`),
        "load",
      );
    }
  }

  const shifts = new Map(fixedShifts);
  const unused = new Set(nodes.filter((id) => !shifts.has(id)));
  while (unused.size !== 0) {
    let selected = null;
    for (const edge of edges) {
      const leftKnown = shifts.has(edge.left);
      const rightKnown = shifts.has(edge.right);
      if (leftKnown === rightKnown) continue;
      if (selected === null || edge.weight > selected.weight) selected = edge;
    }
    if (selected === null) fail("phase-alignment grid is disconnected");
    if (shifts.has(selected.left)) {
      shifts.set(
        selected.right,
        shifts.get(selected.left) + selected.shiftRightToLeft,
      );
      unused.delete(selected.right);
    } else {
      shifts.set(
        selected.left,
        shifts.get(selected.right) - selected.shiftRightToLeft,
      );
      unused.delete(selected.left);
    }
  }

  // Use all adjacent edges after the confidence-weighted spanning-tree unwrap.
  // Each modular observation is lifted nearest the tree prediction, then a
  // weighted graph least-squares solve removes path dependence.
  const referenceIndex = nodeIndex.get(referenceCellId);
  const variables = nodes.filter((id) => !fixedShifts.has(id));
  const variableIndex = new Map(variables.map((id, index) => [id, index]));
  const liftedEdges = edges.map((edge) => {
    const predicted = shifts.get(edge.right) - shifts.get(edge.left);
    const lifted =
      edge.shiftRightToLeft +
      Math.round((predicted - edge.shiftRightToLeft) / SAMPLES_PER_CYCLE) *
        SAMPLES_PER_CYCLE;
    return { ...edge, liftedShift: lifted };
  });
  const matrix = Array.from({ length: variables.length }, () =>
    new Array(variables.length).fill(0),
  );
  const vector = new Array(variables.length).fill(0);
  for (const edge of liftedEdges) {
    const weight = edge.weight;
    const leftVariable = variableIndex.get(edge.left);
    const rightVariable = variableIndex.get(edge.right);
    if (leftVariable !== undefined) {
      matrix[leftVariable][leftVariable] += weight;
      vector[leftVariable] -= weight * edge.liftedShift;
      if (rightVariable === undefined) {
        vector[leftVariable] += weight * fixedShifts.get(edge.right);
      }
    }
    if (rightVariable !== undefined) {
      matrix[rightVariable][rightVariable] += weight;
      vector[rightVariable] += weight * edge.liftedShift;
      if (leftVariable === undefined) {
        vector[rightVariable] += weight * fixedShifts.get(edge.left);
      }
    }
    if (leftVariable !== undefined && rightVariable !== undefined) {
      matrix[leftVariable][rightVariable] -= weight;
      matrix[rightVariable][leftVariable] -= weight;
    }
  }
  if (variables.length !== 0) {
    const solved = solveLinearSystem(matrix, vector);
    for (let index = 0; index < variables.length; ++index) {
      shifts.set(variables[index], solved[index]);
    }
  }

  const midpointComparisons = [];
  for (const edge of liftedEdges) {
    const left = signals.get(edge.left);
    const right = signals.get(edge.right);
    const shiftedLeft = circularShift(left, shifts.get(edge.left));
    const shiftedRight = circularShift(right, shifts.get(edge.right));
    let leftSquare = 0;
    let rightSquare = 0;
    let directSquare = 0;
    let alignedSquare = 0;
    for (let index = 0; index < SAMPLES_PER_CYCLE; ++index) {
      leftSquare += left[index] * left[index];
      rightSquare += right[index] * right[index];
      const direct = 0.5 * (left[index] + right[index]);
      const aligned = 0.5 * (shiftedLeft[index] + shiftedRight[index]);
      directSquare += direct * direct;
      alignedSquare += aligned * aligned;
    }
    const leftRms = Math.sqrt(leftSquare / SAMPLES_PER_CYCLE);
    const rightRms = Math.sqrt(rightSquare / SAMPLES_PER_CYCLE);
    const targetRms = 0.5 * (leftRms + rightRms);
    const directRms = Math.sqrt(directSquare / SAMPLES_PER_CYCLE);
    const alignedRms = Math.sqrt(alignedSquare / SAMPLES_PER_CYCLE);
    midpointComparisons.push({
      left_cell_id: edge.left,
      right_cell_id: edge.right,
      axis: edge.axis,
      direct_midpoint_rms: directRms,
      aligned_midpoint_rms: alignedRms,
      linear_anchor_rms_target: targetRms,
      direct_retained_target_01: directRms / Math.max(targetRms, 1e-30),
      aligned_retained_target_01: alignedRms / Math.max(targetRms, 1e-30),
    });
  }
  const edgeResiduals = liftedEdges.map((edge) =>
    shifts.get(edge.right) - shifts.get(edge.left) - edge.liftedShift,
  );
  const phaseAlignment = {
    method: "shared-route-sum-circular-correlation-unwrapped-grid-v1",
    reference_cell_id: referenceCellId,
    unit: "phase-samples",
    interpolation: "unwrapped-linear",
    cells: captures.map((capture) => ({
      id: capture.id,
      shift_to_canonical_samples: shifts.get(capture.id),
    })),
  };
  return {
    phaseAlignment,
    report: {
      edge_count: liftedEdges.length,
      edge_residual_samples: summary(edgeResiduals.map(Math.abs)),
      minimum_peak_correlation: Math.min(
        ...liftedEdges.map((edge) => edge.peakCorrelation),
      ),
      minimum_ambiguity_margin: Math.min(
        ...liftedEdges.map((edge) => edge.ambiguityMargin),
      ),
      unconstrained_peak_outside_adjacent_window_count: liftedEdges.filter(
        (edge) => Math.abs(edge.unconstrainedShiftRightToLeft) > SAMPLES_PER_CYCLE / 8,
      ).length,
      direct_midpoint_retained_target_01: summary(
        midpointComparisons.map((value) => value.direct_retained_target_01),
      ),
      aligned_midpoint_retained_target_01: summary(
        midpointComparisons.map((value) => value.aligned_retained_target_01),
      ),
      midpoint_comparisons: midpointComparisons,
      preserved_fixed_cell_count: fixedShifts.size,
      maximum_preserved_fixed_shift_error_samples: Math.max(
        0,
        ...[...fixedShifts].map(([id, shift]) =>
          Math.abs(shifts.get(id) - shift)
        ),
      ),
      adjacent_edges: liftedEdges.map((edge) => ({
        left_cell_id: edge.left,
        right_cell_id: edge.right,
        axis: edge.axis,
        constrained_shift_right_to_left_samples: edge.shiftRightToLeft,
        constrained_peak_correlation: edge.peakCorrelation,
        ambiguity_margin: edge.ambiguityMargin,
        unconstrained_shift_right_to_left_samples:
          edge.unconstrainedShiftRightToLeft,
        unconstrained_peak_correlation: edge.unconstrainedPeakCorrelation,
        lifted_shift_right_to_left_samples: edge.liftedShift,
        solved_shift_delta_samples:
          shifts.get(edge.right) - shifts.get(edge.left),
      })),
    },
  };
}

function meanSeamAudit(captures) {
  const routeRows = [];
  const summedRows = [];
  for (const capture of captures) {
    const routeMeans = [];
    for (const route of capture.routes) {
      const mean = readFloat32Le(
        path.join(captureDirectory, route.mean_filename),
        SAMPLES_PER_CYCLE,
      );
      routeMeans.push(mean);
      const deltas = [];
      for (let index = 1; index < mean.length; ++index) {
        deltas.push(Math.abs(mean[index] - mean[index - 1]));
      }
      const seam = Math.abs(mean[0] - mean.at(-1));
      routeRows.push({
        cell_id: capture.id,
        route_id: route.route_id,
        seam_absolute_delta: seam,
        seam_over_derivative_rms:
          seam / Math.max(derivativeRms(mean), 1e-30),
        seam_over_derivative_p99:
          seam / Math.max(quantile(deltas, 0.99), 1e-30),
        maximum_interior_adjacent_delta: Math.max(...deltas),
        wrap_only_outlier: seam > Math.max(...deltas),
      });
    }
    const summed = Float64Array.from(routeMeans[0], (_, index) => {
      let sum = 0;
      for (const mean of routeMeans) sum += mean[index];
      return sum;
    });
    const deltas = [];
    for (let index = 1; index < summed.length; ++index) {
      deltas.push(Math.abs(summed[index] - summed[index - 1]));
    }
    const seam = Math.abs(summed[0] - summed.at(-1));
    summedRows.push({
      cell_id: capture.id,
      seam_absolute_delta: seam,
      seam_over_derivative_rms:
        seam / Math.max(derivativeRms(summed), 1e-30),
      seam_over_derivative_p99:
        seam / Math.max(quantile(deltas, 0.99), 1e-30),
      maximum_interior_adjacent_delta: Math.max(...deltas),
      wrap_only_outlier: seam > Math.max(...deltas),
    });
  }
  return {
    maximum_route: routeRows.reduce((left, right) =>
      right.seam_over_derivative_rms > left.seam_over_derivative_rms
        ? right
        : left,
    ),
    maximum_summed: summedRows.reduce((left, right) =>
      right.seam_over_derivative_rms > left.seam_over_derivative_rms
        ? right
        : left,
    ),
    route_wrap_only_outlier_count: routeRows.filter(
      (row) => row.wrap_only_outlier,
    ).length,
    summed_wrap_only_outlier_count: summedRows.filter(
      (row) => row.wrap_only_outlier,
    ).length,
    summed_cells: summedRows,
  };
}

function buildPackage(captures, identity, phaseAlignment) {
  fs.rmSync(packageDirectory, { recursive: true, force: true });
  fs.mkdirSync(path.join(packageDirectory, "audio"), { recursive: true });
  const routeManifests = new Map();
  let payloadBytes = 0;
  for (const routeDescriptor of ROUTES) {
    const cells = [];
    for (const capture of captures) {
      const route = capture.routes.find(
        (candidate) => candidate.route_id === routeDescriptor.id,
      );
      if (route === undefined) fail(`capture ${capture.id} omitted ${routeDescriptor.id}`);
      const meanRelativePath = `audio/${route.mean_filename}`;
      const residualRelativePath = `audio/${route.residual_filename}`;
      for (const [filename, relativePath, expectedHash] of [
        [route.mean_filename, meanRelativePath, route.mean_sha256],
        [route.residual_filename, residualRelativePath, route.residual_sha256],
      ]) {
        const source = path.join(captureDirectory, filename);
        const destination = path.join(packageDirectory, relativePath);
        fs.copyFileSync(source, destination);
        const copied = fs.readFileSync(destination);
        if (sha256(copied) !== expectedHash) fail(`${filename} copy hash changed`);
        payloadBytes += copied.byteLength;
      }
      cells.push({
        id: `${capture.id}-${routeDescriptor.token}`,
        rpm: capture.rpm,
        lane: capture.lane,
        capture_throttle_01: capture.throttle_01,
        capture_provenance: {
          coalesced_authored_lanes: capture.coalesced_authored_lanes ?? [
            capture.lane,
          ],
          coalesced_capture_throttles_01:
            capture.coalesced_capture_throttles_01 ?? [capture.throttle_01],
        },
        manifold_pressure_pa_abs:
          route.telemetry.mean_manifold_pressure_pa_abs,
        source_cycle_begin_revolutions:
          route.source_cycle_begin_revolutions,
        source_cycle_end_revolutions: route.source_cycle_end_revolutions,
        mean: {
          relative_path: meanRelativePath,
          sample_count: SAMPLES_PER_CYCLE,
          byte_count: route.mean_byte_count,
          payload_sha256: route.mean_sha256,
        },
        residual_bank: {
          relative_path: residualRelativePath,
          cycle_count: CAPTURED_CYCLE_COUNT,
          samples_per_cycle: SAMPLES_PER_CYCLE,
          sample_count: CAPTURED_CYCLE_COUNT * SAMPLES_PER_CYCLE,
          byte_count: route.residual_byte_count,
          payload_sha256: route.residual_sha256,
          selection_contract:
            "change residual ordinal only at a 720-degree boundary",
        },
        capture_fidelity: route.telemetry,
        decomposition_metrics: {
          ...route.metrics,
          tapered_residual_rms:
            route.metrics.source_rms *
            route.metrics.tapered_residual_rms_over_source_rms,
          tapered_residual_power:
            (route.metrics.source_rms *
              route.metrics.tapered_residual_rms_over_source_rms) ** 2,
        },
      });
    }
    const filename = `${routeDescriptor.token}.json`;
    const manifest = {
      schema: "engine-sim-offline/responsive-audio-held-route",
      id: `${ENGINE_ID}-exact-held-${routeDescriptor.token}-live-preview`,
      engine: ENGINE_ID,
      audio: {
        sample_rate_hz: SAMPLE_RATE,
        encoding: "float32le",
        channel_layout: "mono",
        bus_id: routeDescriptor.id,
      },
      phase: {
        cycle_revolutions: CYCLE_REVOLUTIONS,
        samples_per_cycle: SAMPLES_PER_CYCLE,
        residual_cycle_count: CAPTURED_CYCLE_COUNT,
        residual_boundary_value: 0,
        residual_taper: {
          shape: "smoothstep",
          fraction_per_edge: RESIDUAL_TAPER_FRACTION_PER_EDGE,
          frames_per_edge: Math.round(
            SAMPLES_PER_CYCLE * RESIDUAL_TAPER_FRACTION_PER_EDGE,
          ),
        },
      },
      domain: {
        rpm_anchors: RPM_ANCHORS,
        load_coordinate: "measured-intake-manifold-pressure-pa-abs",
        load_lanes: LOAD_LANES,
      },
      cells,
      provenance: {
        engine: {
          id: ENGINE_ID,
          sha256: captures[0].routes[0].engine_provenance_sha256,
        },
        renderer_build: {
          id: "engine-sim-offline-renderer-build",
          sha256: captures[0].routes[0].renderer_source_sha256,
        },
        capture_method: CAPTURE_METHOD_ID,
        decomposition_method: DECOMPOSITION_METHOD_ID,
        capture_identity_sha256: identity.sha256,
      },
    };
    writeJson(path.join(packageDirectory, filename), manifest);
    routeManifests.set(routeDescriptor.id, filename);
  }

  const transferByKey = new Map();
  for (const route of PRESENTATION_ROUTES) {
    const key = JSON.stringify([
      route.impulseResponseAssetId,
      route.impulseResponseGainLinear,
    ]);
    if (transferByKey.has(key)) continue;
    const filename =
      `${portableArtifactToken("ir", key)}-kernel-complex-f64le.bin`;
    const spectrumPath = path.join(packageDirectory, filename);
    spawnSyncChecked(irSpectrumDumper, [
      impulseResponsePath,
      String(route.impulseResponseGainLinear),
      spectrumPath,
    ]);
    const bytes = fs.readFileSync(spectrumPath);
    payloadBytes += bytes.byteLength;
    transferByKey.set(key, Object.freeze({
      fft_size: 65_536,
      coefficient_count: 30_071,
      spectrum_encoding: "interleaved-complex-float64le",
      spectrum_path: filename,
      spectrum_byte_count: bytes.byteLength,
      spectrum_sha256: sha256(bytes),
    }));
  }
  const presentationRoutes = PRESENTATION_ROUTES.map((route) => {
    const key = JSON.stringify([
      route.impulseResponseAssetId,
      route.impulseResponseGainLinear,
    ]);
    return Object.freeze({
      dry_bus_id: route.busId,
      source_route_id: route.sourceRouteId,
      impulse_response_asset_id: route.impulseResponseAssetId,
      impulse_response_payload_sha256: audioAsset.sha256,
      impulse_response_gain_linear: route.impulseResponseGainLinear,
      wet_mix_01: route.wetMix01,
      transfer: transferByKey.get(key),
    });
  });
  const engineProvenances = new Set(
    captures.flatMap((capture) =>
      capture.routes.map((route) => route.engine_provenance_sha256),
    ),
  );
  const rendererProvenances = new Set(
    captures.flatMap((capture) =>
      capture.routes.map((route) => route.renderer_source_sha256),
    ),
  );
  if (engineProvenances.size !== 1 || rendererProvenances.size !== 1) {
    fail("held texture captures disagree about engine or renderer provenance");
  }
  const root = {
    schema: "engine-sim-offline/responsive-audio-held-texture",
    id: `${ENGINE_ID}-exact-held-phase-texture-live-preview`,
    engine: ENGINE_ID,
    fidelity: {
      purpose: "interactive-source-b-versus-source-a-audition-preview",
      physics_rate_hz: PHYSICS_RATE,
      canonical_offline_bake: false,
    },
    representation: {
      kind: "cyclic-mean-plus-boundary-zero-cycle-residual-bank",
      timeline_included: false,
      runtime_coordinates: [
        "rpm",
        "measured-intake-manifold-pressure-pa-abs",
        "unwrapped-crank-revolutions",
      ],
      internal_runtime_state: ["residual-cycle-ordinal"],
    },
    domain: {
      minimum_rpm: RPM_ANCHORS[0],
      maximum_rpm: RPM_ANCHORS.at(-1),
      rpm_anchors: RPM_ANCHORS,
      load_lanes: LOAD_LANES,
      operating_cell_count: captures.length,
    },
    dry_bus_ids: ROUTES.map((route) => route.id),
    route_manifests: ROUTES.map((route) => ({
      bus_id: route.id,
      manifest_path: routeManifests.get(route.id),
    })),
    presentation: {
      audition_bus_id: auditionBusId,
      audition_dry_bus_order: AUDITION_DRY_BUS_IDS,
      captured_to_source_scale: CAPTURED_TO_SOURCE_SCALE,
      master_volume_linear: MASTER_VOLUME_LINEAR,
      routes: presentationRoutes,
    },
    phase_alignment: phaseAlignment,
    texture_selection: {
      algorithm: "splitmix64-shuffled-bags-v1",
      bank_size: CAPTURED_CYCLE_COUNT,
      public_seed: PUBLIC_SEED,
      no_adjacent_repeat: true,
      change_phase: "720-degree-boundary",
    },
    interpolation: {
      mean: {
        method: "common-delay-phase-warp",
        energy_target: "linear-anchor-rms",
      },
      residual: {
        cross_cell_correlation: "independent",
        energy_target: "linear-anchor-power",
        normalization: "sqrt(Ptarget/sum(w_i^2 P_i))",
      },
    },
    provenance: {
      engine: {
        id: ENGINE_ID,
        sha256: [...engineProvenances][0],
      },
      renderer_build: {
        id: "engine-sim-offline-renderer-build",
        sha256: [...rendererProvenances][0],
      },
      capture_identity: identity,
    },
  };
  writeJson(path.join(packageDirectory, "package.json"), root);
  return { root, payloadBytes };
}

function publishResponsiveRuntime(heldRoot) {
  const directionalManifestPath = path.join(
    stagingEngineDirectory,
    "directional/runtime.json",
  );
  const directional = fs.existsSync(directionalManifestPath)
    ? readJson(directionalManifestPath)
    : null;
  if (directional === null) {
    fs.rmSync(path.join(stagingEngineDirectory, "runtime.json"), {
      force: true,
    });
    return null;
  }
  let minimumRpm = directional.domain?.minimum_rpm;
  let maximumRpm = directional.domain?.maximum_rpm;
  {
    if (
      directional.schema !==
        "engine-sim-offline/responsive-audio-directional-texture" ||
      directional.engine !== ENGINE_ID ||
      !Array.isArray(directional.dry_bus_ids) ||
      directional.dry_bus_ids.length !== ROUTES.length ||
      directional.dry_bus_ids.some((busId, index) => busId !== ROUTES[index].id) ||
      directional.provenance?.engine?.sha256 !==
        heldRoot.provenance.engine.sha256 ||
      directional.provenance?.renderer_build?.sha256 !==
        heldRoot.provenance.renderer_build.sha256
    ) {
      fail(`${ENGINE_ID} directional package disagrees with its held package`);
    }
    if (
      !(minimumRpm > 0) ||
      !(maximumRpm > minimumRpm) ||
      minimumRpm !== RESPONSIVE_AUDIO_OUTER_DOMAIN.minimumRpm ||
      maximumRpm !== RESPONSIVE_AUDIO_OUTER_DOMAIN.maximumRpm ||
      minimumRpm > heldRoot.domain.minimum_rpm ||
      maximumRpm < heldRoot.domain.maximum_rpm
    ) {
      fail(`${ENGINE_ID} directional RPM domain does not match the approved interactive envelope`);
    }
  }
  const runtime = {
    schema: "engine-sim-offline/responsive-audio-preview",
    id: `${ENGINE_ID}-responsive-audio`,
    engine: ENGINE_ID,
    fidelity: {
      purpose: "interactive-source-b-versus-source-a-audition-preview",
      physics_rate_hz: PHYSICS_RATE,
      canonical_offline_bake: false,
    },
    audio: {
      sample_rate_hz: SAMPLE_RATE,
      encoding: "float32le",
      channel_layout: "mono",
      bus_id: auditionBusId,
    },
    held_package_path: "held/package.json",
    directional_package_path: "directional/runtime.json",
    domain: {
      minimum_rpm: minimumRpm,
      maximum_rpm: maximumRpm,
    },
    provenance: {
      engine: heldRoot.provenance.engine,
      renderer_build: heldRoot.provenance.renderer_build,
      representation:
        "exact-held-non-repeating-texture-plus-n-route-directional-transients-through-authored-presentation",
    },
  };
  writeJson(path.join(stagingEngineDirectory, "runtime.json"), runtime);
  return runtime;
}

async function coordinator() {
  const overallStartedAt = performance.now();
  for (const required of [
    modulePath,
    wasmPath,
    enginePath,
    impulseResponsePath,
    accessoryPath,
    irSpectrumDumper,
  ]) {
    if (!fs.existsSync(required)) fail(`required input is absent: ${required}`);
  }
  fs.mkdirSync(captureDirectory, { recursive: true });
  const priorPackagePath = path.join(packageDirectory, "package.json");
  const priorPackage = fs.existsSync(priorPackagePath)
    ? readJson(priorPackagePath)
    : null;
  const identity = gridCaptureIdentity();
  const states = RPM_ANCHORS.flatMap((rpm) =>
    LOAD_LANES.map((lane) => ({
      rpm,
      lane,
      identity: captureIdentity(rpm),
    })),
  );
  const cachedStates = states.filter(({ rpm, lane, identity: stateIdentity }) =>
    cachedCaptureIsCurrent(rpm, lane, stateIdentity)
  );
  const preservedCachedArtifacts = new Map(
    cachedStates.map(({ rpm, lane }) => [
      stateId(rpm, lane),
      captureArtifactFingerprint(rpm, lane),
    ]),
  );
  const jobs = states.filter(({ rpm, lane, identity: stateIdentity }) =>
    !cachedCaptureIsCurrent(rpm, lane, stateIdentity)
  );
  const captureQueueWallTimeSeconds = jobs.length === 0
    ? 0
    : await runCaptureQueue(jobs);
  for (const { rpm, lane } of states) {
    if (!cachedCaptureIsCurrent(rpm, lane, captureIdentity(rpm))) {
      fail(`${stateId(rpm, lane)} did not produce a current held capture`);
    }
  }
  for (const [id, before] of preservedCachedArtifacts) {
    const [rpmText, laneId] = id.split("rpm-");
    const lane = LOAD_LANES.find(({ id: candidate }) => candidate === laneId);
    const after = captureArtifactFingerprint(Number(rpmText), lane);
    if (after.aggregate_sha256 !== before.aggregate_sha256) {
      fail(`${id} cached capture changed while adding envelope coverage`);
    }
  }
  const rawCaptures = RPM_ANCHORS.flatMap((rpm) =>
    LOAD_LANES.map((lane) => readJson(metadataPath(rpm, lane))),
  );
  const captures = coalesceDuplicateLoadCaptures(rawCaptures);
  const { phaseAlignment: rawPhaseAlignment, report: phaseAlignmentReport } =
    buildPhaseAlignment(
      rawCaptures,
      reusablePriorPhaseAlignment(priorPackage, identity.sha256),
    );
  const retainedCaptureIds = new Set(captures.map((capture) => capture.id));
  const phaseAlignment = {
    ...rawPhaseAlignment,
    cells: rawPhaseAlignment.cells.filter((cell) =>
      retainedCaptureIds.has(cell.id)
    ),
  };
  if (
    !retainedCaptureIds.has(phaseAlignment.reference_cell_id) ||
    phaseAlignment.cells.length !== captures.length
  ) {
    fail("coalesced held grid invalidated its phase-alignment reference");
  }
  const meanSeamAuditReport = meanSeamAudit(captures);
  const { root, payloadBytes } = buildPackage(
    captures,
    identity,
    phaseAlignment,
  );
  const routeMetrics = captures.flatMap((capture) =>
    capture.routes.map((route) => route.metrics),
  );
  const captureFidelity = captures.flatMap((capture) =>
    capture.routes.map((route) => route.telemetry),
  );
  const report = {
    schema: "engine-sim-offline/held-phase-texture-package-report",
    package_directory: packageDirectory,
    package_manifest: path.join(packageDirectory, "package.json"),
    package_manifest_sha256: sha256(
      fs.readFileSync(path.join(packageDirectory, "package.json")),
    ),
    package_id: root.id,
    capture_identity_sha256: identity.sha256,
    maximum_capture_concurrency: MAXIMUM_CAPTURE_CONCURRENCY,
    newly_captured_state_count: jobs.length,
    cached_state_count: rawCaptures.length - jobs.length,
    preserved_cached_state_count: preservedCachedArtifacts.size,
    preserved_cached_capture_aggregate_sha256: sha256(Buffer.from(JSON.stringify(
      [...preservedCachedArtifacts]
        .map(([id, fingerprint]) => ({
          id,
          aggregate_sha256: fingerprint.aggregate_sha256,
        }))
        .sort((left, right) => compareCodeUnits(left.id, right.id)),
    ))),
    captured_state_count: rawCaptures.length,
    coalesced_duplicate_state_count: rawCaptures.length - captures.length,
    operating_cell_count: captures.length,
    route_payload_count: captures.length * ROUTES.length,
    captured_cycle_count_per_route_cell: CAPTURED_CYCLE_COUNT,
    package_payload_bytes: payloadBytes,
    phase_alignment: phaseAlignmentReport,
    mean_seam_audit: meanSeamAuditReport,
    capture_queue_wall_time_seconds: captureQueueWallTimeSeconds,
    aggregate_child_capture_wall_time_seconds: rawCaptures.reduce(
      (sum, capture) => sum + capture.wall_time_seconds,
      0,
    ),
    total_generator_wall_time_seconds:
      (performance.now() - overallStartedAt) / 1000,
    invariants: {
      maximum_residual_boundary_magnitude: Math.max(
        ...routeMetrics.map(
          (metrics) => metrics.maximum_residual_boundary_magnitude,
        ),
      ),
      mean_seam_over_mean_derivative_rms: summary(
        routeMetrics.map(
          (metrics) => metrics.mean_seam_over_mean_derivative_rms,
        ),
      ),
      raw_residual_rms_over_source_rms: summary(
        routeMetrics.map(
          (metrics) => metrics.raw_residual_rms_over_source_rms,
        ),
      ),
      tapered_residual_rms_over_source_rms: summary(
        routeMetrics.map(
          (metrics) => metrics.tapered_residual_rms_over_source_rms,
        ),
      ),
      reconstruction_error_rms_over_source_rms: summary(
        routeMetrics.map(
          (metrics) => metrics.reconstruction_error_rms_over_source_rms,
        ),
      ),
      rpm_error_rms: summary(
        captureFidelity.map((metrics) => metrics.rpm_error_rms),
      ),
      maximum_absolute_rpm_error: Math.max(
        ...captureFidelity.map(
          (metrics) => metrics.maximum_absolute_rpm_error,
        ),
      ),
    },
    capture_wall_times_seconds: rawCaptures.map((capture) => ({
      id: capture.id,
      wall_time_seconds: capture.wall_time_seconds,
      scenario_duration_seconds: capture.scenario_duration_seconds,
    })),
  };
  fs.rmSync(stagingHeldDirectory, { recursive: true, force: true });
  fs.mkdirSync(stagingEngineDirectory, { recursive: true });
  fs.cpSync(packageDirectory, stagingHeldDirectory, {
    recursive: true,
    force: true,
    errorOnExist: false,
  });
  writeJson(reportPath, report);
  if (!args.includes("--held-only")) publishResponsiveRuntime(root);
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
}

const captureStateArgument = args.indexOf("--capture-state");
if (args.includes("--print-capture-identity")) {
  process.stdout.write(`${JSON.stringify(gridCaptureIdentity())}\n`);
} else if (args.includes("--print-capture-plan")) {
  const states = RPM_ANCHORS.flatMap((rpm) =>
    LOAD_LANES.map((lane) => {
      const sourceScenario = scenario(rpm, lane);
      return {
        rpm,
        lane: lane.id,
        preparation_duration_seconds: preparationDurationSeconds(rpm),
        scenario_duration_seconds: sourceScenario.total_duration.value,
        cached: cachedCaptureIsCurrent(rpm, lane, captureIdentity(rpm)),
      };
    }),
  );
  process.stdout.write(`${JSON.stringify({
    schema: "engine-sim-offline/held-phase-texture-capture-plan",
    engine: ENGINE_ID,
    cached_state_count: states.filter(({ cached }) => cached).length,
    new_state_count: states.filter(({ cached }) => !cached).length,
    new_states: states.filter(({ cached }) => !cached),
  }, null, 2)}\n`);
} else if (captureStateArgument !== -1) {
  const rpm = Number(args[captureStateArgument + 1]);
  const lane = LOAD_LANES.find(
    (candidate) => candidate.id === args[captureStateArgument + 2],
  );
  if (!RPM_ANCHORS.includes(rpm) || lane === undefined) {
    fail("capture state requires a configured RPM anchor and load lane");
  }
  await captureState(rpm, lane);
} else if (args.includes("--all")) {
  await runAllEngines();
} else if (requestedEngineId !== null) {
  await coordinator();
} else {
  fail(
    "usage: node generate-held-texture-packages.mjs --engine ID [--capture-state RPM LANE] [--held-only] | --all [--concurrency 1..4] [--held-only]",
  );
}
