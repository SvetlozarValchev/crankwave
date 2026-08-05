# Post-parity active-intake listening gate

Status: superseded by user transient listening on 2026-08-05; historically accepted
on the held-dyno comparison on 2026-08-04.

## Supersession

This document preserves the evidence and decision made at the 2026-08-04 held-dyno
gate. That comparison did not exercise a hard throttle reopening after a lift. Direct
live free-rev listening on 2026-08-05 exposed a clop/pop where the expected exhaust
bark should occur. With the audible intake route absent, both the canonical 192 kHz
master and a separate experiment using the exact 44.1 kHz engine-sim master ordering
restored the bark. Their agreement rules out delivery rate or master ordering as the
cause of that defect.

The audible intake route is therefore withdrawn from the production master. The
intake and plenum thermodynamics remain in the physical simulation; only their
pressure-derived audible contribution is withdrawn. The clips, hashes, and acceptance
record below remain valid historical evidence for the narrower held-dyno comparison,
not current acceptance of the intake route across engine transients.

The clean exhaust-only closure keeps the former flow-coupled WAV as an upstream raw
source/conditioning/IR oracle and gives the new stateful listening master its own
identity instead of overwriting history:

- `reference/oracles/bmw-m52b28/bmw-m52b28-canonical-exhaust-only-master-v4-20khz-5b7f919-dyno-1500-6500rpm.wav`;
- `8,640,598` bytes, complete-WAV SHA-256
  `58ec677b0565fad0e9a3b9eedba9590d5af9891c647185ebdb9c98f7e469cde7`;
- decoded PCM24 SHA-256
  `52fef731caf12b9a6353e0ed3a928039db74277193d99847b244f852edebf01f`;
  and
- clean Release render-body time `42.082 s` on the development PC.

That current master contains exhaust only. JSON cannot author the rejected audible
intake route, and the production presentation session has no intake-pressure DSP or
mixing path. Intake-pressure capture remains diagnostic data rather than sound.

## Historical decision

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

## Post-acceptance performance closure

The first full browser run after acceptance exposed two separate integration issues:
the smoke test still pinned the former exhaust-only export, and Chrome's running pump
did not retain enough scheduling margin for the additional active route. Neither issue
was an active-intake PCM regression.

Three changes closed that performance gate without introducing another renderer or
changing accepted samples:

- `2dfee5696c762e7e78ce4caec5d24e505972ccb3` caches the 48 exact rational phase
  kernels visited by canonical 20 kHz-to-192 kHz reconstruction;
- `b2a62030e8922fc594a8e6db2142eb8fc412fbeb` mirrors each reconstruction history so
  every 257-tap read is contiguous; and
- `1c77c714814739fa317fed7303d7d2d44cb3ae2f` uses `MessageChannel` for immediate
  browser-pump continuation and processes a bounded four core blocks per running turn.
  Genuine pacing waits still use timers.

A clean `scripts/verify-browser-workbench.sh` run passed its runtime-unit, WASM
integration, canonical-capture, operating-bench, and headless-Chrome sequence. The
browser export is exactly `3,840,056` bytes with SHA-256
`f1c057e2eef807f0d68711dc1e196e04093faab3c306d37f7523c8a8d2ce6130`;
both BMW and 6.2 L V8 interactive starts report `startupUnderruns = 0`; and all 22
repository packages pass the visible workbench compile/session checks.

A fresh native BMW M52TU held-dyno bake completed in `35.00 s` end to end. All 11
published WAVs—six exhaust stems, three intake stems, and both masters—are byte-for-byte
identical to the accepted `9530402` artifact set. Therefore the performance work needs
no new listening decision: it changes reconstruction storage and browser scheduling,
not the accepted 20 kHz capture, 192 kHz presentation, or audio result.
