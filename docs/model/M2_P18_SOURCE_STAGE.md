# Exact P1.8 source-stage contract

Status: normative architecture and verification boundary for the M2 source-stage
checkpoint

Applies to: the typed two-route excitation seam, causal 10 kHz-to-192 kHz
reconstruction, and the exact P1.8 jitter and conditioning state machines

This checkpoint turns two synchronous 10 kHz exhaust-excitation routes into two
conditioned binary64 streams at 192 kHz. It deliberately stops before crop, impulse
response processing, convolution, publication, or artifact creation. Its output is
not a listening candidate.

## 1. Numerical authority and claim boundary

[`P18_PRESENTATION_RENDERER.md`](../../reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md)
is the numerical authority for every constant, operation order, state transition,
random draw, and floating-point requirement in this document. The typed lineage and
four executed random streams are fixed separately by
[`M2_MANIFEST_INPUTS.md`](../contracts/M2_MANIFEST_INPUTS.md), and the integer block
plan is fixed by [`M2_SCHEDULING.md`](../contracts/M2_SCHEDULING.md).

This document records component ownership and integration boundaries; it does not
replace the authority with a second transcription of every equation. A disagreement
is a contract failure to correct, not permission for the implementation to choose a
nearby result.

Signal, coefficient, and state arithmetic is IEEE-754 binary64 under
round-to-nearest/ties-to-even, without contraction, reassociation, or fast-math.
Statement and loop order are part of the result.

The source stage reproduces one narrow legacy presentation behavior. Its values remain
in uncalibrated `engine_sim_source_unit`; they are not microphone pressure, physical
outlet pressure, or a new higher-fidelity source model.

## 2. Typed seam and block contract

The stage boundary is:

```text
two-route ExhaustExcitationBlockView at 10,000 Hz
  -> shared-clock 257-tap causal reconstruction
  -> route-local jitter
  -> route-local DC removal, derivative mixture, and filtered air noise
  -> frame-major two-route conditioned binary64 stream at 192,000 Hz
```

Each input frame owns exactly two simultaneous binary64 route values. The route order
is stable and explicit:

| Local route ID | Semantic route |
|---:|---|
| 1 | `exhaust.reference.0` |
| 2 | `exhaust.reference.1` |

The view also carries the global first-frame index and exact rational sample rate
`10000/1`. Frames are callback-scoped borrowed storage: the stage consumes them
synchronously and retains no input span or pointer after the call.

The frozen reference schedule supplies 850 consecutive blocks of exactly 200 input
frames. Each block produces exactly 3,840 source frames, for 170,000 input frames and
3,264,000 source frames overall. The integrated source stage accepts only one complete
200-frame method block and one caller-owned output span of exactly 3,840 frames per
call. It fills that span synchronously and retains no output storage.

Lower-level reconstruction and route-conditioning components may accept smaller
partitions for focused continuity tests. That testability does not relax the
integrated stage's fixed 200-to-3,840 contract.

## 3. Causal reconstruction and shared clock

The reconstruction method identity is
`kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_cutoff0p95_source_nyquist_unity_dc_binary64_v2`.
It owns:

- one immutable 4,097-row by 257-tap binary64 coefficient table shared by the two
  routes;
- one 257-frame zero-initialized circular history per route;
- one shared circular-history index; and
- one shared integer `distance_to_next_output` clock, initially zero.

The ordinary 4,096 phase rows use the exact Kaiser-windowed sinc construction and
left-to-right normalization in the authority. Row 4,096 is the explicit shifted
phase-zero wrap row. Phase and interpolation mix are derived from the integer clock;
the stage never accumulates binary64 time or resolves each route independently.

For every 10 kHz interval, both routes use the same output count, phase, interpolation
mix, coefficient traversal, and tap order. All outputs for that interval are computed
from the previous history. Only then is the current simultaneous two-route input
committed. This preserves the authority's causal, uncompensated 128-physics-frame
delay; it must not be replaced with a centered offline resampler.

The exact cadence begins `20, 19, 19, 19, 19` source frames per five input frames and
therefore yields 96 source frames per five inputs and 3,840 per 200. Clock, circular
history, and route histories continue unchanged across every caller block.

## 4. Randomness, jitter, and conditioning

The random-generator identity is `p18_reference_pcg32_v1`, version 1, and the
conditioning identity is `p18-synthesizer-conditioning-v1`, version 1. Their manifest
configuration digests identify the implemented configurations and are not invented
before those implementations exist.

The generator is the exact P1.8 PCG32 state machine. Constructor arguments are inputs
to the two-discard seeding procedure, not pre-seeded internal state. Stream selectors
greater than `UINT64_MAX >> 1` are rejected before odd-increment encoding.

The exact reference integration supplies these route-owned jitter and air-noise
generators:

| Owner | Initial state | Stream |
|---|---|---|
| air route index 0 / local ID 1 | `0x75bc579d4c90a640` | `0x7e4ef6200e7c70c1` |
| air route index 1 / local ID 2 | `0x208e57f73615bd95` | `0x786d92e584c43b78` |
| jitter route index 0 / local ID 1 | `0x9e2b91cd0dc51cfc` | `0x1ae6ee3019603abb` |
| jitter route index 1 / local ID 2 | `0xdb7540a0c8b54d74` | `0x41ddcdeb066bf214` |

Combustion and starter generators are not part of this stage. They affected the
upstream captured trace and remain lineage only.

The algorithm component receives its four seed pairs explicitly so focused tests can
use controlled vectors without opening a fixture. Its constructor checks encodable,
pairwise-distinct PCG stream selectors; a changed initial state does not make a reused
selector a separate stream. The later reference request/session is responsible for
binding the exact table above; any other seeds cannot claim the frozen reference
render.

For each route and 192 kHz frame, processing order is fixed:

1. Write the reconstructed sample to the 41-sample jitter history.
2. Consume one binary64 uniform draw from that route's jitter generator.
3. Apply the exact 10 kHz fourth-order low-pass modulation, clamp its offset to
   `[0, 40]`, and use the authority's circular-history orientation and linear
   interpolation.
4. Update the first-order DC state and compute `dc_removed`.
5. Compute the backward derivative from the jittered sample and update its previous
   sample.
6. Consume one signed binary64 draw from that route's air-noise generator and apply
   the independent 2 kHz fourth-order low-pass.
7. Evaluate the exact derivative/DC/noise mixture in the recorded statement order.
8. Replace only a subnormal conditioned result with exact positive zero.

Every floating draw consumes exactly two `next_u32()` results. Jitter is evaluated
before the air-noise draw. The significant constants remain:

```text
jitter amount                  0.5
jitter modulation cutoff       10,000 Hz
jitter history                 41 samples
noise excitation scale         sqrt(2)
source rate                    192,000 Hz
air-noise amount               1
air-noise cutoff               2,000 Hz
derivative mix bits            0x3f847ae140000000
dt bits                        0x3ed5d867c3ece2a5
DC alpha bits                  0x3f357088f45745f4
```

The exact fourth-order coefficient construction and recurrence, DC removal, backward
difference, and subnormal cleanup are the focused primitives recorded in
[`M2_ARTIFACTS_DSP.md`](../contracts/M2_ARTIFACTS_DSP.md). Recalculating immutable
coefficients at a block boundary may not reset their histories.

The resulting values are the dry/pre-convolution binary64 variant. No Float32
conversion or `2^-26` calibration occurs in this stage.

## 5. State ownership and continuity

One source-stage session owns all mutable execution state:

- the shared reconstruction clock and circular-history position;
- two reconstruction histories;
- two jitter histories and write positions;
- two jitter low-pass histories and two jitter PCG32 instances;
- two DC-removal and two derivative histories; and
- two air-noise low-pass histories and two air-noise PCG32 instances.

All state begins at the authority's exact zero/seeded state before input frame zero.
It survives input-block boundaries and any later audible crop boundary. A new clip
uses a new stage instance; concurrent clips never share mutable clocks, histories, or
random generators. The immutable reconstruction table is common to both routes but
has no lazy mutable state.

Routes are processed in stable route order. The shared reconstruction clock prevents
independent route drift, while route-local histories and random streams prevent
cross-route state contamination. No sink chunk size or thread schedule may select DSP
boundaries or random advancement.

The caller owns the exact-sized conditioned output span. On success, the stage returns
the input/source extent and advances its global counters only after the entire block
has completed. The stage exposes no rejecting output callback.

## 6. Validation and failure semantics

Before consuming a block, the stage checks:

- exact `10000/1` input rate;
- exact ordered route IDs;
- a contiguous global first-frame index;
- exactly 200 input frames and exactly 3,840 output frames;
- the exact phase-zero block boundary resolved by the shared clock;
- finite input values.

Construction separately rejects unencodable or multiply owned PCG stream selectors
before the stage can consume a block.

Non-finite exposed input or output is a hard failure under the authority. Construction
also checks non-finite coefficients, and internal finite checks are a deliberate clean
fail-closed safety policy. Invalid phase/tap indices, an oversized PCG stream, wrong
output capacity, clock discontinuity, route mismatch, or rate mismatch are failures.
There is no silence, interpolation substitute, post-start state reset, clamping
fallback, or approximate algorithm. The jitter offset clamp and the one
conditioned-subnormal cleanup are normative operations, not recovery behavior.

Structural preflight failures occur before state mutation and are retryable with a
corrected block and output span. Once reconstruction or conditioning begins, an
arithmetic or internal-invariant failure makes the integrated stage terminal. The
caller discards any partially written output and may not resume from partially
advanced state. The surrounding render session later maps that terminal condition to
its typed failure and must not publish success artifacts.

Cancellation is observed only between complete method-owned blocks, as defined by the
scheduler. A source-stage call itself is serial and non-reentrant.

## 7. Deliberate exclusions

This checkpoint does not:

- open or decode `reference-audit.bin`, `reference-parity.bin`, or any fixture path;
- implement the reference-only audit reader or excitation adapter;
- select or discard the later audible `[384000, 3264000)` source-frame interval;
- decode PCM16 IR data, detect support, or construct the 30,071-coefficient kernel
  specified by [`M2_P18_IR_CONVERSION.md`](M2_P18_IR_CONVERSION.md);
- convolve through the isolated implementation specified by
  [`M2_P18_CONVOLUTION.md`](M2_P18_CONVOLUTION.md), perform wet selection, or create
  dry/configured/selected stem families;
- convert to Float32, apply source calibration, encode WAV, or build a master;
- write telemetry, artifacts, manifests, or transactional directories;
- admit the public `render()` path; or
- establish complete or production-listenable audio.

Fixture integration, crop, IR conversion, fixed convolution, publication, and
hash/listening acceptance remain separate checkpoints. None may feed expected output
back into this stage.

## 8. Verification anchors

Focused verification must establish:

1. PCG32 seeded state, odd increment, raw output vectors, and binary64 uniform/signed
   draw bits for all four streams.
2. Reconstruction table coefficient bits at phase 0, phase 2,048, and phase 4,095,
   plus exact positive-zero/shift identity for row 4,096.
3. Integer phase resolution at interval boundaries and the exact
   `20,19,19,19,19` cadence, including 200-to-3,840 and multi-block totals.
4. Shared-clock route behavior, causal history commit order, and synthetic
   impulse/route-isolation reconstruction vectors.
5. Authority-pinned 10 kHz and 2 kHz filter coefficients, jitter indexing, DC alpha,
   derivative history, mixture order, and subnormal cleanup.
6. Exact synthetic conditioned-output bits and exposed final PCG states, with hidden
   filter/history continuity verified through pinned continuation outputs.
7. Lower-level reconstruction/conditioning equality between one logical sequence
   processed contiguously and the same sequence divided across accepted test
   partitions.
8. Constructor rejection of invalid or multiply owned PCG stream selectors before a
   usable stage exists.
9. Non-mutating, retryable rejection of invalid block rate, route, index, size,
   capacity, and non-finite-input cases.
10. Terminal behavior and discardable partial output for an injected arithmetic or
   post-start internal-invariant failure.

These anchors prove the isolated source-stage algorithm and continuity only. Fixture
stem hashes, kernel identity, masters, performance, and listening acceptance are not
verification targets for this checkpoint.
