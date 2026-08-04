import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import test from "node:test";

import {
  AudioPackageLoadError,
  loadAudioPackage,
} from "../../runtime/audio-package-loader.js";
import { decodeAudioPackageWave } from "../../runtime/audio-package-wave.js";
import { encodeFloat32Wav } from "../../runtime/wav.js";

const encoder = new TextEncoder();
const MANIFEST_URL = "https://example.test/packages/bmw/package.json";
const DIGEST = "01".repeat(32);
const FRAME_COUNT = 100;

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function exactBoundary(frame) {
  return {
    left_frame: frame,
    right_frame: frame,
    fraction_from_left_01: 0,
  };
}

function unit(row, load, torque, direction = "rising", idle = false) {
  const sourceRow = direction === "falling" ? 3 - row : row;
  const start = 10 + sourceRow * 20;
  return {
    completed_cycle_ordinal: 10 + sourceRow,
    start: exactBoundary(start),
    end: exactBoundary(start + 4),
    canonical_rpm: idle ? 700 + row * 2 : 1000 + row * 100,
    measured_rpm: idle ? 700 + row * 2 : 1000 + row * 100,
    average_signed_load: load,
    average_net_torque_nm: torque,
    average_requested_throttle_01: load === 1 ? 1 : load === -1 ? 0.04 : 0.45,
    average_resolved_throttle_01: load === 1 ? 1 : load === -1 ? 0.04 : 0.45,
    state_mask: 27,
    transition_mask: 0,
  };
}

function identity(id) {
  return { id, sha256: DIGEST };
}

function laneArtifact(busId, artifactId) {
  return { bus_id: busId, artifact_id: artifactId };
}

function makeWave(seed) {
  const pcm = new Float32Array(FRAME_COUNT);
  pcm[0] = seed;
  pcm[1] = -0;
  pcm[2] = -0.25;
  pcm[3] = 0.5;
  return { pcm, bytes: encodeFloat32Wav(pcm, 192_000, 1) };
}

function makeFixture() {
  const payloads = {
    "audio/idle/master.wav": makeWave(0.125),
    "audio/running/coast/master.wav": makeWave(0.25),
    "audio/running/part/master.wav": makeWave(0.375),
    "audio/running/power/master.wav": makeWave(0.5),
  };
  const artifact = (id, relativePath) => ({
    id,
    relative_path: relativePath,
    frame_count: FRAME_COUNT,
    byte_count: payloads[relativePath].bytes.byteLength,
    payload_sha256: sha256(payloads[relativePath].bytes),
  });
  const manifest = {
    schema: "engine-sim-offline/audio-package",
    identity: {
      package_id: "bmw-responsive",
      engine: identity("bmw-m52tub28-cleanroom"),
      bake_plan: identity("bmw-responsive-normal-running"),
    },
    provenance: {
      renderer_build: identity("engine-sim-offline-build"),
      source_inputs: identity("bmw-responsive-source-inputs"),
    },
    audio: {
      sample_rate: { numerator: 192_000, denominator: 1 },
      container: "wav",
      encoding: "float32le",
      channel_layout: "mono",
    },
    buses: [
      {
        id: "master.engine.audition",
        kind: "master_engine_audition",
        disposition: "monitor_mix",
        source_route: null,
      },
    ],
    running: {
      cycle_revolutions: 2,
      selector_seed: "42",
      cycle_signal_alignment_frames: 1228.8,
      rpm_grid: {
        minimum_rpm: 1000,
        playback_minimum_rpm: 1100,
        playback_maximum_rpm: 1200,
        maximum_rpm: 1300,
        spacing_rpm: 100,
        padding_rows_per_side: 1,
        neighbor_radius_rows: 1,
        edge_guard_frames: 1,
        maximum_assignment_error_rpm: 5,
      },
      planes: [
        {
          id: "coast",
          load_coordinate: -1,
          direction: "falling",
          source_scenario: identity("bmw-coast-source"),
          artifacts: [
            laneArtifact("master.engine.audition", "running.coast.master"),
          ],
          units: Array.from({ length: 4 }, (_, row) =>
            unit(row, -1, -100 + row, "falling"),
          ),
        },
        {
          id: "part",
          load_coordinate: 0,
          direction: "rising",
          source_scenario: identity("bmw-part-source"),
          artifacts: [
            laneArtifact("master.engine.audition", "running.part.master"),
          ],
          units: Array.from({ length: 4 }, (_, row) =>
            unit(row, 0, row, "rising"),
          ),
        },
        {
          id: "power",
          load_coordinate: 1,
          direction: "rising",
          source_scenario: identity("bmw-power-source"),
          artifacts: [
            laneArtifact("master.engine.audition", "running.power.master"),
          ],
          units: Array.from({ length: 4 }, (_, row) =>
            unit(row, 1, 100 + row, "rising"),
          ),
        },
      ],
      idle: {
        source_scenario: identity("bmw-idle-source"),
        artifacts: [laneArtifact("master.engine.audition", "idle.master")],
        units: [unit(0, 0, 0, "rising", true), unit(1, 0, 1, "rising", true)],
      },
    },
    events: [],
    artifacts: [
      artifact("idle.master", "audio/idle/master.wav"),
      artifact("running.coast.master", "audio/running/coast/master.wav"),
      artifact("running.part.master", "audio/running/part/master.wav"),
      artifact("running.power.master", "audio/running/power/master.wav"),
    ],
  };
  return { manifest, payloads };
}

function jsonBytes(value) {
  return encoder.encode(`${JSON.stringify(value)}\n`);
}

function mockResponse(url, bytes) {
  return {
    ok: true,
    status: 200,
    url,
    async arrayBuffer() {
      return bytes.buffer.slice(
        bytes.byteOffset,
        bytes.byteOffset + bytes.byteLength,
      );
    },
  };
}

function packageFetch(manifest, payloads, requests = []) {
  const bodies = new Map([[MANIFEST_URL, jsonBytes(manifest)]]);
  for (const [relativePath, payload] of Object.entries(payloads)) {
    bodies.set(new URL(relativePath, MANIFEST_URL).href, payload.bytes);
  }
  return async (url) => {
    requests.push(url);
    const body = bodies.get(url);
    if (!body) {
      return { ok: false, status: 404, url, arrayBuffer: async () => new ArrayBuffer(0) };
    }
    return mockResponse(url, body);
  };
}

function nativeFloat32Wave(pcm) {
  const bytes = new Uint8Array(58 + pcm.byteLength);
  const view = new DataView(bytes.buffer);
  const ascii = (offset, value) => {
    for (let index = 0; index < value.length; ++index) {
      view.setUint8(offset + index, value.charCodeAt(index));
    }
  };
  ascii(0, "RIFF");
  view.setUint32(4, bytes.byteLength - 8, true);
  ascii(8, "WAVE");
  ascii(12, "fmt ");
  view.setUint32(16, 18, true);
  view.setUint16(20, 3, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, 192_000, true);
  view.setUint32(28, 192_000 * 4, true);
  view.setUint16(32, 4, true);
  view.setUint16(34, 32, true);
  view.setUint16(36, 0, true);
  ascii(38, "fact");
  view.setUint32(42, 4, true);
  view.setUint32(46, pcm.length, true);
  ascii(50, "data");
  view.setUint32(54, pcm.byteLength, true);
  for (let index = 0; index < pcm.length; ++index) {
    view.setFloat32(58 + index * 4, pcm[index], true);
  }
  return bytes;
}

test("loader fetches, hashes, and preserves every current-contract lane and tape", async () => {
  const { manifest, payloads } = makeFixture();
  const requests = [];
  const loaded = await loadAudioPackage(MANIFEST_URL, {
    fetch: packageFetch(manifest, payloads, requests),
    crypto: webcrypto,
  });

  assert.equal(requests.length, 1 + manifest.artifacts.length);
  assert.equal(new Set(requests).size, requests.length);
  assert.equal(loaded.manifest, loaded.manifest);
  assert.deepEqual(loaded.manifest.running.planes, manifest.running.planes);
  assert.deepEqual(loaded.manifest.running.idle.units, manifest.running.idle.units);
  assert.ok(Object.isFrozen(loaded.manifest));
  assert.ok(Object.isFrozen(loaded.manifest.running.planes[0].units));
  assert.equal(loaded.artifacts.length, manifest.artifacts.length);

  const idle = loaded.artifact("idle.master");
  assert.equal(idle.sampleRate, 192_000);
  assert.equal(idle.channelCount, 1);
  assert.equal(idle.frameCount, FRAME_COUNT);
  assert.equal(idle.formatChunkBytes, 16);
  assert.equal(idle.pcm[0], 0.125);
  assert.ok(Object.is(idle.pcm[1], -0));
  assert.equal(idle.pcm[2], -0.25);
  assert.equal(idle.pcm[3], 0.5);
  assert.deepEqual(idle.bytes, payloads["audio/idle/master.wav"].bytes);
  assert.equal(loaded.artifact("absent"), null);
});

test("WAVE chunk scan accepts both native fmt18 and browser fmt16 without DSP", () => {
  const pcm = Float32Array.of(-1, -0, -0.25, 0.5, 1);
  const browserWave = encodeFloat32Wav(pcm, 192_000, 1);
  const nativeWave = nativeFloat32Wave(pcm);
  const descriptor = (id, bytes) => ({
    id,
    frame_count: pcm.length,
    byte_count: bytes.byteLength,
  });

  const browser = decodeAudioPackageWave(
    browserWave,
    descriptor("browser-wave", browserWave),
  );
  const native = decodeAudioPackageWave(
    nativeWave,
    descriptor("native-wave", nativeWave),
  );
  assert.equal(browser.formatChunkBytes, 16);
  assert.equal(browser.dataByteOffset, 56);
  assert.equal(native.formatChunkBytes, 18);
  assert.equal(native.dataByteOffset, 58);
  assert.deepEqual(browser.samples, pcm);
  assert.deepEqual(native.samples, pcm);
  assert.ok(Object.is(browser.samples[1], -0));
  assert.ok(Object.is(native.samples[1], -0));
});

test("unsafe artifact references fail before any artifact request", async () => {
  for (const path of [
    "../escape.wav",
    "/absolute.wav",
    "https://evil.test/tape.wav",
    "audio\\tape.wav",
    "audio/tape.wav?gain=2",
    "audio/tape.wav#fragment",
    "audio//tape.wav",
  ]) {
    const { manifest, payloads } = makeFixture();
    manifest.artifacts[0].relative_path = path;
    const requests = [];
    await assert.rejects(
      loadAudioPackage(MANIFEST_URL, {
        fetch: packageFetch(manifest, payloads, requests),
        crypto: webcrypto,
      }),
      (error) =>
        error instanceof AudioPackageLoadError &&
        error.code === "audio-package-invalid-artifact-path",
    );
    assert.deepEqual(requests, [MANIFEST_URL]);
  }
});

test("artifact SHA-256 covers exact fetched WAVE bytes", async () => {
  const { manifest, payloads } = makeFixture();
  const corrupted = payloads["audio/running/power/master.wav"].bytes.slice();
  corrupted[corrupted.length - 1] ^= 1;
  payloads["audio/running/power/master.wav"] = {
    ...payloads["audio/running/power/master.wav"],
    bytes: corrupted,
  };

  await assert.rejects(
    loadAudioPackage(MANIFEST_URL, {
      fetch: packageFetch(manifest, payloads),
      crypto: webcrypto,
    }),
    (error) =>
      error instanceof AudioPackageLoadError &&
      error.code === "audio-package-artifact-digest-mismatch" &&
      error.path === "artifacts[3].payload_sha256",
  );
});

test("WAVE fact, data, file lengths, format, and finite samples fail closed", () => {
  const pcm = Float32Array.of(0.25, -0.5);
  const descriptor = (bytes) => ({
    id: "test-wave",
    frame_count: pcm.length,
    byte_count: bytes.byteLength,
  });

  const wrongFact = encodeFloat32Wav(pcm, 192_000, 1);
  new DataView(wrongFact.buffer).setUint32(44, pcm.length + 1, true);
  assert.throws(
    () => decodeAudioPackageWave(wrongFact, descriptor(wrongFact)),
    /fact frame count differs/,
  );

  const wrongData = encodeFloat32Wav(pcm, 192_000, 1).slice(0, -4);
  const wrongDataView = new DataView(wrongData.buffer);
  wrongDataView.setUint32(4, wrongData.byteLength - 8, true);
  wrongDataView.setUint32(52, pcm.byteLength - 4, true);
  assert.throws(
    () => decodeAudioPackageWave(wrongData, descriptor(wrongData)),
    /data length differs/,
  );

  const wrongRate = encodeFloat32Wav(pcm, 48_000, 1);
  assert.throws(
    () => decodeAudioPackageWave(wrongRate, descriptor(wrongRate)),
    /must be mono IEEE Float32 little-endian at 192000 Hz/,
  );

  const nonfinite = encodeFloat32Wav(pcm, 192_000, 1);
  new DataView(nonfinite.buffer).setFloat32(56, Number.NaN, true);
  assert.throws(
    () => decodeAudioPackageWave(nonfinite, descriptor(nonfinite)),
    /sample is not finite/,
  );

  const wrongFileLength = encodeFloat32Wav(pcm, 192_000, 1);
  assert.throws(
    () =>
      decodeAudioPackageWave(wrongFileLength, {
        ...descriptor(wrongFileLength),
        byte_count: wrongFileLength.byteLength + 1,
      }),
    /file length differs/,
  );
});
