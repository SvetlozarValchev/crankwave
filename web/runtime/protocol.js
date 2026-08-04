import {
  ControlCapability,
  ESO_C_API_VERSION,
  ESO_CANONICAL_SAMPLE_RATE,
} from "./c-api-abi.js";
import { PCM_RING_HEADER_SCHEMA } from "./pcm-ring-buffer.js";

export const WORKER_PROTOCOL_ID = "engine-sim-offline/browser-worker-v5";

export const LIVE_CONTROL_CAPABILITIES = Object.freeze([
  Object.freeze({
    kind: "throttle",
    mask: ControlCapability.throttle,
    valueType: "number",
    minimum: 0,
    maximum: 1,
  }),
  Object.freeze({
    kind: "ignition",
    mask: ControlCapability.ignitionEnabled,
    valueType: "boolean",
  }),
  Object.freeze({
    kind: "fuel",
    mask: ControlCapability.fuelEnabled,
    valueType: "boolean",
  }),
  Object.freeze({
    kind: "starter",
    mask: ControlCapability.starterEnabled,
    valueType: "boolean",
  }),
  Object.freeze({
    kind: "limiter",
    mask: ControlCapability.limiterEnabled,
    valueType: "boolean",
  }),
  Object.freeze({
    kind: "external-resisting-torque",
    mask: ControlCapability.externalResistingTorque,
    valueType: "number",
    minimum: 0,
    unit: "N*m",
  }),
  Object.freeze({
    kind: "held-dyno-target-engine-speed",
    mask: ControlCapability.heldDynoTargetEngineSpeed,
    valueType: "number",
    exclusiveMinimum: 0,
    unit: "rpm",
  }),
  Object.freeze({
    kind: "held-dyno-maximum-absorbing-torque",
    mask: ControlCapability.heldDynoMaximumAbsorbingTorque,
    valueType: "number",
    minimum: 0,
    unit: "N*m",
  }),
  Object.freeze({
    kind: "held-dyno-maximum-driving-torque",
    mask: ControlCapability.heldDynoMaximumDrivingTorque,
    valueType: "number",
    minimum: 0,
    unit: "N*m",
  }),
  Object.freeze({
    kind: "vehicle-selected-forward-gear",
    mask: ControlCapability.vehicleSelectedForwardGear,
    valueType: "integer",
    minimum: 0,
    unit: "authored-ordinal",
  }),
  Object.freeze({
    kind: "vehicle-clutch-engagement",
    mask: ControlCapability.vehicleClutchEngagement,
    valueType: "number",
    minimum: 0,
    maximum: 1,
  }),
  Object.freeze({
    kind: "vehicle-service-brake-application",
    mask: ControlCapability.vehicleServiceBrakeApplication,
    valueType: "number",
    minimum: 0,
    maximum: 1,
  }),
]);

export function liveControlCapability(kind) {
  return (
    LIVE_CONTROL_CAPABILITIES.find((capability) => capability.kind === kind) ??
    null
  );
}

export function readyMessage(requestId, moduleUrl) {
  return {
    type: "ready",
    requestId,
    protocol: WORKER_PROTOCOL_ID,
    apiVersion: ESO_C_API_VERSION,
    canonicalSampleRate: ESO_CANONICAL_SAMPLE_RATE,
    moduleUrl,
    ringHeaderSchema: PCM_RING_HEADER_SCHEMA,
    structuralEditContract: "compile-and-replace",
  };
}

export function publicDescriptor(program, selectedBusIndex) {
  const descriptor = program.session.descriptor;
  const totalDeliveryFrames =
    descriptor.totalBlockCountBigInt === null
      ? null
      : descriptor.totalBlockCountBigInt *
        BigInt(descriptor.deliveryFramesPerBlock);
  const preparationDeliveryFrames =
    descriptor.preparationBlockCountBigInt *
    BigInt(descriptor.deliveryFramesPerBlock);
  const liveControlCapabilities = descriptor.liveControlCapabilities;
  return {
    executionKind: descriptor.executionKind,
    executionKindCode: descriptor.executionKindCode,
    openEnded: descriptor.executionKind === "open-ended",
    motionMode: descriptor.motionMode,
    motionModeCode: descriptor.motionModeCode,
    liveControlCapabilities,
    controls: LIVE_CONTROL_CAPABILITIES.filter(
      ({ mask }) => (liveControlCapabilities & mask) !== 0,
    ),
    buses: program.session.buses.map((bus) => ({ ...bus })),
    forwardGearCount: descriptor.forwardGearCount,
    forwardGears: descriptor.forwardGears.map((gear) => ({ ...gear })),
    selectedBusIndex,
    totalDeliveryFrames:
      totalDeliveryFrames === null
        ? null
        : totalDeliveryFrames.toString(10),
    preparationDeliveryFrames: preparationDeliveryFrames.toString(10),
    deliverySampleRate: descriptor.deliveryRateHz,
    deliveryFramesPerBlock: descriptor.deliveryFramesPerBlock,
    maximumDeliveryFramesPerProcessCall:
      descriptor.maximumDeliveryFramesPerProcessCall,
    totalBlockCount: descriptor.totalBlockCount,
    preparationBlockCount: descriptor.preparationBlockCount,
    engineId: descriptor.engineId,
    scenarioId: descriptor.scenarioId,
  };
}

export function validationMessage(requestId, error) {
  const document =
    error.stageName?.startsWith("engine-")
      ? "engine"
      : error.stageName?.startsWith("scenario-")
        ? "scenario"
        : "unknown";
  return {
    type: "validation",
    requestId,
    ok: false,
    operation: error.operation ?? null,
    error: error.toJSON?.() ?? {
      name: error.name,
      message: error.message,
    },
    diagnostics: (error.diagnostics ?? []).map((diagnostic) => ({
      document,
      path: diagnostic.jsonPointer,
      severity: diagnostic.severity,
      code: diagnostic.code,
      message: diagnostic.message,
      subject:
        diagnostic.hasSubject
          ? {
              kind: diagnostic.subjectKind,
              id: diagnostic.subjectId,
            }
          : null,
      source:
        diagnostic.hasSourcePosition
          ? {
              byteOffset: diagnostic.sourceByteOffset.toString(10),
              line: diagnostic.sourceLine,
              column: diagnostic.sourceColumn,
            }
          : null,
      related: diagnostic.related,
    })),
  };
}
