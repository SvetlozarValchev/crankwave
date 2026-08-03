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

The accepted engine-sim-equivalent exhaust audio path, low-order simulation, dyno and
forward-vehicle scenarios, block presentation pipeline, WAV publication, and telemetry
are implemented.
Strict engine/scenario JSON compilation is the only production input path. A compiled
scenario can create an independent mutable `EngineSession`, whose bounded
`process_block()` method
owns the simulation, excitation, resampling, presentation, control, and telemetry
state. Native baking is an unpaced loop over that same method followed by transactional
artifact publication; there is no second whole-render implementation.

Session creation must explicitly select `finite_scenario` or `open_ended`. Native
baking uses the exact finite scenario recipe. Interactive playback admits open-ended
FreeEngine, HeldDyno, and FreeVehicle execution: it runs the same authored pre-audible
history, holds each applicable right-continuous control snapshot at the audible
handoff, and preserves continuous physical and DSP state until the caller restarts,
destroys, or faults the session. A warm dynamic bench may physically release at its
fixed preparation horizon and continue acquisition before that later audible handoff.

Exact C ABI v4 exposes strict JSON compilation, immutable engine/scenario handles,
mutable sessions, twelve typed controls, caller-owned PCM/session telemetry, ordered
forward-gear discovery, and structured diagnostics without leaking C++ types or
exceptions. Its descriptor identifies one of all seven implemented motion modes. The
Emscripten build exports that same boundary with fixed 128 MiB memory. A pinned headless
gate checks all public exports, wasm32 numeric admission, exact native/WASM semantic
state, target-specific hashes, and tight continuous telemetry/PCM error bounds.

The browser workbench is implemented on that ABI. Its Worker owns compilation and the
mutable WASM session, primes a bounded shared PCM ring, and converts the selected
canonical 192 kHz bus to the device rate. The AudioWorklet only drains that ring.
Engine/scenario edits rebuild atomically; a failed compile leaves the current program
available. Worker protocol v3 publishes the explicit motion mode, capabilities,
ordered gear inventory, and nullable mode-specific telemetry, and sends each nonempty
control group through one atomic C-ABI batch. Inertial-dyno sessions admit live
throttle, ignition, and fuel. Free-engine sessions add live limiter state, external
resisting torque, and a momentary starter when the compiled engine declares cranking
hardware, while resolving crank RPM from engine torque and engine-owned inertia.
HeldDyno sessions admit throttle, ignition, fuel, target RPM, and separate maximum
absorbing and driving torques. FreeVehicle sessions admit throttle, ignition, fuel,
limiter, selected gear, and clutch, plus starter and service brake only when their
compiled hardware exists. The workbench renders those controls only when the descriptor
admits them, populates gears from the published inventory, displays the returned
dyno/vehicle sidecars, and groups repository scenarios into named procedures. Session
lifetime is explicit: interactive presets use continuous execution, while named
procedures retain their complete finite timelines and offer a fresh replay. Supported
dynamic modes can be deliberately rebuilt with either lifetime; JavaScript never
infers lifetime from motion mode or synthesizes physics. A `free_engine` scenario may add
`attached_inertia` and an `external_resisting_torque` trajectory; omission of either
means canonical positive zero. The compiler derives the engine baseline with the
versioned mechanism-family cycle-mean kinetic-energy method, adds any attached inertia,
and retains that sum as the cycle-mean validation/provenance reference. Direct journals
use the centered-slider method; certified one-level master rods use the articulated
method. The free-running integrator evaluates the matching mechanism's analytic
`M(theta)` and `dM/dtheta` at every left boundary and adds the constant attachment
there; it does not flatten the moving crank, rods, and pistons into the cycle-mean
value.

For the BMW M52B28 fixture, the derived engine baseline is
`0.2108686520185204 kg*m^2`; neutral uses no attached inertia and no external
resistance. The headless interactive-recipe smoke requires a full-throttle
1,500-to-7,000-rpm crossing in `0.44`--`0.50 s`; its short part-throttle preparation
differs from the controlled pristine oracle and does not establish transient parity.
Gas-exchange pumping is already present in indicated pressure-volume work. FreeEngine
motion now applies pristine engine-sim's authored crank friction plus its executable
one-step-lagged piston-wall law; the wall reaction is resolved by the admitted
centered-slider inverse dynamics instead of importing the legacy constraint solver.
Direct held and coast traces put that replacement within `1.44%` aggregate wall-force
error and below `0.4%` one-step friction-force error. Chen--Flynn is retained only as
fixed warm-preparation evidence; its accountant is discarded at dynamic release, and
released cycle integration remains unavailable. Configuration-dependent
mechanism inertia now follows the pristine one-degree-of-freedom equation
`Q = M(theta)*alpha + 0.5*dM/dtheta*omega^2`; an audited 6,000-rpm coast tick predicts
pristine acceleration within `0.054%`. The controlled response gate passes: WOT
differs from pristine by `0.0134 s`, all coast crossings by at most `0.0049 s`, and
the long natural-balance mean by `1.079 RPM`. Reverse vehicle motion, wheel slip,
driveline compliance, and arbitrary live presentation edits remain explicit missing
capabilities rather than JavaScript approximations. The
stopped/stalled state and pristine unilateral target-speed starter now execute through
the same FreeEngine runtime; starter engagement and release remain explicit authored
or live controls. The selected throttle controller may instead be pristine's stateful governor:
the public normalized demand becomes its speed command, while telemetry separately
reports that request and the governor-resolved throttle opening. The Kohler CH750
fixture exercises this path through a settled 12 N m load step.

Certified one-level master-rod engines currently admit prescribed motion,
`FreeEngine`, and `HeldDyno`. Their dynamic path uses analytic articulated inertia,
per-cylinder piston-travel Chen--Flynn preparation evidence, and leaf-first coupled
piston/rod wall reactions. The radial-five 52,000-frame warm FreeEngine procedure and
the 5.5 s HeldDyno pull are accepted. HeldDyno selects the topology-specific
`bounded-held-dyno-speed-constraint-one-level-master-rod-v1` identity rather than
claiming the direct centered-slider constraint. `FreeVehicle`, nested master rods, and
multi-crank master/slave remain separately closed.

The compiler accepts the currently executable low-order topology without inspecting an
engine name and fails closed on unsupported capabilities. Cylinder and
exhaust/presentation-route execution is count-derived rather than fixed to the BMW
fixture's six cylinders and two routes. A separate inline-twin/one-route fixture reaches
the same session boundary. Direct centered rods execute on inline, V, opposed, and
custom explicit bank axes; a pristine-derived Subaru EJ25 fixture proves the opposed
bank/journal mapping across a bounded dynamic capture. Banks may retain distinct
bank-local heads, ports, standard valvetrains, and cams. The executable core now
materializes one chamber/runner/flow profile per bank and binds each cylinder through
its bank identity. Chamber volume, intake/exhaust runner geometry, flow curves, and
the two flow-sampling radii may differ by bank. Each referenced piston retains its own
calibrated blowby restriction through the resolved cylinder and runtime flow lane.
Standard fixed valvetrains compile distinct physical same-role
cams into deterministic first-use profile pools with explicit per-cylinder bindings;
each physical cam still shares one exact profile across its own lobes. They accept either
harmonic lobe generators or explicit sampled angle-to-lift profiles through the same
runtime sampler.
Finite JSON `held_dyno` scenarios now drive that same crank through a bounded signed
speed constraint. The dyno may absorb or drive only within authored limits, reports
the exact opposite reaction torque, and exposes achieved RPM when saturated. The first
BMW procedure covers a target-driven pull, exact plateau, lift, and unforced overrun;
the native session, C ABI v4, WASM wrapper, and Worker protocol v3 now publish live
target and torque-limit commands plus nullable final-step dyno telemetry.

Finite JSON `free_vehicle` scenarios couple the same crank to a forward-only vehicle,
ordered transmission, bounded clutch, passive road load, and optional one-sided service
brake. The BMW rig publishes five forward gears in authored order with ratios `4.21`,
`2.49`, `1.66`, `1.24`, and `1.00`. Released blocks carry nullable final-step vehicle,
gear, clutch, slip, road-load, speed, and distance telemetry; preparation and
non-vehicle modes report no vehicle sidecar.

The canonical unpaced capture set contains six finite BMW M52TUB28 procedures covering
non-fired crank, startup/catch, settled idle, loaded rise, part load, unforced coast,
neutral limiter, limiter lift/recovery, and key-off shutdown. They are ordinary JSON
scenarios rendered through fresh finite sessions and can run independently in parallel;
the measured six-job native batch completes in approximately `30.4 s` on the
development PC.

The BMW JSON migration fixture reproduces every
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
  to initialize an admitted open-ended FreeEngine, HeldDyno, or FreeVehicle bench. The
  choice is mandatory and never implemented by looping a finite clip.
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
simulation request SHA-256: df65b324c78d43c1b651ab69d217483d3b489199fbb1c5a2a4ad9da7e2143efd
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
