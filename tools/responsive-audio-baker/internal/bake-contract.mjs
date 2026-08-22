import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";

export const CRANKWAVE_MAXIMUM_ENTRY_COUNT = 8_192;
export const CRANKWAVE_MAXIMUM_PATH_BYTES = 512;
export const CRANKWAVE_MAXIMUM_SEGMENT_BYTES = 127;
export const CRANKWAVE_MAXIMUM_ENTRY_BYTES = 2 ** 30;
export const CRANKWAVE_MAXIMUM_CONTAINER_BYTES = 2 ** 32;
const CRANKWAVE_HEADER_BYTES = 128;
const CRANKWAVE_INDEX_ENTRY_PREFIX_BYTES = 56;

function fail(message) {
  throw new Error(message);
}

export function compareCodeUnits(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

export function sha256Hex(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

export function hashLabeledBytes(entries) {
  const hash = createHash("sha256");
  for (const entry of entries) {
    const label = String(entry.label);
    const bytes = Buffer.from(entry.bytes);
    hash.update(Buffer.from(`${Buffer.byteLength(label)}:`, "ascii"));
    hash.update(label, "utf8");
    hash.update(Buffer.from(`:${bytes.byteLength}:`, "ascii"));
    hash.update(bytes);
  }
  return hash.digest("hex");
}

export function hashLabeledFiles(entries) {
  return hashLabeledBytes(entries.map((entry) => ({
    label: entry.label,
    bytes: fs.readFileSync(entry.path),
  })));
}

export function rendererFileIdentity(loaderPath, wasmPath) {
  return Object.freeze({
    loader_sha256: sha256Hex(fs.readFileSync(loaderPath)),
    wasm_sha256: sha256Hex(fs.readFileSync(wasmPath)),
  });
}

export function rendererBytesIdentity(loaderBytes, wasmBytes) {
  return Object.freeze({
    loader_sha256: sha256Hex(loaderBytes),
    wasm_sha256: sha256Hex(wasmBytes),
  });
}

export function portableArtifactToken(kind, authoredId) {
  if (!/^[a-z][a-z0-9-]{0,15}$/u.test(kind)) {
    fail(`artifact token kind is invalid: ${kind}`);
  }
  if (typeof authoredId !== "string" || authoredId.length === 0) {
    fail("artifact token source must be nonempty text");
  }
  return `${kind}-${sha256Hex(Buffer.from(authoredId, "utf8"))}`;
}

export function uniqueArtifactTokens(values, kind, label) {
  const tokens = values.map((value) => portableArtifactToken(kind, value));
  if (new Set(tokens).size !== tokens.length) {
    fail(`${label} produce duplicate portable artifact tokens`);
  }
  return tokens;
}

function isLowerAsciiAlnum(value) {
  return (value >= "a" && value <= "z") || (value >= "0" && value <= "9");
}

function isReservedPortableSegment(segment) {
  const stem = segment.split(".", 1)[0];
  if (["con", "prn", "aux", "nul"].includes(stem)) return true;
  return /^(?:com|lpt)[1-9]$/u.test(stem);
}

export function isPortableCrankwavePath(relativePath) {
  if (
    typeof relativePath !== "string" ||
    relativePath.length === 0 ||
    Buffer.byteLength(relativePath, "utf8") > CRANKWAVE_MAXIMUM_PATH_BYTES ||
    relativePath.startsWith("/") ||
    relativePath.endsWith("/") ||
    relativePath.includes("\\")
  ) {
    return false;
  }
  for (const segment of relativePath.split("/")) {
    if (
      segment.length === 0 ||
      Buffer.byteLength(segment, "utf8") > CRANKWAVE_MAXIMUM_SEGMENT_BYTES ||
      !isLowerAsciiAlnum(segment[0]) ||
      !isLowerAsciiAlnum(segment.at(-1)) ||
      isReservedPortableSegment(segment) ||
      !/^[a-z0-9._-]+$/u.test(segment)
    ) {
      return false;
    }
  }
  return true;
}

function scanRegularTree(root, { readPayloads }) {
  const rootStatus = fs.lstatSync(root);
  if (rootStatus.isSymbolicLink() || !rootStatus.isDirectory()) {
    fail(`package root must be a non-symlink directory: ${root}`);
  }
  const entries = [];
  const visit = (directory, prefix) => {
    for (const name of fs.readdirSync(directory).sort()) {
      const absolute = path.join(directory, name);
      const relative = prefix === "" ? name : `${prefix}/${name}`;
      if (!isPortableCrankwavePath(relative)) {
        fail(`package entry has a nonportable relative path: ${relative}`);
      }
      const status = fs.lstatSync(absolute);
      if (status.isSymbolicLink()) {
        fail(`package tree contains a symlink: ${relative}`);
      }
      if (status.isDirectory()) {
        visit(absolute, relative);
        continue;
      }
      if (!status.isFile()) {
        fail(`package tree contains a nonregular entry: ${relative}`);
      }
      if (entries.length >= CRANKWAVE_MAXIMUM_ENTRY_COUNT) {
        fail("package tree entry count is outside the CRANKWAVE v1 bounds");
      }
      if (status.size > CRANKWAVE_MAXIMUM_ENTRY_BYTES) {
        fail(`package entry exceeds the CRANKWAVE byte limit: ${relative}`);
      }
      const payload = readPayloads ? fs.readFileSync(absolute) : null;
      if (payload !== null && payload.byteLength !== status.size) {
        fail(`package entry changed while it was read: ${relative}`);
      }
      entries.push(Object.freeze({
        path: relative,
        absolute_path: absolute,
        byte_count: status.size,
        ...(payload === null
          ? {}
          : { payload, sha256: sha256Hex(payload) }),
      }));
    }
  };
  visit(root, "");
  return entries;
}

export function fingerprintRegularTree(root) {
  const entries = scanRegularTree(root, { readPayloads: true }).map((entry) =>
    Object.freeze({
      path: entry.path,
      byte_count: entry.byte_count,
      sha256: entry.sha256,
    })
  );
  return Object.freeze({
    entries: Object.freeze(entries),
    aggregate_sha256: sha256Hex(Buffer.from(JSON.stringify(entries))),
  });
}

function exactKeys(value, expected, label) {
  if (
    value === null ||
    typeof value !== "object" ||
    Array.isArray(value) ||
    JSON.stringify(Object.keys(value).sort()) !== JSON.stringify([...expected].sort())
  ) {
    fail(`${label} has an invalid shape`);
  }
}

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
      fail(`crankwave.json repeats object member ${key}`);
    }
    object.keys.add(key);
  }
}

export function validateCrankwavePackageTree(root) {
  const entries = scanRegularTree(root, { readPayloads: false });
  if (entries.length === 0 || entries.length > CRANKWAVE_MAXIMUM_ENTRY_COUNT) {
    fail("package tree entry count is outside the CRANKWAVE v1 bounds");
  }
  let indexBytes = 0n;
  let payloadBytes = 0n;
  const byPath = new Map();
  for (const entry of entries) {
    if (byPath.has(entry.path)) fail(`package tree repeats ${entry.path}`);
    byPath.set(entry.path, entry);
    indexBytes += BigInt(
      CRANKWAVE_INDEX_ENTRY_PREFIX_BYTES + Buffer.byteLength(entry.path, "utf8"),
    );
    payloadBytes += BigInt(entry.byte_count);
  }
  const containerBytes = BigInt(CRANKWAVE_HEADER_BYTES) + indexBytes + payloadBytes;
  if (containerBytes > BigInt(CRANKWAVE_MAXIMUM_CONTAINER_BYTES)) {
    fail("package tree exceeds the CRANKWAVE v1 container byte limit");
  }

  const descriptorEntry = byPath.get("crankwave.json");
  if (descriptorEntry === undefined) fail("package tree omits crankwave.json");
  if (descriptorEntry.byte_count > 16 * 1024) {
    fail("crankwave.json exceeds its byte limit");
  }
  const descriptorBytes = fs.readFileSync(descriptorEntry.absolute_path);
  if (descriptorBytes.byteLength !== descriptorEntry.byte_count) {
    fail("crankwave.json changed while it was read");
  }
  let descriptor;
  try {
    const descriptorText = descriptorBytes.toString("utf8");
    descriptor = JSON.parse(descriptorText);
    rejectDuplicateObjectKeys(descriptorText);
  } catch (error) {
    if (/^crankwave\.json repeats object member /u.test(error?.message ?? "")) {
      throw error;
    }
    throw new Error("crankwave.json is malformed JSON", { cause: error });
  }
  exactKeys(
    descriptor,
    ["schema", "version", "engine_id", "runtime"],
    "crankwave.json",
  );
  exactKeys(
    descriptor.runtime,
    ["kind", "manifest_path", "manifest_sha256"],
    "crankwave.json runtime",
  );
  if (
    descriptor.schema !== "crankwave/crankwave-package" ||
    descriptor.version !== 1 ||
    typeof descriptor.engine_id !== "string" ||
    descriptor.engine_id.length > 128 ||
    !/^[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?$/u.test(descriptor.engine_id) ||
    descriptor.runtime.kind !== "responsive-audio" ||
    descriptor.runtime.manifest_path === "crankwave.json" ||
    !isPortableCrankwavePath(descriptor.runtime.manifest_path) ||
    !/^[0-9a-f]{64}$/u.test(descriptor.runtime.manifest_sha256)
  ) {
    fail("crankwave.json does not satisfy the CRANKWAVE package contract");
  }
  const runtimeEntry = byPath.get(descriptor.runtime.manifest_path);
  if (runtimeEntry === undefined) {
    fail("package tree omits the declared responsive runtime manifest");
  }
  const runtimeBytes = fs.readFileSync(runtimeEntry.absolute_path);
  if (
    runtimeBytes.byteLength !== runtimeEntry.byte_count ||
    sha256Hex(runtimeBytes) !== descriptor.runtime.manifest_sha256
  ) {
    fail("responsive runtime manifest digest differs from crankwave.json");
  }
  return Object.freeze({
    entry_count: entries.length,
    index_byte_count: Number(indexBytes),
    payload_byte_count: Number(payloadBytes),
    container_byte_count: Number(containerBytes),
  });
}

export function reusablePriorPhaseAlignment(priorPackage, currentIdentitySha256) {
  return priorPackage?.provenance?.capture_identity?.sha256 ===
      currentIdentitySha256
    ? priorPackage.phase_alignment ?? null
    : null;
}
