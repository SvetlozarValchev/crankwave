// Add an optional procedural mechanical layer to a separate gasoline carrier.
// Physical captures, exhaust transfer, limiter and lifecycle entries are retained.
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import {createHash, webcrypto} from "node:crypto";
import {execFileSync} from "node:child_process";
import {loadResponsiveAudioCrankwave} from "../web/runtime/crankwave-package.js";
import {validateMechanicalTexture} from "../web/runtime/mechanical-texture-runtime.js";

const [baseFile, engineFile, tuningFile, directory, output, ...extra] = process.argv.slice(2);
assert.ok(baseFile && engineFile && tuningFile && directory && output && !extra.length,
  "usage: node tools/bake-mechanical-texture.mjs <base.crankwave> <engine.json> <tuning.json> <new-package-directory> <new-output.crankwave>");
assert.notEqual(path.resolve(baseFile), path.resolve(output), "Use a separate output carrier");
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const original = await fs.readFile(baseFile);
const engineBytes = await fs.readFile(engineFile);
const authored = JSON.parse(engineBytes);
const tuning = JSON.parse(await fs.readFile(tuningFile, "utf8"));
assert.equal(tuning.schema, "crankwave/mechanical-texture-tuning-v1");
assert.equal(hash(original), tuning.base_carrier_sha256, "Tuning must bind the reviewed base carrier");
assert.equal(hash(engineBytes), tuning.authoring_sha256, "Tuning must bind the reviewed engine definition");
const loaded = await loadResponsiveAudioCrankwave(original, {crypto: webcrypto});
assert.equal(loaded.runtime.kind, "responsive-audio-preview");
assert.equal(loaded.runtime.mechanicalTexture, null, "Base already has mechanical texture");
const e = authored.engine;
assert.equal(e.identity.id, loaded.package.descriptor.engineId);
assert.equal(e.cycle, "four_stroke");
const quantity = (q, units) => {
  assert.ok(q && Object.hasOwn(units, q.unit) && Number.isFinite(q.value), "Unsupported authored units");
  return q.value * units[q.unit];
};
const radians = q => quantity(q, {deg: Math.PI / 180, rad: 1});
const metres = q => quantity(q, {mm: .001, m: 1, in: .0254});
const wrap = x => ((x % (4 * Math.PI)) + 4 * Math.PI) % (4 * Math.PI);
const cylinders = [];
let timing;
for (const c of e.cylinders) {
  const firing = e.ignition.firing_order.find(f => f.wire === c.ignition_wire);
  assert.ok(firing, "Cylinder needs a declared firing phase");
  const phase = radians(firing.crank_angle);
  const edges = {};
  for (const kind of ["intake", "exhaust"]) {
    const lobes = e.cam_lobes.filter(l => l.cylinder === c.id && l.port_kind === kind);
    assert.equal(lobes.length, 1, "Adapter requires one lobe per port");
    const lobe = lobes[0];
    assert.equal(lobe.type, "harmonic", "Adapter currently supports fixed harmonic cams");
    const cams = e.camshafts.filter(cam => cam.lobes.includes(lobe.id));
    assert.equal(cams.length, 1);
    // Native harmonic shape uses 0.050-inch reference lift. The authored duration
    // at that lift is shorter than the zero-lift window; derive its full support.
    assert.ok(Math.abs(metres(lobe.reference_lift) - .00127) < 1e-12);
    const q = (2 * metres(lobe.reference_lift) / metres(lobe.maximum_lift)) ** (1 / lobe.gamma) - 1;
    assert.ok(q > -1 && q < 1);
    const halfWidth = Math.PI * radians(lobe.duration_at_reference_lift) / (2 * Math.acos(q));
    const center = wrap(radians(lobe.centerline) - phase - radians(cams[0].advance));
    edges[kind] = {open: center - halfWidth, close: center + halfWidth};
  }
  const current = {intake_open_angle_rad: edges.intake.open,
    intake_close_angle_rad: edges.intake.close, exhaust_close_angle_rad: edges.exhaust.close};
  if (timing) for (const key of Object.keys(current))
    assert.ok(Math.abs(current[key] - timing[key]) < 1e-10, "Adapter requires common cam windows relative to firing");
  else timing = current;
  cylinders.push({id: c.id, firing_angle_rad: phase, structural_gain: 1});
}
const manifest = {
  schema: "crankwave/mechanical-texture-v1", engine: e.identity.id,
  parameters: tuning.parameters, timing, cylinders,
  intake_reference_pressure_pa_abs: tuning.intake_reference_pressure_pa_abs,
  intake_pressure_exponent: tuning.intake_pressure_exponent, gain_linear: tuning.gain_linear,
  provenance: {engine_identity_sha256: loaded.runtime.manifest.provenance.engine.sha256,
    authoring_sha256: hash(engineBytes), base_carrier_sha256: hash(original),
    note: "Authored acoustic contacts and intake-window breath using the shared diesel mechanical implementation. Cam support is derived from the gasoline engine's harmonic lobes, advance and firing phases; sampled-lobe interpolation/contact forces are not solved. Breath scales with captured manifold pressure. No diesel combustion, fueling cutoff, exhaust transfer or reference PCM is added."}
};
validateMechanicalTexture(manifest, e.identity.id, loaded.runtime.manifest.provenance.engine.sha256);
await fs.mkdir(directory, {recursive: false});
for (const entry of loaded.package.entries) {
  const target = path.join(directory, entry.path);
  await fs.mkdir(path.dirname(target), {recursive: true});
  await fs.writeFile(target, original.subarray(entry.offset, entry.offset + entry.byteCount), {flag: "wx"});
}
const runtimePath = path.join(directory, loaded.package.descriptor.runtime.manifestPath);
const runtime = JSON.parse(await fs.readFile(runtimePath, "utf8"));
runtime.id += "-mechanical";
runtime.mechanical_texture = manifest;
const runtimeBytes = JSON.stringify(runtime, null, 2) + "\n";
await fs.writeFile(runtimePath, runtimeBytes);
const descriptorPath = path.join(directory, "crankwave.json");
const descriptor = JSON.parse(await fs.readFile(descriptorPath, "utf8"));
descriptor.runtime.manifest_sha256 = hash(runtimeBytes);
await fs.writeFile(descriptorPath, JSON.stringify(descriptor, null, 2) + "\n");
execFileSync(path.resolve("build/crankwave"), ["pack-crankwave", "--package-directory", path.resolve(directory),
  "--output", path.resolve(output), "--result-format", "json"], {stdio: "inherit"});
const result = await loadResponsiveAudioCrankwave(await fs.readFile(output), {crypto: webcrypto});
assert.deepEqual(result.runtime.mechanicalTexture, manifest);
for (const entry of loaded.package.entries) {
  if (["crankwave.json", loaded.package.descriptor.runtime.manifestPath].includes(entry.path)) continue;
  assert.equal(result.package.entries.find(e => e.path === entry.path)?.sha256, entry.sha256,
    `Base content changed: ${entry.path}`);
}
assert.equal(hash(await fs.readFile(baseFile)), hash(original));
console.log(JSON.stringify({output, sha256: hash(await fs.readFile(output)), manifest}, null, 2));
