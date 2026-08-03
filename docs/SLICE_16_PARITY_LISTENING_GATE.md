# Slice 16 parity listening gate

Status: accepted by the user on 2026-08-03 after all seven production renders
completed under the native zero-saturation gate and the user reported that the set
"sounds good."

This is the final curated headless-parity listening checkpoint from
[`../PLAN.md`](../PLAN.md). The recorded implementation identity is
`d1606fa0e28543a141cd27b5957debb628ac0112` (`render: reject saturated audition
publication`). At that commit the native publisher counts every PCM24 quantization
that would saturate and rejects the bake as `native-audition-saturated` before
publication. Each artifact below has a complete v10 manifest, so each render
succeeded with exactly zero saturated audition samples.

All audition masters are mono, 192 kHz, 24-bit PCM. Peaks below are sample peaks,
measured in dBFS. SHA-256 values cover the complete WAV file, including its metadata
and audio payload. The corresponding raw masters are mono, 192 kHz Float32 WAVs.

## Accepted matrix

### Fixed-cam inline six: BMW M52TUB28 held-dyno pull and lift

- Engine: `data/engines/bmw-m52tub28-cleanroom/engine.json`
- Scenario:
  `data/engines/bmw-m52tub28-cleanroom/scenarios/held-dyno-pull-lift-1500-6500rpm.json`
- Artifact root: `artifacts/listening/slice16-bmw-inline-held-dyno-d1606fa/`
- Audition:
  `artifacts/listening/slice16-bmw-inline-held-dyno-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-bmw-inline-held-dyno-d1606fa/audio/master.engine.raw.wav`
- Duration: `15.000000 s`
- Audition peak: `-6.344444 dBFS`
- Audition WAV SHA-256:
  `0c3553463d07867cfe4382a97a48c5bad7ccf57af7cd0154843893e1ba3a658e`
- Raw WAV SHA-256:
  `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742`

### Fixed-cam V8: Toyota Sequoia 3UR-FE inertial dyno

- Engine: `data/engines/sequoia-3ur-fe-cleanroom/engine.json`
- Scenario:
  `data/engines/sequoia-3ur-fe-cleanroom/scenarios/inertial-dyno-650-6000rpm.json`
- Artifact root: `artifacts/listening/slice16-toyota-v8-inertial-dyno-d1606fa/`
- Audition:
  `artifacts/listening/slice16-toyota-v8-inertial-dyno-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-toyota-v8-inertial-dyno-d1606fa/audio/master.engine.raw.wav`
- Duration: `15.000000 s`
- Audition peak: `-22.935810 dBFS`
- Audition WAV SHA-256:
  `058ee3828915d0f18b6d0d94b35c0a9d035a73cedd6e3264f48c9ad992c5b30e`
- Raw WAV SHA-256:
  `057bbef9f226414be8aa1096efa36cd39b3ca953e27c96919ed9eeac34490c51`

This is the Toyota scenario's first frozen native parity baseline. It therefore has
no earlier accepted native artifact against which byte identity could be claimed.

### Bank-local VTEC: Honda B18C5 transition

- Engine: `data/engines/honda-b18c5-cleanroom/engine.json`
- Scenario:
  `data/engines/honda-b18c5-cleanroom/scenarios/inertial-dyno-5000-8000rpm.json`
- Artifact root: `artifacts/listening/slice16-honda-vtec-transition-d1606fa/`
- Audition:
  `artifacts/listening/slice16-honda-vtec-transition-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-honda-vtec-transition-d1606fa/audio/master.engine.raw.wav`
- Duration: `15.000000 s`
- Audition peak: `-17.602984 dBFS`
- Audition WAV SHA-256:
  `d683a6b1af18f10a503a328941afb5a34a6ee1253347810f3fc0d68dc26f2fdc`
- Raw WAV SHA-256:
  `7073de0f683c9852173588aae5616f74c18c94951df05ed7ce74d6365f6dbad5`

### Governed engine: Kohler CH750 load step

- Engine: `data/engines/kohler-ch750-cleanroom/engine.json`
- Scenario:
  `data/engines/kohler-ch750-cleanroom/scenarios/governed-load-step-2740rpm.json`
- Artifact root: `artifacts/listening/slice16-kohler-governor-load-step-d1606fa/`
- Audition:
  `artifacts/listening/slice16-kohler-governor-load-step-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-kohler-governor-load-step-d1606fa/audio/master.engine.raw.wav`
- Duration: `16.000000 s`
- Audition peak: `-2.390011 dBFS`
- Audition WAV SHA-256:
  `7e3f108033b137b15d12eff01cfc01faa61d46fa0f569e27b4c4a4db40cf16cb`
- Raw WAV SHA-256:
  `4bf2035a5285e1da547e4cc339fe0aa74a383bad0b24d3a70d0d81b8bd3aa409`

### One-level master/slave rods: radial-five free rev

- Engine: `data/engines/radial-5-cleanroom/engine.json`
- Scenario:
  `data/engines/radial-5-cleanroom/scenarios/warm-running-free-rev-1500rpm.json`
- Artifact root: `artifacts/listening/slice16-radial-master-rod-free-rev-d1606fa/`
- Audition:
  `artifacts/listening/slice16-radial-master-rod-free-rev-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-radial-master-rod-free-rev-d1606fa/audio/master.engine.raw.wav`
- Duration: `4.700000 s`
- Audition peak: `-18.062032 dBFS`
- Audition WAV SHA-256:
  `309a83989d23dcde9944815c25cd1639e48f553e3085c36b3ff77aace683b2dd`
- Raw WAV SHA-256:
  `cc2fd2c17af75b68dfb9a64dca5ae05a271d28e048508e05cb666159badaae61`

### Drivetrain: BMW already-moving fifth-gear pull and lift

- Engine: `data/engines/bmw-m52tub28-cleanroom/engine.json`
- Scenario:
  `data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-fifth-gear-pull-lift-1500rpm.json`
- Artifact root: `artifacts/listening/slice16-bmw-fifth-gear-d1606fa/`
- Audition:
  `artifacts/listening/slice16-bmw-fifth-gear-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-bmw-fifth-gear-d1606fa/audio/master.engine.raw.wav`
- Duration: `23.100000 s`
- Audition peak: `-5.862808 dBFS`
- Audition WAV SHA-256:
  `688c98045911e6ef28a9d669d047161054a9462781f0c5268861db9b853904fe`
- Raw WAV SHA-256:
  `d970c9961db415f7025151aad1a38da5bee45d119b858abb786b15563401c2c8`

### Drivetrain: BMW launch and first-to-second shift

- Engine: `data/engines/bmw-m52tub28-cleanroom/engine.json`
- Scenario:
  `data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-launch-first-second.json`
- Artifact root: `artifacts/listening/slice16-bmw-launch-shift-d1606fa/`
- Audition:
  `artifacts/listening/slice16-bmw-launch-shift-d1606fa/audio/master.engine.audition.wav`
- Raw:
  `artifacts/listening/slice16-bmw-launch-shift-d1606fa/audio/master.engine.raw.wav`
- Duration: `11.000000 s`
- Audition peak: `-6.025176 dBFS`
- Audition WAV SHA-256:
  `17c7977064f8b4a3220730af18ab189a5b840b6344110eb7bd42b250276ce9ae`
- Raw WAV SHA-256:
  `8ddbee737278851ab178e9b5afee0c6e9b12b7fa4613cc92ecf8d87b09273277`

## Exact accepted-baseline regression proof

Six members already had user-accepted native recordings. For each one, the Slice 16
raw master is whole-WAV byte-identical to its accepted baseline: the SHA-256 in the
matrix above is also the SHA-256 of the baseline raw WAV below. The decoded audition
PCM is likewise byte-identical; the last column is the SHA-256 of the headerless
interleaved signed-24-bit sample stream decoded from both old and new audition WAVs.

| Slice 16 member | Accepted baseline artifact root | Exact raw WAV SHA-256 | Exact old/new decoded audition PCM SHA-256 |
|---|---|---|---|
| BMW inline held dyno | `artifacts/listening/bmw-m52tub28-held-dyno-5de43bb-pull-lift-overrun-1500-6500rpm/` | `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742` | `fbda34577c0213fb8ac2b07540669f5bd4f00abdffae8d7acafdd630f44984ed` |
| Honda VTEC transition | `artifacts/listening/honda-b18c5-bank-local-vtec-candidate-6b2d127/` | `7073de0f683c9852173588aae5616f74c18c94951df05ed7ce74d6365f6dbad5` | `9e48ff0e6a1a394ff4c14ea891a4ca7ca5e335a8e4f488141973249bd7e3289d` |
| Kohler governed load step | `artifacts/listening/kohler-ch750-governor-028773a-settled-load-step-2740rpm/` | `4bf2035a5285e1da547e4cc339fe0aa74a383bad0b24d3a70d0d81b8bd3aa409` | `14fd6ee333d992e5050649f46941b1e838086ff260cf32f7a9f85116136b4131` |
| Radial-five free rev | `artifacts/listening/radial-free-engine-candidate-2e5d70d/` | `cc2fd2c17af75b68dfb9a64dca5ae05a271d28e048508e05cb666159badaae61` | `1987d62545c021f12044975bc10693b9eb11e55c627183148c56d384fb4fb702` |
| BMW fifth-gear pull | `artifacts/listening/bmw-m52tub28-free-vehicle-4446100-fifth-gear-pull-lift-1500rpm/` | `d970c9961db415f7025151aad1a38da5bee45d119b858abb786b15563401c2c8` | `9776c1b49c9ec4046b0b8be8362379c05956c794bf47356989bb492706c23fbc` |
| BMW launch and shift | `artifacts/listening/bmw-free-vehicle-launch-guard-11d5853/` | `8ddbee737278851ab178e9b5afee0c6e9b12b7fa4613cc92ecf8d87b09273277` | `907798e9f627d67380e839b688d959aeb31286a76163c7c312752c3e1494afab` |

The Slice 16 audition WAV container hashes are intentionally new because their
encoder metadata carries the updated audition-mix method configuration hash that
declares saturation accounting. Decoding strips that metadata; the equal decoded-PCM
hashes above prove that the gate did not alter any accepted samples.

## Acceptance boundary

The seven recordings cover the representative fixed-cam inline and V families,
bank-local VTEC, governor response, one-level master/slave rod mechanics, and the
accepted loaded drivetrain procedures. Their clean zero-saturation publication,
exact six-member regression evidence, first Toyota native baseline, and the user's
"sounds good" verdict close the Slice 16 headless-parity listening gate.

This acceptance does **not** accept or begin the separate post-parity fidelity queue.
Higher source rates, per-cylinder acoustic lanes, audible intake and mechanical buses,
physically coupled variation/noise, richer exhaust transfer, stems, stereo, and cabin
rendering remain later fidelity work with their own incremental listening gates.
