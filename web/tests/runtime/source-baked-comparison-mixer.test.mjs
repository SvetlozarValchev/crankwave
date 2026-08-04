import assert from "node:assert/strict";
import test from "node:test";

import {
  SourceBakedComparisonMixer,
  SourceBakedComparisonMode,
} from "../../runtime/source-baked-comparison-mixer.js";

test("comparison modes are exact gain routes on the canonical clock", () => {
  const source = Float32Array.of(-0, -0.75, 0.25, 1);
  const baked = Float32Array.of(0.5, -0.25, 0.75, -1);
  const mixer = new SourceBakedComparisonMixer();

  assert.equal(mixer.sampleRate, 192_000);
  assert.equal(mixer.channelCount, 2);
  assert.deepEqual(
    mixer.process(source, baked),
    Float32Array.of(-0, -0, -0.75, -0.75, 0.25, 0.25, 1, 1),
  );

  mixer.mode = SourceBakedComparisonMode.baked;
  assert.deepEqual(
    mixer.process(source, baked),
    Float32Array.of(0.5, 0.5, -0.25, -0.25, 0.75, 0.75, -1, -1),
  );

  mixer.mode = SourceBakedComparisonMode.split;
  assert.deepEqual(
    mixer.process(source, baked),
    Float32Array.of(-0, 0.5, -0.75, -0.25, 0.25, 0.75, 1, -1),
  );
});

test("both paths are metered in every mode with per-block and cumulative levels", () => {
  const mixer = new SourceBakedComparisonMixer({
    mode: SourceBakedComparisonMode.source,
  });
  mixer.process(Float32Array.of(0.5, -0.5), Float32Array.of(1, -1));
  mixer.mode = SourceBakedComparisonMode.baked;
  mixer.process(Float32Array.of(0, 2), Float32Array.of(0, -0.5));

  const diagnostics = mixer.diagnostics();
  assert.equal(diagnostics.mode, SourceBakedComparisonMode.baked);
  assert.equal(diagnostics.lastBlock.ordinal, 2);
  assert.equal(diagnostics.lastBlock.firstFrame, 2);
  assert.equal(diagnostics.lastBlock.frameCount, 2);
  assert.deepEqual(diagnostics.lastBlock.source, {
    finite: true,
    sampleCount: 2,
    peak: 2,
    rms: Math.sqrt(2),
    clipSampleCount: 1,
  });
  assert.deepEqual(diagnostics.lastBlock.baked, {
    finite: true,
    sampleCount: 2,
    peak: 0.5,
    rms: Math.sqrt(0.125),
    clipSampleCount: 0,
  });
  assert.equal(diagnostics.cumulative.blockCount, 2);
  assert.equal(diagnostics.cumulative.frameCount, 4);
  assert.deepEqual(diagnostics.cumulative.source, {
    finite: true,
    sampleCount: 4,
    peak: 2,
    rms: Math.sqrt(1.125),
    clipSampleCount: 1,
  });
  assert.deepEqual(diagnostics.cumulative.baked, {
    finite: true,
    sampleCount: 4,
    peak: 1,
    rms: 0.75,
    clipSampleCount: 0,
  });
  assert.ok(
    Math.abs(
      diagnostics.cumulative.bakedMinusSourceRmsDb -
        20 * Math.log10(0.75 / Math.sqrt(1.125)),
    ) < 1e-12,
  );
});

test("invalid blocks fail transactionally before diagnostics advance", () => {
  const mixer = new SourceBakedComparisonMixer();
  mixer.process(Float32Array.of(0.1), Float32Array.of(0.2));
  const before = mixer.diagnostics();

  assert.throws(
    () => mixer.process(Float32Array.of(0, 1), Float32Array.of(0)),
    /same number of frames/,
  );
  assert.throws(
    () => mixer.process(Float32Array.of(0), Float32Array.of(Number.NaN)),
    /bakedBlock contains a non-finite sample at frame 0/,
  );
  assert.throws(
    () => mixer.process([0], Float32Array.of(0)),
    /sourceBlock must be a mono Float32Array/,
  );
  assert.deepEqual(mixer.diagnostics(), before);
});

test("diagnostic reset is exact and retains the selected audible mode", () => {
  const mixer = new SourceBakedComparisonMixer({
    mode: SourceBakedComparisonMode.split,
  });
  mixer.process(Float32Array.of(2, 0), Float32Array.of(-2, 0));
  mixer.resetDiagnostics();

  assert.deepEqual(mixer.diagnostics(), {
    sampleRate: 192_000,
    channelCount: 2,
    mode: SourceBakedComparisonMode.split,
    lastBlock: null,
    cumulative: {
      blockCount: 0,
      frameCount: 0,
      source: {
        finite: true,
        sampleCount: 0,
        peak: 0,
        rms: 0,
        clipSampleCount: 0,
      },
      baked: {
        finite: true,
        sampleCount: 0,
        peak: 0,
        rms: 0,
        clipSampleCount: 0,
      },
      bakedMinusSourceRmsDb: 0,
    },
  });
  assert.deepEqual(
    mixer.process(Float32Array.of(0.25), Float32Array.of(-0.5)),
    Float32Array.of(0.25, -0.5),
  );
  assert.equal(mixer.diagnostics().lastBlock.ordinal, 1);
  assert.equal(mixer.diagnostics().lastBlock.firstFrame, 0);
});

test("invalid mode values cannot alter routing", () => {
  const mixer = new SourceBakedComparisonMixer();
  assert.throws(() => {
    mixer.mode = "equal-power-crossfade";
  }, /comparison mode/);
  assert.equal(mixer.mode, SourceBakedComparisonMode.source);
});
