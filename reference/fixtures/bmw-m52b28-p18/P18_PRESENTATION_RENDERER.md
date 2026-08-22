# Exact P1.8 presentation-renderer record

Status: frozen behavioral reference for the BMW M52B28 P1.8 capsule

This document closes the algorithm side of the M2 fixture. Together with
`reference-audit.bin`, `component-seeds.bin`, `manifest.json`, the configured IR, and
the expected stems, it is sufficient to implement and evaluate the exact narrow P1.8
presentation route without reading an engine-sim working tree.

This is not clean-room production code and it is not a recommendation to retain these
models. It records legacy behavior in implementation-independent form so the first
clean renderer can be isolated from the later clean simulator. The two input channels
are runtime reference exhaust buses in uncalibrated `crankwave_source_unit`; they are
not microphone pressures or proven physical outlets.

## Authority, scope, and arithmetic

The source revision used to resolve this record is
`9617562a7a5615c2bf84c9ec39cd5ae25c560059`. The relevant source-file identities were:

| Frozen responsibility | SHA-256 of resolved P1.8 source file |
|---|---|
| PCG32 and floating draws | `d5c2fb93895d6a75adfd34cd6234a144981e62a4cba893c6ec1a6c1352a7dbb2` |
| fourth-order low-pass | `7897b861fa93831757e1e7addffa4c6d20eac195b50225f3c838903101326269` |
| jitter | `17de1aa805cf9a0936a184286868e018674c7ebd75744aec4b9295e347a3ffbc` |
| 10 kHz to 192 kHz reconstruction | `190bb370aa81562087e292a7611a0165190cfa24505bfdf78bf462bf745cdfa6` |
| conditioning and variant publication | `350cd2c9658aa598328fe0285874abb975e955cbb99ad18fe9d8661aec706371` |
| overlap-save convolution | `416597b1c4b3319d488f064e379bb889778fe53d640677b601d404fb736a247b` |
| PCM16 IR loading/support | `be581bbd9f39695c526204ce233f2c8048d02c8d54651b0fe5b52901eec27d73` |
| static IR resampling | `a5310e8d1f3d52346bb38d1991dfa6e4f82dcc194e80a8bcc59852135d0c3e45` |
| calibration and capture | `bae90d0624af537be7bbd202e82d8aea1e107c6920b9a5d7c63bc13de0019b1e` |

The source hashes are provenance, not external dependencies. The normative behavior is
the record below.

Unless a conversion is stated explicitly:

- signal, coefficient, filter-state, and FFT arithmetic is IEEE-754 binary64;
- the capture used round-to-nearest/ties-to-even and `-ffp-contract=off`;
- operations are evaluated in the statement and loop order shown, without fast-math,
  reassociation, or contraction;
- integer arithmetic is unsigned with the widths shown;
- `pi_filter = 3.14159265359`, while the reconstruction, static-IR, and FFT algorithms
  use `pi_full = 3.141592653589793238462643383279502884`;
- all deterministic DSP histories and counters are zero-initialized before physics
  record zero; RNG instances are initialized from the explicit seeds below;
- non-finite input or output is a hard failure, not a clamp or fallback.

The expected files pin the accepted result. Bit identity is required on the captured
Clang 21.1.8/x86-64 numerical environment. Other conforming implementations report
sample differences rather than claiming cross-toolchain byte identity.

## Input, clocks, blocks, and crop

Read both `legacy_reference.exhaust_bus_pre_dsp` values for every audit record in
`[0, 170000)`, in route order 0 then 1. Each record is one simultaneous input frame at
10,000 Hz. Do not read the parity lane as renderer input.

The complete route is:

```text
two 10 kHz buses
  -> causal 10 kHz-to-192 kHz reconstruction
  -> jitter
  -> DC removal and derivative mixture
  -> multiplicative filtered air noise
  -> subnormal cleanup
  -> dry variant
  -> configured IR and 100% wet selection
  -> binary64-to-Float32 publication
  -> exact 2^-26 source calibration
  -> Float32 WAVE stems
```

Process all 170,000 physics frames as 850 consecutive blocks of 200 physics frames,
each producing exactly 3,840 source frames. The first 50 blocks are bootstrap, the next
50 are loaded pre-roll, and the final 750 are audible. The complete run produces
3,264,000 source frames. Discard the first 384,000 published frames only when selecting
the audible interval; retain the following 2,880,000 frames. No DSP state is reset at
that boundary.

The source/acoustic/delivery rate is 192,000 Hz, so there is no delivery decimator.
The convolution supports at most 9,600 source frames, but every reference call contains
exactly 3,840. Preserve that partition for byte comparison. State is continuous across
blocks. The convolution FFT length remains 65,536 for every block. There is no tail
flush, latency compensation, phase alignment, stitching, looping, normalization,
limiter, or saturation.

## Causal physics-to-source reconstruction

Method identity:
`kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_cutoff0p95_source_nyquist_unity_dc_binary64_v2`.

Constants:

```text
tap_count = 257
half_width = 128
phase_interval_count = 4096
table_row_count = 4097
kaiser_beta = 12
cutoff_cycles_per_physics_frame = 0.475
bandwidth = 0.95
physics_rate = 10000
source_rate = 192000
slowdown_divisor = 1
```

Define the following in binary64:

```text
bessel_i0(v):
    quarter_square = 0.25 * v * v
    term = 1
    sum = 1
    for k = 1..40:
        term = term * (quarter_square / binary64(k * k))
        sum = sum + term
    return sum

sinc(v):
    if abs(v) < 1e-15: return 1
    radians = pi_full * v
    return sin(radians) / radians
```

Evaluate `inverse_i0_beta = 1 / bessel_i0(12)` exactly once. Construct one 257-tap
Kaiser window. For `tap = 0..256`:

```text
normalized = (binary64(tap) - 128) / 128
radicand = max(0, 1 - normalized * normalized)
window[tap] =
    bessel_i0(12 * sqrt(radicand)) * inverse_i0_beta
```

After construction, set `window[0]` and `window[256]` to exact positive zero. For each
`phase = 0..4095`, in increasing tap order:

```text
fraction = binary64(phase) / 4096
offset = binary64(tap) - 128
distance = offset - fraction
row[tap] = 0.95 * sinc(0.95 * distance) * window[tap]
sum = left-to-right sum(row[0..256])
row[tap] = row[tap] / sum, in increasing tap order
```

The final row is a phase-wrap row:

```text
row_4096[0] = 0
row_4096[tap] = row_0[tap - 1] for tap = 1..256
```

Each route owns a 257-frame circular history initialized to zero. Both routes share the
same exact clock. Let `distance_to_next_output = 0`, `span = 192000`, and
`oldest_history_frame = 0`. For each simultaneous 10 kHz input frame:

```text
remaining = span - distance_to_next_output
output_count = 1 + (remaining - 1) // 10000
offset = distance_to_next_output
```

For each of those outputs, derive the table phase without overflowing `uint64`:

```text
remainder = offset
phase0 = 0
repeat 12 times:
    phase0 = phase0 << 1
    if remainder >= span - remainder:
        remainder = remainder - (span - remainder)
        phase0 = phase0 | 1
    else:
        remainder = remainder * 2
mix = binary64(remainder) / binary64(span)
```

For each route, traverse the circular history from `oldest_history_frame` for all 257
taps and accumulate left-to-right:

```text
coefficient =
    row_phase0[tap]
    + (row_phase0_plus_1[tap] - row_phase0[tap]) * mix
sample = sample + history[tap, route] * coefficient
```

Then set `offset = offset + 10000` and produce the next output. Only after every output
for the current 10 kHz interval has been calculated, replace the oldest history frame
with the current two-route input, advance the circular index, and set:

```text
distance_to_next_output = offset - span
```

Thus the current physics input is committed after the interval outputs; this is a
causal 128-physics-frame uncompensated delay, not a centered offline resampler.

## PCG32 streams and floating draws

Use the route-specific `synthAirNoise` and `synthJitter` initial-state/stream pairs in
`manifest.json` and `component-seeds.bin`. The combustion and starter streams do not
participate after the captured audit buses.

```text
air route 0:    initial_state=0x75bc579d4c90a640 stream=0x7e4ef6200e7c70c1
air route 1:    initial_state=0x208e57f73615bd95 stream=0x786d92e584c43b78
jitter route 0: initial_state=0x9e2b91cd0dc51cfc stream=0x1ae6ee3019603abb
jitter route 1: initial_state=0xdb7540a0c8b54d74 stream=0x41ddcdeb066bf214
```

These are inputs to the seeding procedure, not the post-seeding internal states.

For each PCG32 instance, seed exactly:

```text
state = 0_u64
increment = (stream << 1) | 1
discard next_u32()
state = state + initial_state
discard next_u32()
```

`stream` must be at most `UINT64_MAX >> 1`. A draw is:

```text
next_u32():
    old = state
    state = old * 6364136223846793005 + increment  # modulo 2^64
    xorshifted = u32(((old >> 18) xor old) >> 27)
    rotation = u32(old >> 59)
    return (xorshifted >> rotation)
         | (xorshifted << ((-rotation) & 31))       # modulo 2^32

uniform_double():
    hi = u64(next_u32() >> 5)
    lo = u64(next_u32() >> 6)
    return binary64((hi << 26) | lo) * 0x1.0p-53

uniform_signed_double():
    return 2 * uniform_double() - 1
```

Every jitter frame consumes one `uniform_double()` from that route's jitter stream.
Every air-noise frame consumes one `uniform_signed_double()` from that route's
air-noise stream. Each call therefore advances its PCG32 instance twice.

## Exact fourth-order Butterworth recurrence

Jitter modulation and air noise each own an independent fourth-order low-pass with
four prior input and four prior output samples, all initially zero. Let `x1` and `y1`
be the newest stored values and `x4` and `y4` the oldest.

For cutoff `fc` and rate `fs`, calculate the coefficients in the listed statement
order with `pi_filter`:

```text
f = tan(pi_filter * fc / fs)
f2 = f * f
f3 = f2 * f
f4 = f2 * f2
m = -2 * cos(5 * pi_filter / 8)
n = -2 * cos(7 * pi_filter / 8)

a0 = 1 + (m+n)*f + (2+n*m)*f2 + (m+n)*f3 + f4
a1 = (-4 - 2*(n+m)*f + 2*(m+n)*f3 + 4*f4) / a0
a2 = (6 - 2*(2+m*n)*f2 + 6*f4) / a0
a3 = (-4 + 2*(m+n)*f - 2*(m+n)*f3 + 4*f4) / a0
a4 = (1 - (n+m)*f + (2+m*n)*f2 - (m+n)*f3 + f4) / a0
```

For each input sample `x`:

```text
numerator = f4 / a0
          * (x + 4*x1 + 6*x2 + 4*x3 + x4)
feedback = -a1*y1 - a2*y2 - a3*y3 - a4*y4
y = numerator + feedback
shift x4..x1 and append x as newest
shift y4..y1 and append y as newest
return y
```

Recalculating unchanged coefficients at a block boundary does not reset either
history.

The canonical Clang 21.1.8/glibc coefficient identities are:

| cutoff | value | binary64 bits | decimal |
|---:|---|---|---:|
| 10,000 Hz | `a0` | `0x3ff8978a6f333f13` | 1.5369972556836202 |
| 10,000 Hz | `a1` | `0xc0092c4db522bd0e` | -3.1466325904108379 |
| 10,000 Hz | `a2` | `0x400e48bc73169953` | 3.7855156890174526 |
| 10,000 Hz | `a3` | `0xc0006f859c71d349` | -2.0544540617214904 |
| 10,000 Hz | `a4` | `0x3fdb17708abd3f6a` | 0.42330564068560383 |
| 10,000 Hz | `f4/a0` | `0x3f3fae65b361cfd8` | 0.00048341734817047627 |
| 2,000 Hz | `a0` | `0x3ff16dc259aad6e0` | 1.0892966749792592 |
| 2,000 Hz | `a1` | `0xc00ea1c3764dc23d` | -3.8289860956649862 |
| 2,000 Hz | `a2` | `0x40160176c2620086` | 5.5014295933070851 |
| 2,000 Hz | `a3` | `0xc00c1f1df6143338` | -3.5151938652910779 |
| 2,000 Hz | `a4` | `0x3feaf7f2ff589845` | 0.84276723739891024 |
| 2,000 Hz | `f4/a0` | `0x3eb1b070634db3df` | 1.0543593706683149e-06 |

## Jitter

Each route owns:

```text
history_length = 41
maximum_offset = 40
history = 41 binary64 zeros
write_offset = 0
jitter_amount = 0.5
modulation_cutoff_hz = 10000
modulation_rate_hz = 192000
noise_excitation_scale = sqrt(192000 / 96000) = sqrt(2)
                       = binary64 bits 0x3ff6a09e667f3bcd
```

For each reconstructed route sample:

```text
history[write_offset] = sample
write_offset = (write_offset + 1) modulo 41

mean_offset = 20
random_offset = uniform_double(jitter_rng) * 40
rate_normalized_offset =
    20 + (random_offset - 20) * noise_excitation_scale
filtered_offset =
    butterworth_4_lowpass_10khz(rate_normalized_offset * 0.5)
clamped = clamp(filtered_offset, 0, 40)
lower_offset = floor(clamped)
upper_offset = ceil(clamped)
fraction = clamped - lower_offset

index(delay) = (write_offset + delay) modulo 41
lower = history[index(u64(lower_offset))]
upper = history[index(u64(upper_offset))]
jittered = lower + (upper - lower) * fraction
```

The indexing above is normative. In particular, after the write, offset 0 addresses
the oldest slot and offset 40 addresses the just-written slot; do not replace it with
a differently oriented fractional-delay convention.

## DC removal, derivative, and multiplicative air noise

Constants and initial state for each route:

```text
dt = 1 / 192000
   = binary64 bits 0x3ed5d867c3ece2a5
dc_time_constant = 1 / (20 * pi_filter)
                 = binary64 bits 0x3f904c26be3b05a1
dc_alpha = dt / (dc_time_constant + dt)
         = binary64 bits 0x3f357088f45745f4
dc_state = 0
derivative_previous = 0
derivative_mix = binary64(Float32(0.01))
               = 0.0099999997764825821
               = binary64 bits 0x3f847ae140000000
air_noise_amount = 1
air_noise_cutoff_hz = 2000
air_noise_rate_hz = 192000
noise_excitation_scale = sqrt(2)
```

After jitter, for each route and frame:

```text
dc_alpha = dt / (dc_time_constant + dt)
dc_state = dc_alpha * jittered + (1 - dc_alpha) * dc_state
dc_removed = jittered - dc_state

derivative = (jittered - derivative_previous) / dt
derivative_previous = jittered

noise = uniform_signed_double(air_noise_rng) * noise_excitation_scale
filtered_noise = butterworth_4_lowpass_2khz(noise)
noise_mix = 1 * filtered_noise + (1 - 1)

conditioned =
    derivative * derivative_mix
    + dc_removed * noise_mix * (1 - derivative_mix)
if fpclassify(conditioned) == subnormal:
    conditioned = +0
```

`conditioned` is the binary64 dry/pre-convolution variant. Jitter is evaluated before
the route's air-noise draw. State and random streams remain continuous across all
blocks and the audible crop.

## IR support and static conversion

The source is the manifest-pinned mono signed PCM16 little-endian WAVE at 44,100 Hz.
Decode every PCM sample. The meaningful-support threshold is strict:

```text
support = 1 + the greatest index for which abs(pcm16[index]) > 100
```

For `smooth_39.wav`, the full count is 33,705 and `support = 6,907`. Only that prefix
participates. Normalize PCM amplitude by positive maximum 32,767, not 32,768, and use
the configured binary64 volume whose bits are `0x3f50624dd2f1a9fc`.

Static conversion method identity:
`blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_binary64_v3`.

Constants:

```text
half_width = 12
tap_count = 24
phase_count = 4096
minimum_retained_weight = long_double(1e-8)
source_rate = 44100
target_rate = 192000
target_count = round_half_up(6907 * 192000 / 44100) = 30071
cutoff = min(1, target_rate / source_rate) = 1
area_gain = source_rate / target_rate = 0.2296875
```

For every table `phase = 0..4096` and `tap = 0..23`:

```text
fraction = binary64(phase) / 4096
offset = tap - 12 + 1
distance = binary64(offset) - fraction
normalized_distance = distance / 12
window =
    if abs(normalized_distance) < 1:
        0.42
        + 0.5 * cos(pi_full * normalized_distance)
        + 0.08 * cos(2 * pi_full * normalized_distance)
    else:
        0
weight = cutoff * sinc_1e_minus_12(cutoff * distance) * window
```

Here `sinc_1e_minus_12(v)` returns 1 when `abs(v) < 1e-12`, otherwise
`sin(pi_full*v)/(pi_full*v)`.

Visit target outputs in increasing order while maintaining the exact rational source
position `center + source_fraction / 192000`, initially zero:

```text
scaled_phase = source_fraction * 4096
phase0 = scaled_phase // 192000
phase1 = min(4096, phase0 + 1)
phase_mix = binary64(scaled_phase % 192000) / 192000
weight = table[phase0][tap]
       + (table[phase1][tap] - table[phase0][tap]) * phase_mix

source_fraction = source_fraction + 44100
center = center + source_fraction // 192000
source_fraction = source_fraction % 192000
```

For a tap, the candidate source index is `center + (tap - 11)` and is skipped if it is
outside `[0, 6907)`.

The conversion normalizes finite-matrix columns, not output rows:

1. Sum every retained weight for each source index into an 80-bit x86 extended
   `long double`, visiting outputs then taps in increasing order.
2. If a source weight sum is at most `1e-8`, assign that PCM sample to its nearest
   half-up-rounded target index instead of dividing by the small sum.
3. Otherwise accumulate
   `long_double(pcm16[source]) * long_double(weight) / source_weight_sum[source]`
   into the target's extended-precision accumulator, again outputs then taps.
4. First evaluate `coefficient_scale = configured_volume / 32767` in binary64.
   Convert that result to extended precision, multiply each target accumulator by it,
   and cast once to binary64.

The preserved result contains 30,071 little-endian binary64 coefficients and has
SHA-256
`940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598`.
Regeneration must check that identity rather than silently accepting a nearby kernel.

## Causal overlap-save convolution and selection

Method identity:
`causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1`.

Use:

```text
coefficient_count = 30071
history_count = 30070
maximum_block_frames = 9600
transform_length =
    smallest power of two >= history_count + maximum_block_frames
    = 65536
```

Create a bit-reversal table for 16 bits. Create forward roots in increasing index:

```text
root[k] = complex(cos(-2*pi_full*k/65536), sin(-2*pi_full*k/65536))
for k = 0..32767
```

The transform first applies the bit-reversal permutation, swapping only when
`index < reversed`. Then for widths `2, 4, ..., 65536`, visit bases in increasing
order and butterfly offsets in increasing order:

```text
root_step = 65536 / width
odd = value[base + offset + width/2] * root[offset * root_step]
even = value[base + offset]
value[base + offset] = even + odd
value[base + offset + width/2] = even - odd
```

For inverse transforms, conjugate the stored root in each butterfly and, after all
stages, multiply every complex value by `1 / 65536` in increasing index order.

Zero-pad the 30,071 IR coefficients to 65,536, forward-transform once, and retain that
spectrum. For every 3,840-frame reference input block:

1. clear the complex work array to zero;
2. copy the 30,070-sample continuous history to indices `[0, 30070)`;
3. copy the current 3,840-sample dry block immediately after it;
4. prepare the next history as the final 30,070 samples of old history plus current
   input;
5. forward-transform the work array;
6. multiply each bin by the corresponding kernel bin in increasing order;
7. inverse-transform;
8. publish the real values beginning at index 30,070 for the block length;
9. commit the prepared history.

There is no direct-FIR fallback and a block above 9,600 frames is rejected. Both
routes use the same kernel but independent convolution history. The configured-IR
variant is the convolution output. With wet amount exactly 1:

```text
selected = 1 * configured_ir + (1 - 1) * dry
```

## Float32 publication, fixed calibration, and WAVE identity

For each route, convert dry, configured-IR, and selected from binary64 to IEEE-754
Float32 only after convolution/selection. Then, for each stored Float32 sample:

```text
calibrated = binary64(sample) * 0x1.0p-26
sample = Float32(calibrated)
```

Serialize a classic little-endian RIFF/WAVE file with:

```text
58-byte header
fmt chunk size = 18
format tag = 3 (IEEE float)
channels = 1
sample rate = 192000
byte rate = 768000
block align = 4
bits per sample = 32
cbSize = 0
fact chunk size = 4
fact sample count = 2880000
data bytes = 11520000
```

Write each Float32 payload bit pattern little-endian, with no metadata or padding after
the data. Each expected stem is therefore 11,520,058 bytes. Compare all six files
against the frozen hashes in `manifest.json`.

The audition master is not part of the renderer stem algorithm. It is the coherent
sum of the two selected stems, fixed times-128 monitoring gain, 20 ms quarter-sine
entrance and exit fades, and mono PCM24 serialization at 192 kHz. Its exact FFmpeg
command is frozen in `PROVENANCE.md`; the expected master hash is in `manifest.json`.

## M2 acceptance and isolation

An M2 reference/test adapter may read `reference-audit.bin` and inject its two pre-DSP
buses immediately before the reconstruction stage. The normal renderer API cannot
accept a bus trace or fixture path.

Acceptance requires:

1. regenerated IR-kernel identity;
2. all six stem sample payloads compared at every frame;
3. exact stem hashes on the reference numerical environment;
4. exact reconstructed audition-master hash; and
5. a user listening gate against the preserved oracle.

M3 must not link the audit reader or accept captured buses. It must reach the same
renderer only through newly simulated `CaptureBlock.reference_parity` values and the
already frozen excitation equation.
