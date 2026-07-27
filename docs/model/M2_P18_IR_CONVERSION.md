# Exact P1.8 static-IR conversion contract

Status: normative architecture and verification boundary for the M2 static-IR
checkpoint

Applies to: strict configured-IR WAVE decoding, meaningful-support detection, and the
exact 44.1 kHz-to-192 kHz static conversion

This checkpoint regenerates one frozen binary64 convolution kernel. It does not
connect the captured exhaust buses, source stage, convolver, crop, stems, masters, or
public renderer. A kernel is not a listening candidate.

## 1. Authority and claim boundary

[`P18_PRESENTATION_RENDERER.md`](../../reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md)
is the numerical authority for support detection, coefficient construction, operation
order, configured gain, and floating-point requirements. The pinned input identity is
typed by [`M2_MANIFEST_INPUTS.md`](../contracts/M2_MANIFEST_INPUTS.md).

This implementation reproduces the narrow P1.8 presentation behavior. The configured
IR remains unknown-provenance coloration with local-evaluation-only rights. Exact
kernel identity proves neither physical microphone/radiation accuracy nor production
sound quality.

## 2. Ownership and seam

```text
borrowed WAVE bytes
  -> strict decoder
  -> owned PCM16 samples + meaningful-support count
  -> pure static converter
  -> owned 192 kHz binary64 coefficients
```

The decoder borrows bytes only for one call and returns owned samples. It performs no
path lookup or file I/O. The converter receives all decoded samples, independently
rechecks the supplied support count, and performs no file I/O or serialization.

The focused identity test alone opens the configured `smooth_39.wav`, verifies its
content identity, passes its bytes through these two seams, serializes the resulting
coefficients as little-endian binary64 for hashing, and discards them. It does not read
the preserved expected kernel or any audit/parity bus.

## 3. Strict PCM16 WAVE admission

The decoder accepts one classic RIFF/WAVE container whose declared RIFF extent is the
entire input. It requires:

- exactly one 16-byte `fmt ` chunk and exactly one `data` chunk, in either order;
- PCM format tag 1, one channel, 44,100 Hz, byte rate 88,200, block alignment 2,
  and 16 bits per sample;
- one frame-aligned little-endian signed PCM16 `data` chunk of at most 33,705 frames;
- structurally bounded ancillary chunks without interpreting or allow-listing their
  FourCCs, order, contents, or repetition; and
- one present padding byte after every odd-sized chunk, without assigning semantic
  meaning to that byte's value.

Duplicate `fmt ` or `data`, trailing bytes, truncated declarations, a missing pad
byte, or inconsistent media fields are rejected. This is strict about the configured
PCM media and memory bounds without overfitting incidental metadata written by one
WAVE authoring tool.

Meaningful support is one plus the last sample index whose integer magnitude is
strictly greater than 100. Values exactly `100` or `-100` do not count, and
`INT16_MIN` is widened before taking its magnitude. No sample crosses the threshold
means support zero; the converter rejects that as unusable input.

For the pinned asset, the complete input is 78,602 bytes, contains 33,705 samples, and
has meaningful support 6,907. Its SHA-256 is
`75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc`.

## 4. Exact static conversion

The method identity is
`blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_binary64_v3`.
It uses the authority's 24-tap, 4,097-row Blackman-windowed sinc table, exact rational
44,100/192,000 source clock, and positive half-up target-count rule. Support 6,907
therefore produces 30,071 coefficients.

Table weights, rational phase interpolation, and candidate-source traversal use
binary64 in the recorded output-then-tap order. Each source column is normalized in
x86 80-bit extended precision. Columns whose retained weight sum is at most the
extended conversion of binary64 `1e-8` use the authority's nearest half-up target
fallback before normal accumulation. The configured gain has binary64 bits
`0x3f50624dd2f1a9fc`; division by positive PCM16 maximum 32,767 occurs in binary64,
then scaling remains extended until the one final cast per coefficient.

The converter requires radix-2 `long double` with 64 significand bits and maximum
exponent 16,384. Other floating-point environments fail before allocation rather
than produce a nearby kernel under a different arithmetic model. Fast-math,
contraction, reassociation, a centered resampler, row normalization, and normalization
by 32,768 are not equivalent implementations.

The canonical 240,568-byte little-endian coefficient stream has SHA-256
`940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598`.
That serialized stream is a test comparator, not a runtime artifact contract.

## 5. Validation and bounds

The decoder returns a typed error and byte offset for malformed input. It never
partially exposes decoded samples. The converter rejects empty or oversized input,
zero/out-of-range/mismatched support, negative or non-finite configured gain, an
unsupported extended-precision environment, non-finite intermediate results, and an
out-of-range fallback target.

Maximum accepted source length is 33,705 frames and maximum target length is 146,743
coefficients. All tables and accumulators are bounded by those constants. There is no
silence, truncation, approximate fallback, asset substitution, or persisted mutable
state.

## 6. Verification anchors

Focused verification must establish:

1. exact PCM16 samples and strict support around `+/-100` and `INT16_MIN`;
2. rejection of malformed RIFF bounds, chunk extents, missing padding, duplicate
   media chunks, invalid media fields, and oversized data;
3. owned decoder output after the borrowed byte span is released;
4. half-up target counts and exact synthetic one-sample conversion bits;
5. rejection of invalid support, gain, and source-size inputs;
6. the pinned input size, hash, full frame count, and support;
7. selected coefficient bit probes and the complete canonical kernel hash; and
8. identical results under the supported GCC and Clang numerical envelopes, with
   sanitizer-clean bounded execution.

These checks prove only the decoder and static converter. The next isolated consumer
is specified by [`M2_P18_CONVOLUTION.md`](M2_P18_CONVOLUTION.md), but this checkpoint
does not connect to it. Crop, wet selection, Float32 calibration, WAV publication,
artifact hashes, complete-route performance, and listening acceptance remain later
checkpoints.
