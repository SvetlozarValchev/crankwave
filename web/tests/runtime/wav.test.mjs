import assert from "node:assert/strict";
import test from "node:test";

import { encodeFloat32Wav } from "../../runtime/wav.js";

test("Float32 WAV carries canonical PCM values without quantization", () => {
  const pcm = Float32Array.of(-1, -0.25, 0, 0.5, 1);
  const wav = encodeFloat32Wav(pcm, 192_000, 1);
  const view = new DataView(wav.buffer);
  assert.equal(new TextDecoder().decode(wav.subarray(0, 4)), "RIFF");
  assert.equal(new TextDecoder().decode(wav.subarray(8, 12)), "WAVE");
  assert.equal(view.getUint16(20, true), 3);
  assert.equal(view.getUint32(24, true), 192_000);
  assert.equal(view.getUint32(44, true), pcm.length);
  assert.equal(view.getUint32(52, true), pcm.byteLength);
  for (let index = 0; index < pcm.length; ++index) {
    assert.equal(view.getFloat32(56 + index * 4, true), pcm[index]);
  }
});
