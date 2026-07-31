import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const OUTPUT_CAPACITY = 4 * 1024 * 1024;
const EXPECTED_WASM_MEMORY_BYTES = 128 * 1024 * 1024;
const MAGIC = Buffer.from([0x45, 0x53, 0x4f, 0x57, 0x50, 0x41, 0x52, 0x00]);

function fail(message) {
  throw new Error(message);
}

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function parseBundle(bytes, label) {
  if (bytes.length < 24 || !bytes.subarray(0, 8).equals(MAGIC)) {
    fail(`${label}: parity bundle has an invalid header`);
  }
  const version = bytes.readUInt32LE(8);
  const metadataSize = bytes.readUInt32LE(12);
  const numericCount = bytes.readUInt32LE(16);
  const pcmCount = bytes.readUInt32LE(20);
  if (version !== 1) {
    fail(`${label}: unsupported parity bundle version ${version}`);
  }
  const metadataBegin = 24;
  const numericBegin = metadataBegin + metadataSize;
  const pcmBegin = numericBegin + numericCount * 8;
  const end = pcmBegin + pcmCount * 4;
  if (
    metadataBegin > numericBegin ||
    numericBegin > pcmBegin ||
    pcmBegin > end ||
    end !== bytes.length
  ) {
    fail(`${label}: parity bundle extents are inconsistent`);
  }

  let metadata;
  try {
    metadata = JSON.parse(bytes.toString("utf8", metadataBegin, numericBegin));
  } catch (error) {
    fail(`${label}: parity metadata is not valid JSON: ${error.message}`);
  }
  const numericBytes = bytes.subarray(numericBegin, pcmBegin);
  const pcmBytes = bytes.subarray(pcmBegin, end);
  const numeric = new Float64Array(numericCount);
  const pcm = new Float32Array(pcmCount);
  for (let index = 0; index < numericCount; ++index) {
    numeric[index] = numericBytes.readDoubleLE(index * 8);
  }
  for (let index = 0; index < pcmCount; ++index) {
    pcm[index] = pcmBytes.readFloatLE(index * 4);
  }
  return { bytes, metadata, numericBytes, pcmBytes, numeric, pcm };
}

function compareNumeric(nativeValues, wasmValues, label) {
  assert.equal(
    nativeValues.length,
    wasmValues.length,
    `${label}: sample count differs`,
  );
  let maximumAbsoluteError = 0;
  let maximumRelativeError = 0;
  let squaredError = 0;
  let finiteCount = 0;
  for (let index = 0; index < nativeValues.length; ++index) {
    const nativeValue = nativeValues[index];
    const wasmValue = wasmValues[index];
    if (!Number.isFinite(nativeValue) || !Number.isFinite(wasmValue)) {
      if (
        !(
          (Number.isNaN(nativeValue) && Number.isNaN(wasmValue)) ||
          nativeValue === wasmValue
        )
      ) {
        fail(`${label}: non-finite value differs at sample ${index}`);
      }
      continue;
    }
    const error = Math.abs(nativeValue - wasmValue);
    const scale = Math.max(1, Math.abs(nativeValue), Math.abs(wasmValue));
    maximumAbsoluteError = Math.max(maximumAbsoluteError, error);
    maximumRelativeError = Math.max(maximumRelativeError, error / scale);
    squaredError += error * error;
    ++finiteCount;
  }
  return {
    maximum_absolute_error: maximumAbsoluteError,
    maximum_relative_error: maximumRelativeError,
    rms_error: finiteCount === 0 ? 0 : Math.sqrt(squaredError / finiteCount),
  };
}

function allocate(module, bytes) {
  const extent = Math.max(1, bytes.length);
  const pointer = module._malloc(extent);
  if (pointer === 0) {
    fail(`wasm allocation of ${extent} bytes failed`);
  }
  module.HEAPU8.set(bytes, pointer);
  return pointer;
}

async function runWasm(modulePath, inputs) {
  const imported = await import(pathToFileURL(modulePath).href);
  if (typeof imported.default !== "function") {
    fail("Emscripten parity module has no default factory export");
  }
  const module = await imported.default({
    locateFile(file) {
      return path.join(path.dirname(modulePath), file);
    },
  });
  if (
    typeof module._eso_wasm_parity_run !== "function" ||
    typeof module._malloc !== "function" ||
    typeof module._free !== "function" ||
    !(module.HEAPU8 instanceof Uint8Array)
  ) {
    fail("Emscripten parity module is missing a required public export");
  }
  if (module.HEAPU8.buffer.byteLength !== EXPECTED_WASM_MEMORY_BYTES) {
    fail(
      `wasm memory is ${module.HEAPU8.buffer.byteLength} bytes; expected fixed ` +
        `${EXPECTED_WASM_MEMORY_BYTES}`,
    );
  }

  const allocations = [];
  try {
    const pointers = inputs.map((value) => {
      const pointer = allocate(module, value);
      allocations.push(pointer);
      return pointer;
    });
    const output = module._malloc(OUTPUT_CAPACITY);
    const outputSize = module._malloc(4);
    if (output === 0 || outputSize === 0) {
      fail("wasm output allocation failed inside the fixed 128 MiB memory");
    }
    allocations.push(output, outputSize);
    module.HEAPU32[outputSize >>> 2] = 0;

    const status = module._eso_wasm_parity_run(
      pointers[0],
      inputs[0].length,
      pointers[1],
      inputs[1].length,
      pointers[2],
      inputs[2].length,
      pointers[3],
      inputs[3].length,
      pointers[4],
      inputs[4].length,
      pointers[5],
      inputs[5].length,
      output,
      OUTPUT_CAPACITY,
      outputSize,
    );
    if (status !== 0) {
      fail(`wasm parity driver failed with status ${status}`);
    }
    const extent = module.HEAPU32[outputSize >>> 2];
    if (extent > OUTPUT_CAPACITY) {
      fail(`wasm parity driver returned impossible extent ${extent}`);
    }
    return Buffer.from(module.HEAPU8.slice(output, output + extent));
  } finally {
    for (let index = allocations.length - 1; index >= 0; --index) {
      module._free(allocations[index]);
    }
  }
}

function readExpectations(file) {
  const parsed = JSON.parse(fs.readFileSync(file, "utf8"));
  if (parsed.format !== "engine-sim-offline-wasm-parity-expectations-v1") {
    fail("parity expectation file has an unsupported format");
  }
  return parsed;
}

function requireHash(actualBytes, expected, label) {
  const actual = sha256(actualBytes);
  assert.equal(actual, expected, `${label}: SHA-256 changed`);
  return actual;
}

function semanticBytes(metadata) {
  return Buffer.from(JSON.stringify(metadata.semantic), "utf8");
}

async function main() {
  if (process.argv.length !== 12) {
    fail(
      "usage: node compare_parity.mjs <module.mjs> <native.bundle> " +
        "<engine.json> <scenario.json> <ir-id> <ir.wav> " +
        "<accessory-id> <accessory.json> <wasm.bundle> <expectations.json>",
    );
  }
  const [
    modulePath,
    nativeBundlePath,
    enginePath,
    scenarioPath,
    irId,
    irPath,
    accessoryId,
    accessoryPath,
    wasmBundlePath,
    expectationPath,
  ] = process.argv.slice(2);

  const engine = fs.readFileSync(enginePath);
  const scenario = fs.readFileSync(scenarioPath);
  const ir = fs.readFileSync(irPath);
  const accessory = fs.readFileSync(accessoryPath);
  const inputs = [
    engine,
    scenario,
    Buffer.from(irId, "utf8"),
    ir,
    Buffer.from(accessoryId, "utf8"),
    accessory,
  ];
  const nativeBundleBytes = fs.readFileSync(nativeBundlePath);
  const wasmBundleBytes = await runWasm(path.resolve(modulePath), inputs);
  fs.writeFileSync(wasmBundlePath, wasmBundleBytes);

  const nativeBundle = parseBundle(nativeBundleBytes, "native");
  const wasmBundle = parseBundle(wasmBundleBytes, "wasm");
  const measuring = expectationPath === "-";
  const expectations = measuring ? null : readExpectations(expectationPath);

  assert.equal(nativeBundle.metadata.format, wasmBundle.metadata.format);
  assert.deepEqual(
    nativeBundle.metadata.semantic,
    wasmBundle.metadata.semantic,
    "native and wasm topology, controls, clocks, and discrete state differ",
  );
  assert.equal(nativeBundle.metadata.abi.api_version, 4);
  assert.equal(wasmBundle.metadata.abi.api_version, 4);
  assert.equal(nativeBundle.metadata.abi.pointer_size, 8);
  assert.equal(nativeBundle.metadata.abi.size_type_size, 8);
  assert.equal(wasmBundle.metadata.abi.pointer_size, 4);
  assert.equal(wasmBundle.metadata.abi.size_type_size, 4);
  for (const abi of [nativeBundle.metadata.abi, wasmBundle.metadata.abi]) {
    assert.equal(abi.float_size, 4);
    assert.equal(abi.double_size, 8);
    assert.equal(abi.little_endian, true);
  }

  const numericError = compareNumeric(
    nativeBundle.numeric,
    wasmBundle.numeric,
    "telemetry",
  );
  const pcmError = compareNumeric(
    nativeBundle.pcm,
    wasmBundle.pcm,
    "audition PCM",
  );
  if (!measuring) {
    assert.equal(
      nativeBundle.pcm.length,
      expectations.pcm_sample_count,
      "audition PCM fixture extent changed",
    );
    assert.ok(
      numericError.maximum_absolute_error <=
        expectations.bounds.telemetry_maximum_absolute_error,
      `telemetry maximum absolute error ${numericError.maximum_absolute_error} ` +
        "exceeds its parity bound",
    );
    assert.ok(
      numericError.maximum_relative_error <=
        expectations.bounds.telemetry_maximum_relative_error,
      `telemetry maximum relative error ${numericError.maximum_relative_error} ` +
        "exceeds its parity bound",
    );
    assert.ok(
      pcmError.maximum_absolute_error <=
        expectations.bounds.pcm_maximum_absolute_error,
      `PCM maximum absolute error ${pcmError.maximum_absolute_error} exceeds its parity bound`,
    );
    assert.ok(
      pcmError.rms_error <= expectations.bounds.pcm_rms_error,
      `PCM RMS error ${pcmError.rms_error} exceeds its parity bound`,
    );

    requireHash(engine, expectations.inputs.engine_json_sha256, "engine JSON");
    requireHash(
      scenario,
      expectations.inputs.scenario_json_sha256,
      "scenario JSON",
    );
    requireHash(
      ir,
      expectations.inputs.impulse_response_sha256,
      "impulse response",
    );
    requireHash(
      accessory,
      expectations.inputs.accessory_configuration_sha256,
      "accessory configuration",
    );
    requireHash(
      semanticBytes(nativeBundle.metadata),
      expectations.semantic_sha256,
      "semantic transcript",
    );
    requireHash(
      nativeBundle.numericBytes,
      expectations.native.telemetry_f64le_sha256,
      "native telemetry",
    );
    requireHash(
      nativeBundle.pcmBytes,
      expectations.native.audition_f32le_sha256,
      "native PCM",
    );
    requireHash(
      wasmBundle.numericBytes,
      expectations.wasm.telemetry_f64le_sha256,
      "wasm telemetry",
    );
    requireHash(
      wasmBundle.pcmBytes,
      expectations.wasm.audition_f32le_sha256,
      "wasm PCM",
    );
  }

  process.stdout.write(
    `${JSON.stringify({
      native_bundle_sha256: sha256(nativeBundleBytes),
      wasm_bundle_sha256: sha256(wasmBundleBytes),
      telemetry: numericError,
      pcm: pcmError,
      semantic_sha256: sha256(semanticBytes(nativeBundle.metadata)),
      pcm_sample_count: nativeBundle.pcm.length,
      measured_hashes: measuring
        ? {
            inputs: {
              engine_json_sha256: sha256(engine),
              scenario_json_sha256: sha256(scenario),
              impulse_response_sha256: sha256(ir),
              accessory_configuration_sha256: sha256(accessory),
            },
            native: {
              telemetry_f64le_sha256: sha256(nativeBundle.numericBytes),
              audition_f32le_sha256: sha256(nativeBundle.pcmBytes),
            },
            wasm: {
              telemetry_f64le_sha256: sha256(wasmBundle.numericBytes),
              audition_f32le_sha256: sha256(wasmBundle.pcmBytes),
            },
          }
        : undefined,
    })}\n`,
  );
}

main().catch((error) => {
  process.stderr.write(`wasm parity failure: ${error.stack ?? error}\n`);
  process.exitCode = 1;
});
