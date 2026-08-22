export const PCM_RING_HEADER_BYTES = 64;
export const PCM_RING_MINIMUM_CAPACITY_FRAMES = 2_048;
export const PCM_RING_MAXIMUM_CAPACITY_FRAMES = 262_144;

// Shared by renderers and standalone package consumers.  These are transport
// states, not simulator ABI values; the numeric values deliberately remain
// wire-compatible with the existing AudioWorklet protocol.
export const PcmRingProducerState = Object.freeze({
  idle: 0,
  streaming: 1,
  paused: 2,
  ended: 3,
  failed: 4,
});

export const PcmRingHeader = Object.freeze({
  writeFrame: 0,
  readFrame: 1,
  availableFrames: 2,
  underrunFrames: 3,
  underrunEvents: 4,
  generation: 5,
  producerState: 6,
});

export const PCM_RING_HEADER_SCHEMA = Object.freeze({
  id: "crankwave/pcm-ring-spsc-v1",
  headerBytes: PCM_RING_HEADER_BYTES,
  sampleEncoding: "float32-interleaved-native-endian",
  counters: Object.freeze({
    writeFrame: PcmRingHeader.writeFrame,
    readFrame: PcmRingHeader.readFrame,
    availableFrames: PcmRingHeader.availableFrames,
    underrunFrames: PcmRingHeader.underrunFrames,
    underrunEvents: PcmRingHeader.underrunEvents,
    generation: PcmRingHeader.generation,
    producerState: PcmRingHeader.producerState,
  }),
});

function integerInRange(value, minimum, maximum, name) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    throw new RangeError(`${name} must be an integer in [${minimum}, ${maximum}]`);
  }
  return value;
}

export function nextPowerOfTwo(value) {
  integerInRange(
    value,
    1,
    PCM_RING_MAXIMUM_CAPACITY_FRAMES,
    "ring capacity",
  );
  let result = 1;
  while (result < value) {
    result *= 2;
  }
  return result;
}

export function choosePcmRingCapacity(leadFrames, minimumBlockFrames) {
  integerInRange(
    leadFrames,
    1,
    PCM_RING_MAXIMUM_CAPACITY_FRAMES,
    "leadFrames",
  );
  integerInRange(
    minimumBlockFrames,
    1,
    PCM_RING_MAXIMUM_CAPACITY_FRAMES,
    "minimumBlockFrames",
  );
  const requested = Math.max(
    PCM_RING_MINIMUM_CAPACITY_FRAMES,
    minimumBlockFrames,
    leadFrames * 2,
  );
  if (requested > PCM_RING_MAXIMUM_CAPACITY_FRAMES) {
    throw new RangeError(
      `requested PCM ring exceeds ${PCM_RING_MAXIMUM_CAPACITY_FRAMES} frames`,
    );
  }
  return nextPowerOfTwo(requested);
}

export function createPcmRingBuffer({
  capacityFrames,
  channelCount,
  generation = 1,
  producerState = PcmRingProducerState.idle,
}) {
  if (typeof SharedArrayBuffer === "undefined") {
    throw new Error(
      "SharedArrayBuffer is unavailable; serve the app with cross-origin isolation",
    );
  }
  integerInRange(
    capacityFrames,
    PCM_RING_MINIMUM_CAPACITY_FRAMES,
    PCM_RING_MAXIMUM_CAPACITY_FRAMES,
    "capacityFrames",
  );
  if ((capacityFrames & (capacityFrames - 1)) !== 0) {
    throw new RangeError("capacityFrames must be a power of two");
  }
  integerInRange(channelCount, 1, 32, "channelCount");
  integerInRange(generation, 1, 0x7fff_ffff, "generation");
  const byteLength =
    PCM_RING_HEADER_BYTES +
    capacityFrames * channelCount * Float32Array.BYTES_PER_ELEMENT;
  const sharedBuffer = new SharedArrayBuffer(byteLength);
  const header = new Int32Array(sharedBuffer, 0, PCM_RING_HEADER_BYTES / 4);
  Atomics.store(header, PcmRingHeader.generation, generation);
  Atomics.store(header, PcmRingHeader.producerState, producerState);
  return {
    sharedBuffer,
    capacityFrames,
    channelCount,
    headerBytes: PCM_RING_HEADER_BYTES,
    schema: PCM_RING_HEADER_SCHEMA,
  };
}

export class PcmRingProducer {
  #sharedBuffer;
  #header;
  #samples;
  #capacityFrames;
  #channelCount;
  #mask;

  constructor(sharedBuffer, capacityFrames, channelCount) {
    if (!(sharedBuffer instanceof SharedArrayBuffer)) {
      throw new TypeError("PCM transport requires a SharedArrayBuffer");
    }
    integerInRange(
      capacityFrames,
      PCM_RING_MINIMUM_CAPACITY_FRAMES,
      PCM_RING_MAXIMUM_CAPACITY_FRAMES,
      "capacityFrames",
    );
    if ((capacityFrames & (capacityFrames - 1)) !== 0) {
      throw new RangeError("capacityFrames must be a power of two");
    }
    integerInRange(channelCount, 1, 32, "channelCount");
    const expectedBytes =
      PCM_RING_HEADER_BYTES +
      capacityFrames * channelCount * Float32Array.BYTES_PER_ELEMENT;
    if (sharedBuffer.byteLength !== expectedBytes) {
      throw new RangeError(
        `PCM SharedArrayBuffer is ${sharedBuffer.byteLength} bytes; ` +
          `${expectedBytes} bytes are required`,
      );
    }
    this.#sharedBuffer = sharedBuffer;
    this.#header = new Int32Array(
      sharedBuffer,
      0,
      PCM_RING_HEADER_BYTES / Int32Array.BYTES_PER_ELEMENT,
    );
    this.#samples = new Float32Array(sharedBuffer, PCM_RING_HEADER_BYTES);
    this.#capacityFrames = capacityFrames;
    this.#channelCount = channelCount;
    this.#mask = capacityFrames - 1;
  }

  get sharedBuffer() {
    return this.#sharedBuffer;
  }

  get capacityFrames() {
    return this.#capacityFrames;
  }

  get channelCount() {
    return this.#channelCount;
  }

  get availableFrames() {
    return Atomics.load(this.#header, PcmRingHeader.availableFrames);
  }

  get freeFrames() {
    return this.#capacityFrames - this.availableFrames;
  }

  writeInterleaved(interleaved, sourceFrameOffset = 0, requestedFrames) {
    if (!(interleaved instanceof Float32Array)) {
      throw new TypeError("PCM producer input must be Float32Array");
    }
    if (interleaved.length % this.#channelCount !== 0) {
      throw new RangeError("interleaved PCM length is not channel-aligned");
    }
    const sourceFrames = interleaved.length / this.#channelCount;
    integerInRange(sourceFrameOffset, 0, sourceFrames, "sourceFrameOffset");
    const remaining = sourceFrames - sourceFrameOffset;
    const frameCount =
      requestedFrames === undefined
        ? remaining
        : integerInRange(requestedFrames, 0, remaining, "requestedFrames");
    const writableFrames = Math.min(frameCount, this.freeFrames);
    if (writableFrames === 0) {
      return 0;
    }

    const writeFrame =
      Atomics.load(this.#header, PcmRingHeader.writeFrame) & this.#mask;
    const firstFrames = Math.min(writableFrames, this.#capacityFrames - writeFrame);
    const firstSamples = firstFrames * this.#channelCount;
    const sourceSampleOffset = sourceFrameOffset * this.#channelCount;
    const ringSampleOffset = writeFrame * this.#channelCount;
    this.#samples.set(
      interleaved.subarray(
        sourceSampleOffset,
        sourceSampleOffset + firstSamples,
      ),
      ringSampleOffset,
    );
    const secondFrames = writableFrames - firstFrames;
    if (secondFrames !== 0) {
      const secondSamples = secondFrames * this.#channelCount;
      this.#samples.set(
        interleaved.subarray(
          sourceSampleOffset + firstSamples,
          sourceSampleOffset + firstSamples + secondSamples,
        ),
        0,
      );
    }

    Atomics.store(
      this.#header,
      PcmRingHeader.writeFrame,
      (writeFrame + writableFrames) & this.#mask,
    );
    // Publish samples only after every Float32 store is complete.
    Atomics.add(this.#header, PcmRingHeader.availableFrames, writableFrames);
    Atomics.notify(this.#header, PcmRingHeader.availableFrames);
    return writableFrames;
  }

  reset(generation, producerState = PcmRingProducerState.idle) {
    integerInRange(generation, 1, 0x7fff_ffff, "generation");
    Atomics.store(this.#header, PcmRingHeader.writeFrame, 0);
    Atomics.store(this.#header, PcmRingHeader.readFrame, 0);
    Atomics.store(this.#header, PcmRingHeader.availableFrames, 0);
    Atomics.store(this.#header, PcmRingHeader.underrunFrames, 0);
    Atomics.store(this.#header, PcmRingHeader.underrunEvents, 0);
    Atomics.store(this.#header, PcmRingHeader.generation, generation);
    Atomics.store(this.#header, PcmRingHeader.producerState, producerState);
  }

  setProducerState(state) {
    Atomics.store(this.#header, PcmRingHeader.producerState, state);
  }

  snapshot() {
    return {
      writeFrame: Atomics.load(this.#header, PcmRingHeader.writeFrame),
      readFrame: Atomics.load(this.#header, PcmRingHeader.readFrame),
      availableFrames: this.availableFrames,
      underrunFrames: Atomics.load(
        this.#header,
        PcmRingHeader.underrunFrames,
      ),
      underrunEvents: Atomics.load(
        this.#header,
        PcmRingHeader.underrunEvents,
      ),
      generation: Atomics.load(this.#header, PcmRingHeader.generation),
      producerState: Atomics.load(this.#header, PcmRingHeader.producerState),
    };
  }
}

// Used by focused tests and by non-AudioWorklet consumers. The production
// AudioWorklet follows the same header protocol and writes silence on underrun.
export class PcmRingConsumer {
  #header;
  #samples;
  #capacityFrames;
  #channelCount;
  #mask;

  constructor(sharedBuffer, capacityFrames, channelCount) {
    this.#header = new Int32Array(
      sharedBuffer,
      0,
      PCM_RING_HEADER_BYTES / Int32Array.BYTES_PER_ELEMENT,
    );
    this.#samples = new Float32Array(sharedBuffer, PCM_RING_HEADER_BYTES);
    this.#capacityFrames = capacityFrames;
    this.#channelCount = channelCount;
    this.#mask = capacityFrames - 1;
  }

  readInterleaved(target, requestedFrames = target.length / this.#channelCount) {
    if (!(target instanceof Float32Array)) {
      throw new TypeError("PCM consumer target must be Float32Array");
    }
    if (!Number.isInteger(requestedFrames) || requestedFrames < 0) {
      throw new RangeError("requestedFrames must be a non-negative integer");
    }
    if (target.length < requestedFrames * this.#channelCount) {
      throw new RangeError("PCM consumer target is too small");
    }
    const available = Atomics.load(
      this.#header,
      PcmRingHeader.availableFrames,
    );
    const readableFrames = Math.min(available, requestedFrames);
    const readFrame =
      Atomics.load(this.#header, PcmRingHeader.readFrame) & this.#mask;
    const firstFrames = Math.min(readableFrames, this.#capacityFrames - readFrame);
    const firstSamples = firstFrames * this.#channelCount;
    const ringSampleOffset = readFrame * this.#channelCount;
    target.set(
      this.#samples.subarray(
        ringSampleOffset,
        ringSampleOffset + firstSamples,
      ),
      0,
    );
    const secondFrames = readableFrames - firstFrames;
    if (secondFrames !== 0) {
      const secondSamples = secondFrames * this.#channelCount;
      target.set(this.#samples.subarray(0, secondSamples), firstSamples);
    }
    const missingFrames = requestedFrames - readableFrames;
    if (missingFrames !== 0) {
      target.fill(
        0,
        readableFrames * this.#channelCount,
        requestedFrames * this.#channelCount,
      );
      Atomics.add(
        this.#header,
        PcmRingHeader.underrunFrames,
        missingFrames,
      );
      Atomics.add(this.#header, PcmRingHeader.underrunEvents, 1);
    }
    Atomics.store(
      this.#header,
      PcmRingHeader.readFrame,
      (readFrame + readableFrames) & this.#mask,
    );
    Atomics.sub(this.#header, PcmRingHeader.availableFrames, readableFrames);
    return readableFrames;
  }
}
