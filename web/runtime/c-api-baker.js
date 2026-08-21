import {
  AssetKind,
  ESO_C_API_VERSION,
  Layout,
  Status,
  WASM32_ABI_WORDS,
  statusName,
} from "./c-api-abi.js";
import { EngineSimRuntimeError, readContextError } from "./c-api-errors.js";
import { WasmHeap, asUint8Array, withWasmAllocations } from "./wasm-heap.js";

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
  "_eso_bake_vehicleengine",
  "_eso_destroy_vehicleengine",
  "_eso_vehicleengine_get_descriptor",
  "_eso_vehicleengine_copy_identity",
  "_eso_vehicleengine_copy_bytes",
]);

function requireText(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError(`${label} must be a nonempty string`);
  }
  return value;
}

function requireDigest(value, label) {
  const result = asUint8Array(value);
  if (result.byteLength !== 32 || !result.some((byte) => byte !== 0)) {
    throw new TypeError(`${label} must be a nonzero 32-byte SHA-256 digest`);
  }
  return result;
}

function normalizeAsset(asset, index) {
  if (asset === null || typeof asset !== "object") {
    throw new TypeError(`assets[${index}] must be an object`);
  }
  const kind =
    asset.kind === "audio"
      ? AssetKind.audio
      : asset.kind === "accessory-configuration"
        ? AssetKind.accessoryConfiguration
        : null;
  if (kind === null) {
    throw new TypeError(`assets[${index}].kind is unsupported`);
  }
  return Object.freeze({
    kind,
    id: requireText(asset.id, `assets[${index}].id`),
    bytes: asUint8Array(asset.bytes),
  });
}

function writeView(view, pointer, dataOffset, sizeOffset, value) {
  view.setUint32(pointer + dataOffset, value.pointer, true);
  view.setUint32(pointer + sizeOffset, value.byteLength, true);
}

function safeNumber(value, label) {
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new RangeError(`${label} exceeds JavaScript's safe integer range`);
  }
  return Number(value);
}

function hex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export class EngineSimVehicleEngineBaker {
  #module;
  #heap;
  #context = 0;
  #disposed = false;

  constructor(module) {
    for (const name of REQUIRED_EXPORTS) {
      if (typeof module?.[name] !== "function") {
        throw new EngineSimRuntimeError(`required baker WASM export ${name} is missing`, {
          operation: "initialize-baker",
          detailCode: "browser-baker-wasm-export-missing",
          diagnostics: [],
        });
      }
    }
    this.#module = module;
    this.#heap = new WasmHeap(module);
    this.#admitAbi();
    const output = this.#heap.allocate(4, "baker C API context pointer");
    try {
      const status = module._eso_context_create(ESO_C_API_VERSION, output);
      if (status !== Status.ok) {
        throw new EngineSimRuntimeError(
          `creating the baker context failed with ${statusName(status)}`,
          { operation: "initialize-baker", status, statusName: statusName(status) },
        );
      }
      this.#context = this.#heap.view.getUint32(output, true);
      if (this.#context === 0) {
        throw new EngineSimRuntimeError("the baker returned a null context", {
          operation: "initialize-baker",
        });
      }
    } finally {
      this.#heap.free(output);
    }
  }

  bake({
    engineJson,
    assets = [],
    sharedStarterRuntimeJson,
    sharedStarterAudio,
    releaseIdentity,
    wasmModuleSha256,
    assetCatalogSha256,
  }) {
    this.#assertAlive();
    const normalizedAssets = Array.from(assets, normalizeAsset);
    const encodedEngine = this.#heap.encodeUtf8(requireText(engineJson, "engineJson"));
    const encodedRelease = this.#heap.encodeUtf8(
      requireText(releaseIdentity, "releaseIdentity"),
    );
    const starterRuntime = asUint8Array(sharedStarterRuntimeJson);
    const starterAudio = asUint8Array(sharedStarterAudio);
    const moduleDigest = requireDigest(wasmModuleSha256, "wasmModuleSha256");
    const catalogDigest = requireDigest(assetCatalogSha256, "assetCatalogSha256");
    const allocations = [
      { bytes: encodedEngine.byteLength, purpose: "engine JSON" },
      { bytes: encodedRelease.byteLength, purpose: "release identity" },
      { bytes: starterRuntime.byteLength, purpose: "shared starter runtime" },
      { bytes: starterAudio.byteLength, purpose: "shared starter audio" },
      {
        bytes: normalizedAssets.length * Layout.assetPayload.size,
        purpose: "bake asset descriptors",
      },
      { bytes: Layout.vehicleEngineBakeInputs.size, purpose: "bake inputs" },
      { bytes: 8, purpose: "VEHICLEENGINE output handle" },
    ];
    for (const [index, asset] of normalizedAssets.entries()) {
      allocations.push({ bytes: this.#heap.encodeUtf8(asset.id).byteLength, purpose: `asset ${index} ID` });
      allocations.push({ bytes: asset.bytes.byteLength, purpose: `asset ${index} payload` });
    }

    return withWasmAllocations(this.#heap, allocations, (pointers) => {
      const [enginePointer, releasePointer, starterRuntimePointer,
        starterAudioPointer, assetArrayPointer, inputsPointer, outputPointer] = pointers;
      const heap = this.#heap;
      heap.bytes.set(encodedEngine, enginePointer);
      heap.bytes.set(encodedRelease, releasePointer);
      heap.bytes.set(starterRuntime, starterRuntimePointer);
      heap.bytes.set(starterAudio, starterAudioPointer);
      const view = heap.view;
      let allocationIndex = 7;
      for (const [index, asset] of normalizedAssets.entries()) {
        const idBytes = heap.encodeUtf8(asset.id);
        const idPointer = pointers[allocationIndex++];
        const payloadPointer = pointers[allocationIndex++];
        heap.bytes.set(idBytes, idPointer);
        heap.bytes.set(asset.bytes, payloadPointer);
        const descriptor = assetArrayPointer + index * Layout.assetPayload.size;
        view.setUint32(descriptor + Layout.assetPayload.kind, asset.kind, true);
        view.setUint32(descriptor + Layout.assetPayload.idData, idPointer, true);
        view.setUint32(descriptor + Layout.assetPayload.idBytes, idBytes.byteLength, true);
        view.setUint32(descriptor + Layout.assetPayload.payloadData, payloadPointer, true);
        view.setUint32(descriptor + Layout.assetPayload.payloadBytes, asset.bytes.byteLength, true);
      }

      const layout = Layout.vehicleEngineBakeInputs;
      writeView(view, inputsPointer, layout.engineJsonData, layout.engineJsonBytes,
        { pointer: enginePointer, byteLength: encodedEngine.byteLength });
      view.setUint32(inputsPointer + layout.assets, assetArrayPointer, true);
      view.setUint32(inputsPointer + layout.assetCount, normalizedAssets.length, true);
      writeView(view, inputsPointer, layout.starterRuntimeData, layout.starterRuntimeBytes,
        { pointer: starterRuntimePointer, byteLength: starterRuntime.byteLength });
      writeView(view, inputsPointer, layout.starterAudioData, layout.starterAudioBytes,
        { pointer: starterAudioPointer, byteLength: starterAudio.byteLength });
      writeView(view, inputsPointer, layout.releaseIdentityData, layout.releaseIdentityBytes,
        { pointer: releasePointer, byteLength: encodedRelease.byteLength });
      heap.bytes.set(moduleDigest, inputsPointer + layout.wasmModuleSha256);
      heap.bytes.set(catalogDigest, inputsPointer + layout.assetCatalogSha256);

      const status = this.#module._eso_bake_vehicleengine(
        this.#context,
        inputsPointer,
        outputPointer,
      );
      if (status !== Status.ok) {
        throw readContextError(
          this.#module,
          this.#heap,
          this.#context,
          "bake VEHICLEENGINE",
          status,
        );
      }
      const vehicleEngine = this.#heap.view.getBigUint64(outputPointer, true);
      if (vehicleEngine === 0n) {
        throw new EngineSimRuntimeError("the baker returned an invalid handle", {
          operation: "bake VEHICLEENGINE",
        });
      }
      try {
        return this.#copyResult(vehicleEngine);
      } finally {
        const destroyStatus = this.#module._eso_destroy_vehicleengine(
          this.#context,
          vehicleEngine,
        );
        if (destroyStatus !== Status.ok) {
          throw readContextError(
            this.#module,
            this.#heap,
            this.#context,
            "destroy VEHICLEENGINE",
            destroyStatus,
          );
        }
      }
    });
  }

  dispose() {
    if (this.#disposed) return;
    this.#disposed = true;
    if (this.#context !== 0) {
      const status = this.#module._eso_context_destroy(this.#context);
      this.#context = 0;
      if (status !== Status.ok) {
        throw new EngineSimRuntimeError(
          `destroying the baker context failed with ${statusName(status)}`,
          { operation: "dispose-baker", status, statusName: statusName(status) },
        );
      }
    }
  }

  #copyResult(vehicleEngine) {
    const descriptorLayout = Layout.vehicleEngineDescriptor;
    return withWasmAllocations(
      this.#heap,
      [{ bytes: descriptorLayout.size, purpose: "VEHICLEENGINE descriptor" }],
      ([descriptorPointer]) => {
        let status = this.#module._eso_vehicleengine_get_descriptor(
          this.#context,
          vehicleEngine,
          descriptorPointer,
        );
        if (status !== Status.ok) {
          throw readContextError(this.#module, this.#heap, this.#context,
            "read VEHICLEENGINE descriptor", status);
        }
        const view = this.#heap.view;
        const byteCount = safeNumber(
          view.getBigUint64(descriptorPointer + descriptorLayout.containerBytes, true),
          "VEHICLEENGINE byte count",
        );
        const engineIdBytes = view.getUint32(
          descriptorPointer + descriptorLayout.engineIdBytes,
          true,
        );
        const profileIdBytes = view.getUint32(
          descriptorPointer + descriptorLayout.profileIdBytes,
          true,
        );
        const metadata = Object.freeze({
          byteCount,
          entryCount: safeNumber(view.getBigUint64(descriptorPointer + descriptorLayout.entryCount, true), "entry count"),
          heldCellCount: safeNumber(view.getBigUint64(descriptorPointer + descriptorLayout.heldCellCount, true), "held cell count"),
          directionalCaptureCount: safeNumber(view.getBigUint64(descriptorPointer + descriptorLayout.directionalCaptureCount, true), "directional capture count"),
          lifecycleCaptureCount: safeNumber(view.getBigUint64(descriptorPointer + descriptorLayout.lifecycleCaptureCount, true), "lifecycle capture count"),
          containerSha256: hex(this.#heap.bytes.subarray(
            descriptorPointer + descriptorLayout.containerSha256,
            descriptorPointer + descriptorLayout.containerSha256 + 32,
          )),
          cacheIdentitySha256: hex(this.#heap.bytes.subarray(
            descriptorPointer + descriptorLayout.cacheIdentitySha256,
            descriptorPointer + descriptorLayout.cacheIdentitySha256 + 32,
          )),
        });
        return withWasmAllocations(
          this.#heap,
          [
            { bytes: engineIdBytes + 1, purpose: "baked engine ID" },
            { bytes: profileIdBytes + 1, purpose: "bake profile ID" },
            { bytes: Layout.vehicleEngineIdentityBuffers.size, purpose: "bake identity buffers" },
            { bytes: byteCount, purpose: "VEHICLEENGINE carrier copy" },
            { bytes: 4, purpose: "VEHICLEENGINE byte count output" },
          ],
          ([engineIdPointer, profileIdPointer, identityPointer, bytesPointer,
            byteCountPointer]) => {
            const identity = Layout.vehicleEngineIdentityBuffers;
            const currentView = this.#heap.view;
            currentView.setUint32(identityPointer + identity.engineIdData, engineIdPointer, true);
            currentView.setUint32(identityPointer + identity.engineIdCapacity, engineIdBytes + 1, true);
            currentView.setUint32(identityPointer + identity.profileIdData, profileIdPointer, true);
            currentView.setUint32(identityPointer + identity.profileIdCapacity, profileIdBytes + 1, true);
            status = this.#module._eso_vehicleengine_copy_identity(
              this.#context,
              vehicleEngine,
              identityPointer,
            );
            if (status !== Status.ok) {
              throw readContextError(this.#module, this.#heap, this.#context,
                "copy VEHICLEENGINE identity", status);
            }
            status = this.#module._eso_vehicleengine_copy_bytes(
              this.#context,
              vehicleEngine,
              bytesPointer,
              byteCount,
              byteCountPointer,
            );
            if (status !== Status.ok) {
              throw readContextError(this.#module, this.#heap, this.#context,
                "copy VEHICLEENGINE carrier", status);
            }
            const written = this.#heap.view.getUint32(byteCountPointer, true);
            if (written !== byteCount) {
              throw new EngineSimRuntimeError("the baker copied an incomplete carrier", {
                operation: "copy VEHICLEENGINE carrier",
              });
            }
            return Object.freeze({
              ...metadata,
              engineId: this.#heap.decodeUtf8(engineIdPointer, engineIdBytes),
              profileId: this.#heap.decodeUtf8(profileIdPointer, profileIdBytes),
              bytes: this.#heap.copyOut(bytesPointer, byteCount),
            });
          },
        );
      },
    );
  }

  #admitAbi() {
    if (this.#module._eso_api_version() !== ESO_C_API_VERSION) {
      throw new EngineSimRuntimeError("the baker C API version is incompatible", {
        operation: "initialize-baker",
      });
    }
    const pointer = this.#heap.allocate(Layout.abiLayout.size, "baker ABI layout");
    try {
      const status = this.#module._eso_get_abi_layout(pointer);
      const words = Array.from(
        this.#module.HEAPU32.subarray(pointer >>> 2, (pointer >>> 2) + 12),
      );
      if (status !== Status.ok ||
          words.some((word, index) => word !== WASM32_ABI_WORDS[index])) {
        throw new EngineSimRuntimeError("the baker wasm32 ABI layout is incompatible", {
          operation: "initialize-baker",
        });
      }
    } finally {
      this.#heap.free(pointer);
    }
  }

  #assertAlive() {
    if (this.#disposed || this.#context === 0) {
      throw new EngineSimRuntimeError("the VEHICLEENGINE baker is disposed", {
        operation: "bake VEHICLEENGINE",
      });
    }
  }
}
