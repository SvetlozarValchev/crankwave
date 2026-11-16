import {
  loadDirectionalPhaseCell,
} from "./directional-phase-cell.js";
import { isSupportedAudioDeliverySampleRate } from "./audio-delivery-profile.js";

const RUNTIME_SCHEMA =
  "crankwave/responsive-audio-directional-texture";
const FFT_SIZE = 65_536;
const IR_COEFFICIENT_COUNT = 30_071;
const IR_HISTORY_FRAMES = IR_COEFFICIENT_COUNT - 1;
const FLOAT32_BELOW_ONE = bitCastUint32ToFloat32(0x3f7fffff);
const PEAK_RETENTION_PER_FRAME = bitCastUint32ToFloat32(0x3f7ffef2);
const GAIN_RETENTION_PER_FRAME = bitCastUint32ToFloat32(0x3f7fe1df);
const GAIN_BLEND_PER_FRAME = bitCastUint32ToFloat32(0x39f10800);

function fail(message) {
  throw new TypeError(message);
}

function object(value, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  return value;
}

function array(value, label) {
  if (!Array.isArray(value)) fail(`${label} must be an array`);
  return value;
}

function string(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    fail(`${label} must be a nonempty string`);
  }
  return value;
}

function finite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail(`${label} must be finite`);
  }
  return value;
}

function positive(value, label) {
  const result = finite(value, label);
  if (!(result > 0)) fail(`${label} must be positive`);
  return result;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function bitCastUint32ToFloat32(value) {
  const bytes = new ArrayBuffer(4);
  new DataView(bytes).setUint32(0, value, true);
  return new DataView(bytes).getFloat32(0, true);
}

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("dry directional runtime manifest URL must be nonempty");
  }
  return new URL(value, globalThis.location?.href ?? "http://localhost/");
}

function relativeUrl(manifestUrl, path, label) {
  const relativePath = string(path, label);
  if (
    relativePath.startsWith("/") ||
    relativePath.includes("..") ||
    relativePath.includes("\\")
  ) {
    fail(`${label} must stay beneath the runtime manifest`);
  }
  return new URL(relativePath, manifestUrl);
}

async function fetchBytes(url, fetchImplementation, label) {
  const response = await fetchImplementation(url.href, { cache: "no-store" });
  if (!response?.ok || typeof response.arrayBuffer !== "function") {
    throw new Error(
      `${label} fetch failed: HTTP ${response?.status ?? "?"} for ${url.href}`,
    );
  }
  return new Uint8Array(await response.arrayBuffer());
}

function sameIdentity(left, right) {
  return left?.sha256 === right?.sha256;
}

function requireChildIdentity(child, manifest, label) {
  if (child.sampleRate !== manifest.audio.sample_rate_hz) {
    fail(`${label} sample rate does not match the runtime`);
  }
  if (child.manifest.engine !== manifest.engine) {
    fail(`${label} engine does not match the runtime`);
  }
  if (!sameIdentity(child.manifest.provenance.engine, manifest.provenance.engine)) {
    fail(`${label} engine provenance does not match the runtime`);
  }
  if (
    !sameIdentity(
      child.manifest.provenance.renderer_build,
      manifest.provenance.renderer_build,
    )
  ) {
    fail(`${label} renderer provenance does not match the runtime`);
  }
}

// Loads only independent dry phase units. No authored RPM timeline or
// presentation transfer is present in this package: the outer responsive
// runtime supplies consumer state and owns all transfer/master processing.
// live crank/load endpoint used by the cursor below.
export async function loadDryDirectionalPhaseRuntime(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("dry directional runtime loading requires fetch");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifestBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "runtime.json",
  );
  const manifest = JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes),
  );
  object(manifest, "manifest");
  if (manifest.schema !== RUNTIME_SCHEMA) {
    fail(`unsupported dry directional runtime schema ${manifest.schema}`);
  }
  for (const forbidden of [
    "transfer",
    "master",
    "master_volume_linear",
    "captured_to_source_scale",
  ]) {
    if (Object.hasOwn(manifest, forbidden)) {
      fail(`dry directional texture must not contain presentation field ${forbidden}`);
    }
  }
  string(manifest.id, "manifest.id");
  string(manifest.engine, "manifest.engine");

  object(manifest.audio, "manifest.audio");
  const sampleRate = positiveInteger(
    manifest.audio.sample_rate_hz,
    "audio.sample_rate_hz",
  );
  if (!isSupportedAudioDeliverySampleRate(sampleRate)) {
    fail("dry directional runtime requires a supported delivery rate");
  }
  if (
    manifest.audio.encoding !== "float32le" ||
    manifest.audio.channel_layout !== "mono"
  ) {
    fail("dry directional runtime output must be mono float32le");
  }
  if (Object.hasOwn(manifest.audio, "bus_id")) {
    fail("directional texture root must not claim a presentation bus");
  }

  const dryBusIds = array(manifest.dry_bus_ids, "manifest.dry_bus_ids").map(
    (value, index) => string(value, `dry_bus_ids[${index}]`),
  );
  const routeDescriptors = array(
    manifest.route_manifests,
    "manifest.route_manifests",
  ).map((value, index) => {
    const descriptor = object(value, `route_manifests[${index}]`);
    return Object.freeze({
      busId: string(descriptor.bus_id, `route_manifests[${index}].bus_id`),
      risingUrl: relativeUrl(
        manifestUrl,
        descriptor.rising_manifest_path,
        `route_manifests[${index}].rising_manifest_path`,
      ),
      fallingUrl: relativeUrl(
        manifestUrl,
        descriptor.falling_manifest_path,
        `route_manifests[${index}].falling_manifest_path`,
      ),
    });
  });
  if (
    dryBusIds.length === 0 ||
    dryBusIds.length !== routeDescriptors.length ||
    new Set(dryBusIds).size !== dryBusIds.length ||
    dryBusIds.some((busId, index) => busId !== routeDescriptors[index].busId)
  ) {
    fail("directional dry buses and route manifests disagree");
  }

  object(manifest.domain, "manifest.domain");
  const minimumRpm = positive(manifest.domain.minimum_rpm, "domain.minimum_rpm");
  const maximumRpm = positive(manifest.domain.maximum_rpm, "domain.maximum_rpm");
  if (!(maximumRpm > minimumRpm)) fail("runtime RPM domain is invalid");
  object(manifest.provenance, "manifest.provenance");
  object(manifest.provenance.engine, "provenance.engine");
  object(manifest.provenance.renderer_build, "provenance.renderer_build");

  const childOptions = {
    fetch: fetchImplementation,
    crypto: cryptoImplementation,
  };
  const loadedPairs = await Promise.all(
    routeDescriptors.map(async (descriptor) => {
      const [rising, falling] = await Promise.all([
        loadDirectionalPhaseCell(descriptor.risingUrl, childOptions),
        loadDirectionalPhaseCell(descriptor.fallingUrl, childOptions),
      ]);
      return Object.freeze({ busId: descriptor.busId, rising, falling });
    }),
  );
  const children = loadedPairs.flatMap((pair) => [pair.rising, pair.falling]);
  for (const [pairIndex, pair] of loadedPairs.entries()) {
    for (const direction of ["rising", "falling"]) {
      const child = pair[direction];
      const label = `routePairs[${pairIndex}].${direction}`;
      requireChildIdentity(child, manifest, label);
      if (child.busIds.length !== 1 || child.busIds[0] !== pair.busId) {
        fail(`${label} bus ID does not match its ordered route descriptor`);
      }
      if (
        child.minimumRpm !== minimumRpm ||
        child.maximumRpm !== maximumRpm
      ) {
        fail(`${label} RPM domain does not match the directional root`);
      }
    }
  }
  const childMinimumRpm = Math.max(
    minimumRpm,
    ...children.map((child) => child.minimumRpm),
  );
  const childMaximumRpm = Math.min(
    maximumRpm,
    ...children.map((child) => child.maximumRpm),
  );
  if (!(childMaximumRpm > childMinimumRpm)) {
    fail("dry route phase manifests have no common RPM domain");
  }

  return Object.freeze({
    kind: "dry-directional-phase-runtime",
    manifestUrl: manifestUrl.href,
    manifest,
    sampleRate,
    minimumRpm: childMinimumRpm,
    maximumRpm: childMaximumRpm,
    routePairs: Object.freeze(loadedPairs),
  });
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

class FixedFftPlan {
  #fftSize;
  #bitCount;
  #reversed;
  #forwardRootReal;
  #forwardRootImaginary;

  constructor(fftSize = FFT_SIZE) {
    if (!Number.isSafeInteger(fftSize) || fftSize < 2 || (fftSize & (fftSize - 1)) !== 0) {
      throw new RangeError("fixed FFT size must be a positive power of two");
    }
    this.#fftSize = fftSize;
    this.#bitCount = Math.log2(fftSize);
    this.#reversed = new Uint32Array(fftSize);
    for (let index = 0; index < fftSize; ++index) {
      let remaining = index;
      let reversed = 0;
      for (let bit = 0; bit < this.#bitCount; ++bit) {
        reversed = (reversed << 1) | (remaining & 1);
        remaining >>>= 1;
      }
      this.#reversed[index] = reversed;
    }
    this.#forwardRootReal = new Float64Array(fftSize / 2);
    this.#forwardRootImaginary = new Float64Array(fftSize / 2);
    for (let index = 0; index < fftSize / 2; ++index) {
      const angle = (-2 * Math.PI * index) / fftSize;
      this.#forwardRootReal[index] = Math.cos(angle);
      this.#forwardRootImaginary[index] = Math.sin(angle);
    }
  }

  forward(real, imaginary) {
    this.#transform(real, imaginary, false);
  }

  inverse(real, imaginary) {
    this.#transform(real, imaginary, true);
  }

  #transform(real, imaginary, inverse) {
    if (
      !(real instanceof Float64Array) ||
      !(imaginary instanceof Float64Array) ||
      real.length !== this.#fftSize ||
      imaginary.length !== this.#fftSize
    ) {
      throw new RangeError(`fixed FFT requires two ${this.#fftSize}-element Float64 arrays`);
    }
    for (let index = 0; index < this.#fftSize; ++index) {
      const reversed = this.#reversed[index];
      if (index < reversed) {
        const realSwap = real[index];
        real[index] = real[reversed];
        real[reversed] = realSwap;
        const imaginarySwap = imaginary[index];
        imaginary[index] = imaginary[reversed];
        imaginary[reversed] = imaginarySwap;
      }
    }
    for (let width = 2; width <= this.#fftSize; width *= 2) {
      const halfWidth = width / 2;
      const rootStep = this.#fftSize / width;
      for (let base = 0; base < this.#fftSize; base += width) {
        for (let offset = 0; offset < halfWidth; ++offset) {
          const rootIndex = offset * rootStep;
          const rootReal = this.#forwardRootReal[rootIndex];
          const rootImaginary = inverse
            ? -this.#forwardRootImaginary[rootIndex]
            : this.#forwardRootImaginary[rootIndex];
          const oddIndex = base + offset + halfWidth;
          const evenIndex = base + offset;
          const oddReal =
            real[oddIndex] * rootReal - imaginary[oddIndex] * rootImaginary;
          const oddImaginary =
            real[oddIndex] * rootImaginary + imaginary[oddIndex] * rootReal;
          const evenReal = real[evenIndex];
          const evenImaginary = imaginary[evenIndex];
          real[evenIndex] = evenReal + oddReal;
          imaginary[evenIndex] = evenImaginary + oddImaginary;
          real[oddIndex] = evenReal - oddReal;
          imaginary[oddIndex] = evenImaginary - oddImaginary;
        }
      }
    }
    if (inverse) {
      const scale = 1 / this.#fftSize;
      for (let index = 0; index < this.#fftSize; ++index) {
        real[index] *= scale;
        imaginary[index] *= scale;
      }
    }
  }
}

export class FixedSpectrumConvolver {
  #kernelReal;
  #kernelImaginary;
  #maximumBlockFrames;
  #fftSize;
  #historyFrames;
  #plan;
  #history;
  #real;
  #imaginary;

  constructor(spectrum, maximumBlockFrames) {
    if (!spectrum || spectrum.real?.length !== spectrum.imaginary?.length) {
      throw new TypeError("fixed convolution spectrum is invalid");
    }
    this.#fftSize = spectrum.fftSize ?? FFT_SIZE;
    const coefficientCount = spectrum.coefficientCount ?? IR_COEFFICIENT_COUNT;
    if (!Number.isSafeInteger(coefficientCount) || coefficientCount <= 0 || coefficientCount > this.#fftSize) {
      throw new RangeError("fixed convolution coefficient count is invalid");
    }
    if (spectrum.real.length !== this.#fftSize) {
      throw new RangeError("fixed convolution spectrum length does not match its FFT size");
    }
    this.#historyFrames = coefficientCount - 1;
    if (!Number.isSafeInteger(maximumBlockFrames) || maximumBlockFrames <= 0 ||
        maximumBlockFrames > this.#fftSize - this.#historyFrames) {
      throw new RangeError("fixed convolution maximum block does not fit its FFT");
    }
    this.#kernelReal = spectrum.real;
    this.#kernelImaginary = spectrum.imaginary;
    this.#maximumBlockFrames = maximumBlockFrames;
    this.#plan = new FixedFftPlan(this.#fftSize);
    this.#history = new Float64Array(this.#historyFrames);
    this.#real = new Float64Array(this.#fftSize);
    this.#imaginary = new Float64Array(this.#fftSize);
  }

  reset() {
    this.#history.fill(0);
  }

  process(input) {
    if (!(input instanceof Float64Array)) {
      throw new TypeError("fixed convolution input must be Float64Array");
    }
    if (input.length === 0 || input.length > this.#maximumBlockFrames) {
      throw new RangeError("fixed convolution block is empty or too large");
    }
    this.#real.fill(0);
    this.#imaginary.fill(0);
    this.#real.set(this.#history, 0);
    this.#real.set(input, this.#historyFrames);
    this.#plan.forward(this.#real, this.#imaginary);
    for (let index = 0; index < this.#fftSize; ++index) {
      const real = this.#real[index];
      const imaginary = this.#imaginary[index];
      const kernelReal = this.#kernelReal[index];
      const kernelImaginary = this.#kernelImaginary[index];
      this.#real[index] = real * kernelReal - imaginary * kernelImaginary;
      this.#imaginary[index] = real * kernelImaginary + imaginary * kernelReal;
    }
    this.#plan.inverse(this.#real, this.#imaginary);
    const output = new Float64Array(input.length);
    for (let frame = 0; frame < input.length; ++frame) {
      const sample = this.#real[this.#historyFrames + frame];
      if (!Number.isFinite(sample)) {
        throw new RangeError(`fixed convolution produced a non-finite frame ${frame}`);
      }
      output[frame] = sample;
    }
    if (this.#historyFrames === 0) {
      // No overlap history is needed for a one-tap kernel.
    } else if (input.length >= this.#historyFrames) {
      this.#history.set(input.subarray(input.length - this.#historyFrames));
    } else {
      this.#history.copyWithin(0, input.length);
      this.#history.set(input, this.#historyFrames - input.length);
    }
    return output;
  }
}

// Prepare smaller convolution jobs once at load time. Reconstruct every FIR
// coefficient from the verified fixed spectrum; do not shorten the filter tail.
export function partitionFixedSpectrum(spectrum, blockFrames) {
  const size = spectrum?.fftSize;
  const count = spectrum?.coefficientCount;
  if (!Number.isSafeInteger(blockFrames) || blockFrames < 1 ||
      (blockFrames & (blockFrames - 1)) !== 0 || blockFrames > FFT_SIZE / 2 ||
      !Number.isSafeInteger(size) || size < 2 || size > FFT_SIZE ||
      (size & (size - 1)) !== 0 || !Number.isSafeInteger(count) || count < 1 || count > size ||
      !(spectrum.real instanceof Float64Array) || !(spectrum.imaginary instanceof Float64Array) ||
      spectrum.real.length !== size || spectrum.imaginary.length !== size) {
    throw new RangeError("cannot partition an invalid fixed spectrum or block size");
  }
  const impulse = spectrum.real.slice();
  const imaginary = spectrum.imaginary.slice();
  new FixedFftPlan(size).inverse(impulse, imaginary);
  const fftSize = blockFrames * 2;
  const partitionCount = Math.ceil(count / blockFrames);
  const real = new Float64Array(partitionCount * fftSize);
  const imag = new Float64Array(real.length);
  const plan = new FixedFftPlan(fftSize);
  for (let partition = 0; partition < partitionCount; ++partition) {
    const offset = partition * fftSize;
    const outputReal = real.subarray(offset, offset + fftSize);
    const outputImaginary = imag.subarray(offset, offset + fftSize);
    outputReal.set(impulse.subarray(partition * blockFrames,
      Math.min(count, (partition + 1) * blockFrames)));
    plan.forward(outputReal, outputImaginary);
  }
  return Object.freeze({ fftSize, coefficientCount: count, partitionFrameCount: blockFrames,
    partitionCount, real, imaginary: imag });
}

// Complete uniform-partitioned convolution. A process call contains whole
// partitions, with their exact frame count declared by the prepared spectrum.
export class PartitionedSpectrumConvolver {
  #kernelReal;
  #kernelImaginary;
  #partitionCount;
  #plan;
  #fftSize;
  #blockFrames;
  #inputSpectraReal;
  #inputSpectraImaginary;
  #inputReal;
  #inputImaginary;
  #outputReal;
  #outputImaginary;
  #overlap;
  #nextOverlap;
  #processedBlockCount = 0;

  constructor(spectrum) {
    const fftSize = spectrum?.fftSize;
    const blockFrames = spectrum?.partitionFrameCount;
    if (
      !Number.isSafeInteger(fftSize) || fftSize < 2 || fftSize > FFT_SIZE ||
      (fftSize & (fftSize - 1)) !== 0 ||
      !Number.isSafeInteger(blockFrames) || blockFrames <= 0 ||
      2 * blockFrames - 1 > fftSize ||
      !Number.isSafeInteger(spectrum?.partitionCount) ||
      spectrum.partitionCount <= 0 ||
      !(spectrum.real instanceof Float64Array) ||
      !(spectrum.imaginary instanceof Float64Array) ||
      spectrum.real.length !== spectrum.partitionCount * fftSize ||
      spectrum.imaginary.length !== spectrum.real.length
    ) {
      throw new RangeError("partitioned convolver received an invalid spectrum");
    }
    this.#fftSize = fftSize;
    this.#blockFrames = blockFrames;
    this.#plan = new FixedFftPlan(fftSize);
    this.#inputReal = new Float64Array(fftSize);
    this.#inputImaginary = new Float64Array(fftSize);
    this.#outputReal = new Float64Array(fftSize);
    this.#outputImaginary = new Float64Array(fftSize);
    this.#overlap = new Float64Array(blockFrames - 1);
    this.#nextOverlap = new Float64Array(blockFrames - 1);
    this.#kernelReal = spectrum.real;
    this.#kernelImaginary = spectrum.imaginary;
    this.#partitionCount = spectrum.partitionCount;
    this.#inputSpectraReal = new Float64Array(
      this.#partitionCount * this.#fftSize,
    );
    this.#inputSpectraImaginary = new Float64Array(
      this.#partitionCount * this.#fftSize,
    );
  }

  reset() {
    this.#inputSpectraReal.fill(0);
    this.#inputSpectraImaginary.fill(0);
    this.#inputReal.fill(0);
    this.#inputImaginary.fill(0);
    this.#outputReal.fill(0);
    this.#outputImaginary.fill(0);
    this.#overlap.fill(0);
    this.#nextOverlap.fill(0);
    this.#processedBlockCount = 0;
  }

  process(input) {
    if (!(input instanceof Float64Array)) {
      throw new TypeError("partitioned convolution input must be Float64Array");
    }
    if (
      input.length === 0 ||
      input.length % this.#blockFrames !== 0
    ) {
      throw new RangeError(
        `partitioned convolution input must contain complete ${this.#blockFrames}-frame blocks`,
      );
    }
    const output = new Float64Array(input.length);
    for (
      let offset = 0;
      offset < input.length;
      offset += this.#blockFrames
    ) {
      this.#processBlock(
        input.subarray(offset, offset + this.#blockFrames),
        output.subarray(offset, offset + this.#blockFrames),
      );
    }
    return output;
  }

  #processBlock(input, output) {
    this.#inputReal.fill(0);
    this.#inputImaginary.fill(0);
    for (let frame = 0; frame < this.#blockFrames; ++frame) {
      const sample = input[frame];
      if (!Number.isFinite(sample)) {
        throw new RangeError(
          `partitioned convolution input was non-finite at frame ${frame}`,
        );
      }
      this.#inputReal[frame] = sample;
    }
    this.#plan.forward(this.#inputReal, this.#inputImaginary);
    this.#outputReal.fill(0);
    this.#outputImaginary.fill(0);

    const availablePartitions = Math.min(
      this.#processedBlockCount + 1,
      this.#partitionCount,
    );
    const currentSlot = this.#processedBlockCount % this.#partitionCount;
    for (let partition = 0; partition < availablePartitions; ++partition) {
      const kernelOffset = partition * this.#fftSize;
      const inputOffset = partition === 0
        ? 0
        : ((currentSlot + this.#partitionCount - partition) %
            this.#partitionCount) * this.#fftSize;
      const inputReal = partition === 0
        ? this.#inputReal
        : this.#inputSpectraReal;
      const inputImaginary = partition === 0
        ? this.#inputImaginary
        : this.#inputSpectraImaginary;
      for (let bin = 0; bin < this.#fftSize; ++bin) {
        const inputIndex = inputOffset + bin;
        const kernelIndex = kernelOffset + bin;
        const leftReal = inputReal[inputIndex];
        const leftImaginary = inputImaginary[inputIndex];
        const rightReal = this.#kernelReal[kernelIndex];
        const rightImaginary = this.#kernelImaginary[kernelIndex];
        this.#outputReal[bin] +=
          leftReal * rightReal - leftImaginary * rightImaginary;
        this.#outputImaginary[bin] +=
          leftReal * rightImaginary + leftImaginary * rightReal;
      }
    }
    this.#plan.inverse(this.#outputReal, this.#outputImaginary);

    for (let frame = 0; frame < this.#blockFrames; ++frame) {
      const sample = this.#outputReal[frame] +
        (frame < this.#overlap.length ? this.#overlap[frame] : 0);
      if (!Number.isFinite(sample)) {
        throw new RangeError(
          `partitioned convolution output was non-finite at frame ${frame}`,
        );
      }
      output[frame] = sample;
    }
    for (let frame = 0; frame < this.#nextOverlap.length; ++frame) {
      const sample = this.#outputReal[this.#blockFrames + frame];
      if (!Number.isFinite(sample)) {
        throw new RangeError(
          `partitioned convolution overlap was non-finite at frame ${frame}`,
        );
      }
      this.#nextOverlap[frame] = sample;
    }

    const slotOffset = currentSlot * this.#fftSize;
    this.#inputSpectraReal.set(this.#inputReal, slotOffset);
    this.#inputSpectraImaginary.set(this.#inputImaginary, slotOffset);
    const completedOverlap = this.#overlap;
    this.#overlap = this.#nextOverlap;
    this.#nextOverlap = completedOverlap;
    if (this.#processedBlockCount === Number.MAX_SAFE_INTEGER) {
      throw new RangeError("partitioned convolution block horizon overflowed");
    }
    ++this.#processedBlockCount;
  }
}

export class CanonicalMasterDynamics {
  #volumeLinear;
  #peakSourceUnits = Math.fround(30_000);
  #gainLinear = Math.fround(1);

  constructor(volumeLinear) {
    this.#volumeLinear = Math.fround(volumeLinear);
  }

  reset() {
    this.#peakSourceUnits = Math.fround(30_000);
    this.#gainLinear = Math.fround(1);
  }

  process(input) {
    const source = Math.fround(input);
    this.#peakSourceUnits = Math.fround(
      PEAK_RETENTION_PER_FRAME * this.#peakSourceUnits,
    );
    const magnitude = Math.abs(source);
    if (magnitude > this.#peakSourceUnits) this.#peakSourceUnits = magnitude;
    const requestedGain = Math.fround(
      clamp(
        Math.fround(22_000 / this.#peakSourceUnits),
        Math.fround(0.00001),
        Math.fround(1.3),
      ),
    );
    this.#gainLinear = Math.fround(
      Math.fround(GAIN_RETENTION_PER_FRAME * this.#gainLinear) +
        Math.fround(GAIN_BLEND_PER_FRAME * requestedGain),
    );
    const leveled = Math.fround(source * this.#gainLinear);
    const volumeApplied = Math.fround(leveled * this.#volumeLinear);
    const normalized = Math.fround(volumeApplied / Math.fround(32_767));
    const softened = Math.fround(Math.tanh(normalized));
    if (!Number.isFinite(softened)) {
      throw new RangeError("master dynamics produced a non-finite sample");
    }
    return clamp(softened, -FLOAT32_BELOW_ONE, FLOAT32_BELOW_ONE);
  }

  diagnostics() {
    return Object.freeze({
      peakSourceUnits: this.#peakSourceUnits,
      gainLinear: this.#gainLinear,
      volumeLinear: this.#volumeLinear,
    });
  }
}
