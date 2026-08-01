# M4 operating-point model

Status: normative implementation companion; fixed-horizon sampling cutover implemented

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

StarterCapabilityV1T<Field> {
  Field<StarterCapabilityType> type
  Field<double> maximum_torque_nm
  Field<double> target_speed_rad_s
  Field<TorqueTermMask> included_terms
}

AuthoredLowOrderOperatingPointV1Profile {
  AuthoredLowOrderEngineCoreV1 core
  AuthoredChenFlynnCycleMeanAggregateLossV1 aggregate_loss
  AuthoredAccessoryConfigurationIdentityV1 accessory_configuration
  AuthoredStarterCapabilityV1 starter
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
| `FixedHorizonCycleSampling::method` | `fixed-horizon-trailing-complete-cycle-sample-v1` | 1 |

The implementation-owned canonical descriptor and configuration SHA-256 for a method
are pinned when that implementation exists and is admitted. A literature PDF hash is
evidence identity, never a `MethodIdentity::configuration_sha256`.

The implemented cycle-accounting and fixed-sampling authorities are:

| Method | Configuration SHA-256 |
|---|---|
| `four-stroke-piecewise-linear-cycle-quadrature-v1` | `57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac` |
| `chen-flynn-cycle-mean-aggregate-loss-v1` | `6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489` |
| `fixed-horizon-trailing-complete-cycle-sample-v1` | `9efbb15d0ad27d3f97d75d135b642c9a7feec6610c50e7ec82523ec62808da63` |

These are SHA-256 digests of the canonical LF descriptors exposed by the production
implementations. Exact BMW profile validation admits the two engine-owned
cycle-accounting identities, while scenario and held-result validation admit the
scenario-owned fixed-sampling identity. Each comparison includes ID, version, and
configuration digest, not merely ID and version. One bounded trailing-cycle sampler
is composed into the held-speed executor as the operating policy of the sole low-order
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
`starter.type` is either `mechanically_disengaged`, with canonical positive-zero
torque and target speed, or `cranking`, with finite positive maximum torque and target
speed. `starter.included_terms` is exactly the starter bit (`0x80`). Held and
inertial-dyno scenario journals still require starter disabled. The accounting
compiler must prove that indicated gas (`0x01`), aggregate loss (`0x7e`), and starter
(`0x80`) are pairwise disjoint and their union is the complete known mask (`0xff`).

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
output at completed-cycle/sample resolution.

The profile admits `HeldSpeed` and `InertialDyno`, both with
`FixedHorizonCycleSampling`, finite positive running speed, the exact oil condition
above, and a fired running state at every journal point: ignition, fuel, and dyno
enabled; starter and limiter disabled. `HeldSpeed` constrains speed during capture;
`InertialDyno` releases the state at the declared fixed horizon into the admitted
crank-dynamics method.
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

`FixedHorizonCycleSampling` owns the method identity, exact preparation horizon, and
positive trailing complete-cycle count. Sampling remains scenario/test-cell policy
and is not duplicated in the engine profile. Its normative contract is
[`M4_FIXED_HORIZON_SAMPLING.md`](M4_FIXED_HORIZON_SAMPLING.md); there is no alternate
preparation policy, compatibility tag, adapter, or fallback.

This variant was added to the executable-profile union atomically with its authored
and resolved validation, method policy, topology/root mapping, randomness access,
final manifest wire, and request-identity wire. The low-order capture runtime selects
exactly one profile policy: the M3 fixed-crank accountant or this M4
complete-cycle/fixed-sample policy, with the latter selecting constrained held capture
or released inertial capture from the scenario. Both consume the same transactional
core step inside the same block loop; neither policy can instantiate, evaluate, or
fall back to the other. Malformed cycle evidence or an insufficient trailing window at
the exact fixed horizon terminalizes the session before the containing capture block
reaches its consumer. Successful M4 completion alone retains the request-v5-bound
typed operating result.

The canonical BMW profile factory constructs this profile directly from the reusable
low-order core under a fresh operating provenance root. Its exact validator pins the
coefficient bits, oil and accessory conditions, torque partition, implemented method
identities, capability statement, and complete provenance bundle. The listening
request-set factory separately rebuilds that engine and each held-speed scenario
through a fresh builder before finishing its ledger; it never copies a completed
profile, shares mutable state between points, or relabels an M3 request.

### 1.2 Canonical held operating-regression request set

`make_bmw_m52b28_held_regression_request_set()` has no calibration arguments. It
constructs exactly four requests in this order:

| Point key | Scenario ID | RPM | Throttle |
|---|---|---:|---:|
| `rpm1500-throttle0p85` | `bmw-m52b28-held-regression-rpm1500-throttle0p85` | 1500 | 0.85 |
| `rpm3000-throttle0p25` | `bmw-m52b28-held-regression-rpm3000-throttle0p25` | 3000 | 0.25 |
| `rpm3000-throttle0p85` | `bmw-m52b28-held-regression-rpm3000-throttle0p85` | 3000 | 0.85 |
| `rpm6500-throttle0p85` | `bmw-m52b28-held-regression-rpm6500-throttle0p85` | 6500 | 0.85 |

Every point uses a `6.44 s` fixed preparation horizon and audible start, the latest 32
eligible complete cycles, `15.0 s` audible duration, `21.44 s` total duration,
`10000 Hz` physics/capture rates, `192000 Hz` source/acoustic/delivery rates,
200-frame capture blocks, 3800-record event capacity, and public seed `0xC0FFEE`.
The integer horizons are `64400` preparation and `214400` total physics/capture
frames, plus `2880000` audible and `4116480` total 192 kHz frames.

The three `0.85` points isolate RPM; the two `3000 rpm` points isolate throttle. The
`0.25` value is a normalized throttle command, not a target or percentage load. Each
request returns its achieved net shaft torque, power, and net BMEP from one
request-v5-bound fixed sample; it neither compares adjacent windows nor claims
stationarity. Exact point keys, conditions, CLI selectors, source-route non-claims,
and listening gate are frozen separately in
[`M4_BMW_HELD_REGRESSION_MATRIX.md`](../M4_BMW_HELD_REGRESSION_MATRIX.md).

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
| Scenario ID | `bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2` |
| Initial / listening-target speed | `1500 rpm` / `6500 rpm` |
| Throttle | `0.85` |
| Fixed preparation horizon / audible start | `6.44 s` / `6.44 s` |
| Trailing complete-cycle sample | `32` cycles |
| Audible duration / total duration | `15.0 s` / `21.44 s` |
| Total crank-referred equivalent inertia | `7.9 kg*m^2` |
| Passive brake curve | `40 N*m` from `1000` through `7500 rpm` |
| Crank-dynamics method | `rigid-crank-zoh-work-energy-v1`, version 1 |
| Passive-brake method | `piecewise-linear-positive-speed-passive-brake-v1`, version 1 |
| Physics / capture rates | `10000 Hz` / `10000 Hz` |
| Source / acoustic / delivery rates | `192000 Hz` / `192000 Hz` / `192000 Hz` |
| Capture block / event capacities | `200` frames / `3800` records |
| Public seed | `0xC0FFEE` |

`7.9 kg*m^2` is a declared test-cell total, including all rigidly crank-referred
engine, coupling, and flywheel inertia. It is not a measured BMW value and must not be
added to a second hidden engine inertia. A future component inventory may derive this
total as `J_engine + sum(J_i * ratio_i^2)`; v1 owns only the resolved total and its
provenance.

The first executable calibration at `6.5 kg*m^2` reached the declared brake-domain
ceiling (`7500.019868 rpm`) at scenario time `20.8741 s`, before the fixed
`21.44 s` horizon. The canonical total was therefore changed to `7.9 kg*m^2` to put
the 1500-to-6500 listening climb near the end of its 15-second released window while
retaining the fixed throttle and passive load. This is an explicit test-cell request
calibration; no hidden clamp or prescribed speed was introduced.

The listening target is evidence, not a prescribed trajectory. The fixed-horizon run
records the first frame at or above `6500 rpm`, if any, and continues to the declared
duration. Missing the target does not get disguised by resampling or clamping; the
canonical publisher fails its listening gate and reports the final speed. The brake
curve uses an admitted piecewise-linear method, is a nonnegative resisting magnitude,
and must cover every evaluated positive speed. Extrapolation, reverse rotation, and a
zero-speed stick model are absent from v1.

Preparation holds exactly `1500 rpm` using a test-cell actuator while the passive
brake remains active. At the exact `6.44 s` horizon the sampler finalizes the latest
32 eligible complete cycles, and the hold actuator becomes zero at physics frame
`64400`. The sample is bounded preparation evidence, not a stationarity certificate
or a release condition computed from the observed values. Crank angle, gas state,
flame state, pressure history, randomness, and the latest completed aggregate-loss
state continue without a reset.

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

The quadrature does not establish stationarity. Held-speed reporting uses the
separate fixed-horizon sample boundary in section 5.

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

The canonical positive-zero starter work is complete because the complete held/dyno
scenario journal keeps the starter disabled, regardless of whether the engine has
cranking hardware. It is not an inferred residual.

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
available complete zero when the scenario declares the starter disabled. Engaged
starter operation is admitted only in `free_engine`, where the source-faithful
unilateral target-speed constraint applies positive-forward torque up to the compiled
maximum and engagement/release remain owned by the authored or live control state.

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

## 5. Held-speed fixed sample and result boundary

Held speed prescribes constant positive RPM and throttle while the test cell supplies
the balancing reaction. Equivalent inertia is not needed for mean reaction over a
complete-cycle sample. The constrained held result intentionally reports the complete
same-cycle reaction at cycle resolution rather than fabricating a per-frame actuator
waveform. The separate inertial result reports shaft motion and energy evidence under
its admitted equivalent inertia and brake law.

Production uses exactly one `FixedHorizonCycleSampling` policy. Its declared horizon
`H` is the preparation endpoint and audible start; its positive count `M` is the size
of the one trailing sample. A completed cycle is eligible when its exact end-boundary
time is no later than `H`. The sampler validates all chronological cycle inputs through
that horizon, retains only the latest `M` eligible cycles, and finalizes exactly at
`H`. It does not compare two windows, stop early, extend the horizon, or claim that the
engine is stationary.

Each retained cycle carries coherent indicated-gas, positive aggregate-loss, starter,
and brake works plus its exact boundary evidence and end-boundary absolute pressure
for every non-atmosphere gas volume in ascending stable `GasVolumeId` order. Boundary
values reuse the interpolation evidence emitted by the cycle integrator; no second
wrapped-angle crossing detector exists.

Stable chronological reduction over the single sample defines the reported values:

```text
mean_brake_torque = sum_cycle(W_brake) / (M * 4*pi)
net_BMEP = sum_cycle(W_brake) / (M * total_displacement)
mean_power = sum_cycle(W_brake) / sum_cycle(cycle_duration)
mean_boundary_pressure(volume_i) =
    sum_cycle(p_end_boundary_cycle_i) / M
```

`HeldSpeedFixedHorizonSampleEvidence` owns the exact sampling method identity, `M`,
`H`, one chronological `HeldSpeedCycleBlockEvidence`, and an attestation of the last
eligible cycle ordinal and end boundary at `H`. `HeldSpeedOperatingPointResult` owns,
in order, the `simulation_request_identity_v5_sha256`, operating conditions,
generic-prior applicability label, and that sampling evidence. Its
`reported_block()` is the one trailing sample.

An insufficient trailing window, malformed cycle evidence, or nonfinite reduction is
a typed contract/runtime failure before audible output is committed. There is no
residual, tolerance, limiting-volume, stationarity flag, or rejected-policy terminal
vocabulary in the production result. There is also no old preparation tag, request
encoder, parser, adapter, or fallback. The exact plan, descriptor, validation,
reduction, and wire contracts are frozen in
[`M4_FIXED_HORIZON_SAMPLING.md`](M4_FIXED_HORIZON_SAMPLING.md).

The held result does not expose a per-frame actuator/dyno reaction; that omission is a
boundary of constrained held capture, not a missing inertial-dyno implementation.

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

Only the complete, request-bound M4 modeled shaft-output sample may be compared with
these landmarks. Indicated-gas torque, aggregate loss, or any component term may not
be compared independently and relabelled as BMW evidence.

The sources state no applicable power-test standard, atmospheric or thermal
conditions, fuel, accessory configuration, run-in state, or manufacturer tolerance.
They also provide no indicated pressure, FMEP, component losses, full torque curve,
or control maps. M4 therefore reports its value and ratio to each landmark but does
not fit the generic loss prior to them or treat agreement as validation.

Until same-condition evidence exists, a modeled maximum torque or power outside
`0.5` through `1.5` times the corresponding BMW value is a warning-only gross-error
tripwire. This interval is project QA policy, not a BMW tolerance and not an accuracy
or calibration acceptance gate. Non-finite output, invalid fixed-sample evidence,
energy-identity failure, or comparison against the M52TU landmarks fails the
operating-point evaluation.

### 6.1 Canonical full-throttle torque sweep

This subsection freezes the historical torque-sweep evidence-v2 artifact grammar.
Its request-v3 names remain artifact identity; they are not aliases accepted by the
sole current request-v5 encoder.

The first modeled sweep is frozen before any point is executed. It consists of nine
independent held-speed sessions in ascending order:

```text
1500, 2500, 3000, 3500, 3950, 4500, 5300, 6000, 6500 rpm
```

Each point uses throttle `1.0`; the original-M52 operating profile; the profile-owned
warm stock-accessory condition; `101325 Pa`, `298.15 K`, and zero relative humidity;
the profile fuel; initial gas/crankcase temperature `298.15 K`; wall, coolant, and oil
temperature `363.15 K`; fired running state with ignition, fuel, and dyno enabled and
starter plus limiter disabled; public seed `0xC0FFEE`; and `10000 Hz` physics/capture
rates plus `192000 Hz` source-processing, acoustic, and delivery rates. Initial crank
angle is exactly the operating core's `crank_tdc_reference_rad`. The sole operating
state point is ID `torque-sweep-held-running` at time zero. No point reuses mutable
state from another point.

Every session has a fixed `6.44 s` preparation horizon and audible start, followed by
one sample of the latest `32` eligible complete cycles. Audible duration is exactly
`0.02 s`, making total duration exactly `6.46 s`; this post-horizon tail is transport
continuity evidence, not an audible clip. The capture block capacity is 200 frames and
the event-journal capacity is 3800 records. Each scenario uses `RenderQuality` ID
`low-order-operating-point-torque-sweep-v1`, version 1. Each scenario ID is
`bmw-m52b28-held-<rpm>rpm-full-throttle-torque-sweep-v2`.

Only a request-v3-bound, complete `HeldSpeedOperatingPointResult` with the exact
32-cycle fixed sample contributes to the sweep. Any invalid request/result, non-finite
quantity, malformed or insufficient sample, missing complete torque term, or partial
point fails the entire record. There is no alternate horizon, second policy, retry,
or interpolation over a failed point.

The evidence record retains, for every point, the canonical simulation-request-v3
digest, RPM, throttle, indicated-gas, aggregate-loss, starter, and net-shaft
cycle-mean torque, net BMEP, mean power, the single sample's first and last cycle,
applicability label, and elapsed time. It also retains the clean source commit. The
publication is exactly `bmw-m52b28-m4-torque-sweep-v2.json` plus
`bmw-m52b28-m4-torque-sweep-v2.json.sha256`.

The JSON wire schema is
`engine-sim-offline.bmw-m52b28-torque-sweep-evidence.v2`; its canonical grammar is
`engine-sim-offline.bmw-m52b28-torque-sweep-evidence-canonical-json.v2` and its
`schema_version` is `2`. Conditions identify the fixed-sampling method, its
configuration digest, the 32-cycle count, and fixed-horizon frame. Each point carries
`simulation_request_v3_sha256`, one `sample_first_cycle`/`sample_last_cycle` range,
and no comparison-window fields. The exact member order, scalar grammar, warning
order, sidecar bytes, and complete v2 schema are frozen in
[`M4_FIXED_HORIZON_SAMPLING.md`](M4_FIXED_HORIZON_SAMPLING.md); no previous evidence
schema or encoder is retained.

For each point the runner independently encodes simulation-request-v3 from that
point's fresh engine, scenario, and finished provenance bundle, supplies that digest
to the runtime, then requires the returned result to bind the same digest. A mutated
held-listening request or a reused provenance ledger is not an admissible shortcut.

The nine points execute sequentially in the frozen ascending-RPM order. Timing uses
`std::chrono::steady_clock`. A point's `elapsed_ns` interval starts immediately before
constructing its runtime/session and ends immediately after obtaining and validating
the complete request-bound typed result. The total interval starts immediately before
encoding the first point's simulation-request-v3 identity and ends immediately after
validating the ninth point's result. It therefore includes all nine request-identity
encodes and the small sequential runner overhead between point intervals. Both
durations use `duration_cast<nanoseconds>` and must fit the unsigned 64-bit evidence
field.

Comparison is deterministic and does not fit the model:

- the `3950 rpm` modeled net-shaft torque is divided by `280 N*m`;
- the `5300 rpm` modeled mean power is divided by `142000 W`;
- sampled maximum net-shaft torque and mean power are selected from the nine points in
  ascending-RPM order, with the lower RPM winning an exact binary64 tie; and
- those two sampled maxima are divided by `280 N*m` and `142000 W` respectively and
  receive the warning-only `0.5` through `1.5` gross-error tripwire.

The exact landmark-point ratios and sampled-maximum ratios are reported separately.
The sparse sampled maxima are not described as continuous curve maxima. Agreement is
not BMW calibration or validation because the manufacturer test conditions remain
unknown. The runner produces evidence only; low/middle/high RPM and load audio is the
next separate listening checkpoint.

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
