import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  VehicleEngineAudioEngine,
  VehicleEngineAudioEngineError,
} from "../../runtime/vehicleengine-audio-engine.js";
import {
  HeldPhaseTextureCursor,
  loadHeldPhaseTexturePackage,
} from "../../runtime/held-phase-texture-runtime.js";

const ENTRY_MODULE = fileURLToPath(
  new URL("../../runtime/vehicleengine-audio-engine.js", import.meta.url),
);

function moduleSpecifiers(source) {
  const specifiers = new Set();
  const patterns = [
    /\bfrom\s*["']([^"']+)["']/gu,
    /\bimport\s*["']([^"']+)["']/gu,
    /\bimport\s*\(\s*["']([^"']+)["']/gu,
  ];
  for (const pattern of patterns) {
    for (const match of source.matchAll(pattern)) specifiers.add(match[1]);
  }
  return [...specifiers];
}

function staticModuleClosure(entry) {
  const pending = [entry];
  const visited = new Set();
  const edges = [];
  while (pending.length !== 0) {
    const sourcePath = pending.pop();
    if (visited.has(sourcePath)) continue;
    visited.add(sourcePath);
    const source = fs.readFileSync(sourcePath, "utf8");
    for (const specifier of moduleSpecifiers(source)) {
      edges.push({ sourcePath, specifier });
      if (!specifier.startsWith(".")) continue;
      const dependency = path.resolve(path.dirname(sourcePath), specifier);
      assert.equal(
        fs.statSync(dependency).isFile(),
        true,
        `${specifier} from ${sourcePath} must resolve to a file`,
      );
      pending.push(dependency);
    }
  }
  return { modules: visited, edges };
}

function heldCell(
  index,
  rpm,
  lane,
  manifoldPressurePaAbs,
  loadAliases = null,
) {
  const cell = {
    index,
    id: `${rpm}rpm-${lane}`,
    rpm,
    lane,
    manifoldPressurePaAbs,
    shiftToCanonicalSamples: 0,
    meanCombinedRms: 1,
    residualCombinedPower: 0,
    routes: Object.freeze([]),
    selectorSeed: BigInt(index + 1),
  };
  if (loadAliases !== null) {
    cell.loadAliases = Object.freeze(
      loadAliases.map((alias) => Object.freeze({ ...alias })),
    );
  }
  return Object.freeze(cell);
}

function loadedRuntimeFixture({ batchFrames = 1 } = {}) {
  const lanes = Object.freeze([
    Object.freeze({ id: "closed", throttle01: 0.2 }),
    Object.freeze({ id: "open", throttle01: 0.8 }),
  ]);
  const cells = Object.freeze([
    heldCell(0, 1_000, "closed", 30_000),
    heldCell(1, 1_000, "open", 90_000),
    heldCell(2, 2_000, "closed", 40_000),
    heldCell(3, 2_000, "open", 100_000),
  ]);
  const heldPackage = Object.freeze({
    kind: "held-phase-texture-package",
    manifest: Object.freeze({
      domain: Object.freeze({ load_lanes: lanes }),
    }),
    busIds: Object.freeze([]),
    loadCoordinate: "manifold-pressure-pa-absolute",
    minimumRpm: 1_000,
    maximumRpm: 2_000,
    rows: Object.freeze([
      Object.freeze({ rpm: 1_000, cells: Object.freeze(cells.slice(0, 2)) }),
      Object.freeze({ rpm: 2_000, cells: Object.freeze(cells.slice(2, 4)) }),
    ]),
    cells,
    samplesPerCycle: 4,
    residualCycleCount: 2,
    meanGram: Object.freeze(
      cells.map(() => Object.freeze(cells.map(() => 1))),
    ),
    publicSeed: 1n,
    selection: Object.freeze({}),
  });
  const runtime = Object.freeze({
    kind: "responsive-audio-preview",
    sampleRate: 192_000,
    minimumRpm: 1_000,
    maximumRpm: 2_000,
    heldMinimumRpm: 1_000,
    heldMaximumRpm: 2_000,
    batchFrames,
    busIds: Object.freeze([]),
    heldPackage,
    directionalPackage: Object.freeze({ routePairs: Object.freeze([]) }),
    motoringPackage: null,
    routePresentations: Object.freeze([]),
    sharedFullWetTransfer: false,
    capturedToSourceScale: 1,
    auditionRouteIndices: Object.freeze([]),
    masterVolumeLinear: 1,
  });
  return Object.freeze({
    package: Object.freeze({
      kind: "vehicleengine-package",
      descriptor: Object.freeze({ engineId: "unit-engine" }),
    }),
    runtime,
  });
}

function loadedCoalescedRuntimeFixture() {
  const base = loadedRuntimeFixture({ batchFrames: 8 });
  const lanes = base.runtime.heldPackage.manifest.domain.load_lanes;
  const aliases = lanes.map(({ id: lane, throttle01 }) => ({
    lane,
    throttle01,
  }));
  const cells = Object.freeze([
    heldCell(0, 1_000, "closed", 40_000, aliases),
    heldCell(1, 2_000, "closed", 30_000, [aliases[0]]),
    heldCell(2, 2_000, "open", 90_000, [aliases[1]]),
  ]);
  const heldPackage = Object.freeze({
    ...base.runtime.heldPackage,
    cells,
    rows: Object.freeze([
      Object.freeze({ rpm: 1_000, cells: Object.freeze(cells.slice(0, 1)) }),
      Object.freeze({ rpm: 2_000, cells: Object.freeze(cells.slice(1)) }),
    ]),
    meanGram: Object.freeze(
      cells.map(() => Object.freeze(cells.map(() => 1))),
    ),
  });
  return Object.freeze({
    package: base.package,
    runtime: Object.freeze({
      ...base.runtime,
      heldPackage,
    }),
  });
}

function heldLoaderFixture({ mutateFirstRoute, mutateSecondRoute } = {}) {
  const encoder = new TextEncoder();
  const encodeJson = (value) => encoder.encode(`${JSON.stringify(value)}\n`);
  const encodeFloat32 = (values) => {
    const bytes = new Uint8Array(values.length * 4);
    const view = new DataView(bytes.buffer);
    values.forEach((value, index) => view.setFloat32(index * 4, value, true));
    return bytes;
  };
  const digest = (bytes) => createHash("sha256").update(bytes).digest("hex");
  const lanes = [
    { id: "coast", throttle01: 0 },
    { id: "mid", throttle01: 0.5 },
    { id: "power", throttle01: 1 },
  ];
  const aliases = (ids) => ({
    coalesced_authored_lanes: ids,
    coalesced_capture_throttles_01: ids.map(
      (id) => lanes.find((lane) => lane.id === id).throttle01,
    ),
  });
  const mean = encodeFloat32([0, 1, 0, -1]);
  const residual = encodeFloat32([0, 0, 0, 0, 0, 0, 0, 0]);
  const meanDescriptor = {
    relative_path: "audio/mean.f32le",
    sample_count: 4,
    byte_count: mean.byteLength,
    payload_sha256: digest(mean),
  };
  const residualDescriptor = {
    relative_path: "audio/residuals.f32le",
    cycle_count: 2,
    samples_per_cycle: 4,
    sample_count: 8,
    byte_count: residual.byteLength,
    payload_sha256: digest(residual),
    selection_contract: "change residual ordinal only at a 720-degree boundary",
  };
  const cells = [
    {
      id: "1000rpm-coast-route",
      rpm: 1_000,
      lane: "coast",
      capture_throttle_01: 0,
      capture_provenance: aliases(["coast", "mid", "power"]),
      manifold_pressure_pa_abs: 40_000,
      source_cycle_begin_revolutions: 0,
      source_cycle_end_revolutions: 4,
      mean: meanDescriptor,
      residual_bank: residualDescriptor,
    },
    {
      id: "2000rpm-coast-route",
      rpm: 2_000,
      lane: "coast",
      capture_throttle_01: 0,
      capture_provenance: aliases(["coast", "mid"]),
      manifold_pressure_pa_abs: 30_000,
      source_cycle_begin_revolutions: 0,
      source_cycle_end_revolutions: 4,
      mean: meanDescriptor,
      residual_bank: residualDescriptor,
    },
    {
      id: "2000rpm-power-route",
      rpm: 2_000,
      lane: "power",
      capture_throttle_01: 1,
      capture_provenance: aliases(["power"]),
      manifold_pressure_pa_abs: 90_000,
      source_cycle_begin_revolutions: 0,
      source_cycle_end_revolutions: 4,
      mean: meanDescriptor,
      residual_bank: residualDescriptor,
    },
  ];
  const route = (busId) => ({
    schema: "engine-sim-offline/responsive-audio-held-route",
    id: `unit-${busId}`,
    engine: "unit-engine",
    audio: {
      bus_id: busId,
      sample_rate_hz: 192_000,
      encoding: "float32le",
      channel_layout: "mono",
    },
    phase: {
      cycle_revolutions: 2,
      samples_per_cycle: 4,
      residual_cycle_count: 2,
      residual_boundary_value: 0,
      residual_taper: { frames_per_edge: 1 },
    },
    domain: {
      rpm_anchors: [1_000, 2_000],
      load_coordinate: "measured-intake-manifold-pressure-pa-abs",
      load_lanes: lanes,
    },
    cells: structuredClone(cells),
  });
  const firstRoute = route("exhaust.dry");
  const secondRoute = route("intake.dry");
  mutateFirstRoute?.(firstRoute);
  mutateSecondRoute?.(secondRoute);
  const root = {
    schema: "engine-sim-offline/responsive-audio-held-texture",
    id: "unit-held",
    engine: "unit-engine",
    representation: {
      kind: "cyclic-mean-plus-boundary-zero-cycle-residual-bank",
      timeline_included: false,
    },
    domain: {
      minimum_rpm: 1_000,
      maximum_rpm: 2_000,
      rpm_anchors: [1_000, 2_000],
      load_lanes: lanes,
      operating_cell_count: 3,
    },
    dry_bus_ids: ["exhaust.dry", "intake.dry"],
    route_manifests: [
      { bus_id: "exhaust.dry", manifest_path: "exhaust.json" },
      { bus_id: "intake.dry", manifest_path: "intake.json" },
    ],
    phase_alignment: {
      method: "shared-route-sum-circular-correlation-unwrapped-grid-v1",
      reference_cell_id: "2000rpm-power",
      unit: "phase-samples",
      interpolation: "unwrapped-linear",
      cells: [
        { id: "1000rpm-coast", shift_to_canonical_samples: 0 },
        { id: "2000rpm-coast", shift_to_canonical_samples: 0 },
        { id: "2000rpm-power", shift_to_canonical_samples: 0 },
      ],
    },
    texture_selection: {
      algorithm: "splitmix64-shuffled-bags-v1",
      bank_size: 2,
      public_seed: "1",
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
      },
    },
  };
  const origin = "https://held-fixture.invalid/held/";
  const resources = new Map([
    [`${origin}package.json`, encodeJson(root)],
    [`${origin}exhaust.json`, encodeJson(firstRoute)],
    [`${origin}intake.json`, encodeJson(secondRoute)],
    [`${origin}audio/mean.f32le`, mean],
    [`${origin}audio/residuals.f32le`, residual],
  ]);
  return {
    manifestUrl: `${origin}package.json`,
    fetch: async (url) => {
      const bytes = resources.get(String(url));
      return {
        ok: bytes !== undefined,
        status: bytes === undefined ? 404 : 200,
        async arrayBuffer() {
          return bytes?.slice().buffer ?? new ArrayBuffer(0);
        },
      };
    },
  };
}

function assertFacadeError(code) {
  return (error) =>
    error instanceof VehicleEngineAudioEngineError && error.code === code;
}

function assertNear(actual, expected) {
  const tolerance = 8 * Number.EPSILON * Math.max(1, Math.abs(expected));
  assert.ok(
    Math.abs(actual - expected) <= tolerance,
    `expected ${actual} to lie within ${tolerance} of ${expected}`,
  );
}

test("audio facade dependency closure excludes simulator and WASM modules", () => {
  const closure = staticModuleClosure(ENTRY_MODULE);
  assert.ok(closure.modules.size > 2, "the test must traverse the full closure");
  assert.ok(
    [...closure.modules].some((modulePath) =>
      modulePath.endsWith("/vehicleengine-package.js")
    ),
    "the closure must include the VEHICLEENGINE verifier",
  );
  for (const { sourcePath, specifier } of closure.edges) {
    assert.doesNotMatch(
      specifier,
      /c-api|browser-engine|wasm/iu,
      `${sourcePath} must not import simulator or WASM machinery`,
    );
  }
});

test("audio facade validates construction, operating points, and frame counts", () => {
  assert.throws(
    () => new VehicleEngineAudioEngine(null),
    assertFacadeError("invalid-runtime-package"),
  );
  const engine = new VehicleEngineAudioEngine(loadedRuntimeFixture());
  assert.equal(engine.engineId, "unit-engine");
  assert.equal(engine.sampleRate, 192_000);
  assert.equal(engine.blockFrames, 1);

  assert.throws(
    () => engine.setOperatingPoint(null),
    assertFacadeError("invalid-operating-point"),
  );
  assert.throws(
    () => engine.setOperatingPoint({
      rpm: 999,
      throttle01: 0.5,
      load01: 0.5,
    }),
    assertFacadeError("rpm-outside-package"),
  );
  assert.throws(
    () => engine.setOperatingPoint({
      rpm: 1_500,
      throttle01: 1.01,
      load01: 0.5,
    }),
    assertFacadeError("invalid-operating-point"),
  );
  assert.throws(
    () => engine.setOperatingPoint({
      rpm: 1_500,
      throttle01: 0.5,
      load01: Number.NaN,
    }),
    assertFacadeError("invalid-operating-point"),
  );
  assert.throws(() => engine.render(0), assertFacadeError("invalid-frame-count"));
  assert.throws(
    () => engine.render(1_048_577),
    assertFacadeError("invalid-frame-count"),
  );
});

test("audio facade maps normalized load through lane coordinates and RPM", () => {
  const engine = new VehicleEngineAudioEngine(loadedRuntimeFixture());

  assertNear(engine.loadManifoldPressurePa(1_500, 0), 35_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.2), 35_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.5), 65_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.8), 95_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 1), 95_000);
  assertNear(engine.loadManifoldPressurePa(1_250, 0.35), 47_500);

  const point = engine.setOperatingPoint({
    rpm: 1_500,
    throttle01: 0.75,
    load01: 0.5,
  });
  assert.equal(Object.isFrozen(point), true);
  assertNear(engine.diagnostics().loadManifoldPressurePa, 65_000);
  const rendered = engine.render(1);
  assert.ok(rendered instanceof Float32Array);
  assert.equal(rendered.length, 1);
});

test("coalesced held aliases preserve continuous load and cell weights", () => {
  const loaded = loadedCoalescedRuntimeFixture();
  const engine = new VehicleEngineAudioEngine(loaded);

  assertNear(engine.loadManifoldPressurePa(1_000, 0), 40_000);
  assertNear(engine.loadManifoldPressurePa(1_000, 0.5), 40_000);
  assertNear(engine.loadManifoldPressurePa(1_000, 1), 40_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.2), 35_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.5), 50_000);
  assertNear(engine.loadManifoldPressurePa(1_500, 0.8), 65_000);

  const cursor = new HeldPhaseTextureCursor(loaded.runtime.heldPackage);
  const exactAlias = cursor.operatingWeights({
    rpm: 1_000,
    manifoldPressurePaAbs: 40_000,
  });
  assert.deepEqual(
    exactAlias.cells.map(({ id, weight }) => ({ id, weight })),
    [{ id: "1000rpm-closed", weight: 1 }],
  );
  const interpolated = cursor.operatingWeights({
    rpm: 1_500,
    manifoldPressurePaAbs: 50_000,
  });
  assert.equal(new Set(interpolated.cells.map(({ id }) => id)).size, 3);
  assertNear(
    interpolated.cells.reduce((sum, { weight }) => sum + weight, 0),
    1,
  );
  assert.doesNotThrow(() => {
    engine.process({ rpm: 1_000, throttle01: 0.5, load01: 0.5 }, 8);
    engine.process({ rpm: 1_500, throttle01: 0.5, load01: 0.5 }, 8);
    engine.process({ rpm: 2_000, throttle01: 0.5, load01: 0.5 }, 8);
  });
});

test("held loader validates coalesced aliases against the declared domain", async () => {
  const fixture = heldLoaderFixture();
  const package_ = await loadHeldPhaseTexturePackage(fixture.manifestUrl, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });
  assert.deepEqual(package_.rows[0].cells[0].loadAliases, [
    { lane: "coast", throttle01: 0 },
    { lane: "mid", throttle01: 0.5 },
    { lane: "power", throttle01: 1 },
  ]);
  assert.equal(package_.rows[0].cells.length, 1);
  assert.equal(package_.rows[1].cells.length, 2);

  const staleThrottle = heldLoaderFixture({
    mutateFirstRoute(route) {
      route.cells[0].capture_provenance.coalesced_capture_throttles_01[1] = 0.4;
    },
  });
  await assert.rejects(
    loadHeldPhaseTexturePackage(staleThrottle.manifestUrl, {
      fetch: staleThrottle.fetch,
      crypto: webcrypto,
    }),
    /coalesced alias mid disagrees with domain\.load_lanes/u,
  );
});

test("held loader rejects route disagreement about coalesced aliases", async () => {
  const fixture = heldLoaderFixture({
    mutateSecondRoute(route) {
      route.cells[0].capture_provenance.coalesced_authored_lanes.pop();
      route.cells[0].capture_provenance.coalesced_capture_throttles_01.pop();
    },
  });
  await assert.rejects(
    loadHeldPhaseTexturePackage(fixture.manifestUrl, {
      fetch: fixture.fetch,
      crypto: webcrypto,
    }),
    /1000rpm-coast route coordinates disagree/u,
  );
});

test("streaming process accepts dense endpoints behind one uniform batch", () => {
  const engine = new VehicleEngineAudioEngine(
    loadedRuntimeFixture({ batchFrames: 8 }),
  );
  const points = [
    { rpm: 1_200, throttle01: 0.3, load01: 0.4, frames: 3 },
    { rpm: 1_350, throttle01: 0.5, load01: 0.6, frames: 3 },
    { rpm: 1_500, throttle01: 0.7, load01: 0.8, frames: 3 },
    { rpm: 1_400, throttle01: 0.2, load01: 0.3, frames: 7 },
  ];
  let submittedFrames = 0;
  for (const point of points) {
    const output = engine.process(point, point.frames);
    submittedFrames += point.frames;
    assert.ok(output instanceof Float32Array);
    assert.equal(output.length, point.frames);
    assert.equal(engine.operatingPoint.rpm, point.rpm);
    const diagnostics = engine.diagnostics();
    assert.equal(diagnostics.committedOperatingPoint.rpm, point.rpm);
    assert.equal(diagnostics.renderMode, "streaming");
    assert.equal(diagnostics.renderedFrames, submittedFrames);
    assert.equal(
      diagnostics.runtime.audibleInputFrameCount,
      submittedFrames,
    );
    assert.equal(
      diagnostics.runtime.audibleOutputFrameCount,
      Math.floor(submittedFrames / engine.blockFrames) * engine.blockFrames,
    );
    assert.equal(
      diagnostics.queuedFrames + diagnostics.runtime.pendingFrames,
      engine.blockFrames,
      "streaming must preserve exactly one presentation batch of latency",
    );
  }
  assert.equal(engine.queuedFrames, engine.blockFrames);
  assert.equal(engine.diagnostics().runtime.pendingFrames, 0);
  assert.throws(
    () => engine.render(1),
    assertFacadeError("mixed-render-modes"),
  );

  engine.reset();
  const rendered = engine.render(1);
  assert.equal(rendered.length, 1);
  assert.equal(engine.diagnostics().renderMode, "offline");
  assert.throws(
    () => engine.process({
      rpm: 1_300,
      throttle01: 0.4,
      load01: 0.5,
    }, 1),
    assertFacadeError("mixed-render-modes"),
  );
});

test("streaming process defaults to one 20 ms endpoint", () => {
  const engine = new VehicleEngineAudioEngine(loadedRuntimeFixture());
  assert.equal(engine.processFrames, 3_840);
  assert.equal(engine.latencyFrames, engine.blockFrames);
  assert.deepEqual(engine.format, {
    sampleRateHz: 192_000,
    channelCount: 1,
    sampleEncoding: "float32",
    interleaving: "mono",
    processFrames: 3_840,
    latencyFrames: engine.blockFrames,
    internalBlockFrames: engine.blockFrames,
  });
  const rendered = engine.process({
    rpm: 1_200,
    throttle01: 0.3,
    load01: 0.4,
  });
  assert.equal(rendered.length, 3_840);
  assert.equal(engine.diagnostics().renderedFrames, 3_840);
});

test("streaming validates endpoints and counts before advancing", () => {
  const engine = new VehicleEngineAudioEngine(loadedRuntimeFixture());
  const point = { rpm: 1_200, throttle01: 0.3, load01: 0.4 };
  assert.throws(
    () => engine.process(point, 0),
    assertFacadeError("invalid-frame-count"),
  );
  assert.throws(
    () => engine.process({ ...point, rpm: 2_001 }, 1),
    assertFacadeError("rpm-outside-package"),
  );
  assert.equal(engine.diagnostics().renderMode, null);
  assert.equal(engine.diagnostics().renderedFrames, 0);
  assert.equal(engine.diagnostics().runtime.audibleInputFrameCount, 0);
});
