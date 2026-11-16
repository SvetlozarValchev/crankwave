# Diesel prepared-fuel combustion v1

Status: implemented numerical component, used by the experimental
[mechanical diesel sound path](MECHANICAL_DIESEL_SOUND.md). The general production
engine session and C API do not yet admit this combustion family.

Implementation: [`diesel_combustion_v1.hpp`](../../src/simulation/diesel_combustion_v1.hpp)
and [`diesel_combustion_v1.cpp`](../../src/simulation/diesel_combustion_v1.cpp).
Internal method identifier: `diesel-prepared-fuel-combustion-v1`.

## Domain and ownership

The component advances the fuel inventory of one cylinder through a finite train
of up to eight nonoverlapping hydraulic injection pulses. One main pulse supports
the initial mechanical-injection numerical fixture. Multiple pulses use the same
inventory and ignition state. This is a reduced phenomenological model of fuel
preparation, ignition progress, premixed burning, and mixing-controlled burning.
It is not a validated real-engine calibration or a complete diesel simulation.

All coefficients must be supplied explicitly. The numerical tests use synthetic
coefficients with no claimed engine provenance. A production integration must bind
an applicable calibration and method configuration through the normal provenance,
admission, and request-identity contracts.

The caller owns crank-angle scheduling and converts unwrapped injection crossings
to local hydraulic delivery times. Pulse times do not represent injector electrical
commands. The caller also owns chamber thermodynamics, gas exchange, sensible fuel
enthalpy, volume work, wall losses, and any governor/limiter. Each advance receives
held temperature, absolute pressure, charge mass, and available oxygen mass. It
returns injected fuel mass, consumed oxygen, and chemical heat for that gas owner
to apply once. The caller must subtract consumed oxygen before the next advance;
passing an unchanged oxygen inventory would supply fresh oxygen every time.

Conditions are held over each advance. Splitting a long call at pulse boundaries
does not update its pressure or temperature. A coupled gas integrator must choose
and refine its own step size, apply heat and mass, and re-evaluate conditions. The
test fixture exercises that feedback explicitly.

## Hydraulic delivery

Pulses are supplied in chronological order with unique positive IDs, start time,
duration, total fuel mass, opening fraction, and closing fraction. Fractions are
nonnegative and sum to at most one. Zero-duration pulses are invalid; zero-mass
pulses are valid and never arm ignition. Empty pulse trains support motoring.
Adjacent pulses are valid. Overlapping pulses for the same injector are rejected.

The delivery rate is a normalized trapezoid: a linear opening ramp, constant
plateau, and linear closing ramp. Zero ramp fractions produce a rectangle; a
fraction sum of one produces a triangle. For represented knot times `s, o, c, e`:

```text
effective area time = (o-s)/2 + (c-o) + (e-c)/2
peak mass flow      = pulse fuel mass / effective area time
```

The normalization uses represented knot intervals so integrating the pulse yields
the authored mass. Nonfinite parameters, collapsed positive ramp intervals,
unrepresentable durations, and overflowing flow/energy values are rejected.

`advance_to(end_time_s, conditions, fuel_enabled)` uses an absolute end time.
Callers can construct that time from an integer/rational grid. Delivery is integrated
over elapsed time, so arriving exactly at a pulse start delivers zero mass; the
following positive interval delivers its first portion. Pulse knees are explicit
internal boundaries. No equality test on wrapped crank angle exists here.

`fuel_enabled=false` removes delivery over that interval, including the remainder
of an active pulse. Re-enabling does not replay missed fuel. Fuel already delivered
continues preparing and burning. An upstream fuel governor can supply later pulse
trains with different masses; this component does not reinterpret an air throttle
as a fuel controller.

## Ignition and fuel pools

Let `Y_O2 = oxygen_mass / charge_mass`. The explicit reference-normalized delay law is:

```text
tau = tau_ref * exp(T_activation * (1/T - 1/T_ref))
      * (p_ref / p)^pressure_exponent
      * (Y_O2_ref / Y_O2)^oxygen_exponent
```

This is the selected phenomenological form, not a universal diesel correlation.
The exponents and activation temperature must be nonnegative. Disabled dependencies
evaluate as exact zero log corrections. Calculations use log differences to avoid
overflow in intermediate ratios. Zero oxygen gives infinite delay and no ignition;
nonfinite, overflowing, or underflowed finite-delay arithmetic is an error.

Once nonzero fuel delivery begins, or unburned fuel is present, ignition progress
integrates `dt/tau` until it reaches one. The step splits at that onset. No combustion
heat is released before it. A threshold that falls within rounding distance of an
already represented hydraulic boundary is assigned to that boundary under the
explicit eight-epsilon progress check; no zero-duration step is executed.

Unprepared fuel `U` follows a first-order preparation law:

```text
dU/dt = injected_mass_flow - U / preparation_time
```

The implementation analytically integrates this equation for the linear rate on
each hydraulic segment. Exponential convolution weights use a small-argument
series when `dt/preparation_time < 0.001` to avoid cancellation. The mass leaving
`U` enters the premixed pool before ignition and the diffusion pool after ignition.

Once ignited, premixed fuel requests exponential burning with the explicit
`premixed_burn_time`. Prepared diffusion fuel requests immediate burning; its
supply is limited by preparation. Both requests share the available oxygen budget
in proportion to their requested burn mass. Unburned oxygen-limited fuel remains
in its pool. Heat release is burned mass times the supplied lower heating value;
no hidden efficiency or audio gain changes chemical energy.

The invariant is:

```text
injected fuel = unprepared fuel + premixed fuel + diffusion fuel + burned fuel
```

The state retains the first ignition time through the entire pulse train. A later
pulse uses that state instead of starting an independent gasoline-like flame.
Extinction, reignition, wall films, soot chemistry, crevice fuel, and detailed
unburned/burned-zone temperatures are outside this method's domain. Widely separated
injections requiring extinction or reignition need a later model. Common-rail
calibration cannot be claimed from pulse-list support alone.

The next fresh cylinder charge requires a fresh session. The gas owner must account
for residual/unburned fuel before replacing the old inventory.

The internal [held-speed cylinder runtime](DIESEL_CYLINDER_V1.md) now supplies that
gas ownership, crank scheduling, valve exchange, and repeated-cycle accounting.
It explicitly carries fuel left at exhaust opening as a nonreacting tracer.

## Numerical and failure semantics

Compilation copies the bounded pulse plan. Advancing allocates no memory. There
are at most four hydraulic knots per pulse, one ignition split, and a final interval.
Traversal order is fixed and no random draws occur. Absolute time must advance.
Errors return typed diagnostics without changing any session state, including
errors after an intermediate pulse segment has already been evaluated locally.

Analytically nonnegative reservoir values can receive a roundoff correction bounded
by 64 binary64 epsilons of their available mass. Larger violations fail. The fuel
ledger is checked at relative tolerance `1e-10`; pools are never renormalized to
hide missing mass. The method obeys the repository floating-point build policy.

Regrouping an identical sequence of integration endpoints into callbacks preserves
the state exactly. Changing the physics integration grid is a numerical convergence
question, not a byte-identity promise. Cross-toolchain determinism remains subject
to the existing [model envelope](../../MODEL.md).

## Verification

[`diesel_combustion_v1_test.cpp`](../../tests/diesel_combustion_v1_test.cpp) covers
analytic pulse areas and first-order reservoir solutions, delay dependencies,
ignition inside a step and at a pulse endpoint, all eight pulses, zero fuel,
oxygen starvation and recovery, fuel cut/re-enable, conservation, invalid plans,
transaction rollback, and callback regrouping.

The coupled fixture is a closed-valve ideal-gas cylinder traversing compression and
expansion. It uses constant heat capacities, no wall heat transfer, and synthetic
injection coefficients. Motoring compression is checked against `p V^gamma = const`;
firing runs check chemical energy, sensible injected-fuel energy, volume work,
pressure, and ignition-angle convergence. Equal-air light/heavy fueling and equal-
total-fuel main-only/pilot-main cases verify that pressure follows combustion state.
They do not validate a torque curve, four-stroke gas exchange, mechanical clatter,
or a particular real engine.

```bash
cmake -S . -B build -DCRANKWAVE_BUILD_TESTS=ON
cmake --build build --target crankwave_diesel_combustion_v1_tests
ctest --test-dir build -R '^simulation.diesel_combustion_v1$' --output-on-failure
```

The test executable optionally writes its synthetic pressure comparison to a CSV
path supplied as its only argument. This is a numerical diagnostic, not a new
production renderer or engine authoring format.

## Technical context

Prepared premixed fuel and subsequent mixing-controlled heat release are discussed
in [A Zero-Dimensional Mixing Controlled Combustion Model, 2019](https://www.mdpi.com/1996-1073/12/10/2000).
That marine two-stroke paper motivates the decomposition; its equations and fitted
coefficients are not claimed as the implemented method or copied as road-engine
calibration. The simple reservoir equations here define this implementation.

The importance of pilot/main interaction to combustion noise is demonstrated by
[Busch et al., 2015](https://saemobilus.sae.org/articles/experimental-numerical-investigations-close-coupled-pilot-injections-reduce-combustion-noise-a-small-bore-diesel-engine-2015-01-0796).
Matching such experiments requires appropriate injection, gas-state, and acoustic
calibration beyond this numerical foundation.
