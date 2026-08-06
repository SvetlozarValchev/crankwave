import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import test from "node:test";

import {
  ResponsiveAudioLifecycleCursor,
  loadResponsiveAudioLifecycleRuntime,
} from "../../runtime/responsive-audio-lifecycle-runtime.js";

const MANIFEST_URL = new URL(
  "https://fixtures.invalid/lifecycle/runtime.json",
);
const LOAD_COORDINATE = "measured-intake-manifold-pressure-pa-abs";
const LANES = Object.freeze([
  Object.freeze({ id: "coast", throttle01: 0 }),
  Object.freeze({ id: "power", throttle01: 1 }),
]);

function jsonBytes(value) {
  return new TextEncoder().encode(`${JSON.stringify(value)}\n`);
}

function pcmBytes(frameCount, sampleAt) {
  const bytes = new Uint8Array(frameCount * Float32Array.BYTES_PER_ELEMENT);
  const view = new DataView(bytes.buffer);
  for (let frame = 0; frame < frameCount; ++frame) {
    view.setFloat32(frame * Float32Array.BYTES_PER_ELEMENT, sampleAt(frame), true);
  }
  return bytes;
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function response(bytes) {
  return Object.freeze({
    ok: true,
    status: 200,
    async arrayBuffer() {
      return bytes.slice().buffer;
    },
  });
}

function buildFixture() {
  const blobs = new Map();
  const put = (relativePath, bytes) => {
    const url = new URL(relativePath, MANIFEST_URL).href;
    blobs.set(url, bytes);
    return Object.freeze({ relativePath, sha256: sha256(bytes) });
  };
  const artifact = (path, bytes, frameCount) => {
    const stored = put(path, bytes);
    return Object.freeze({
      path,
      sha256: stored.sha256,
      frame_count: frameCount,
    });
  };

  const starterArtifact = artifact(
    "audio/starter.f32le",
    pcmBytes(32, (frame) => 0.25 + frame / 1_000),
    32,
  );
  const startupArtifact = artifact(
    "audio/startup.f32le",
    pcmBytes(40, (frame) => 0.5 + frame / 2_000),
    40,
  );
  const shutdownArtifact = artifact(
    "audio/shutdown.f32le",
    pcmBytes(40, (frame) => -0.5 - frame / 2_000),
    40,
  );

  const admissionEvidence = jsonBytes({
    schema: "engine-sim-offline/startup-admission-floor-evidence",
    atlas_manifest: "../../held/exhaust-front-dry.json",
    atlas_load_coordinate: LOAD_COORDINATE,
    running_floor_rpm: 500,
    presentation_latency_frames: 0,
    fit: "deterministic unit fixture",
    rows: [
      { throttle_01: 0 },
      { throttle_01: 1 },
    ],
  });
  const storedAdmissionEvidence = put(
    "evidence/startup-admission.json",
    admissionEvidence,
  );

  const scenarioRoles = ["starter", "startup", "shutdown"];
  const scenarios = scenarioRoles.map((role) => {
    const derivedId = `fixture-${role}-scenario`;
    const scenarioPath = `source/scenarios/${role}.json`;
    const scenario = put(
      scenarioPath,
      jsonBytes({
        schema: "engine-sim-offline/scenario",
        id: derivedId,
        engine: "fixture-engine",
      }),
    );
    const evidencePath = `evidence/${role}.json`;
    const evidence = put(
      evidencePath,
      jsonBytes({
        schema: "engine-sim-offline/lifecycle-capture-evidence",
        role,
        physics_rate_hz: 10_000,
        delivery_rate_hz: 192_000,
        scenario_id: derivedId,
      }),
    );
    return Object.freeze({
      role,
      canonical_source_id: `fixture-${role}-canonical-source`,
      derived_id: derivedId,
      path: scenarioPath,
      sha256: scenario.sha256,
      evidence_path: evidencePath,
      evidence_sha256: evidence.sha256,
    });
  });

  const checkpoint = (kind, frame, rpm) => Object.freeze({
    kind,
    frame,
    rpm,
    precision: "exact",
    method: "deterministic-unit-fixture",
  });
  const seam = (target, sourceFrame, targetSourceFrame, rpm) => Object.freeze({
    source_frame: sourceFrame,
    crossfade_frames: 2,
    source_rpm: rpm,
    target_source_frame: targetSourceFrame,
    target_rpm: rpm,
    correlation: 1,
    target,
    target_reference: `${target}-unit-reference`,
  });
  const manifest = {
    schema: "engine-sim-offline/responsive-audio-lifecycle",
    id: "fixture-engine-lifecycle",
    engine: "fixture-engine",
    audio: {
      sample_rate_hz: 192_000,
      encoding: "float32le",
      channel_layout: "mono",
      bus_id: "master.engine.audition",
    },
    starter: {
      artifact: starterArtifact,
      mean_crank_rpm: 250,
      reference_rpm: 250,
      loop_start_frame: 2,
      loop_end_frame: 28,
      crossfade_frames: 4,
      attack_fade_frames: 1,
      release_fade_frames: 1,
    },
    startup: {
      artifact: startupArtifact,
      checkpoints: [
        checkpoint("ignition-on", 0, 250),
        checkpoint("first-combustion", 4, 300),
        checkpoint("starter-release", 6, 500),
        checkpoint("running-floor", 8, 600),
      ],
      entry: seam("starter", 2, 2, 250),
      exit: seam("running", 24, 24, 600),
    },
    shutdown: {
      artifact: shutdownArtifact,
      checkpoints: [
        checkpoint("settled-idle", 0, 700),
        checkpoint("ignition-off", 4, 650),
        checkpoint("engine-stopped", 24, 0),
      ],
      entry: seam("running", 2, 2, 700),
      silence_frame: 24,
      exit_fade_frames: 2,
      quiet_tail_frames: 16,
      quiet_peak_threshold: 1e-4,
      quiet_rms_threshold: 5e-5,
    },
    startup_admission: {
      schema: "engine-sim-offline/continuous-startup-admission-v1",
      running_bed_load_coordinate: LOAD_COORDINATE,
      admission_lane_coordinate: "authored-throttle-01",
      blend: "constant-power",
      pre_floor_progress: "smoothstep-first-fire-rpm-to-running-floor",
      completion_progress: "smoothstep-committed-crank-travel",
      completion_crank_travel_revolutions: 0.25,
      monotone_ownership: true,
      lanes: [
        { id: "coast", throttle_01: 0, floor_running_gain_linear: 0.5 },
        { id: "power", throttle_01: 1, floor_running_gain_linear: 0.75 },
      ],
      coast_stability: {
        lane_id: "coast",
        requires_starter_released: true,
        post_peak_crank_travel_revolutions: 0.5,
        clock_law: "lane-weighted-post-peak-admission",
      },
      evidence: {
        path: "evidence/startup-admission.json",
        method: "deterministic-unit-fixture",
        sha256: storedAdmissionEvidence.sha256,
        corrected_held_manifest_sha256: "a".repeat(64),
      },
    },
    provenance: {
      engine: { id: "fixture-engine", sha256: "b".repeat(64) },
      renderer_build: { id: "fixture-renderer", sha256: "c".repeat(64) },
      physics_rate_hz: 10_000,
      delivery_rate_hz: 192_000,
      scenarios,
    },
  };
  blobs.set(MANIFEST_URL.href, jsonBytes(manifest));

  const calls = [];
  const fetch = async (url, options) => {
    calls.push(Object.freeze({ url, options }));
    const bytes = blobs.get(url);
    if (bytes === undefined) {
      return Object.freeze({ ok: false, status: 404 });
    }
    return response(bytes);
  };
  return { blobs, calls, fetch, manifest };
}

test("strict lifecycle loading accepts a complete hash-bound in-memory package", async () => {
  const fixture = buildFixture();
  const package_ = await loadResponsiveAudioLifecycleRuntime(MANIFEST_URL, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });

  assert.equal(package_.kind, "responsive-audio-lifecycle");
  assert.equal(package_.engine, "fixture-engine");
  assert.equal(package_.sampleRate, 192_000);
  assert.equal(package_.starter.artifact.samples.length, 32);
  assert.equal(package_.startup.artifact.samples.length, 40);
  assert.equal(package_.shutdown.artifact.samples.length, 40);
  assert.equal(package_.startup.checkpoints.get("first-combustion").frame, 4);
  assert.deepEqual(
    fixture.calls.slice(0, 4).map(({ url }) => url).sort(),
    [
      MANIFEST_URL.href,
      new URL("audio/shutdown.f32le", MANIFEST_URL).href,
      new URL("audio/starter.f32le", MANIFEST_URL).href,
      new URL("audio/startup.f32le", MANIFEST_URL).href,
    ].sort(),
  );
  assert.ok(fixture.calls.every(({ options }) => options.cache === "no-store"));
});

test("lifecycle loading rejects a PCM payload that no longer matches its digest", async () => {
  const fixture = buildFixture();
  const starterUrl = new URL("audio/starter.f32le", MANIFEST_URL).href;
  fixture.blobs.get(starterUrl)[0] ^= 0x80;

  await assert.rejects(
    loadResponsiveAudioLifecycleRuntime(MANIFEST_URL, {
      fetch: fixture.fetch,
      crypto: webcrypto,
    }),
    /SHA-256 does not match its manifest/,
  );
});

test("lifecycle loading rejects corrupted provenance evidence", async () => {
  const fixture = buildFixture();
  const evidenceUrl = new URL("evidence/startup.json", MANIFEST_URL).href;
  fixture.blobs.get(evidenceUrl)[0] ^= 0x80;

  await assert.rejects(
    loadResponsiveAudioLifecycleRuntime(MANIFEST_URL, {
      fetch: fixture.fetch,
      crypto: webcrypto,
    }),
    /manifest\.provenance\.scenarios\[1\] evidence SHA-256 does not match its manifest/,
  );
});

function state(frame, overrides = {}) {
  const laneWeights = Object.freeze([
    Object.freeze({ id: "coast", weight: 0.75 }),
    Object.freeze({ id: "power", weight: 0.25 }),
  ]);
  return {
    frame,
    starter: false,
    ignition: false,
    fuel: false,
    runningBedReady: false,
    rpm: 0,
    requestedThrottle01: 0.25,
    manifoldPressurePaAbs: 80_000,
    rpmSlopeRpmPerSecond: 0,
    admissionLaneWeights: laneWeights,
    runningBedLaneWeights: laneWeights,
    unwrappedCrankRevolutions: 0,
    indicatedGasTorqueNm: null,
    ...overrides,
  };
}

test("cursor renders starter, admits the running bed, and completes shutdown", async () => {
  const fixture = buildFixture();
  const package_ = await loadResponsiveAudioLifecycleRuntime(MANIFEST_URL, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });
  const cursor = new ResponsiveAudioLifecycleCursor(package_, {
    audioLatencyFrames: 0,
    runningFloorRpm: 500,
    heldAnchorFloorRpm: 600,
    atlasLoadLanes: LANES,
    atlasLoadCoordinate: LOAD_COORDINATE,
  });
  cursor.reset(7);
  const mix = (frameCount) => cursor.mixPair(
    new Float32Array(frameCount).fill(0.125),
    new Float32Array(frameCount).fill(0.75),
  ).bakedBlock;

  cursor.setState(state(0));
  assert.deepEqual(mix(1), Float32Array.of(0));

  cursor.setState(state(1, { starter: true, rpm: 250 }));
  const starter = mix(3);
  assert.ok(starter.every((sample) => sample > 0));
  assert.equal(cursor.diagnostics().outputMode, "cranking");

  cursor.setState(state(4, {
    starter: true,
    ignition: true,
    fuel: true,
    rpm: 300,
    rpmSlopeRpmPerSecond: 2_000,
    unwrappedCrankRevolutions: 0.2,
    indicatedGasTorqueNm: 40,
  }));
  const startup = mix(2);
  assert.ok(startup.every((sample) => sample > 0));
  assert.equal(cursor.diagnostics().activeEvent, "startup");
  assert.equal(cursor.diagnostics().startupCount, 1);

  cursor.setState(state(6, {
    ignition: true,
    fuel: true,
    runningBedReady: true,
    rpm: 500,
    rpmSlopeRpmPerSecond: 2_000,
    unwrappedCrankRevolutions: 1.2,
    indicatedGasTorqueNm: 60,
  }));
  mix(2);
  cursor.setState(state(8, {
    ignition: true,
    fuel: true,
    runningBedReady: true,
    rpm: 700,
    rpmSlopeRpmPerSecond: 1_000,
    unwrappedCrankRevolutions: 3,
    indicatedGasTorqueNm: 80,
  }));
  const handoff = mix(4);
  assert.ok(handoff.includes(0.75));
  assert.equal(cursor.diagnostics().admissionProgress, 1);
  assert.equal(cursor.diagnostics().outputMode, "running");

  cursor.setState(state(12, {
    runningBedReady: true,
    rpm: 650,
    rpmSlopeRpmPerSecond: -3_000,
    unwrappedCrankRevolutions: 3.3,
  }));
  const shutdown = mix(24);
  assert.ok(shutdown.some((sample) => sample < 0));
  assert.equal(shutdown.at(-1), 0);

  const diagnostics = cursor.diagnostics();
  assert.equal(diagnostics.generation, 7);
  assert.equal(diagnostics.startupCount, 1);
  assert.equal(diagnostics.shutdownCount, 1);
  assert.equal(diagnostics.outputMode, "stopped");
  assert.equal(diagnostics.activeEvent, null);
});
