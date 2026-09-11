import assert from "node:assert/strict";
import {webcrypto} from "node:crypto";
import test from "node:test";

import {CrankwaveAudioEngine} from "../../runtime/crankwave-audio-engine.js";
import {DieselBiquad, DieselExhaustSystem, DieselPressureTexture} from "../../runtime/diesel-mechanical-runtime.js";

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
