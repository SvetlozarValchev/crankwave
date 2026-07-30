import assert from "node:assert/strict";
import test from "node:test";

import {
  PCM_RING_HEADER_BYTES,
  PCM_RING_HEADER_SCHEMA,
  PcmRingConsumer,
  PcmRingProducer,
  createPcmRingBuffer,
} from "../../runtime/pcm-ring-buffer.js";

test("bounded SPSC ring preserves interleaved samples across wrap", () => {
  const ring = createPcmRingBuffer({
    capacityFrames: 2048,
    channelCount: 2,
  });
  const producer = new PcmRingProducer(
    ring.sharedBuffer,
    ring.capacityFrames,
    ring.channelCount,
  );
  const consumer = new PcmRingConsumer(
    ring.sharedBuffer,
    ring.capacityFrames,
    ring.channelCount,
  );

  const first = new Float32Array(1800 * 2);
  for (let index = 0; index < first.length; ++index) {
    first[index] = index + 0.25;
  }
  assert.equal(producer.writeInterleaved(first), 1800);
  const firstRead = new Float32Array(1700 * 2);
  assert.equal(consumer.readInterleaved(firstRead), 1700);
  assert.deepEqual(firstRead, first.subarray(0, firstRead.length));

  const second = new Float32Array(1000 * 2);
  for (let index = 0; index < second.length; ++index) {
    second[index] = -index - 0.5;
  }
  assert.equal(producer.writeInterleaved(second), 1000);
  const remainder = new Float32Array(1100 * 2);
  assert.equal(consumer.readInterleaved(remainder), 1100);
  const expected = new Float32Array(remainder.length);
  expected.set(first.subarray(firstRead.length), 0);
  expected.set(second, first.length - firstRead.length);
  assert.deepEqual(remainder, expected);
});

test("consumer zero-fills and counts underrun without moving past producer", () => {
  const ring = createPcmRingBuffer({
    capacityFrames: 2048,
    channelCount: 1,
  });
  const producer = new PcmRingProducer(
    ring.sharedBuffer,
    ring.capacityFrames,
    ring.channelCount,
  );
  const consumer = new PcmRingConsumer(
    ring.sharedBuffer,
    ring.capacityFrames,
    ring.channelCount,
  );
  producer.writeInterleaved(Float32Array.of(1, 2, 3));
  const output = new Float32Array(8);
  assert.equal(consumer.readInterleaved(output), 3);
  assert.deepEqual(output, Float32Array.of(1, 2, 3, 0, 0, 0, 0, 0));
  assert.equal(producer.snapshot().underrunFrames, 5);
  assert.equal(producer.snapshot().underrunEvents, 1);
});

test("ring schema is the exact 64-byte browser contract", () => {
  assert.equal(PCM_RING_HEADER_BYTES, 64);
  assert.deepEqual(PCM_RING_HEADER_SCHEMA.counters, {
    writeFrame: 0,
    readFrame: 1,
    availableFrames: 2,
    underrunFrames: 3,
    underrunEvents: 4,
    generation: 5,
    producerState: 6,
  });
});
