// Render a prescribed reference-shaped sweep from an authenticated native carrier.
import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {createHash, webcrypto} from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import {CrankwaveAudioEngine} from "../../../web/runtime/crankwave-audio-engine.js";
import {encodeFloat32Wav} from "../../../web/runtime/wav.js";

const [carrier, controlsPath, output, ...extra] = process.argv.slice(2);
assert.ok(carrier && controlsPath && output && !extra.length,
    "usage: node ramp-audition.mjs <golf.crankwave> <controls.json> <new-output-directory>; requires ffmpeg");
const bytes = await fs.readFile(carrier);
const controlBytes = await fs.readFile(controlsPath);
const controls = JSON.parse(controlBytes);
const engine = await CrankwaveAudioEngine.load(bytes, {crypto: webcrypto});
assert.equal(engine.engineId, "golf16-common-rail-i4");
assert.ok(Array.isArray(controls.frames) && controls.frames.length > 0);
const frameSeconds = engine.processFrames / engine.sampleRate;
for (const [i, frame] of controls.frames.entries()) {
  assert.ok(Number.isFinite(frame.time_s) && Math.abs(frame.time_s - i * frameSeconds) < 1e-8);
  assert.ok(Number.isFinite(frame.rpm) && frame.rpm >= engine.minimumRpm && frame.rpm <= engine.maximumRpm);
  for (const key of ["load01", "throttle01"]) {
    assert.ok(Number.isFinite(frame[key]) && frame[key] >= 0 && frame[key] <= 1);
  }
}
const directory = path.resolve(output);
await fs.mkdir(directory, {recursive: false});
engine.setOperatingPoint(controls.frames[0]);
engine.reset();
const pcm = new Float32Array(engine.processFrames * controls.frames.length);
controls.frames.forEach(({time_s, ...control}, i) => {
  pcm.set(engine.process(control), i * engine.processFrames);
});
let peak = 0;
let energy = 0;
for (const sample of pcm) {
  assert.ok(Number.isFinite(sample) && Math.abs(sample) < 1);
  peak = Math.max(peak, Math.abs(sample));
  energy += sample * sample;
}
const rms = Math.sqrt(energy / pcm.length);
assert.ok(rms > 0);
const gain = Math.min(10 ** (-25 / 20) / rms, 0.85 / peak);
const listening = Float32Array.from(pcm, sample => sample * gain);
const fade = Math.round(engine.sampleRate * 0.02);
for (let i = 0; i < Math.min(fade, Math.floor(listening.length / 2)); ++i) {
  const weight = 0.5 - 0.5 * Math.cos(Math.PI * i / (fade - 1));
  listening[i] *= weight;
  listening[listening.length - 1 - i] *= weight;
}
for (const [name, samples] of [["rev-rundown-raw", pcm], ["rev-rundown", listening]]) {
  const rawPath = path.join(directory, `${name}-192k.wav`);
  await fs.writeFile(rawPath, encodeFloat32Wav(samples, engine.sampleRate, 1), {flag: "wx"});
  if (name === "rev-rundown") {
    execFileSync("ffmpeg", ["-v", "error", "-n", "-i", rawPath, "-ar", "48000",
      "-c:a", "pcm_s16le", path.join(directory, `${name}-48k.wav`)]);
  }
}
await fs.writeFile(path.join(directory, "controls.json"), controlBytes, {flag: "wx"});
const metadata = {
  carrierSha256: createHash("sha256").update(bytes).digest("hex"),
  controlsSha256: createHash("sha256").update(controlBytes).digest("hex"),
  source: controls.source,
  sourceSha256: controls.source_sha256,
  controlsMethod: controls.method,
  synthesis: "Production Crankwave runtime; native carrier loaded without modification. No reference PCM in candidate.",
  sampleRate: engine.sampleRate,
  seconds: pcm.length / engine.sampleRate,
  rpmRange: [Math.min(...controls.frames.map(f => f.rpm)), Math.max(...controls.frames.map(f => f.rpm))],
  rms, peak,
  listeningGainDb: 20 * Math.log10(gain),
  mastering: "One constant gain for entire clip, 20 ms endpoint fades, 48 kHz PCM16 export.",
  diagnostics: engine.diagnostics().runtime
};
await fs.writeFile(path.join(directory, "audition.json"), JSON.stringify(metadata, null, 2) + "\n", {flag: "wx"});
console.log(JSON.stringify({directory, ...metadata}, null, 2));
