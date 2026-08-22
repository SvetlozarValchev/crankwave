import {
  AudioSignalDisposition,
  AudioBusKind,
  BlockPhase,
  ControlCapability,
  ControlKind,
  CRANKWAVE_CANONICAL_SAMPLE_RATE,
  CRANKWAVE_INVALID_HANDLE,
  Layout,
  MotionMode,
  ProcessKind,
  QUANTITY_FIELDS,
  SessionExecutionKind,
  SourceRouteKind,
  TORQUE_FIELDS,
  audioBusKindName,
  audioSignalDispositionName,
  sourceRouteKindName,
  blockPhaseName,
  clutchDispositionName,
  heldDynoDispositionName,
  motionModeName,
  roadLoadDispositionName,
  sessionExecutionKindName,
} from "./c-api-abi.js";
import { CrankwaveRuntimeError } from "./c-api-errors.js";

function decimal(value) {
  return value.toString(10);
}

function exactRate(numerator, denominator, label) {
  if (denominator === 0n) {
    throw new CrankwaveRuntimeError(`${label} has a zero denominator`, {
      operation: "inspect-session",
      detailCode: "browser-runtime-invalid-rational-rate",
      diagnostics: [],
    });
  }
  const value = Number(numerator) / Number(denominator);
  if (!Number.isFinite(value) || value <= 0) {
    throw new CrankwaveRuntimeError(`${label} is not a finite positive rate`, {
      operation: "inspect-session",
      detailCode: "browser-runtime-invalid-rational-rate",
      diagnostics: [],
    });
  }
  return value;
}

function normalizeU64(value, label) {
  let result;
  try {
    result = BigInt(value);
  } catch {
    throw new TypeError(`${label} must be an unsigned 64-bit integer`);
  }
  if (result < 0n || result > 0xffff_ffff_ffff_ffffn) {
    throw new RangeError(`${label} is outside the unsigned 64-bit range`);
  }
  return result;
}

function requireFiniteNumber(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError(`${label} must be a finite number`);
  }
  return value;
}

function requireUnitInterval(value, label) {
  const result = requireFiniteNumber(value, label);
  if (Object.is(result, -0) || result < 0 || result > 1) {
    throw new TypeError(`${label} must be in [0, 1]`);
  }
  return result;
}

function requireNonnegative(value, label) {
  const result = requireFiniteNumber(value, label);
  if (Object.is(result, -0) || result < 0) {
    throw new TypeError(`${label} must be nonnegative`);
  }
  return result;
}

function requirePositive(value, label) {
  const result = requireFiniteNumber(value, label);
  if (result <= 0) {
    throw new TypeError(`${label} must be positive`);
  }
  return result;
}

function readQuantity(view, pointer) {
  const layout = Layout.quantityValue;
  return {
    value: view.getFloat64(pointer + layout.value, true),
    availability: view.getUint32(pointer + layout.availability, true),
    completeness: view.getUint32(pointer + layout.completeness, true),
    unavailableReason: view.getUint32(pointer + layout.unavailableReason, true),
  };
}

function readTorqueValue(view, pointer) {
  const layout = Layout.torqueValue;
  return {
    valueNm: view.getFloat64(pointer + layout.value, true),
    availability: view.getUint32(pointer + layout.availability, true),
    completeness: view.getUint32(pointer + layout.completeness, true),
    unavailableReason: view.getUint32(pointer + layout.unavailableReason, true),
    includedTerms: decimal(view.getBigUint64(pointer + layout.includedTerms, true)),
    omittedTerms: decimal(view.getBigUint64(pointer + layout.omittedTerms, true)),
  };
}

function readEngineTelemetry(view, pointer) {
  const layout = Layout.engineTelemetry;
  const torque = {};
  let fieldPointer = pointer + layout.torque;
  for (const name of TORQUE_FIELDS) {
    torque[name] = readTorqueValue(view, fieldPointer);
    fieldPointer += Layout.torqueValue.size;
  }
  for (const name of QUANTITY_FIELDS) {
    torque[name] = readQuantity(view, fieldPointer);
    fieldPointer += Layout.quantityValue.size;
  }
  return {
    engineStepEndIndex: decimal(
      view.getBigUint64(pointer + layout.engineStepEndIndex, true),
    ),
    validityMask: view.getUint32(pointer + layout.validityMask, true),
    ignitionEnabled:
      view.getUint32(pointer + layout.ignitionEnabled, true) !== 0,
    fuelEnabled: view.getUint32(pointer + layout.fuelEnabled, true) !== 0,
    starterEnabled: view.getUint32(pointer + layout.starterEnabled, true) !== 0,
    dynoEnabled: view.getUint32(pointer + layout.dynoEnabled, true) !== 0,
    limiterEnabled:
      view.getUint32(pointer + layout.limiterEnabled, true) !== 0,
    limiterCutActive:
      view.getUint32(pointer + layout.limiterCutActive, true) !== 0,
    thetaRad: view.getFloat64(pointer + layout.theta, true),
    thetaCycleRad: view.getFloat64(pointer + layout.thetaCycle, true),
    angularSpeedRadS: view.getFloat64(pointer + layout.angularSpeed, true),
    angularAccelerationRadS2: view.getFloat64(
      pointer + layout.angularAcceleration,
      true,
    ),
    engineSpeedRpm: view.getFloat64(pointer + layout.engineSpeedRpm, true),
    requestedThrottle01: view.getFloat64(
      pointer + layout.requestedThrottle,
      true,
    ),
    resolvedThrottle01: view.getFloat64(
      pointer + layout.resolvedThrottle,
      true,
    ),
    intakePlatePosition01: view.getFloat64(
      pointer + layout.intakePlatePosition,
      true,
    ),
    mainFlowMultiplier01: view.getFloat64(
      pointer + layout.mainFlowMultiplier,
      true,
    ),
    requestedExternalResistingTorqueNm: view.getFloat64(
      pointer + layout.requestedExternalResistingTorque,
      true,
    ),
    torque,
  };
}

function readPresenceFlag(view, pointer, label) {
  const value = view.getUint32(pointer, true);
  if (value !== 0 && value !== 1) {
    throw new CrankwaveRuntimeError(`${label} is not a canonical presence flag`, {
      operation: "process-session",
      detailCode: "browser-runtime-telemetry-presence-invalid",
      diagnostics: [],
    });
  }
  return value === 1;
}

function readHeldDynoTelemetry(view, pointer) {
  const layout = Layout.heldDynoTelemetry;
  const dispositionCode = view.getUint32(pointer + layout.disposition, true);
  return {
    targetEngineSpeedRpm: view.getFloat64(
      pointer + layout.targetEngineSpeedRpm,
      true,
    ),
    maximumAbsorbingTorqueNm: view.getFloat64(
      pointer + layout.maximumAbsorbingTorqueNm,
      true,
    ),
    maximumDrivingTorqueNm: view.getFloat64(
      pointer + layout.maximumDrivingTorqueNm,
      true,
    ),
    requiredActuatorTorqueNm: view.getFloat64(
      pointer + layout.requiredActuatorTorqueNm,
      true,
    ),
    appliedActuatorTorqueNm: view.getFloat64(
      pointer + layout.appliedActuatorTorqueNm,
      true,
    ),
    disposition: heldDynoDispositionName(dispositionCode),
    dispositionCode,
  };
}

function readFreeVehicleTelemetry(view, pointer) {
  const layout = Layout.freeVehicleTelemetry;
  const hasSelectedForwardGear = readPresenceFlag(
    view,
    pointer + layout.hasSelectedForwardGear,
    "free-vehicle selected-forward-gear presence",
  );
  const hasFinalClutchSlip = readPresenceFlag(
    view,
    pointer + layout.hasFinalClutchSlip,
    "free-vehicle final-clutch-slip presence",
  );
  const clutchDispositionCode = view.getUint32(
    pointer + layout.clutchDisposition,
    true,
  );
  const roadLoadDispositionCode = view.getUint32(
    pointer + layout.roadLoadDisposition,
    true,
  );
  return {
    vehicleSpeedMS: view.getFloat64(pointer + layout.vehicleSpeedMS, true),
    vehicleDistanceM: view.getFloat64(pointer + layout.vehicleDistanceM, true),
    selectedForwardGearOrdinal: hasSelectedForwardGear
      ? view.getUint32(pointer + layout.selectedForwardGearOrdinal, true)
      : null,
    clutchEngagement01: view.getFloat64(
      pointer + layout.clutchEngagement01,
      true,
    ),
    serviceBrakeApplication01: view.getFloat64(
      pointer + layout.serviceBrakeApplication01,
      true,
    ),
    clutchDisposition: clutchDispositionName(clutchDispositionCode),
    clutchDispositionCode,
    clutchTorqueCapacityNm: view.getFloat64(
      pointer + layout.clutchTorqueCapacityNm,
      true,
    ),
    appliedAverageClutchTorqueOnEngineNm: view.getFloat64(
      pointer + layout.appliedAverageClutchTorqueOnEngineNm,
      true,
    ),
    finalClutchSlipRadS: hasFinalClutchSlip
      ? view.getFloat64(pointer + layout.finalClutchSlipRadS, true)
      : null,
    roadLoadDisposition: roadLoadDispositionName(roadLoadDispositionCode),
    roadLoadDispositionCode,
    requestedRoadLoadForceN: view.getFloat64(
      pointer + layout.requestedRoadLoadForceN,
      true,
    ),
    appliedAverageRoadLoadForceN: view.getFloat64(
      pointer + layout.appliedAverageRoadLoadForceN,
      true,
    ),
  };
}

function readSessionTelemetry(view, pointer) {
  const layout = Layout.sessionTelemetry;
  const hasHeldDyno = readPresenceFlag(
    view,
    pointer + layout.hasHeldDyno,
    "held-dyno sidecar presence",
  );
  const hasFreeVehicle = readPresenceFlag(
    view,
    pointer + layout.hasFreeVehicle,
    "free-vehicle sidecar presence",
  );
  return {
    physicsStepEnd: decimal(
      view.getBigUint64(pointer + layout.physicsStepEnd, true),
    ),
    meanIntakeManifoldPressurePaAbs: view.getFloat64(
      pointer + layout.meanIntakeManifoldPressurePaAbs,
      true,
    ),
    ...readEngineTelemetry(view, pointer + layout.engine),
    heldDyno: hasHeldDyno
      ? readHeldDynoTelemetry(view, pointer + layout.heldDyno)
      : null,
    freeVehicle: hasFreeVehicle
      ? readFreeVehicleTelemetry(view, pointer + layout.freeVehicle)
      : null,
  };
}

function readCycleBoundaryEvidence(view, pointer) {
  const layout = Layout.cycleBoundaryEvidence;
  return {
    cycleOrdinal: decimal(view.getBigInt64(pointer + layout.cycleOrdinal, true)),
    leftPhysicsFrame: decimal(
      view.getBigUint64(pointer + layout.leftPhysicsFrame, true),
    ),
    rightPhysicsFrame: decimal(
      view.getBigUint64(pointer + layout.rightPhysicsFrame, true),
    ),
    fractionFromLeft01: view.getFloat64(
      pointer + layout.fractionFromLeft01,
      true,
    ),
    thetaUnwrappedRad: view.getFloat64(
      pointer + layout.thetaUnwrappedRad,
      true,
    ),
    timeS: view.getFloat64(pointer + layout.timeS, true),
    deliveryFrame: view.getFloat64(pointer + layout.deliveryFrame, true),
  };
}

function readCycleControlEvidence(view, pointer) {
  const layout = Layout.cycleControlEvidence;
  return {
    timeWeightedMean01: view.getFloat64(
      pointer + layout.timeWeightedMean01,
      true,
    ),
    minimum01: view.getFloat64(pointer + layout.minimum01, true),
    maximum01: view.getFloat64(pointer + layout.maximum01, true),
    changeCount: view.getUint32(pointer + layout.changeCount, true),
  };
}

function readCycleNetShaftEvidence(view, pointer) {
  const layout = Layout.cycleNetShaftEvidence;
  return {
    angularWorkJ: view.getFloat64(pointer + layout.angularWorkJ, true),
    cycleMeanTorqueNm: view.getFloat64(
      pointer + layout.cycleMeanTorqueNm,
      true,
    ),
    availability: view.getUint32(pointer + layout.availability, true),
    completeness: view.getUint32(pointer + layout.completeness, true),
    unavailableReason: view.getUint32(
      pointer + layout.unavailableReason,
      true,
    ),
    includedTerms: decimal(
      view.getBigUint64(pointer + layout.includedTerms, true),
    ),
    omittedTerms: decimal(
      view.getBigUint64(pointer + layout.omittedTerms, true),
    ),
  };
}

function readCompletedCycleEvidence(view, pointer) {
  const layout = Layout.completedCycleEvidence;
  return {
    completedCycleOrdinal: decimal(
      view.getBigUint64(pointer + layout.completedCycleOrdinal, true),
    ),
    startBoundary: readCycleBoundaryEvidence(
      view,
      pointer + layout.startBoundary,
    ),
    endBoundary: readCycleBoundaryEvidence(view, pointer + layout.endBoundary),
    durationS: view.getFloat64(pointer + layout.durationS, true),
    meanEngineSpeedRpm: view.getFloat64(
      pointer + layout.meanEngineSpeedRpm,
      true,
    ),
    requestedThrottle: readCycleControlEvidence(
      view,
      pointer + layout.requestedThrottle,
    ),
    resolvedEngineThrottle: readCycleControlEvidence(
      view,
      pointer + layout.resolvedEngineThrottle,
    ),
    intakePlatePosition: readCycleControlEvidence(
      view,
      pointer + layout.intakePlatePosition,
    ),
    instantaneousNetShaft: readCycleNetShaftEvidence(
      view,
      pointer + layout.instantaneousNetShaft,
    ),
    startStateFlags: view.getUint32(pointer + layout.startStateFlags, true),
    endStateFlags: view.getUint32(pointer + layout.endStateFlags, true),
    stateTransitionFlags: view.getUint32(
      pointer + layout.stateTransitionFlags,
      true,
    ),
  };
}

function readProcessInfo(view, pointer) {
  const layout = Layout.processInfo;
  const kind = view.getUint32(pointer + layout.kind, true);
  const phase = view.getUint32(pointer + layout.blockPhase, true);
  return {
    kind: kind === ProcessKind.block ? "block" : "completed",
    kindCode: kind,
    blockPhase: blockPhaseName(phase),
    blockPhaseCode: phase,
    blockOrdinal: decimal(view.getBigUint64(pointer + layout.blockOrdinal, true)),
    firstPhysicsFrame: decimal(
      view.getBigUint64(pointer + layout.firstPhysicsFrame, true),
    ),
    physicsFrameCount: view.getUint32(
      pointer + layout.physicsFrameCount,
      true,
    ),
    firstDeliveryFrame: decimal(
      view.getBigUint64(pointer + layout.firstDeliveryFrame, true),
    ),
    deliveryFrameCount: view.getUint32(
      pointer + layout.deliveryFrameCount,
      true,
    ),
    telemetryWritten: view.getUint32(pointer + layout.telemetryWritten, true),
    cycleEvidenceWritten: view.getUint32(
      pointer + layout.cycleEvidenceWritten,
      true,
    ),
    completedPhysicsFrameCount: decimal(
      view.getBigUint64(pointer + layout.completedPhysicsFrames, true),
    ),
    completedDeliveryFrameCount: decimal(
      view.getBigUint64(pointer + layout.completedDeliveryFrames, true),
    ),
    completedBlockCount: decimal(
      view.getBigUint64(pointer + layout.completedBlockCount, true),
    ),
    liveControlsAccepted:
      view.getUint32(pointer + layout.liveControlsAccepted, true) !== 0,
    hasHeldSpeedOperatingPoint:
      view.getUint32(pointer + layout.hasHeldSpeedOperatingPoint, true) !== 0,
    hasInertialDynoResult:
      view.getUint32(pointer + layout.hasInertialDynoResult, true) !== 0,
  };
}

export class CrankwaveSession {
  #client;
  #module;
  #heap;
  #context;
  #handle;
  #descriptor;
  #forwardGears;
  #buses;
  #auditionBusIndex;
  #audioPointer = 0;
  #audioCopyPointer = 0;
  #telemetryPointer = 0;
  #cycleEvidencePointer = 0;
  #processPointer = 0;
  #disposed = false;
  #terminal = false;
  #nextDeliveryFrame = 0n;
  #nextControlSequence = 1n;

  constructor(client, handle) {
    this.#client = client;
    this.#module = client.module;
    this.#heap = client.heap;
    this.#context = client.context;
    this.#handle = handle;
    try {
      this.#descriptor = this.#readDescriptor();
      this.#forwardGears = this.#readForwardGears();
      this.#descriptor.forwardGears = this.#forwardGears;
      this.#buses = this.#readBuses();
      const audition = this.#buses.filter(
        (bus) => bus.kindCode === AudioBusKind.engineAuditionMaster,
      );
      if (audition.length !== 1) {
        throw new CrankwaveRuntimeError(
          `the session exposes ${audition.length} audition master buses`,
          {
            operation: "inspect-session",
            detailCode: "browser-runtime-audition-bus-cardinality",
            diagnostics: [],
          },
        );
      }
      this.#auditionBusIndex = audition[0].index;
      if (
        audition[0].sampleRateHz !== CRANKWAVE_CANONICAL_SAMPLE_RATE ||
        this.#descriptor.deliveryRateHz !== CRANKWAVE_CANONICAL_SAMPLE_RATE
      ) {
        throw new CrankwaveRuntimeError(
          "the browser runtime admits only a canonical 192 kHz master",
          {
            operation: "inspect-session",
            detailCode: "browser-runtime-noncanonical-master-rate",
            diagnostics: [],
          },
        );
      }
      for (const bus of this.#buses) {
        if (bus.sampleRateHz !== CRANKWAVE_CANONICAL_SAMPLE_RATE) {
          throw new CrankwaveRuntimeError(
            `audio bus ${bus.id} is not at the canonical 192 kHz rate`,
            {
              operation: "inspect-session",
              detailCode: "browser-runtime-noncanonical-bus-rate",
              diagnostics: [],
            },
          );
        }
      }
      const maximumChannels = Math.max(
        ...this.#buses.map((bus) => bus.channelCount),
      );
      const maximumSamples =
        this.#descriptor.maximumDeliveryFramesPerProcessCall *
        maximumChannels;
      this.#audioPointer = this.#heap.allocate(
        maximumSamples * Float32Array.BYTES_PER_ELEMENT,
        "audition-master process block",
      );
      this.#audioCopyPointer = this.#heap.allocate(
        Layout.audioCopyBuffer.size,
        "audio-copy descriptor",
      );
      this.#telemetryPointer = this.#heap.allocate(
        this.#descriptor.maximumTelemetryFramesPerProcessCall *
          Layout.sessionTelemetry.size,
        "session telemetry block",
      );
      this.#cycleEvidencePointer = this.#heap.allocate(
        this.#descriptor.maximumCycleEvidencePerProcessCall *
          Layout.completedCycleEvidence.size,
        "completed-cycle evidence block",
      );
      this.#processPointer = this.#heap.allocate(
        Layout.processInfo.size,
        "process result",
      );
    } catch (error) {
      this.dispose();
      throw error;
    }
  }

  get descriptor() {
    this.#assertAlive();
    return this.#descriptor;
  }

  get buses() {
    this.#assertAlive();
    return this.#buses;
  }

  get forwardGears() {
    this.#assertAlive();
    return this.#forwardGears;
  }

  get auditionBus() {
    this.#assertAlive();
    return this.#buses[this.#auditionBusIndex];
  }

  get auditionBusIndex() {
    this.#assertAlive();
    return this.#auditionBusIndex;
  }

  get terminal() {
    return this.#terminal;
  }

  get nextDeliveryFrame() {
    return this.#nextDeliveryFrame;
  }

  get firstAudibleDeliveryFrame() {
    return (
      this.#descriptor.preparationBlockCountBigInt *
      BigInt(this.#descriptor.deliveryFramesPerBlock)
    );
  }

  enqueueControls(controls) {
    this.#assertAlive();
    if (this.#descriptor.liveControlCapabilities === 0) {
      throw new CrankwaveRuntimeError(
        "this session mode does not accept live controls",
        {
          operation: "enqueue-controls",
          detailCode: "browser-runtime-controls-unavailable",
          diagnostics: [],
        },
      );
    }
    if (!Array.isArray(controls) || controls.length === 0) {
      throw new TypeError("controls must be a non-empty array");
    }
    if (controls.length > this.#descriptor.controlCommandQueueCapacity) {
      throw new CrankwaveRuntimeError(
        "control batch exceeds the session queue capacity",
        {
          operation: "enqueue-controls",
          detailCode: "browser-runtime-control-capacity",
          diagnostics: [],
        },
      );
    }
    const pointer = this.#heap.allocate(
      controls.length * Layout.controlCommand.size,
      "live-control command batch",
    );
    const rejection = this.#heap.allocate(
      Layout.controlRejection.size,
      "live-control rejection",
    );
    try {
      const view = this.#heap.view;
      this.#heap.bytes.fill(
        0,
        pointer,
        pointer + controls.length * Layout.controlCommand.size,
      );
      let priorFrame = null;
      for (let index = 0; index < controls.length; ++index) {
        const input = controls[index];
        const frame = normalizeU64(input.deliveryFrame, "control deliveryFrame");
        if (priorFrame !== null && frame < priorFrame) {
          throw new TypeError("controls must be ordered by deliveryFrame");
        }
        priorFrame = frame;
        const base = pointer + index * Layout.controlCommand.size;
        view.setBigUint64(base + Layout.controlCommand.deliveryFrame, frame, true);
        view.setBigUint64(
          base + Layout.controlCommand.sequence,
          this.#nextControlSequence,
          true,
        );
        ++this.#nextControlSequence;
        let requiredCapability = 0;
        switch (input.kind) {
          case "throttle": {
            const value = requireUnitInterval(
              input.value,
              "throttle control value",
            );
            requiredCapability = ControlCapability.throttle;
            view.setUint32(base + Layout.controlCommand.kind, ControlKind.throttle, true);
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              value,
              true,
            );
            break;
          }
          case "ignition":
            requiredCapability = ControlCapability.ignitionEnabled;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.ignitionEnabled,
              true,
            );
            if (typeof input.value !== "boolean") {
              throw new TypeError("ignition control value must be boolean");
            }
            view.setUint32(
              base + Layout.controlCommand.enabled,
              input.value ? 1 : 0,
              true,
            );
            break;
          case "fuel":
            requiredCapability = ControlCapability.fuelEnabled;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.fuelEnabled,
              true,
            );
            if (typeof input.value !== "boolean") {
              throw new TypeError("fuel control value must be boolean");
            }
            view.setUint32(
              base + Layout.controlCommand.enabled,
              input.value ? 1 : 0,
              true,
            );
            break;
          case "starter":
            requiredCapability = ControlCapability.starterEnabled;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.starterEnabled,
              true,
            );
            if (typeof input.value !== "boolean") {
              throw new TypeError("starter control value must be boolean");
            }
            view.setUint32(
              base + Layout.controlCommand.enabled,
              input.value ? 1 : 0,
              true,
            );
            break;
          case "limiter":
            requiredCapability = ControlCapability.limiterEnabled;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.limiterEnabled,
              true,
            );
            if (typeof input.value !== "boolean") {
              throw new TypeError("limiter control value must be boolean");
            }
            view.setUint32(
              base + Layout.controlCommand.enabled,
              input.value ? 1 : 0,
              true,
            );
            break;
          case "external-resisting-torque":
            requiredCapability = ControlCapability.externalResistingTorque;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.externalResistingTorque,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requireNonnegative(
                input.value,
                "external resisting torque",
              ),
              true,
            );
            break;
          case "held-dyno-target-engine-speed":
            requiredCapability = ControlCapability.heldDynoTargetEngineSpeed;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.heldDynoTargetEngineSpeed,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requirePositive(
                input.value,
                "held-dyno target engine speed",
              ),
              true,
            );
            break;
          case "held-dyno-maximum-absorbing-torque":
            requiredCapability =
              ControlCapability.heldDynoMaximumAbsorbingTorque;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.heldDynoMaximumAbsorbingTorque,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requireNonnegative(
                input.value,
                "held-dyno maximum absorbing torque",
              ),
              true,
            );
            break;
          case "held-dyno-maximum-driving-torque":
            requiredCapability =
              ControlCapability.heldDynoMaximumDrivingTorque;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.heldDynoMaximumDrivingTorque,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requireNonnegative(
                input.value,
                "held-dyno maximum driving torque",
              ),
              true,
            );
            break;
          case "vehicle-selected-forward-gear": {
            requiredCapability = ControlCapability.vehicleSelectedForwardGear;
            if (
              !Number.isSafeInteger(input.value) ||
              Object.is(input.value, -0) ||
              input.value < 0 ||
              input.value > this.#descriptor.forwardGearCount
            ) {
              throw new TypeError(
                "vehicle selected forward gear must be zero for neutral or a published authored ordinal",
              );
            }
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.vehicleSelectedForwardGear,
              true,
            );
            view.setUint32(
              base + Layout.controlCommand.idValue,
              input.value,
              true,
            );
            break;
          }
          case "vehicle-clutch-engagement":
            requiredCapability = ControlCapability.vehicleClutchEngagement;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.vehicleClutchEngagement,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requireUnitInterval(
                input.value,
                "vehicle clutch engagement",
              ),
              true,
            );
            break;
          case "vehicle-service-brake-application":
            requiredCapability =
              ControlCapability.vehicleServiceBrakeApplication;
            view.setUint32(
              base + Layout.controlCommand.kind,
              ControlKind.vehicleServiceBrakeApplication,
              true,
            );
            view.setFloat64(
              base + Layout.controlCommand.scalarValue,
              requireUnitInterval(
                input.value,
                "vehicle service-brake application",
              ),
              true,
            );
            break;
          default:
            throw new CrankwaveRuntimeError(
              `unsupported live control: ${String(input.kind)}`,
              {
                operation: "enqueue-controls",
                detailCode: "browser-runtime-unsupported-control",
                diagnostics: [],
              },
            );
        }
        if (
          (this.#descriptor.liveControlCapabilities & requiredCapability) ===
          0
        ) {
          throw new CrankwaveRuntimeError(
            `${input.kind} was not admitted for this session mode`,
            {
              operation: "enqueue-controls",
              detailCode: "browser-runtime-control-not-admitted",
              diagnostics: [],
            },
          );
        }
      }
      const status = this.#module._crankwave_session_enqueue_controls(
        this.#context,
        this.#handle,
        pointer,
        controls.length,
        rejection,
      );
      this.#client.assertStatus(status, "enqueue-controls");
      return { accepted: controls.length };
    } finally {
      this.#heap.free(rejection);
      this.#heap.free(pointer);
    }
  }

  processBlock(busIndex = this.#auditionBusIndex) {
    this.#assertAlive();
    const audio = Layout.audioCopyBuffer;
    if (
      !Number.isSafeInteger(busIndex) ||
      busIndex < 0 ||
      busIndex >= this.#buses.length
    ) {
      throw new CrankwaveRuntimeError(
        `audio bus index ${String(busIndex)} is outside the session descriptor`,
        {
          operation: "process-session",
          detailCode: "browser-runtime-audio-bus-index-invalid",
          diagnostics: [],
        },
      );
    }
    const bus = this.#buses[busIndex];
    const maximumSamples =
      this.#descriptor.maximumDeliveryFramesPerProcessCall * bus.channelCount;
    const view = this.#heap.view;
    this.#heap.bytes.fill(
      0,
      this.#audioCopyPointer,
      this.#audioCopyPointer + audio.size,
    );
    this.#heap.bytes.fill(
      0,
      this.#processPointer,
      this.#processPointer + Layout.processInfo.size,
    );
    view.setUint32(
      this.#audioCopyPointer + audio.busIndex,
      busIndex,
      true,
    );
    view.setUint32(
      this.#audioCopyPointer + audio.samples,
      this.#audioPointer,
      true,
    );
    view.setUint32(
      this.#audioCopyPointer + audio.sampleCapacity,
      maximumSamples,
      true,
    );
    const status = this.#module._crankwave_session_process(
      this.#context,
      this.#handle,
      this.#audioCopyPointer,
      1,
      this.#telemetryPointer,
      this.#descriptor.maximumTelemetryFramesPerProcessCall,
      this.#cycleEvidencePointer,
      this.#descriptor.maximumCycleEvidencePerProcessCall,
      this.#processPointer,
    );
    this.#client.assertStatus(status, "process-session");
    const process = readProcessInfo(view, this.#processPointer);
    const telemetry = [];
    for (let index = 0; index < process.telemetryWritten; ++index) {
      telemetry.push(
        readSessionTelemetry(
          view,
          this.#telemetryPointer + index * Layout.sessionTelemetry.size,
        ),
      );
    }
    if (
      process.cycleEvidenceWritten >
      this.#descriptor.maximumCycleEvidencePerProcessCall
    ) {
      throw new CrankwaveRuntimeError(
        "the C API reported more completed cycles than the supplied buffer can hold",
        {
          operation: "process-session",
          detailCode: "browser-runtime-cycle-evidence-write-overflow",
          diagnostics: [],
        },
      );
    }
    const completedCycles = [];
    for (let index = 0; index < process.cycleEvidenceWritten; ++index) {
      completedCycles.push(
        readCompletedCycleEvidence(
          view,
          this.#cycleEvidencePointer +
            index * Layout.completedCycleEvidence.size,
        ),
      );
    }
    const samplesWritten = view.getUint32(
      this.#audioCopyPointer + audio.samplesWritten,
      true,
    );
    if (samplesWritten > maximumSamples) {
      throw new CrankwaveRuntimeError(
        "the C API reported more audio samples than the supplied buffer can hold",
        {
          operation: "process-session",
          detailCode: "browser-runtime-audio-write-overflow",
          diagnostics: [],
        },
      );
    }
    const samples = new Float32Array(samplesWritten);
    samples.set(
      new Float32Array(
        this.#heap.buffer,
        this.#audioPointer,
        samplesWritten,
      ),
    );
    if (process.kindCode === ProcessKind.completed) {
      this.#terminal = true;
    } else if (process.kindCode === ProcessKind.block) {
      this.#nextDeliveryFrame =
        BigInt(process.firstDeliveryFrame) + BigInt(process.deliveryFrameCount);
    } else {
      throw new CrankwaveRuntimeError(
        `the C API returned unknown process kind ${process.kindCode}`,
        {
          operation: "process-session",
          detailCode: "browser-runtime-process-kind-invalid",
          diagnostics: [],
        },
      );
    }
    return {
      process,
      telemetry,
      completedCycles,
      samples,
      bus,
      audible: process.blockPhaseCode === BlockPhase.audible,
    };
  }

  dispose() {
    if (this.#disposed) {
      return;
    }
    this.#disposed = true;
    this.#heap?.free(this.#processPointer);
    this.#heap?.free(this.#cycleEvidencePointer);
    this.#heap?.free(this.#telemetryPointer);
    this.#heap?.free(this.#audioCopyPointer);
    this.#heap?.free(this.#audioPointer);
    this.#processPointer = 0;
    this.#cycleEvidencePointer = 0;
    this.#telemetryPointer = 0;
    this.#audioCopyPointer = 0;
    this.#audioPointer = 0;
    if (
      this.#client &&
      this.#handle !== CRANKWAVE_INVALID_HANDLE
    ) {
      const status = this.#module._crankwave_destroy_session(
        this.#context,
        this.#handle,
      );
      this.#handle = CRANKWAVE_INVALID_HANDLE;
      this.#client.assertStatus(status, "destroy-session");
    }
  }

  #readDescriptor() {
    const pointer = this.#heap.allocate(
      Layout.sessionDescriptor.size,
      "session descriptor",
    );
    try {
      const status = this.#module._crankwave_session_get_descriptor(
        this.#context,
        this.#handle,
        pointer,
      );
      this.#client.assertStatus(status, "inspect-session");
      const view = this.#heap.view;
      const layout = Layout.sessionDescriptor;
      const physicsNumerator = view.getBigUint64(
        pointer + layout.physicsRateNumerator,
        true,
      );
      const physicsDenominator = view.getBigUint64(
        pointer + layout.physicsRateDenominator,
        true,
      );
      const deliveryNumerator = view.getBigUint64(
        pointer + layout.deliveryRateNumerator,
        true,
      );
      const deliveryDenominator = view.getBigUint64(
        pointer + layout.deliveryRateDenominator,
        true,
      );
      const totalBlocks = view.getBigUint64(
        pointer + layout.totalBlockCount,
        true,
      );
      const preparationBlocks = view.getBigUint64(
        pointer + layout.preparationBlockCount,
        true,
      );
      const executionKindCode = view.getUint32(
        pointer + layout.executionKind,
        true,
      );
      const motionModeCode = view.getUint32(
        pointer + layout.motionMode,
        true,
      );
      if (
        executionKindCode !== SessionExecutionKind.finiteScenario &&
        executionKindCode !== SessionExecutionKind.openEnded
      ) {
        throw new CrankwaveRuntimeError(
          `the session descriptor has unknown execution kind ${executionKindCode}`,
          {
            operation: "inspect-session",
            detailCode: "browser-runtime-session-execution-kind-invalid",
            diagnostics: [],
          },
        );
      }
      if (
        motionModeCode < MotionMode.heldSpeed ||
        motionModeCode > MotionMode.freeVehicle
      ) {
        throw new CrankwaveRuntimeError(
          `the session descriptor has unknown motion mode ${motionModeCode}`,
          {
            operation: "inspect-session",
            detailCode: "browser-runtime-motion-mode-invalid",
            diagnostics: [],
          },
        );
      }
      const openEnded =
        executionKindCode === SessionExecutionKind.openEnded;
      if (
        (openEnded && totalBlocks !== 0n) ||
        (!openEnded && totalBlocks === 0n)
      ) {
        throw new CrankwaveRuntimeError(
          "the session execution kind and total block count disagree",
          {
            operation: "inspect-session",
            detailCode: "browser-runtime-session-horizon-invalid",
            diagnostics: [],
          },
        );
      }
      const descriptor = {
        maximumDeliveryFramesPerProcessCall: view.getUint32(
          pointer + layout.maximumDeliveryFrames,
          true,
        ),
        controlCommandQueueCapacity: view.getUint32(
          pointer + layout.controlQueueCapacity,
          true,
        ),
        maximumTelemetryFramesPerProcessCall: view.getUint32(
          pointer + layout.maximumTelemetryFrames,
          true,
        ),
        maximumCycleEvidencePerProcessCall: view.getUint32(
          pointer + layout.maximumCycleEvidence,
          true,
        ),
        physicsRate: {
          numerator: decimal(physicsNumerator),
          denominator: decimal(physicsDenominator),
        },
        physicsRateHz: exactRate(
          physicsNumerator,
          physicsDenominator,
          "physics rate",
        ),
        deliveryRate: {
          numerator: decimal(deliveryNumerator),
          denominator: decimal(deliveryDenominator),
        },
        deliveryRateHz: exactRate(
          deliveryNumerator,
          deliveryDenominator,
          "delivery rate",
        ),
        physicsFramesPerBlock: view.getUint32(
          pointer + layout.physicsFramesPerBlock,
          true,
        ),
        deliveryFramesPerBlock: view.getUint32(
          pointer + layout.deliveryFramesPerBlock,
          true,
        ),
        executionKind: sessionExecutionKindName(executionKindCode),
        executionKindCode,
        motionMode: motionModeName(motionModeCode),
        motionModeCode,
        totalBlockCount: openEnded ? null : decimal(totalBlocks),
        totalBlockCountBigInt: openEnded ? null : totalBlocks,
        preparationBlockCount: decimal(preparationBlocks),
        preparationBlockCountBigInt: preparationBlocks,
        audioBusCount: view.getUint32(pointer + layout.audioBusCount, true),
        liveControlCapabilities: view.getUint32(
          pointer + layout.liveControlCapabilities,
          true,
        ),
        forwardGearCount: view.getUint32(
          pointer + layout.forwardGearCount,
          true,
        ),
        engineIdUtf8Bytes: view.getUint32(pointer + layout.engineIdBytes, true),
        scenarioIdUtf8Bytes: view.getUint32(
          pointer + layout.scenarioIdBytes,
          true,
        ),
      };
      const identity = this.#readIdentity(descriptor);
      descriptor.engineId = identity.engineId;
      descriptor.scenarioId = identity.scenarioId;
      return descriptor;
    } finally {
      this.#heap.free(pointer);
    }
  }

  #readIdentity(descriptor) {
    const engine = this.#heap.allocate(
      descriptor.engineIdUtf8Bytes + 1,
      "session engine identity",
    );
    const scenario = this.#heap.allocate(
      descriptor.scenarioIdUtf8Bytes + 1,
      "session scenario identity",
    );
    const buffers = this.#heap.allocate(
      Layout.sessionIdentityBuffers.size,
      "session identity buffers",
    );
    try {
      const view = this.#heap.view;
      const layout = Layout.sessionIdentityBuffers;
      view.setUint32(buffers + layout.engineData, engine, true);
      view.setUint32(
        buffers + layout.engineCapacity,
        descriptor.engineIdUtf8Bytes + 1,
        true,
      );
      view.setUint32(buffers + layout.scenarioData, scenario, true);
      view.setUint32(
        buffers + layout.scenarioCapacity,
        descriptor.scenarioIdUtf8Bytes + 1,
        true,
      );
      const status = this.#module._crankwave_session_copy_identity(
        this.#context,
        this.#handle,
        buffers,
      );
      this.#client.assertStatus(status, "copy-session-identity");
      return {
        engineId: this.#heap.decodeUtf8(engine, descriptor.engineIdUtf8Bytes),
        scenarioId: this.#heap.decodeUtf8(
          scenario,
          descriptor.scenarioIdUtf8Bytes,
        ),
      };
    } finally {
      this.#heap.free(buffers);
      this.#heap.free(scenario);
      this.#heap.free(engine);
    }
  }

  #readForwardGears() {
    const pointer = this.#heap.allocate(
      Layout.forwardGearDescriptor.size,
      "forward-gear descriptor",
    );
    const idBuffer = this.#heap.allocate(
      Layout.mutableUtf8Buffer.size,
      "forward-gear semantic-id buffer",
    );
    try {
      const result = [];
      for (let index = 0; index < this.#descriptor.forwardGearCount; ++index) {
        const status = this.#module._crankwave_session_get_forward_gear_descriptor(
          this.#context,
          this.#handle,
          index,
          pointer,
        );
        this.#client.assertStatus(status, "inspect-forward-gear");
        const view = this.#heap.view;
        const layout = Layout.forwardGearDescriptor;
        const authoredOrdinal = view.getUint32(
          pointer + layout.authoredOrdinal,
          true,
        );
        if (authoredOrdinal !== index + 1) {
          throw new CrankwaveRuntimeError(
            `forward gear ${index} has authored ordinal ${authoredOrdinal}`,
            {
              operation: "inspect-session",
              detailCode: "browser-runtime-forward-gear-order-invalid",
              diagnostics: [],
            },
          );
        }
        const semanticIdBytes = view.getUint32(
          pointer + layout.semanticIdBytes,
          true,
        );
        const semanticIdPointer = this.#heap.allocate(
          semanticIdBytes + 1,
          "forward-gear semantic id",
        );
        try {
          view.setUint32(
            idBuffer + Layout.mutableUtf8Buffer.data,
            semanticIdPointer,
            true,
          );
          view.setUint32(
            idBuffer + Layout.mutableUtf8Buffer.capacity,
            semanticIdBytes + 1,
            true,
          );
          const copyStatus =
            this.#module._crankwave_session_copy_forward_gear_semantic_id(
              this.#context,
              this.#handle,
              index,
              idBuffer,
            );
          this.#client.assertStatus(copyStatus, "copy-forward-gear-semantic-id");
          result.push({
            index,
            gearId: view.getUint32(pointer + layout.gearId, true),
            authoredOrdinal,
            semanticId: this.#heap.decodeUtf8(
              semanticIdPointer,
              semanticIdBytes,
            ),
            ratio: view.getFloat64(pointer + layout.ratio, true),
          });
        } finally {
          this.#heap.free(semanticIdPointer);
        }
      }
      return result;
    } finally {
      this.#heap.free(idBuffer);
      this.#heap.free(pointer);
    }
  }

  #readBuses() {
    const pointer = this.#heap.allocate(
      Layout.audioBusDescriptor.size,
      "audio-bus descriptor",
    );
    const idBuffer = this.#heap.allocate(
      Layout.mutableUtf8Buffer.size,
      "audio-bus id buffer",
    );
    try {
      const result = [];
      for (let index = 0; index < this.#descriptor.audioBusCount; ++index) {
        const status = this.#module._crankwave_session_get_audio_bus_descriptor(
          this.#context,
          this.#handle,
          index,
          pointer,
        );
        this.#client.assertStatus(status, "inspect-audio-bus");
        const view = this.#heap.view;
        const layout = Layout.audioBusDescriptor;
        const numerator = view.getBigUint64(
          pointer + layout.sampleRateNumerator,
          true,
        );
        const denominator = view.getBigUint64(
          pointer + layout.sampleRateDenominator,
          true,
        );
        const idBytes = view.getUint32(pointer + layout.idBytes, true);
        const idPointer = this.#heap.allocate(idBytes + 1, "audio-bus id");
        try {
          view.setUint32(
            idBuffer + Layout.mutableUtf8Buffer.data,
            idPointer,
            true,
          );
          view.setUint32(
            idBuffer + Layout.mutableUtf8Buffer.capacity,
            idBytes + 1,
            true,
          );
          const copyStatus = this.#module._crankwave_session_copy_audio_bus_id(
            this.#context,
            this.#handle,
            index,
            idBuffer,
          );
          this.#client.assertStatus(copyStatus, "copy-audio-bus-id");
          const kind = view.getUint32(pointer + layout.kind, true);
          const hasRouteIdValue = view.getUint32(
            pointer + layout.hasRouteId,
            true,
          );
          const sourceRouteKindCode = view.getUint32(
            pointer + layout.sourceRouteKind,
            true,
          );
          const signalDispositionCode = view.getUint32(
            pointer + layout.signalDisposition,
            true,
          );
          const hasRouteId = hasRouteIdValue === 1;
          const sourceRouteBus =
            kind === AudioBusKind.sourceRouteDry ||
            kind === AudioBusKind.sourceRouteConfiguredTransfer ||
            kind === AudioBusKind.sourceRouteSelected;
          const masterBus =
            kind === AudioBusKind.engineRawMaster ||
            kind === AudioBusKind.engineAuditionMaster;
          if (
            (hasRouteIdValue !== 0 && hasRouteIdValue !== 1) ||
            (!sourceRouteBus && !masterBus) ||
            (sourceRouteBus &&
              (!hasRouteId ||
                sourceRouteKindCode === SourceRouteKind.unspecified)) ||
            (masterBus &&
              (hasRouteId ||
                sourceRouteKindCode !== SourceRouteKind.unspecified ||
                signalDispositionCode !== AudioSignalDisposition.active)) ||
            sourceRouteKindName(sourceRouteKindCode).startsWith("unknown-") ||
            audioSignalDispositionName(signalDispositionCode).startsWith(
              "unknown-",
            )
          ) {
            throw new CrankwaveRuntimeError(
              "the session returned an invalid audio source-route descriptor",
              {
                operation: "inspect-audio-bus",
                detailCode: "browser-runtime-audio-source-route-invalid",
                diagnostics: [],
              },
            );
          }
          result.push({
            index,
            id: this.#heap.decodeUtf8(idPointer, idBytes),
            kind: audioBusKindName(kind),
            kindCode: kind,
            sourceRouteKind: sourceRouteKindName(sourceRouteKindCode),
            sourceRouteKindCode,
            signalDisposition: audioSignalDispositionName(signalDispositionCode),
            signalDispositionCode,
            channelCount: view.getUint32(pointer + layout.channelCount, true),
            sampleRate: {
              numerator: decimal(numerator),
              denominator: decimal(denominator),
            },
            sampleRateHz: exactRate(
              numerator,
              denominator,
              `audio bus ${index} rate`,
            ),
            routeId: hasRouteId
              ? view.getUint32(pointer + layout.routeId, true)
              : null,
          });
        } finally {
          this.#heap.free(idPointer);
        }
      }
      return result;
    } finally {
      this.#heap.free(idBuffer);
      this.#heap.free(pointer);
    }
  }

  #assertAlive() {
    if (this.#disposed) {
      throw new CrankwaveRuntimeError("the engine session is disposed", {
        operation: "session",
        detailCode: "browser-runtime-session-disposed",
        diagnostics: [],
      });
    }
    if (this.#terminal) {
      throw new CrankwaveRuntimeError("the engine session is terminal", {
        operation: "session",
        detailCode: "browser-runtime-session-terminal",
        diagnostics: [],
      });
    }
  }
}

export { normalizeU64 };
