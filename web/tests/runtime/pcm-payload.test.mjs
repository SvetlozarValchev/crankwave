import assert from "node:assert/strict";
import test from "node:test";

import {
  decodeAudioPayload,
  PCM16_SCALE_ENCODING,
} from "../../runtime/pcm-payload.js";

test("PCM16 scaled payload decodes little-endian samples into float32", () => {
  const bytes = new Uint8Array(6);
  const view = new DataView(bytes.buffer);
  view.setInt16(0, -32768, true);
  view.setInt16(2, 0, true);
  view.setInt16(4, 16384, true);
  const samples = decodeAudioPayload(
    bytes,
    3,
    PCM16_SCALE_ENCODING,
    { scale: 0.5 },
    "fixture",
  );
  assert.deepEqual([...samples], [-0.5, 0, 0.25]);
});

test("PCM16 scaled payload rejects a missing or invalid scale", () => {
  assert.throws(
    () => decodeAudioPayload(new Uint8Array(2), 1, PCM16_SCALE_ENCODING, {}, "fixture"),
    /scale must be a finite positive number/,
  );
});
