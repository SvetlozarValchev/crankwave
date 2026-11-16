// Optional gasoline presentation layer. Reuses the exact smooth contact and
// crank-window breath implementation introduced for diesel; no diesel burn model.
import {DieselMechanicalTexture} from "./diesel-mechanical-runtime.js";
import {requireSupportedAudioDeliverySampleRate} from "./audio-delivery-profile.js";

const RATE = 192000;
const KEYS = ["schema", "engine", "parameters", "timing", "cylinders",
  "intake_reference_pressure_pa_abs", "intake_pressure_exponent", "gain_linear", "provenance"];

function check(condition, message) {
  if (!condition) throw new Error(`Invalid mechanical texture: ${message}`);
}
function object(value, keys, label) {
  check(value && typeof value === "object" && !Array.isArray(value) &&
    Object.keys(value).length === keys.length && keys.every(k => Object.hasOwn(value, k)), label);
}
function number(value, minimum, maximum, label) {
  check(Number.isFinite(value) && value >= minimum && value <= maximum, label);
}

export function validateMechanicalTexture(value, engine, sourceIdentity) {
  if (value === undefined) return null;
  object(value, KEYS, "manifest fields");
  check(value.schema === "crankwave/mechanical-texture-v1" && value.engine === engine, "schema/engine");
  const p = value.parameters;
  object(p, ["valve_gain_pcm", "valve_frequency_hz", "valve_decay_s", "valve_attack_s",
    "pumping_gain_pcm", "pumping_lowpass_hz", "seed"], "parameters");
  for (const [key, min, max] of [["valve_gain_pcm", 0, 1], ["valve_frequency_hz", 200, 3000],
    ["valve_decay_s", .0005, .02], ["valve_attack_s", .001, .01],
    ["pumping_gain_pcm", 0, .1], ["pumping_lowpass_hz", 300, 3000], ["seed", 0, 0xffffffff]])
    number(p[key], min, max, key);
  check(Number.isInteger(p.seed), "seed must be integral");
  const t = value.timing;
  object(t, ["intake_open_angle_rad", "intake_close_angle_rad", "exhaust_close_angle_rad"], "timing");
  for (const [key, angle] of Object.entries(t)) number(angle, 0, 4 * Math.PI, key);
  check(t.intake_close_angle_rad > t.intake_open_angle_rad, "intake window");
  check(Array.isArray(value.cylinders) && value.cylinders.length >= 1 && value.cylinders.length <= 12,
    "cylinder count");
  const ids = new Set();
  for (const c of value.cylinders) {
    object(c, ["id", "firing_angle_rad", "structural_gain"], "cylinder");
    check(typeof c.id === "string" && /^[a-z0-9][a-z0-9._-]*$/.test(c.id) && !ids.has(c.id), "cylinder id");
    ids.add(c.id);
    number(c.firing_angle_rad, 0, 4 * Math.PI, "cylinder phase");
    check(c.firing_angle_rad < 4 * Math.PI, "cylinder phase wraps at 720 degrees");
    number(c.structural_gain, 0, 2, "structural gain");
  }
  number(value.intake_reference_pressure_pa_abs, 50000, 200000, "intake reference pressure");
  number(value.intake_pressure_exponent, .25, 2, "intake pressure exponent");
  number(value.gain_linear, 0, 2, "gain");
  object(value.provenance, ["engine_identity_sha256", "authoring_sha256", "base_carrier_sha256", "note"], "provenance");
  for (const key of ["engine_identity_sha256", "authoring_sha256", "base_carrier_sha256"])
    check(typeof value.provenance[key] === "string" && /^[a-f0-9]{64}$/.test(value.provenance[key]), key);
  check(value.provenance.engine_identity_sha256 === sourceIdentity, "source identity");
  check(typeof value.provenance.note === "string" && value.provenance.note.length > 0, "provenance note");
  return Object.freeze(structuredClone(value));
}

// Matches the facade's source clock, warmup, and fixed presentation batches.
// Output joins the exhaust after its transfer, before the facade latency queue.
export class MechanicalTextureCursor {
  constructor(manifest, batchFrames, sampleRate = RATE) {
    this.manifest = manifest;
    this.batchFrames = batchFrames;
    this.sampleRate = requireSupportedAudioDeliverySampleRate(sampleRate);
    this.texture = new DieselMechanicalTexture(manifest.parameters, manifest.timing, manifest.cylinders, this.sampleRate);
    this.pending = new Float32Array(batchFrames);
    this.reset();
  }
  reset() {
    this.texture.reset();
    this.pending.fill(0);
    this.count = 0;
    this.frames = 0;
    this.valveEnergy = 0;
    this.intakeEnergy = 0;
  }
  renderBlock({frameCount, start, end, warm = false}) {
    const output = new Float32Array(Math.floor((this.count + frameCount) / this.batchFrames) * this.batchFrames);
    let offset = 0;
    const m = this.manifest;
    for (let i = 0; i < frameCount; ++i) {
      const u = (i + 1) / frameCount;
      const rpm = start.rpm + (end.rpm - start.rpm) * u;
      const revs = start.unwrappedCrankRevolutions +
        (end.unwrappedCrankRevolutions - start.unwrappedCrankRevolutions) * u;
      const map = start.manifoldPressurePaAbs + (end.manifoldPressurePaAbs - start.manifoldPressurePaAbs) * u;
      this.texture.process(revs, rpm);
      const intakeGain = Math.max(0, Math.min(1, map / m.intake_reference_pressure_pa_abs)) ** m.intake_pressure_exponent;
      const valve = this.texture.valves * m.gain_linear;
      const intake = this.texture.pumping * intakeGain * m.gain_linear;
      const fade = warm ? 1 : Math.min(1, ++this.frames / (this.sampleRate * .02));
      this.pending[this.count++] = (valve + intake) * fade;
      if (!warm) {
        this.valveEnergy += (valve * fade) ** 2;
        this.intakeEnergy += (intake * fade) ** 2;
      }
      if (this.count === this.batchFrames) {
        output.set(this.pending, offset);
        offset += this.batchFrames;
        this.count = 0;
      }
    }
    return output;
  }
  diagnostics() {
    return {valveRms: Math.sqrt(this.valveEnergy / Math.max(1, this.frames)),
      intakeRms: Math.sqrt(this.intakeEnergy / Math.max(1, this.frames)), frames: this.frames};
  }
}
