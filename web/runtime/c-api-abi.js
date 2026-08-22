// Frozen wasm32 representation of include/crankwave/c_api.h.
//
// This module deliberately describes one ABI version. A mismatched module is
// rejected during startup; there is no compatibility decoder.

export const CRANKWAVE_C_API_VERSION = 10;
export const CRANKWAVE_INVALID_HANDLE = 0n;
export const CRANKWAVE_CANONICAL_SAMPLE_RATE = 192_000;
export const CRANKWAVE_SHA256_DIGEST_SIZE = 32;

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
  bakeFailed: 15,
});

export const AssetKind = Object.freeze({
  audio: 1,
  accessoryConfiguration: 2,
});

export const AudioBusKind = Object.freeze({
  sourceRouteDry: 1,
  sourceRouteConfiguredTransfer: 2,
  sourceRouteSelected: 3,
  engineRawMaster: 4,
  engineAuditionMaster: 5,
});

export const SourceRouteKind = Object.freeze({
  unspecified: 0,
  exhaustOutlet: 1,
  intakeInlet: 2,
  mechanicalEngine: 3,
  mechanicalStarter: 4,
});

export const AudioSignalDisposition = Object.freeze({
  active: 1,
  declaredSilent: 2,
});

export const ControlKind = Object.freeze({
  throttle: 1,
  ignitionEnabled: 2,
  fuelEnabled: 3,
  limiterEnabled: 4,
  externalResistingTorque: 5,
  starterEnabled: 6,
  heldDynoTargetEngineSpeed: 7,
  heldDynoMaximumAbsorbingTorque: 8,
  heldDynoMaximumDrivingTorque: 9,
  vehicleSelectedForwardGear: 10,
  vehicleClutchEngagement: 11,
  vehicleServiceBrakeApplication: 12,
});

export const ControlCapability = Object.freeze({
  throttle: 1 << 0,
  ignitionEnabled: 1 << 1,
  fuelEnabled: 1 << 2,
  limiterEnabled: 1 << 3,
  externalResistingTorque: 1 << 4,
  starterEnabled: 1 << 5,
  heldDynoTargetEngineSpeed: 1 << 6,
  heldDynoMaximumAbsorbingTorque: 1 << 7,
  heldDynoMaximumDrivingTorque: 1 << 8,
  vehicleSelectedForwardGear: 1 << 9,
  vehicleClutchEngagement: 1 << 10,
  vehicleServiceBrakeApplication: 1 << 11,
});

export const SessionExecutionKind = Object.freeze({
  finiteScenario: 1,
  openEnded: 2,
});

export const MotionMode = Object.freeze({
  heldSpeed: 1,
  prescribedKinematicSweep: 2,
  heldDyno: 3,
  loadTargetHeldCapture: 4,
  inertialDyno: 5,
  freeEngine: 6,
  freeVehicle: 7,
});

export const HeldDynoDisposition = Object.freeze({
  tracking: 1,
  absorbingTorqueLimited: 2,
  drivingTorqueLimited: 3,
});

export const ClutchDisposition = Object.freeze({
  neutral: 1,
  disengaged: 2,
  engineDrivingTorqueLimited: 3,
  vehicleBackdriveTorqueLimited: 4,
  tracking: 5,
});

export const RoadLoadDisposition = Object.freeze({
  moving: 1,
  stoppedWithinStep: 2,
  heldAtRest: 3,
});

export const EngineCycleState = Object.freeze({
  ignitionEnabled: 1 << 0,
  fuelEnabled: 1 << 1,
  starterEnabled: 1 << 2,
  dynoEnabled: 1 << 3,
  limiterEnabled: 1 << 4,
  limiterCutActive: 1 << 5,
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
  "bake-failed",
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
  "bake",
]);

export function statusName(status) {
  return STATUS_NAMES[status] ?? `unknown-status-${status}`;
}

export function errorStageName(stage) {
  return ERROR_STAGE_NAMES[stage] ?? `unknown-stage-${stage}`;
}

export function audioBusKindName(kind) {
  switch (kind) {
    case AudioBusKind.sourceRouteDry:
      return "source-route-dry";
    case AudioBusKind.sourceRouteConfiguredTransfer:
      return "source-route-configured-transfer";
    case AudioBusKind.sourceRouteSelected:
      return "source-route-selected";
    case AudioBusKind.engineRawMaster:
      return "engine-raw-master";
    case AudioBusKind.engineAuditionMaster:
      return "engine-audition-master";
    default:
      return `unknown-audio-bus-${kind}`;
  }
}

export function sourceRouteKindName(kind) {
  switch (kind) {
    case SourceRouteKind.unspecified:
      return "unspecified";
    case SourceRouteKind.exhaustOutlet:
      return "exhaust-outlet";
    case SourceRouteKind.intakeInlet:
      return "intake-inlet";
    case SourceRouteKind.mechanicalEngine:
      return "mechanical-engine";
    case SourceRouteKind.mechanicalStarter:
      return "mechanical-starter";
    default:
      return `unknown-source-route-${kind}`;
  }
}

export function audioSignalDispositionName(disposition) {
  switch (disposition) {
    case AudioSignalDisposition.active:
      return "active";
    case AudioSignalDisposition.declaredSilent:
      return "declared-silent";
    default:
      return `unknown-audio-signal-disposition-${disposition}`;
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

export function sessionExecutionKindName(kind) {
  switch (kind) {
    case SessionExecutionKind.finiteScenario:
      return "finite-scenario";
    case SessionExecutionKind.openEnded:
      return "open-ended";
    default:
      return `unknown-session-execution-${kind}`;
  }
}

export function motionModeName(mode) {
  switch (mode) {
    case MotionMode.heldSpeed:
      return "held-speed";
    case MotionMode.prescribedKinematicSweep:
      return "prescribed-kinematic-sweep";
    case MotionMode.heldDyno:
      return "held-dyno";
    case MotionMode.loadTargetHeldCapture:
      return "load-target-held-capture";
    case MotionMode.inertialDyno:
      return "inertial-dyno";
    case MotionMode.freeEngine:
      return "free-engine";
    case MotionMode.freeVehicle:
      return "free-vehicle";
    default:
      return `unknown-motion-mode-${mode}`;
  }
}

export function heldDynoDispositionName(disposition) {
  switch (disposition) {
    case HeldDynoDisposition.tracking:
      return "tracking";
    case HeldDynoDisposition.absorbingTorqueLimited:
      return "absorbing-torque-limited";
    case HeldDynoDisposition.drivingTorqueLimited:
      return "driving-torque-limited";
    default:
      return `unknown-held-dyno-disposition-${disposition}`;
  }
}

export function clutchDispositionName(disposition) {
  switch (disposition) {
    case ClutchDisposition.neutral:
      return "neutral";
    case ClutchDisposition.disengaged:
      return "disengaged";
    case ClutchDisposition.engineDrivingTorqueLimited:
      return "engine-driving-torque-limited";
    case ClutchDisposition.vehicleBackdriveTorqueLimited:
      return "vehicle-backdrive-torque-limited";
    case ClutchDisposition.tracking:
      return "tracking";
    default:
      return `unknown-clutch-disposition-${disposition}`;
  }
}

export function roadLoadDispositionName(disposition) {
  switch (disposition) {
    case RoadLoadDisposition.moving:
      return "moving";
    case RoadLoadDisposition.stoppedWithinStep:
      return "stopped-within-step";
    case RoadLoadDisposition.heldAtRest:
      return "held-at-rest";
    default:
      return `unknown-road-load-disposition-${disposition}`;
  }
}

// Every offset is a wasm32 clang C layout offset. Startup checks the public
// crankwave_abi_layout_t sizes before any of these layouts are used.
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
  crankwaveBakeInputs: Object.freeze({
    size: 104,
    engineJsonData: 0,
    engineJsonBytes: 4,
    assets: 8,
    assetCount: 12,
    starterRuntimeData: 16,
    starterRuntimeBytes: 20,
    starterAudioData: 24,
    starterAudioBytes: 28,
    releaseIdentityData: 32,
    releaseIdentityBytes: 36,
    wasmModuleSha256: 40,
    assetCatalogSha256: 72,
  }),
  crankwaveDescriptor: Object.freeze({
    size: 112,
    containerBytes: 0,
    entryCount: 8,
    heldCellCount: 16,
    directionalCaptureCount: 24,
    lifecycleCaptureCount: 32,
    engineIdBytes: 40,
    profileIdBytes: 44,
    containerSha256: 48,
    cacheIdentitySha256: 80,
  }),
  crankwaveIdentityBuffers: Object.freeze({
    size: 16,
    engineIdData: 0,
    engineIdCapacity: 4,
    profileIdData: 8,
    profileIdCapacity: 12,
  }),
  abiLayout: Object.freeze({ size: 48 }),
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
    size: 56,
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
    size: 104,
    maximumDeliveryFrames: 0,
    controlQueueCapacity: 4,
    maximumTelemetryFrames: 8,
    maximumCycleEvidence: 12,
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
    executionKind: 88,
    motionMode: 92,
    forwardGearCount: 96,
  }),
  sessionIdentityBuffers: Object.freeze({
    size: 16,
    engineData: 0,
    engineCapacity: 4,
    scenarioData: 8,
    scenarioCapacity: 12,
  }),
  audioBusDescriptor: Object.freeze({
    size: 48,
    kind: 0,
    channelCount: 4,
    sampleRateNumerator: 8,
    sampleRateDenominator: 16,
    hasRouteId: 24,
    routeId: 28,
    sourceRouteKind: 32,
    signalDisposition: 36,
    idBytes: 40,
  }),
  forwardGearDescriptor: Object.freeze({
    size: 24,
    gearId: 0,
    authoredOrdinal: 4,
    ratio: 8,
    semanticIdBytes: 16,
  }),
  controlCommand: Object.freeze({
    size: 40,
    deliveryFrame: 0,
    sequence: 8,
    kind: 16,
    enabled: 20,
    scalarValue: 24,
    idValue: 32,
    reserved: 36,
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
    size: 96,
    kind: 0,
    blockPhase: 4,
    blockOrdinal: 8,
    firstPhysicsFrame: 16,
    physicsFrameCount: 24,
    firstDeliveryFrame: 32,
    deliveryFrameCount: 40,
    telemetryWritten: 44,
    cycleEvidenceWritten: 48,
    completedPhysicsFrames: 56,
    completedDeliveryFrames: 64,
    completedBlockCount: 72,
    liveControlsAccepted: 80,
    hasHeldSpeedOperatingPoint: 84,
    hasInertialDynoResult: 88,
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
    size: 536,
    engineStepEndIndex: 0,
    validityMask: 8,
    ignitionEnabled: 12,
    fuelEnabled: 16,
    starterEnabled: 20,
    dynoEnabled: 24,
    limiterEnabled: 28,
    limiterCutActive: 32,
    theta: 40,
    thetaCycle: 48,
    angularSpeed: 56,
    angularAcceleration: 64,
    engineSpeedRpm: 72,
    requestedThrottle: 80,
    resolvedThrottle: 88,
    intakePlatePosition: 96,
    mainFlowMultiplier: 104,
    requestedExternalResistingTorque: 112,
    torque: 120,
  }),
  heldDynoTelemetry: Object.freeze({
    size: 48,
    targetEngineSpeedRpm: 0,
    maximumAbsorbingTorqueNm: 8,
    maximumDrivingTorqueNm: 16,
    requiredActuatorTorqueNm: 24,
    appliedActuatorTorqueNm: 32,
    disposition: 40,
  }),
  freeVehicleTelemetry: Object.freeze({
    size: 96,
    vehicleSpeedMS: 0,
    vehicleDistanceM: 8,
    hasSelectedForwardGear: 16,
    selectedForwardGearOrdinal: 20,
    clutchEngagement01: 24,
    serviceBrakeApplication01: 32,
    clutchDisposition: 40,
    hasFinalClutchSlip: 44,
    clutchTorqueCapacityNm: 48,
    appliedAverageClutchTorqueOnEngineNm: 56,
    finalClutchSlipRadS: 64,
    roadLoadDisposition: 72,
    requestedRoadLoadForceN: 80,
    appliedAverageRoadLoadForceN: 88,
  }),
  sessionTelemetry: Object.freeze({
    size: 704,
    physicsStepEnd: 0,
    meanIntakeManifoldPressurePaAbs: 8,
    engine: 16,
    hasHeldDyno: 552,
    hasFreeVehicle: 556,
    heldDyno: 560,
    freeVehicle: 608,
  }),
  cycleBoundaryEvidence: Object.freeze({
    size: 56,
    cycleOrdinal: 0,
    leftPhysicsFrame: 8,
    rightPhysicsFrame: 16,
    fractionFromLeft01: 24,
    thetaUnwrappedRad: 32,
    timeS: 40,
    deliveryFrame: 48,
  }),
  cycleControlEvidence: Object.freeze({
    size: 32,
    timeWeightedMean01: 0,
    minimum01: 8,
    maximum01: 16,
    changeCount: 24,
  }),
  cycleNetShaftEvidence: Object.freeze({
    size: 48,
    angularWorkJ: 0,
    cycleMeanTorqueNm: 8,
    availability: 16,
    completeness: 20,
    unavailableReason: 24,
    includedTerms: 32,
    omittedTerms: 40,
  }),
  completedCycleEvidence: Object.freeze({
    size: 296,
    completedCycleOrdinal: 0,
    startBoundary: 8,
    endBoundary: 64,
    durationS: 120,
    meanEngineSpeedRpm: 128,
    requestedThrottle: 136,
    resolvedEngineThrottle: 168,
    intakePlatePosition: 200,
    instantaneousNetShaft: 232,
    startStateFlags: 280,
    endStateFlags: 284,
    stateTransitionFlags: 288,
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
  CRANKWAVE_C_API_VERSION,
  4,
  4,
  4,
  8,
  1,
  Layout.controlCommand.size,
  Layout.sessionDescriptor.size,
  Layout.forwardGearDescriptor.size,
  Layout.audioBusDescriptor.size,
  Layout.sessionTelemetry.size,
  Layout.completedCycleEvidence.size,
]);
