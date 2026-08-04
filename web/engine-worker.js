import {
  BrowserEngineRuntime,
  publicError,
} from "./runtime/browser-engine-runtime.js";
import { PCM_RING_HEADER_SCHEMA } from "./runtime/pcm-ring-buffer.js";

let runtime = null;
let initializing = false;

function post(message, transfer = []) {
  self.postMessage(message, transfer);
}

function requireRequestId(message) {
  if (message.requestId === undefined || message.requestId === null) {
    throw new TypeError(`${String(message.type)} requires requestId`);
  }
}

function admitRingSchema(schema) {
  if (schema === undefined) {
    return;
  }
  if (
    schema?.id !== PCM_RING_HEADER_SCHEMA.id ||
    schema?.headerBytes !== PCM_RING_HEADER_SCHEMA.headerBytes ||
    schema?.sampleEncoding !== PCM_RING_HEADER_SCHEMA.sampleEncoding
  ) {
    throw new Error("the UI and Worker PCM ring schemas do not match");
  }
  for (const [name, index] of Object.entries(
    PCM_RING_HEADER_SCHEMA.counters,
  )) {
    if (schema?.counters?.[name] !== index) {
      throw new Error(`the UI PCM ring counter ${name} has the wrong index`);
    }
  }
}

async function initialize(message) {
  if (runtime !== null || initializing) {
    throw new Error("the engine Worker is already initialized");
  }
  initializing = true;
  try {
    admitRingSchema(message.ringHeaderSchema);
    const moduleUrl = new URL(
      message.moduleUrl ?? "./engine-sim-offline.js",
      import.meta.url,
    );
    runtime = await BrowserEngineRuntime.create({
      moduleUrl,
      emit: post,
    });
    runtime.announceReady(message.requestId, moduleUrl.href);
  } finally {
    initializing = false;
  }
}

async function dispatch(message) {
  if (typeof message !== "object" || message === null) {
    throw new TypeError("Worker command must be an object");
  }
  if (typeof message.type !== "string") {
    throw new TypeError("Worker command requires a string type");
  }
  requireRequestId(message);
  if (message.type === "initialize") {
    await initialize(message);
    return;
  }
  if (runtime === null) {
    throw new Error("initialize the engine Worker before sending commands");
  }

  switch (message.type) {
    case "build":
      runtime.build(message);
      break;
    case "select-audio-bus":
      runtime.selectAudioBus(message);
      break;
    case "load-audio-package":
      await runtime.loadAudioPackage(message);
      break;
    case "set-comparison-mode":
      runtime.setComparisonMode(message);
      break;
    case "start":
      runtime.start(message);
      break;
    case "stop":
      runtime.stop(message);
      break;
    case "restart":
      runtime.restart(message);
      break;
    case "enqueue-controls":
      runtime.enqueueControls(message);
      break;
    case "export-wav":
      await runtime.exportWav(message);
      break;
    case "status":
      runtime.status(message);
      break;
    case "dispose":
      runtime.dispose(message);
      runtime = null;
      break;
    default:
      throw new Error(
        `unsupported Worker command ${message.type}; structural edits use build`,
      );
  }
}

self.addEventListener("message", (event) => {
  const requestId = event.data?.requestId ?? null;
  void dispatch(event.data).catch((error) => {
    post({
      type: "error",
      requestId,
      error: publicError(error),
    });
  });
});
