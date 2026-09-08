# Crankwave

Crankwave is a deterministic C++ engine simulator and responsive engine-audio
authoring system. It compiles declarative engine and scenario JSON, renders
audition audio and telemetry, and can bake a complete `.crankwave` package for
simulator-free playback.

The native CLI and browser workbench use the same simulation and DSP core. A
baked package contains verified audio material and runtime metadata; consumers
provide only RPM, throttle, and load and do not run the simulator.

## What it provides

- Strict JSON compilation for engine topology, physics, controls, and audio
  presentation.
- Deterministic native and WebAssembly execution through one block-processing
  session API.
- Native WAV and telemetry rendering for finite scenarios.
- Responsive held, directional, startup, shutdown, and starter audio baking.
- A hash-bound `.crankwave` carrier with simulator-free JavaScript playback.
- A browser authoring workbench and a standalone package-consumer harness.
- Explicit failure for unsupported mechanisms or malformed contracts.

Crankwave is a source-informed rewrite of
[AngeTheGreat's engine-sim](https://github.com/ange-yaghi/engine-sim). The
upstream project is a behavior and capability reference, not a runtime
dependency. Crankwave does not embed the upstream GUI, Piranha scripting
runtime, or `.mr` input path.

## Architecture

```text
engine.json + scenario.json + installed assets
                         │
                    Crankwave CLI
                    /           \
         WAV + telemetry       .crankwave
                                    │
                         simulator-free playback
                         (RPM + throttle + load)
```

Engine and scenario documents compile into immutable models. Each execution
creates an independent mutable session that owns simulation, excitation,
resampling, presentation, controls, and telemetry. Native rendering runs that
session without pacing; the browser Worker runs the same API behind a shared
PCM ring consumed by an AudioWorklet.

The production path uses 20 kHz physics/capture blocks and 192 kHz presentation
audio. The detailed model is documented in [MODEL.md](MODEL.md); the curated
documentation map is in [`docs/README.md`](docs/README.md).

## Build

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- Node.js 20 or newer and GNU tar for the one-time source asset download

```bash
node scripts/source-assets.mjs fetch
cmake -S . -B build -DCRANKWAVE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The native executable is `build/crankwave`.

Audio and binary test fixtures are distributed as versioned GitHub Release
attachments. Cloning the repository does not download them or require Git LFS.
The setup command above restores their original paths, checks the archive and
every file against `source-assets.lock.json`, and reuses verified local files
on subsequent runs. No GitHub token is required.

See [source asset setup and maintenance](docs/SOURCE_ASSETS.md) for offline use
and publishing updated fixtures.

## Render a scenario

```bash
build/crankwave render \
  --engine data/engines/bmw-m52b28/engine.json \
  --scenario data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json \
  --output-directory artifacts/bmw-json-dyno
```

The three named arguments are required exactly once. `--asset-root` is an
optional developer override; normal builds resolve content-addressed assets
from the installed catalog.

## Bake and verify a package

```bash
build/crankwave bake-crankwave \
  --engine data/engines/bmw-m52tub28-cleanroom/engine.json \
  --output artifacts/m52tu.crankwave \
  --result-format json

build/crankwave verify-crankwave \
  --input artifacts/m52tu.crankwave \
  --result-format json
```

Additional package commands:

- `pack-crankwave` packages an already prepared responsive-audio tree.
- `inspect-crankwave` authenticates and reports the carrier structure.
- `verify-crankwave` also hashes every member and validates runtime bindings.

The carrier contract is documented in
[CRANKWAVE_CONTAINER_V1.md](docs/contracts/CRANKWAVE_CONTAINER_V1.md).

## SPARQ integration

<a href="https://playsparq.com">
  <img src="docs/assets/sparq-logo.png" alt="SPARQ logo" width="88">
</a>

### Crankwave for SPARQ

**[sparq-crankwave](https://github.com/SvetlozarValchev/sparq-crankwave)**
brings engine editing, deterministic baking, and vehicle runtime integration to
the [SPARQ game engine](https://playsparq.com).

## Browser workbench

Build and serve the local authoring workbench:

```bash
scripts/build-workbench.sh
node scripts/serve-workbench.mjs
```

The server prints its localhost URL and supplies the cross-origin-isolation
headers required for shared audio memory. The full browser gate is:

```bash
scripts/verify-browser-workbench.sh
```

To audition a baked package as an external consumer:

```bash
node scripts/serve-crankwave-harness.mjs
```

Choose a local `.crankwave` file, press **Play**, and adjust RPM, throttle, and
load. The page verifies the complete package before producing audio. The public
bridge API is described in
[CRANKWAVE_AUDIO_BRIDGE.md](docs/CRANKWAVE_AUDIO_BRIDGE.md).

## Install and release layout

```bash
cmake --install build --prefix artifacts/crankwave-install
```

The install contains:

```text
bin/crankwave
share/crankwave/<release>/
```

The `crankwave_distribution` CMake target emits
`crankwave-<release>.tar` with a SHA-256 sidecar. Installed releases include a
byte-bound `release.json` and `release.json.sha256`.

## Source and asset provenance

Imported upstream impulse responses retain their upstream MIT notice and exact
catalog identities. The shared starter uses Ika.Komura's CC0 recording
`Car not starting.wav`; its source identity, processing recipe, loop markers,
and derived payload hash are recorded in the runtime manifest.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for source links and license
details.

## License

Crankwave is available under the [MIT License](LICENSE).
