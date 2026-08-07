import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import test from "node:test";

import {
  createRevengineFetch,
  isPortableRevenginePath,
  loadRevenginePackage,
  RevenginePackageError,
} from "../../runtime/revengine-package.js";

const encoder = new TextEncoder();
const MAGIC = encoder.encode("REVENG01");
const HEADER_BYTES = 128;
const PREFIX_BYTES = 56;

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest();
}

function sha256Hex(bytes) {
  return sha256(bytes).toString("hex");
}

function writeU16(bytes, offset, value) {
  new DataView(bytes.buffer).setUint16(offset, value, true);
}

function writeU32(bytes, offset, value) {
  new DataView(bytes.buffer).setUint32(offset, value, true);
}

function writeU64(bytes, offset, value) {
  new DataView(bytes.buffer).setBigUint64(offset, BigInt(value), true);
}

function pack(entries) {
  const ordered = [...entries]
    .map(([path, payload]) => [path, new Uint8Array(payload)])
    .sort(([left], [right]) => left.localeCompare(right));
  const indexBytes = ordered.reduce(
    (total, [path]) => total + PREFIX_BYTES + encoder.encode(path).byteLength,
    0,
  );
  const payloadBytes = ordered.reduce(
    (total, [, payload]) => total + payload.byteLength,
    0,
  );
  const payloadOffset = HEADER_BYTES + indexBytes;
  const bytes = new Uint8Array(payloadOffset + payloadBytes);
  bytes.set(MAGIC);
  writeU16(bytes, 8, 1);
  writeU16(bytes, 10, HEADER_BYTES);
  writeU32(bytes, 12, 0);
  writeU32(bytes, 16, ordered.length);
  writeU32(bytes, 20, PREFIX_BYTES);
  writeU64(bytes, 24, HEADER_BYTES);
  writeU64(bytes, 32, indexBytes);
  writeU64(bytes, 40, payloadOffset);
  writeU64(bytes, 48, payloadBytes);
  writeU64(bytes, 56, bytes.byteLength);
  let indexCursor = HEADER_BYTES;
  let payloadCursor = payloadOffset;
  for (const [path, payload] of ordered) {
    const pathBytes = encoder.encode(path);
    writeU16(bytes, indexCursor, pathBytes.byteLength);
    writeU16(bytes, indexCursor + 2, 0);
    writeU32(bytes, indexCursor + 4, 0);
    writeU64(bytes, indexCursor + 8, payloadCursor);
    writeU64(bytes, indexCursor + 16, payload.byteLength);
    bytes.set(sha256(payload), indexCursor + 24);
    bytes.set(pathBytes, indexCursor + PREFIX_BYTES);
    bytes.set(payload, payloadCursor);
    indexCursor += PREFIX_BYTES + pathBytes.byteLength;
    payloadCursor += payload.byteLength;
  }
  bytes.set(sha256(bytes.subarray(HEADER_BYTES, payloadOffset)), 64);
  bytes.set(sha256(bytes.subarray(payloadOffset)), 96);
  return bytes;
}

function fixture() {
  const manifest = encoder.encode(
    '{"schema":"engine-sim-offline/responsive-audio-preview","engine":"unit-engine"}\n',
  );
  const descriptor = encoder.encode(
    `${JSON.stringify({
      schema: "engine-sim-offline/revengine-package",
      version: 1,
      engine_id: "unit-engine",
      runtime: {
        kind: "responsive-audio",
        manifest_path: "packages/unit-engine/runtime.json",
        manifest_sha256: sha256Hex(manifest),
      },
    })}\n`,
  );
  const starter = encoder.encode("licensed starter fixture");
  return {
    manifest,
    descriptor,
    starter,
    bytes: pack([
      ["revengine.json", descriptor],
      ["packages/unit-engine/runtime.json", manifest],
      ["packages/shared-recorded-starter/runtime.json", starter],
    ]),
  };
}

test("REVENGINE verifies its carrier, descriptor, and virtual package tree", async () => {
  const source = fixture();
  const package_ = await loadRevenginePackage(source.bytes, { crypto: webcrypto });
  assert.equal(package_.kind, "revengine-package");
  assert.equal(package_.descriptor.engineId, "unit-engine");
  assert.equal(
    package_.descriptor.runtime.manifestPath,
    "packages/unit-engine/runtime.json",
  );
  assert.equal(package_.entries.length, 3);

  const fetch = createRevengineFetch(package_);
  const runtime = await fetch(
    "https://revengine.invalid/packages/unit-engine/runtime.json",
  );
  assert.equal(runtime.ok, true);
  assert.deepEqual(new Uint8Array(await runtime.arrayBuffer()), source.manifest);
  const shared = await fetch(
    new URL(
      "../shared-recorded-starter/runtime.json",
      "https://revengine.invalid/packages/unit-engine/runtime.json",
    ).href,
  );
  assert.equal(shared.ok, true);
  assert.deepEqual(new Uint8Array(await shared.arrayBuffer()), source.starter);
  assert.equal((await fetch("https://example.com/runtime.json")).status, 404);
  assert.equal(
    (await fetch("https://revengine.invalid/packages/%2e%2e/revengine.json"))
      .status,
    404,
  );
});

test("REVENGINE rejects aggregate and per-entry payload corruption", async () => {
  const source = fixture();
  const aggregateCorrupt = source.bytes.slice();
  aggregateCorrupt[aggregateCorrupt.length - 1] ^= 0x20;
  await assert.rejects(
    loadRevenginePackage(aggregateCorrupt, { crypto: webcrypto }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "payload-hash-mismatch",
  );

  const entryCorrupt = source.bytes.slice();
  entryCorrupt[entryCorrupt.length - 1] ^= 0x20;
  const payloadOffset = Number(
    new DataView(entryCorrupt.buffer).getBigUint64(40, true),
  );
  entryCorrupt.set(sha256(entryCorrupt.subarray(payloadOffset)), 96);
  await assert.rejects(
    loadRevenginePackage(entryCorrupt, { crypto: webcrypto }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "entry-hash-mismatch",
  );
});

test("REVENGINE rejects stale semantic descriptor bindings", async () => {
  const source = fixture();
  const staleDescriptor = encoder.encode(
    `${JSON.stringify({
      schema: "engine-sim-offline/revengine-package",
      version: 1,
      engine_id: "unit-engine",
      runtime: {
        kind: "responsive-audio",
        manifest_path: "packages/unit-engine/runtime.json",
        manifest_sha256: "0".repeat(64),
      },
    })}\n`,
  );
  const stale = pack([
    ["revengine.json", staleDescriptor],
    ["packages/unit-engine/runtime.json", source.manifest],
    ["packages/shared-recorded-starter/runtime.json", source.starter],
  ]);
  await assert.rejects(
    loadRevenginePackage(stale, { crypto: webcrypto }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "invalid-descriptor" &&
      error.path === "packages/unit-engine/runtime.json",
  );
});

test("REVENGINE rejects duplicate decoded descriptor members", async () => {
  const source = fixture();
  const duplicateDescriptor = encoder.encode(
    `{"schema":"engine-sim-offline/revengine-package",` +
      `"sch\\u0065ma":"engine-sim-offline/revengine-package",` +
      `"version":1,"engine_id":"unit-engine","runtime":{` +
      `"kind":"responsive-audio",` +
      `"manifest_path":"packages/unit-engine/runtime.json",` +
      `"manifest_sha256":"${sha256Hex(source.manifest)}"}}\n`,
  );
  const duplicate = pack([
    ["revengine.json", duplicateDescriptor],
    ["packages/unit-engine/runtime.json", source.manifest],
  ]);
  await assert.rejects(
    loadRevenginePackage(duplicate, { crypto: webcrypto }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "invalid-descriptor" &&
      /repeats object member schema/u.test(error.message),
  );
});

test("REVENGINE enforces the descriptor byte and self-reference bounds", async () => {
  const oversized = pack([
    ["revengine.json", new Uint8Array(16 * 1024 + 1)],
  ]);
  await assert.rejects(
    loadRevenginePackage(oversized, { crypto: webcrypto }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "invalid-descriptor" &&
      /byte limit/u.test(error.message),
  );

  const selfDescriptor = encoder.encode(
    `${JSON.stringify({
      schema: "engine-sim-offline/revengine-package",
      version: 1,
      engine_id: "unit-engine",
      runtime: {
        kind: "responsive-audio",
        manifest_path: "revengine.json",
        manifest_sha256: "0".repeat(64),
      },
    })}\n`,
  );
  await assert.rejects(
    loadRevenginePackage(pack([["revengine.json", selfDescriptor]]), {
      crypto: webcrypto,
    }),
    (error) =>
      error instanceof RevenginePackageError &&
      error.code === "invalid-descriptor" &&
      /cannot name itself/u.test(error.message),
  );
});

test("REVENGINE portable paths match the carrier grammar", () => {
  for (const path of [
    "revengine.json",
    "audio/idle.f32le",
    "packages/unit-engine/runtime.json",
  ]) {
    assert.equal(isPortableRevenginePath(path), true, path);
  }
  for (const path of [
    "",
    "/runtime.json",
    "../runtime.json",
    "safe/../runtime.json",
    "safe\\runtime.json",
    "safe//runtime.json",
    "safe/.hidden",
    "safe/X.json",
    "con/file.bin",
  ]) {
    assert.equal(isPortableRevenginePath(path), false, path);
  }
});
