# M2 artifact and focused-DSP contract

Status: normative interface record for the M2 telemetry/WAV sink and focused-DSP
checkpoint

Applies to: bounded byte encoding, transactional directory publication, and the
small exact P1.8 stateful primitives shared with the source-stage implementation

This checkpoint does not decode the BMW fixture, construct excitation, run the
complete presentation route, serialize a production render manifest, or admit render
success. It therefore produces no listening candidate and makes no sound-quality
claim.

## 1. Separation of responsibilities

Artifact creation has three independent layers:

```text
typed frames/records -> bounded encoder callbacks -> RenderSink transaction
                                              |
complete RenderManifest -> explicit encoder --+
```

- WAV and telemetry encoders own only their versioned wire formats. They retain no
  callback span and allocate at most the configured encoder chunk buffer plus fixed
  topology/state.
- `DirectoryRenderSink` owns artifact declaration, contiguous writes, sealing,
  verification, private staging, and atomic publication. Sink chunking cannot select
  capture or DSP partitions.
- Manifest serialization is an explicit injected dependency. The directory sink has
  no summary, host-ABI dump, or default placeholder representation. A missing,
  throwing, or empty manifest encoder makes commit fail closed.

The complete capture-to-artifact orchestration remains in the split acoustic-route
items in PLAN. Until that route constructs and validates a complete manifest, the
public `render()` boundary continues to return `render-pipeline-not-admitted` without
beginning a sink.

## 2. WAV encoding

`WavEncoder` supports only channel/rate/encoding meanings that the current
`AudioContract` resolves without guessing:

| Contract value | WAVE representation | Input API |
|---|---|---|
| `mono`, `float32le` | classic RIFF/WAVE IEEE Float32 with `fact` | finite `float` samples |
| `mono`, `pcm_s24le` | classic RIFF/WAVE signed PCM24 | pre-quantized `int32_t` values in `[-8388608, 8388607]` |

Float32 serialization preserves every finite input bit pattern, including finite
values outside `[-1, 1]`; it neither clips nor normalizes. The generic PCM24 encoder
does not invent a dither or float-to-integer quantizer. The acoustic route must name
and test any such policy before it can call the PCM24 input API.

The encoder validates an integral positive sample rate representable by classic WAVE,
the complete RIFF/data sizes, exact frame count, finite Float32 values, PCM24 range,
and state transitions. It emits little-endian bytes at contiguous offsets in
caller-configurable chunks of at most 64 KiB. Consumer rejection or exception
permanently fails that encoder so the surrounding transaction can abort.

The generic classic PCM24 container is not claimed to reproduce the preserved BMW
audition master's FFmpeg-authored WAVE_FORMAT_EXTENSIBLE/LIST container bytes. The
next complete route must reproduce that exact frozen FFmpeg master/container hash for
parity; it may additionally publish separately labelled candidate or listening
copies. It may not treat this generic encoder as the oracle-container hash path. The
frozen P1.8 Float32 stem container is the byte-parity target served by the current
Float32 representation.

## 3. Capture telemetry encoding

`TelemetryEncoder` writes
`engine-sim-offline.capture-telemetry.le.v1`, a canonical little-endian stream:

- one copied topology and clock header;
- every `EngineCaptureSample` and every frame-major cylinder, port, gas-volume,
  flow-edge, and source-route value;
- complete torque availability, completeness, reasons, and term masks;
- both source-route variants;
- every event variant and stable ordinal;
- optional reference-parity values when declared;
- one exact frame-count footer.

Transport blocks are not physical observables. The stream records global sample and
timestamp identities, assigns each event to its containing global frame, and omits
block-local capacity and boundary details. Therefore the same valid logical capture
has identical bytes when divided into different accepted block partitions.

The encoder copies layout topology when constructed. Each borrowed
`CaptureBlockView` is validated and consumed synchronously; no borrowed layout, sample,
event, or parity span survives the call. Non-finite serialized values, discontinuous
clocks, changed topology, changed parity presence, frame-count mismatch, callback
failure, and invalid state all fail closed.

## 4. Transactional directory publication

One `DirectoryRenderSink` publishes one directory beneath an already named
publication root. The publication name is one validated portable path component;
artifact and manifest paths are validated normalized relative paths. Portable
case-fold collisions, prefix conflicts, duplicate identities/roles, undeclared
artifacts, and reserved manifest paths are rejected.

`begin_transaction()` creates a private sibling staging directory. Artifact files are
opened relative to owned directory descriptors without following symlinks. Writes
must be contiguous from offset zero. Sealing verifies the declared byte count and
incrementally calculated SHA-256. Commit requires the exact output-contract artifact
set and exact manifest artifact records, writes the encoded complete manifest and its
lowercase SHA-256 sidecar into staging, synchronizes the staged tree, and performs one
atomic no-replace rename. An existing destination is never overwritten.

The current implementation admits this guarantee on Linux through `renameat2` with
`RENAME_NOREPLACE`. A platform without an equivalent atomic no-replace primitive
fails closed before staging at begin. Failed begin leaves the sink idle; failed commit
cleans its owned staging directory and is terminal-aborted; destruction or explicit
abort of a begun transaction removes only that owned staging directory. No success
directory is visible before a successful commit.

A Linux build admits staging because the syscall interface exists; if the running
kernel or mounted filesystem rejects that atomic operation, commit fails terminally
without publishing. The directory transaction protects against ordinary failures and
at-rest tampering inside its private stage. It is not a security boundary against a
hostile same-user process racing the final identity check and rename.

## 5. Focused P1.8 DSP and source-stage primitives

The DSP target contains exact P1.8 building blocks whose arithmetic is already frozen
in `P18_PRESENTATION_RENDERER.md`:

- fourth-order low-pass coefficient construction and recurrence;
- first-order DC removal;
- backward first difference;
- the one specified conditioned-sample subnormal cleanup;
- binary64-to-Float32 publication followed by exact `2^-26` calibration;
- PCG32 state and binary64 draw construction; and
- the immutable 257-tap, 4,097-row causal-reconstruction coefficient table.

Their names deliberately identify them as P1.8 behavioral-reference code rather than
general production DSP. Tests pin coefficient and recurrence bit patterns, continuous
state across caller chunk boundaries, publication order, and fail-closed handling of
invalid/non-finite input.

The internal presentation target composes those primitives into the exact
shared-clock reconstruction and two independent jitter/DC/derivative/air-noise paths.
Its typed seam, state ownership, validation, and deliberate stopping boundary are
recorded in
[`M2_P18_SOURCE_STAGE.md`](../model/M2_P18_SOURCE_STAGE.md).

IR conversion, fixed overlap-save convolution, crop, stems, and master remain absent.
No partial source-stage output is an audible candidate. The complete unchanged route
must pass its fixture/oracle hash tests before any WAV is offered for listening.
