import assert from "node:assert/strict";
import test from "node:test";

import {
  audiblePackageCycleBoundaryFrame,
  ResponsiveAudioPackageFollower,
  RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS,
} from "../../runtime/responsive-audio-package-follower.js";

const SAMPLE_RATE = 192_000;
const ALIGNMENT_FRAMES = 1228.8;
const TAPE_FRAMES = 48_000;
const RUNNING_RPMS = Array.from({ length: 11 }, (_unused, row) =>
  10_000 + row * 1_000,
);
const IDLE_RPMS = Array.from({ length: 13 }, (_unused, row) =>
  14_700 + row * 50,
);

function exactBoundary(frame) {
  return {
    left_frame: frame,
    right_frame: frame,
    fraction_from_left_01: 0,
  };
}

function unit(rpm, row, load, throttle, { idle = false, constantMeasuredRpm = false } = {}) {
  const start = 4_000 + row * 3_200;
  const measuredRpm = constantMeasuredRpm ? 15_000 : rpm;
  const period = Math.round((2 * 60 * SAMPLE_RATE) / measuredRpm);
  return {
    completed_cycle_ordinal: row + 1,
    start: exactBoundary(start),
    end: exactBoundary(start + period),
    canonical_rpm: rpm,
    measured_rpm: measuredRpm,
    average_signed_load: load,
    average_net_torque_nm: load * 100 + row,
    average_requested_throttle_01: throttle,
    average_resolved_throttle_01: 1 - throttle,
    state_mask: 27,
    transition_mask: 0,
    idle,
  };
}

function fixture({ impulses = false, constantMeasuredRpm = false } = {}) {
  const definitions = [
    { id: "coast", load: -1, throttle: 0.05, value: -0.2 },
    { id: "part", load: 0, throttle: 0.45, value: 0.2 },
    { id: "power", load: 1, throttle: 1, value: 0.4 },
  ];
  const artifacts = new Map();
  const makeTape = (value, units) => {
    const tape = new Float32Array(TAPE_FRAMES);
    if (impulses) {
      for (const cycle of units) {
        tape[cycle.start.left_frame] = value;
      }
    } else {
      tape.fill(value);
    }
    return tape;
  };
  const planes = definitions.map((definition) => {
    const units = RUNNING_RPMS.map((rpm, row) =>
      unit(rpm, row, definition.load, definition.throttle, {
        constantMeasuredRpm,
      }),
    );
    const artifactId = `running.${definition.id}.master`;
    artifacts.set(artifactId, makeTape(definition.value, units));
    return {
      id: definition.id,
      load_coordinate: definition.load,
      direction: definition.id === "coast" ? "falling" : "rising",
      source_scenario: { id: `${definition.id}-source`, sha256: "01".repeat(32) },
      artifacts: [
        { bus_id: "master.engine.audition", artifact_id: artifactId },
      ],
      units,
    };
  });
  const idleUnits = IDLE_RPMS.map((rpm, row) =>
    unit(rpm, row, -1, 0.05, { idle: true, constantMeasuredRpm }),
  );
  artifacts.set("idle.master", makeTape(impulses ? 0 : 0.3, idleUnits));
  const manifest = {
    schema: "engine-sim-offline/audio-package",
    identity: {},
    provenance: {},
    audio: {
      sample_rate: { numerator: SAMPLE_RATE, denominator: 1 },
      container: "wav",
      encoding: "float32le",
      channel_layout: "mono",
    },
    buses: [{ id: "master.engine.audition" }],
    running: {
      cycle_revolutions: 2,
      selector_seed: "1311768467463790320",
      cycle_signal_alignment_frames: ALIGNMENT_FRAMES,
      rpm_grid: {
        minimum_rpm: 10_000,
        playback_minimum_rpm: 13_000,
        playback_maximum_rpm: 17_000,
        maximum_rpm: 20_000,
        spacing_rpm: 1_000,
        padding_rows_per_side: 3,
        neighbor_radius_rows: 3,
        edge_guard_frames: 100,
        maximum_assignment_error_rpm: 0,
      },
      planes,
      idle: {
        source_scenario: { id: "idle-source", sha256: "02".repeat(32) },
        artifacts: [
          { bus_id: "master.engine.audition", artifact_id: "idle.master" },
        ],
        units: idleUnits,
      },
    },
    events: [],
    artifacts: [],
  };
  return {
    manifest,
    artifact(id) {
      const pcm = artifacts.get(id);
      return pcm ? { id, pcm } : null;
    },
  };
}

function clock(startDeliveryFrame, endDeliveryFrame, rpm, signedLoad) {
  return {
    start: { deliveryFrame: startDeliveryFrame, rpm, signedLoad },
    end: { deliveryFrame: endDeliveryFrame, rpm, signedLoad },
  };
}

function throttleClock(startDeliveryFrame, endDeliveryFrame, rpm, throttle01) {
  return {
    start: { deliveryFrame: startDeliveryFrame, rpm, throttle01 },
    end: { deliveryFrame: endDeliveryFrame, rpm, throttle01 },
  };
}

function completedCycle(cycleOrdinal, audibleMarkerFrame) {
  return {
    completedCycleOrdinal: String(cycleOrdinal),
    endBoundary: {
      deliveryFrame: audibleMarkerFrame - ALIGNMENT_FRAMES,
    },
  };
}

test("exact native cycle evidence reanchors the source marker without rounding", () => {
  const loadedPackage = fixture({ impulses: true, constantMeasuredRpm: true });
  const follower = new ResponsiveAudioPackageFollower(loadedPackage);
  const markerFrame = 6_000.375;
  const cycle = completedCycle(1, markerFrame);

  assert.equal(
    audiblePackageCycleBoundaryFrame(loadedPackage, cycle),
    markerFrame,
  );
  const rendered = follower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 6_200,
    sourceClock: clock(0, 6_200, 15_000, 1),
    completedCycles: [cycle],
  });
  const pcm = rendered.bus("master.engine.audition");
  const left = Math.floor(markerFrame);
  const right = left + 1;
  const localMass = pcm[left] + pcm[right];
  const centroid = (left * pcm[left] + right * pcm[right]) / localMass;
  assert.ok(localMass > 0.39 && localMass <= 0.4);
  assert.ok(Math.abs(centroid - markerFrame) < 1e-3);

  const exactSelection = follower
    .diagnostics()
    .selectionHistory.find((selection) => selection.exact);
  assert.equal(exactSelection.markerFrame, markerFrame);
  assert.equal(
    exactSelection.onsetFrame,
    markerFrame - RESPONSIVE_AUDIO_FOLLOWER_CONSTANTS.overlapFrames,
  );
  assert.equal(follower.diagnostics().exactReanchorCount, 1);
  assert.equal(follower.diagnostics().sourceCursorTransitionCount, 0);
  assert.equal(follower.diagnostics().selectionHistory.length, 2);
});

test("coarse-grid listening isolate selects only the nearest source row", () => {
  const loadedPackage = fixture();
  const cycles = Array.from({ length: 22 }, (_unused, index) =>
    completedCycle(index + 1, 6_000 + index * 1_536),
  );
  const render = (follower) =>
    follower.renderBlock({
      firstDeliveryFrame: 0,
      frameCount: 42_000,
      sourceClock: clock(0, 42_000, 15_000, 1),
      completedCycles: cycles,
    });
  const first = new ResponsiveAudioPackageFollower(loadedPackage);
  const second = new ResponsiveAudioPackageFollower(loadedPackage);
  const firstPcm = render(first).bus("master.engine.audition");
  const secondPcm = render(second).bus("master.engine.audition");
  assert.deepEqual(firstPcm, secondPcm);
  assert.deepEqual(first.diagnostics(), second.diagnostics());

  const offsets = first
    .diagnostics()
    .selectionHistory.map((selection) => selection.variationOffset);
  assert.ok(offsets.length >= 20);
  assert.ok(offsets.every((offset) => offset === 0));
});

test("directional planes mix linearly while idle uses the accepted equal-power handoff", () => {
  const partFollower = new ResponsiveAudioPackageFollower(fixture());
  const part = partFollower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 64,
    sourceClock: clock(0, 64, 16_000, 0),
  }).bus("master.engine.audition");
  for (const sample of part) {
    assert.ok(Math.abs(sample - 0.2) < 1e-6);
  }

  const throttleFollower = new ResponsiveAudioPackageFollower(fixture());
  const throttle = throttleFollower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 64,
    sourceClock: throttleClock(0, 64, 16_000, 0.45),
  }).bus("master.engine.audition");
  assert.ok(Math.abs(throttle[63] - 0.2) < 1e-6);

  const idleFollower = new ResponsiveAudioPackageFollower(fixture());
  const idle = idleFollower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 64,
    sourceClock: clock(0, 64, 15_000, 0),
  }).bus("master.engine.audition");
  const expected = (0.2 + 0.3) / Math.sqrt(2);
  assert.ok(Math.abs(idle[63] - expected) < 1e-6);

  const transitionFollower = new ResponsiveAudioPackageFollower(fixture());
  transitionFollower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 64,
    sourceClock: clock(0, 64, 16_000, 0),
  });
  const transitioned = transitionFollower.renderBlock({
    firstDeliveryFrame: 64,
    frameCount: SAMPLE_RATE,
    sourceClock: clock(64, 64 + SAMPLE_RATE, 16_000, 1),
  }).bus("master.engine.audition");
  assert.ok(Math.abs(transitioned.at(-1) - 0.4) < 1e-4);
});

test("physical source state changes reach B only after the exact package delay", () => {
  const follower = new ResponsiveAudioPackageFollower(fixture());
  const first = follower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 3_840,
    sourceClock: clock(0, 3_840, 16_000, -1),
  }).bus("master.engine.audition");
  assert.ok(Math.abs(first.at(-1) - -0.2) < 1e-6);

  // The physical load step begins at frame 3840. The last sample below is at
  // audible frame 5068, whose aligned source coordinate is still 3839.2.
  const beforeAlignedStep = follower.renderBlock({
    firstDeliveryFrame: 3_840,
    frameCount: 1_229,
    sourceClock: clock(3_840, 5_069, 16_000, 1),
  }).bus("master.engine.audition");
  assert.ok(Math.abs(beforeAlignedStep.at(-1) - -0.2) < 1e-6);
  assert.equal(follower.diagnostics().sourceCoordinate01, 0);

  follower.renderBlock({
    firstDeliveryFrame: 5_069,
    frameCount: 1,
    sourceClock: clock(5_069, 5_070, 16_000, 1),
  });
  const diagnostics = follower.diagnostics();
  assert.ok(diagnostics.sourceCoordinate01 > 0);
  assert.ok(Math.abs(diagnostics.lastAlignedSourceFrame - 3_840.2) < 1e-9);
  assert.equal(diagnostics.stateWarmupFrameCount, 1_229);
});

test("accelerating source-owned successors inherit one normalized seam phase", () => {
  const follower = new ResponsiveAudioPackageFollower(fixture());
  follower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 40_000,
    sourceClock: {
      start: { deliveryFrame: 0, rpm: 13_000, signedLoad: 1 },
      end: { deliveryFrame: 40_000, rpm: 17_000, signedLoad: 1 },
    },
  });
  const diagnostics = follower.diagnostics();
  assert.ok(diagnostics.sourceCursorTransitionCount >= 20);
  assert.equal(diagnostics.exactReanchorCount, 0);
  const phases = diagnostics.selectionHistory.map(
    (selection) => selection.seamPhaseCycles,
  );
  assert.ok(phases.length >= 20);
  for (const phase of phases) {
    assert.equal(phase, phases[0]);
  }
  for (const selection of diagnostics.selectionHistory) {
    assert.ok(Math.abs(selection.sourceSpanCycles - 1) < 1e-12);
  }
});

test("reset is the explicit boundary for leaving warm normal running", () => {
  const follower = new ResponsiveAudioPackageFollower(fixture());
  follower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 2_000,
    sourceClock: clock(0, 2_000, 15_000, 1),
  });
  assert.throws(
    () =>
      follower.renderBlock({
        firstDeliveryFrame: 2_000,
        frameCount: 64,
        sourceClock: clock(2_000, 2_064, 0, 0),
      }),
    /warm-normal-running follower/,
  );
  follower.reset();
  const diagnostics = follower.diagnostics();
  assert.equal(diagnostics.scope, "warm-normal-running-only");
  assert.equal(diagnostics.nextDeliveryFrame, null);
  assert.equal(diagnostics.selectionHistory.length, 0);
  assert.equal(diagnostics.buses[0].peak, 0);
});

test("normal running stays finite and unclipped without hidden limiting", () => {
  const follower = new ResponsiveAudioPackageFollower(fixture());
  const first = follower.renderBlock({
    firstDeliveryFrame: 0,
    frameCount: 4_096,
    sourceClock: {
      start: {
        deliveryFrame: 0,
        rpm: 13_000,
        signedLoad: -1,
        throttle01: 0.05,
      },
      end: {
        deliveryFrame: 4_096,
        rpm: 17_000,
        signedLoad: 1,
        throttle01: 1,
      },
    },
  }).bus("master.engine.audition");
  const second = follower.renderBlock({
    firstDeliveryFrame: 4_096,
    frameCount: 4_096,
    sourceClock: clock(4_096, 8_192, 17_000, 1),
  }).bus("master.engine.audition");
  for (const sample of [...first, ...second]) {
    assert.ok(Number.isFinite(sample));
    assert.ok(Math.abs(sample) <= 1);
  }
  const diagnostics = follower.diagnostics();
  assert.equal(diagnostics.buses[0].finite, true);
  assert.equal(diagnostics.buses[0].clipSampleCount, 0);
  assert.equal(diagnostics.uncoveredFrameCount, 0);
  assert.equal(diagnostics.exactSilentFrameCount, 0);
  assert.ok(diagnostics.buses[0].peak <= 1);
  assert.equal(diagnostics.nextDeliveryFrame, 8_192);

  assert.throws(
    () =>
      follower.renderBlock({
        firstDeliveryFrame: 8_192,
        frameCount: 64,
        sourceClock: {
          start: {
            deliveryFrame: 8_192,
            rpm: Number.NaN,
            signedLoad: 0,
          },
          end: { deliveryFrame: 8_256, rpm: 15_000, signedLoad: 0 },
        },
      }),
    /sourceClock.start.rpm must be finite/,
  );
  assert.equal(follower.diagnostics().nextDeliveryFrame, 8_192);
});
