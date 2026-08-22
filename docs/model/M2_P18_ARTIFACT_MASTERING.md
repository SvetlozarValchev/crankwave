# Exact P1.8 artifact and mastering contract

Status: frozen local-evaluation contract for the BMW reference fixture

Contract ID: `p18_reference_artifacts_and_mastering_v1`

Applies to: the eight artifacts required by
`bmw-m52b28-reference-source-matrix-v1`, their repository-owned publication paths,
the raw reference reduction, the audition transform, and the exact WAVE containers

This record freezes behavior before implementation. It does not implement a renderer,
admit public `render()` success, produce a listening candidate, or make a new physics
or sound-quality claim.

## 1. Authority and claim boundary

The six stem identities come from the self-contained P1.8 fixture capsule. The
audition identity comes from the preserved liked oracle. The raw-master identity and
the detailed mastering checkpoints below were independently derived from those
content-addressed inputs and checked against the frozen FFmpeg 6.1.1 command in
[`PROVENANCE.md`](../../reference/oracles/bmw-m52b28/PROVENANCE.md).

The normative behavior is this record, not a dependency on FFmpeg or engine-sim.
Links to the tagged FFmpeg 6.1.1 source document the observed reference behavior:

- [`af_amix.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavfilter/af_amix.c)
  and
  [`af_volume.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavfilter/af_volume.c)
  for Float32 summation and monitoring gain;
- [`af_afade.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavfilter/af_afade.c)
  for the quarter-sine fade;
- [`audioconvert.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libswresample/audioconvert.c)
  and [`pcm.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavcodec/pcm.c)
  for the observed Float32-to-S32-to-PCM24 path;
- [`riffenc.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavformat/riffenc.c)
  and [`wavenc.c`](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavformat/wavenc.c)
  for the reference RIFF/WAVE layout.

All eight artifacts are local evaluation evidence. The unresolved rights for
`smooth_39.wav` prohibit treating the configured-IR derivatives as distributable
production dependencies. Exact identity establishes only reproduction of the narrow
exhaust-only P1.8 presentation route on its pinned numerical envelope. It does not
establish:

- intake, mechanical, starter, drivetrain, cabin, environment, or spatial content;
- public-render or M3 engine-physics parity;
- production completeness or higher fidelity;
- distribution permission; or
- user listening acceptance.

The audition master is a monitoring derivative of the selected exhaust stems. It is
not a third sound source and is never fed back into physics or another acoustic stage.
Its fixed x128 factor is monitoring gain, not physical source or microphone
calibration. The `fifth-gear-equivalent` title is a listening label inherited from the
oracle, not evidence of a simulated gearbox, road load, or vehicle. The raw master is a
newly derived canonical artifact; unlike the audition master, it is not a separately
preserved or user-liked historical file.

## 2. Frozen artifact set and publication paths

Every audio contract is mono, 192,000 Hz, and exactly 2,880,000 frames covering the
15-second audible interval. The six stems and raw master use `float32le`; the audition
master uses `pcm_s24le`. The source-matrix diagnostic flag is part of the artifact
identity.

| Role | Transaction-relative path | Diagnostic | Bytes | Complete SHA-256 |
|---|---|---:|---:|---|
| `exhaust.reference.0.dry` | `audio/exhaust.reference.0.dry.wav` | yes | 11,520,058 | `e5a96cb5d3b9f1732e741916706a99c6a7c5e1a912e3d751cb92846d33ce6eeb` |
| `exhaust.reference.0.configured_ir` | `audio/exhaust.reference.0.configured-ir.wav` | yes | 11,520,058 | `a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32` |
| `exhaust.reference.0.selected` | `audio/exhaust.reference.0.selected.wav` | no | 11,520,058 | `a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32` |
| `exhaust.reference.1.dry` | `audio/exhaust.reference.1.dry.wav` | yes | 11,520,058 | `2ad2ed41097af30421047f3e4a6033086ec70b9731082833699caad1da9d81ea` |
| `exhaust.reference.1.configured_ir` | `audio/exhaust.reference.1.configured-ir.wav` | yes | 11,520,058 | `f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d` |
| `exhaust.reference.1.selected` | `audio/exhaust.reference.1.selected.wav` | no | 11,520,058 | `f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d` |
| `master.reference.raw` | `audio/master.reference.raw.wav` | no | 11,520,058 | `2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146` |
| `master.reference.audition` | `audio/master.reference.audition.wav` | no | 8,640,302 | `f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb` |

The complete eight-file artifact set is 89,280,708 bytes, including all WAVE headers
and metadata. Its sample payloads total 89,280,000 bytes. The completed transaction
also publishes:

```text
manifest/render-manifest.v2.json
manifest/render-manifest.v2.json.sha256
```

The canonical reference-manifest encoder owns the first file's contents under
`crankwave.render-manifest.reference-presentation.v2`.
`DirectoryRenderSink` owns the lowercase digest sidecar. Neither metadata path is an
audio artifact role.

The configured-IR and selected hashes are intentionally equal on each route because
the frozen wet amount is exactly one. Both semantic artifacts remain required; one
cannot be silently omitted or represented only by an alias in the transaction.

## 3. Common Float32 WAVE representation

All six stems and `master.reference.raw` use the already implemented classic
little-endian IEEE Float32 WAVE representation:

```text
58-byte header
fmt chunk size       18
format tag           3
channels             1
sample rate          192000
byte rate            768000
block align          4
bits per sample      32
fmt extension size   0
fact sample count    2880000
data bytes           11520000
```

The exact 58-byte header is:

```text
5249464632c8af0057415645666d7420120000000300010000ee020000b80b00040020000000666163740400000000f22b006461746100c8af00
```

Its SHA-256 is
`98439b16a5dcdf8a3350aa34cf82f6b0e5e46dc6cf45b4355537f615ccbaf6b7`.
There is no metadata or trailing pad.

## 4. Raw reference master

Inputs are the already calibrated stored Float32 samples from
`exhaust.reference.0.selected` and `exhaust.reference.1.selected`. For audible frame
`j`, in route order:

```text
raw[j] = Float32(route_0_selected[j] + route_1_selected[j])
```

The addition is one IEEE-754 binary32 operation under round-to-nearest/ties-to-even.
Inputs and result must be finite. There is no averaging, normalization, gain, fade,
limiter, clamp, or additional source calibration.

Frozen identities:

| Boundary | SHA-256 |
|---|---|
| 11,520,000-byte Float32 sample payload | `fe2475249df2f6a51b2c82c8251493216db1a1ec094a7a0c5577a11c430f410f` |
| complete 11,520,058-byte WAVE | `2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146` |

## 5. Audition gain and fades

Let:

```text
N = 2880000
F = 3840
fade_out_start = N - F = 2876160
pi = binary64 bits 0x400921fb54442d18
```

First apply the exact monitoring gain in binary32:

```text
monitor[j] = Float32(raw[j] * Float32(128))
```

Evaluate the quarter-sine gain in binary64 and in the written operation order:

```text
if 0 <= j < F:
    k = j
    ratio = binary64(k) / binary64(F)
    angle = (ratio * pi) / 2
    gain = sin(angle)
else if F <= j <= fade_out_start:
    gain = 1
else:
    k = N - j
    ratio = binary64(k) / binary64(F)
    angle = (ratio * pi) / 2
    gain = sin(angle)

faded[j] = Float32(binary64(monitor[j]) * gain)
```

The gain remains binary64 through the multiplication. Rounding it to Float32 before
the multiplication changes oracle samples. State is frame-indexed and independent of
caller chunk boundaries.

Boundary meanings:

- frame `0` has gain zero;
- frame `3839` uses `3839 / 3840`;
- frames `3840` through `2876160`, inclusive, have unity gain;
- frame `2876161` uses `3839 / 3840`;
- frame `2879999` uses `1 / 3840`, so the last sample is not forced to zero.

Golden binary64 gain identities:

| `k` | Bits |
|---:|---|
| 0 | `0x0000000000000000` |
| 1 | `0x3f3acee9e6f0d0f9` |
| 2 | `0x3f4acee9c14f807a` |
| 426 | `0x3fc6314d8dfc1b0f` |
| 1583 | `0x3fe34da873b8103d` |
| 3838 | `0x3fefffff4c544ff4` |
| 3839 | `0x3fefffffd31513de` |
| 3840 | `0x3ff0000000000000` |

The canonical little-endian binary64 table for `k = 0..3840` has SHA-256
`25514f82ee4ebfcf9e15c6aa0807f40f636ab0edc59500bfcf2ca066e0d4d476`
on the pinned Clang 21.1.8/glibc/x86-64 environment.

Two sentinels distinguish the required binary64 gain multiplication from a premature
Float32 gain:

| Frame | Monitor bits | Correct faded bits | Correct PCM24 | Premature-Float32 faded bits / PCM24 |
|---:|---|---|---:|---|
| 426 | `0x3ccf2426` | `0x3b8fa800` | 36,776 | `0x3b8fa7ff` / 36,775 |
| 1583 | `0x3d45b3ef` | `0x3cee853f` | 244,244 | `0x3cee8540` / 244,245 |

## 6. Float32-to-PCM24 conversion

No dither or noise shaping participates. Reject non-finite input. For finite Float32
`x = faded[j]`:

```text
if x >= 1:
    s32 = INT32_MAX
    saturated = true
else if x <= -1:
    s32 = INT32_MIN
    saturated = true
else:
    scaled = Float32(x * 0x1p31f)
    s32 = round_to_nearest_ties_even(scaled)
    saturated = false

pcm24 = floor(s32 / 256)
```

Serialize the low 24 bits of `pcm24` little-endian. The floor division fixes the
observed arithmetic right shift for negative values; C++ signed division's
round-toward-zero behavior is not equivalent. This is also not direct rounding at
24-bit scale.

The converter reports saturation. A successful P1.8 reference render requires exactly
zero saturated samples; saturation is not a mastering fallback. The frozen audition
peak is `0.6672717928886414`, approximately `-3.514 dBFS`, and the oracle has zero
saturation.

Golden quantizer vectors:

| Float32 bits | S32 | PCM24 | Little-endian bytes |
|---|---:|---:|---|
| `0x00000000` | 0 | 0 | `00 00 00` |
| `0x80000000` | 0 | 0 | `00 00 00` |
| `0x2f800000` | 0 | 0 | `00 00 00` |
| `0x30400000` | 2 | 0 | `00 00 00` |
| `0x33ff8000` | 256 | 1 | `01 00 00` |
| `0x34004000` | 256 | 1 | `01 00 00` |
| `0xb0400000` | -2 | -1 | `ff ff ff` |
| `0x33800000` | 128 | 0 | `00 00 00` |
| `0xb3800000` | -128 | -1 | `ff ff ff` |
| `0x3f7fffff` | 2,147,483,520 | 8,388,607 | `ff ff 7f` |
| `0x3f800000` | 2,147,483,647 | 8,388,607 | `ff ff 7f` |
| `0xbf7fffff` | -2,147,483,520 | -8,388,608 | `00 00 80` |
| `0xbf800000` | -2,147,483,648 | -8,388,608 | `00 00 80` |

## 7. Exact audition WAVE container

`master.reference.audition` begins with a fixed 302-byte
WAVE_FORMAT_EXTENSIBLE/INFO prefix:

| Offset | Exact content |
|---:|---|
| 0 | FourCC `RIFF` |
| 4 | LE32 RIFF size `8640294` (`0x0083d726`) |
| 8 | FourCC `WAVE` |
| 12 | FourCC `fmt ` |
| 16 | LE32 chunk size `40` |
| 20 | LE16 format tag `0xfffe` |
| 22 | LE16 channel count `1` |
| 24 | LE32 sample rate `192000` |
| 28 | LE32 byte rate `576000` |
| 32 | LE16 block align `3` |
| 34 | LE16 container bits `24` |
| 36 | LE16 extension size `22` |
| 38 | LE16 valid bits `24` |
| 40 | LE32 channel mask `0x00000004` (front center) |
| 44 | PCM GUID bytes `01 00 00 00 00 00 10 00 80 00 00 aa 00 38 9b 71` |
| 60 | FourCC `LIST` |
| 64 | LE32 chunk size `226` |
| 68 | FourCC `INFO` |
| 72 | FourCC `ICMT` |
| 76 | LE32 value size `140` |
| 80 | 139-byte comment plus NUL |
| 220 | FourCC `INAM` |
| 224 | LE32 value size `44` |
| 228 | 43-byte title plus NUL |
| 272 | FourCC `ISFT` |
| 276 | LE32 value size `14` |
| 280 | 13-byte encoder compatibility identity plus NUL |
| 294 | FourCC `data` |
| 298 | LE32 data size `8640000` |
| 302 | first PCM24 payload byte |

Exact NUL-terminated ASCII metadata values:

```text
ICMT = 1500-6500 RPM over 15 s; 85% effort; coherent sum of two linear wet/dry exhaust buses; fixed x128 monitoring gain; no limiter or compressor
INAM = BMW M52B28 fifth-gear-equivalent dyno sweep
ISFT = Lavf60.16.100
```

`Lavf60.16.100` is a frozen compatibility byte inherited from the oracle container.
It does not claim that the new encoder invokes or embeds FFmpeg. All metadata chunk
sizes are even. There is no `fact`, `JUNK`, `bext`, trailing pad, or trailing metadata
chunk.

Frozen prefix identities:

| Byte interval | SHA-256 |
|---|---|
| complete prefix `[0,302)` | `781f526e8df5ae102c5af9c3a6c5734fe2fa5e45f4ca03e0c1b04d59e3ebd16b` |
| `fmt ` chunk `[12,60)` | `1b5b3995d7f2eddec82cdb694cbcae10980703b7bd02860b6e2f790553a429d5` |
| `LIST` chunk `[60,294)` | `8b9ed60efbe263602b146dd56ec810e01e90995b95a92796c9bd9342e399cadf` |

## 8. Mastering checkpoint identities

The implementation must expose or test these exact intermediate payloads. A matching
final hash alone is not permission to hide a different mastering path.

| Boundary | Encoding | SHA-256 |
|---|---|---|
| raw master | Float32 LE | `fe2475249df2f6a51b2c82c8251493216db1a1ec094a7a0c5577a11c430f410f` |
| monitoring gain | Float32 LE | `0a2abe8ea8f166c1022efda26c57e5ad4eda5e7cb515100a5e6d16eb465318db` |
| faded | Float32 LE | `af194389df2ba20ab9d1bc5e3f97735afbb6c76d4c215a7ac2e1d45ecd3a3633` |
| pre-truncation | S32 LE | `b0505bc9a81cfdcea0256ff6e5731ac2a1f58f90f43911bc84d799926151d924` |
| audition data | PCM24 LE | `2153869958bb924e4eda277a37e95eab1abb7c29aa9fa389c1fa8f879e7bfdcf` |
| audition file | exact WAVE | `f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb` |

## 9. Implementation admission tests

The future mastering implementation is not admitted until focused tests:

1. pin every gain bit pattern and both premature-rounding sentinels above;
2. pin fade boundaries `0`, `3839`, `3840`, `2876160`, `2876161`, and `2879999`;
3. exercise every quantizer vector, finite saturation reporting, and non-finite
   rejection;
4. compare the 302-byte prefix byte-for-byte and pin its component hashes;
5. reproduce every intermediate and complete master identity from the two frozen
   selected stems;
6. require zero saturation and the frozen peak for the reference render; and
7. prove byte identity across several valid input and output callback partitions.

Those tests verify the documented algorithm. Only the later complete eight-artifact
fixture render and user listening gate can accept the acoustic route.
