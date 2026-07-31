# BMW free-vehicle listening gate

Status: accepted by user on 2026-07-31 after implementation, focused regressions,
clean production renders, and accepted-path byte checks passed.

This is the slice-12 checkpoint from [`../PLAN.md`](../PLAN.md). It adds only the
forward vehicle motion owner around the accepted engine and audio core. Gas,
combustion, source friction, exhaust excitation, routing, conditioning, impulse
response, and mastering are unchanged.

## Implemented behavior

The finite authored `free_vehicle` mode owns a nonnegative linear vehicle speed,
travelled distance, neutral or one ordered forward gear, clutch engagement, and a
one-sided service brake. Each released physics step:

1. consumes the right-continuous throttle, operating-state, gear, clutch, and brake
   controls at the left boundary;
2. predicts the engine crank from the already accepted dynamic-crank torque state;
3. solves clutch and road-load impulses together in the frozen pristine row order
   using exactly 128 projected passes; and
4. commits engine angle, vehicle speed, and distance semi-implicitly.

The road load is pristine engine-sim's constant rolling resistance plus quadratic
aerodynamic drag at its fixed 25 C air-density assumption. The explicitly authored
maximum service-brake force is a greenfield extension. Reverse, wheel slip, tire
forces, driveline compliance, and open-ended/live drivetrain commands are not part of
this slice.

The resolved scenario owns a complete immutable copy of the selected rig values; the
runtime has no hidden pointer back to compiler/package state. Gear ratio, selected
gear, clutch lane, brake lane, road/clutch primitive methods, and the exact coupled
solve all participate in request identity.

## Checkpoints

| Commit | Checkpoint |
|---|---|
| `0783352` | Exact bounded clutch-then-road coupled solve and hashed method identities. |
| `812e0a6` | Self-contained finite `FreeVehicle` contract, provenance, validation, and canonical request encoding. |
| `24a2092` | Shared dynamic-crank runtime and capture-session execution. |
| `4446100` | BMW service-brake capacity and the two authored listening procedures. |

Focused contract, resolver, manifest, primitive, runtime/capture, native-input, BMW
FreeEngine parity, held-dyno, and full-build checks pass. The coupled checks cover a
brake holding a clutch-loaded car at rest, locked engine/vehicle deceleration,
neutral road load, held preparation, launch, an exact gear boundary, and finite
completion.

The pre-slice accepted held-dyno audition and raw WAVs remain byte-for-byte identical,
both before and after the BMW rig gained its service-brake capacity:

| Accepted artifact | SHA-256 |
|---|---|
| Held-dyno audition WAV | `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496` |
| Held-dyno raw WAV | `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742` |

## Listening set

Both production renders identify clean commit
`44461002ace15575936296cfbc8666639cea441e`. They ran concurrently; the 11-second
launch rendered in 19.9 seconds and the 23.1-second fifth-gear pull rendered in 36.8
seconds, so the complete parallel batch was bounded by the latter.

### Neutral, launch, and first-to-second shift

Scenario:
`data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-launch-first-second.json`

Artifact:
`artifacts/listening/bmw-m52tub28-free-vehicle-4446100-launch-first-second/`

The car begins stopped in neutral with clutch open and service brake held. After the
hidden warm preparation it releases the brake, selects first, stages clutch take-up,
climbs under load, cuts throttle and opens the clutch, passes through neutral, selects
second, re-engages, climbs again, and lifts.

Listen to `audio/master.engine.audition.wav`.

- duration: 11.000 seconds, mono, 192 kHz, 24-bit PCM
- peak: `-6.025176 dBFS`; RMS: `-17.780039 dBFS`; no full-scale samples
- audition SHA-256: `4a096535cda350ee52426638005a4197ad4d998a83efbfa658ac17b84bdd2216`
- raw SHA-256: `8ddbee737278851ab178e9b5afee0c6e9b12b7fa4613cc92ecf8d87b09273277`
- manifest SHA-256: `15a1042581ffff9450a028ed4162df5a2f70584e0c897b5e87c13b3ed279ef78`

### Already-moving fifth-gear pull and lift

Scenario:
`data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-fifth-gear-pull-lift-1500rpm.json`

Artifact:
`artifacts/listening/bmw-m52tub28-free-vehicle-4446100-fifth-gear-pull-lift-1500rpm/`

The car begins at `16.887400783 m/s` (`60.79 km/h`), the road speed corresponding to
1,500 RPM in the authored 1.00:1 fifth gear, with clutch locked. It applies full
throttle after preparation, accelerates naturally against vehicle inertia, rolling
resistance, and aerodynamic drag, then lifts. RPM is not prescribed and no dyno is
present.

Listen to `audio/master.engine.audition.wav`.

- duration: 23.100 seconds, mono, 192 kHz, 24-bit PCM
- peak: `-5.862808 dBFS`; RMS: `-17.649868 dBFS`; no full-scale samples
- audition SHA-256: `a60070611a71d5208d0222f0aab85f019b51a17ec1216573e2c6814294d4599f`
- raw SHA-256: `d970c9961db415f7025151aad1a38da5bee45d119b858abb786b15563401c2c8`
- manifest SHA-256: `87104d7445aa3055020a6072db1fea6493dbfdc173bab415b9bb7bb3c7b5f552`

## Acceptance boundary

This gate asks whether loaded launch, clutch take-up, the first-to-second shift,
natural fifth-gear acceleration, and lift sound coherent through the unchanged BMW
renderer. A rejection is fixed inside slice 12 before public drivetrain/API work is
stacked on top. Slice 13 does not begin until the user accepts this listening set.

User verdict: accepted without a requested correction.

Subsequent status: slice 13 publishes the accepted drivetrain's ordered gear inventory,
timestamped gear/clutch/brake controls, and mode-owned telemetry through the native,
C ABI v4, WASM, and Worker v2 boundary. See
[`contracts/HEADLESS_OPERATING_BENCH_API_SLICE_13.md`](contracts/HEADLESS_OPERATING_BENCH_API_SLICE_13.md).
