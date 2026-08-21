import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import { readFile, realpath } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

if (process.argv.length !== 4) {
  throw new Error(
    "usage: node installed_vehicleengine_playback_test.mjs " +
      "<installed-vehicleengine-audio-engine.js> <carrier.vehicleengine>",
  );
}

const modulePath = await realpath(path.resolve(process.argv[2]));
const carrierPath = await realpath(path.resolve(process.argv[3]));
const { VehicleEngineAudioEngine } = await import(pathToFileURL(modulePath).href);
const carrier = await readFile(carrierPath);
const SESSION_SEED = "736234";

function digest(samples) {
  return createHash("sha256")
    .update(new Uint8Array(
      samples.buffer,
      samples.byteOffset,
      samples.byteLength,
    ))
    .digest("hex");
}

function concatenate(chunks) {
  const frameCount = chunks.reduce((total, chunk) => total + chunk.length, 0);
  const output = new Float32Array(frameCount);
  let offset = 0;
  for (const chunk of chunks) {
    output.set(chunk, offset);
    offset += chunk.length;
  }
  return output;
}

function assertFinite(samples) {
  for (const sample of samples) assert.equal(Number.isFinite(sample), true);
}

function controlPoint(engine, ordinal, count) {
  const phase = ordinal / (count - 1);
  const sweep = phase <= 0.5 ? phase * 2 : (1 - phase) * 2;
  const span = engine.maximumRpm - engine.minimumRpm;
  return Object.freeze({
    rpm: engine.minimumRpm + span * (0.12 + 0.72 * sweep),
    throttle01: 0.2 + 0.65 * sweep,
    load01: 0.25 + 0.55 * (0.5 - 0.5 * Math.cos(phase * 2 * Math.PI)),
  });
}

async function load() {
  return VehicleEngineAudioEngine.load(carrier, {
    crypto: webcrypto,
    sessionSeed: SESSION_SEED,
  });
}

async function renderStreamingTrajectory() {
  const engine = await load();
  const defaultProcessFrames = engine.sampleRate / 50;
  assert.equal(Number.isSafeInteger(defaultProcessFrames), true);
  const callCount = Math.ceil(2 * engine.blockFrames / defaultProcessFrames) + 4;
  const chunks = [];
  let submittedFrames = 0;
  for (let ordinal = 0; ordinal < callCount; ++ordinal) {
    const point = controlPoint(engine, ordinal, callCount);
    const output = engine.process(point);
    assert.equal(output.length, defaultProcessFrames);
    assertFinite(output);
    chunks.push(output);
    submittedFrames += output.length;

    const diagnostics = engine.diagnostics();
    assert.equal(diagnostics.renderMode, "streaming");
    assert.deepEqual(diagnostics.committedOperatingPoint, point);
    assert.equal(diagnostics.renderedFrames, submittedFrames);
    assert.equal(
      diagnostics.runtime.audibleInputFrameCount,
      submittedFrames,
    );
    assert.equal(
      diagnostics.runtime.audibleOutputFrameCount,
      Math.floor(submittedFrames / engine.blockFrames) * engine.blockFrames,
    );
    assert.equal(
      diagnostics.queuedFrames + diagnostics.runtime.pendingFrames,
      engine.blockFrames,
    );
  }
  const pcm = concatenate(chunks);
  assert.equal(
    pcm.subarray(0, engine.blockFrames).every((sample) => sample === 0),
    true,
    "streaming playback must expose exactly one silent latency batch",
  );
  let audiblePower = 0;
  for (const sample of pcm.subarray(engine.blockFrames)) {
    audiblePower += sample * sample;
  }
  assert.ok(audiblePower > 0, "installed streaming playback must emit audio");
  return { engine, pcm };
}

const first = await renderStreamingTrajectory();
const second = await renderStreamingTrajectory();
assert.equal(digest(first.pcm), digest(second.pcm));
assert.deepEqual(first.pcm, second.pcm);

// For a constant endpoint, process() is byte-identical to render() after its
// one explicit latency batch. This proves that 20 ms control submission changes
// scheduling, not the packaged synthesis or presentation bytes.
const streamingConstant = await load();
const constantPoint = streamingConstant.operatingPoint;
const constantChunks = [];
let constantFrames = 0;
while (constantFrames <= streamingConstant.blockFrames) {
  const output = streamingConstant.process(constantPoint);
  constantChunks.push(output);
  constantFrames += output.length;
}
const constantPcm = concatenate(constantChunks);
const expected = await load();
expected.setOperatingPoint(constantPoint);
const expectedPcm = expected.render(
  constantPcm.length - streamingConstant.blockFrames,
);
assert.deepEqual(
  constantPcm.subarray(streamingConstant.blockFrames),
  expectedPcm,
);

const corrupted = Uint8Array.from(carrier);
corrupted[corrupted.length - 1] ^= 0x01;
await assert.rejects(
  VehicleEngineAudioEngine.load(corrupted, {
    crypto: webcrypto,
    sessionSeed: SESSION_SEED,
  }),
  (error) => error?.code === "payload-hash-mismatch",
);
