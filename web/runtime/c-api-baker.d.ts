export interface CrankwaveBakeAsset {
  readonly kind: "audio" | "accessory-configuration";
  readonly id: string;
  readonly bytes: ArrayBuffer | ArrayBufferView;
}

export interface CrankwaveBakeRequest {
  readonly engineJson: string;
  readonly assets?: readonly CrankwaveBakeAsset[];
  readonly sharedStarterRuntimeJson: ArrayBuffer | ArrayBufferView;
  readonly sharedStarterAudio: ArrayBuffer | ArrayBufferView;
  readonly releaseIdentity: string;
  readonly wasmModuleSha256: ArrayBuffer | ArrayBufferView;
  readonly assetCatalogSha256: ArrayBuffer | ArrayBufferView;
}

export interface BakedCrankwave {
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

export class CrankwaveBaker {
  constructor(module: unknown);
  bake(request: CrankwaveBakeRequest): BakedCrankwave;
  dispose(): void;
}
