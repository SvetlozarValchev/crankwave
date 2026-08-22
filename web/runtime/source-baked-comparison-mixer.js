import { CRANKWAVE_CANONICAL_SAMPLE_RATE } from "./c-api-abi.js";

export const SourceBakedComparisonMode = Object.freeze({
  source: "source-a",
  baked: "baked-b",
});

function comparisonMode(value) {
  if (!Object.values(SourceBakedComparisonMode).includes(value)) {
    throw new RangeError(
      "comparison mode must be source-a or baked-b",
    );
  }
  return value;
}

function validateMonoBlock(block, name) {
  if (!(block instanceof Float32Array)) {
    throw new TypeError(`${name} must be a mono Float32Array`);
  }
  let peak = 0;
  let squareSum = 0;
  let clipSampleCount = 0;
  for (let frame = 0; frame < block.length; ++frame) {
    const sample = block[frame];
    if (!Number.isFinite(sample)) {
      throw new RangeError(`${name} contains a non-finite sample at frame ${frame}`);
    }
    const magnitude = Math.abs(sample);
    peak = Math.max(peak, magnitude);
    squareSum += sample * sample;
    if (magnitude > 1) {
      ++clipSampleCount;
    }
  }
  return { peak, squareSum, clipSampleCount };
}

function rms(squareSum, sampleCount) {
  return sampleCount === 0 ? 0 : Math.sqrt(squareSum / sampleCount);
}

function levelDeltaDb(sourceRms, bakedRms) {
  if (sourceRms === 0 && bakedRms === 0) {
    return 0;
  }
  if (sourceRms === 0 || bakedRms === 0) {
    return null;
  }
  return 20 * Math.log10(bakedRms / sourceRms);
}

function publicSignalStatistics(statistics, sampleCount) {
  return Object.freeze({
    finite: true,
    sampleCount,
    peak: statistics.peak,
    rms: rms(statistics.squareSum, sampleCount),
    clipSampleCount: statistics.clipSampleCount,
  });
}

function emptyAggregate() {
  return { peak: 0, squareSum: 0, clipSampleCount: 0 };
}

// This is deliberately only a gain router on the canonical clock. Both inputs
// are validated and metered on every call, independent of the audible mode.
// Device-rate conversion and transport remain downstream shared concerns.
export class SourceBakedComparisonMixer {
  #mode;
  #blockCount = 0;
  #frameCount = 0;
  #source = emptyAggregate();
  #baked = emptyAggregate();
  #lastBlock = null;

  constructor({ mode = SourceBakedComparisonMode.source } = {}) {
    this.#mode = comparisonMode(mode);
  }

  get sampleRate() {
    return CRANKWAVE_CANONICAL_SAMPLE_RATE;
  }

  get channelCount() {
    return 2;
  }

  get mode() {
    return this.#mode;
  }

  set mode(value) {
    this.#mode = comparisonMode(value);
  }

  process(sourceBlock, bakedBlock) {
    const source = validateMonoBlock(sourceBlock, "sourceBlock");
    const baked = validateMonoBlock(bakedBlock, "bakedBlock");
    if (sourceBlock.length !== bakedBlock.length) {
      throw new RangeError(
        "sourceBlock and bakedBlock must contain the same number of frames",
      );
    }
    if (this.#frameCount > Number.MAX_SAFE_INTEGER - sourceBlock.length) {
      throw new RangeError("comparison diagnostic frame count overflowed");
    }

    const output = new Float32Array(sourceBlock.length * 2);
    for (let frame = 0; frame < sourceBlock.length; ++frame) {
      const sourceSample = sourceBlock[frame];
      const bakedSample = bakedBlock[frame];
      const outputOffset = frame * 2;
      if (this.#mode === SourceBakedComparisonMode.source) {
        output[outputOffset] = sourceSample;
        output[outputOffset + 1] = sourceSample;
      } else {
        output[outputOffset] = bakedSample;
        output[outputOffset + 1] = bakedSample;
      }
    }

    const firstFrame = this.#frameCount;
    ++this.#blockCount;
    this.#frameCount += sourceBlock.length;
    this.#source.peak = Math.max(this.#source.peak, source.peak);
    this.#source.squareSum += source.squareSum;
    this.#source.clipSampleCount += source.clipSampleCount;
    this.#baked.peak = Math.max(this.#baked.peak, baked.peak);
    this.#baked.squareSum += baked.squareSum;
    this.#baked.clipSampleCount += baked.clipSampleCount;

    const sourcePublic = publicSignalStatistics(source, sourceBlock.length);
    const bakedPublic = publicSignalStatistics(baked, bakedBlock.length);
    this.#lastBlock = Object.freeze({
      ordinal: this.#blockCount,
      firstFrame,
      frameCount: sourceBlock.length,
      mode: this.#mode,
      source: sourcePublic,
      baked: bakedPublic,
      bakedMinusSourceRmsDb: levelDeltaDb(
        sourcePublic.rms,
        bakedPublic.rms,
      ),
    });
    return output;
  }

  diagnostics() {
    const source = publicSignalStatistics(this.#source, this.#frameCount);
    const baked = publicSignalStatistics(this.#baked, this.#frameCount);
    return Object.freeze({
      sampleRate: CRANKWAVE_CANONICAL_SAMPLE_RATE,
      channelCount: 2,
      mode: this.#mode,
      lastBlock: this.#lastBlock,
      cumulative: Object.freeze({
        blockCount: this.#blockCount,
        frameCount: this.#frameCount,
        source,
        baked,
        bakedMinusSourceRmsDb: levelDeltaDb(source.rms, baked.rms),
      }),
    });
  }

  resetDiagnostics() {
    this.#blockCount = 0;
    this.#frameCount = 0;
    this.#source = emptyAggregate();
    this.#baked = emptyAggregate();
    this.#lastBlock = null;
  }
}
