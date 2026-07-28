# Engine Sim Offline

Engine Sim Offline is a source-informed clean-slate engine simulator and audio
renderer. It targets deterministic, headless baking first; lightweight Unity, Roblox,
and Web runtimes consume compiled results later.

The project remains intentionally narrow around the accepted BMW M52B28 parity
baseline. Production code does not link engine-sim or the failed experimental
implementation. The accepted DSP and presentation session now use production-neutral
APIs; the old P1.8 replay executable, reference-manifest API, and compatibility build
targets have been retired. The preserved P1.8 observation patch, fixture identity,
manifest, and algorithm record remain immutable reference evidence only. `P1.8` is a
historical plan-checkpoint label, not a production method or API name.

The implementation plan and listening gates are in [`PLAN.md`](PLAN.md). The
simulation architecture and admission rules are in [`MODEL.md`](MODEL.md), with the
exact M3 BMW parity algorithm in
[`docs/model/M3_PARITY_MODEL.md`](docs/model/M3_PARITY_MODEL.md), and the concrete
resolved engine/scenario request is fixed in
[`docs/contracts/M3_BMW_REQUEST.md`](docs/contracts/M3_BMW_REQUEST.md). The frozen
M4 simulation-manifest encoding, including its explicit randomness policy, is in
[`docs/contracts/M4_SIMULATION_MANIFEST_WIRE.md`](docs/contracts/M4_SIMULATION_MANIFEST_WIRE.md).
The frozen BMW reference contract is in
[`reference/oracles/bmw-m52b28/SOURCE_MATRIX.md`](reference/oracles/bmw-m52b28/SOURCE_MATRIX.md).
The authored/resolved, scenario, capture, result, source, and manifest foundations are
recorded in
[`docs/contracts/M2_DATA_CONTRACT.md`](docs/contracts/M2_DATA_CONTRACT.md). The
headless render boundary, transaction protocol, and deliberately narrow CLI are
recorded separately in
[`docs/contracts/M2_RENDER_API.md`](docs/contracts/M2_RENDER_API.md). Integer clock
resolution, method-owned partitioning, bounded block traversal, and deterministic
cancellation are recorded in
[`docs/contracts/M2_SCHEDULING.md`](docs/contracts/M2_SCHEDULING.md). Bounded WAV and
telemetry encoding, transactional directory publication, and the original focused
baseline DSP primitives are recorded in
[`docs/contracts/M2_ARTIFACTS_DSP.md`](docs/contracts/M2_ARTIFACTS_DSP.md).
The remaining M2 links below are historical baseline records. They preserve the
frozen fixture terminology and identities for auditability; they do not advertise a
supported replay executable, manifest API, or production compatibility surface.
The frozen eight-artifact identities, raw-master reduction, audition arithmetic, and
byte-exact evidence container are in
[`docs/model/M2_P18_ARTIFACT_MASTERING.md`](docs/model/M2_P18_ARTIFACT_MASTERING.md).
The historical split between complete simulation inputs and isolated reference
presentation lineage is in
[`docs/contracts/M2_MANIFEST_INPUTS.md`](docs/contracts/M2_MANIFEST_INPUTS.md).
The frozen reference-presentation manifest JSON bytes and the original simulation
deferral are in
[`docs/contracts/M2_REFERENCE_MANIFEST_WIRE.md`](docs/contracts/M2_REFERENCE_MANIFEST_WIRE.md).
The exact fixture-free reconstruction, stochastic conditioning, and typed two-route
source-stage boundary are in
[`docs/model/M2_P18_SOURCE_STAGE.md`](docs/model/M2_P18_SOURCE_STAGE.md).
The strict configured-IR decoder and exact static kernel regeneration boundary are in
[`docs/model/M2_P18_IR_CONVERSION.md`](docs/model/M2_P18_IR_CONVERSION.md).
The exact fixed-topology transform, immutable configured-IR spectrum, and independent
overlap-save route histories are in
[`docs/model/M2_P18_CONVOLUTION.md`](docs/model/M2_P18_CONVOLUTION.md).
The isolated complete-route wiring, publication boundary, and measured BMW result are
in
[`docs/model/M2_P18_REFERENCE_INTEGRATION.md`](docs/model/M2_P18_REFERENCE_INTEGRATION.md).

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

## Historical BMW reference evidence

The immutable evidence capsule remains at
[`reference/fixtures/bmw-m52b28-p18`](reference/fixtures/bmw-m52b28-p18), with source
lineage in
[`reference/oracles/bmw-m52b28/PROVENANCE.md`](reference/oracles/bmw-m52b28/PROVENANCE.md).
Its P1.8 paths and semantic IDs remain stable because they identify historical bytes.
They are not accepted runtime configuration names.

The capsule can be integrity-checked without invoking a renderer:

```bash
python3 tools/validate_reference_fixture.py reference/fixtures/bmw-m52b28-p18
```

There is deliberately no `engine-sim-offline-p18-reference-render` executable and no
legacy replay API. Reference readers and the minimum oracle-comparison tests stay
outside the production dependency graph; the generally named DSP and presentation
session are the single maintained rendering implementation.
