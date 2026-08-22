import { DeviceRateResampler } from "/runtime/device-resampler.js";
import {
  PcmRingProducer,
  PcmRingProducerState,
} from "/runtime/pcm-ring-buffer.js";
import { CrankwaveAudioEngine } from "/runtime/crankwave-audio-engine.js";

const MAXIMUM_CARRIER_BYTES = 2 ** 32;
const PUMP_RENDER_BUDGET = 4;
const STATS_INTERVAL_MS = 250;

let engine = null;
let producer = null;
let resampler = null;
let outputSampleRate = null;
let leadFrames = 0;
let generation = 1;
let playing = false;
let priming = false;
let pumpScheduled = false;
let pendingOutput = new Float32Array(0);
let pendingOutputOffset = 0;
let lastOutputRms = 0;
let lastStatsAt = 0;
let commandTail = Promise.resolve();

function publicError(error) {
  return Object.freeze({
    name: typeof error?.name === "string" ? error.name : "Error",
    code: typeof error?.code === "string" ? error.code : null,
    message:
      typeof error?.message === "string" && error.message.length > 0
        ? error.message
        : String(error),
  });
}

function reply(requestId, result = {}) {
  self.postMessage({ type: "reply", requestId, ok: true, result });
}

function reject(requestId, error) {
  self.postMessage({
    type: "reply",
    requestId,
    ok: false,
    error: publicError(error),
  });
}

function event(type, value = {}) {
  self.postMessage({ type, ...value });
}

function requireEngine() {
  if (engine === null) throw new Error("Load a CRANKWAVE package first.");
  return engine;
}

function requireOutput() {
  if (producer === null || resampler === null || outputSampleRate === null) {
    throw new Error("Attach browser audio output before starting playback.");
  }
}

function metadata() {
  const current = requireEngine();
  const point = current.operatingPoint;
  return Object.freeze({
    engineId: current.engineId,
    sampleRate: current.sampleRate,
    minimumRpm: current.minimumRpm,
    maximumRpm: current.maximumRpm,
    blockFrames: current.blockFrames,
    operatingPoint: point,
    manifoldPressurePaAbs: current.loadManifoldPressurePa(
      point.rpm,
      point.load01,
    ),
  });
}

function stopOutput(state = PcmRingProducerState.paused) {
  playing = false;
  priming = false;
  pendingOutput = new Float32Array(0);
  pendingOutputOffset = 0;
  producer?.setProducerState(state);
}

function nextGeneration() {
  generation = generation >= 0x7fff_fffe ? 1 : generation + 1;
  return generation;
}

function finiteUnit(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError(`${label} must be finite.`);
  }
  if (value < 0 || value > 1) {
    throw new RangeError(`${label} must lie in [0, 1].`);
  }
  return value;
}

function finiteRpm(value) {
  const current = requireEngine();
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError("rpm must be finite.");
  }
  if (value < current.minimumRpm || value > current.maximumRpm) {
    throw new RangeError(
      `rpm must lie in ${current.minimumRpm}..${current.maximumRpm}.`,
    );
  }
  return value;
}

function setOperatingPoint(value) {
  const current = requireEngine();
  const point = {
    rpm: finiteRpm(value?.rpm),
    throttle01: finiteUnit(value?.throttle01, "throttle01"),
    load01: finiteUnit(value?.load01, "load01"),
  };
  current.setOperatingPoint(point);
  const accepted = current.operatingPoint;
  return Object.freeze({
    operatingPoint: accepted,
    manifoldPressurePaAbs: current.loadManifoldPressurePa(
      accepted.rpm,
      accepted.load01,
    ),
  });
}

function rms(samples) {
  if (samples.length === 0) return 0;
  let squareSum = 0;
  for (const sample of samples) {
    if (!Number.isFinite(sample)) {
      throw new RangeError("CRANKWAVE output contains a non-finite sample.");
    }
    squareSum += sample * sample;
  }
  return Math.sqrt(squareSum / samples.length);
}

function postStats(force = false) {
  if (producer === null) return;
  const now = performance.now();
  if (!force && now - lastStatsAt < STATS_INTERVAL_MS) return;
  lastStatsAt = now;
  event("stats", {
    ...producer.snapshot(),
    sampleRate: outputSampleRate,
    leadFrames,
    playing,
    priming,
    outputRms: lastOutputRms,
    operatingPoint: engine?.operatingPoint ?? null,
  });
}

function schedulePump(delay = 0) {
  if (!playing || pumpScheduled) return;
  pumpScheduled = true;
  setTimeout(() => {
    pumpScheduled = false;
    pump();
  }, delay);
}

function publishPendingOutput() {
  if (pendingOutputOffset >= pendingOutput.length) return true;
  const written = producer.writeInterleaved(
    pendingOutput,
    pendingOutputOffset,
    pendingOutput.length - pendingOutputOffset,
  );
  pendingOutputOffset += written;
  if (pendingOutputOffset < pendingOutput.length) return false;
  pendingOutput = new Float32Array(0);
  pendingOutputOffset = 0;
  return true;
}

function pump() {
  if (!playing) return;
  try {
    requireOutput();
    let rendered = 0;
    while (rendered < PUMP_RENDER_BUDGET) {
      if (!publishPendingOutput()) break;
      if (producer.availableFrames >= leadFrames) break;

      const current = requireEngine();
      const canonical = current.render(current.blockFrames);
      if (!(canonical instanceof Float32Array)) {
        throw new TypeError("CRANKWAVE render output must be mono Float32 PCM.");
      }
      rendered += 1;
      if (canonical.length === 0) continue;
      const device = resampler.push(canonical);
      lastOutputRms = rms(device);
      pendingOutput = device;
      pendingOutputOffset = 0;
    }

    publishPendingOutput();
    if (priming && producer.availableFrames >= leadFrames) {
      priming = false;
    producer.setProducerState(PcmRingProducerState.streaming);
      event("audio-running", {
        availableFrames: producer.availableFrames,
        sampleRate: outputSampleRate,
      });
    }
    postStats();
    schedulePump(producer.availableFrames >= leadFrames ? 8 : 0);
  } catch (error) {
    playing = false;
    priming = false;
    producer?.setProducerState(PcmRingProducerState.failed);
    postStats(true);
    event("playback-error", { error: publicError(error) });
  }
}

async function loadPackage(message) {
  stopOutput();
  producer = null;
  resampler = null;
  outputSampleRate = null;
  const bytes = message.bytes;
  if (!(bytes instanceof ArrayBuffer)) {
    throw new TypeError("load-package requires a transferred ArrayBuffer.");
  }
  if (bytes.byteLength === 0 || bytes.byteLength > MAXIMUM_CARRIER_BYTES) {
    throw new RangeError("CRANKWAVE carrier size is outside the v1 bounds.");
  }
  const loaded = await CrankwaveAudioEngine.load(bytes);
  engine = loaded;
  return metadata();
}

function attachOutput(message) {
  requireEngine();
  if (!(message.sharedBuffer instanceof SharedArrayBuffer)) {
    throw new TypeError("attach-output requires a SharedArrayBuffer.");
  }
  if (message.channelCount !== 1) {
    throw new RangeError("The CRANKWAVE harness requires mono output.");
  }
  if (
    !Number.isSafeInteger(message.outputSampleRate) ||
    message.outputSampleRate < 8_000 ||
    message.outputSampleRate > engine.sampleRate
  ) {
    throw new RangeError("The browser output sample rate is unsupported.");
  }
  if (
    !Number.isSafeInteger(message.leadFrames) ||
    message.leadFrames <= 0 ||
    message.leadFrames > message.capacityFrames
  ) {
    throw new RangeError("Audio lead must fit inside the shared ring.");
  }

  stopOutput();
  producer = new PcmRingProducer(
    message.sharedBuffer,
    message.capacityFrames,
    message.channelCount,
  );
  outputSampleRate = message.outputSampleRate;
  leadFrames = message.leadFrames;
  generation = Number.isSafeInteger(message.generation)
    ? message.generation
    : generation;
  producer.reset(generation, PcmRingProducerState.idle);
  resampler = new DeviceRateResampler({
    inputSampleRate: engine.sampleRate,
    outputSampleRate,
    channelCount: 1,
  });
  pendingOutput = new Float32Array(0);
  pendingOutputOffset = 0;
  return Object.freeze({ outputSampleRate, leadFrames, generation });
}

function play() {
  requireEngine();
  requireOutput();
  if (playing) return Object.freeze({ playing: true, priming });
  producer.reset(nextGeneration(), PcmRingProducerState.idle);
  resampler = new DeviceRateResampler({
    inputSampleRate: engine.sampleRate,
    outputSampleRate,
    channelCount: 1,
  });
  pendingOutput = new Float32Array(0);
  pendingOutputOffset = 0;
  lastOutputRms = 0;
  playing = true;
  priming = true;
  postStats(true);
  schedulePump();
  return Object.freeze({ playing, priming, generation });
}

async function dispatch(message) {
  if (message === null || typeof message !== "object") {
    throw new TypeError("Worker commands must be objects.");
  }
  switch (message.type) {
    case "load-package":
      return loadPackage(message);
    case "attach-output":
      return attachOutput(message);
    case "set-operating-point":
      return setOperatingPoint(message.operatingPoint);
    case "play":
      return play();
    case "stop":
      stopOutput();
      postStats(true);
      return Object.freeze({ playing: false });
    case "status":
      return Object.freeze({
        package: engine === null ? null : metadata(),
        output: producer?.snapshot() ?? null,
        playing,
        priming,
      });
    case "dispose":
      stopOutput(PcmRingProducerState.ended);
      engine = null;
      producer = null;
      resampler = null;
      outputSampleRate = null;
      return Object.freeze({ disposed: true });
    default:
      throw new Error(`Unsupported harness command ${String(message.type)}.`);
  }
}

self.addEventListener("message", (messageEvent) => {
  const message = messageEvent.data;
  const requestId = message?.requestId;
  commandTail = commandTail.then(async () => {
    try {
      const result = await dispatch(message);
      reply(requestId, result);
    } catch (error) {
      reject(requestId, error);
    }
  });
});
