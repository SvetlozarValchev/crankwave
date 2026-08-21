import {
  PCM_RING_HEADER_BYTES,
  PcmRingProducerState,
  PcmRingHeader,
  choosePcmRingCapacity,
  createPcmRingBuffer,
} from "/runtime/pcm-ring-buffer.js";

const WORKER_URL = "/harness/harness-worker.js";
const WORKLET_URL = "/harness/audio-worklet.js";
const PROCESSOR_NAME = "vehicleengine-ring-output";
const MAXIMUM_CARRIER_BYTES = 2 ** 32;
const OUTPUT_LEAD_SECONDS = 0.35;

const elements = {
  securityGate: document.querySelector("#security-gate"),
  securityStatus: document.querySelector("#security-status"),
  packageStatus: document.querySelector("#package-status"),
  audioStatus: document.querySelector("#audio-status"),
  chooseFileButton: document.querySelector("#choose-file-button"),
  packageFileInput: document.querySelector("#package-file-input"),
  dropZone: document.querySelector("#drop-zone"),
  fileName: document.querySelector("#file-name"),
  fileDetail: document.querySelector("#file-detail"),
  packageMessage: document.querySelector("#package-message"),
  engineId: document.querySelector("#engine-id"),
  rpmCoverage: document.querySelector("#rpm-coverage"),
  packageRate: document.querySelector("#package-rate"),
  playButton: document.querySelector("#play-button"),
  stopButton: document.querySelector("#stop-button"),
  rpmInput: document.querySelector("#rpm-input"),
  rpmOutput: document.querySelector("#rpm-output"),
  rpmMinimum: document.querySelector("#rpm-minimum"),
  rpmMaximum: document.querySelector("#rpm-maximum"),
  throttleInput: document.querySelector("#throttle-input"),
  throttleOutput: document.querySelector("#throttle-output"),
  loadInput: document.querySelector("#load-input"),
  loadOutput: document.querySelector("#load-output"),
  mapOutput: document.querySelector("#map-output"),
  deviceRate: document.querySelector("#device-rate"),
  bufferOutput: document.querySelector("#buffer-output"),
  underrunOutput: document.querySelector("#underrun-output"),
  levelMeter: document.querySelector("#level-meter"),
  levelOutput: document.querySelector("#level-output"),
  audioMessage: document.querySelector("#audio-message"),
};

const state = {
  securityAdmitted: false,
  worker: null,
  nextRequestId: 1,
  pendingRequests: new Map(),
  metadata: null,
  busy: false,
  playing: false,
  priming: false,
  controlScheduled: false,
  controlRevision: 0,
  audioContext: null,
  workletLoaded: false,
  audioNode: null,
  ring: null,
  generation: 1,
};

function setChip(element, text, chipState) {
  element.textContent = text;
  element.dataset.state = chipState;
}

function setMessage(element, text, isError = false) {
  element.textContent = text;
  element.classList.toggle("is-error", isError);
}

function formatRate(value) {
  return Number.isFinite(value) ? `${(value / 1000).toFixed(1)} kHz` : "—";
}

function formatBytes(value) {
  if (!Number.isFinite(value) || value < 0) return "—";
  const units = ["B", "KiB", "MiB", "GiB"];
  let amount = value;
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) {
    amount /= 1024;
    unit += 1;
  }
  return `${amount.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function formatRpm(value) {
  return Number.isFinite(value) ? Math.round(value).toLocaleString("en-US") : "—";
}

function formatMap(value) {
  return Number.isFinite(value) ? `${(value / 1000).toFixed(1)} kPa abs` : "—";
}

function updateButtons() {
  const packageReady = state.metadata !== null && !state.busy;
  elements.chooseFileButton.disabled = !state.securityAdmitted || state.busy;
  elements.playButton.disabled =
    !state.securityAdmitted || !packageReady || state.playing || state.priming;
  elements.stopButton.disabled = !state.playing && !state.priming;
  for (const input of [
    elements.rpmInput,
    elements.throttleInput,
    elements.loadInput,
  ]) {
    input.disabled = !packageReady;
  }
}

function readOperatingPoint() {
  return Object.freeze({
    rpm: Number(elements.rpmInput.value),
    throttle01: Number(elements.throttleInput.value) / 100,
    load01: Number(elements.loadInput.value) / 100,
  });
}

function renderControlValues() {
  const point = readOperatingPoint();
  elements.rpmOutput.value = `${formatRpm(point.rpm)} RPM`;
  elements.throttleOutput.value = `${Math.round(point.throttle01 * 100)}%`;
  elements.loadOutput.value = `${Math.round(point.load01 * 100)}%`;
}

function applyOperatingPoint(point, manifoldPressurePaAbs) {
  if (point && Number.isFinite(point.rpm)) {
    elements.rpmInput.value = String(Math.round(point.rpm));
  }
  if (point && Number.isFinite(point.throttle01)) {
    elements.throttleInput.value = String(Math.round(point.throttle01 * 100));
  }
  if (point && Number.isFinite(point.load01)) {
    elements.loadInput.value = String(Math.round(point.load01 * 100));
  }
  elements.mapOutput.textContent = formatMap(manifoldPressurePaAbs);
  renderControlValues();
}

function publicWorkerError(value) {
  const error = new Error(value?.message ?? "The audio worker rejected a command.");
  error.name = value?.name ?? "Error";
  if (typeof value?.code === "string") error.code = value.code;
  return error;
}

function command(type, value = {}, transfer = []) {
  if (state.worker === null) {
    return Promise.reject(new Error("The audio worker is unavailable."));
  }
  const requestId = state.nextRequestId++;
  return new Promise((resolve, reject) => {
    state.pendingRequests.set(requestId, { resolve, reject });
    state.worker.postMessage({ type, requestId, ...value }, transfer);
  });
}

function rejectPendingRequests(error) {
  for (const pending of state.pendingRequests.values()) pending.reject(error);
  state.pendingRequests.clear();
}

function acceptStats(message) {
  const sampleRate = Number(message.sampleRate);
  const availableFrames = Number(message.availableFrames);
  elements.bufferOutput.textContent =
    Number.isFinite(sampleRate) && sampleRate > 0 && Number.isFinite(availableFrames)
      ? `${Math.round((availableFrames / sampleRate) * 1000)} ms`
      : "—";
  elements.underrunOutput.textContent = Number.isFinite(message.underrunEvents)
    ? String(message.underrunEvents)
    : "0";

  const rms = Number(message.outputRms);
  const decibels = Number.isFinite(rms) && rms > 0 ? 20 * Math.log10(rms) : -Infinity;
  const meterAmount = Number.isFinite(decibels)
    ? Math.max(0, Math.min(1, (decibels + 60) / 60))
    : 0;
  elements.levelMeter.style.width = `${meterAmount * 100}%`;
  elements.levelOutput.value = Number.isFinite(decibels)
    ? `${decibels.toFixed(1)} dBFS`
    : "−∞ dBFS";
}

function acceptWorkerMessage(message) {
  if (message?.type === "reply") {
    const pending = state.pendingRequests.get(message.requestId);
    if (!pending) return;
    state.pendingRequests.delete(message.requestId);
    if (message.ok) pending.resolve(message.result);
    else pending.reject(publicWorkerError(message.error));
    return;
  }
  if (message?.type === "stats") {
    acceptStats(message);
    return;
  }
  if (message?.type === "audio-running") {
    state.priming = false;
    state.playing = true;
    setChip(elements.audioStatus, "Audio running", "good");
    setMessage(
      elements.audioMessage,
      `Streaming mono output at ${formatRate(message.sampleRate)}.`,
    );
    updateButtons();
    return;
  }
  if (message?.type === "playback-error") {
    detachAudioNode();
    setChip(elements.audioStatus, "Playback failed", "bad");
    setMessage(elements.audioMessage, message.error?.message ?? "Playback failed.", true);
    updateButtons();
  }
}

function startWorker() {
  state.worker = new Worker(WORKER_URL, {
    type: "module",
    name: "vehicleengine-audio-bridge",
  });
  state.worker.addEventListener("message", (event) => {
    acceptWorkerMessage(event.data);
  });
  state.worker.addEventListener("error", (event) => {
    const error = new Error(event.message || "The audio worker failed to load.");
    rejectPendingRequests(error);
    state.busy = false;
    state.playing = false;
    state.priming = false;
    setChip(elements.packageStatus, "Worker failed", "bad");
    setChip(elements.audioStatus, "Audio unavailable", "bad");
    setMessage(elements.packageMessage, error.message, true);
    updateButtons();
  });
}

function detachAudioNode() {
  if (state.audioNode !== null) {
    state.audioNode.port.postMessage({ type: "stop" });
    state.audioNode.disconnect();
    state.audioNode = null;
  }
  state.ring = null;
  state.playing = false;
  state.priming = false;
  elements.bufferOutput.textContent = "—";
  elements.underrunOutput.textContent = "0";
  elements.levelMeter.style.width = "0%";
  elements.levelOutput.value = "−∞ dBFS";
}

function clearPackagePresentation() {
  state.metadata = null;
  elements.engineId.textContent = "—";
  elements.rpmCoverage.textContent = "—";
  elements.packageRate.textContent = "—";
  elements.rpmMinimum.textContent = "—";
  elements.rpmMaximum.textContent = "—";
  elements.rpmOutput.value = "—";
  elements.throttleOutput.value = "—";
  elements.loadOutput.value = "—";
  elements.mapOutput.textContent = "—";
  elements.deviceRate.textContent = "—";
}

async function loadFile(file) {
  if (!(file instanceof File)) return;
  if (!state.securityAdmitted || state.worker === null) {
    setMessage(
      elements.packageMessage,
      "Open this page through the isolated localhost harness server first.",
      true,
    );
    return;
  }
  if (file.size <= 0 || file.size > MAXIMUM_CARRIER_BYTES) {
    setChip(elements.packageStatus, "Package rejected", "bad");
    setMessage(
      elements.packageMessage,
      "The selected file is empty or exceeds the VEHICLEENGINE v1 carrier limit.",
      true,
    );
    return;
  }

  state.busy = true;
  updateButtons();
  setChip(elements.packageStatus, "Verifying package", "busy");
  setChip(elements.audioStatus, "Audio stopped", "idle");
  elements.fileName.textContent = file.name;
  elements.fileDetail.textContent = `${formatBytes(file.size)} · local browser file`;
  setMessage(
    elements.packageMessage,
    "Reading and cryptographically verifying the complete carrier…",
  );
  try {
    if (state.playing || state.priming) await command("stop");
    detachAudioNode();
    clearPackagePresentation();
    const bytes = await file.arrayBuffer();
    const result = await command("load-package", { bytes }, [bytes]);
    state.metadata = result;

    elements.engineId.textContent = result.engineId;
    elements.rpmCoverage.textContent =
      `${formatRpm(result.minimumRpm)}–${formatRpm(result.maximumRpm)} RPM`;
    elements.packageRate.textContent = formatRate(result.sampleRate);
    elements.rpmInput.min = String(Math.ceil(result.minimumRpm));
    elements.rpmInput.max = String(Math.floor(result.maximumRpm));
    elements.rpmMinimum.textContent = formatRpm(result.minimumRpm);
    elements.rpmMaximum.textContent = formatRpm(result.maximumRpm);
    applyOperatingPoint(result.operatingPoint, result.manifoldPressurePaAbs);

    setChip(elements.packageStatus, "Package ready", "good");
    setMessage(
      elements.packageMessage,
      `Verified ${result.engineId}. Playback will use only the packaged audio runtime.`,
    );
  } catch (error) {
    clearPackagePresentation();
    setChip(elements.packageStatus, "Package rejected", "bad");
    setMessage(elements.packageMessage, error.message, true);
  } finally {
    state.busy = false;
    elements.packageFileInput.value = "";
    updateButtons();
  }
}

function scheduleOperatingPoint() {
  renderControlValues();
  if (state.metadata === null || state.busy) return;
  state.controlRevision += 1;
  if (state.controlScheduled) return;
  state.controlScheduled = true;
  requestAnimationFrame(() => {
    state.controlScheduled = false;
    const revision = state.controlRevision;
    const operatingPoint = readOperatingPoint();
    command("set-operating-point", { operatingPoint })
      .then((result) => {
        if (revision !== state.controlRevision) return;
        applyOperatingPoint(result.operatingPoint, result.manifoldPressurePaAbs);
      })
      .catch((error) => {
        setMessage(elements.audioMessage, error.message, true);
      });
  });
}

async function ensureAudioGraph() {
  const AudioContextClass = window.AudioContext ?? window.webkitAudioContext;
  if (state.audioContext === null) {
    state.audioContext = new AudioContextClass({ latencyHint: "interactive" });
  }
  const resumePromise = state.audioContext.resume();
  if (!state.workletLoaded) {
    await state.audioContext.audioWorklet.addModule(WORKLET_URL);
    state.workletLoaded = true;
  }
  await resumePromise;
  if (state.audioContext.state !== "running") {
    throw new Error("The browser did not grant the audio start gesture.");
  }

  if (state.audioNode === null) {
    const leadFrames = Math.ceil(
      state.audioContext.sampleRate * OUTPUT_LEAD_SECONDS,
    );
    const capacityFrames = choosePcmRingCapacity(leadFrames, 128);
    state.generation = state.generation >= 0x7fff_fffe ? 1 : state.generation + 1;
    const ring = createPcmRingBuffer({
      capacityFrames,
      channelCount: 1,
      generation: state.generation,
      producerState: PcmRingProducerState.idle,
    });
    await command("attach-output", {
      sharedBuffer: ring.sharedBuffer,
      capacityFrames: ring.capacityFrames,
      channelCount: ring.channelCount,
      outputSampleRate: state.audioContext.sampleRate,
      leadFrames,
      generation: state.generation,
    });
    state.audioNode = new AudioWorkletNode(state.audioContext, PROCESSOR_NAME, {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [1],
      processorOptions: {
        sharedBuffer: ring.sharedBuffer,
        headerBytes: PCM_RING_HEADER_BYTES,
        capacityFrames: ring.capacityFrames,
        channelCount: ring.channelCount,
        indices: PcmRingHeader,
      },
    });
    state.audioNode.connect(state.audioContext.destination);
    state.ring = ring;
    elements.deviceRate.textContent = formatRate(state.audioContext.sampleRate);
  }
}

async function play() {
  if (state.metadata === null || state.playing || state.priming) return;
  state.priming = true;
  updateButtons();
  setChip(elements.audioStatus, "Priming audio", "busy");
  setMessage(elements.audioMessage, "Building a stable browser-audio lead…");
  try {
    await ensureAudioGraph();
    await command("play");
  } catch (error) {
    state.priming = false;
    state.playing = false;
    setChip(elements.audioStatus, "Playback failed", "bad");
    setMessage(elements.audioMessage, error.message, true);
    updateButtons();
  }
}

async function stop() {
  if (!state.playing && !state.priming) return;
  try {
    await command("stop");
  } catch (error) {
    setMessage(elements.audioMessage, error.message, true);
  }
  state.playing = false;
  state.priming = false;
  setChip(elements.audioStatus, "Audio stopped", "idle");
  setMessage(elements.audioMessage, "Playback is stopped; the package remains loaded.");
  updateButtons();
}

function configureSecurity() {
  const AudioContextClass = window.AudioContext ?? window.webkitAudioContext;
  state.securityAdmitted =
    window.isSecureContext === true &&
    window.crossOriginIsolated === true &&
    typeof SharedArrayBuffer === "function" &&
    typeof Worker === "function" &&
    typeof AudioWorkletNode === "function" &&
    typeof AudioContextClass === "function" &&
    typeof globalThis.crypto?.subtle?.digest === "function";
  elements.securityGate.hidden = state.securityAdmitted;
  setChip(
    elements.securityStatus,
    state.securityAdmitted ? "Isolated audio ready" : "Browser checks failed",
    state.securityAdmitted ? "good" : "bad",
  );
}

function bindEvents() {
  elements.chooseFileButton.addEventListener("click", () => {
    elements.packageFileInput.click();
  });
  elements.packageFileInput.addEventListener("change", () => {
    void loadFile(elements.packageFileInput.files?.[0]);
  });
  elements.dropZone.addEventListener("click", () => {
    if (!elements.chooseFileButton.disabled) elements.packageFileInput.click();
  });
  elements.dropZone.addEventListener("keydown", (event) => {
    if (
      (event.key === "Enter" || event.key === " ") &&
      state.securityAdmitted &&
      !state.busy
    ) {
      event.preventDefault();
      elements.packageFileInput.click();
    }
  });
  for (const type of ["dragenter", "dragover"]) {
    elements.dropZone.addEventListener(type, (event) => {
      event.preventDefault();
      if (!state.busy) elements.dropZone.classList.add("is-dragging");
    });
  }
  for (const type of ["dragleave", "drop"]) {
    elements.dropZone.addEventListener(type, (event) => {
      event.preventDefault();
      elements.dropZone.classList.remove("is-dragging");
    });
  }
  elements.dropZone.addEventListener("drop", (event) => {
    if (state.securityAdmitted && !state.busy) {
      void loadFile(event.dataTransfer?.files?.[0]);
    }
  });
  for (const input of [
    elements.rpmInput,
    elements.throttleInput,
    elements.loadInput,
  ]) {
    input.addEventListener("input", scheduleOperatingPoint);
  }
  elements.playButton.addEventListener("click", () => void play());
  elements.stopButton.addEventListener("click", () => void stop());
}

window.addEventListener("beforeunload", () => {
  state.audioNode?.port.postMessage({ type: "stop" });
  state.audioNode?.disconnect();
  state.worker?.postMessage({ type: "dispose", requestId: 0 });
  state.worker?.terminate();
  void state.audioContext?.close();
});

configureSecurity();
bindEvents();
if (state.securityAdmitted) startWorker();
updateButtons();
