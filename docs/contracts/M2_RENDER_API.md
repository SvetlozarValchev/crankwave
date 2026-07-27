# M2 render API and CLI shell

Status: normative interface record for the current M2 render-boundary checkbox

Applies to: resolved render-request ownership, preflight admission, typed rejection,
sink transaction semantics, and the initial headless CLI

This checkpoint creates the one public orchestration boundary. It does not implement a
simulator, fixture decoder, DSP route, serializer, or file sink, and therefore makes no
audio-quality claim.

## 1. Public boundary

The synchronous C++ entry point is:

```cpp
RenderResult render(const RenderSpecification &specification,
                    const RenderScenario &scenario,
                    RenderSink &sink);
```

`RenderSpecification` owns exactly the resolved records required to decide whether a
render is admissible:

- `EngineSpec`;
- `PresentationCalibration`;
- `ProvenanceLedger`;
- the selected `SourceMatrixContract`;
- one owning `RenderAssetPayload` for every presentation asset.

The scenario stays a separate argument so that result validation can match the exact
request. One call is one session. The API accepts no audit file, caller-provided
excitation, backend, callback producer, registry, or legacy implementation selector.
There are no mutable globals or hidden worker pools.

## 2. Current behavior

Preflight runs in this order:

1. provenance ledger;
2. source matrix;
3. resolved engine;
4. resolved scenario;
5. engine/scenario compatibility;
6. presentation calibration;
7. exact frozen P1.8 constraints when the BMW reference matrix is selected;
8. source-matrix routes/dispositions against engine and presentation routes;
9. required audio rate/frame shape against the scenario delivery clock;
10. asset payload coverage and payload SHA-256 against presentation records;
11. evidence rights versus distribution intent.

Structural rejection returns `invalid_specification`. Rights/distribution rejection
returns `evidence_rights_failure`. Both retain the exact `ValidationReport` rather
than flattening its issue paths into one string. A malformed requested profile uses
the canonical failure identity `unresolved`, so even an invalid request can produce a
contract-valid typed result.

If preflight passes in the current build, `render` returns:

```text
kind:        incomplete_source_route
detail_code: render-pipeline-not-admitted
```

That result is intentional. No complete capture-to-artifact execution route exists
yet. The call does not begin the sink, fabricate a manifest, open evidence locators,
read fixture data, synthesize silence or a tone, or create output.
`EvidenceSource::locator` remains provenance metadata, not an asset-byte path. Asset
bytes enter only through the owning payloads and must hash exactly to their resolved,
content-addressed presentation records before a route can be admitted.

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

Concrete transaction enforcement, filesystem staging, WAV and telemetry serialization
belong to the later sink checkbox. Its typed publication errors map to
`artifact_publication_failure`; a sink protocol rejection maps to
`contract_violation`. Cancellation belongs to the scheduling/streaming checkbox and
will receive its own type before it becomes reachable.

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

The next M2 checkbox adds deterministic clocks, partitioning, and bounded streaming.
It does not weaken this admission boundary or add a second render path.
