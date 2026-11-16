import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import {execFileSync} from "node:child_process";
import {createHash} from "node:crypto";
import {CrankwaveAudioEngine} from "../../../web/runtime/crankwave-audio-engine.js";
import {encodeFloat32Wav} from "../../../web/runtime/wav.js";

const [baseFile, variantFile, output, ...extra] = process.argv.slice(2);
assert.ok(baseFile && variantFile && output && !extra.length,
  "usage: node audition.mjs <base.crankwave> <mechanical.crankwave> <new-output-directory>; requires ffmpeg");
await fs.mkdir(output, {recursive: false});
const rate = 192000;
const report = {carriers: {}, captures: {}, comparison: {idle: "0-5 s original; 5.5-10.5 s mechanical",
  sweep: "0-18 s original; 18.5-36.5 s mechanical", processing: "Each complete segment RMS-matched to -25 dBFS with constant gain and 20 ms boundary fades; no compression."}};
function stats(x) {
  let peak = 0, energy = 0;
  for (const v of x) { assert.ok(Number.isFinite(v) && Math.abs(v) < 1); peak = Math.max(peak, Math.abs(v)); energy += v * v; }
  return {rms: Math.sqrt(energy / x.length), peak, seconds: x.length / rate};
}
function listen(x) {
  const s = stats(x), gain = Math.min(10 ** (-25 / 20) / s.rms, .85 / s.peak);
  const y = Float32Array.from(x, v => v * gain);
  for (let i = 0; i < 3840; ++i) {
    const f = .5 - .5 * Math.cos(Math.PI * i / 3839);
    y[i] *= f; y[y.length - 1 - i] *= f;
  }
  return {pcm: y, gainDb: 20 * Math.log10(gain)};
}
async function save(name, pcm) {
  const raw = path.join(output, name + "-192k.wav");
  await fs.writeFile(raw, encodeFloat32Wav(pcm, rate, 1), {flag: "wx"});
  execFileSync("ffmpeg", ["-v", "error", "-n", "-i", raw, "-ar", "48000", "-c:a", "pcm_s16le",
    path.join(output, name + "-48k.wav")]);
}
const controls = Array.from({length: 900}, (_, i) => {
  const t = i / 50;
  let rpm = 765, load01 = 0;
  if (t >= 4 && t < 8) { rpm += (3000 - 765) * (t - 4) / 4; load01 = .65 * Math.min(1, t - 4); }
  else if (t >= 8 && t < 9) { rpm = 3000; load01 = .65; }
  else if (t >= 9 && t < 14) { rpm = 3000 - (3000 - 765) * (t - 9) / 5; }
  return {rpm, load01, throttle01: load01};
});
const signals = {};
for (const [label, file] of [["before", baseFile], ["after", variantFile]]) {
  const bytes = await fs.readFile(file);
  const engine = await CrankwaveAudioEngine.load(bytes);
  assert.equal(engine.engineId, "uaz469-reference-3m");
  assert.equal(engine.sampleRate, rate);
  assert.equal(Boolean(engine.diagnostics().mechanicalTexture), label === "after");
  report.carriers[label] = {path: file, sha256: createHash("sha256").update(bytes).digest("hex")};
  signals[label] = {};
  for (const [name, rpm, load01] of [["idle", 765, 0], ["loaded", 2000, .65], ["coast", 2000, 0]]) {
    engine.setOperatingPoint({rpm, load01, throttle01: load01}); engine.reset();
    const pcm = engine.render(1008000).slice(48000);
    signals[label][name] = pcm;
    const copy = listen(pcm);
    await save(label + "-" + name, copy.pcm);
    report.captures[label + "-" + name] = {...stats(pcm), gainDb: copy.gainDb,
      mechanical: engine.diagnostics().mechanicalTexture};
  }
  engine.setOperatingPoint(controls[0]); engine.reset();
  const count = controls.length * engine.processFrames;
  const blocks = [], allControls = [...controls];
  while (allControls.length * engine.processFrames < count + engine.latencyFrames)
    allControls.push(controls.at(-1));
  for (const control of allControls) blocks.push(engine.process(control));
  const stream = new Float32Array(blocks.length * engine.processFrames);
  blocks.forEach((b, i) => stream.set(b, i * engine.processFrames));
  const pcm = stream.slice(engine.latencyFrames, engine.latencyFrames + count);
  signals[label].sweep = pcm;
  await save(label + "-sweep-raw", pcm);
  const copy = listen(pcm);
  await save(label + "-sweep", copy.pcm);
  report.captures[label + "-sweep"] = {...stats(pcm), gainDb: copy.gainDb,
    mechanical: engine.diagnostics().mechanicalTexture};
}
for (const name of ["idle", "loaded", "coast", "sweep"]) {
  const before = listen(signals.before[name]).pcm, after = listen(signals.after[name]).pcm;
  const joined = new Float32Array(before.length + rate / 2 + after.length);
  joined.set(before); joined.set(after, before.length + rate / 2);
  await save(name + "-comparison", joined);
}
await fs.writeFile(path.join(output, "controls.json"), JSON.stringify(controls, null, 2) + "\n");
await fs.writeFile(path.join(output, "report.json"), JSON.stringify(report, null, 2) + "\n");
console.log(JSON.stringify(report, null, 2));
