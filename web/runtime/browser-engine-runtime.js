import {
  AudioBusKind,
  ESO_CANONICAL_SAMPLE_RATE,
  ProcessKind,
  RingState,
  SessionExecutionKind,
} from "./c-api-abi.js";
import { loadAudioPackage } from "./audio-package-loader.js";
import { EngineSimCapiClient } from "./c-api-client.js";
import { EngineSimRuntimeError } from "./c-api-errors.js";
import { runCanonicalExport } from "./canonical-export.js";
import {
  DEVICE_RESAMPLER_ID,
  DeviceRateResampler,
} from "./device-resampler.js";
import {
  PcmRingProducer,
  choosePcmRingCapacity,
  createPcmRingBuffer,
} from "./pcm-ring-buffer.js";
import { ResponsiveAudioPackageFollower } from "./responsive-audio-package-follower.js";
import { SourceBakedSessionClock } from "./source-baked-session-clock.js";
import {
  SourceBakedComparisonMixer,
  SourceBakedComparisonMode,
} from "./source-baked-comparison-mixer.js";
import {
  liveControlCapability,
  publicDescriptor,
  readyMessage,
  validationMessage,
} from "./protocol.js";

const RUNTIME_STATS_INTERVAL_MS = 250;
const PRIMING_CORE_BLOCKS_PER_TURN = 4;
const RUNNING_CORE_BLOCKS_PER_TURN = 4;

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

async function sha256Utf8(text, cryptoImplementation) {
  if (!cryptoImplementation?.subtle?.digest) {
    throw runtimeError(
      "Web Crypto SHA-256 is unavailable",
      "browser-runtime-package-crypto-unavailable",
      "load-audio-package",
    );
  }
  let digest;
  try {
    digest = new Uint8Array(
      await cryptoImplementation.subtle.digest(
        "SHA-256",
        new TextEncoder().encode(text),
      ),
    );
  } catch (error) {
    throw new EngineSimRuntimeError("hashing the compiled engine JSON failed", {
      operation: "load-audio-package",
      detailCode: "browser-runtime-package-engine-hash-failed",
      diagnostics: [],
      cause: error,
    });
  }
  return Array.from(digest, (byte) =>
    byte.toString(16).padStart(2, "0"),
  ).join("");
}

function summarizedFollowerDiagnostics(follower) {
  if (follower === null) {
    return null;
  }
  const diagnostics = follower.diagnostics();
  const { selectionHistory = [], ...summary } = diagnostics;
  return {
    ...summary,
    selectionCount: selectionHistory.length,
    lastSelection: selectionHistory.at(-1) ?? null,
  };
}

export class BrowserEngineRuntime {
  #emit;
  #client;
  #program = null;
  #engineJson = null;
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
  #packageLoader;
  #crypto;
  #audioPackage = null;
  #packageBusId = null;
  #packageFollower = null;
  #packageClock = null;
  #comparisonMixer = null;
  #comparisonMode = SourceBakedComparisonMode.source;
  #packageFollowerAdmitted = false;
  #packageUnavailableFrameCount = 0;

  static async create({ moduleUrl, emit }) {
    const client = await EngineSimCapiClient.create(moduleUrl);
    return new BrowserEngineRuntime(client, emit);
  }

  constructor(
    client,
    emit,
    {
      packageLoader = loadAudioPackage,
      crypto = globalThis.crypto,
    } = {},
  ) {
    if (typeof emit !== "function") {
      throw new TypeError("BrowserEngineRuntime requires an event emitter");
    }
    this.#client = client;
    this.#emit = emit;
    this.#packageLoader = packageLoader;
    this.#crypto = crypto;
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
  }) {
    this.#assertNotDisposed();
    this.#assertNotExporting("build");
    const priorState = this.#state;
    this.#pausePump("compiling");
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
      this.#restoreAfterFailedMutation(priorState);
      if ((error.diagnostics?.length ?? 0) !== 0) {
        this.#emit(validationMessage(requestId, error));
        return;
      }
      throw error;
    }

    const previous = this.#program;
    this.#program = replacement;
    this.#engineJson = engineJson;
    this.#selectedBusIndex = replacement.session.auditionBusIndex;
    this.#detachAudioPackage("engine-rebuilt", requestId);
    this.#discardOutput(RingState.ended);
    this.#state = "ready";
    previous?.dispose();
    this.#emitBuilt(requestId);
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
      this.#replaceSession();
    }
    this.#selectedBusIndex = busIndex;
    if (!this.#selectedBusAdmitsAudioPackage()) {
      this.#detachAudioPackage("source-bus-changed", requestId);
    }
    this.#discardOutput(RingState.ended);
    this.#state = "ready";
    this.#emitBuilt(requestId);
    this.#emitState(requestId);
  }

  async loadAudioPackage({ requestId, packageManifestUrl }) {
    this.#requireProgram("load-audio-package");
    this.#assertNotExporting("load-audio-package");
    if (this.#state !== "ready") {
      throw runtimeError(
        "load the responsive audio package before starting its source session",
        "browser-runtime-package-load-requires-ready-session",
        "load-audio-package",
      );
    }
    if (!this.#selectedBusAdmitsAudioPackage()) {
      throw runtimeError(
        "responsive A/B requires the mono engine audition master source bus",
        "browser-runtime-package-source-bus-incompatible",
        "load-audio-package",
      );
    }

    const expectedProgram = this.#program;
    const expectedEngineJson = this.#engineJson;
    const loaded = await this.#packageLoader(packageManifestUrl);
    const engineDigest = await sha256Utf8(expectedEngineJson, this.#crypto);
    if (this.#program !== expectedProgram || this.#state !== "ready") {
      throw runtimeError(
        "the source session changed while its audio package was loading",
        "browser-runtime-package-load-source-changed",
        "load-audio-package",
      );
    }
    const packageEngine = loaded.manifest.identity.engine;
    if (
      packageEngine.id !== this.#program.engineId ||
      packageEngine.sha256 !== engineDigest
    ) {
      throw runtimeError(
        "the audio package does not identify the exact compiled engine JSON",
        "browser-runtime-package-engine-identity-mismatch",
        "load-audio-package",
      );
    }
    if (
      loaded.manifest.buses.length !== 1 ||
      loaded.manifest.buses[0].kind !== "master_engine_audition"
    ) {
      throw runtimeError(
        "the first responsive A/B path requires exactly one package audition master",
        "browser-runtime-package-bus-incompatible",
        "load-audio-package",
      );
    }

    const packageBusId = loaded.manifest.buses[0].id;
    const follower = new ResponsiveAudioPackageFollower(loaded);
    if (!follower.busIds.includes(packageBusId)) {
      throw runtimeError(
        "the package follower did not expose its declared audition master",
        "browser-runtime-package-bus-missing",
        "load-audio-package",
      );
    }

    this.#audioPackage = loaded;
    this.#packageBusId = packageBusId;
    this.#comparisonMode = SourceBakedComparisonMode.source;
    this.#packageFollower = follower;
    this.#packageClock = new SourceBakedSessionClock();
    this.#comparisonMixer = new SourceBakedComparisonMixer({
      mode: this.#comparisonMode,
    });
    this.#packageFollowerAdmitted = false;
    this.#packageUnavailableFrameCount = 0;
    const byteCount = loaded.artifacts.reduce(
      (sum, artifact) => sum + artifact.bytes.byteLength,
      0,
    );
    this.#emit({
      type: "audio-package",
      requestId,
      status: "loaded",
      packageId: loaded.manifest.identity.package_id,
      engine: { ...packageEngine },
      manifestUrl: loaded.manifestUrl,
      busIds: [...follower.busIds],
      artifactCount: loaded.artifacts.length,
      byteCount,
      comparisonMode: this.#comparisonMode,
    });
    this.#emitComparisonMode(requestId);
    this.#emitState(requestId);
  }

  setComparisonMode({ requestId, mode }) {
    this.#requireProgram("set-comparison-mode");
    if (this.#comparisonMixer === null) {
      throw runtimeError(
        "load a matching responsive audio package before selecting Baked B",
        "browser-runtime-package-not-loaded",
        "set-comparison-mode",
      );
    }
    this.#comparisonMixer.mode = mode;
    this.#comparisonMode = this.#comparisonMixer.mode;
    this.#emitComparisonMode(requestId);
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
    this.#detachAudioPackage("runtime-disposed", requestId);
    this.#program?.dispose();
    this.#program = null;
    this.#engineJson = null;
    this.#client.dispose();
    this.#state = "disposed";
    this.#emitState(requestId);
  }

  #configureOutput(settings, requestId) {
    const bus = this.#program.session.buses[this.#selectedBusIndex];
    const channelCount = this.#audioPackage === null ? bus.channelCount : 2;
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
        const sourceClock =
          this.#packageClock === null
            ? null
            : this.#packageClock.acceptBlock(block);
        let bakedPcm = null;
        if (this.#packageFollower !== null) {
          if (block.bus.channelCount !== 1) {
            throw runtimeError(
              "responsive A/B source ceased to be mono",
              "browser-runtime-package-source-bus-incompatible",
              "process-session",
            );
          }
          if (this.#isWarmNormalRunning(sourceClock)) {
            if (!this.#packageFollowerAdmitted) {
              this.#packageFollower.reset();
              this.#packageFollowerAdmitted = true;
            }
            const rendered = this.#packageFollower.renderBlock({
              firstDeliveryFrame: block.process.firstDeliveryFrame,
              frameCount: block.process.deliveryFrameCount,
              sourceClock,
              completedCycles: block.completedCycles,
            });
            bakedPcm = rendered.bus(this.#packageBusId);
            if (!(bakedPcm instanceof Float32Array)) {
              throw runtimeError(
                "responsive follower omitted the loaded package bus",
                "browser-runtime-package-output-missing",
                "process-session",
              );
            }
          } else {
            if (this.#packageFollowerAdmitted) {
              this.#packageFollower.reset();
              this.#packageFollowerAdmitted = false;
            }
            bakedPcm = new Float32Array(block.process.deliveryFrameCount);
            this.#packageUnavailableFrameCount +=
              block.process.deliveryFrameCount;
          }
        }
        if (block.audible) {
          this.#canonicalFrames += BigInt(
            block.samples.length / block.bus.channelCount,
          );
          let canonicalPcm = block.samples;
          if (bakedPcm !== null) {
            canonicalPcm = this.#comparisonMixer.process(
              block.samples,
              bakedPcm,
            );
          }
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

  #replaceSession() {
    const replacement = this.#program.createSession(
      this.#program.executionKind,
    );
    const previous = this.#program.session;
    this.#program.session = replacement;
    previous.dispose();
    this.#resetAudioPackagePlayback();
  }

  #discardOutput(finalRingState) {
    ++this.#pumpEpoch;
    this.#output?.producer.setProducerState(finalRingState);
    this.#output = null;
    this.#outputSettings = null;
    this.#completionPending = false;
  }

  #restoreAfterFailedMutation(priorState) {
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
  }

  #selectedBusAdmitsAudioPackage() {
    if (this.#program === null || this.#selectedBusIndex < 0) {
      return false;
    }
    const bus = this.#program.session.buses[this.#selectedBusIndex];
    return (
      bus.kindCode === AudioBusKind.engineAuditionMaster &&
      bus.channelCount === 1 &&
      bus.sampleRateHz === ESO_CANONICAL_SAMPLE_RATE
    );
  }

  #resetAudioPackagePlayback() {
    if (this.#audioPackage === null) {
      return;
    }
    this.#packageFollower = new ResponsiveAudioPackageFollower(
      this.#audioPackage,
    );
    this.#packageClock = new SourceBakedSessionClock();
    this.#comparisonMixer = new SourceBakedComparisonMixer({
      mode: this.#comparisonMode,
    });
    this.#packageFollowerAdmitted = false;
    this.#packageUnavailableFrameCount = 0;
  }

  #detachAudioPackage(reason, requestId) {
    if (this.#audioPackage === null) {
      return;
    }
    const packageId = this.#audioPackage.manifest.identity.package_id;
    this.#audioPackage = null;
    this.#packageBusId = null;
    this.#packageFollower = null;
    this.#packageClock = null;
    this.#comparisonMixer = null;
    this.#comparisonMode = SourceBakedComparisonMode.source;
    this.#packageFollowerAdmitted = false;
    this.#packageUnavailableFrameCount = 0;
    this.#emit({
      type: "audio-package",
      requestId: requestId ?? null,
      status: "unloaded",
      packageId,
      reason,
    });
  }

  #emitComparisonMode(requestId) {
    this.#emit({
      type: "comparison-mode",
      requestId: requestId ?? null,
      mode: this.#comparisonMode,
    });
  }

  #isWarmNormalRunning(sourceClock) {
    return (
      sourceClock.start.rpm > 0 &&
      sourceClock.end.rpm > 0 &&
      sourceClock.start.ignitionEnabled &&
      sourceClock.end.ignitionEnabled &&
      sourceClock.start.fuelEnabled &&
      sourceClock.end.fuelEnabled
    );
  }

  #packageRuntimeStats() {
    if (this.#audioPackage === null) {
      return null;
    }
    return {
      packageId: this.#audioPackage.manifest.identity.package_id,
      followerAdmitted: this.#packageFollowerAdmitted,
      unavailableFrameCount: this.#packageUnavailableFrameCount,
      comparison: this.#comparisonMixer?.diagnostics() ?? null,
      follower: summarizedFollowerDiagnostics(this.#packageFollower),
      sourceClock: this.#packageClock?.diagnostics() ?? null,
    };
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
      audioPackageId:
        this.#audioPackage?.manifest.identity.package_id ?? null,
      comparisonMode:
        this.#audioPackage === null ? null : this.#comparisonMode,
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
          audioPackage: this.#packageRuntimeStats(),
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
      audioPackage: this.#packageRuntimeStats(),
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
