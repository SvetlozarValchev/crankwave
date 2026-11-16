// Reproduce the variety audition through the production Crankwave runtime.
import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {createHash, webcrypto} from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import {CrankwaveAudioEngine} from "../../../web/runtime/crankwave-audio-engine.js";
import {encodeFloat32Wav} from "../../../web/runtime/wav.js";

const [carrier, output, uralCarrier, previousCarrier, ...extra] = process.argv.slice(2);
assert.ok(carrier && output && !extra.length,
    "usage: node audition.mjs <golf.crankwave> <new-output-directory> [ural.crankwave] [previous-golf.crankwave]; requires ffmpeg");
const directory = path.resolve(output);
await fs.mkdir(directory, {recursive: false});
const bytes = await fs.readFile(carrier);
const engine = await CrankwaveAudioEngine.load(bytes, {crypto: webcrypto});
assert.equal(engine.engineId, "golf16-common-rail-i4");
const idleRpm = Number(process.env.CRANKWAVE_IDLE_RPM ?? 800);
assert.ok(Number.isFinite(idleRpm) && idleRpm >= engine.minimumRpm && idleRpm < 950,
    "CRANKWAVE_IDLE_RPM must be within the carrier range and below 950 RPM");
const rate = engine.sampleRate;
const metadata = {
  carrierSha256: createHash("sha256").update(bytes).digest("hex"),
  sampleRate: rate,
  idleRpm,
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
  let rpm = idleRpm;
  let load01 = 0.12;
  if (t >= 4 && t < 8) {
    rpm = idleRpm + (3200 - idleRpm) * (t - 4) / 4;
    load01 = 0.12 + 0.63 * Math.min(1, t - 4);
  } else if (t >= 8 && t < 10) {
    rpm = 3200;
    load01 = 0.75;
  } else if (t >= 10 && t < 14) {
    rpm = 3200 - (3200 - idleRpm) * (t - 10) / 4;
    load01 = rpm > 950 ? 0 : 0.12 * (950 - rpm) / (950 - idleRpm);
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

const modernIdle = held(engine, idleRpm, 0.12);
const modernLoaded = held(engine, 1800, 0.65);
for (const [name, pcm] of [["idle", modernIdle], ["loaded-1800", modernLoaded]]) {
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
    [`Golf-style common rail idle, ${idleRpm} RPM`, modernIdle],
    ["Ural loaded, 1500 RPM", held(ural, 1500, 0.65)],
    ["Golf-style common rail loaded, 1800 RPM", modernLoaded]
  ];
  const comparison = new Float32Array(Math.round(21.5 * rate));
  metadata.comparison = [];
  sections.forEach(([label, pcm], i) => {
    const copy = listeningCopy(pcm, -25);
    comparison.set(copy.pcm, Math.round(i * 5.5 * rate));
    metadata.comparison.push({label, startSeconds: i * 5.5, endSeconds: i * 5.5 + 5,
      gainDb: copy.gainDb, ...statistics(copy.pcm)});
  });
  await save("ural-then-modern-comparison", comparison);
}
if (previousCarrier) {
  const previousBytes = await fs.readFile(previousCarrier);
  const previous = await CrankwaveAudioEngine.load(previousBytes, {crypto: webcrypto});
  assert.equal(previous.engineId, engine.engineId);
  metadata.previousCarrierSha256 = createHash("sha256").update(previousBytes).digest("hex");
  metadata.characterComparisons = {};
  for (const [name, rpm, fuel, after] of [["idle", idleRpm, 0.12, modernIdle],
      ["loaded", 1800, 0.65, modernLoaded]]) {
    const before = held(previous, rpm, fuel);
    const comparison = new Float32Array(Math.round(10.5 * rate));
    const details = [];
    [before, after].forEach((pcm, i) => {
      const copy = listeningCopy(pcm, -25);
      comparison.set(copy.pcm, Math.round(i * 5.5 * rate));
      details.push({label: i ? "after" : "before", startSeconds: i * 5.5,
        endSeconds: i * 5.5 + 5, gainDb: copy.gainDb, ...statistics(copy.pcm)});
    });
    await save(`before-after-${name}`, comparison);
    metadata.characterComparisons[name] = {rpm, fuel, sections: details};
  }
}
await fs.writeFile(path.join(directory, "audition.json"), JSON.stringify(metadata, null, 2) + "\n");
console.log(JSON.stringify({directory, ...metadata}, null, 2));
