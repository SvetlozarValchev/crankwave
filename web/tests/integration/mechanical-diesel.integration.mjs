import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {webcrypto} from "node:crypto";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import {CrankwaveAudioEngine} from "../../runtime/crankwave-audio-engine.js";
import {loadResponsiveAudioCrankwave} from "../../runtime/crankwave-package.js";
import {encodeFloat32Wav} from "../../runtime/wav.js";

const [cli, definition, requestedDirectory] = process.argv.slice(2);
assert.ok(
    cli && definition,
    "usage: node mechanical-diesel.integration.mjs <crankwave-cli> <engine.json> [new-output-directory]");
const authored = JSON.parse(await fs.readFile(definition, "utf8"));
const fixtures = {
  "mechanical-diesel-i4": {
    cells: 48,
    rpms: [800, 1700, 2800],
    frequencies: [680, 1150, 1900, 2850, 4200],
    cylinderOrder: [1, 3, 4, 2]
  },
  "ural4320-mechanical-v8": {
    cells: 63,
    rpms: [650, 1500, 2100],
    frequencies: [410, 760, 1210, 1870, 2760, 3950],
    cylinderOrder: [1, 5, 4, 2, 6, 3, 7, 8]
  },
  "golf16-common-rail-i4": {
    cells: authored.bake.rpms.length * authored.bake.fueling.length,
    rpms: [850, 1800, 3200],
    frequencies: authored.presentation.block_modes.map(mode => mode.frequency_hz),
    cylinderOrder: [1, 3, 4, 2]
  },
  "lister-tr1-mechanical-single": {
    cells: 49,
    rpms: [850, 1500, 2500],
    frequencies: [300, 620, 1150, 1950, 3100],
    cylinderOrder: [1]
  }
};
const fixture = fixtures[authored.engine.id];
assert.ok(fixture, "integration requires a known tracked diesel fixture");
const [idleRpm, loadedRpm, highRpm] = fixture.rpms;
const directory = requestedDirectory ?
    path.resolve(requestedDirectory) :
    await fs.mkdtemp(path.join(os.tmpdir(), "crankwave-diesel-"));
if (requestedDirectory) await fs.mkdir(directory, {recursive: false});
const carrierPath = path.join(directory, `${authored.engine.id}.crankwave`);
try {
  const bake = JSON.parse(execFileSync(
      cli,
      [
        "bake-crankwave", "--engine", definition, "--output", carrierPath,
        "--result-format", "json"
      ],
      {encoding: "utf8", timeout: 180000}));
  assert.equal(bake.ok, true);
  assert.equal(bake.result.held_cell_count, fixture.cells);
  assert.equal(bake.result.profile_id, "mechanical-diesel-v1");
  const verify = JSON.parse(execFileSync(
      cli, ["verify-crankwave", "--input", carrierPath, "--result-format", "json"],
      {encoding: "utf8"}));
  assert.equal(verify.result.verified, true);
  assert.equal(verify.result.package.runtime_kind, "diesel-mechanical-audio-v1");
  const bytes = await fs.readFile(carrierPath);
  // Publication conflict must fail without replacing the already verified file.
  assert.throws(
      () => execFileSync(
          cli,
          [
            "bake-crankwave", "--engine", definition, "--output", carrierPath,
            "--result-format", "json"
          ],
          {stdio: "pipe"}),
      (error) => error.status === 73);
  assert.deepEqual(await fs.readFile(carrierPath), bytes);
  const loaded = await loadResponsiveAudioCrankwave(bytes, {crypto: webcrypto});
  const engine = new CrankwaveAudioEngine(loaded);
  assert.equal(engine.loadCoordinate, "normalized-fueling");
  {
    const {manifest, samples} = loaded.runtime;
    if (authored.presentation.mechanical_texture) {
      const rounding = authored.presentation.loaded_pressure_texture;
      const idleRounding = rounding &&
          (rounding.fueling_start_01 < .12 || rounding.fueling_full_01 < .2);
      assert.equal(manifest.version, authored.presentation.loaded_clatter ?
        (authored.presentation.loaded_clatter.modes[0].cylinder_coupling ? 16 : 15) : authored.presentation.inline_inertia ? 14 :
          idleRounding ? 13 : authored.engine.operating_boundaries ? 12 :
          authored.presentation.loaded_pressure_texture ? 11 :
          authored.presentation.coasting_flow_texture ? 10 :
          authored.presentation.coasting_balance ? 9 :
          authored.presentation.low_fuel_exhaust ? 8 :
          authored.presentation.mechanical_texture.valve_attack_s ? 7 : 6);
      assert.deepEqual(manifest.mechanical_timing, {
        intake_open_angle_rad: authored.engine.cylinder.intake_valve.open_angle_atdc_rad,
        intake_close_angle_rad: authored.engine.cylinder.intake_valve.close_angle_atdc_rad,
        exhaust_close_angle_rad: authored.engine.cylinder.exhaust_valve.close_angle_atdc_rad
      }, 'native manifest must derive mechanical timing from the physical valve windows');
    }
    const n = manifest.samples_per_cycle;
    const area = Math.PI * authored.engine.cylinder.bore_m ** 2 / 4;
    for (let cell = 0; cell < manifest.cells.length; ++cell) {
      const force = samples[(cell * n + n / 2) * loaded.runtime.sourceChannels];
      const pressure = force / area + authored.engine.cylinder.crankcase_pressure_pa_abs;
      // The original fixtures recompressed exhaust to 11 bar or more at the
      // gas-exchange TDC. These warm open-exhaust fixtures must breathe.
      assert.ok(pressure < 300000, "exhaust TDC must not trap a compressed charge");
    }
  }
  if (authored.engine.id === "ural4320-mechanical-v8") {
    for (const rpm of [1500, 2100, 2275]) {
      const levels = [];
      for (const load01 of [0, 0.85]) {
        engine.setOperatingPoint({rpm, load01, throttle01: load01});
        engine.reset();
        const tail = engine.render(192000).subarray(96000);
        levels.push(Math.sqrt(tail.reduce((sum, x) => sum + x * x, 0) / tail.length));
      }
      assert.ok(levels[0] < levels[1] * 1.25,
          "this unbraked Ural must not surge severalfold in loudness on fuel cut");
    }
  }
  const captures = [];
  for (const [name, rpm, fuel] of [
           ["idle", idleRpm, 0.12], ["loaded", loadedRpm, 0.65],
           ["high", highRpm, 0.9]]) {
    engine.setOperatingPoint({rpm, throttle01: fuel, load01: fuel});
    engine.reset();
    const pcm = engine.render(384000);
    assert.ok(pcm.every((x) => Number.isFinite(x) && Math.abs(x) <= 1));
    // Measure the final second independently of cursor warmup diagnostics.
    const tail = pcm.subarray(192000);
    const mean = tail.reduce((sum, x) => sum + x, 0) / tail.length;
    const audibleRms =
        Math.sqrt(tail.reduce((sum, x) => sum + (x - mean) ** 2, 0) / tail.length);
    assert.ok(audibleRms > 0.005, "sustained playback must contain audible AC energy");
    // Report spectral balance and crest as diagnostics only. Earlier arbitrary
    // thresholds passed user-rejected audio and must not constrain timbre tuning.
    let low = 0;
    let highEnergy = 0;
    let totalEnergy = 0;
    let peak = 0;
    const alpha = 1 - Math.exp(-2 * Math.PI * 800 / engine.sampleRate);
    for (let i = 0; i < pcm.length; ++i) {
      low += alpha * (pcm[i] - low);
      if (i >= 192000) {
        highEnergy += (pcm[i] - low) ** 2;
        totalEnergy += pcm[i] ** 2;
        peak = Math.max(peak, Math.abs(pcm[i]));
      }
    }
    const highbandRatio = highEnergy / totalEnergy;
    const crestFactor = peak / audibleRms;
    const diagnostics = engine.diagnostics().runtime;
    assert.ok(
        diagnostics.blockRms > 0.001,
        "physical pressure must produce an audible block component");
    assert.ok(
        diagnostics.exhaustRms > 0.001,
        "physical valve flow must produce an audible exhaust component");
    assert.deepEqual(
        diagnostics.structuralFrequenciesHz, fixture.frequencies);
    assert.deepEqual(
        diagnostics.cylinderIds,
        fixture.cylinderOrder.map((id) => `cylinder-${id}`));
    engine.reset();
    assert.deepEqual(engine.render(pcm.length), pcm, "native package replays exactly");
    await fs.writeFile(
        path.join(directory, `${name}.wav`),
        encodeFloat32Wav(pcm, engine.sampleRate, 1), {flag: "wx"});
    captures.push({
      name,
      rpm,
      fuel,
      audibleRms,
      highbandRatio,
      crestFactor,
      blockRms: diagnostics.blockRms,
      exhaustRms: diagnostics.exhaustRms,
      valveRms: diagnostics.valveRms,
      pumpingRms: diagnostics.pumpingRms
    });
  }
  const fuelRms = [];
  for (const load01 of [0.08, 0.65]) {
    engine.setOperatingPoint({rpm: loadedRpm, throttle01: load01, load01});
    engine.reset();
    engine.render(384000);
    fuelRms.push(engine.diagnostics().runtime.blockRms);
    if (!authored.engine.operating_boundaries)
      assert.equal(engine.loadManifoldPressurePa(loadedRpm, load01), 101325);
    else {
      const row = authored.engine.operating_boundaries.filter(c => c.rpm === loadedRpm);
      const hi = row.findIndex(c => c.fueling_01 > load01);
      const a = row[hi - 1], b = row[hi];
      const weight = (load01 - a.fueling_01) / (b.fueling_01 - a.fueling_01);
      assert.equal(engine.loadManifoldPressurePa(loadedRpm, load01),
          a.intake.pressure_pa_abs * (1 - weight) + b.intake.pressure_pa_abs * weight);
    }
  }
  assert.ok(
      fuelRms[1] > fuelRms[0] * 2,
      "diesel combustion radiation must respond to fueling at the same RPM");
  const streamed = [];
  engine.setOperatingPoint({rpm: idleRpm, throttle01: 0.1, load01: 0.1});
  engine.reset();
  for (let i = 0; i < 200; ++i) {
    const position = i / 199;
    const pcm = engine.process({
      rpm: idleRpm + (highRpm - idleRpm) * position,
      throttle01: position,
      load01: 0.1 + 0.8 * position
    });
    assert.ok(pcm.every(Number.isFinite));
    streamed.push(pcm);
  }
  const sweep = new Float32Array(streamed.length * engine.processFrames);
  streamed.forEach((block, i) => sweep.set(block, i * engine.processFrames));
  await fs.writeFile(
      path.join(directory, "sweep.wav"), encodeFloat32Wav(sweep, engine.sampleRate, 1),
      {flag: "wx"});
  console.log(JSON.stringify({
    directory,
    carrierPath,
    verified: true,
    captures,
    constantRpmFuelBlockRms: fuelRms
  }));
} finally {
  if (!requestedDirectory) await fs.rm(directory, {recursive: true, force: true});
}
