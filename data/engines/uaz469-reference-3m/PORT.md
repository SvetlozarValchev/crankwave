# UAZ-469 / UMZ-451M: 3:00 reference variant

This is the explicit JSON port of `engine-sim/assets/engines/uaz469_reference_3m.mr`, reviewed on 2026-09-08. The MR SHA-256 is `1022ef727755337c2714f5b26290b86917c2d8edf2e8456aeb5d8ab9417b4447`. The original MR and accompanying source notes are copied into `source/`; the engine-sim originals remain untouched.

This variant shares the cleanroom engine's physical parameters: 92 mm bore/stroke, 6.7:1 compression, 1–2–4–3 firing, carburetor, cylinder flow, ignition, 765 RPM idle target and 4,200 RPM redline. Its presentation uses the separate `reference_3m` synthetic exhaust response and the matching **0.05518654 authored IR gain**. The response emphasizes upper harmonics against the 3:00 reference section while retaining 0.28 ms pulse rounding. Derivative mix remains 0.00012, air noise 0.08 and jitter 0.010. No reference recording PCM is included.

The byte-identical 6,144-sample, 44.1 kHz PCM16 IR has SHA-256 `f547b216ea1f11c6bc90ad60440c27cf67affd94955468455978b91554cb1494`. Its generator, EQ profile, source README and normalization manifest are preserved together. The authored MR gain includes the source variant's preview gain; it is not replaced with the cleanroom IR gain.

The reviewed physical mapping and its limits are described in [the cleanroom port](../uaz469-cleanroom/PORT.md). Crankwave uses an explicit generic Chen-Flynn aggregate loss policy, so this is not a claim of identical free-running engine-sim physics or measured UAZ acoustics. The JSON preserves the source 5.38 axle metadata; Godot keeps its existing 5.125 axle, torque curve, clutch and manual gearbox.

## Reproduce

From the Crankwave root, use a new output path with sufficient free space:

```sh
python3 scripts/port-uaz469.py --source-root ../engine-sim --variant reference-3m
build/crankwave bake-crankwave \
  --engine data/engines/uaz469-reference-3m/engine.json \
  --asset-root data/engines/uaz469-reference-3m \
  --output ~/tmp/uaz469-reference-3m.crankwave --result-format json
build/crankwave verify-crankwave \
  --input ~/tmp/uaz469-reference-3m.crankwave --result-format json
```

The converter rejects unreviewed MR or IR hashes. The verified carrier is archived at `artifacts/uaz469-reference-3m/uaz469.crankwave`; `artifacts/uaz469/uaz469.crankwave` also points to this active variant for the viewer and Godot export workflow. The base 2026-09-08 bake SHA-256 is `65c48d7e56a688627bb0cc260db793cd78804550f579a2bd8b8fecb720416c74`. The active carrier with captured limiter performances has SHA-256 `d736ea73ff706349e81ea11a1000472e40309e3a1592cf77342dc0ca617dd3b7`.

The current native package profile is `interactive-preview-redline-v1`: **10 kHz preview physics**, 192 kHz presentation, 33 held cells, six directional captures and four lifecycle captures. This is not a 20 kHz production-bake claim. The Godot adapter uses the held dry textures with live fixed-time exhaust convolution/mastering and baked startup/shutdown, including Crankwave's shared mechanical starter. It does not implement the complete JavaScript directional transient overlay. Existing spatial routing and saved cabin tuning remain in use. See the Godot project's `addons/crankwave/README.md` for its import and signal-comparison checks.


## Captured limiter performance

The ordinary package bake disables ignition limiting while recording its held
cells. It does not capture a limiter performance just because the JSON declares
one. The active carrier additionally contains eight cut/recovery variations from
an actual native `free_engine` render of this same JSON at **20 kHz physics** and
192 kHz audio. The base held/directional material remains the 10 kHz preview bake.
The render produced 38 cut activations and 38 releases; eight settled variations
were retained. Event markers have the telemetry's **20 ms block resolution**.
They are not sample-exact ignition events.

After the base bake above, run from the Crankwave root with new output paths:

```sh
build/crankwave render \
  --engine data/engines/uaz469-reference-3m/engine.json \
  --scenario data/engines/uaz469-reference-3m/scenarios/neutral-limiter.json \
  --asset-root data/engines/uaz469-reference-3m \
  --output-directory ~/tmp/uaz-neutral-limiter --result-format json
node tools/bake-limiter-performance.mjs \
  ~/tmp/uaz469-reference-3m.crankwave \
  data/engines/uaz469-reference-3m/engine.json \
  ~/tmp/uaz-neutral-limiter ~/tmp/uaz-limiter-package ~/tmp/uaz-with-limiter.crankwave
build/crankwave verify-crankwave \
  --input ~/tmp/uaz-with-limiter.crankwave --result-format json
```

The post-cook tool uses native dry exhaust WAVs and telemetry, and the official
`pack-crankwave` carrier writer. `limiter/runtime.json` contains clip boundaries,
per-clip running reference RMS, PCM hashes, source JSON hash, base-carrier hash and
native request/telemetry identities. During playback the recorded dry exhaust is
unpitched, level-matched to running material and sent through the same live
exhaust response. Long overspeed cuts sustain a crossfaded portion of recorded
cut exhaust; actual host release selects the captured recovery. Gameplay owns
RPM and the 100 ms timer. This is a captured performance adapter, not the full
engine simulator running in Godot.

Refresh the harness at `http://127.0.0.1:4173/`, select the active carrier again,
press Play, select **N**, and hold **W** or set Accelerator to 100%. The status
reports limiter cuts. The manual RPM slider is an operating-point audition,
not a simulated neutral-throttle run. The original bank and Godot assets remain
backed up under `~/tmp/godot-uaz-audio/limiter-performance/`.
