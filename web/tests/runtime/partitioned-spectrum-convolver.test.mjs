import assert from "node:assert/strict";
import test from "node:test";

import {
  PartitionedSpectrumConvolver,
  partitionFixedSpectrum,
} from "../../runtime/dry-directional-phase-runtime.js";
import { prepareHeldPlaybackRuntime } from "../../runtime/held-texture-presentation-runtime.js";

const fftSize = 8_192;
const blockFrames = 3_840;

function twoPartitionSpectrum() {
  const partitionCount = 2;
  const real = new Float64Array(partitionCount * fftSize);
  const imaginary = new Float64Array(partitionCount * fftSize);
  for (let bin = 0; bin < fftSize; ++bin) {
    const angle = (-2 * Math.PI * bin) / fftSize;
    real[bin] = 1 + 0.5 * Math.cos(angle);
    imaginary[bin] = 0.5 * Math.sin(angle);
    real[fftSize + bin] = 0.25;
  }
  return Object.freeze({
    fftSize,
    coefficientCount: blockFrames + 1,
    partitionFrameCount: blockFrames,
    partitionCount,
    real,
    imaginary,
  });
}

function inputFrames() {
  return Float64Array.from(
    { length: 2 * blockFrames },
    (_, frame) => Math.sin(frame * 0.013) + 0.2 * Math.cos(frame * 0.007),
  );
}

function directReference(input) {
  return Float64Array.from(input, (_, frame) =>
    input[frame] +
    0.5 * (frame >= 1 ? input[frame - 1] : 0) +
    0.25 * (frame >= blockFrames ? input[frame - blockFrames] : 0)
  );
}

test("partitioned spectrum convolution preserves the complete causal tail", () => {
  const spectrum = twoPartitionSpectrum();
  const input = inputFrames();
  const expected = directReference(input);
  const convolver = new PartitionedSpectrumConvolver(spectrum);
  const output = convolver.process(input);
  let maximumError = 0;
  for (let frame = 0; frame < output.length; ++frame) {
    maximumError = Math.max(
      maximumError,
      Math.abs(output[frame] - expected[frame]),
    );
  }
  assert.ok(maximumError < 1e-10, `maximum error ${maximumError}`);

  convolver.reset();
  const first = convolver.process(input.slice(0, blockFrames));
  const second = convolver.process(input.slice(blockFrames));
  assert.deepEqual(
    Float64Array.from([...first, ...second]),
    output,
    "separate process calls changed the partition traversal",
  );
  assert.throws(
    () => convolver.process(new Float64Array(blockFrames - 1)),
    /complete 3840-frame blocks/,
  );
});

test("prepared smaller partitions preserve every FIR tap, history wrap and reset", () => {
  const taps = Float64Array.from({ length: 23 }, (_, i) => (i % 5 - 2) / 16);
  taps[22] = .625; // Nonzero final partial partition must survive conversion.
  const size = 64;
  const real = new Float64Array(size), imaginary = new Float64Array(size);
  for (let bin = 0; bin < size; ++bin) for (let tap = 0; tap < taps.length; ++tap) {
    const angle = -2 * Math.PI * bin * tap / size;
    real[bin] += taps[tap] * Math.cos(angle);
    imaginary[bin] += taps[tap] * Math.sin(angle);
  }
  const originalReal = real.slice(), originalImaginary = imaginary.slice();
  const spectrum = partitionFixedSpectrum({ fftSize: size, coefficientCount: taps.length,
    real, imaginary }, 8);
  assert.equal(spectrum.partitionCount, 3);
  assert.equal(spectrum.coefficientCount, taps.length);
  assert.deepEqual(real, originalReal);
  assert.deepEqual(imaginary, originalImaginary);
  const input = Float64Array.from({ length: 96 }, (_, i) => i < 64 ? (i % 13 - 6) / 8 : 0);
  const expected = input.map((_, i) => taps.reduce((sum, tap, j) =>
    sum + tap * (i >= j ? input[i - j] : 0), 0));
  const convolver = new PartitionedSpectrumConvolver(spectrum);
  const output = convolver.process(input);
  for (let i = 0; i < output.length; ++i) assert.ok(Math.abs(output[i] - expected[i]) < 1e-12);
  convolver.reset();
  const restarted = new Float64Array(input.length);
  for (let i = 0; i < input.length; i += 8) restarted.set(convolver.process(input.subarray(i, i + 8)), i);
  assert.deepEqual(restarted, output);
  assert.throws(() => convolver.process(new Float64Array(7)), /complete 8-frame/);
  assert.throws(() => partitionFixedSpectrum({ fftSize: size, coefficientCount: taps.length,
    real, imaginary }, 7), /invalid/);
  assert.throws(() => new PartitionedSpectrumConvolver({ ...spectrum, partitionFrameCount: 0 }), /invalid/);
  assert.throws(() => new PartitionedSpectrumConvolver({ ...spectrum, partitionFrameCount: 10 }), /invalid/);
});

test("playback scheduling shares kernels, preserves warmup and leaves carrier metadata intact", () => {
  const spectrum = { fftSize: 32, coefficientCount: 3,
    real: new Float64Array(32).fill(1), imaginary: new Float64Array(32) };
  const transfer = Object.freeze({ kind: "fixed-overlap-save-complex-spectrum-v1", coefficientCount: 3 });
  const runtime = Object.freeze({ kind: "responsive-audio-preview", sampleRate: 48000,
    batchFrames: 8192, sharedFullWetTransfer: true, manifest: Object.freeze({ audio: { block_frames: 8192 } }),
    routePresentations: Object.freeze([Object.freeze({ transfer, spectrum }), Object.freeze({ transfer, spectrum })]) });
  const prepared = prepareHeldPlaybackRuntime(runtime, 1024);
  assert.equal(prepared.batchFrames, 1024);
  assert.equal(prepared.warmupFrames, 8192);
  assert.equal(prepared.manifest, runtime.manifest);
  assert.equal(runtime.batchFrames, 8192);
  assert.equal(runtime.routePresentations[0].spectrum, spectrum);
  assert.equal(prepared.routePresentations[0].sourceTransfer, transfer);
  assert.equal(prepared.routePresentations[0].spectrum, prepared.routePresentations[1].spectrum);
  assert.equal(prepareHeldPlaybackRuntime(runtime, 1024), prepared, "voices reuse prepared spectra");
  assert.notEqual(prepareHeldPlaybackRuntime(runtime, 2048), prepared);
  assert.throws(() => prepareHeldPlaybackRuntime({ ...runtime, sampleRate: 192000 }, 1024), /48 kHz/);
  assert.throws(() => prepareHeldPlaybackRuntime(runtime, 960), /power-of-two/);
  assert.throws(() => prepareHeldPlaybackRuntime(prepared, 1024), /fixed-transfer/);
});
