import assert from "node:assert/strict";
import test from "node:test";

import { SessionExecutionKind } from "../../runtime/c-api-abi.js";
import { liveExecutionKindForScenarioJson } from "../../runtime/browser-engine-runtime.js";

test("FreeEngine uses open-ended live execution", () => {
  assert.equal(
    liveExecutionKindForScenarioJson(
      JSON.stringify({ mode: { type: "free_engine" } }),
    ),
    SessionExecutionKind.openEnded,
  );
});

test("authored non-FreeEngine modes remain finite", () => {
  for (const type of [
    "held_speed",
    "prescribed_kinematic_sweep",
    "inertial_dyno",
    "free_vehicle",
  ]) {
    assert.equal(
      liveExecutionKindForScenarioJson(
        JSON.stringify({ mode: { type } }),
      ),
      SessionExecutionKind.finiteScenario,
    );
  }
});

test("malformed JSON remains on the native diagnostic path", () => {
  assert.equal(
    liveExecutionKindForScenarioJson('{"mode":'),
    SessionExecutionKind.finiteScenario,
  );
});

test("execution selection requires JSON text", () => {
  assert.throws(
    () => liveExecutionKindForScenarioJson(null),
    /scenarioJson must be an exact JSON string/u,
  );
});
