# Engine Sim WASM

Engine Sim WASM is a portable engine simulation and audio-rendering core. Its
production authoring boundary is one native executable: declarative JSON goes in,
and audition PCM, physical telemetry, or a complete `.vehicleengine` carrier comes out.
Playback consumers use the simulator-free JavaScript modules packaged with the
carrier format; they do not run the engine simulation.

The project is a source-informed rewrite. Original engine-sim is a capability and
behavior oracle, not a runtime dependency. The JSON contract preserves its useful
declarative engine-building capabilities without carrying `.mr`, Piranha, GUI state,
dead fields, or a general-purpose scripting language.

## Current status

The accepted engine-sim-equivalent exhaust audio path, low-order simulation, dyno and
forward-vehicle scenarios, block presentation pipeline, WAV publication, and telemetry
are implemented.
Headless executable parity for the explicitly admitted pristine engine-sim capability
scope is accepted. [Slice 16](docs/SLICE_16_PARITY_LISTENING_GATE.md) froze seven
zero-saturation recordings spanning a
fixed-cam inline-six, fixed-cam V8, VTEC transition, governed load step, one-level
master-rod engine, sustained vehicle pull, and launch/shift procedure. The first
two post-parity fidelity gates are also accepted: production and cooker scenarios
use one canonical 20 kHz physics/capture path, and identity-stable cylinder-primary
lanes merge at an explicit collector before shared route propagation. A third gate
made captured plenum pressure audible and was accepted on its 2026-08-04 held-dyno
comparison, but a 2026-08-05 live throttle-reopen check exposed a clop/pop in place of
the expected bark. Exhaust-only comparisons restored the bark with both the 192 kHz
master and an exact 44.1 kHz engine-sim-order experiment, so the audible intake route
is withdrawn. Intake/plenum thermodynamics remain part of the simulation. The
mechanical-engine force-capture seam is complete but remains diagnostic-only pending
a defensible structural/radiation model. The first isolated queue-item-5 correction
is retained as an audibly neutral foundation: actual delayed exhaust-valve flow
modulates only the existing filtered-air term. It is not claimed as a noticeable
quality increase;
combustion-work and pressure-ratio coupling remain separate. Source processing,
acoustics, and delivery remain at 192 kHz.
This scoped claim does not include `.mr` or native-GUI parity, arbitrary unproven
mechanisms, fuel-volume-display parity, or audible-fidelity claims beyond the accepted
20 kHz exhaust path.
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

Exact C ABI v9 exposes strict JSON compilation, immutable engine/scenario handles,
mutable sessions, twelve typed controls, caller-owned PCM/session telemetry and exact
completed-cycle evidence, ordered forward-gear discovery, and structured diagnostics
without leaking C++ types or exceptions. Its descriptor identifies one of all seven
implemented motion modes. The
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

Certified one-level master-rod engines admit prescribed motion, `FreeEngine`,
`HeldDyno`, and `FreeVehicle`. Their dynamic path uses analytic articulated inertia,
per-cylinder piston-travel Chen--Flynn preparation evidence, and leaf-first coupled
piston/rod wall reactions. The radial-five warm FreeEngine procedure, 5.5 s HeldDyno
pull, and source-backed propeller/direct-drive FreeVehicle procedure are accepted.
HeldDyno selects the topology-specific
`bounded-held-dyno-speed-constraint-one-level-master-rod-v1` identity rather than
claiming the direct centered-slider constraint. Nested master rods and offset, geared,
or otherwise independent multi-crank master/slave mechanisms remain explicitly
outside the admitted topology.

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
the native session, C ABI v9, WASM wrapper, and Worker protocol v3 now publish live
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
catalog-admitted assets from its bundled content-addressed library by default and
atomically publishes a new output directory. An explicit asset root remains available
only as a developer override for authored local URIs.

The current production flow is:

```text
engine.json + scenario.json + installed assets
                         |
                 native C++ CLI
                    /          \
       audition + telemetry   .vehicleengine
                                    |
                          simulator-free playback
```

The installed distribution and GHCR image contain no Node.js runtime and no
simulation WebAssembly module. The source-tree browser workbench still builds a WASM
session as a development and interactive-simulation surface; it is not part of the
production authoring distribution or the `.vehicleengine` playback path.

Its canonical method quantum is 400 physics/capture frames at 20 kHz to 3,840 delivery
frames at 192 kHz: 20 ms per `process_block()` call. The browser's explicitly labelled
atlas-audition preset temporarily admits 200 frames at 10 kHz into the same 3,840-frame
presentation path so Source A can meet its realtime deadline; Baked B remains disabled
until the new atlas runtime lands. Native publication and every canonical scenario
remain 20 kHz. The accepted control/candidate
evidence is recorded in
[the post-parity fidelity rate gate](docs/POST_PARITY_FIDELITY_RATE_GATE.md). Historical
10 kHz parity recordings remain evidence only; the preview is not a second production
rate or compatibility path.

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
- Production CLI renders resolve referenced assets such as impulse responses by exact
  kind, stable ID, and authored SHA-256 through the bundled catalog. The developer
  asset-root override instead resolves authored URIs relative to the engine document;
  compilation still content-verifies declared hashes.
- Authoring systems query the release-bound, captioned IR palette with
  `engine-sim-offline inspect-ir-catalog --result-format json`. They select an exact
  release + ID + SHA-256 tuple and put only selected, route-used assets in engine JSON.
- Offline and realtime execution use one block-processing implementation. Their
  lifetime is explicit; pacing and delivery remain adapter policy.
- Unsupported fields and topologies fail with path-addressed diagnostics. They are
  never accepted and ignored.
- Structural edits compile a new immutable engine. Timestamped controls modify a
  mutable session.

## Build

A C++20 compiler and CMake 3.21 or newer are required. Native rendering and
responsive cooking have no Node.js dependency.

```bash
cmake -S . -B build -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The CLI build stages its catalog and shared runtime-audio package beside the
build-tree executable. A normal developer prefix install places the native CLI under
`bin` and resources under the versioned
`share/engine-sim-offline/<release>/` directory:

```bash
cmake --build build
cmake --install build --prefix artifacts/engine-sim-offline-install
```

The `engine_sim_offline_distribution` target requires a clean native source and
installed-resource closure. It emits the complete native-only relocatable prefix;
there is no separately supplied renderer pair. Every install publishes a byte-bound
`release.json` and `release.json.sha256`; consumers pin the semantic release identity
and the latter binding digest.

`CMAKE_INSTALL_BINDIR` and `CMAKE_INSTALL_DATADIR` may be changed to other relative
GNUInstallDirs locations. Catalog discovery uses
only paths relative to the installed bindir, so the complete prefix remains
relocatable.

The source-tree native/WASM parity gate remains available for development of the
interactive simulation workbench. It does not feed the release archive or OCI image:

```bash
scripts/verify-wasm-parity.sh
```

Build and serve the local browser workbench (development Node.js tooling only):

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
build/engine-sim-offline inspect-ir-catalog --result-format json
build/engine-sim-offline render \
  --engine data/engines/bmw-m52b28/engine.json \
  --scenario data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json \
  --output-directory artifacts/bmw-json-dyno
```

The three named render arguments above are required exactly once and may appear in any
order. `--asset-root <directory>` is an optional, explicit developer override; when it
is absent, the executable discovers and enforces the bundled catalog. The output
directory must not already exist. There are no profile selectors or legacy input
aliases. The catalog format, discovery rules, and integrity boundary are specified in
[BUILTIN_ASSET_CATALOG_V1.md](docs/contracts/BUILTIN_ASSET_CATALOG_V1.md). Catalog
membership is a technical runtime allowlist, not a licensing or provenance assertion.

The stable production cooker is one native command. It compiles the engine, derives
the redline-affine responsive profile, captures all required held, directional, and
lifecycle material, packages it, verifies the carrier, and atomically publishes only
the finished `.vehicleengine`:

```bash
build/engine-sim-offline bake-vehicleengine \
  --engine data/engines/bmw-m52tub28-cleanroom/engine.json \
  --output artifacts/m52tu.vehicleengine \
  --result-format json

build/engine-sim-offline verify-vehicleengine \
  --input artifacts/m52tu.vehicleengine \
  --result-format json
```

The installed command has the same interface and discovers only its own
manifest-bound assets:

```bash
artifacts/engine-sim-offline-install/bin/engine-sim-offline bake-vehicleengine \
  --engine /absolute/path/to/engine.json \
  --output /absolute/path/to/new.vehicleengine \
  --deadline-unix-ms 1800000000000 \
  --result-format json
```

ESO deterministically derives the versioned `interactive-preview-redline-v1` profile
from the engine's declared operating range; the final anchor is exactly the redline
and the capture grid remains fixed at 11 anchors.
The selection contract is specified in
[RESPONSIVE_PROFILE_SELECTION_V2.md](docs/contracts/RESPONSIVE_PROFILE_SELECTION_V2.md).

The release target stages the full relocatable prefix and writes
`build/distribution/<config>/engine-sim-offline-<release>.tar` plus a
one-line `.tar.sha256` sidecar. Archive member order, timestamps, ownership, and
permissions are normalized; rebuilding the same admitted source/toolchain closure
produces identical archive bytes. An ordinary `cmake --install` remains the separate
development-prefix path described above.

Pushing an exact `v<release>` tag runs two independent native release builds, requires
byte-identical archives, verifies the installed v2 manifest, and then publishes the
tar, its digest, the exact manifest, and a GitHub/OCI binding. The corresponding GHCR
image is built only from that verified tar. It is a generic, non-root Linux/amd64
Debian slim image with the native CLI as its entrypoint:

```bash
docker run --rm \
  ghcr.io/svetlozarvalchev/engine-sim-offline@sha256:<manifest-digest> \
  --version
```

Release tags are discovery aids. Production callers pin the OCI digest and retain the
matching semantic release, distribution SHA-256, and `release.json` SHA-256 recorded
in `engine-sim-offline-<release>.binding.json`.

`--asset-root` remains an explicit developer override for source-tree experiments.
Production adapters omit it, pin the installed release and manifest digest, and use
the one-step `vehicleengine-bake-workflow.v2.json` contract.

`inspect-vehicleengine` authenticates and reports the carrier structure and index;
`verify-vehicleengine` additionally hashes every payload and validates the package
descriptor's runtime-manifest binding. The carrier format is specified in
[VEHICLEENGINE_CONTAINER_V1.md](docs/contracts/VEHICLEENGINE_CONTAINER_V1.md).

### Audition a VEHICLEENGINE as an external consumer

The standalone audio harness proves the other side of the boundary: it accepts one
local `.vehicleengine` file and supplies only RPM, normalized throttle, and normalized
load. It does not load engine JSON, scenario JSON, the C API, or a simulation WASM
module:

```bash
node scripts/serve-vehicleengine-harness.mjs
```

Open `http://127.0.0.1:4173/`, choose the carrier, press **Play**, and move the three
operating-point controls. The browser verifies the complete carrier before a Worker
renders its packaged mono PCM. The public `VehicleEngineAudioEngine` facade is also usable
directly by another bridge or host. Its API and control semantics are documented in
[VEHICLEENGINE_AUDIO_BRIDGE.md](docs/VEHICLEENGINE_AUDIO_BRIDGE.md).

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

The generic JSON path's current exhaust-only v4 listening master has its own
deterministic identity. The historical migration PCM above remains an upstream
raw/stem oracle; it is not relabelled as the current audition output:

```text
simulation request SHA-256: b08b197a2cf4df993112d9b345c095e2290a5f426ebb3b4462a60af6faed89da
audition WAV byte count:    8640598
audition WAV SHA-256:       58ec677b0565fad0e9a3b9eedba9590d5af9891c647185ebdb9c98f7e469cde7
audition PCM24 SHA-256:     52fef731caf12b9a6353e0ed3a928039db74277193d99847b244f852edebf01f
```

Checkpoint 7 and the operating-bench rig historically advanced metadata and request
identities without changing PCM. The later v4 master is a sound-bearing change: it
runs the user-approved stateful leveler and tanh stage at 192 kHz, and the rejected
pressure-derived intake route is absent from both authoring and presentation.

Historical model, manifest, provenance, and listening records remain under `docs/` and
`reference/`. They document how the accepted implementation was established; they do
not define the new product API or retain old executable modes.

## License

Engine Sim WASM is proprietary software. Copyright (c) 2026 Svetlozar Valchev.
All rights are reserved; see [LICENSE](LICENSE). Third-party materials retain their
own licenses and notices, including the MIT-licensed upstream engine-sim material
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
