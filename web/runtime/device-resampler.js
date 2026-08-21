export const DEVICE_RESAMPLER_ID =
  "engine-sim-offline/windowed-sinc-129-phase2048-v1";

// This resampler is also part of the standalone VEHICLEENGINE consumer surface.
// Keep its rate contract independent of the simulation C API: a VEHICLEENGINE
// player must not need to load or understand the renderer ABI.
const MAXIMUM_SUPPORTED_SAMPLE_RATE = 192_000;

const TAP_COUNT = 129;
const MINIMUM_OFFSET = -64;
const MAXIMUM_OFFSET = 64;
const PHASE_COUNT = 2_048;
const ROLLOFF = 0.96;

function finiteRate(value, name) {
  if (
    typeof value !== "number" ||
    !Number.isSafeInteger(value) ||
    value < 8_000 ||
    value > MAXIMUM_SUPPORTED_SAMPLE_RATE
  ) {
    throw new RangeError(
      `${name} must be an integer in [8000, ${MAXIMUM_SUPPORTED_SAMPLE_RATE}] Hz`,
    );
  }
  return value;
}

function blackmanWindow(distance) {
  const normalized = Math.abs(distance) / MAXIMUM_OFFSET;
  if (normalized >= 1) {
    return 0;
  }
  return (
    0.42 +
    0.5 * Math.cos(Math.PI * normalized) +
    0.08 * Math.cos(2 * Math.PI * normalized)
  );
}

function sincLowPass(distance, cutoff) {
  if (Math.abs(distance) < 1e-14) {
    return cutoff;
  }
  return Math.sin(Math.PI * cutoff * distance) / (Math.PI * distance);
}

function makePhaseTable(inputRate, outputRate) {
  const cutoff = Math.min(1, outputRate / inputRate) * ROLLOFF;
  const table = new Float64Array((PHASE_COUNT + 1) * TAP_COUNT);
  for (let phase = 0; phase <= PHASE_COUNT; ++phase) {
    const fraction = phase / PHASE_COUNT;
    let sum = 0;
    for (let tap = 0; tap < TAP_COUNT; ++tap) {
      const offset = MINIMUM_OFFSET + tap;
      const distance = offset - fraction;
      const coefficient =
        sincLowPass(distance, cutoff) * blackmanWindow(distance);
      table[phase * TAP_COUNT + tap] = coefficient;
      sum += coefficient;
    }
    if (!Number.isFinite(sum) || Math.abs(sum) < 1e-12) {
      throw new Error("device resampler coefficient normalization failed");
    }
    for (let tap = 0; tap < TAP_COUNT; ++tap) {
      table[phase * TAP_COUNT + tap] /= sum;
    }
  }
  return table;
}

export class DeviceRateResampler {
  #inputRate;
  #outputRate;
  #channelCount;
  #inputFramesPerOutputFrame;
  #phaseTable;
  #identity;
  #buffer = new Float32Array(0);
  #bufferStartFrame = 0;
  #realInputFrameCount = 0;
  #nextOutputFrame = 0;
  #finished = false;

  constructor({
    inputSampleRate = MAXIMUM_SUPPORTED_SAMPLE_RATE,
    outputSampleRate,
    channelCount,
  }) {
    this.#inputRate = finiteRate(inputSampleRate, "inputSampleRate");
    this.#outputRate = finiteRate(outputSampleRate, "outputSampleRate");
    if (!Number.isSafeInteger(channelCount) || channelCount < 1 || channelCount > 32) {
      throw new RangeError("channelCount must be an integer in [1, 32]");
    }
    this.#channelCount = channelCount;
    this.#inputFramesPerOutputFrame = this.#inputRate / this.#outputRate;
    this.#identity = this.#inputRate === this.#outputRate;
    this.#phaseTable = this.#identity
      ? null
      : makePhaseTable(this.#inputRate, this.#outputRate);
  }

  get id() {
    return DEVICE_RESAMPLER_ID;
  }

  get inputSampleRate() {
    return this.#inputRate;
  }

  get outputSampleRate() {
    return this.#outputRate;
  }

  get channelCount() {
    return this.#channelCount;
  }

  maximumOutputFramesForInput(inputFrames) {
    if (!Number.isSafeInteger(inputFrames) || inputFrames < 0) {
      throw new RangeError("inputFrames must be a non-negative safe integer");
    }
    if (this.#identity) {
      return inputFrames;
    }
    return (
      Math.ceil(
        ((inputFrames + TAP_COUNT + 2) * this.#outputRate) / this.#inputRate,
      ) + 2
    );
  }

  push(interleaved) {
    if (this.#finished) {
      throw new Error("cannot push PCM after the device resampler is finished");
    }
    if (!(interleaved instanceof Float32Array)) {
      throw new TypeError("resampler input must be Float32Array");
    }
    if (interleaved.length % this.#channelCount !== 0) {
      throw new RangeError("resampler input is not channel-aligned");
    }
    if (this.#identity) {
      this.#realInputFrameCount += interleaved.length / this.#channelCount;
      return interleaved.slice();
    }
    const frames = interleaved.length / this.#channelCount;
    this.#append(interleaved);
    this.#realInputFrameCount += frames;
    return this.#process(Number.POSITIVE_INFINITY, frames);
  }

  finish() {
    if (this.#finished) {
      return new Float32Array(0);
    }
    this.#finished = true;
    if (this.#identity) {
      return new Float32Array(0);
    }
    const targetOutputFrames = Math.ceil(
      (this.#realInputFrameCount * this.#outputRate) / this.#inputRate,
    );
    this.#append(
      new Float32Array((MAXIMUM_OFFSET + 2) * this.#channelCount),
    );
    return this.#process(targetOutputFrames, MAXIMUM_OFFSET + 2);
  }

  #append(interleaved) {
    if (interleaved.length === 0) {
      return;
    }
    const next = new Float32Array(this.#buffer.length + interleaved.length);
    next.set(this.#buffer, 0);
    next.set(interleaved, this.#buffer.length);
    this.#buffer = next;
  }

  #process(outputFrameLimit, newlyAvailableInputFrames) {
    const availableEnd =
      this.#bufferStartFrame + this.#buffer.length / this.#channelCount;
    const estimate = Math.max(
      1,
      this.maximumOutputFramesForInput(newlyAvailableInputFrames),
    );
    let output = new Float32Array(estimate * this.#channelCount);
    let writtenFrames = 0;

    while (this.#nextOutputFrame < outputFrameLimit) {
      const inputPosition =
        this.#nextOutputFrame * this.#inputFramesPerOutputFrame;
      const centerFrame = Math.floor(inputPosition);
      if (centerFrame + MAXIMUM_OFFSET >= availableEnd) {
        break;
      }
      if (writtenFrames === output.length / this.#channelCount) {
        const grown = new Float32Array(output.length * 2);
        grown.set(output);
        output = grown;
      }
      const fraction = inputPosition - centerFrame;
      const phase = Math.min(
        PHASE_COUNT,
        Math.max(0, Math.round(fraction * PHASE_COUNT)),
      );
      const coefficientOffset = phase * TAP_COUNT;
      const outputOffset = writtenFrames * this.#channelCount;
      for (let channel = 0; channel < this.#channelCount; ++channel) {
        let sum = 0;
        for (let tap = 0; tap < TAP_COUNT; ++tap) {
          const absoluteFrame = centerFrame + MINIMUM_OFFSET + tap;
          const localFrame = absoluteFrame - this.#bufferStartFrame;
          if (
            absoluteFrame >= 0 &&
            localFrame >= 0 &&
            localFrame < this.#buffer.length / this.#channelCount
          ) {
            sum +=
              this.#buffer[localFrame * this.#channelCount + channel] *
              this.#phaseTable[coefficientOffset + tap];
          }
        }
        output[outputOffset + channel] = sum;
      }
      ++writtenFrames;
      ++this.#nextOutputFrame;
    }

    const nextInputPosition =
      this.#nextOutputFrame * this.#inputFramesPerOutputFrame;
    const firstRequiredFrame = Math.max(
      0,
      Math.floor(nextInputPosition) + MINIMUM_OFFSET,
    );
    const discardFrames = Math.max(
      0,
      Math.min(
        firstRequiredFrame - this.#bufferStartFrame,
        this.#buffer.length / this.#channelCount,
      ),
    );
    if (discardFrames !== 0) {
      this.#buffer = this.#buffer.slice(discardFrames * this.#channelCount);
      this.#bufferStartFrame += discardFrames;
    }
    return output.slice(0, writtenFrames * this.#channelCount);
  }
}
