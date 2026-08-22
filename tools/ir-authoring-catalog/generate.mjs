#!/usr/bin/env node

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPOSITORY = path.resolve(HERE, "../..");
const CATALOG_PATH = path.join(
  REPOSITORY,
  "assets/builtin/ir-authoring-catalog.v1.json",
);
const PAYLOAD_ROOT = path.join(
  REPOSITORY,
  "assets/builtin/ir-library/payloads",
);
const REVIEWS_PATH = path.join(
  REPOSITORY,
  "assets/builtin/ir-library/perceptual-reviews.v1.json",
);

const SOURCE_REPOSITORY = "https://github.com/SvetlozarValchev/crankwave";
const SOURCE_TREE_COMMIT = "35dac075491addbdd7a58663a9d92c75480df4ce";
const SOURCE_ASSET_INTRODUCTION_COMMIT =
  "24718fa29b7ec29f733456d546b9f329e68f55ec";
const SOURCE_LICENSE_SHA256 =
  "9f64449d4ef2db6b57d6af9d36e5eca5b6de3ece2db14a847ae71d4e0dcbff15";
const RELEASE_IDENTITY = "1.2.0";
const SAMPLE_RATE_HZ = 44_100;
const LEGACY_MAXIMUM_INPUT_FRAMES = 33_705;
const LEGACY_MAXIMUM_KERNEL_COEFFICIENTS = 30_071;
const EXTENDED_MAXIMUM_INPUT_FRAMES = 131_072;
const EXTENDED_MAXIMUM_KERNEL_COEFFICIENTS = 570_654;
const OUTPUT_RATE_HZ = 192_000;
const SUPPORT_THRESHOLD_PCM16_MAGNITUDE = 100;
const ROUND_DIGITS = 9;

function fail(message) {
  throw new Error(message);
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function gitBlobSha1(bytes) {
  const prefix = Buffer.from(`blob ${bytes.byteLength}\0`, "ascii");
  return createHash("sha1").update(prefix).update(bytes).digest("hex");
}

function round(value, digits = ROUND_DIGITS) {
  if (!Number.isFinite(value)) fail("attempted to serialize a non-finite metric");
  return Number(value.toFixed(digits));
}

function roundOrNull(value, digits = ROUND_DIGITS) {
  return value === null ? null : round(value, digits);
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function codeUnitCompare(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

function sourceDefinitions() {
  const definitions = [];
  for (let ordinal = 1; ordinal <= 49; ordinal += 1) {
    const number = pad2(ordinal);
    definitions.push({
      id: `smooth-${number}`,
      display_name: `Smooth Library ${number}`,
      source_relative_path: `smooth/smooth_${number}.wav`,
    });
  }
  for (let ordinal = 1; ordinal <= 4; ordinal += 1) {
    const number = pad2(ordinal);
    definitions.push({
      id: `archive-engine-${number}`,
      display_name: `Archive Engine ${number}`,
      source_relative_path: `archive/engine_${number}.wav`,
    });
  }
  definitions.push({
    id: "archive-test-engine",
    display_name: "Archive Test Engine (original PCM24)",
    source_relative_path: "archive/test_engine.wav",
  });
  for (let ordinal = 1; ordinal <= 13; ordinal += 1) {
    const number = pad2(ordinal);
    definitions.push({
      id: `archive-test-engine-${number}`,
      display_name: `Archive Test Engine ${number}`,
      source_relative_path: `archive/test_engine_${number}_16.wav`,
    });
  }
  for (const definition of [
    {
      id: "archive-test-engine-14-eq-adjusted",
      display_name: "Archive Test Engine 14 (EQ adjusted)",
      source_relative_path: "archive/test_engine_14_eq_adjusted_16.wav",
    },
    {
      id: "archive-test-engine-15-eq-adjusted",
      display_name: "Archive Test Engine 15 (EQ adjusted)",
      source_relative_path: "archive/test_engine_15_eq_adjusted_16.wav",
    },
    {
      id: "archive-test-engine-16",
      display_name: "Archive Test Engine 16",
      source_relative_path: "archive/test_engine_16.wav",
    },
    {
      id: "archive-test-engine-16-eq-adjusted",
      display_name: "Archive Test Engine 16 (EQ adjusted)",
      source_relative_path: "archive/test_engine_16_eq_adjusted_16.wav",
    },
    {
      id: "archive-test-engine-17",
      display_name: "Archive Test Engine 17",
      source_relative_path: "archive/test_engine_17.wav",
    },
    {
      id: "archive-test-engine-18",
      display_name: "Archive Test Engine 18",
      source_relative_path: "archive/test_engine_18.wav",
    },
  ]) {
    definitions.push(definition);
  }
  definitions.sort((left, right) => codeUnitCompare(left.id, right.id));
  assert.equal(definitions.length, 73);
  assert.equal(new Set(definitions.map(({ id }) => id)).size, 73);
  assert.equal(
    new Set(definitions.map(({ source_relative_path }) => source_relative_path)).size,
    73,
  );
  return definitions;
}

function parseWave(bytes, label) {
  if (bytes.byteLength < 12 || bytes.toString("ascii", 0, 4) !== "RIFF" ||
      bytes.toString("ascii", 8, 12) !== "WAVE") {
    fail(`${label}: expected a RIFF/WAVE file`);
  }
  if (bytes.readUInt32LE(4) + 8 !== bytes.byteLength) {
    fail(`${label}: RIFF extent does not equal the file length`);
  }
  let format = null;
  let data = null;
  let offset = 12;
  while (offset < bytes.byteLength) {
    if (offset + 8 > bytes.byteLength) fail(`${label}: truncated RIFF chunk header`);
    const kind = bytes.toString("ascii", offset, offset + 4);
    const size = bytes.readUInt32LE(offset + 4);
    const begin = offset + 8;
    const end = begin + size;
    if (end > bytes.byteLength) fail(`${label}: truncated ${kind} chunk`);
    if (kind === "fmt ") {
      if (format !== null || size !== 16) fail(`${label}: unsupported fmt chunk`);
      format = {
        format_tag: bytes.readUInt16LE(begin),
        channel_count: bytes.readUInt16LE(begin + 2),
        sample_rate_hz: bytes.readUInt32LE(begin + 4),
        byte_rate: bytes.readUInt32LE(begin + 8),
        block_alignment: bytes.readUInt16LE(begin + 12),
        bits_per_sample: bytes.readUInt16LE(begin + 14),
      };
    } else if (kind === "data") {
      if (data !== null) fail(`${label}: duplicate data chunk`);
      data = bytes.subarray(begin, end);
    }
    offset = end + (size & 1);
  }
  if (offset !== bytes.byteLength || format === null || data === null) {
    fail(`${label}: incomplete WAVE structure`);
  }
  if (format.format_tag !== 1 || format.channel_count !== 1 ||
      format.sample_rate_hz !== SAMPLE_RATE_HZ ||
      ![16, 24].includes(format.bits_per_sample)) {
    fail(`${label}: expected mono 44.1 kHz PCM16 or PCM24`);
  }
  const bytesPerSample = format.bits_per_sample / 8;
  if (format.block_alignment !== bytesPerSample ||
      format.byte_rate !== SAMPLE_RATE_HZ * bytesPerSample ||
      data.byteLength % bytesPerSample !== 0) {
    fail(`${label}: inconsistent PCM media fields`);
  }
  const frameCount = data.byteLength / bytesPerSample;
  const samples = new Float64Array(frameCount);
  const integerMagnitudes = new Float64Array(frameCount);
  const divisor = 2 ** (format.bits_per_sample - 1);
  for (let index = 0; index < frameCount; index += 1) {
    const sampleOffset = index * bytesPerSample;
    let integer;
    if (bytesPerSample === 2) {
      integer = data.readInt16LE(sampleOffset);
    } else {
      integer = data.readUIntLE(sampleOffset, 3);
      if ((integer & 0x800000) !== 0) integer -= 0x1000000;
    }
    samples[index] = integer / divisor;
    integerMagnitudes[index] = Math.abs(integer);
  }
  return { format, frameCount, samples, integerMagnitudes };
}

function nextPowerOfTwo(value) {
  let result = 1;
  while (result < value) result *= 2;
  return result;
}

function fftPower(samples, supportFrameCount) {
  const size = nextPowerOfTwo(Math.max(2, supportFrameCount));
  const real = new Float64Array(size);
  const imaginary = new Float64Array(size);
  real.set(samples.subarray(0, supportFrameCount));
  for (let index = 1, reversed = 0; index < size; index += 1) {
    let bit = size >> 1;
    while ((reversed & bit) !== 0) {
      reversed ^= bit;
      bit >>= 1;
    }
    reversed ^= bit;
    if (index < reversed) {
      const realValue = real[index];
      real[index] = real[reversed];
      real[reversed] = realValue;
      const imaginaryValue = imaginary[index];
      imaginary[index] = imaginary[reversed];
      imaginary[reversed] = imaginaryValue;
    }
  }
  for (let length = 2; length <= size; length *= 2) {
    const angle = -2 * Math.PI / length;
    const stepReal = Math.cos(angle);
    const stepImaginary = Math.sin(angle);
    for (let begin = 0; begin < size; begin += length) {
      let twiddleReal = 1;
      let twiddleImaginary = 0;
      const half = length / 2;
      for (let index = 0; index < half; index += 1) {
        const even = begin + index;
        const odd = even + half;
        const oddReal = real[odd] * twiddleReal - imaginary[odd] * twiddleImaginary;
        const oddImaginary = real[odd] * twiddleImaginary + imaginary[odd] * twiddleReal;
        real[odd] = real[even] - oddReal;
        imaginary[odd] = imaginary[even] - oddImaginary;
        real[even] += oddReal;
        imaginary[even] += oddImaginary;
        const nextReal = twiddleReal * stepReal - twiddleImaginary * stepImaginary;
        twiddleImaginary = twiddleReal * stepImaginary + twiddleImaginary * stepReal;
        twiddleReal = nextReal;
      }
    }
  }
  const power = new Float64Array(size / 2 + 1);
  for (let index = 0; index < power.length; index += 1) {
    power[index] = real[index] ** 2 + imaginary[index] ** 2;
  }
  return { power, fftSize: size };
}

function spectralMetrics(samples, supportFrameCount) {
  const { power, fftSize } = fftPower(samples, supportFrameCount);
  const frequencyStep = SAMPLE_RATE_HZ / fftSize;
  const bands = [
    ["sub_20_120_hz", 20, 120],
    ["low_120_500_hz", 120, 500],
    ["mid_500_2000_hz", 500, 2_000],
    ["high_2000_8000_hz", 2_000, 8_000],
    ["air_8000_nyquist_hz", 8_000, SAMPLE_RATE_HZ / 2 + frequencyStep],
  ];
  const bandPower = Object.fromEntries(bands.map(([name]) => [name, 0]));
  let totalPower = 0;
  let weightedFrequency = 0;
  for (let index = 1; index < power.length; index += 1) {
    const frequency = index * frequencyStep;
    const value = power[index];
    if (frequency >= 20) {
      totalPower += value;
      weightedFrequency += value * frequency;
    }
    for (const [name, lower, upper] of bands) {
      if (frequency >= lower && frequency < upper) {
        bandPower[name] += value;
        break;
      }
    }
  }
  if (!(totalPower > 0)) fail("IR spectrum has no positive energy above 20 Hz");
  const bandEnergyFraction = {};
  for (const [name] of bands) {
    bandEnergyFraction[name] = round(bandPower[name] / totalPower);
  }

  // A fixed one-twelfth-octave aggregation avoids reporting individual FFT-bin
  // leakage as a physical resonance. Peaks remain measurements, not captions.
  const octaveBands = [];
  const ratio = 2 ** (1 / 12);
  for (let lower = 20; lower < SAMPLE_RATE_HZ / 2;) {
    const upper = Math.min(SAMPLE_RATE_HZ / 2, lower * ratio);
    const first = Math.max(1, Math.ceil(lower / frequencyStep));
    const last = Math.min(power.length - 1, Math.floor(upper / frequencyStep));
    let sum = 0;
    let count = 0;
    for (let index = first; index <= last; index += 1) {
      sum += power[index];
      count += 1;
    }
    if (count > 0) {
      octaveBands.push({ center: Math.sqrt(lower * upper), value: sum / count });
    }
    lower = upper;
  }
  const candidates = octaveBands.filter((band, index) =>
    index > 0 && index + 1 < octaveBands.length &&
    band.value > octaveBands[index - 1].value &&
    band.value >= octaveBands[index + 1].value
  );
  const maximumBandPower = Math.max(...octaveBands.map(({ value }) => value));
  const resonantPeaks = candidates
    .map(({ center, value }) => ({
      frequency_hz: round(center, 3),
      relative_level_db: round(10 * Math.log10(value / maximumBandPower), 3),
    }))
    .filter(({ relative_level_db }) => relative_level_db >= -30)
    .sort((left, right) =>
      right.relative_level_db - left.relative_level_db ||
      left.frequency_hz - right.frequency_hz
    )
    .slice(0, 5);
  if (resonantPeaks.length === 0) {
    const maximum = octaveBands.reduce((left, right) =>
      right.value > left.value ? right : left
    );
    resonantPeaks.push({
      frequency_hz: round(maximum.center, 3),
      relative_level_db: 0,
    });
  }
  return {
    fft_size: fftSize,
    spectral_centroid_hz: round(weightedFrequency / totalPower, 3),
    band_energy_fraction: bandEnergyFraction,
    resonant_peaks: resonantPeaks,
  };
}

function firstTailDecayFrame(samples, peakFrame, referenceEnergy, ratio) {
  let tailEnergy = 0;
  const tail = new Float64Array(samples.length);
  for (let index = samples.length - 1; index >= peakFrame; index -= 1) {
    tailEnergy += samples[index] ** 2;
    tail[index] = tailEnergy;
  }
  const threshold = referenceEnergy * ratio;
  for (let index = peakFrame; index < samples.length; index += 1) {
    if (tail[index] <= threshold) return index;
  }
  return null;
}

function objectiveMetrics(wave) {
  const thresholdScale = 2 ** (wave.format.bits_per_sample - 16);
  const threshold = SUPPORT_THRESHOLD_PCM16_MAGNITUDE * thresholdScale;
  let supportFrameCount = 0;
  let peakFrame = 0;
  let peakAmplitude = 0;
  let sumSquares = 0;
  for (let index = 0; index < wave.frameCount; index += 1) {
    const amplitude = Math.abs(wave.samples[index]);
    if (amplitude > peakAmplitude) {
      peakAmplitude = amplitude;
      peakFrame = index;
    }
    sumSquares += wave.samples[index] ** 2;
    if (wave.integerMagnitudes[index] > threshold) supportFrameCount = index + 1;
  }
  if (supportFrameCount === 0 || !(sumSquares > 0)) fail("IR has no meaningful support");
  let referenceEnergy = 0;
  for (let index = peakFrame; index < supportFrameCount; index += 1) {
    referenceEnergy += wave.samples[index] ** 2;
  }
  const supportSamples = wave.samples.subarray(0, supportFrameCount);
  const decay = {};
  for (const [name, ratio] of [
    ["minus_20_db_seconds", 1e-2],
    ["minus_40_db_seconds", 1e-4],
    ["minus_60_db_seconds", 1e-6],
  ]) {
    const frame = firstTailDecayFrame(supportSamples, peakFrame, referenceEnergy, ratio);
    decay[name] = roundOrNull(
      frame === null ? null : (frame - peakFrame) / SAMPLE_RATE_HZ,
    );
  }
  return {
    duration_seconds: round(wave.frameCount / SAMPLE_RATE_HZ),
    meaningful_support_frame_count: supportFrameCount,
    meaningful_support_seconds: round(supportFrameCount / SAMPLE_RATE_HZ),
    support_threshold_amplitude_01: round(
      SUPPORT_THRESHOLD_PCM16_MAGNITUDE / 32768,
    ),
    peak_frame: peakFrame,
    peak_amplitude_01: round(peakAmplitude),
    rms_amplitude_01: round(Math.sqrt(sumSquares / wave.frameCount)),
    root_energy: round(Math.sqrt(sumSquares)),
    schroeder_tail_decay_from_peak: decay,
    spectrum: spectralMetrics(wave.samples, supportFrameCount),
  };
}

function legacyConvertedCoefficientCount(supportFrameCount) {
  return Math.floor(supportFrameCount * OUTPUT_RATE_HZ / SAMPLE_RATE_HZ + 0.5);
}

function compatibility(wave, metrics) {
  const coefficients = legacyConvertedCoefficientCount(
    metrics.meaningful_support_frame_count,
  );
  const reasons = [];
  let state = "selectable";
  if (![16, 24].includes(wave.format.bits_per_sample)) {
    state = "requires-conversion";
    reasons.push("runtime-v2-requires-pcm16-or-pcm24");
  }
  if (wave.frameCount > EXTENDED_MAXIMUM_INPUT_FRAMES ||
      coefficients > EXTENDED_MAXIMUM_KERNEL_COEFFICIENTS) {
    if (state === "selectable") state = "requires-long-kernel";
    if (wave.frameCount > EXTENDED_MAXIMUM_INPUT_FRAMES) {
      reasons.push("runtime-v2-input-frame-capacity-exceeded");
    }
    if (coefficients > EXTENDED_MAXIMUM_KERNEL_COEFFICIENTS) {
      reasons.push("runtime-v2-compiled-kernel-capacity-exceeded");
    }
  }
  const legacyFixed = wave.format.bits_per_sample === 16 &&
    wave.frameCount <= LEGACY_MAXIMUM_INPUT_FRAMES &&
    coefficients <= LEGACY_MAXIMUM_KERNEL_COEFFICIENTS;
  return {
    state,
    assessed_runtime: {
      release_identity: RELEASE_IDENTITY,
      native_conversion_method:
        "hybrid-static-ir-pcm16-pcm24-44100-to-192000-binary64-v2",
      wasm32_conversion_method:
        "hybrid-static-ir-pcm16-pcm24-44100-to-192000-binary64-wasm32-binary128-v2",
      convolution_method:
        "hybrid-fixed-or-uniform-partitioned-causal-fft-binary64-v2",
      end_to_end_catalog_sweep: "passed-all-73",
    },
    decoder: {
      admitted: [16, 24].includes(wave.format.bits_per_sample) &&
        wave.frameCount <= EXTENDED_MAXIMUM_INPUT_FRAMES,
      branch: legacyFixed
        ? "legacy-pcm16-v1-bit-identical"
        : "extended-pcm16-pcm24-v2",
      maximum_input_frames: EXTENDED_MAXIMUM_INPUT_FRAMES,
    },
    compiled_kernel: {
      admitted: coefficients <= EXTENDED_MAXIMUM_KERNEL_COEFFICIENTS,
      branch: legacyFixed
        ? "legacy-fixed-v1-bit-identical"
        : "uniform-partitioned-v2",
      converted_coefficient_count: coefficients,
      maximum_coefficient_count: EXTENDED_MAXIMUM_KERNEL_COEFFICIENTS,
    },
    legacy_output_preservation: legacyFixed
      ? "v1-coefficient-and-fixed-spectrum-bytes-unchanged"
      : null,
    reasons,
  };
}

function percentile(sorted, fraction) {
  const index = (sorted.length - 1) * fraction;
  const lower = Math.floor(index);
  const upper = Math.ceil(index);
  if (lower === upper) return sorted[lower];
  return sorted[lower] * (upper - index) + sorted[upper] * (index - lower);
}

function thresholds(rawEntries) {
  const values = (getter) => rawEntries.map(getter).sort((a, b) => a - b);
  const centroid = values((entry) => entry.objective.spectrum.spectral_centroid_hz);
  const high = values((entry) =>
    entry.objective.spectrum.band_energy_fraction.high_2000_8000_hz +
    entry.objective.spectrum.band_energy_fraction.air_8000_nyquist_hz
  );
  const decay = values((entry) =>
    entry.objective.schroeder_tail_decay_from_peak.minus_20_db_seconds ??
    entry.objective.meaningful_support_seconds
  );
  return {
    centroid_terciles_hz: [round(percentile(centroid, 1 / 3), 3), round(percentile(centroid, 2 / 3), 3)],
    high_energy_quartiles: [round(percentile(high, 1 / 4)), round(percentile(high, 3 / 4))],
    decay_terciles_seconds: [round(percentile(decay, 1 / 3)), round(percentile(decay, 2 / 3))],
    root_energy_median: round(percentile(values((entry) => entry.objective.root_energy), 0.5)),
  };
}

function semanticProfile(entry, collectionThresholds, review) {
  const centroid = entry.objective.spectrum.spectral_centroid_hz;
  const highEnergy =
    entry.objective.spectrum.band_energy_fraction.high_2000_8000_hz +
    entry.objective.spectrum.band_energy_fraction.air_8000_nyquist_hz;
  const decay =
    entry.objective.schroeder_tail_decay_from_peak.minus_20_db_seconds ??
    entry.objective.meaningful_support_seconds;
  const tags = [];
  let spectralLabel = "mid-balanced";
  if (centroid <= collectionThresholds.centroid_terciles_hz[0]) {
    tags.push("dark");
    spectralLabel = "dark-weighted";
  } else if (centroid >= collectionThresholds.centroid_terciles_hz[1]) {
    tags.push("bright");
    spectralLabel = "bright-weighted";
  }
  if (highEnergy <= collectionThresholds.high_energy_quartiles[0]) {
    tags.push("smooth");
  } else if (highEnergy >= collectionThresholds.high_energy_quartiles[1]) {
    tags.push("sharp");
  }
  let decayLabel = "medium-decay";
  if (decay <= collectionThresholds.decay_terciles_seconds[0]) {
    tags.push("damped", "short-decay");
    decayLabel = "short-decay";
  } else if (decay >= collectionThresholds.decay_terciles_seconds[1]) {
    tags.push("reverberant", "long-decay");
    decayLabel = "long-decay";
  }
  if (spectralLabel === "bright-weighted" && decayLabel !== "short-decay") {
    tags.push("open");
  } else if (spectralLabel === "dark-weighted" &&
             highEnergy <= collectionThresholds.high_energy_quartiles[0]) {
    tags.push("muffled");
  }
  tags.sort();
  const strongest = entry.objective.spectrum.resonant_peaks[0];
  const measuredCaption =
    `${decayLabel[0].toUpperCase()}${decayLabel.slice(1)}, ${spectralLabel} response; ` +
    `${entry.objective.meaningful_support_seconds.toFixed(3)} s measured support and ` +
    `strongest measured one-twelfth-octave peak near ${Math.round(strongest.frequency_hz)} Hz.`;
  const energyRatio = collectionThresholds.root_energy_median / entry.objective.root_energy;
  const initialGain = Math.max(0.00025, Math.min(0.01, 0.001 * energyRatio));
  const reviewed = review ?? null;
  return {
    caption: {
      text: reviewed?.caption ?? measuredCaption,
      basis: reviewed === null
        ? "objective-measurement-summary-v1"
        : "human-level-matched-audition-v1",
      review_status: reviewed === null
        ? "pending-human-audition"
        : "human-reviewed",
    },
    perceptual_tags: {
      objective_proxies: tags,
      curated: reviewed?.tags ?? [],
      status: reviewed === null
        ? "measurement-proxies-only-pending-human-audition"
        : "human-reviewed",
    },
    recommended_controls: {
      impulse_response_gain_linear: {
        minimum: round(Math.max(0.0001, initialGain / 4), 7),
        initial: round(initialGain, 7),
        maximum: round(Math.min(0.04, initialGain * 4), 7),
        basis: "catalog-relative-root-energy-level-match-seed-v1",
      },
      wet_mix_01: {
        minimum: 0.25,
        initial: 0.75,
        maximum: 1,
        basis: "fixed-authoring-search-range-v1",
      },
      qualification: "audition-starting-range-not-a-sound-quality-guarantee",
    },
  };
}

function loadReviews() {
  const document = JSON.parse(fs.readFileSync(REVIEWS_PATH, "utf8"));
  assert.equal(document.schema, "crankwave/ir-perceptual-reviews.v1");
  assert.equal(document.review_method, "level-matched-fixed-dry-engine-probe-v1");
  assert(Array.isArray(document.reviews));
  const reviews = new Map();
  for (const review of document.reviews) {
    assert.deepEqual(Object.keys(review).sort(), ["caption", "id", "reviewer", "sha256", "tags"].sort());
    assert.match(review.id, /^[a-z0-9][a-z0-9-]{0,127}$/u);
    assert.match(review.sha256, /^[0-9a-f]{64}$/u);
    assert.equal(typeof review.caption, "string");
    assert(review.caption.length >= 12 && review.caption.length <= 240);
    assert(Array.isArray(review.tags) && review.tags.every((tag) => typeof tag === "string"));
    assert.equal(typeof review.reviewer, "string");
    if (reviews.has(review.id)) fail(`duplicate perceptual review for ${review.id}`);
    reviews.set(review.id, review);
  }
  return reviews;
}

function buildCatalog({ sourceRoot = null, importPayloads = false }) {
  if (importPayloads && sourceRoot === null) fail("--import requires --source-root");
  if (importPayloads) fs.mkdirSync(PAYLOAD_ROOT, { recursive: true });
  const definitions = sourceDefinitions();
  const reviews = loadReviews();
  const rawEntries = definitions.map((definition) => {
    const sourcePath = sourceRoot === null
      ? null
      : path.join(sourceRoot, definition.source_relative_path);
    let bytes;
    if (sourcePath !== null) {
      bytes = fs.readFileSync(sourcePath);
    } else {
      const existing = JSON.parse(fs.readFileSync(CATALOG_PATH, "utf8"));
      const catalogEntry = existing.entries.find(({ id }) => id === definition.id);
      if (catalogEntry === undefined) fail(`catalog is missing ${definition.id}`);
      bytes = fs.readFileSync(path.join(PAYLOAD_ROOT, `${catalogEntry.sha256}.wav`));
    }
    const contentSha256 = sha256(bytes);
    const payloadPath = path.join(PAYLOAD_ROOT, `${contentSha256}.wav`);
    if (importPayloads) {
      if (fs.existsSync(payloadPath)) {
        if (!fs.readFileSync(payloadPath).equals(bytes)) {
          fail(`content-addressed payload mismatch for ${definition.id}`);
        }
      } else {
        fs.copyFileSync(sourcePath, payloadPath, fs.constants.COPYFILE_EXCL);
      }
    }
    const wave = parseWave(bytes, definition.source_relative_path);
    const objective = objectiveMetrics(wave);
    return {
      id: definition.id,
      display_name: definition.display_name,
      sha256: contentSha256,
      payload: {
        repository_relative_path:
          `assets/builtin/ir-library/payloads/${contentSha256}.wav`,
        installed_bundle_relative_path: `payloads/${contentSha256}`,
        byte_count: bytes.byteLength,
      },
      source: {
        repository: SOURCE_REPOSITORY,
        repository_tree_commit: SOURCE_TREE_COMMIT,
        repository_relative_path: `assets/sound-library/${definition.source_relative_path}`,
        source_file_sha256: contentSha256,
        source_git_blob_sha1: gitBlobSha1(bytes),
        import: "exact-byte-copy",
      },
      media: {
        container: "riff-wave",
        encoding: `pcm-s${wave.format.bits_per_sample}-le`,
        sample_rate_hz: wave.format.sample_rate_hz,
        channel_count: wave.format.channel_count,
        bits_per_sample: wave.format.bits_per_sample,
        frame_count: wave.frameCount,
      },
      objective,
      compatibility: compatibility(wave, objective),
      rights: {
        status: "MIT",
        provenance_status: "source-path-commit-and-content-hash-recorded",
        repository_license_observed: "MIT",
        repository_license_path: "LICENSE",
        repository_license_sha256: SOURCE_LICENSE_SHA256,
        asset_specific_rights_evidence: null,
        redistribution_status: "permitted-with-license-notice",
      },
    };
  });
  const collectionThresholds = thresholds(rawEntries);
  const entries = rawEntries.map((entry) => {
    const review = reviews.get(entry.id) ?? null;
    if (review !== null && review.sha256 !== entry.sha256) {
      fail(`stale perceptual review hash for ${entry.id}`);
    }
    return {
      ...entry,
      authoring: semanticProfile(entry, collectionThresholds, review),
    };
  });
  for (const reviewedId of reviews.keys()) {
    if (!entries.some(({ id }) => id === reviewedId)) fail(`review references unknown IR ${reviewedId}`);
  }
  const states = Object.fromEntries(
    ["selectable", "requires-long-kernel", "requires-conversion"].map((state) => [
      state,
      entries.filter((entry) => entry.compatibility.state === state).length,
    ]),
  );
  return {
    schema: "crankwave/ir-authoring-catalog.v1",
    catalog_id: "crankwave-built-in-ir-library",
    catalog_revision: 1,
    release_identity: RELEASE_IDENTITY,
    entry_count: entries.length,
    selection_contract: {
      required_selector: ["release_identity", "id", "sha256"],
      asset_kind: "impulse_response",
      engine_json_declaration_rule: "declare-only-assets-used-by-presentation-routes",
      arbitrary_path_selection: false,
    },
    source_collection: {
      repository: SOURCE_REPOSITORY,
      repository_tree_commit: SOURCE_TREE_COMMIT,
      asset_introduction_commit: SOURCE_ASSET_INTRODUCTION_COMMIT,
      repository_relative_root: "assets/sound-library",
      import: "exact-byte-copy-content-addressed-by-sha256",
    },
    measurement_method: {
      id: "crankwave-ir-objective-analysis-v1",
      decoded_domain: "mono-normalized-pcm-without-level-normalization",
      meaningful_support: "last-frame-strictly-above-100-over-32768-amplitude",
      decay: "reverse-integrated-squared-amplitude-from-absolute-peak",
      spectrum: "raw-meaningful-support-zero-padded-radix2-dft-power",
      resonant_peaks: "local-maxima-of-one-twelfth-octave-mean-power",
      numeric_encoding: "json-number-rounded-to-at-most-nine-decimal-places",
      collection_thresholds: collectionThresholds,
    },
    semantic_method: {
      id: "crankwave-ir-measurement-proxy-semantics-v1",
      objective_proxy_warning: "bright-dark-sharp-smooth-open-muffled-decay-tags-are-relative-measurement-proxies-not-listening-claims",
      curated_review_method: "level-matched-fixed-dry-engine-probe-v1",
      curated_review_status: reviews.size === entries.length
        ? "complete"
        : "incomplete-human-audition-required",
      reviewed_entry_count: reviews.size,
    },
    compatibility_summary: states,
    rights_summary: {
      status: "MIT",
      repository_license_observed: "MIT",
      repository_license_path: "LICENSE",
      repository_license_sha256: SOURCE_LICENSE_SHA256,
      asset_specific_rights_evidence: null,
      release_gate: "none",
    },
    entries,
  };
}

function usage() {
  return [
    "usage:",
    "  node tools/ir-authoring-catalog/generate.mjs --check",
    "  node tools/ir-authoring-catalog/generate.mjs --import --source-root DIR",
  ].join("\n");
}

function main(argv) {
  let mode = null;
  let sourceRoot = null;
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--check" || argument === "--import") {
      if (mode !== null) fail("choose exactly one of --check or --import");
      mode = argument.slice(2);
    } else if (argument === "--source-root") {
      sourceRoot = path.resolve(argv[++index] ?? fail("--source-root requires a value"));
    } else if (argument === "--help") {
      process.stdout.write(`${usage()}\n`);
      return;
    } else {
      fail(`unknown argument: ${argument}`);
    }
  }
  if (mode === null) fail(usage());
  const catalog = buildCatalog({
    sourceRoot,
    importPayloads: mode === "import",
  });
  const canonical = `${JSON.stringify(catalog, null, 2)}\n`;
  if (mode === "import") {
    fs.writeFileSync(CATALOG_PATH, canonical, { encoding: "utf8", flag: "w" });
    process.stdout.write(`imported ${catalog.entry_count} exact IR payloads and wrote ${CATALOG_PATH}\n`);
  } else {
    const existing = fs.readFileSync(CATALOG_PATH, "utf8");
    if (existing !== canonical) fail("tracked IR authoring catalog is stale; regenerate it with --import");
    const payloadNames = fs.readdirSync(PAYLOAD_ROOT).sort();
    const expectedNames = catalog.entries.map(({ sha256: digest }) => `${digest}.wav`).sort();
    assert.deepEqual(payloadNames, expectedNames, "payload tree must contain exactly the catalogued hashes");
    process.stdout.write(`verified ${catalog.entry_count} exact IR payloads and deterministic catalog bytes\n`);
  }
}

try {
  main(process.argv.slice(2));
} catch (error) {
  process.stderr.write(`ir-authoring-catalog: ${error.message}\n`);
  process.exitCode = 1;
}
