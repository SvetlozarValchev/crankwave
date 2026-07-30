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
They are currently reached through a temporary hardcoded BMW M52B28 profile.

The active cutover replaces that profile with:

```text
engine.json + scenario.json + assets
                  |
          validate and compile
                  |
             EngineSession
                  |
        PCM buses + telemetry
            /             \
       native WAV       WASM preview
```

The BMW remains only as an automated byte-identity migration fixture. Once JSON
compilation reproduces its resolved request and accepted output exactly, the executable
BMW factory is removed. There will not be parallel profile and JSON production paths.

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

The JSON CLI commands described by the active plan are not implemented at this
checkpoint. The current shell truthfully reports the unavailable serialized-input
route:

```bash
build/engine-sim-offline --help
build/engine-sim-offline --version
build/engine-sim-offline render
```

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

Historical model, manifest, provenance, and listening records remain under `docs/` and
`reference/`. They document how the accepted implementation was established; they do
not define the new product API or retain old executable modes.
