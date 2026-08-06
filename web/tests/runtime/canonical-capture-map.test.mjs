import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const REPOSITORY = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../../..",
);
const SCENARIO_ROOT = path.join(
  REPOSITORY,
  "data/engines/bmw-m52tub28-cleanroom/scenarios",
);

const PROCEDURES = Object.freeze([
  {
    packageId: "bmw-m52tub28-canonical-crank",
    file: "canonical-crank-only-0rpm.json",
    covers: ["crank"],
  },
  {
    packageId: "bmw-m52tub28-cold-start",
    file: "cold-start-crank-catch-0rpm.json",
    covers: ["startup/catch"],
  },
  {
    packageId: "bmw-m52tub28-held-idle",
    file: "held-idle-region-700rpm.json",
    covers: ["settled idle"],
  },
  {
    packageId: "bmw-m52tub28-canonical-load-cycle",
    file: "canonical-loaded-rise-part-load-coast-1500-4500rpm.json",
    covers: ["loaded rise", "part load", "coast fall"],
  },
  {
    packageId: "bmw-m52tub28-free-rev",
    file: "warm-running-free-rev-700rpm.json",
    webFile: "warm-running-free-rev-700rpm-10khz-preview.json",
    covers: ["neutral limiter", "limiter lift/recovery"],
  },
  {
    packageId: "bmw-m52tub28-canonical-shutdown",
    file: "canonical-key-off-shutdown-700rpm.json",
    covers: ["shutdown"],
  },
]);

function scenario(file) {
  return JSON.parse(fs.readFileSync(path.join(SCENARIO_ROOT, file), "utf8"));
}

test("six procedures cover the complete slice-15 capture vocabulary", () => {
  assert.deepEqual(
    PROCEDURES.flatMap(({ covers }) => covers),
    [
      "crank",
      "startup/catch",
      "settled idle",
      "loaded rise",
      "part load",
      "coast fall",
      "neutral limiter",
      "limiter lift/recovery",
      "shutdown",
    ],
  );
  for (const procedure of PROCEDURES) {
    const authored = scenario(procedure.file);
    assert.equal(authored.schema, "engine-sim-offline/scenario");
    assert.equal(authored.engine, "bmw-m52tub28-cleanroom");
    assert.equal(authored.quality.id, "listening");
    assert.deepEqual(authored.output.buses, [
      "master-engine-raw",
      "master-engine-audition",
    ]);
  }
});

test("compound procedures retain their canonical phase boundaries", () => {
  const crank = scenario("canonical-crank-only-0rpm.json");
  assert.deepEqual(
    {
      ignition: crank.initial_state.ignition_enabled,
      fuel: crank.initial_state.fuel_enabled,
      starter: crank.initial_state.starter_enabled,
      duration: crank.audible_duration.value,
    },
    { ignition: false, fuel: false, starter: true, duration: 2.5 },
  );

  const load = scenario(
    "canonical-loaded-rise-part-load-coast-1500-4500rpm.json",
  );
  assert.equal(load.mode.type, "held_dyno");
  assert.deepEqual(
    load.mode.throttle_01.points.map(({ time, value }) => [time.value, value]),
    [[0, 1], [9, 0.45], [11, 0.04]],
  );
  assert.equal(load.mode.maximum_driving_torque.value, 0);
  assert.deepEqual(
    [load.audible_start.value, load.audible_duration.value],
    [3, 11],
  );

  const freeRev = scenario("warm-running-free-rev-700rpm.json");
  assert.equal(freeRev.initial_state.limiter_enabled, true);
  assert.ok(freeRev.mode.throttle_01.points.some(({ value }) => value >= 0.9));
  assert.ok(freeRev.mode.throttle_01.points.some(({ value }) => value <= 0.04));

  const shutdown = scenario("canonical-key-off-shutdown-700rpm.json");
  assert.equal(shutdown.mode.type, "free_engine");
  assert.deepEqual(shutdown.events, [
    {
      id: "key-off",
      time: { value: 1.7, unit: "s" },
      payload: {
        type: "operating_state_patch",
        ignition_enabled: false,
        fuel_enabled: false,
      },
    },
  ]);
  assert.deepEqual(
    [shutdown.audible_start.value, shutdown.audible_duration.value],
    [0.9, 1.9],
  );
});

test("the Web workbench keeps the listening catalog focused on responsive A/B", () => {
  const app = fs.readFileSync(path.join(REPOSITORY, "web/app.js"), "utf8");
  const responsiveEngines = [
    "bmw-m52b28",
    "bmw-m52tub28-cleanroom",
    "raspy-muscle-620-cleanroom",
    "sequoia-3ur-fe-cleanroom",
    "harley-evolution-1340-cleanroom",
    "shovelhead-bank-local-heads",
    "honda-b18c5-cleanroom",
    "kohler-ch750-cleanroom",
    "radial-5-cleanroom",
    "subaru-ej25-cleanroom",
  ];
  for (const engine of responsiveEngines) {
    assert.match(app, new RegExp(`id: "${engine}-interactive-ab"`, "u"));
    assert.match(
      app,
      new RegExp(`/packages/${engine}-responsive-audio/runtime\\.json`, "u"),
    );
  }
  for (const { packageId } of PROCEDURES) {
    assert.doesNotMatch(app, new RegExp(`id: "${packageId}"`, "u"));
  }
  assert.match(app, /id: "bmw-m52tub28-cleanroom-lifecycle-ab"/u);
});

test("the temporary realtime preview halves only the physics and capture clocks", () => {
  const canonical = scenario("warm-running-free-rev-700rpm.json");
  const preview = scenario("warm-running-free-rev-700rpm-10khz-preview.json");
  assert.deepEqual(canonical.rates, {
    physics: { numerator: "20000", denominator: "1", unit: "Hz" },
    capture: { numerator: "20000", denominator: "1", unit: "Hz" },
    source_processing: { numerator: "192000", denominator: "1", unit: "Hz" },
    acoustics: { numerator: "192000", denominator: "1", unit: "Hz" },
    delivery: { numerator: "192000", denominator: "1", unit: "Hz" },
  });
  assert.deepEqual(preview.rates, {
    ...canonical.rates,
    physics: { numerator: "10000", denominator: "1", unit: "Hz" },
    capture: { numerator: "10000", denominator: "1", unit: "Hz" },
  });
});

test("every Web package declares its session lifetime independently of mode", () => {
  const app = fs.readFileSync(path.join(REPOSITORY, "web/app.js"), "utf8");
  const catalog = app.slice(
    app.indexOf("const WORKBENCH_PACKAGES"),
    app.indexOf("const RING_HEADER"),
  );
  const objectCount = [...catalog.matchAll(/Object\.freeze\(\{/gu)].length;
  const entries = [
    ...catalog.matchAll(
      /Object\.freeze\(\{\s+id: (?:DEFAULT_PACKAGE_ID|"[^"]+"),\s+label: "([^"]+)",\s+executionKind: SessionExecutionKind\.(openEnded|finiteScenario),/gu,
    ),
  ];

  assert.ok(objectCount > 0);
  assert.equal(entries.length, objectCount);
  for (const [, label, executionKind] of entries) {
    assert.equal(
      executionKind,
      label.endsWith("Interactive A/B") || label.endsWith("Lifecycle A/B")
        ? "openEnded"
        : "finiteScenario",
      label,
    );
  }
});
