# Engine Sim Offline

Engine Sim Offline is a source-informed clean-slate engine simulator and audio
renderer. It targets deterministic, headless baking first; lightweight Unity, Roblox,
and Web runtimes consume compiled results later.

The project is intentionally narrow while the BMW M52B28 parity path is established.
Production code does not link engine-sim or the failed experimental implementation.
The preserved P1.8 observation patch and fixture are reference evidence only.

The implementation plan and listening gates are in [`PLAN.md`](PLAN.md). The
simulation architecture and admission rules are in [`MODEL.md`](MODEL.md), with the
exact M3 BMW parity algorithm in
[`docs/model/M3_PARITY_MODEL.md`](docs/model/M3_PARITY_MODEL.md). The frozen BMW
reference contract is in
[`reference/oracles/bmw-m52b28/SOURCE_MATRIX.md`](reference/oracles/bmw-m52b28/SOURCE_MATRIX.md).
The current authored/resolved, scenario, capture, result, source, and manifest
interfaces are recorded in
[`docs/contracts/M2_DATA_CONTRACT.md`](docs/contracts/M2_DATA_CONTRACT.md). The
headless render boundary, transaction protocol, and deliberately narrow CLI are
recorded separately in
[`docs/contracts/M2_RENDER_API.md`](docs/contracts/M2_RENDER_API.md). Integer clock
resolution, method-owned partitioning, bounded block traversal, and deterministic
cancellation are recorded in
[`docs/contracts/M2_SCHEDULING.md`](docs/contracts/M2_SCHEDULING.md). Bounded WAV and
telemetry encoding, transactional directory publication, and the exact focused P1.8
DSP primitives are recorded in
[`docs/contracts/M2_ARTIFACTS_DSP.md`](docs/contracts/M2_ARTIFACTS_DSP.md).
The frozen eight-artifact identities, raw-master reduction, audition arithmetic, and
byte-exact reference container are in
[`docs/model/M2_P18_ARTIFACT_MASTERING.md`](docs/model/M2_P18_ARTIFACT_MASTERING.md).
The truthful split between complete simulation inputs and isolated reference
presentation lineage is in
[`docs/contracts/M2_MANIFEST_INPUTS.md`](docs/contracts/M2_MANIFEST_INPUTS.md).
The exact fixture-free reconstruction, stochastic conditioning, and typed two-route
source-stage boundary are in
[`docs/model/M2_P18_SOURCE_STAGE.md`](docs/model/M2_P18_SOURCE_STAGE.md).

## Build

A C++20 compiler and CMake 3.21 or newer are required. The foundation has no external
dependencies.

```bash
cmake -S . -B build -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Configure with `-DENGINE_SIM_OFFLINE_BUILD_TESTS=OFF` when embedding the project
without its tests.

The current CLI shell exposes only truthful capabilities:

```bash
build/engine-sim-offline --help
build/engine-sim-offline --version
build/engine-sim-offline render
```

`render` currently exits unavailable because no serialized input loader or complete
capture-to-artifact execution route is admitted yet. It does not create silence or a
placeholder file.
