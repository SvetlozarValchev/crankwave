// Reproduce the variety audition through the production Crankwave runtime.
import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {createHash, webcrypto} from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import {CrankwaveAudioEngine} from "../../../web/runtime/crankwave-audio-engine.js";
import {encodeFloat32Wav} from "../../../web/runtime/wav.js";

const [carrier, output, uralCarrier, ...extra] = process.argv.slice(2);
assert.ok(carrier && output && !extra.length,
    "usage: node audition.mjs <lister.crankwave> <new-output-directory> [ural.crankwave]; requires ffmpeg");
const directory = path.resolve(output);
await fs.mkdir(directory, {recursive: false});
const bytes = await fs.readFile(carrier);
const engine = await CrankwaveAudioEngine.load(bytes, {crypto: webcrypto});
assert.equal(engine.engineId, "lister-tr1-mechanical-single");
const rate = engine.sampleRate;
const metadata = {
  carrierSha256: createHash("sha256").update(bytes).digest("hex"),
  sampleRate: rate,
  timing: "Prescribed RPM and fueling; no governor or crankshaft dynamics solved.",
  captures: {}
};

function statistics(pcm) {
  let energy = 0;
  let peak = 0;
  for (const sample of pcm) {
    assert.ok(Number.isFinite(sample) && Math.abs(sample) < 1);
    energy += sample * sample;
    peak = Math.max(peak, Math.abs(sample));
  }
  return {seconds: pcm.length / rate, rms: Math.sqrt(energy / pcm.length), peak};
}

async function save(name, pcm) {
  const source = path.join(directory, `${name}-192k.wav`);
  await fs.writeFile(source, encodeFloat32Wav(pcm, rate, 1), {flag: "wx"});
  execFileSync("ffmpeg", ["-v", "error", "-n", "-i", source, "-ar", "48000",
    "-c:a", "pcm_s16le", path.join(directory, `${name}-48k.wav`)]);
}

function listeningCopy(pcm, targetDb) {
  const {rms, peak} = statistics(pcm);
  const gain = Math.min(10 ** (targetDb / 20) / rms, 0.85 / peak);
  const result = Float32Array.from(pcm, sample => sample * gain);
  // Smooth file boundaries only. No compression, per-cycle leveling or added audio.
  const fade = Math.round(rate * 0.02);
  for (let i = 0; i < fade; ++i) {
    const weight = 0.5 - 0.5 * Math.cos(Math.PI * i / (fade - 1));
    result[i] *= weight;
    result[result.length - 1 - i] *= weight;
  }
  return {pcm: result, gainDb: 20 * Math.log10(gain)};
}

const controls = [];
for (let i = 0; i < 900; ++i) {
  const t = i * 0.02;
  let rpm = 850;
  let load01 = 0.12;
  if (t >= 4 && t < 8) {
    rpm = 850 + 1150 * (t - 4) / 4;
    load01 = 0.12 + 0.63 * Math.min(1, t - 4);
  } else if (t >= 8 && t < 10) {
    rpm = 2000;
    load01 = 0.75;
  } else if (t >= 10 && t < 14) {
    rpm = 2000 - 1150 * (t - 10) / 4;
    load01 = 0;
  }
  controls.push({rpm, load01, throttle01: load01});
}
engine.setOperatingPoint(controls[0]);
engine.reset();
const sweep = new Float32Array(controls.length * engine.processFrames);
controls.forEach((control, i) => sweep.set(engine.process(control), i * engine.processFrames));
await save("idle-revs-coast-raw", sweep);
const listening = listeningCopy(sweep, -23);
await save("idle-revs-coast", listening.pcm);
metadata.captures.sweep = {...statistics(sweep), listeningGainDb: listening.gainDb,
  diagnostics: engine.diagnostics().runtime};
await fs.writeFile(path.join(directory, "controls.json"), JSON.stringify(controls, null, 2) + "\n");

function held(e, rpm, load01) {
  e.setOperatingPoint({rpm, load01, throttle01: load01});
  e.reset();
  // Discard 0.25 seconds of the initial audible fade and keep five settled seconds.
  return e.render(1_008_000).slice(48_000);
}

const singleIdle = held(engine, 850, 0.12);
const singleLoaded = held(engine, 1500, 0.65);
for (const [name, pcm] of [["idle", singleIdle], ["loaded-1500", singleLoaded]]) {
  const copy = listeningCopy(pcm, -25);
  await save(name, copy.pcm);
  metadata.captures[name] = {...statistics(pcm), listeningGainDb: copy.gainDb};
}

if (uralCarrier) {
  const uralBytes = await fs.readFile(uralCarrier);
  const ural = await CrankwaveAudioEngine.load(uralBytes, {crypto: webcrypto});
  assert.equal(ural.engineId, "ural4320-mechanical-v8");
  metadata.uralCarrierSha256 = createHash("sha256").update(uralBytes).digest("hex");
  const sections = [
    ["Ural idle, 725 RPM", held(ural, 725, 0.12)],
    ["Lister-style single idle, 850 RPM", singleIdle],
    ["Ural loaded, 1500 RPM", held(ural, 1500, 0.65)],
    ["Lister-style single loaded, 1500 RPM", singleLoaded]
  ];
  const comparison = new Float32Array(Math.round(21.5 * rate));
  metadata.comparison = [];
  sections.forEach(([label, pcm], i) => {
    const copy = listeningCopy(pcm, -25);
    comparison.set(copy.pcm, Math.round(i * 5.5 * rate));
    metadata.comparison.push({label, startSeconds: i * 5.5, endSeconds: i * 5.5 + 5,
      gainDb: copy.gainDb, ...statistics(copy.pcm)});
  });
  await save("ural-then-single-comparison", comparison);
}
await fs.writeFile(path.join(directory, "audition.json"), JSON.stringify(metadata, null, 2) + "\n");
console.log(JSON.stringify({directory, ...metadata}, null, 2));
