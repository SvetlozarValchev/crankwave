const ZERO_RAMP_FRAMES = 32;
const RECOVERY_RAMP_FRAMES = 32;
const PRODUCER_IDLE = 0;
const PRODUCER_STREAMING = 1;
const PRODUCER_PAUSED = 2;
const PRODUCER_ENDED = 3;
const PRODUCER_FAILED = 4;

class CrankwaveRingOutput extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const config = options.processorOptions;
    if (
      !config ||
      !(config.sharedBuffer instanceof SharedArrayBuffer) ||
      config.headerBytes !== 64 ||
      !Number.isInteger(config.capacityFrames) ||
      config.capacityFrames < 256 ||
      !Number.isInteger(config.channelCount) ||
      config.channelCount < 1 ||
      !config.indices
    ) {
      throw new Error("Invalid shared audio ring configuration.");
    }
    const requiredBytes =
      config.headerBytes +
      config.capacityFrames *
        config.channelCount *
        Float32Array.BYTES_PER_ELEMENT;
    if (config.sharedBuffer.byteLength < requiredBytes) {
      throw new Error("Shared audio ring storage is truncated.");
    }

    this.state = new Int32Array(
      config.sharedBuffer,
      0,
      config.headerBytes / Int32Array.BYTES_PER_ELEMENT,
    );
    this.samples = new Float32Array(config.sharedBuffer, config.headerBytes);
    this.capacityFrames = config.capacityFrames;
    this.channelCount = config.channelCount;
    this.indices = config.indices;
    this.lastOutput = new Float32Array(this.channelCount);
    this.zeroRampStart = new Float32Array(this.channelCount);
    this.zeroRampPosition = ZERO_RAMP_FRAMES;
    this.recoveryRampPosition = RECOVERY_RAMP_FRAMES;
    this.inUnderrun = false;
    this.underrunCounted = false;
    this.running = true;
    this.generation = Atomics.load(this.state, this.indices.generation);

    this.port.onmessage = (event) => {
      if (event.data?.type === "stop") this.running = false;
    };
  }

  resetForGeneration(generation) {
    this.generation = generation;
    this.lastOutput.fill(0);
    this.zeroRampStart.fill(0);
    this.zeroRampPosition = ZERO_RAMP_FRAMES;
    this.recoveryRampPosition = RECOVERY_RAMP_FRAMES;
    this.inUnderrun = false;
    this.underrunCounted = false;
  }

  beginUnderrun(countAsUnderrun) {
    if (this.inUnderrun) {
      if (countAsUnderrun && !this.underrunCounted) {
        Atomics.add(this.state, this.indices.underrunEvents, 1);
        this.underrunCounted = true;
      }
      return;
    }
    this.inUnderrun = true;
    this.zeroRampPosition = 0;
    this.zeroRampStart.set(this.lastOutput);
    this.underrunCounted = countAsUnderrun;
    if (countAsUnderrun) {
      Atomics.add(this.state, this.indices.underrunEvents, 1);
    }
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!this.running || output.length === 0) {
      for (const channel of output) channel.fill(0);
      return this.running;
    }

    const frameCount = output[0].length;
    const generation = Atomics.load(this.state, this.indices.generation);
    if (generation !== this.generation) this.resetForGeneration(generation);

    let available = Atomics.load(this.state, this.indices.availableFrames);
    const producerState = Atomics.load(
      this.state,
      this.indices.producerState,
    );
    available = Math.max(0, Math.min(this.capacityFrames, available));
    const consumerHeld =
      producerState === PRODUCER_IDLE || producerState === PRODUCER_PAUSED;
    const framesToRead = consumerHeld ? 0 : Math.min(frameCount, available);
    let readFrame = Atomics.load(this.state, this.indices.readFrame);
    readFrame =
      ((readFrame % this.capacityFrames) + this.capacityFrames) %
      this.capacityFrames;

    if (framesToRead > 0 && this.inUnderrun) {
      this.inUnderrun = false;
      this.underrunCounted = false;
      this.recoveryRampPosition = 0;
    }

    for (let frame = 0; frame < framesToRead; frame += 1) {
      const sourceFrame = (readFrame + frame) % this.capacityFrames;
      const gain =
        this.recoveryRampPosition < RECOVERY_RAMP_FRAMES
          ? (this.recoveryRampPosition + 1) / RECOVERY_RAMP_FRAMES
          : 1;
      for (let channel = 0; channel < output.length; channel += 1) {
        const sourceChannel = Math.min(channel, this.channelCount - 1);
        const sample =
          this.samples[sourceFrame * this.channelCount + sourceChannel] * gain;
        output[channel][frame] = sample;
        if (channel < this.channelCount) this.lastOutput[channel] = sample;
      }
      if (this.recoveryRampPosition < RECOVERY_RAMP_FRAMES) {
        this.recoveryRampPosition += 1;
      }
    }

    const missingFrames = frameCount - framesToRead;
    if (missingFrames > 0) {
      const countAsUnderrun = producerState === PRODUCER_STREAMING;
      this.beginUnderrun(countAsUnderrun);
      if (countAsUnderrun) {
        Atomics.add(this.state, this.indices.underrunFrames, missingFrames);
      }
      for (let frame = framesToRead; frame < frameCount; frame += 1) {
        const factor =
          this.zeroRampPosition < ZERO_RAMP_FRAMES
            ? 1 - (this.zeroRampPosition + 1) / ZERO_RAMP_FRAMES
            : 0;
        for (let channel = 0; channel < output.length; channel += 1) {
          const sourceChannel = Math.min(channel, this.channelCount - 1);
          const sample = this.zeroRampStart[sourceChannel] * factor;
          output[channel][frame] = sample;
          if (channel < this.channelCount) this.lastOutput[channel] = sample;
        }
        if (this.zeroRampPosition < ZERO_RAMP_FRAMES) {
          this.zeroRampPosition += 1;
        }
      }
    }

    if (framesToRead > 0) {
      Atomics.store(
        this.state,
        this.indices.readFrame,
        (readFrame + framesToRead) % this.capacityFrames,
      );
      Atomics.sub(this.state, this.indices.availableFrames, framesToRead);
    }
    const producerTerminal =
      producerState === PRODUCER_ENDED || producerState === PRODUCER_FAILED;
    return !(
      producerTerminal &&
      framesToRead === 0 &&
      this.zeroRampPosition >= ZERO_RAMP_FRAMES
    );
  }
}

registerProcessor("crankwave-ring-output", CrankwaveRingOutput);
