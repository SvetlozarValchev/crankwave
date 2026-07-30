const RIFF_HEADER_BYTES_WITH_FACT = 56;
const MAXIMUM_RIFF_PAYLOAD_BYTES = 0xffff_ffff;

export function concatenateFloat32(chunks, totalSamples) {
  if (!Array.isArray(chunks)) {
    throw new TypeError("PCM chunks must be an array");
  }
  if (!Number.isSafeInteger(totalSamples) || totalSamples < 0) {
    throw new RangeError("totalSamples must be a non-negative safe integer");
  }
  const output = new Float32Array(totalSamples);
  let offset = 0;
  for (const chunk of chunks) {
    if (!(chunk instanceof Float32Array)) {
      throw new TypeError("each PCM chunk must be Float32Array");
    }
    if (offset + chunk.length > output.length) {
      throw new RangeError("PCM chunks exceed declared totalSamples");
    }
    output.set(chunk, offset);
    offset += chunk.length;
  }
  if (offset !== output.length) {
    throw new RangeError("PCM chunks do not fill declared totalSamples");
  }
  return output;
}

function writeAscii(view, offset, text) {
  for (let index = 0; index < text.length; ++index) {
    view.setUint8(offset + index, text.charCodeAt(index));
  }
}

export function encodeFloat32Wav(interleaved, sampleRate, channelCount) {
  if (!(interleaved instanceof Float32Array)) {
    throw new TypeError("WAV input must be Float32Array");
  }
  if (!Number.isSafeInteger(sampleRate) || sampleRate < 1) {
    throw new RangeError("WAV sampleRate must be a positive integer");
  }
  if (!Number.isSafeInteger(channelCount) || channelCount < 1 || channelCount > 32) {
    throw new RangeError("WAV channelCount must be an integer in [1, 32]");
  }
  if (interleaved.length % channelCount !== 0) {
    throw new RangeError("WAV input is not channel-aligned");
  }
  const frameCount = interleaved.length / channelCount;
  const dataBytes = interleaved.byteLength;
  const fileBytes = RIFF_HEADER_BYTES_WITH_FACT + dataBytes;
  if (
    fileBytes - 8 > MAXIMUM_RIFF_PAYLOAD_BYTES ||
    frameCount > MAXIMUM_RIFF_PAYLOAD_BYTES
  ) {
    throw new RangeError("canonical PCM exceeds the RIFF/WAVE 32-bit size limit");
  }

  const output = new Uint8Array(fileBytes);
  const view = new DataView(output.buffer);
  writeAscii(view, 0, "RIFF");
  view.setUint32(4, fileBytes - 8, true);
  writeAscii(view, 8, "WAVE");

  writeAscii(view, 12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 3, true); // WAVE_FORMAT_IEEE_FLOAT
  view.setUint16(22, channelCount, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * channelCount * 4, true);
  view.setUint16(32, channelCount * 4, true);
  view.setUint16(34, 32, true);

  writeAscii(view, 36, "fact");
  view.setUint32(40, 4, true);
  view.setUint32(44, frameCount, true);

  writeAscii(view, 48, "data");
  view.setUint32(52, dataBytes, true);
  for (let index = 0; index < interleaved.length; ++index) {
    view.setFloat32(
      RIFF_HEADER_BYTES_WITH_FACT + index * Float32Array.BYTES_PER_ELEMENT,
      interleaved[index],
      true,
    );
  }
  return output;
}
