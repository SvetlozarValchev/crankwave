import { ESO_CANONICAL_SAMPLE_RATE } from "./c-api-abi.js";
import { AudioPackageLoadError } from "./audio-package-manifest.js";

function fail(code, path, message) {
  throw new AudioPackageLoadError(code, path, message);
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function fourCc(bytes, offset) {
  return String.fromCharCode(
    bytes[offset],
    bytes[offset + 1],
    bytes[offset + 2],
    bytes[offset + 3],
  );
}

function requireWaveBytes(bytes, path) {
  if (!(bytes instanceof Uint8Array)) {
    throw new TypeError(`${path} must be Uint8Array`);
  }
  if (bytes.byteLength < 12) {
    fail(
      "audio-package-invalid-wave",
      path,
      "is shorter than a RIFF/WAVE header",
    );
  }
}

// The decoder intentionally reads every sample little-endian through DataView.
// It performs no normalization, resampling, level correction, or other DSP.
export function decodeAudioPackageWave(
  bytes,
  artifact,
  expectedSampleRate = ESO_CANONICAL_SAMPLE_RATE,
) {
  const path = artifact?.id ? `artifacts.${artifact.id}` : "artifact";
  requireWaveBytes(bytes, path);
  if (!isRecord(artifact)) {
    throw new TypeError("artifact descriptor must be an object");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (fourCc(bytes, 0) !== "RIFF" || fourCc(bytes, 8) !== "WAVE") {
    fail(
      "audio-package-invalid-wave",
      path,
      "is not a little-endian RIFF/WAVE file",
    );
  }
  if (view.getUint32(4, true) !== bytes.byteLength - 8) {
    fail(
      "audio-package-invalid-wave",
      path,
      "RIFF size does not close over exact payload bytes",
    );
  }

  let format = null;
  let factFrameCount = null;
  let dataOffset = null;
  let dataByteCount = null;
  let cursor = 12;
  while (cursor < bytes.byteLength) {
    if (cursor + 8 > bytes.byteLength) {
      fail(
        "audio-package-invalid-wave",
        path,
        "contains a truncated chunk header",
      );
    }
    const id = fourCc(bytes, cursor);
    const size = view.getUint32(cursor + 4, true);
    const payloadOffset = cursor + 8;
    const payloadEnd = payloadOffset + size;
    const paddedEnd = payloadEnd + (size & 1);
    if (payloadEnd > bytes.byteLength || paddedEnd > bytes.byteLength) {
      fail(
        "audio-package-invalid-wave",
        path,
        `chunk ${id} exceeds RIFF bounds`,
      );
    }
    if (id === "fmt ") {
      if (format !== null || (size !== 16 && size !== 18)) {
        fail(
          "audio-package-invalid-wave",
          path,
          "requires exactly one 16- or 18-byte fmt chunk",
        );
      }
      format = {
        chunkSize: size,
        tag: view.getUint16(payloadOffset, true),
        channelCount: view.getUint16(payloadOffset + 2, true),
        sampleRate: view.getUint32(payloadOffset + 4, true),
        byteRate: view.getUint32(payloadOffset + 8, true),
        blockAlign: view.getUint16(payloadOffset + 12, true),
        bitsPerSample: view.getUint16(payloadOffset + 14, true),
      };
      if (size === 18 && view.getUint16(payloadOffset + 16, true) !== 0) {
        fail(
          "audio-package-invalid-wave",
          path,
          "18-byte IEEE Float fmt extension must be empty",
        );
      }
    } else if (id === "fact") {
      if (factFrameCount !== null || size !== 4) {
        fail(
          "audio-package-invalid-wave",
          path,
          "requires exactly one four-byte fact chunk",
        );
      }
      factFrameCount = view.getUint32(payloadOffset, true);
    } else if (id === "data") {
      if (dataOffset !== null) {
        fail(
          "audio-package-invalid-wave",
          path,
          "contains more than one data chunk",
        );
      }
      dataOffset = payloadOffset;
      dataByteCount = size;
    }
    cursor = paddedEnd;
  }
  if (cursor !== bytes.byteLength) {
    fail(
      "audio-package-invalid-wave",
      path,
      "chunk table does not consume exact RIFF bytes",
    );
  }
  if (format === null || factFrameCount === null || dataOffset === null) {
    fail(
      "audio-package-invalid-wave",
      path,
      "requires fmt, fact, and data chunks",
    );
  }
  if (
    format.tag !== 3 ||
    format.channelCount !== 1 ||
    format.sampleRate !== expectedSampleRate ||
    format.byteRate !== expectedSampleRate * 4 ||
    format.blockAlign !== 4 ||
    format.bitsPerSample !== 32
  ) {
    fail(
      "audio-package-unsupported-audio-format",
      path,
      `must be mono IEEE Float32 little-endian at ${expectedSampleRate} Hz`,
    );
  }
  if (!Number.isSafeInteger(artifact.frame_count) || artifact.frame_count < 1) {
    fail(
      "audio-package-invalid-wave",
      path,
      "has an invalid manifest frame count",
    );
  }
  if (factFrameCount !== artifact.frame_count) {
    fail(
      "audio-package-invalid-wave",
      path,
      "fact frame count differs from the manifest",
    );
  }
  if (dataByteCount !== artifact.frame_count * Float32Array.BYTES_PER_ELEMENT) {
    fail(
      "audio-package-invalid-wave",
      path,
      "data length differs from exact mono frame count",
    );
  }
  if (bytes.byteLength !== artifact.byte_count) {
    fail(
      "audio-package-invalid-wave",
      path,
      "file length differs from manifest byte count",
    );
  }

  const samples = new Float32Array(artifact.frame_count);
  for (let frame = 0; frame < samples.length; ++frame) {
    const sample = view.getFloat32(dataOffset + frame * 4, true);
    if (!Number.isFinite(sample)) {
      fail(
        "audio-package-invalid-wave",
        `${path}.samples[${frame}]`,
        "sample is not finite",
      );
    }
    samples[frame] = sample;
  }
  return Object.freeze({
    sampleRate: format.sampleRate,
    channelCount: 1,
    frameCount: artifact.frame_count,
    formatChunkBytes: format.chunkSize,
    dataByteOffset: dataOffset,
    samples,
  });
}
