# M2 manifest-input contract

Status: normative typed-input contract; reference wire encoding frozen, simulation
encoding deliberately deferred to the concrete M3 request

Applies to: the input lineage represented by `RenderManifestContent`, the isolated
P1.8 reference-presentation route, and the public simulation-success boundary

Contract ID: `render_manifest_inputs_v1`

## 1. Why the split exists

A simulation manifest and a reference-presentation replay do not claim the same work.
A simulation executes a complete resolved `EngineSpec`, `RenderScenario`, and
`PresentationCalibration`. M2 reference replay instead reads two captured pre-DSP
exhaust buses and executes only the accepted presentation route.

Representing replay with a fabricated BMW `EngineSpec` or filling the fixture's
missing scenario leaves with defaults would falsely claim that this repository ran
the deferred M3 physics. `RenderManifestContent.inputs` is therefore a tagged C++
variant with no compatibility alias:

```text
SimulationManifestInputs
  complete ResolvedRenderInputs

ReferencePresentationInputsV1
  exact fixture lineage
  exact reader/adapter/seam identities
  minimal reference engine/route context
  exact capture and crop window
  complete executed PresentationCalibration
```

The reference JSON bytes and variant kind are frozen in
[`M2_REFERENCE_MANIFEST_WIRE.md`](M2_REFERENCE_MANIFEST_WIRE.md). A C++ variant index
is never a wire identity. The simulation alternative remains unencodable until its
concrete M3 request exists and its own schema is frozen.

## 2. Simulation alternative

`SimulationManifestInputs` contains the complete existing `ResolvedRenderInputs`:

- `EngineSpec`;
- `PresentationCalibration`; and
- `RenderScenario`.

Its validation remains the full render-admission validation. Rates, public seed,
route ownership, presentation configuration, and delivery frame count are derived
from that resolved request and cross-checked against the common manifest fields.

The public `render(spec, scenario, sink)` boundary accepts only this input class. A
validated public `RenderSuccess` must contain the simulation alternative and the exact
requested engine, presentation, and scenario. Reference replay is not an overload,
fixture path, caller-supplied bus, or hidden fallback in that API.

## 3. Reference-presentation alternative

`ReferencePresentationInputsV1` is restricted to the frozen local-evaluation BMW
fixture. Its repository-owned identities are:

```text
schema version          1
fixture ID              bmw-m52b28-p18-reference-capture-v1
fixture schema          1
engine ID               bmw-m52b28
engine profile ID       bmw-m52b28-p18-reference
calibration ID          bmw-m52b28-p18-presentation-v1
```

The profile ID describes only the reference presentation context. It is not the
deferred M3 physics profile.

### 3.1 Content-addressed lineage

Each digest below covers the complete named file, not only an inner sample payload.

| Typed field | Classification | Bytes | SHA-256 |
|---|---|---:|---|
| `manifest` | verified lineage root; never an execution input | 21,435 | `52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07` |
| `parity_evidence` | verified upstream evidence; never an execution input | 38,080,608 | `19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c` |
| `audit_input` | M2 execution input | 21,760,064 | `93fbaef5fe887ba229d7acc28235d63c98f9205d2fe7e426a3e501473a2643a4` |
| `component_seed_input` | lineage and typed presentation-seed input | 216 | `ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f` |
| `renderer_algorithm_record` | verified normative method evidence; never an execution input | 20,832 | `0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008` |
| `configured_ir_input` | M2 execution input | 78,602 | `75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc` |
| `kernel_oracle_comparator` | independently observed regeneration comparator; never convolution input | 240,568 | `940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598` |

The configured IR remains local-evaluation-only because its distribution rights are
unresolved. The kernel oracle must be regenerated and compared. Reading it as the
runtime convolution kernel would bypass the IR-conversion algorithm under test.
Expected stems and the liked master are likewise output comparators, never renderer
inputs.

The private `P18ReferenceCatalogV1` owns these immutable expectations once for the
reference session. Preflight returns a separately typed observed identity for every
file only after streaming the actual bytes. Later manifest construction must copy
those observed identities; it may not copy an expected catalog digest into an
observed or emitted record.

### 3.2 Reader, adapter, and seam

The input records three independently content-addressed method identities:

```text
audit reader       p18-reference-audit-reader-v1, version 1
audit adapter      p18-reference-audit-excitation-adapter-v1, version 1
excitation seam    exhaust-excitation-block-v1, version 1
audit lane         legacy_reference.exhaust_bus_pre_dsp
```

Every `MethodIdentity.configuration_sha256` must be nonzero and records the actual
implemented configuration. This checkpoint does not invent future configuration
digests before those implementations exist.

The adapter is an unscaled, ordered copy from the two binary64 audit-bus values into
the two typed `ExhaustExcitationBlock` routes. It does not relabel the values as
microphone pressure or recreate upstream combustion.

### 3.3 Minimal route context

The reference context contains no crank geometry, gas system, combustion model,
torque model, or scenario mode. It contains only the route identities needed to
validate presentation and source-matrix ownership:

| Local route ID | Semantic ID | Source-matrix classification |
|---:|---|---|
| 1 | `exhaust.reference.0` | `exhaust_outlet` |
| 2 | `exhaust.reference.1` | `exhaust_outlet` |

IDs 1 and 2 are repository-local stable manifest IDs. They are not claimed to be
engine-sim runtime indices. `exhaust_outlet` is the frozen source-matrix routing
classification used to select the presentation and artifact policy. It does not
reclassify either captured runtime bus as a demonstrated physical exhaust-outlet
observable.

### 3.4 Exact consumed window

| Quantity | Frozen value |
|---|---:|
| Physics rate | 10,000 Hz |
| Capture rate | 10,000 Hz |
| Source-processing rate | 192,000 Hz |
| Acoustic rate | 192,000 Hz |
| Delivery rate | 192,000 Hz |
| Fixture records | 170,000 |
| Consumed records | `[0, 170000)` |
| Audible records | `[20000, 170000)` |
| Fixed capture block | 200 records |
| Total source frames | 3,264,000 |
| Audible source frames | `[384000, 3264000)` |
| Delivered frames | 2,880,000 |
| Public seed | 12,648,430 (`0xC0FFEE`) |

These values describe a 17-second continuously warmed presentation execution with
the audible `[2 s, 17 s)` crop. State is not reset at the crop boundary.

## 4. Executed randomness

The common `RandomPlan` describes the renderer executing now, not every stochastic
component that affected the upstream capture. For reference replay its ordered
component-seed vector is exactly:

| Order | Kind | Route | Initial state | Stream selector |
|---:|---|---|---|---|
| 0 | `presentation_air_noise` | 1 | `0x75bc579d4c90a640` | `0x7e4ef6200e7c70c1` |
| 1 | `presentation_air_noise` | 2 | `0x208e57f73615bd95` | `0x786d92e584c43b78` |
| 2 | `presentation_jitter` | 1 | `0x9e2b91cd0dc51cfc` | `0x1ae6ee3019603abb` |
| 3 | `presentation_jitter` | 2 | `0xdb7540a0c8b54d74` | `0x41ddcdeb066bf214` |

The six combustion pairs and starter pair remain fixture lineage because they already
affected the captured buses upstream. Including them as current execution streams is
a validation error. Vector order is preserved because ordering elsewhere represents
topology or arithmetic; serializers must not apply blanket sorting.

## 5. Build and execution truthfulness

The common `DeterminismEnvelope` identifies the current clean renderer build, loaded
standard library, math library, and compiler runtime, compiled numeric policy,
floating-point environment, worker count, and reduction topology. The old engine-sim
capture build and observation patch remain fixture provenance; they are not copied
into the current build identity.

The private source-stamp checkpoint obtains the full Git `HEAD`, compiler ID/version,
and effective target triple from the build itself. Its renderer-source digest is
SHA-256 over a versioned stream of sorted, normalized relative path, decimal byte
count, and complete-file SHA-256 records for all tracked and untracked
`CMakeLists.txt`, `cmake/`, `include/`, and `src/` files. Git ignore configuration
cannot hide a renderer input. The generator runs whenever its private target builds.
Dirty, unavailable, nested-repository, or unrepresentable source closures return typed
errors; they never produce admissible build evidence. Separate loaded-provider and
numeric-environment checkpoints admit the actual libstdc++, glibc libm, and libgcc_s
providers plus the exact compiled ISA and calling-thread floating-point policy. The
reference-manifest v2 schema represents those facts explicitly. One private,
zero-argument composer now seals the complete source, provider, and numeric evidence
and its exact `DeterminismEnvelope` projection. The isolated reference executable uses
that build-owned value before path parsing, output creation, or DSP; no caller can
supply a revision or compiler identity.

Toolchain admission is deliberately narrower than build support: only a top-level,
single-configuration `Release` build with empty compiler `ARG1`, global flags,
configured target, and launchers, plus the exact supported `-O3 -DNDEBUG` Release
flags, receives a target triple. The always-run generator asks GCC for its effective
multiarch or Clang for its machine triple using that exact supported flag set.
Response files, caller flags, custom or multi-config builds, and other flag sets remain
buildable but stamp as unavailable so changed code generation cannot collide under one
renderer identity.

### 5.1 P1.8 method configuration identities

The private reference target owns one canonical LF-terminated UTF-8 configuration
descriptor for every method identity represented by the reference manifest. The
descriptor bytes are retained beside a separately pinned SHA-256; initialization fails
if recomputing the descriptor does not equal the pin. Stable IDs and versions are
cross-checked against `P18ReferenceCatalogV1`, but the catalog does not invent a
configuration digest.

| Method ID | Version | Configuration SHA-256 |
|---|---:|---|
| `p18-reference-audit-reader-v1` | 1 | `3577c0c36a817c0c1c6ba944e0662dbf65cc1c8cedf6a530accff8baf76d7a7e` |
| `p18-reference-audit-excitation-adapter-v1` | 1 | `08cd0ee63b6cc97c097d6a91577d65292a70d4a66302a1d45cdc23c3a47521e3` |
| `exhaust-excitation-block-v1` | 1 | `f508eddbd4e031547bb6e9a4f49f7962c1f7bafdc4d0f740f309e2a370956208` |
| `p18_reference_pcg32_v1` | 1 | `303578ab8b93ec4bde570e555df0034b30461f8680e97d04744bc49d5941f236` |
| `sha256_length_prefixed_capture_component_pcg32_v1` | 1 | `f4fd3fd4005e607c79a1f6571166de539c849ab43708899a8eeb7d196ef1c7b3` |
| `kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_cutoff0p95_source_nyquist_unity_dc_binary64_v2` | 1 | `96e801125575b0a6235671c43a7da437739f969695ebc700a3753b5f5c9387ac` |
| `p18-synthesizer-conditioning-v1` | 1 | `8abbacdc9202a01a05daa306ea42a3fa3dd405a5484000e7e0de5d5673dfc2bc` |
| `blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_binary64_v3` | 1 | `9758d764d28a60bfcd571fa6af128e06f37c1036b8cc1d65b93c4b2e7cef9554` |
| `causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1` | 1 | `9879bf8f2186105ec105af5f9b47bd877abb072d598371309c8b025076b28b50` |
| `p18-float32-stem-publication-v1` | 1 | `6ab31f524e245a642f6fb9c6fe6dd18b85eaf5c1cd00daf63d1199c18fa188bf` |
| `p18-reference-audition-mix-v1` | 1 | `6e332228f8e64f30a821e9961e8a16c8d55da2a0a14a3a4118ee40054b2c4105` |

The seed-derivation identity records how the verified component-seed inventory was
created. The current renderer reads that verified inventory and executes the recorded
PCG32 streams; it does not claim to derive the seeds again during replay.

### 5.2 Observed reference provenance

`P18ReferenceProvenance` is a sealed private value. Its sole factory accepts
`P18VerifiedReferenceLineage`; it cannot accept expected catalog records. The ledger
contains seven evidence records in canonical fixture order:

```text
p18-fixture-manifest
p18-parity-evidence
p18-audit-input
p18-component-seed-input
p18-presentation-renderer-record
smooth-39-ir
p18-kernel-comparator
```

Every evidence locator is the stable repository-relative fixture path, every content
digest is copied from the independently observed lineage identity, revisions are
absent, and rights are conservatively `local_evaluation_only`. `EvidenceSource` has
no byte-count field, so the ledger does not hide a size inside its locator or claim.
The verified observed byte counts remain available in `P18VerifiedReferenceLineage`
and must be copied separately into the next reference-manifest content checkpoint.

Three `reference_fixture` claims distinguish the complete seven-file fixture, the
renderer algorithm record, and the configured IR. Exactly 28 authored resolutions
cover every resolved presentation leaf: engine profile; six methods; three algorithm
record fields; five conditioning values; four configured-IR asset fields; gain and wet
mix for both routes; publication gain; and the four audition values. The IR asset
leaves cite the configured-IR claim; all other leaves cite the renderer-record claim.

The provenance bundle is self-digesting. Its canonical SHA-256 grammar ID is
`engine-sim-offline.p18-provenance-ledger-digest.v1`. Unsigned integers use little
endian (`u32` or `u64`); strings use a `u64` byte count followed by exact bytes;
digests use 32 raw bytes; vectors use a `u64` count; optionals use a one-byte
zero/one presence tag; and contract enums use their one-byte declared value. The
stream contains, in order:

```text
length-prefixed grammar ID
schema ID
bundle ID
evidence vector: ID, locator, revision?, content SHA-256?, rights
claim vector: ID, origin, citations, uncertainty?
resolution vector: ID, parameter path, mode, claim ID, method?, dependencies
```

An optional method contains ID, `u32` version, and configuration digest. Optional
standard uncertainty uses canonical binary64 bits with either signed zero normalized
to positive zero, followed by its method string. `bundle.sha256` itself is the sole
excluded field, preventing recursive identity. The pinned current bundle digest over
the observed BMW fixture is
`76dfb503bc1852f1a1d11f739c612d11e4ad4c05ee0ab5c5eda873a35e04f60d`.
Changing evidence content, a locator, or a resolution changes the digest; changing
only the stored bundle digest does not.

`ExecutionFacts` continue to describe only the current run. Wall time, host, CPU,
thread count, job count, and peak memory are excluded from deterministic content
identity.

The common output contract, routes, buses, artifacts, and source-matrix omissions are
validated identically for either input alternative. The reference alternative
requires the exact frozen BMW source matrix and its eight local-evaluation artifacts.

## 6. Claim boundary

This split:

- prevents fixture replay from impersonating a physics render;
- provides complete typed lineage for the later reference request and manifest;
- keeps the public render/result contract simulation-only; and
- supplies the minimal context needed to validate the exact P1.8 presentation.

The method/provenance checkpoint constructs no manifest, encodes no JSON, publishes
nothing, and does not alter audio. It does not collect execution facts, admit public
render success, or make a sound-quality claim.
