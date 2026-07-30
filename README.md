# Engine Sim Offline

Engine Sim Offline is a portable engine simulation and audio-rendering core. Its
product boundary is declarative JSON in and streamed PCM plus physical telemetry out.
The same C++ session will run unpaced for native WAV rendering and incrementally in
WASM for an interactive browser workbench.

The project is a source-informed rewrite. Original engine-sim is a capability and
behavior oracle, not a runtime dependency. The JSON contract preserves its useful
declarative engine-building capabilities without carrying `.mr`, Piranha, GUI state,
dead fields, or a general-purpose scripting language.

## Current status

The accepted engine-sim-equivalent exhaust audio path, low-order simulation, dyno
scenarios, block presentation pipeline, WAV publication, and telemetry are implemented.
Strict engine/scenario JSON compilation is the only production input path. A compiled
scenario creates one mutable `EngineSession`, whose bounded `process_block()` method
owns the simulation, excitation, resampling, presentation, control, and telemetry
state. Native baking is an unpaced loop over that same method followed by transactional
artifact publication; there is no second whole-render implementation.

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
       native WAV       planned WASM preview
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
- Referenced assets such as impulse responses are resolved relative to the engine
  document and content-verified.
- Offline and realtime execution use one block-processing implementation. Only pacing
  and delivery differ.
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
simulation request SHA-256: a07360b0a7a780850e601e1316113f4541b852195a361c79549005ea1f487c7e
audition WAV byte count:    8640586
audition WAV SHA-256:       f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552
```

Checkpoint 7 replaced obsolete two-route method names with truthful ordered N-route
identities. That metadata-only correction added 14 container bytes and changed the
whole-WAV hash; the PCM24 `data` chunk above remains byte-identical.

Historical model, manifest, provenance, and listening records remain under `docs/` and
`reference/`. They document how the accepted implementation was established; they do
not define the new product API or retain old executable modes.
