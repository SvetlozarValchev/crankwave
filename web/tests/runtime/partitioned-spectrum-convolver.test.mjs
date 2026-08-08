import assert from "node:assert/strict";
import test from "node:test";

import {
  PartitionedSpectrumConvolver,
} from "../../runtime/dry-directional-phase-runtime.js";

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
