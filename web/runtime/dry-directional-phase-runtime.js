import {
  loadDirectionalPhaseCell,
} from "./directional-phase-cell.js";

const RUNTIME_SCHEMA =
  "crankwave/responsive-audio-directional-texture";
const CANONICAL_SAMPLE_RATE = 192_000;
const FFT_SIZE = 65_536;
const IR_COEFFICIENT_COUNT = 30_071;
const IR_HISTORY_FRAMES = IR_COEFFICIENT_COUNT - 1;
const PARTITIONED_FFT_SIZE = 8_192;
const PARTITIONED_FFT_BIT_COUNT = 13;
const PARTITIONED_BLOCK_FRAMES = 3_840;
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
  if (sampleRate !== CANONICAL_SAMPLE_RATE) {
    fail(`dry directional runtime requires ${CANONICAL_SAMPLE_RATE} Hz audio`);
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
  #reversed;
  #forwardRootReal;
  #forwardRootImaginary;

  constructor() {
    this.#reversed = new Uint32Array(FFT_SIZE);
    for (let index = 0; index < FFT_SIZE; ++index) {
      let remaining = index;
      let reversed = 0;
      for (let bit = 0; bit < 16; ++bit) {
        reversed = (reversed << 1) | (remaining & 1);
        remaining >>>= 1;
      }
      this.#reversed[index] = reversed;
    }
    this.#forwardRootReal = new Float64Array(FFT_SIZE / 2);
    this.#forwardRootImaginary = new Float64Array(FFT_SIZE / 2);
    for (let index = 0; index < FFT_SIZE / 2; ++index) {
      const angle = (-2 * Math.PI * index) / FFT_SIZE;
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
      real.length !== FFT_SIZE ||
      imaginary.length !== FFT_SIZE
    ) {
      throw new RangeError("fixed FFT requires two 65536-element Float64 arrays");
    }
    for (let index = 0; index < FFT_SIZE; ++index) {
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
    for (let width = 2; width <= FFT_SIZE; width *= 2) {
      const halfWidth = width / 2;
      const rootStep = FFT_SIZE / width;
      for (let base = 0; base < FFT_SIZE; base += width) {
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
      const scale = 1 / FFT_SIZE;
      for (let index = 0; index < FFT_SIZE; ++index) {
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
  #plan = new FixedFftPlan();
  #history = new Float64Array(IR_HISTORY_FRAMES);
  #real = new Float64Array(FFT_SIZE);
  #imaginary = new Float64Array(FFT_SIZE);

  constructor(spectrum, maximumBlockFrames) {
    this.#kernelReal = spectrum.real;
    this.#kernelImaginary = spectrum.imaginary;
    this.#maximumBlockFrames = maximumBlockFrames;
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
    this.#real.set(input, IR_HISTORY_FRAMES);
    this.#plan.forward(this.#real, this.#imaginary);
    for (let index = 0; index < FFT_SIZE; ++index) {
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
      const sample = this.#real[IR_HISTORY_FRAMES + frame];
      if (!Number.isFinite(sample)) {
        throw new RangeError(`fixed convolution produced a non-finite frame ${frame}`);
      }
      output[frame] = sample;
    }
    if (input.length >= IR_HISTORY_FRAMES) {
      this.#history.set(input.subarray(input.length - IR_HISTORY_FRAMES));
    } else {
      this.#history.copyWithin(0, input.length);
      this.#history.set(input, IR_HISTORY_FRAMES - input.length);
    }
    return output;
  }
}

class PartitionedFftPlan {
  #reversed = new Uint32Array(PARTITIONED_FFT_SIZE);
  #forwardRootReal = new Float64Array(PARTITIONED_FFT_SIZE / 2);
  #forwardRootImaginary = new Float64Array(PARTITIONED_FFT_SIZE / 2);

  constructor() {
    for (let index = 0; index < PARTITIONED_FFT_SIZE; ++index) {
      let remaining = index;
      let reversed = 0;
      for (let bit = 0; bit < PARTITIONED_FFT_BIT_COUNT; ++bit) {
        reversed = (reversed << 1) | (remaining & 1);
        remaining >>>= 1;
      }
      this.#reversed[index] = reversed;
    }
    for (let index = 0; index < PARTITIONED_FFT_SIZE / 2; ++index) {
      const angle = (-2 * Math.PI * index) / PARTITIONED_FFT_SIZE;
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
      real.length !== PARTITIONED_FFT_SIZE ||
      imaginary.length !== PARTITIONED_FFT_SIZE
    ) {
      throw new RangeError(
        "partitioned FFT requires two 8192-element Float64 arrays",
      );
    }
    for (let index = 0; index < PARTITIONED_FFT_SIZE; ++index) {
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
    for (let width = 2; width <= PARTITIONED_FFT_SIZE; width *= 2) {
      const halfWidth = width / 2;
      const rootStep = PARTITIONED_FFT_SIZE / width;
      for (let base = 0; base < PARTITIONED_FFT_SIZE; base += width) {
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
      const scale = 1 / PARTITIONED_FFT_SIZE;
      for (let index = 0; index < PARTITIONED_FFT_SIZE; ++index) {
        real[index] *= scale;
        imaginary[index] *= scale;
      }
    }
  }
}

// Complete uniform-partitioned configured-IR convolution for the additive v2
// transfer descriptor. The public process call may contain several exact 20 ms
// partitions, but never pads or drops a partial partition.
export class PartitionedSpectrumConvolver {
  #kernelReal;
  #kernelImaginary;
  #partitionCount;
  #plan = new PartitionedFftPlan();
  #inputSpectraReal;
  #inputSpectraImaginary;
  #inputReal = new Float64Array(PARTITIONED_FFT_SIZE);
  #inputImaginary = new Float64Array(PARTITIONED_FFT_SIZE);
  #outputReal = new Float64Array(PARTITIONED_FFT_SIZE);
  #outputImaginary = new Float64Array(PARTITIONED_FFT_SIZE);
  #overlap = new Float64Array(PARTITIONED_BLOCK_FRAMES - 1);
  #nextOverlap = new Float64Array(PARTITIONED_BLOCK_FRAMES - 1);
  #processedBlockCount = 0;

  constructor(spectrum) {
    if (
      spectrum?.fftSize !== PARTITIONED_FFT_SIZE ||
      spectrum?.partitionFrameCount !== PARTITIONED_BLOCK_FRAMES ||
      !Number.isSafeInteger(spectrum?.partitionCount) ||
      spectrum.partitionCount <= 0 ||
      !(spectrum.real instanceof Float64Array) ||
      !(spectrum.imaginary instanceof Float64Array) ||
      spectrum.real.length !== spectrum.partitionCount * PARTITIONED_FFT_SIZE ||
      spectrum.imaginary.length !== spectrum.real.length
    ) {
      throw new RangeError("partitioned convolver received an invalid spectrum");
    }
    this.#kernelReal = spectrum.real;
    this.#kernelImaginary = spectrum.imaginary;
    this.#partitionCount = spectrum.partitionCount;
    this.#inputSpectraReal = new Float64Array(
      this.#partitionCount * PARTITIONED_FFT_SIZE,
    );
    this.#inputSpectraImaginary = new Float64Array(
      this.#partitionCount * PARTITIONED_FFT_SIZE,
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
      input.length % PARTITIONED_BLOCK_FRAMES !== 0
    ) {
      throw new RangeError(
        "partitioned convolution input must contain complete 3840-frame blocks",
      );
    }
    const output = new Float64Array(input.length);
    for (
      let offset = 0;
      offset < input.length;
      offset += PARTITIONED_BLOCK_FRAMES
    ) {
      this.#processBlock(
        input.subarray(offset, offset + PARTITIONED_BLOCK_FRAMES),
        output.subarray(offset, offset + PARTITIONED_BLOCK_FRAMES),
      );
    }
    return output;
  }

  #processBlock(input, output) {
    this.#inputReal.fill(0);
    this.#inputImaginary.fill(0);
    for (let frame = 0; frame < PARTITIONED_BLOCK_FRAMES; ++frame) {
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
      const kernelOffset = partition * PARTITIONED_FFT_SIZE;
      const inputOffset = partition === 0
        ? 0
        : ((currentSlot + this.#partitionCount - partition) %
            this.#partitionCount) * PARTITIONED_FFT_SIZE;
      const inputReal = partition === 0
        ? this.#inputReal
        : this.#inputSpectraReal;
      const inputImaginary = partition === 0
        ? this.#inputImaginary
        : this.#inputSpectraImaginary;
      for (let bin = 0; bin < PARTITIONED_FFT_SIZE; ++bin) {
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

    for (let frame = 0; frame < PARTITIONED_BLOCK_FRAMES; ++frame) {
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
      const sample = this.#outputReal[PARTITIONED_BLOCK_FRAMES + frame];
      if (!Number.isFinite(sample)) {
        throw new RangeError(
          `partitioned convolution overlap was non-finite at frame ${frame}`,
        );
      }
      this.#nextOverlap[frame] = sample;
    }

    const slotOffset = currentSlot * PARTITIONED_FFT_SIZE;
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
