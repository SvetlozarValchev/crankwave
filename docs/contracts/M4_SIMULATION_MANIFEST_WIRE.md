# M4 simulation-manifest wire contract

Status: normative canonical encoding for completed simulation manifests and resolved
simulation-request identities

Manifest wire schema ID:
`engine-sim-offline.render-manifest.simulation.v4`

Request-identity wire schema ID:
`engine-sim-offline.simulation-request-identity.v1`

Machine schema:
[`schemas/render_manifest_simulation_v4.cddl`](../../schemas/render_manifest_simulation_v4.cddl)

Schema SHA-256:
`a0b2ddb15332951637c8bf8cdfa5e7b8f2516128a68739d8dcaa520359081937`

## 1. Scope and admission

This contract defines the canonical simulation alternative of `RenderManifest`. Its
manifest input discriminator is exactly:

```text
{
  "kind": "simulation_v3",
  "value": {
    "resolved": {
      "engine": <EngineSpec>,
      "presentation": <PresentationCalibration>,
      "randomness": <ResolvedRandomnessPolicy>,
      "scenario": <RenderScenario>
    }
  }
}
```

The completed-manifest root is exactly:

```text
{
  "wire_schema": "engine-sim-offline.render-manifest.simulation.v4",
  "content": <RenderManifestContent>,
  "execution": <ExecutionFacts>
}
```

The schema also defines a smaller canonical identity for the resolved simulator
request before presentation and render composition:

```text
{
  "wire_schema": "engine-sim-offline.simulation-request-identity.v1",
  "engine": <EngineSpec>,
  "scenario": <RenderScenario>,
  "provenance": <ProvenanceBundleRef>
}
```

The request identity deliberately excludes `PresentationCalibration`, determinism,
render outputs, and execution facts. It is the independent golden/cache identity for
the sealed BMW engine/scenario factory result. Its distinct `wire_schema` value
domain-separates its bytes and hash from a completed simulation manifest even when
both contain the same engine and scenario productions. Implementations must use the
same engine, scenario, primitive, ordering, and fixed-RPM encoding rules for both
roots; a second request serializer with a different grammar is forbidden.

Before encoding either root, the caller must validate the complete typed object.
Completed-manifest encoding requires successful manifest validation against the
object's provenance ledger and selected source matrix. Request-identity encoding
requires successful exact BMW request validation when the object claims the frozen
BMW identity. The encoder separately checks wire representability and fixed-rate RPM
content identity. Neither root grants admission merely because a value matches the
CDDL shape.

The `reference_presentation_v1` input belongs exclusively to
`engine-sim-offline.render-manifest.reference-presentation.v2`. It has no alias,
fallback, numeric variant index, or compatibility interpretation in this schema.
Conversely, `simulation_v3` is not encodable under the reference-presentation schema.
The superseded simulation-v3 wire/API/path is not retained, accepted, or aliased;
there is no backward-compatibility path.

This checkpoint freezes data representation, not behavior. It does not claim that M4
physics has executed, produced correct observables, reached public `render()` success,
matched the oracle, or produced acceptable sound.

## 2. Canonical JSON bytes

There is exactly one byte representation for an admitted value:

- The document is compact JSON encoded as UTF-8 without a BOM. It contains no
  insignificant whitespace and ends with exactly one LF byte (`0a`). That final LF
  is part of the document.
- The complete document, including its final LF, is bounded to 4 MiB (4,194,304
  bytes). An otherwise representable value whose encoding would exceed that bound is
  rejected; the encoder never truncates it.
- Object members are emitted recursively in their CDDL declaration order. No member
  may be reordered, omitted, duplicated, or added. Arrays retain their typed vector
  order; an encoder must never sort them.
- Strings must be valid Unicode scalar sequences. Emit non-ASCII scalars directly as
  UTF-8. Escape quotation mark and reverse solidus as `\"` and `\\`; use `\b`, `\t`,
  `\n`, `\f`, and `\r` for those five control characters and lowercase `\u00xx` for
  the remaining U+0000 through U+001F characters. Do not escape solidus or emit
  surrogate-pair escapes.
- A `u32` is a native JSON integer in `0..4294967295`, emitted as shortest unsigned
  decimal with no leading zero. Stable IDs use this representation directly rather
  than an object containing `value`.
- Every `uint64_t`, including masks, counts, frame indices, rate
  numerator/denominator, seeds, streams, and byte sizes, is a JSON string `0x`
  followed by exactly sixteen lowercase hexadecimal digits.
- Every `double` is finite and is a JSON string `0x` followed by sixteen lowercase
  hexadecimal digits. Nonzero values use their exact IEEE-754 binary64 bit pattern;
  both signs of zero use canonical positive zero, `0x0000000000000000`. NaN and
  infinities are rejected.
- `ExecutionFacts::wall_elapsed` is named `wall_elapsed_ns` on the wire and encoded as
  its signed nanosecond count in a canonical base-10 `int64_t` string: `0`, or an
  optional minus followed by a nonzero digit and remaining digits. A plus sign and
  leading zeros are forbidden. Completed-manifest validation still requires a
  positive duration.
- SHA-256 values are exactly 64 lowercase hexadecimal digits with no `0x` prefix.
  Booleans use JSON `true` and `false`.
- Every C++ optional shown in the CDDL is always present and contains either JSON
  `null` or its value. Absence is not equivalent to `null`.
- Enum and variant tags are exactly the lowercase strings declared by the CDDL.
  `unspecified`, numeric enum ordinals, unknown tags, and unknown object members are
  invalid.

`ResolvedValue<T>` is encoded as `{"value":T,"resolution_id":string}` in that
order. A `StableId<T>` is encoded as its native `u32`. A `MethodIdentity` is encoded
as `id`, native-JSON `version`, then `configuration_sha256`. Torque-term masks and
resolved `uint64_t` values use the same sixteen-digit `u64` hexadecimal grammar as
other 64-bit values.

Every C++ variant is a two-member object with `kind` first and `value` second. The
kind strings are not C++ variant indices and cannot change when alternative ordering
changes.

## 3. Complete resolved simulation inputs

The machine schema enumerates every field in the current `ResolvedRenderInputs`
members:

- `EngineSpec`: identity, topology, all eight selected methods, the complete
  executable physics profile, torque capability, and provenance schema binding;
- `PresentationCalibration` schema 2: all six exact configuration-hashed methods,
  conditioning values, audio assets, per-route presentation, publication
  calibration, audition policy, and provenance schema binding;
- `ResolvedRandomnessPolicy`: the explicit seed namespace plus exact generator and
  derivation method identities, with provenance binding for all three leaves; and
- `RenderScenario`: ambient, fuel, thermal and crankcase state, preparation,
  operating-state journal, horizons, rates, quality, public seed, selected mode, and
  provenance schema binding.

Topology IDs and references are encoded as direct `u32` stable IDs. Optional route
ownership fields remain present as a stable ID/resolved value or `null`. All physical
scalars, table abscissae/ordinates, trajectory points, angles, times, gains, and
tolerances retain exact binary64 identity. All resolved leaves retain their
`resolution_id`; the wire must not flatten provenance out of the request.

The six presentation method identities jointly own reconstruction, conditioning,
strict IR decoding/conversion, convolution, stem publication, audition reduction,
mastering, container, timeline, crop, and tail conventions. There is no second
caller-authored algorithm record that can describe behavior the executable methods
do not perform.

The seed namespace is not inferred from `RenderScenario::scenario_id`. Reusing an
accepted stochastic domain or selecting a new one is therefore an explicit,
reviewable profile decision. The common initialized `RandomPlan::generator` and
`RandomPlan::derivation` must exactly equal the two resolved policy identities;
component seeds cannot claim an unrelated algorithm. The only
currently admitted identities are:

- `pcg32_xsh_rr_64_32_binary64_v1`, version 1, configuration SHA-256
  `a48383d2716a059b0b60aabf4c6febb0a81edbad622da4634823bf22421eaf0c`;
  and
- `sha256_length_prefixed_capture_component_pcg32_v1`, version 1, configuration
  SHA-256
  `0e86ea38fb2e681bb6463b3916c6ad30536f6af05cb0be1b00d3cacb763593b4`.

Admission recompiles the provisioned plan from the namespace, scenario public seed,
and stable topology. The current executors instantiate one combustion lane per
cylinder and one air-noise plus one jitter lane per configured presentation route
even when the corresponding scale is zero, so all are recorded. Its canonical order is
combustion by ascending stable cylinder ID, then air noise by ascending stable route
ID, then jitter by ascending stable route ID. Each coordinate is derived under its
fixed `combustion`, `synth_air_noise`, or `synth_jitter` domain with component index
equal to the nonzero stable owner ID minus one. Container reordering or inserting a
new owner therefore cannot rekey an existing component. Both the complete ordered
plan and the engine profile's retained combustion initializations must equal this
derivation; changing only a namespace, cached seed, or manifest seed is invalid.

The current `ExecutablePhysicsProfile` has exactly one typed alternative:

| C++ alternative | Wire kind |
|---|---|
| `LegacyLowOrderV1Profile` | `legacy_low_order_v1` |

Its wire value includes every currently typed mechanism, gas-path, valvetrain,
ignition, fuel, combustion-stream, loss, and reference-excitation field. This name is
the admitted physics-method identity from the M3 model record. It is not a P1.8 alias
and does not import a reference reader or fixture object.

All already-typed `RenderScenario` variants have canonical tags:

| C++ variant | Wire kind |
|---|---|
| `FixedSettling` | `fixed_settling` |
| `ConvergenceSettling` | `convergence_settling` |
| `HeldSpeed` | `held_speed` |
| `PrescribedKinematicSweep` | `prescribed_kinematic_sweep` |
| `LoadTargetHeldCapture` | `load_target_held_capture` |
| `InertialDyno` | `inertial_dyno` |
| `ScalarTrajectory` in `RpmTrajectory::rpm` | `scalar_trajectory` |
| `FixedRateRpmTrajectory` in `RpmTrajectory::rpm` | `fixed_rate_rpm` |

These alternatives and their fields already belong to the typed M4 scenario
contract, so representing them is not speculative. Encoding one does not make its
physics method available: engine/scenario validation, torque capability,
reachability, renderer admission, and implementation availability remain separate
requirements. The exact M3 BMW request still uses only fixed settling, a prescribed
kinematic sweep, and fixed-rate RPM.

The CDDL lists all recognized non-`unspecified` enum strings needed by these records.
The exact BMW validator remains narrower than the representational enum sets. For
example, representing a `vee_engine` layout does not permit it to masquerade as the
frozen inline-six request.

## 4. Content-addressed fixed-rate RPM

`FixedRateRpmTrajectory::post_step_rpm` is the one typed vector that is projected
content-addressably instead of being expanded into JSON. Its wire value is exactly:

```text
{
  "rate": <RationalRateHz>,
  "first_step_index": <u64-hex>,
  "semantics": "post_step_rpm",
  "sample_count": <u64-hex>,
  "samples_f64le_sha256": <sha256>,
  "resolution_id": <string>
}
```

There is no `post_step_rpm`, `samples`, decimal-keyframe, fixture-path, or inline
binary member. For the frozen BMW request the identity is:

```text
rate.numerator          = "0x0000000000002710"
rate.denominator        = "0x0000000000000001"
first_step_index        = "0x0000000000000000"
semantics               = "post_step_rpm"
sample_count            = "0x0000000000029810"
samples_f64le_sha256    =
  "b6206910b9c7b19694e35e08a3c5d8450f03cfdcbf43e818eb2a0606927d8cda"
```

Before emitting either canonical root, the encoder must:

1. reject an unrepresentable vector size;
2. derive `sample_count` from the complete owned
   `FixedRateRpmTrajectory::post_step_rpm` vector;
3. recompute `canonical_binary64_le_sha256()` over every owned sample, which hashes
   each IEEE-754 binary64 bit pattern in little-endian order with no header or count;
4. require the recomputed value to equal the typed
   `samples_f64le_sha256`; and
5. emit the derived count and verified digest.

The encoder must not trust the stored digest without recomputation, open
`reference-parity.bin`, accept precomputed fixture metadata in place of the owned
vector, or copy values from the provenance ledger. The production encoder remains
path-free and reference-reader-free.

The compact descriptor is the complete canonical identity of the owned RPM lane, but
it is not a standalone reconstruction payload. Replaying a request still requires
materializing bytes whose count and canonical digest match the descriptor. This is
intentional: the manifest and request identity remain bounded while the typed
simulator continues to own and consume all 170,000 binary64 samples.

## 5. Common manifest content and execution

Outside `content.inputs`, the simulation v4 content shape and canonical rules are the
same as the reference-presentation v2 content shape:

```text
schema_version
inputs
provenance
determinism
rates
randomness
output_contract
routes
output_buses
artifacts
```

The records are not inherited by reference or alias. They are explicitly reproduced
in the machine schema so this schema is independently reviewable and rejects unknown
members.

`content` is the deterministic render identity used by
`same_content_identity()`. For simulation it includes the complete resolved engine,
presentation, randomness policy, and scenario projection in addition to the
provenance bundle,
deterministic build/environment envelope, rates, random streams, output policy,
routing, and complete artifact payload identities.

`execution` describes one observation of that content: run ID, UTC start, elapsed
time, host, CPU, process concurrency, and memory observation. It is excluded from
`same_content_identity()`. Two completed runs can therefore have equal content
identity but different canonical whole-manifest bytes and sidecar hashes.

Although `RenderManifest::execution` is optional while a render is in progress, this
completed-manifest root requires a non-null `execution` object.
`peak_resident_bytes` is the sole nullable execution member. The request-identity
root contains no execution facts.

Build, runtime-provider, numeric-policy, routing, artifact, and execution identities
retain the meanings and admission constraints established by the typed manifest:

- build fields describe the current clean simulator/renderer source and toolchain,
  not the historical engine-sim capture producer;
- standard-library, math-library, compiler-runtime, numeric-policy,
  instruction-set, and floating-point fields describe the providers and arithmetic
  policy that actually executed;
- component seeds identify the lanes initialized by the simulation and presentation
  stages and exactly equal the independently recompiled provisioned random plan;
- actual draw counts and cadence are runtime execution evidence, not assertions made
  by this preflight inventory;
- artifact byte counts and hashes cover complete emitted files; and
- execution facts are observations from the current run, never caller guesses or
  oracle values.

The currently admitted deterministic envelope retains the exact IDs and values in
the CDDL: numeric policy
`x86-64-v1-binary64-x87-extended-strict-v1`, ISA `x86-64-v1`, IEEE-754 binary64
round-to-nearest/ties-to-even, contraction/FTZ/DAZ disabled, one deterministic
worker, and `serial-stable-order` reduction. Provider identities remain observed
content tokens. An encoder serializes these typed values; it does not authenticate,
rewrite, infer, or substitute them.

### 5.1 Job-owned artifact paths and audition metadata

The opaque simulation job derives publication names from the same admitted values; it
does not accept a second caller-built path or INFO-metadata description.

For each admitted rendered exhaust route, the three
`SourceRouteRequirement::artifact_roles` entries are the method-owned positional
tuple `dry`, `configured_ir`, and `selected`, in that order. The job copies those
identities into the corresponding named plan fields; it does not infer signal meaning
from role spelling or artifact-list order. A different count or ownership mapping is
not executable by this presentation method and fails before transaction begin.

For each admitted audio requirement, its artifact path is `audio/`, followed by the
artifact role with every `/` byte replaced by lowercase `%2f`, followed by `.wav`.
All other valid semantic-ID bytes (`a-z`, `0-9`, `.`, `_`, and `-`) are copied
unchanged. `%` is not a valid input byte, so this projection is injective. The
manifest path remains the schema-owned
`manifest/render-manifest.v4.json`. The shipped directory sink binds that path and
`encode_simulation_manifest_v4()` internally; callers configure only its publication
destination.

The audition WAVE INFO values are derived exactly as these ASCII concatenations:

```text
ICMT = engine=<engine.engine_id.value>;profile=<engine.profile_id.value>;scenario=<scenario.scenario_id>;presentation=<presentation.calibration_id>;source_matrix=<source_matrix.id>
INAM = engine=<engine.engine_id.value>;scenario=<scenario.scenario_id>
ISFT = engine-sim-offline;method=<presentation.methods.audition_mix.value.id>;version=<shortest-u32-decimal>;configuration_sha256=<64-lowercase-hex>
```

The job validates all three derived strings as nonempty, NUL-free, and at most 4,096
bytes before beginning a sink transaction. Display names, timestamps, run IDs, host
facts, and historical encoder prose are not inputs. Thus the resolved manifest basis
plus the admitted audition method completely determines the WAVE metadata bytes.

## 6. File identity and non-claims

When published through `DirectoryRenderSink`, the canonical completed document is
`manifest/render-manifest.v4.json`. Its sidecar is
`manifest/render-manifest.v4.json.sha256`, containing the SHA-256 of the complete
encoded manifest—including the final LF—as 64 lowercase hexadecimal digits followed
by one LF. The manifest and sidecar are transaction metadata, not artifact records,
and the manifest does not embed its own whole-file digest.

A canonical simulation-request identity is an in-memory golden/cache byte sequence
unless an owning transaction explicitly assigns it a path. This contract does not
invent a second manifest path, artifact role, or sidecar convention for it.

The complete sealed BMW M52B28 M3 request—including its engine, scenario, compact RPM
descriptor, and provenance bundle—has canonical request-identity SHA-256:

```text
f6f0ffc8d32167a52785003d9fb4568b32cc23adfc8e1ca4e7263210701f5aa4
```

This digest is pinned here and in the factory test, not inside the request or its
provenance. Embedding it in either would create a self-referential identity.

Successful encoding proves only:

- the typed value was admitted by the required validators;
- every represented field has one canonical JSON form;
- the fixed-rate RPM descriptor matches the complete owned vector; and
- the bytes are deterministic under this wire domain.

It does not prove numerical convergence, physical validity beyond the admitted
contract, torque completeness, oracle agreement, acoustic fidelity, runtime budget,
distribution rights, public-render enablement, or user listening acceptance.
