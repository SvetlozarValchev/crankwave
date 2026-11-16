import assert from "node:assert/strict";
import {webcrypto} from "node:crypto";
import test from "node:test";

import {CrankwaveAudioEngine} from "../../runtime/crankwave-audio-engine.js";
import {DieselBiquad, DieselExhaustSystem, DieselPressureTexture, DieselMechanicalTexture, DieselLowFuelExhaust, DieselCoastingBalance, DieselCoastingFlowTexture, DieselLoadedPressureTexture, DieselInlineInertia, DieselLoadedClatter} from "../../runtime/diesel-mechanical-runtime.js";

import {digest, jsonBytes, packTestCarrier} from "./crankwave-test-carrier.mjs";

function fixture(mutate = () => {}) {
  const engineSource = jsonBytes({fixture: "synthetic diesel DSP input"});
  const n = 2048;
  const sources = Buffer.alloc(n * 4 * 8);
  const cells = [];
  for (const rpm of [800, 1600])
    for (const fueling of [0, 1]) {
      const index = cells.length;
      cells.push({
        rpm,
        fueling_01: fueling,
        manifold_pressure_pa_abs: 101325,
        settled_cycles: 12,
        indicated_work_j: 500 * fueling,
        peak_pressure_pa_abs: 4e6 + fueling * 8e6
      });
      for (let i = 0; i < n; ++i) {
        const phase = i / n;
        const wrapped = phase > 0.5 ? phase - 1 : phase;
        const force = 1500 * Math.cos(phase * 2 * Math.PI) +
            fueling * 50_000 * Math.exp(-((wrapped / 0.02) ** 2));
        const exhaust = 0.008 * (1 + Math.cos(phase * 2 * Math.PI)) * (1 + 2 * fueling);
        sources.writeFloatLE(force, (index * n + i) * 8);
        sources.writeFloatLE(exhaust, (index * n + i) * 8 + 4);
      }
    }
  const record = {origin: "artistic", note: "Synthetic DSP verification fixture"};
  const manifest = {
    schema: "crankwave/diesel-mechanical-audio",
    version: 2,
    engine: "test-diesel",
    model: "mechanical-diesel-v1",
    sample_rate_hz: 192000,
    physics_rate_hz: 20000,
    gas_substeps: 32,
    load_coordinate: "normalized-fueling",
    source_sha256: digest(engineSource),
    backend: {
      kind: "native-cpp-experimental",
      executable_sha256: "b".repeat(64),
      numeric_policy: "x86-64-v1-binary64-x87-extended-strict-v1"
    },
    methods: [
      "diesel-held-speed-cylinder-v1", "diesel-prepared-fuel-combustion-v1",
      "pressure-force-modal-radiation-v2"
    ],
    samples_per_cycle: n,
    cells,
    cylinders: [{id: "one", firing_angle_rad: 0, fuel_trim: 1, structural_gain: 1}],
    presentation: {
      block_modes: [{frequency_hz: 1900, q: 8, gain_pcm_per_n: 0.00008}],
      block_lowpass_hz: 6500,
      exhaust_gain_pcm_per_kg_s: 5,
      exhaust_lowpass_hz: 900,
      master_gain: 0.8,
      articulation: {
        block_highpass_hz: 450,
        exhaust_highpass_hz: 90,
        cycle_fuel_spread_01: 0.1,
        cycle_timing_spread_deg: 0.7,
        variation_seed: 81743
      }
    },
    provenance: {physical_parameters: record, presentation: record},
    sources: {
      path: "sources.f32le",
      sha256: digest(sources),
      layout: "cell-phase-force-n-exhaust-kg-s",
      byte_count: sources.length
    },
  };
  mutate(manifest, sources);
  // Re-authenticate deliberately malformed numeric payloads, so validation cannot
  // accidentally rely only on the carrier's hashes.
  manifest.sources.sha256 = digest(sources);
  const runtime = jsonBytes(manifest);
  const descriptor = jsonBytes({
    schema: "crankwave/crankwave-package",
    version: 1,
    engine_id: "test-diesel",
    runtime: {
      kind: "diesel-mechanical-audio-v1",
      manifest_path: "runtime.json",
      manifest_sha256: digest(runtime)
    }
  });
  return packTestCarrier([
    ["crankwave.json", descriptor], ["engine.json", engineSource],
    ["runtime.json", runtime], ["sources.f32le", sources]
  ]);
}
const load = (bytes) => CrankwaveAudioEngine.load(bytes, {crypto: webcrypto});
const rms = (samples) =>
    Math.sqrt(samples.reduce((n, x) => n + x * x, 0) / samples.length);

test(
    "diesel carrier selects fuel independently of equal manifold pressure",
    async () => {
      const engine = await load(fixture());
      assert.equal(engine.loadCoordinate, "normalized-fueling");
      assert.equal(engine.loadManifoldPressurePa(1000, 0), 101325);
      assert.equal(engine.loadManifoldPressurePa(1000, 1), 101325);
      engine.setOperatingPoint({rpm: 1000, throttle01: 0, load01: 0});
      engine.reset();
      const motoring = engine.render(192000);
      engine.setOperatingPoint({rpm: 1000, throttle01: 0, load01: 1});
      engine.reset();
      const firing = engine.render(192000);
      assert.ok(rms(firing) > rms(motoring) * 2);
      assert.ok(
          firing.every((sample) => Number.isFinite(sample) && Math.abs(sample) <= 1));
      assert.ok(engine.diagnostics().runtime.blockRms > 0);
      assert.ok(engine.diagnostics().runtime.exhaustRms > 0);
    });

test("diesel render partitioning and reset preserve PCM exactly", async () => {
  const engine = await load(fixture());
  const whole = engine.render(22000);
  engine.reset();
  const chunks = [1, 1000, 7199, 13800].map((n) => engine.render(n));
  const combined = Float32Array.from(chunks.flatMap((chunk) => [...chunk]));
  assert.deepEqual(combined, whole);
});

test("diesel peak and component energy exclude inaudible filter warmup", async () => {
  const engine = await load(fixture());
  let diagnostics = engine.diagnostics().runtime;
  assert.equal(diagnostics.peak, 0);
  assert.equal(diagnostics.blockRms, 0);
  assert.equal(diagnostics.exhaustRms, 0);
  const pcm = engine.render(3840);
  const peak = pcm.reduce((value, x) => Math.max(value, Math.abs(x)), 0);
  diagnostics = engine.diagnostics().runtime;
  assert.ok(Math.abs(diagnostics.peak - peak) < 1e-7);
  assert.ok(diagnostics.blockRms > 0);
  assert.ok(diagnostics.exhaustRms > 0);
  engine.reset();
  assert.equal(engine.diagnostics().runtime.peak, 0);
});

test(
    "diesel streaming follows dense RPM/fuel endpoints with fixed latency",
    async () => {
      const engine = await load(fixture());
      const initial = engine.process({rpm: 900, throttle01: 0.2, load01: 0.2}, 127);
      assert.ok(initial.every((x) => x === 0));
      for (let i = 0; i < 90; ++i) {
        const n = i % 2 ? 1237 : 997;
        const pcm =
            engine.process({rpm: 900 + 6 * i, throttle01: 0.5, load01: i / 100}, n);
        assert.equal(pcm.length, n);
        assert.ok(pcm.every(Number.isFinite));
      }
      assert.deepEqual(engine.diagnostics().runtime.structuralFrequenciesHz, [1900]);
      const before = engine.diagnostics();
      assert.throws(() => engine.process({rpm: 1000, throttle01: 0.5, load01: NaN}));
      assert.deepEqual(engine.diagnostics(), before);
    });

test(
    "modal transfer matches independent unity resonant gain and attenuates off-resonant forcing",
    () => {
      const gain = (frequency) => {
        const f = new DieselBiquad("bandpass", 1900, 8);
        let inPower = 0;
        let outPower = 0;
        for (let i = 0; i < 96000; ++i) {
          const x = Math.sin(2 * Math.PI * frequency * i / 192000);
          const y = f.process(x);
          if (i > 48000) {
            inPower += x * x;
            outPower += y * y;
          }
        }
        return Math.sqrt(outPower / inPower);
      };
      assert.ok(Math.abs(gain(1900) - 1) < 0.001);
      assert.ok(gain(950) < 0.1);
      assert.ok(gain(3800) < 0.1);
    });

test(
    "diesel loader rejects malformed model, grid, transfer, and authenticated samples",
    async () => {
      const mutations = [
        (m) => {
          m.presentation.articulation.block_highpass_hz = 0;
        },
        (m) => {
          m.presentation.articulation.variation_seed = 1.5;
        },
        (m) => {
          m.presentation.articulation.cycle_timing_spread_deg = 100;
        },
        (m) => {
          m.version = 1;
        },
        (m) => {
          m.version = 3;
        },
        (m) => {
          m.load_coordinate = "manifold-pressure-pa-absolute";
        },
        (m) => {
          m.cells[1].fueling_01 = 0;
        },
        (m) => {
          m.cells.pop();
        },
        (m) => {
          m.presentation.block_modes[0].frequency_hz = 190000;
        },
        (m) => {
          m.presentation.block_modes[0].q = -1;
        },
        (m) => {
          m.cylinders.push({...m.cylinders[0]});
        },
        (m) => {
          m.engine = "unbound-engine";
        },
        (m) => {
          m.sources.byte_count -= 4;
        },
        (_, bytes) => {
          bytes.writeFloatLE(NaN, 0);
        },
        (m) => {
          m.methods[0] = "unimplemented-method";
        },
        (m) => {
          m.source_sha256 = "c".repeat(64);
        },
      ];
      for (const mutate of mutations) await assert.rejects(load(fixture(mutate)));
    });


test(
    "legacy diesel carriers retain their original presentation admission", async () => {
      const engine = await load(fixture((m) => {
        m.version = 1;
        m.methods[2] = "pressure-force-modal-radiation-v1";
        delete m.presentation.articulation;
      }));
      assert.ok(engine.render(192000).every(Number.isFinite));
    });

test(
    "cycle variation is repeatable across reads but differs between firing cycles",
    async () => {
      const engine = await load(fixture());
      engine.setOperatingPoint({rpm: 800, throttle01: 0.5, load01: 0.5});
      engine.reset();
      const pcm = engine.render(192000);
      const cycleFrames = 28800;
      const a = pcm.subarray(cycleFrames * 2, cycleFrames * 3);
      const b = pcm.subarray(cycleFrames * 3, cycleFrames * 4);
      const difference = rms(a.map((x, i) => x - b[i]));
      assert.ok(difference > rms(a) * 0.01, "firing cycles must not be a cloned buzz");
      const other = await load(fixture((m) => {
        m.presentation.articulation.variation_seed++;
      }));
      other.setOperatingPoint({rpm: 800, throttle01: 0.5, load01: 0.5});
      other.reset();
      assert.notDeepEqual(other.render(192000), pcm);
      engine.reset();
      assert.deepEqual(engine.render(192000), pcm);
    });

test(
    "block highpass rejects slow pressure motion while retaining the clatter band",
    () => {
      const gain = (hz) => {
        const filter = new DieselBiquad("highpass", 450, Math.SQRT1_2);
        let energy = 0;
        for (let i = 0; i < 96000; ++i) {
          const y = filter.process(Math.sin(2 * Math.PI * hz * i / 192000));
          if (i >= 48000) energy += y * y;
        }
        return Math.sqrt(2 * energy / 48000);
      };
      assert.ok(gain(50) < 0.02);
      assert.ok(gain(2000) > 0.99);
    });


function exhaustFixture(mutate = () => {}) {
  return fixture((m, samples) => {
    m.version = 3;
    m.methods[2] = "pressure-force-modal-radiation-v3";
    m.presentation.exhaust_system = {
      branches: [{
        id: "bank",
        cylinder_ids: ["one"],
        delay_s: 0.0038,
        reflection: -0.65,
        loss_lowpass_hz: 3200,
        gain: 1
      }],
      radiation_gain_pcm_s2_per_kg: 0.008,
      flow_noise_gain_pcm_per_kg_s: 8,
      noise_highpass_hz: 450,
      noise_seed: 2384320
    };
    mutate(m, samples);
  });
}

function texturedFixture(mutate = () => {}) {
  return exhaustFixture((m, samples) => {
    m.version = 5;
    m.methods[2] = "pressure-force-modal-radiation-v5";
    m.presentation.block_modes[0].gain_pcm_per_n = 0.000001;
    m.presentation.pressure_texture = {
      decay_s: 0.018, coherent_gain: 0.08, compression_gain: 0.12,
      stochastic_gain: 16, seed: 238650
    };
    mutate(m, samples);
  });
}

test("pressure texture separates motoring, varies burns and decays after fuel cut", () => {
  const options = {
    decay_s: 0.018, coherent_gain: 0, compression_gain: 0, stochastic_gain: 16, seed: 238650
  };
  for (const rate of [24000, 192000]) {
    const texture = new DieselPressureTexture(options, [{id: "one"}, {id: "two"}], rate);
    for (let i = 0; i < rate / 10; ++i) {
      const motored = 30000 * Math.sin(i / rate * 100);
      assert.equal(texture.process(0, motored, motored), 0,
          "compression alone cannot generate combustion texture");
    }
    texture.reset();
    const run = () => {
      const out = new Float64Array(rate);
      const other = new Float64Array(rate);
      for (let i = 0; i < rate; ++i) {
        const phase = i % (rate / 10);
        const pressure = i < rate / 2 && phase < rate / 500 ? 10000 : 0;
        out[i] = texture.process(0, pressure, 0);
        other[i] = texture.process(1, pressure, 0);
      }
      assert.notDeepEqual(out, other, "cylinders use independent stochastic excitation");
      return out;
    };
    const first = run();
    assert.ok(rms(first.subarray(0, rate / 2)) > 100);
    assert.ok(rms(first.subarray(rate * 0.9)) < 0.001,
        "fuel cut leaves only a decaying tail");
    assert.notDeepEqual(first.subarray(0, rate / 10), first.subarray(rate / 10, rate / 5),
        "identical pressure events must have different fine structure");
    texture.reset();
    assert.deepEqual(run(), first);
  }
});

test("textured native layout replays across chunks, controls and reset", async () => {
  const engine = await load(texturedFixture());
  const whole = engine.render(45000);
  engine.reset();
  const split = [1, 127, 8193, 36679].map(n => engine.render(n));
  assert.deepEqual(Float32Array.from(split.flatMap(x => [...x])), whole);
  const different = await load(texturedFixture(m => m.presentation.pressure_texture.seed++));
  assert.notDeepEqual(different.render(45000), whole);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 800, load01: 0, throttle01: 0});
    engine.reset();
    return Array.from({length: 30}, (_, i) => engine.process({
      rpm: 800 + i * 20, load01: i > 20 ? 0 : i / 30, throttle01: i / 30
    }));
  };
  const first = sequence();
  assert.ok(first.every(block => block.every(x => Number.isFinite(x) && Math.abs(x) <= 1)));
  assert.deepEqual(sequence(), first);
});

test("pressure texture admission rejects malformed and unversioned parameters", async () => {
  for (const mutate of [
    m => { delete m.presentation.pressure_texture; },
    m => { delete m.presentation.exhaust_system; },
    m => { m.presentation.pressure_texture.decay_s = 0; },
    m => { m.presentation.pressure_texture.coherent_gain = 2; },
    m => { m.presentation.pressure_texture.compression_gain = -1; },
    m => { m.presentation.pressure_texture.stochastic_gain = 33; },
    m => { m.presentation.pressure_texture.seed = 1.5; },
    m => { m.presentation.pressure_texture.extra = 1; },
    m => { m.version = 3; m.methods[2] = "pressure-force-modal-radiation-v3"; },
    m => { m.version = 4; m.methods[2] = "pressure-force-modal-radiation-v4"; }
  ]) await assert.rejects(load(texturedFixture(mutate)));
});

test("bank exhaust, radiation and stochastic flow texture replay exactly", async () => {
  const engine = await load(exhaustFixture());
  const whole = engine.render(45000);
  assert.ok(whole.every((x) => Number.isFinite(x) && Math.abs(x) <= 1));
  engine.reset();
  const split = [127, 8193, 777, 35903].map((n) => engine.render(n));
  assert.deepEqual(Float32Array.from(split.flatMap((x) => [...x])), whole);
  const different = await load(exhaustFixture((m) => {
    m.presentation.exhaust_system.noise_seed++;
  }));
  assert.notDeepEqual(different.render(45000), whole);
});

test("exhaust branch paths are causal, decay, and add no noise without flow", () => {
  const options = {
    branches: [{
      id: "bank",
      cylinder_ids: ["one"],
      delay_s: 0.0038,
      reflection: 0,
      loss_lowpass_hz: 3200,
      gain: 1
    }],
    radiation_gain_pcm_s2_per_kg: 0.008,
    flow_noise_gain_pcm_per_kg_s: 8,
    noise_highpass_hz: 450,
    noise_seed: 2384320
  };
  for (const rate of [48000, 192000]) {
    const system = new DieselExhaustSystem(options, [{id: "one"}], rate);
    for (let i = 0; i < 100; ++i) {
      system.process([0]);
      assert.equal(system.flow, 0);
      assert.equal(system.noise, 0);
    }
    system.reset();
    let integral = 0;
    let first = null;
    for (let i = 0; i < rate / 4; ++i) {
      system.process([i === 0 ? 1 : 0]);
      integral += system.flow;
      if (first === null && Math.abs(system.flow) > 1e-12) first = i;
    }
    assert.equal(first, Math.floor(0.0038 * rate));
    assert.ok(Math.abs(integral - 1) < 1e-6, "unity DC gain with no reflection");
    assert.ok(Math.abs(system.flow) < 1e-9);
  }
  for (const reflection of [-0.9, 0.9]) {
    const system = new DieselExhaustSystem(
        {...options, branches: [{...options.branches[0], reflection}]}, [{id: "one"}],
        48000);
    let peak = 0;
    let tailEnergy = 0;
    for (let i = 0; i < 96000; ++i) {
      system.process([i === 0 ? 1 : 0]);
      peak = Math.max(peak, Math.abs(system.flow));
      if (i >= 48000) tailEnergy += system.flow ** 2;
    }
    assert.ok(peak < 1 && tailEnergy < 1e-8, "bounded reflection must decay");
  }
});

test(
    "exhaust admission requires a complete unique routing and bounded feedback",
    async () => {
      for (const mutate
               of [(e) => {
                 e.branches[0].reflection = 1;
               },
                  (e) => {
                    e.branches[0].delay_s = 0;
                  },
                  (e) => {
                    e.branches[0].cylinder_ids = ["unknown"];
                  },
                  (e) => {
                    e.branches[0].cylinder_ids = [];
                  },
                  (e) => {
                    e.branches.push({...e.branches[0], id: "second"});
                  },
                  (e) => {
                    e.noise_seed = NaN;
                  },
                  (e) => {
                    e.radiation_gain_pcm_s2_per_kg = -1;
                  },
      ])
        await assert.rejects(
            load(exhaustFixture((m) => mutate(m.presentation.exhaust_system))));
    });

const mechanicalParameters = {
  valve_gain_pcm: 0.16, valve_frequency_hz: 1050, valve_decay_s: 0.0018,
  pumping_gain_pcm: 0.045, pumping_lowpass_hz: 850, seed: 4320725
};
const mechanicalTiming = {
  intake_open_angle_rad: Math.PI,
  intake_close_angle_rad: 3 * Math.PI,
  exhaust_close_angle_rad: Math.PI
};
function mechanicalFixture(mutate = () => {}) {
  return texturedFixture((m, samples) => {
    m.version = 6;
    m.methods[2] = 'pressure-force-modal-radiation-v6';
    m.mechanical_timing = {...mechanicalTiming};
    m.presentation.mechanical_texture = {...mechanicalParameters};
    mutate(m, samples);
  });
}

test('mechanical closures follow cylinder phases and stop exciting at a stationary crank', () => {
  for (const rate of [24000, 192000]) {
    const texture = new DieselMechanicalTexture(mechanicalParameters, mechanicalTiming,
        [{id: 'one', firing_angle_rad: 0, structural_gain: 1}], rate);
    const run = () => {
      const out = new Float64Array(rate);
      for (let i = 0; i < rate; ++i) out[i] = texture.process(2 * i / rate, 120);
      assert.equal(texture.events, 2, 'one intake and one exhaust closure per 720 degrees');
      assert.equal(rms(out.subarray(0, rate / 4)), 0, 'nothing before first valve opening/closure');
      return out;
    };
    const first = run();
    texture.reset();
    assert.deepEqual(run(), first);
    const stopped = new Float64Array(rate / 4);
    for (let i = 0; i < stopped.length; ++i) stopped[i] = texture.process(2 - 2 / rate, 0);
    assert.equal(texture.events, 2, 'stationary crank adds no new impacts');
    assert.ok(rms(stopped.subarray(rate / 5)) < 1e-12, 'only physical-time filter tails remain');
    texture.reset();
    for (let i = 0; i < rate; ++i) texture.process(4 * i / rate, 240);
    assert.equal(texture.events, 4, 'doubling RPM doubles event count');
    const shifted = new DieselMechanicalTexture(mechanicalParameters, mechanicalTiming,
        [{id: 'one', firing_angle_rad: Math.PI, structural_gain: 1}], rate);
    for (let i = 0; i < rate / 2; ++i) shifted.process(2 * i / rate, 120);
    assert.equal(shifted.events, 0, 'cylinder binding shifts both closures');
  }
});

test('mechanical layers survive fuel cut without changing their excitation', async () => {
  const engine = await load(mechanicalFixture());
  const stats = [];
  for (const load01 of [0, 1]) {
    engine.setOperatingPoint({rpm: 1000, load01, throttle01: load01});
    engine.reset();
    engine.render(192000);
    stats.push(engine.diagnostics().runtime);
  }
  assert.ok(stats[0].valveRms > 0.001 && stats[0].pumpingRms > 0.001);
  assert.equal(stats[0].valveRms, stats[1].valveRms);
  assert.equal(stats[0].pumpingRms, stats[1].pumpingRms);
  assert.ok(stats[0].blockRms < stats[1].blockRms,
      'combustion still drops away when mechanical sources continue');
});

test('muting new sources recovers version 5 PCM exactly at idle, load and coast', async () => {
  const old = await load(texturedFixture());
  const next = await load(mechanicalFixture(m => {
    m.presentation.mechanical_texture.valve_gain_pcm = 0;
    m.presentation.mechanical_texture.pumping_gain_pcm = 0;
  }));
  for (const [rpm, load01] of [[800, 0.12], [1600, 1], [1600, 0]]) {
    for (const e of [old, next]) { e.setOperatingPoint({rpm, load01, throttle01: load01}); e.reset(); }
    assert.deepEqual(next.render(45000), old.render(45000));
  }
});

test('mechanical events and noise replay across read partitions and dense controls', async () => {
  const engine = await load(mechanicalFixture());
  const whole = engine.render(45000);
  engine.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...engine.render(n)])), whole);
  const other = await load(mechanicalFixture(m => m.presentation.mechanical_texture.seed++));
  assert.notDeepEqual(other.render(45000), whole);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 800, load01: 0.12, throttle01: 0}); engine.reset();
    return Array.from({length: 60}, (_, i) => engine.process({
      rpm: 800 + (i < 30 ? i : 60 - i) * 20,
      load01: i < 30 ? 0.6 : 0, throttle01: i < 30 ? 0.6 : 0
    }));
  };
  const first = sequence();
  assert.ok(first.every(block => block.every(x => Number.isFinite(x) && Math.abs(x) < 1)));
  assert.deepEqual(sequence(), first);
});

test('mechanical runtime strictly admits timing, bounds and version dependencies', async () => {
  for (const mutate of [
    m => { delete m.mechanical_timing; },
    m => { delete m.presentation.mechanical_texture; },
    m => { delete m.presentation.pressure_texture; },
    m => { m.mechanical_timing.intake_open_angle_rad = m.mechanical_timing.intake_close_angle_rad; },
    m => { m.mechanical_timing.exhaust_close_angle_rad = 13; },
    m => { m.mechanical_timing.extra = 1; },
    m => { m.presentation.mechanical_texture.valve_gain_pcm = -1; },
    m => { m.presentation.mechanical_texture.valve_frequency_hz = 9000; },
    m => { m.presentation.mechanical_texture.valve_decay_s = 0.1; },
    m => { m.presentation.mechanical_texture.pumping_gain_pcm = 1; },
    m => { m.presentation.mechanical_texture.pumping_lowpass_hz = 0; },
    m => { m.presentation.mechanical_texture.seed = 0.5; },
    m => { m.presentation.mechanical_texture.extra = 1; },
    m => { m.version = 5; m.methods[2] = 'pressure-force-modal-radiation-v5'; },
    m => { m.version = 7; m.methods[2] = 'pressure-force-modal-radiation-v7'; }
  ]) await assert.rejects(load(mechanicalFixture(mutate)));
});

function smoothMechanicalFixture(mutate = () => {}) {
  return mechanicalFixture((m, samples) => {
    m.version = 7;
    m.methods[2] = 'pressure-force-modal-radiation-v7';
    m.presentation.mechanical_texture.valve_attack_s = 0.0012;
    mutate(m, samples);
  });
}

test('finite valve contact removes the onset step at matched event energy', () => {
  for (const rate of [24000, 192000]) {
    const render = attack => {
      const parameters = {...mechanicalParameters, pumping_gain_pcm: 0};
      if (attack) parameters.valve_attack_s = attack;
      const texture = new DieselMechanicalTexture(parameters, mechanicalTiming,
          [{id: 'one', firing_angle_rad: 0, structural_gain: 1}], rate);
      const pcm = Float64Array.from({length: rate / 3}, (_, i) => texture.process(2 * i / rate, 120));
      assert.equal(texture.events, 1);
      return pcm.slice(rate / 4, rate / 4 + Math.round(rate * 0.02));
    };
    const instant = render(0), contact = render(0.0012);
    assert.equal(contact[0], 0);
    assert.equal(contact[1], 0, 'zero force and slope at contact onset');
    const maxStep = x => x.reduce((max, sample, i) => i ? Math.max(max, Math.abs(sample - x[i - 1])) : max, 0);
    assert.ok(rms(contact) > rms(instant) * 0.1, 'smoothing retains a mechanical event');
    assert.ok(maxStep(contact) / rms(contact) < maxStep(instant) / rms(instant) * 0.5,
        'the onset is smoother even after equalizing event RMS');
    // A finite force pulse preserves timing and decay when the rendering rate changes.
    assert.ok(rms(contact.slice(-Math.round(rate * 0.005))) < rms(contact) * 0.001);
  }
});

test('smooth mechanical excitation remains independent of fuel and replays exactly', async () => {
  const engine = await load(smoothMechanicalFixture());
  const first = engine.render(45000);
  engine.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...engine.render(n)])), first);
  const stats = [];
  for (const load01 of [1, 0]) {
    engine.setOperatingPoint({rpm: 1000, load01, throttle01: load01}); engine.reset();
    engine.render(96000); stats.push(engine.diagnostics().runtime);
  }
  assert.equal(stats[0].valveRms, stats[1].valveRms);
  assert.equal(stats[0].pumpingRms, stats[1].pumpingRms);
  assert.ok(stats[1].valveRms > 0);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 800, load01: 0.12, throttle01: 0}); engine.reset();
    return Array.from({length: 60}, (_, i) => engine.process({
      rpm: 800 + (i < 30 ? i : 60 - i) * 20,
      load01: i < 30 ? 0.6 : 0, throttle01: i < 30 ? 0.6 : 0
    }));
  };
  assert.deepEqual(sequence(), sequence());
});

test('smooth valve contact requires version 7 and a bounded duration', async () => {
  for (const mutate of [
    m => { delete m.presentation.mechanical_texture.valve_attack_s; },
    m => { m.presentation.mechanical_texture.valve_attack_s = 0; },
    m => { m.presentation.mechanical_texture.valve_attack_s = 0.02; },
    m => { m.version = 6; m.methods[2] = 'pressure-force-modal-radiation-v6'; }
  ]) await assert.rejects(load(smoothMechanicalFixture(mutate)));
});

const coastingParameters = {
  fueling_threshold_01: 0.2, gain: 0.16, lowpass_hz: 550, transition_s: 0.04
};
function coastingFixture(mutate = () => {}) {
  return smoothMechanicalFixture((m, samples) => {
    m.version = 8;
    m.methods[2] = 'pressure-force-modal-radiation-v8';
    m.presentation.low_fuel_exhaust = {...coastingParameters};
    mutate(m, samples);
  });
}

test('low-fuel exhaust bypasses loaded firing and softens fuel cut without a gain step', () => {
  for (const rate of [24000, 192000]) {
    const coast = new DieselLowFuelExhaust(coastingParameters, rate);
    for (let i = 0; i < rate / 10; ++i) assert.equal(coast.process(1, 0.3), 1);
    const transition = Float64Array.from({length: rate / 2}, () => coast.process(1, 0));
    const stepLimit = (1 - coastingParameters.gain) / (rate * coastingParameters.transition_s);
    assert.ok(1 - transition[0] <= stepLimit * 1.001, 'fuel cut cannot switch gain instantaneously');
    for (let i = 1; i < transition.length; ++i)
      assert.ok(Math.abs(transition[i] - transition[i - 1]) <= stepLimit * 1.001);
    assert.ok(Math.abs(transition.at(-1) - coastingParameters.gain) < 0.00001);
    const recover = Float64Array.from({length: rate}, () => coast.process(1, 0.3));
    assert.ok(Math.abs(recover[0] - transition.at(-1)) < stepLimit * 1.001);
    assert.equal(recover.at(-1), 1, 'restored firing eventually returns to exact bypass');
    coast.reset();
    for (let i = 0; i < rate / 10; ++i) assert.equal(coast.process(0, 0), 0);
  }
});

test('low-fuel exhaust preserves loaded PCM and keeps mechanical layers at idle and on coast', async () => {
  const previous = await load(smoothMechanicalFixture());
  const current = await load(coastingFixture());
  for (const [rpm, load01] of [[800, 0.2], [1200, 0.3], [1600, 0.85]]) {
    for (const e of [previous, current]) { e.setOperatingPoint({rpm, load01, throttle01: load01}); e.reset(); }
    assert.deepEqual(current.render(45000), previous.render(45000));
  }
  const stats = [];
  for (const e of [previous, current]) {
    e.setOperatingPoint({rpm: 1600, load01: 0, throttle01: 0}); e.reset();
    e.render(192000); stats.push(e.diagnostics().runtime);
  }
  for (const stem of ['blockRms', 'valveRms', 'pumpingRms']) assert.equal(stats[0][stem], stats[1][stem]);
  assert.ok(stats[1].exhaustRms > 0, 'coasting keeps a pumped-air exhaust contribution');
  assert.ok(stats[1].exhaustRms < stats[0].exhaustRms * 0.2);
  const idle = [];
  for (const e of [previous, current]) {
    e.setOperatingPoint({rpm: 800, load01: 0.12, throttle01: 0}); e.reset();
    e.render(192000); idle.push(e.diagnostics().runtime);
  }
  for (const stem of ['blockRms', 'valveRms', 'pumpingRms']) assert.equal(idle[0][stem], idle[1][stem]);
  assert.ok(idle[1].exhaustRms < idle[0].exhaustRms * 0.9, 'idle receives some exhaust reduction');
  assert.ok(idle[1].exhaustRms > idle[0].exhaustRms * 0.5, 'idle retains more exhaust than full coast');
});

test('coast entry and fuel restoration replay across reset and read partitions', async () => {
  const engine = await load(coastingFixture());
  engine.setOperatingPoint({rpm: 1200, load01: 0, throttle01: 0}); engine.reset();
  const first = engine.render(45000); engine.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...engine.render(n)])), first);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 1600, load01: 0.85, throttle01: 0.85}); engine.reset();
    return Array.from({length: 75}, (_, i) => engine.process({
      rpm: 1600 - Math.min(i, 40) * 20,
      load01: i < 10 ? 0.85 : i < 50 ? 0 : 0.12, throttle01: i < 10 ? 0.85 : 0
    }));
  };
  const a = sequence();
  assert.ok(a.every(block => block.every(x => Number.isFinite(x) && Math.abs(x) < 1)));
  assert.deepEqual(sequence(), a);
});

test('coasting transfer requires version 8 and bounded parameters', async () => {
  for (const mutate of [
    m => { delete m.presentation.low_fuel_exhaust; },
    m => { delete m.presentation.mechanical_texture.valve_attack_s; },
    m => { m.presentation.low_fuel_exhaust.fueling_threshold_01 = 0; },
    m => { m.presentation.low_fuel_exhaust.gain = 2; },
    m => { m.presentation.low_fuel_exhaust.lowpass_hz = 0; },
    m => { m.presentation.low_fuel_exhaust.transition_s = 0; },
    m => { m.presentation.low_fuel_exhaust.extra = 1; },
    m => { m.version = 7; m.methods[2] = 'pressure-force-modal-radiation-v7'; },
    m => { m.version = 9; m.methods[2] = 'pressure-force-modal-radiation-v9'; }
  ]) await assert.rejects(load(coastingFixture(mutate)));
});

const coastBalanceParameters = {
  fueling_threshold_01: 0.06, bass_gain: 0.18, movement_gain: 0.48,
  crossover_hz: 180, mechanical_gain: 1.6, transition_s: 0.04
};
function balancedCoastFixture(mutate = () => {}) {
  return coastingFixture((m, samples) => {
    m.version = 9;
    m.methods[2] = 'pressure-force-modal-radiation-v9';
    m.presentation.coasting_balance = {...coastBalanceParameters};
    mutate(m, samples);
  });
}

test('coasting split remains complementary and fades mechanical gain in physical time', () => {
  for (const rate of [24000, 192000]) {
    const flat = new DieselCoastingBalance({...coastBalanceParameters, bass_gain: 0.4, movement_gain: 0.4}, rate);
    for (let i = 0; i < rate / 10; ++i) {
      const source = Math.sin(i / rate * 2 * Math.PI * 500);
      assert.ok(Math.abs(flat.process(source, 0, 0) - 0.4 * source) < 1e-15,
          'equal gains must not color the signal');
    }
    const balance = new DieselCoastingBalance(coastBalanceParameters, rate);
    for (let i = 0; i < rate / 10; ++i) {
      assert.equal(balance.process(1, 0.16, 0.12), 0.16);
      assert.equal(balance.mechanicalGain, 1);
    }
    let previous = 1;
    for (let i = 0; i < rate; ++i) {
      balance.process(1, 0.16, 0);
      assert.ok(balance.mechanicalGain - previous < 0.601 / (rate * 0.04));
      previous = balance.mechanicalGain;
    }
    assert.equal(balance.mechanicalGain, 1.6);
    for (let i = 0; i < rate; ++i) balance.process(1, 0.16, 0.12);
    assert.equal(balance.mechanicalGain, 1);
    assert.equal(balance.process(1, 0.16, 0.12), 0.16, 'idle returns to exact bypass');
    balance.reset();
    assert.equal(balance.process(0, 0, 0), 0);
  }
});

test('coasting balance preserves quieter idle and loaded PCM while restoring movement and mechanics', async () => {
  const previous = await load(coastingFixture()), current = await load(balancedCoastFixture());
  for (const [rpm, load01] of [[800, 0.12], [1200, 0.3], [1600, 0.85]]) {
    for (const e of [previous, current]) {e.setOperatingPoint({rpm, load01, throttle01: load01});e.reset();}
    assert.deepEqual(current.render(45000), previous.render(45000));
  }
  const stats = [];
  for (const e of [previous, current]) {
    e.setOperatingPoint({rpm: 1600, load01: 0, throttle01: 0});e.reset();e.render(192000);
    stats.push(e.diagnostics().runtime);
  }
  assert.equal(stats[0].blockRms, stats[1].blockRms, 'no extra combustion is added on coast');
  for (const stem of ['valveRms', 'pumpingRms'])
    assert.ok(Math.abs(stats[1][stem] / stats[0][stem] - 1.6) < 1e-10);
  assert.ok(stats[1].exhaustRms > stats[0].exhaustRms, 'exhaust movement is restored');
});

test('balanced coasting replays exactly across partitions, fuel cut, and restoration', async () => {
  const engine = await load(balancedCoastFixture());
  engine.setOperatingPoint({rpm: 1200, load01: 0, throttle01: 0});engine.reset();
  const first = engine.render(45000);engine.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...engine.render(n)])), first);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 1600, load01: 0.85, throttle01: 0.85});engine.reset();
    return Array.from({length: 75}, (_, i) => engine.process({
      rpm: 1600 - Math.min(i, 40) * 20,
      load01: i < 10 ? 0.85 : i < 50 ? 0 : 0.12, throttle01: i < 10 ? 0.85 : 0
    }));
  };
  const a = sequence();
  assert.ok(a.every(block => block.every(x => Number.isFinite(x) && Math.abs(x) < 1)));
  assert.deepEqual(sequence(), a);
});

test('coasting balance requires version 9 and bounded parameters', async () => {
  for (const mutate of [
    m => {delete m.presentation.coasting_balance;},
    m => {delete m.presentation.low_fuel_exhaust;},
    m => {m.presentation.coasting_balance.fueling_threshold_01 = 0;},
    m => {m.presentation.coasting_balance.bass_gain = -1;},
    m => {m.presentation.coasting_balance.movement_gain = 2;},
    m => {m.presentation.coasting_balance.crossover_hz = 0;},
    m => {m.presentation.coasting_balance.mechanical_gain = 3;},
    m => {m.presentation.coasting_balance.transition_s = 0;},
    m => {m.presentation.coasting_balance.extra = 1;},
    m => {m.version = 8;m.methods[2] = 'pressure-force-modal-radiation-v8';},
    m => {m.version = 10;m.methods[2] = 'pressure-force-modal-radiation-v10';}
  ]) await assert.rejects(load(balancedCoastFixture(mutate)));
});

const coastingFlowParameters = {
  gain_pcm_per_kg_s: 0.8, highpass_hz: 250, lowpass_hz: 1800,
  envelope_s: 0.002, seed: 432008
};
function flowingCoastFixture(mutate = () => {}) {
  return balancedCoastFixture((m, samples) => {
    m.version = 10;
    m.methods[2] = 'pressure-force-modal-radiation-v10';
    m.presentation.coasting_flow_texture = {...coastingFlowParameters};
    mutate(m, samples);
  });
}

test('coasting texture follows outward gas flow, decays at rest, and replays by seed', () => {
  const energies = [];
  for (const rate of [24000, 192000]) {
    const make = () => new DieselCoastingFlowTexture(coastingFlowParameters,
        [{id: 'right', cylinder_ids: ['one'], gain: 1}], [{id: 'one'}], rate);
    const a = make(), b = make();
    for (let i = 0; i < 100; i++) assert.equal(a.process([0]), 0);
    a.reset();
    const signal = new Float64Array(rate), doubled = new Float64Array(rate);
    for (let i = 0; i < rate; i++) {
      const flow = 0.03 * (1 + Math.sin(2 * Math.PI * 47 * i / rate));
      signal[i] = a.process([flow]);doubled[i] = b.process([flow * 2]);
    }
    assert.ok(rms(signal) > 0.005);
    assert.deepEqual(doubled, signal.map(x => x * 2), 'amplitude follows captured flow');
    energies.push(rms(signal));
    a.reset();
    for (let i = 0; i < rate; i++) {
      assert.equal(a.process([0.03 * (1 + Math.sin(2 * Math.PI * 47 * i / rate))]), signal[i]);
    }
    let tail;
    for (let i = 0; i < rate / 2; i++) tail = a.process([0]);
    assert.ok(Math.abs(tail) < 1e-12, 'no permanent noise bed after gas flow ends');
    a.reset();
    for (let i = 0; i < 1000; i++) assert.equal(a.process([-0.1]), 0, 'reverse flow is not an outward jet');
  }
  assert.ok(energies[1] / energies[0] > 0.9 && energies[1] / energies[0] < 1.1,
      'authored texture level is consistent across sample rates');
});

test('coasting flow respects bank routing and changes its fine structure with the seed', () => {
  const make = (seed) => new DieselCoastingFlowTexture({...coastingFlowParameters, seed},
      [{id: 'right', cylinder_ids: ['two'], gain: 1}], [{id: 'one'}, {id: 'two'}], 24000);
  const a = make(10), b = make(11);
  for (let i = 0; i < 1000; i++) assert.equal(a.process([1, 0]), 0);
  a.reset();
  const first = Float64Array.from({length: 24000}, () => a.process([0, 0.04]));
  const second = Float64Array.from({length: 24000}, () => b.process([0, 0.04]));
  assert.notDeepEqual(first, second);
  assert.ok(rms(first) / rms(second) > 0.9 && rms(first) / rms(second) < 1.1);
});

test('flowing coast preserves fueled PCM and existing mechanics while adding only exhaust energy', async () => {
  const previous = await load(balancedCoastFixture()), current = await load(flowingCoastFixture());
  for (const fuel of [0.06, 0.12, 0.65, 1]) {
    for (const e of [previous, current]) {e.setOperatingPoint({rpm: 1200, load01: fuel, throttle01: 0});e.reset();}
    assert.deepEqual(current.render(45000), previous.render(45000));
    assert.equal(current.diagnostics().runtime.coastingFlowRms, 0);
  }
  const pcm = [], stats = [];
  for (const e of [previous, current]) {
    e.setOperatingPoint({rpm: 1600, load01: 0, throttle01: 0});e.reset();
    pcm.push(e.render(192000));stats.push(e.diagnostics().runtime);
  }
  assert.notDeepEqual(pcm[0], pcm[1]);
  assert.ok(stats[1].coastingFlowRms > 0);
  assert.equal(stats[0].coastingFlowRms, 0);
  for (const stem of ['blockRms', 'valveRms', 'pumpingRms']) assert.equal(stats[0][stem], stats[1][stem]);
  assert.ok(stats[1].exhaustRms > stats[0].exhaustRms);
});

test('flowing coast stays deterministic across read partitions and repeated gear changes', async () => {
  const engine = await load(flowingCoastFixture());
  engine.setOperatingPoint({rpm: 1200, load01: 0, throttle01: 0});engine.reset();
  const whole = engine.render(45000);engine.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...engine.render(n)])), whole);
  const sequence = () => {
    engine.setOperatingPoint({rpm: 1600, load01: 0.85, throttle01: 0.85});engine.reset();
    return Array.from({length: 100}, (_, i) => engine.process({
      rpm: 1600 - Math.min(i % 50, 30) * 20,
      load01: i % 50 < 10 || i % 50 >= 35 ? 0.85 : 0,
      throttle01: i % 50 < 10 || i % 50 >= 35 ? 0.85 : 0
    }));
  };
  const first = sequence();
  assert.ok(first.every(block => block.every(x => Number.isFinite(x) && Math.abs(x) < 1)));
  assert.deepEqual(sequence(), first);
});

test('coasting flow requires version 10, its dependencies and a bounded spectrum', async () => {
  for (const mutate of [
    m => {delete m.presentation.coasting_flow_texture;},
    m => {delete m.presentation.coasting_balance;},
    m => {m.version = 9;m.methods[2] = 'pressure-force-modal-radiation-v9';},
    m => {m.version = 11;m.methods[2] = 'pressure-force-modal-radiation-v11';},
    m => {m.presentation.coasting_flow_texture.gain_pcm_per_kg_s = 5;},
    m => {m.presentation.coasting_flow_texture.highpass_hz = 0;},
    m => {m.presentation.coasting_flow_texture.lowpass_hz = 4000;},
    m => {m.presentation.coasting_flow_texture.highpass_hz = 800;m.presentation.coasting_flow_texture.lowpass_hz = 800;},
    m => {m.presentation.coasting_flow_texture.envelope_s = 0;},
    m => {m.presentation.coasting_flow_texture.seed = 0.5;},
    m => {m.presentation.coasting_flow_texture.extra = 1;}
  ]) await assert.rejects(load(flowingCoastFixture(mutate)));
});

const loadedPressureParameters = {decay_s: 0.012, attack_s: 0.004,
  secondary_bank_gain: 0.55, fueling_start_01: 0.2, fueling_full_01: 0.5, transition_s: 0.04};
function roundedLoadedFixture(mutate = () => {}) {
  return flowingCoastFixture((m, samples) => {
    m.version = 11;m.methods[2] = 'pressure-force-modal-radiation-v11';
    m.presentation.loaded_pressure_texture = {...loadedPressureParameters};
    mutate(m, samples);
  });
}

test('rounded loaded texture preserves low fuel PCM and changes only combustion radiation', async () => {
  const prior = await load(flowingCoastFixture()), current = await load(roundedLoadedFixture());
  for (const fuel of [0, 0.06, 0.12, 0.2]) {
    for (const e of [prior, current]) {e.setOperatingPoint({rpm: 1200, load01: fuel, throttle01: 0});e.reset();}
    assert.deepEqual(current.render(45000), prior.render(45000));
  }
  const signals = [], stats = [];
  for (const e of [prior, current]) {
    e.setOperatingPoint({rpm: 1600, load01: 0.75, throttle01: 0.75});e.reset();
    signals.push(e.render(192000));stats.push(e.diagnostics().runtime);
  }
  assert.notDeepEqual(signals[0], signals[1]);
  assert.notEqual(stats[0].blockRms, stats[1].blockRms);
  for (const stem of ['exhaustRms', 'valveRms', 'pumpingRms', 'coastingFlowRms'])
    assert.equal(stats[0][stem], stats[1][stem]);
});

test('rounded bank excitation has a delayed attack, bounded load fade and no extra motoring force', () => {
  const base = {decay_s: 0.018, coherent_gain: 0, compression_gain: 0.12, stochastic_gain: 17, seed: 123};
  const cylinders = [{id: 'one', structural_gain: 1}, {id: 'two', structural_gain: 0.8}];
  const branches = [{gain: 1, cylinder_ids: ['one']}, {gain: 0.2, cylinder_ids: ['two']}];
  for (const rate of [24000, 192000]) {
    const texture = new DieselLoadedPressureTexture(base, loadedPressureParameters, cylinders, branches, rate);
    texture.setFueling(0.75);
    const pcm = Float64Array.from({length: rate * 0.1}, (_, i) => texture.process(0, i === 0 ? 1000 : 0, 0));
    assert.ok(rms(pcm.subarray(rate * 0.004, rate * 0.006)) > rms(pcm.subarray(0, rate * 0.0005)) * 2,
        'stochastic pulse rises over milliseconds rather than arriving as a bang');
    const energy = texture.weights.reduce((s, w, i) => s + (w * cylinders[i].structural_gain) ** 2, 0);
    assert.ok(Math.abs(energy - 1.64) < 1e-12);
    texture.reset();texture.setFueling(0);
    for (let i = 0; i < rate; ++i) {
      const old = texture.amount;texture.setFueling(0.75);
      assert.ok(texture.amount >= old && texture.amount - old <= 1 / (rate * 0.04));
    }
    assert.equal(texture.amount, 1);
    for (let i = 0; i < rate; ++i) texture.setFueling(0);
    assert.equal(texture.amount, 0);
    texture.reset();texture.setFueling(0.75);
    for (let i = 0; i < 1000; ++i) assert.equal(texture.process(0, 123, 123), 123 * 0.12);
    const silent = new DieselLoadedPressureTexture(base, loadedPressureParameters,
        cylinders.map(c => ({...c, structural_gain: 0})), branches, rate);
    silent.setFueling(1);
    assert.ok(Number.isFinite(silent.process(0, 100, 0)), 'muted structural routing remains finite');
  }
});

test('rounded loaded playback replays across partitions and repeated fuel transitions', async () => {
  const e = await load(roundedLoadedFixture());
  e.setOperatingPoint({rpm: 1200, load01: 0.75, throttle01: 0.75});e.reset();
  const pcm = e.render(45000);e.reset();
  assert.deepEqual(Float32Array.from([1, 127, 8193, 36679].flatMap(n => [...e.render(n)])), pcm);
  const run = () => {
    e.setOperatingPoint({rpm: 1200, load01: 0.12, throttle01: 0});e.reset();
    return Array.from({length: 100}, (_, i) => e.process({rpm: 1200 + (i % 40) * 10,
      load01: i % 50 < 20 ? 0.75 : 0, throttle01: i % 50 < 20 ? 0.75 : 0}));
  };
  const first = run();assert.ok(first.every(b => b.every(x => Number.isFinite(x) && Math.abs(x) < 1)));
  assert.deepEqual(run(), first);
});

test('rounded loaded texture requires version 11 and a valid fade interval', async () => {
  for (const mutate of [
    m => {delete m.presentation.loaded_pressure_texture;},
    m => {delete m.presentation.coasting_flow_texture;},
    m => {m.version = 10;m.methods[2] = 'pressure-force-modal-radiation-v10';},
    m => {m.version = 12;m.methods[2] = 'pressure-force-modal-radiation-v12';},
    m => {m.presentation.loaded_pressure_texture.decay_s = 0;},
    m => {m.presentation.loaded_pressure_texture.attack_s = 0;},
    m => {m.presentation.loaded_pressure_texture.secondary_bank_gain = 2;},
    m => {m.presentation.loaded_pressure_texture.fueling_start_01 = 0;},
    m => {m.presentation.loaded_pressure_texture.fueling_full_01 = 0.2;},
    m => {m.presentation.loaded_pressure_texture.transition_s = 0;},
    m => {m.presentation.loaded_pressure_texture.extra = 1;}
  ]) await assert.rejects(load(roundedLoadedFixture(mutate)));
});

// Version 12 carries a motored force for the SAME charge boundary as each
// fueled cell. This fixture deliberately makes compression depend on load.
function mappedBoundaryFixture(mutate = () => {}, compressionOnly = false) {
  let manifest, legacy;
  roundedLoadedFixture((m, bytes) => {manifest = structuredClone(m);legacy = bytes;});
  const n = manifest.samples_per_cycle;
  const sources = Buffer.alloc(n * 4 * 12);
  manifest.version = 12;
  manifest.methods[2] = 'pressure-force-modal-radiation-v12';
  manifest.sources.layout = 'cell-phase-force-n-exhaust-kg-s-motored-force-n';
  manifest.sources.byte_count = sources.length;
  for (let cell = 0; cell < 4; cell++) {
    manifest.cells[cell].manifold_pressure_pa_abs = 101325 * (1 + cell % 2);
    for (let i = 0; i < n; i++) {
      const at = (cell * n + i) * 12;
      const motored = legacy.readFloatLE((Math.floor(cell / 2) * 2 * n + i) * 8) *
          (compressionOnly ? 1 + cell % 2 : 1);
      sources.writeFloatLE(compressionOnly ? motored : legacy.readFloatLE((cell * n + i) * 8), at);
      sources.writeFloatLE(legacy.readFloatLE((cell * n + i) * 8 + 4), at + 4);
      sources.writeFloatLE(motored, at + 8);
    }
  }
  mutate(manifest, sources);
  manifest.sources.sha256 = digest(sources);
  const runtime = jsonBytes(manifest);
  return packTestCarrier([
    ['crankwave.json', jsonBytes({schema: 'crankwave/crankwave-package', version: 1,
      engine_id: 'test-diesel', runtime: {kind: 'diesel-mechanical-audio-v1',
        manifest_path: 'runtime.json', manifest_sha256: digest(runtime)}})],
    ['engine.json', jsonBytes({fixture: 'synthetic diesel DSP input'})],
    ['runtime.json', runtime], ['sources.f32le', sources]
  ]);
}

test('mapped motored channel preserves old PCM when charge boundaries agree', async () => {
  const old = await load(roundedLoadedFixture());
  const mapped = await load(mappedBoundaryFixture());
  for (const load01 of [0, 0.12, 0.65, 1]) {
    for (const e of [old, mapped]) {e.setOperatingPoint({rpm: 1200, load01, throttle01: load01});e.reset();}
    assert.deepEqual(mapped.render(45000), old.render(45000));
  }
  assert.equal(mapped.loadManifoldPressurePa(1200, 0.5), 151987.5);
});

test('boosted compression cannot excite combustion noise at intermediate RPM and fuel', async () => {
  const engine = await load(mappedBoundaryFixture(m => {
    m.presentation.pressure_texture.compression_gain = 0;
    m.presentation.exhaust_gain_pcm_per_kg_s = 0;
    m.presentation.exhaust_system.radiation_gain_pcm_s2_per_kg = 0;
    m.presentation.exhaust_system.flow_noise_gain_pcm_per_kg_s = 0;
    m.presentation.mechanical_texture.valve_gain_pcm = 0;
    m.presentation.mechanical_texture.pumping_gain_pcm = 0;
    m.presentation.coasting_flow_texture.gain_pcm_per_kg_s = 0;
  }, true));
  engine.setOperatingPoint({rpm: 1200, load01: 0.65, throttle01: 0.65});engine.reset();
  const output = engine.render(45000);
  assert.equal(rms(output), 0);
});

test('mapped source layout validates version, stride and motored-force bounds', async () => {
  for (const mutate of [
    m => {m.sources.layout = 'cell-phase-force-n-exhaust-kg-s';},
    m => {m.sources.byte_count -= 4;},
    m => {m.version = 11;m.methods[2] = 'pressure-force-modal-radiation-v11';},
    (m, s) => {s.writeFloatLE(NaN, 8);},
    (m, s) => {s.writeFloatLE(2e7, 8);},
    (m, s) => {s.writeFloatLE(11, 4);}
  ]) await assert.rejects(load(mappedBoundaryFixture(mutate)));
});

test('version 13 admits pressure rounding at idle and preserves existing parameter behavior', async () => {
  const old = await load(mappedBoundaryFixture());
  const unchanged = await load(mappedBoundaryFixture(m => {
    m.version = 13; m.methods[2] = 'pressure-force-modal-radiation-v13';
  }));
  const idle = await load(mappedBoundaryFixture(m => {
    m.version = 13; m.methods[2] = 'pressure-force-modal-radiation-v13';
    Object.assign(m.presentation.loaded_pressure_texture,
      {fueling_start_01: 0, fueling_full_01: .12, attack_s: .007, decay_s: .025});
  }));
  for (const fuel of [0, .12, .65]) {
    for (const e of [old, unchanged, idle]) {
      e.setOperatingPoint({rpm: 1200, load01: fuel, throttle01: fuel}); e.reset();
    }
    const before = old.render(45000), same = unchanged.render(45000), rounded = idle.render(45000);
    assert.deepEqual(before, same);
    if (fuel === 0) assert.deepEqual(before, rounded, 'no combustion texture from motoring');
    else assert.notDeepEqual(before, rounded);
    assert.ok(rounded.every(Number.isFinite));
    idle.reset();
    const partitioned = new Float32Array(45000);
    partitioned.set(idle.render(15000)); partitioned.set(idle.render(30000), 15000);
    assert.deepEqual(rounded, partitioned);
    const a = old.diagnostics().runtime, b = idle.diagnostics().runtime;
    for (const key of ['exhaustRms', 'valveRms', 'pumpingRms', 'coastingFlowRms'])
      assert.equal(a[key], b[key], `${key} is independent of pressure rounding`);
  }
  for (const mutate of [
    m => {m.presentation.loaded_pressure_texture.fueling_start_01 = -.01;},
    m => {m.presentation.loaded_pressure_texture.fueling_full_01 = 0;},
    m => {Object.assign(m.presentation.loaded_pressure_texture, {fueling_start_01: .1, fueling_full_01: .1});},
  ]) await assert.rejects(load(mappedBoundaryFixture(m => {
    m.version = 13; m.methods[2] = 'pressure-force-modal-radiation-v13'; mutate(m);
  })));
});

const inertiaParameters = {reciprocating_mass_kg: .45, frequency_hz: 110, q: 2.5,
  gain_pcm_per_n: .0004, highpass_hz: 140, lowpass_hz: 500};
const inertiaGeometry = {crank_radius_m: .04025, connecting_rod_length_m: .137};
function inertiaFixture(mutate = () => {}) {
  return mappedBoundaryFixture(m => {
    m.version = 14; m.methods[2] = 'pressure-force-modal-radiation-v14';
    m.inertial_geometry = {...inertiaGeometry};
    m.presentation.inline_inertia = {...inertiaParameters};
    mutate(m);
  });
}

test('inline inertia cancels I4 primary forces, retains secondary force and scales with speed squared', () => {
  const c = [0, Math.PI, 2 * Math.PI, 3 * Math.PI].map(firing_angle_rad => ({firing_angle_rad}));
  const body = new DieselInlineInertia(inertiaParameters, inertiaGeometry, c);
  const expected = 4 * .45 * .04025 ** 2 / .137 * (2 * Math.PI * 3000 / 60) ** 2;
  body.process(0, 3000); assert.ok(Math.abs(body.force / expected - 1) < 1e-12);
  body.process(.25, 3000); assert.ok(Math.abs(body.force / expected + 1) < 1e-12);
  body.process(.125, 3000); assert.ok(Math.abs(body.force) < 1e-9);
  body.process(0, 1500); assert.ok(Math.abs(body.force / expected - .25) < 1e-12);
  body.process(0, 0); assert.equal(body.force, 0);
});

test('inline inertia survives fuel cut, replays exactly and leaves the other layers unchanged', async () => {
  const old = await load(mappedBoundaryFixture());
  const muted = await load(inertiaFixture(m => {m.presentation.inline_inertia.gain_pcm_per_n = 0;}));
  const active = await load(inertiaFixture());
  for (const load01 of [0, .12, .65]) {
    for (const e of [old, muted, active]) {e.setOperatingPoint({rpm: 1200, load01, throttle01: load01});e.reset();}
    const before = old.render(45000), zero = muted.render(45000), pcm = active.render(45000);
    assert.deepEqual(before, zero);
    assert.notDeepEqual(before, pcm);
    assert.ok(pcm.every(x => Number.isFinite(x) && Math.abs(x) < 1));
    assert.ok(active.diagnostics().runtime.inertiaRms > 0);
    for (const key of ['blockRms', 'exhaustRms', 'valveRms', 'pumpingRms', 'coastingFlowRms'])
      assert.equal(old.diagnostics().runtime[key], active.diagnostics().runtime[key]);
    active.reset();const split = new Float32Array(45000);
    split.set(active.render(15000));split.set(active.render(30000),15000);
    assert.deepEqual(pcm, split);
  }
});

test('inline inertia requires explicit version 14, geometry and bounded physical and radiation parameters', async () => {
  for (const mutate of [
    m => {m.version = 13;m.methods[2] = 'pressure-force-modal-radiation-v13';},
    m => {delete m.inertial_geometry;},
    m => {m.inertial_geometry.connecting_rod_length_m = .02;},
    m => {delete m.presentation.inline_inertia;},
    m => {m.presentation.inline_inertia.reciprocating_mass_kg = 0;},
    m => {m.presentation.inline_inertia.gain_pcm_per_n = .1;},
    m => {m.presentation.inline_inertia.frequency_hz = 0;},
    m => {m.presentation.inline_inertia.q = 0;},
    m => {m.presentation.inline_inertia.highpass_hz = 300;m.presentation.inline_inertia.lowpass_hz = 300;},
    m => {m.presentation.inline_inertia.extra = 1;}
  ]) await assert.rejects(load(inertiaFixture(mutate)));
});

const clatterParameters = {rpm_start: 800, rpm_full: 1400, fueling_start_01: .18,
  fueling_full_01: .55, attack_s: .0003, decay_s: .0025, transition_s: .04,
  coherent_gain: 1, stochastic_gain: 2, highpass_hz: 450, lowpass_hz: 5500, seed: 15983226,
  modes: [{frequency_hz: 650, q: 3, gain_pcm_per_n: .00007},
    {frequency_hz: 1280, q: 5, gain_pcm_per_n: .00006}]};
function clatterFixture(mutate = () => {}) {
  return inertiaFixture(m => {
    m.version = 15;m.methods[2] = 'pressure-force-modal-radiation-v15';
    m.presentation.loaded_clatter = structuredClone(clatterParameters);mutate(m);
  });
}

test('loaded clatter needs combustion pressure rise, decays after it and replays its seeded texture', () => {
  const c = new DieselLoadedClatter(clatterParameters, [{id:'test-cylinder',structural_gain:1}]);
  function run(fired, motored, count) {
    const out = new Float64Array(count);
    for(let i=0;i<count;i++) {
      c.beginFrame(1600,.65);c.excite(0,fired(i),motored(i));out[i]=c.finishFrame();
    }
    return out;
  }
  // Identical motored/fired variation must never be classified as combustion.
  const motored = i => 30000*Math.cos(i*.01);
  assert.ok(run(motored,motored,10000).every(x=>x===0));
  c.reset();
  const fired = i => i<800 ? 40000*Math.sin(Math.PI*i/800)**2 : 0;
  const first = run(fired,()=>0,30000);
  assert.ok(first.some(x=>Math.abs(x)>1e-4));
  assert.ok(first.slice(-1000).every(x=>Math.abs(x)<1e-12));
  c.reset();assert.deepEqual(run(fired,()=>0,30000),first);
});

test('loaded clatter preserves idle/coast PCM, adds fueled high-RPM detail, and leaves existing layers exact', async () => {
  const previous=await load(inertiaFixture());
  const next=await load(clatterFixture());
  const muted=await load(clatterFixture(m=>m.presentation.loaded_clatter.modes.forEach(x=>x.gain_pcm_per_n=0)));
  for(const [rpm,fuel,active] of [[800,.65,false],[1600,.12,false],[1600,0,false],[1600,.65,true]]) {
    for(const e of [previous,next,muted]){e.setOperatingPoint({rpm,load01:fuel,throttle01:fuel});e.reset();}
    const before=previous.render(45000),after=next.render(45000);
    assert.deepEqual(muted.render(45000),before);
    if(active)assert.notDeepEqual(after,before);else assert.deepEqual(after,before);
    assert.ok(after.every(x=>Number.isFinite(x)&&Math.abs(x)<1));
    assert.equal(next.diagnostics().runtime.loadedClatterRms>0,active);
    for(const key of ['blockRms','exhaustRms','valveRms','pumpingRms','coastingFlowRms','inertiaRms'])
      assert.equal(previous.diagnostics().runtime[key],next.diagnostics().runtime[key]);
    next.reset();const split=new Float32Array(45000);
    split.set(next.render(12000));split.set(next.render(33000),12000);assert.deepEqual(split,after);
  }
});

test('loaded clatter requires version 15 and rejects malformed envelopes, intervals, modes and seeds', async () => {
  for(const mutate of [
    m=>{m.version=14;m.methods[2]='pressure-force-modal-radiation-v14';},
    m=>{delete m.presentation.loaded_clatter;},
    m=>{m.presentation.loaded_clatter.rpm_full=800;},
    m=>{m.presentation.loaded_clatter.fueling_full_01=.18;},
    m=>{m.presentation.loaded_clatter.attack_s=0;},
    m=>{m.presentation.loaded_clatter.decay_s=.1;},
    m=>{m.presentation.loaded_clatter.transition_s=0;},
    m=>{m.presentation.loaded_clatter.stochastic_gain=13;},
    m=>{m.presentation.loaded_clatter.coherent_gain=-1;},
    m=>{m.presentation.loaded_clatter.seed=.5;},
    m=>{m.presentation.loaded_clatter.highpass_hz=1500;m.presentation.loaded_clatter.lowpass_hz=2000;},
    m=>{m.presentation.loaded_clatter.modes=[];},
    m=>{m.presentation.loaded_clatter.modes[0].gain_pcm_per_n=.1;},
    m=>{m.presentation.loaded_clatter.modes[0].q=0;},
    m=>{m.presentation.loaded_clatter.modes[0].frequency_hz=0;},
    m=>{m.presentation.loaded_clatter.extra=1;}
  ])await assert.rejects(load(clatterFixture(mutate)));
});

function coupledClatterFixture(mutate = () => {}) {
  return clatterFixture(m => {
    m.version = 16;m.methods[2] = 'pressure-force-modal-radiation-v16';
    m.presentation.loaded_clatter.modes.forEach((mode, i) => {
      mode.cylinder_coupling = m.cylinders.map((c, j) => ({id:c.id, gain:j===i ? 1 : .3}));
    });
    mutate(m);
  });
}

test('mode coupling selects cylinder excitation independently and binds by ID, including signed weights', () => {
  const cylinders = [{id:'left',structural_gain:1},{id:'right',structural_gain:1}];
  const parameters = structuredClone(clatterParameters);
  parameters.stochastic_gain = 0;
  parameters.modes[0].cylinder_coupling = [{id:'right',gain:0},{id:'left',gain:1}];
  parameters.modes[1].cylinder_coupling = [{id:'left',gain:0},{id:'right',gain:-1}];
  function render(p, cs, active) {
    const layer = new DieselLoadedClatter(p, cs), out = new Float64Array(16000);
    for(let i=0;i<out.length;i++) {
      layer.beginFrame(1600,.65);
      cs.forEach((c,j)=>layer.excite(j,c.id===active && i<800 ? 40000*Math.sin(Math.PI*i/800)**2 : 0,0));
      out[i] = layer.finishFrame();
    }
    return out;
  }
  for(const [index,id,sign] of [[0,'left',1],[1,'right',-1]]) {
    const solo = structuredClone(parameters);solo.modes = [solo.modes[index]];
    delete solo.modes[0].cylinder_coupling;
    const expected = render(solo,[{id,structural_gain:sign}],id);
    const actual = render(parameters,cylinders,id);
    assert.ok(actual.some(x=>Math.abs(x)>1e-5));
    assert.deepEqual(actual,expected);
    assert.deepEqual(render(parameters,[...cylinders].reverse(),id),actual);
  }
});

test('coupled clatter preserves bypass and existing layers and replays across read partitions', async () => {
  const previous=await load(inertiaFixture()),next=await load(coupledClatterFixture());
  const muted=await load(coupledClatterFixture(m=>m.presentation.loaded_clatter.modes.forEach(mode=>
    mode.cylinder_coupling.forEach(c=>c.gain=0))));
  for(const [rpm,fuel,active] of [[800,.65,false],[1600,.12,false],[1600,0,false],[1600,.65,true]]) {
    for(const e of [previous,next,muted]) {e.setOperatingPoint({rpm,load01:fuel,throttle01:fuel});e.reset();}
    const before=previous.render(45000),after=next.render(45000);
    assert.deepEqual(muted.render(45000),before);
    if(active)assert.notDeepEqual(after,before);else assert.deepEqual(after,before);
    assert.ok(after.every(x=>Number.isFinite(x)&&Math.abs(x)<1));
    for(const key of ['blockRms','exhaustRms','valveRms','pumpingRms','coastingFlowRms','inertiaRms'])
      assert.equal(next.diagnostics().runtime[key],previous.diagnostics().runtime[key]);
    next.reset();const split=new Float32Array(45000);
    split.set(next.render(17003));split.set(next.render(27997),17003);assert.deepEqual(split,after);
  }
});

test('version 16 requires complete, unique, bounded per-mode cylinder bindings', async () => {
  for(const mutate of [
    m=>{m.version=15;m.methods[2]='pressure-force-modal-radiation-v15';},
    m=>{delete m.presentation.loaded_clatter.modes[0].cylinder_coupling;},
    m=>{m.presentation.loaded_clatter.modes.forEach(mode=>delete mode.cylinder_coupling);},
    m=>{m.presentation.loaded_clatter.modes[0].cylinder_coupling.pop();},
    m=>{m.presentation.loaded_clatter.modes[0].cylinder_coupling[0].id='unknown';},
    m=>{
      m.cylinders.push({...m.cylinders[0],id:'coupled-second',firing_angle_rad:2*Math.PI});
      m.presentation.exhaust_system.branches[0].cylinder_ids.push('coupled-second');
      m.presentation.loaded_clatter.modes.forEach(mode=>mode.cylinder_coupling.push({id:'coupled-second',gain:1}));
      const c=m.presentation.loaded_clatter.modes[0].cylinder_coupling;c[1].id=c[0].id;
    },
    m=>{m.presentation.loaded_clatter.modes[0].cylinder_coupling[0].gain=2.01;},
    m=>{m.presentation.loaded_clatter.modes[0].cylinder_coupling[0].gain=-2.01;},
    m=>{m.presentation.loaded_clatter.modes[0].cylinder_coupling[0].extra=1;}
  ])await assert.rejects(load(coupledClatterFixture(mutate)));
});
