# Engine authoring workflow

For new engines supported by the importer, author and validate **engine-sim MR**
first. The agent's engine definition is `main.mr`; deterministic tooling owns
the Crankwave conversion. Do not independently rewrite or retune the imported
Crankwave JSON to disguise a backend difference.

- Use the real local engine-sim compiler and native audition tool under
  `tools/engine-sim-authoring/`. Do not substitute a text parser or mock validator.
- `prepare.py` builds the native toolkit from the local engine-sim fork.
  `prepare_graph.py` builds the evaluated-graph adapter against that same build.
  Both accept explicit paths; consult their `--help` output.
- Author against the engine-sim API, then use `audition.py validate` and
  `audition.py audition`. Keep attempted MR files, controls, telemetry and audio.
- `export.py` reads the evaluated graph and creates Crankwave inputs.
  `compare.py` audits actual compiled values and compares native render traces.
- `bake.py` runs import, native base baking, real limiter capture/attachment,
  container verification and runtime lifecycle checks. It publishes
  `complete.crankwave` only after those checks pass.
- `record_harness.mjs` records the actual standalone browser worker and output
  resampler. Listen to the baked runtime result as well as the native audition.

Current importer scope: one crankshaft at the origin, direct rods, fixed standard
cams and direct throttle. Spark ignition and the local fork's scheduled two-stage
burn approximation are supported. The latter explicitly preserves its premixed,
diffusion and flame-speed coefficients; the native fork does not execute
temperature-triggered autoignition or direct injection. Do not confuse it with
Crankwave's separate mechanical-diesel pressure/flow backend. Governors, VTEC and
master/slave rods remain unsupported by this importer. Preserve accepted diesel
assets and use the existing mechanical-diesel workflow for that backend.

The exporter is experimental. Strict export rejects the supplementary aggregate
loss policy required by Crankwave but absent from native MR. Explicit diagnostic
builds use `--diagnostic-loss-policy`; keep that limitation in their reports.
Successful compilation, listener acceptance and exact parameter checks do not
establish bit-identical physics or audio. The native base bake currently captures
at 10 kHz physics, while the limiter renderer uses 20 kHz.

Reuse the packaged shared recorded starter. Starter torque/speed in MR describe
cranking physics, not a replacement recording. Verify pickup handoff, limiter
cut/recovery, restart and key-off in playback. A carrier that only passes its
container hash checks is not sufficient.

Keep accepted engines and listening baselines immutable. Write experiments to
new output directories. Do not claim subjective listening from numerical checks.
Use isolated authoring trials when requested; compare observable authored files,
tool actions and outcomes, without claiming access to hidden reasoning.

`pilot.py --specification tools/engine-sim-authoring/mixed-trials.json` defines
five independent MR cases with fixed briefs and targets. `finish_trials.py`
waits for completed runs, checks that the final source has a native audition,
exports it, renders both backends, bakes the complete carrier and records the
browser harness. The comparison source differs only in its explicit 20 kHz
simulation frequency, which the standalone Crankwave renderer requires. Keep
the original authored source and 10 kHz auditions alongside that comparison.
Run `evaluate.py` with the same specification for independent behavioral checks;
a pipeline completion is not behavioral or subjective acceptance.
