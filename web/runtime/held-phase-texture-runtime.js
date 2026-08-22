const ROOT_SCHEMA =
  "crankwave/responsive-audio-held-texture";
const ROUTE_SCHEMA =
  "crankwave/responsive-audio-held-route";
const PACKAGE_KIND = "held-phase-texture-package";
const SELECTION_ALGORITHM = "splitmix64-shuffled-bags-v1";
const ALIGNMENT_METHOD =
  "shared-route-sum-circular-correlation-unwrapped-grid-v1";
const MEAN_INTERPOLATION_METHOD = "common-delay-phase-warp";
const MEAN_ENERGY_TARGET = "linear-anchor-rms";
const RESIDUAL_CORRELATION_MODEL = "independent";
const RESIDUAL_ENERGY_TARGET = "linear-anchor-power";
const REQUIRED_RUNNING_STATE_MASK = 0x3;
const LIMITER_CUT_STATE_MASK = 0x10;
const UINT64_MASK = (1n << 64n) - 1n;
const SPLITMIX_INCREMENT = 0x9e3779b97f4a7c15n;
const SPLITMIX_MULTIPLIER_1 = 0xbf58476d1ce4e5b9n;
const SPLITMIX_MULTIPLIER_2 = 0x94d049bb133111ebn;

export class HeldPhaseTextureRuntimeError extends Error {
  constructor(code, message, { recoverable = false } = {}) {
    super(message);
    this.name = "HeldPhaseTextureRuntimeError";
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

function array(value, label) {
  if (!Array.isArray(value)) fail(`${label} must be an array`);
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

function positive(value, label) {
  const result = finite(value, label);
  if (!(result > 0)) fail(`${label} must be positive`);
  return result;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function exactKeys(value, expected, label) {
  object(value, label);
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (
    actual.length !== wanted.length ||
    actual.some((key, index) => key !== wanted[index])
  ) {
    fail(`${label} has unknown or missing fields`);
  }
  return value;
}

function loadLanes(value, label = "domain.load_lanes") {
  const lanes = array(value, label).map((entry, index) => {
    object(entry, `${label}[${index}]`);
    const id = string(entry.id, `${label}[${index}].id`);
    const throttle01 = finite(
      entry.throttle01,
      `${label}[${index}].throttle01`,
    );
    if (throttle01 < 0 || throttle01 > 1) {
      fail(`${label}[${index}].throttle01 must be within [0, 1]`);
    }
    return Object.freeze({ id, throttle01 });
  });
  if (lanes.length === 0 || new Set(lanes.map(({ id }) => id)).size !== lanes.length) {
    fail(`${label} must contain unique declared lanes`);
  }
  for (let index = 1; index < lanes.length; ++index) {
    if (!(lanes[index].throttle01 > lanes[index - 1].throttle01)) {
      fail(`${label} throttle coordinates must be strictly increasing`);
    }
  }
  return Object.freeze(lanes);
}

function cellLoadAliases(descriptor, lane, throttle01, declaredLoadLanes, label) {
  const declaredByLane = new Map(
    declaredLoadLanes.map((entry, index) => [entry.id, { ...entry, index }]),
  );
  const retained = declaredByLane.get(lane);
  if (retained === undefined || retained.throttle01 !== throttle01) {
    fail(`${label} retained lane/throttle disagrees with domain.load_lanes`);
  }
  if (descriptor.capture_provenance === undefined) {
    return Object.freeze([Object.freeze({ lane, throttle01 })]);
  }
  const provenance = exactKeys(
    descriptor.capture_provenance,
    ["coalesced_authored_lanes", "coalesced_capture_throttles_01"],
    `${label}.capture_provenance`,
  );
  const laneValues = array(
    provenance.coalesced_authored_lanes,
    `${label}.capture_provenance.coalesced_authored_lanes`,
  );
  const throttleValues = array(
    provenance.coalesced_capture_throttles_01,
    `${label}.capture_provenance.coalesced_capture_throttles_01`,
  );
  if (laneValues.length === 0 || laneValues.length !== throttleValues.length) {
    fail(`${label} coalesced lane/throttle arrays must be nonempty and paired`);
  }
  const aliases = laneValues.map((value, index) => {
    const aliasLane = string(
      value,
      `${label}.capture_provenance.coalesced_authored_lanes[${index}]`,
    );
    const aliasThrottle = finite(
      throttleValues[index],
      `${label}.capture_provenance.coalesced_capture_throttles_01[${index}]`,
    );
    const declared = declaredByLane.get(aliasLane);
    if (declared === undefined || declared.throttle01 !== aliasThrottle) {
      fail(`${label} coalesced alias ${aliasLane} disagrees with domain.load_lanes`);
    }
    return Object.freeze({
      lane: aliasLane,
      throttle01: aliasThrottle,
      declaredIndex: declared.index,
    });
  });
  if (
    aliases[0].lane !== lane ||
    aliases[0].throttle01 !== throttle01 ||
    new Set(aliases.map(({ lane: aliasLane }) => aliasLane)).size !== aliases.length
  ) {
    fail(`${label} coalesced aliases must begin with the retained unique lane`);
  }
  for (let index = 1; index < aliases.length; ++index) {
    if (!(aliases[index].declaredIndex > aliases[index - 1].declaredIndex)) {
      fail(`${label} coalesced aliases must follow declared load-lane order`);
    }
  }
  return Object.freeze(
    aliases.map(({ lane: aliasLane, throttle01: aliasThrottle }) =>
      Object.freeze({ lane: aliasLane, throttle01: aliasThrottle })
    ),
  );
}

function aliasesAgree(left, right) {
  return (
    left.length === right.length &&
    left.every(
      (entry, index) =>
        entry.lane === right[index].lane &&
        entry.throttle01 === right[index].throttle01,
    )
  );
}

function cellsByDeclaredLane(row, declaredLoadLanes) {
  const declaredByLane = new Map(
    declaredLoadLanes.map((entry) => [entry.id, entry.throttle01]),
  );
  const result = new Map();
  for (const cell of row.cells) {
    const aliases = Array.isArray(cell.loadAliases)
      ? cell.loadAliases
      : [{ lane: cell.lane, throttle01: declaredByLane.get(cell.lane) }];
    for (const alias of aliases) {
      if (
        declaredByLane.get(alias.lane) !== alias.throttle01 ||
        result.has(alias.lane)
      ) {
        throw new RangeError(
          `${row.rpm} RPM has duplicate or undeclared held load aliases`,
        );
      }
      result.set(alias.lane, cell);
    }
  }
  if (
    result.size !== declaredLoadLanes.length ||
    declaredLoadLanes.some(({ id }) => !result.has(id))
  ) {
    throw new RangeError(
      `${row.rpm} RPM does not implement the complete held load-lane aliases`,
    );
  }
  return result;
}

function collapseAliasCoincidentCurves(curves, rpm) {
  const result = [];
  for (const curve of curves) {
    const previous = result.at(-1);
    if (
      previous === undefined ||
      previous.manifoldPressurePaAbs !== curve.manifoldPressurePaAbs
    ) {
      result.push(curve);
      continue;
    }
    if (
      previous.leftCell !== curve.leftCell ||
      previous.rightCell !== curve.rightCell
    ) {
      throw new RangeError(`held load-lane curves coincide at ${rpm} RPM`);
    }
    previous.lanes.push(...curve.lanes);
  }
  return result;
}

function combineCellWeights(entries) {
  const result = [];
  const byCell = new Map();
  for (const entry of entries) {
    if (!(entry.weight > 0)) continue;
    const existing = byCell.get(entry.cell);
    if (existing === undefined) {
      const retained = { cell: entry.cell, weight: entry.weight };
      byCell.set(entry.cell, retained);
      result.push(retained);
    } else {
      existing.weight += entry.weight;
    }
  }
  return result;
}

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("held phase texture package URL must be nonempty");
  }
  return new URL(value, globalThis.location?.href ?? "http://localhost/");
}

function relativeUrl(manifestUrl, path, label) {
  const relativePath = string(path, label);
  if (
    relativePath.startsWith("/") ||
    relativePath.includes("..") ||
    relativePath.includes("\\")
  ) {
    fail(`${label} must stay beneath its manifest`);
  }
  return new URL(relativePath, manifestUrl);
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
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

async function sha256Hex(bytes, cryptoImplementation, label) {
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error("Web Crypto SHA-256 is required for held texture loading");
  }
  return hex(
    new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", bytes)),
  );
}

async function requireSha256(bytes, expected, cryptoImplementation, label) {
  string(expected, `${label}.sha256`);
  const digest = await sha256Hex(bytes, cryptoImplementation, label);
  if (digest !== expected) {
    throw new Error(`${label} SHA-256 does not match its manifest`);
  }
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

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function interpolate(left, right, amount) {
  return left + (right - left) * amount;
}

function positiveModulo(value, modulus) {
  return ((value % modulus) + modulus) % modulus;
}

function stateId(rpm, lane) {
  return `${rpm}rpm-${lane}`;
}

function sampleCyclicCubic(samples, position) {
  const length = samples.length;
  const wrapped = positiveModulo(position, length);
  const index1 = Math.floor(wrapped);
  const amount = wrapped - index1;
  const index0 = (index1 + length - 1) % length;
  const index2 = (index1 + 1) % length;
  const index3 = (index1 + 2) % length;
  const value0 = samples[index0];
  const value1 = samples[index1];
  const value2 = samples[index2];
  const value3 = samples[index3];
  const coefficient0 = value1;
  const coefficient1 = 0.5 * (value2 - value0);
  const coefficient2 =
    value0 - 2.5 * value1 + 2 * value2 - 0.5 * value3;
  const coefficient3 =
    0.5 * (value3 - value0) + 1.5 * (value1 - value2);
  return (
    ((coefficient3 * amount + coefficient2) * amount + coefficient1) * amount +
    coefficient0
  );
}

function sampleCyclicCubicSegment(samples, offset, length, position) {
  const wrapped = positiveModulo(position, length);
  const index1 = Math.floor(wrapped);
  const amount = wrapped - index1;
  const index0 = (index1 + length - 1) % length;
  const index2 = (index1 + 1) % length;
  const index3 = (index1 + 2) % length;
  const value0 = samples[offset + index0];
  const value1 = samples[offset + index1];
  const value2 = samples[offset + index2];
  const value3 = samples[offset + index3];
  const coefficient0 = value1;
  const coefficient1 = 0.5 * (value2 - value0);
  const coefficient2 =
    value0 - 2.5 * value1 + 2 * value2 - 0.5 * value3;
  const coefficient3 =
    0.5 * (value3 - value0) + 1.5 * (value1 - value2);
  return (
    ((coefficient3 * amount + coefficient2) * amount + coefficient1) * amount +
    coefficient0
  );
}

function signalPower(samples) {
  let squareSum = 0;
  for (const sample of samples) squareSum += sample * sample;
  return squareSum / samples.length;
}

function combinedSignalPower(routes) {
  if (!Array.isArray(routes) || routes.length === 0) {
    fail("combined signal power requires at least one route");
  }
  const sampleCount = routes[0].length;
  if (routes.some((route) => route.length !== sampleCount)) {
    fail("route payloads differ in length");
  }
  let squareSum = 0;
  for (let index = 0; index < sampleCount; ++index) {
    let sample = 0;
    for (const route of routes) sample += route[index];
    squareSum += sample * sample;
  }
  return squareSum / sampleCount;
}

function parseUint64(value, label) {
  let result;
  try {
    if (typeof value === "bigint") result = value;
    else if (typeof value === "string" && /^[0-9]+$/.test(value)) {
      result = BigInt(value);
    } else if (Number.isSafeInteger(value) && value >= 0) {
      result = BigInt(value);
    } else {
      fail(`${label} must be an unsigned 64-bit integer string`);
    }
  } catch (error) {
    if (error instanceof TypeError) throw error;
    fail(`${label} must be an unsigned 64-bit integer string`);
  }
  if (result < 0n || result > UINT64_MASK) {
    fail(`${label} exceeds unsigned 64-bit range`);
  }
  return result;
}

function splitmix64(value) {
  let result = (value + SPLITMIX_INCREMENT) & UINT64_MASK;
  result =
    ((result ^ (result >> 30n)) * SPLITMIX_MULTIPLIER_1) & UINT64_MASK;
  result =
    ((result ^ (result >> 27n)) * SPLITMIX_MULTIPLIER_2) & UINT64_MASK;
  return (result ^ (result >> 31n)) & UINT64_MASK;
}

function fnv1a64(value) {
  let hash = 0xcbf29ce484222325n;
  const bytes = new TextEncoder().encode(value);
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * 0x100000001b3n) & UINT64_MASK;
  }
  return hash;
}

function floorDivBigInt(value, divisor) {
  let quotient = value / divisor;
  const remainder = value % divisor;
  if (remainder < 0n) quotient -= 1n;
  return quotient;
}

function floorModuloBigInt(value, divisor) {
  const result = value % divisor;
  return result < 0n ? result + divisor : result;
}

async function loadRouteManifest(
  rootManifestUrl,
  relativePath,
  declaredLoadLanes,
  fetchImplementation,
  cryptoImplementation,
) {
  const manifestUrl = relativeUrl(rootManifestUrl, relativePath, "root.routes entry");
  const manifestBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "held texture route manifest",
  );
  const manifest = JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes),
  );
  object(manifest, "route manifest");
  if (manifest.schema !== ROUTE_SCHEMA) {
    fail(`unsupported held texture route schema ${manifest.schema}`);
  }
  string(manifest.id, "route manifest.id");
  string(manifest.engine, "route manifest.engine");
  object(manifest.audio, "route manifest.audio");
  const busId = string(manifest.audio.bus_id, "route audio.bus_id");
  const sampleRate = positiveInteger(
    manifest.audio.sample_rate_hz,
    "route audio.sample_rate_hz",
  );
  if (
    manifest.audio.encoding !== "float32le" ||
    manifest.audio.channel_layout !== "mono"
  ) {
    fail("held texture route payloads must be mono float32le");
  }
  object(manifest.phase, "route manifest.phase");
  const samplesPerCycle = positiveInteger(
    manifest.phase.samples_per_cycle,
    "route phase.samples_per_cycle",
  );
  const residualCycleCount = positiveInteger(
    manifest.phase.residual_cycle_count,
    "route phase.residual_cycle_count",
  );
  if (manifest.phase.cycle_revolutions !== 2) {
    fail("held texture cycles must span exactly two crank revolutions");
  }
  if (manifest.phase.residual_boundary_value !== 0) {
    fail("held texture residual boundaries must be exactly zero");
  }
  object(manifest.phase.residual_taper, "route phase.residual_taper");
  positiveInteger(
    manifest.phase.residual_taper.frames_per_edge,
    "route residual taper frames_per_edge",
  );
  object(manifest.domain, "route manifest.domain");
  if (
    manifest.domain.load_coordinate !==
    "measured-intake-manifold-pressure-pa-abs"
  ) {
    fail("held texture routes require physical manifold-pressure load");
  }
  const descriptors = array(manifest.cells, "route manifest.cells");
  if (descriptors.length === 0) fail("held texture route has no cells");

  const cells = await Promise.all(
    descriptors.map(async (descriptor, descriptorIndex) => {
      object(descriptor, `route cell[${descriptorIndex}]`);
      const id = string(descriptor.id, `route cell[${descriptorIndex}].id`);
      const rpm = positive(descriptor.rpm, `${id}.rpm`);
      const lane = string(descriptor.lane, `${id}.lane`);
      const throttle01 = finite(
        descriptor.capture_throttle_01,
        `${id}.capture_throttle_01`,
      );
      if (throttle01 < 0 || throttle01 > 1) {
        fail(`${id}.capture_throttle_01 must be within [0, 1]`);
      }
      const loadAliases = cellLoadAliases(
        descriptor,
        lane,
        throttle01,
        declaredLoadLanes,
        id,
      );
      const map = positive(
        descriptor.manifold_pressure_pa_abs,
        `${id}.manifold_pressure_pa_abs`,
      );
      const sourceCycleBeginRevolutions = finite(
        descriptor.source_cycle_begin_revolutions,
        `${id}.source_cycle_begin_revolutions`,
      );
      const sourceCycleEndRevolutions = finite(
        descriptor.source_cycle_end_revolutions,
        `${id}.source_cycle_end_revolutions`,
      );
      if (
        sourceCycleEndRevolutions - sourceCycleBeginRevolutions !==
        residualCycleCount * 2
      ) {
        fail(`${id} source cycle bounds do not match the residual bank`);
      }
      object(descriptor.mean, `${id}.mean`);
      object(descriptor.residual_bank, `${id}.residual_bank`);
      if (
        descriptor.mean.sample_count !== samplesPerCycle ||
        descriptor.residual_bank.cycle_count !== residualCycleCount ||
        descriptor.residual_bank.samples_per_cycle !== samplesPerCycle ||
        descriptor.residual_bank.sample_count !==
          samplesPerCycle * residualCycleCount
      ) {
        fail(`${id} payload shape does not match the route phase contract`);
      }
      if (
        descriptor.residual_bank.selection_contract !==
        "change residual ordinal only at a 720-degree boundary"
      ) {
        fail(`${id} has an unsupported residual selection contract`);
      }

      const meanUrl = relativeUrl(
        manifestUrl,
        descriptor.mean.relative_path,
        `${id}.mean.relative_path`,
      );
      const residualUrl = relativeUrl(
        manifestUrl,
        descriptor.residual_bank.relative_path,
        `${id}.residual_bank.relative_path`,
      );
      const [meanBytes, residualBytes] = await Promise.all([
        fetchBytes(meanUrl, fetchImplementation, `${id} mean`),
        fetchBytes(residualUrl, fetchImplementation, `${id} residual bank`),
      ]);
      if (
        meanBytes.byteLength !== descriptor.mean.byte_count ||
        residualBytes.byteLength !== descriptor.residual_bank.byte_count
      ) {
        throw new RangeError(`${id} payload byte count does not match its manifest`);
      }
      await Promise.all([
        requireSha256(
          meanBytes,
          descriptor.mean.payload_sha256,
          cryptoImplementation,
          `${id} mean`,
        ),
        requireSha256(
          residualBytes,
          descriptor.residual_bank.payload_sha256,
          cryptoImplementation,
          `${id} residual bank`,
        ),
      ]);
      const mean = decodeFloat32Le(meanBytes, samplesPerCycle, `${id} mean`);
      const residuals = decodeFloat32Le(
        residualBytes,
        samplesPerCycle * residualCycleCount,
        `${id} residual bank`,
      );
      for (let cycle = 0; cycle < residualCycleCount; ++cycle) {
        const offset = cycle * samplesPerCycle;
        if (
          residuals[offset] !== 0 ||
          residuals[offset + samplesPerCycle - 1] !== 0
        ) {
          fail(`${id} residual cycle ${cycle} is not boundary-zero`);
        }
      }
      return Object.freeze({
        id,
        stateId: stateId(rpm, lane),
        rpm,
        lane,
        throttle01,
        loadAliases,
        manifoldPressurePaAbs: map,
        sourceCycleBeginRevolutions,
        sourceCycleEndRevolutions,
        mean,
        residuals,
        meanPower: signalPower(mean),
        residualPower: signalPower(residuals),
        descriptor,
      });
    }),
  );

  return Object.freeze({
    manifestUrl: manifestUrl.href,
    manifest,
    busId,
    sampleRate,
    samplesPerCycle,
    residualCycleCount,
    cells: Object.freeze(cells),
  });
}

function requireRootContracts(manifest, residualCycleCount) {
  object(manifest.phase_alignment, "root.phase_alignment");
  const alignment = manifest.phase_alignment;
  if (
    alignment.method !== ALIGNMENT_METHOD ||
    alignment.unit !== "phase-samples" ||
    alignment.interpolation !== "unwrapped-linear"
  ) {
    fail("held texture phase alignment contract is unsupported");
  }
  string(alignment.reference_cell_id, "phase_alignment.reference_cell_id");
  array(alignment.cells, "phase_alignment.cells");

  object(manifest.texture_selection, "root.texture_selection");
  const selection = manifest.texture_selection;
  if (
    selection.algorithm !== SELECTION_ALGORITHM ||
    selection.bank_size !== residualCycleCount ||
    selection.no_adjacent_repeat !== true ||
    selection.change_phase !== "720-degree-boundary"
  ) {
    fail("held texture selector contract is unsupported");
  }
  const publicSeed = parseUint64(
    selection.public_seed,
    "texture_selection.public_seed",
  );

  object(manifest.interpolation, "root.interpolation");
  object(manifest.interpolation.mean, "root.interpolation.mean");
  object(manifest.interpolation.residual, "root.interpolation.residual");
  if (
    manifest.interpolation.mean.method !== MEAN_INTERPOLATION_METHOD ||
    manifest.interpolation.mean.energy_target !== MEAN_ENERGY_TARGET ||
    manifest.interpolation.residual.cross_cell_correlation !==
      RESIDUAL_CORRELATION_MODEL ||
    manifest.interpolation.residual.energy_target !== RESIDUAL_ENERGY_TARGET
  ) {
    fail("held texture interpolation contract is unsupported");
  }
  return { alignment, selection, publicSeed };
}

function buildMeanGram(cells, samplesPerCycle) {
  const alignedMeans = cells.map((cell) => {
    const result = new Float64Array(samplesPerCycle);
    for (let phase = 0; phase < samplesPerCycle; ++phase) {
      let sum = 0;
      for (const route of cell.routes) {
        sum += sampleCyclicCubic(
          route.mean,
          phase - cell.shiftToCanonicalSamples,
        );
      }
      result[phase] = sum;
    }
    return result;
  });
  const gram = Array.from(
    { length: cells.length },
    () => new Float64Array(cells.length),
  );
  for (let left = 0; left < cells.length; ++left) {
    for (let right = 0; right <= left; ++right) {
      let productSum = 0;
      for (let phase = 0; phase < samplesPerCycle; ++phase) {
        productSum += alignedMeans[left][phase] * alignedMeans[right][phase];
      }
      const productMean = productSum / samplesPerCycle;
      gram[left][right] = productMean;
      gram[right][left] = productMean;
    }
  }
  return { alignedMeans, gram };
}

export async function loadHeldPhaseTexturePackage(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("held phase texture loading requires fetch");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const rootBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "held texture package manifest",
  );
  const manifestSha256 = await sha256Hex(
    rootBytes,
    cryptoImplementation,
    "held texture package manifest",
  );
  const manifest = JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(rootBytes),
  );
  object(manifest, "root manifest");
  if (manifest.schema !== ROOT_SCHEMA) {
    fail(`unsupported held texture package schema ${manifest.schema}`);
  }
  string(manifest.id, "root manifest.id");
  string(manifest.engine, "root manifest.engine");
  object(manifest.representation, "root manifest.representation");
  if (
    manifest.representation.kind !==
      "cyclic-mean-plus-boundary-zero-cycle-residual-bank" ||
    manifest.representation.timeline_included !== false
  ) {
    fail("held texture package must not contain an authored timeline");
  }
  object(manifest.domain, "root manifest.domain");
  const minimumRpm = positive(manifest.domain.minimum_rpm, "domain.minimum_rpm");
  const maximumRpm = positive(manifest.domain.maximum_rpm, "domain.maximum_rpm");
  if (!(maximumRpm > minimumRpm)) fail("held texture RPM domain is invalid");
  const declaredRpmAnchors = array(
    manifest.domain.rpm_anchors,
    "domain.rpm_anchors",
  ).map((value, index) => positive(value, `domain.rpm_anchors[${index}]`));
  for (let index = 1; index < declaredRpmAnchors.length; ++index) {
    if (!(declaredRpmAnchors[index] > declaredRpmAnchors[index - 1])) {
      fail("held texture RPM anchors must be strictly increasing");
    }
  }
  if (
    declaredRpmAnchors[0] !== minimumRpm ||
    declaredRpmAnchors.at(-1) !== maximumRpm
  ) {
    fail("held texture RPM bounds must equal the outer anchors");
  }
  const declaredLoadLanes = loadLanes(manifest.domain.load_lanes);
  const dryBusIds = array(manifest.dry_bus_ids, "root manifest.dry_bus_ids")
    .map((value, index) => string(value, `dry_bus_ids[${index}]`));
  const routeDescriptors = array(
    manifest.route_manifests,
    "root manifest.route_manifests",
  ).map((value, index) => {
    object(value, `route_manifests[${index}]`);
    return Object.freeze({
      busId: string(value.bus_id, `route_manifests[${index}].bus_id`),
      path: string(
        value.manifest_path,
        `route_manifests[${index}].manifest_path`,
      ),
    });
  });
  if (
    dryBusIds.length === 0 ||
    dryBusIds.length !== routeDescriptors.length ||
    new Set(dryBusIds).size !== dryBusIds.length ||
    dryBusIds.some((id, index) => id !== routeDescriptors[index].busId)
  ) {
    fail("ordered dry bus IDs and route manifests disagree");
  }
  const routes = await Promise.all(
    routeDescriptors.map((descriptor) =>
      loadRouteManifest(
        manifestUrl,
        descriptor.path,
        declaredLoadLanes,
        fetchImplementation,
        cryptoImplementation,
      ),
    ),
  );
  if (routes.length === 0) fail("held texture package has no dry routes");
  for (let index = 0; index < routes.length; ++index) {
    const declaredBusId = routeDescriptors[index].busId;
    if (routes[index].busId !== declaredBusId) {
      fail(`route ${index} manifest bus ID disagrees with its root descriptor`);
    }
  }
  const referenceRoute = routes[0];
  for (const route of routes) {
    if (
      route.sampleRate !== referenceRoute.sampleRate ||
      route.samplesPerCycle !== referenceRoute.samplesPerCycle ||
      route.residualCycleCount !== referenceRoute.residualCycleCount ||
      route.manifest.engine !== manifest.engine
    ) {
      fail("held texture route contracts disagree");
    }
  }
  const contracts = requireRootContracts(
    manifest,
    referenceRoute.residualCycleCount,
  );
  const alignmentByState = new Map();
  for (const [index, descriptor] of contracts.alignment.cells.entries()) {
    object(descriptor, `phase_alignment.cells[${index}]`);
    const id = string(descriptor.id, `phase_alignment.cells[${index}].id`);
    const shift = finite(
      descriptor.shift_to_canonical_samples,
      `${id}.shift_to_canonical_samples`,
    );
    if (alignmentByState.has(id)) fail(`duplicate phase alignment cell ${id}`);
    alignmentByState.set(id, shift);
  }

  const routeCellMaps = routes.map(
    (route) => new Map(route.cells.map((cell) => [cell.stateId, cell])),
  );
  const pairedCells = referenceRoute.cells.map((referenceCell, index) => {
    const cells = routeCellMaps.map((cellMap, routeIndex) => {
      const cell = cellMap.get(referenceCell.stateId);
      if (cell === undefined) {
        fail(`route ${routes[routeIndex].busId} is missing ${referenceCell.stateId}`);
      }
      return cell;
    });
    for (const cell of cells) {
      if (
        cell.rpm !== referenceCell.rpm ||
        cell.lane !== referenceCell.lane ||
        cell.manifoldPressurePaAbs !== referenceCell.manifoldPressurePaAbs ||
        cell.throttle01 !== referenceCell.throttle01 ||
        !aliasesAgree(cell.loadAliases, referenceCell.loadAliases)
      ) {
        fail(`${referenceCell.stateId} route coordinates disagree`);
      }
      if (
        cell.sourceCycleBeginRevolutions !==
          referenceCell.sourceCycleBeginRevolutions ||
        cell.sourceCycleEndRevolutions !==
          referenceCell.sourceCycleEndRevolutions
      ) {
        fail(`${referenceCell.stateId} route cycle bounds disagree`);
      }
    }
    const shiftToCanonicalSamples = alignmentByState.get(referenceCell.stateId);
    if (shiftToCanonicalSamples === undefined) {
      fail(`phase alignment is missing ${referenceCell.stateId}`);
    }
    return {
      index,
      id: referenceCell.stateId,
      rpm: referenceCell.rpm,
      lane: referenceCell.lane,
      throttle01: referenceCell.throttle01,
      loadAliases: referenceCell.loadAliases,
      manifoldPressurePaAbs: referenceCell.manifoldPressurePaAbs,
      shiftToCanonicalSamples,
      routes: Object.freeze(cells),
      meanCombinedRms: Math.sqrt(combinedSignalPower(
        cells.map((cell) => cell.mean),
      )),
      residualCombinedPower: combinedSignalPower(
        cells.map((cell) => cell.residuals),
      ),
      selectorSeed: fnv1a64(referenceCell.stateId),
    };
  });
  if (
    routeCellMaps.some((cellMap) => cellMap.size !== pairedCells.length) ||
    alignmentByState.size !== pairedCells.length ||
    pairedCells.length !== manifest.domain.operating_cell_count
  ) {
    fail("held texture package operating-cell counts disagree");
  }
  if (!alignmentByState.has(contracts.alignment.reference_cell_id)) {
    fail("held texture phase-alignment reference cell is absent");
  }
  const rpmAnchors = [...new Set(pairedCells.map((cell) => cell.rpm))].sort(
    (left, right) => left - right,
  );
  if (
    rpmAnchors.length !== declaredRpmAnchors.length ||
    rpmAnchors.some((rpm, index) => rpm !== declaredRpmAnchors[index])
  ) {
    fail("route cells do not implement the declared RPM anchors");
  }
  const rows = rpmAnchors.map((rpm) => {
    const cells = pairedCells
      .filter((cell) => cell.rpm === rpm)
      .sort(
        (left, right) =>
          left.manifoldPressurePaAbs - right.manifoldPressurePaAbs,
      );
    if (cells.length === 0) fail(`${rpm} RPM has no held texture cells`);
    for (let index = 1; index < cells.length; ++index) {
      if (
        !(cells[index].manifoldPressurePaAbs >
          cells[index - 1].manifoldPressurePaAbs)
      ) {
        fail(`${rpm} RPM manifold-pressure cells are not strictly ordered`);
      }
    }
    const row = Object.freeze({ rpm, cells: Object.freeze(cells) });
    cellsByDeclaredLane(row, declaredLoadLanes);
    return row;
  });
  const { alignedMeans, gram } = buildMeanGram(
    pairedCells,
    referenceRoute.samplesPerCycle,
  );
  for (let index = 0; index < pairedCells.length; ++index) {
    pairedCells[index].alignedMeanCombined = alignedMeans[index];
  }
  for (const cell of pairedCells) Object.freeze(cell);

  return Object.freeze({
    kind: PACKAGE_KIND,
    manifestUrl: manifestUrl.href,
    manifestSha256,
    manifest,
    sampleRate: referenceRoute.sampleRate,
    samplesPerCycle: referenceRoute.samplesPerCycle,
    residualCycleCount: referenceRoute.residualCycleCount,
    busIds: Object.freeze(routes.map((route) => route.busId)),
    loadCoordinate: referenceRoute.manifest.domain.load_coordinate,
    minimumRpm,
    maximumRpm,
    rpmAnchors: Object.freeze(rpmAnchors),
    rows: Object.freeze(rows),
    cells: Object.freeze(pairedCells),
    meanGram: Object.freeze(gram),
    publicSeed: contracts.publicSeed,
    selection: contracts.selection,
  });
}

function validEndpoint(endpoint) {
  return (
    endpoint !== null &&
    Number.isFinite(endpoint.rpm) &&
    Number.isFinite(endpoint.manifoldPressurePaAbs) &&
    Number.isFinite(endpoint.unwrappedCrankRevolutions) &&
    endpoint.rpm > 0 &&
    endpoint.manifoldPressurePaAbs > 0 &&
    Number.isSafeInteger(endpoint.stateMask) &&
    (endpoint.stateMask & REQUIRED_RUNNING_STATE_MASK) ===
      REQUIRED_RUNNING_STATE_MASK &&
    (endpoint.stateMask & LIMITER_CUT_STATE_MASK) === 0
  );
}

export class HeldPhaseTextureCursor {
  #package;
  #sessionSeed;
  #residualMix;
  #active = false;
  #permutationCache = new Map();
  #renderedFrameCount = 0;
  #cycleBoundaryCount = 0;
  #lastCycleOrdinal = null;
  #lastDiagnostics = null;
  #minimumMeanGain = Infinity;
  #maximumMeanGain = 0;
  #minimumResidualGain = Infinity;
  #maximumResidualGain = 0;

  constructor(package_, { sessionSeed = "0" } = {}) {
    if (package_?.kind !== PACKAGE_KIND) {
      fail("HeldPhaseTextureCursor requires a loaded held texture package");
    }
    this.#package = package_;
    this.#sessionSeed = parseUint64(sessionSeed, "sessionSeed");
    this.#residualMix = 1;
  }

  get activeSegmentId() {
    return this.#active ? "held-phase-texture-live" : null;
  }

  get busIds() {
    return this.#package.busIds;
  }

  reset() {
    this.#active = false;
    this.#permutationCache.clear();
    this.#renderedFrameCount = 0;
    this.#cycleBoundaryCount = 0;
    this.#lastCycleOrdinal = null;
    this.#lastDiagnostics = null;
    this.#minimumMeanGain = Infinity;
    this.#maximumMeanGain = 0;
    this.#minimumResidualGain = Infinity;
    this.#maximumResidualGain = 0;
  }

  initialize(endpoint) {
    this.#active = this.#covers(endpoint);
    this.#lastCycleOrdinal = this.#active
      ? Math.floor(endpoint.unwrappedCrankRevolutions / 2)
      : null;
    return Object.freeze({ segmentId: this.activeSegmentId });
  }

  createOutputBuffers(capacity) {
    if (!Number.isSafeInteger(capacity) || capacity <= 0) {
      throw new RangeError("held texture output capacity must be positive");
    }
    return this.#package.busIds.map(() => new Float32Array(capacity));
  }

  diagnostics() {
    return Object.freeze({
      active: this.#active,
      renderedFrameCount: this.#renderedFrameCount,
      cycleBoundaryCount: this.#cycleBoundaryCount,
      selectorAlgorithm: SELECTION_ALGORITHM,
      publicSeed: this.#package.publicSeed.toString(),
      sessionSeed: this.#sessionSeed.toString(),
      residualMix: this.#residualMix,
      minimumMeanGain:
        this.#minimumMeanGain === Infinity ? null : this.#minimumMeanGain,
      maximumMeanGain: this.#maximumMeanGain,
      minimumResidualGain:
        this.#minimumResidualGain === Infinity
          ? null
          : this.#minimumResidualGain,
      maximumResidualGain: this.#maximumResidualGain,
      last: this.#lastDiagnostics,
    });
  }

  operatingWeights(endpoint) {
    if (
      endpoint === null ||
      !Number.isFinite(endpoint.rpm) ||
      !Number.isFinite(endpoint.manifoldPressurePaAbs) ||
      endpoint.rpm <= 0 ||
      endpoint.manifoldPressurePaAbs <= 0
    ) {
      fail("held texture operating weights require positive RPM and manifold pressure");
    }
    const weightedCells = this.#weights(
      clamp(endpoint.rpm, this.#package.minimumRpm, this.#package.maximumRpm),
      endpoint.manifoldPressurePaAbs,
    );
    const laneWeights = new Map(
      this.#package.manifest.domain.load_lanes.map(({ id }) => [id, 0]),
    );
    for (const entry of weightedCells) {
      laneWeights.set(
        entry.cell.lane,
        (laneWeights.get(entry.cell.lane) ?? 0) + entry.weight,
      );
    }
    return Object.freeze({
      loadCoordinate: this.#package.loadCoordinate,
      cells: Object.freeze(weightedCells.map((entry) => Object.freeze({
        id: entry.cell.id,
        lane: entry.cell.lane,
        rpm: entry.cell.rpm,
        manifoldPressurePaAbs: entry.cell.manifoldPressurePaAbs,
        weight: entry.weight,
      }))),
      lanes: Object.freeze([...laneWeights].map(([id, weight]) =>
        Object.freeze({ id, weight })
      )),
    });
  }

  renderBlockInto({ frameCount, start, end, outputBuffers }) {
    if (!Number.isSafeInteger(frameCount) || frameCount <= 0) {
      throw new RangeError("held texture render frameCount must be positive");
    }
    if (!this.#active || !this.#covers(start) || !this.#covers(end)) {
      throw new HeldPhaseTextureRuntimeError(
        "held-phase-texture-outside-coverage",
        `held texture covers ${this.#package.minimumRpm}..${this.#package.maximumRpm} RPM with ignition and fuel on`,
        { recoverable: true },
      );
    }
    if (
      !Array.isArray(outputBuffers) ||
      outputBuffers.length !== this.#package.busIds.length ||
      outputBuffers.some(
        (output) =>
          !(output instanceof Float32Array) || output.length < frameCount,
      )
    ) {
      throw new RangeError("held texture route output buffers are too small");
    }

    let finalWeights = null;
    let finalMeanGain = 1;
    let finalResidualGain = 1;
    let finalCycleOrdinal = this.#lastCycleOrdinal;
    for (let frame = 0; frame < frameCount; ++frame) {
      const amount = (frame + 1) / frameCount;
      const rpm = interpolate(start.rpm, end.rpm, amount);
      const map = interpolate(
        start.manifoldPressurePaAbs,
        end.manifoldPressurePaAbs,
        amount,
      );
      const crankRevolutions = interpolate(
        start.unwrappedCrankRevolutions,
        end.unwrappedCrankRevolutions,
        amount,
      );
      const weightedCells = this.#weights(rpm, map);
      const shiftToCanonicalSamples = weightedCells.reduce(
        (sum, entry) =>
          sum + entry.weight * entry.cell.shiftToCanonicalSamples,
        0,
      );
      const phaseCycles = crankRevolutions / 2;
      const enginePhaseSamples =
        positiveModulo(phaseCycles, 1) * this.#package.samplesPerCycle;
      const cycleOrdinalNumber = Math.floor(phaseCycles);
      if (!Number.isSafeInteger(cycleOrdinalNumber)) {
        throw new RangeError("engine cycle ordinal exceeds exact integer range");
      }
      if (
        this.#lastCycleOrdinal !== null &&
        cycleOrdinalNumber !== this.#lastCycleOrdinal
      ) {
        this.#cycleBoundaryCount += Math.abs(
          cycleOrdinalNumber - this.#lastCycleOrdinal,
        );
      }
      this.#lastCycleOrdinal = cycleOrdinalNumber;
      finalCycleOrdinal = cycleOrdinalNumber;
      const cycleOrdinal = BigInt(cycleOrdinalNumber);

      const meanRoutes = new Float64Array(this.#package.busIds.length);
      const residualRoutes = new Float64Array(this.#package.busIds.length);
      let targetMeanRms = 0;
      let targetResidualPower = 0;
      let rawResidualExpectedPower = 0;
      for (const entry of weightedCells) {
        const cell = entry.cell;
        const weight = entry.weight;
        // shift_to_canonical_samples follows the package's circular-shift
        // convention: aligned[n] = authored[n - shift].  Subtract the cell
        // shift and add the interpolated target shift so an exact anchor still
        // evaluates at its authored engine phase.
        const warpedPhase =
          enginePhaseSamples +
          shiftToCanonicalSamples -
          cell.shiftToCanonicalSamples;
        for (let routeIndex = 0; routeIndex < cell.routes.length; ++routeIndex) {
          meanRoutes[routeIndex] +=
            weight *
            sampleCyclicCubic(cell.routes[routeIndex].mean, warpedPhase);
        }
        targetMeanRms += weight * cell.meanCombinedRms;

        const residualCycle = this.#residualOrdinal(cell, cycleOrdinal);
        entry.residualOrdinal = residualCycle;
        const residualOffset =
          residualCycle * this.#package.samplesPerCycle;
        for (let routeIndex = 0; routeIndex < cell.routes.length; ++routeIndex) {
          residualRoutes[routeIndex] +=
            weight *
            sampleCyclicCubicSegment(
              cell.routes[routeIndex].residuals,
              residualOffset,
              this.#package.samplesPerCycle,
              enginePhaseSamples,
            );
        }
        targetResidualPower += weight * cell.residualCombinedPower;
        rawResidualExpectedPower +=
          weight * weight * cell.residualCombinedPower;
      }

      let rawMeanPower = 0;
      for (const left of weightedCells) {
        for (const right of weightedCells) {
          rawMeanPower +=
            left.weight *
            right.weight *
            this.#package.meanGram[left.cell.index][right.cell.index];
        }
      }
      const meanGain =
        rawMeanPower > 1e-30
          ? targetMeanRms / Math.sqrt(rawMeanPower)
          : 1;
      const residualGain =
        targetResidualPower > 1e-30 && rawResidualExpectedPower > 1e-30
          ? Math.sqrt(targetResidualPower / rawResidualExpectedPower)
          : 0;
      for (let routeIndex = 0; routeIndex < outputBuffers.length; ++routeIndex) {
        const sample =
          meanGain * meanRoutes[routeIndex] +
          this.#residualMix * residualGain * residualRoutes[routeIndex];
        if (!Number.isFinite(sample)) {
          throw new RangeError(
            `held texture synthesis was non-finite at frame ${frame}, route ${routeIndex}`,
          );
        }
        outputBuffers[routeIndex][frame] = sample;
      }
      this.#minimumMeanGain = Math.min(this.#minimumMeanGain, meanGain);
      this.#maximumMeanGain = Math.max(this.#maximumMeanGain, meanGain);
      this.#minimumResidualGain = Math.min(
        this.#minimumResidualGain,
        residualGain,
      );
      this.#maximumResidualGain = Math.max(
        this.#maximumResidualGain,
        residualGain,
      );
      finalWeights = weightedCells;
      finalMeanGain = meanGain;
      finalResidualGain = residualGain;
    }
    this.#renderedFrameCount += frameCount;
    this.#lastDiagnostics = Object.freeze({
      rpm: end.rpm,
      manifoldPressurePaAbs: end.manifoldPressurePaAbs,
      cycleOrdinal: finalCycleOrdinal,
      meanGain: finalMeanGain,
      residualGain: finalResidualGain,
      cells: Object.freeze(
        (finalWeights ?? []).map((entry) =>
          Object.freeze({
            id: entry.cell.id,
            weight: entry.weight,
            residualOrdinal: entry.residualOrdinal,
          }),
        ),
      ),
    });
  }

  #covers(endpoint) {
    return (
      validEndpoint(endpoint) &&
      endpoint.rpm >= this.#package.minimumRpm &&
      endpoint.rpm <= this.#package.maximumRpm
    );
  }

  #weights(rpm, map) {
    const rows = this.#package.rows;
    let leftRow;
    let rightRow;
    let rpmAmount;
    if (rpm <= rows[0].rpm) {
      leftRow = rightRow = rows[0];
      rpmAmount = 0;
    } else if (rpm >= rows.at(-1).rpm) {
      leftRow = rightRow = rows.at(-1);
      rpmAmount = 0;
    } else {
      let rightIndex = 1;
      while (rows[rightIndex].rpm < rpm) ++rightIndex;
      rightRow = rows[rightIndex];
      if (rightRow.rpm === rpm) {
        leftRow = rightRow;
        rpmAmount = 0;
      } else {
        leftRow = rows[rightIndex - 1];
        rpmAmount = (rpm - leftRow.rpm) / (rightRow.rpm - leftRow.rpm);
      }
    }

    const declaredLoadLanes = this.#package.manifest.domain.load_lanes;
    const leftByLane = cellsByDeclaredLane(leftRow, declaredLoadLanes);
    const rightByLane = leftRow === rightRow
      ? leftByLane
      : cellsByDeclaredLane(rightRow, declaredLoadLanes);
    const laneCurves = collapseAliasCoincidentCurves(
      declaredLoadLanes.map(({ id: lane }) => {
        const leftCell = leftByLane.get(lane);
        const rightCell = rightByLane.get(lane);
        return {
          lanes: [lane],
          leftCell,
          rightCell,
          manifoldPressurePaAbs: interpolate(
            leftCell.manifoldPressurePaAbs,
            rightCell.manifoldPressurePaAbs,
            rpmAmount,
          ),
        };
      }).sort(
        (left, right) =>
          left.manifoldPressurePaAbs - right.manifoldPressurePaAbs,
      ),
      rpm,
    );
    for (let index = 1; index < laneCurves.length; ++index) {
      if (
        !(laneCurves[index].manifoldPressurePaAbs >
          laneCurves[index - 1].manifoldPressurePaAbs)
      ) {
        throw new RangeError(
          `held load-lane curves are not strictly ordered at ${rpm} RPM`,
        );
      }
    }

    let loadCurves;
    if (map <= laneCurves[0].manifoldPressurePaAbs) {
      loadCurves = [{ curve: laneCurves[0], weight: 1 }];
    } else if (map >= laneCurves.at(-1).manifoldPressurePaAbs) {
      loadCurves = [{ curve: laneCurves.at(-1), weight: 1 }];
    } else {
      let rightIndex = 1;
      while (laneCurves[rightIndex].manifoldPressurePaAbs < map) ++rightIndex;
      const left = laneCurves[rightIndex - 1];
      const right = laneCurves[rightIndex];
      const amount =
        (map - left.manifoldPressurePaAbs) /
        (right.manifoldPressurePaAbs - left.manifoldPressurePaAbs);
      loadCurves = [
        { curve: left, weight: 1 - amount },
        { curve: right, weight: amount },
      ];
    }
    const result = [];
    for (const { curve, weight } of loadCurves) {
      if (leftRow === rightRow || rpmAmount === 0) {
        if (weight > 0) result.push({ cell: curve.leftCell, weight });
        continue;
      }
      const leftWeight = weight * (1 - rpmAmount);
      const rightWeight = weight * rpmAmount;
      if (leftWeight > 0) result.push({ cell: curve.leftCell, weight: leftWeight });
      if (rightWeight > 0) result.push({ cell: curve.rightCell, weight: rightWeight });
    }
    return combineCellWeights(result);
  }

  #residualOrdinal(cell, cycleOrdinal) {
    const bagSize = BigInt(this.#package.residualCycleCount);
    const bagOrdinal = floorDivBigInt(cycleOrdinal, bagSize);
    const slot = Number(floorModuloBigInt(cycleOrdinal, bagSize));
    return this.#permutation(cell, bagOrdinal)[slot];
  }

  #permutation(cell, bagOrdinal) {
    const key = `${cell.index}:${bagOrdinal}`;
    const cached = this.#permutationCache.get(key);
    if (cached !== undefined) return cached;
    const permutation = this.#rawPermutation(cell, bagOrdinal);
    const previous = this.#rawPermutation(cell, bagOrdinal - 1n);
    if (permutation[0] === previous.at(-1)) {
      const swap = permutation[0];
      permutation[0] = permutation[1];
      permutation[1] = swap;
    }
    this.#permutationCache.set(key, permutation);
    if (this.#permutationCache.size > 256) {
      const oldest = this.#permutationCache.keys().next().value;
      this.#permutationCache.delete(oldest);
    }
    return permutation;
  }

  #rawPermutation(cell, bagOrdinal) {
    const permutation = Array.from(
      { length: this.#package.residualCycleCount },
      (_, index) => index,
    );
    let state = splitmix64(
      this.#package.publicSeed ^
        this.#sessionSeed ^
        cell.selectorSeed ^
        BigInt.asUintN(64, bagOrdinal),
    );
    for (let index = permutation.length - 1; index > 0; --index) {
      state = splitmix64(state);
      const swapIndex = Number(state % BigInt(index + 1));
      const swap = permutation[index];
      permutation[index] = permutation[swapIndex];
      permutation[swapIndex] = swap;
    }
    return permutation;
  }
}
