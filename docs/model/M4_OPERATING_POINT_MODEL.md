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
Chen and Flynn.

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

M4 does not call an operating point settled from torque alone. The named convergence
method must compare, over the declared consecutive-cycle window:

- complete-cycle mean brake torque; and
- phase-aligned pressure state for every physical gas volume.

The implementation must freeze the exact norm, comparison ordering, tolerances,
boundary phase, minimum preparation, maximum preparation, and failure behavior before
enabling held-speed public results. Failure to converge by the maximum duration fails
closed.

A typed held-speed result records at least RPM, throttle, ambient/thermal/fuel/
accessory/starter conditions, completed-cycle range, indicated work, aggregate loss
work, brake work, net torque, net BMEP, mean power, convergence residuals, and the
generic-prior applicability label. Per-frame instantaneous actuator and dyno reaction
remain unavailable.

## 6. Sources

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

The SAE papers are not vendored and may be paywalled. Institutional theses are linked,
not redistributed. A future BMW calibration requires identified same-condition
indicated and brake evidence; fitting a published BMW torque curve is calibration, not
independent validation.
