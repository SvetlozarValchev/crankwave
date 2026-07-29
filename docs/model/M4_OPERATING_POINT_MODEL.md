# M4 operating-point model

Status: normative implementation companion, implemented one checked subsection at a
time

Applies to: M4 held-speed and inertial-dyno BMW M52B28 operation

Date: 2026-07-28

## 1. Purpose and claim boundary

M4 adds virtual-test-cell operation around the user-accepted M3 BMW gas and audio core.
It does not silently change the frozen `bmw-m52b28-legacy-low-order-v1` profile. That
profile remains an incomplete prescribed-trajectory parity model and must continue to
fail held-speed admission.

A distinct M4 operating profile may reuse accepted M3 mechanism, valvetrain, gas,
ignition, combustion, heat-transfer, excitation, and presentation methods. It must
replace the M3 fixed crank-friction term with the complete aggregate loss closure
defined below, classify the disabled/disengaged starter separately, and integrate
complete cycles. “Complete” means every declared shaft-torque category is represented.
It does not mean that generic loss coefficients are BMW measurements or that the
result is independently validated.

The reusable typed portion is `LowOrderEngineCoreV1`. The M3-only
`LegacyFixedCrankLossV1` is a sibling in `LegacyLowOrderV1Profile`, not part of that
core; an M4 profile must compose the core with its own aggregate-loss, accessory, and
starter contract rather than copying or overriding the legacy loss.

Runtime composition follows the same rule: the reusable gas solver produces
indicated-gas torque only. The M3 adapter alone adds its fixed crank loss. M4 consumes
the indicated result and applies the complete-cycle aggregate closure below; there is
no switch inside the gas solver and no path on which both losses can be active.

The reusable execution seam is `LowOrderEngineCoreV1Runtime`, not either component
session. It compiles the scenario schedule once and exposes only a transactional
mechanics-plus-gas post-step view. Both prescribed sweeps and held speed use this same
runtime. The captured-exhaust compiler also receives this explicit core rather than
selecting an executable profile. Profile-specific loss accounting observes the core
view downstream. Capture clocks, block partitioning, and event-journal capacity are
transport policy outside the reusable physics core.

The first M4 torque curve is labelled:

> generic Chen–Flynn low-order BMW model prediction

It is not labelled BMW truth, measured brake torque, SAE net power, or independent
validation.

### 1.1 Frozen production type contract

`M4` remains a roadmap label only. The production C++ alternative, wire tag,
provenance root, and first BMW profile ID are exactly:

| Role | Frozen identity |
|---|---|
| Authored C++ alternative | `AuthoredLowOrderOperatingPointV1Profile` |
| Resolved C++ alternative | `LowOrderOperatingPointV1Profile` |
| Wire kind | `low_order_operating_point_v1` |
| Provenance root | `engine.physics.low-order-operating-point-v1` |
| BMW profile ID | `bmw-m52b28-low-order-operating-point-v1` |

The authored record has the following exact member structure and order:

```text
AuthoredChenFlynnCycleMeanAggregateLossV1 {
  AuthoredValue<double> constant_fmep_bar
  AuthoredValue<double> peak_pressure_coefficient
  AuthoredValue<double> mean_piston_speed_coefficient_bar_s_per_m
  AuthoredValue<double> mean_piston_speed_squared_coefficient_bar_s2_per_m2
  AuthoredValue<double> required_oil_temperature_k
  AuthoredValue<TorqueTermMask> included_terms
}

AuthoredAccessoryConfigurationIdentityV1 {
  AuthoredValue<std::string> configuration_id
  AuthoredValue<Sha256Digest> content_sha256
}

AuthoredMechanicallyDisengagedStarterV1 {
  AuthoredValue<bool> mechanically_disengaged
  AuthoredValue<TorqueTermMask> included_terms
}

AuthoredLowOrderOperatingPointV1Profile {
  AuthoredLowOrderEngineCoreV1 core
  AuthoredChenFlynnCycleMeanAggregateLossV1 aggregate_loss
  AuthoredAccessoryConfigurationIdentityV1 accessory_configuration
  AuthoredMechanicallyDisengagedStarterV1 starter
  AuthoredValue<MethodSelection> cycle_quadrature
}
```

The resolved records have the same member names and order, drop the `Authored`
prefix, use `ResolvedValue` for scalar leaves, and resolve `cycle_quadrature` to a
`ResolvedValue<MethodIdentity> cycle_quadrature`. The alternative contains no optional
legacy loss and no switch between M3 and operating behavior.

The exact new method IDs are:

| Owner | Method ID | Version |
|---|---|---:|
| `EngineSpec::methods.losses` | `chen-flynn-cycle-mean-aggregate-loss-v1` | 1 |
| profile `cycle_quadrature` | `four-stroke-piecewise-linear-cycle-quadrature-v1` | 1 |
| `ConvergenceSettling::method` | `adjacent-nonoverlapping-cycle-block-mean-v1` | 1 |

The implementation-owned canonical descriptor and configuration SHA-256 for a method
are pinned when that implementation exists and is admitted. A literature PDF hash is
evidence identity, never a `MethodIdentity::configuration_sha256`.

The implemented cycle-accounting and settling authorities are:

| Method | Configuration SHA-256 |
|---|---|
| `four-stroke-piecewise-linear-cycle-quadrature-v1` | `57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac` |
| `chen-flynn-cycle-mean-aggregate-loss-v1` | `6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489` |
| `adjacent-nonoverlapping-cycle-block-mean-v1` | `b1a1ad37088ceb2a88dbb5db4bb385067fefa6b244b091deeebe5bf38acac406` |

These are SHA-256 digests of the canonical LF descriptors exposed by the production
implementations. Exact BMW profile validation admits the two engine-owned
cycle-accounting identities, while scenario and held-result validation admit the
scenario-owned convergence identity. Each comparison includes ID, version, and
configuration digest, not merely ID and version. The bounded convergence observer is
composed into the held-speed executor as the operating policy of the sole low-order
capture session.

For the first BMW profile, the binary64 values are exact:

| Field | Decimal | Binary64 bits |
|---|---:|---|
| `constant_fmep_bar` | `0.4` | `0x3fd999999999999a` |
| `peak_pressure_coefficient` | `0.005` | `0x3f747ae147ae147b` |
| `mean_piston_speed_coefficient_bar_s_per_m` | `0.09` | `0x3fb70a3d70a3d70a` |
| `mean_piston_speed_squared_coefficient_bar_s2_per_m2` | `0.0009` | `0x3f4d7dbf487fcb92` |
| `required_oil_temperature_k` | `363.15` | `0x4076b26666666666` |

All coefficients are finite, nonnegative with canonical positive zero, and at least
one is positive. Negative zero is rejected. The first BMW profile admits only this
coefficient tuple. The required oil temperature is an immutable applicability
condition for the whole preparation and capture, not merely an initial value; no oil
state evolves in this profile.

`aggregate_loss.included_terms` is exactly
`friction_pump_and_accessory_torque_term_mask()` (`0x7e`).
`starter.mechanically_disengaged` is exactly `true` and
`starter.included_terms` is exactly the starter bit (`0x80`). The accounting compiler
must prove that indicated gas (`0x01`), aggregate loss (`0x7e`), and starter (`0x80`)
are pairwise disjoint and their union is the complete known mask (`0xff`).

The first BMW profile accessory authority is exactly:

| Role | Frozen identity |
|---|---|
| Configuration ID | `bmw-m52b28-warm-stock-accessories-v1` |
| Production descriptor | `data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json` |
| Descriptor serialization | UTF-8 JSON, LF line endings, exactly one final LF |
| Descriptor content SHA-256 | `ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97` |

The descriptor bytes declare the modeled warm positive-speed held-running inventory
and state, repeat the exact coefficient tuple and `363.15 K` applicability condition
with their binary64 identities, and freeze the `0x7e` aggregate, separate disengaged
starter `0x80`, and complete `0xff` accounting partition. They explicitly classify
the loss model as a generic prior rather than a BMW measurement and prohibit
component-loss inference from the aggregate.

`accessory_configuration.configuration_id` and
`accessory_configuration.content_sha256` must equal the ID and digest above. The
matching provenance evidence source carries the same descriptor digest. The exact BMW
validator admits the descriptor and tuple as one pair: changing the descriptor bytes,
accessory identity, or any loss value requires a new reviewed profile admission.
Merely swapping an ID or reusing coefficients under an unadmitted accessory digest is
invalid.

This v1 profile requires all cylinder strokes to be bit-identical. Cylinder
displacements are visited in ascending stable `CylinderId` order; their stable sum
must bit-equal `EngineSpec::total_displacement_m3`, which is the total-displacement
authority used for work and BMEP. The cycle reference angle is not a duplicate field:
it is exactly `core.mechanism.crank.crank_tdc_reference_rad`.

The exact torque capability is:

```text
instantaneous_net_shaft =
  available, complete-within-the-admitted-causal-mean-value-method,
  included_terms=0xff, omitted_terms=0
cycle_mean_net_shaft =
  available, complete, included_terms=0xff, omitted_terms=0
equivalent_inertia_available = true
```

The instantaneous capability is not a physical component-friction waveform. It is
available only through the admitted causal one-cycle-lagged aggregate-loss closure;
held results continue to expose the same-cycle aggregate loss and complete shaft
output at completed-cycle/block resolution.

The profile admits `HeldSpeed` and `InertialDyno`, both with
`ConvergenceSettling`, finite positive running speed, the exact oil condition above,
and a fired running state at every journal point: ignition, fuel, and dyno enabled;
starter and limiter disabled. `HeldSpeed` constrains speed during capture;
`InertialDyno` releases the converged state into the admitted crank-dynamics method.
The scenario contains no accessory selector; the profile owns that condition. Reused
core values must be resolved afresh under the new provenance root—an M3 object and
its `ResolutionRecord`s may not be shallow-copied and relabelled.

The scenario's conventional stoichiometric mass-AFR metadata is not the core's
dimensionless molecular ratio. For this inherited pseudo-gas model it is derived in
the exact written order
`(molecular_afr / 0.25) * (0.02897 / fuel_molecular_mass_kg_per_mol)`.
The first BMW profile therefore declares `14.484999999999998`, not `12.5`. The
runtime bit-binds the reported operating condition to that conversion even though the
legacy gas solver consumes the molecular representation directly.

`ConvergenceSettling` gains a leading
`ResolvedValue<MethodIdentity> method` member. Convergence remains scenario/test-cell
policy and is not duplicated in the engine profile.

This variant was added to the executable-profile union atomically with its authored
and resolved validation, method policy, topology/root mapping, randomness access,
final manifest wire, and request-identity wire. The low-order capture runtime selects
exactly one profile policy: the M3 fixed-crank accountant or this M4
complete-cycle/convergence policy, with the latter selecting constrained held capture
or released inertial capture from the scenario. Both consume the same transactional
core step inside the same block loop; neither policy can instantiate, evaluate, or
fall back to the other. A cutoff failure terminalizes the session before the
containing capture block reaches its consumer. Successful M4 completion alone retains
the request-bound typed operating result.

The canonical BMW profile factory constructs this profile directly from the reusable
low-order core under a fresh operating provenance root. Its exact validator pins the
coefficient bits, oil and accessory conditions, torque partition, implemented method
identities, capability statement, and complete provenance bundle. The listening
request factory separately rebuilds that engine and its held-speed scenario through one
fresh builder before finishing one shared ledger; it never copies the completed profile
or relabels an M3 request.

### 1.2 First canonical held-speed listening request

`make_bmw_m52b28_held_speed_listening_request()` has no calibration arguments. Its
exact first listening point is:

| Field | Canonical value |
|---|---:|
| Scenario ID | `bmw-m52b28-held-3000rpm-listening-v1` |
| Engine speed | `3000 rpm` |
| Throttle | `0.85` |
| Minimum warm-up / settling | `0 s` / `0 s` |
| Maximum preparation / audible start | `3.22 s` |
| Comparison cycles per block | `16` |
| Cycle-mean torque tolerance | `0.25 N*m` |
| Boundary-pressure tolerance | `1500 Pa` |
| Audible duration / total duration | `15.0 s` / `18.22 s` |
| Physics / capture rates | `10000 Hz` / `10000 Hz` |
| Source / acoustic / delivery rates | `192000 Hz` / `192000 Hz` / `192000 Hz` |
| Capture block / event capacities | `200` frames / `3800` records |
| Public seed | `0xC0FFEE` |

The original `0.22 s`, two-cycle-block candidate was run as a diagnostic on these
same engine, ambient, thermal, fuel, held-control, and numeric-envelope conditions. At
its cutoff the adjacent blocks differed by `146.38408799394455 N*m` and
`285912.14626085013 Pa`. Treating that state as converged would therefore have required
meaningless placeholder-scale tolerances, so it was rejected rather than frozen.

The selected `3.22 s`, 16-cycle-block calibration retained the latest adjacent
complete-cycle blocks and measured residuals of `0.14219052207965888 N*m` and
`870.20266385539435 Pa`. The round admitted bounds (`0.25 N*m`, `1500 Pa`) leave finite
margin around that deterministic reference observation while remaining materially
tighter than the rejected early transient. The request test executes the core through
the fixed cutoff and requires a request-bound settled result; it does not merely inspect
the authored constants.

The resulting integer horizons are `32200` preparation and `182200` total physics/
capture frames, plus `2880000` audible and `3498240` total 192 kHz frames. At the
declared 200-frame capture block capacity, the complete run is 911 blocks, of which 750
cover the audible interval.

The reused `legacy_low_order_v1` method configuration retains the admitted M3 content
identity `435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c`.
That authority is the frozen
`aa1c9a1553b301300258e9fc1de16e6e47c2012c:docs/model/M3_PARITY_MODEL.md`
Git blob (`760ddd8e436704ed707623a6dad0e6556606d08f`), not a newly invented
compiled-method descriptor.

This operating-regression profile also retains accepted M3 fixture-derived combustion
seeds, header lengths, and reference-excitation values. Their evidence remains
`local_evaluation_only`: the profile has no runtime fixture dependency, but its
provenance is not yet admissible for a distributable product package. Those authorities
must be sourced or re-authored before the M7 package gate.

### 1.3 First inertial-dyno listening request

The inertial pull is a new physical operating mode, not the M3 prescribed RPM lane and
not a reproduction of engine-sim's GUI dyno sweep. The original engine-sim sweep moves
an ideal speed constraint at an authored `500 rpm/s`; it measures constraint reaction
while the constraint owns crank speed. `InertialDyno` instead lets the modeled shaft
torque accelerate one declared crank-referred inertia against one declared passive
load.

The first request freezes these conditions before listening:

| Field | Canonical value |
|---|---:|
| Scenario ID | `bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v1` |
| Initial / listening-target speed | `1500 rpm` / `6500 rpm` |
| Throttle | `0.85` |
| Maximum preparation / audible start | `6.44 s` |
| Audible duration / total duration | `15.0 s` / `21.44 s` |
| Total crank-referred equivalent inertia | `6.5 kg*m^2` |
| Passive brake curve | `40 N*m` from `1000` through `7500 rpm` |
| Crank-dynamics method | `rigid-crank-zoh-work-energy-v1`, version 1 |
| Passive-brake method | `piecewise-linear-positive-speed-passive-brake-v1`, version 1 |
| Physics / capture rates | `10000 Hz` / `10000 Hz` |
| Source / acoustic / delivery rates | `192000 Hz` / `192000 Hz` / `192000 Hz` |
| Capture block / event capacities | `200` frames / `3800` records |
| Public seed | `0xC0FFEE` |

`6.5 kg*m^2` is a declared test-cell total, including all rigidly crank-referred
engine, coupling, and flywheel inertia. It is not a measured BMW value and must not be
added to a second hidden engine inertia. A future component inventory may derive this
total as `J_engine + sum(J_i * ratio_i^2)`; v1 owns only the resolved total and its
provenance.

The listening target is evidence, not a prescribed trajectory. The fixed-horizon run
records the first frame at or above `6500 rpm`, if any, and continues to the declared
duration. Missing the target does not get disguised by resampling or clamping; the
canonical publisher fails its listening gate and reports the final speed. The brake
curve uses an admitted piecewise-linear method, is a nonnegative resisting magnitude,
and must cover every evaluated positive speed. Extrapolation, reverse rotation, and a
zero-speed stick model are absent from v1.

Preparation holds exactly `1500 rpm` using a test-cell actuator while the passive
brake remains active. Existing adjacent-block torque and phase-aligned pressure
convergence is evaluated at the fixed `6.44 s` cutoff. The hold actuator becomes zero
at that exact physics-frame boundary; crank angle, gas state, flame state, pressure
history, randomness, and the latest completed aggregate-loss state continue without a
reset.

The first dynamics method is a deterministic rigid one-degree-of-freedom mean-value
model. With positive running direction, the committed state from step `n` supplies:

```text
tau_engine_n = tau_indicated_n + tau_applied_loss_n + tau_starter_n
tau_brake_n  = -B(omega_n)
tau_total_n  = tau_engine_n + tau_brake_n
alpha_n      = tau_total_n / J_equivalent

omega_n1 = omega_n + alpha_n * dt
theta_n1 = theta_n + omega_n * dt + 0.5 * alpha_n * dt^2
```

Torque is zero-order held over that interval. This update preserves the constant-step
work/kinetic-energy identity
`tau_total_n * (theta_n1 - theta_n) = 0.5 * J * (omega_n1^2 - omega_n^2)`;
the runtime retains the maximum absolute binary64 residual. A step that would reach
zero speed resolves the within-step stop and returns a typed stall rather than
silently reversing. A step outside the compiled brake domain fails rather than
extrapolating.

The gas solver produces the post-step indicated torque used by the following dynamics
interval, matching the existing causal mechanics-then-gas transaction. Chen–Flynn
cannot supply same-cycle instantaneous loss because its peak pressure is known only
after that cycle closes. When completed cycle `k` closes, its mean loss torque becomes
a constant mean-value dynamics input for cycle `k+1`. Variable-speed cycle mean RPM is
derived from the represented cycle duration as `120 / duration_s`. The preparation
must provide the first complete lagged-loss state; there is no guessed startup value.

This causal one-cycle lag is an explicit dynamics approximation. It is complete within
the named low-order crank-dynamics method, but it is not a physical instantaneous
friction waveform and does not expose component friction. Public evidence keeps the
applied lagged loss, newly estimated completed-cycle loss, indicated torque, passive
brake, and total dynamics torque distinct.

The canonical descriptor for `rigid-crank-zoh-work-energy-v1` includes the
`cycle-k estimate -> cycle-k+1 application` causality rule; accepting the same ID with
a same-cycle, guessed-first-cycle, or interpolated loss law is forbidden. The brake
method uses binary64 linear interpolation between ascending angular-speed points,
returns the exact endpoint value at either endpoint, and has no extrapolation rule.

## 2. Indexed four-stroke torque quadrature

The internal method name is
`four-stroke-piecewise-linear-cycle-quadrature-v1`, version 1.

For a declared finite reference angle `theta_ref`, represented boundaries are derived
independently:

```text
theta_boundary(n) = theta_ref + binary64(n) * (4*pi)
```

The implementation retains a checked signed integer `n`; it never finds later
boundaries by repeatedly adding `4*pi`. Global crank angle and time must advance
strictly. A call that reaches two represented boundaries fails with a typed error
because one call can return at most one completed cycle.

Inputs have post-step semantics. Between adjacent samples:

- time is linear in unwrapped crank angle;
- each torque lane actually supplied to this generic primitive is linear in unwrapped
  crank angle;
- work for each term and their stable ordered sum uses trapezoidal
  torque-versus-angle quadrature;
- a crossed boundary is evaluated by the same linear interpolation;
- the bracket sample identities and interpolation fraction remain evidence;
- the initial partial cycle is discarded.

Upstream physical integration must split known discontinuities at their exact event
times. Quadrature cannot reconstruct a discontinuity that the physical solver smeared
inside one supplied step.

For the values supplied to this generic primitive:

```text
W_sum = integral_over_represented_cycle(tau_sum dtheta)
tau_sum_mean = W_sum / (4*pi)
mean_effective_pressure_sum = W_sum / total_displacement
P_sum_mean = W_sum / (t_end - t_start)
```

These internal names deliberately say `sum`, not `net`. The primitive accepts raw
physical-category values and cannot prove that an upstream producer covered every
term. Only an encapsulated M4 torque-accounting source whose compiler proves the exact
term inventory may promote its completed-cycle sum to public net/brake telemetry.
Per-sample masks are not duplicated at 10 kHz.

The current generic primitive has mandatory binary64 sample fields for indicated gas,
friction/pump/accessory, and starter. The operating accountant supplies the physical
instantaneous indicated-gas torque and canonical `+0.0` placeholders for the other two
fields. It requires the corresponding completed placeholder-lane works to remain
canonical zero and consumes only `indicated_gas_work_j`; it neither publishes nor
promotes the primitive's indicated-only summed work as brake work.

The Chen–Flynn loss depends on the same completed cycle's peak pressure and therefore
cannot exist as an input waveform while that cycle is being integrated. After the
indicated cycle closes, the aggregate accountant evaluates the loss and combines
completed-cycle works. It must not manufacture a constant-through-cycle friction
signal merely to fill a sample field.

Every represented boundary crossing is observable, including the first crossing that
only ends the discarded initial partial cycle. It carries the same exact boundary
angle, time, left/right post-step sample identities, and interpolation fraction used
by the quadrature. Downstream cycle observers reuse this evidence; they do not run a
second wrapped-angle crossing detector.

The quadrature does not establish settling. Held-speed admission also requires the
separate convergence evidence in section 5.

## 3. M4 aggregate loss closure

M4 uses one cycle-mean Chen–Flynn total-friction mean effective pressure correlation:

```text
mean_piston_speed_m_s = 2 * stroke_m * engine_speed_rpm / 60

FMEP_bar =
    a_bar
    + b * displacement_weighted_mean_peak_cylinder_pressure_bar_abs
    + c_bar_s_per_m * mean_piston_speed_m_s
    + d_bar_s2_per_m2 * mean_piston_speed_m_s^2
```

The generic four-stroke prior is:

```text
a = 0.4 bar
b = 0.005
c = 0.09 bar*s/m
d = 0.0009 bar*s^2/m^2
```

For completed cycle `k`, M4 resolves the multicylinder pressure input as:

```text
pmax_mean_abs(k) =
    sum_over_cylinders(displacement_i * max_cycle_k(p_i_abs))
    / total_displacement
```

This displacement-weighted resolver is an explicit M4 choice; it is not attributed to
Chen and Flynn. Absolute cylinder pressure is read directly from the post-step gas
state rather than from quantized capture telemetry. For the same piecewise-linear
outer-step representation used by cycle quadrature, each cylinder maximum includes
the interpolated start-boundary value, every interior post-step value, and the
interpolated end-boundary value. A linear segment has no interior maximum above both
endpoints. The initial partial cycle is discarded.

The corresponding positive loss work and running-direction torque are:

```text
W_loss(k) = 100000 * FMEP_bar(k) * total_displacement
tau_loss_mean(k) = -sign(omega) * W_loss(k) / (4*pi)

W_starter(k) = +0.0
W_brake(k) = W_full_cycle_indicated_gas(k) - W_loss(k) + W_starter(k)
tau_brake_mean(k) = W_brake(k) / (4*pi)
net_BMEP(k) = W_brake(k) / total_displacement
```

The canonical positive-zero starter work is complete only because the profile is
mechanically disengaged and the complete scenario journal keeps the starter disabled.
It is not an inferred residual.

The M4 correlation replaces the M3 `13.558174560000001 N*m` fixed crank-friction
term. Adding both would double-count crank friction.

Full-cycle cylinder `p*dV` already includes gas-exchange pumping. M4 does not subtract
a second pumping term. The `pump_and_oil` torque category here means mechanically
driven pump, oil-system, and churning loss.

## 4. Ownership, domain, and limitations

The total-FMEP value owns only the aggregate numerical scope:

```text
crank friction | piston/ring friction | bearing friction |
valvetrain friction | pump/oil | accessory
```

No component value may be inferred from the aggregate. The constant coefficient
absorbs invariant/accessory losses in the cited model. M4 therefore requires one
resolved, content-identified accessory configuration. A changed configuration requires
a new admitted coefficient set.

Starter torque remains separate. For a positive-speed held-running capture it is an
available complete zero only if the scenario declares the starter disabled and the
profile declares it mechanically disengaged. Starter-enabled operation is rejected
until an engaged-starter model exists.

Chen–Flynn supplies no oil-temperature law. This M4 profile admits only the fixed warm
oil state `363.15 K` and rejects other oil temperatures rather than inventing a
correction. It admits positive running speed for held and inertial-dyno operation. It
does not claim cold-oil, oil-grade, reverse, startup, transient, or
changed-accessory accuracy.

The correlation is cycle-mean. M4 does not emit a constant-through-cycle bookkeeping
torque and must not describe one as an instantaneous friction waveform.

The coefficients are a generic prior used in a published four-stroke SI model, not
BMW M52 measurements. Chen–Flynn originated from a single-cylinder compression-
ignition research engine. Published work also reports poor SI-cycle behavior when
peak pressure occurs materially later than about 20 degrees after top dead center.
The M4 result therefore depends directly on the simulated peak pressure and retains
this explicit model-form limitation.

## 5. Held-speed convergence and result boundary

Held speed prescribes constant positive RPM and throttle while the test cell supplies
the balancing reaction. Equivalent inertia is not needed for mean reaction over a
periodic complete cycle. The constrained held result intentionally reports the
complete same-cycle reaction at cycle/block resolution rather than fabricating a
per-frame actuator waveform. The separate inertial result reports shaft motion and
energy evidence under its admitted equivalent inertia and brake law.

The convergence method is
`adjacent-nonoverlapping-cycle-block-mean-v1`, version 1. It is a deterministic
stationarity heuristic, not a deterministic-periodicity test, statistical confidence
interval, or physical validation claim. The accepted low-order combustion core has
nonzero deterministic per-ignition variation, so raw adjacent-cycle equality is not
an admissible settling rule.

The exact resolved method identity is carried by `ConvergenceSettling::method`; its
configuration SHA-256 must equal the admitted convergence implementation descriptor.

`cycles_per_block = N` must be positive. After
`minimum_warm_up_duration_s + minimum_settling_duration_s`, a complete cycle is
eligible only if its start boundary is at or after that threshold and its end boundary
is at or before `maximum_preparation_duration_s`. The latest `2*N` eligible cycles at
the fixed preparation cutoff form two adjacent, non-overlapping blocks:

```text
block A = older N cycles
block B = newer N cycles
```

The physical pressure snapshot for one cycle is its exact end-boundary Poincare state.
It contains absolute pressure for every `physically_resolved` gas volume in ascending
stable `GasVolumeId` order. The resettable atmosphere alias is excluded. Each boundary
value is interpolated from the same left/right samples and fraction emitted by the
cycle integrator:

```text
p_boundary = p_left + fraction_from_left * (p_right - p_left)
```

No pressure observer independently recomputes a cycle index, boundary angle, or
crossing fraction. It retains only the previous/current pressure vectors and bounded
complete-cycle summaries.

Stable chronological summation defines the two block means:

```text
mean_brake_torque(block) =
    sum_cycle(W_brake) / (N * 4*pi)

mean_boundary_pressure(block, volume_i) =
    sum_cycle(p_boundary_cycle_i) / N
```

The residuals are:

```text
torque_residual_nm =
    abs(mean_brake_torque(block_B) - mean_brake_torque(block_A))

pressure_residual_pa =
    max_over_physical_volumes(
        abs(mean_boundary_pressure(block_B, i)
            - mean_boundary_pressure(block_A, i)))
```

The stable gas-volume identity attaining the pressure maximum is retained; ties keep
the first ascending identity. Both comparisons are inclusive. The point is settled
only when:

```text
torque_residual_nm <= cycle_mean_torque_tolerance_nm
pressure_residual_pa <= pressure_tolerance_pa
```

The method requires all `2*N` eligible complete cycles. The compiler budgets an
initial phase acquisition plus those cycles; at held speed a conservative necessary
post-threshold duration is:

```text
(2*N + 1) * 120 / engine_speed_rpm seconds
```

Insufficient cycles or a failed residual at the fixed maximum preparation cutoff
fails closed as `preparation-not-converged`, before an audible block can be committed.
An M4 listening scenario makes the maximum preparation cutoff equal its audible start,
so an unclassified gap cannot exist.

M4 therefore does not call an operating point settled from torque alone. The evidence
retains:

- the A and B cycle ordinal/time/boundary ranges;
- both work-derived torque means, their residual, and its tolerance;
- both phase-aligned pressure means, the L-infinity residual, its limiting volume,
  and its tolerance; and
- the exact convergence method identity and `N`.

Block B is the reported operating-point window. Its indicated, loss, starter, and
brake works are summed coherently. Reported torque is total brake work divided by
`N*4*pi`; net BMEP is total brake work divided by `N*total_displacement`; mean power
is total brake work divided by the summed cycle duration.

A typed held-speed result records at least RPM, throttle, ambient/thermal/fuel/
accessory/starter conditions, completed-cycle range, indicated work, aggregate loss
work, starter work, brake work, net torque, net BMEP, mean power, convergence
residuals, and the generic-prior applicability label. The held result does not expose
a per-frame actuator/dyno reaction; that omission is a boundary of constrained held
capture, not a missing inertial-dyno implementation.

## 6. BMW manufacturer plausibility landmarks

The M4 comparison baseline is the original pre-1998 BMW M52B28, not the 1998
double-VANOS M52 redesign/M52TU. BMW-issued manufacturer material declares the
following nominal values:

| Quantity | Manufacturer value |
|---|---:|
| Cylinders | 6 |
| Bore / stroke | 84 mm / 84 mm |
| Displacement | 2793 cm3 |
| Compression ratio | 10.2:1 |
| Maximum torque | 280 N*m at 3950 rpm |
| Maximum output | 142 kW at 5300 rpm |

Using the published displacement `0.002793 m3`, these landmarks imply:

- `115.820 kW` and `12.598 bar` brake MEP at `280 N*m / 3950 rpm`;
- `255.849 N*m` and `11.511 bar` brake MEP at `142 kW / 5300 rpm`.

Six mathematical `84 mm` by `84 mm` cylinders give approximately
`2793.0518 cm3`; the published whole-cubic-centimetre displacement is therefore not
an exact geometry-equality target.

Only the complete, converged M4 modeled shaft-output result may be compared with these
landmarks. Indicated-gas torque, aggregate loss, or any component term may not be
compared independently and relabelled as BMW evidence.

The sources state no applicable power-test standard, atmospheric or thermal
conditions, fuel, accessory configuration, run-in state, or manufacturer tolerance.
They also provide no indicated pressure, FMEP, component losses, full torque curve,
or control maps. M4 therefore reports its value and ratio to each landmark but does
not fit the generic loss prior to them or treat agreement as validation.

Until same-condition evidence exists, a modeled maximum torque or power outside
`0.5` through `1.5` times the corresponding BMW value is a warning-only gross-error
tripwire. This interval is project QA policy, not a BMW tolerance and not an accuracy
or calibration acceptance gate. Non-finite output, non-convergence, energy-identity
failure, or comparison against the M52TU landmarks fails the operating-point
evaluation.

## 7. Sources

- Chen and Flynn, “Development of a Single Cylinder Compression Ignition Research
  Engine,” [SAE 650733](https://saemobilus.sae.org/papers/development-a-single-cylinder-compression-ignition-research-engine-650733),
  is the original attribution for the correlation.
- Tingting Li, *A High Efficiency and Clean Combustion Strategy for Compression
  Ignition Engines: Integration of Low Heat Rejection Concepts with Low Temperature
  Combustion*, Texas A&M University dissertation, 2017, printed pp. 39–40, records
  the total-friction interpretation, coefficient ranges, and accessory/invariant
  scope of the constant term:
  [institutional PDF](https://oaktrust.library.tamu.edu/server/api/core/bitstreams/7bd25bcc-4712-4277-b714-d67206d6b604/content).
- Jan Wittenbecher, *Development of a Dynamic Mean Value Engine Model*, University of
  Washington thesis, 2017, pp. 22–23, records the exact generic four-stroke
  coefficients above and applies them to a Honda four-stroke SI model:
  [institutional PDF](https://digital.lib.washington.edu/bitstreams/6a99eb25-f5a0-43fc-92cc-7fa6ac1c8496/download),
  reviewed content SHA-256
  `f378d08af3e9a3b4c7662e2a3f6b2354350317a52edaaa6eb47520f3be24914e`.
- Pipitone, “A New Simple Friction Model for S.I. Engine,”
  [SAE 2009-01-1984](https://iris.unipa.it/handle/10447/46811), records the cited
  late-peak-pressure limitation.
- Kee and Blair, “Acceleration Test Method for a High Performance Two-Stroke Racing
  Engine,” [SAE 942478](https://saemobilus.sae.org/papers/acceleration-test-method-a-high-performance-two-stroke-racing-engine-942478),
  describes an inertia flywheel whose measured acceleration yields torque and power.
- The Modelica Association's
  [Rotational library guide](https://doc.modelica.org/Modelica%204.0.0/Resources/helpDymola/Modelica_Mechanics_Rotational_UsersGuide.html)
  supplies the standard rigid one-dimensional inertia, applied-torque, brake, and
  sign-convention model boundary used here. It is structural support, not validation
  of this BMW parameter set.
- BMW AG, *Owner's Manual for the vehicle*, order number `01 41 9 790 377`,
  edition `US VIII/97`, online edition `07/98`, printed page 160, records the
  original 328i engine geometry and output landmarks. The reviewed extracted PDF has
  SHA-256
  `df6c0304c13a7da4d9e2afc923e0e40d64a3eccd715020420b73387e880f36a7`;
  the containing archive has SHA-256
  `da34f3af2e851c40e46a706ec664ffafefc76637d3139e21b2dea20a466e7a44`:
  [archived BMW-issued manual](https://www.bmwsections.com/docs/d.php?file=1998_manual_e36).
- BMW Group PressClub, *L'histoire des six cylindres en ligne BMW*, published
  2000-05-06, and its section 2.3 table distinguish the original 1994 M52 values
  from the 1998 double-VANOS redesign. The reviewed official attachment has SHA-256
  `3ebddf08bd798204248ff0d6eec38d2553299dd8503063a35bf7329903710be8`:
  [article](https://www.press.bmwgroup.com/france/article/detail/T0028512FR/l-histoire-des-six-cylindres-en-ligne-bmw?language=fr),
  [official attachment](https://www.press.bmwgroup.com/france/article/attachment/T0028512FR/48815).
- BMW Group Classic's
  [BMW 328i (E36)](https://www.bmwgroup-classic.com/en/models/bmw-classics/product-description-page.ad-464-10.bmw-328i-e36.html)
  page independently corroborates `2793 cm3` and `142 kW at 5300 rpm`.

The SAE papers are not vendored and may be paywalled. Institutional theses are linked,
not redistributed. A future BMW calibration requires identified same-condition
indicated and brake evidence; fitting a published BMW torque curve is calibration, not
independent validation.
