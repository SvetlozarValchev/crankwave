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
