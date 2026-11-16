import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import { HeldPhaseTextureCursor } from "../../runtime/held-phase-texture-runtime.js";

// Nonzero, two-route fixture: every cell and residual cycle has distinct PCM.
// The golden fingerprints were captured before the cycle/steady-state caches.
export function textureFixture(sampleRate) {
  const lanes = [{ id: "coast", throttle01: 0 }, { id: "power", throttle01: 1 }];
  const cells = [1000, 2000].flatMap((rpm, row) => lanes.map((lane, column) => {
    const index = row * 2 + column;
    const routes = [0, 1].map(route => ({
      mean: Float32Array.from({ length: 16 }, (_, i) => ((i * (index + 1) + route) % 13 - 6) / 8),
      residuals: Float32Array.from({ length: 64 }, (_, i) => ((i * 3 + index * 7 + route * 11) % 19 - 9) / 32),
    }));
    return { index, id: `cell-${index}`, rpm, lane: lane.id,
      manifoldPressurePaAbs: 30000 + column * 60000 + row * 10000,
      shiftToCanonicalSamples: index / 4, selectorSeed: BigInt(index + 1), routes };
  }));
  const gram = cells.map(a => cells.map(b => a.routes[0].mean.reduce((sum, _, i) =>
    sum + (a.routes[0].mean[i] + a.routes[1].mean[i]) *
      (b.routes[0].mean[i] + b.routes[1].mean[i]), 0) / 16));
  for (const cell of cells) {
    cell.meanCombinedRms = Math.sqrt(gram[cell.index][cell.index]);
    cell.residualCombinedPower = cell.routes[0].residuals.reduce((sum, _, i) =>
      sum + (cell.routes[0].residuals[i] + cell.routes[1].residuals[i]) ** 2, 0) / 64;
  }
  return { kind: "held-phase-texture-package", sampleRate,
    manifest: { domain: { load_lanes: lanes } }, busIds: ["a", "b"], cells,
    rows: [1000, 2000].map(rpm => ({ rpm, cells: cells.filter(cell => cell.rpm === rpm) })),
    minimumRpm: 1000, maximumRpm: 2000, samplesPerCycle: 16, residualCycleCount: 4,
    meanGram: gram, publicSeed: 17n, loadCoordinate: "manifold-pressure-pa-absolute" };
}

export function exerciseTexture(Cursor, sampleRate, sessionSeed) {
  const cursor = new Cursor(textureFixture(sampleRate), { sessionSeed });
  const hash = createHash("sha256");
  const endpoint = (rpm, map, crank) => ({ rpm, manifoldPressurePaAbs: map,
    unwrappedCrankRevolutions: crank, stateMask: 3 });
  const render = (start, end, frames) => {
    const outputBuffers = cursor.createOutputBuffers(frames);
    cursor.renderBlockInto({ frameCount: frames, start, end, outputBuffers });
    for (const output of outputBuffers) hash.update(new Uint8Array(output.buffer));
    hash.update(JSON.stringify(cursor.diagnostics()));
  };
  for (const origin of [-10.5, 0, 7.5, 2 ** 40 + 7.5]) {
    cursor.reset();
    let point = endpoint(1375, 55321, origin);
    cursor.initialize(point);
    for (let step = 0; step < 20; ++step) {
      const next = endpoint(step % 3 === 0 ? 1825 : 1375,
        step % 4 === 0 ? 95000 : 55321, point.unwrappedCrankRevolutions + .75);
      // Interleaved public queries must not leave stale blend state behind.
      cursor.operatingWeights(endpoint(2000, 100000, 0));
      render(point, next, sampleRate / 50);
      render(next, next, 17);
      point = next;
    }
    // Reinitialize without reset, revisit an earlier cycle, and change cells.
    const next = endpoint(1000, 30000, origin);
    cursor.initialize(next);
    render(next, endpoint(2000, 100000, origin + 8), 259);
  }
  return hash.digest("hex");
}

const GOLDEN = {
  "48000:0": "97b4679280e862d10b59beeb0a219dbea80118cad0b334865612dd427ee72ac4",
  "48000:1": "b4dc361c6d2568a99f80c42f7e578cd712ce70b19aa141ca8fbaaad7b4faab88",
  "48000:18446744073709551615": "e8110e4180630bcbd23ea1a494be2dade27430c156eca409ec3ea6c2607d6bff",
  "192000:0": "43aeef730c8127d9a727660f8ba3438a4fce37c79cbf75970b08b54519a6758e",
  "192000:1": "3193e8bf8d3921239aa433908953db8373751112c1e9d937b0523441aa3d1400",
  "192000:18446744073709551615": "dbb5a2c2a70e1f670d1b47da81cb710d7d194bd8e0014ea00650bfda91caf336"
};
for (const rate of [48000, 192000]) {
  for (const seed of ["0", "1", "18446744073709551615"]) {
    test(`held texture preserves seeded PCM and diagnostics at ${rate} Hz, seed ${seed}`, () => {
      assert.equal(exerciseTexture(HeldPhaseTextureCursor, rate, seed), GOLDEN[`${rate}:${seed}`]);
    });
  }
}
