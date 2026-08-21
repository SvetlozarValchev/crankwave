import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { EngineSimVehicleEngineBaker } from "../../web/runtime/c-api-baker.js";
import { loadResponsiveAudioVehicleEngine } from "../../web/runtime/vehicleengine-package.js";

function digest(bytes) {
  return new Uint8Array(createHash("sha256").update(bytes).digest());
}

async function main() {
  if (process.argv.length !== 10) {
    throw new Error(
      "usage: node smoke_baker.mjs <loader.mjs> <module.wasm> <engine.json> " +
        "<accessory.json> <impulse-response.wav> <starter-runtime.json> " +
        "<starter-audio.f32le> <catalog.json>",
    );
  }
  const [loaderPath, wasmPath, enginePath, accessoryPath, impulseResponsePath,
    starterRuntimePath, starterAudioPath, catalogPath] = process.argv
    .slice(2)
    .map((value) => path.resolve(value));
  const [wasmBytes, engineJson, accessory, impulseResponse, starterRuntime,
    starterAudio, catalog] = await Promise.all([
    readFile(wasmPath),
    readFile(enginePath, "utf8"),
    readFile(accessoryPath),
    readFile(impulseResponsePath),
    readFile(starterRuntimePath),
    readFile(starterAudioPath),
    readFile(catalogPath),
  ]);
  const imported = await import(pathToFileURL(loaderPath).href);
  const module = await imported.default({
    wasmBinary: wasmBytes,
    locateFile(file) {
      return path.join(path.dirname(loaderPath), file);
    },
  });
  const baker = new EngineSimVehicleEngineBaker(module);
  const started = performance.now();
  try {
    const result = baker.bake({
      engineJson,
      assets: [
        { kind: "accessory-configuration", id: "warm-generic-accessories", bytes: accessory },
        { kind: "audio", id: "smooth-39", bytes: impulseResponse },
      ],
      sharedStarterRuntimeJson: starterRuntime,
      sharedStarterAudio: starterAudio,
      releaseIdentity: "1.2.0",
      wasmModuleSha256: digest(wasmBytes),
      assetCatalogSha256: digest(catalog),
    });
    assert.equal(result.engineId, "sequoia-3ur-fe-cleanroom");
    assert.equal(result.heldCellCount, 33);
    assert.equal(result.directionalCaptureCount, 6);
    assert.equal(result.lifecycleCaptureCount, 4);
    assert.equal(result.bytes.byteLength, result.byteCount);
    assert.equal(new TextDecoder().decode(result.bytes.subarray(0, 8)), "VEHENG01");
    const loaded = await loadResponsiveAudioVehicleEngine(result.bytes, {
      crypto: webcrypto,
    });
    assert.equal(loaded.package.descriptor.engineId, result.engineId);
    assert.ok(loaded.runtime.lifecyclePackage !== null);
    assert.ok(loaded.runtime.sharedRecordedStarterPackage !== null);
    process.stdout.write(`${JSON.stringify({
      ok: true,
      elapsed_ms: Math.round(performance.now() - started),
      engine_id: result.engineId,
      container_bytes: result.byteCount,
      entry_count: result.entryCount,
      held_cell_count: result.heldCellCount,
      directional_capture_count: result.directionalCaptureCount,
      lifecycle_capture_count: result.lifecycleCaptureCount,
      container_sha256: result.containerSha256,
      cache_identity_sha256: result.cacheIdentitySha256,
    })}\n`);
  } finally {
    baker.dispose();
  }
}

main().catch((error) => {
  process.stderr.write(`WASM baker smoke failure: ${error.stack ?? error}\n`);
  process.exitCode = 1;
});
