# Headless BMW held-dyno listening gate

Status: accepted by user on 2026-07-31 after implementation, regression, and clean
production-render checks passed.

This is the slice-11 checkpoint from [`../PLAN.md`](../PLAN.md). It changes only crank
motion ownership: the accepted BMW gas, combustion, friction, configuration-inertia,
exhaust routing, conditioning, impulse response, and mastering paths remain in place.
It does not introduce a replacement sound model.

## Implemented behavior

The `held_dyno` scenario owns a post-step target-RPM lane and nonnegative absorbing and
driving torque limits. Each released physics step computes the signed actuator torque
needed to reach the target, clamps it to those limits, advances the same dynamic crank
used by `free_engine`, and publishes dyno reaction as the exact negative of actuator
torque. The implementation follows the velocity-constraint semantics audited from
pristine engine-sim commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`; its reduced one-degree-of-freedom method
identity is `bounded-held-dyno-speed-constraint`.

Fixed preparation is explicitly an initialization hold, not an observed actuator
step. The contract requires the target lane to equal the exact initial RPM throughout
that hidden interval. Actuator and reaction telemetry begin when dynamic motion is
released.

At this slice-11 checkpoint, the internal runtime accepted a per-step target RPM and
absorbing/driving limit, but those commands were not yet public. Slice 13 subsequently
published them through `EngineSession`, exact C ABI v4, WASM, and Worker protocol v2;
see
[`contracts/HEADLESS_OPERATING_BENCH_API_SLICE_13.md`](contracts/HEADLESS_OPERATING_BENCH_API_SLICE_13.md).

## BMW procedure

- Engine: `data/engines/bmw-m52tub28-cleanroom/engine.json`
- Scenario:
  `data/engines/bmw-m52tub28-cleanroom/scenarios/held-dyno-pull-lift-1500-6500rpm.json`
- Implementation commit: `5de43bb4af9a3c47e2c0861ebe9ec611b8b00e26`
- Hidden preparation: 3 seconds at exactly 1,500 RPM and full throttle
- Audible pull: 1,500 to 6,500 RPM at 500 RPM/s and full throttle
- Plateau: 1 second at 6,500 RPM and full throttle
- Lift/overrun: 4 seconds at 0.04 throttle with the target descending to 2,500 RPM
- Absorbing limit: 10,000 lb-ft
- Driving limit: 0 N-m

The zero driving limit is deliberate. The dyno may absorb engine torque during the
pull and hold, but it may not motor the crank to force a pitch trace after lift. When
natural overrun falls below the descending target, the actuator and reaction become
exactly zero and the engine continues under its own gas and friction torque.

## Executable evidence

The focused primitive and authored BMW end-to-end tests pass together with scenario
resolution, contract, manifest encoder, scheduler, capture-session, and accepted BMW
free-engine source-parity regressions.

- Maximum pull target error: 14.9441 RPM at physics frame 129,847
  (6,477.46 RPM achieved versus 6,492.4 RPM requested)
- Mid-pull dyno reaction: 108.036 N-m absorbing
- Plateau speed: exactly 6,500 RPM
- Final natural-overrun speed: 1,207.02 RPM
- Final actuator/reaction: exactly zero

The small pull underrun is bounded driving saturation, not an unconstrained controller
error: this procedure forbids the dyno from adding torque. The one-second plateau lets
the engine catch and hold exactly 6,500 RPM.

## Listening artifact

Production rendering from the clean implementation commit took 29.05 seconds and
published:

`artifacts/listening/bmw-m52tub28-held-dyno-5de43bb-pull-lift-overrun-1500-6500rpm/`

Listen to:

`audio/master.engine.audition.wav`

The clip is 15.000 seconds, mono, 192 kHz, 24-bit PCM. Its measured peak is
`-6.344444 dBFS` and RMS level is `-17.541168 dBFS`, so it is not clipped.

| Artifact | SHA-256 |
|---|---|
| `audio/master.engine.audition.wav` | `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496` |
| `audio/master.engine.raw.wav` | `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742` |
| `manifest/render-manifest.v6.json` | `a47d7f57187a390a9632f3f5f5338d775c68b0caad02d426c7e515c184274780` |

The manifest records the clean Git commit, compiler/runtime identity, resolved
`held_dyno` request, target-lane hash, method identity, output hashes, and measured
execution facts.

## Acceptance boundary

This gate asks only whether the target-driven pull, exact hold, lift, and unforced
overrun sound coherent through the already accepted BMW renderer. It does not accept
public live dyno controls, drivetrain behavior, or later capture procedures. Slice 12
may now begin from the exact artifact and implementation identities recorded above.

User verdict: accepted without a requested correction.
