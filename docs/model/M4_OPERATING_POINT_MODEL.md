# M4 operating-point model

Status: normative pre-implementation companion, implemented one checked subsection at
a time

Applies to: M4 held-speed BMW M52B28 operating points

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

The first M4 torque curve is labelled:

> generic Chen–Flynn low-order BMW model prediction

It is not labelled BMW truth, measured brake torque, SAE net power, or independent
validation.

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
- indicated-gas, friction/pump/accessory, and starter torques are each linear in
  unwrapped crank angle;
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

W_brake(k) = W_full_cycle_indicated_gas(k) - W_loss(k)
tau_brake_mean(k) = W_brake(k) / (4*pi)
net_BMEP(k) = W_brake(k) / total_displacement
```

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
correction. It admits positive held speed only. It does not claim cold-oil, oil-grade,
reverse, startup, transient, or changed-accessory accuracy.

The correlation is cycle-mean. Any constant-through-cycle torque used for bookkeeping
is only work-equivalent and must not be described as an instantaneous friction
waveform.

The coefficients are a generic prior used in a published four-stroke SI model, not
BMW M52 measurements. Chen–Flynn originated from a single-cylinder compression-
ignition research engine. Published work also reports poor SI-cycle behavior when
peak pressure occurs materially later than about 20 degrees after top dead center.
The M4 result therefore depends directly on the simulated peak pressure and retains
this explicit model-form limitation.

## 5. Held-speed convergence and result boundary

Held speed prescribes constant positive RPM and throttle while the test cell supplies
the balancing reaction. Equivalent inertia is not needed for mean reaction over a
periodic complete cycle, but instantaneous actuator/dyno reaction remains unavailable
until equivalent inertia and its derivative are admitted.

The convergence method is
`adjacent-nonoverlapping-cycle-block-mean-v1`, version 1. It is a deterministic
stationarity heuristic, not a deterministic-periodicity test, statistical confidence
interval, or physical validation claim. The accepted low-order combustion core has
nonzero deterministic per-ignition variation, so raw adjacent-cycle equality is not
an admissible settling rule.

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
work, brake work, net torque, net BMEP, mean power, convergence residuals, and the
generic-prior applicability label. Per-frame instantaneous actuator and dyno reaction
remain unavailable.

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
- Tingting Li, *A Computationally Efficient Physics-Based Model for Internal
  Combustion Engine Simulation and Control System Design*, Texas A&M University
  dissertation, 2017, pp. 57–59, records the total-friction interpretation,
  coefficient ranges, and accessory/invariant scope of the constant term:
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
