# Exact P1.8 fixed-convolution contract

Status: normative architecture and verification boundary for the isolated M2
fixed-topology FFT and causal overlap-save checkpoint

Applies to: the immutable 65,536-point transform plan, immutable configured-IR
spectrum, and one independent continuous convolution history per route

This checkpoint turns a binary64 dry stream into the configured-IR binary64 variant.
It deliberately does not connect the source stage, fixture, crop, selection, WAV
encoder, stems, or masters. An isolated convolver is not a listening candidate.

## 1. Authority and claim boundary

[`P18_PRESENTATION_RENDERER.md`](../../reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md)
is the numerical authority for the transform topology, traversal order, continuous
history, and reference partition. The exact 30,071-coefficient input is produced by
[`M2_P18_IR_CONVERSION.md`](M2_P18_IR_CONVERSION.md).

The method identity is
`causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1`. Complex signal,
coefficient, and transform arithmetic is IEEE-754 binary64 under
round-to-nearest/ties-to-even, without contraction, reassociation, or fast-math.
Statement and loop order are part of the frozen result.

This reproduces one narrow P1.8 presentation operation. It makes no claim that the
configured IR is a physical exhaust-radiation, microphone, or room model, and it does
not establish higher fidelity or production sound quality.

## 2. Immutable plan and kernel ownership

```text
exactly 30,071 binary64 IR coefficients
  -> shared immutable 65,536-point FFT plan
  -> zero-pad and forward-transform once
  -> shared immutable 65,536-bin kernel spectrum
       |                                  |
       v                                  v
route 0 convolver                    route 1 convolver
  independent history/work            independent history/work
```

The plan owns one 16-bit reversal table and 32,768 forward roots. It has no lazy or
mutable execution state. A kernel owns the plan by shared immutable lifetime and one
zero-padded, forward-transformed spectrum. Multiple kernels may share a plan, and the
two reference route instances share one kernel.

Each convolver exclusively owns its 30,070-sample history, prospective-history
scratch, output scratch, and 65,536-value complex work array. Route instances never
share mutable history or work storage. They are intentionally non-copyable,
non-movable, serial, and non-reentrant. Concurrent clips use independent convolver
instances; only immutable plans and kernels may be shared.

## 3. Fixed transform topology

The transform length is exactly 65,536, the smallest power of two covering the
30,070-sample history plus the admitted maximum 9,600-frame input block. The plan:

1. constructs the 16-bit reversal table in increasing input-index order;
2. constructs forward roots `k = 0..32767` from `-2*pi_full*k/65536`;
3. applies bit reversal, swapping only when `index < reversed`;
4. visits radix-2 widths `2, 4, ..., 65536`, then bases and butterfly offsets in
   increasing order; and
5. for inverse transforms, conjugates the stored forward root in each butterfly and
   multiplies every completed value by binary64 `1 / 65536` in increasing order.

The kernel copies all 30,071 coefficients into the real part of a zero-filled work
array, applies that exact forward transform once, and retains only the resulting
spectrum. There is no alternate FFT backend or topology-dependent planning step.

## 4. Causal overlap-save execution

One call accepts a nonempty finite binary64 input block of at most 9,600 frames and a
same-length caller-owned output span. Input and output storage may overlap because all
input needed by the operation is consumed before caller output is written.

For each call, in order, the convolver:

1. clears the complex work array;
2. copies continuous history to indices `[0, 30070)` and the input immediately after
   it;
3. prepares the next history from the final 30,070 samples of old history plus input;
4. applies the fixed forward transform;
5. multiplies corresponding bins by the immutable kernel in increasing index order;
6. applies the fixed inverse transform;
7. stages the real values beginning at index 30,070; and
8. copies the complete staged block to caller output, then commits history.

The frozen reference route always calls this primitive with exactly 3,840 frames for
each of 850 consecutive blocks on each route. Integration must preserve that partition
for byte comparison even though the isolated primitive admits other bounded block
sizes for numerical continuity verification. Different valid partitions can change
FFT rounding and are compared within the declared test tolerance, never presented as
byte-equivalent to the frozen partition. State is not reset at the later audible crop.

There is no direct-FIR fallback, tail flush, latency compensation, phase alignment,
normalization, limiter, saturation, or hidden partition selection.

## 5. Validation and failure semantics

Plan construction rejects a non-finite root. A transform requires exactly 65,536
finite complex values and rejects a non-finite result after completing the recorded
topology. Kernel construction requires exactly 30,071 finite coefficients and a valid
shared plan, creating one when the caller supplies none.

Before mutating continuous state or caller output, a convolver call rejects an empty
block, a block above 9,600 frames, unequal input/output lengths, or any non-finite
input sample. Arithmetic is completed in private scratch storage. A later non-finite
transform or output failure leaves both caller output and continuous history
unchanged. There is no silence, truncation, approximate fallback, coefficient repair,
or partial state commit.

For focused numerical comparisons, frame `i` must satisfy:

```text
absolute_error_i <=
    4096 * binary64_epsilon *
    max(1, direct_absolute_product_sum_for_frame_i)
```

The comparator is a direct causal-convolution oracle, and the product sum is the sum
of absolute input-coefficient products contributing to that frame. Every alternate
partition must independently satisfy this same direct-oracle bound; matching two
partitioned FFT results to each other is not a substitute.

Focused verification must establish:

1. bit-reversal, root, forward, inverse, and fixed traversal behavior;
2. kernel construction and rejection at its exact coefficient boundary;
3. impulse and deterministic direct-convolution agreement within the declared FFT
   tolerance;
4. bit repeatability for an identical partition plus numerical continuity across
   alternate admitted partitions, with 3,840 frames forced for reference identity;
5. shared immutable kernel identity with independent route histories;
6. exact in-place and overlapping-span behavior; and
7. non-mutating rejection of invalid shapes and non-finite input, including caller
   output and continuous-history sentinels after an injected arithmetic failure.

On the current development host, a standalone Clang 21 `-O3` focused benchmark
processed two independent routes sequentially, 850 blocks per route and 3,840 frames
per block. Its 3,400 forward/inverse transforms completed in 2.92 seconds with
approximately 9 MiB resident memory. This is checkpoint evidence, not an end-to-end
render measurement or performance guarantee. The full route still has to include
source processing, conversion, publication, hashing, and mastering within the PLAN
budget.

## 6. Deliberate exclusions

This checkpoint does not:

- open the configured IR, expected kernel, audit/parity buses, or any fixture path;
- construct source-stage output or bind either frozen BMW route;
- crop bootstrap/pre-roll, perform wet selection, or convert to Float32;
- apply `2^-26` calibration, encode WAV, build stems or masters, or publish artifacts;
- serialize a manifest, admit public `render()` success, or establish complete-route
  hashes; or
- produce production-listenable audio or satisfy the user listening gate.

The next isolated reference integration must connect the already frozen source stage,
IR conversion, and this unchanged convolver through a test-only fixture adapter. Only
that complete route may reproduce all six stem and two master hashes and publish the
controlled listening set.
