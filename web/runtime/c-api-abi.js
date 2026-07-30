// Frozen wasm32 representation of include/engine_sim_offline/c_api.h.
//
// This module deliberately describes one ABI version. A mismatched module is
// rejected during startup; there is no compatibility decoder.

export const ESO_C_API_VERSION = 1;
export const ESO_INVALID_HANDLE = 0n;
export const ESO_CANONICAL_SAMPLE_RATE = 192_000;

export const Status = Object.freeze({
  ok: 0,
  abiVersionMismatch: 1,
  invalidArgument: 2,
  invalidHandle: 3,
  notAvailable: 4,
  bufferTooSmall: 5,
  resourceExhausted: 6,
  engineParseFailed: 7,
  engineCompileFailed: 8,
  scenarioParseFailed: 9,
  scenarioCompileFailed: 10,
  sessionCreateFailed: 11,
  controlRejected: 12,
  processFailed: 13,
  internalError: 14,
});

export const AssetKind = Object.freeze({
  audio: 1,
  accessoryConfiguration: 2,
});

export const AudioBusKind = Object.freeze({
  exhaustRouteDry: 1,
  exhaustRouteConfiguredIr: 2,
  exhaustRouteSelected: 3,
  engineRawMaster: 4,
  engineAuditionMaster: 5,
});

export const ControlKind = Object.freeze({
  throttle: 1,
  ignitionEnabled: 2,
  fuelEnabled: 3,
  limiterEnabled: 4,
  externalResistingTorque: 5,
});

export const ControlCapability = Object.freeze({
  throttle: 1 << 0,
  ignitionEnabled: 1 << 1,
  fuelEnabled: 1 << 2,
  limiterEnabled: 1 << 3,
  externalResistingTorque: 1 << 4,
});

export const ProcessKind = Object.freeze({
  block: 1,
  completed: 2,
});

export const BlockPhase = Object.freeze({
  preparation: 1,
  audible: 2,
});

export const RingState = Object.freeze({
  idle: 0,
  streaming: 1,
  paused: 2,
  ended: 3,
  failed: 4,
});

export const STATUS_NAMES = Object.freeze([
  "ok",
  "abi-version-mismatch",
  "invalid-argument",
  "invalid-handle",
  "not-available",
  "buffer-too-small",
  "resource-exhausted",
  "engine-parse-failed",
  "engine-compile-failed",
  "scenario-parse-failed",
  "scenario-compile-failed",
  "session-create-failed",
  "control-rejected",
  "process-failed",
  "internal-error",
]);

export const ERROR_STAGE_NAMES = Object.freeze([
  "none",
  "argument",
  "handle",
  "engine-parse",
  "engine-compile",
  "scenario-parse",
  "scenario-compile",
  "session-create",
  "control",
  "process",
  "abi",
]);

export function statusName(status) {
  return STATUS_NAMES[status] ?? `unknown-status-${status}`;
}

export function errorStageName(stage) {
  return ERROR_STAGE_NAMES[stage] ?? `unknown-stage-${stage}`;
}

export function audioBusKindName(kind) {
  switch (kind) {
    case AudioBusKind.exhaustRouteDry:
      return "exhaust-route-dry";
    case AudioBusKind.exhaustRouteConfiguredIr:
      return "exhaust-route-configured-ir";
    case AudioBusKind.exhaustRouteSelected:
      return "exhaust-route-selected";
    case AudioBusKind.engineRawMaster:
      return "engine-raw-master";
    case AudioBusKind.engineAuditionMaster:
      return "engine-audition-master";
    default:
      return `unknown-audio-bus-${kind}`;
  }
}

export function blockPhaseName(phase) {
  switch (phase) {
    case BlockPhase.preparation:
      return "preparation";
    case BlockPhase.audible:
      return "audible";
    default:
      return `unknown-block-phase-${phase}`;
  }
}

// Every offset is a wasm32 clang C layout offset. Startup checks the public
// eso_abi_layout_t sizes before any of these layouts are used.
export const Layout = Object.freeze({
  utf8View: Object.freeze({ size: 8, data: 0, bytes: 4 }),
  byteView: Object.freeze({ size: 8, data: 0, bytes: 4 }),
  mutableUtf8Buffer: Object.freeze({ size: 8, data: 0, capacity: 4 }),
  assetPayload: Object.freeze({
    size: 20,
    kind: 0,
    idData: 4,
    idBytes: 8,
    payloadData: 12,
    payloadBytes: 16,
  }),
  abiLayout: Object.freeze({ size: 40 }),
  errorInfo: Object.freeze({
    size: 24,
    status: 0,
    stage: 4,
    code: 8,
    detailBytes: 12,
    messageBytes: 16,
    diagnosticCount: 20,
  }),
  errorTextBuffers: Object.freeze({
    size: 16,
    detailData: 0,
    detailCapacity: 4,
    messageData: 8,
    messageCapacity: 12,
  }),
  diagnosticInfo: Object.freeze({
    size: 52,
    severity: 0,
    code: 4,
    hasSubject: 8,
    hasSourcePosition: 12,
    sourceByteOffset: 16,
    sourceLine: 24,
    sourceColumn: 28,
    jsonPointerBytes: 32,
    subjectKindBytes: 36,
    subjectIdBytes: 40,
    messageBytes: 44,
    relatedCount: 48,
  }),
  diagnosticTextBuffers: Object.freeze({
    size: 32,
    jsonPointerData: 0,
    jsonPointerCapacity: 4,
    subjectKindData: 8,
    subjectKindCapacity: 12,
    subjectIdData: 16,
    subjectIdCapacity: 20,
    messageData: 24,
    messageCapacity: 28,
  }),
  relatedDiagnosticInfo: Object.freeze({
    size: 20,
    hasSubject: 0,
    jsonPointerBytes: 4,
    subjectKindBytes: 8,
    subjectIdBytes: 12,
    messageBytes: 16,
  }),
  sessionDescriptor: Object.freeze({
    size: 88,
    maximumDeliveryFrames: 0,
    controlQueueCapacity: 4,
    maximumTelemetryFrames: 8,
    physicsRateNumerator: 16,
    physicsRateDenominator: 24,
    deliveryRateNumerator: 32,
    deliveryRateDenominator: 40,
    physicsFramesPerBlock: 48,
    deliveryFramesPerBlock: 52,
    totalBlockCount: 56,
    preparationBlockCount: 64,
    audioBusCount: 72,
    liveControlCapabilities: 76,
    engineIdBytes: 80,
    scenarioIdBytes: 84,
  }),
  sessionIdentityBuffers: Object.freeze({
    size: 16,
    engineData: 0,
    engineCapacity: 4,
    scenarioData: 8,
    scenarioCapacity: 12,
  }),
  audioBusDescriptor: Object.freeze({
    size: 40,
    kind: 0,
    channelCount: 4,
    sampleRateNumerator: 8,
    sampleRateDenominator: 16,
    hasRouteId: 24,
    routeId: 28,
    idBytes: 32,
  }),
  controlCommand: Object.freeze({
    size: 40,
    deliveryFrame: 0,
    sequence: 8,
    kind: 16,
    enabled: 20,
    scalarValue: 24,
    reserved: 32,
  }),
  controlRejection: Object.freeze({ size: 8, code: 0, commandIndex: 4 }),
  audioCopyBuffer: Object.freeze({
    size: 16,
    busIndex: 0,
    samples: 4,
    sampleCapacity: 8,
    samplesWritten: 12,
  }),
  processInfo: Object.freeze({
    size: 88,
    kind: 0,
    blockPhase: 4,
    blockOrdinal: 8,
    firstPhysicsFrame: 16,
    physicsFrameCount: 24,
    firstDeliveryFrame: 32,
    deliveryFrameCount: 40,
    telemetryWritten: 44,
    completedPhysicsFrames: 48,
    completedDeliveryFrames: 56,
    completedBlockCount: 64,
    liveControlsAccepted: 72,
    hasHeldSpeedOperatingPoint: 76,
    hasInertialDynoResult: 80,
  }),
  quantityValue: Object.freeze({
    size: 24,
    value: 0,
    availability: 8,
    completeness: 12,
    unavailableReason: 16,
  }),
  torqueValue: Object.freeze({
    size: 40,
    value: 0,
    availability: 8,
    completeness: 12,
    unavailableReason: 16,
    includedTerms: 24,
    omittedTerms: 32,
  }),
  engineTelemetry: Object.freeze({
    size: 544,
    physicsStepEnd: 0,
    engineStepEndIndex: 8,
    validityMask: 16,
    ignitionEnabled: 20,
    fuelEnabled: 24,
    starterEnabled: 28,
    dynoEnabled: 32,
    limiterEnabled: 36,
    limiterCutActive: 40,
    theta: 48,
    thetaCycle: 56,
    angularSpeed: 64,
    angularAcceleration: 72,
    engineSpeedRpm: 80,
    requestedThrottle: 88,
    resolvedThrottle: 96,
    intakePlatePosition: 104,
    mainFlowMultiplier: 112,
    requestedExternalResistingTorque: 120,
    torque: 128,
  }),
});

export const TORQUE_FIELDS = Object.freeze([
  "instantaneousIndicatedGas",
  "pumpingPartition",
  "frictionPumpAndAccessory",
  "starter",
  "instantaneousNetShaft",
  "cycleMeanNetShaft",
  "actuator",
  "dynoReaction",
]);

export const QUANTITY_FIELDS = Object.freeze([
  "cycleWorkJ",
  "netBmepPa",
  "instantaneousPowerW",
  "cycleMeanPowerW",
]);

export const WASM32_ABI_WORDS = Object.freeze([
  ESO_C_API_VERSION,
  4,
  4,
  4,
  8,
  1,
  Layout.controlCommand.size,
  Layout.sessionDescriptor.size,
  Layout.audioBusDescriptor.size,
  Layout.engineTelemetry.size,
]);
