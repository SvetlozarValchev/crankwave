export const PCM16_SCALE_ENCODING = "pcm16le-scale-v1";

function fail(label, message) {
  throw new RangeError(`${label} ${message}`);
}

export function validatePcm16Scale(value, label = "quantization.scale") {
  if (typeof value !== "number" || !Number.isFinite(value) || !(value > 0)) {
    fail(label, "must be a finite positive number");
  }
  return value;
}

export function decodePcm16Le(bytes, frameCount, scale, label = "PCM16 payload") {
  if (!(bytes instanceof Uint8Array)) bytes = new Uint8Array(bytes);
  validatePcm16Scale(scale, `${label}.scale`);
  if (bytes.byteLength !== frameCount * 2) {
    fail(label, `byte count does not match frame_count (${bytes.byteLength} != ${frameCount * 2})`);
  }
  const samples = new Float32Array(frameCount);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let frame = 0; frame < frameCount; ++frame) {
    const sample = view.getInt16(frame * 2, true) / 32768 * scale;
    if (!Number.isFinite(sample)) fail(label, `contains a non-finite sample at ${frame}`);
    samples[frame] = sample;
  }
  return samples;
}

export function decodeAudioPayload(
  bytes,
  frameCount,
  encoding,
  quantization,
  label = "audio payload",
) {
  if (encoding === "float32le") {
    if (bytes.byteLength !== frameCount * 4) {
      fail(label, "byte count does not match frame_count");
    }
    const samples = new Float32Array(frameCount);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let frame = 0; frame < frameCount; ++frame) {
      const sample = view.getFloat32(frame * 4, true);
      if (!Number.isFinite(sample)) fail(label, `contains a non-finite sample at ${frame}`);
      samples[frame] = sample;
    }
    return samples;
  }
  if (encoding === PCM16_SCALE_ENCODING) {
    return decodePcm16Le(bytes, frameCount, quantization?.scale, label);
  }
  fail(label, `uses unsupported encoding ${encoding}`);
}
