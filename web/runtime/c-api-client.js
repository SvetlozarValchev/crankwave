import {
  AssetKind,
  ESO_C_API_VERSION,
  ESO_INVALID_HANDLE,
  ESO_SHA256_DIGEST_SIZE,
  Layout,
  SessionExecutionKind,
  Status,
  WASM32_ABI_WORDS,
  statusName,
} from "./c-api-abi.js";
import { EngineSimRuntimeError, readContextError } from "./c-api-errors.js";
import { WasmHeap, asUint8Array } from "./wasm-heap.js";
import { EngineSimSession } from "./c-api-session.js";

const REQUIRED_EXPORTS = Object.freeze([
  "_malloc",
  "_free",
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
  "_eso_engine_copy_provenance_sha256",
  "_eso_renderer_copy_source_closure_sha256",
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
]);

const FIXED_WASM_MEMORY_BYTES = 128 * 1024 * 1024;

function requireSessionExecutionKind(value) {
  if (
    value !== SessionExecutionKind.finiteScenario &&
    value !== SessionExecutionKind.openEnded
  ) {
    throw new TypeError(
      "session execution kind must be finiteScenario or openEnded",
    );
  }
  return value;
}

function assetKind(value) {
  switch (value) {
    case "audio":
    case AssetKind.audio:
      return AssetKind.audio;
    case "accessory-configuration":
    case AssetKind.accessoryConfiguration:
      return AssetKind.accessoryConfiguration;
    default:
      throw new EngineSimRuntimeError(`unsupported asset kind: ${String(value)}`, {
        operation: "compile-engine",
        detailCode: "browser-runtime-unsupported-asset-kind",
        diagnostics: [],
      });
  }
}

function requireJsonText(value, name) {
  if (typeof value !== "string") {
    throw new TypeError(`${name} must be an exact JSON UTF-8 string`);
  }
  return value;
}

function normalizeAssets(assets) {
  if (!Array.isArray(assets)) {
    throw new TypeError("assets must be an array");
  }
  return assets.map((asset, index) => {
    if (typeof asset !== "object" || asset === null) {
      throw new TypeError(`asset ${index} must be an object`);
    }
    if (typeof asset.id !== "string" || asset.id.length === 0) {
      throw new TypeError(`asset ${index} must have a non-empty UTF-8 id`);
    }
    return {
      kind: assetKind(asset.kind),
      id: asset.id,
      bytes: asUint8Array(asset.bytes),
    };
  });
}

export async function loadEngineSimWasm(moduleUrl) {
  const resolvedUrl =
    moduleUrl instanceof URL ? moduleUrl.href : new URL(moduleUrl, import.meta.url).href;
  const imported = await import(resolvedUrl);
  if (typeof imported.default !== "function") {
    throw new EngineSimRuntimeError(
      "engine-sim-offline.js does not export an Emscripten module factory",
      {
        operation: "initialize",
        detailCode: "browser-runtime-module-factory-missing",
        diagnostics: [],
      },
    );
  }
  return imported.default({
    locateFile(path) {
      return new URL(path, resolvedUrl).href;
    },
  });
}

export class CompiledEngineProgram {
  #client;
  #disposed = false;

  constructor(
    client,
    engine,
    scenario,
    session,
    executionKind,
    engineId,
    engineProvenanceSha256,
    rendererSourceSha256,
    scenarioId,
  ) {
    this.#client = client;
    this.engine = engine;
    this.scenario = scenario;
    this.session = session;
    this.executionKind = executionKind;
    this.engineId = engineId;
    this.engineProvenanceSha256 = engineProvenanceSha256;
    this.rendererSourceSha256 = rendererSourceSha256;
    this.scenarioId = scenarioId;
  }

  createSession(executionKind) {
    this.#assertAlive();
    return this.#client.createSession(
      this.scenario,
      requireSessionExecutionKind(executionKind),
    );
  }

  dispose() {
    if (this.#disposed) {
      return;
    }
    this.#disposed = true;
    this.session?.dispose();
    this.session = null;
    this.#client.destroyScenario(this.scenario);
    this.scenario = ESO_INVALID_HANDLE;
    this.#client.destroyEngine(this.engine);
    this.engine = ESO_INVALID_HANDLE;
  }

  #assertAlive() {
    if (this.#disposed) {
      throw new EngineSimRuntimeError("the compiled engine program is disposed", {
        operation: "create-session",
        detailCode: "browser-runtime-program-disposed",
        diagnostics: [],
      });
    }
  }
}

export class EngineSimCapiClient {
  #module;
  #heap;
  #context = 0;
  #disposed = false;

  static async create(moduleUrl) {
    return new EngineSimCapiClient(await loadEngineSimWasm(moduleUrl));
  }

  constructor(module) {
    for (const name of REQUIRED_EXPORTS) {
      if (typeof module?.[name] !== "function") {
        throw new EngineSimRuntimeError(`required WASM export ${name} is missing`, {
          operation: "initialize",
          detailCode: "browser-runtime-wasm-export-missing",
          diagnostics: [],
        });
      }
    }
    this.#module = module;
    this.#heap = new WasmHeap(module);
    this.#admitFrozenAbi();
    const output = this.#heap.allocate(4, "C API context pointer");
    try {
      const status = module._eso_context_create(ESO_C_API_VERSION, output);
      if (status !== Status.ok) {
        throw new EngineSimRuntimeError(
          `creating the C API context failed with ${statusName(status)}`,
          {
            operation: "initialize",
            status,
            statusName: statusName(status),
            diagnostics: [],
          },
        );
      }
      this.#context = this.#heap.view.getUint32(output, true);
      if (this.#context === 0) {
        throw new EngineSimRuntimeError("the C API returned a null context", {
          operation: "initialize",
          detailCode: "browser-runtime-null-context",
          diagnostics: [],
        });
      }
    } finally {
      this.#heap.free(output);
    }
  }

  get module() {
    this.#assertAlive();
    return this.#module;
  }

  get heap() {
    this.#assertAlive();
    return this.#heap;
  }

  get context() {
    this.#assertAlive();
    return this.#context;
  }

  compile(engineJson, scenarioJson, assets, executionKind) {
    this.#assertAlive();
    const normalizedEngineJson = requireJsonText(engineJson, "engineJson");
    const normalizedScenarioJson = requireJsonText(scenarioJson, "scenarioJson");
    const normalizedAssets = normalizeAssets(assets);
    const normalizedExecutionKind =
      requireSessionExecutionKind(executionKind);

    let engine = ESO_INVALID_HANDLE;
    let scenario = ESO_INVALID_HANDLE;
    let session = null;
    try {
      engine = this.#compileEngine(normalizedEngineJson, normalizedAssets);
      const engineId = this.#copyHandleId(
        "_eso_engine_copy_id",
        engine,
        "copy-engine-id",
      );
      const engineProvenanceSha256 =
        this.#copyEngineProvenanceSha256(engine);
      const rendererSourceSha256 = this.#copySha256(
        "_eso_renderer_copy_source_closure_sha256",
        [],
        "copy-renderer-source-closure-sha256",
      );
      scenario = this.#compileScenario(engine, normalizedScenarioJson);
      const scenarioId = this.#copyHandleId(
        "_eso_scenario_copy_id",
        scenario,
        "copy-scenario-id",
      );
      session = this.createSession(scenario, normalizedExecutionKind);
      if (session.descriptor.executionKindCode !== normalizedExecutionKind) {
        throw new EngineSimRuntimeError(
          "the created session reported a different execution kind",
          {
            operation: "create-session",
            detailCode: "browser-runtime-session-execution-kind-mismatch",
            diagnostics: [],
          },
        );
      }
      return new CompiledEngineProgram(
        this,
        engine,
        scenario,
        session,
        normalizedExecutionKind,
        engineId,
        engineProvenanceSha256,
        rendererSourceSha256,
        scenarioId,
      );
    } catch (error) {
      session?.dispose();
      if (scenario !== ESO_INVALID_HANDLE) {
        this.destroyScenario(scenario);
      }
      if (engine !== ESO_INVALID_HANDLE) {
        this.destroyEngine(engine);
      }
      throw error;
    }
  }

  createSession(scenario, executionKind) {
    this.#assertAlive();
    const normalizedExecutionKind =
      requireSessionExecutionKind(executionKind);
    const output = this.#heap.allocate(8, "engine-session handle");
    try {
      const status = this.#module._eso_create_session(
        this.#context,
        scenario,
        normalizedExecutionKind,
        output,
      );
      this.assertStatus(status, "create-session");
      const handle = this.#heap.view.getBigUint64(output, true);
      if (handle === ESO_INVALID_HANDLE) {
        throw new EngineSimRuntimeError("the C API returned an invalid session handle", {
          operation: "create-session",
          detailCode: "browser-runtime-invalid-session-handle",
          diagnostics: [],
        });
      }
      return new EngineSimSession(this, handle);
    } finally {
      this.#heap.free(output);
    }
  }

  destroyEngine(handle) {
    if (handle === ESO_INVALID_HANDLE || this.#disposed) {
      return;
    }
    const status = this.#module._eso_destroy_engine(this.#context, handle);
    this.assertStatus(status, "destroy-engine");
  }

  destroyScenario(handle) {
    if (handle === ESO_INVALID_HANDLE || this.#disposed) {
      return;
    }
    const status = this.#module._eso_destroy_scenario(this.#context, handle);
    this.assertStatus(status, "destroy-scenario");
  }

  assertStatus(status, operation) {
    if (status !== Status.ok) {
      throw readContextError(
        this.#module,
        this.#heap,
        this.#context,
        operation,
        status,
      );
    }
  }

  dispose() {
    if (this.#disposed) {
      return;
    }
    this.#disposed = true;
    if (this.#context !== 0) {
      const status = this.#module._eso_context_destroy(this.#context);
      this.#context = 0;
      if (status !== Status.ok) {
        throw new EngineSimRuntimeError(
          `destroying the C API context failed with ${statusName(status)}`,
          {
            operation: "dispose",
            status,
            statusName: statusName(status),
            diagnostics: [],
          },
        );
      }
    }
  }

  #admitFrozenAbi() {
    if (this.#module._eso_api_version() !== ESO_C_API_VERSION) {
      throw new EngineSimRuntimeError("the WASM module exposes the wrong API version", {
        operation: "initialize",
        detailCode: "browser-runtime-abi-version-mismatch",
        diagnostics: [],
      });
    }
    if (this.#heap.buffer.byteLength !== FIXED_WASM_MEMORY_BYTES) {
      throw new EngineSimRuntimeError(
        `the WASM module has ${this.#heap.buffer.byteLength} bytes of memory; ` +
          `the admitted fixed image has ${FIXED_WASM_MEMORY_BYTES}`,
        {
          operation: "initialize",
          detailCode: "browser-runtime-memory-contract-mismatch",
          diagnostics: [],
        },
      );
    }
    const pointer = this.#heap.allocate(Layout.abiLayout.size, "ABI layout");
    try {
      const status = this.#module._eso_get_abi_layout(pointer);
      if (status !== Status.ok) {
        throw new EngineSimRuntimeError(
          `reading the WASM ABI layout failed with ${statusName(status)}`,
          {
            operation: "initialize",
            status,
            statusName: statusName(status),
            diagnostics: [],
          },
        );
      }
      const words = new Uint32Array(
        this.#heap.buffer,
        pointer,
        WASM32_ABI_WORDS.length,
      );
      for (let index = 0; index < WASM32_ABI_WORDS.length; ++index) {
        if (words[index] !== WASM32_ABI_WORDS[index]) {
          throw new EngineSimRuntimeError(
            `the WASM ABI layout differs at word ${index}: ` +
              `${words[index]} != ${WASM32_ABI_WORDS[index]}`,
            {
              operation: "initialize",
              detailCode: "browser-runtime-abi-layout-mismatch",
              diagnostics: [],
            },
          );
        }
      }
    } finally {
      this.#heap.free(pointer);
    }
  }

  #compileEngine(engineJson, assets) {
    const heap = this.#heap;
    const engineBytes = heap.encodeUtf8(engineJson);
    const engineAllocation = heap.copyIn(engineBytes, "engine JSON");
    const engineView = heap.allocate(Layout.utf8View.size, "engine JSON view");
    const assetArray = heap.allocate(
      Math.max(1, assets.length * Layout.assetPayload.size),
      "asset descriptor array",
    );
    const output = heap.allocate(8, "compiled-engine handle");
    const ownedAssetPointers = [];
    try {
      const view = heap.view;
      view.setUint32(engineView + Layout.utf8View.data, engineAllocation.pointer, true);
      view.setUint32(engineView + Layout.utf8View.bytes, engineBytes.byteLength, true);
      for (let index = 0; index < assets.length; ++index) {
        const asset = assets[index];
        const id = heap.copyIn(heap.encodeUtf8(asset.id), `asset ${index} id`);
        const bytes = heap.copyIn(asset.bytes, `asset ${index} payload`);
        ownedAssetPointers.push(id.pointer, bytes.pointer);
        const base = assetArray + index * Layout.assetPayload.size;
        view.setUint32(base + Layout.assetPayload.kind, asset.kind, true);
        view.setUint32(base + Layout.assetPayload.idData, id.pointer, true);
        view.setUint32(base + Layout.assetPayload.idBytes, id.byteLength, true);
        view.setUint32(base + Layout.assetPayload.payloadData, bytes.pointer, true);
        view.setUint32(base + Layout.assetPayload.payloadBytes, bytes.byteLength, true);
      }
      const status = this.#module._eso_compile_engine_json(
        this.#context,
        engineView,
        assets.length === 0 ? 0 : assetArray,
        assets.length,
        output,
      );
      this.assertStatus(status, "compile-engine");
      const handle = view.getBigUint64(output, true);
      if (handle === ESO_INVALID_HANDLE) {
        throw new EngineSimRuntimeError(
          "the C API returned an invalid compiled-engine handle",
          {
            operation: "compile-engine",
            detailCode: "browser-runtime-invalid-engine-handle",
            diagnostics: [],
          },
        );
      }
      return handle;
    } finally {
      for (let index = ownedAssetPointers.length - 1; index >= 0; --index) {
        heap.free(ownedAssetPointers[index]);
      }
      heap.free(output);
      heap.free(assetArray);
      heap.free(engineView);
      heap.free(engineAllocation.pointer);
    }
  }

  #compileScenario(engine, scenarioJson) {
    const heap = this.#heap;
    const bytes = heap.copyIn(heap.encodeUtf8(scenarioJson), "scenario JSON");
    const input = heap.allocate(Layout.utf8View.size, "scenario JSON view");
    const output = heap.allocate(8, "compiled-scenario handle");
    try {
      const view = heap.view;
      view.setUint32(input + Layout.utf8View.data, bytes.pointer, true);
      view.setUint32(input + Layout.utf8View.bytes, bytes.byteLength, true);
      const status = this.#module._eso_compile_scenario_json(
        this.#context,
        engine,
        input,
        output,
      );
      this.assertStatus(status, "compile-scenario");
      const handle = view.getBigUint64(output, true);
      if (handle === ESO_INVALID_HANDLE) {
        throw new EngineSimRuntimeError(
          "the C API returned an invalid compiled-scenario handle",
          {
            operation: "compile-scenario",
            detailCode: "browser-runtime-invalid-scenario-handle",
            diagnostics: [],
          },
        );
      }
      return handle;
    } finally {
      heap.free(output);
      heap.free(input);
      heap.free(bytes.pointer);
    }
  }

  #copyHandleId(functionName, handle, operation) {
    const heap = this.#heap;
    const buffer = heap.allocate(Layout.mutableUtf8Buffer.size, `${operation} buffer`);
    const outputSize = heap.allocate(4, `${operation} size`);
    try {
      let status = this.#module[functionName](
        this.#context,
        handle,
        buffer,
        outputSize,
      );
      this.assertStatus(status, operation);
      const byteCount = heap.view.getUint32(outputSize, true);
      const text = heap.allocate(byteCount + 1, `${operation} text`);
      try {
        heap.view.setUint32(
          buffer + Layout.mutableUtf8Buffer.data,
          text,
          true,
        );
        heap.view.setUint32(
          buffer + Layout.mutableUtf8Buffer.capacity,
          byteCount + 1,
          true,
        );
        status = this.#module[functionName](
          this.#context,
          handle,
          buffer,
          outputSize,
        );
        this.assertStatus(status, operation);
        return heap.decodeUtf8(text, byteCount);
      } finally {
        heap.free(text);
      }
    } finally {
      heap.free(outputSize);
      heap.free(buffer);
    }
  }

  #copyEngineProvenanceSha256(engine) {
    return this.#copySha256(
      "_eso_engine_copy_provenance_sha256",
      [engine],
      "copy-engine-provenance-sha256",
    );
  }

  #copySha256(functionName, leadingArguments, operation) {
    const heap = this.#heap;
    const output = heap.allocate(
      ESO_SHA256_DIGEST_SIZE,
      `${operation} digest`,
    );
    try {
      const status = this.#module[functionName](
        this.#context,
        ...leadingArguments,
        output,
      );
      this.assertStatus(status, operation);
      const bytes = new Uint8Array(
        heap.buffer,
        output,
        ESO_SHA256_DIGEST_SIZE,
      );
      let lowercaseHex = "";
      for (const byte of bytes) {
        lowercaseHex += byte.toString(16).padStart(2, "0");
      }
      return lowercaseHex;
    } finally {
      heap.free(output);
    }
  }

  #assertAlive() {
    if (this.#disposed) {
      throw new EngineSimRuntimeError("the WASM C API client is disposed", {
        operation: "runtime",
        detailCode: "browser-runtime-client-disposed",
        diagnostics: [],
      });
    }
  }
}
