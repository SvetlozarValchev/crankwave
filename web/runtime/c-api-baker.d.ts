export interface VehicleEngineBakeAsset {
  readonly kind: "audio" | "accessory-configuration";
  readonly id: string;
  readonly bytes: ArrayBuffer | ArrayBufferView;
}

export interface VehicleEngineBakeRequest {
  readonly engineJson: string;
  readonly assets?: readonly VehicleEngineBakeAsset[];
  readonly sharedStarterRuntimeJson: ArrayBuffer | ArrayBufferView;
  readonly sharedStarterAudio: ArrayBuffer | ArrayBufferView;
  readonly releaseIdentity: string;
  readonly wasmModuleSha256: ArrayBuffer | ArrayBufferView;
  readonly assetCatalogSha256: ArrayBuffer | ArrayBufferView;
}

export interface BakedVehicleEngine {
  readonly bytes: Uint8Array;
  readonly byteCount: number;
  readonly entryCount: number;
  readonly heldCellCount: number;
  readonly directionalCaptureCount: number;
  readonly lifecycleCaptureCount: number;
  readonly engineId: string;
  readonly profileId: string;
  readonly containerSha256: string;
  readonly cacheIdentitySha256: string;
}

export class EngineSimVehicleEngineBaker {
  constructor(module: unknown);
  bake(request: VehicleEngineBakeRequest): BakedVehicleEngine;
  dispose(): void;
}
