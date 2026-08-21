# VEHICLEENGINE audio bridge

`web/runtime/vehicleengine-audio-engine.js` is the small consumer-facing playback
boundary for a packaged responsive engine. It deliberately does not import the
engine simulator, C API, browser workbench runtime, engine/scenario JSON, or a WASM
renderer.

```js
import { VehicleEngineAudioEngine } from "./runtime/vehicleengine-audio-engine.js";

const engine = await VehicleEngineAudioEngine.load(vehicleengineBytes);
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

The bridge integrates crank phase and RPM slope internally. At each RPM it first
interpolates every authored load lane's captured manifold-pressure coordinate, then
piecewise-interpolates those lanes by their declared normalized coordinates. This
keeps exact authored lanes exact while allowing throttle and load to change
independently. Manifold pressure is an internal representation detail; the harness
shows the derived value only as a diagnostic.

`render(frameCount)` is the constant-control offline interface. It always returns
exactly that many finite mono `Float32` samples at the package sample rate.
`process(operatingPoint, frameCount)` is the streaming interface: it advances the
provided start-to-end trajectory across exactly `frameCount` physical frames and
returns exactly the same number of frames. Its default frame count is 3,840 frames,
or 20 ms at the required 192 kHz package rate. Bridges should read that cadence
from `engine.processFrames` (also `format.processFrames`) and the fixed presentation
delay from `engine.latencyFrames` (also `format.latencyFrames`) rather than copying
those current numeric values into Web, Unity, or vehicle-physics adapters.

The responsive presentation works internally in fixed 32,768-frame batches.
Streaming playback therefore has one explicit 32,768-frame batch of zero-filled,
uniform output latency; it does not hold or quantize the 20 ms control endpoints.
Offline `render()` does not add that streaming latency. A caller must `reset()`
before switching between the two interfaces. `reset()` also clears playback state
and deterministically reseeds the current operating point. `format`, `minimumRpm`,
`maximumRpm`, `operatingPoint`, and `diagnostics()` provide the non-simulation
metadata a host needs.

This first bridge is the already-running audio surface. The host owns engine speed;
it does not solve vehicle or crank dynamics, and it does not infer ignition, starter,
or key-off physics from throttle. Those are separate optional bridge capabilities,
not hidden behavior in the three-input contract.

## Standalone browser harness

Run:

```bash
node scripts/serve-vehicleengine-harness.mjs
```

and open `http://127.0.0.1:4173/`. The server exposes only the harness and the audio
runtime module directory. It supplies the COOP/COEP headers required by shared audio
memory, but has no routes for repository engine data, scenarios, packages, or WASM.

The local file is transferred to a dedicated Worker. After complete carrier and
per-entry SHA-256 verification, the Worker renders 192 kHz PCM through the facade,
resamples it to the browser device rate, and maintains a bounded shared ring. The
AudioWorklet only drains that ring; it never runs package synthesis in the realtime
callback.
