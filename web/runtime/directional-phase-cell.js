const SCHEMA = "crankwave/responsive-audio-directional-route";

export class DirectionalPhaseCellError extends Error {
  constructor(code, message, { recoverable = false } = {}) {
    super(message);
    this.name = "DirectionalPhaseCellError";
    this.code = code;
    this.recoverable = recoverable;
  }
}

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

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("phase-cell manifest URL must be nonempty");
  }
  return new URL(value, globalThis.location?.href ?? "http://localhost/");
}

function hex(bytes) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

async function fetchBytes(url, fetchImplementation, label) {
  const response = await fetchImplementation(url.href, { cache: "no-store" });
  if (!response?.ok || typeof response.arrayBuffer !== "function") {
    throw new Error(`${label} fetch failed: HTTP ${response?.status ?? "?"} for ${url.href}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

function decodeFloat32Le(bytes, expectedSamples, label) {
  if (bytes.byteLength !== expectedSamples * 4) {
    throw new RangeError(
      `${label} has ${bytes.byteLength} bytes; expected ${expectedSamples * 4}`,
    );
  }
  const result = new Float32Array(expectedSamples);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let index = 0; index < expectedSamples; ++index) {
    const sample = view.getFloat32(index * 4, true);
    if (!Number.isFinite(sample)) {
      throw new RangeError(`${label} contains a non-finite sample at ${index}`);
    }
    result[index] = sample;
  }
  return result;
}

async function loadCell(manifestUrl, descriptor, manifest, fetchImplementation, cryptoImplementation) {
  object(descriptor, "cell");
  const id = string(descriptor.id, "cell.id");
  const rpm = finite(descriptor.rpm, `${id}.rpm`);
  const manifoldPressurePaAbs = finite(
    descriptor.manifold_pressure_pa_abs,
    `${id}.manifold_pressure_pa_abs`,
  );
  if (rpm <= 0 || manifoldPressurePaAbs <= 0) {
    fail(`${id} RPM and manifold pressure must be positive`);
  }
  const lane = string(descriptor.lane, `${id}.lane`);
  const throttle01 = finite(descriptor.capture_throttle_01, `${id}.capture_throttle_01`);
  const sourceCycleBeginRevolutions = finite(
    descriptor.source_cycle_begin_revolutions,
    `${id}.source_cycle_begin_revolutions`,
  );
  const sourceCycleOrdinal = sourceCycleBeginRevolutions / 2;
  if (
    sourceCycleBeginRevolutions < 0 ||
    Math.abs(sourceCycleOrdinal - Math.round(sourceCycleOrdinal)) > 1e-9
  ) {
    fail(`${id}.source_cycle_begin_revolutions must be on a 720-degree boundary`);
  }
  const relativePath = string(descriptor.relative_path, `${id}.relative_path`);
  if (relativePath.startsWith("/") || relativePath.includes("..") || relativePath.includes("\\")) {
    fail(`${id}.relative_path must stay beneath the manifest`);
  }
  const url = new URL(relativePath, manifestUrl);
  const bytes = await fetchBytes(url, fetchImplementation, id);
  if (bytes.byteLength !== descriptor.byte_count) {
    throw new RangeError(`${id} byte count does not match its manifest`);
  }
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error("Web Crypto SHA-256 is required for the phase-cell experiment");
  }
  const digest = hex(new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", bytes)));
  if (digest !== descriptor.payload_sha256) {
    throw new Error(`${id} SHA-256 does not match its manifest`);
  }
  const sampleCount = manifest.phase.samples_per_cycle * manifest.phase.cycle_count;
  return Object.freeze({
    id,
    rpm,
    lane,
    throttle01,
    manifoldPressurePaAbs,
    sourceCycleBeginRevolutions,
    pcm: decodeFloat32Le(bytes, sampleCount, id),
  });
}

export async function loadDirectionalPhaseCell(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") fail("phase-cell loading requires fetch");
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifestBytes = await fetchBytes(manifestUrl, fetchImplementation, "atlas.json");
  const manifest = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes));
  object(manifest, "manifest");
  if (manifest.schema !== SCHEMA) fail(`unsupported phase-cell schema ${manifest.schema}`);
  string(manifest.id, "manifest.id");
  string(manifest.engine, "manifest.engine");
  object(manifest.audio, "manifest.audio");
  const sampleRate = positiveInteger(manifest.audio.sample_rate_hz, "audio.sample_rate_hz");
  if (manifest.audio.encoding !== "float32le" || manifest.audio.channel_layout !== "mono") {
    fail("phase-cell delivery audio must be mono float32le");
  }
  string(manifest.audio.bus_id, "audio.bus_id");
  object(manifest.phase, "manifest.phase");
  const samplesPerCycle = positiveInteger(
    manifest.phase.samples_per_cycle,
    "phase.samples_per_cycle",
  );
  const cycleCount = positiveInteger(manifest.phase.cycle_count, "phase.cycle_count");
  if (manifest.phase.cycle_revolutions !== 2) fail("phase cells must represent 720-degree cycles");
  object(manifest.domain, "manifest.domain");
  const loadCoordinate = string(
    manifest.domain.load_coordinate,
    "domain.load_coordinate",
  );
  if (
    loadCoordinate !== "measured-intake-manifold-pressure-pa-abs" &&
    loadCoordinate !== "requested-throttle-01"
  ) {
    fail(`unsupported phase-cell load coordinate ${loadCoordinate}`);
  }
  const minimumRpm = finite(manifest.domain.minimum_rpm, "domain.minimum_rpm");
  const maximumRpm = finite(manifest.domain.maximum_rpm, "domain.maximum_rpm");
  if (!(minimumRpm > 0 && maximumRpm > minimumRpm)) fail("phase-cell RPM domain is invalid");
  if (!Array.isArray(manifest.cells) || manifest.cells.length < 4) {
    fail("phase-cell experiment requires a nontrivial cell grid");
  }
  const cells = await Promise.all(
    manifest.cells.map((cell) => loadCell(
      manifestUrl,
      cell,
      manifest,
      fetchImplementation,
      cryptoImplementation,
    )),
  );
  const rpmAnchors = [...new Set(cells.map((cell) => cell.rpm))].sort((a, b) => a - b);
  const cellsByRpm = new Map();
  const cellLoadCoordinate = (cell) => loadCoordinate === "requested-throttle-01"
    ? cell.throttle01
    : cell.manifoldPressurePaAbs;
  for (const rpm of rpmAnchors) {
    const row = cells.filter((cell) => cell.rpm === rpm).sort(
      (a, b) => cellLoadCoordinate(a) - cellLoadCoordinate(b),
    );
    if (row.length < 1) fail(`${rpm} RPM needs at least one physical-load cell`);
    for (let index = 1; index < row.length; ++index) {
      if (!(cellLoadCoordinate(row[index]) > cellLoadCoordinate(row[index - 1]))) {
        fail(`${rpm} RPM load cells are not strictly ordered by ${loadCoordinate}`);
      }
    }
    cellsByRpm.set(rpm, Object.freeze(row));
  }
  object(manifest.provenance, "manifest.provenance");
  object(manifest.provenance.engine, "provenance.engine");
  object(manifest.provenance.renderer_build, "provenance.renderer_build");
  Object.freeze(cells);
  Object.freeze(rpmAnchors);
  return Object.freeze({
    kind: "responsive-audio-directional-route",
    manifestUrl: manifestUrl.href,
    manifest,
    sampleRate,
    busIds: Object.freeze([manifest.audio.bus_id]),
    samplesPerCycle,
    cycleCount,
    minimumRpm,
    maximumRpm,
    loadCoordinate,
    cellLoadCoordinate,
    rpmAnchors,
    cells,
    cellsAtRpm(rpm) {
      return cellsByRpm.get(rpm) ?? null;
    },
  });
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function positiveModulo(value, modulus) {
  return ((value % modulus) + modulus) % modulus;
}

function rpmBracket(anchors, rpm) {
  if (rpm <= anchors[0]) return { left: anchors[0], right: anchors[0], amount: 0 };
  if (rpm >= anchors.at(-1)) {
    return { left: anchors.at(-1), right: anchors.at(-1), amount: 0 };
  }
  let rightIndex = 1;
  while (anchors[rightIndex] < rpm) ++rightIndex;
  const left = anchors[rightIndex - 1];
  const right = anchors[rightIndex];
  return { left, right, amount: (rpm - left) / (right - left) };
}

function sampleCell(cell, package_, crankRevolutions) {
  // A multi-cycle payload starts at a 720-degree boundary, but that alone does
  // not say which retained cycle is slot zero. Keep the captured absolute cycle
  // origin so neighbouring RPM cells do not acquire arbitrary 3-cycle rotations.
  const cyclePosition = positiveModulo(
    (crankRevolutions - cell.sourceCycleBeginRevolutions) / 2,
    package_.cycleCount,
  );
  const position = cyclePosition * package_.samplesPerCycle;
  const leftIndex = Math.floor(position) % cell.pcm.length;
  const rightIndex = (leftIndex + 1) % cell.pcm.length;
  return interpolate(cell.pcm[leftIndex], cell.pcm[rightIndex], position - Math.floor(position));
}

function sampleLoadRow(package_, rpm, loadCoordinate, crankRevolutions) {
  const row = package_.cellsAtRpm(rpm);
  const cellCoordinate = package_.cellLoadCoordinate;
  if (loadCoordinate <= cellCoordinate(row[0])) {
    return sampleCell(row[0], package_, crankRevolutions);
  }
  if (loadCoordinate >= cellCoordinate(row.at(-1))) {
    return sampleCell(row.at(-1), package_, crankRevolutions);
  }
  let rightIndex = 1;
  while (cellCoordinate(row[rightIndex]) < loadCoordinate) ++rightIndex;
  const left = row[rightIndex - 1];
  const right = row[rightIndex];
  const amount = (loadCoordinate - cellCoordinate(left)) /
    (cellCoordinate(right) - cellCoordinate(left));
  return interpolate(
    sampleCell(left, package_, crankRevolutions),
    sampleCell(right, package_, crankRevolutions),
    amount,
  );
}

function validEndpoint(value, requiredStateMask, forbiddenStateMask) {
  return value !== null &&
    Number.isFinite(value.rpm) &&
    Number.isFinite(value.manifoldPressurePaAbs) &&
    Number.isFinite(value.requestedThrottle01) &&
    Number.isFinite(value.unwrappedCrankRevolutions) &&
    value.rpm > 0 &&
    value.manifoldPressurePaAbs > 0 &&
    Number.isSafeInteger(value.stateMask) &&
    (value.stateMask & requiredStateMask) === requiredStateMask &&
    (value.stateMask & forbiddenStateMask) === 0;
}

export class DirectionalPhaseCellCursor {
  #package;
  #requiredStateMask;
  #forbiddenStateMask;
  #active = false;

  constructor(
    package_,
    { requiredStateMask = 0x3, forbiddenStateMask = 0x10 } = {},
  ) {
    if (package_?.kind !== "responsive-audio-directional-route") {
      fail("DirectionalPhaseCellCursor requires a loaded directional route");
    }
    this.#package = package_;
    if (
      !Number.isSafeInteger(requiredStateMask) ||
      !Number.isSafeInteger(forbiddenStateMask) ||
      requiredStateMask < 0 ||
      forbiddenStateMask < 0 ||
      (requiredStateMask & forbiddenStateMask) !== 0
    ) {
      fail("phase-cell state masks must be disjoint nonnegative integers");
    }
    this.#requiredStateMask = requiredStateMask;
    this.#forbiddenStateMask = forbiddenStateMask;
  }

  get activeSegmentId() {
    return this.#active ? "phase-cells-physical-map" : null;
  }

  get busIds() {
    return this.#package.busIds;
  }

  reset() {
    this.#active = false;
  }

  initialize(endpoint) {
    this.#active = this.#covers(endpoint);
    return Object.freeze({ segmentId: this.activeSegmentId });
  }

  createOutputBuffers(capacity) {
    if (!Number.isSafeInteger(capacity) || capacity <= 0) {
      throw new RangeError("phase-cell output capacity must be positive");
    }
    return [new Float32Array(capacity)];
  }

  #covers(endpoint) {
    return validEndpoint(
      endpoint,
      this.#requiredStateMask,
      this.#forbiddenStateMask,
    ) &&
      endpoint.rpm >= this.#package.minimumRpm &&
      endpoint.rpm <= this.#package.maximumRpm;
  }

  renderBlockInto({ frameCount, start, end, outputBuffers }) {
    if (!this.#active || !this.#covers(start) || !this.#covers(end)) {
      throw new DirectionalPhaseCellError(
        "responsive-audio-directional-route-outside-coverage",
        `phase-cell B covers ${this.#package.minimumRpm}..${this.#package.maximumRpm} RPM with ignition and fuel on`,
        { recoverable: true },
      );
    }
    const output = outputBuffers?.[0];
    if (!(output instanceof Float32Array) || output.length < frameCount) {
      throw new RangeError("phase-cell output buffer is too small");
    }
    for (let frame = 0; frame < frameCount; ++frame) {
      const amount = (frame + 1) / frameCount;
      const rpm = interpolate(start.rpm, end.rpm, amount);
      const map = interpolate(
        start.manifoldPressurePaAbs,
        end.manifoldPressurePaAbs,
        amount,
      );
      const throttle = interpolate(
        start.requestedThrottle01,
        end.requestedThrottle01,
        amount,
      );
      const loadCoordinate = this.#package.loadCoordinate === "requested-throttle-01"
        ? throttle
        : map;
      const crank = interpolate(
        start.unwrappedCrankRevolutions,
        end.unwrappedCrankRevolutions,
        amount,
      );
      const bracket = rpmBracket(this.#package.rpmAnchors, rpm);
      const left = sampleLoadRow(
        this.#package,
        bracket.left,
        loadCoordinate,
        crank,
      );
      output[frame] = bracket.left === bracket.right
        ? left
        : interpolate(
            left,
            sampleLoadRow(
              this.#package,
              bracket.right,
              loadCoordinate,
              crank,
            ),
            bracket.amount,
          );
    }
  }
}
