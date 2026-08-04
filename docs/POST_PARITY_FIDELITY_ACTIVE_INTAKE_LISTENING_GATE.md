# Post-parity active-intake listening gate

Status: accepted by user listening on 2026-08-04.

## Decision

The cooker has one production intake-audio path. Each rendered `intake_inlet` route
uses the captured plenum pressure already produced by the low-order gas simulation;
there is no optional silent route, legacy renderer, or alternate intake synthesizer.
The accepted path is:

```text
20 kHz absolute plenum pressure
  -> subtract scenario ambient pressure
  -> causal 20 kHz-to-192 kHz reconstruction
  -> deterministic 10 Hz DC removal
  -> authored route source gain
  -> shared publication calibration and Float32 stem publication
  -> identical dry/configured-transfer/selected intake stems
  -> ordered raw and audition masters
```

Intake does not consume exhaust jitter or air-noise random streams and does not pass
through exhaust conditioning, an impulse response, or convolution. Exhaust continues
through its existing accepted path. The canonical clocks remain 400 capture frames at
`20000/1 Hz` to 3,840 presentation frames at `192000/1 Hz` per 20 ms method block.

The implementation sequence is:

- `db39f4317f0bb31d4a86c176309c6b871a1bf39b`: explicit silent topology checkpoint;
- `0264dd322ebaf8c29326f8b2a754caec0c2fb33f`: isolated intake-pressure source stage;
- `4ef681453589c3c857f42c07e3bfeb489648e969`: captured plenum-pressure publication;
- `1cb19f7275035b0ed65a70ae9c9f9518f73e3b5a`: sole active presentation and master path;
- `6e06ef42ad59e913570975838b0b67fc5ec89a4a`: correct bake-result failure reporting;
- `de35486e23b215fc6baa3b155c6aa0eaa7e5e500`: presentation randomness restricted to
  exhaust routes; and
- `953040294274362fd699436e3040cbcee8f70d30`: accepted BMW intake gain (`4.0`).

The first item is historical evidence, not a retained runtime alternative.

## Listening comparison

The gate uses engine `bmw-m52tub28-cleanroom` and scenario
`bmw-m52tub28-cleanroom-held-dyno-pull-lift-1500-6500rpm`. All three files are mono
PCM24 at 192 kHz and 15 seconds long:

| Clip | Purpose | Repository path | Complete-WAV SHA-256 |
|---|---|---|---|
| A | accepted exhaust-only 20 kHz control | `artifacts/listening/fidelity-intake-pressure-bmw-20khz-9530402/comparison/A-control-exhaust-only.wav` | `e77a236d2c94fb66b6dc2799e212b6cd0f4f9bd27643568e2f1dd379cb884dcf` |
| B | active intake solo | `artifacts/listening/fidelity-intake-pressure-bmw-20khz-9530402/comparison/B-intake-solo.wav` | `f7738d24fc218266a33ea4d5a0959e9fbeabb239243e5bc2568f5a92b250acc4` |
| C | exhaust plus active intake | `artifacts/listening/fidelity-intake-pressure-bmw-20khz-9530402/comparison/C-full-exhaust-plus-intake.wav` | `24e48c3e1eba564ee34fbb57080faeccf6ce491399213215cb70355f076d4b95` |

`A` is byte-identical to the previously accepted
`fidelity-rate-bmw-20khz-canonical-a3ae7cd` audition master. `B` applies the same
`128.0` monitoring gain and 20 ms quarter-sine edge fades as the master; it is not
independently normalized. `C` is the candidate's native audition master.

No clip uses normalization, saturation, limiting, soft clipping, or point-specific
level matching. Observed peaks were `-6.2 dBFS` for A, `-14.2 dBFS` for B, and
`-6.1 dBFS` for C, so the comparison did not reach full scale.

## Exhaust-preservation proof

Every candidate exhaust WAV is byte-identical to its accepted 20 kHz control. The
terminology-only `configured_ir` to `configured_transfer` filename change does not
change its bytes. Candidate stems are under
`artifacts/listening/fidelity-intake-pressure-bmw-20khz-9530402/audio`; controls are
under `artifacts/listening/fidelity-rate-bmw-20khz-canonical-a3ae7cd/audio`:

| Route/stem | Complete-WAV SHA-256 |
|---|---|
| `exhaust.front.dry` | `c0fa165703e54ad5cd71a23938e9badc4d396aa92eddb3241a4098fd24e0097f` |
| `exhaust.front.configured_transfer` / control `configured_ir` | `d8b32bc6c5277f2e8d9c09387741bd8511417029843985bfaaef6df570cb7c01` |
| `exhaust.front.selected` | `d8b32bc6c5277f2e8d9c09387741bd8511417029843985bfaaef6df570cb7c01` |
| `exhaust.rear.dry` | `679592061575ed333da12021be8eddafad02337e7e6fb25e6171d46e078342e3` |
| `exhaust.rear.configured_transfer` / control `configured_ir` | `46ca91d04ad0f56e2e06f84d810cf1eb703bfb083e8d0846738e723a64fe4aeb` |
| `exhaust.rear.selected` | `46ca91d04ad0f56e2e06f84d810cf1eb703bfb083e8d0846738e723a64fe4aeb` |

The three candidate intake stems are also mutually byte-identical, as required by
their identity transfer, at SHA-256
`ce6c3aa1dd1cb35b4d2d9fe6c140ea164f45531e1b00bf215337861709f31405`.

## Runtime and acceptance boundary

The complete acceptance command took `39.38 s` on the development PC. The candidate
manifest separately records the render body's
`wall_elapsed_ns = 38,756,215,613` (`38.756 s`), one process thread, and one render
job. These are observations, not a machine-independent performance guarantee.

The user accepted A/B/C as sounding good on 2026-08-04. This closes the audible-intake
gate and makes the active path above canonical. It does not claim a resolved inlet
mouth impedance, intake radiation geometry, load-dependent transfer, stereo spatial
model, or mechanical/valvetrain source path; each remains a separate future fidelity
change with its own controlled audition.
