# M2 render API and CLI shell

Status: retired historical record; the current public native boundary is
[`bake.hpp`](../../include/engine_sim_offline/bake.hpp) over
[`session.hpp`](../../include/engine_sim_offline/session.hpp), with the retained sink
protocol in [`publication.hpp`](../../include/engine_sim_offline/publication.hpp)

Applies to: resolved render-request ownership, preflight admission, typed rejection,
sink transaction semantics, and the initial headless CLI

This document records the former M2 orchestration boundary and is not a current API
contract. The `render()` API described below has been deleted; no compatibility wrapper
is retained. Present-tense statements below describe repository state at that
checkpoint. The checkpoint did not implement a simulator, fixture decoder, DSP route,
serializer, or file sink, and therefore made no audio-quality claim.

## 1. Public boundary

The synchronous C++ entry point is:

```cpp
RenderResult render(const RenderSpecification &specification,
                    const RenderScenario &scenario,
                    RenderSink &sink,
                    RenderControl control = {});
```

`RenderSpecification` owns exactly the resolved records required to decide whether a
render is admissible:

- `EngineSpec`;
- `PresentationCalibration`;
- `ResolvedRandomnessPolicy`;
- `ProvenanceLedger`;
- the selected `SourceMatrixContract`;
- one owning `RenderAssetPayload` for every presentation asset.

The scenario stays a separate argument so that result validation can match the exact
request. One call is one session. The API accepts no audit file, caller-provided
excitation, backend, callback producer, registry, or legacy implementation selector.
There are no mutable globals or hidden worker pools.

`RenderManifestContent` is simulation-only. The retired reference-presentation
alternative is preserved as historical evidence in
[`M2_MANIFEST_INPUTS.md`](M2_MANIFEST_INPUTS.md), not as another request form. Public
result validation rejects any `RenderSuccess` whose manifest does not contain the
complete simulation inputs matching this API call.

## 2. Current behavior

Preflight runs in this order:

1. provenance ledger;
2. source matrix;
3. resolved engine;
4. resolved scenario;
5. engine/scenario compatibility;
6. presentation calibration;
7. resolved randomness policy;
8. canonical provisioned random-plan compilation and cached combustion-seed equality;
9. exact frozen baseline constraints when the BMW reference matrix is selected;
10. source-matrix routes/dispositions against engine and presentation routes;
11. required audio rate/frame shape against the scenario delivery clock;
12. asset payload coverage and payload SHA-256 against presentation records;
13. evidence rights versus distribution intent.

Structural rejection returns `invalid_specification`. Rights/distribution rejection
returns `evidence_rights_failure`. Both retain the exact `ValidationReport` rather
than flattening its issue paths into one string. A malformed requested profile uses
the canonical failure identity `unresolved`, so even an invalid request can produce a
contract-valid typed result.

After preflight, the renderer compiles one opaque job from the exact retained request.
The job owns the admitted simulation, excitation, presentation plan, verified asset
payloads and kernels, initialized random plan, renderer identity, and matching
manifest basis. A structurally valid request that cannot compile the complete route
still returns:

```text
kind:        incomplete_source_route
detail_code: render-pipeline-not-admitted
```

That rejection occurs before the sink transaction begins. An admitted job instead
executes the bounded simulation-to-capture-to-excitation-to-presentation chain,
completes its manifest from the sealed artifact and live execution evidence, validates
that exact completed manifest, and makes one terminal sink commit attempt.
`EvidenceSource::locator` remains provenance metadata, not an asset-byte path. Asset
bytes enter only through the owning payloads and must hash exactly to their resolved,
content-addressed presentation records before a route can be admitted. The job never
reads a fixture, synthesizes fallback audio, or accepts a second caller-built
execution plan.

## 3. Sink transaction

`RenderSink` fixes the publication boundary needed by later streaming work:

```text
idle -> begun -> committed
              \-> aborted
```

- Rejection before a complete execution route is admitted leaves the sink idle.
- Successful `begin_transaction` creates staging state and enters begun. A failed
  begin is atomic and leaves the sink idle, so the renderer does not call `abort`.
- The renderer declares artifact identity and path, writes callback-scoped byte
  chunks with contiguous per-role offsets, then seals each artifact with its record.
- A declare/write/seal failure is followed by exactly one `abort`.
- `commit` is a terminal attempt and the only operation allowed to publish the
  complete required artifact set and completed manifest. Failed commit atomically
  discards staging and leaves the transaction aborted; it is not followed by a second
  `abort`.
- `abort` is idempotent and `noexcept`; it exposes no final required artifact or
  success manifest.
- Calls are synchronous, serial, and non-reentrant. A sink may not retain a borrowed
  chunk span.
- Sink callback sizes never select simulation, capture, or DSP block sizes.

`RenderSink` is a trusted transactional endpoint: the renderer verifies every value
it sends and preserves typed rejections, but an arbitrary implementation can always
lie about persistence. The shipped `DirectoryRenderSink` is the concrete confined
publisher. Its public configuration selects only the destination root and one
publication-name component; simulation-manifest v10 encoding and metadata paths are
fixed by the sink/schema and are not another caller-authored contract.

Concrete transaction enforcement, filesystem staging, and bounded WAV and telemetry
serialization are recorded in
[`M2_ARTIFACTS_DSP.md`](M2_ARTIFACTS_DSP.md). The opaque job now connects the
simulation route to this public transaction. Typed publication errors map to
`artifact_publication_failure`; a sink protocol rejection maps to
`contract_violation`. A downstream presentation or sink failure is relayed through
the nested synchronous callbacks without being flattened into a generic
capture-consumer rejection.

`RenderControl` carries a stop token. Preflight and evidence-rights checks precede
cancellation, so a stop cannot conceal malformed input. Cancellation is observed only
at deterministic internal block boundaries and once before finalization, and reports
the typed `cancelled` failure kind. A pre-requested stop leaves the sink idle; a stop
after transaction begin aborts exactly once. The exact schedule and bounded cursor
are recorded in [`M2_SCHEDULING.md`](M2_SCHEDULING.md).

## 4. CLI shell

The executable name is `engine-sim-offline`. Its admitted surface is deliberately
small:

| Invocation | Result |
|---|---|
| `--help` | usage, exit `0` |
| `--version` | build label, exit `0` |
| `render` | unavailable explanation, exit `69` |
| missing, unknown, or extra arguments | usage error, exit `64` |

There are no engine, scenario, calibration, or output-path flags because no serialized
input contract or loader has been admitted. The shipped `render` command cannot report
success or create output. There is no alternate CLI runner or success-capable test
backend.

## 5. Verification boundary

Focused tests prove that valid, structurally invalid, rights-incompatible, mismatched,
malformed-profile, cross-routed, media-shape-incompatible, and payload-mismatched
requests all return deterministic typed results without one sink callback. A
render-layer result validator binds failures to the complete recorded request rather
than only its profile ID. Payload bindings use canonical ID/byte-count/actual-SHA-256
records, so even two malformed byte sequences with identical diagnostic text are
different requests. The same binding applies to typed unreachable-target results.
CLI tests cover output streams and exit codes. Production render and CLI targets link
no reference-audit reader or fixture adapter.

The deterministic clock/streaming, artifact, focused-DSP, and M3
simulation/excitation checkpoints are implemented without weakening this admission
boundary or adding a second publisher. The historical transactional reference
checkpoint was completed and retired. M4 now binds the current stages, verified
assets, renderer observation, and matching manifest basis into one opaque job. The
exact Release integration gate calls only the public `render()` boundary, proves one
begin/eight declarations/eight seals/one commit/no abort, validates the returned
request and completed manifest, and pins all eight live-simulation WAVE identities
plus the audition PCM identity. The shipped CLI remains a loader-free shell; it is
not an alternate execution path.
