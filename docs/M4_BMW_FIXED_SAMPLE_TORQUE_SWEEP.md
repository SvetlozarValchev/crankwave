# M4 BMW fixed-sample torque-sweep gate

Status: passed; complete nine-point evidence published atomically
Recorded: 2026-07-29

## Outcome

The current clean execution of the sole fixed-horizon production path completed all
nine canonical BMW M52B28 full-throttle points after the held-matrix model-authority
update. Every point retained the latest 32 complete four-stroke cycles at the fixed
`6.44 s` preparation horizon. The runner published one complete v2 evidence set; it
did not retry, interpolate, extend a horizon, or use a superseded convergence path.

| RPM | Net torque (N*m) | Mean power (kW) | Sample cycles |
|---:|---:|---:|---:|
| 1500 | 229.938 | 36.119 | 47–78 |
| 2500 | 327.807 | 85.820 | 101–132 |
| 3000 | 356.510 | 112.001 | 127–158 |
| 3500 | 356.374 | 130.618 | 154–185 |
| 3950 | 355.363 | 146.993 | 178–209 |
| 4500 | 352.902 | 166.301 | 208–239 |
| 5300 | 344.515 | 191.211 | 251–282 |
| 6000 | 249.194 | 156.573 | 288–319 |
| 6500 | 318.545 | 216.827 | 315–346 |

The existing warning-only manufacturer comparison reports the sampled maximum power
outside its broad `0.5..1.5` ratio. This is retained honestly. The operating profile
is a generic low-order Chen-Flynn model prediction, not a calibrated BMW torque model.

## Clean execution identity

- source commit: `071a086c0a079f3aa4e43de707b75551664b0cf2`
- source tree: `d083e805c5762c0c927e3ac8cf5dd161464e7804`
- branch: `clean-room/bmw-baseline`
- build: Release, Clang `21.1.8`, target
  `crankwave_m4_bmw_torque_sweep`
- command:
  `build-m4-fixed-sampling-clang/crankwave-m4-bmw-torque-sweep artifacts/m4-fixed-horizon-071a086-torque-sweep`
- total simulated-job wall time reported by the evidence: `25.983 s`
- per-point elapsed time: `2.874..2.914 s`; the evidence runner executes the nine
  points sequentially
- evidence file: `artifacts/m4-fixed-horizon-071a086-torque-sweep/bmw-m52b28-m4-torque-sweep-v2.json`
- evidence SHA-256:
  `42229dd7d05329dea7ffc6e0b61ac59b9556169078be6092f7a1a2771fb922b0`

The output directory is a local ignored artifact. The evidence itself embeds the
source commit, exact request-v3 digests, provenance digest, method identity, sample
ranges, conditions, results, warnings, and elapsed times.

## Non-claims

This gate proves deterministic execution, complete fixed-sample publication, and a
bounded engineering-plausibility comparison. It does not prove BMW calibration,
audio fidelity, idle/transient behavior, or user listening acceptance. Those remain
separate M4 gates.
