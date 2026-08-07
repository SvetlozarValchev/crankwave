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

function loadedRuntimeFixture() {
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
    batchFrames: 1,
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
