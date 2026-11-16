#!/usr/bin/env node
import fs from "node:fs/promises";
import {webcrypto} from "node:crypto";
import {CrankwaveAudioEngine} from "../web/runtime/crankwave-audio-engine.js";
import {encodeFloat32Wav} from "../web/runtime/wav.js";

const usage =
    "node tools/render-crankwave.mjs <input.crankwave> <output.wav> <rpm> <load01> [seconds=4]";
try {
  const [input, output, rpmText, loadText, secondsText = "4", ...extra] =
      process.argv.slice(2);
  if (!input || !output || !rpmText || loadText === undefined || extra.length)
    throw new Error(usage);
  const rpm = Number(rpmText);
  const load01 = Number(loadText);
  const seconds = Number(secondsText);
  if (!Number.isFinite(seconds) || seconds <= 0 || seconds > 60)
    throw new Error("Duration must be in (0, 60] seconds.");
  const engine =
      await CrankwaveAudioEngine.load(await fs.readFile(input), {crypto: webcrypto});
  engine.setOperatingPoint({rpm, throttle01: load01, load01});
  engine.reset();
  const count = Math.round(seconds * engine.sampleRate);
  if (count < 1) throw new Error("Duration is shorter than one output frame.");
  const pcm = new Float32Array(count);
  let offset = 0;
  while (offset < count) {
    const frames = Math.min(192000, count - offset);
    pcm.set(engine.render(frames), offset);
    offset += frames;
  }
  // Existing renders are never overwritten implicitly.
  await fs.writeFile(output, encodeFloat32Wav(pcm, engine.sampleRate, 1), {flag: "wx"});
  let power = 0;
  let peak = 0;
  for (const sample of pcm) {
    power += sample * sample;
    peak = Math.max(peak, Math.abs(sample));
  }
  console.log(JSON.stringify({
    output,
    engine: engine.engineId,
    rpm,
    load01,
    seconds: count / engine.sampleRate,
    loadCoordinate: engine.loadCoordinate,
    peak,
    rms: Math.sqrt(power / count)
  }));
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
