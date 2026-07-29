# M4 BMW held idle-region and low-load gate

Status: implemented at `4b651273e007553a793762f847ba237a9ee3396f`; awaiting
user listening
Frozen: 2026-07-29

Execution identities, operating results, timings, hashes, and listening paths are
recorded in
[`M4_BMW_HELD_IDLE_LOW_LOAD_LISTENING_GATE.md`](M4_BMW_HELD_IDLE_LOW_LOAD_LISTENING_GATE.md).

This gate extends the accepted BMW held operating regression set into the idle-speed
region and a materially lower-load condition. It uses the same engine, held-speed test
cell, excitation, presentation, and publication path already accepted by listening.
It does not introduce a second simulator, alternate audio method, load-search system,
or compatibility surface.

## Product boundary

The first audio-follower product receives RPM from the host drivetrain. A dyno-held
idle-region capture is therefore a truthful input to that product when it is labelled
as held. It can reveal whether the current engine audio remains clean, cyclic, and
credible at low speed without first adding a self-governing crank-speed controller.

It is not a free idle. The current model produces approximately `+24.97 N*m` net shaft
torque at `700 rpm` and throttle command `0.0`; the held-speed test cell must absorb
that residual torque. The present closed-command torque crosses zero only near
`1355 rpm`. Accepting the audio in this gate does not accept those values as BMW torque
calibration, prove idle equilibrium, or hide the residual behind an idle controller.

Genuine free-idle behavior requires a separately justified closed-throttle flow and
idle-air/control model, released crank dynamics, explicit stall behavior, and a
bounded stability criterion. That work must receive its own physical contract and
listening gate rather than being smuggled into these captures.

## Frozen points and order

The request set contains exactly these two fresh, independent held-speed points in
this order:

| Index | Stable point key | Scenario ID | RPM | Throttle command |
|---:|---|---|---:|---:|
| 0 | `rpm700-throttle0` | `bmw-m52b28-held-idle-region-rpm700-throttle0` | 700 | 0.0 |
| 1 | `rpm1500-throttle0p10` | `bmw-m52b28-held-low-load-rpm1500-throttle0p10` | 1500 | 0.10 |

The first point is a warm idle-*condition* capture: the speed is held externally and
the driver throttle command is closed. `700 rpm` is source-informed by the BMW AG TIS
technical-data value of `700 +/- 50 rpm` for the warm E36 M52 B28 with consumers off,
available through this [BMW workshop-manual mirror](https://workshop-manuals.com/bmw/3_series_e36/328i_m52_sal/3_technical_data/13__fuel_system_%28m52%29/0__checking-setting_engine-emission/2_td__checking_adjusting_engine_and_exhaust_e36_m52_b_28/).
The mirror is documentary provenance, not an independently archived manufacturer
artifact and not evidence that this model presently regulates to that speed.

The second point is throttle-authored and must report its achieved load. A read-only
probe of the current production path measured approximately `256 kPa` net BMEP and
`57 N*m` at `1500 rpm` / `0.10`; these are diagnostic expectations, not frozen target
values. The clip is auditioned alongside the already accepted `1500 rpm` / `0.85`
anchor so a broken or merely gain-shifted lower-load result is easy to hear.

Throttle command is not load. This gate does not implement or claim
`LoadTargetHeldCapture`, choose a hidden throttle to meet a requested BMEP, or label
the second point with a target-load percentage.

## Shared request contract

Both points use:

| Field | Frozen value |
|---|---:|
| Fixed preparation horizon / audible start | `12.88 s` |
| Trailing complete-cycle sample | `32` cycles |
| Audible duration | `15.0 s` |
| Total duration | `27.88 s` |
| Physics / capture rates | `10000 Hz` / `10000 Hz` |
| Source / acoustic / delivery rates | `192000 Hz` / `192000 Hz` / `192000 Hz` |
| Capture block / event capacities | `200` frames / `3800` records |
| Public seed | `0xC0FFEE` |
| Operating-state event ID | `held-idle-low-load-running` |

The exact physics/capture frame boundaries are `128800` for the preparation endpoint
and audible start and `278800` for completion. The audible output is exactly `2880000`
frames; the complete 192 kHz timeline is exactly `5352960` frames.

The longer preparation horizon is deliberately limited to these low-speed captures.
At low RPM it provides substantially more discarded cycle history before selecting
the final 32 complete cycles. It remains a fixed-horizon sampling policy and makes no
stationarity or convergence claim.

Both points reuse the canonical BMW M52B28 operating profile, warm stock-accessory
condition, ambient and thermal conditions, fuel, seed, rates, running-state controls,
and accepted two-route exhaust presentation. Ignition, fuel, and dyno are enabled;
starter and limiter are disabled. No point-specific gain, EQ, noise, IR, mastering,
or monitoring correction is permitted.

Every point owns fresh engine, scenario, simulation, presentation, and publication
state. The request set is an exact BMW authoring surface, not a generic configurable
factory:

```cpp
BmwM52b28HeldIdleLowLoadRequestSetResult
make_bmw_m52b28_held_idle_low_load_request_set();

contract::ValidationReport validate_bmw_m52b28_held_idle_low_load_request_set(
    const BmwM52b28HeldIdleLowLoadRequestSet &request_set);
```

Its two exact listening selectors are:

| CLI token | Selected point key |
|---|---|
| `held-idle-region-rpm700-throttle0` | `rpm700-throttle0` |
| `held-low-load-rpm1500-throttle0p10` | `rpm1500-throttle0p10` |

Unknown selectors and option-shaped publication names fail closed. These selectors
are added to the sole current M4 listening executable; they do not create another
renderer or preserve an older API.

## Verification and listening stop

Implementation is complete only when:

1. Focused tests pin the exact set size/order, identities, binary64 RPM/throttle,
   timing, rates, seed, conditions, and common presentation. Any mutation or reorder
   fails exact validation.
2. Both full sessions retain exactly the latest 32 eligible complete cycles and
   report finite, complete torque, power, and achieved-BMEP accounting. Deterministic
   reruns must agree; tests do not invent an audio-quality score.
3. The complete test suite and both public renders pass from a clean commit.
4. Both renders launch concurrently as independent processes into new directories.
   The pair targets at most about `30 s` wall time; more than `60 s` blocks acceptance
   under the project performance contract.
5. Evidence records the clean source commit, request/scenario identity, retained
   cycles, achieved torque/BMEP/power, render time, output path, manifest hash, and
   audition-WAV hash for each point.
6. The two new audition WAVs and the accepted `1500 rpm` / `0.85` anchor are published,
   then work stops for user listening before throttle/lift/overrun work begins.

Listening decides whether the 700-rpm held clip remains clean and cycle-resolved and
whether the 1500-rpm / 0.10 clip is a believable lower-load counterpart. Acceptance
does not claim self-regulated idle, recovery from load, stall behavior, load-target
solving, throttle transients, a physical intake source, a mechanical source, BMW
torque accuracy, or any M5 offline-fidelity replacement.
