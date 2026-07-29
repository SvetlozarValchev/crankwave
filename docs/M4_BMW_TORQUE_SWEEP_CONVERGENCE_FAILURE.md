# M4 BMW torque-sweep frozen-v1 convergence failure

Status: diagnostic gate; no torque-sweep evidence record was published
Recorded: 2026-07-29

## Outcome

The first clean execution of the frozen nine-point request set failed closed at the
second point, `2500 rpm`. The preceding `1500 rpm` session completed, but the runner
correctly discarded the partial set and created no output directory. No point was
retried, interpolated, or executed with altered convergence settings within that
clean run.

An uncommitted integration probe had exposed the same exact terminal context before
the runner checkpoint was committed. It was not accepted as evidence; the clean,
build-stamped CLI execution reproduced the failure bit-for-bit.

This is not evidence that the sweep runner is broken. It is evidence that the frozen
`6.44 s`, two-by-16-cycle, `1500 Pa` pressure criterion does not admit the current
canonical model at every requested point.

## Clean execution identity

- source commit: `97ef33af8f4e3391500dae838fd483fe1e0ed654`
- source tree: `d8f47171ae31291cd6b3f0a1f2e1ee6abe5d7a37`
- branch: `clean-room/bmw-baseline`
- build: Release, Clang `21.1.8`, target
  `engine_sim_offline_m4_bmw_torque_sweep`
- command:
  `./build-m4-listening-clang/engine-sim-offline-m4-bmw-torque-sweep artifacts/bmw-m52b28-m4-torque-sweep-v1-attempt-1`
- observed wall time: `5.92 s`
- publication check: command status `1`; the requested output directory did not exist
  after failure

The build-owned source stamp was regenerated only after the commit and tree were
clean. The failed command therefore exercised the real CLI, canonical request factory,
request-v2 identity encoder, bounded capture session, result binding, convergence
observer, and all-or-nothing publication gate—not a caller-injected test stamp. The
publisher itself was never entered because a complete nine-point record did not exist.

## Exact failure

```text
preparation-not-converged:
scenario=bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v1;
fixed-cutoff convergence failed;
error-code=5;
retained-cycle-count=32;
required-cycle-count=32;
block-a-first-ordinal=101;
block-a-last-ordinal=116;
block-b-first-ordinal=117;
block-b-last-ordinal=132;
torque-residual-binary64=4600432227973450752;
torque-tolerance-binary64=4604930618986332160;
pressure-residual-binary64=4666284760132184832;
pressure-tolerance-binary64=4654311885213007872;
limiting-gas-volume-id=4
```

Those exact binary64 values decode to:

- torque residual: `0.37528913618660908 N*m`, within the `0.75 N*m` tolerance;
- pressure residual: `9202.532607962843 Pa`, outside the `1500 Pa` tolerance; and
- limiting volume: stable gas-volume ID `4`, which the canonical topology maps to
  `cylinder.1`.

The two required 16-cycle blocks were present, so this was neither an insufficient-
cycle failure nor a torque failure.

## What this result does not establish

The terminal public failure retains the two block ranges, residuals, tolerances, and
limiting volume, but not the underlying per-cycle/per-volume pressure series. One
final adjacent-block comparison therefore cannot distinguish:

1. a chamber state that is still drifting at `6.44 s`; from
2. a settled deterministic cycle-varying state whose adjacent 16-cycle pressure means
   naturally differ by more than `1500 Pa`.

Raising the tolerance, extending the cutoff, increasing the block size, or changing
combustion behavior before making that distinction would be outcome-driven tuning.
The next checkpoint is diagnostic-only evidence using the already computed cycle and
pressure observables. Any resulting change must be frozen explicitly under a new
request identity before the complete sweep is executed again.

## Frozen first diagnostic

The first diagnostic is intentionally smaller than a statistical calibration study.
It does not change production simulation math, the frozen request, block size, or
tolerances, and it does not add a second convergence method.

One read-only accessor will preserve the existing
`AdjacentCycleBlockConvergenceError` after the held runtime has already terminalized.
It exposes the complete two-block evidence that the runtime currently reduces to a
`FailureContext`; it cannot affect convergence or resume a failed session. A focused
test must prove that the public failure remains exactly unchanged.

A reference-only diagnostic then executes the canonical `2500 rpm` engine and state at
three fixed convergence cutoffs:

| Cutoff | Total horizon | Purpose |
|---:|---:|---|
| `6.44 s` (`64400` frames) | `6.46 s` (`64600` frames) | bit-exact reproduction of the frozen failure |
| `12.88 s` (`128800` frames) | `12.90 s` (`129000` frames) | first later deterministic window |
| `25.76 s` (`257600` frames) | `25.78 s` (`257800` frames) | second later deterministic window |

The `6.44 s` case is the unchanged canonical request. Each later diagnostic request is
an in-memory copy that changes only its scenario ID, maximum preparation duration,
audible start, and total horizon; audible duration remains `0.02 s`. Ambient, thermal
state, engine, operating state, throttle, RPM, seed, rates, convergence method,
two-by-16-cycle block size, and both tolerances remain bit-identical. Each diagnostic
scenario receives its own request-v2 identity. Every emitted capture block is still
validated.

For each cutoff the diagnostic records, from either the typed successful result or the
typed terminal convergence error:

- settled/nonconverged status and exact binary64 residuals and tolerances;
- both completed-cycle ordinal ranges and the limiting gas-volume ID;
- every physical volume's two block-mean boundary pressures; and
- the 32 retained per-cycle end-boundary pressures for `cylinder.1`, plus the four
  retained work lanes needed to audit the torque reduction.

The `6.44 s` diagnostic is admissible only if its public `FailureContext` and retained
typed evidence reproduce the recorded residual bits, block ordinals, and limiting
volume exactly. Later results are descriptive deterministic windows, not independent
physical trials. No automatic significance test or tolerance change follows from
three windows:

- consistently moving late-window means with falling residuals supports continued
  settling;
- stable late-window means with repeatedly excessive adjacent-block residuals supports
  a deterministic cycle-variation floor for `N=16`; and
- any mixed pattern remains inconclusive and causes a longer diagnostic, not tuning.

The diagnostic executable and accessor are temporary investigation scaffolding. Once
the result is recorded and a model decision is frozen, they are removed unless they
prove to be a small, generally useful failure-observability seam.

## First diagnostic result

The frozen diagnostic was executed from clean source commit
`8588945b3ae8df1a58619dd6aadee98a09d90d70` (tree
`d5d9b8136e3a531ace8a1ed2e7ed858b93b98eee`) with the Release Clang `21.1.8`
build. The command was:

```text
./build-m4-listening-clang/engine-sim-offline-m4-bmw-held-settling-diagnostic
```

The complete deterministic report is reproducible from that command. It contains all
physical-volume means and all 32 retained `cylinder.1` pressure and four-work-lane
observations for every window. The decision-bearing subset is:

| Cutoff | Request-v2 SHA-256 | Torque residual | Pressure residual | Cylinder 1 A/B mean pressure | Cycle ranges |
|---:|---|---:|---:|---:|---|
| `6.44 s` | `d86dd9f6b2465736f62c9d5d00ffb2106f872208fa29eef0e71080bf3cb35409` | `0.37528913618660908 N*m` | `9202.532607962843 Pa` | `1403298.1530081446 / 1412500.6856161074 Pa` | `101..116 / 117..132` |
| `12.88 s` | `29868f9537de0fc6e644212f50694f19e184433c4ede7bf4a67562792ea2a9dd` | `1.0741284191178124 N*m` | `12090.370132419979 Pa` | `1411814.1379205545 / 1399723.7677881345 Pa` | `235..250 / 251..266` |
| `25.76 s` | `bfa85894f13380038086340cfe6d123c39fbfdd6cceee2f5a39806debb30866a` | `0.16214885967116288 N*m` | `1557.3257954858709 Pa` | `1409620.8702170989 / 1408063.544421613 Pa` | `503..518 / 519..534` |

Every window retained the required cycles and failed the unchanged `1500 Pa` pressure
comparison with `cylinder.1` as the limiting volume. The first window reproduced the
previous public failure bit-for-bit. Torque independently passed, failed, then passed.

This supports a deterministic cycle-variation floor, not continued settling. The
signed cylinder-pressure block difference reverses from `+9202.532607962843 Pa` to
`-12090.370132419979 Pa` and then `-1557.3257954858709 Pa`; meanwhile the three
combined 32-cycle centers are `1407899.419312126`, `1405768.9528543446`, and
`1408842.207319356 Pa`, a total span of only `0.21834790514975883%`. Extending the
cutoff therefore selects a different deterministic PCG-driven cycle sample rather
than monotonically reducing a slow-state transient. The late near-pass is not a
reason to select `25.76 s`, and the existing absolute tolerance will not be widened to
make this run pass.

The v1 adjacent-block equality check is rejected as the torque sweep's preparation
admission rule because it asks a deliberately cycle-varying signal to become
effectively periodic. Simulation, combustion, and audio behavior remain unchanged.
The next bounded diagnostic will evaluate all nine RPMs at predeclared `6.44 s` and
`12.88 s` cutoffs using a fixed trailing 32-complete-cycle estimate. Its purpose is to
choose an initialization-deletion horizon, not to hunt for a passing adjacent pair.
If that cross-RPM evidence is adequate, v2 will report honest fixed-horizon,
fixed-sample evidence and will not call the result “converged.”

## Frozen cross-RPM fixed-sample diagnostic

Before the v2 preparation policy is written, one bounded diagnostic will test whether
`6.44 s` is an adequate initialization-deletion horizon across the complete frozen
RPM set. It changes no engine, simulation, audio, convergence, or publication code.

Each of the nine canonical RPM points executes from fresh state at both `6.44 s` and
`12.88 s`. The first execution is the exact canonical v1 request. The later in-memory
copy changes only its scenario ID, maximum preparation duration, audible start, and
total horizon (`12.90 s`); audible duration remains `0.02 s`. All other request fields
and the public seed remain bit-identical. Each request has its own request-v2 identity,
and every capture block is validated. At most nine independent workers run—one per
RPM—and each worker executes its earlier window before its later window. Results are
reported in ascending canonical RPM order regardless of completion order.

Whether the old v1 equality gate happens to pass is recorded but does not select a
window. From its typed success or typed terminal evidence, each execution combines
the retained adjacent blocks into the same fixed trailing sample of `M = 32` complete
cycles. Chronological binary64 reductions report:

- totals for indicated-gas, positive aggregate-loss, starter, and brake work;
- each per-cycle brake-torque sample as that cycle's brake work divided by `4*pi`,
  and its chronological 32-cycle mean;
- each per-cycle end-boundary pressure and chronological 32-cycle mean for every
  physical gas volume; and
- the deterministic within-window RMS dispersion of the torque and pressure samples.

For a scalar `q`, the descriptive between-horizon check is frozen before execution:

```text
mean(q) = chronological_sum(q[0..31]) / 32
rms_dispersion(q) = sqrt(
    chronological_sum((q[i] - mean(q))^2, i=0..31) / 32)
delta = abs(mean_later(q) - mean_earlier(q))
envelope = existing_absolute_floor(q)
         + max(rms_dispersion_earlier(q), rms_dispersion_later(q))
```

The existing absolute floor is `0.75 N*m` for cycle brake torque and `1500 Pa` for
each volume pressure. Those underlying v1 floor constants remain numerically
unchanged; this diagnostic envelope explicitly adds one within-window RMS scale. The
coefficient on RMS dispersion is exactly unity: the check asks whether a
separated-window mean shift exceeds that stated sum. It was not fitted to a residual
or selected as a statistical confidence level. The model already declares nonzero
deterministic per-ignition variation, so ignoring that scale would repeat the v1
category error. This is a deterministic engineering diagnostic, not an independence
assumption, probability statement, hypothesis test, or proof of stationarity.

A point supports the `6.44 s` initialization horizon only if its torque delta and
every per-volume pressure delta are within their respective envelopes. If a point is
outside, only that preidentified point is extended to the already declared `25.76 s`
cutoff and the same `12.88 s` versus `25.76 s` comparison is recorded. There is no
retry with altered constants. The outcomes are exact:

- if all nine `6.44 s` versus `12.88 s` comparisons are inside, v2 may use one global
  `6.44 s` deletion horizon;
- otherwise, if every required `12.88 s` versus `25.76 s` extension is inside, v2 may
  use one global `12.88 s` deletion horizon; and
- if any required extension remains outside, the diagnostic is inconclusive and v2
  is not frozen.

Any admitted v2 uses the selected horizon and a trailing 32-complete-cycle sample,
with honest fixed-sample evidence and no convergence claim. It replaces the rejected
v1 production policy rather than creating a parallel legacy path.

The diagnostic writes no repository artifact. Its deterministic stdout report and
the decision-bearing subset recorded here are sufficient; the final torque sweep
remains the only atomic published evidence set.

## Cross-RPM result

The diagnostic was executed from clean source commit
`ba499e5b15ce8e7dcc82082ff4547e4e627bda9f` (tree
`aa88dd19ba4bfb567ce8cb3124b5e8ec4e892085`) with the Release Clang `21.1.8`
build. The command was:

```text
./build-m4-listening-clang/engine-sim-offline-m4-bmw-held-settling-diagnostic
```

It completed all 18 required fresh sessions in `9.62 s` wall time with `25696 KiB`
maximum resident memory. The nine RPM workers ran concurrently; the two horizons for
each RPM remained sequential. The complete deterministic stdout SHA-256 was
`81f2a2d32034297a2144cab4675edd033c562f059e84a58373bd8f4ad926c67c`, matching
two earlier byte-identical integration executions. Raw pressure samples were retained
and reduced inside the diagnostic; stdout kept the exact per-volume mean/RMS and
comparison inputs rather than dumping all 12096 pressure scalars.

The decision-bearing result is:

| RPM | Old v1 gate at 6.44 / 12.88 s | Torque mean at 6.44 / 12.88 s (N*m) | Torque delta / envelope (N*m) | Limiting pressure volume | Pressure delta / envelope (Pa) | Outcome |
|---:|---|---:|---:|---:|---:|---|
| `1500` | pass / fail | `229.93815282491784 / 231.02453531987294` | `1.0863824949551031 / 7.3155433870172288` | `16` | `511.55127497692592 / 6529.457027094737` | supports `6.44 s` |
| `2500` | fail / fail | `327.80729774369189 / 328.10876683208789` | `0.30146908839600428 / 4.331451429309439` | `16` | `602.55639341729693 / 7025.4576908850559` | supports `6.44 s` |
| `3000` | fail / fail | `356.51031224965595 / 356.51053338732572` | `0.00022113766976872284 / 2.3682761269262635` | `16` | `46.977475725347176 / 4628.1003740610431` | supports `6.44 s` |
| `3500` | fail / fail | `356.37379260153864 / 356.51384033654307` | `0.1400477350044298 / 2.6428375166043896` | `16` | `628.52818835293874 / 15121.493433344931` | supports `6.44 s` |
| `3950` | fail / fail | `355.36253237532242 / 355.41341647503981` | `0.050884099717393383 / 2.5631668463321473` | `4` | `4303.7054748837836 / 19060.41334930437` | supports `6.44 s` |
| `4500` | fail / fail | `352.90231326165895 / 351.6447344354072` | `1.2575788262517449 / 2.6023536551337467` | `4` | `4081.9996920526028 / 17505.12606911379` | supports `6.44 s` |
| `5300` | fail / fail | `344.51526148362086 / 344.32642839555604` | `0.18883308806482546 / 2.5443121307968126` | `4` | `2259.1539011178538 / 17995.500828540477` | supports `6.44 s` |
| `6000` | fail / fail | `249.19417842936389 / 249.16321844356031` | `0.030959985803576728 / 2.0767137536802842` | `4` | `3970.1567581326235 / 16998.271554843705` | supports `6.44 s` |
| `6500` | fail / fail | `318.5450097785635 / 318.51389271646934` | `0.03111706209415388 / 3.0132459629188535` | `4` | `3945.0154495954048 / 17426.917970228649` | supports `6.44 s` |

Here “limiting” means the physical volume with the largest observed
`delta / envelope` ratio, not the old v1 L-infinity residual. Every torque and
per-volume pressure comparison was inside its predeclared envelope. No point was
extended to `25.76 s`.

The frozen decision rule therefore selects one global `6.44 s`
initialization-deletion horizon and a trailing 32-complete-cycle sample. The result
does not certify the modeled torque values or claim statistical convergence; those
values still face the separately frozen manufacturer plausibility comparison. It
only establishes that the later fixed samples do not show a gross state shift beyond
the model's own cycle-variation scale. The old adjacent-block pass/fail outcome—one
early pass followed by 17 failures—is retained as further evidence that it selected
lucky deterministic windows rather than a better-prepared state.
