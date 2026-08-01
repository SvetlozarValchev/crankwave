# M4 fixed-horizon complete-cycle sampling

Status: implemented as the sole current M4 preparation and sampling policy; clean-run
torque evidence and fixed-sample PCM regression are published; the four-point held
listening gate remains open
Applies to: M4 held-speed and inertial-dyno preparation and operating evidence
Supersedes: adjacent non-overlapping block pass/fail convergence in production

## 1. Decision

The production preparation policy is one fixed simulation horizon followed by one
fixed trailing complete-cycle sample. It does not claim that a stochastic or
deterministically cycle-varying engine has “converged.”

The diagnostic evidence and decision are recorded in
[`M4_BMW_TORQUE_SWEEP_CONVERGENCE_FAILURE.md`](../M4_BMW_TORQUE_SWEEP_CONVERGENCE_FAILURE.md).
Across all nine canonical BMW RPM points, the trailing 32-cycle estimates at `6.44 s`
and `12.88 s` stayed inside the predeclared engineering envelope. The selected
torque-sweep and inertial-dyno fixed preparation horizon is therefore `6.44 s`; the
sample contains the latest `32` complete four-stroke cycles ending no later than that
horizon.

Earlier diagnostic prose called `6.44 s` an “initialization-deletion horizon.” More
precisely, it is the fixed preparation endpoint. The discarded initialization prefix
ends where the trailing 32-cycle sample begins, so its duration varies with RPM. No
production field will mislabel the full `6.44 s` as discarded time.

This cutover does not change the engine core, combustion variation, gas integration,
shaft dynamics, capture samples, excitation, presentation, or audio. It changes only
how a bounded preparation run is summarized and admitted.

## 2. No compatibility path

This is an incompatible replacement, not a second production mode:

- delete `ConvergenceSettling` and the adjacent-block method identity;
- delete the adjacent-block convergence observer and its tests;
- delete pressure/torque residuals, tolerances, limiting-volume fields, and the
  `preparation_not_converged` terminal path;
- delete the temporary failure accessor and both diagnostic tools after the selected
  horizon is recorded;
- do not add a legacy parser, alias, adapter, fallback, feature flag, or alternate
  runtime; and
- migrate every canonical M4 held-speed, inertial-dyno, and torque-sweep request to
  the replacement in the same production cutover.

`FixedSettling` remains because prescribed non-operating scenarios use a declared
fixed warm-up plus settling schedule. It is a different current product behavior,
not compatibility for the rejected stochastic equality gate.

## 3. Scenario contract

The replacement preparation variant is:

```text
FixedHorizonCycleSampling {
    method
    fixed_preparation_horizon_s
    trailing_complete_cycle_count
}
```

Its method ID is `fixed-horizon-trailing-complete-cycle-sample-v1`, version `1`. The
exact method descriptor is:

```text
engine-sim-offline.simulation-method-configuration.v1
method=fixed-horizon-trailing-complete-cycle-sample-v1
version=1
operation=fixed-preparation-horizon-trailing-complete-cycle-sampling
plan-method-identity=must-exactly-equal-this-descriptor-content-identity
plan-fixed-preparation-horizon-s=finite-positive-binary64-on-the-physics-frame-grid
plan-trailing-complete-cycle-count=positive-u32-M
cycle-input=owned-complete-cycle-ordinal,start-and-end-boundary-evidence-theta-rad-and-time-s,four-coherent-work-lanes,and-owned-end-boundary-pressure-array
cycle-work-domain=finite-binary64-indicated-gas-work-j,finite-positive-binary64-aggregate-loss-work-j,canonical-positive-zero-starter-work-j,and-finite-binary64-brake-work-j
cycle-brake-work-identity=brake-work-j-must-bit-equal-(indicated-gas-work-j-minus-positive-aggregate-loss-work-j)-plus-starter-work-j-in-written-order
boundary-evidence=exact-sample-has-equal-left-right-sample-index-and-positive-zero-fraction;bracketed-sample-has-left-index-less-than-right-index-and-finite-fraction-strictly-between-zero-and-one
cycle-extent=finite-start-and-end-theta-rad-and-time-s;start-time-nonnegative;end-time-and-theta-strictly-increase;end-boundary-right-sample-index-strictly-increases
cycle-chronology=after-first-input-ordinal-increments-exactly-one-and-start-boundary-evidence-theta-and-time-exactly-equal-previous-end-boundary
cycle-pressure-input=exact-plan-shape-and-identities-in-ascending-order;every-absolute-pressure-finite-and-positive
eligibility=complete-cycle-end-time-s-less-than-or-equal-to-fixed-preparation-horizon-s
window=retain-only-latest-M-eligible-complete-cycles-in-chronological-order
finalization=observe-never-publishes-a-result;evaluate-only-on-explicit-fixed-horizon-finalization
work-reduction=separate-left-to-right-binary64-sums-in-cycle-chronology-for-indicated-gas,positive-aggregate-loss,starter,and-brake-work
mean-brake-torque-nm=sum-brake-work-j-divided-by-(binary64(M)-times-binary64-four-times-std-numbers-pi-v-binary64)-in-written-order
pressure-reduction=for-each-ascending-gas-volume-id-left-to-right-binary64-sum-in-cycle-chronology-then-divide-by-binary64(M)
insufficient-window=typed-terminal-insufficient-cycles-error
malformed-input=first-typed-terminal-error;subsequent-observe-and-finalize-return-stored-error
successful-finalization=one-terminal-fixed-sample-result;subsequent-finalize-returns-stored-result-and-observe-reports-closed
stationarity-claim=none
parallel-reduction=none
binary64-execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-numbers-pi-v-binary64-under-render-determinism-envelope
external-numeric-authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
```

The descriptor includes one final LF and no CR or NUL. Its SHA-256 is frozen below
after mechanical extraction from this block:

```text
9efbb15d0ad27d3f97d75d135b642c9a7feec6610c50e7ec82523ec62808da63
```

`PreparationPolicy` becomes exactly
`variant<FixedSettling, FixedHorizonCycleSampling>`. This incompatible tagged-union
change is incorporated by the sole current canonical authorities: simulation
manifest v10, simulation input `simulation_v9`, and request identity v7. Their exact
CDDL, schema hash, roots, paths, and no-compatibility replacement are frozen in
[`M4_SIMULATION_MANIFEST_WIRE.md`](../contracts/M4_SIMULATION_MANIFEST_WIRE.md) and
[`render_manifest_simulation_v10.cddl`](../../schemas/render_manifest_simulation_v10.cddl).
The request-v7 root keeps member order `wire_schema`, `engine`, `scenario`,
`random_plan`, `provenance`. The outer `scenario.preparation` tagged union writes
members in this order:

1. `kind`;
2. `value`.

The `kind` value is `fixed_horizon_cycle_sampling`; inside `value`, members are
written in this order:

1. `method`;
2. `fixed_preparation_horizon_s`; and
3. `trailing_complete_cycle_count`.

No old preparation tag, request-v6 encoder, simulation-manifest-v9 encoder, v9 path,
or forwarding alias remains encodable after the cutover.

Validation requires:

- the exact method identity above;
- a finite positive horizon exactly representable on the physics frame grid;
- a positive cycle count whose bounded retained capacity is representable;
- for held-speed and inertial-dyno operation, `audible_start_s` exactly equals the
  fixed preparation horizon; and
- enough horizon at the declared held or initial inertial RPM for the conservative
  sufficient admission bound `(M + 1) * 120 / rpm`.

The last condition is admission protection. Runtime failure to retain `M` valid
cycles after a successfully compiled canonical request is a contract violation, not
evidence that the engine “did not converge.”

## 4. Sampling primitive

One `FixedHorizonCycleSampler` owns the bounded latest-cycle ring. It reuses the exact
complete-cycle records emitted by `OperatingCycleAccountant`; it never recomputes an
angle crossing.

Its compiled plan has exactly these fields in written order:

```text
FixedHorizonCycleSamplerPlan {
    MethodIdentity method
    uint32 trailing_complete_cycle_count
    double fixed_preparation_horizon_s
    vector<GasVolumeId> physical_gas_volume_ids
}
```

`physical_gas_volume_ids` is the nonempty, strictly increasing stable-ID inventory of
every engine gas volume whose kind is not `atmosphere`. It is compiled from the engine
once. Every observed cycle must carry the exact same IDs and order; no caller-supplied
subset, reordering, or atmosphere alias is accepted.

The sampler input owns, in order, completed-cycle ordinal; exact start and end
boundary evidence; start/end theta and time; indicated-gas, positive aggregate-loss,
starter, and brake work; and end-boundary absolute pressure for every plan volume.
The boundary, chronology, work-identity, pressure-shape, and finite/positive rules are
exactly those frozen in the method descriptor. There is no second public input shape.

A cycle is eligible when its complete end boundary is at or before the fixed
preparation horizon. The sampler keeps only the latest `M` eligible cycles. It accepts
and validates all chronological cycle inputs through the horizon so the retained
window is provably the latest one. Finalization occurs exactly once at the fixed
horizon.

Successful finalization returns:

- method identity, `M`, and fixed preparation horizon;
- the one chronological `M`-cycle evidence block;
- the last eligible completed-cycle ordinal and exact end-boundary evidence; and
- the existing coherent four work totals, cycle-mean torque breakdown, net BMEP,
  mean power, and mean boundary pressures derived from that block.

It has no residual, tolerance, limiting volume, settled flag, early-stop path, or
automatic horizon extension. Errors are limited to invalid plans, capacity failure,
malformed/nonfinite/nonphysical cycle input, insufficient complete cycles, and
nonfinite reduction.

## 5. Public result

The public evidence record has exactly these fields and order:

```text
HeldSpeedFixedHorizonSampleEvidence {
    MethodIdentity method
    uint32 trailing_complete_cycle_count
    double fixed_preparation_horizon_s
    HeldSpeedCycleBlockEvidence trailing_complete_cycles
    uint64 last_eligible_completed_cycle_ordinal_at_fixed_horizon
    OperatingPointBoundaryEvidence
        last_eligible_cycle_end_boundary_at_fixed_horizon
}
```

`HeldSpeedCycleBlockEvidence` and its per-cycle evidence keep their current exact field
order and meanings; their former word “block” means one bounded chronological sample,
not one side of a comparison. Its `completed_cycle_count` equals
`trailing_complete_cycle_count`. Its first/last ordinals cover all owned per-cycle
records in order. Its last ordinal and end boundary equal the two explicit
last-eligible attestation fields bit-for-bit.

`HeldSpeedOperatingPointResult` becomes, in order:

```text
HeldSpeedOperatingPointResult {
    Sha256Digest simulation_request_identity_v7_sha256
    HeldSpeedOperatingPointConditions conditions
    string applicability_label
    HeldSpeedFixedHorizonSampleEvidence sampling
}
```

`reported_block()` returns `sampling.trailing_complete_cycles`. Request-bound
validation independently recomputes every written work and pressure reduction, proves
the sample contains exactly `M` contiguous cycles, proves its final cycle is the last
eligible cycle at the fixed horizon, and binds the exact request-v7 digest. The
inertial-dyno result and runtime request binding rename the same field to
`simulation_request_identity_v7_sha256`; no v5-named member remains.

The result and failure vocabulary must not use “converged,” “settled,” or
“nonconverged” for this policy. A fixed sample is sufficient evidence for the bounded
M4 estimate; it is not a physical-validation or stochastic-stationarity certificate.

## 6. Canonical BMW migration

The production cutover uses:

| Request | Fixed preparation horizon | Trailing cycles | Other timing |
|---|---:|---:|---|
| Four-point held operating regression | `6.44 s` | `32` | exact points and identities are frozen in `M4_BMW_HELD_REGRESSION_MATRIX.md`; `15.0 s` audible; `21.44 s` total |
| Inertial-dyno listening | `6.44 s` | `32` | `bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2`; release remains frame `64400` |
| Nine-point torque sweep | `6.44 s` | `32` | replace the final `v1` of each existing canonical point ID with `v2`; `0.02 s` tail; total `6.46 s` |

The four-point held set is the sole current held-listening construction surface. Its
point keys, scenario IDs, CLI tokens, shared timing, and hard listening stop are
defined in
[`M4_BMW_HELD_REGRESSION_MATRIX.md`](../M4_BMW_HELD_REGRESSION_MATRIX.md). The
earlier `3.22 s` one-point held request was only a migration bridge. It was deleted,
not retained as a legacy factory or generic `held` CLI alias. Its byte-identical PCM
comparison remains historical evidence in
[`M4_FIXED_SAMPLE_AUDIO_REGRESSION.md`](../M4_FIXED_SAMPLE_AUDIO_REGRESSION.md).

The inertial and torque-sweep migrations use their current `v2` scenario identities.
There are no v1 factories. Provenance and request digests are regenerated from the
new exact contracts.

The torque-sweep evidence wire becomes
`engine-sim-offline.bmw-m52b28-torque-sweep-evidence.v2`, its canonical grammar
becomes
`engine-sim-offline.bmw-m52b28-torque-sweep-evidence-canonical-json.v2`, and its
publication becomes `bmw-m52b28-m4-torque-sweep-v2.json` plus the same-name
`.sha256` sidecar. It contains one sampled cycle range per point and removes A/B
ranges, residuals, tolerances, and limiting-volume claims. The runner remains
all-or-nothing and publishes only the complete nine-point set from a clean source
commit.

This frozen evidence-v2 grammar is historical artifact identity. Its
`simulation_request_v3_sha256` member remains unchanged; it is not a request-v3
encoder alias and cannot admit a request through the sole current request-v7 API.

The compact UTF-8 JSON has no BOM, insignificant whitespace, or final LF. Its exact
object-member order is:

1. root: `wire_schema`, `schema_version`, `source_commit`,
   `model_record_sha256`, `engine_profile_id`, `conditions`, `points`, `comparisons`,
   `warnings`, `execution`;
2. conditions: `ambient_pressure_pa_abs`, `ambient_temperature_k`,
   `relative_humidity_01`, `gas_temperature_k`, `wall_temperature_k`,
   `coolant_temperature_k`, `oil_temperature_k`, `crankcase_pressure_pa_abs`,
   `crankcase_temperature_k`, `total_displacement_m3`, `fuel_id`,
   `lower_heating_value_j_per_kg`, `stoichiometric_air_fuel_mass_ratio`,
   `accessory_configuration_id`, `accessory_configuration_sha256`,
   `initial_theta_rad`, `throttle_01`, `public_seed`, `physics_rate_numerator`,
   `physics_rate_denominator`, `sampling_method_id`, `sampling_method_version`,
   `sampling_method_configuration_sha256`, `trailing_complete_cycle_count`,
   `fixed_preparation_horizon_frame`, `tail_frame_count`;
3. each point: `scenario_id`, `simulation_request_v3_sha256`,
   `provenance_bundle_sha256`, `engine_speed_rpm`, `throttle_01`,
   `indicated_gas_torque_nm`, `aggregate_loss_torque_nm`, `starter_torque_nm`,
   `net_shaft_torque_nm`, `net_bmep_pa`, `mean_power_w`, `sample_first_cycle`,
   `sample_last_cycle`, `applicability_label`, `elapsed_ns`;
4. comparisons: `torque_at_3950_nm`, `torque_at_3950_to_280_ratio`,
   `power_at_5300_w`, `power_at_5300_to_142000_ratio`,
   `sampled_maximum_torque_nm`, `sampled_maximum_torque_rpm`,
   `sampled_maximum_torque_to_280_ratio`, `sampled_maximum_power_w`,
   `sampled_maximum_power_rpm`, `sampled_maximum_power_to_142000_ratio`,
   `gross_error_lower_ratio`, `gross_error_upper_ratio`; and
5. execution: `point_count`, `total_elapsed_ns`.

`schema_version` is the unsigned decimal JSON integer `2`; `point_count` is also an
unsigned decimal JSON integer. The existing canonical scalar grammar remains:
binary64 and other integer values use quoted fixed-width lowercase hexadecimal,
SHA-256 uses 64 lowercase hexadecimal digits, strings use canonical JSON escaping,
and warnings retain their existing ordered IDs and warning-only `0.5..1.5` rules.
The sidecar remains exactly 64 lowercase digest digits, two spaces, the v2 JSON
basename, and one LF. Each point's request-v3 field must equal both a fresh encoding
of that exact canonical request and the request-bound typed result.

## 7. Verification and removal

The implementation gate is:

1. focused sampler invariants and malformed/insufficient input tests;
2. request/result identity and reduction tests for all canonical M4 requests;
3. unchanged engine-core, gas, combustion, RNG, capture, excitation, and presentation
   tests;
4. clean all-nine torque-sweep publication and manufacturer plausibility comparison;
5. held and inertial BMW re-renders because request identities change; and
6. PCM comparison against the already accepted clips.

The contract cutover is expected to leave audible PCM unchanged because sampling is
read-only evidence over the same fixed preparation frames. Any PCM change is treated
as a regression and investigated before M4 continues.

The diagnostic result is already recorded at clean commit `ba499e5`; its historical
commit remains reproducible. Therefore the production cutover itself removes the
temporary diagnostic executable, retained-convergence-error accessor, old observer,
old method identity, old result fields, old failure vocabulary, and their tests. It
does not port diagnostic scaffolding to the replacement contract. No rejected
production path remains while the v2 evidence is generated.
