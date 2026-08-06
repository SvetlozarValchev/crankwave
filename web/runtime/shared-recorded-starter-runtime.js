const SCHEMA = "engine-sim-offline/shared-recorded-starter-research";
const KIND = "shared-recorded-starter-research";
const SAMPLE_RATE = 192_000;
const EXPECTED_ID = "shared-recorded-semi-truck-starter-local-audition";
const EXPECTED_SOURCE_SHA256 =
  "8edcfa21f846098472dd3f57236f23367a7667f4458f7452b565370062635a81";
const EXPECTED_PAYLOAD_SHA256 =
  "1949863ca58aef11146d4a842609ef217f6b7df4ba6db38f478eb918cef2964a";
const EXPECTED_FRAME_COUNT = 259_318;
const EXPECTED_BYTE_COUNT = EXPECTED_FRAME_COUNT * Float32Array.BYTES_PER_ELEMENT;
const RIGHTS_WARNING =
  "UNLICENSED RESEARCH SOURCE: downloaded semi-truck diesel recording; " +
  "local audition only; do not redistribute or ship.";

const EXPECTED_MARKERS = Object.freeze({
  repeatInFrame: 82_969,
  repeatOutFrame: 255_843,
  cutFrame: 259_318,
  seamCrossfadeFrames: 9_600,
});

const EXPECTED_SETTINGS = Object.freeze({
  defaultEnabled: true,
  sourceGain: 1,
  speedUpStartRpm: 500,
  speedUpEndRpm: 760,
  basePlaybackRate: 0.8,
  catchPlaybackRate: 1.05,
  speedUpCurve: 1.6,
  rpmSmoothingMilliseconds: 0,
  attackMilliseconds: 8,
  preCatchEngineGain: 0.903125,
  catchRpm: 400,
  ignitionDuckLeadMilliseconds: 30,
  catchStarterGain: 0.2,
  engineCatchGain: 1,
  handoffMilliseconds: 520,
  catchOffsetMilliseconds: 0,
});

function fail(message) {
  throw new TypeError(message);
}

function object(value, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  return value;
}

function string(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    fail(`${label} must be a nonempty string`);
  }
  return value;
}

function finite(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    fail(`${label} must be finite`);
  }
  return value;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function nonnegativeInteger(value, label) {
  if (!Number.isSafeInteger(value) || value < 0) {
    fail(`${label} must be a nonnegative integer`);
  }
  return value;
}

function boolean(value, label) {
  if (typeof value !== "boolean") fail(`${label} must be boolean`);
  return value;
}

function exact(value, expected, label) {
  if (!Object.is(value, expected)) {
    fail(`${label} must equal the accepted engine-audio-lab value ${String(expected)}`);
  }
  return value;
}

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("shared recorded starter manifest URL must be nonempty");
  }
  return new URL(value, globalThis.location?.href ?? "http://localhost/");
}

function relativeUrl(manifestUrl, value, label) {
  const path = string(value, label);
  if (path.startsWith("/") || path.includes("..") || path.includes("\\")) {
    fail(`${label} must stay beneath its manifest`);
  }
  return new URL(path, manifestUrl);
}

async function fetchBytes(url, fetchImplementation, label) {
  const response = await fetchImplementation(url.href, { cache: "no-store" });
  if (!response?.ok || typeof response.arrayBuffer !== "function") {
    throw new Error(
      `${label} fetch failed: HTTP ${response?.status ?? "?"} for ${url.href}`,
    );
  }
  return new Uint8Array(await response.arrayBuffer());
}

function hex(bytes) {
  return Array.from(
    bytes,
    (value) => value.toString(16).padStart(2, "0"),
  ).join("");
}

function decodeFloat32Le(bytes, frameCount) {
  if (bytes.byteLength !== frameCount * Float32Array.BYTES_PER_ELEMENT) {
    throw new RangeError("shared starter payload byte count is invalid");
  }
  const samples = new Float32Array(frameCount);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let frame = 0; frame < frameCount; ++frame) {
    const sample = view.getFloat32(frame * Float32Array.BYTES_PER_ELEMENT, true);
    if (!Number.isFinite(sample)) {
      throw new RangeError(
        `shared starter payload contains a non-finite sample at frame ${frame}`,
      );
    }
    samples[frame] = sample;
  }
  return samples;
}

function acceptedMarkers(manifest) {
  const markers = object(manifest.markers, "manifest.markers");
  const result = {
    repeatInFrame: positiveInteger(
      markers.repeat_in_frame,
      "manifest.markers.repeat_in_frame",
    ),
    repeatOutFrame: positiveInteger(
      markers.repeat_out_frame,
      "manifest.markers.repeat_out_frame",
    ),
    cutFrame: positiveInteger(
      markers.cut_frame,
      "manifest.markers.cut_frame",
    ),
    seamCrossfadeFrames: positiveInteger(
      markers.seam_crossfade_frames,
      "manifest.markers.seam_crossfade_frames",
    ),
  };
  for (const [name, expected] of Object.entries(EXPECTED_MARKERS)) {
    exact(result[name], expected, `manifest.markers.${name}`);
  }
  if (
    !(result.repeatInFrame < result.repeatOutFrame) ||
    result.repeatOutFrame >= result.cutFrame ||
    result.seamCrossfadeFrames * 2 >=
      result.repeatOutFrame - result.repeatInFrame
  ) {
    fail("shared starter repeat/cut geometry is invalid");
  }
  return Object.freeze(result);
}

function acceptedSettings(manifest) {
  const mix = object(manifest.mix, "manifest.mix");
  const result = {
    defaultEnabled: boolean(
      mix.default_enabled,
      "manifest.mix.default_enabled",
    ),
    sourceGain: finite(mix.source_gain, "manifest.mix.source_gain"),
    speedUpStartRpm: finite(
      mix.speed_up_start_rpm,
      "manifest.mix.speed_up_start_rpm",
    ),
    speedUpEndRpm: finite(
      mix.speed_up_end_rpm,
      "manifest.mix.speed_up_end_rpm",
    ),
    basePlaybackRate: finite(
      mix.base_playback_rate,
      "manifest.mix.base_playback_rate",
    ),
    catchPlaybackRate: finite(
      mix.catch_playback_rate,
      "manifest.mix.catch_playback_rate",
    ),
    speedUpCurve: finite(
      mix.speed_up_curve,
      "manifest.mix.speed_up_curve",
    ),
    rpmSmoothingMilliseconds: finite(
      mix.rpm_smoothing_milliseconds,
      "manifest.mix.rpm_smoothing_milliseconds",
    ),
    attackMilliseconds: finite(
      mix.attack_milliseconds,
      "manifest.mix.attack_milliseconds",
    ),
    preCatchEngineGain: finite(
      mix.pre_catch_engine_gain,
      "manifest.mix.pre_catch_engine_gain",
    ),
    catchRpm: finite(mix.catch_rpm, "manifest.mix.catch_rpm"),
    ignitionDuckLeadMilliseconds: finite(
      mix.ignition_duck_lead_milliseconds,
      "manifest.mix.ignition_duck_lead_milliseconds",
    ),
    catchStarterGain: finite(
      mix.catch_starter_gain,
      "manifest.mix.catch_starter_gain",
    ),
    engineCatchGain: finite(
      mix.engine_catch_gain,
      "manifest.mix.engine_catch_gain",
    ),
    handoffMilliseconds: finite(
      mix.handoff_milliseconds,
      "manifest.mix.handoff_milliseconds",
    ),
    catchOffsetMilliseconds: finite(
      mix.catch_offset_milliseconds,
      "manifest.mix.catch_offset_milliseconds",
    ),
  };
  for (const [name, expected] of Object.entries(EXPECTED_SETTINGS)) {
    exact(result[name], expected, `manifest.mix.${name}`);
  }
  return Object.freeze(result);
}

function validateResearchProvenance(manifest) {
  const rights = object(manifest.rights, "manifest.rights");
  exact(rights.status, "unverified", "manifest.rights.status");
  exact(rights.audition_only, true, "manifest.rights.audition_only");
  exact(
    rights.redistribution_authorized,
    false,
    "manifest.rights.redistribution_authorized",
  );
  exact(rights.warning, RIGHTS_WARNING, "manifest.rights.warning");
  const provenance = object(manifest.provenance, "manifest.provenance");
  const source = object(provenance.source, "manifest.provenance.source");
  exact(
    source.sha256,
    EXPECTED_SOURCE_SHA256,
    "manifest.provenance.source.sha256",
  );
  exact(
    source.asset_file,
    "recorded-starter-source.mp3",
    "manifest.provenance.source.asset_file",
  );
  exact(source.codec, "mp3", "manifest.provenance.source.codec");
  exact(
    source.sample_rate_hz,
    44_100,
    "manifest.provenance.source.sample_rate_hz",
  );
  exact(source.channels, 2, "manifest.provenance.source.channels");
  if (!string(source.original_path, "manifest.provenance.source.original_path")
    .includes("SEMI_TRUCK")) {
    fail("shared starter source must retain its semi-truck research provenance");
  }
  object(provenance.canonicalization, "manifest.provenance.canonicalization");
}

export async function loadSharedRecordedStarterRuntime(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("shared recorded starter loading requires fetch");
  }
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error("Web Crypto SHA-256 is required for the shared starter");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifestBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "shared starter manifest",
  );
  const manifest = object(
    JSON.parse(
      new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes),
    ),
    "manifest",
  );
  exact(manifest.schema, SCHEMA, "manifest.schema");
  exact(manifest.id, EXPECTED_ID, "manifest.id");
  exact(
    manifest.purpose,
    "shared-source-a-and-b-local-audition-layer",
    "manifest.purpose",
  );
  validateResearchProvenance(manifest);
  const markers = acceptedMarkers(manifest);
  const settings = acceptedSettings(manifest);

  const audio = object(manifest.audio, "manifest.audio");
  exact(audio.sample_rate_hz, SAMPLE_RATE, "manifest.audio.sample_rate_hz");
  exact(audio.encoding, "float32le", "manifest.audio.encoding");
  exact(audio.channel_layout, "mono", "manifest.audio.channel_layout");
  exact(
    positiveInteger(audio.frame_count, "manifest.audio.frame_count"),
    EXPECTED_FRAME_COUNT,
    "manifest.audio.frame_count",
  );
  exact(
    positiveInteger(audio.byte_count, "manifest.audio.byte_count"),
    EXPECTED_BYTE_COUNT,
    "manifest.audio.byte_count",
  );
  exact(
    string(audio.payload_sha256, "manifest.audio.payload_sha256"),
    EXPECTED_PAYLOAD_SHA256,
    "manifest.audio.payload_sha256",
  );
  const payloadUrl = relativeUrl(
    manifestUrl,
    audio.relative_path,
    "manifest.audio.relative_path",
  );
  const payload = await fetchBytes(
    payloadUrl,
    fetchImplementation,
    "shared starter payload",
  );
  if (payload.byteLength !== EXPECTED_BYTE_COUNT) {
    throw new RangeError(
      `shared starter payload has ${payload.byteLength} bytes; ` +
      `expected ${EXPECTED_BYTE_COUNT}`,
    );
  }
  const digest = hex(
    new Uint8Array(
      await cryptoImplementation.subtle.digest("SHA-256", payload),
    ),
  );
  if (digest !== EXPECTED_PAYLOAD_SHA256) {
    throw new Error(
      `shared starter SHA-256 mismatch: expected ${EXPECTED_PAYLOAD_SHA256}, ` +
      `fetched ${digest}`,
    );
  }
  const samples = decodeFloat32Le(payload, EXPECTED_FRAME_COUNT);
  return Object.freeze({
    kind: KIND,
    manifestUrl: manifestUrl.href,
    payloadUrl: payloadUrl.href,
    manifest,
    sampleRate: SAMPLE_RATE,
    samples,
    markers,
    settings,
    defaultEnabled: settings.defaultEnabled,
    rightsWarning: RIGHTS_WARNING,
    sourceSha256: EXPECTED_SOURCE_SHA256,
    payloadSha256: EXPECTED_PAYLOAD_SHA256,
  });
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function acceptedState(value) {
  object(value, "shared starter state");
  if (!Number.isSafeInteger(value.frame) || value.frame < 0) {
    fail("shared starter state.frame must be a nonnegative safe integer");
  }
  return Object.freeze({
    starter: boolean(value.starter, "shared starter state.starter"),
    ignition: boolean(value.ignition, "shared starter state.ignition"),
    fuel: boolean(value.fuel, "shared starter state.fuel"),
    rpm: finite(value.rpm, "shared starter state.rpm"),
    combustionDetected: boolean(
      value.combustionDetected,
      "shared starter state.combustionDetected",
    ),
    frame: value.frame,
  });
}

function validateMonoBlock(value, label) {
  if (!(value instanceof Float32Array)) {
    fail(`${label} must be a mono Float32Array`);
  }
  return value;
}

export class SharedRecordedStarterCursor {
  #package;
  #audioLatencyFrames;
  #enabled;
  #generation = 0;
  #scheduledStates = [];
  #lastScheduledFrame = -1;
  #outputFrame = null;
  #state = Object.freeze({
    starter: false,
    ignition: false,
    fuel: false,
    rpm: 0,
    combustionDetected: false,
    frame: 0,
  });
  #lastStarterCommanded = false;
  #sessionActive = false;
  #sessionStartFrame = -1;
  #sourcePosition = 0;
  #catchFrame = -1;
  #physicalCatchFrame = -1;
  #audibleCatchFrame = -1;
  #handoffStartFrame = -1;
  #handoffEndFrame = -1;
  #smoothedRpm = 0;
  #peakRpm = 0;
  #currentPlaybackRate = 0;
  #currentStarterGain = 0;
  #currentEngineGain = 1;
  #audiblyActive = false;
  #sessionCount = 0;
  #catchCount = 0;
  #fallbackCatchCount = 0;
  #renderedFrames = 0;
  #postRepeatTailEnvelopeFrames = 0;
  #unavailableEnvelopeFrames = 0;
  #currentUnavailableEnvelopeRunFrames = 0;
  #longestUnavailableEnvelopeRunFrames = 0;

  constructor(package_, { audioLatencyFrames = 0 } = {}) {
    if (package_?.kind !== KIND || package_.sampleRate !== SAMPLE_RATE) {
      fail("SharedRecordedStarterCursor requires a verified shared starter package");
    }
    this.#package = package_;
    this.#audioLatencyFrames = nonnegativeInteger(
      audioLatencyFrames,
      "shared starter audioLatencyFrames",
    );
    this.#enabled = package_.defaultEnabled;
  }

  get enabled() {
    return this.#enabled;
  }

  set enabled(value) {
    this.setEnabled(value);
  }

  setEnabled(value) {
    this.#enabled = boolean(value, "shared starter enabled");
  }

  reset(generation = this.#generation + 1) {
    if (!Number.isSafeInteger(generation) || generation < 0) {
      fail("shared starter generation must be a nonnegative safe integer");
    }
    this.#generation = generation;
    this.#scheduledStates = [];
    this.#lastScheduledFrame = -1;
    this.#outputFrame = null;
    this.#state = Object.freeze({
      starter: false,
      ignition: false,
      fuel: false,
      rpm: 0,
      combustionDetected: false,
      frame: 0,
    });
    this.#lastStarterCommanded = false;
    this.#sessionActive = false;
    this.#sessionStartFrame = -1;
    this.#sourcePosition = 0;
    this.#catchFrame = -1;
    this.#physicalCatchFrame = -1;
    this.#audibleCatchFrame = -1;
    this.#handoffStartFrame = -1;
    this.#handoffEndFrame = -1;
    this.#smoothedRpm = 0;
    this.#peakRpm = 0;
    this.#currentPlaybackRate = 0;
    this.#currentStarterGain = 0;
    this.#currentEngineGain = 1;
    this.#audiblyActive = false;
    this.#sessionCount = 0;
    this.#catchCount = 0;
    this.#fallbackCatchCount = 0;
    this.#renderedFrames = 0;
    this.#postRepeatTailEnvelopeFrames = 0;
    this.#unavailableEnvelopeFrames = 0;
    this.#currentUnavailableEnvelopeRunFrames = 0;
    this.#longestUnavailableEnvelopeRunFrames = 0;
  }

  setState(value) {
    const state = acceptedState(value);
    if (state.frame < this.#lastScheduledFrame) {
      fail("shared starter states must be scheduled in nondecreasing frame order");
    }
    if (this.#outputFrame !== null && state.frame < this.#outputFrame) {
      fail("shared starter state arrived after its output frame was rendered");
    }
    this.#scheduledStates.push(state);
    this.#lastScheduledFrame = state.frame;
    if (this.#outputFrame === null) this.#outputFrame = state.frame;
  }

  #applyScheduledStates(frame) {
    let consumed = 0;
    while (
      consumed < this.#scheduledStates.length &&
      this.#scheduledStates[consumed].frame <= frame
    ) {
      this.#state = this.#scheduledStates[consumed];
      ++consumed;
    }
    if (consumed > 0) this.#scheduledStates.splice(0, consumed);
  }

  #beginSession(frame) {
    this.#sessionActive = true;
    this.#sessionStartFrame = frame;
    this.#sourcePosition = 0;
    this.#catchFrame = -1;
    this.#physicalCatchFrame = -1;
    this.#audibleCatchFrame = -1;
    this.#handoffStartFrame = -1;
    this.#handoffEndFrame = -1;
    this.#smoothedRpm = Math.max(0, Math.abs(this.#state.rpm));
    this.#peakRpm = this.#smoothedRpm;
    this.#currentPlaybackRate = this.#package.settings.basePlaybackRate;
    this.#currentStarterGain = 0;
    this.#currentEngineGain = 1;
    this.#audiblyActive = false;
    ++this.#sessionCount;
  }

  #setCatch(frame, fallback) {
    if (!this.#sessionActive || this.#catchFrame >= 0) return;
    const settings = this.#package.settings;
    this.#physicalCatchFrame = frame;
    this.#audibleCatchFrame = frame + this.#audioLatencyFrames;
    this.#catchFrame = this.#audibleCatchFrame;
    const catchOffsetFrames = Math.round(
      settings.catchOffsetMilliseconds * SAMPLE_RATE / 1_000,
    );
    const duckFrames = Math.max(
      0,
      Math.round(
        settings.ignitionDuckLeadMilliseconds * SAMPLE_RATE / 1_000,
      ),
    );
    const nominalStart = Math.max(
      0,
      this.#audibleCatchFrame + catchOffsetFrames,
    );
    this.#handoffStartFrame = Math.max(
      nominalStart,
      this.#audibleCatchFrame + duckFrames,
    );
    this.#handoffEndFrame = this.#handoffStartFrame + Math.max(
      0,
      Math.round(settings.handoffMilliseconds * SAMPLE_RATE / 1_000),
    );
    ++this.#catchCount;
    if (fallback) ++this.#fallbackCatchCount;
  }

  #sampleAt(position) {
    const samples = this.#package.samples;
    const center = Math.floor(position);
    const fraction = position - center;
    const at = (index) => samples[
      Math.max(0, Math.min(samples.length - 1, index))
    ] ?? 0;
    const p0 = at(center - 1);
    const p1 = at(center);
    const p2 = at(center + 1);
    const p3 = at(center + 2);
    const a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    const b = p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3;
    const c = -0.5 * p0 + 0.5 * p2;
    return ((a * fraction + b) * fraction + c) * fraction + p1;
  }

  #starterSample() {
    const { markers } = this.#package;
    if (this.#sourcePosition >= this.#package.samples.length) return 0;
    const seamFrames = Math.max(
      0,
      Math.min(
        markers.seamCrossfadeFrames,
        markers.repeatOutFrame - markers.repeatInFrame - 1,
      ),
    );
    const seamStart = markers.repeatOutFrame - seamFrames;
    if (
      seamFrames > 0 &&
      this.#sourcePosition >= seamStart &&
      this.#sourcePosition < markers.repeatOutFrame
    ) {
      const progress = (this.#sourcePosition - seamStart) / seamFrames;
      const angle = progress * Math.PI / 2;
      const loopPosition =
        markers.repeatInFrame + (this.#sourcePosition - seamStart);
      return this.#sampleAt(this.#sourcePosition) * Math.cos(angle) +
        this.#sampleAt(loopPosition) * Math.sin(angle);
    }
    return this.#sampleAt(this.#sourcePosition);
  }

  #advanceSource(rate, allowLoop) {
    const { markers } = this.#package;
    this.#sourcePosition += rate;
    const seamFrames = Math.max(
      0,
      Math.min(
        markers.seamCrossfadeFrames,
        markers.repeatOutFrame - markers.repeatInFrame - 1,
      ),
    );
    const resumeFrame = markers.repeatInFrame + seamFrames;
    const loopLength = markers.repeatOutFrame - resumeFrame;
    if (
      allowLoop &&
      this.#sourcePosition >= markers.repeatOutFrame &&
      loopLength > 0
    ) {
      this.#sourcePosition = resumeFrame +
        (this.#sourcePosition - markers.repeatOutFrame) % loopLength;
    } else if (
      !allowLoop &&
      this.#sourcePosition >= this.#package.samples.length
    ) {
      this.#sourcePosition = this.#package.samples.length;
    }
  }

  #renderFrame(frame, sourceSample, bakedSample) {
    const settings = this.#package.settings;
    if (!this.#enabled) {
      this.#currentStarterGain = 0;
      this.#currentEngineGain = 1;
      this.#audiblyActive = false;
      this.#currentUnavailableEnvelopeRunFrames = 0;
      return [sourceSample, bakedSample];
    }

    const starterCommanded = this.#state.starter;
    const priorHandoffComplete =
      this.#sessionActive &&
      this.#handoffEndFrame >= 0 &&
      frame >= this.#handoffEndFrame;
    if (
      starterCommanded &&
      !this.#lastStarterCommanded &&
      (!this.#sessionActive || priorHandoffComplete)
    ) {
      this.#beginSession(frame);
    }
    if (
      this.#sessionActive &&
      this.#catchFrame < 0 &&
      this.#state.combustionDetected &&
      Math.abs(this.#state.rpm) >= settings.catchRpm
    ) {
      this.#setCatch(frame, false);
    } else if (
      this.#sessionActive &&
      this.#catchFrame < 0 &&
      this.#lastStarterCommanded &&
      !starterCommanded
    ) {
      this.#setCatch(frame, true);
    }
    this.#lastStarterCommanded = starterCommanded;
    if (!this.#sessionActive) {
      this.#currentStarterGain = 0;
      this.#currentEngineGain = 1;
      this.#audiblyActive = false;
      this.#currentUnavailableEnvelopeRunFrames = 0;
      return [sourceSample, bakedSample];
    }

    const smoothingFrames = Math.max(
      0,
      settings.rpmSmoothingMilliseconds * SAMPLE_RATE / 1_000,
    );
    const smoothingAlpha = smoothingFrames > 0
      ? 1 - Math.exp(-1 / smoothingFrames)
      : 1;
    const targetRpm = Math.max(0, Math.abs(this.#state.rpm));
    this.#smoothedRpm += (targetRpm - this.#smoothedRpm) * smoothingAlpha;
    this.#peakRpm = Math.max(this.#peakRpm, this.#smoothedRpm);
    const speedSpan = settings.speedUpEndRpm - settings.speedUpStartRpm;
    const linearProgress = speedSpan > 0
      ? clamp(
        (this.#peakRpm - settings.speedUpStartRpm) / speedSpan,
        0,
        1,
      )
      : this.#peakRpm >= settings.speedUpEndRpm ? 1 : 0;
    const shapedProgress = linearProgress ** settings.speedUpCurve;
    this.#currentPlaybackRate = settings.basePlaybackRate +
      (settings.catchPlaybackRate - settings.basePlaybackRate) * shapedProgress;

    let starterGain = settings.sourceGain;
    let engineGain = settings.preCatchEngineGain;
    const attackFrames = Math.max(
      0,
      Math.round(settings.attackMilliseconds * SAMPLE_RATE / 1_000),
    );
    if (attackFrames > 0 && frame < this.#sessionStartFrame + attackFrames) {
      const progress = (frame - this.#sessionStartFrame) / attackFrames;
      const attack = Math.sin(clamp(progress, 0, 1) * Math.PI / 2);
      starterGain *= attack;
      engineGain = 1 + (engineGain - 1) * attack;
    }
    const duckFrames = Math.max(
      0,
      Math.round(
        settings.ignitionDuckLeadMilliseconds * SAMPLE_RATE / 1_000,
      ),
    );
    const duckStart = this.#handoffStartFrame - duckFrames;
    if (
      this.#handoffStartFrame >= 0 &&
      duckFrames > 0 &&
      frame >= duckStart &&
      frame < this.#handoffStartFrame
    ) {
      const progress = (frame - duckStart) / duckFrames;
      const eased = 0.5 - 0.5 * Math.cos(clamp(progress, 0, 1) * Math.PI);
      starterGain *=
        1 + (settings.catchStarterGain - 1) * eased;
      engineGain = settings.preCatchEngineGain +
        (settings.engineCatchGain - settings.preCatchEngineGain) * eased;
    } else if (
      this.#handoffStartFrame >= 0 &&
      frame >= this.#handoffStartFrame
    ) {
      if (
        this.#handoffEndFrame <= this.#handoffStartFrame ||
        frame >= this.#handoffEndFrame
      ) {
        starterGain = 0;
        engineGain = 1;
      } else {
        const progress = (frame - this.#handoffStartFrame) /
          (this.#handoffEndFrame - this.#handoffStartFrame);
        const tail = 0.5 + 0.5 * Math.cos(clamp(progress, 0, 1) * Math.PI);
        starterGain *= settings.catchStarterGain * tail;
        engineGain = 1 + (settings.engineCatchGain - 1) * tail;
      }
    }

    const sourceAvailable =
      this.#sourcePosition < this.#package.samples.length;
    const recorded = starterGain !== 0 ? this.#starterSample() : 0;
    this.#currentStarterGain = starterGain;
    this.#currentEngineGain = engineGain;
    this.#audiblyActive = starterGain !== 0 && sourceAvailable;
    if (starterGain !== 0) {
      if (this.#sourcePosition >= this.#package.markers.repeatOutFrame) {
        ++this.#postRepeatTailEnvelopeFrames;
      }
      if (sourceAvailable) {
        ++this.#renderedFrames;
        this.#currentUnavailableEnvelopeRunFrames = 0;
      } else {
        ++this.#unavailableEnvelopeFrames;
        ++this.#currentUnavailableEnvelopeRunFrames;
        this.#longestUnavailableEnvelopeRunFrames = Math.max(
          this.#longestUnavailableEnvelopeRunFrames,
          this.#currentUnavailableEnvelopeRunFrames,
        );
      }
      this.#advanceSource(
        this.#currentPlaybackRate,
        this.#handoffEndFrame < 0 || frame < this.#handoffEndFrame,
      );
    } else {
      this.#currentUnavailableEnvelopeRunFrames = 0;
    }
    return [
      sourceSample * engineGain + recorded * starterGain,
      bakedSample * engineGain + recorded * starterGain,
    ];
  }

  mixPair(sourceBlockValue, bakedBlockValue) {
    const sourceBlock = validateMonoBlock(sourceBlockValue, "sourceBlock");
    const bakedBlock = validateMonoBlock(bakedBlockValue, "bakedBlock");
    if (sourceBlock.length !== bakedBlock.length) {
      throw new RangeError(
        "shared starter source and baked blocks must have equal frame counts",
      );
    }
    if (this.#outputFrame === null) {
      this.#outputFrame = this.#scheduledStates[0]?.frame ?? 0;
    }
    if (this.#outputFrame > Number.MAX_SAFE_INTEGER - sourceBlock.length) {
      throw new RangeError("shared starter output frame overflowed");
    }
    const sourceOutput = new Float32Array(sourceBlock.length);
    const bakedOutput = new Float32Array(bakedBlock.length);
    for (let index = 0; index < sourceBlock.length; ++index) {
      const frame = this.#outputFrame + index;
      this.#applyScheduledStates(frame);
      const [source, baked] = this.#renderFrame(
        frame,
        sourceBlock[index],
        bakedBlock[index],
      );
      if (!Number.isFinite(source) || !Number.isFinite(baked)) {
        throw new RangeError(
          `shared starter produced a non-finite pair at output frame ${frame}`,
        );
      }
      sourceOutput[index] = source;
      bakedOutput[index] = baked;
    }
    this.#outputFrame += sourceBlock.length;
    return Object.freeze({ sourceBlock: sourceOutput, bakedBlock: bakedOutput });
  }

  diagnostics() {
    return Object.freeze({
      schema: SCHEMA,
      id: EXPECTED_ID,
      configured: true,
      loaded: true,
      auditionOnly: true,
      redistributionAuthorized: false,
      licenseWarning: RIGHTS_WARNING,
      sourceSha256: this.#package.sourceSha256,
      payloadSha256: this.#package.payloadSha256,
      sampleRate: SAMPLE_RATE,
      enabled: this.#enabled,
      active: this.#audiblyActive,
      generation: this.#generation,
      outputFrame: this.#outputFrame,
      queuedStateCount: this.#scheduledStates.length,
      state: Object.freeze({ ...this.#state }),
      sessionCount: this.#sessionCount,
      catchCount: this.#catchCount,
      fallbackCatchCount: this.#fallbackCatchCount,
      sessionStartFrame: this.#sessionStartFrame,
      catchFrame: this.#catchFrame,
      physicalCatchFrame: this.#physicalCatchFrame,
      audibleCatchFrame: this.#audibleCatchFrame,
      audioLatencyFrames: this.#audioLatencyFrames,
      handoffStartFrame: this.#handoffStartFrame,
      handoffEndFrame: this.#handoffEndFrame,
      sourceFrame: Math.floor(this.#sourcePosition),
      sourcePosition: this.#sourcePosition,
      playbackRate: this.#currentPlaybackRate,
      starterGain: this.#currentStarterGain,
      engineGain: this.#currentEngineGain,
      peakRpm: this.#peakRpm,
      renderedFrames: this.#renderedFrames,
      postRepeatTailEnvelopeFrames: this.#postRepeatTailEnvelopeFrames,
      unavailableEnvelopeFrames: this.#unavailableEnvelopeFrames,
      longestUnavailableEnvelopeRunFrames:
        this.#longestUnavailableEnvelopeRunFrames,
    });
  }
}
