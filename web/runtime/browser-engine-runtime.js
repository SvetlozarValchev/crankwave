import {
  ESO_CANONICAL_SAMPLE_RATE,
  ProcessKind,
  RingState,
  SessionExecutionKind,
} from "./c-api-abi.js";
import { EngineSimCapiClient } from "./c-api-client.js";
import { EngineSimRuntimeError } from "./c-api-errors.js";
import { runCanonicalExport } from "./canonical-export.js";
import {
  DEVICE_RESAMPLER_ID,
  DeviceRateResampler,
} from "./device-resampler.js";
import {
  loadHeldTexturePresentationRuntime,
  HeldTexturePresentationRuntimeCursor,
  HeldTexturePresentationRuntimeError,
} from "./held-texture-presentation-runtime.js";
import { ResponsiveAudioLifecycleCursor } from "./responsive-audio-lifecycle-runtime.js";
import {
  loadSharedRecordedStarterRuntime,
  resolveSharedRecordedStarterManifestUrl,
  SharedRecordedStarterCursor,
} from "./shared-recorded-starter-runtime.js";
import {
  PcmRingProducer,
  choosePcmRingCapacity,
  createPcmRingBuffer,
} from "./pcm-ring-buffer.js";
import {
  liveControlCapability,
  publicDescriptor,
  readyMessage,
  validationMessage,
} from "./protocol.js";
import {
  SourceBakedComparisonMixer,
  SourceBakedComparisonMode,
} from "./source-baked-comparison-mixer.js";

const RUNTIME_STATS_INTERVAL_MS = 250;
const PRIMING_CORE_BLOCKS_PER_TURN = 4;
const RUNNING_CORE_BLOCKS_PER_TURN = 4;
const ATLAS_AUDITION_BUS_ID = "master-engine-audition";
const ATLAS_RUNTIME_BUS_KIND = "engine-audition-master";
const TWO_PI = 2 * Math.PI;
const ResponsiveEngineStateFlag = Object.freeze({
  ignitionEnabled: 1 << 0,
  fuelEnabled: 1 << 1,
  starterEnabled: 1 << 2,
  limiterEnabled: 1 << 3,
  limiterCutActive: 1 << 4,
});
function isRecoverableAtlasCoverageExit(error) {
  return error instanceof HeldTexturePresentationRuntimeError &&
    error.recoverable;
}

async function loadBakedAudioPackage(url) {
  const package_ = await loadHeldTexturePresentationRuntime(url);
  if (package_.lifecyclePackage === null) return package_;
  const sharedStarterPath = package_.manifest.shared_recorded_starter_package_path;
  if (sharedStarterPath === undefined) return package_;
  const sharedStarterUrl = resolveSharedRecordedStarterManifestUrl(
    package_.manifestUrl,
    sharedStarterPath,
  );
  const sharedRecordedStarterPackage = await loadSharedRecordedStarterRuntime(
    sharedStarterUrl,
  );
  return Object.freeze({ ...package_, sharedRecordedStarterPackage });
}

function isResponsiveAudioPackage(package_) {
  return package_?.kind === "responsive-audio-preview";
}

function atlasStateMask(telemetry) {
  let mask = 0;
  if (telemetry.ignitionEnabled) {
    mask |= ResponsiveEngineStateFlag.ignitionEnabled;
  }
  if (telemetry.fuelEnabled) {
    mask |= ResponsiveEngineStateFlag.fuelEnabled;
  }
  if (telemetry.starterEnabled) {
    mask |= ResponsiveEngineStateFlag.starterEnabled;
  }
  if (telemetry.limiterEnabled) {
    mask |= ResponsiveEngineStateFlag.limiterEnabled;
  }
  if (telemetry.limiterCutActive) {
    mask |= ResponsiveEngineStateFlag.limiterCutActive;
  }
  return mask;
}

function endpointCombustionEnabled(endpoint) {
  const required =
    ResponsiveEngineStateFlag.ignitionEnabled |
    ResponsiveEngineStateFlag.fuelEnabled;
  return endpoint !== null &&
    (endpoint.stateMask & required) === required;
}

function projectEndpointToRunningState(endpoint) {
  return Object.freeze({
    ...endpoint,
    stateMask:
      endpoint.stateMask |
      ResponsiveEngineStateFlag.ignitionEnabled |
      ResponsiveEngineStateFlag.fuelEnabled,
  });
}

function projectEndpointToMotoringState(endpoint) {
  return Object.freeze({
    ...endpoint,
    stateMask:
      (endpoint.stateMask | ResponsiveEngineStateFlag.fuelEnabled) &
      ~ResponsiveEngineStateFlag.ignitionEnabled,
  });
}

function lifecycleStateFromEndpoint(
  endpoint,
  runningBedReady,
  admissionLaneWeights,
  runningBedLaneWeights,
) {
  return Object.freeze({
    frame: endpoint.frame,
    starter:
      (endpoint.stateMask & ResponsiveEngineStateFlag.starterEnabled) !== 0,
    ignition:
      (endpoint.stateMask & ResponsiveEngineStateFlag.ignitionEnabled) !== 0,
    fuel: (endpoint.stateMask & ResponsiveEngineStateFlag.fuelEnabled) !== 0,
    runningBedReady,
    rpm: endpoint.rpm,
    requestedThrottle01: endpoint.requestedThrottle01,
    manifoldPressurePaAbs: endpoint.manifoldPressurePaAbs,
    rpmSlopeRpmPerSecond: endpoint.rpmSlopeRpmPerSecond,
    admissionLaneWeights,
    runningBedLaneWeights,
    unwrappedCrankRevolutions: endpoint.unwrappedCrankRevolutions,
    indicatedGasTorqueNm: endpoint.instantaneousIndicatedGasTorqueNm,
  });
}

function authoredThrottleLaneWeights(package_, throttle01) {
  const lanes = package_.heldPackage.manifest.domain.load_lanes;
  if (!Array.isArray(lanes) || lanes.length < 2) {
    throw new TypeError("held texture package needs at least two authored load lanes");
  }
  let leftIndex = 0;
  let rightIndex = 0;
  if (throttle01 <= lanes[0].throttle01) {
    leftIndex = rightIndex = 0;
  } else if (throttle01 >= lanes.at(-1).throttle01) {
    leftIndex = rightIndex = lanes.length - 1;
  } else {
    rightIndex = 1;
    while (lanes[rightIndex].throttle01 < throttle01) ++rightIndex;
    leftIndex = rightIndex - 1;
  }
  const amount = leftIndex === rightIndex
    ? 0
    : (throttle01 - lanes[leftIndex].throttle01) /
      (lanes[rightIndex].throttle01 - lanes[leftIndex].throttle01);
  return Object.freeze(lanes.map((lane, index) => Object.freeze({
    id: lane.id,
    weight: index === leftIndex
      ? 1 - amount
      : index === rightIndex
        ? amount
        : 0,
  })));
}

// Phase-unit playback is crank-synchronous and does not need the chronological
// atlas's half-second admission window. Consecutive committed endpoints let it
// warm its stateful transfer throughout preparation and respond to manual
// throttle gestures without repeatedly falling back to "waiting".
class DryPhaseLiveStateTracker {
  #previousEndpoint = null;

  reset() {
    this.#previousEndpoint = null;
  }

  observe(block) {
    const telemetry = block.telemetry.at(-1);
    if (telemetry === undefined) {
      this.#previousEndpoint = null;
      return Object.freeze({
        kind: "invalid",
        endpoint: null,
        endpoints: null,
        rpm: null,
        reason: "live telemetry did not contain an endpoint",
      });
    }
    const observedRpm = Number.isFinite(telemetry.engineSpeedRpm)
      ? telemetry.engineSpeedRpm
      : null;
    const endpointFrame =
      Number(BigInt(block.process.firstDeliveryFrame)) +
      block.process.deliveryFrameCount;
    if (
      !Number.isSafeInteger(endpointFrame) ||
      !Number.isFinite(telemetry.engineSpeedRpm) ||
      !Number.isFinite(telemetry.meanIntakeManifoldPressurePaAbs) ||
      telemetry.meanIntakeManifoldPressurePaAbs <= 0 ||
      !Number.isFinite(telemetry.thetaRad) ||
      !Number.isFinite(telemetry.requestedThrottle01) ||
      telemetry.requestedThrottle01 < 0 ||
      telemetry.requestedThrottle01 > 1
    ) {
      this.#previousEndpoint = null;
      return Object.freeze({
        kind: "invalid",
        endpoint: null,
        endpoints: null,
        rpm: observedRpm,
        reason: "live telemetry could not form a valid playback endpoint",
      });
    }
    const endpoint = Object.freeze({
      rpm: telemetry.engineSpeedRpm,
      rpmSlopeRpmPerSecond: this.#previousEndpoint === null
        ? 0
        : (telemetry.engineSpeedRpm - this.#previousEndpoint.rpm) /
          ((endpointFrame - this.#previousEndpoint.frame) /
            ESO_CANONICAL_SAMPLE_RATE),
      manifoldPressurePaAbs: telemetry.meanIntakeManifoldPressurePaAbs,
      requestedThrottle01: telemetry.requestedThrottle01,
      unwrappedCrankRevolutions: telemetry.thetaRad / TWO_PI,
      stateMask: atlasStateMask(telemetry),
      instantaneousIndicatedGasTorqueNm:
        telemetry.torque?.instantaneousIndicatedGas?.availability === 1 &&
          Number.isFinite(telemetry.torque.instantaneousIndicatedGas.valueNm)
          ? telemetry.torque.instantaneousIndicatedGas.valueNm
          : null,
      frame: endpointFrame,
    });
    const endpoints = this.#previousEndpoint === null
      ? null
      : Object.freeze({ start: this.#previousEndpoint, end: endpoint });
    this.#previousEndpoint = endpoint;
    return Object.freeze({
      kind: endpoints === null ? "first" : "pair",
      endpoint,
      endpoints,
      rpm: endpoint.rpm,
      reason: null,
    });
  }
}

function runtimeError(message, detailCode, operation = "browser-runtime") {
  return new EngineSimRuntimeError(message, {
    operation,
    detailCode,
    diagnostics: [],
  });
}

function publicError(error) {
  if (typeof error?.toJSON === "function") {
    return error.toJSON();
  }
  return {
    name: error?.name ?? "Error",
    message: error?.message ?? String(error),
    operation: null,
    status: null,
    statusName: null,
    stage: null,
    stageName: null,
    code: null,
    detailCode: null,
    diagnostics: [],
  };
}

function outputConfiguration(outputSampleRate, leadFrames) {
  if (
    !Number.isSafeInteger(outputSampleRate) ||
    outputSampleRate < 8_000 ||
    outputSampleRate > ESO_CANONICAL_SAMPLE_RATE
  ) {
    throw new RangeError(
      `outputSampleRate must be an integer in [8000, ${ESO_CANONICAL_SAMPLE_RATE}]`,
    );
  }
  if (!Number.isSafeInteger(leadFrames) || leadFrames < 1) {
    throw new RangeError("leadFrames must be a positive integer");
  }
  return { outputSampleRate, leadFrames };
}

export class BrowserEngineRuntime {
  #emit;
  #client;
  #program = null;
  #selectedBusIndex = -1;
  #state = "empty";
  #output = null;
  #outputSettings = null;
  #generation = 0;
  #pumpEpoch = 0;
  #completionPending = false;
  #coreBlocks = 0;
  #canonicalFrames = 0n;
  #deviceFrames = 0;
  #startedAt = 0;
  #lastStatsAt = 0;
  #pumpReceivePort;
  #pumpSendPort;
  #atlasPackage = null;
  #atlasPackageError = null;
  #atlasUrl = null;
  #atlasCursor = null;
  #lifecycleCursor = null;
  #sharedStarterCursor = null;
  #atlasOutputBuffers = null;
  #atlasSilenceBuffer = null;
  #atlasBusIndex = -1;
  #dryPhaseTracker = new DryPhaseLiveStateTracker();
  #atlasStatus = "unavailable";
  #atlasDetailCode = "browser-runtime-atlas-not-configured";
  #atlasMessage = "this build has no audio atlas configured";
  #comparisonMixer = null;
  #comparisonMode = SourceBakedComparisonMode.source;

  static async create({ moduleUrl, emit }) {
    const client = await EngineSimCapiClient.create(moduleUrl);
    return new BrowserEngineRuntime(client, emit);
  }

  constructor(client, emit) {
    if (typeof emit !== "function") {
      throw new TypeError("BrowserEngineRuntime requires an event emitter");
    }
    this.#client = client;
    this.#emit = emit;
    const pumpChannel = new MessageChannel();
    this.#pumpReceivePort = pumpChannel.port1;
    this.#pumpSendPort = pumpChannel.port2;
    this.#pumpReceivePort.onmessage = (event) => this.#pump(event.data);
    this.#pumpReceivePort.unref?.();
    this.#pumpSendPort.unref?.();
  }

  announceReady(requestId, moduleUrl) {
    this.#emit(readyMessage(requestId, moduleUrl));
  }

  build({
    requestId,
    engineJson,
    scenarioJson,
    assets,
    executionKind,
    audioAtlasManifestUrl = null,
  }) {
    this.#assertNotDisposed();
    this.#assertNotExporting("build");
    if (this.#state === "compiling") {
      throw runtimeError(
        "an engine build replacement is already in progress",
        "browser-runtime-build-replacement-active",
        "build",
      );
    }
    const priorState = this.#state;
    this.#pausePump("compiling");
    this.#emitState(requestId);
    this.#output?.producer.setProducerState(
      this.#output.published ? RingState.paused : RingState.idle,
    );
    let replacement;
    try {
      replacement = this.#client.compile(
        engineJson,
        scenarioJson,
        assets,
        executionKind,
      );
    } catch (error) {
      this.#restoreAfterFailedMutation(priorState, requestId);
      if ((error.diagnostics?.length ?? 0) !== 0) {
        this.#emit(validationMessage(requestId, error));
        return;
      }
      throw error;
    }

    if (
      audioAtlasManifestUrl !== null &&
      audioAtlasManifestUrl !== undefined
    ) {
      this.#emitAtlasLoading(requestId, audioAtlasManifestUrl);
      return this.#completeAtlasBuild({
        requestId,
        priorState,
        replacement,
        atlasUrl: audioAtlasManifestUrl,
      });
    }

    this.#commitCompiledBuild({
      requestId,
      replacement,
      atlasUrl: null,
      loadedAtlas: null,
      atlasLoadError: null,
    });
  }

  async #completeAtlasBuild({
    requestId,
    priorState,
    replacement,
    atlasUrl,
  }) {
    let loadedAtlas = null;
    let atlasLoadError = null;
    try {
      loadedAtlas = await loadBakedAudioPackage(atlasUrl);
      if (loadedAtlas.sampleRate !== ESO_CANONICAL_SAMPLE_RATE) {
        throw runtimeError(
          `audio atlas sample rate ${loadedAtlas.sampleRate} Hz is not the canonical ${ESO_CANONICAL_SAMPLE_RATE} Hz rate`,
          "browser-runtime-atlas-noncanonical-rate",
          "load-audio-atlas",
        );
      }
      if (loadedAtlas.manifest.engine !== replacement.engineId) {
        throw runtimeError(
          `audio atlas engine ${loadedAtlas.manifest.engine} does not match compiled engine ${replacement.engineId}`,
          "browser-runtime-atlas-engine-mismatch",
          "load-audio-atlas",
        );
      }
      if (
        loadedAtlas.manifest.provenance.engine.sha256 !==
        replacement.engineProvenanceSha256
      ) {
        throw runtimeError(
          "audio atlas engine provenance does not match the exact compiled engine input",
          "browser-runtime-atlas-engine-provenance-mismatch",
          "load-audio-atlas",
        );
      }
      const capturedRendererSourceSha256 =
        loadedAtlas.manifest.provenance.renderer_build.sha256;
      if (
        loadedAtlas.manifest.provenance.renderer_build.id !==
        "engine-sim-offline-renderer-build"
      ) {
        throw runtimeError(
          "audio atlas renderer provenance uses an unsupported identity kind",
          "browser-runtime-atlas-renderer-provenance-mismatch",
          "load-audio-atlas",
        );
      }
      const rendererCompatibility = loadedAtlas.rendererCompatibility;
      const rendererIsExact =
        capturedRendererSourceSha256 === replacement.rendererSourceSha256;
      const rendererIsVerifiedCompatible =
        rendererCompatibility !== null &&
        rendererCompatibility.captureSourceClosureSha256 ===
          capturedRendererSourceSha256 &&
        rendererCompatibility.admittedSourceClosureSha256 ===
          replacement.rendererSourceSha256;
      if (!rendererIsExact && !rendererIsVerifiedCompatible) {
        throw runtimeError(
          "audio atlas renderer provenance is neither exact nor covered by verified runtime parity evidence",
          "browser-runtime-atlas-renderer-provenance-mismatch",
          "load-audio-atlas",
        );
      }
      if (!loadedAtlas.busIds.includes(ATLAS_AUDITION_BUS_ID)) {
        throw runtimeError(
          `audio atlas does not publish required bus ${ATLAS_AUDITION_BUS_ID}`,
          "browser-runtime-atlas-audition-bus-missing",
          "load-audio-atlas",
        );
      }
    } catch (error) {
      atlasLoadError = error;
      loadedAtlas = null;
    }
    try {
      this.#commitCompiledBuild({
        requestId,
        replacement,
        atlasUrl,
        loadedAtlas,
        atlasLoadError,
      });
    } catch (error) {
      replacement.dispose();
      this.#restoreAfterFailedMutation(priorState, requestId);
      throw error;
    }
  }

  #commitCompiledBuild({
    requestId,
    replacement,
    atlasUrl,
    loadedAtlas,
    atlasLoadError,
  }) {
    const selectedBusIndex = replacement.session.auditionBusIndex;
    const atlasPlayback = this.#createAtlasPlaybackState({
      session: replacement.session,
      selectedBusIndex,
      atlasUrl,
      loadedAtlas,
      error: atlasLoadError,
    });
    const previous = this.#program;
    this.#program = replacement;
    this.#selectedBusIndex = selectedBusIndex;
    this.#discardOutput(RingState.ended);
    this.#applyAtlasPlaybackState(atlasPlayback);
    this.#state = "ready";
    previous?.dispose();
    this.#emitBuilt(requestId);
    this.#emitAtlasStatus(requestId, true);
    this.#emitComparisonMode(requestId, true, "compiled build installed");
    this.#emitState(requestId);
  }

  selectAudioBus({ requestId, busIndex }) {
    this.#requireProgram("select-audio-bus");
    this.#assertNotExporting("select-audio-bus");
    if (this.#state === "running" || this.#state === "preparing") {
      throw runtimeError(
        "stop playback before selecting another audio bus",
        "browser-runtime-audio-bus-selection-while-running",
        "select-audio-bus",
      );
    }
    if (
      !Number.isSafeInteger(busIndex) ||
      busIndex < 0 ||
      busIndex >= this.#program.session.buses.length
    ) {
      throw runtimeError(
        `audio bus index ${String(busIndex)} is outside the session descriptor`,
        "browser-runtime-audio-bus-index-invalid",
        "select-audio-bus",
      );
    }
    if (this.#state !== "ready") {
      this.#replaceSession(busIndex);
    } else {
      const atlasPlayback = this.#createAtlasPlaybackState({
        session: this.#program.session,
        selectedBusIndex: busIndex,
        atlasUrl: this.#atlasUrl,
        loadedAtlas: this.#atlasPackage,
        error: this.#atlasPackageError,
      });
      this.#selectedBusIndex = busIndex;
      this.#applyAtlasPlaybackState(atlasPlayback);
    }
    this.#discardOutput(RingState.ended);
    this.#state = "ready";
    this.#emitBuilt(requestId);
    this.#emitAtlasStatus(requestId, true);
    this.#emitComparisonMode(requestId, true, "audio bus selection changed");
    this.#emitState(requestId);
  }

  setComparisonMode({ requestId, mode }) {
    this.#requireProgram("set-comparison-mode");
    this.#assertNotExporting("set-comparison-mode");
    if (this.#state === "compiling") {
      throw runtimeError(
        "comparison mode cannot change while a compiled build replacement is in progress",
        "browser-runtime-comparison-mode-during-build-replacement",
        "set-comparison-mode",
      );
    }
    if (!Object.values(SourceBakedComparisonMode).includes(mode)) {
      throw runtimeError(
        "comparison mode must be source-a or baked-b",
        "browser-runtime-comparison-mode-invalid",
        "set-comparison-mode",
      );
    }
    if (
      mode === SourceBakedComparisonMode.baked &&
      (!["active", "motoring", "tail-only"].includes(this.#atlasStatus) ||
        this.#comparisonMixer === null)
    ) {
      throw runtimeError(
        "baked audio is not active; wait for the audio atlas to enter coverage",
        "browser-runtime-baked-audio-unavailable",
        "set-comparison-mode",
      );
    }
    this.#comparisonMode = mode;
    if (this.#comparisonMixer !== null) {
      this.#comparisonMixer.mode = mode;
    }
    this.#emitComparisonMode(requestId, false, null);
    this.#emitAtlasStatus(requestId, true);
  }

  start({ requestId, outputSampleRate, leadFrames }) {
    this.#requireProgram("start");
    this.#assertNotExporting("start");
    if (this.#state === "running" || this.#state === "preparing") {
      throw runtimeError(
        "the engine session is already starting or running",
        "browser-runtime-session-already-running",
        "start",
      );
    }
    if (this.#state === "completed" || this.#state === "failed") {
      throw runtimeError(
        "restart the terminal engine session before starting it",
        "browser-runtime-restart-required",
        "start",
      );
    }

    if (this.#state === "paused") {
      const requested =
        outputSampleRate === undefined && leadFrames === undefined
          ? this.#outputSettings
          : outputConfiguration(outputSampleRate, leadFrames);
      if (
        requested.outputSampleRate !== this.#outputSettings.outputSampleRate ||
        requested.leadFrames !== this.#outputSettings.leadFrames
      ) {
        throw runtimeError(
          "changing device output settings requires restart",
          "browser-runtime-output-restart-required",
          "start",
        );
      }
    } else {
      this.#configureOutput(
        outputConfiguration(outputSampleRate, leadFrames),
        requestId,
      );
    }

    if (
      this.#output.published &&
      this.#output.producer.availableFrames >=
        this.#outputSettings.leadFrames
    ) {
      this.#state = "running";
      this.#output.producer.setProducerState(RingState.streaming);
    } else {
      this.#output.requestId = requestId;
      this.#state = "preparing";
      this.#output.producer.setProducerState(RingState.idle);
    }
    this.#emitState(requestId);
    this.#schedulePump(0);
  }

  stop({ requestId }) {
    this.#requireProgram("stop");
    this.#assertNotExporting("stop");
    if (this.#state === "running" || this.#state === "preparing") {
      this.#pausePump("paused");
      this.#output?.producer.setProducerState(
        this.#output.published ? RingState.paused : RingState.idle,
      );
    }
    this.#emitState(requestId);
    this.#emitRuntimeStats(true);
  }

  restart({ requestId, outputSampleRate, leadFrames }) {
    this.#requireProgram("restart");
    this.#assertNotExporting("restart");
    const settings =
      outputSampleRate === undefined && leadFrames === undefined
        ? this.#outputSettings
        : outputConfiguration(outputSampleRate, leadFrames);
    if (settings === null) {
      throw runtimeError(
        "restart needs outputSampleRate and leadFrames before the first start",
        "browser-runtime-output-not-configured",
        "restart",
      );
    }
    this.#pausePump("restarting");
    this.#replaceSession();
    this.#discardOutput(RingState.ended);
    this.#state = "ready";
    this.#emitAtlasStatus(requestId, true);
    this.#emitComparisonMode(requestId, true, "engine session restarted");
    this.start({ requestId, ...settings });
  }

  enqueueControls({ requestId, controls }) {
    this.#requireProgram("enqueue-controls");
    this.#assertNotExporting("enqueue-controls");
    const descriptor = this.#program.session.descriptor;
    if (descriptor.liveControlCapabilities === 0) {
      throw runtimeError(
        "the compiled scenario does not accept live controls",
        "browser-runtime-controls-unavailable",
        "enqueue-controls",
      );
    }
    if (!Array.isArray(controls) || controls.length === 0) {
      throw new TypeError("controls must be a non-empty array");
    }
    const session = this.#program.session;
    const defaultFrame =
      session.nextDeliveryFrame > session.firstAudibleDeliveryFrame
        ? session.nextDeliveryFrame
        : session.firstAudibleDeliveryFrame;
    const resolvedControls = controls.map((control, index) => {
      if (typeof control !== "object" || control === null) {
        throw new TypeError(`control ${index} must be an object`);
      }
      const capability = liveControlCapability(control.kind);
      if (capability === null) {
        throw runtimeError(
          `unsupported live control: ${String(control.kind)}`,
          "browser-runtime-unsupported-control",
          "enqueue-controls",
        );
      }
      if ((descriptor.liveControlCapabilities & capability.mask) === 0) {
        throw runtimeError(
          `${control.kind} was not admitted for the compiled scenario`,
          "browser-runtime-control-not-admitted",
          "enqueue-controls",
        );
      }
      return {
        kind: control.kind,
        value: control.value,
        deliveryFrame:
          control.deliveryFrame === undefined
            ? defaultFrame
            : BigInt(control.deliveryFrame),
      };
    });
    session.enqueueControls(resolvedControls);
    this.#emit({
      type: "controls-result",
      requestId,
      accepted: true,
      controls: resolvedControls.map((control) => ({
        kind: control.kind,
        value: control.value,
        deliveryFrame: control.deliveryFrame.toString(10),
      })),
    });
  }

  async exportWav({ requestId, controls }) {
    this.#requireProgram("export-wav");
    this.#assertNotExporting("export-wav");
    if (this.#state === "running" || this.#state === "preparing") {
      throw runtimeError(
        "stop live preparation or playback before starting an unpaced export",
        "browser-runtime-export-while-running",
        "export-wav",
      );
    }
    const priorState = this.#state;
    this.#state = "exporting";
    this.#emitState(requestId);
    try {
      const result = await runCanonicalExport({
        program: this.#program,
        busIndex: this.#selectedBusIndex,
        controls,
        onProgress: (progress) =>
          this.#emit({
            type: "export-progress",
            requestId,
            ...progress,
          }),
      });
      this.#state = priorState;
      this.#emitState(requestId);
      this.#emit(
        {
          type: "wav-export",
          requestId,
          encoding: "ieee-float32-le",
          coreIdentical: true,
          executionKind: result.executionKind,
          sampleRate: result.sampleRate,
          channelCount: result.channelCount,
          frameCount: result.frameCount,
          bus: result.bus,
          pcmFloat32: result.pcm.buffer,
          wav: result.wav.buffer,
        },
        [result.pcm.buffer, result.wav.buffer],
      );
    } catch (error) {
      this.#state = priorState;
      this.#emitState(requestId);
      throw error;
    }
  }

  status({ requestId }) {
    this.#assertNotDisposed();
    this.#emitState(requestId);
    this.#emitRuntimeStats(true, requestId);
    this.#emitAtlasStatus(requestId, true);
    this.#emitComparisonMode(requestId, false, null);
    this.#emitSharedStarterStatus(requestId);
  }

  setSharedStarterEnabled({ requestId, enabled }) {
    this.#assertNotDisposed();
    if (this.#sharedStarterCursor === null) {
      throw runtimeError(
        "this responsive package has no shared recorded starter layer",
        "browser-runtime-shared-starter-unavailable",
        "set-shared-starter-enabled",
      );
    }
    this.#sharedStarterCursor.setEnabled(enabled);
    this.#emitSharedStarterStatus(requestId);
  }

  dispose({ requestId } = {}) {
    if (this.#state === "disposed") {
      return;
    }
    if (this.#state === "exporting") {
      throw runtimeError(
        "cannot dispose while a canonical export is active",
        "browser-runtime-export-active",
        "dispose",
      );
    }
    ++this.#pumpEpoch;
    this.#pumpReceivePort.close();
    this.#pumpSendPort.close();
    this.#discardOutput(RingState.ended);
    this.#program?.dispose();
    this.#program = null;
    this.#atlasPackage = null;
    this.#atlasPackageError = null;
    this.#atlasUrl = null;
    this.#atlasCursor = null;
    this.#lifecycleCursor = null;
    this.#sharedStarterCursor = null;
    this.#atlasOutputBuffers = null;
    this.#atlasSilenceBuffer = null;
    this.#comparisonMixer = null;
    this.#dryPhaseTracker.reset();
    this.#client.dispose();
    this.#state = "disposed";
    this.#emitState(requestId);
  }

  #configureOutput(settings, requestId) {
    const bus = this.#program.session.buses[this.#selectedBusIndex];
    const channelCount = this.#atlasSelectedBusEligible()
      ? 2
      : bus.channelCount;
    const resampler = new DeviceRateResampler({
      inputSampleRate: ESO_CANONICAL_SAMPLE_RATE,
      outputSampleRate: settings.outputSampleRate,
      channelCount,
    });
    const maximumBlockFrames = resampler.maximumOutputFramesForInput(
      this.#program.session.descriptor.deliveryFramesPerBlock,
    );
    const capacityFrames = choosePcmRingCapacity(
      settings.leadFrames,
      maximumBlockFrames,
    );
    ++this.#generation;
    this.#lifecycleCursor?.reset(this.#generation);
    this.#sharedStarterCursor?.reset(this.#generation);
    const ring = createPcmRingBuffer({
      capacityFrames,
      channelCount,
      generation: this.#generation,
      producerState: RingState.idle,
    });
    const producer = new PcmRingProducer(
      ring.sharedBuffer,
      ring.capacityFrames,
      ring.channelCount,
    );
    this.#outputSettings = settings;
    this.#output = {
      producer,
      resampler,
      ring,
      requestId,
      published: false,
      pending: null,
      pendingFrameOffset: 0,
      capacityFrames,
      channelCount,
    };
    this.#completionPending = false;
    this.#coreBlocks = 0;
    this.#canonicalFrames = 0n;
    this.#deviceFrames = 0;
    this.#startedAt = 0;
    this.#lastStatsAt = 0;
  }

  #schedulePump(delayMilliseconds) {
    const epoch = ++this.#pumpEpoch;
    if (delayMilliseconds === 0) {
      // A posted task yields to pending controls without Chrome's 4 ms nested-
      // timer clamp. Genuine pacing waits below continue to use timers.
      this.#pumpSendPort.postMessage(epoch);
      return;
    }
    setTimeout(() => this.#pump(epoch), delayMilliseconds);
  }

  #pump(epoch) {
    if (
      epoch !== this.#pumpEpoch ||
      (this.#state !== "preparing" && this.#state !== "running")
    ) {
      return;
    }
    try {
      const blockLimit =
        this.#state === "preparing"
          ? PRIMING_CORE_BLOCKS_PER_TURN
          : RUNNING_CORE_BLOCKS_PER_TURN;
      for (let processed = 0; processed < blockLimit; ++processed) {
        if (!this.#drainPendingPcm()) {
          this.#emitRuntimeStats();
          this.#schedulePump(2);
          return;
        }
        if (this.#completionPending) {
          this.#finishRun();
          return;
        }
        if (
          this.#state === "preparing" &&
          this.#output.producer.availableFrames >=
            this.#outputSettings.leadFrames
        ) {
          this.#beginPrimedStreaming();
          return;
        }
        if (
          this.#state === "running" &&
          this.#output.producer.availableFrames >=
            this.#outputSettings.leadFrames
        ) {
          this.#emitRuntimeStats();
          this.#schedulePump(4);
          return;
        }

        const block = this.#program.session.processBlock(
          this.#selectedBusIndex,
        );
        if (block.process.kindCode === ProcessKind.completed) {
          if (
            this.#program.executionKind === SessionExecutionKind.openEnded
          ) {
            throw runtimeError(
              "the open-ended interactive session completed unexpectedly",
              "browser-runtime-open-session-completed",
              "process-session",
            );
          }
          const tail = this.#output.resampler.finish();
          this.#completionPending = true;
          if (tail.length !== 0) {
            this.#output.pending = tail;
            this.#output.pendingFrameOffset = 0;
          }
          if (this.#drainPendingPcm() && this.#completionPending) {
            this.#finishRun();
          } else {
            this.#schedulePump(2);
          }
          return;
        }

        ++this.#coreBlocks;
        const atlasObservation = this.#atlasSelectedBusEligible()
          ? this.#dryPhaseTracker.observe(block)
          : null;
        if (!block.audible && isResponsiveAudioPackage(this.#atlasPackage)) {
          this.#warmDryPhaseRuntime(block, atlasObservation);
        }
        if (block.audible) {
          const canonicalPcm = this.#atlasSelectedBusEligible()
            ? this.#renderAtlasComparison(block, atlasObservation)
            : block.samples;
          this.#canonicalFrames += BigInt(
            canonicalPcm.length / this.#output.channelCount,
          );
          const devicePcm = this.#output.resampler.push(canonicalPcm);
          if (devicePcm.length !== 0) {
            this.#output.pending = devicePcm;
            this.#output.pendingFrameOffset = 0;
          }
        }
        this.#emit({
          type: "telemetry",
          selectedBusIndex: this.#selectedBusIndex,
          process: block.process,
          frames: block.telemetry,
        });
        this.#emitRuntimeStats();
      }
      this.#schedulePump(0);
    } catch (error) {
      this.#state = "failed";
      this.#output?.producer.setProducerState(RingState.failed);
      this.#emitState();
      this.#emit({ type: "error", requestId: null, error: publicError(error) });
    }
  }

  #finishRun() {
    if (
      this.#program.executionKind === SessionExecutionKind.openEnded
    ) {
      throw runtimeError(
        "the open-ended interactive session reached finite completion",
        "browser-runtime-open-session-completed",
        "process-session",
      );
    }
    this.#completionPending = false;
    this.#state = "completed";
    if (this.#output.published) {
      this.#output.producer.setProducerState(RingState.ended);
    } else {
      this.#publishAudioRing(RingState.ended);
    }
    this.#emitRuntimeStats(true);
    this.#emitState();
  }

  #drainPendingPcm() {
    if (this.#output.pending === null) {
      return true;
    }
    const written = this.#output.producer.writeInterleaved(
      this.#output.pending,
      this.#output.pendingFrameOffset,
    );
    this.#output.pendingFrameOffset += written;
    this.#deviceFrames += written;
    if (
      this.#output.pendingFrameOffset <
      this.#output.pending.length / this.#output.channelCount
    ) {
      return false;
    }
    this.#output.pending = null;
    this.#output.pendingFrameOffset = 0;
    return true;
  }

  #beginPrimedStreaming() {
    this.#state = "running";
    this.#startedAt = performance.now();
    this.#publishAudioRing(RingState.streaming);
    this.#emitRuntimeStats(true);
    this.#emitState(this.#output.requestId);
    this.#schedulePump(4);
  }

  #publishAudioRing(producerState) {
    if (this.#output.published) {
      this.#output.producer.setProducerState(producerState);
      return;
    }
    this.#output.producer.setProducerState(producerState);
    this.#output.published = true;
    const { ring } = this.#output;
    this.#emit({
      type: "audio-ring",
      requestId: this.#output.requestId,
      sharedBuffer: ring.sharedBuffer,
      capacityFrames: ring.capacityFrames,
      channelCount: ring.channelCount,
      sampleRate: this.#outputSettings.outputSampleRate,
      headerBytes: ring.headerBytes,
      schema: ring.schema,
      generation: this.#generation,
      resamplerId: DEVICE_RESAMPLER_ID,
    });
  }

  #pausePump(state) {
    ++this.#pumpEpoch;
    this.#state = state;
  }

  #replaceSession(selectedBusIndex = this.#selectedBusIndex) {
    const replacement = this.#program.createSession(
      this.#program.executionKind,
    );
    let atlasPlayback;
    try {
      atlasPlayback = this.#createAtlasPlaybackState({
        session: replacement,
        selectedBusIndex,
        atlasUrl: this.#atlasUrl,
        loadedAtlas: this.#atlasPackage,
        error: this.#atlasPackageError,
      });
    } catch (error) {
      replacement.dispose();
      throw error;
    }
    const previous = this.#program.session;
    this.#program.session = replacement;
    this.#selectedBusIndex = selectedBusIndex;
    this.#applyAtlasPlaybackState(atlasPlayback);
    previous.dispose();
  }

  #createAtlasPlaybackState({
    session,
    selectedBusIndex,
    atlasUrl,
    loadedAtlas,
    error,
  }) {
    const resolvedAtlasUrl =
      loadedAtlas?.manifestUrl ??
      (atlasUrl === null || atlasUrl === undefined ? null : String(atlasUrl));
    const base = {
      atlasPackage: loadedAtlas,
      atlasPackageError: error,
      atlasUrl: resolvedAtlasUrl,
      atlasCursor: null,
      lifecycleCursor: null,
      sharedStarterCursor: null,
      atlasOutputBuffers: null,
      atlasSilenceBuffer: null,
      atlasBusIndex: -1,
      comparisonMixer: null,
      comparisonMode: SourceBakedComparisonMode.source,
      atlasStatus: "unavailable",
      atlasDetailCode: "browser-runtime-atlas-not-configured",
      atlasMessage:
        "This build has no continuous audio atlas. Source A remains live.",
    };
    if (error !== null) {
      return {
        ...base,
        atlasStatus: "error",
        atlasDetailCode:
          error.code ??
          error.detailCode ??
          "browser-runtime-atlas-load-failed",
        atlasMessage: error.message,
      };
    }
    if (loadedAtlas === null) {
      return base;
    }
    if (!this.#isAtlasBusEligible(loadedAtlas, session, selectedBusIndex)) {
      return {
        ...base,
        atlasStatus: "unavailable",
        atlasDetailCode: "browser-runtime-atlas-source-bus-ineligible",
        atlasMessage:
          `select the compiled ${ATLAS_RUNTIME_BUS_KIND} bus to audition baked audio`,
      };
    }

    const capacity =
      session.descriptor.maximumDeliveryFramesPerProcessCall;
    if (!isResponsiveAudioPackage(loadedAtlas)) {
      throw runtimeError(
        "baked package is not a responsive-audio preview",
        "browser-runtime-responsive-audio-kind-invalid",
        "load-responsive-audio",
      );
    }
    const atlasCursor = new HeldTexturePresentationRuntimeCursor(loadedAtlas);
    const lifecycleCursor = loadedAtlas.lifecyclePackage === null
      ? null
      : new ResponsiveAudioLifecycleCursor(
          loadedAtlas.lifecyclePackage,
          {
            audioLatencyFrames: atlasCursor.latencyFrames,
            runningFloorRpm: loadedAtlas.minimumRpm,
            heldAnchorFloorRpm: loadedAtlas.heldMinimumRpm,
            atlasLoadLanes:
              loadedAtlas.heldPackage.manifest.domain.load_lanes,
            atlasLoadCoordinate: loadedAtlas.heldPackage.loadCoordinate,
          },
        );
    const sharedStarterCursor = loadedAtlas.sharedRecordedStarterPackage === undefined
      ? null
      : new SharedRecordedStarterCursor(
          loadedAtlas.sharedRecordedStarterPackage,
          { audioLatencyFrames: atlasCursor.latencyFrames },
        );
    const atlasOutputBuffers = null;
    return {
      ...base,
      atlasCursor,
      lifecycleCursor,
      sharedStarterCursor,
      atlasOutputBuffers,
      atlasSilenceBuffer: new Float32Array(capacity),
      atlasBusIndex: loadedAtlas.busIds.indexOf(ATLAS_AUDITION_BUS_ID),
      comparisonMixer: new SourceBakedComparisonMixer({
        mode: SourceBakedComparisonMode.source,
      }),
      atlasStatus: lifecycleCursor === null ? "ready" : "active",
      atlasDetailCode: lifecycleCursor === null
        ? "browser-runtime-atlas-ready"
        : "browser-runtime-lifecycle-ready",
      atlasMessage: lifecycleCursor === null
        ? "baked package is loaded; start the session to acquire live coverage"
        : "baked lifecycle is ready from zero RPM; starter, first-fire, running and key-off remain under live control",
    };
  }

  #applyAtlasPlaybackState(playback) {
    this.#dryPhaseTracker.reset();
    this.#atlasPackage = playback.atlasPackage;
    this.#atlasPackageError = playback.atlasPackageError;
    this.#atlasUrl = playback.atlasUrl;
    this.#atlasCursor = playback.atlasCursor;
    this.#lifecycleCursor = playback.lifecycleCursor;
    this.#sharedStarterCursor = playback.sharedStarterCursor;
    this.#atlasOutputBuffers = playback.atlasOutputBuffers;
    this.#atlasSilenceBuffer = playback.atlasSilenceBuffer;
    this.#atlasBusIndex = playback.atlasBusIndex;
    this.#comparisonMixer = playback.comparisonMixer;
    this.#comparisonMode = playback.comparisonMode;
    this.#atlasStatus = playback.atlasStatus;
    this.#atlasDetailCode = playback.atlasDetailCode;
    this.#atlasMessage = playback.atlasMessage;
  }

  #isAtlasBusEligible(loadedAtlas, session, selectedBusIndex) {
    const selected = session.buses[selectedBusIndex] ?? null;
    return (
      loadedAtlas !== null &&
      selected?.kind === ATLAS_RUNTIME_BUS_KIND &&
      selected.channelCount === 1 &&
      loadedAtlas.busIds.includes(ATLAS_AUDITION_BUS_ID)
    );
  }

  #atlasSelectedBusEligible() {
    if (this.#atlasPackage === null || this.#program === null) {
      return false;
    }
    return this.#isAtlasBusEligible(
      this.#atlasPackage,
      this.#program.session,
      this.#selectedBusIndex,
    );
  }

  #warmDryPhaseRuntime(block, observation) {
    if (this.#lifecycleCursor !== null) {
      this.#warmLifecycleRunningBed(block, observation);
      return;
    }
    const endpoints = this.#advanceAtlasLifecycle(observation);
    if (this.#atlasStatus !== "active" || endpoints === null) return;
    try {
      this.#atlasCursor.warmBlock({
        frameCount: block.process.deliveryFrameCount,
        start: endpoints.start,
        end: endpoints.end,
      });
    } catch (error) {
      if (isRecoverableAtlasCoverageExit(error)) {
        this.#leaveAtlasCoverage(error.message, observation);
      } else {
        this.#failAtlasPlayback(error);
      }
    }
  }

  #renderAtlasComparison(block, observation) {
    return this.#renderDryPhaseComparison(block, observation);
  }

  #renderDryPhaseComparison(block, observation) {
    if (
      this.#comparisonMixer === null ||
      this.#atlasSilenceBuffer === null ||
      !(this.#atlasCursor instanceof HeldTexturePresentationRuntimeCursor)
    ) {
      throw runtimeError(
        "eligible dry directional playback was not initialized",
        "browser-runtime-dry-directional-playback-invariant",
        "render-audio-atlas",
      );
    }
    const frameCount = block.samples.length;
    const silence = frameCount === this.#atlasSilenceBuffer.length
      ? this.#atlasSilenceBuffer
      : this.#atlasSilenceBuffer.subarray(0, frameCount);
    if (this.#lifecycleCursor !== null) {
      return this.#renderLifecycleComparison(block, observation, silence);
    }
    const stateTransition = observation?.endpoints === null ||
      observation?.endpoints === undefined
      ? null
      : Object.freeze({
          startCombustion: endpointCombustionEnabled(
            observation.endpoints.start,
          ),
          endCombustion: endpointCombustionEnabled(observation.endpoints.end),
        });
    const priorAtlasStatus = this.#atlasStatus;
    const endpoints = this.#advanceAtlasLifecycle(observation);
    if (this.#atlasStatus === "motoring") {
      try {
        const rendered =
          endpoints !== null &&
          stateTransition?.startCombustion === true &&
          stateTransition.endCombustion === false
            ? this.#atlasCursor.renderRunningToMotoringTransitionBlock({
                sourceBlock: block.samples,
                runningStart: projectEndpointToRunningState(endpoints.start),
                runningEnd: projectEndpointToRunningState(endpoints.end),
                motoringStart: projectEndpointToMotoringState(endpoints.start),
                motoringEnd: projectEndpointToMotoringState(endpoints.end),
              })
            : this.#atlasCursor.renderMotoringBlock({
                sourceBlock: block.samples,
                start: projectEndpointToMotoringState(endpoints.start),
                end: projectEndpointToMotoringState(endpoints.end),
              });
        return this.#comparisonMixer.process(
          rendered.sourceBlock,
          rendered.bakedBlock,
        );
      } catch (error) {
        if (isRecoverableAtlasCoverageExit(error)) {
          this.#enterAtlasTailOnly(observation);
          const rendered = this.#atlasCursor.renderTailOnlyBlock({
            sourceBlock: block.samples,
          });
          return this.#comparisonMixer.process(
            rendered.sourceBlock,
            rendered.bakedBlock,
          );
        }
        this.#failAtlasPlayback(error);
        return this.#comparisonMixer.process(block.samples, silence);
      }
    }
    if (this.#atlasStatus === "tail-only") {
      try {
        if (
          endpoints !== null &&
          stateTransition?.startCombustion === true &&
          stateTransition.endCombustion === false
        ) {
          const rendered = this.#atlasCursor.renderCombustionTransitionBlock({
            sourceBlock: block.samples,
            start: projectEndpointToRunningState(endpoints.start),
            end: projectEndpointToRunningState(endpoints.end),
            startGain: 1,
            endGain: 0,
          });
          return this.#comparisonMixer.process(
            rendered.sourceBlock,
            rendered.bakedBlock,
          );
        }
        const rendered = this.#atlasCursor.renderTailOnlyBlock({
          sourceBlock: block.samples,
        });
        return this.#comparisonMixer.process(
          rendered.sourceBlock,
          rendered.bakedBlock,
        );
      } catch (error) {
        this.#failAtlasPlayback(error);
        return this.#comparisonMixer.process(block.samples, silence);
      }
    }
    if (this.#atlasStatus !== "active" || endpoints === null) {
      return this.#comparisonMixer.process(block.samples, silence);
    }
    try {
      const rendered =
        stateTransition?.startCombustion === false &&
        stateTransition.endCombustion === true &&
        priorAtlasStatus === "motoring"
          ? this.#atlasCursor.renderMotoringToRunningTransitionBlock({
              sourceBlock: block.samples,
              motoringStart: projectEndpointToMotoringState(
                observation.endpoints.start,
              ),
              motoringEnd: projectEndpointToMotoringState(
                observation.endpoints.end,
              ),
              runningStart: endpoints.start,
              runningEnd: endpoints.end,
            })
          : stateTransition?.startCombustion === false &&
              stateTransition.endCombustion === true
            ? this.#atlasCursor.renderCombustionTransitionBlock({
              sourceBlock: block.samples,
              start: endpoints.start,
              end: endpoints.end,
              startGain: 0,
              endGain: 1,
            })
            : this.#atlasCursor.renderBlock({
              sourceBlock: block.samples,
              start: endpoints.start,
              end: endpoints.end,
            });
      return this.#comparisonMixer.process(
        rendered.sourceBlock,
        rendered.bakedBlock,
      );
    } catch (error) {
      if (isRecoverableAtlasCoverageExit(error)) {
        this.#leaveAtlasCoverage(error.message, observation);
        return this.#comparisonMixer.process(block.samples, silence);
      }
      this.#failAtlasPlayback(error);
      return this.#comparisonMixer.process(block.samples, silence);
    }
  }

  #physicalLifecycleEndpoints(block, observation) {
    if (
      observation === null ||
      observation.kind === "invalid" ||
      observation.endpoint === null
    ) {
      return null;
    }
    if (observation.endpoints !== null) return observation.endpoints;
    const frameCount = block.process.deliveryFrameCount;
    const startFrame = observation.endpoint.frame - frameCount;
    if (!Number.isSafeInteger(startFrame) || startFrame < 0) return null;
    return Object.freeze({
      start: Object.freeze({
        ...observation.endpoint,
        frame: startFrame,
        rpmSlopeRpmPerSecond: 0,
      }),
      end: observation.endpoint,
    });
  }

  #projectLifecycleRunningEndpoint(endpoint) {
    return Object.freeze({
      ...endpoint,
      rpm: Math.max(
        this.#atlasPackage.minimumRpm,
        Math.min(this.#atlasPackage.maximumRpm, Math.abs(endpoint.rpm)),
      ),
      stateMask:
        endpoint.stateMask |
        ResponsiveEngineStateFlag.ignitionEnabled |
        ResponsiveEngineStateFlag.fuelEnabled,
    });
  }

  #scheduleLifecycleState(endpoints, projectedEndpoints, runningBedReady) {
    for (const key of ["start", "end"]) {
      const endpoint = endpoints[key];
      const runningBedWeights = this.#atlasCursor.runningLoadWeights(
        projectedEndpoints[key],
      );
      const state = lifecycleStateFromEndpoint(
        endpoint,
        runningBedReady,
        authoredThrottleLaneWeights(
          this.#atlasPackage,
          endpoint.requestedThrottle01,
        ),
        runningBedWeights.lanes,
      );
      this.#lifecycleCursor.setState(state);
      this.#sharedStarterCursor?.setState({
        starter: state.starter,
        ignition: state.ignition,
        fuel: state.fuel,
        rpm: state.rpm,
        combustionDetected:
          state.ignition &&
          state.fuel &&
          state.indicatedGasTorqueNm !== null &&
          state.indicatedGasTorqueNm > 0,
        frame: state.frame,
      });
    }
  }

  #ensureLifecycleRunningBed(endpoints) {
    const projected = Object.freeze({
      start: this.#projectLifecycleRunningEndpoint(endpoints.start),
      end: this.#projectLifecycleRunningEndpoint(endpoints.end),
    });
    if (this.#atlasCursor.activeSegmentId === null) {
      const initialized = this.#atlasCursor.initialize(projected.start);
      if (initialized.segmentId === null) {
        throw new HeldTexturePresentationRuntimeError(
          "lifecycle-running-bed-seed-rejected",
          "the hidden running bed rejected the lifecycle seed",
        );
      }
    }
    return projected;
  }

  #warmLifecycleRunningBed(block, observation) {
    const endpoints = this.#physicalLifecycleEndpoints(block, observation);
    if (endpoints === null) return;
    try {
      const projected = this.#ensureLifecycleRunningBed(endpoints);
      this.#atlasCursor.warmBlock({
        frameCount: block.process.deliveryFrameCount,
        start: projected.start,
        end: projected.end,
      });
    } catch (error) {
      this.#failAtlasPlayback(error);
    }
  }

  #renderLifecycleComparison(block, observation, silence) {
    const endpoints = this.#physicalLifecycleEndpoints(block, observation);
    if (endpoints === null) {
      return this.#comparisonMixer.process(block.samples, silence);
    }
    try {
      const projected = this.#ensureLifecycleRunningBed(endpoints);
      const running = this.#atlasCursor.renderBlock({
        sourceBlock: block.samples,
        start: projected.start,
        end: projected.end,
      });
      this.#scheduleLifecycleState(endpoints, projected, true);
      let pair = this.#lifecycleCursor.mixPair(
        running.sourceBlock,
        running.bakedBlock,
      );
      if (this.#sharedStarterCursor !== null) {
        pair = this.#sharedStarterCursor.mixPair(
          pair.sourceBlock,
          pair.bakedBlock,
        );
      }
      return this.#comparisonMixer.process(pair.sourceBlock, pair.bakedBlock);
    } catch (error) {
      this.#failAtlasPlayback(error);
      return this.#comparisonMixer.process(block.samples, silence);
    }
  }

  #advanceAtlasLifecycle(observation) {
    if (["error", "unavailable", "loading"].includes(this.#atlasStatus)) {
      return null;
    }
    if (observation === null || observation.kind === "invalid") {
      if (this.#atlasStatus !== "outside-coverage") {
        this.#leaveAtlasCoverage(
          observation?.reason ?? "live playback endpoint is unavailable",
          observation,
        );
      }
      return null;
    }

    if (!endpointCombustionEnabled(observation.endpoint)) {
      if (this.#atlasCursor?.activeSegmentId === null) {
        if (this.#atlasStatus !== "outside-coverage") {
          this.#leaveAtlasCoverage(
            "the package has no running seed from which to preserve a transfer tail",
            observation,
          );
        }
        return null;
      }
      if (
        this.#atlasCursor.hasMotoringTexture &&
        this.#atlasCursor.motoringCovers(
          projectEndpointToMotoringState(observation.endpoint),
        )
      ) {
        if (this.#atlasStatus !== "motoring") {
          this.#atlasStatus = "motoring";
          this.#atlasDetailCode =
            "browser-runtime-atlas-source-derived-motoring-active";
          this.#atlasMessage =
            "baked B is following source-derived ignition-off motoring texture from live RPM, manifold pressure and crank phase";
          this.#emitAtlasStatus(null, true);
        }
      } else if (this.#atlasStatus !== "tail-only") {
        this.#enterAtlasTailOnly(observation);
      }
      return observation.endpoints;
    }

    if (!this.#endpointInsideAtlasCoverage(observation.endpoint)) {
      if (this.#atlasStatus !== "outside-coverage") {
        this.#leaveAtlasCoverage(
          "live engine left the baked package's admitted operating domain",
          observation,
        );
      }
      return null;
    }

    if (["motoring", "tail-only"].includes(this.#atlasStatus)) {
      const resumedFromMotoring = this.#atlasStatus === "motoring";
      if (
        observation.endpoints === null ||
        !this.#endpointRpmInsideAtlasCoverage(observation.endpoints.start)
      ) {
        return null;
      }
      const projected = Object.freeze({
        start: projectEndpointToRunningState(observation.endpoints.start),
        end: projectEndpointToRunningState(observation.endpoints.end),
      });
      if (
        this.#atlasStatus === "tail-only" &&
        this.#atlasCursor.reseedRunning(projected.start).segmentId === null
      ) {
        this.#leaveAtlasCoverage(
          "the baked playback cursor rejected the rolling combustion restart",
          observation,
        );
        return null;
      }
      this.#atlasStatus = "active";
      this.#atlasDetailCode = resumedFromMotoring
        ? "browser-runtime-atlas-running-restarted-from-motoring"
        : "browser-runtime-atlas-running-resumed";
      this.#atlasMessage =
        "baked running texture resumed from live crank phase without resetting the presentation transfer/master";
      this.#emitAtlasStatus(null, true);
      return projected;
    }

    if (["ready", "outside-coverage"].includes(this.#atlasStatus)) {
      this.#beginAtlasArming(observation.endpoint);
      return null;
    }
    if (this.#atlasStatus === "arming") {
      if (
        observation.endpoints === null ||
        !this.#endpointInsideAtlasCoverage(observation.endpoints.start)
      ) {
        return null;
      }
      return this.#activateAtlasCursor(observation)
        ? observation.endpoints
        : null;
    }
    if (this.#atlasStatus === "active") {
      if (observation.endpoints === null) {
        this.#beginAtlasArming(observation.endpoint);
        return null;
      }
      return observation.endpoints;
    }
    return null;
  }

  #endpointInsideAtlasCoverage(endpoint) {
    const requiredRunningState =
      ResponsiveEngineStateFlag.ignitionEnabled |
      ResponsiveEngineStateFlag.fuelEnabled;
    return endpoint !== null &&
      this.#atlasPackage !== null &&
      Number.isFinite(endpoint.rpm) &&
      endpoint.rpm >= this.#atlasPackage.minimumRpm &&
      endpoint.rpm <= this.#atlasPackage.maximumRpm &&
      (endpoint.stateMask & requiredRunningState) === requiredRunningState;
  }

  #endpointRpmInsideAtlasCoverage(endpoint) {
    return endpoint !== null &&
      this.#atlasPackage !== null &&
      Number.isFinite(endpoint.rpm) &&
      endpoint.rpm >= this.#atlasPackage.minimumRpm &&
      endpoint.rpm <= this.#atlasPackage.maximumRpm;
  }

  #enterAtlasTailOnly(observation) {
    const endpoint = observation?.endpoint ?? observation?.endpoints?.end ?? null;
    this.#atlasStatus = "tail-only";
    this.#atlasDetailCode =
      "browser-runtime-atlas-combustion-off-tail-only-experiment";
    this.#atlasMessage =
      `baked B remains selected at ${this.#formatAtlasRpm(endpoint?.rpm)} RPM; new combustion excitation is gated to zero while the existing transfer/master tail drains. This diagnostic does not synthesize ignition-off pumping or mechanical rundown.`;
    this.#emitAtlasStatus(null, true);
  }

  #beginAtlasArming(endpoint) {
    this.#atlasCursor?.reset();
    this.#atlasStatus = "arming";
    this.#atlasDetailCode = "browser-runtime-atlas-acquiring-endpoint-pair";
    this.#atlasMessage =
      `captured the first covered endpoint at ${this.#formatAtlasRpm(endpoint.rpm)} RPM; waiting for the next consecutive endpoint`;
    this.#forceSourceFallback(
      "baked audio is acquiring consecutive covered endpoints",
    );
    this.#emitAtlasStatus(null, true);
  }

  #activateAtlasCursor(observation) {
    try {
      this.#atlasCursor.initialize(observation.endpoints.start);
      if (this.#atlasCursor.activeSegmentId === null) {
        this.#leaveAtlasCoverage(
          "the baked playback cursor rejected the covered endpoint pair",
          observation,
        );
        return false;
      }
      this.#atlasStatus = "active";
      this.#atlasDetailCode = "browser-runtime-atlas-active";
      this.#atlasMessage =
        "baked audio is responding to live RPM, manifold pressure and crank phase";
      this.#emitAtlasStatus(null, true);
      return true;
    } catch (error) {
      if (isRecoverableAtlasCoverageExit(error)) {
        this.#leaveAtlasCoverage(error.message, observation);
      } else {
        this.#failAtlasPlayback(error);
      }
      return false;
    }
  }

  #formatAtlasRpm(value) {
    return Number.isFinite(value) ? value.toFixed(0) : "unavailable";
  }

  #atlasCoverageDescription() {
    if (this.#atlasPackage === null) return "unavailable";
    return `${this.#formatAtlasRpm(this.#atlasPackage.minimumRpm)}..${this.#formatAtlasRpm(this.#atlasPackage.maximumRpm)} RPM`;
  }

  #leaveAtlasCoverage(reason, observation = null) {
    const wasBaked = this.#comparisonMode === SourceBakedComparisonMode.baked;
    const endpoint = observation?.endpoint ?? observation?.endpoints?.end ?? null;
    const currentRpm = endpoint?.rpm ?? observation?.rpm ?? null;
    const requiredRunningState =
      ResponsiveEngineStateFlag.ignitionEnabled |
      ResponsiveEngineStateFlag.fuelEnabled;
    const runningStateMissing = endpoint !== null &&
      (endpoint.stateMask & requiredRunningState) !== requiredRunningState;
    const rpmOutside = Number.isFinite(currentRpm) &&
      this.#atlasPackage !== null &&
      (currentRpm < this.#atlasPackage.minimumRpm ||
        currentRpm > this.#atlasPackage.maximumRpm);
    this.#atlasCursor?.reset();
    this.#atlasStatus = "outside-coverage";
    this.#atlasDetailCode = rpmOutside
      ? "browser-runtime-atlas-rpm-outside-coverage"
      : runningStateMissing
        ? "browser-runtime-atlas-running-state-outside-coverage"
        : "browser-runtime-atlas-live-endpoint-outside-coverage";
    this.#atlasMessage =
      `baked audio is outside package coverage: current ${this.#formatAtlasRpm(currentRpm)} RPM vs admitted ${this.#atlasCoverageDescription()}${runningStateMissing ? "; ignition and fuel must both be on" : ""}. Source A remains live.`;
    this.#forceSourceFallback(reason, wasBaked);
    this.#emitAtlasStatus(null, true);
  }

  #forceSourceFallback(reason, knownWasBaked = null) {
    const wasBaked = knownWasBaked ??
      this.#comparisonMode === SourceBakedComparisonMode.baked;
    this.#comparisonMode = SourceBakedComparisonMode.source;
    if (this.#comparisonMixer !== null) {
      this.#comparisonMixer.mode = SourceBakedComparisonMode.source;
    }
    if (wasBaked) {
      this.#emitComparisonMode(null, true, reason);
    }
  }

  #failAtlasPlayback(error) {
    const wasBaked = this.#comparisonMode === SourceBakedComparisonMode.baked;
    this.#atlasCursor?.reset();
    this.#lifecycleCursor?.reset(this.#generation);
    this.#sharedStarterCursor?.reset(this.#generation);
    this.#atlasStatus = "error";
    this.#atlasDetailCode =
      error.code ?? error.detailCode ?? "browser-runtime-atlas-playback-failed";
    this.#atlasMessage = error.message;
    this.#comparisonMode = SourceBakedComparisonMode.source;
    if (this.#comparisonMixer !== null) {
      this.#comparisonMixer.mode = SourceBakedComparisonMode.source;
    }
    if (wasBaked) {
      this.#emitComparisonMode(
        null,
        true,
        "baked audio left admitted atlas coverage",
      );
    }
    this.#emitAtlasStatus(null, true);
  }

  #atlasSnapshot() {
    const comparisonDiagnostics = this.#comparisonMixer?.diagnostics() ?? null;
    return Object.freeze({
      status: this.#atlasStatus,
      configured: this.#atlasUrl !== null,
      atlasUrl: this.#atlasUrl,
      atlasId: this.#atlasPackage?.manifest.id ?? null,
      atlasEngineId: this.#atlasPackage?.manifest.engine ?? null,
      minimumRpm: this.#atlasPackage?.minimumRpm ?? null,
      maximumRpm: this.#atlasPackage?.maximumRpm ?? null,
      selectedBusEligible: this.#atlasSelectedBusEligible(),
      bakedAvailable:
        ["active", "motoring", "tail-only"].includes(this.#atlasStatus) &&
        this.#atlasSelectedBusEligible(),
      comparisonMode: this.#comparisonMode,
      activeSegmentId: this.#atlasCursor?.activeSegmentId ?? null,
      detailCode: this.#atlasDetailCode,
      message: this.#atlasMessage,
      diagnostics: comparisonDiagnostics === null
        ? null
        : Object.freeze({
            ...comparisonDiagnostics,
            lifecycle: this.#lifecycleCursor?.diagnostics() ?? null,
            sharedRecordedStarter:
              this.#sharedStarterCursor?.diagnostics() ?? null,
          }),
    });
  }

  #emitAtlasLoading(requestId, atlasUrl) {
    this.#emit({
      type: "audio-atlas-status",
      requestId,
      status: "loading",
      configured: true,
      atlasUrl: String(atlasUrl),
      atlasId: null,
      atlasEngineId: null,
      selectedBusEligible: false,
      bakedAvailable: false,
      comparisonMode: SourceBakedComparisonMode.source,
      activeSegmentId: null,
      detailCode: "browser-runtime-atlas-loading",
      message: "loading and verifying the continuous audio atlas",
      diagnostics: null,
    });
    this.#emit({
      type: "comparison-mode",
      requestId: requestId ?? null,
      mode: SourceBakedComparisonMode.source,
      bakedAvailable: false,
      forced: true,
      reason: "compiled build replacement is loading",
    });
  }

  #emitAtlasStatus(requestId, _force = false) {
    this.#emit({
      type: "audio-atlas-status",
      requestId: requestId ?? null,
      ...this.#atlasSnapshot(),
    });
  }

  #emitSharedStarterStatus(requestId) {
    this.#emit({
      type: "shared-starter-status",
      requestId: requestId ?? null,
      sharedStarter: this.#sharedStarterCursor?.diagnostics() ?? Object.freeze({
        configured: false,
        loaded: false,
        enabled: false,
        active: false,
      }),
    });
  }

  #emitComparisonMode(requestId, forced, reason) {
    this.#emit({
      type: "comparison-mode",
      requestId: requestId ?? null,
      mode: this.#comparisonMode,
      bakedAvailable:
        ["active", "motoring", "tail-only"].includes(this.#atlasStatus) &&
        this.#atlasSelectedBusEligible(),
      forced,
      reason,
    });
  }

  #discardOutput(finalRingState) {
    ++this.#pumpEpoch;
    this.#output?.producer.setProducerState(finalRingState);
    this.#output = null;
    this.#outputSettings = null;
    this.#completionPending = false;
  }

  #restoreAfterFailedMutation(priorState, requestId = null) {
    this.#state = priorState;
    if (priorState === "running") {
      this.#output?.producer.setProducerState(RingState.streaming);
      this.#schedulePump(0);
    } else if (priorState === "preparing") {
      this.#output?.producer.setProducerState(RingState.idle);
      this.#schedulePump(0);
    } else if (priorState === "paused") {
      this.#output?.producer.setProducerState(
        this.#output.published ? RingState.paused : RingState.idle,
      );
    }
    this.#emitAtlasStatus(requestId, true);
    this.#emitComparisonMode(requestId, false, null);
    this.#emitState(requestId);
  }

  #emitBuilt(requestId) {
    this.#emit({
      type: "built",
      requestId,
      engineId: this.#program.engineId,
      scenarioId: this.#program.scenarioId,
      descriptor: publicDescriptor(
        this.#program,
        this.#selectedBusIndex,
      ),
      audioAtlas: this.#atlasSnapshot(),
    });
  }

  #emitState(requestId) {
    this.#emit({
      type: "state",
      requestId: requestId ?? null,
      state: this.#state,
      generation: this.#generation,
      selectedBusIndex: this.#selectedBusIndex,
      nextDeliveryFrame:
        this.#program && !["empty", "disposed"].includes(this.#state)
          ? this.#program.session.nextDeliveryFrame.toString(10)
          : null,
    });
  }

  #emitRuntimeStats(force = false, requestId = null) {
    if (this.#output === null) {
      if (force) {
        this.#emit({
          type: "runtime-stats",
          requestId,
          state: this.#state,
          generation: this.#generation,
          coreBlocks: this.#coreBlocks,
          generatedCanonicalFrames: this.#canonicalFrames.toString(10),
          generatedDeviceFrames: this.#deviceFrames,
          outputSampleRate: null,
          resamplerId: null,
          elapsedMilliseconds: 0,
          ring: null,
        });
      }
      return;
    }
    const now = performance.now();
    if (!force && now - this.#lastStatsAt < RUNTIME_STATS_INTERVAL_MS) {
      return;
    }
    this.#lastStatsAt = now;
    this.#emit({
      type: "runtime-stats",
      requestId,
      state: this.#state,
      generation: this.#generation,
      coreBlocks: this.#coreBlocks,
      generatedCanonicalFrames: this.#canonicalFrames.toString(10),
      generatedDeviceFrames: this.#deviceFrames,
      outputSampleRate: this.#outputSettings.outputSampleRate,
      resamplerId: DEVICE_RESAMPLER_ID,
      elapsedMilliseconds: this.#startedAt === 0 ? 0 : now - this.#startedAt,
      ring: this.#output.producer.snapshot(),
    });
  }

  #requireProgram(operation) {
    this.#assertNotDisposed();
    if (this.#program === null) {
      throw runtimeError(
        "compile an engine and scenario before using the session",
        "browser-runtime-program-missing",
        operation,
      );
    }
  }

  #assertNotExporting(operation) {
    if (this.#state === "exporting") {
      throw runtimeError(
        "the canonical export owns the worker until it completes",
        "browser-runtime-export-active",
        operation,
      );
    }
  }

  #assertNotDisposed() {
    if (this.#state === "disposed") {
      throw runtimeError(
        "the browser engine runtime is disposed",
        "browser-runtime-disposed",
      );
    }
  }
}

export { publicError };
