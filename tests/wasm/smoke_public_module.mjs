import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const EXPECTED_MEMORY_BYTES = 128 * 1024 * 1024;
const PUBLIC_EXPORTS = [
  "_eso_api_version",
  "_eso_get_abi_layout",
  "_eso_context_create",
  "_eso_context_destroy",
  "_eso_context_get_last_error",
  "_eso_context_copy_last_error_text",
  "_eso_context_get_diagnostic",
  "_eso_context_copy_diagnostic_text",
  "_eso_context_get_related_diagnostic",
  "_eso_context_copy_related_diagnostic_text",
  "_eso_compile_engine_json",
  "_eso_destroy_engine",
  "_eso_engine_copy_id",
  "_eso_compile_scenario_json",
  "_eso_destroy_scenario",
  "_eso_scenario_copy_id",
  "_eso_create_session",
  "_eso_destroy_session",
  "_eso_session_get_descriptor",
  "_eso_session_copy_identity",
  "_eso_session_get_forward_gear_descriptor",
  "_eso_session_copy_forward_gear_semantic_id",
  "_eso_session_get_audio_bus_descriptor",
  "_eso_session_copy_audio_bus_id",
  "_eso_session_enqueue_controls",
  "_eso_session_process",
  "_malloc",
  "_free",
];

async function main() {
  if (process.argv.length !== 3) {
    throw new Error("usage: node smoke_public_module.mjs <module.mjs>");
  }
  const modulePath = path.resolve(process.argv[2]);
  const imported = await import(pathToFileURL(modulePath).href);
  assert.equal(typeof imported.default, "function", "module factory is missing");
  const module = await imported.default({
    locateFile(file) {
      return path.join(path.dirname(modulePath), file);
    },
  });

  for (const name of PUBLIC_EXPORTS) {
    assert.equal(typeof module[name], "function", `missing public export ${name}`);
  }
  assert.ok(module.HEAPU8 instanceof Uint8Array, "HEAPU8 is not exported");
  assert.ok(module.HEAPU32 instanceof Uint32Array, "HEAPU32 is not exported");
  assert.equal(
    module.HEAPU8.buffer.byteLength,
    EXPECTED_MEMORY_BYTES,
    "production module does not use fixed 128 MiB memory",
  );
  assert.equal(module._eso_api_version(), 8, "unexpected C ABI version");

  const layout = module._malloc(48);
  const contextOutput = module._malloc(4);
  assert.notEqual(layout, 0, "ABI layout allocation failed");
  assert.notEqual(contextOutput, 0, "context output allocation failed");
  try {
    assert.equal(module._eso_get_abi_layout(layout), 0);
    const words = Array.from(
      module.HEAPU32.subarray(layout >>> 2, (layout >>> 2) + 12),
    );
    assert.deepEqual(words.slice(0, 6), [8, 4, 4, 4, 8, 1]);
    assert.ok(words.slice(6).every((value) => value > 0));

    module.HEAPU32[contextOutput >>> 2] = 0xffffffff;
    assert.equal(module._eso_context_create(7, contextOutput), 1);
    assert.equal(module.HEAPU32[contextOutput >>> 2], 0);

    assert.equal(module._eso_context_create(8, contextOutput), 0);
    const context = module.HEAPU32[contextOutput >>> 2];
    assert.notEqual(context, 0, "context creation returned null");
    assert.equal(module._eso_context_destroy(context), 0);
  } finally {
    module._free(contextOutput);
    module._free(layout);
  }

  process.stdout.write(
    `${JSON.stringify({
      api_version: 8,
      pointer_size: 4,
      size_type_size: 4,
      memory_bytes: EXPECTED_MEMORY_BYTES,
      public_export_count: PUBLIC_EXPORTS.length - 2,
    })}\n`,
  );
}

main().catch((error) => {
  process.stderr.write(`public wasm smoke failure: ${error.stack ?? error}\n`);
  process.exitCode = 1;
});
