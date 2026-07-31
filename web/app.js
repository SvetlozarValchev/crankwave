import {
  ESO_CANONICAL_SAMPLE_RATE,
  ESO_C_API_VERSION,
} from "./runtime/c-api-abi.js";
import { WORKER_PROTOCOL_ID } from "./runtime/protocol.js";

const WORKER_URL = "/web/engine-worker.js";
const WORKLET_URL = "/web/audio-worklet.js";
const DEFAULT_PACKAGE_ID = "bmw-m52b28-free-rev";
const WORKBENCH_PACKAGES = Object.freeze([
  Object.freeze({
    id: DEFAULT_PACKAGE_ID,
    label: "BMW M52B28 · Interactive free rev",
    engineUrl: "/data/engines/bmw-m52b28/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52b28/scenarios/warm-running-free-rev-1500rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52b28-dyno",
    label: "BMW M52B28 · Inertial dyno 1500–6500 rpm",
    engineUrl: "/data/engines/bmw-m52b28/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52b28-dyno-lift-overrun",
    label: "BMW M52B28 · Dyno with lift and overrun",
    engineUrl: "/data/engines/bmw-m52b28/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm-lift-overrun.json",
  }),
  Object.freeze({
    id: "raspy-muscle-620-free-rev",
    label: "6.2L old-school V8 · Interactive free rev",
    engineUrl: "/data/engines/raspy-muscle-620-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/raspy-muscle-620-cleanroom/scenarios/warm-running-free-rev-800rpm.json",
  }),
  Object.freeze({
    id: "raspy-muscle-620-held-idle",
    label: "6.2L old-school V8 · Held idle 800 rpm",
    engineUrl: "/data/engines/raspy-muscle-620-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/raspy-muscle-620-cleanroom/scenarios/held-idle-region-800rpm.json",
  }),
  Object.freeze({
    id: "raspy-muscle-620-dyno",
    label: "6.2L old-school V8 · Inertial dyno 800–5900 rpm",
    engineUrl: "/data/engines/raspy-muscle-620-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/raspy-muscle-620-cleanroom/scenarios/inertial-dyno-800-5900rpm.json",
  }),
  Object.freeze({
    id: "sequoia-3ur-fe-free-rev",
    label: "Toyota Sequoia 3UR-FE · Interactive free rev",
    engineUrl: "/data/engines/sequoia-3ur-fe-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/sequoia-3ur-fe-cleanroom/scenarios/warm-running-free-rev-650rpm.json",
  }),
  Object.freeze({
    id: "sequoia-3ur-fe-held-idle",
    label: "Toyota Sequoia 3UR-FE · Held idle 650 rpm",
    engineUrl: "/data/engines/sequoia-3ur-fe-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/sequoia-3ur-fe-cleanroom/scenarios/held-idle-region-650rpm.json",
  }),
  Object.freeze({
    id: "sequoia-3ur-fe-dyno",
    label: "Toyota Sequoia 3UR-FE · Inertial dyno 650–6000 rpm",
    engineUrl: "/data/engines/sequoia-3ur-fe-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/sequoia-3ur-fe-cleanroom/scenarios/inertial-dyno-650-6000rpm.json",
  }),
  Object.freeze({
    id: "harley-evolution-1340-free-rev",
    label: "Harley-Davidson Evolution 1340 · Interactive free rev",
    engineUrl: "/data/engines/harley-evolution-1340-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/harley-evolution-1340-cleanroom/scenarios/warm-running-free-rev-800rpm.json",
  }),
  Object.freeze({
    id: "harley-evolution-1340-held-idle",
    label: "Harley-Davidson Evolution 1340 · Held idle 800 rpm",
    engineUrl: "/data/engines/harley-evolution-1340-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/harley-evolution-1340-cleanroom/scenarios/held-idle-region-800rpm.json",
  }),
  Object.freeze({
    id: "harley-evolution-1340-dyno",
    label: "Harley-Davidson Evolution 1340 · Inertial dyno 800–5000 rpm",
    engineUrl: "/data/engines/harley-evolution-1340-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/harley-evolution-1340-cleanroom/scenarios/inertial-dyno-800-5000rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-free-rev",
    label: "BMW M52TUB28 · Interactive free rev",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/warm-running-free-rev-700rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-cold-start",
    label: "BMW M52TUB28 · Cold crank and catch",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/cold-start-crank-catch-0rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-held-idle",
    label: "BMW M52TUB28 · Held idle 700 rpm",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/held-idle-region-700rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-dyno",
    label: "BMW M52TUB28 · Inertial dyno 700–6500 rpm",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/inertial-dyno-700-6500rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-held-dyno-pull-lift",
    label: "BMW M52TUB28 · Held-dyno pull, hold, lift and overrun",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/held-dyno-pull-lift-1500-6500rpm.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-launch-first-second",
    label: "BMW M52TUB28 · Vehicle launch and first-to-second shift",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-launch-first-second.json",
  }),
  Object.freeze({
    id: "bmw-m52tub28-fifth-gear-pull-lift",
    label: "BMW M52TUB28 · Fifth-gear pull and lift",
    engineUrl: "/data/engines/bmw-m52tub28-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-fifth-gear-pull-lift-1500rpm.json",
  }),
  Object.freeze({
    id: "honda-b18c5-held-below-vtec",
    label: "Honda B18C5 · Held below VTEC 5400 rpm",
    engineUrl: "/data/engines/honda-b18c5-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/honda-b18c5-cleanroom/scenarios/held-below-vtec-5400rpm.json",
  }),
  Object.freeze({
    id: "honda-b18c5-dyno",
    label: "Honda B18C5 · Inertial dyno 5000–8000 rpm",
    engineUrl: "/data/engines/honda-b18c5-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/honda-b18c5-cleanroom/scenarios/inertial-dyno-5000-8000rpm.json",
  }),
  Object.freeze({
    id: "honda-b18c5-held-above-vtec",
    label: "Honda B18C5 · Held above VTEC 7000 rpm",
    engineUrl: "/data/engines/honda-b18c5-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/honda-b18c5-cleanroom/scenarios/held-above-vtec-7000rpm.json",
  }),
  Object.freeze({
    id: "kohler-ch750-governed-load-step",
    label: "Kohler CH750 · Governed load step at 2740 rpm",
    engineUrl: "/data/engines/kohler-ch750-cleanroom/engine.json",
    scenarioUrl:
      "/data/engines/kohler-ch750-cleanroom/scenarios/governed-load-step-2740rpm.json",
  }),
]);

const RING_HEADER = Object.freeze({
  byteLength: 64,
  writeFrame: 0,
  readFrame: 1,
  availableFrames: 2,
  underrunFrames: 3,
  underrunEvents: 4,
  generation: 5,
  producerState: 6,
});
const RING_SCHEMA_ID = "engine-sim-offline/pcm-ring-spsc-v1";
const RING_HEADER_SCHEMA = Object.freeze({
  id: RING_SCHEMA_ID,
  headerBytes: RING_HEADER.byteLength,
  sampleEncoding: "float32-interleaved-native-endian",
  counters: Object.freeze({
    writeFrame: RING_HEADER.writeFrame,
    readFrame: RING_HEADER.readFrame,
    availableFrames: RING_HEADER.availableFrames,
    underrunFrames: RING_HEADER.underrunFrames,
    underrunEvents: RING_HEADER.underrunEvents,
    generation: RING_HEADER.generation,
    producerState: RING_HEADER.producerState,
  }),
});

const MAX_INSPECTOR_FIELDS = 2000;
const MAX_TRACE_POINTS = 720;
const JSON_INDENT = 2;
const QUANTITY_UNAVAILABLE_REASONS = Object.freeze([
  "Available",
  "Scenario not applicable",
  "Model not admitted",
  "Equivalent inertia missing",
  "Cycle integration not admitted",
  "Not settled",
  "Required input missing",
]);

const $ = (selector) => document.querySelector(selector);

const elements = {
  isolationGate: $("#isolation-gate"),
  isolationStatus: $("#isolation-status"),
  workerStatus: $("#worker-status"),
  buildStatus: $("#build-status"),
  packageSelect: $("#package-select"),
  loadPackageButton: $("#load-package-button"),
  buildButton: $("#build-button"),
  dirtyIndicator: $("#dirty-indicator"),
  documentTabs: [...document.querySelectorAll(".document-tab")],
  engineTab: $("#engine-tab"),
  scenarioTab: $("#scenario-tab"),
  engineErrorCount: $("#engine-error-count"),
  scenarioErrorCount: $("#scenario-error-count"),
  openDocumentButton: $("#open-document-button"),
  saveDocumentButton: $("#save-document-button"),
  formatDocumentButton: $("#format-document-button"),
  documentName: $("#document-name"),
  engineInput: $("#engine-file-input"),
  scenarioInput: $("#scenario-file-input"),
  engineEditor: $("#engine-editor"),
  scenarioEditor: $("#scenario-editor"),
  parseStatus: $("#parse-status"),
  cursorStatus: $("#cursor-status"),
  inspector: $(".inspector"),
  inspectorTree: $("#inspector-tree"),
  inspectorCount: $("#inspector-count"),
  addAssetsButton: $("#add-assets-button"),
  assetInput: $("#asset-file-input"),
  assetList: $("#asset-list"),
  assetRowTemplate: $("#asset-row-template"),
  sessionTitle: $("#session-title"),
  sessionSubtitle: $("#session-subtitle"),
  sessionState: $("#session-state"),
  motionModeBadge: $("#motion-mode-badge"),
  startButton: $("#start-button"),
  stopButton: $("#stop-button"),
  restartButton: $("#restart-button"),
  gestureNote: $("#gesture-note"),
  rpmValue: $("#rpm-value"),
  rpmMeter: $("#rpm-meter"),
  torqueValue: $("#torque-value"),
  torqueStatus: $("#torque-status"),
  powerValue: $("#power-value"),
  powerStatus: $("#power-status"),
  elapsedValue: $("#elapsed-value"),
  telemetryCanvas: $("#telemetry-canvas"),
  busSelect: $("#bus-select"),
  exportButton: $("#export-button"),
  controlsAdmission: $("#controls-admission"),
  throttleLabel: $("#throttle-label"),
  throttleInput: $("#throttle-input"),
  throttleOutput: $("#throttle-output"),
  throttleMinimumLabel: $("#throttle-minimum-label"),
  throttleMaximumLabel: $("#throttle-maximum-label"),
  starterButton: $("#starter-button"),
  starterLabel: $("#starter-label"),
  ignitionButton: $("#ignition-button"),
  ignitionLabel: $("#ignition-label"),
  fuelButton: $("#fuel-button"),
  fuelLabel: $("#fuel-label"),
  limiterButton: $("#limiter-button"),
  limiterLabel: $("#limiter-label"),
  externalResistanceControl: $("#external-resistance-control"),
  externalResistanceInput: $("#external-resistance-input"),
  heldDynoControls: $("#held-dyno-controls"),
  heldDynoTargetRpmInput: $("#held-dyno-target-rpm-input"),
  heldDynoAbsorbingTorqueInput: $("#held-dyno-absorbing-torque-input"),
  heldDynoDrivingTorqueInput: $("#held-dyno-driving-torque-input"),
  freeVehicleControls: $("#free-vehicle-controls"),
  vehicleGearSelect: $("#vehicle-gear-select"),
  vehicleClutchInput: $("#vehicle-clutch-input"),
  vehicleClutchOutput: $("#vehicle-clutch-output"),
  vehicleBrakeControl: $("#vehicle-brake-control"),
  vehicleBrakeInput: $("#vehicle-brake-input"),
  vehicleBrakeOutput: $("#vehicle-brake-output"),
  heldDynoTelemetry: $("#held-dyno-telemetry"),
  dynoTargetValue: $("#dyno-target-value"),
  dynoRequiredTorqueValue: $("#dyno-required-torque-value"),
  dynoAppliedTorqueValue: $("#dyno-applied-torque-value"),
  dynoLimitsValue: $("#dyno-limits-value"),
  dynoDispositionValue: $("#dyno-disposition-value"),
  freeVehicleTelemetry: $("#free-vehicle-telemetry"),
  vehicleSpeedValue: $("#vehicle-speed-value"),
  vehicleDistanceValue: $("#vehicle-distance-value"),
  vehicleGearValue: $("#vehicle-gear-value"),
  vehicleClutchValue: $("#vehicle-clutch-value"),
  vehicleSlipValue: $("#vehicle-slip-value"),
  vehicleRoadLoadValue: $("#vehicle-road-load-value"),
  vehicleDispositionValue: $("#vehicle-disposition-value"),
  realtimeHealth: $("#realtime-health"),
  ringFillValue: $("#ring-fill-value"),
  leadValue: $("#lead-value"),
  underrunValue: $("#underrun-value"),
  realtimeFactorValue: $("#realtime-factor-value"),
  ringFillMeter: $("#ring-fill-meter"),
  runtimeDetail: $("#runtime-detail"),
  clearDiagnosticsButton: $("#clear-diagnostics-button"),
  diagnosticsList: $("#diagnostics-list"),
  toastRegion: $("#toast-region"),
};

function newDocument(name) {
  return {
    name,
    text: "",
    parsed: null,
    parseError: null,
    sourceUrl: null,
    dirty: false,
  };
}

const state = {
  activeDocument: "engine",
  documents: {
    engine: newDocument("engine.json"),
    scenario: newDocument("scenario.json"),
  },
  requiredAssets: [],
  selectedAssets: new Map(),
  localDiagnostics: {
    engine: [],
    scenario: [],
  },
  workerDiagnostics: [],
  worker: null,
  workerReady: false,
  requestSequence: 0,
  requestKinds: new Map(),
  buildRequestId: null,
  buildMutationRequests: new Set(),
  buildThrottlePresentations: new Map(),
  mutationPriorStates: new Map(),
  built: null,
  sessionState: "idle",
  securityAdmitted: false,
  liveState: {
    throttle: 0.1,
    starter: false,
    ignition: true,
    fuel: true,
    limiter: false,
    "external-resisting-torque": 0,
    "held-dyno-target-engine-speed": 0,
    "held-dyno-maximum-absorbing-torque": 0,
    "held-dyno-maximum-driving-torque": 0,
    "vehicle-selected-forward-gear": 0,
    "vehicle-clutch-engagement": 0,
    "vehicle-service-brake-application": 0,
  },
  editingControls: new Set(),
  starterInputHeld: false,
  pendingControls: new Map(),
  lastAcceptedControlFrame: null,
  audio: {
    context: null,
    workletLoaded: false,
    node: null,
    ring: null,
    initialLeadFrames: 0,
  },
  runtimeStats: null,
  telemetry: null,
  trace: [],
  traceDrawRequested: false,
};

function setChip(element, text, status = "") {
  const dot = element.querySelector(".status-dot");
  element.textContent = "";
  if (dot) {
    element.append(dot);
  } else {
    const nextDot = document.createElement("span");
    nextDot.className = "status-dot";
    element.append(nextDot);
  }
  element.append(document.createTextNode(text));
  if (status) {
    element.dataset.status = status;
  } else {
    delete element.dataset.status;
  }
}

function showToast(message, isError = false) {
  const toast = document.createElement("div");
  toast.className = `toast${isError ? " is-error" : ""}`;
  toast.textContent = message;
  elements.toastRegion.append(toast);
  window.setTimeout(() => toast.remove(), 4400);
}

function nextRequestId() {
  state.requestSequence += 1;
  return state.requestSequence;
}

function postWorker(message, transfer = []) {
  if (!state.worker || !state.workerReady) {
    throw new Error("The browser runtime is not ready.");
  }
  state.requestKinds.set(message.requestId, message.type);
  state.worker.postMessage(message, transfer);
}

function activeDocument() {
  return state.documents[state.activeDocument];
}

function activeEditor() {
  return state.activeDocument === "engine"
    ? elements.engineEditor
    : elements.scenarioEditor;
}

function encodePointerSegment(segment) {
  return String(segment).replaceAll("~", "~0").replaceAll("/", "~1");
}

function pointerFor(path) {
  return path.length === 0
    ? ""
    : `/${path.map(encodePointerSegment).join("/")}`;
}

function parsePosition(error, source) {
  const match = /position\s+(\d+)/i.exec(error.message);
  if (!match) {
    return { line: null, column: null };
  }
  const offset = Number(match[1]);
  const prefix = source.slice(0, offset);
  const lines = prefix.split("\n");
  return { line: lines.length, column: lines.at(-1).length + 1 };
}

function parseDocument(kind, render = true) {
  const documentState = state.documents[kind];
  const editor =
    kind === "engine" ? elements.engineEditor : elements.scenarioEditor;
  documentState.text = editor.value;
  state.localDiagnostics[kind] = [];

  if (documentState.text.trim() === "") {
    documentState.parsed = null;
    documentState.parseError = "Document is empty.";
    state.localDiagnostics[kind].push({
      document: kind,
      path: "",
      severity: "error",
      code: "malformed_document",
      message: "Document is empty.",
    });
  } else {
    try {
      documentState.parsed = JSON.parse(documentState.text);
      documentState.parseError = null;
    } catch (error) {
      const position = parsePosition(error, documentState.text);
      documentState.parsed = null;
      documentState.parseError = error.message;
      state.localDiagnostics[kind].push({
        document: kind,
        path: "",
        severity: "error",
        code: "malformed_document",
        message: error.message,
        line: position.line,
        column: position.column,
      });
    }
  }

  editor.classList.toggle("has-error", Boolean(documentState.parseError));
  if (kind === "engine") {
    discoverAssets();
  }
  if (render && kind === state.activeDocument) {
    renderParseState();
    renderInspector();
  }
  renderDiagnostics();
  renderDirtyState();
  return documentState.parsed;
}

function setDocument(kind, text, options = {}) {
  const documentState = state.documents[kind];
  documentState.text = text;
  documentState.name =
    options.name ?? (kind === "engine" ? "engine.json" : "scenario.json");
  documentState.sourceUrl = options.sourceUrl ?? null;
  documentState.dirty = options.dirty ?? false;
  const editor =
    kind === "engine" ? elements.engineEditor : elements.scenarioEditor;
  editor.value = text;
  parseDocument(kind, kind === state.activeDocument);
  renderDocumentChrome();
}

function switchDocument(kind) {
  if (kind === state.activeDocument) {
    return;
  }
  state.activeDocument = kind;
  for (const tab of elements.documentTabs) {
    const selected = tab.dataset.document === kind;
    tab.classList.toggle("is-active", selected);
    tab.setAttribute("aria-selected", String(selected));
  }
  elements.engineEditor.hidden = kind !== "engine";
  elements.scenarioEditor.hidden = kind !== "scenario";
  renderDocumentChrome();
  renderParseState();
  renderInspector();
  updateCursorStatus();
}

function renderDocumentChrome() {
  elements.documentName.textContent = activeDocument().name;
  renderDirtyState();
}

function renderDirtyState() {
  const dirty =
    state.documents.engine.dirty || state.documents.scenario.dirty;
  elements.dirtyIndicator.hidden = !dirty;
}

function renderParseState() {
  const documentState = activeDocument();
  elements.parseStatus.className = "";
  if (documentState.parseError) {
    elements.parseStatus.textContent = `Invalid JSON · ${documentState.parseError}`;
    elements.parseStatus.classList.add("is-error");
  } else if (documentState.parsed) {
    elements.parseStatus.textContent = "Valid JSON";
    elements.parseStatus.classList.add("is-valid");
  } else {
    elements.parseStatus.textContent = "Waiting for a document";
  }
}

function updateCursorStatus() {
  const editor = activeEditor();
  const prefix = editor.value.slice(0, editor.selectionStart);
  const lines = prefix.split("\n");
  elements.cursorStatus.textContent = `Ln ${lines.length}, Col ${
    lines.at(-1).length + 1
  }`;
}

function markDocumentEdited(kind) {
  const documentState = state.documents[kind];
  documentState.dirty = true;
  window.clearTimeout(documentState.parseTimer);
  documentState.parseTimer = window.setTimeout(() => parseDocument(kind), 180);
  renderDirtyState();
}

function setAtPath(root, path, value) {
  let cursor = root;
  for (let index = 0; index < path.length - 1; index += 1) {
    cursor = cursor[path[index]];
  }
  cursor[path.at(-1)] = value;
}

function createScalarField(root, path, label, value) {
  const row = document.createElement("label");
  row.className = "scalar-field";
  row.dataset.jsonPointer = pointerFor(path);

  const name = document.createElement("span");
  name.textContent = label;
  name.title = pointerFor(path) || "/";
  row.append(name);

  const input = document.createElement("input");
  if (typeof value === "boolean") {
    input.type = "checkbox";
    input.checked = value;
  } else if (typeof value === "number") {
    input.type = "number";
    input.step = "any";
    input.value = String(value);
  } else if (value === null) {
    input.type = "text";
    input.value = "null";
    input.disabled = true;
  } else {
    input.type = "text";
    input.value = String(value);
  }

  input.addEventListener("change", () => {
    let nextValue;
    if (typeof value === "boolean") {
      nextValue = input.checked;
    } else if (typeof value === "number") {
      nextValue = Number(input.value);
      if (!Number.isFinite(nextValue)) {
        showToast(`${pointerFor(path)} must remain a finite number.`, true);
        input.value = String(value);
        return;
      }
    } else {
      nextValue = input.value;
    }
    setAtPath(root, path, nextValue);
    const documentState = activeDocument();
    documentState.text = `${JSON.stringify(root, null, JSON_INDENT)}\n`;
    documentState.dirty = true;
    activeEditor().value = documentState.text;
    parseDocument(state.activeDocument, false);
    renderDocumentChrome();
  });
  row.append(input);
  return row;
}

function createTreeGroup(root, value, path, label, depth, budget) {
  const details = document.createElement("details");
  details.className = "tree-group";
  details.open = depth < 2;

  const summary = document.createElement("summary");
  const count = Array.isArray(value)
    ? `${value.length} items`
    : `${Object.keys(value).length} properties`;
  summary.textContent = `${label} · ${count}`;
  details.append(summary);

  const children = document.createElement("div");
  children.className = "tree-fields";
  const entries = Array.isArray(value)
    ? value.map((entry, index) => [index, entry])
    : Object.entries(value);

  for (const [key, child] of entries) {
    if (budget.count >= MAX_INSPECTOR_FIELDS) {
      break;
    }
    const childPath = [...path, key];
    if (child !== null && typeof child === "object") {
      children.append(
        createTreeGroup(root, child, childPath, String(key), depth + 1, budget),
      );
    } else {
      children.append(
        createScalarField(root, childPath, String(key), child),
      );
      budget.count += 1;
    }
  }
  details.append(children);
  return details;
}

function renderInspector() {
  elements.inspectorTree.textContent = "";
  const parsed = activeDocument().parsed;
  if (parsed === null || typeof parsed !== "object") {
    const empty = document.createElement("p");
    empty.className = "empty-state";
    empty.textContent = "Load a valid JSON document to inspect it.";
    elements.inspectorTree.append(empty);
    elements.inspectorCount.textContent = "0 fields";
    return;
  }

  const budget = { count: 0 };
  const rootLabel = Array.isArray(parsed) ? "root array" : "root object";
  elements.inspectorTree.append(
    createTreeGroup(parsed, parsed, [], rootLabel, 0, budget),
  );
  elements.inspectorCount.textContent = `${budget.count} fields`;
  if (budget.count === MAX_INSPECTOR_FIELDS) {
    const notice = document.createElement("p");
    notice.className = "empty-state";
    notice.textContent = `Inspector capped at ${MAX_INSPECTOR_FIELDS} scalar fields. The JSON editor remains complete.`;
    elements.inspectorTree.append(notice);
  }
}

function inferAssetKind(candidate, path) {
  if (
    candidate.kind === "impulse_response" ||
    /\.(wav|wave|aif|aiff)$/i.test(candidate.uri)
  ) {
    return "audio";
  }
  if (
    candidate.kind === "accessory_configuration" ||
    path.some((part) => String(part).includes("accessory"))
  ) {
    return "accessory-configuration";
  }
  return null;
}

function discoverAssets() {
  const root = state.documents.engine.parsed;
  const sourceUrl = state.documents.engine.sourceUrl;
  const discovered = [];
  const seen = new Set();

  function visit(value, path) {
    if (value === null || typeof value !== "object") {
      return;
    }
    if (
      typeof value.id === "string" &&
      typeof value.uri === "string" &&
      value.id.length > 0
    ) {
      const kind = inferAssetKind(value, path);
      if (kind && !seen.has(value.id)) {
        seen.add(value.id);
        let resolvedUrl = null;
        if (sourceUrl) {
          try {
            resolvedUrl = new URL(value.uri, sourceUrl).href;
          } catch {
            resolvedUrl = null;
          }
        }
        discovered.push({
          id: value.id,
          kind,
          uri: value.uri,
          sha256: typeof value.sha256 === "string" ? value.sha256 : null,
          path: pointerFor(path),
          resolvedUrl,
        });
      }
    }
    for (const [key, child] of Object.entries(value)) {
      visit(child, [...path, key]);
    }
  }

  visit(root, []);
  state.requiredAssets = discovered;
  const requiredIds = new Set(discovered.map((asset) => asset.id));
  for (const id of state.selectedAssets.keys()) {
    if (!requiredIds.has(id)) {
      state.selectedAssets.delete(id);
    }
  }
  renderAssets();
}

function renderAssets() {
  elements.assetList.textContent = "";
  if (state.requiredAssets.length === 0) {
    const empty = document.createElement("p");
    empty.className = "empty-state";
    empty.textContent = "No external assets discovered.";
    elements.assetList.append(empty);
    return;
  }

  for (const requirement of state.requiredAssets) {
    const row = elements.assetRowTemplate.content.firstElementChild.cloneNode(true);
    const selected = state.selectedAssets.get(requirement.id);
    row.querySelector(".asset-kind-icon").textContent =
      requirement.kind === "audio" ? "W" : "J";
    row.querySelector(".asset-id").textContent = requirement.id;
    row.querySelector(".asset-path").textContent = selected
      ? selected.file.name
      : requirement.uri;
    row.querySelector(".asset-kind").textContent =
      requirement.kind === "audio" ? "AUDIO" : "CONFIG";
    const status = row.querySelector(".asset-state");
    if (selected) {
      status.textContent = "selected";
      status.classList.add("is-ready");
    } else if (requirement.resolvedUrl) {
      status.textContent = "repository";
      status.classList.add("is-ready");
    } else {
      status.textContent = "required";
    }
    const choose = row.querySelector(".asset-choose");
    choose.textContent = selected ? "Replace" : "Choose";
    choose.addEventListener("click", () => {
      elements.assetInput.dataset.targetId = requirement.id;
      elements.assetInput.multiple = false;
      elements.assetInput.click();
    });
    elements.assetList.append(row);
  }
}

function selectAssetFiles(files, targetId = null) {
  if (targetId && files[0]) {
    state.selectedAssets.set(targetId, { file: files[0] });
    renderAssets();
    return;
  }

  const unmatchedRequirements = state.requiredAssets.filter(
    (requirement) => !state.selectedAssets.has(requirement.id),
  );
  for (const file of files) {
    const exact = state.requiredAssets.find((requirement) => {
      const basename = requirement.uri.split("/").at(-1);
      return basename === file.name || requirement.id === file.name;
    });
    const fallback = unmatchedRequirements.shift();
    const requirement = exact ?? fallback;
    if (requirement) {
      state.selectedAssets.set(requirement.id, { file });
    } else {
      showToast(`No unresolved JSON asset references ${file.name}.`, true);
    }
  }
  renderAssets();
}

async function materializeAssets() {
  const payloads = [];
  for (const requirement of state.requiredAssets) {
    const selected = state.selectedAssets.get(requirement.id);
    let bytes;
    if (selected) {
      bytes = await selected.file.arrayBuffer();
    } else if (requirement.resolvedUrl) {
      const response = await fetch(requirement.resolvedUrl, {
        cache: "no-store",
      });
      if (!response.ok) {
        throw new Error(
          `${requirement.id}: ${response.status} while loading ${requirement.uri}`,
        );
      }
      bytes = await response.arrayBuffer();
    } else {
      throw new Error(
        `${requirement.id}: select the referenced ${requirement.kind} asset.`,
      );
    }
    payloads.push({
      id: requirement.id,
      kind: requirement.kind,
      bytes,
    });
  }
  return payloads;
}

function renderDiagnostics() {
  const diagnostics = [
    ...state.localDiagnostics.engine,
    ...state.localDiagnostics.scenario,
    ...state.workerDiagnostics,
  ];
  const counts = { engine: 0, scenario: 0 };
  for (const diagnostic of diagnostics) {
    if (
      diagnostic.severity === "error" &&
      Object.hasOwn(counts, diagnostic.document)
    ) {
      counts[diagnostic.document] += 1;
    }
  }
  setTabCount(elements.engineErrorCount, counts.engine);
  setTabCount(elements.scenarioErrorCount, counts.scenario);

  elements.diagnosticsList.textContent = "";
  if (diagnostics.length === 0) {
    const empty = document.createElement("div");
    empty.className = "diagnostic-empty";
    const mark = document.createElement("span");
    mark.setAttribute("aria-hidden", "true");
    mark.textContent = "✓";
    empty.append(mark, document.createTextNode("No diagnostics reported."));
    elements.diagnosticsList.append(empty);
    return;
  }

  for (const diagnostic of diagnostics) {
    const row = document.createElement("article");
    row.className = "diagnostic";
    row.dataset.severity = diagnostic.severity;

    const severity = document.createElement("span");
    severity.className = "diagnostic-severity";
    severity.title = diagnostic.severity;
    row.append(severity);

    const content = document.createElement("div");
    const message = document.createElement("p");
    message.className = "diagnostic-message";
    message.textContent = diagnostic.message;
    content.append(message);

    const meta = document.createElement("div");
    meta.className = "diagnostic-meta";
    if (diagnostic.document) {
      const documentLabel = document.createElement("span");
      documentLabel.textContent = diagnostic.document;
      meta.append(documentLabel);
    }
    if (diagnostic.path !== undefined) {
      const pathButton = document.createElement("button");
      pathButton.className = "diagnostic-path";
      pathButton.type = "button";
      pathButton.textContent = diagnostic.path || "/";
      pathButton.title = diagnostic.path || "/";
      pathButton.addEventListener("click", () => revealDiagnostic(diagnostic));
      meta.append(pathButton);
    }
    if (diagnostic.line) {
      const location = document.createElement("span");
      location.textContent = `Ln ${diagnostic.line}, Col ${
        diagnostic.column ?? 1
      }`;
      meta.append(location);
    }
    content.append(meta);
    row.append(content);

    const code = document.createElement("span");
    code.className = "diagnostic-code";
    code.textContent = diagnostic.code ?? "runtime";
    row.append(code);
    elements.diagnosticsList.append(row);
  }
}

function setTabCount(element, count) {
  element.textContent = String(count);
  element.hidden = count === 0;
}

function revealDiagnostic(diagnostic) {
  if (
    diagnostic.document === "engine" ||
    diagnostic.document === "scenario"
  ) {
    switchDocument(diagnostic.document);
  }
  elements.inspector.open = true;
  const fields = elements.inspectorTree.querySelectorAll("[data-json-pointer]");
  const target = [...fields].find(
    (field) => field.dataset.jsonPointer === (diagnostic.path ?? ""),
  );
  if (!target) {
    activeEditor().focus();
    return;
  }
  for (
    let ancestor = target.parentElement;
    ancestor && ancestor !== elements.inspectorTree;
    ancestor = ancestor.parentElement
  ) {
    if (ancestor instanceof HTMLDetailsElement) {
      ancestor.open = true;
    }
  }
  elements.inspectorTree
    .querySelectorAll(".is-targeted")
    .forEach((field) => field.classList.remove("is-targeted"));
  target.classList.add("is-targeted");
  target.scrollIntoView({ block: "center", behavior: "smooth" });
}

function packageById(id) {
  return WORKBENCH_PACKAGES.find((candidate) => candidate.id === id) ?? null;
}

function populatePackageSelect() {
  elements.packageSelect.textContent = "";
  const groups = new Map();
  for (const packageDefinition of WORKBENCH_PACKAGES) {
    const separator = packageDefinition.label.indexOf(" · ");
    const groupLabel =
      separator < 0
        ? "Repository procedures"
        : packageDefinition.label.slice(0, separator);
    const procedureLabel =
      separator < 0
        ? packageDefinition.label
        : packageDefinition.label.slice(separator + 3);
    let group = groups.get(groupLabel);
    if (!group) {
      group = document.createElement("optgroup");
      group.label = groupLabel;
      groups.set(groupLabel, group);
      elements.packageSelect.append(group);
    }
    const option = document.createElement("option");
    option.value = packageDefinition.id;
    option.textContent = procedureLabel;
    group.append(option);
  }
  elements.packageSelect.value = DEFAULT_PACKAGE_ID;
}

function repositoryDocumentName(url, kind) {
  const marker = kind === "engine" ? "/data/engines/" : "/scenarios/";
  const path = new URL(url, location.href).pathname;
  const markerIndex = path.indexOf(marker);
  return markerIndex >= 0
    ? path.slice(markerIndex + marker.length)
    : path.split("/").at(-1);
}

async function loadPackage(packageId, { quiet = false } = {}) {
  const packageDefinition = packageById(packageId);
  if (!packageDefinition) {
    showToast(`Unknown workbench package: ${packageId}`, true);
    return;
  }

  elements.packageSelect.disabled = true;
  elements.loadPackageButton.disabled = true;
  try {
    const [engineResponse, scenarioResponse] = await Promise.all([
      fetch(packageDefinition.engineUrl, { cache: "no-store" }),
      fetch(packageDefinition.scenarioUrl, { cache: "no-store" }),
    ]);
    if (!engineResponse.ok || !scenarioResponse.ok) {
      throw new Error(
        `${packageDefinition.label}: repository JSON could not be fetched.`,
      );
    }
    const [engineText, scenarioText] = await Promise.all([
      engineResponse.text(),
      scenarioResponse.text(),
    ]);
    state.selectedAssets.clear();
    setDocument("engine", engineText, {
      name: repositoryDocumentName(packageDefinition.engineUrl, "engine"),
      sourceUrl: new URL(packageDefinition.engineUrl, location.href).href,
    });
    setDocument("scenario", scenarioText, {
      name: repositoryDocumentName(packageDefinition.scenarioUrl, "scenario"),
      sourceUrl: new URL(packageDefinition.scenarioUrl, location.href).href,
    });
    elements.packageSelect.value = packageDefinition.id;
    switchDocument("engine");
    if (!quiet) {
      showToast(`${packageDefinition.label} loaded.`);
    }
  } catch (error) {
    showToast(error.message, true);
  } finally {
    elements.packageSelect.disabled = false;
    elements.loadPackageButton.disabled = false;
  }
}

async function openDocumentFile(kind, file) {
  try {
    const text = await file.text();
    setDocument(kind, text, { name: file.name, sourceUrl: null });
    switchDocument(kind);
  } catch (error) {
    showToast(`Could not read ${file.name}: ${error.message}`, true);
  }
}

function saveActiveDocument() {
  const documentState = activeDocument();
  const blob = new Blob([activeEditor().value], {
    type: "application/json;charset=utf-8",
  });
  downloadBlob(blob, documentState.name || `${state.activeDocument}.json`);
}

function formatActiveDocument() {
  const parsed = parseDocument(state.activeDocument, false);
  if (!parsed) {
    renderParseState();
    showToast("Fix the JSON syntax before formatting.", true);
    return;
  }
  const documentState = activeDocument();
  documentState.text = `${JSON.stringify(parsed, null, JSON_INDENT)}\n`;
  documentState.dirty = true;
  activeEditor().value = documentState.text;
  renderParseState();
  renderInspector();
  renderDirtyState();
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function resetRunPresentation() {
  state.telemetry = null;
  state.runtimeStats = null;
  state.trace.length = 0;
  detachAudioNode();
  updateBuiltControls();
  renderTelemetry();
  renderRuntimeStats();
  scheduleTraceDraw();
}

function torqueNm(quantity) {
  if (!Number.isFinite(quantity?.value)) {
    return Number.NaN;
  }
  if (quantity.unit === "N*m") {
    return quantity.value;
  }
  if (quantity.unit === "lb*ft") {
    return quantity.value * 1.3558179483314004;
  }
  return Number.NaN;
}

function authoredLiveState() {
  const scenario = state.documents.scenario.parsed;
  const mode = scenario?.mode;
  const authoredThrottle =
    mode?.throttle_01?.points?.find(
      (point) => point?.time?.value === 0,
    )?.value ?? mode?.throttle_01?.points?.[0]?.value;
  const authoredResistancePoint =
    mode?.external_resisting_torque?.points?.find(
      (point) => point?.time?.value === 0,
    ) ?? mode?.external_resisting_torque?.points?.[0];
  const authoredResistance = torqueNm(authoredResistancePoint?.value);
  const authoredDynoTarget = angularSpeedRpm(
    mode?.target_engine_speed?.points?.[0]?.value,
  );
  const authoredAbsorbingLimit = torqueNm(mode?.maximum_absorbing_torque);
  const authoredDrivingLimit = torqueNm(mode?.maximum_driving_torque);
  const authoredGear = (state.built?.descriptor.forwardGears ?? []).find(
    (gear) => gear.semanticId === mode?.initial_gear,
  );
  return {
    throttle:
      Number.isFinite(authoredThrottle) &&
      authoredThrottle >= 0 &&
      authoredThrottle <= 1
        ? authoredThrottle
        : 0,
    ignition: scenario?.initial_state?.ignition_enabled === true,
    fuel: scenario?.initial_state?.fuel_enabled === true,
    starter: scenario?.initial_state?.starter_enabled === true,
    limiter: scenario?.initial_state?.limiter_enabled === true,
    "external-resisting-torque":
      Number.isFinite(authoredResistance) && authoredResistance >= 0
        ? authoredResistance
        : 0,
    "held-dyno-target-engine-speed":
      Number.isFinite(authoredDynoTarget) && authoredDynoTarget > 0
        ? authoredDynoTarget
        : angularSpeedRpm(scenario?.initial_state?.engine_speed) || 0,
    "held-dyno-maximum-absorbing-torque":
      Number.isFinite(authoredAbsorbingLimit) && authoredAbsorbingLimit >= 0
        ? authoredAbsorbingLimit
        : 0,
    "held-dyno-maximum-driving-torque":
      Number.isFinite(authoredDrivingLimit) && authoredDrivingLimit >= 0
        ? authoredDrivingLimit
        : 0,
    "vehicle-selected-forward-gear": authoredGear?.authoredOrdinal ?? 0,
    "vehicle-clutch-engagement": Number.isFinite(
      mode?.initial_clutch_engagement_01,
    )
      ? mode.initial_clutch_engagement_01
      : 0,
    "vehicle-service-brake-application": Number.isFinite(
      mode?.initial_service_brake_application_01,
    )
      ? mode.initial_service_brake_application_01
      : 0,
  };
}

function angularSpeedRpm(quantity) {
  if (!Number.isFinite(quantity?.value)) {
    return Number.NaN;
  }
  if (quantity.unit === "rpm") {
    return quantity.value;
  }
  if (quantity.unit === "rad/s") {
    return (quantity.value * 60) / (2 * Math.PI);
  }
  return Number.NaN;
}

function throttlePresentation(engineDocument) {
  const engine = engineDocument?.engine;
  const selectedId = engine?.throttle_controller;
  const controllers = Array.isArray(engine?.throttle_controllers)
    ? engine.throttle_controllers
    : [];
  const selected = controllers.find(
    (controller) => controller?.id === selectedId,
  );
  if (selected?.type !== "governor") {
    return Object.freeze({ type: "direct" });
  }

  const minimumRpm = angularSpeedRpm(selected.minimum_engine_speed);
  const maximumRpm = angularSpeedRpm(selected.maximum_engine_speed);
  if (
    !Number.isFinite(minimumRpm) ||
    !Number.isFinite(maximumRpm) ||
    minimumRpm < 0 ||
    maximumRpm < minimumRpm
  ) {
    return Object.freeze({ type: "direct" });
  }
  return Object.freeze({ type: "governor", minimumRpm, maximumRpm });
}

function activeThrottlePresentation() {
  return (
    state.built?.throttlePresentation ??
    throttlePresentation(state.documents.engine.parsed)
  );
}

function formatRpm(value) {
  return `${Math.round(value).toLocaleString()} rpm`;
}

function renderThrottleControl(value) {
  const presentation = activeThrottlePresentation();
  if (presentation.type === "governor") {
    const setpointRpm =
      presentation.minimumRpm +
      value * (presentation.maximumRpm - presentation.minimumRpm);
    elements.throttleLabel.textContent = "Governor setpoint";
    elements.throttleOutput.value = formatRpm(setpointRpm);
    elements.throttleMinimumLabel.textContent = formatRpm(
      presentation.minimumRpm,
    );
    elements.throttleMaximumLabel.textContent = formatRpm(
      presentation.maximumRpm,
    );
    return;
  }

  elements.throttleLabel.textContent = "Throttle";
  elements.throttleOutput.value = `${Math.round(value * 100)}%`;
  elements.throttleMinimumLabel.textContent = "Closed";
  elements.throttleMaximumLabel.textContent = "Wide open";
}

function renderLiveState() {
  elements.throttleInput.value = String(
    Math.round(state.liveState.throttle * 100),
  );
  renderThrottleControl(state.liveState.throttle);
  elements.starterButton.setAttribute(
    "aria-pressed",
    String(state.liveState.starter),
  );
  elements.starterLabel.textContent = state.liveState.starter
    ? "Cranking"
    : "Hold to crank";
  elements.ignitionButton.setAttribute(
    "aria-checked",
    String(state.liveState.ignition),
  );
  elements.ignitionLabel.textContent = state.liveState.ignition ? "On" : "Off";
  elements.fuelButton.setAttribute(
    "aria-checked",
    String(state.liveState.fuel),
  );
  elements.fuelLabel.textContent = state.liveState.fuel ? "On" : "Off";
  elements.limiterButton.setAttribute(
    "aria-checked",
    String(state.liveState.limiter),
  );
  elements.limiterLabel.textContent = state.liveState.limiter ? "On" : "Off";
  elements.externalResistanceInput.value = String(
    state.liveState["external-resisting-torque"],
  );
  elements.heldDynoTargetRpmInput.value = String(
    Math.round(state.liveState["held-dyno-target-engine-speed"]),
  );
  elements.heldDynoAbsorbingTorqueInput.value = String(
    Math.round(state.liveState["held-dyno-maximum-absorbing-torque"]),
  );
  elements.heldDynoDrivingTorqueInput.value = String(
    Math.round(state.liveState["held-dyno-maximum-driving-torque"]),
  );
  elements.vehicleGearSelect.value = String(
    state.liveState["vehicle-selected-forward-gear"],
  );
  elements.vehicleClutchInput.value = String(
    Math.round(state.liveState["vehicle-clutch-engagement"] * 100),
  );
  elements.vehicleClutchOutput.value = `${Math.round(
    state.liveState["vehicle-clutch-engagement"] * 100,
  )}%`;
  elements.vehicleBrakeInput.value = String(
    Math.round(state.liveState["vehicle-service-brake-application"] * 100),
  );
  elements.vehicleBrakeOutput.value = `${Math.round(
    state.liveState["vehicle-service-brake-application"] * 100,
  )}%`;
}

function resetLiveControls() {
  state.lastAcceptedControlFrame = null;
  state.pendingControls.clear();
  state.editingControls.clear();
  state.starterInputHeld = false;
  state.liveState = authoredLiveState();
  renderLiveState();
}

async function buildSession() {
  parseDocument("engine", false);
  parseDocument("scenario", false);
  renderDiagnostics();
  if (
    state.localDiagnostics.engine.length > 0 ||
    state.localDiagnostics.scenario.length > 0
  ) {
    showToast("Fix JSON syntax errors before applying the package.", true);
    return;
  }
  if (!state.workerReady) {
    showToast("The WASM runtime is not ready.", true);
    return;
  }
  if (["preparing", "running"].includes(state.sessionState)) {
    showToast("Stop playback before applying structural JSON edits.", true);
    return;
  }

  elements.buildButton.disabled = true;
  state.workerDiagnostics = [];
  const priorState = state.sessionState;
  setSessionState("building");
  setChip(elements.buildStatus, "Building", "busy");

  try {
    const assets = await materializeAssets();
    const requestId = nextRequestId();
    state.buildRequestId = requestId;
    state.buildMutationRequests.add(requestId);
    state.buildThrottlePresentations.set(
      requestId,
      throttlePresentation(state.documents.engine.parsed),
    );
    state.mutationPriorStates.set(requestId, priorState);
    const transfer = assets.map((asset) => asset.bytes);
    postWorker(
      {
        type: "build",
        requestId,
        engineJson: state.documents.engine.text,
        scenarioJson: state.documents.scenario.text,
        assets,
      },
      transfer,
    );
  } catch (error) {
    setSessionState(state.built ? priorState : "failed");
    setChip(elements.buildStatus, "Build failed", "bad");
    state.workerDiagnostics = [
      {
        document: "engine",
        path: "",
        severity: "error",
        code: "asset_load_failed",
        message: error.message,
      },
    ];
    renderDiagnostics();
    elements.buildButton.disabled = false;
  }
}

function setSessionState(nextState, detail = "") {
  state.sessionState = nextState;
  elements.sessionState.dataset.state =
    nextState === "failed" ? "error" : nextState;
  elements.sessionState.textContent =
    nextState.charAt(0).toUpperCase() + nextState.slice(1);
  if (detail) {
    elements.sessionSubtitle.textContent = detail;
  }
  updateBuiltControls();
}

function normalizeCapabilities(descriptor) {
  return Object.fromEntries(
    (descriptor.controls ?? []).map((capability) => [capability.kind, true]),
  );
}

function readableToken(value) {
  return String(value ?? "")
    .split("-")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

function forwardGearLabel(ordinal) {
  if (ordinal === null || ordinal === 0) {
    return "Neutral";
  }
  const gear = state.built?.descriptor.forwardGears?.find(
    (candidate) => candidate.authoredOrdinal === ordinal,
  );
  return gear
    ? `${gear.authoredOrdinal} · ${gear.semanticId} · ${gear.ratio.toFixed(2)}:1`
    : `Gear ${ordinal}`;
}

function renderOperatingDescriptor() {
  const descriptor = state.built?.descriptor;
  const capabilities = state.built?.capabilities ?? {};
  const motionMode = descriptor?.motionMode ?? null;
  elements.motionModeBadge.hidden = motionMode === null;
  elements.motionModeBadge.textContent = motionMode
    ? readableToken(motionMode)
    : "No motion mode";

  elements.externalResistanceControl.hidden =
    !capabilities["external-resisting-torque"];
  elements.heldDynoControls.hidden =
    !capabilities["held-dyno-target-engine-speed"] &&
    !capabilities["held-dyno-maximum-absorbing-torque"] &&
    !capabilities["held-dyno-maximum-driving-torque"];
  elements.freeVehicleControls.hidden =
    !capabilities["vehicle-selected-forward-gear"] &&
    !capabilities["vehicle-clutch-engagement"] &&
    !capabilities["vehicle-service-brake-application"];
  elements.vehicleBrakeControl.hidden =
    !capabilities["vehicle-service-brake-application"];
  elements.heldDynoTelemetry.hidden = motionMode !== "held-dyno";
  elements.freeVehicleTelemetry.hidden = motionMode !== "free-vehicle";

  elements.vehicleGearSelect.textContent = "";
  const neutral = document.createElement("option");
  neutral.value = "0";
  neutral.textContent = "Neutral";
  elements.vehicleGearSelect.append(neutral);
  for (const gear of descriptor?.forwardGears ?? []) {
    const option = document.createElement("option");
    option.value = String(gear.authoredOrdinal);
    option.textContent = forwardGearLabel(gear.authoredOrdinal);
    elements.vehicleGearSelect.append(option);
  }
}

function acceptBuilt(message) {
  if (!state.buildMutationRequests.has(message.requestId)) {
    return;
  }
  const isInitialBuild = message.requestId === state.buildRequestId;
  const compiledThrottlePresentation =
    state.buildThrottlePresentations.get(message.requestId) ??
    state.built?.throttlePresentation ??
    throttlePresentation(state.documents.engine.parsed);
  state.buildMutationRequests.delete(message.requestId);
  state.buildThrottlePresentations.delete(message.requestId);
  state.mutationPriorStates.delete(message.requestId);
  state.requestKinds.delete(message.requestId);
  state.built = {
    engineId: message.engineId,
    scenarioId: message.scenarioId,
    descriptor: message.descriptor,
    capabilities: normalizeCapabilities(message.descriptor),
    throttlePresentation: compiledThrottlePresentation,
  };
  renderOperatingDescriptor();
  resetRunPresentation();
  resetLiveControls();
  if (isInitialBuild) {
    state.documents.engine.dirty = false;
    state.documents.scenario.dirty = false;
  }
  state.workerDiagnostics = [];
  elements.buildButton.disabled = false;
  elements.sessionTitle.textContent = message.engineId;
  elements.sessionSubtitle.textContent =
    message.descriptor.executionKind === "open-ended"
      ? `${message.scenarioId} · interactive bench`
      : `${message.scenarioId} · finite authored run`;
  setSessionState("ready");
  setChip(elements.buildStatus, "Build admitted", "good");
  renderDocumentChrome();
  renderBuses();
  renderDiagnostics();
  updateBuiltControls();
  showToast(
    isInitialBuild
      ? message.descriptor.openEnded
        ? "Engine package compiled. The interactive bench is open-ended."
        : "Engine package compiled. The authored run is finite."
      : "Monitor bus selected; the live session was reset.",
  );
}

function renderBuses() {
  elements.busSelect.textContent = "";
  const buses = state.built?.descriptor.buses ?? [];
  const grouped = {
    master: [],
    route: [],
  };
  for (const bus of buses) {
    if (String(bus.kind).startsWith("engine-")) {
      grouped.master.push(bus);
    } else {
      grouped.route.push(bus);
    }
  }
  for (const [groupName, groupBuses] of Object.entries(grouped)) {
    if (groupBuses.length === 0) {
      continue;
    }
    const group = document.createElement("optgroup");
    group.label = groupName === "master" ? "Master buses" : "Route buses";
    for (const bus of groupBuses) {
      const option = document.createElement("option");
      option.value = String(bus.index);
      option.textContent = `${bus.id} · ${bus.channelCount}ch · ${formatRate(
        bus.sampleRateHz,
      )}`;
      group.append(option);
    }
    elements.busSelect.append(group);
  }
  elements.busSelect.value = String(
    state.built?.descriptor.selectedBusIndex ?? "",
  );
  elements.busSelect.disabled = buses.length === 0;
}

function formatRate(value) {
  if (!Number.isFinite(value)) {
    return "unknown rate";
  }
  return value >= 1000
    ? `${(value / 1000).toLocaleString(undefined, {
        maximumFractionDigits: 1,
      })} kHz`
    : `${value} Hz`;
}

function updateBuiltControls() {
  const built = state.built;
  const running = state.sessionState === "running";
  const active = running || state.sessionState === "preparing";
  const usable =
    Boolean(built) &&
    ![
      "building",
      "compiling",
      "exporting",
      "preparing",
      "failed",
      "completed",
    ].includes(state.sessionState);
  elements.startButton.disabled = !usable || running || !state.securityAdmitted;
  elements.stopButton.disabled = !active;
  const structuralBusy = [
    "building",
    "compiling",
    "exporting",
    "running",
    "preparing",
  ].includes(state.sessionState);
  elements.buildButton.disabled = structuralBusy;
  elements.restartButton.disabled =
    !built || !state.securityAdmitted || structuralBusy;
  elements.exportButton.disabled =
    !built ||
    active ||
    state.sessionState === "exporting";
  elements.busSelect.disabled =
    !built ||
    (built.descriptor.buses?.length ?? 0) === 0 ||
    active ||
    state.sessionState === "exporting";

  const capabilities = built?.capabilities ?? {};
  elements.throttleInput.disabled = !running || !capabilities.throttle;
  elements.starterButton.disabled = !running || !capabilities.starter;
  elements.ignitionButton.disabled = !running || !capabilities.ignition;
  elements.fuelButton.disabled = !running || !capabilities.fuel;
  elements.limiterButton.disabled = !running || !capabilities.limiter;
  elements.externalResistanceInput.disabled =
    !running || !capabilities["external-resisting-torque"];
  elements.heldDynoTargetRpmInput.disabled =
    !running || !capabilities["held-dyno-target-engine-speed"];
  elements.heldDynoAbsorbingTorqueInput.disabled =
    !running || !capabilities["held-dyno-maximum-absorbing-torque"];
  elements.heldDynoDrivingTorqueInput.disabled =
    !running || !capabilities["held-dyno-maximum-driving-torque"];
  elements.vehicleGearSelect.disabled =
    !running || !capabilities["vehicle-selected-forward-gear"];
  elements.vehicleClutchInput.disabled =
    !running || !capabilities["vehicle-clutch-engagement"];
  elements.vehicleBrakeInput.disabled =
    !running || !capabilities["vehicle-service-brake-application"];
  const admitted = Object.values(capabilities).some(Boolean);
  elements.controlsAdmission.textContent = admitted ? "Admitted" : "Not admitted";
}

async function ensureAudioContext() {
  if (!state.securityAdmitted) {
    throw new Error("Cross-origin isolated shared audio is unavailable.");
  }
  if (!state.audio.context) {
    const AudioContextClass = window.AudioContext ?? window.webkitAudioContext;
    state.audio.context = new AudioContextClass({
      latencyHint: "interactive",
    });
  }
  if (!state.audio.workletLoaded) {
    await state.audio.context.audioWorklet.addModule(WORKLET_URL);
    state.audio.workletLoaded = true;
  }
  if (state.audio.context.state !== "running") {
    await state.audio.context.resume();
  }
  if (state.audio.context.state !== "running") {
    throw new Error("The browser did not grant the audio start gesture.");
  }
  return state.audio.context;
}

async function startSession(command = "start") {
  if (!state.built) {
    return;
  }
  try {
    const context = await ensureAudioContext();
    elements.gestureNote.textContent = `${formatRate(
      context.sampleRate,
    )} browser output`;
    const leadFrames = Math.ceil(context.sampleRate * 0.2);
    postWorker({
      type: command,
      requestId: nextRequestId(),
      outputSampleRate: context.sampleRate,
      leadFrames,
    });
  } catch (error) {
    showToast(error.message, true);
  }
}

function stopSession() {
  if (
    !state.workerReady ||
    !["preparing", "running"].includes(state.sessionState)
  ) {
    return;
  }
  if (state.liveState.starter && state.built?.capabilities.starter) {
    state.liveState.starter = false;
    renderLiveState();
    sendControl("starter", false);
  }
  state.starterInputHeld = false;
  postWorker({ type: "stop", requestId: nextRequestId() });
}

function detachAudioNode() {
  if (state.audio.node) {
    state.audio.node.port.postMessage({ type: "stop" });
    state.audio.node.disconnect();
    state.audio.node = null;
  }
  state.audio.ring = null;
  state.audio.initialLeadFrames = 0;
}

function attachAudioRing(message) {
  const context = state.audio.context;
  if (!context || !(message.sharedBuffer instanceof SharedArrayBuffer)) {
    throw new Error("The Worker supplied an invalid shared audio ring.");
  }
  if (
    message.headerBytes !== RING_HEADER.byteLength ||
    message.schema?.id !== RING_SCHEMA_ID ||
    message.schema?.headerBytes !== RING_HEADER.byteLength ||
    message.schema?.sampleEncoding !== RING_HEADER_SCHEMA.sampleEncoding ||
    message.sampleRate !== context.sampleRate ||
    !Number.isInteger(message.capacityFrames) ||
    message.capacityFrames < 256 ||
    !Number.isInteger(message.channelCount) ||
    message.channelCount < 1
  ) {
    throw new Error("The shared audio ring descriptor is not admitted.");
  }
  const requiredBytes =
    message.headerBytes +
    message.capacityFrames * message.channelCount * Float32Array.BYTES_PER_ELEMENT;
  if (message.sharedBuffer.byteLength < requiredBytes) {
    throw new Error("The shared audio ring is smaller than its descriptor.");
  }

  detachAudioNode();
  const ringState = new Int32Array(
    message.sharedBuffer,
    0,
    RING_HEADER.byteLength / Int32Array.BYTES_PER_ELEMENT,
  );
  const initialLeadFrames = Math.max(
    0,
    Atomics.load(ringState, RING_HEADER.availableFrames),
  );
  const ring = {
    state: ringState,
    capacityFrames: message.capacityFrames,
    channelCount: message.channelCount,
    sampleRate: message.sampleRate,
  };
  const node = new AudioWorkletNode(context, "eso-ring-output", {
    numberOfInputs: 0,
    numberOfOutputs: 1,
    outputChannelCount: [message.channelCount],
    processorOptions: {
      sharedBuffer: message.sharedBuffer,
      headerBytes: message.headerBytes,
      capacityFrames: message.capacityFrames,
      channelCount: message.channelCount,
      indices: RING_HEADER,
    },
  });
  node.onprocessorerror = () => {
    showToast("The shared-memory audio drain stopped unexpectedly.", true);
    setSessionState("failed", "AudioWorklet processor failure.");
  };
  node.connect(context.destination);
  state.audio.node = node;
  state.audio.ring = ring;
  state.audio.initialLeadFrames = initialLeadFrames;
  renderRuntimeStats();
}

function sendControls(controls) {
  if (!state.built || state.sessionState !== "running") {
    return false;
  }
  if (!Array.isArray(controls) || controls.length === 0) {
    throw new TypeError("sendControls requires a nonempty control batch");
  }
  for (const control of controls) {
    if (!state.built.capabilities[control.kind]) {
      showToast(`${control.kind} was not admitted for this scenario.`, true);
      return false;
    }
  }
  const requestId = nextRequestId();
  for (const { kind, value } of controls) {
    state.pendingControls.set(kind, {
      requestId,
      value,
      deliveryFrame: null,
    });
  }
  postWorker({
    type: "enqueue-controls",
    requestId,
    controls,
  });
  return true;
}

function sendControl(kind, value) {
  return sendControls([{ kind, value }]);
}

function toggleSwitch(kind, button, label) {
  const nextValue = button.getAttribute("aria-checked") !== "true";
  state.liveState[kind] = nextValue;
  button.setAttribute("aria-checked", String(nextValue));
  label.textContent = nextValue ? "On" : "Off";
  sendControl(kind, nextValue);
}

function quantityText(quantity, unitScale = 1) {
  if (!quantity?.available || !Number.isFinite(quantity.value)) {
    return "—";
  }
  return (quantity.value * unitScale).toLocaleString(undefined, {
    maximumFractionDigits: 1,
  });
}

function quantityStatus(quantity) {
  if (!quantity?.available) {
    return quantity?.reason ?? "Unavailable";
  }
  return quantity.complete ? "Complete model" : "Partial model";
}

function scalarText(value, unit, maximumFractionDigits = 1) {
  if (!Number.isFinite(value)) {
    return "—";
  }
  const number = value.toLocaleString(undefined, { maximumFractionDigits });
  return unit ? `${number} ${unit}` : number;
}

function renderModeTelemetry(telemetry) {
  const dyno = telemetry?.heldDyno;
  elements.dynoTargetValue.textContent = dyno
    ? scalarText(dyno.targetEngineSpeedRpm, "RPM", 0)
    : "—";
  elements.dynoRequiredTorqueValue.textContent = dyno
    ? scalarText(dyno.requiredActuatorTorqueNm, "N·m")
    : "—";
  elements.dynoAppliedTorqueValue.textContent = dyno
    ? scalarText(dyno.appliedActuatorTorqueNm, "N·m")
    : "—";
  elements.dynoLimitsValue.textContent = dyno
    ? `${scalarText(dyno.maximumAbsorbingTorqueNm, "N·m")} absorb / ${scalarText(
        dyno.maximumDrivingTorqueNm,
        "N·m",
      )} drive`
    : "—";
  elements.dynoDispositionValue.textContent = dyno
    ? readableToken(dyno.disposition)
    : "—";

  const vehicle = telemetry?.freeVehicle;
  elements.vehicleSpeedValue.textContent = vehicle
    ? scalarText(vehicle.vehicleSpeedMS * 3.6, "km/h")
    : "—";
  elements.vehicleDistanceValue.textContent = vehicle
    ? scalarText(vehicle.vehicleDistanceM, "m")
    : "—";
  elements.vehicleGearValue.textContent = vehicle
    ? forwardGearLabel(vehicle.selectedForwardGearOrdinal)
    : "—";
  elements.vehicleClutchValue.textContent = vehicle
    ? `${scalarText(vehicle.clutchEngagement01 * 100, "%", 0)} · ${scalarText(
        vehicle.appliedAverageClutchTorqueOnEngineNm,
        "N·m",
      )}`
    : "—";
  elements.vehicleSlipValue.textContent = vehicle
    ? scalarText(vehicle.finalClutchSlipRadS, "rad/s")
    : "—";
  elements.vehicleRoadLoadValue.textContent = vehicle
    ? `${scalarText(vehicle.appliedAverageRoadLoadForceN, "N")} / ${scalarText(
        vehicle.requestedRoadLoadForceN,
        "N requested",
      )}`
    : "—";
  elements.vehicleDispositionValue.textContent = vehicle
    ? `${readableToken(vehicle.clutchDisposition)} · ${readableToken(
        vehicle.roadLoadDisposition,
      )}`
    : "—";
}

function acceptTelemetry(message) {
  const frame = message.frames.at(-1);
  if (!frame) {
    return;
  }
  const torque = frame.torque.instantaneousNetShaft;
  const power = frame.torque.instantaneousPowerW;
  const deliveryFrame = Number(message.process.firstDeliveryFrame);
  const sampleRate = state.built?.descriptor.deliverySampleRate;
  state.telemetry = {
    elapsedSeconds:
      Number.isFinite(deliveryFrame) && Number.isFinite(sampleRate)
        ? deliveryFrame / sampleRate
        : 0,
    engineSpeedRpm: frame.engineSpeedRpm,
    netShaftTorque: {
      value: torque.valueNm,
      available: torque.availability === 1,
      complete: torque.completeness === 1,
      reason:
        QUANTITY_UNAVAILABLE_REASONS[torque.unavailableReason] ??
        `Unavailable reason ${torque.unavailableReason}`,
    },
    instantaneousPower: {
      value: power.value,
      available: power.availability === 1,
      complete: power.completeness === 1,
      reason:
        QUANTITY_UNAVAILABLE_REASONS[power.unavailableReason] ??
        `Unavailable reason ${power.unavailableReason}`,
    },
    heldDyno: frame.heldDyno,
    freeVehicle: frame.freeVehicle,
  };
  const processEndDeliveryFrame =
    BigInt(message.process.firstDeliveryFrame) +
    BigInt(message.process.deliveryFrameCount);
  const pendingAtFrame = (kind) => {
    const pending = state.pendingControls.get(kind);
    if (!pending) {
      return false;
    }
    if (
      pending.deliveryFrame !== null &&
      processEndDeliveryFrame > BigInt(pending.deliveryFrame)
    ) {
      state.pendingControls.delete(kind);
      return false;
    }
    return true;
  };
  const mayFollowTelemetry = (kind) =>
    !state.editingControls.has(kind) && !pendingAtFrame(kind);
  if (mayFollowTelemetry("throttle")) {
    state.liveState.throttle = frame.requestedThrottle01;
  }
  if (!pendingAtFrame("ignition")) {
    state.liveState.ignition = frame.ignitionEnabled;
  }
  if (!pendingAtFrame("fuel")) {
    state.liveState.fuel = frame.fuelEnabled;
  }
  if (!pendingAtFrame("starter")) {
    state.liveState.starter = frame.starterEnabled;
  }
  if (!pendingAtFrame("limiter") && typeof frame.limiterEnabled === "boolean") {
    state.liveState.limiter = frame.limiterEnabled;
  }
  if (
    mayFollowTelemetry("external-resisting-torque") &&
    Number.isFinite(frame.requestedExternalResistingTorqueNm)
  ) {
    state.liveState["external-resisting-torque"] =
      frame.requestedExternalResistingTorqueNm;
  }
  if (frame.heldDyno) {
    if (mayFollowTelemetry("held-dyno-target-engine-speed")) {
      state.liveState["held-dyno-target-engine-speed"] =
        frame.heldDyno.targetEngineSpeedRpm;
    }
    if (mayFollowTelemetry("held-dyno-maximum-absorbing-torque")) {
      state.liveState["held-dyno-maximum-absorbing-torque"] =
        frame.heldDyno.maximumAbsorbingTorqueNm;
    }
    if (mayFollowTelemetry("held-dyno-maximum-driving-torque")) {
      state.liveState["held-dyno-maximum-driving-torque"] =
        frame.heldDyno.maximumDrivingTorqueNm;
    }
  }
  if (frame.freeVehicle) {
    if (mayFollowTelemetry("vehicle-selected-forward-gear")) {
      state.liveState["vehicle-selected-forward-gear"] =
        frame.freeVehicle.selectedForwardGearOrdinal ?? 0;
    }
    if (mayFollowTelemetry("vehicle-clutch-engagement")) {
      state.liveState["vehicle-clutch-engagement"] =
        frame.freeVehicle.clutchEngagement01;
    }
    if (mayFollowTelemetry("vehicle-service-brake-application")) {
      state.liveState["vehicle-service-brake-application"] =
        frame.freeVehicle.serviceBrakeApplication01;
    }
  }
  renderLiveState();

  state.trace.push({
    elapsedSeconds: state.telemetry.elapsedSeconds,
    rpm: state.telemetry.engineSpeedRpm,
    torqueNm: state.telemetry.netShaftTorque.available
      ? state.telemetry.netShaftTorque.value
      : null,
  });
  if (state.trace.length > MAX_TRACE_POINTS) {
    state.trace.splice(0, state.trace.length - MAX_TRACE_POINTS);
  }
  renderTelemetry();
  scheduleTraceDraw();
}

function renderTelemetry() {
  const telemetry = state.telemetry;
  if (!telemetry) {
    elements.rpmValue.textContent = "—";
    elements.rpmMeter.style.width = "0";
    elements.torqueValue.textContent = "—";
    elements.powerValue.textContent = "—";
    elements.torqueStatus.textContent = "Unavailable";
    elements.powerStatus.textContent = "Unavailable";
    elements.elapsedValue.textContent = "00:00.000";
    renderModeTelemetry(null);
    return;
  }
  const redline = getRedlineRpm();
  elements.rpmValue.textContent = Math.round(
    telemetry.engineSpeedRpm,
  ).toLocaleString();
  elements.rpmMeter.style.width = `${Math.min(
    100,
    Math.max(0, (telemetry.engineSpeedRpm / redline) * 100),
  )}%`;
  elements.torqueValue.textContent = quantityText(telemetry.netShaftTorque);
  elements.powerValue.textContent = quantityText(
    telemetry.instantaneousPower,
    0.001,
  );
  elements.torqueStatus.textContent = quantityStatus(
    telemetry.netShaftTorque,
  );
  elements.powerStatus.textContent = quantityStatus(
    telemetry.instantaneousPower,
  );
  elements.elapsedValue.textContent = formatDuration(telemetry.elapsedSeconds);
  renderModeTelemetry(telemetry);
}

function getRedlineRpm() {
  const candidate =
    state.documents.engine.parsed?.engine?.limits?.redline?.value;
  return Number.isFinite(candidate) && candidate > 0 ? candidate : 8000;
}

function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) {
    return "00:00.000";
  }
  const minutes = Math.floor(seconds / 60);
  const remaining = seconds - minutes * 60;
  return `${String(minutes).padStart(2, "0")}:${remaining
    .toFixed(3)
    .padStart(6, "0")}`;
}

function scheduleTraceDraw() {
  if (state.traceDrawRequested) {
    return;
  }
  state.traceDrawRequested = true;
  requestAnimationFrame(() => {
    state.traceDrawRequested = false;
    drawTrace();
  });
}

function drawTrace() {
  const canvas = elements.telemetryCanvas;
  const bounds = canvas.getBoundingClientRect();
  const scale = Math.max(1, window.devicePixelRatio || 1);
  const width = Math.max(1, Math.round(bounds.width * scale));
  const height = Math.max(1, Math.round(bounds.height * scale));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const context = canvas.getContext("2d");
  context.clearRect(0, 0, width, height);
  if (state.trace.length < 2) {
    context.fillStyle = "#627177";
    context.font = `${10 * scale}px ${getComputedStyle(document.body).fontFamily}`;
    context.textAlign = "center";
    context.fillText(
      "Telemetry appears when the session advances",
      width / 2,
      height / 2,
    );
    return;
  }

  const padding = 5 * scale;
  const plotWidth = width - padding * 2;
  const plotHeight = height - padding * 2;
  const redline = getRedlineRpm();
  const maximumTorque = Math.max(
    1,
    ...state.trace.map((point) => Math.abs(point.torqueNm ?? 0)),
  );

  function stroke(color, value, maximum) {
    context.beginPath();
    context.strokeStyle = color;
    context.lineWidth = 1.5 * scale;
    context.lineJoin = "round";
    for (let index = 0; index < state.trace.length; index += 1) {
      const point = state.trace[index];
      const x =
        padding + (index / Math.max(1, state.trace.length - 1)) * plotWidth;
      const normalized = Math.min(1, Math.max(0, value(point) / maximum));
      const y = padding + (1 - normalized) * plotHeight;
      if (index === 0) {
        context.moveTo(x, y);
      } else {
        context.lineTo(x, y);
      }
    }
    context.stroke();
  }

  stroke("#ee8b3b", (point) => point.rpm, redline);
  stroke(
    "#66aee8",
    (point) => Math.abs(point.torqueNm ?? 0),
    maximumTorque,
  );
}

function acceptRuntimeStats(message) {
  state.runtimeStats = message;
  renderRuntimeStats();
}

function readRingStats() {
  const ring = state.audio.ring;
  if (!ring) {
    return null;
  }
  const available = Math.min(
    ring.capacityFrames,
    Math.max(0, Atomics.load(ring.state, RING_HEADER.availableFrames)),
  );
  return {
    fillFrames: available,
    capacityFrames: ring.capacityFrames,
    underrunFrames: Math.max(
      0,
      Atomics.load(ring.state, RING_HEADER.underrunFrames),
    ),
    underrunEvents: Math.max(
      0,
      Atomics.load(ring.state, RING_HEADER.underrunEvents),
    ),
  };
}

function renderRuntimeStats() {
  const shared = readRingStats();
  const stats = state.runtimeStats;
  if (!shared && !stats) {
    elements.ringFillValue.textContent = "—";
    elements.leadValue.textContent = "—";
    elements.underrunValue.textContent = "0";
    elements.realtimeFactorValue.textContent = "—";
    elements.ringFillMeter.style.width = "0";
    elements.realtimeHealth.textContent = "Waiting";
    return;
  }

  const fillFrames = shared?.fillFrames ?? stats?.ring?.availableFrames ?? 0;
  const capacityFrames =
    shared?.capacityFrames ?? state.audio.ring?.capacityFrames ?? 0;
  const sampleRate =
    state.audio.ring?.sampleRate ?? stats.outputSampleRate;
  const fill = capacityFrames > 0 ? fillFrames / capacityFrames : 0;
  const leadFrames = fillFrames;
  const leadMs =
    Number.isFinite(sampleRate) && sampleRate > 0
      ? (leadFrames / sampleRate) * 1000
      : null;
  const methodQuantumMs =
    Number.isFinite(state.built?.descriptor.deliveryFramesPerBlock) &&
    Number.isFinite(state.built?.descriptor.deliverySampleRate)
      ? (state.built.descriptor.deliveryFramesPerBlock /
          state.built.descriptor.deliverySampleRate) *
        1000
      : null;
  const controlLeadMs =
    Number.isFinite(leadMs) && Number.isFinite(methodQuantumMs)
      ? leadMs + methodQuantumMs
      : null;
  const underruns =
    shared?.underrunEvents ?? stats?.ring?.underrunEvents ?? 0;
  const elapsedSeconds = (stats?.elapsedMilliseconds ?? 0) / 1000;
  const streamedFrames = Number.isFinite(stats?.generatedDeviceFrames)
    ? Math.max(
        0,
        stats.generatedDeviceFrames - state.audio.initialLeadFrames,
      )
    : null;
  const factor =
    elapsedSeconds >= 0.25 &&
    Number.isFinite(streamedFrames) &&
    Number.isFinite(stats?.outputSampleRate) &&
    stats.outputSampleRate > 0
      ? streamedFrames / stats.outputSampleRate / elapsedSeconds
      : null;

  elements.ringFillValue.textContent = `${Math.round(fill * 100)}%`;
  elements.leadValue.textContent = Number.isFinite(controlLeadMs)
    ? `≈ ${controlLeadMs.toFixed(0)} ms`
    : "—";
  elements.leadValue.title = Number.isFinite(controlLeadMs)
    ? `${leadMs.toFixed(1)} ms ring fill + ${methodQuantumMs.toFixed(
        1,
      )} ms method quantum${
        state.lastAcceptedControlFrame === null
          ? ""
          : `; last accepted target frame ${state.lastAcceptedControlFrame}`
      }`
    : "";
  elements.underrunValue.textContent = String(underruns);
  elements.realtimeFactorValue.textContent = Number.isFinite(factor)
    ? `${factor.toFixed(2)}×`
    : "—";
  elements.ringFillMeter.style.width = `${Math.max(
    0,
    Math.min(100, fill * 100),
  )}%`;
  elements.ringFillMeter.style.background =
    fill < 0.08 ? "var(--red)" : fill < 0.2 ? "var(--yellow)" : "var(--green)";

  if (state.sessionState === "preparing" && !shared) {
    elements.ringFillValue.textContent = "Priming";
    elements.leadValue.textContent = "Pending";
    elements.leadValue.title =
      "Control lead becomes measurable when the primed PCM ring is published.";
    elements.ringFillMeter.style.background = "var(--accent)";
    elements.realtimeHealth.textContent = "Preparing";
    elements.runtimeDetail.textContent =
      "The Worker is advancing preparation and will publish audio only after the requested lead is buffered.";
  } else if (underruns > 0) {
    elements.realtimeHealth.textContent = "Underrun";
    elements.runtimeDetail.textContent = `${
      shared?.underrunFrames ?? stats?.ring?.underrunFrames ?? 0
    } output frames were replaced by a bounded fade to zero.`;
  } else if (state.sessionState === "running") {
    elements.realtimeHealth.textContent = "Healthy";
    elements.runtimeDetail.textContent =
      "Worker production and AudioWorklet delivery are keeping their requested lead.";
  } else {
    elements.realtimeHealth.textContent = "Stopped";
  }
}

function acceptWorkerDiagnostics(message) {
  if (
    state.buildRequestId !== null &&
    message.requestId !== state.buildRequestId
  ) {
    return;
  }
  state.workerDiagnostics = message.diagnostics.map((diagnostic) => ({
    ...diagnostic,
    severity: diagnostic.severity === 2 ? "warning" : "error",
    line: diagnostic.source?.line ?? null,
    column: diagnostic.source?.column ?? null,
  }));
  renderDiagnostics();
  if (
    state.workerDiagnostics.some(
      (diagnostic) => diagnostic.severity === "error",
    )
  ) {
    const priorState = state.mutationPriorStates.get(message.requestId);
    state.buildMutationRequests.delete(message.requestId);
    state.buildThrottlePresentations.delete(message.requestId);
    state.mutationPriorStates.delete(message.requestId);
    state.requestKinds.delete(message.requestId);
    setSessionState(
      state.built ? (priorState ?? "ready") : "failed",
      state.built
        ? "Input rejected; the prior compiled session is still active."
        : "The input contract was rejected.",
    );
    setChip(elements.buildStatus, "Validation failed", "bad");
    elements.buildButton.disabled = false;
  }
}

function admitReadyMessage(message) {
  const schema = message.ringHeaderSchema;
  const counters = schema?.counters;
  if (
    message.protocol !== WORKER_PROTOCOL_ID ||
    message.apiVersion !== ESO_C_API_VERSION ||
    message.canonicalSampleRate !== ESO_CANONICAL_SAMPLE_RATE ||
    message.structuralEditContract !== "compile-and-replace" ||
    schema?.id !== RING_SCHEMA_ID ||
    schema.headerBytes !== RING_HEADER.byteLength ||
    schema.sampleEncoding !== RING_HEADER_SCHEMA.sampleEncoding ||
    counters?.writeFrame !== RING_HEADER.writeFrame ||
    counters?.readFrame !== RING_HEADER.readFrame ||
    counters?.availableFrames !== RING_HEADER.availableFrames ||
    counters?.underrunFrames !== RING_HEADER.underrunFrames ||
    counters?.underrunEvents !== RING_HEADER.underrunEvents ||
    counters?.generation !== RING_HEADER.generation ||
    counters?.producerState !== RING_HEADER.producerState
  ) {
    throw new Error(
      "The Worker protocol, C ABI, or shared-ring schema does not match this workbench.",
    );
  }
  state.workerReady = true;
  state.requestKinds.delete(message.requestId);
  setChip(elements.workerStatus, `WASM ABI ${message.apiVersion}`, "good");
}

function acceptWorkerState(message) {
  const requestKind = state.requestKinds.get(message.requestId);
  if (requestKind && requestKind !== "export-wav") {
    state.requestKinds.delete(message.requestId);
  }
  if (
    requestKind === "restart" &&
    ["preparing", "running"].includes(message.state)
  ) {
    resetLiveControls();
  }
  setSessionState(message.state, message.detail ?? "");
  if (message.state === "running") {
    setChip(elements.buildStatus, "Session running", "good");
  } else if (message.state === "preparing") {
    setChip(elements.buildStatus, "Preparing simulation", "busy");
  } else if (message.state === "ready") {
    setChip(elements.buildStatus, "Build admitted", "good");
  } else if (message.state === "completed") {
    state.starterInputHeld = false;
    setChip(elements.buildStatus, "Authored run complete", "good");
    showToast("The finite authored scenario completed.");
  } else if (message.state === "paused") {
    state.starterInputHeld = false;
    setChip(elements.buildStatus, "Session stopped", "");
  } else if (message.state === "failed") {
    state.starterInputHeld = false;
    setChip(elements.buildStatus, "Runtime fault", "bad");
  } else if (message.state === "exporting") {
    setChip(elements.buildStatus, "Exporting WAV", "busy");
  }
}

function acceptControlsResult(message) {
  state.requestKinds.delete(message.requestId);
  if (!Array.isArray(message.controls) || message.controls.length === 0) {
    throw new Error("The Worker returned an invalid controls result.");
  }
  for (const control of message.controls) {
    const pending = state.pendingControls.get(control.kind);
    if (message.accepted) {
      state.lastAcceptedControlFrame = control.deliveryFrame;
      if (pending?.requestId === message.requestId) {
        pending.deliveryFrame = control.deliveryFrame;
      }
    } else if (pending?.requestId === message.requestId) {
      state.pendingControls.delete(control.kind);
    }
    if (!message.accepted) {
      showToast(
        `${control.kind} control rejected: ${message.reason ?? "not admitted"}`,
        true,
      );
    }
  }
}

function acceptWavExport(message) {
  state.requestKinds.delete(message.requestId);
  if (!(message.wav instanceof ArrayBuffer)) {
    showToast("The Worker returned an invalid WAV payload.", true);
    return;
  }
  const busId = message.bus?.id ?? "engine";
  const engineId = state.built?.engineId ?? "engine";
  const scenarioId = state.built?.scenarioId ?? "run";
  const filename = `${engineId}-${scenarioId}-${busId}.wav`;
  downloadBlob(
    new Blob([message.wav], { type: "audio/wav" }),
    filename,
  );
  showToast(`Saved ${filename}.`);
}

function acceptWorkerError(message) {
  const error = message.error ?? message;
  const requestKind = state.requestKinds.get(message.requestId);
  state.requestKinds.delete(message.requestId);
  for (const [kind, pending] of state.pendingControls) {
    if (pending.requestId === message.requestId) {
      state.pendingControls.delete(kind);
    }
  }
  const priorState = state.mutationPriorStates.get(message.requestId);
  const retainedPriorBuild =
    state.buildMutationRequests.has(message.requestId) && Boolean(state.built);
  state.buildMutationRequests.delete(message.requestId);
  state.buildThrottlePresentations.delete(message.requestId);
  state.mutationPriorStates.delete(message.requestId);
  state.workerDiagnostics = [
    ...state.workerDiagnostics,
    {
      document: null,
      path: "",
      severity: "error",
      code: error.detailCode ?? error.code ?? "runtime_error",
      message: error.message,
    },
  ];
  renderDiagnostics();
  let fatal = false;
  if (retainedPriorBuild) {
    setSessionState(
      priorState ?? "ready",
      `${error.message} The prior compiled session is still active.`,
    );
  } else if (
    message.requestId === null ||
    requestKind === "initialize" ||
    requestKind === "build" ||
    requestKind === undefined
  ) {
    fatal = true;
    setSessionState("failed", error.message);
  } else {
    showToast(error.message, true);
  }
  if (fatal) {
    setChip(elements.buildStatus, "Runtime fault", "bad");
  } else if (retainedPriorBuild) {
    setChip(elements.buildStatus, "Prior build retained", "bad");
  }
  elements.buildButton.disabled = false;
}

function acceptExportProgress(message) {
  const complete = Number(message.completedBlocks);
  const total = Number(message.totalBlocks);
  const percent =
    Number.isFinite(complete) && Number.isFinite(total) && total > 0
      ? Math.min(100, Math.max(0, (complete / total) * 100))
      : null;
  setChip(
    elements.buildStatus,
    Number.isFinite(percent)
      ? `Export ${percent.toFixed(0)}%`
      : "Exporting WAV",
    "busy",
  );
}

function handleWorkerMessage(event) {
  const message = event.data;
  if (!message || typeof message.type !== "string") {
    return;
  }
  try {
    switch (message.type) {
      case "ready":
        admitReadyMessage(message);
        break;
      case "validation":
        acceptWorkerDiagnostics(message);
        break;
      case "built":
        acceptBuilt(message);
        break;
      case "state":
        acceptWorkerState(message);
        break;
      case "audio-ring":
        attachAudioRing(message);
        break;
      case "telemetry":
        acceptTelemetry(message);
        break;
      case "runtime-stats":
        acceptRuntimeStats(message);
        break;
      case "controls-result":
        acceptControlsResult(message);
        break;
      case "wav-export":
        acceptWavExport(message);
        break;
      case "export-progress":
        acceptExportProgress(message);
        break;
      case "error":
        acceptWorkerError(message);
        break;
      default:
        acceptWorkerError({
          code: "unknown_worker_message",
          message: `Unexpected Worker message: ${message.type}`,
        });
    }
  } catch (error) {
    acceptWorkerError({
      code: "invalid_worker_message",
      message: error.message,
    });
  }
}

function startWorker() {
  if (!("Worker" in window)) {
    setChip(elements.workerStatus, "Workers unavailable", "bad");
    return;
  }
  try {
    state.worker = new Worker(WORKER_URL, {
      type: "module",
      name: "engine-sim-offline-runtime",
    });
    state.worker.addEventListener("message", handleWorkerMessage);
    state.worker.addEventListener("error", (event) => {
      setChip(elements.workerStatus, "Runtime failed", "bad");
      acceptWorkerError({
        code: "worker_load_failed",
        message: event.message || "The engine Worker could not start.",
      });
    });
    setChip(elements.workerStatus, "Runtime loading", "busy");
    state.worker.postMessage({
      type: "initialize",
      requestId: nextRequestId(),
      ringHeaderSchema: RING_HEADER_SCHEMA,
    });
  } catch (error) {
    setChip(elements.workerStatus, "Runtime unavailable", "bad");
    showToast(error.message, true);
  }
}

function configureSecurityGate() {
  const AudioContextClass = window.AudioContext ?? window.webkitAudioContext;
  state.securityAdmitted =
    window.crossOriginIsolated === true &&
    typeof SharedArrayBuffer === "function" &&
    typeof AudioContextClass === "function" &&
    typeof AudioWorkletNode === "function";
  elements.isolationGate.hidden = state.securityAdmitted;
  setChip(
    elements.isolationStatus,
    state.securityAdmitted ? "Isolated memory" : "Isolation missing",
    state.securityAdmitted ? "good" : "bad",
  );
}

function requestWavExport() {
  if (!state.built) {
    return;
  }
  postWorker({
    type: "export-wav",
    requestId: nextRequestId(),
  });
}

function bindEvents() {
  for (const tab of elements.documentTabs) {
    tab.addEventListener("click", () => switchDocument(tab.dataset.document));
  }

  for (const [kind, editor] of [
    ["engine", elements.engineEditor],
    ["scenario", elements.scenarioEditor],
  ]) {
    editor.addEventListener("input", () => markDocumentEdited(kind));
    editor.addEventListener("click", updateCursorStatus);
    editor.addEventListener("keyup", updateCursorStatus);
    editor.addEventListener("keydown", (event) => {
      if (event.key !== "Tab") {
        return;
      }
      event.preventDefault();
      const begin = editor.selectionStart;
      const end = editor.selectionEnd;
      editor.setRangeText("  ", begin, end, "end");
      markDocumentEdited(kind);
    });
  }

  elements.openDocumentButton.addEventListener("click", () => {
    const input =
      state.activeDocument === "engine"
        ? elements.engineInput
        : elements.scenarioInput;
    input.click();
  });
  elements.engineInput.addEventListener("change", () => {
    if (elements.engineInput.files[0]) {
      void openDocumentFile("engine", elements.engineInput.files[0]);
    }
    elements.engineInput.value = "";
  });
  elements.scenarioInput.addEventListener("change", () => {
    if (elements.scenarioInput.files[0]) {
      void openDocumentFile("scenario", elements.scenarioInput.files[0]);
    }
    elements.scenarioInput.value = "";
  });
  elements.saveDocumentButton.addEventListener("click", saveActiveDocument);
  elements.formatDocumentButton.addEventListener("click", formatActiveDocument);
  elements.loadPackageButton.addEventListener("click", () => {
    void loadPackage(elements.packageSelect.value);
  });
  elements.buildButton.addEventListener("click", () => void buildSession());

  elements.addAssetsButton.addEventListener("click", () => {
    delete elements.assetInput.dataset.targetId;
    elements.assetInput.multiple = true;
    elements.assetInput.click();
  });
  elements.assetInput.addEventListener("change", () => {
    selectAssetFiles(
      [...elements.assetInput.files],
      elements.assetInput.dataset.targetId ?? null,
    );
    elements.assetInput.value = "";
    delete elements.assetInput.dataset.targetId;
    elements.assetInput.multiple = true;
  });

  elements.startButton.addEventListener("click", () => void startSession());
  elements.stopButton.addEventListener("click", stopSession);
  elements.restartButton.addEventListener("click", () => {
    state.trace.length = 0;
    scheduleTraceDraw();
    void startSession("restart");
  });
  elements.exportButton.addEventListener("click", requestWavExport);
  elements.busSelect.addEventListener("change", () => {
    if (!state.workerReady || !state.built) {
      return;
    }
    const requestId = nextRequestId();
    state.buildMutationRequests.add(requestId);
    state.mutationPriorStates.set(requestId, state.sessionState);
    postWorker({
      type: "select-audio-bus",
      requestId,
      busIndex: Number(elements.busSelect.value),
    });
  });

  elements.throttleInput.addEventListener("input", () => {
    const value = Number(elements.throttleInput.value) / 100;
    state.editingControls.add("throttle");
    state.liveState.throttle = value;
    renderThrottleControl(value);
  });
  let throttleTimer = null;
  elements.throttleInput.addEventListener("input", () => {
    const requestedValue = Number(elements.throttleInput.value) / 100;
    window.clearTimeout(throttleTimer);
    throttleTimer = window.setTimeout(() => {
      sendControl("throttle", requestedValue);
      state.editingControls.delete("throttle");
    }, 30);
  });
  elements.ignitionButton.addEventListener("click", () =>
    toggleSwitch(
      "ignition",
      elements.ignitionButton,
      elements.ignitionLabel,
    ),
  );
  elements.fuelButton.addEventListener("click", () =>
    toggleSwitch("fuel", elements.fuelButton, elements.fuelLabel),
  );
  elements.limiterButton.addEventListener("click", () =>
    toggleSwitch("limiter", elements.limiterButton, elements.limiterLabel),
  );
  const setStarterHeld = (enabled) => {
    if (state.starterInputHeld === enabled) {
      return;
    }
    if (enabled && elements.starterButton.disabled) {
      return;
    }
    state.starterInputHeld = enabled;
    state.liveState.starter = enabled;
    renderLiveState();
    sendControl("starter", enabled);
  };
  elements.starterButton.addEventListener("pointerdown", (event) => {
    event.preventDefault();
    elements.starterButton.setPointerCapture(event.pointerId);
    setStarterHeld(true);
  });
  elements.starterButton.addEventListener("pointerup", () =>
    setStarterHeld(false),
  );
  elements.starterButton.addEventListener("pointercancel", () =>
    setStarterHeld(false),
  );
  elements.starterButton.addEventListener("lostpointercapture", () =>
    setStarterHeld(false),
  );
  elements.starterButton.addEventListener("keydown", (event) => {
    if ((event.key === " " || event.key === "Enter") && !event.repeat) {
      event.preventDefault();
      setStarterHeld(true);
    }
  });
  elements.starterButton.addEventListener("keyup", (event) => {
    if (event.key === " " || event.key === "Enter") {
      event.preventDefault();
      setStarterHeld(false);
    }
  });
  window.addEventListener("blur", () => setStarterHeld(false));
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      setStarterHeld(false);
    }
  });
  let externalResistanceTimer = null;
  elements.externalResistanceInput.addEventListener("input", () => {
    const requestedValue = Number(elements.externalResistanceInput.value);
    window.clearTimeout(externalResistanceTimer);
    if (!Number.isFinite(requestedValue) || requestedValue < 0) {
      return;
    }
    state.editingControls.add("external-resisting-torque");
    state.liveState["external-resisting-torque"] = requestedValue;
    externalResistanceTimer = window.setTimeout(() => {
      sendControl("external-resisting-torque", requestedValue);
      state.editingControls.delete("external-resisting-torque");
    }, 80);
  });

  const dynoControlKinds = [
    "held-dyno-target-engine-speed",
    "held-dyno-maximum-absorbing-torque",
    "held-dyno-maximum-driving-torque",
  ];
  let dynoControlTimer = null;
  const scheduleDynoControls = () => {
    window.clearTimeout(dynoControlTimer);
    const values = [
      Number(elements.heldDynoTargetRpmInput.value),
      Number(elements.heldDynoAbsorbingTorqueInput.value),
      Number(elements.heldDynoDrivingTorqueInput.value),
    ];
    if (
      !Number.isFinite(values[0]) ||
      values[0] <= 0 ||
      values.slice(1).some((value) => !Number.isFinite(value) || value < 0)
    ) {
      return;
    }
    for (let index = 0; index < dynoControlKinds.length; ++index) {
      state.editingControls.add(dynoControlKinds[index]);
      state.liveState[dynoControlKinds[index]] = values[index];
    }
    dynoControlTimer = window.setTimeout(() => {
      sendControls(
        dynoControlKinds.map((kind, index) => ({ kind, value: values[index] })),
      );
      for (const kind of dynoControlKinds) {
        state.editingControls.delete(kind);
      }
    }, 80);
  };
  for (const input of [
    elements.heldDynoTargetRpmInput,
    elements.heldDynoAbsorbingTorqueInput,
    elements.heldDynoDrivingTorqueInput,
  ]) {
    input.addEventListener("input", scheduleDynoControls);
  }

  const vehicleControlKinds = [
    "vehicle-selected-forward-gear",
    "vehicle-clutch-engagement",
    "vehicle-service-brake-application",
  ];
  let vehicleControlTimer = null;
  const scheduleVehicleControls = () => {
    window.clearTimeout(vehicleControlTimer);
    const gear = Number(elements.vehicleGearSelect.value);
    const clutch = Number(elements.vehicleClutchInput.value) / 100;
    const brake = Number(elements.vehicleBrakeInput.value) / 100;
    if (
      !Number.isSafeInteger(gear) ||
      gear < 0 ||
      !Number.isFinite(clutch) ||
      clutch < 0 ||
      clutch > 1 ||
      !Number.isFinite(brake) ||
      brake < 0 ||
      brake > 1
    ) {
      return;
    }
    const values = [gear, clutch, brake];
    for (let index = 0; index < vehicleControlKinds.length; ++index) {
      const kind = vehicleControlKinds[index];
      if (state.built?.capabilities[kind]) {
        state.editingControls.add(kind);
        state.liveState[kind] = values[index];
      }
    }
    renderLiveState();
    vehicleControlTimer = window.setTimeout(() => {
      const controls = vehicleControlKinds
        .map((kind, index) => ({ kind, value: values[index] }))
        .filter(({ kind }) => state.built?.capabilities[kind]);
      sendControls(controls);
      for (const { kind } of controls) {
        state.editingControls.delete(kind);
      }
    }, 50);
  };
  elements.vehicleGearSelect.addEventListener(
    "change",
    scheduleVehicleControls,
  );
  elements.vehicleClutchInput.addEventListener(
    "input",
    scheduleVehicleControls,
  );
  elements.vehicleBrakeInput.addEventListener(
    "input",
    scheduleVehicleControls,
  );
  elements.clearDiagnosticsButton.addEventListener("click", () => {
    state.workerDiagnostics = [];
    renderDiagnostics();
  });

  window.addEventListener("resize", scheduleTraceDraw);
  window.addEventListener("beforeunload", () => {
    detachAudioNode();
    state.worker?.terminate();
  });
}

async function initialize() {
  configureSecurityGate();
  populatePackageSelect();
  bindEvents();
  renderInspector();
  renderAssets();
  renderDiagnostics();
  renderTelemetry();
  renderRuntimeStats();
  drawTrace();
  startWorker();
  window.setInterval(renderRuntimeStats, 250);
  await loadPackage(DEFAULT_PACKAGE_ID, { quiet: true });
}

void initialize();
