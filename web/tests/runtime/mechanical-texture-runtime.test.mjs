import assert from "node:assert/strict";
import test from "node:test";
import {MechanicalTextureCursor, validateMechanicalTexture} from "../../runtime/mechanical-texture-runtime.js";
import {DieselMechanicalTexture} from "../../runtime/diesel-mechanical-runtime.js";
import {mechanicalFixture} from "./mechanical-texture-fixture.mjs";

test("mechanical extension admits only bound, finite, supported configuration", () => {
  assert.equal(validateMechanicalTexture(undefined, "unit-engine", "a".repeat(64)), null);
  assert.deepEqual(validateMechanicalTexture(mechanicalFixture(), "unit-engine", "a".repeat(64)), mechanicalFixture());
  for (const mutate of [
    m => m.engine = "other", m => m.schema = "unknown", m => m.unused = true,
    m => m.parameters.valve_attack_s = 0, m => m.parameters.seed = 1.5,
    m => m.gain_linear = NaN, m => m.intake_reference_pressure_pa_abs = 0,
    m => m.cylinders.push({...m.cylinders[0]}),
    m => m.timing.intake_close_angle_rad = m.timing.intake_open_angle_rad,
    m => m.provenance.engine_identity_sha256 = "f".repeat(64)
  ]) {
    const m = mechanicalFixture(); mutate(m);
    assert.throws(() => validateMechanicalTexture(m, "unit-engine", "a".repeat(64)), /Invalid mechanical texture/);
  }
});

function render(cursor, chunks, map = 100000) {
  let frame = 0;
  const endpoint = n => ({rpm: 1000, manifoldPressurePaAbs: map,
    unwrappedCrankRevolutions: n / 192000 * 1000 / 60});
  const output = [];
  for (const count of chunks) {
    output.push(cursor.renderBlock({frameCount: count, start: endpoint(frame), end: endpoint(frame + count)}));
    frame += count;
  }
  const result = new Float32Array(output.reduce((s, x) => s + x.length, 0));
  let offset = 0;
  for (const x of output) { result.set(x, offset); offset += x.length; }
  return result;
}

test("mechanical adapter reuses smooth diesel contacts without combustion or a different noise source", () => {
  const m = mechanicalFixture();
  const cursor = new MechanicalTextureCursor(m, 1024);
  const actual = render(cursor, [32768]);
  const shared = new DieselMechanicalTexture(m.parameters, m.timing, m.cylinders);
  for (let i = 0; i < actual.length; ++i) {
    const revs = (i + 1) / 32768 * (32768 / 192000 * 1000 / 60);
    const expected = shared.process(revs, 1000) * Math.min(1, (i + 1) / 3840);
    assert.equal(actual[i], Math.fround(expected));
  }
});

test("mechanical batches preserve contact history through partial process calls and reset", () => {
  const m = mechanicalFixture();
  const whole = render(new MechanicalTextureCursor(m, 1024), [32768]);
  const cursor = new MechanicalTextureCursor(m, 1024);
  const split = render(cursor, [1, 1023, 67, 4000, 12000, 15677]);
  assert.equal(split.length, whole.length);
  assert.ok(split.some(x => x !== 0));
  for (let i = 0; i < whole.length; ++i) assert.ok(Math.abs(split[i] - whole[i]) < 1e-7);
  cursor.reset();
  assert.deepEqual(render(cursor, [32768]), whole);
});

test("48 kHz mechanical playback retains contact timing and the 20 ms fade", () => {
  const manifest = mechanicalFixture(), rate = 48000, count = 8192;
  const cursor = new MechanicalTextureCursor(manifest, 1024, rate);
  const endpoint = n => ({rpm: 1000, manifoldPressurePaAbs: 100000,
    unwrappedCrankRevolutions: n / rate * 1000 / 60});
  const actual = cursor.renderBlock({frameCount: count, start: endpoint(0), end: endpoint(count)});
  const shared = new DieselMechanicalTexture(manifest.parameters, manifest.timing, manifest.cylinders, rate);
  for (let i = 0; i < count; ++i) {
    const expected = shared.process((i + 1) / count * endpoint(count).unwrappedCrankRevolutions, 1000) *
      Math.min(1, (i + 1) / (rate * .02));
    assert.equal(actual[i], Math.fround(expected));
  }
  assert.ok(actual.some(sample => sample !== 0));
  assert.throws(() => new MechanicalTextureCursor(manifest, 1024, 44100), /sample rate/);
});

test("gasoline manifold pressure changes breath while the valvetrain keeps rotating", () => {
  const full = new MechanicalTextureCursor(mechanicalFixture(), 1024);
  const closed = new MechanicalTextureCursor(mechanicalFixture(), 1024);
  render(full, [32768]); render(closed, [32768], 25000);
  const a = full.diagnostics(), b = closed.diagnostics();
  assert.ok(a.valveRms > 0 && a.intakeRms > 0);
  assert.equal(a.valveRms, b.valveRms);
  assert.ok(Math.abs(b.intakeRms / a.intakeRms - .25 ** .75) < 1e-12);
});
