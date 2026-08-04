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
import { loadAudioAtlas } from "./audio-atlas-loader.js";
import {
  AudioAtlasEngineStateFlag,
  ContinuousAudioAtlasCursor,
  ContinuousAudioAtlasCursorError,
} from "./continuous-audio-atlas-cursor.js";
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
const ATLAS_MACRO_SLOPE_WINDOW_FRAMES = ESO_CANONICAL_SAMPLE_RATE / 2;
const ATLAS_MACRO_SLOPE_HISTORY_FRAMES =
  (ESO_CANONICAL_SAMPLE_RATE * 3) / 4;
const ATLAS_AUDITION_BUS_ID = "master-engine-audition";
const ATLAS_RUNTIME_BUS_KIND = "engine-audition-master";
const ATLAS_AUTHORED_WOT_THROTTLE_01 = 1;
const ATLAS_AUTHORED_WOT_TOLERANCE = 1e-12;
const TWO_PI = 2 * Math.PI;
const RECOVERABLE_ATLAS_COVERAGE_EXIT_CODES = new Set([
  "continuous-atlas-state-transition-unsupported",
  "continuous-atlas-active-segment-uncovered",
  "continuous-atlas-source-rpm-mismatch",
  "continuous-atlas-source-slope-mismatch",
  "continuous-atlas-source-load-mismatch",
  "continuous-atlas-source-map-mismatch",
  "continuous-atlas-active-segment-exhausted",
]);

function isRecoverableAtlasCoverageExit(error) {
  return (
    error instanceof ContinuousAudioAtlasCursorError &&
    RECOVERABLE_ATLAS_COVERAGE_EXIT_CODES.has(error.code)
  );
}

function atlasStateMask(telemetry) {
  let mask = 0;
  if (telemetry.ignitionEnabled) {
    mask |= AudioAtlasEngineStateFlag.ignitionEnabled;
  }
  if (telemetry.fuelEnabled) {
    mask |= AudioAtlasEngineStateFlag.fuelEnabled;
  }
  if (telemetry.starterEnabled) {
    mask |= AudioAtlasEngineStateFlag.starterEnabled;
  }
  if (telemetry.limiterEnabled) {
    mask |= AudioAtlasEngineStateFlag.limiterEnabled;
  }
  if (telemetry.limiterCutActive) {
    mask |= AudioAtlasEngineStateFlag.limiterCutActive;
  }
  return mask;
}

// The atlas direction coordinate is deliberately a causal, macro-scale secant.
// Combustion ripple makes the instantaneous crank acceleration unsuitable for
// chronological-tape admission. Preparation blocks feed the same history, so
// the first audible block can already have a mature estimate.
class AtlasLiveStateTracker {
  #rpmHistory = [];
  #previousEndpoint = null;

  reset() {
    this.#rpmHistory.length = 0;
    this.#previousEndpoint = null;
  }

  observe(block) {
    const telemetry = block.telemetry.at(-1);
    if (telemetry === undefined) {
      this.#previousEndpoint = null;
      return null;
    }
    const endpointFrame =
      Number(BigInt(block.process.firstDeliveryFrame)) +
      block.process.deliveryFrameCount;
    if (!Number.isSafeInteger(endpointFrame)) {
      throw runtimeError(
        "atlas telemetry passed JavaScript's exact delivery-frame range",
        "browser-runtime-atlas-delivery-frame-inexact",
        "track-audio-atlas",
      );
    }
    const rpm = telemetry.engineSpeedRpm;
    this.#rpmHistory.push({ frame: endpointFrame, rpm });
    const oldestRetainedFrame =
      endpointFrame - ATLAS_MACRO_SLOPE_HISTORY_FRAMES;
    while (
      this.#rpmHistory.length > 2 &&
      this.#rpmHistory[1].frame < oldestRetainedFrame
    ) {
      this.#rpmHistory.shift();
    }

    const targetFrame = endpointFrame - ATLAS_MACRO_SLOPE_WINDOW_FRAMES;
    let baseline = null;
    let baselineDistance = Number.POSITIVE_INFINITY;
    for (const candidate of this.#rpmHistory) {
      if (candidate.frame >= endpointFrame) {
        break;
      }
      const distance = Math.abs(candidate.frame - targetFrame);
      if (distance < baselineDistance) {
        baseline = candidate;
        baselineDistance = distance;
      }
    }
    let endpoint = null;
    if (
      baseline !== null &&
      endpointFrame - baseline.frame >=
        ATLAS_MACRO_SLOPE_WINDOW_FRAMES * 0.9 &&
      Number.isFinite(rpm) &&
      rpm > 0 &&
      Number.isFinite(baseline.rpm) &&
      Number.isFinite(telemetry.meanIntakeManifoldPressurePaAbs) &&
      telemetry.meanIntakeManifoldPressurePaAbs > 0 &&
      Number.isFinite(telemetry.thetaRad) &&
      Number.isFinite(telemetry.requestedThrottle01) &&
      Math.abs(
        telemetry.requestedThrottle01 - ATLAS_AUTHORED_WOT_THROTTLE_01,
      ) <= ATLAS_AUTHORED_WOT_TOLERANCE
    ) {
      const seconds =
        (endpointFrame - baseline.frame) / ESO_CANONICAL_SAMPLE_RATE;
      endpoint = Object.freeze({
        rpm,
        rpmSlopeRpmPerSecond: (rpm - baseline.rpm) / seconds,
        // This first atlas has one explicitly authored full-load lane. MAP is
        // retained as an independent measured admission coordinate; it is not
        // substituted for the authored load coordinate.
        signedLoadCoordinate: 1,
        manifoldPressurePaAbs: telemetry.meanIntakeManifoldPressurePaAbs,
        unwrappedCrankRevolutions: telemetry.thetaRad / TWO_PI,
        stateMask: atlasStateMask(telemetry),
      });
    }
    const result =
      endpoint === null || this.#previousEndpoint === null
        ? null
        : Object.freeze({
            start: this.#previousEndpoint,
            end: endpoint,
          });
    this.#previousEndpoint = endpoint;
    return result;
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
  #atlasOutputBuffers = null;
  #atlasSilenceBuffer = null;
  #atlasBusIndex = -1;
  #atlasTracker = new AtlasLiveStateTracker();
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
      loadedAtlas = await loadAudioAtlas(atlasUrl);
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
      if (
        loadedAtlas.manifest.provenance.renderer_build.sha256 !==
        replacement.rendererSourceSha256
      ) {
        throw runtimeError(
          "audio atlas renderer provenance does not match the exact running renderer build",
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
      (this.#atlasStatus !== "active" || this.#comparisonMixer === null)
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
    this.#atlasOutputBuffers = null;
    this.#atlasSilenceBuffer = null;
    this.#comparisonMixer = null;
    this.#atlasTracker.reset();
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
        const atlasEndpoints = this.#atlasSelectedBusEligible()
          ? this.#atlasTracker.observe(block)
          : null;
        if (block.audible) {
          const canonicalPcm = this.#atlasSelectedBusEligible()
            ? this.#renderAtlasComparison(block, atlasEndpoints)
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
    const atlasCursor = new ContinuousAudioAtlasCursor(loadedAtlas);
    const atlasOutputBuffers = atlasCursor.createOutputBuffers(capacity);
    return {
      ...base,
      atlasCursor,
      atlasOutputBuffers,
      atlasSilenceBuffer: new Float32Array(capacity),
      atlasBusIndex: loadedAtlas.busIds.indexOf(ATLAS_AUDITION_BUS_ID),
      comparisonMixer: new SourceBakedComparisonMixer({
        mode: SourceBakedComparisonMode.source,
      }),
      atlasStatus: "arming",
      atlasDetailCode: "browser-runtime-atlas-awaiting-authored-wot",
      atlasMessage:
        "baked audio is waiting for consecutive full-throttle endpoints inside atlas coverage",
    };
  }

  #applyAtlasPlaybackState(playback) {
    this.#atlasTracker.reset();
    this.#atlasPackage = playback.atlasPackage;
    this.#atlasPackageError = playback.atlasPackageError;
    this.#atlasUrl = playback.atlasUrl;
    this.#atlasCursor = playback.atlasCursor;
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

  #renderAtlasComparison(block, endpoints) {
    if (
      this.#comparisonMixer === null ||
      this.#atlasSilenceBuffer === null ||
      this.#atlasOutputBuffers === null ||
      this.#atlasBusIndex < 0
    ) {
      throw runtimeError(
        "eligible audio atlas playback was not initialized",
        "browser-runtime-atlas-playback-invariant",
        "render-audio-atlas",
      );
    }
    const frameCount = block.samples.length;
    if (frameCount > this.#atlasSilenceBuffer.length) {
      throw runtimeError(
        "audio block exceeds the admitted atlas playback capacity",
        "browser-runtime-atlas-block-capacity-exceeded",
        "render-audio-atlas",
      );
    }
    const silence =
      frameCount === this.#atlasSilenceBuffer.length
        ? this.#atlasSilenceBuffer
        : this.#atlasSilenceBuffer.subarray(0, frameCount);
    let baked = silence;

    if (this.#atlasStatus === "active" && endpoints === null) {
      this.#leaveAtlasCoverage(
        "live engine left the atlas's explicitly authored full-throttle lane",
      );
    }
    if (this.#atlasStatus === "arming" && endpoints !== null) {
      try {
        this.#atlasCursor.initialize(endpoints.start);
      } catch (error) {
        this.#atlasCursor.reset();
        if (
          !(error instanceof ContinuousAudioAtlasCursorError) ||
          error.code !== "continuous-atlas-no-moving-segment"
        ) {
          this.#failAtlasPlayback(error);
        }
      }
      if (this.#atlasCursor.activeSegmentId !== null) {
        try {
          this.#atlasCursor.renderBlockInto({
            frameCount,
            start: endpoints.start,
            end: endpoints.end,
            outputBuffers: this.#atlasOutputBuffers,
          });
          this.#atlasStatus = "active";
          this.#atlasDetailCode = "browser-runtime-atlas-active";
          this.#atlasMessage =
            "baked audio is advancing continuously from the live engine state";
          this.#emitAtlasStatus(null, true);
        } catch (error) {
          if (isRecoverableAtlasCoverageExit(error)) {
            this.#leaveAtlasCoverage(error.message);
          } else {
            this.#failAtlasPlayback(error);
          }
        }
      }
    } else if (this.#atlasStatus === "active") {
      try {
        this.#atlasCursor.renderBlockInto({
          frameCount,
          start: endpoints.start,
          end: endpoints.end,
          outputBuffers: this.#atlasOutputBuffers,
        });
      } catch (error) {
        if (isRecoverableAtlasCoverageExit(error)) {
          this.#leaveAtlasCoverage(error.message);
        } else {
          this.#failAtlasPlayback(error);
        }
      }
    }

    if (this.#atlasStatus === "active") {
      const output = this.#atlasOutputBuffers[this.#atlasBusIndex];
      baked =
        frameCount === output.length
          ? output
          : output.subarray(0, frameCount);
    }
    return this.#comparisonMixer.process(block.samples, baked);
  }

  #leaveAtlasCoverage(reason) {
    const wasBaked = this.#comparisonMode === SourceBakedComparisonMode.baked;
    this.#atlasCursor?.reset();
    this.#atlasStatus = "arming";
    this.#atlasDetailCode = "browser-runtime-atlas-awaiting-authored-wot";
    this.#atlasMessage =
      "baked audio is waiting for consecutive full-throttle endpoints inside atlas coverage";
    this.#comparisonMode = SourceBakedComparisonMode.source;
    if (this.#comparisonMixer !== null) {
      this.#comparisonMixer.mode = SourceBakedComparisonMode.source;
    }
    if (wasBaked) {
      this.#emitComparisonMode(null, true, reason);
    }
    this.#emitAtlasStatus(null, true);
  }

  #failAtlasPlayback(error) {
    const wasBaked = this.#comparisonMode === SourceBakedComparisonMode.baked;
    this.#atlasCursor?.reset();
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
    return Object.freeze({
      status: this.#atlasStatus,
      configured: this.#atlasUrl !== null,
      atlasUrl: this.#atlasUrl,
      atlasId: this.#atlasPackage?.manifest.id ?? null,
      atlasEngineId: this.#atlasPackage?.manifest.engine ?? null,
      selectedBusEligible: this.#atlasSelectedBusEligible(),
      bakedAvailable:
        this.#atlasStatus === "active" && this.#atlasSelectedBusEligible(),
      comparisonMode: this.#comparisonMode,
      activeSegmentId: this.#atlasCursor?.activeSegmentId ?? null,
      detailCode: this.#atlasDetailCode,
      message: this.#atlasMessage,
      diagnostics: this.#comparisonMixer?.diagnostics() ?? null,
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

  #emitComparisonMode(requestId, forced, reason) {
    this.#emit({
      type: "comparison-mode",
      requestId: requestId ?? null,
      mode: this.#comparisonMode,
      bakedAvailable:
        this.#atlasStatus === "active" && this.#atlasSelectedBusEligible(),
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
