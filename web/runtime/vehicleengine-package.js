import { loadHeldTexturePresentationRuntime } from "./held-texture-presentation-runtime.js";
import {
  loadSharedRecordedStarterRuntime,
  resolveSharedRecordedStarterManifestUrl,
} from "./shared-recorded-starter-runtime.js";

const MAGIC = new TextEncoder().encode("VEHENG01");
const CONTAINER_VERSION = 1;
const HEADER_BYTES = 128;
const INDEX_ENTRY_PREFIX_BYTES = 56;
const MAXIMUM_ENTRY_COUNT = 8_192;
const MAXIMUM_PATH_BYTES = 512;
const MAXIMUM_ENTRY_BYTES = 2 ** 30;
const MAXIMUM_CONTAINER_BYTES = 2 ** 32;
const MAXIMUM_DESCRIPTOR_BYTES = 16 * 1024;
const VIRTUAL_ORIGIN = "https://vehicleengine.invalid";
const DESCRIPTOR_PATH = "vehicleengine.json";
const DESCRIPTOR_SCHEMA = "engine-sim-offline/vehicleengine-package";
const verifiedPackages = new WeakMap();

export class VehicleEnginePackageError extends Error {
  constructor(code, message, { path = null } = {}) {
    super(message);
    this.name = "VehicleEnginePackageError";
    this.code = code;
    this.path = path;
  }
}

function fail(code, message, options) {
  throw new VehicleEnginePackageError(code, message, options);
}

function bytes(value) {
  if (value instanceof Uint8Array) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  fail("invalid-argument", "VEHICLEENGINE input must be an ArrayBuffer or byte view");
}

function readSafeU64(view, offset, label) {
  const value = view.getBigUint64(offset, true);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
    fail("resource-limit", `${label} exceeds the JavaScript safe-integer range`);
  }
  return Number(value);
}

function hex(value) {
  return Array.from(value, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

async function digestHex(value, cryptoImplementation, label) {
  if (typeof cryptoImplementation?.subtle?.digest !== "function") {
    fail("crypto-unavailable", `Web Crypto SHA-256 is required for ${label}`);
  }
  return hex(
    new Uint8Array(await cryptoImplementation.subtle.digest("SHA-256", value)),
  );
}

function exactKeys(value, expected, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail("invalid-descriptor", `${label} must be an object`);
  }
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (
    actual.length !== wanted.length ||
    actual.some((key, index) => key !== wanted[index])
  ) {
    fail("invalid-descriptor", `${label} has unknown or missing fields`);
  }
  return value;
}

// JSON.parse keeps only the last value for a repeated member. The native
// authoring parser rejects duplicates, so scan the already syntax-validated
// source and preserve that fail-closed contract in JavaScript as well.
function rejectDuplicateObjectKeys(text) {
  const stack = [];
  for (let index = 0; index < text.length; index += 1) {
    const token = text[index];
    if (token === "{") {
      stack.push({ kind: "object", keys: new Set() });
      continue;
    }
    if (token === "[") {
      stack.push({ kind: "array" });
      continue;
    }
    if (token === "}" || token === "]") {
      stack.pop();
      continue;
    }
    if (token !== '"') continue;

    const start = index;
    for (index += 1; index < text.length; index += 1) {
      if (text[index] === "\\") {
        index += 1;
        continue;
      }
      if (text[index] === '"') break;
    }
    let next = index + 1;
    while (/\s/u.test(text[next] ?? "")) next += 1;
    if (text[next] !== ":") continue;
    const object = stack.at(-1);
    if (object?.kind !== "object") continue;
    const key = JSON.parse(text.slice(start, index + 1));
    if (object.keys.has(key)) {
      fail("invalid-descriptor", `vehicleengine.json repeats object member ${key}`);
    }
    object.keys.add(key);
  }
}

function portableId(value, label) {
  if (
    typeof value !== "string" ||
    !/^[a-z0-9](?:[a-z0-9._-]{0,126}[a-z0-9])?$/u.test(value)
  ) {
    fail("invalid-descriptor", `${label} is not a portable stable ID`);
  }
  return value;
}

function sha256(value, label) {
  if (typeof value !== "string" || !/^[0-9a-f]{64}$/u.test(value)) {
    fail("invalid-descriptor", `${label} must be a lowercase SHA-256 digest`);
  }
  return value;
}

function reservedSegment(segment) {
  const stem = segment.split(".", 1)[0];
  return (
    ["con", "prn", "aux", "nul"].includes(stem) ||
    /^(?:com|lpt)[1-9]$/u.test(stem)
  );
}

export function isPortableVehicleEnginePath(value) {
  if (
    typeof value !== "string" ||
    value.length === 0 ||
    value.length > MAXIMUM_PATH_BYTES ||
    value.startsWith("/") ||
    value.endsWith("/")
  ) {
    return false;
  }
  const segments = value.split("/");
  return segments.every(
    (segment) =>
      segment.length > 0 &&
      segment.length <= 127 &&
      /^[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?$/u.test(segment) &&
      !reservedSegment(segment),
  );
}

function parseDescriptor(entryBytes, entries) {
  let descriptor;
  try {
    const text = new TextDecoder("utf-8", { fatal: true }).decode(entryBytes);
    descriptor = JSON.parse(text);
    rejectDuplicateObjectKeys(text);
  } catch (error) {
    if (error instanceof VehicleEnginePackageError) throw error;
    fail("invalid-descriptor", "vehicleengine.json is not canonical UTF-8 JSON", {
      cause: error,
    });
  }
  exactKeys(descriptor, ["schema", "version", "engine_id", "runtime"], "descriptor");
  if (descriptor.schema !== DESCRIPTOR_SCHEMA || descriptor.version !== 1) {
    fail("invalid-descriptor", "vehicleengine.json schema or version is unsupported");
  }
  portableId(descriptor.engine_id, "descriptor.engine_id");
  const runtime = exactKeys(
    descriptor.runtime,
    ["kind", "manifest_path", "manifest_sha256"],
    "descriptor.runtime",
  );
  if (runtime.kind !== "responsive-audio") {
    fail("invalid-descriptor", "descriptor.runtime.kind is unsupported");
  }
  if (!isPortableVehicleEnginePath(runtime.manifest_path)) {
    fail("invalid-descriptor", "descriptor.runtime.manifest_path is not portable");
  }
  if (runtime.manifest_path === DESCRIPTOR_PATH) {
    fail("invalid-descriptor", "descriptor cannot name itself as its runtime manifest");
  }
  sha256(runtime.manifest_sha256, "descriptor.runtime.manifest_sha256");
  const manifestEntry = entries.get(runtime.manifest_path);
  if (manifestEntry === undefined) {
    fail("invalid-descriptor", "the responsive runtime manifest is absent", {
      path: runtime.manifest_path,
    });
  }
  if (manifestEntry.sha256 !== runtime.manifest_sha256) {
    fail("invalid-descriptor", "responsive runtime manifest identity is stale", {
      path: runtime.manifest_path,
    });
  }
  return Object.freeze({
    schema: descriptor.schema,
    version: descriptor.version,
    engineId: descriptor.engine_id,
    runtime: Object.freeze({
      kind: runtime.kind,
      manifestPath: runtime.manifest_path,
      manifestSha256: runtime.manifest_sha256,
    }),
  });
}

function entryPayload(container, entry) {
  return container.subarray(entry.offset, entry.offset + entry.byteCount);
}

// Fully verifies one complete VEHICLEENGINE v1 carrier before exposing any entry.
export async function loadVehicleEnginePackage(
  input,
  { crypto: cryptoImplementation = globalThis.crypto } = {},
) {
  const source = bytes(input);
  if (source.byteLength > MAXIMUM_CONTAINER_BYTES) {
    fail("resource-limit", "VEHICLEENGINE container exceeds the v1 byte limit");
  }
  // Reject malformed or impossible headers before doubling memory for an owned
  // carrier snapshot. No asynchronous work occurs before the snapshot, so the
  // caller cannot mutate a normal JavaScript buffer between these checks and
  // the copy.
  if (source.byteLength < HEADER_BYTES) {
    fail("malformed-header", "VEHICLEENGINE header is truncated");
  }
  if (MAGIC.some((value, index) => source[index] !== value)) {
    fail("malformed-header", "VEHICLEENGINE magic is invalid");
  }
  const sourceView = new DataView(
    source.buffer,
    source.byteOffset,
    source.byteLength,
  );
  const version = sourceView.getUint16(8, true);
  if (version !== CONTAINER_VERSION) {
    fail("unsupported-version", "VEHICLEENGINE container version is unsupported");
  }
  const headerBytes = sourceView.getUint16(10, true);
  const flags = sourceView.getUint32(12, true);
  const entryCount = sourceView.getUint32(16, true);
  const entryPrefixBytes = sourceView.getUint32(20, true);
  const indexOffset = readSafeU64(sourceView, 24, "index offset");
  const indexBytes = readSafeU64(sourceView, 32, "index byte count");
  const payloadOffset = readSafeU64(sourceView, 40, "payload offset");
  const payloadBytes = readSafeU64(sourceView, 48, "payload byte count");
  const containerBytes = readSafeU64(sourceView, 56, "container byte count");
  if (
    headerBytes !== HEADER_BYTES ||
    flags !== 0 ||
    entryPrefixBytes !== INDEX_ENTRY_PREFIX_BYTES ||
    indexOffset !== HEADER_BYTES
  ) {
    fail("malformed-header", "VEHICLEENGINE v1 fixed header fields are noncanonical");
  }
  if (entryCount < 1 || entryCount > MAXIMUM_ENTRY_COUNT) {
    fail("resource-limit", "VEHICLEENGINE entry count is outside the v1 bounds");
  }
  if (
    payloadOffset !== indexOffset + indexBytes ||
    containerBytes !== payloadOffset + payloadBytes ||
    containerBytes !== source.byteLength
  ) {
    fail("malformed-header", "VEHICLEENGINE ranges do not cover the exact container");
  }
  if (
    indexBytes < entryCount * (INDEX_ENTRY_PREFIX_BYTES + 1) ||
    indexBytes > entryCount * (INDEX_ENTRY_PREFIX_BYTES + MAXIMUM_PATH_BYTES)
  ) {
    fail("resource-limit", "VEHICLEENGINE index size is inconsistent with its entries");
  }

  // Take ownership of immutable-by-convention bytes before any asynchronous
  // hash. All subsequent parsing and virtual fetches use only this snapshot.
  const container = source.slice();
  const view = new DataView(
    container.buffer,
    container.byteOffset,
    container.byteLength,
  );
  const expectedIndexSha256 = hex(container.subarray(64, 96));
  const expectedPayloadSha256 = hex(container.subarray(96, 128));
  const actualIndexSha256 = await digestHex(
    container.subarray(indexOffset, payloadOffset),
    cryptoImplementation,
    "the VEHICLEENGINE index",
  );
  if (actualIndexSha256 !== expectedIndexSha256) {
    fail("index-hash-mismatch", "VEHICLEENGINE index SHA-256 does not match its header");
  }

  const entries = new Map();
  let cursor = indexOffset;
  let expectedEntryOffset = payloadOffset;
  let previousPath = null;
  for (let ordinal = 0; ordinal < entryCount; ordinal += 1) {
    if (cursor + INDEX_ENTRY_PREFIX_BYTES > payloadOffset) {
      fail("malformed-index", "VEHICLEENGINE index entry prefix is truncated");
    }
    const pathBytes = view.getUint16(cursor, true);
    const entryFlags = view.getUint16(cursor + 2, true);
    const reserved = view.getUint32(cursor + 4, true);
    const offset = readSafeU64(view, cursor + 8, "entry offset");
    const byteCount = readSafeU64(view, cursor + 16, "entry byte count");
    const entrySha256 = hex(container.subarray(cursor + 24, cursor + 56));
    const pathStart = cursor + INDEX_ENTRY_PREFIX_BYTES;
    const pathEnd = pathStart + pathBytes;
    if (
      pathBytes < 1 ||
      pathBytes > MAXIMUM_PATH_BYTES ||
      entryFlags !== 0 ||
      reserved !== 0 ||
      pathEnd > payloadOffset ||
      byteCount > MAXIMUM_ENTRY_BYTES ||
      offset !== expectedEntryOffset ||
      offset + byteCount > containerBytes
    ) {
      fail("malformed-index", "VEHICLEENGINE index entry fields or ranges are invalid");
    }
    let path;
    try {
      path = new TextDecoder("utf-8", { fatal: true }).decode(
        container.subarray(pathStart, pathEnd),
      );
    } catch (error) {
      fail("invalid-path", "VEHICLEENGINE entry path is not UTF-8", { cause: error });
    }
    if (!isPortableVehicleEnginePath(path)) {
      fail("invalid-path", "VEHICLEENGINE entry path is not portable", { path });
    }
    if (previousPath !== null && path <= previousPath) {
      fail("noncanonical-index", "VEHICLEENGINE paths are not strictly sorted", {
        path,
      });
    }
    entries.set(
      path,
      Object.freeze({ path, offset, byteCount, sha256: entrySha256 }),
    );
    previousPath = path;
    cursor = pathEnd;
    expectedEntryOffset = offset + byteCount;
  }
  if (cursor !== payloadOffset || expectedEntryOffset !== containerBytes) {
    fail("noncanonical-index", "VEHICLEENGINE index or payload has undeclared bytes");
  }
  const actualPayloadSha256 = await digestHex(
    container.subarray(payloadOffset),
    cryptoImplementation,
    "the VEHICLEENGINE payload",
  );
  if (actualPayloadSha256 !== expectedPayloadSha256) {
    fail("payload-hash-mismatch", "VEHICLEENGINE payload SHA-256 does not match its header");
  }
  for (const entry of entries.values()) {
    const actual = await digestHex(
      entryPayload(container, entry),
      cryptoImplementation,
      entry.path,
    );
    if (actual !== entry.sha256) {
      fail("entry-hash-mismatch", "VEHICLEENGINE entry SHA-256 does not match", {
        path: entry.path,
      });
    }
  }
  const descriptorEntry = entries.get(DESCRIPTOR_PATH);
  if (descriptorEntry === undefined) {
    fail("invalid-descriptor", "VEHICLEENGINE package omits vehicleengine.json");
  }
  if (descriptorEntry.byteCount > MAXIMUM_DESCRIPTOR_BYTES) {
    fail("invalid-descriptor", "vehicleengine.json exceeds its byte limit");
  }
  const descriptor = parseDescriptor(
    entryPayload(container, descriptorEntry),
    entries,
  );
  const package_ = Object.freeze({
    kind: "vehicleengine-package",
    version,
    descriptor,
    entries: Object.freeze(Array.from(entries.values())),
    indexSha256: expectedIndexSha256,
    payloadSha256: expectedPayloadSha256,
  });
  verifiedPackages.set(package_, { container, entries });
  return package_;
}

function pathFromVirtualUrl(value) {
  if (typeof value === "string" && value.includes("%")) return null;
  let url;
  try {
    url = new URL(value);
  } catch {
    return null;
  }
  if (
    url.origin !== VIRTUAL_ORIGIN ||
    url.username !== "" ||
    url.password !== "" ||
    url.search !== "" ||
    url.hash !== "" ||
    !url.pathname.startsWith("/")
  ) {
    return null;
  }
  const encoded = url.pathname.slice(1);
  let path;
  try {
    path = decodeURIComponent(encoded);
  } catch {
    return null;
  }
  if (encodeURI(path) !== encoded || !isPortableVehicleEnginePath(path)) return null;
  return path;
}

export function createVehicleEngineFetch(package_) {
  const verified = verifiedPackages.get(package_);
  if (package_?.kind !== "vehicleengine-package" || verified === undefined) {
    fail("invalid-argument", "a verified VEHICLEENGINE package is required");
  }
  return async function vehicleengineFetch(value) {
    const path = pathFromVirtualUrl(value);
    const entry = path === null ? undefined : verified.entries.get(path);
    if (entry === undefined) {
      return Object.freeze({
        ok: false,
        status: 404,
        redirected: false,
        url: String(value),
        async arrayBuffer() {
          return new ArrayBuffer(0);
        },
      });
    }
    return Object.freeze({
      ok: true,
      status: 200,
      redirected: false,
      url: `${VIRTUAL_ORIGIN}/${path}`,
      async arrayBuffer() {
        const payload = entryPayload(verified.container, entry);
        return payload.buffer.slice(
          payload.byteOffset,
          payload.byteOffset + payload.byteLength,
        );
      },
    });
  };
}

// Loads the proven responsive runtime directly from a fully verified carrier.
export async function loadResponsiveAudioVehicleEngine(
  input,
  { crypto: cryptoImplementation = globalThis.crypto } = {},
) {
  const package_ = await loadVehicleEnginePackage(input, {
    crypto: cryptoImplementation,
  });
  const fetchImplementation = createVehicleEngineFetch(package_);
  const manifestUrl = `${VIRTUAL_ORIGIN}/${package_.descriptor.runtime.manifestPath}`;
  const runtime = await loadHeldTexturePresentationRuntime(manifestUrl, {
    fetch: fetchImplementation,
    crypto: cryptoImplementation,
  });
  if (runtime.manifest.engine !== package_.descriptor.engineId) {
    fail("runtime-identity-mismatch", "responsive runtime engine does not match vehicleengine.json");
  }
  if (runtime.lifecyclePackage === null) {
    return Object.freeze({ package: package_, runtime });
  }
  const sharedPath = runtime.manifest.shared_recorded_starter_package_path;
  if (sharedPath === undefined) {
    return Object.freeze({ package: package_, runtime });
  }
  const sharedUrl = resolveSharedRecordedStarterManifestUrl(
    runtime.manifestUrl,
    sharedPath,
  );
  const sharedRecordedStarterPackage = await loadSharedRecordedStarterRuntime(
    sharedUrl,
    { fetch: fetchImplementation, crypto: cryptoImplementation },
  );
  return Object.freeze({
    package: package_,
    runtime: Object.freeze({ ...runtime, sharedRecordedStarterPackage }),
  });
}
