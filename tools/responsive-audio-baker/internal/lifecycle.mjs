import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPOSITORY = path.resolve(HERE, "../../..");
const ENGINE_ID = process.argv[2] ?? "";
const INVENTORY_PATH = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_INVENTORY");
const INVENTORY = JSON.parse(fs.readFileSync(INVENTORY_PATH, "utf8"));
const ENGINE_ENTRY = INVENTORY.engines.find(({ engine_id: id }) => id === ENGINE_ID);
if (!ENGINE_ENTRY) throw new Error(`unknown engine id: ${ENGINE_ID}`);
const PACKAGE_ROOT = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_OUTPUT");
const BAKE_CACHE_ROOT = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_CACHE");
const RESPONSIVE_RUNTIME = path.join(PACKAGE_ROOT, "runtime.json");
const FINAL_PACKAGE = path.join(PACKAGE_ROOT, "lifecycle");
const MODULE_ROOT = path.join(REPOSITORY, "web");
const MODULE_PATH = requiredEnvironmentPath("ESO_RESPONSIVE_BAKE_MODULE");
const ENGINE_PATH = inventoryPath(ENGINE_ENTRY.engine_path, "engine");
const HELD_MANIFEST_PATH = path.join(PACKAGE_ROOT, "held/package.json");
const CANDIDATE_ROOT = path.join(BAKE_CACHE_ROOT, "lifecycle", "candidate");

function requiredEnvironmentPath(name) {
  const value = process.env[name];
  if (typeof value !== "string" || value.length === 0 || !path.isAbsolute(value)) {
    throw new Error(`${name} must be an absolute path`);
  }
  return path.normalize(value);
}

function inventoryPath(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`${label} path is absent`);
  }
  const resolved = path.isAbsolute(value)
    ? path.normalize(value)
    : path.resolve(REPOSITORY, value);
  if (
    !path.isAbsolute(value) &&
    !resolved.startsWith(`${REPOSITORY}${path.sep}`)
  ) {
    throw new Error(`${label} path escapes the repository`);
  }
  return resolved;
}

const SAMPLE_RATE_HZ = 192_000;
const PHYSICS_RATE_HZ = 10_000;
const FOUR_STROKE_CYCLE_REVOLUTIONS = 2;
const MAXIMUM_SEAM_FRAMES = Math.round(0.1 * SAMPLE_RATE_HZ);
const EVENT_LEAD_FRAMES = Math.round(0.17 * SAMPLE_RATE_HZ);
const QUIET_FRAMES = Math.round(0.3 * SAMPLE_RATE_HZ);
const PRESENTATION_FADE_FRAMES = Math.round(0.02 * SAMPLE_RATE_HZ);

const IGNITION_TIME_SECONDS = 0.7;
const SHUTDOWN_AUDIBLE_START_SECONDS = 2;
const KEYOFF_TIME_SECONDS = 2.8;
const KEYOFF_DECELERATION_SECONDS = 0.5;
const ACCEPTED_VARIANT3_GAINS = Object.freeze({
  coast: 0.5761944116355173,
  low: 0.48887626246321264,
  mid: 0.7179832867135557,
  power: 0.7940403012442127,
});

function fail(message) {
  throw new Error(message);
}

function readText(filePath) {
  return fs.readFileSync(filePath, "utf8");
}

function parseJson(filePath) {
  return JSON.parse(readText(filePath));
}

function stableJson(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

function temporaryRunRoot(prefix) {
  const runs = path.join(BAKE_CACHE_ROOT, "lifecycle", "runs");
  fs.mkdirSync(runs, { recursive: true });
  return fs.mkdtempSync(path.join(runs, prefix));
}

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function requireInteger(value, label, minimum = 0) {
  if (!Number.isSafeInteger(value) || value < minimum) {
    fail(`${label} must be an integer >= ${minimum}`);
  }
  return value;
}

function requireFinite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail(`${label} must be finite`);
  }
  return value;
}

function requireHash(value, label) {
  if (typeof value !== "string" || !/^[0-9a-f]{64}$/u.test(value)) {
    fail(`${label} must be a lowercase SHA-256 digest`);
  }
  return value;
}

function exactKeys(object, expected, label) {
  const actual = Object.keys(object).sort();
  const wanted = [...expected].sort();
  assert.deepEqual(actual, wanted, `${label} has unexpected or missing fields`);
}

function templatePath() {
  const free = ENGINE_ENTRY.canonical_scenarios.free_interactive;
  const relative = free?.path ?? free?.template_path ??
    ENGINE_ENTRY.canonical_scenarios.available_load_step?.path ??
    ENGINE_ENTRY.canonical_scenarios.available_topology_test?.path;
  if (typeof relative !== "string") fail(`${ENGINE_ID} has no scenario template`);
  return inventoryPath(relative, "scenario template");
}

function makeScenarioBase(id, durationSeconds) {
  const scenario = structuredClone(parseJson(templatePath()));
  scenario.id = `${ENGINE_ID}-${id}`;
  scenario.engine = ENGINE_ID;
  scenario.initial_state = {
    engine_speed: { value: 0, unit: "rpm" },
    crank_angle: scenario.initial_state?.crank_angle ?? { value: 0, unit: "rad" },
    ignition_enabled: false,
    fuel_enabled: false,
    starter_enabled: true,
    dyno_enabled: false,
    limiter_enabled: true,
  };
  scenario.preparation = {
    type: "fixed_settling",
    warm_up_duration: { value: 0, unit: "s" },
    settling_duration: { value: 0, unit: "s" },
  };
  scenario.mode = {
    type: "free_engine",
    throttle_01: {
      interpolation: "right_continuous_hold",
      points: [{ time: { value: 0, unit: "s" }, value: 0 }],
    },
  };
  scenario.events = [];
  for (const rate of ["physics", "capture"]) {
    scenario.rates[rate] = { numerator: String(PHYSICS_RATE_HZ), denominator: "1", unit: "Hz" };
  }
  for (const rate of ["source_processing", "acoustics", "delivery"]) {
    scenario.rates[rate] = { numerator: String(SAMPLE_RATE_HZ), denominator: "1", unit: "Hz" };
  }
  scenario.quality = {
    id: "listening",
    process_block_capacity_frames: 3840,
    event_queue_capacity: 64,
    telemetry_capacity_frames: 1,
  };
  scenario.total_duration = { value: durationSeconds, unit: "s" };
  scenario.audible_start = { value: 0, unit: "s" };
  scenario.audible_duration = { value: durationSeconds, unit: "s" };
  scenario.output = {
    buses: ["master-engine-raw", "master-engine-audition"],
    telemetry_channels: [],
  };
  return scenario;
}

function starterScenario() {
  return makeScenarioBase("lifecycle-starter-candidate", 2.5);
}

function startupScenario(releaseSeconds = null) {
  const duration = releaseSeconds === null
    ? 4.8
    : Math.ceil((releaseSeconds + 3) / 0.02) * 0.02;
  const scenario = makeScenarioBase("lifecycle-startup-candidate", duration);
  scenario.initial_state.fuel_enabled = true;
  scenario.events.push({
    id: "ignition-on",
    time: { value: IGNITION_TIME_SECONDS, unit: "s" },
    payload: { type: "operating_state_patch", ignition_enabled: true },
  });
  if (releaseSeconds !== null) {
    scenario.events.push({
      id: "starter-release",
      time: { value: releaseSeconds, unit: "s" },
      payload: { type: "operating_state_patch", starter_enabled: false },
    });
  }
  return scenario;
}

function outputCrankInertia(engine) {
  const crank = engine.engine.crankshafts.find(
    ({ id }) => id === engine.engine.output_crankshaft,
  );
  if (!crank || crank.moment_of_inertia?.unit !== "kg*m2") {
    fail(`${ENGINE_ID} output crank inertia is unavailable`);
  }
  return requireFinite(crank.moment_of_inertia.value, "output crank inertia");
}

function shutdownScenario(heldFloorRpm, resistanceNm, engine) {
  const scenario = makeScenarioBase("lifecycle-shutdown-candidate", 5);
  scenario.initial_state.engine_speed.value = heldFloorRpm;
  scenario.initial_state.ignition_enabled = true;
  scenario.initial_state.fuel_enabled = true;
  scenario.initial_state.starter_enabled = false;
  scenario.preparation = {
    type: "fixed_horizon",
    preparation_duration: { value: SHUTDOWN_AUDIBLE_START_SECONDS, unit: "s" },
    trailing_complete_cycle_count: 4,
  };
  scenario.mode.external_resisting_torque = {
    value_dimension: "torque",
    interpolation: "right_continuous_hold",
    points: [
      { time: { value: 0, unit: "s" }, value: { value: 0, unit: "N*m" } },
      {
        time: { value: KEYOFF_TIME_SECONDS, unit: "s" },
        value: { value: resistanceNm, unit: "N*m" },
      },
    ],
  };
  scenario.events = [{
    id: "key-off",
    time: { value: KEYOFF_TIME_SECONDS, unit: "s" },
    payload: {
      type: "operating_state_patch",
      ignition_enabled: false,
      fuel_enabled: false,
    },
  }];
  scenario.audible_start = { value: SHUTDOWN_AUDIBLE_START_SECONDS, unit: "s" };
  scenario.audible_duration = {
    value: scenario.total_duration.value - SHUTDOWN_AUDIBLE_START_SECONDS,
    unit: "s",
  };
  void engine;
  return scenario;
}

function elevatedShutdownScenario() {
  const initialRpm = ENGINE_ENTRY.lifecycle?.elevated_shutdown_rpm ??
    Math.round(ENGINE_ENTRY.rpm_domain.maximum_rpm * 0.65);
  const preparationSeconds = Math.max(
    0.1,
    Math.ceil(((6 * 120) / initialRpm) / 0.02) * 0.02,
  );
  const minimumRunningLeadSeconds =
    Math.ceil((120 / initialRpm) / 0.02) * 0.02;
  const keyoffSeconds = Math.max(
    ENGINE_ENTRY.lifecycle?.elevated_keyoff_seconds ?? 0.16,
    preparationSeconds + minimumRunningLeadSeconds,
  );
  const totalSeconds = Math.max(
    5,
    Math.ceil((keyoffSeconds + 3) / 0.02) * 0.02,
  );
  const scenario = makeScenarioBase(
    "lifecycle-shutdown-high-rpm-candidate",
    totalSeconds,
  );
  scenario.initial_state.engine_speed.value = initialRpm;
  scenario.initial_state.ignition_enabled = true;
  scenario.initial_state.fuel_enabled = true;
  scenario.initial_state.starter_enabled = false;
  scenario.preparation = {
    type: "fixed_horizon",
    preparation_duration: { value: preparationSeconds, unit: "s" },
    trailing_complete_cycle_count: 1,
  };
  scenario.mode.throttle_01.points[0].value = 0.5;
  scenario.mode.external_resisting_torque = {
    value_dimension: "torque",
    interpolation: "right_continuous_hold",
    points: [
      { time: { value: 0, unit: "s" }, value: { value: 0, unit: "N*m" } },
    ],
  };
  scenario.events = [{
    id: "key-off",
    time: { value: keyoffSeconds, unit: "s" },
    payload: {
      type: "operating_state_patch",
      ignition_enabled: false,
    },
  }];
  scenario.audible_start = { value: preparationSeconds, unit: "s" };
  scenario.audible_duration = {
    value: totalSeconds - preparationSeconds,
    unit: "s",
  };
  return scenario;
}

function chooseDynamicRelease(probe, runningFloorRpm) {
  const ignitionFrame = eventFrame(probe, IGNITION_TIME_SECONDS);
  const firstFire = probe.points.find((point) =>
    point.source_frame >= ignitionFrame &&
    point.ignition_enabled && point.fuel_enabled &&
    point.indicated_gas_availability !== 0 &&
    point.indicated_gas_torque_nm > 0
  );
  if (!firstFire) fail(`${ENGINE_ID} startup probe never fires`);
  const floor = probe.points.find((point) =>
    point.source_frame >= firstFire.source_frame && Math.abs(point.rpm) >= runningFloorRpm
  );
  if (!floor) fail(`${ENGINE_ID} startup probe never reaches ${runningFloorRpm} rpm`);
  const releaseCycle = probe.cycles.find((cycle) =>
    cycle.start_frame >= floor.source_frame && cycle.end_frame < probe.pcm.length
  );
  if (!releaseCycle) fail(`${ENGINE_ID} startup probe has no post-floor cycle`);
  return {
    seconds: Math.ceil(
      releaseCycle.end_frame / SAMPLE_RATE_HZ * PHYSICS_RATE_HZ,
    ) / PHYSICS_RATE_HZ,
    firstFire,
    floor,
    releaseCycle,
  };
}

function derivePreviewScenario(source, role) {
  const scenario = structuredClone(source);
  assert.equal(scenario.schema, "engine-sim-offline/scenario");
  assert.equal(scenario.engine, ENGINE_ID);
  assert.equal(scenario.rates.source_processing.numerator, "192000");
  assert.equal(scenario.rates.acoustics.numerator, "192000");
  assert.equal(scenario.rates.delivery.numerator, "192000");
  scenario.id = `${scenario.id}-10khz-lifecycle-preview`;
  scenario.rates.physics.numerator = String(PHYSICS_RATE_HZ);
  scenario.rates.capture.numerator = String(PHYSICS_RATE_HZ);
  assert.deepEqual(scenario.output.buses, [
    "master-engine-raw",
    "master-engine-audition",
  ]);
  void role;
  return scenario;
}

function concatenate(chunks, totalLength) {
  const output = new Float32Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    output.set(chunk, offset);
    offset += chunk.length;
  }
  assert.equal(offset, totalLength);
  return output;
}

function sourceFrameForProcess(process, firstAudibleFrame, frameCount) {
  const end = Number(BigInt(process.firstDeliveryFrame)) + process.deliveryFrameCount;
  return Math.max(0, Math.min(frameCount - 1, end - Number(firstAudibleFrame) - 1));
}

function normalizeCycle(cycle, firstAudibleFrame, frameCount) {
  const startFrame = Math.round(
    cycle.startBoundary.deliveryFrame - Number(firstAudibleFrame),
  );
  const endFrame = Math.round(
    cycle.endBoundary.deliveryFrame - Number(firstAudibleFrame),
  );
  if (startFrame < 0 || endFrame > frameCount || endFrame <= startFrame) {
    return null;
  }
  return {
    start_frame: startFrame,
    end_frame: endFrame,
    mean_rpm: cycle.meanEngineSpeedRpm,
  };
}

async function capturePerformance(client, engineText, assets, item, stageRoot) {
  const canonicalPath = item.canonicalPath ?? path.join(SCENARIO_ROOT, item.file);
  const canonical = item.scenario ?? parseJson(canonicalPath);
  const scenario = derivePreviewScenario(canonical, item.role);
  if (Object.hasOwn(item, "throttle01Override")) {
    assert.equal(item.role, "startup");
    assert.equal(scenario.mode.type, "free_engine");
    assert.deepEqual(scenario.mode.throttle_01.points, [
      { time: { value: 0, unit: "s" }, value: 0.1 },
    ]);
    assert.equal(item.throttle01Override, 0);
    scenario.id = `${scenario.id}-zero-throttle-audition`;
    scenario.mode.throttle_01.points[0].value = item.throttle01Override;
  }
  const scenarioText = stableJson(scenario);
  const scenarioRelative = `source/scenarios/${item.role}-10khz.json`;
  const scenarioPath = path.join(stageRoot, scenarioRelative);
  fs.mkdirSync(path.dirname(scenarioPath), { recursive: true });
  fs.writeFileSync(scenarioPath, scenarioText);

  const program = client.compile(
    engineText,
    scenarioText,
    assets,
    1,
  );
  const chunks = [];
  const pendingTelemetry = [];
  const pendingCycles = [];
  let sampleCount = 0;
  try {
    const { session } = program;
    assert.equal(session.descriptor.physicsRateHz, PHYSICS_RATE_HZ);
    assert.equal(session.descriptor.deliveryRateHz, SAMPLE_RATE_HZ);
    assert.equal(session.auditionBus.id, "master.engine.audition");
    assert.equal(session.auditionBus.channelCount, 1);
    assert.equal(session.auditionBus.sampleRateHz, SAMPLE_RATE_HZ);
    for (;;) {
      const result = session.processBlock();
      if (result.process.kind === "completed") break;
      if (result.audible) {
        chunks.push(result.samples);
        sampleCount += result.samples.length;
        pendingTelemetry.push({
          process: result.process,
          telemetry: result.telemetry,
        });
      }
      pendingCycles.push(...result.completedCycles);
    }
    const pcm = concatenate(chunks, sampleCount);
    for (let index = 0; index < pcm.length; ++index) {
      if (!Number.isFinite(pcm[index])) {
        fail(`${item.role} PCM contains a non-finite sample at ${index}`);
      }
    }
    const firstAudibleFrame = session.firstAudibleDeliveryFrame;
    const points = pendingTelemetry.flatMap(({ process, telemetry }) =>
      telemetry.map((frame) => ({
        source_frame: sourceFrameForProcess(
          process,
          firstAudibleFrame,
          pcm.length,
        ),
        physics_step_end: Number(frame.physicsStepEnd),
        simulation_seconds: Number(frame.physicsStepEnd) / PHYSICS_RATE_HZ,
        rpm: frame.engineSpeedRpm,
        ignition_enabled: frame.ignitionEnabled,
        fuel_enabled: frame.fuelEnabled,
        starter_enabled: frame.starterEnabled,
        indicated_gas_torque_nm:
          frame.torque.instantaneousIndicatedGas.valueNm,
        indicated_gas_availability:
          frame.torque.instantaneousIndicatedGas.availability,
      })),
    );
    const cycles = pendingCycles
      .map((cycle) => normalizeCycle(cycle, firstAudibleFrame, pcm.length))
      .filter((cycle) => cycle !== null);
    return {
      role: item.role,
      canonical,
      canonicalPath,
      scenario,
      scenarioText,
      scenarioRelative,
      scenarioSha256: sha256(scenarioText),
      pcm,
      points,
      cycles,
      engineId: program.engineId,
      engineSha256: program.engineProvenanceSha256,
      rendererSha256: program.rendererSourceSha256,
    };
  } finally {
    program.dispose();
  }
}

function interpolateRpm(points, sourceFrame) {
  if (points.length === 0) fail("capture has no telemetry points");
  let upper = points.findIndex((point) => point.source_frame >= sourceFrame);
  if (upper < 0) upper = points.length - 1;
  const lower = Math.max(0, upper - 1);
  const left = points[lower];
  const right = points[upper];
  const amount = right.source_frame > left.source_frame
    ? (sourceFrame - left.source_frame) /
      (right.source_frame - left.source_frame)
    : 0;
  return left.rpm + (right.rpm - left.rpm) * Math.max(0, Math.min(1, amount));
}

function normalizedSignature(pcm, startFrame, endFrame, size = 512) {
  assert.ok(startFrame >= 0 && endFrame <= pcm.length && endFrame > startFrame);
  const signature = new Float64Array(size);
  let mean = 0;
  for (let index = 0; index < size; ++index) {
    const position = startFrame + (endFrame - startFrame - 1) * index /
      Math.max(1, size - 1);
    const left = Math.floor(position);
    const right = Math.min(endFrame - 1, left + 1);
    const amount = position - left;
    const value = pcm[left] + (pcm[right] - pcm[left]) * amount;
    signature[index] = value;
    mean += value;
  }
  mean /= size;
  let energy = 0;
  for (let index = 0; index < size; ++index) {
    signature[index] -= mean;
    energy += signature[index] * signature[index];
  }
  if (energy <= 1e-20) return null;
  const scale = 1 / Math.sqrt(energy);
  for (let index = 0; index < size; ++index) signature[index] *= scale;
  return signature;
}

function dot(left, right) {
  let value = 0;
  for (let index = 0; index < left.length; ++index) {
    value += left[index] * right[index];
  }
  return value;
}

function windowRms(pcm, startFrame, endFrame) {
  let energy = 0;
  for (let frame = startFrame; frame < endFrame; ++frame) {
    energy += pcm[frame] * pcm[frame];
  }
  return endFrame > startFrame
    ? Math.sqrt(energy / (endFrame - startFrame))
    : 0;
}

function chooseEventSeam(
  label,
  sourcePcm,
  sourceCycles,
  targetPcm,
  targetCycles,
) {
  let best = null;
  for (const source of sourceCycles) {
    for (const target of targetCycles) {
      const crossfadeFrames = Math.floor(Math.min(
        source.end_frame - source.start_frame,
        target.end_frame - target.start_frame,
        MAXIMUM_SEAM_FRAMES,
      ));
      if (crossfadeFrames < 2) continue;
      const sourceSignature = normalizedSignature(
        sourcePcm,
        source.start_frame,
        source.start_frame + crossfadeFrames,
      );
      const targetSignature = normalizedSignature(
        targetPcm,
        target.start_frame,
        target.start_frame + crossfadeFrames,
      );
      if (sourceSignature === null || targetSignature === null) continue;
      const correlation = dot(sourceSignature, targetSignature);
      const sourceRms = windowRms(
        sourcePcm,
        source.start_frame,
        source.start_frame + crossfadeFrames,
      );
      const targetRms = windowRms(
        targetPcm,
        target.start_frame,
        target.start_frame + crossfadeFrames,
      );
      if (sourceRms <= 1e-8 || targetRms <= 1e-8) continue;
      const levelPenalty = Math.abs(Math.log(sourceRms / targetRms));
      const rpmPenalty = Math.abs(Math.log(
        Math.max(1, source.mean_rpm) / Math.max(1, target.mean_rpm),
      ));
      const score = correlation - levelPenalty * 0.15 - rpmPenalty * 0.5;
      const choice = {
        source_frame: source.start_frame,
        crossfade_frames: crossfadeFrames,
        source_rpm: source.mean_rpm,
        target_source_frame: target.start_frame,
        target_rpm: target.mean_rpm,
        correlation: Math.max(-1, Math.min(1, correlation)),
        score,
      };
      if (
        best === null ||
        choice.score > best.score + 1e-12 ||
        (Math.abs(choice.score - best.score) <= 1e-12 &&
          choice.source_frame < best.source_frame)
      ) {
        best = choice;
      }
    }
  }
  if (best === null) fail(`${label} has no finite audible seam pair`);
  delete best.score;
  return best;
}

function choosePreEventSeam(
  label,
  sourcePcm,
  sourcePoints,
  eventFrame,
  targetPcm,
  targetCycles,
) {
  const sourceFrame = eventFrame - EVENT_LEAD_FRAMES;
  if (
    sourceFrame < 0 ||
    sourceFrame + MAXIMUM_SEAM_FRAMES > eventFrame ||
    sourceFrame + MAXIMUM_SEAM_FRAMES > sourcePcm.length
  ) {
    fail(`${label} lacks its 170 ms pre-event seam window`);
  }
  const sourceCycle = {
    start_frame: sourceFrame,
    end_frame: sourceFrame + MAXIMUM_SEAM_FRAMES,
    mean_rpm: interpolateRpm(sourcePoints, sourceFrame),
  };
  const targets = targetCycles
    .filter((cycle) => cycle.end_frame - cycle.start_frame >= MAXIMUM_SEAM_FRAMES)
    .map((cycle) => ({
      ...cycle,
      end_frame: cycle.start_frame + MAXIMUM_SEAM_FRAMES,
    }));
  return chooseEventSeam(
    label,
    sourcePcm,
    [sourceCycle],
    targetPcm,
    targets,
  );
}

function eventTime(scenario, id) {
  const event = scenario.events.find((candidate) => candidate.id === id);
  if (!event) fail(`scenario ${scenario.id} has no ${id} event`);
  assert.equal(event.time.unit, "s");
  return event.time.value;
}

function eventFrame(capture, seconds) {
  const audibleStart = capture.scenario.audible_start.value;
  return Math.round((seconds - audibleStart) * SAMPLE_RATE_HZ);
}

function checkpoint(kind, frame, points, precision, method) {
  return {
    kind,
    frame,
    rpm: interpolateRpm(points, frame),
    precision,
    method,
  };
}

function makeStarter(capture) {
  const stableStart = Math.min(
    capture.pcm.length - 2,
    Math.max(
      1,
      Math.round(Math.max(0.3 * SAMPLE_RATE_HZ, capture.pcm.length * 0.3)),
    ),
  );
  const stableCycles = capture.cycles.filter(
    (cycle) => cycle.start_frame >= stableStart &&
      cycle.end_frame <= capture.pcm.length,
  );
  if (stableCycles.length < 2) {
    fail("starter capture has fewer than two complete post-settle crank cycles");
  }
  const loopStartFrame = stableCycles[0].start_frame;
  const loopEndFrame = stableCycles[stableCycles.length - 1].end_frame;
  const crossfadeFrames = Math.max(1, Math.min(
    stableCycles[0].end_frame - stableCycles[0].start_frame,
    stableCycles[stableCycles.length - 1].end_frame -
      stableCycles[stableCycles.length - 1].start_frame,
    Math.floor((loopEndFrame - loopStartFrame - 1) / 2),
  ));
  const meanCrankRpm = stableCycles.length * FOUR_STROKE_CYCLE_REVOLUTIONS *
    60 * SAMPLE_RATE_HZ / (loopEndFrame - loopStartFrame);
  return {
    stableCycles,
    manifest: {
      artifact: null,
      mean_crank_rpm: meanCrankRpm,
      reference_rpm: meanCrankRpm,
      loop_start_frame: loopStartFrame,
      loop_end_frame: loopEndFrame,
      crossfade_frames: crossfadeFrames,
      attack_fade_frames: PRESENTATION_FADE_FRAMES,
      release_fade_frames: PRESENTATION_FADE_FRAMES,
    },
  };
}

function makeStartup(capture, starterCapture, starter) {
  const ignitionSeconds = eventTime(capture.scenario, "ignition-on");
  const releaseSeconds = eventTime(capture.scenario, "starter-release");
  const ignitionFrame = eventFrame(capture, ignitionSeconds);
  const releaseFrame = eventFrame(capture, releaseSeconds);
  const firstCombustionPoint = capture.points.find((point) =>
    point.source_frame >= ignitionFrame &&
    point.ignition_enabled &&
    point.fuel_enabled &&
    point.indicated_gas_availability !== 0 &&
    point.indicated_gas_torque_nm > 0,
  );
  if (!firstCombustionPoint) {
    fail("startup capture exposes no positive indicated-gas torque after ignition");
  }
  const firstCombustionFrame = firstCombustionPoint.source_frame;
  const entry = choosePreEventSeam(
    "startup entry",
    capture.pcm,
    capture.points,
    firstCombustionFrame,
    starterCapture.pcm,
    starter.stableCycles,
  );
  entry.target = "starter";
  entry.target_reference = "starter-loop";

  const stableTailStart = releaseFrame + SAMPLE_RATE_HZ;
  const sourceCandidates = capture.cycles.filter(
    (cycle) => cycle.start_frame >= stableTailStart &&
      cycle.end_frame <= capture.pcm.length,
  );
  // The canonical catch deliberately holds 10% throttle and stabilizes near
  // 2,380 RPM, not at the shutdown procedure's 700-RPM idle. The responsive
  // running atlas is selected at the live handoff RPM. With no contiguous
  // final-master atlas tape available, use neighboring post-release cycles
  // from this performance as the honest same-RPM seam reference.
  const targetCandidates = sourceCandidates;
  const exit = chooseEventSeam(
    "startup exit",
    capture.pcm,
    sourceCandidates,
    capture.pcm,
    targetCandidates,
  );
  exit.target = "running";
  exit.target_reference = "startup-post-release-cycle-reference";
  const checkpoints = [
    checkpoint(
      "ignition-on",
      ignitionFrame,
      capture.points,
      "exact",
      "authored-operating-state-event",
    ),
    checkpoint(
      "first-combustion",
      firstCombustionFrame,
      capture.points,
      "advance-block",
      "first-positive-indicated-gas-torque-proxy",
    ),
    checkpoint(
      "starter-release",
      releaseFrame,
      capture.points,
      "exact",
      "authored-operating-state-event",
    ),
    checkpoint(
      "running-floor",
      exit.source_frame,
      capture.points,
      "cycle-boundary",
      "post-release-cycle-correlated-to-captured-running-idle",
    ),
  ];
  return {
    artifact: null,
    checkpoints,
    entry,
    exit,
  };
}

function makeShutdown(capture) {
  const ignitionSeconds = eventTime(capture.scenario, "key-off");
  const ignitionFrame = eventFrame(capture, ignitionSeconds);
  const runningTargets = capture.cycles.filter(
    (cycle) => cycle.start_frame >= 0 && cycle.end_frame <= ignitionFrame,
  );
  const entry = choosePreEventSeam(
    "shutdown entry",
    capture.pcm,
    capture.points,
    ignitionFrame,
    capture.pcm,
    runningTargets,
  );
  entry.target = "running";
  entry.target_reference = "shutdown-pre-keyoff-cycle-reference";
  const stoppedPoint = capture.points.find((point) =>
    point.source_frame >= ignitionFrame &&
    !point.ignition_enabled &&
    !point.fuel_enabled &&
    Math.abs(point.rpm) <= 1,
  );
  if (!stoppedPoint) fail("shutdown capture does not reach <= 1 RPM");
  const stoppedFrame = stoppedPoint.source_frame;
  const preEventRms = windowRms(capture.pcm, entry.source_frame, ignitionFrame);
  const quietPeakThreshold = Math.max(4 / 32_768, preEventRms * 0.01);
  const quietRmsThreshold = Math.max(2 / 32_768, preEventRms * 0.0032);
  let lastAboveQuiet = stoppedFrame - 1;
  for (let frame = stoppedFrame; frame < capture.pcm.length; ++frame) {
    if (Math.abs(capture.pcm[frame]) > quietPeakThreshold) {
      lastAboveQuiet = frame;
    }
  }
  const silenceFrame = Math.max(stoppedFrame, lastAboveQuiet + 1);
  if (capture.pcm.length - silenceFrame < QUIET_FRAMES) {
    fail(
      `shutdown retains ${capture.pcm.length - silenceFrame} verified-quiet ` +
      `frames; ${QUIET_FRAMES} required`,
    );
  }
  if (
    windowRms(capture.pcm, silenceFrame, capture.pcm.length) >
    quietRmsThreshold
  ) {
    fail("shutdown post-roll exceeds its relative quiet-RMS threshold");
  }
  return {
    artifact: null,
    checkpoints: [
      checkpoint(
        "settled-idle",
        entry.source_frame,
        capture.points,
        "cycle-window",
        "pre-key-off-window-correlated-to-captured-running-idle",
      ),
      checkpoint(
        "ignition-off",
        ignitionFrame,
        capture.points,
        "exact",
        "authored-operating-state-event",
      ),
      checkpoint(
        "engine-stopped",
        stoppedFrame,
        capture.points,
        "advance-block",
        "first-observed-absolute-engine-speed-at-or-below-1-rpm",
      ),
    ],
    entry,
    silence_frame: silenceFrame,
    exit_fade_frames: PRESENTATION_FADE_FRAMES,
    quiet_tail_frames: capture.pcm.length - silenceFrame,
    quiet_peak_threshold: quietPeakThreshold,
    quiet_rms_threshold: quietRmsThreshold,
  };
}

function makeElevatedShutdown(capture) {
  const ignitionSeconds = eventTime(capture.scenario, "key-off");
  const ignitionFrame = eventFrame(capture, ignitionSeconds);
  const runningCycles = capture.cycles.filter(
    (cycle) => cycle.start_frame >= 0 && cycle.end_frame <= ignitionFrame,
  );
  if (runningCycles.length === 0) {
    fail("elevated shutdown has no complete pre-keyoff running cycle");
  }
  const entryCycle = runningCycles[0];
  const entry = {
    source_frame: entryCycle.start_frame,
    crossfade_frames: Math.max(1, Math.min(
      entryCycle.end_frame - entryCycle.start_frame,
      PRESENTATION_FADE_FRAMES,
    )),
    source_rpm: entryCycle.mean_rpm,
    target_source_frame: entryCycle.start_frame,
    target_rpm: entryCycle.mean_rpm,
    correlation: 1,
    target: "running",
    target_reference: "shutdown-high-rpm-pre-keyoff-cycle-reference",
  };
  const stoppedPoint = capture.points.find((point) =>
    point.source_frame >= ignitionFrame &&
    !point.ignition_enabled &&
    point.fuel_enabled &&
    Math.abs(point.rpm) <= 1,
  );
  if (!stoppedPoint) fail("elevated shutdown does not reach <= 1 RPM");
  const stoppedFrame = stoppedPoint.source_frame;
  const preEventRms = windowRms(capture.pcm, entry.source_frame, ignitionFrame);
  const quietPeakThreshold = Math.max(4 / 32_768, preEventRms * 0.01);
  const quietRmsThreshold = Math.max(2 / 32_768, preEventRms * 0.0032);
  let lastAboveQuiet = stoppedFrame - 1;
  for (let frame = stoppedFrame; frame < capture.pcm.length; ++frame) {
    if (Math.abs(capture.pcm[frame]) > quietPeakThreshold) lastAboveQuiet = frame;
  }
  const silenceFrame = Math.max(stoppedFrame, lastAboveQuiet + 1);
  if (capture.pcm.length - silenceFrame < QUIET_FRAMES) {
    fail(
      `elevated shutdown retains ${capture.pcm.length - silenceFrame} ` +
      `verified-quiet frames; ${QUIET_FRAMES} required`,
    );
  }
  if (
    windowRms(capture.pcm, silenceFrame, capture.pcm.length) >
      quietRmsThreshold
  ) {
    fail("elevated shutdown post-roll exceeds its relative quiet-RMS threshold");
  }
  return {
    artifact: null,
    checkpoints: [
      checkpoint(
        "settled-running",
        entry.source_frame,
        capture.points,
        "cycle-window",
        "captured-high-rpm-pre-key-off-cycle",
      ),
      checkpoint(
        "ignition-off",
        ignitionFrame,
        capture.points,
        "exact",
        "authored-operating-state-event",
      ),
      checkpoint(
        "engine-stopped",
        stoppedFrame,
        capture.points,
        "advance-block",
        "first-observed-absolute-engine-speed-at-or-below-1-rpm",
      ),
    ],
    entry,
    silence_frame: silenceFrame,
    exit_fade_frames: PRESENTATION_FADE_FRAMES,
    quiet_tail_frames: capture.pcm.length - silenceFrame,
    quiet_peak_threshold: quietPeakThreshold,
    quiet_rms_threshold: quietRmsThreshold,
  };
}

function writeArtifact(stageRoot, capture) {
  const relativePath = `audio/${capture.role}.master-engine-audition.f32le`;
  const outputPath = path.join(stageRoot, relativePath);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const bytes = Buffer.from(
    capture.pcm.buffer,
    capture.pcm.byteOffset,
    capture.pcm.byteLength,
  );
  fs.writeFileSync(outputPath, bytes);
  return {
    path: relativePath,
    sha256: sha256(bytes),
    frame_count: capture.pcm.length,
  };
}

function writeEvidence(stageRoot, capture) {
  const relativePath = `evidence/${capture.role}.json`;
  const evidence = {
    schema: "engine-sim-offline/lifecycle-capture-evidence",
    role: capture.role,
    physics_rate_hz: PHYSICS_RATE_HZ,
    delivery_rate_hz: SAMPLE_RATE_HZ,
    scenario_id: capture.scenario.id,
    points: capture.points,
    completed_cycles: capture.cycles,
  };
  const bytes = stableJson(evidence);
  const outputPath = path.join(stageRoot, relativePath);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, bytes);
  return { path: relativePath, sha256: sha256(bytes) };
}

function validateArtifact(root, artifact, label) {
  exactKeys(artifact, ["path", "sha256", "frame_count"], label);
  if (
    typeof artifact.path !== "string" ||
    artifact.path.startsWith("/") ||
    artifact.path.split("/").includes("..")
  ) {
    fail(`${label}.path is not a package-relative path`);
  }
  requireHash(artifact.sha256, `${label}.sha256`);
  requireInteger(artifact.frame_count, `${label}.frame_count`, 2);
  const bytes = fs.readFileSync(path.join(root, artifact.path));
  assert.equal(bytes.byteLength, artifact.frame_count * 4, `${label} byte length`);
  assert.equal(sha256(bytes), artifact.sha256, `${label} digest`);
  for (let offset = 0; offset < bytes.length; offset += 4) {
    if (!Number.isFinite(bytes.readFloatLE(offset))) {
      fail(`${label} contains non-finite PCM at byte ${offset}`);
    }
  }
}

function validateCheckpoint(checkpointValue, frameCount, label) {
  exactKeys(
    checkpointValue,
    ["kind", "frame", "rpm", "precision", "method"],
    label,
  );
  requireInteger(checkpointValue.frame, `${label}.frame`);
  if (checkpointValue.frame >= frameCount) fail(`${label}.frame is outside PCM`);
  requireFinite(checkpointValue.rpm, `${label}.rpm`);
  if (checkpointValue.rpm < 0) fail(`${label}.rpm is negative`);
  if (typeof checkpointValue.precision !== "string" ||
      typeof checkpointValue.method !== "string") {
    fail(`${label} precision/method must be strings`);
  }
}

function validateSeam(seam, sourceFrames, targetFrames, label) {
  exactKeys(
    seam,
    [
      "source_frame",
      "crossfade_frames",
      "source_rpm",
      "target_source_frame",
      "target_rpm",
      "correlation",
      "target",
      "target_reference",
    ],
    label,
  );
  requireInteger(seam.source_frame, `${label}.source_frame`);
  requireInteger(seam.crossfade_frames, `${label}.crossfade_frames`, 1);
  requireInteger(seam.target_source_frame, `${label}.target_source_frame`);
  if (seam.source_frame + seam.crossfade_frames > sourceFrames) {
    fail(`${label} exceeds source PCM`);
  }
  if (seam.target_source_frame + seam.crossfade_frames > targetFrames) {
    fail(`${label} exceeds target reference PCM`);
  }
  for (const field of ["source_rpm", "target_rpm", "correlation"]) {
    requireFinite(seam[field], `${label}.${field}`);
  }
  if (seam.correlation < -1 || seam.correlation > 1) {
    fail(`${label}.correlation is outside [-1, 1]`);
  }
}

function validateManifest(stageRoot, manifest, expected) {
  const hasElevatedShutdown = Object.hasOwn(manifest, "shutdown_elevated");
  exactKeys(
    manifest,
    [
      "schema",
      "id",
      "engine",
      "audio",
      "starter",
      "startup",
      "shutdown",
      "startup_admission",
      "provenance",
      ...(hasElevatedShutdown ? ["shutdown_elevated"] : []),
    ],
    "manifest",
  );
  assert.equal(manifest.schema, "engine-sim-offline/responsive-audio-lifecycle");
  assert.equal(manifest.id, `${ENGINE_ID}-lifecycle-preview`);
  assert.equal(manifest.engine, ENGINE_ID);
  assert.deepEqual(manifest.audio, {
    sample_rate_hz: SAMPLE_RATE_HZ,
    encoding: "float32le",
    channel_layout: "mono",
    bus_id: "master.engine.audition",
  });
  validateArtifact(stageRoot, manifest.starter.artifact, "starter.artifact");
  validateArtifact(stageRoot, manifest.startup.artifact, "startup.artifact");
  validateArtifact(stageRoot, manifest.shutdown.artifact, "shutdown.artifact");
  const starterFrames = manifest.starter.artifact.frame_count;
  for (const field of [
    "loop_start_frame",
    "loop_end_frame",
    "crossfade_frames",
    "attack_fade_frames",
    "release_fade_frames",
  ]) {
    requireInteger(manifest.starter[field], `starter.${field}`, 1);
  }
  requireFinite(manifest.starter.mean_crank_rpm, "starter.mean_crank_rpm");
  requireFinite(manifest.starter.reference_rpm, "starter.reference_rpm");
  if (
    manifest.starter.loop_start_frame >= manifest.starter.loop_end_frame ||
    manifest.starter.loop_end_frame > starterFrames ||
    manifest.starter.crossfade_frames * 2 >
      manifest.starter.loop_end_frame - manifest.starter.loop_start_frame
  ) {
    fail("starter loop geometry is invalid");
  }
  const startupFrames = manifest.startup.artifact.frame_count;
  const startupKinds = [
    "ignition-on",
    "first-combustion",
    "starter-release",
    "running-floor",
  ];
  assert.deepEqual(manifest.startup.checkpoints.map(({ kind }) => kind), startupKinds);
  manifest.startup.checkpoints.forEach((value, index) =>
    validateCheckpoint(value, startupFrames, `startup.checkpoints[${index}]`),
  );
  validateSeam(
    manifest.startup.entry,
    startupFrames,
    starterFrames,
    "startup.entry",
  );
  validateSeam(
    manifest.startup.exit,
    startupFrames,
    startupFrames,
    "startup.exit",
  );
  const startupByKind = new Map(
    manifest.startup.checkpoints.map((value) => [value.kind, value]),
  );
  if (
    manifest.startup.entry.source_frame +
      manifest.startup.entry.crossfade_frames >
      startupByKind.get("ignition-on").frame ||
    startupByKind.get("ignition-on").frame >
      startupByKind.get("first-combustion").frame ||
    startupByKind.get("first-combustion").frame >
      startupByKind.get("starter-release").frame ||
    startupByKind.get("starter-release").frame >
      startupByKind.get("running-floor").frame ||
    startupByKind.get("running-floor").frame !==
      manifest.startup.exit.source_frame
  ) {
    fail("startup checkpoints/seams are not ordered");
  }
  const shutdownFrames = manifest.shutdown.artifact.frame_count;
  const shutdownKinds = ["settled-idle", "ignition-off", "engine-stopped"];
  assert.deepEqual(
    manifest.shutdown.checkpoints.map(({ kind }) => kind),
    shutdownKinds,
  );
  manifest.shutdown.checkpoints.forEach((value, index) =>
    validateCheckpoint(value, shutdownFrames, `shutdown.checkpoints[${index}]`),
  );
  validateSeam(
    manifest.shutdown.entry,
    shutdownFrames,
    shutdownFrames,
    "shutdown.entry",
  );
  const shutdownByKind = new Map(
    manifest.shutdown.checkpoints.map((value) => [value.kind, value]),
  );
  requireInteger(manifest.shutdown.silence_frame, "shutdown.silence_frame");
  requireInteger(manifest.shutdown.exit_fade_frames, "shutdown.exit_fade_frames", 1);
  requireInteger(manifest.shutdown.quiet_tail_frames, "shutdown.quiet_tail_frames");
  requireFinite(manifest.shutdown.quiet_peak_threshold, "shutdown.quiet_peak_threshold");
  requireFinite(manifest.shutdown.quiet_rms_threshold, "shutdown.quiet_rms_threshold");
  if (
    shutdownByKind.get("settled-idle").frame !==
      manifest.shutdown.entry.source_frame ||
    manifest.shutdown.entry.source_frame +
      manifest.shutdown.entry.crossfade_frames >
      shutdownByKind.get("ignition-off").frame ||
    shutdownByKind.get("ignition-off").frame >
      shutdownByKind.get("engine-stopped").frame ||
    shutdownByKind.get("engine-stopped").frame >
      manifest.shutdown.silence_frame ||
    manifest.shutdown.silence_frame >= shutdownFrames ||
    shutdownFrames - manifest.shutdown.silence_frame < QUIET_FRAMES ||
    manifest.shutdown.quiet_tail_frames !==
      shutdownFrames - manifest.shutdown.silence_frame
  ) {
    fail("shutdown checkpoints/seam/quiet tail are invalid");
  }
  if (hasElevatedShutdown) {
    const elevated = manifest.shutdown_elevated;
    validateArtifact(
      stageRoot,
      elevated.artifact,
      "shutdown_elevated.artifact",
    );
    const elevatedFrames = elevated.artifact.frame_count;
    const elevatedKinds = [
      "settled-running",
      "ignition-off",
      "engine-stopped",
    ];
    assert.deepEqual(
      elevated.checkpoints.map(({ kind }) => kind),
      elevatedKinds,
    );
    elevated.checkpoints.forEach((value, index) =>
      validateCheckpoint(
        value,
        elevatedFrames,
        `shutdown_elevated.checkpoints[${index}]`,
      ),
    );
    validateSeam(
      elevated.entry,
      elevatedFrames,
      elevatedFrames,
      "shutdown_elevated.entry",
    );
    const elevatedByKind = new Map(
      elevated.checkpoints.map((value) => [value.kind, value]),
    );
    requireInteger(
      elevated.silence_frame,
      "shutdown_elevated.silence_frame",
    );
    requireInteger(
      elevated.exit_fade_frames,
      "shutdown_elevated.exit_fade_frames",
      1,
    );
    requireInteger(
      elevated.quiet_tail_frames,
      "shutdown_elevated.quiet_tail_frames",
    );
    requireFinite(
      elevated.quiet_peak_threshold,
      "shutdown_elevated.quiet_peak_threshold",
    );
    requireFinite(
      elevated.quiet_rms_threshold,
      "shutdown_elevated.quiet_rms_threshold",
    );
    if (
      elevatedByKind.get("settled-running").frame !==
        elevated.entry.source_frame ||
      elevated.entry.source_frame + elevated.entry.crossfade_frames >
        elevatedByKind.get("ignition-off").frame ||
      elevatedByKind.get("ignition-off").frame >
        elevatedByKind.get("engine-stopped").frame ||
      elevatedByKind.get("engine-stopped").frame > elevated.silence_frame ||
      elevated.silence_frame >= elevatedFrames ||
      elevatedFrames - elevated.silence_frame < QUIET_FRAMES ||
      elevated.quiet_tail_frames !== elevatedFrames - elevated.silence_frame
    ) {
      fail("elevated shutdown checkpoints/seam/quiet tail are invalid");
    }
  }
  assert.equal(manifest.provenance.physics_rate_hz, PHYSICS_RATE_HZ);
  assert.equal(manifest.provenance.delivery_rate_hz, SAMPLE_RATE_HZ);
  assert.equal(manifest.provenance.engine.sha256, expected.engineSha256);
  assert.equal(manifest.provenance.renderer_build.sha256, expected.rendererSha256);
  for (const scenario of manifest.provenance.scenarios) {
    requireHash(scenario.sha256, `scenario ${scenario.role} digest`);
    const bytes = fs.readFileSync(path.join(stageRoot, scenario.path));
    assert.equal(sha256(bytes), scenario.sha256, `scenario ${scenario.role} digest`);
    const parsed = JSON.parse(bytes);
    assert.equal(parsed.rates.physics.numerator, String(PHYSICS_RATE_HZ));
    assert.equal(parsed.rates.capture.numerator, String(PHYSICS_RATE_HZ));
    assert.equal(parsed.rates.delivery.numerator, String(SAMPLE_RATE_HZ));
  }
}

async function main() {
  if (os.endianness() !== "LE") fail("float32le publisher requires a little-endian host");
  if (fs.existsSync(FINAL_PACKAGE) || fs.existsSync(CANDIDATE_ROOT)) {
    fail(`refusing to replace existing lifecycle candidate for ${ENGINE_ID}`);
  }
  for (const required of [MODULE_PATH, ENGINE_PATH, RESPONSIVE_RUNTIME, HELD_MANIFEST_PATH]) {
    if (!fs.statSync(required).isFile()) fail(`required file missing: ${required}`);
  }
  const responsive = parseJson(RESPONSIVE_RUNTIME);
  if (Object.hasOwn(responsive, "lifecycle_package_path")) {
    fail("responsive root already declares lifecycle_package_path");
  }
  assert.equal(responsive.fidelity.physics_rate_hz, PHYSICS_RATE_HZ);
  assert.equal(responsive.audio.sample_rate_hz, SAMPLE_RATE_HZ);

  const runRoot = temporaryRunRoot("build-");
  const stageRoot = path.join(runRoot, "lifecycle");
  fs.mkdirSync(stageRoot, { recursive: true });
  const heldBytes = fs.readFileSync(HELD_MANIFEST_PATH);
  const held = JSON.parse(heldBytes);
  const heldFloorRpm = held.domain.rpm_anchors[0];
  const runningFloorRpm = responsive.domain.minimum_rpm;
  const engineDocument = parseJson(ENGINE_PATH);
  const resistanceNm = outputCrankInertia(engineDocument) *
    heldFloorRpm * 2 * Math.PI / 60 / KEYOFF_DECELERATION_SECONDS;
  const { EngineSimCapiClient } = await import(pathToFileURL(
    path.join(MODULE_ROOT, "runtime/c-api-client.js"),
  ));
  const client = await EngineSimCapiClient.create(pathToFileURL(MODULE_PATH));
  const engineText = readText(ENGINE_PATH);
  const assets = ENGINE_ENTRY.assets.audio.map(({ id, path: relative }) => ({
    kind: "audio",
    id,
    bytes: fs.readFileSync(inventoryPath(relative, `audio asset ${id}`)),
  }));
  const accessory = ENGINE_ENTRY.assets.accessory_configuration;
  if (accessory?.path) {
    assets.push({
      kind: "accessory-configuration",
      id: accessory.reference_id,
      bytes: fs.readFileSync(inventoryPath(
        accessory.path,
        `accessory asset ${accessory.reference_id}`,
      )),
    });
  }
  let starterCapture;
  let startupCapture;
  let shutdownCapture;
  let release;
  try {
    const canonicalPath = templatePath();
    process.stderr.write(`${ENGINE_ID}: capturing starter\n`);
    starterCapture = await capturePerformance(client, engineText, assets, {
      role: "starter", scenario: starterScenario(), canonicalPath,
    }, stageRoot);
    process.stderr.write(`${ENGINE_ID}: probing dynamic starter release\n`);
    const probe = await capturePerformance(client, engineText, assets, {
      role: "startup-probe", scenario: startupScenario(), canonicalPath,
    }, stageRoot);
    release = chooseDynamicRelease(probe, runningFloorRpm);
    process.stderr.write(`${ENGINE_ID}: capturing startup; release=${release.seconds.toFixed(6)}s\n`);
    startupCapture = await capturePerformance(client, engineText, assets, {
      role: "startup", scenario: startupScenario(release.seconds), canonicalPath,
    }, stageRoot);
    process.stderr.write(`${ENGINE_ID}: capturing shutdown; resistance=${resistanceNm.toFixed(3)}Nm\n`);
    shutdownCapture = await capturePerformance(client, engineText, assets, {
      role: "shutdown",
      scenario: shutdownScenario(heldFloorRpm, resistanceNm, engineDocument),
      canonicalPath,
    }, stageRoot);
  } finally {
    client.dispose();
  }
  const captures = [starterCapture, startupCapture, shutdownCapture];
  for (const capture of captures) {
    assert.equal(capture.engineId, ENGINE_ID);
    assert.equal(capture.engineSha256, captures[0].engineSha256);
    assert.equal(capture.rendererSha256, captures[0].rendererSha256);
    capture.artifact = writeArtifact(stageRoot, capture);
    capture.evidence = writeEvidence(stageRoot, capture);
  }
  assert.equal(
    captures[0].engineSha256,
    responsive.provenance.engine.sha256,
    "lifecycle engine provenance differs from accepted running package",
  );
  assert.equal(
    captures[0].rendererSha256,
    responsive.provenance.renderer_build.sha256,
    "lifecycle renderer provenance differs from accepted running package",
  );

  const starter = makeStarter(starterCapture);
  starter.manifest.artifact = starterCapture.artifact;
  const startup = makeStartup(startupCapture, starterCapture, starter);
  startup.artifact = startupCapture.artifact;
  const shutdown = makeShutdown(shutdownCapture);
  shutdown.artifact = shutdownCapture.artifact;

  const loadCoordinate = held.representation.runtime_coordinates[1];
  const admissionEvidence = {
    schema: "engine-sim-offline/startup-admission-floor-evidence",
    candidate_status: "generic-variant-3-audition-not-per-engine-fitted",
    atlas_manifest: "../../held/package.json",
    atlas_manifest_sha256: sha256(heldBytes),
    atlas_load_coordinate: loadCoordinate,
    running_floor_rpm: runningFloorRpm,
    held_anchor_floor_rpm: heldFloorRpm,
    release: {
      method: "first-complete-four-stroke-cycle-after-first-positive-combustion-and-running-floor",
      seconds: release.seconds,
      first_positive_combustion_frame: release.firstFire.source_frame,
      running_floor_frame: release.floor.source_frame,
      release_cycle: release.releaseCycle,
    },
    lanes: held.domain.load_lanes.map((lane) => ({
      id: lane.id,
      throttle_01: lane.throttle01,
      floor_running_gain_linear: ACCEPTED_VARIANT3_GAINS[lane.id],
    })),
  };
  for (const lane of admissionEvidence.lanes) {
    if (!Number.isFinite(lane.floor_running_gain_linear)) {
      fail(`${ENGINE_ID} has unsupported held lane ${lane.id}`);
    }
  }
  const admissionEvidenceText = stableJson(admissionEvidence);
  fs.writeFileSync(path.join(stageRoot, "evidence/startup-admission.json"), admissionEvidenceText);
  const startupAdmission = {
    schema: "engine-sim-offline/continuous-startup-admission-v1",
    running_bed_load_coordinate: loadCoordinate,
    admission_lane_coordinate: "authored-throttle-01",
    blend: "constant-power",
    pre_floor_progress: "smoothstep-first-fire-rpm-to-running-floor",
    completion_progress: "smoothstep-committed-crank-travel",
    completion_crank_travel_revolutions: 2,
    monotone_ownership: true,
    lanes: admissionEvidence.lanes,
    coast_stability: {
      lane_id: "coast",
      requires_starter_released: true,
      post_peak_crank_travel_revolutions: 4,
      clock_law: "lane-weighted-post-peak-admission",
    },
    evidence: {
      method: "generic accepted Variant 3 semantic-lane gains; audition candidate, not per-engine fitted",
      path: "evidence/startup-admission.json",
      sha256: sha256(admissionEvidenceText),
      corrected_held_manifest_sha256: sha256(heldBytes),
    },
  };

  const manifest = {
    schema: "engine-sim-offline/responsive-audio-lifecycle",
    id: `${ENGINE_ID}-lifecycle-preview`,
    engine: ENGINE_ID,
    audio: {
      sample_rate_hz: SAMPLE_RATE_HZ,
      encoding: "float32le",
      channel_layout: "mono",
      bus_id: "master.engine.audition",
    },
    starter: starter.manifest,
    startup,
    shutdown,
    startup_admission: startupAdmission,
    provenance: {
      engine: {
        id: captures[0].engineId,
        sha256: captures[0].engineSha256,
      },
      renderer_build: {
        id: "engine-sim-offline-renderer-build",
        sha256: captures[0].rendererSha256,
      },
      representation:
        "fresh-10khz-physics-192khz-presented-master-lifecycle-performances-with-cycle-correlated-event-seams",
      physics_rate_hz: PHYSICS_RATE_HZ,
      delivery_rate_hz: SAMPLE_RATE_HZ,
      fidelity_alignment:
        "matches-current-responsive-preview;canonical-20khz-events-are-not-mixed-into-10khz-running-package",
      checkpoint_alignment:
        "authored-control-times-are-exact-on-final-pcm;combustion-and-stop-use-one-block-c-api-telemetry-bounds;renderer-exposes-no-separate-audio-latency-field",
      seam_reference_scope:
        "same-performance-final-master-cycle-correlation;the-responsive-held-representation-exposes-no-contiguous-final-master-reference-tape",
      scenarios: captures.map((capture) => ({
        role: capture.role,
        canonical_source_id: capture.canonical.id,
        derived_id: capture.scenario.id,
        path: capture.scenarioRelative,
        sha256: capture.scenarioSha256,
        evidence_path: capture.evidence.path,
        evidence_sha256: capture.evidence.sha256,
      })),
    },
  };
  const manifestText = stableJson(manifest);
  fs.writeFileSync(path.join(stageRoot, "runtime.json"), manifestText);
  validateManifest(stageRoot, manifest, captures[0]);
  assert.deepEqual(JSON.parse(readText(path.join(stageRoot, "runtime.json"))), manifest);

  fs.mkdirSync(path.dirname(CANDIDATE_ROOT), { recursive: true });
  fs.renameSync(stageRoot, CANDIDATE_ROOT);
  fs.cpSync(CANDIDATE_ROOT, FINAL_PACKAGE, { recursive: true });
  const updatedRoot = parseJson(RESPONSIVE_RUNTIME);
  updatedRoot.lifecycle_package_path = "lifecycle/runtime.json";
  if (process.env.ESO_RESPONSIVE_BAKE_SHARED_STARTER === "1") {
    updatedRoot.shared_recorded_starter_package_path =
      "shared-recorded-starter/runtime.json";
  }
  fs.writeFileSync(RESPONSIVE_RUNTIME, stableJson(updatedRoot));
  const admittedRoot = parseJson(RESPONSIVE_RUNTIME);
  assert.equal(admittedRoot.lifecycle_package_path, "lifecycle/runtime.json");
  assert.equal(
    admittedRoot.provenance.engine.sha256,
    manifest.provenance.engine.sha256,
  );
  assert.equal(
    admittedRoot.provenance.renderer_build.sha256,
    manifest.provenance.renderer_build.sha256,
  );
  validateManifest(FINAL_PACKAGE, parseJson(path.join(FINAL_PACKAGE, "runtime.json")), captures[0]);

  process.stdout.write(`${stableJson({
    package: path.relative(REPOSITORY, FINAL_PACKAGE),
    manifest_sha256: sha256(manifestText),
    engine_sha256: captures[0].engineSha256,
    renderer_sha256: captures[0].rendererSha256,
    captures: captures.map((capture) => ({
      role: capture.role,
      frame_count: capture.pcm.length,
      seconds: capture.pcm.length / SAMPLE_RATE_HZ,
      cycles: capture.cycles.length,
      artifact_sha256: capture.artifact.sha256,
    })),
    starter: starter.manifest,
    startup_checkpoints: startup.checkpoints,
    shutdown_checkpoints: shutdown.checkpoints,
    shutdown_silence_frame: shutdown.silence_frame,
    running_floor_rpm: runningFloorRpm,
    held_anchor_floor_rpm: heldFloorRpm,
    keyoff_resistance_nm: resistanceNm,
  })}`);
}

async function captureElevatedShutdown() {
  if (os.endianness() !== "LE") {
    fail("float32le publisher requires a little-endian host");
  }
  for (const required of [
    MODULE_PATH,
    ENGINE_PATH,
    RESPONSIVE_RUNTIME,
    path.join(FINAL_PACKAGE, "runtime.json"),
  ]) {
    if (!fs.statSync(required).isFile()) fail(`required file missing: ${required}`);
  }
  const responsive = parseJson(RESPONSIVE_RUNTIME);
  const currentManifest = parseJson(path.join(FINAL_PACKAGE, "runtime.json"));
  if (Object.hasOwn(currentManifest, "shutdown_elevated")) {
    fail("lifecycle package already contains an elevated shutdown");
  }
  const runRoot = temporaryRunRoot(`${ENGINE_ID}-high-shutdown-`);
  const stageRoot = path.join(runRoot, "lifecycle");
  fs.mkdirSync(stageRoot, { recursive: true });
  const { EngineSimCapiClient } = await import(pathToFileURL(
    path.join(MODULE_ROOT, "runtime/c-api-client.js"),
  ));
  const client = await EngineSimCapiClient.create(pathToFileURL(MODULE_PATH));
  const engineText = readText(ENGINE_PATH);
  const assets = ENGINE_ENTRY.assets.audio.map(({ id, path: relative }) => ({
    kind: "audio",
    id,
    bytes: fs.readFileSync(inventoryPath(relative, `audio asset ${id}`)),
  }));
  const accessory = ENGINE_ENTRY.assets.accessory_configuration;
  if (accessory?.path) {
    assets.push({
      kind: "accessory-configuration",
      id: accessory.reference_id,
      bytes: fs.readFileSync(inventoryPath(
        accessory.path,
        `accessory asset ${accessory.reference_id}`,
      )),
    });
  }
  let capture;
  const startedAt = process.hrtime.bigint();
  try {
    capture = await capturePerformance(client, engineText, assets, {
      role: "shutdown-high-rpm",
      scenario: elevatedShutdownScenario(),
      canonicalPath: templatePath(),
    }, stageRoot);
  } finally {
    client.dispose();
  }
  const captureWallSeconds = Number(process.hrtime.bigint() - startedAt) / 1e9;
  assert.equal(capture.engineId, ENGINE_ID);
  assert.equal(capture.engineSha256, responsive.provenance.engine.sha256);
  assert.equal(capture.rendererSha256, responsive.provenance.renderer_build.sha256);
  capture.artifact = writeArtifact(stageRoot, capture);
  capture.evidence = writeEvidence(stageRoot, capture);
  const shutdownElevated = makeElevatedShutdown(capture);
  shutdownElevated.artifact = capture.artifact;
  const scenarioProvenance = {
    role: "shutdown-elevated",
    canonical_source_id: capture.canonical.id,
    derived_id: capture.scenario.id,
    path: capture.scenarioRelative,
    sha256: capture.scenarioSha256,
    evidence_path: capture.evidence.path,
    evidence_sha256: capture.evidence.sha256,
  };
  const assembledRoot = path.join(runRoot, "assembled-lifecycle");
  fs.cpSync(FINAL_PACKAGE, assembledRoot, { recursive: true });
  for (const relativePath of [
    capture.artifact.path,
    capture.evidence.path,
    capture.scenarioRelative,
  ]) {
    const destination = path.join(assembledRoot, relativePath);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.cpSync(path.join(stageRoot, relativePath), destination, {
      errorOnExist: true,
    });
  }
  const manifest = structuredClone(currentManifest);
  manifest.shutdown_elevated = shutdownElevated;
  manifest.provenance.scenarios.push(scenarioProvenance);
  const manifestText = stableJson(manifest);
  fs.writeFileSync(path.join(assembledRoot, "runtime.json"), manifestText);
  validateManifest(assembledRoot, manifest, {
    engineSha256: capture.engineSha256,
    rendererSha256: capture.rendererSha256,
  });

  const displacedRoot = path.join(runRoot, "base-lifecycle");
  fs.renameSync(FINAL_PACKAGE, displacedRoot);
  try {
    fs.renameSync(assembledRoot, FINAL_PACKAGE);
  } catch (error) {
    fs.renameSync(displacedRoot, FINAL_PACKAGE);
    throw error;
  }
  fs.rmSync(displacedRoot, { recursive: true, force: true });
  validateManifest(FINAL_PACKAGE, parseJson(path.join(FINAL_PACKAGE, "runtime.json")), {
    engineSha256: capture.engineSha256,
    rendererSha256: capture.rendererSha256,
  });
  const ignitionOff = shutdownElevated.checkpoints.find(
    ({ kind }) => kind === "ignition-off",
  );
  const stopped = shutdownElevated.checkpoints.find(
    ({ kind }) => kind === "engine-stopped",
  );
  process.stdout.write(stableJson({
    package: path.relative(REPOSITORY, FINAL_PACKAGE),
    manifest_sha256: sha256(manifestText),
    capture_wall_seconds: captureWallSeconds,
    renderer_sha256: capture.rendererSha256,
    engine_sha256: capture.engineSha256,
    ignition_off_rpm: ignitionOff.rpm,
    ignition_off_frame: ignitionOff.frame,
    engine_stopped_frame: stopped.frame,
    rundown_seconds: (stopped.frame - ignitionOff.frame) / SAMPLE_RATE_HZ,
    silence_frame: shutdownElevated.silence_frame,
    quiet_tail_frames: shutdownElevated.quiet_tail_frames,
    artifact: capture.artifact,
    evidence: capture.evidence,
    scenario: scenarioProvenance,
  }));
}

const operation = process.argv[3] === "--capture-elevated-shutdown"
  ? captureElevatedShutdown
  : main;

operation().catch((error) => {
  process.stderr.write(`Lifecycle package generation failed: ${error.stack}\n`);
  if (typeof error?.toJSON === "function") {
    process.stderr.write(`${JSON.stringify(
      error.toJSON(),
      (_key, value) => typeof value === "bigint" ? value.toString() : value,
      2,
    )}\n`);
  }
  process.exitCode = 1;
});
