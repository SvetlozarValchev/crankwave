# CRANKWAVE audio bridge

`web/runtime/crankwave-audio-engine.js` is the small consumer-facing playback
boundary for a packaged responsive engine. It deliberately does not import the
engine simulator, C API, browser workbench runtime, engine/scenario JSON, or a WASM
renderer.

```js
import { CrankwaveAudioEngine } from "./runtime/crankwave-audio-engine.js";

const engine = await CrankwaveAudioEngine.load(crankwaveBytes);
engine.setOperatingPoint({
  rpm: 2_500,
  throttle01: 0.4,
  load01: 0.6,
});

const mono192k = engine.render(8_192);
```

Streaming bridges should submit one dense operating-point endpoint every 20 ms:

```js
const mono192k = engine.process({
  rpm: 2_550,
  throttle01: 0.42,
  load01: 0.61,
});
```

The required inputs are intentionally limited to:

- `rpm`: externally owned engine speed inside the carrier's published range;
- `throttle01`: normalized driver demand, used by the packaged directional
  transient policy;
- `load01`: normalized running load, used to select the packaged held texture.

The bridge integrates crank phase and RPM slope internally. For the
`manifold-pressure-pa-absolute` load coordinate, at each RPM it first
interpolates every authored load lane's captured manifold-pressure coordinate, then
piecewise-interpolates those lanes by their declared normalized coordinates. This
keeps exact authored lanes exact while allowing throttle and load to change
independently. Manifold pressure is an internal representation detail; the harness
shows the derived value only as a diagnostic.

Experimental mechanical diesel carriers declare `engine.loadCoordinate` as
`normalized-fueling`. For these, `load01` selects fuel directly and manifold
pressure remains an independently interpolated observation. `throttle01` has no
separate acoustic effect in this steady diesel model; the driving host maps demand
to RPM and fueling. The harness labels the manual control Fueling and hides the
air-throttle control. See [mechanical diesel sound](model/MECHANICAL_DIESEL_SOUND.md).

`render(frameCount)` is the constant-control offline interface. It always returns
exactly that many finite mono `Float32` samples at the package sample rate.
`process(operatingPoint, frameCount)` is the streaming interface: it advances the
provided start-to-end trajectory across exactly `frameCount` physical frames and
returns exactly the same number of frames. Its default frame count is 3,840 frames,
or 20 ms at the required 192 kHz package rate. Bridges should read that cadence
from `engine.processFrames` (also `format.processFrames`) and the fixed presentation
delay from `engine.latencyFrames` (also `format.latencyFrames`) rather than copying
those current numeric values into Web, Unity, or vehicle-physics adapters.

The canonical responsive presentation uses 32,768-frame batches; the mechanical
diesel presentation uses 3,840-frame batches. The experimental 48 kHz carrier
defaults to 8,192-frame batches. Streaming playback has one explicit
`engine.blockFrames` batch of zero-filled,
uniform output latency; it does not hold or quantize the 20 ms control endpoints.
Offline `render()` does not add that streaming latency. A caller must `reset()`
before switching between the two interfaces. `reset()` also clears playback state
and deterministically reseeds the current operating point. `format`, `minimumRpm`,
`maximumRpm`, `operatingPoint`, and `diagnostics()` provide the non-simulation
metadata a host needs.

For a 48 kHz responsive carrier with fixed transfer spectra, a game host can use
smaller convolution partitions:

```js
const engine = await CrankwaveAudioEngine.load(crankwaveBytes, {
  playbackBlockFrames: 1_024,
});
// Or reuse one loaded package across voices:
// new CrankwaveAudioEngine(loaded, { playbackBlockFrames: 1_024 });
```

This makes the internal streaming delay 21.33 ms at 48 kHz. Controls still arrive
every 20 ms, and output buffering keeps the delay uniform. Preparation reconstructs
all FIR taps from the verified spectrum once and shares partition spectra between
voices using the same loaded runtime. It preserves the original warm-up length,
crank phase, recorded starter and limiter, and the complete filter tail. The
carrier and its manifests remain unchanged. Supported overrides are powers of two
from 256 to 4,096 that divide the original batch; unsupported runtimes reject the
option. The consumer API uses the carrier's original batching unless this option
is supplied.

The standalone harness selects 1,024-frame partitions for supported 48 kHz
responsive carriers. This reduces presentation delay and processing bursts;
browser output lead and device latency are additional. Cycle-selection caching
and exact steady-state blend reuse apply to both 48 kHz and 192 kHz playback.

This first bridge is the already-running audio surface. The host owns engine speed;
it does not solve vehicle or crank dynamics, and it does not infer ignition, starter,
or key-off physics from throttle. Those are separate optional bridge capabilities,
not hidden behavior in the three-input contract.

## Standalone browser harness

Run:

```bash
node scripts/serve-crankwave-harness.mjs
```

and open `http://127.0.0.1:4173/`. The server exposes only the harness and the audio
runtime module directory. It supplies the COOP/COEP headers required by shared audio
memory, but has no routes for repository engine data, scenarios, packages, or WASM.

The local file is transferred to a dedicated Worker. After complete carrier and
per-entry SHA-256 verification, the Worker renders at the carrier's delivery rate through the facade,
resamples it to the browser device rate, and maintains a bounded shared ring. The
AudioWorklet only drains that ring; it never runs package synthesis in the realtime
callback.

Driving mode is an external audition host in `web/crankwave-harness/driving-model.js`.
It uses the Godot UAZ's four forward ratios (4.124, 2.641, 1.58, 1), 5.125 civil
axle, 0.4 m tires, 1,650 kg loaded mass, rolling/air resistance, engine braking and
service brakes. The torque curve follows the UMZ-451M's published 166.7 Nm plateau
at 2,200–2,500 RPM and 75 PS / 55.2 kW rating at 4,000 RPM
([1976 technical handbook, table 1](https://djvu.online/file/OWQKOgdUHw5ix)).
Intermediate curve points, 110 kg above the 469B's 1,540 kg curb mass for driver/gear,
85% drivetrain efficiency, and the aerodynamic assumptions are authored estimates.
It does not claim measured acceleration or a complete engine model.

Pedal attack takes 0.8 s to reach full demand, with faster release and a progressive
power curve. Launch RPM rises at about 900 RPM/s and initial clutch take-up is
limited to 1/s; gear re-engagement and clutch release remain faster. Light pedal
still takes up the clutch sufficiently to pull away. The automatic driver upshifts
at 2,000–2,600 RPM depending on pedal, downshifts at 1,050–1,300 RPM with a 200 RPM
post-downshift margin, and pauses 1 s between shifts. It lifts throttle, opens the
clutch, selects the next gear after 0.25 s, then re-engages from 0.4 to 0.85 s. It is a small
longitudinal audition model, not the full Crankwave combustion/vehicle simulator
or a vehicle specification extracted from the carrier. It is used for all selected
engines, with RPM targets bounded to their published coverage and UAZ power capped
above its normal range; the dashboard
identifies the UAZ rig explicitly. The original `.crankwave` sound is unchanged.

The Worker advances this model at 120 Hz on rendered audio time and submits dense
endpoints through `engine.process()` at its published `processFrames` cadence.
This avoids wall-clock/tab throttling changing the driving speed or quantizing
gear transitions to presentation batches. Manual mode uses the same streaming
interface. Browser output lead is 120 ms, in addition to the package's advertised
presentation latency. Stop pauses driving; Reset returns to rest and clears pedals.
Keyboard pedals release on blur/hidden tabs; slider positions remain deliberately
latched. Switching to Drive mode starts a fresh drive. D and N switch gear selection.

Checks:

```sh
node --test web/tests/runtime/driving-model.test.mjs
node web/tests/integration/crankwave-harness.integration.mjs \
  http://127.0.0.1:4173/ /path/to/engine.crankwave
```


## Optional captured limiter playback

`engine.limiter` is either `null` or `{ activationRpm, cutSeconds, idleRpm }` from
the verified carrier's optional `limiter_package_path`. A host with that material
may pass `limiterCutActive: true/false` alongside RPM/throttle/load. Requesting a
cut from a carrier without recorded limiter material fails explicitly. The host
owns torque interruption, RPM motion, and the hold timer; the audio facade does
not infer overspeed or manipulate vehicle physics.

`tools/bake-limiter-performance.mjs` attaches native free-engine exhaust captures
and their telemetry to a base bank through the official `pack-crankwave` command.
The PCM remains on its captured 192 kHz time axis. A cut chooses the next recorded
variation; extended cuts sustain captured cut exhaust with a short crossfade;
release selects recorded combustion recovery and hands back to normal textures.
Each clip is level-matched once at entry using its firing reference RMS and the
running dry signal's 25 ms RMS estimate. Both use the same authored exhaust
convolution and mastering. Playback does not mute the output bus.

For the UAZ the limiter is 4,500 RPM / 100 ms. The facade accepts up to 4,700 RPM
with this family, allowing the host to cross the threshold. Held and directional
cell lookup still clamps to the original captured domain; crank phase follows
the supplied RPM. This does not create new held anchors above 4,200 RPM.
`diagnostics().runtime.limiter` reports availability and trigger count.

The harness's **N + full accelerator** mode now crosses the threshold and
repeatedly cuts/recovers when this capability is available.

```sh
node --test web/tests/runtime/limiter-performance-runtime.test.mjs
CRANKWAVE_TEST_CAPTURED_LIMITER=1 node web/tests/integration/crankwave-harness.integration.mjs \
  http://127.0.0.1:4173/ artifacts/uaz469/uaz469.crankwave
```
