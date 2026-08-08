import {
  HeldPhaseTextureCursor,
  HeldPhaseTextureRuntimeError,
  loadHeldPhaseTexturePackage,
} from "./held-phase-texture-runtime.js";
import {
  CanonicalMasterDynamics,
  FixedSpectrumConvolver,
  PartitionedSpectrumConvolver,
  loadDryDirectionalPhaseRuntime,
} from "./dry-directional-phase-runtime.js";
import {
  DirectionalPhaseCellCursor,
  DirectionalPhaseCellError,
} from "./directional-phase-cell.js";
import {
  loadStatePhaseTextureRuntime,
  StatePhaseTextureCursor,
  StatePhaseTextureRuntimeError,
} from "./state-phase-texture-runtime.js";
import { SteadyTransientEnvelope } from "./steady-transient-envelope.js";
import { loadResponsiveAudioLifecycleRuntime } from "./responsive-audio-lifecycle-runtime.js";
import { loadRendererRuntimeCompatibility } from "./renderer-runtime-compatibility.js";

const RUNTIME_SCHEMA =
  "engine-sim-offline/responsive-audio-preview";
const RUNTIME_KIND = "responsive-audio-preview";
const CANONICAL_SAMPLE_RATE = 192_000;
const FFT_SIZE = 65_536;
const IR_COEFFICIENT_COUNT = 30_071;
const SPECTRUM_ENCODING = "interleaved-complex-float64le";
const FIXED_TRANSFER_KIND = "fixed-overlap-save-complex-spectrum-v1";
const PARTITIONED_TRANSFER_KIND =
  "uniform-partitioned-overlap-save-complex-spectra-v1";
const PARTITIONED_FFT_SIZE = 8_192;
const PARTITIONED_BLOCK_FRAMES = 3_840;
const PARTITIONED_BATCH_FRAMES = PARTITIONED_BLOCK_FRAMES * 8;
const MAXIMUM_PARTITIONED_COEFFICIENT_COUNT = 570_654;
const BATCH_FRAMES = 32_768;
const REQUIRED_RUNNING_STATE_MASK = 0x3;
const LIMITER_CUT_STATE_MASK = 0x10;
const EMPTY_FLOAT32 = new Float32Array(0);

export class HeldTexturePresentationRuntimeError extends Error {
  constructor(code, message, { recoverable = false } = {}) {
    super(message);
    this.name = "HeldTexturePresentationRuntimeError";
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

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("held texture presentation manifest URL must be nonempty");
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
  return Array.from(
    bytes,
    (value) => value.toString(16).padStart(2, "0"),
  ).join("");
}

async function requireSha256(bytes, expected, cryptoImplementation, label) {
  string(expected, `${label}.sha256`);
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    throw new Error(
      "Web Crypto SHA-256 is required for held texture presentation loading",
    );
  }
  const digest = hex(
    new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", bytes)),
  );
  if (digest !== expected) {
    throw new Error(`${label} SHA-256 does not match its manifest`);
  }
}

function decodeSpectrum(bytes, transfer) {
  const expectedBins = transfer.fftSize * transfer.partitionCount;
  const expectedBytes = expectedBins * 2 * 8;
  if (bytes.byteLength !== expectedBytes) {
    throw new RangeError(
      `IR spectrum has ${bytes.byteLength} bytes; expected ${expectedBytes}`,
    );
  }
  const real = new Float64Array(expectedBins);
  const imaginary = new Float64Array(expectedBins);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let index = 0; index < expectedBins; ++index) {
    const realValue = view.getFloat64(index * 16, true);
    const imaginaryValue = view.getFloat64(index * 16 + 8, true);
    if (!Number.isFinite(realValue) || !Number.isFinite(imaginaryValue)) {
      throw new RangeError(`IR spectrum contains a non-finite bin at ${index}`);
    }
    real[index] = realValue;
    imaginary[index] = imaginaryValue;
  }
  return Object.freeze({
    kind: transfer.kind,
    fftSize: transfer.fftSize,
    coefficientCount: transfer.coefficientCount,
    partitionFrameCount: transfer.partitionFrameCount,
    partitionCount: transfer.partitionCount,
    real,
    imaginary,
  });
}

async function loadSpectrum(
  transfer,
  packageManifestUrl,
  fetchImplementation,
  cryptoImplementation,
) {
  const spectrumUrl = relativeUrl(
    packageManifestUrl,
    transfer.spectrumPath,
    "held route transfer.spectrum_path",
  );
  const spectrumBytes = await fetchBytes(
    spectrumUrl,
    fetchImplementation,
    "held route IR spectrum",
  );
  if (spectrumBytes.byteLength !== transfer.spectrumByteCount) {
    throw new RangeError(
      "held route IR spectrum byte count does not match its package",
    );
  }
  await requireSha256(
    spectrumBytes,
    transfer.spectrumSha256,
    cryptoImplementation,
    "held route IR spectrum",
  );
  return decodeSpectrum(spectrumBytes, transfer);
}

function parseTransfer(transferValue, label) {
  const transfer = object(transferValue, label);
  const kind = Object.hasOwn(transfer, "kind")
    ? string(transfer.kind, `${label}.kind`)
    : FIXED_TRANSFER_KIND;
  const fftSize = positiveInteger(transfer.fft_size, `${label}.fft_size`);
  const coefficientCount = positiveInteger(
    transfer.coefficient_count,
    `${label}.coefficient_count`,
  );
  let partitionFrameCount = 0;
  let partitionCount = 1;
  if (kind === FIXED_TRANSFER_KIND) {
    if (fftSize !== FFT_SIZE || coefficientCount !== IR_COEFFICIENT_COUNT) {
      fail(`${label} must use the canonical 65536/30071 fixed IR shape`);
    }
    if (BATCH_FRAMES > fftSize - (coefficientCount - 1)) {
      fail(`${label} does not fit the overlap-save batch`);
    }
    if (
      Object.hasOwn(transfer, "partition_frame_count") ||
      Object.hasOwn(transfer, "partition_count")
    ) {
      fail(`${label} fixed transfer must not contain partition fields`);
    }
  } else if (kind === PARTITIONED_TRANSFER_KIND) {
    partitionFrameCount = positiveInteger(
      transfer.partition_frame_count,
      `${label}.partition_frame_count`,
    );
    partitionCount = positiveInteger(
      transfer.partition_count,
      `${label}.partition_count`,
    );
    if (
      fftSize !== PARTITIONED_FFT_SIZE ||
      partitionFrameCount !== PARTITIONED_BLOCK_FRAMES ||
      coefficientCount <= IR_COEFFICIENT_COUNT ||
      coefficientCount > MAXIMUM_PARTITIONED_COEFFICIENT_COUNT ||
      partitionCount !== Math.ceil(coefficientCount / partitionFrameCount)
    ) {
      fail(`${label} has an invalid complete partitioned IR shape`);
    }
  } else {
    fail(`unsupported ${label} kind ${kind}`);
  }
  if (transfer.spectrum_encoding !== SPECTRUM_ENCODING) {
    fail(`unsupported ${label} encoding ${transfer.spectrum_encoding}`);
  }
  return Object.freeze({
    kind,
    fftSize,
    coefficientCount,
    partitionFrameCount,
    partitionCount,
    spectrumEncoding: transfer.spectrum_encoding,
    spectrumPath: string(
      transfer.spectrum_path,
      `${label}.spectrum_path`,
    ),
    spectrumByteCount: positiveInteger(
      transfer.spectrum_byte_count,
      `${label}.spectrum_byte_count`,
    ),
    spectrumSha256: string(
      transfer.spectrum_sha256,
      `${label}.spectrum_sha256`,
    ),
  });
}

function validatePresentation(manifest, heldPackage) {
  if (
    manifest.engine !== heldPackage.manifest.engine ||
    heldPackage.sampleRate !== CANONICAL_SAMPLE_RATE
  ) {
    fail("held package engine or sample rate does not match its presentation");
  }
  object(heldPackage.manifest.provenance, "held package.provenance");
  object(heldPackage.manifest.provenance.engine, "held package.provenance.engine");
  object(
    heldPackage.manifest.provenance.renderer_build,
    "held package.provenance.renderer_build",
  );
  const presentation = object(
    heldPackage.manifest.presentation,
    "held package.presentation",
  );
  if (Object.hasOwn(heldPackage.manifest, "transfer")) {
    fail("held texture must not duplicate presentation transfer fields");
  }
  for (const forbidden of [
    "publication_gain_linear",
    "dry_route_publication_scale",
    "audition_bus_gain_linear",
  ]) {
    if (Object.hasOwn(presentation, forbidden)) {
      fail(`held presentation must not contain legacy field ${forbidden}`);
    }
  }
  const routes = array(presentation.routes, "held presentation.routes").map(
    (value, index) => {
      const route = object(value, `held presentation.routes[${index}]`);
      if (Object.hasOwn(route, "source_gain_linear")) {
        fail("held presentation routes must not contain dead source gain");
      }
      const dryBusId = string(
        route.dry_bus_id,
        `held presentation.routes[${index}].dry_bus_id`,
      );
      const wetMix01 = finite(
        route.wet_mix_01,
        `held presentation.routes[${index}].wet_mix_01`,
      );
      if (wetMix01 < 0 || wetMix01 > 1) {
        fail(`held presentation route ${dryBusId} has invalid wet mix`);
      }
      const impulseResponseGainLinear = finite(
        route.impulse_response_gain_linear,
        `held presentation.routes[${index}].impulse_response_gain_linear`,
      );
      if (impulseResponseGainLinear < 0) {
        fail(`held presentation route ${dryBusId} has negative IR gain`);
      }
      return Object.freeze({
        dryBusId,
        sourceRouteId: string(
          route.source_route_id,
          `held presentation.routes[${index}].source_route_id`,
        ),
        impulseResponseAssetId: string(
          route.impulse_response_asset_id,
          `held presentation.routes[${index}].impulse_response_asset_id`,
        ),
        impulseResponsePayloadSha256: string(
          route.impulse_response_payload_sha256,
          `held presentation.routes[${index}].impulse_response_payload_sha256`,
        ),
        impulseResponseGainLinear,
        wetMix01,
        transfer: parseTransfer(
          route.transfer,
          `held presentation.routes[${index}].transfer`,
        ),
      });
    },
  );
  if (
    routes.length !== heldPackage.busIds.length ||
    routes.some((route, index) => route.dryBusId !== heldPackage.busIds[index])
  ) {
    fail("held presentation routes must match ordered held dry buses exactly");
  }
  const auditionDryBusOrder = array(
    presentation.audition_dry_bus_order,
    "held presentation.audition_dry_bus_order",
  ).map((value, index) =>
    string(value, `held presentation.audition_dry_bus_order[${index}]`)
  );
  if (
    auditionDryBusOrder.length !== routes.length ||
    new Set(auditionDryBusOrder).size !== routes.length ||
    auditionDryBusOrder.some(
      (busId) => !routes.some((route) => route.dryBusId === busId),
    )
  ) {
    fail("held audition order must select every dry route exactly once");
  }
  return Object.freeze({
    auditionBusId: string(
      presentation.audition_bus_id,
      "held presentation.audition_bus_id",
    ),
    routes: Object.freeze(routes),
    auditionDryBusOrder: Object.freeze(auditionDryBusOrder),
    auditionRouteIndices: Object.freeze(
      auditionDryBusOrder.map((busId) =>
        routes.findIndex((route) => route.dryBusId === busId)
      ),
    ),
    capturedToSourceScale: positive(
      presentation.captured_to_source_scale,
      "held presentation.captured_to_source_scale",
    ),
    masterVolumeLinear: positive(
      presentation.master_volume_linear,
      "held presentation.master_volume_linear",
    ),
  });
}

function identity(value, label) {
  const descriptor = object(value, label);
  return Object.freeze({
    id: string(descriptor.id, `${label}.id`),
    sha256: string(descriptor.sha256, `${label}.sha256`),
  });
}

function sameIdentity(left, right) {
  return left.id === right.id && left.sha256 === right.sha256;
}

function validateDirectionalPackage(
  manifest,
  heldPackage,
  heldPresentation,
  directionalPackage,
) {
  if (
    directionalPackage.sampleRate !== CANONICAL_SAMPLE_RATE ||
    directionalPackage.minimumRpm !== manifest.domain.minimum_rpm ||
    directionalPackage.maximumRpm !== manifest.domain.maximum_rpm
  ) {
    fail("directional package sample rate or authored RPM domain disagrees");
  }
  if (
    directionalPackage.manifest.engine !== manifest.engine ||
    heldPackage.manifest.engine !== manifest.engine
  ) {
    fail("held, directional, and presentation engines must match exactly");
  }

  const heldProvenance = object(
    heldPackage.manifest.provenance,
    "held package.provenance",
  );
  const directionalProvenance = object(
    directionalPackage.manifest.provenance,
    "directional package.provenance",
  );
  const heldEngine = identity(heldProvenance.engine, "held engine identity");
  const directionalEngine = identity(
    directionalProvenance.engine,
    "directional engine identity",
  );
  const heldRenderer = identity(
    heldProvenance.renderer_build,
    "held renderer identity",
  );
  const directionalRenderer = identity(
    directionalProvenance.renderer_build,
    "directional renderer identity",
  );
  if (
    !sameIdentity(heldEngine, directionalEngine) ||
    !sameIdentity(heldRenderer, directionalRenderer)
  ) {
    fail("held and directional engine/renderer identities must match exactly");
  }
  if (manifest.provenance !== undefined) {
    const presentationProvenance = object(
      manifest.provenance,
      "presentation.provenance",
    );
    if (
      !sameIdentity(
        heldEngine,
        identity(
          presentationProvenance.engine,
          "presentation engine identity",
        ),
      ) ||
      !sameIdentity(
        heldRenderer,
        identity(
          presentationProvenance.renderer_build,
          "presentation renderer identity",
        ),
      )
    ) {
      fail("presentation provenance must match its held/directional material");
    }
  }

  const routePairs = array(
    directionalPackage.routePairs,
    "directional routePairs",
  );
  if (
    routePairs.length !== heldPresentation.routes.length ||
    routePairs.some(
      (pair, index) => pair.busId !== heldPresentation.routes[index].dryBusId,
    )
  ) {
    fail("held and directional ordered dry routes must match exactly");
  }
  let seamAlgorithm = null;
  for (const [index, pair] of routePairs.entries()) {
    for (const [direction, route] of [
      ["rising", pair.rising],
      ["falling", pair.falling],
    ]) {
      object(route, `directional routePairs[${index}].${direction}`);
      const seam = object(
        route.manifest.seam_closure,
        `directional routePairs[${index}].${direction}.seam_closure`,
      );
      const algorithm = string(
        seam.algorithm,
        `directional routePairs[${index}].${direction}.seam_closure.algorithm`,
      );
      if (seam.phase_aligned !== true) {
        fail(`directional route ${pair.busId} ${direction} is not seam-closed`);
      }
      if (seamAlgorithm === null) seamAlgorithm = algorithm;
      else if (seamAlgorithm !== algorithm) {
        fail("directional routes disagree on their seam-closure algorithm");
      }
    }
  }
}

// Loads a held, consumer-state-indexed texture package and its one immutable
// presentation transfer. The wrapper contains no authored RPM timeline.
export async function loadHeldTexturePresentationRuntime(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("held texture presentation loading requires fetch");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifestBytes = await fetchBytes(
    manifestUrl,
    fetchImplementation,
    "held texture presentation manifest",
  );
  const manifest = JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes),
  );
  object(manifest, "manifest");
  if (manifest.schema !== RUNTIME_SCHEMA) {
    fail(`unsupported held presentation schema ${manifest.schema}`);
  }
  if (Object.hasOwn(manifest, "settings")) {
    fail("responsive audio has no selectable residual/transient ablation settings");
  }
  string(manifest.id, "manifest.id");
  string(manifest.engine, "manifest.engine");

  object(manifest.audio, "manifest.audio");
  if (
    manifest.audio.sample_rate_hz !== CANONICAL_SAMPLE_RATE ||
    manifest.audio.encoding !== "float32le" ||
    manifest.audio.channel_layout !== "mono"
  ) {
    fail("held texture presentation output must be mono 192 kHz float32le");
  }
  const busId = string(manifest.audio.bus_id, "audio.bus_id");
  const heldPackageUrl = relativeUrl(
    manifestUrl,
    manifest.held_package_path,
    "held_package_path",
  );
  const directionalPackageUrl = relativeUrl(
    manifestUrl,
    manifest.directional_package_path,
    "directional_package_path",
  );
  const motoringPackageUrl = Object.hasOwn(manifest, "motoring_package_path")
    ? relativeUrl(
        manifestUrl,
        manifest.motoring_package_path,
        "motoring_package_path",
      )
    : null;
  const lifecyclePackageUrl = Object.hasOwn(manifest, "lifecycle_package_path")
    ? relativeUrl(
        manifestUrl,
        manifest.lifecycle_package_path,
        "lifecycle_package_path",
      )
    : null;
  object(manifest.domain, "manifest.domain");
  const minimumRpm = positive(
    manifest.domain.minimum_rpm,
    "manifest.domain.minimum_rpm",
  );
  const maximumRpm = positive(
    manifest.domain.maximum_rpm,
    "manifest.domain.maximum_rpm",
  );
  if (!(maximumRpm > minimumRpm)) {
    fail("held presentation RPM domain is invalid");
  }

  const heldPackage = await loadHeldPhaseTexturePackage(heldPackageUrl, {
    fetch: fetchImplementation,
    crypto: cryptoImplementation,
  });
  if (
    minimumRpm > heldPackage.minimumRpm ||
    maximumRpm < heldPackage.maximumRpm
  ) {
    fail("presentation RPM domain must contain every held anchor");
  }
  const presentation = validatePresentation(manifest, heldPackage);
  if (busId !== presentation.auditionBusId) {
    fail("presentation output bus must equal the authored audition bus");
  }
  const directionalPackage = await loadDryDirectionalPhaseRuntime(
    directionalPackageUrl,
    {
      fetch: fetchImplementation,
      crypto: cryptoImplementation,
    },
  );
  validateDirectionalPackage(
    manifest,
    heldPackage,
    presentation,
    directionalPackage,
  );
  const rendererCompatibility = await loadRendererRuntimeCompatibility(
    manifest.runtime_renderer_compatibility,
    manifest.provenance?.renderer_build?.sha256,
    manifestUrl,
    { fetch: fetchImplementation, crypto: cryptoImplementation },
  );
  const motoringPackage = motoringPackageUrl === null
    ? null
    : await loadStatePhaseTextureRuntime(motoringPackageUrl, {
        fetch: fetchImplementation,
        crypto: cryptoImplementation,
      });
  const lifecyclePackage = lifecyclePackageUrl === null
    ? null
    : await loadResponsiveAudioLifecycleRuntime(lifecyclePackageUrl, {
        fetch: fetchImplementation,
        crypto: cryptoImplementation,
      });
  if (motoringPackage !== null) {
    if (
      motoringPackage.sampleRate !== CANONICAL_SAMPLE_RATE ||
      motoringPackage.manifest.engine !== manifest.engine ||
      motoringPackage.requiredOnMask !== 0x02 ||
      motoringPackage.requiredOffMask !== 0x01 ||
      motoringPackage.dryBusIds.length !== presentation.routes.length ||
      motoringPackage.dryBusIds.some(
        (bus, index) => bus !== presentation.routes[index].dryBusId,
      )
    ) {
      fail("motoring state package does not match the responsive presentation");
    }
    if (
      motoringPackage.minimumRpm > minimumRpm ||
      motoringPackage.maximumRpm < maximumRpm
    ) {
      fail("motoring state package must cover the complete running RPM domain");
    }
    const motoringProvenance = object(
      motoringPackage.manifest.provenance,
      "motoring package.provenance",
    );
    if (
      !sameIdentity(
        identity(motoringProvenance.engine, "motoring engine identity"),
        identity(heldPackage.manifest.provenance.engine, "held engine identity"),
      ) ||
      !sameIdentity(
        identity(
          motoringProvenance.renderer_build,
          "motoring renderer identity",
        ),
        identity(
          heldPackage.manifest.provenance.renderer_build,
          "held renderer identity",
        ),
      )
    ) {
      fail("motoring package provenance does not match running material");
    }
  }
  if (lifecyclePackage !== null) {
    const lifecycleProvenance = object(
      lifecyclePackage.manifest.provenance,
      "lifecycle package.provenance",
    );
    if (
      lifecyclePackage.engine !== manifest.engine ||
      lifecyclePackage.sampleRate !== CANONICAL_SAMPLE_RATE ||
      !sameIdentity(
        identity(lifecycleProvenance.engine, "lifecycle engine identity"),
        identity(heldPackage.manifest.provenance.engine, "held engine identity"),
      ) ||
      !sameIdentity(
        identity(
          lifecycleProvenance.renderer_build,
          "lifecycle renderer identity",
        ),
        identity(
          heldPackage.manifest.provenance.renderer_build,
          "held renderer identity",
        ),
      ) ||
      lifecyclePackage.startupAdmission.evidence
        .correctedHeldManifestSha256 !== heldPackage.manifestSha256
    ) {
      fail("lifecycle package identity or held-material binding does not match running material");
    }
  }
  const spectrumPromises = new Map();
  const routePresentations = presentation.routes.map((route) => {
    const key = JSON.stringify([
      route.transfer.kind,
      route.transfer.fftSize,
      route.transfer.coefficientCount,
      route.transfer.partitionFrameCount,
      route.transfer.partitionCount,
      route.transfer.spectrumEncoding,
      route.transfer.spectrumPath,
      route.transfer.spectrumByteCount,
      route.transfer.spectrumSha256,
    ]);
    let spectrumPromise = spectrumPromises.get(key);
    if (spectrumPromise === undefined) {
      spectrumPromise = loadSpectrum(
        route.transfer,
        new URL(heldPackage.manifestUrl),
        fetchImplementation,
        cryptoImplementation,
      );
      spectrumPromises.set(key, spectrumPromise);
    }
    return { ...route, spectrumPromise };
  });
  const loadedRoutePresentations = await Promise.all(
    routePresentations.map(async (route) => Object.freeze({
      ...route,
      spectrum: await route.spectrumPromise,
      spectrumPromise: undefined,
    })),
  );
  const sharedFullWetTransfer = loadedRoutePresentations.every(
    (route) =>
      route.wetMix01 === 1 &&
      route.spectrum === loadedRoutePresentations[0].spectrum,
  );
  const hasPartitionedTransfer = loadedRoutePresentations.some(
    (route) => route.transfer.kind === PARTITIONED_TRANSFER_KIND,
  );
  return Object.freeze({
    kind: RUNTIME_KIND,
    manifestUrl: manifestUrl.href,
    manifest,
    sampleRate: CANONICAL_SAMPLE_RATE,
    busIds: Object.freeze([busId]),
    minimumRpm,
    maximumRpm,
    heldMinimumRpm: heldPackage.minimumRpm,
    heldMaximumRpm: heldPackage.maximumRpm,
    batchFrames: hasPartitionedTransfer
      ? PARTITIONED_BATCH_FRAMES
      : BATCH_FRAMES,
    coefficientCount: Math.max(
      ...loadedRoutePresentations.map(
        (route) => route.transfer.coefficientCount,
      ),
    ),
    capturedToSourceScale: presentation.capturedToSourceScale,
    masterVolumeLinear: presentation.masterVolumeLinear,
    auditionRouteIndices: presentation.auditionRouteIndices,
    routePresentations: Object.freeze(loadedRoutePresentations),
    sharedFullWetTransfer,
    heldPackage,
    directionalPackage,
    motoringPackage,
    lifecyclePackage,
    rendererCompatibility,
    spectrum: loadedRoutePresentations[0].spectrum,
  });
}

function validRunningEndpoint(value) {
  return value !== null &&
    Number.isFinite(value.rpm) &&
    Number.isFinite(value.manifoldPressurePaAbs) &&
    Number.isFinite(value.requestedThrottle01) &&
    Number.isFinite(value.unwrappedCrankRevolutions) &&
    Number.isSafeInteger(value.stateMask) &&
    value.rpm > 0 &&
    value.manifoldPressurePaAbs > 0 &&
    value.requestedThrottle01 >= 0 &&
    value.requestedThrottle01 <= 1 &&
    (value.stateMask & REQUIRED_RUNNING_STATE_MASK) ===
      REQUIRED_RUNNING_STATE_MASK;
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function concatenate(chunks, totalLength) {
  if (chunks.length === 0) return EMPTY_FLOAT32;
  if (chunks.length === 1) return chunks[0];
  const result = new Float32Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += chunk.length;
  }
  return result;
}

export class HeldTexturePresentationRuntimeCursor {
  #package;
  #heldCursor;
  #heldBuffers = null;
  #heldBufferCapacity = 0;
  #directionalCursors = null;
  #directionalBuffers = null;
  #directionalBufferCapacity = 0;
  #motoringCursor = null;
  #envelope = null;
  #lastEndpoint = null;
  #envelopeFrame = 0;
  #convolvers;
  #sharedConvolver = null;
  #master;
  #pendingDryRoutes;
  #pendingSource;
  #pendingFrames = 0;
  #pendingWarmPrefixFrames = 0;
  #active = false;
  #audible = false;
  #warmFrameCount = 0;
  #audibleInputFrameCount = 0;
  #audibleOutputFrameCount = 0;
  #transientRisingRenderedBlockCount = 0;
  #transientFallingRenderedBlockCount = 0;
  #transientRisingActiveFrameCount = 0;
  #transientFallingActiveFrameCount = 0;
  #transientOverlapFrameCount = 0;
  #transientMaximumRisingGain = 0;
  #transientMaximumFallingGain = 0;
  #transientAdmittedEvents = [];
  #tailOnlyInputFrameCount = 0;
  #tailOnlyOutputFrameCount = 0;
  #combustionTransitionBlockCount = 0;
  #motoringRenderedBlockCount = 0;
  #runningToMotoringTransitionCount = 0;
  #motoringToRunningTransitionCount = 0;

  constructor(package_, { sessionSeed = "0" } = {}) {
    if (package_?.kind !== RUNTIME_KIND) {
      fail(
        "HeldTexturePresentationRuntimeCursor requires a loaded runtime package",
      );
    }
    this.#package = package_;
    this.#heldCursor = new HeldPhaseTextureCursor(package_.heldPackage, {
      sessionSeed,
    });
    this.#directionalCursors = package_.directionalPackage.routePairs.map(
      (pair) => Object.freeze({
        busId: pair.busId,
        rising: new DirectionalPhaseCellCursor(pair.rising),
        falling: new DirectionalPhaseCellCursor(pair.falling),
      }),
    );
    this.#envelope = new SteadyTransientEnvelope();
    this.#motoringCursor = package_.motoringPackage === null
      ? null
      : new StatePhaseTextureCursor(package_.motoringPackage);
    this.#convolvers = package_.sharedFullWetTransfer
      ? []
      : package_.routePresentations.map(
          (route) => route.transfer.kind === PARTITIONED_TRANSFER_KIND
            ? new PartitionedSpectrumConvolver(route.spectrum)
            : new FixedSpectrumConvolver(
                route.spectrum,
                package_.batchFrames,
              ),
        );
    if (package_.sharedFullWetTransfer) {
      const route = package_.routePresentations[0];
      this.#sharedConvolver = route.transfer.kind === PARTITIONED_TRANSFER_KIND
        ? new PartitionedSpectrumConvolver(route.spectrum)
        : new FixedSpectrumConvolver(route.spectrum, package_.batchFrames);
    }
    this.#master = new CanonicalMasterDynamics(package_.masterVolumeLinear);
    this.#pendingDryRoutes = package_.routePresentations.map(
      () => new Float64Array(package_.batchFrames),
    );
    this.#pendingSource = new Float32Array(package_.batchFrames);
  }

  get activeSegmentId() {
    return this.#active ? "held-texture-presentation-live" : null;
  }

  get busIds() {
    return this.#package.busIds;
  }

  get latencyFrames() {
    return this.#package.batchFrames;
  }

  runningLoadWeights(endpoint) {
    if (!validRunningEndpoint(endpoint)) {
      fail("running load weights require a valid combustion endpoint");
    }
    return this.#heldCursor.operatingWeights(this.#normalizeEndpoint(endpoint));
  }

  reset() {
    this.#heldCursor.reset();
    if (this.#directionalCursors !== null) {
      for (const pair of this.#directionalCursors) {
        pair.rising.reset();
        pair.falling.reset();
      }
    }
    this.#envelope?.reset();
    this.#motoringCursor?.reset();
    for (const convolver of this.#convolvers) convolver.reset();
    this.#sharedConvolver?.reset();
    this.#master.reset();
    this.#pendingFrames = 0;
    this.#pendingWarmPrefixFrames = 0;
    this.#active = false;
    this.#audible = false;
    this.#lastEndpoint = null;
    this.#envelopeFrame = 0;
    this.#warmFrameCount = 0;
    this.#audibleInputFrameCount = 0;
    this.#audibleOutputFrameCount = 0;
    this.#transientRisingRenderedBlockCount = 0;
    this.#transientFallingRenderedBlockCount = 0;
    this.#transientRisingActiveFrameCount = 0;
    this.#transientFallingActiveFrameCount = 0;
    this.#transientOverlapFrameCount = 0;
    this.#transientMaximumRisingGain = 0;
    this.#transientMaximumFallingGain = 0;
    this.#transientAdmittedEvents.length = 0;
    this.#tailOnlyInputFrameCount = 0;
    this.#tailOnlyOutputFrameCount = 0;
    this.#combustionTransitionBlockCount = 0;
    this.#motoringRenderedBlockCount = 0;
    this.#runningToMotoringTransitionCount = 0;
    this.#motoringToRunningTransitionCount = 0;
  }

  initialize(endpoint) {
    if (!this.#covers(endpoint)) {
      this.#active = false;
      return Object.freeze({ segmentId: null });
    }
    this.#heldCursor.initialize(this.#normalizeEndpoint(endpoint));
    this.#active = this.#heldCursor.activeSegmentId !== null;
    if (this.#active && this.#directionalCursors !== null) {
      const normalized = this.#normalizeDirectionalEndpoint(endpoint);
      for (const pair of this.#directionalCursors) {
        for (const cursor of [pair.rising, pair.falling]) {
          cursor.initialize(normalized);
          if (cursor.activeSegmentId === null) {
            this.#active = false;
            break;
          }
        }
        if (!this.#active) break;
      }
    }
    this.#lastEndpoint = this.#active ? this.#copyEndpoint(endpoint) : null;
    return Object.freeze({ segmentId: this.activeSegmentId });
  }

  // Re-seed only crank-synchronous source generation. Presentation transfer,
  // pending latency alignment and master state deliberately survive a supported
  // combustion-state transition.
  reseedRunning(endpoint) {
    this.#heldCursor.reset();
    if (this.#directionalCursors !== null) {
      for (const pair of this.#directionalCursors) {
        pair.rising.reset();
        pair.falling.reset();
      }
    }
    this.#envelope?.reset();
    const initialized = this.initialize(endpoint);
    if (this.#active && this.#audible && this.#envelope !== null) {
      this.#envelopeFrame = 0;
      this.#envelope.beginAudible(
        this.#envelopeEndpoint(endpoint, this.#envelopeFrame),
      );
    }
    return initialized;
  }

  get hasMotoringTexture() {
    return this.#motoringCursor !== null;
  }

  motoringCovers(endpoint) {
    return this.#motoringCursor?.covers(endpoint) === true;
  }

  enterMotoring(endpoint) {
    if (this.#motoringCursor === null) {
      return Object.freeze({ segmentId: null });
    }
    return this.#motoringCursor.initialize(endpoint);
  }

  warmBlock({ frameCount, start, end }) {
    this.#requireRenderArguments(frameCount, start, end);
    if (this.#audible) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-warm-after-audible",
        "held texture preparation cannot resume after audible delivery began",
      );
    }
    const dryRoutes = this.#renderDryRoutes(frameCount, start, end);
    let offset = 0;
    while (offset < frameCount) {
      const copied = Math.min(
        frameCount - offset,
        this.#package.batchFrames - this.#pendingFrames,
      );
      for (let routeIndex = 0; routeIndex < dryRoutes.length; ++routeIndex) {
        this.#pendingDryRoutes[routeIndex].set(
          dryRoutes[routeIndex].subarray(offset, offset + copied),
          this.#pendingFrames,
        );
      }
      this.#pendingFrames += copied;
      offset += copied;
      if (this.#pendingFrames === this.#package.batchFrames) {
        this.#processPending(false);
      }
    }
    this.#warmFrameCount += frameCount;
    this.#lastEndpoint = this.#copyEndpoint(end);
  }

  warm(arguments_) {
    this.warmBlock(arguments_);
  }

  beginAudible() {
    if (!this.#active) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-runtime-inactive",
        "held texture runtime has not entered live coverage",
        { recoverable: true },
      );
    }
    if (this.#audible) return;
    this.#pendingWarmPrefixFrames = this.#pendingFrames;
    if (this.#envelope !== null) {
      if (this.#lastEndpoint === null) {
        throw new HeldTexturePresentationRuntimeError(
          "held-texture-missing-audible-seed",
          "held texture runtime has no committed endpoint for audible seeding",
        );
      }
      this.#envelopeFrame = 0;
      this.#envelope.beginAudible(
        this.#envelopeEndpoint(this.#lastEndpoint, this.#envelopeFrame),
      );
    }
    this.#audible = true;
  }

  renderBlock({ sourceBlock, start, end }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (!this.#audible) this.beginAudible();
    this.#requireRenderArguments(sourceBlock.length, start, end);
    const dryRoutes = this.#renderCombinedDryRoutes(
      sourceBlock.length,
      start,
      end,
    );
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#lastEndpoint = this.#copyEndpoint(end);
    return rendered;
  }

  renderCombustionTransitionBlock({
    sourceBlock,
    start,
    end,
    startGain,
    endGain,
  }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (
      !Number.isFinite(startGain) ||
      !Number.isFinite(endGain) ||
      startGain < 0 ||
      startGain > 1 ||
      endGain < 0 ||
      endGain > 1
    ) {
      throw new RangeError("combustion transition gains must lie in [0, 1]");
    }
    if (!this.#audible) this.beginAudible();
    this.#requireRenderArguments(sourceBlock.length, start, end);
    const dryRoutes = this.#renderCombinedDryRoutes(
      sourceBlock.length,
      start,
      end,
    );
    for (const route of dryRoutes) {
      for (let frame = 0; frame < route.length; ++frame) {
        const amount = (frame + 1) / route.length;
        route[frame] *= startGain + (endGain - startGain) * amount;
      }
    }
    ++this.#combustionTransitionBlockCount;
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#lastEndpoint = this.#copyEndpoint(end);
    return rendered;
  }

  renderTailOnlyBlock({ sourceBlock }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (!this.#audible) this.beginAudible();
    const dryRoutes = this.#package.routePresentations.map(
      () => new Float64Array(sourceBlock.length),
    );
    this.#tailOnlyInputFrameCount += sourceBlock.length;
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#tailOnlyOutputFrameCount += rendered.bakedBlock.length;
    return rendered;
  }

  renderMotoringBlock({ sourceBlock, start, end }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (!this.#audible) this.beginAudible();
    const dryRoutes = this.#renderMotoringDryRoutes(
      sourceBlock.length,
      start,
      end,
    );
    ++this.#motoringRenderedBlockCount;
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#lastEndpoint = this.#copyEndpoint(end);
    return rendered;
  }

  renderRunningToMotoringTransitionBlock({
    sourceBlock,
    runningStart,
    runningEnd,
    motoringStart,
    motoringEnd,
  }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (!this.#audible) this.beginAudible();
    this.#requireRenderArguments(sourceBlock.length, runningStart, runningEnd);
    if (this.enterMotoring(motoringStart).segmentId === null) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-motoring-seed-outside-coverage",
        "motoring state texture rejected the ignition-off transition seed",
        { recoverable: true },
      );
    }
    const running = this.#renderCombinedDryRoutes(
      sourceBlock.length,
      runningStart,
      runningEnd,
    );
    const motoring = this.#renderMotoringDryRoutes(
      sourceBlock.length,
      motoringStart,
      motoringEnd,
    );
    const dryRoutes = this.#morphDryRoutes(running, motoring, 0, 1);
    ++this.#runningToMotoringTransitionCount;
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#lastEndpoint = this.#copyEndpoint(motoringEnd);
    return rendered;
  }

  renderMotoringToRunningTransitionBlock({
    sourceBlock,
    motoringStart,
    motoringEnd,
    runningStart,
    runningEnd,
  }) {
    if (!(sourceBlock instanceof Float32Array) || sourceBlock.length === 0) {
      throw new TypeError("live source A must be a nonempty mono Float32Array");
    }
    if (!this.#audible) this.beginAudible();
    const motoring = this.#renderMotoringDryRoutes(
      sourceBlock.length,
      motoringStart,
      motoringEnd,
    );
    if (this.reseedRunning(runningStart).segmentId === null) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-running-restart-seed-outside-coverage",
        "running state texture rejected the rolling ignition restart seed",
        { recoverable: true },
      );
    }
    this.#requireRenderArguments(sourceBlock.length, runningStart, runningEnd);
    const running = this.#renderCombinedDryRoutes(
      sourceBlock.length,
      runningStart,
      runningEnd,
    );
    const dryRoutes = this.#morphDryRoutes(motoring, running, 0, 1);
    ++this.#motoringToRunningTransitionCount;
    const rendered = this.#presentDryRoutes(sourceBlock, dryRoutes);
    this.#lastEndpoint = this.#copyEndpoint(runningEnd);
    return rendered;
  }

  #presentDryRoutes(sourceBlock, dryRoutes) {
    const sourceChunks = [];
    const bakedChunks = [];
    let outputFrames = 0;
    let offset = 0;
    while (offset < sourceBlock.length) {
      const copied = Math.min(
        sourceBlock.length - offset,
        this.#package.batchFrames - this.#pendingFrames,
      );
      this.#pendingSource.set(
        sourceBlock.subarray(offset, offset + copied),
        this.#pendingFrames,
      );
      for (let routeIndex = 0; routeIndex < dryRoutes.length; ++routeIndex) {
        this.#pendingDryRoutes[routeIndex].set(
          dryRoutes[routeIndex].subarray(offset, offset + copied),
          this.#pendingFrames,
        );
      }
      this.#pendingFrames += copied;
      offset += copied;
      if (this.#pendingFrames === this.#package.batchFrames) {
        const rendered = this.#processPending(true);
        sourceChunks.push(rendered.sourceBlock);
        bakedChunks.push(rendered.bakedBlock);
        outputFrames += rendered.sourceBlock.length;
      }
    }
    this.#audibleInputFrameCount += sourceBlock.length;
    this.#audibleOutputFrameCount += outputFrames;
    const alignedSource = concatenate(sourceChunks, outputFrames);
    const baked = concatenate(bakedChunks, outputFrames);
    return Object.freeze({
      sourceBlock: alignedSource,
      bakedBlock: baked,
      source: alignedSource,
      baked,
    });
  }

  flush() {
    if (!this.#audible || this.#pendingFrames === 0) {
      return Object.freeze({
        sourceBlock: EMPTY_FLOAT32,
        bakedBlock: EMPTY_FLOAT32,
        source: EMPTY_FLOAT32,
        baked: EMPTY_FLOAT32,
      });
    }
    const rendered = this.#processPending(true);
    this.#audibleOutputFrameCount += rendered.sourceBlock.length;
    return Object.freeze({
      ...rendered,
      source: rendered.sourceBlock,
      baked: rendered.bakedBlock,
    });
  }

  diagnostics() {
    return Object.freeze({
      active: this.#active,
      audible: this.#audible,
      latencyFrames: this.latencyFrames,
      pendingFrames: this.#pendingFrames,
      pendingWarmPrefixFrames: this.#pendingWarmPrefixFrames,
      warmFrameCount: this.#warmFrameCount,
      audibleInputFrameCount: this.#audibleInputFrameCount,
      audibleOutputFrameCount: this.#audibleOutputFrameCount,
      residualMix: this.#heldCursor.diagnostics().residualMix,
      transientMix: 1,
      outerMinimumRpm: this.#package.minimumRpm,
      outerMaximumRpm: this.#package.maximumRpm,
      heldMinimumRpm: this.#package.heldMinimumRpm,
      heldMaximumRpm: this.#package.heldMaximumRpm,
      heldTexture: this.#heldCursor.diagnostics(),
      transient: Object.freeze({
        enabled: this.#envelope !== null,
        risingRenderedBlockCount:
          this.#transientRisingRenderedBlockCount,
        fallingRenderedBlockCount:
          this.#transientFallingRenderedBlockCount,
        risingActiveFrameCount: this.#transientRisingActiveFrameCount,
        fallingActiveFrameCount: this.#transientFallingActiveFrameCount,
        overlapFrameCount: this.#transientOverlapFrameCount,
        maximumRisingGain: this.#transientMaximumRisingGain,
        maximumFallingGain: this.#transientMaximumFallingGain,
        admittedEvents: Object.freeze([...this.#transientAdmittedEvents]),
        envelope: this.#envelope?.diagnostics() ?? null,
      }),
      combustionStateExperiment: Object.freeze({
        mode: this.#motoringCursor === null
          ? "tail-only-no-motored-pumping"
          : "optional-source-derived-motoring-phase-family",
        transitionBlockCount: this.#combustionTransitionBlockCount,
        tailOnlyInputFrameCount: this.#tailOnlyInputFrameCount,
        tailOnlyOutputFrameCount: this.#tailOnlyOutputFrameCount,
        motoringAvailable: this.#motoringCursor !== null,
        motoringActiveSegmentId:
          this.#motoringCursor?.activeSegmentId ?? null,
        motoringRenderedBlockCount: this.#motoringRenderedBlockCount,
        runningToMotoringTransitionCount:
          this.#runningToMotoringTransitionCount,
        motoringToRunningTransitionCount:
          this.#motoringToRunningTransitionCount,
      }),
      master: this.#master.diagnostics(),
    });
  }

  #covers(endpoint) {
    return validRunningEndpoint(endpoint) &&
      endpoint.rpm >= this.#package.minimumRpm &&
      endpoint.rpm <= this.#package.maximumRpm;
  }

  #normalizeEndpoint(endpoint) {
    return Object.freeze({
      ...endpoint,
      rpm: clamp(
        endpoint.rpm,
        this.#package.heldMinimumRpm,
        this.#package.heldMaximumRpm,
      ),
      stateMask: endpoint.stateMask & ~LIMITER_CUT_STATE_MASK,
    });
  }

  #normalizeDirectionalEndpoint(endpoint) {
    return Object.freeze({
      ...endpoint,
      stateMask: endpoint.stateMask & ~LIMITER_CUT_STATE_MASK,
    });
  }

  #copyEndpoint(endpoint) {
    return Object.freeze({ ...endpoint });
  }

  #envelopeEndpoint(endpoint, frame) {
    return Object.freeze({
      frame,
      requestedThrottle01: endpoint.requestedThrottle01,
      rpmSlopeRpmPerSecond: endpoint.rpmSlopeRpmPerSecond,
    });
  }

  #requireRenderArguments(frameCount, start, end) {
    if (!Number.isSafeInteger(frameCount) || frameCount <= 0) {
      throw new RangeError("held texture render frameCount must be positive");
    }
    if (!this.#active || !this.#covers(start) || !this.#covers(end)) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-presentation-outside-coverage",
        `held texture presentation covers ${this.#package.minimumRpm}..${this.#package.maximumRpm} RPM with ignition and fuel on`,
        { recoverable: true },
      );
    }
  }

  #ensureHeldBuffers(frameCount) {
    if (frameCount <= this.#heldBufferCapacity) return;
    this.#heldBufferCapacity = frameCount;
    this.#heldBuffers = this.#heldCursor.createOutputBuffers(frameCount);
  }

  #ensureDirectionalBuffers(frameCount) {
    if (this.#directionalCursors === null) {
      throw new Error("directional buffers require transient material");
    }
    if (frameCount <= this.#directionalBufferCapacity) return;
    this.#directionalBufferCapacity = frameCount;
    this.#directionalBuffers = this.#directionalCursors.map(
      (pair) => Object.freeze({
        busId: pair.busId,
        rising: pair.rising.createOutputBuffers(frameCount),
        falling: pair.falling.createOutputBuffers(frameCount),
      }),
    );
  }

  #renderHeldRoutes(frameCount, start, end) {
    this.#ensureHeldBuffers(frameCount);
    const normalizedStart = this.#normalizeEndpoint(start);
    const normalizedEnd = this.#normalizeEndpoint(end);
    try {
      this.#heldCursor.renderBlockInto({
        frameCount,
        start: normalizedStart,
        end: normalizedEnd,
        outputBuffers: this.#heldBuffers,
      });
    } catch (error) {
      if (error instanceof HeldPhaseTextureRuntimeError) {
        throw new HeldTexturePresentationRuntimeError(
          error.code,
          error.message,
          { recoverable: error.recoverable },
        );
      }
      throw error;
    }
  }

  #renderDryRoutes(frameCount, start, end) {
    this.#renderHeldRoutes(frameCount, start, end);
    const outputs = this.#heldBuffers.map(() => new Float64Array(frameCount));
    const startCombustionGain =
      (start.stateMask & LIMITER_CUT_STATE_MASK) === 0 ? 1 : 0;
    const endCombustionGain =
      (end.stateMask & LIMITER_CUT_STATE_MASK) === 0 ? 1 : 0;
    for (let frame = 0; frame < frameCount; ++frame) {
      const amount = (frame + 1) / frameCount;
      const combustionGain =
        startCombustionGain +
        (endCombustionGain - startCombustionGain) * amount;
      for (let routeIndex = 0; routeIndex < outputs.length; ++routeIndex) {
        const drySourceUnits = this.#heldBuffers[routeIndex][frame] *
          (this.#package.sharedFullWetTransfer
            ? 1
            : this.#package.capturedToSourceScale) *
          combustionGain;
        if (!Number.isFinite(drySourceUnits)) {
          throw new RangeError(
            `held texture synthesis was non-finite at frame ${frame}, route ${routeIndex}`,
          );
        }
        outputs[routeIndex][frame] = drySourceUnits;
      }
    }
    return outputs;
  }

  #renderDirectionalRoutes(frameCount, start, end, direction) {
    this.#ensureDirectionalBuffers(frameCount);
    const normalizedStart = this.#normalizeDirectionalEndpoint(start);
    const normalizedEnd = this.#normalizeDirectionalEndpoint(end);
    for (let routeIndex = 0; routeIndex < this.#directionalCursors.length; ++routeIndex) {
      const pair = this.#directionalCursors[routeIndex];
      const cursor = pair[direction];
      try {
        cursor.renderBlockInto({
          frameCount,
          start: normalizedStart,
          end: normalizedEnd,
          outputBuffers: this.#directionalBuffers[routeIndex][direction],
        });
      } catch (error) {
        if (error instanceof DirectionalPhaseCellError) {
          throw new HeldTexturePresentationRuntimeError(
            error.code,
            error.message,
            { recoverable: error.recoverable },
          );
        }
        throw error;
      }
    }
  }

  #renderCombinedDryRoutes(frameCount, start, end) {
    if (this.#envelope === null || this.#directionalCursors === null) {
      throw new Error("combined held texture render requires transient material");
    }
    this.#renderHeldRoutes(frameCount, start, end);
    const envelopeStart = this.#envelopeEndpoint(start, this.#envelopeFrame);
    const envelopeEnd = this.#envelopeEndpoint(
      end,
      this.#envelopeFrame + frameCount,
    );
    const envelope = this.#envelope.processBlock({
      frameCount,
      start: envelopeStart,
      end: envelopeEnd,
    });
    if (envelope.admittedEvent !== null) {
      this.#transientAdmittedEvents.push(envelope.admittedEvent);
    }
    this.#envelopeFrame += frameCount;

    let risingActive = false;
    let fallingActive = false;
    for (let frame = 0; frame < frameCount; ++frame) {
      const risingGain = envelope.risingGain[frame];
      const fallingGain = envelope.fallingGain[frame];
      if (
        !Number.isFinite(risingGain) ||
        !Number.isFinite(fallingGain) ||
        risingGain < 0 ||
        fallingGain < 0
      ) {
        throw new RangeError(
          `transient envelope was invalid at frame ${frame}`,
        );
      }
      if (risingGain > 0) {
        risingActive = true;
        ++this.#transientRisingActiveFrameCount;
        this.#transientMaximumRisingGain = Math.max(
          this.#transientMaximumRisingGain,
          risingGain,
        );
      }
      if (fallingGain > 0) {
        fallingActive = true;
        ++this.#transientFallingActiveFrameCount;
        this.#transientMaximumFallingGain = Math.max(
          this.#transientMaximumFallingGain,
          fallingGain,
        );
      }
      if (risingGain > 0 && fallingGain > 0) {
        ++this.#transientOverlapFrameCount;
        throw new RangeError(
          "rising and falling transient envelopes must not overlap",
        );
      }
    }
    if (risingActive) {
      this.#renderDirectionalRoutes(frameCount, start, end, "rising");
      ++this.#transientRisingRenderedBlockCount;
    }
    if (fallingActive) {
      this.#renderDirectionalRoutes(frameCount, start, end, "falling");
      ++this.#transientFallingRenderedBlockCount;
    }

    const outputs = this.#heldBuffers.map(() => new Float64Array(frameCount));
    const startCombustionGain =
      (start.stateMask & LIMITER_CUT_STATE_MASK) === 0 ? 1 : 0;
    const endCombustionGain =
      (end.stateMask & LIMITER_CUT_STATE_MASK) === 0 ? 1 : 0;
    for (let frame = 0; frame < frameCount; ++frame) {
      const risingGain = envelope.risingGain[frame];
      const fallingGain = envelope.fallingGain[frame];
      const amount = (frame + 1) / frameCount;
      const combustionGain =
        startCombustionGain +
        (endCombustionGain - startCombustionGain) * amount;
      for (let routeIndex = 0; routeIndex < outputs.length; ++routeIndex) {
        const steady = this.#heldBuffers[routeIndex][frame];
        let dryPublished = steady;
        if (risingGain > 0) {
          const rising = this.#directionalBuffers[routeIndex].rising[0][frame];
          dryPublished += risingGain * (rising - steady);
        }
        if (fallingGain > 0) {
          const falling = this.#directionalBuffers[routeIndex].falling[0][frame];
          dryPublished += fallingGain * (falling - steady);
        }
        const drySourceUnits = dryPublished *
          (this.#package.sharedFullWetTransfer
            ? 1
            : this.#package.capturedToSourceScale) *
          combustionGain;
        if (!Number.isFinite(drySourceUnits)) {
          throw new RangeError(
            `combined held/transient synthesis was non-finite at frame ${frame}, route ${routeIndex}`,
          );
        }
        outputs[routeIndex][frame] = drySourceUnits;
      }
    }
    return outputs;
  }

  #renderMotoringDryRoutes(frameCount, start, end) {
    if (this.#motoringCursor === null) {
      throw new HeldTexturePresentationRuntimeError(
        "held-texture-motoring-unavailable",
        "responsive package has no source-derived motoring phase family",
        { recoverable: true },
      );
    }
    let captured;
    try {
      captured = this.#motoringCursor.renderBlock({ frameCount, start, end });
    } catch (error) {
      if (error instanceof StatePhaseTextureRuntimeError) {
        throw new HeldTexturePresentationRuntimeError(
          error.code,
          error.message,
          { recoverable: error.recoverable },
        );
      }
      throw error;
    }
    return captured.map((route, routeIndex) => {
      const output = new Float64Array(frameCount);
      for (let frame = 0; frame < frameCount; ++frame) {
        const drySourceUnits = route[frame] *
          (this.#package.sharedFullWetTransfer
            ? 1
            : this.#package.capturedToSourceScale);
        if (!Number.isFinite(drySourceUnits)) {
          throw new RangeError(
            `motoring texture was non-finite at frame ${frame}, route ${routeIndex}`,
          );
        }
        output[frame] = drySourceUnits;
      }
      return output;
    });
  }

  #morphDryRoutes(leftRoutes, rightRoutes, startAmount, endAmount) {
    if (
      leftRoutes.length !== rightRoutes.length ||
      leftRoutes.some(
        (route, index) => route.length !== rightRoutes[index].length,
      )
    ) {
      throw new RangeError("state texture route shapes do not match");
    }
    return leftRoutes.map((left, routeIndex) => {
      const right = rightRoutes[routeIndex];
      const output = new Float64Array(left.length);
      for (let frame = 0; frame < left.length; ++frame) {
        const phase = (frame + 1) / left.length;
        const amount = startAmount + (endAmount - startAmount) * phase;
        output[frame] = left[frame] + (right[frame] - left[frame]) * amount;
      }
      return output;
    });
  }

  #processPending(keepOutput) {
    const frameCount = this.#pendingFrames;
    if (frameCount === 0) {
      throw new RangeError("cannot process an empty held texture batch");
    }
    let sharedWet = null;
    let wetRoutes = null;
    if (this.#sharedConvolver !== null) {
      const combined = new Float64Array(frameCount);
      for (let frame = 0; frame < frameCount; ++frame) {
        let sample = 0;
        for (const routeIndex of this.#package.auditionRouteIndices) {
          sample += this.#pendingDryRoutes[routeIndex][frame];
        }
        combined[frame] = sample * this.#package.capturedToSourceScale;
      }
      sharedWet = this.#sharedConvolver.process(combined);
    } else {
      wetRoutes = this.#convolvers.map((convolver, routeIndex) =>
        convolver.process(
          this.#pendingDryRoutes[routeIndex].slice(0, frameCount),
        )
      );
    }
    const outputStart = keepOutput ? this.#pendingWarmPrefixFrames : frameCount;
    const outputFrameCount = frameCount - outputStart;
    const bakedBlock = keepOutput
      ? new Float32Array(outputFrameCount)
      : null;
    for (let frame = 0; frame < frameCount; ++frame) {
      let levelerMix = sharedWet === null ? null : Math.fround(sharedWet[frame]);
      if (sharedWet === null) {
        for (const routeIndex of this.#package.auditionRouteIndices) {
          const presentation = this.#package.routePresentations[routeIndex];
          const dry = this.#pendingDryRoutes[routeIndex][frame];
          const selected =
            presentation.wetMix01 * wetRoutes[routeIndex][frame] +
            (1 - presentation.wetMix01) * dry;
          const sourceSample = Math.fround(selected);
          if (!Number.isFinite(sourceSample)) {
            throw new RangeError(
              `held presentation selection was non-finite at frame ${frame}, route ${routeIndex}`,
            );
          }
          levelerMix = levelerMix === null
            ? sourceSample
            : Math.fround(levelerMix + sourceSample);
        }
      }
      const mastered = this.#master.process(levelerMix);
      if (keepOutput && frame >= outputStart) {
        bakedBlock[frame - outputStart] = mastered;
      }
    }
    const sourceBlock = keepOutput
      ? this.#pendingSource.slice(outputStart, frameCount)
      : null;
    this.#pendingFrames = 0;
    this.#pendingWarmPrefixFrames = 0;
    return keepOutput ? { sourceBlock, bakedBlock } : null;
  }
}
