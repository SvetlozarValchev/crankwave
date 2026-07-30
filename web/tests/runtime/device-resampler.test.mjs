import assert from "node:assert/strict";
import test from "node:test";

import {
  DEVICE_RESAMPLER_ID,
  DeviceRateResampler,
} from "../../runtime/device-resampler.js";

function concatenate(left, right) {
  const output = new Float32Array(left.length + right.length);
  output.set(left);
  output.set(right, left.length);
  return output;
}

test("versioned resampler produces the exact duration at common device rates", () => {
  for (const outputSampleRate of [44_100, 48_000, 96_000, 192_000]) {
    const resampler = new DeviceRateResampler({
      outputSampleRate,
      channelCount: 1,
    });
    assert.equal(
      resampler.id,
      "engine-sim-offline/windowed-sinc-129-phase2048-v1",
    );
    assert.equal(resampler.id, DEVICE_RESAMPLER_ID);
    const input = new Float32Array(19_200).fill(1);
    const output = concatenate(resampler.push(input), resampler.finish());
    assert.equal(output.length, outputSampleRate / 10);
    for (let index = 100; index < output.length - 100; ++index) {
      assert.ok(Math.abs(output[index] - 1) < 1e-6);
    }
  }
});

test("streaming block boundaries do not change resampled output", () => {
  const input = new Float32Array(19_200);
  for (let index = 0; index < input.length; ++index) {
    input[index] =
      0.7 * Math.sin((2 * Math.PI * 731 * index) / 192_000) +
      0.2 * Math.sin((2 * Math.PI * 12_311 * index) / 192_000);
  }
  const whole = new DeviceRateResampler({
    outputSampleRate: 48_000,
    channelCount: 1,
  });
  const wholeOutput = concatenate(whole.push(input), whole.finish());

  const streamed = new DeviceRateResampler({
    outputSampleRate: 48_000,
    channelCount: 1,
  });
  const chunks = [];
  let total = 0;
  for (let offset = 0; offset < input.length; offset += 3840) {
    const chunk = streamed.push(input.subarray(offset, offset + 3840));
    chunks.push(chunk);
    total += chunk.length;
  }
  const tail = streamed.finish();
  chunks.push(tail);
  total += tail.length;
  const streamedOutput = new Float32Array(total);
  let destination = 0;
  for (const chunk of chunks) {
    streamedOutput.set(chunk, destination);
    destination += chunk.length;
  }
  assert.deepEqual(streamedOutput, wholeOutput);
});

test("a 192 kHz device receives the canonical master byte-for-byte", () => {
  const input = Float32Array.of(
    -1,
    -0.125,
    0,
    0.125,
    0.875,
    Number.MIN_VALUE,
  );
  const resampler = new DeviceRateResampler({
    outputSampleRate: 192_000,
    channelCount: 1,
  });
  assert.deepEqual(
    new Uint8Array(resampler.push(input).buffer),
    new Uint8Array(input.buffer),
  );
  assert.equal(resampler.finish().length, 0);
});
