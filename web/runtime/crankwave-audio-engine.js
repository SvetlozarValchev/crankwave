import {
  HeldTexturePresentationRuntimeCursor,
} from "./held-texture-presentation-runtime.js";
import { loadResponsiveAudioCrankwave } from "./crankwave-package.js";

const RUNNING_STATE_MASK = 0x03;
const DEFAULT_RENDER_FRAMES = 8_192;
const PROCESS_CALLS_PER_SECOND = 50;
const MAXIMUM_RENDER_FRAMES = 1_048_576;

export class CrankwaveAudioEngineError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "CrankwaveAudioEngineError";
    this.code = code;
  }
}

function fail(code, message) {
  throw new CrankwaveAudioEngineError(code, message);
}

function finite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail("invalid-operating-point", `${label} must be finite`);
  }
  return value;
}

function unitInterval(value, label) {
  const result = finite(value, label);
  if (result < 0 || result > 1) {
    fail("invalid-operating-point", `${label} must lie in [0, 1]`);
  }
  return result;
}

function positiveFrameCount(value) {
  if (
    !Number.isSafeInteger(value) ||
    value <= 0 ||
    value > MAXIMUM_RENDER_FRAMES
  ) {
    fail(
      "invalid-frame-count",
      `frameCount must be an integer in [1, ${MAXIMUM_RENDER_FRAMES}]`,
    );
  }
  return value;
}

function copyOperatingPoint(value, minimumRpm, maximumRpm) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail("invalid-operating-point", "operating point must be an object");
  }
  const rpm = finite(value.rpm, "rpm");
  if (rpm < minimumRpm || rpm > maximumRpm) {
    fail(
      "rpm-outside-package",
      `rpm must lie in the CRANKWAVE range ${minimumRpm}..${maximumRpm}`,
    );
  }
  return Object.freeze({
    rpm,
    throttle01: unitInterval(value.throttle01, "throttle01"),
    load01: unitInterval(value.load01, "load01"),
  });
}

function rpmRows(package_, rpm) {
  const rows = package_.rows;
  const clamped = Math.max(
    package_.minimumRpm,
    Math.min(package_.maximumRpm, rpm),
  );
  if (clamped <= rows[0].rpm) {
    return { left: rows[0], right: rows[0], amount: 0 };
  }
  if (clamped >= rows.at(-1).rpm) {
    return { left: rows.at(-1), right: rows.at(-1), amount: 0 };
  }
  let rightIndex = 1;
  while (rows[rightIndex].rpm < clamped) ++rightIndex;
  const right = rows[rightIndex];
  if (right.rpm === clamped) {
    return { left: right, right, amount: 0 };
  }
  const left = rows[rightIndex - 1];
  return {
    left,
    right,
    amount: (clamped - left.rpm) / (right.rpm - left.rpm),
  };
}

function cellsByLane(row, lanes) {
  const declaredByLane = new Map(
    lanes.map(({ id, throttle01 }) => [id, throttle01]),
  );
  const cells = new Map();
  for (const cell of row.cells) {
    const aliases = Array.isArray(cell.loadAliases)
      ? cell.loadAliases
      : [{ lane: cell.lane, throttle01: declaredByLane.get(cell.lane) }];
    for (const alias of aliases) {
      if (
        declaredByLane.get(alias.lane) !== alias.throttle01 ||
        cells.has(alias.lane)
      ) {
        fail(
          "invalid-runtime-package",
          `${row.rpm} RPM has duplicate or undeclared load aliases`,
        );
      }
      cells.set(alias.lane, cell);
    }
  }
  if (
    cells.size !== lanes.length ||
    lanes.some(({ id }) => !cells.has(id))
  ) {
    fail(
      "invalid-runtime-package",
      `${row.rpm} RPM does not implement the complete load-lane aliases`,
    );
  }
  return cells;
}

function loadLaneCurves(package_, rpm) {
  const lanes = package_.manifest.domain.load_lanes;
  if (!Array.isArray(lanes) || lanes.length < 2) {
    fail(
      "invalid-runtime-package",
      "CRANKWAVE held material must declare at least two load lanes",
    );
  }
  for (let index = 0; index < lanes.length; ++index) {
    const lane = lanes[index];
    if (
      typeof lane?.id !== "string" ||
      lane.id.length === 0 ||
      !Number.isFinite(lane.throttle01) ||
      lane.throttle01 < 0 ||
      lane.throttle01 > 1 ||
      (index !== 0 && !(lane.throttle01 > lanes[index - 1].throttle01))
    ) {
      fail(
        "invalid-runtime-package",
        "CRANKWAVE load-lane coordinates must be ordered in [0, 1]",
      );
    }
  }
  const { left, right, amount } = rpmRows(package_, rpm);
  const leftCells = cellsByLane(left, lanes);
  const rightCells = left === right ? leftCells : cellsByLane(right, lanes);
  return lanes.map((lane) => {
    const leftMap = leftCells.get(lane.id).manifoldPressurePaAbs;
    const rightMap = rightCells.get(lane.id).manifoldPressurePaAbs;
    const map = leftMap + (rightMap - leftMap) * amount;
    if (!Number.isFinite(map) || map <= 0) {
      fail(
        "invalid-runtime-package",
        `CRANKWAVE load lane ${lane.id} has invalid captured pressure`,
      );
    }
    return Object.freeze({
      id: lane.id,
      coordinate01: lane.throttle01,
      manifoldPressurePaAbs: map,
    });
  });
}

function mapLoadToPressure(package_, rpm, load01) {
  const lanes = loadLaneCurves(package_, rpm);
  if (load01 <= lanes[0].coordinate01) {
    return lanes[0].manifoldPressurePaAbs;
  }
  if (load01 >= lanes.at(-1).coordinate01) {
    return lanes.at(-1).manifoldPressurePaAbs;
  }
  let rightIndex = 1;
  while (lanes[rightIndex].coordinate01 < load01) ++rightIndex;
  const left = lanes[rightIndex - 1];
  const right = lanes[rightIndex];
  const amount =
    (load01 - left.coordinate01) /
    (right.coordinate01 - left.coordinate01);
  return left.manifoldPressurePaAbs +
    (right.manifoldPressurePaAbs - left.manifoldPressurePaAbs) * amount;
}

function concatenateQueued(chunks, frameCount) {
  const output = new Float32Array(frameCount);
  let written = 0;
  while (written < frameCount) {
    const head = chunks[0];
    const copied = Math.min(frameCount - written, head.pcm.length - head.offset);
    output.set(head.pcm.subarray(head.offset, head.offset + copied), written);
    head.offset += copied;
    written += copied;
    if (head.offset === head.pcm.length) chunks.shift();
  }
  return output;
}

// A deliberately small, simulator-independent consumer facade:
//
//   .crankwave + { rpm, throttle01, load01 } -> mono Float32 PCM
//
// It does not load engine/scenario JSON, the C API, or a renderer WASM module.
export class CrankwaveAudioEngine {
  #loaded;
  #runtime;
  #cursor;
  #operatingPoint;
  #committedPoint;
  #crankRevolutions = 0;
  #lastRpmSlope = 0;
  #queue = [];
  #queuedFrames = 0;
  #renderedFrames = 0;
  #renderMode = null;
  #streamingInputFrames = 0;

  static async load(
    input,
    {
      crypto: cryptoImplementation = globalThis.crypto,
      sessionSeed = "0",
    } = {},
  ) {
    const loaded = await loadResponsiveAudioCrankwave(input, {
      crypto: cryptoImplementation,
    });
    return new CrankwaveAudioEngine(loaded, { sessionSeed });
  }

  constructor(loaded, { sessionSeed = "0" } = {}) {
    if (
      loaded?.package?.kind !== "crankwave-package" ||
      loaded?.runtime?.kind !== "responsive-audio-preview"
    ) {
      fail(
        "invalid-runtime-package",
        "CrankwaveAudioEngine requires a verified responsive CRANKWAVE",
      );
    }
    this.#loaded = loaded;
    this.#runtime = loaded.runtime;
    this.#cursor = new HeldTexturePresentationRuntimeCursor(
      this.#runtime,
      { sessionSeed },
    );
    const defaultRpm = Math.max(
      this.minimumRpm,
      Math.min(this.maximumRpm, 1_000),
    );
    this.#operatingPoint = Object.freeze({
      rpm: defaultRpm,
      throttle01: 0.15,
      load01: 0.15,
    });
    this.reset();
  }

  get engineId() {
    return this.#loaded.package.descriptor.engineId;
  }

  get sampleRate() {
    return this.#runtime.sampleRate;
  }

  get channelCount() {
    return 1;
  }

  get minimumRpm() {
    return this.#runtime.minimumRpm;
  }

  get maximumRpm() {
    return this.#runtime.maximumRpm;
  }

  get blockFrames() {
    return this.#runtime.batchFrames;
  }

  get processFrames() {
    return this.sampleRate / PROCESS_CALLS_PER_SECOND;
  }

  get latencyFrames() {
    return this.blockFrames;
  }

  get format() {
    return Object.freeze({
      sampleRateHz: this.sampleRate,
      channelCount: this.channelCount,
      sampleEncoding: "float32",
      interleaving: "mono",
      processFrames: this.processFrames,
      latencyFrames: this.latencyFrames,
      internalBlockFrames: this.blockFrames,
    });
  }

  get operatingPoint() {
    return this.#operatingPoint;
  }

  get queuedFrames() {
    return this.#queuedFrames;
  }

  loadManifoldPressurePa(rpm, load01) {
    const checkedRpm = finite(rpm, "rpm");
    if (checkedRpm < this.minimumRpm || checkedRpm > this.maximumRpm) {
      fail(
        "rpm-outside-package",
        `rpm must lie in the CRANKWAVE range ${this.minimumRpm}..${this.maximumRpm}`,
      );
    }
    return mapLoadToPressure(
      this.#runtime.heldPackage,
      checkedRpm,
      unitInterval(load01, "load01"),
    );
  }

  setOperatingPoint(value) {
    this.#operatingPoint = copyOperatingPoint(
      value,
      this.minimumRpm,
      this.maximumRpm,
    );
    return this.#operatingPoint;
  }

  reset() {
    this.#cursor.reset();
    this.#queue.length = 0;
    this.#queuedFrames = 0;
    this.#renderedFrames = 0;
    this.#renderMode = null;
    this.#streamingInputFrames = 0;
    this.#crankRevolutions = 0;
    this.#lastRpmSlope = 0;
    this.#committedPoint = this.#operatingPoint;
    const start = this.#endpoint(
      this.#committedPoint,
      this.#crankRevolutions,
      0,
    );
    if (this.#cursor.initialize(start).segmentId === null) {
      fail(
        "operating-point-outside-package",
        "CRANKWAVE rejected the initial operating point",
      );
    }
    const duration = this.blockFrames / this.sampleRate;
    this.#crankRevolutions +=
      this.#committedPoint.rpm * duration / 60;
    const end = this.#endpoint(
      this.#committedPoint,
      this.#crankRevolutions,
      0,
    );
    this.#cursor.warmBlock({
      frameCount: this.blockFrames,
      start,
      end,
    });
    this.#cursor.beginAudible();
    return this.#operatingPoint;
  }

  // Streaming hosts should use process(). It accepts a dense operating-point
  // endpoint, advances exactly frameCount physical frames, and returns the
  // same number of output frames. The responsive presentation works in larger
  // internal batches, so one batch of silence is queued once as explicit,
  // uniform control latency instead of coarsening the host trajectory.
  process(value, frameCount = this.processFrames) {
    const requested = positiveFrameCount(frameCount);
    const point = copyOperatingPoint(
      value,
      this.minimumRpm,
      this.maximumRpm,
    );
    if (this.#renderMode === "offline") {
      fail(
        "mixed-render-modes",
        "reset the CRANKWAVE engine before switching from render() to process()",
      );
    }
    if (this.#renderMode === null) {
      this.#renderMode = "streaming";
      const latency = new Float32Array(this.blockFrames);
      this.#queue.push({ pcm: latency, offset: 0 });
      this.#queuedFrames = latency.length;
    }
    this.#operatingPoint = point;
    const completedBefore = Math.floor(
      this.#streamingInputFrames / this.blockFrames,
    ) * this.blockFrames;
    const completedAfter = Math.floor(
      (this.#streamingInputFrames + requested) / this.blockFrames,
    ) * this.blockFrames;
    const rendered = this.#renderControlBlock(requested, point);
    if (rendered.length !== completedAfter - completedBefore) {
      fail(
        "runtime-output-shape",
        "CRANKWAVE runtime violated its fixed presentation batching",
      );
    }
    this.#streamingInputFrames += requested;
    this.#appendRendered(rendered);
    if (this.#queuedFrames < requested) {
      fail(
        "runtime-output-underflow",
        "CRANKWAVE presentation latency queue underflowed",
      );
    }
    const output = concatenateQueued(this.#queue, requested);
    this.#queuedFrames -= requested;
    const expectedQueued = this.blockFrames -
      (this.#streamingInputFrames % this.blockFrames);
    if (this.#queuedFrames !== expectedQueued) {
      fail(
        "runtime-latency-invariant",
        "CRANKWAVE presentation did not preserve one uniform latency batch",
      );
    }
    this.#renderedFrames += requested;
    return output;
  }

  render(frameCount = DEFAULT_RENDER_FRAMES) {
    const requested = positiveFrameCount(frameCount);
    if (this.#renderMode === "streaming") {
      fail(
        "mixed-render-modes",
        "reset the CRANKWAVE engine before switching from process() to render()",
      );
    }
    this.#renderMode = "offline";
    while (this.#queuedFrames < requested) {
      const rendered = this.#renderControlBlock(
        this.blockFrames,
        this.#operatingPoint,
      );
      if (rendered.length !== this.blockFrames) {
        fail(
          "runtime-output-shape",
          "CRANKWAVE runtime did not return one complete mono PCM block",
        );
      }
      this.#appendRendered(rendered);
    }
    const output = concatenateQueued(this.#queue, requested);
    this.#queuedFrames -= requested;
    this.#renderedFrames += requested;
    return output;
  }

  diagnostics() {
    return Object.freeze({
      engineId: this.engineId,
      sampleRate: this.sampleRate,
      blockFrames: this.blockFrames,
      renderedFrames: this.#renderedFrames,
      queuedFrames: this.#queuedFrames,
      renderMode: this.#renderMode,
      operatingPoint: this.#operatingPoint,
      committedOperatingPoint: this.#committedPoint,
      loadManifoldPressurePa: this.loadManifoldPressurePa(
        this.#operatingPoint.rpm,
        this.#operatingPoint.load01,
      ),
      runtime: this.#cursor.diagnostics(),
    });
  }

  #endpoint(point, crankRevolutions, rpmSlopeRpmPerSecond) {
    return Object.freeze({
      rpm: point.rpm,
      requestedThrottle01: point.throttle01,
      manifoldPressurePaAbs: this.loadManifoldPressurePa(
        point.rpm,
        point.load01,
      ),
      unwrappedCrankRevolutions: crankRevolutions,
      rpmSlopeRpmPerSecond,
      stateMask: RUNNING_STATE_MASK,
    });
  }

  #appendRendered(rendered) {
    if (rendered.length === 0) return;
    this.#queue.push({ pcm: rendered, offset: 0 });
    this.#queuedFrames += rendered.length;
  }

  #renderControlBlock(frameCount, endPoint) {
    const startPoint = this.#committedPoint;
    const duration = frameCount / this.sampleRate;
    const rpmSlope = (endPoint.rpm - startPoint.rpm) / duration;
    const start = this.#endpoint(
      startPoint,
      this.#crankRevolutions,
      this.#lastRpmSlope,
    );
    this.#crankRevolutions +=
      0.5 * (startPoint.rpm + endPoint.rpm) * duration / 60;
    const end = this.#endpoint(
      endPoint,
      this.#crankRevolutions,
      rpmSlope,
    );
    const rendered = this.#cursor.renderBlock({
      sourceBlock: new Float32Array(frameCount),
      start,
      end,
    }).bakedBlock;
    if (!(rendered instanceof Float32Array)) {
      fail(
        "runtime-output-shape",
        "CRANKWAVE runtime did not return mono Float32 PCM",
      );
    }
    this.#committedPoint = endPoint;
    this.#lastRpmSlope = rpmSlope;
    return rendered;
  }
}
