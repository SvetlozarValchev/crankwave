# BMW M52B28 exhaust-acoustic source matrix

Status: **frozen M5 checkpoint**

Scope: canonical product-side BMW exhaust-acoustic listening renders

The source-matrix semantic ID is
`bmw-m52b28-exhaust-acoustic-source-matrix`. Its content digest is the SHA-256
of this file's exact bytes. This matrix replaces the reference-oracle matrix; it
does not retain reference route, stem, master, or factory identities.

This is deliberately an exhaust-only checkpoint, not a claim of complete engine
audio. Its distribution intent is `local_evaluation` while the BMW provenance
still contains evidence whose redistribution rights are not established.

## Required audio

All four artifacts are mono, exactly 2,880,000 frames (15 seconds) at 192,000 Hz.
The outlet stems and raw master are little-endian Float32 WAVE; the audition master
is little-endian PCM24 WAVE. No required artifact is diagnostic.

The acoustic network supplies pressure in Pa. Float32 publication applies the
assembly-owned calibration exactly once: `sample = pressure_pa / 256 Pa`. Therefore
`1.0` in either outlet stem or the raw master represents 256 Pa. The audition master
starts from the same calibrated raw sum and then applies only its one common listening
gain and fades.

| Owner | Owner kind | Artifact role | Signal contract |
|---|---|---|---|
| `exhaust.outlet.front` | exhaust outlet route | `exhaust.outlet.front.pressure` | Radiated one-metre pressure calibrated at 256 Pa per full scale |
| `exhaust.outlet.rear` | exhaust outlet route | `exhaust.outlet.rear.pressure` | Radiated one-metre pressure calibrated at 256 Pa per full scale |
| `master.engine.raw` | raw engine output bus | `master.engine.raw` | Time-coherent, deterministic sum of both calibrated outlet stems before audition-only processing |
| `master.engine.audition` | audition engine output bus | `master.engine.audition` | Listening derivative of the raw engine master using the separately declared mastering transform |

The route order is front, then rear. The output-bus order is raw, then audition.
Each owner owns exactly the one artifact shown above; the matrix requires exactly
these four artifacts.

## Explicit omissions

| Semantic ID | Kind | Rationale |
|---|---|---|
| `intake` | source route | No intake-acoustic route is implemented or inferred from the exhaust or master. |
| `mechanical.engine` | source route | No separately observable engine-mechanical route is implemented. |
| `mechanical.starter` | source route | No starter route is present in this already-running-engine checkpoint. |
| `drivetrain` | external system | Transmission and drivetrain radiation are outside the engine asset. |
| `vehicle.tire-road` | external system | Tire, road, and vehicle radiation are outside the engine asset. |
| `presentation.spatial-field` | presentation scene | No cabin, environment, microphone scene, or spatial field is claimed. |
| `scenario.non-pull-behaviors` | scenario behavior | Startup, shutdown, fuel cut, limiter, and dedicated overrun behavior are not established by this checkpoint. |

These omissions remain explicit in every resolved output contract. A later source
model must add and validate a missing route rather than hide it in either master.
