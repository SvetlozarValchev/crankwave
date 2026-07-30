# Engine Sim Offline

Engine Sim Offline is a portable engine simulation and audio-rendering core. Its
product boundary is declarative JSON in and streamed PCM plus physical telemetry out.
The same C++ session runs unpaced for native WAV rendering and incrementally in a
fixed-memory WASM module for the interactive browser workbench.

The project is a source-informed rewrite. Original engine-sim is a capability and
behavior oracle, not a runtime dependency. The JSON contract preserves its useful
declarative engine-building capabilities without carrying `.mr`, Piranha, GUI state,
dead fields, or a general-purpose scripting language.

## Current status

The accepted engine-sim-equivalent exhaust audio path, low-order simulation, dyno
scenarios, block presentation pipeline, WAV publication, and telemetry are implemented.
Strict engine/scenario JSON compilation is the only production input path. A compiled
scenario can create an independent mutable `EngineSession`, whose bounded
`process_block()` method
owns the simulation, excitation, resampling, presentation, control, and telemetry
state. Native baking is an unpaced loop over that same method followed by transactional
artifact publication; there is no second whole-render implementation.

Session creation must explicitly select `finite_scenario` or `open_ended`. Native
baking uses the exact finite scenario recipe. Interactive playback currently admits
open-ended execution only for FreeEngine: it runs the same preparation, holds the
authored release-state snapshot, and preserves continuous physical and DSP state until
the caller restarts, destroys, or faults the session.

An exact-version C ABI now exposes strict JSON compilation, immutable engine/scenario
handles, mutable sessions, typed controls, caller-owned PCM/telemetry, and structured
diagnostics without leaking C++ types or exceptions. The Emscripten build exports that
same boundary with fixed 128 MiB memory. A pinned headless gate checks all public
exports, wasm32 numeric admission, exact native/WASM semantic state, target-specific
hashes, and tight continuous telemetry/PCM error bounds.

The browser workbench is implemented on that ABI. Its Worker owns compilation and the
mutable WASM session, primes a bounded shared PCM ring, and converts the selected
canonical 192 kHz bus to the device rate. The AudioWorklet only drains that ring.
Engine/scenario edits rebuild atomically; a failed compile leaves the current program
available. Inertial-dyno sessions admit live throttle, ignition, and fuel. Free-engine
sessions add live limiter state and external resisting torque while resolving crank
RPM from engine torque and engine-owned inertia. A `free_engine` scenario may add
`attached_inertia` and an `external_resisting_torque` trajectory; omission of either
means canonical positive zero. The compiler derives the engine baseline with the
versioned cycle-mean centered slider-crank kinetic-energy method, adds any attached
inertia, and gives only that resolved total to the crank integrator.

For the BMW M52B28 fixture, the derived engine baseline is
`0.2108686520185204 kg*m^2`; neutral uses no attached inertia and no external
resistance. The headless pristine-engine-sim gate requires a full-throttle
1,500-to-7,000-rpm crossing in `0.44`--`0.50 s`. This acceleration calibration does
not authorize a global Chen--Flynn loss retune: the remaining closed-throttle
coastdown discrepancy needs separate pumping and piston-friction work. A drivetrain,
gears, starter control, regulated idle, and arbitrary live presentation edits remain
explicit missing capabilities rather than UI-only approximations.

The compiler accepts the currently executable low-order topology without inspecting an
engine name and fails closed on unsupported capabilities. Cylinder and
exhaust/presentation-route execution is count-derived rather than fixed to the BMW
fixture's six cylinders and two routes. A separate inline-twin/one-route fixture reaches
the same session boundary. The BMW JSON migration fixture reproduces every
sound-bearing PCM byte of the user-approved inertial dyno. The native CLI resolves
engine-relative assets inside an explicit asset root and atomically publishes a new
output directory.

The current production flow is:

```text
engine.json + scenario.json + assets
                  |
          validate and compile
                  |
             EngineSession
                  |
        PCM buses + telemetry
            /             \
       native WAV       fixed-memory WASM
                              |
                   Worker + PCM ring
                              |
                       AudioWorklet + UI
```

The BMW now remains only as JSON data, an automated byte-identity migration fixture,
and historical evidence. The generic compiler intentionally establishes new request
and WAV-container identities instead of preserving BMW-specific provenance machinery;
the sound-bearing PCM is exact. There is no parallel profile production path.

The authoritative sequence and acceptance gates are in [PLAN.md](PLAN.md). The original
engine-sim capability mapping is in
[ENGINE_JSON_CAPABILITY_MATRIX.md](docs/contracts/ENGINE_JSON_CAPABILITY_MATRIX.md),
and the shared native/WASM lifecycle is in
[ENGINE_SESSION_API.md](docs/contracts/ENGINE_SESSION_API.md).

## Product boundaries

- `engine.json` owns reusable engine topology, physical parameters, source routing, and
  default audio presentation.
- `scenario.json` owns operating mode, controls/events, ambient state, render horizon,
  rates, quality, and seed.
- Session creation owns whether that finite recipe is executed to completion or used
  to initialize an open-ended FreeEngine bench. The choice is mandatory and never
  implemented by looping a finite clip.
- Referenced assets such as impulse responses are resolved relative to the engine
  document and content-verified.
- Offline and realtime execution use one block-processing implementation. Their
  lifetime is explicit; pacing and delivery remain adapter policy.
- Unsupported fields and topologies fail with path-addressed diagnostics. They are
  never accepted and ignored.
- Structural edits compile a new immutable engine. Timestamped controls modify a
  mutable session.

## Build

A C++20 compiler and CMake 3.21 or newer are required.

```bash
cmake -S . -B build -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The reproducible native/WASM parity gate uses a pinned Emscripten container:

```bash
scripts/verify-wasm-parity.sh
```

Build and serve the local browser workbench:

```bash
scripts/build-workbench.sh
node scripts/serve-workbench.mjs
```

Open the URL printed by the server. It serves only the staged `web`, `data`, and
`reference` trees and supplies the cross-origin-isolation headers required by shared
audio memory. The complete automated browser gate builds the pinned WASM target, runs
the focused JavaScript and real-module integration tests, checks those headers, and
drives the full UI in headless Chrome:

```bash
scripts/verify-browser-workbench.sh
```

Browser authored-capture export is a deterministic Float32 WAVE produced by a fresh
unpaced `finite_scenario` WASM session. It is separate from the open-ended interactive
session and does not silently truncate later live controls. Native publication remains
the authoritative PCM24 artifact path. Both use the same C++ session/DSP
implementation, while their admitted numeric environments retain independently exact
hashes and are compared by the native/WASM parity gate.

```bash
build/engine-sim-offline --help
build/engine-sim-offline --version
build/engine-sim-offline render \
  --engine data/engines/bmw-m52b28/engine.json \
  --scenario data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json \
  --asset-root . \
  --output-directory artifacts/bmw-json-dyno
```

The render command has one current syntax: all four named arguments are required
exactly once, their order is arbitrary, and the output directory must not already
exist. There are no profile selectors or legacy input aliases.

## Preserved reference

The accepted migration floor is:

```text
reference/oracles/bmw-m52b28/
  bmw-m52b28-last-good-ffcc45c-dyno-1500-6500rpm.wav
```

Its SHA-256 is:

```text
87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444
```

The accepted 8,640,000-byte PCM24 `data` chunk has SHA-256:

```text
176010069c88c99a3cc8262099fa5f02eba3af9517b1c92e148d88ace869756f
```

The older `bmw-m52b28-5th-gear-equivalent` reference has a different PCM
payload and is not the accepted migration oracle.

The generic JSON path deliberately produces a new deterministic whole-WAV identity
because its metadata carries generic compiler identities rather than obsolete
BMW-specific ones:

```text
simulation request SHA-256: 8cb2a5a7584b3f8e53a57b453b5e39986cb45e32723e76affea12cba31b5a816
audition WAV byte count:    8640586
audition WAV SHA-256:       f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552
```

Checkpoint 7 replaced obsolete two-route method names with truthful ordered N-route
identities. That metadata-only correction added 14 container bytes and changed the
whole-WAV hash; the PCM24 `data` chunk above remains byte-identical.
The operating-bench rig is resolved into package provenance, so adding the E36
evaluation rig changed the request identity without changing either audio hash.

Historical model, manifest, provenance, and listening records remain under `docs/` and
`reference/`. They document how the accepted implementation was established; they do
not define the new product API or retain old executable modes.
