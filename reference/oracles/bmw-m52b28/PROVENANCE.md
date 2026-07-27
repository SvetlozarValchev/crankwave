# BMW M52B28 listening-oracle provenance

- Status: behavioral evaluation oracle only
- Distribution status: unresolved; do not ship as a production asset

## Preserved artifact

The preserved file is:

```text
bmw-m52b28-5th-gear-equivalent-dyno-1500-6500rpm.wav
```

Its media identity is recorded in `media.json`. Human listening established that this
is the liked baseline. Hashes and manifests establish identity, not sound quality.

## Verified generation chain

### 1. Frozen source snapshot

- engine-sim source: `9617562a7a5615c2bf84c9ec39cd5ae25c560059`
  (`P1.8 Add fixed source calibration`)
- Piranha submodule: `db28b8f7a8c566b777d226833cd001b6767a79bf`
- simple-2d-constraint-solver:
  `7ab315d7fa81dd2969ac3bf5709b8c90a7651a7a`

The generator had been configured at P1.7 commit
`b393edfcb33f8b45835b7fb21555a431c0a103a2` with a dirty worktree. Its recorded
aggregate source digest has been independently reproduced from the P1.8 snapshot and
pinned submodules, so P1.8 is the effective source content.

Generator identity recorded by the package:

- executable SHA-256:
  `6b7c5db18756f52645a9004fafb2157fc4261992ff88798ca913b368e0360383`
- executable size: `1,828,728` bytes
- build: Release, Clang 21.1.8
- aggregate generator-source SHA-256:
  `b345061519e56e422f7ea135f56cd276de18f81897d655d3061f4308f87ece13`

That exact executable has since been overwritten. Its machine-code identity was not
recreated, but the canonical capture's behavior has now been reconstructed and proved
as described below.

### Behavioral reconstruction proof

The pinned source and submodules were rebuilt independently with Clang 21.1.8, Release
settings, and floating-point contraction disabled. The clean reconstructed executable
has SHA-256
`4b1fa28c8cbf6f4ae6c2c27e601c254298459a99baeb4dcdd0a0b7f95695bc51`.

An optional, compile-time-disabled observation patch then captured physical/control
terms separately from redundant validation-only legacy excitation and bus values. Four
canonical renders were compared:

1. the clean P1.8 reconstruction;
2. the patched build with audit support compiled out;
3. the patched audit-capable build with capture inactive;
4. the patched audit-capable build actively writing the fixture.

All six stems and the scenario preview were byte-for-byte identical in all four runs.
The actively observed wet stems also reproduced the preserved listening-master WAV
byte-for-byte with the frozen FFmpeg command below. This proves that the reconstructed
canonical trajectory matches the liked oracle and that observation did not perturb its
audio output.

The resulting self-contained fixture, closed manifest, independent validator, and exact
observation patch are preserved at:

- [`reference/fixtures/bmw-m52b28-p18`](../../fixtures/bmw-m52b28-p18/README.md)
- [`P18_PRESENTATION_RENDERER.md`](../../fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md)
- [`reference/tooling/p18-reference-audit.patch`](../../tooling/p18-reference-audit.patch)
- [`tools/validate_reference_fixture.py`](../../../tools/validate_reference_fixture.py)

The fixture is the forward handoff. Subsequent renderer and simulator work no longer
depends on the external failed tree.

### 2. Offline capture command

The recovered command was:

```bash
build-clang-p1.5/truck-audio-baker \
  --repo-root workspace/listening/render-snapshot/source \
  --script assets/bmw_m52b28_offline_main.mr \
  --output workspace/listening/bmw-m52b28-fifth-equivalent-pull-package \
  --profile quick \
  --rpm-min 1500 \
  --rpm-max 6500 \
  --rpm-step 5000 \
  --settle 1 \
  --sweep-duration 15 \
  --source-calibration-gain 1.490116119384765625e-8 \
  --delivery-rate 192000 \
  --no-validation \
  --capture-id baked.loaded_acceleration
```

The bake took approximately 9.76 seconds and package validation passed.

Relevant package identities:

- package manifest SHA-256:
  `2059cc78bb2524a062bbf79b7d4a10d152af6f4a75b2205b19cbdfe6e2feb95c`
- source-capture manifest SHA-256:
  `6b48b7e99accc0c18703576e53c45e8061d6b406053a714e60a5f061b133829d`

The historical external package was treated as read-only evidence under:

```text
/home/cbethax/depot/dev/engine-sim-offline-failed/engine-sim/workspace/listening/
  bmw-m52b28-fifth-equivalent-pull-package/
```

It is not a dependency of subsequent clean-room work; its required identities and the
new pre-presentation fixture are preserved in this repository.

### 3. Listening-master command

The final WAV was not the package's PCM16 audition preview. With the working directory
set to the package's
`source-captures/captures/baked.loaded_acceleration/` directory, it was made from the
two Float32 exhaust stems using:

```bash
ffmpeg \
  -i exhaust_000/linear_wet_dry.wav \
  -i exhaust_001/linear_wet_dry.wav \
  -filter_complex \
  '[0:a][1:a]amix=inputs=2:normalize=0,volume=128,afade=t=in:st=0:d=0.02:curve=qsin,afade=t=out:st=14.98:d=0.02:curve=qsin' \
  -map_metadata -1 \
  -metadata title='BMW M52B28 fifth-gear-equivalent dyno sweep' \
  -metadata comment='1500-6500 RPM over 15 s; 85% effort; coherent sum of two linear wet/dry exhaust buses; fixed x128 monitoring gain; no limiter or compressor' \
  -c:a pcm_s24le \
  bmw-m52b28-5th-gear-equivalent-dyno-1500-6500rpm.wav
```

Re-running this mastering command against the preserved input stems reproduced the
oracle SHA-256 exactly.

The reproducing tool reported `ffmpeg version 6.1.1-3ubuntu5`, built with Ubuntu
GCC 13. Its complete `ffmpeg -version` output has SHA-256
`5b320c97f515e79171f10a2147d875ffb6b14bd2b25681047b73c46fe0a527ee`.
This records the byte-equivalence environment; the M2 renderer may later own this
small deterministic mastering step directly.

Input stem identities:

- exhaust 0 `linear_wet_dry.wav`:
  `a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32`
- exhaust 1 `linear_wet_dry.wav`:
  `f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d`

The listening master adds only coherent summation, fixed ×128 monitoring gain, 20 ms
quarter-sine fades, and PCM24 conversion. It has no limiter, compressor, loudness
normalization, or AGC.

## What the oracle contains

The WAV contains:

- two exhaust buses with alternating cylinder-index assignments;
- engine-sim source conditioning;
- full configured impulse-response coloration;
- a coherent mono sum and listening gain.

It does not contain:

- an intake bus;
- a separate mechanical bus;
- transmission sound;
- starter or shutdown behavior;
- a physical microphone/spatial scene.

The BMW asset gives the two exhaust systems audio volumes `0.5` and `1.0`. Both use
`ir_lib.default_0`, which resolves to:

```text
es/sound-library/smooth/smooth_39.wav
```

IR identity:

- SHA-256:
  `75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc`
- mono PCM16, 44.1 kHz, 33,705 frames, 0.764286 seconds
- configured volume: `0.001` on both buses
- effective source support used by the baker: 6,907 samples
- deterministic conversion to 30,071 samples at 192 kHz using the baker's
  24-tap/4,096-phase Blackman-windowed-sinc, per-source-area, DC-preserving policy

The exact input, canonical converted binary64 kernel, and all six expected stems are
preserved under
[`reference/fixtures/bmw-m52b28-p18/presentation`](../../fixtures/bmw-m52b28-p18/presentation/).
The headerless little-endian kernel is 240,568 bytes and has SHA-256
`940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598`.

For this capture, `linear_wet_dry` equals `linear_configured_ir` byte-for-byte on each
bus. The oracle is therefore a 100%-configured-IR result, not a dry engine-source
reference. The static IR is a material part of the liked body and resonance.

The P1.8 route performs causal 10 kHz-to-192 kHz conversion, then fractional-delay
jitter, DC removal, a derivative mixture, multiplicative low-pass air noise, and
subnormal cleanup. Configured IR/wet-dry generation follows. The baker's fixed `2^-26`
source calibration is applied afterward, before serialization and headroom checks.

## Verified scenario semantics

- Scenario type: prescribed dyno RPM trajectory.
- Audible-interval requested effort/throttle: constant `0.85`.
- Audible-interval ignition and fuel delivery: on.
- Nominal range: 1500–6500 RPM.
- Audible duration: 15 seconds.
- Newly preserved fixture history: 17 seconds, including one second of bootstrap and
  one second of loaded pre-roll before the audible interval. Exact starter, dyno,
  ignition, fuel, and throttle intervals are closed in the fixture manifest.
- Control interval: 20 ms; 751 recorded control points.
- First two points: 1500 RPM.
- Last recorded point: 6493.333333 RPM.
- Physics clock: 10 kHz.
- Source-processing, acoustic, delivery, and file clocks: 192 kHz.
- Public seed: `12648430` (`0xC0FFEE`).

“Fifth-gear-equivalent” is a listening label. The baker drives crank RPM directly and
leaves the transmission neutral. The asset's fifth gear happens to be `1.00:1`, but
this is not a wheel-speed, road-load, or transmission simulation. Its recorded dyno
reaction values must not be treated as a steady brake-torque map.

## Verified model inputs

- entry script SHA-256:
  `b78978e0fab039fa3d53d902b4cfad6d36f5e6ab4095f6a453901064e54d6adf`
- BMW asset SHA-256:
  `2c7746f82e86cc22b0ab243f61e7fb8c155c3abf1084ad7b6f8bfee3d4e875a9`
- engine-sim script entry SHA-256:
  `699fdf93f3424854cdc14308bcf4358810ee12ec9fd6f3630cf27ad4090882e0`
- IR-library script SHA-256:
  `ddcfb6d03d7177a8f5e9233ffd311ee81cf74b045f84a86c08071fbfac4eb815`
- engine name: BMW M52B28
- topology: inline six, four stroke
- computed displacement: `0.0027930477143328905 m³`
- firing order: 1-5-3-6-2-4

The P1.8 tree pin is authoritative for the complete recursive `.mr` import closure,
including units/constants, actions, objects/defaults, infrastructure, part-library
intake/cam/head/ignition definitions, utilities, and settings. The wrapper also imports
the default UI theme, which does not affect the headless capture audio.

The E36 vehicle/transmission block in the asset was added by the user's fork. It did
not drive this prescribed-RPM capture.

## Licensing and attribution

- The engine-sim repository declares the MIT License, copyright 2022
  AngeTheGreat/Ange Yaghi. Its notice is preserved beside this record.
- The pinned simple-2d-constraint-solver declares MIT.
- The pinned Piranha tree contains no applicable license notice and remains
  `NOASSERTION`.
- The BMW script contains no citations for its physical parameter values.
- The fork-added E36 values also contain no data citations.
- `smooth_39.wav` has no asset-specific license, source session, generation recipe, or
  chain-of-title in the repository.
- The source package classifies the IR input, configured-IR output, selected scripts,
  and derived audition output as `NOASSERTION`.

The repository-level MIT notice must not be silently treated as proof that the IR or
derived oracle may be redistributed. The preserved WAV is for local behavioral
evaluation until the rights question is resolved.

## Remaining unknowns and limitations

- Physical provenance and accuracy of the BMW parameter values.
- Physical meaning and provenance of `smooth_39.wav`.
- Whether every inherited fork audio change is necessary to reproduce the liked result.
- The historical executable's exact machine-code identity; canonical-capture behavior,
  rather than executable bytes, is what the reconstruction proof closes.
- The historical package manifests remain only in the external failed tree. All six
  freshly regenerated, byte-identical reference stems, their presentation input,
  resolved kernel identity, the resulting oracle, and the pre-presentation fixture are
  preserved here.

The historical package alone contains only processed exhaust artifacts.
`linear_wet_dry` is post-IR; even `linear_dry` is
post-jitter/DC/derivative/air-noise conditioning and exposes only combined exhaust
scalar buses. It could not supply the clean physical `CaptureBlock` boundary by itself.
That former gap is closed by the newly observed parity and audit lanes described above.
