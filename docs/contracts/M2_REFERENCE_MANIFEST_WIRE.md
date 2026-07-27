# M2 reference-manifest wire contract

Status: normative canonical encoding for a completed reference-presentation manifest

Wire schema ID: `engine-sim-offline.render-manifest.reference-presentation.v1`

Machine schema:
[`schemas/render_manifest_reference_presentation_v1.cddl`](../../schemas/render_manifest_reference_presentation_v1.cddl)

Schema SHA-256:
`7aaa135d04f4d4a7041193567ea8d95854298686f2fc759312d07bc56904ee60`

## 1. Scope and admission

This contract encodes one complete `RenderManifest` whose `inputs` alternative is
`ReferencePresentationInputsV1`. It covers the common manifest content, the complete
reference presentation input lineage, and the completed run's execution facts.

The root is exactly:

```text
{
  "wire_schema": "engine-sim-offline.render-manifest.reference-presentation.v1",
  "content": <RenderManifestContent>,
  "execution": <ExecutionFacts>
}
```

Before encoding, the owning render session must validate the complete typed manifest
against its provenance ledger and source matrix. The injected encoder cannot perform
that cross-record step because its interface receives only the manifest; it separately
checks wire representability. The CDDL describes the wire shape, while all typed
validation, frozen P1.8 pins, path rules, and exact artifact-set rules remain
normative. Publication is allowed only when both layers accept the object.

Although the C++ `RenderManifest::execution` member is optional while a render is in
progress, this completed-manifest wire contract requires a non-null `execution`
object. `peak_resident_bytes` is the sole nullable execution member.

The input discriminator is the two-member object `{"kind":...,"value":...}`. Its only
kind in this schema is `reference_presentation_v1`. A `SimulationManifestInputs`
value, a `simulation_v1` kind, a C++ variant index, or any other kind is unencodable and
must be rejected.

## 2. Canonical JSON bytes

There is exactly one byte representation for an admitted value:

- The document is compact JSON encoded as UTF-8 without a BOM. It contains no
  insignificant whitespace and ends with exactly one LF byte (`0a`). That final LF
  is part of the document.
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
- Every `uint64_t`, including counts, frame indices, rate numerator/denominator,
  seeds, streams, and byte sizes, is a JSON string `0x` followed by exactly sixteen
  lowercase hexadecimal digits.
- Every `double` is finite and is a JSON string `0x` followed by sixteen lowercase
  hexadecimal digits. Nonzero values use their exact IEEE-754 binary64 bit pattern;
  both signs of zero use canonical positive zero, `0x0000000000000000`. This avoids
  two wire identities for values that the typed contract compares equal. NaN and
  infinities are rejected.
- `ExecutionFacts::wall_elapsed` is named `wall_elapsed_ns` on the wire and encoded as
  its signed nanosecond count in a canonical base-10 `int64_t` string: `0`, or an
  optional minus followed by a nonzero digit and remaining digits. A plus sign and
  leading zeros are forbidden. Completed-manifest validation still requires a
  positive duration.
- SHA-256 values are exactly 64 lowercase hexadecimal digits with no `0x` prefix.
  Booleans use JSON `true` and `false`.
- Every C++ optional shown in the CDDL is always present and contains either JSON
  `null` or its value. This applies to component owners, artifact audio contracts,
  and peak resident memory. Absence is not equivalent to `null`.
- Enum and variant tags are exactly the lowercase strings declared by the CDDL.
  `unspecified`, numeric enum ordinals, unknown tags, and unknown object members are
  invalid.

`ResolvedValue<T>` is encoded as `{"value":T,"resolution_id":string}` in that
order. A `StableId<T>` is encoded as its native `u32`. All other record field names
and orders map directly to the corresponding C++ declarations, as enumerated by the
CDDL.

## 3. Content identity and execution identity

`content` is the deterministic render identity used by `same_content_identity()`.
It includes the reference lineage, executed presentation calibration, provenance
bundle, deterministic build/environment envelope, rates, random streams, output
policy, routing, and complete artifact payload identities.

`execution` describes this particular observation of that content: run ID, UTC start,
elapsed time, host, CPU, process concurrency, and memory observation. It is excluded
from `same_content_identity()`. Two runs can therefore have equal content identity
but different canonical whole-manifest bytes and different whole-file sidecar hashes.
The sidecar hash must not be treated as a content-only identity.

The identities must be truthful at their layer:

- `determinism.build` and the floating-point/instruction-set fields identify the
  current clean renderer that generated the artifacts, not the historical
  engine-sim capture producer;
- historical capture and fixture identities remain under
  `content.inputs.value.fixture` and cover the complete frozen files named by that
  contract, including container/header bytes;
- artifact `byte_count` and `payload_sha256` cover each complete emitted artifact
  file, not only its audio sample payload; and
- execution values are observed from the current run. Peak RSS is `null` only when it
  was not available; an unavailable measurement must not be replaced with zero or a
  guess.

An encoder serializes recorded typed values. It must not rewrite build names,
normalize vector order, substitute a host identity, infer missing execution facts,
or copy identities from the oracle.

## 4. Manifest file and sidecar

For the P1.8 transaction, the canonical document is published at
`manifest/render-manifest.v1.json`. `DirectoryRenderSink` computes SHA-256 over the
entire encoded file—starting at `{` and including its final LF—and writes
`manifest/render-manifest.v1.json.sha256` as exactly the 64-digit lowercase digest
followed by one LF.

The manifest and sidecar are transaction metadata, not artifact roles. Neither is
listed in `content.artifacts`, and the manifest does not embed its own digest. This
avoids recursive identity. Both files join the staged tree only after all artifact
records match the sealed whole-file payloads; they become visible with the same
atomic no-replace publication as the artifacts.

## 5. Deliberate simulation deferral

The existing C++ variant also contains `SimulationManifestInputs`, but its resolved
BMW request and executed M3 physics do not exist yet. This M2 schema deliberately
defines none of `EngineSpec`, `RenderScenario`, `ResolvedRenderInputs`, or the
simulation variant's tag/shape. Implementations must return an encoding error for
that alternative rather than projecting it into this reference schema or inventing a
provisional simulation wire format.

`simulation_v1` will be frozen in M3 from the concrete resolved request actually
consumed by the parity renderer. It is a separate schema extension, not a legacy
compatibility interpretation of this one.
