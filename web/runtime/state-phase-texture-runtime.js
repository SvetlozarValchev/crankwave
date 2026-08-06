import {
  DirectionalPhaseCellCursor,
  DirectionalPhaseCellError,
  loadDirectionalPhaseCell,
} from "./directional-phase-cell.js";

const RUNTIME_SCHEMA =
  "engine-sim-offline/responsive-audio-state-phase-texture";
const RUNTIME_KIND = "responsive-audio-state-phase-texture";
const CANONICAL_SAMPLE_RATE = 192_000;
const KNOWN_STATE_MASK = 0x1f;

export class StatePhaseTextureRuntimeError extends Error {
  constructor(code, message, { recoverable = false } = {}) {
    super(message);
    this.name = "StatePhaseTextureRuntimeError";
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

function positive(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) {
    fail(`${label} must be positive and finite`);
  }
  return value;
}

function positiveInteger(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    fail(`${label} must be a positive integer`);
  }
  return value;
}

function mask(value, label) {
  if (!Number.isSafeInteger(value) || value < 0 || value > KNOWN_STATE_MASK) {
    fail(`${label} must contain only known engine-state bits`);
  }
  return value;
}

function resolveUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value !== "string" || value.length === 0) {
    fail("state phase texture manifest URL must be nonempty");
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

async function fetchJson(url, fetchImplementation, label) {
  const response = await fetchImplementation(url.href, { cache: "no-store" });
  if (!response?.ok || typeof response.arrayBuffer !== "function") {
    throw new Error(
      `${label} fetch failed: HTTP ${response?.status ?? "?"} for ${url.href}`,
    );
  }
  return JSON.parse(
    new TextDecoder("utf-8", { fatal: true }).decode(
      await response.arrayBuffer(),
    ),
  );
}

function sameIdentity(left, right) {
  return left?.id === right?.id && left?.sha256 === right?.sha256;
}

function stateMatches(package_, endpoint) {
  return endpoint !== null &&
    Number.isSafeInteger(endpoint.stateMask) &&
    (endpoint.stateMask & package_.requiredOnMask) === package_.requiredOnMask &&
    (endpoint.stateMask & package_.requiredOffMask) === 0;
}

export async function loadStatePhaseTextureRuntime(
  manifestUrlValue,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    fail("state phase texture loading requires fetch");
  }
  const manifestUrl = resolveUrl(manifestUrlValue);
  const manifest = object(
    await fetchJson(manifestUrl, fetchImplementation, "state phase runtime"),
    "manifest",
  );
  if (manifest.schema !== RUNTIME_SCHEMA) {
    fail(`unsupported state phase texture schema ${manifest.schema}`);
  }
  string(manifest.id, "manifest.id");
  string(manifest.engine, "manifest.engine");

  const selector = object(manifest.state_selector, "state_selector");
  string(selector.id, "state_selector.id");
  const requiredOnMask = mask(
    selector.required_on_mask,
    "state_selector.required_on_mask",
  );
  const requiredOffMask = mask(
    selector.required_off_mask,
    "state_selector.required_off_mask",
  );
  const wildcardMask = mask(
    selector.wildcard_mask,
    "state_selector.wildcard_mask",
  );
  if (
    (requiredOnMask & requiredOffMask) !== 0 ||
    (requiredOnMask & wildcardMask) !== 0 ||
    (requiredOffMask & wildcardMask) !== 0 ||
    (requiredOnMask | requiredOffMask | wildcardMask) !== KNOWN_STATE_MASK
  ) {
    fail("state selector masks must be disjoint and partition all known bits");
  }

  const audio = object(manifest.audio, "audio");
  const sampleRate = positiveInteger(audio.sample_rate_hz, "audio.sample_rate_hz");
  if (
    sampleRate !== CANONICAL_SAMPLE_RATE ||
    audio.encoding !== "float32le" ||
    audio.channel_layout !== "mono"
  ) {
    fail("state phase texture audio must be canonical mono float32le");
  }

  const dryBusIds = array(manifest.dry_bus_ids, "dry_bus_ids").map(
    (value, index) => string(value, `dry_bus_ids[${index}]`),
  );
  const routeDescriptors = array(
    manifest.route_manifests,
    "route_manifests",
  ).map((value, index) => {
    const route = object(value, `route_manifests[${index}]`);
    return Object.freeze({
      busId: string(route.bus_id, `route_manifests[${index}].bus_id`),
      url: relativeUrl(
        manifestUrl,
        route.manifest_path,
        `route_manifests[${index}].manifest_path`,
      ),
    });
  });
  if (
    dryBusIds.length === 0 ||
    dryBusIds.length !== routeDescriptors.length ||
    new Set(dryBusIds).size !== dryBusIds.length ||
    dryBusIds.some((busId, index) => busId !== routeDescriptors[index].busId)
  ) {
    fail("state phase dry buses and route manifests disagree");
  }

  const domain = object(manifest.domain, "domain");
  const minimumRpm = positive(domain.minimum_rpm, "domain.minimum_rpm");
  const maximumRpm = positive(domain.maximum_rpm, "domain.maximum_rpm");
  if (!(maximumRpm > minimumRpm)) fail("state phase RPM domain is invalid");
  const rpmAnchors = array(domain.rpm_anchors, "domain.rpm_anchors").map(
    (value, index) => positive(value, `domain.rpm_anchors[${index}]`),
  );
  if (
    rpmAnchors.length < 2 ||
    rpmAnchors.some((rpm, index) =>
      rpm < minimumRpm ||
      rpm > maximumRpm ||
      (index > 0 && rpm <= rpmAnchors[index - 1])
    )
  ) {
    fail("state phase RPM anchors must be strictly ascending inside its domain");
  }
  const loadCoordinate = string(domain.load_coordinate, "domain.load_coordinate");
  if (loadCoordinate !== "measured-intake-manifold-pressure-pa-abs") {
    fail(`unsupported state phase load coordinate ${loadCoordinate}`);
  }
  const provenance = object(manifest.provenance, "provenance");
  object(provenance.engine, "provenance.engine");
  object(provenance.renderer_build, "provenance.renderer_build");

  const routes = await Promise.all(
    routeDescriptors.map(async (descriptor, index) => {
      const route = await loadDirectionalPhaseCell(descriptor.url, {
        fetch: fetchImplementation,
        crypto: cryptoImplementation,
      });
      if (
        route.sampleRate !== sampleRate ||
        route.busIds.length !== 1 ||
        route.busIds[0] !== descriptor.busId ||
        route.minimumRpm !== minimumRpm ||
        route.maximumRpm !== maximumRpm ||
        route.loadCoordinate !== loadCoordinate ||
        route.rpmAnchors.length !== rpmAnchors.length ||
        route.rpmAnchors.some((rpm, anchorIndex) => rpm !== rpmAnchors[anchorIndex])
      ) {
        fail(`state phase route ${index} domain does not match its root`);
      }
      if (
        route.manifest.engine !== manifest.engine ||
        !sameIdentity(route.manifest.provenance.engine, provenance.engine) ||
        !sameIdentity(
          route.manifest.provenance.renderer_build,
          provenance.renderer_build,
        )
      ) {
        fail(`state phase route ${index} provenance does not match its root`);
      }
      for (const [cellIndex, cell] of route.manifest.cells.entries()) {
        const masks = cell.capture_fidelity?.state_masks;
        if (
          !Array.isArray(masks) ||
          masks.length !== 1 ||
          masks[0] !== requiredOnMask
        ) {
          fail(
            `state phase route ${index} cell ${cellIndex} does not prove exact authored state ${requiredOnMask}`,
          );
        }
      }
      return route;
    }),
  );
  return Object.freeze({
    kind: RUNTIME_KIND,
    manifestUrl: manifestUrl.href,
    manifest,
    sampleRate,
    dryBusIds: Object.freeze(dryBusIds),
    minimumRpm,
    maximumRpm,
    rpmAnchors: Object.freeze(rpmAnchors),
    loadCoordinate,
    requiredOnMask,
    requiredOffMask,
    wildcardMask,
    routes: Object.freeze(routes),
  });
}

export class StatePhaseTextureCursor {
  #package;
  #routeCursors;
  #buffers = null;
  #capacity = 0;
  #active = false;

  constructor(package_) {
    if (package_?.kind !== RUNTIME_KIND) {
      fail("StatePhaseTextureCursor requires a loaded state phase package");
    }
    this.#package = package_;
    this.#routeCursors = package_.routes.map(
      (route) => new DirectionalPhaseCellCursor(route, {
        requiredStateMask: package_.requiredOnMask,
        forbiddenStateMask: package_.requiredOffMask,
      }),
    );
  }

  get activeSegmentId() {
    return this.#active ? this.#package.manifest.state_selector.id : null;
  }

  covers(endpoint) {
    return stateMatches(this.#package, endpoint) &&
      Number.isFinite(endpoint.rpm) &&
      endpoint.rpm >= this.#package.minimumRpm &&
      endpoint.rpm <= this.#package.maximumRpm;
  }

  reset() {
    for (const cursor of this.#routeCursors) cursor.reset();
    this.#active = false;
  }

  initialize(endpoint) {
    this.#active = this.covers(endpoint);
    if (this.#active) {
      for (const cursor of this.#routeCursors) {
        cursor.initialize(endpoint);
        if (cursor.activeSegmentId === null) {
          this.#active = false;
          break;
        }
      }
    }
    return Object.freeze({ segmentId: this.activeSegmentId });
  }

  renderBlock({ frameCount, start, end }) {
    if (!this.#active || !this.covers(start) || !this.covers(end)) {
      throw new StatePhaseTextureRuntimeError(
        "state-phase-texture-outside-coverage",
        `${this.#package.manifest.state_selector.id} covers ${this.#package.minimumRpm}..${this.#package.maximumRpm} RPM`,
        { recoverable: true },
      );
    }
    if (frameCount > this.#capacity) {
      this.#capacity = frameCount;
      this.#buffers = this.#routeCursors.map(
        (cursor) => cursor.createOutputBuffers(frameCount),
      );
    }
    const output = [];
    for (let routeIndex = 0; routeIndex < this.#routeCursors.length; ++routeIndex) {
      try {
        this.#routeCursors[routeIndex].renderBlockInto({
          frameCount,
          start,
          end,
          outputBuffers: this.#buffers[routeIndex],
        });
      } catch (error) {
        if (error instanceof DirectionalPhaseCellError) {
          throw new StatePhaseTextureRuntimeError(
            error.code,
            error.message,
            { recoverable: error.recoverable },
          );
        }
        throw error;
      }
      const route = new Float64Array(frameCount);
      route.set(this.#buffers[routeIndex][0].subarray(0, frameCount));
      output.push(route);
    }
    return output;
  }
}
