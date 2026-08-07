import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  RevengineAudioEngine,
  RevengineAudioEngineError,
} from "../../runtime/revengine-audio-engine.js";

const ENTRY_MODULE = fileURLToPath(
  new URL("../../runtime/revengine-audio-engine.js", import.meta.url),
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

function heldCell(index, rpm, lane, manifoldPressurePaAbs) {
  return Object.freeze({
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
  });
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
      kind: "revengine-package",
      descriptor: Object.freeze({ engineId: "unit-engine" }),
    }),
    runtime,
  });
}

function assertFacadeError(code) {
  return (error) =>
    error instanceof RevengineAudioEngineError && error.code === code;
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
      modulePath.endsWith("/revengine-package.js")
    ),
    "the closure must include the REVENGINE verifier",
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
    () => new RevengineAudioEngine(null),
    assertFacadeError("invalid-runtime-package"),
  );
  const engine = new RevengineAudioEngine(loadedRuntimeFixture());
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
  const engine = new RevengineAudioEngine(loadedRuntimeFixture());

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

test("streaming process accepts dense endpoints behind one uniform batch", () => {
  const engine = new RevengineAudioEngine(
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
  const engine = new RevengineAudioEngine(loadedRuntimeFixture());
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
  const engine = new RevengineAudioEngine(loadedRuntimeFixture());
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
