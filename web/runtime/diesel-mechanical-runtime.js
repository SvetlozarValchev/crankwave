const SCHEMA = "crankwave/diesel-mechanical-audio";
const TAU = 2 * Math.PI;
const SAMPLE_RATE = 192_000;
const BATCH_FRAMES = 3_840;

export class DieselMechanicalRuntimeError extends Error {
  constructor(message) {
    super(message);
    this.name = "DieselMechanicalRuntimeError";
    this.code = "invalid-diesel-runtime";
  }
}

function requireValue(condition, message) {
  if (!condition) throw new DieselMechanicalRuntimeError(message);
}
function object(value, names, label) {
  requireValue(
      value !== null && typeof value === "object" && !Array.isArray(value),
      `${label} must be an object`);
  const actual = Object.keys(value);
  requireValue(
      actual.length === names.length &&
          names.every((name) => Object.hasOwn(value, name)),
      `${label} has unknown or missing fields`);
}
function number(value, min, max, label) {
  requireValue(
      typeof value === "number" && Number.isFinite(value) && value >= min &&
          value <= max,
      `${label} is out of range`);
  return value;
}
function id(value, label) {
  requireValue(
      typeof value === "string" &&
          /^[a-z0-9](?:[a-z0-9._-]{0,126}[a-z0-9])?$/u.test(value),
      `${label} is not a portable ID`);
}
function digest(value, label) {
  requireValue(
      typeof value === "string" && /^[0-9a-f]{64}$/u.test(value),
      `${label} is not a SHA-256 digest`);
}

function parseJson(text) {
  // Match native duplicate-member admission instead of JSON.parse's last-wins rule.
  const value = JSON.parse(text);
  const stack = [];
  for (let i = 0; i < text.length; ++i) {
    const c = text[i];
    if (c === "{")
      stack.push(new Set());
    else if (c === "[")
      stack.push(null);
    else if (c === "}" || c === "]")
      stack.pop();
    else if (c === "\"") {
      const start = i;
      while (++i < text.length) {
        if (text[i] === "\\")
          ++i;
        else if (text[i] === "\"")
          break;
      }
      let after = i + 1;
      while (/\s/u.test(text[after] ?? "")) ++after;
      if (text[after] === ":" && stack.at(-1) instanceof Set) {
        const key = JSON.parse(text.slice(start, i + 1));
        requireValue(!stack.at(-1).has(key), `Repeated manifest member ${key}`);
        stack.at(-1).add(key);
      }
    }
  }
  return value;
}

export async function loadDieselMechanicalRuntime(
    manifestUrl,
    {fetch: fetchImplementation, crypto: cryptoImplementation = globalThis.crypto}) {
  const response = await fetchImplementation(manifestUrl);
  requireValue(response.ok, "Diesel manifest could not be loaded");
  const manifestBytes = await response.arrayBuffer();
  requireValue(
      manifestBytes.byteLength <= 256 * 1024, "Diesel manifest exceeds its limit");
  const text = new TextDecoder("utf-8", {fatal: true}).decode(manifestBytes);
  const m = parseJson(text);
  object(
      m,
      [
        "schema", "version", "engine", "model", "sample_rate_hz", "physics_rate_hz",
        "gas_substeps", "load_coordinate", "source_sha256", "backend", "methods",
        "samples_per_cycle", "cells", "cylinders", "presentation", "provenance",
        "sources"
      ],
      "manifest");
  requireValue(
      m.schema === SCHEMA && [1, 2, 3, 5].includes(m.version) &&
          m.model === "mechanical-diesel-v1",
      "Unsupported diesel model");
  requireValue(
      m.sample_rate_hz === SAMPLE_RATE && m.physics_rate_hz === 20_000 &&
          m.gas_substeps === 32 && m.load_coordinate === "normalized-fueling",
      "Unsupported diesel clock or load coordinate");
  id(m.engine, "engine");
  digest(m.source_sha256, "source_sha256");
  object(m.backend, ["kind", "executable_sha256", "numeric_policy"], "backend");
  requireValue(
      m.backend.kind === "native-cpp-experimental" &&
          m.backend.numeric_policy === "x86-64-v1-binary64-x87-extended-strict-v1",
      "Unsupported diesel backend");
  digest(m.backend.executable_sha256, "executable_sha256");
  requireValue(
      JSON.stringify(m.methods) === JSON.stringify([
        "diesel-held-speed-cylinder-v1", "diesel-prepared-fuel-combustion-v1",
        `pressure-force-modal-radiation-v${m.version}`
      ]),
      "Unsupported diesel method set");
  const n = number(m.samples_per_cycle, 2048, 16384, "samples_per_cycle");
  requireValue(
      Number.isInteger(n) && (n & (n - 1)) === 0, "Invalid phase sample count");
  requireValue(
      Array.isArray(m.cells) && m.cells.length >= 4 && m.cells.length <= 512,
      "Invalid diesel cell count");
  const rows = [];
  for (let i = 0; i < m.cells.length; ++i) {
    const c = m.cells[i];
    object(
        c,
        [
          "rpm", "fueling_01", "manifold_pressure_pa_abs", "settled_cycles",
          "indicated_work_j", "peak_pressure_pa_abs"
        ],
        `cell ${i}`);
    number(c.rpm, 400, 4500, "cell RPM");
    number(c.fueling_01, 0, 1, "cell fueling");
    number(c.manifold_pressure_pa_abs, 1000, 300000, "cell MAP");
    number(c.settled_cycles, 4, 64, "settling cycles");
    requireValue(
        Number.isInteger(c.settled_cycles), "Settling cycle count must be integral");
    number(c.indicated_work_j, -1e7, 1e7, "cycle work");
    number(c.peak_pressure_pa_abs, 1000, 1e9, "peak pressure");
    if (rows.length === 0 || rows.at(-1).rpm !== c.rpm) {
      requireValue(
          rows.length === 0 || c.rpm > rows.at(-1).rpm, "RPM rows must be ordered");
      rows.push({rpm: c.rpm, cells: []});
    }
    const row = rows.at(-1);
    requireValue(
        row.cells.length === 0 || c.fueling_01 > row.cells.at(-1).fueling_01,
        "Fuel cells must be ordered");
    row.cells.push({...c, index: i});
  }
  requireValue(rows.length >= 2 && rows.length <= 32, "Invalid RPM grid");
  const fuelAxis = rows[0].cells.map((c) => c.fueling_01);
  requireValue(
      fuelAxis.length >= 2 && fuelAxis.length <= 16 && fuelAxis[0] === 0 &&
          fuelAxis.at(-1) === 1,
      "Fuel grid must cover zero to one");
  for (const row of rows) {
    requireValue(
        row.cells.length === fuelAxis.length &&
            row.cells.every((c, i) => c.fueling_01 === fuelAxis[i]),
        "Diesel grid must be rectangular");
  }
  requireValue(
      Array.isArray(m.cylinders) && m.cylinders.length >= 1 && m.cylinders.length <= 12,
      "Invalid cylinder bindings");
  const ids = new Set();
  for (const c of m.cylinders) {
    object(c, ["id", "firing_angle_rad", "fuel_trim", "structural_gain"], "cylinder");
    id(c.id, "cylinder ID");
    requireValue(!ids.has(c.id), "Duplicate cylinder ID");
    ids.add(c.id);
    number(c.firing_angle_rad, 0, 4 * Math.PI, "firing angle");
    requireValue(
        c.firing_angle_rad < 4 * Math.PI, "Firing angle must be below 720 degrees");
    number(c.fuel_trim, 0.5, 1.5, "fuel trim");
    number(c.structural_gain, -2, 2, "structural gain");
  }
  const p = m.presentation;
  object(
      p,
      [
        "block_modes", "block_lowpass_hz", "exhaust_gain_pcm_per_kg_s",
        "exhaust_lowpass_hz", "master_gain",
        ...(m.version >= 2 ? ["articulation"] : []),
        ...(m.version >= 3 ? ["exhaust_system"] : []),
        ...(m.version === 5 ? ["pressure_texture"] : [])
      ],
      "presentation");
  if (m.version >= 2) {
    const a = p.articulation;
    object(
        a,
        [
          "block_highpass_hz", "exhaust_highpass_hz", "cycle_fuel_spread_01",
          "cycle_timing_spread_deg", "variation_seed"
        ],
        "articulation");
    number(a.block_highpass_hz, 100, 1500, "block highpass");
    number(a.exhaust_highpass_hz, 30, 300, "exhaust highpass");
    number(a.cycle_fuel_spread_01, 0, 0.2, "cycle fuel spread");
    number(a.cycle_timing_spread_deg, 0, 2, "cycle timing spread");
    number(a.variation_seed, 0, 0xffffffff, "variation seed");
    requireValue(Number.isInteger(a.variation_seed), "Variation seed must be integral");
  }
  if (m.version >= 3) {
    const e = p.exhaust_system;
    object(
        e,
        [
          "branches", "radiation_gain_pcm_s2_per_kg", "flow_noise_gain_pcm_per_kg_s",
          "noise_highpass_hz", "noise_seed"
        ],
        "exhaust system");
    number(e.radiation_gain_pcm_s2_per_kg, 0, 0.1, "radiation gain");
    number(e.flow_noise_gain_pcm_per_kg_s, 0, 50, "flow noise gain");
    number(e.noise_highpass_hz, 100, 3000, "noise highpass");
    number(e.noise_seed, 0, 0xffffffff, "noise seed");
    requireValue(Number.isInteger(e.noise_seed), "Noise seed must be integral");
    requireValue(
        Array.isArray(e.branches) && e.branches.length >= 1 && e.branches.length <= 8,
        "Invalid exhaust branch count");
    const assigned = new Set();
    const branchIds = new Set();
    for (const b of e.branches) {
      object(
          b, ["id", "cylinder_ids", "delay_s", "reflection", "loss_lowpass_hz", "gain"],
          "exhaust branch");
      id(b.id, "branch ID");
      requireValue(!branchIds.has(b.id), "Repeated branch ID");
      branchIds.add(b.id);
      number(b.delay_s, 0.001, 0.04, "branch delay");
      number(b.reflection, -0.9, 0.9, "branch reflection");
      number(b.loss_lowpass_hz, 500, 8000, "branch loss lowpass");
      number(b.gain, 0, 2, "branch gain");
      requireValue(
          Array.isArray(b.cylinder_ids) && b.cylinder_ids.length > 0 &&
              b.cylinder_ids.length <= ids.size,
          "Invalid branch bindings");
      for (const cylinder of b.cylinder_ids) {
        requireValue(
            ids.has(cylinder) && !assigned.has(cylinder),
            "Unknown or repeated branch cylinder");
        assigned.add(cylinder);
      }
    }
    requireValue(assigned.size === ids.size, "Every cylinder needs one exhaust branch");
  }
  if (m.version === 5) {
    const t = p.pressure_texture;
    object(t, ["decay_s", "coherent_gain", "compression_gain", "stochastic_gain", "seed"],
        "pressure texture");
    number(t.decay_s, 0.001, 0.05, "pressure texture decay");
    number(t.coherent_gain, 0, 1, "coherent pressure gain");
    number(t.compression_gain, 0, 1, "compression gain");
    number(t.stochastic_gain, 0, 32, "stochastic pressure gain");
    number(t.seed, 0, 0xffffffff, "pressure texture seed");
    requireValue(Number.isInteger(t.seed), "Pressure texture seed must be integral");
  }
  number(p.block_lowpass_hz, 2000, 8000, "block lowpass");
  number(p.exhaust_lowpass_hz, 100, 3000, "exhaust lowpass");
  number(p.exhaust_gain_pcm_per_kg_s, 0, 100, "exhaust gain");
  number(p.master_gain, 0, 4, "master gain");
  requireValue(
      Array.isArray(p.block_modes) && p.block_modes.length >= 1 &&
          p.block_modes.length <= 12,
      "Invalid modal bank");
  for (const mode of p.block_modes) {
    object(mode, ["frequency_hz", "q", "gain_pcm_per_n"], "mode");
    number(mode.frequency_hz, 100, 8000, "mode frequency");
    number(mode.q, 0.5, 30, "mode Q");
    number(mode.gain_pcm_per_n, -0.001, 0.001, "mode gain");
  }
  object(m.provenance, ["physical_parameters", "presentation"], "provenance");
  for (const value of Object.values(m.provenance)) {
    object(value, ["origin", "note"], "provenance record");
    requireValue(
        ["inferred", "artistic", "measurement", "calibrated"].includes(value.origin) &&
            typeof value.note === "string" && value.note.length > 0 &&
            value.note.length <= 4096,
        "Invalid provenance record");
  }
  object(m.sources, ["path", "sha256", "layout", "byte_count"], "sources");
  requireValue(
      m.sources.path === "sources.f32le" &&
          m.sources.layout === "cell-phase-force-n-exhaust-kg-s",
      "Unsupported source layout");
  digest(m.sources.sha256, "sources.sha256");
  const expected = m.cells.length * n * 8;
  requireValue(
      m.sources.byte_count === expected, "Source byte count does not match grid");
  const sourceResponse =
      await fetchImplementation(new URL(m.sources.path, manifestUrl).href);
  requireValue(sourceResponse.ok, "Diesel sources could not be loaded");
  const bytes = await sourceResponse.arrayBuffer();
  requireValue(
      bytes.byteLength === expected, "Source payload length does not match grid");
  const hash = Array
                   .from(
                       new Uint8Array(
                           await cryptoImplementation.subtle.digest("SHA-256", bytes)),
                       (b) => b.toString(16).padStart(2, "0"))
                   .join("");
  requireValue(hash === m.sources.sha256, "Source payload hash mismatch");
  const view = new DataView(bytes);
  const samples = new Float32Array(expected / 4);
  for (let i = 0; i < samples.length; ++i) {
    const value = view.getFloat32(i * 4, true);
    number(value, i % 2 === 0 ? -1e7 : -10, i % 2 === 0 ? 1e7 : 10, "source sample");
    samples[i] = value;
  }
  return Object.freeze({
    kind: "diesel-mechanical-audio",
    manifest: m,
    samples,
    rows,
    fuelAxis,
    sampleRate: SAMPLE_RATE,
    batchFrames: BATCH_FRAMES,
    minimumRpm: rows[0].rpm,
    maximumRpm: rows.at(-1).rpm
  });
}

// RBJ biquad in transposed direct form II. Modal frequencies live in physical
// seconds and never follow playback RPM. Band-pass modes have unity peak gain.
export class DieselBiquad {
  constructor(kind, frequency, q, sampleRate = SAMPLE_RATE) {
    const w = TAU * frequency / sampleRate;
    const alpha = Math.sin(w) / (2 * q);
    const norm = 1 / (1 + alpha);
    this.a1 = -2 * Math.cos(w) * norm;
    this.a2 = (1 - alpha) * norm;
    this.b0 = (kind === "bandpass" ?
                   alpha :
                   (kind === "highpass" ? 1 + Math.cos(w) : 1 - Math.cos(w)) / 2) *
        norm;
    this.b1 = kind === "bandpass" ? 0 : (kind === "highpass" ? -2 : 2) * this.b0;
    this.b2 = kind === "bandpass" ? -this.b0 : this.b0;
    this.reset();
  }
  reset() {
    this.z1 = 0;
    this.z2 = 0;
  }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
}

function lowerIndex(values, x, getter) {
  let lo = 0;
  let hi = values.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >>> 1;
    if (getter(values[mid]) <= x)
      lo = mid;
    else
      hi = mid;
  }
  return lo;
}

// Stable per-cylinder/event variation. It changes physical source selection and
// bounded presentation timing, with no free-running hiss or frame-size PRNG state.
function cylinderSeed(id, seed) {
  let hash = seed >>> 0;
  for (let i = 0; i < id.length; ++i)
    hash = Math.imul(hash ^ id.charCodeAt(i), 16777619);
  return hash >>> 0;
}
function eventValue(seed, cycle, lane) {
  let x = (seed ^ Math.imul(cycle | 0, 0x9e3779b1) ^ lane) >>> 0;
  x = Math.imul(x ^ (x >>> 16), 0x21f0aaad);
  x = Math.imul(x ^ (x >>> 15), 0x735a2d97);
  return ((x ^ (x >>> 15)) >>> 0) / 0x80000000 - 1;
}

// Stable, lossy delay coloration of each bank's captured mass flow. Delay is
// physical time. This is a one-way acoustic approximation, not pipe gas dynamics.
export class DieselExhaustSystem {
  constructor(parameters, cylinders, sampleRate = SAMPLE_RATE) {
    this.parameters = parameters;
    this.sampleRate = sampleRate;
    this.noiseHighpass = new DieselBiquad(
        "highpass", parameters.noise_highpass_hz, Math.SQRT1_2, sampleRate);
    this.branches = parameters.branches.map(
        (b) => ({
          ...b,
          indices: b.cylinder_ids.map((id) => cylinders.findIndex((c) => c.id === id)),
          delayFrames: b.delay_s * sampleRate,
          line: new Float64Array(Math.ceil(b.delay_s * sampleRate) + 1),
          loss:
              new DieselBiquad("lowpass", b.loss_lowpass_hz, Math.SQRT1_2, sampleRate),
          seed: cylinderSeed(b.id, parameters.noise_seed) || 1,
        }));
    this.reset();
  }
  reset() {
    this.flow = 0;
    this.noise = 0;
    this.noiseHighpass.reset();
    for (const b of this.branches) {
      b.line.fill(0);
      b.position = 0;
      b.rng = b.seed;
      b.loss.reset();
    }
  }
  process(flows) {
    let sum = 0;
    let noise = 0;
    for (const b of this.branches) {
      let source = 0;
      for (const index of b.indices) source += flows[index];
      const read = (b.position - b.delayFrames + b.line.length) % b.line.length;
      const at = Math.floor(read);
      const fraction = read - at;
      const delayed =
          b.line[at] * (1 - fraction) + b.line[(at + 1) % b.line.length] * fraction;
      const reflected = b.loss.process(delayed);
      b.line[b.position] = source + b.reflection * reflected;
      b.position = (b.position + 1) % b.line.length;
      sum += b.gain * reflected;
      // Stochastic jet texture is explicitly an authored, flow-gated source.
      // Zero flow adds no noise; its amplitude is not calibrated acoustic pressure.
      let x = b.rng;
      x ^= x << 13;
      x ^= x >>> 17;
      x ^= x << 5;
      b.rng = x >>> 0;
      noise += b.gain * Math.max(0, source) * (b.rng / 0x80000000 - 1);
    }
    this.flow = sum;
    this.noise = this.noiseHighpass.process(noise) *
        this.parameters.flow_noise_gain_pcm_per_kg_s;
  }
}

// Combustion-driven stochastic excitation. Subtract the motored cylinder at the
// same phase before accumulating positive pressure increments. Each cylinder's
// decaying envelope excites independent noise, giving different fine structure
// to successive burns while keeping their energy tied to the physical source.
// Parameters describe an acoustic approximation, not extra heat or cylinder force.
export class DieselPressureTexture {
  constructor(parameters, cylinders, sampleRate = SAMPLE_RATE) {
    this.parameters = parameters;
    this.release = Math.exp(-1 / (sampleRate * parameters.decay_s));
    this.noiseScale = Math.sqrt(3 * sampleRate / SAMPLE_RATE);
    this.cylinders = cylinders.map(c => ({seed: cylinderSeed(c.id, parameters.seed) || 1}));
    this.reset();
  }
  reset() {
    for (const c of this.cylinders) {
      c.previous = 0;
      c.envelope = 0;
      c.rng = c.seed;
    }
  }
  process(index, firedForce, motoredForce) {
    const c = this.cylinders[index];
    const residual = firedForce - motoredForce;
    c.envelope = c.envelope * this.release + Math.max(0, residual - c.previous);
    c.previous = residual;
    let x = c.rng;
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    c.rng = x >>> 0;
    const noise = (c.rng / 0x80000000 - 1) * this.noiseScale;
    const p = this.parameters;
    return p.compression_gain * motoredForce + p.coherent_gain * residual +
        p.stochastic_gain * c.envelope * noise;
  }
}

export class DieselMechanicalRuntimeCursor {
  constructor(runtime) {
    this.runtime = runtime;
    const p = runtime.manifest.presentation;
    this.modes = p.block_modes.map(
        (mode) => ({
          filter: new DieselBiquad("bandpass", mode.frequency_hz, mode.q),
          gain: mode.gain_pcm_per_n
        }));
    this.blockLowpass = new DieselBiquad("lowpass", p.block_lowpass_hz, Math.SQRT1_2);
    this.exhaustLowpass =
        new DieselBiquad("lowpass", p.exhaust_lowpass_hz, Math.SQRT1_2);
    this.articulation = p.articulation;
    this.blockHighpass = p.articulation ?
        new DieselBiquad("highpass", p.articulation.block_highpass_hz, Math.SQRT1_2) :
        null;
    this.exhaustHighpassFilter = p.articulation ?
        new DieselBiquad("highpass", p.articulation.exhaust_highpass_hz, Math.SQRT1_2) :
        null;
    this.cylinders = runtime.manifest.cylinders.map(
        (c) => ({...c, seed: cylinderSeed(c.id, p.articulation?.variation_seed ?? 0)}));
    this.exhaustSystem = p.exhaust_system ?
        new DieselExhaustSystem(p.exhaust_system, this.cylinders) :
        null;
    this.pressureTexture = p.pressure_texture ?
        new DieselPressureTexture(p.pressure_texture, this.cylinders) : null;
    this.cylinderFlows = new Float64Array(this.cylinders.length);
    this.pending = new Float32Array(runtime.batchFrames);
    this.reset();
  }
  reset() {
    this.modes.forEach((mode) => mode.filter.reset());
    this.blockLowpass.reset();
    this.exhaustLowpass.reset();
    this.blockHighpass?.reset();
    this.exhaustHighpassFilter?.reset();
    this.exhaustSystem?.reset();
    this.pressureTexture?.reset();
    this.radiationPrevious = 0;
    this.exhaustPrevious = 0;
    this.exhaustHighpass = 0;
    this.pendingCount = 0;
    this.frames = 0;
    this.audibleFrames = 0;
    this.audible = false;
    this.peak = 0;
    this.blockEnergy = 0;
    this.exhaustEnergy = 0;
  }
  initialize(endpoint) {
    number(endpoint.rpm, this.runtime.minimumRpm, this.runtime.maximumRpm, "RPM");
    number(endpoint.fueling01, 0, 1, "fueling");
    return {segmentId: "mechanical-diesel-v1"};
  }
  beginAudible() {
    this.audible = true;
    this.audibleFrames = 0;
  }
  warmBlock({frameCount, start, end}) {
    this.generate(frameCount, start, end, false);
  }
  renderBlock({sourceBlock, start, end}) {
    return {bakedBlock: this.generate(sourceBlock.length, start, end, true)};
  }
  // MAP is telemetry, interpolated independently from the fuel selection axis.
  manifoldPressure(rpm, fuel) {
    const rows = this.runtime.rows;
    const ri = lowerIndex(rows, rpm, (row) => row.rpm);
    const a = (rpm - rows[ri].rpm) / (rows[ri + 1].rpm - rows[ri].rpm);
    const fi = lowerIndex(this.runtime.fuelAxis, fuel, (x) => x);
    const f = this.runtime.fuelAxis;
    const b = (fuel - f[fi]) / (f[fi + 1] - f[fi]);
    const pressure = (row) => row.cells[fi].manifold_pressure_pa_abs * (1 - b) +
        row.cells[fi + 1].manifold_pressure_pa_abs * b;
    return pressure(rows[ri]) * (1 - a) + pressure(rows[ri + 1]) * a;
  }
  sample(cell, phase, channel) {
    const n = this.runtime.manifest.samples_per_cycle;
    const x = (phase - Math.floor(phase)) * n;
    const i = Math.floor(x);
    const f = x - i;
    const base = cell * n * 2;
    const samples = this.runtime.samples;
    return samples[base + i * 2 + channel] * (1 - f) +
        samples[base + ((i + 1) % n) * 2 + channel] * f;
  }
  generate(count, start, end, keep) {
    const r = this.runtime;
    this.initialize(start);
    this.initialize(end);
    const outputCount =
        Math.floor((this.pendingCount + count) / r.batchFrames) * r.batchFrames;
    const output = new Float32Array(keep ? outputCount : 0);
    let written = 0;
    const p = r.manifest.presentation;
    const duration = count / SAMPLE_RATE;
    const slope = (end.rpm - start.rpm) / duration;
    const highpass = Math.exp(-TAU * 30 / SAMPLE_RATE);
    for (let i = 0; i < count; ++i) {
      const amount = (i + 0.5) / count;
      const t = (i + 0.5) / SAMPLE_RATE;
      const rpm = start.rpm + (end.rpm - start.rpm) * amount;
      const fueling = start.fueling01 + (end.fueling01 - start.fueling01) * amount;
      const revolutions =
          start.unwrappedCrankRevolutions + (start.rpm * t + 0.5 * slope * t * t) / 60;
      const ri = lowerIndex(r.rows, rpm, (row) => row.rpm);
      const rw = (rpm - r.rows[ri].rpm) / (r.rows[ri + 1].rpm - r.rows[ri].rpm);
      let force = 0;
      let exhaust = 0;
      for (let cylinderIndex = 0; cylinderIndex < this.cylinders.length;
           ++cylinderIndex) {
        const cylinder = this.cylinders[cylinderIndex];
        let phase = revolutions / 2 - cylinder.firing_angle_rad / (4 * Math.PI);
        let fuelScale = cylinder.fuel_trim;
        if (this.articulation) {
          const cycle = Math.floor(phase + 0.5);
          // Crossfade event parameters during intake, far from the firing event.
          const u = Math.min(1, (phase + 0.5 - cycle) / 0.15);
          const blend = u * u * (3 - 2 * u);
          const variation = (lane) =>
              eventValue(cylinder.seed, cycle - 1, lane) * (1 - blend) +
              eventValue(cylinder.seed, cycle, lane) * blend;
          fuelScale *=
              1 + this.articulation.cycle_fuel_spread_01 * variation(0x68bc21eb);
          // Presentation timing spread, not a claim of solved crank-speed dynamics.
          phase +=
              this.articulation.cycle_timing_spread_deg / 720 * variation(0x02e5be93);
        }
        const fuel = Math.min(1, fueling * fuelScale);
        const fi = lowerIndex(r.fuelAxis, fuel, (x) => x);
        const fw = (fuel - r.fuelAxis[fi]) / (r.fuelAxis[fi + 1] - r.fuelAxis[fi]);
        const c0 = r.rows[ri].cells[fi].index;
        const c1 = r.rows[ri + 1].cells[fi].index;
        const w00 = (1 - rw) * (1 - fw);
        const w01 = (1 - rw) * fw;
        const w10 = rw * (1 - fw);
        const w11 = rw * fw;
        let cylinderForce =
            this.sample(c0, phase, 0) * w00 + this.sample(c0 + 1, phase, 0) * w01 +
            this.sample(c1, phase, 0) * w10 + this.sample(c1 + 1, phase, 0) * w11;
        if (this.pressureTexture) {
          const motored = this.sample(r.rows[ri].cells[0].index, phase, 0) * (1 - rw) +
              this.sample(r.rows[ri + 1].cells[0].index, phase, 0) * rw;
          cylinderForce = this.pressureTexture.process(cylinderIndex, cylinderForce, motored);
        }
        force += cylinder.structural_gain * cylinderForce;
        const flow = this.sample(c0, phase, 1) * w00 +
            this.sample(c0 + 1, phase, 1) * w01 + this.sample(c1, phase, 1) * w10 +
            this.sample(c1 + 1, phase, 1) * w11;
        this.cylinderFlows[cylinderIndex] = flow;
        exhaust += flow;
      }
      if (this.exhaustSystem) {
        this.exhaustSystem.process(this.cylinderFlows);
        exhaust = this.exhaustSystem.flow;
      }
      if (this.blockHighpass) force = this.blockHighpass.process(force);
      let block = 0;
      for (const mode of this.modes) block += mode.filter.process(force) * mode.gain;
      block = this.blockLowpass.process(block);
      this.exhaustHighpass =
          highpass * (this.exhaustHighpass + exhaust - this.exhaustPrevious);
      this.exhaustPrevious = exhaust;
      const exhaustExcitation = this.exhaustHighpassFilter ?
          this.exhaustHighpassFilter.process(exhaust) :
          this.exhaustHighpass;
      let tailpipe;
      if (this.exhaustSystem) {
        const rate = (exhaustExcitation - this.radiationPrevious) * SAMPLE_RATE;
        this.radiationPrevious = exhaustExcitation;
        tailpipe = this.exhaustLowpass.process(
            exhaustExcitation * p.exhaust_gain_pcm_per_kg_s +
            rate * p.exhaust_system.radiation_gain_pcm_s2_per_kg +
            this.exhaustSystem.noise);
      } else {
        tailpipe = this.exhaustLowpass.process(exhaustExcitation) *
            p.exhaust_gain_pcm_per_kg_s;
      }
      // Explicit artistic saturating output stage; physics captures remain SI.
      let sample = Math.tanh((block + tailpipe) * p.master_gain);
      requireValue(Number.isFinite(sample), "Diesel DSP produced nonfinite audio");
      if (keep) sample *= Math.min(1, ++this.audibleFrames / 3840);
      // Warmup may contain a large filter-initialization impulse that is never
      // emitted. Playback diagnostics must describe the audible path.
      if (keep) {
        this.peak = Math.max(this.peak, Math.abs(sample));
        this.blockEnergy += block * block;
        this.exhaustEnergy += tailpipe * tailpipe;
      }
      this.pending[this.pendingCount++] = sample;
      ++this.frames;
      if (this.pendingCount === r.batchFrames) {
        if (keep) {
          output.set(this.pending, written);
          written += r.batchFrames;
        }
        this.pendingCount = 0;
      }
    }
    return output;
  }
  diagnostics() {
    return Object.freeze({
      model: "mechanical-diesel-v1",
      loadCoordinate: "normalized-fueling",
      frames: this.frames,
      peak: this.peak,
      blockRms: Math.sqrt(this.blockEnergy / Math.max(1, this.audibleFrames)),
      exhaustRms: Math.sqrt(this.exhaustEnergy / Math.max(1, this.audibleFrames)),
      cylinderIds: this.runtime.manifest.cylinders.map((c) => c.id),
      structuralFrequenciesHz:
          this.runtime.manifest.presentation.block_modes.map((m) => m.frequency_hz)
    });
  }
}
