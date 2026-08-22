import path from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";

async function main() {
  if (process.argv.length !== 3) {
    throw new Error("usage: node ReadWasmSourceClosure.mjs crankwave.js");
  }
  const loaderPath = path.resolve(process.argv[2]);
  const imported = await import(pathToFileURL(loaderPath).href);
  if (typeof imported.default !== "function") {
    throw new Error("WASM loader does not export a module factory");
  }
  const module = await imported.default({
    locateFile(file) {
      return path.join(path.dirname(loaderPath), file);
    },
  });
  for (const name of [
    "_malloc",
    "_free",
    "_crankwave_api_version",
    "_crankwave_context_create",
    "_crankwave_context_destroy",
    "_crankwave_renderer_copy_source_closure_sha256",
  ]) {
    if (typeof module[name] !== "function") {
      throw new Error(`WASM renderer is missing ${name}`);
    }
  }
  if (module._crankwave_api_version() !== 10 || !(module.HEAPU8 instanceof Uint8Array)) {
    throw new Error("WASM renderer exposes an incompatible C API");
  }

  const contextOutput = module._malloc(4);
  const closureOutput = module._malloc(32);
  if (contextOutput === 0 || closureOutput === 0) {
    throw new Error("WASM renderer identity allocation failed");
  }
  try {
    module.HEAPU8.fill(0, contextOutput, contextOutput + 4);
    if (module._crankwave_context_create(10, contextOutput) !== 0) {
      throw new Error("WASM renderer context creation failed");
    }
    const context = new DataView(
      module.HEAPU8.buffer,
      contextOutput,
      4,
    ).getUint32(0, true);
    if (context === 0) throw new Error("WASM renderer returned a null context");
    try {
      module.HEAPU8.fill(0, closureOutput, closureOutput + 32);
      if (
        module._crankwave_renderer_copy_source_closure_sha256(
          context,
          closureOutput,
        ) !== 0
      ) {
        throw new Error("WASM renderer source closure is unavailable");
      }
      const digest = module.HEAPU8.subarray(closureOutput, closureOutput + 32);
      process.stdout.write(`${Buffer.from(digest).toString("hex")}\n`);
    } finally {
      if (module._crankwave_context_destroy(context) !== 0) {
        throw new Error("WASM renderer context destruction failed");
      }
    }
  } finally {
    module._free(closureOutput);
    module._free(contextOutput);
  }
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error}\n`);
  process.exitCode = 1;
});
