import assert from "node:assert/strict";
import test from "node:test";

import { FixedSpectrumConvolver } from "../../runtime/dry-directional-phase-runtime.js";

test("fixed convolver accepts the smaller game delivery FFT shape", () => {
  const spectrum = {
    fftSize: 8,
    coefficientCount: 3,
    real: new Float64Array(8).fill(1),
    imaginary: new Float64Array(8),
  };
  const convolver = new FixedSpectrumConvolver(spectrum, 4);
  const first = convolver.process(Float64Array.of(1, 2, 3, 4));
  assert.deepEqual([...first], [1, 2, 3, 4]);
  const second = convolver.process(Float64Array.of(5, 6, 7, 8));
  assert.deepEqual([...second], [5, 6, 7, 8]);
});
