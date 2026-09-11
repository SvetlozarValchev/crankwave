# Diesel held-speed cylinder v1

Status: implemented numerical runtime, also used by the experimental
[mechanical diesel sound baker](MECHANICAL_DIESEL_SOUND.md). The general production
EngineSession/C API and capture contracts do not yet admit it. The numerical
fixture is synthetic and has no real-engine calibration.

Implementation: [`diesel_cylinder_v1.hpp`](../../src/simulation/diesel_cylinder_v1.hpp)
and [`diesel_cylinder_v1.cpp`](../../src/simulation/diesel_cylinder_v1.cpp).
Method identifier: `diesel-held-speed-cylinder-v1`.

## Domain and initialization

This runtime owns one four-stroke cylinder at an explicitly held positive RPM.
It couples the [prepared-fuel combustion component](DIESEL_COMBUSTION_V1.md) to
slider-crank volume, a calorically perfect gas, oxygen inventory, two valve ports,
volume work, and wall heat exchange. Exhaust and intake are fixed, fuel-free
reservoirs with independently supplied pressure, temperature, and oxygen fraction.
Neither reservoir pressure nor valve area changes with the fuel command.

The initial state is at intake-valve closure (IVC), using explicit initial charge
pressure, temperature, and oxygen mass fraction. Firing TDC is angle zero, as in
[MODEL.md](../../MODEL.md). For intake closure angle `theta_IVC` in `(2*pi, 4*pi)`,
cycle zero starts at `theta_IVC - 4*pi` and ends at `theta_IVC`. Each later cycle
adds `4*pi` to the unwrapped angle. Initialization does not imply a settled warm
operating point: subsequent cycles retain the gas history and must be observed
until the operating point settles.

Admitted valve windows do not wrap: exhaust opens at or after firing TDC, before
intake opens, and closes before intake closes. Intake closure lies between
`2*pi` and `4*pi`. The valves may overlap around exhaust TDC; both ports then
exchange gas with their fixed reservoirs. Pressure-wave coupling is not modeled.
Effective area is an explicit maximum multiplied by `sin²(pi*u)`, where
`u` runs from zero to one across the valve window. The maximum includes discharge
coefficient. This is an authored area envelope, not a cam-lift or port-flow model.
Zero area supports closed-port numerical fixtures.

No coefficients are inferred from a fuel name, injection-system name, or sound.
The API is internal; its plain SI inputs are not a substitute for the provenance,
method admission, identity, and serialization required by a production engine.

## Injection and controls

A bounded list of up to eight hydraulic pulses gives ID, start angle relative to
firing TDC, angular duration, full-command fuel mass, and opening/closing fractions.
All pulses must lie between the starting IVC and exhaust-valve opening (EVO).
The underlying combustion compiler validates IDs, overlap, represented intervals,
mass, and coefficients, including when the initial fuel command is zero.

At held angular speed `omega`, the time conversion is:

```text
local pulse start = (start angle - starting IVC angle) / omega
pulse duration   = angular duration / omega
cycle duration   = 4*pi / omega
```

The initial fuel fraction is passed to `compile`. Later `fueling_01` values are
sampled at IVC and scale every pulse mass in the next train. Changes within a
cycle wait for the next IVC. `fuel_enabled=false` immediately cuts hydraulic
delivery over the current advance; already delivered fuel can continue burning.
Restoring fuel does not replay a missed pulse. A control supplied to an advance
applies from that frame's starting boundary, including IVC events inside it.

One main pulse and pilot/main use the same chamber inventory and ignition law.
Crank pulses may optionally set `minimum_duration_fraction_01` in `(0, 1]`.
The default 1 retains fixed-duration delivery. With a smaller value, duration is
`full_duration * (minimum + (1 - minimum) * fueling_01)`; mass is still exactly
`full_fuel_mass_kg * fueling_01`. Each cycle rebuilds its injection edges from the
latched fuel command, so gas and combustion split at the same hydraulic knots.
This simple metering law supplies a shorter light-load pulse without altering
full-load behavior.

This supports comparing injection schedules; it does not model a mechanical pump,
rail pressure, injector electronics, governor, or cold-start controller. Angular
duration converts at the selected held RPM. A common-rail calibration expressed
in seconds must supply the corresponding angular duration at each operating point.
Changing RPM during an existing session is unsupported.

## Thermodynamics and flow

One constant `R` and `gamma` describe all gas, products, and delivered fuel mass:

```text
cv = R / (gamma - 1)
cp = gamma * cv
U  = mass * cv * temperature
p  = (gamma - 1) * U / volume
```

Oxygen is a transported mass inventory. Consumption converts oxygen and fuel to
implicit products without removing total charge mass. The model does not provide
chemical species, variable heat capacities, evaporation enthalpy, or emissions.
Injected fuel brings an explicitly supplied sensible energy per kilogram; heat
release is applied once, from the combustion component's burned-fuel ledger.

Each valve uses a quasi-steady isentropic nozzle between the cylinder and its
reservoir. The higher-pressure side supplies upstream pressure `pu`, temperature
`Tu`, and composition. For downstream/upstream pressure ratio `r`:

```text
r_critical = (2 / (gamma + 1))^(gamma / (gamma - 1))
r_used     = max(r, r_critical)
mass flow  = A_effective * pu / sqrt(R*Tu)
             * sqrt(2*gamma/(gamma-1)
                    * (r_used^(2/gamma) - r_used^((gamma+1)/gamma)))
```

This follows the ideal compressible nozzle relations and sonic flow limit
described by [NASA Glenn](https://www.grc.nasa.gov/WWW/K-12/airplane/mflchk.html).
Using those relations for an effective engine-valve area is this model's declared
approximation; it does not establish a real valve's discharge coefficient.

Both published port-transfer signs are positive **into the cylinder**. Inflow
brings the boundary oxygen fraction and `cp*Tu` enthalpy. Outflow exports cylinder
oxygen and unburned-tracer fractions and cylinder `cp*T` enthalpy. Reverse intake
and reverse exhaust flow use these same rules; neither is clamped to zero.

Volume changes use the centered slider-crank geometry with the supplied bore,
stroke, connecting rod, and compression ratio. An adiabatic volume move applies
`U_new = U_old * (V_old/V_new)^(gamma-1)` and records `U_old-U_new` as gas work.
The instantaneous pressure torque is `(p-p_crankcase) * dV/dtheta`. Complete-cycle
work includes pumping; IMEP is complete-cycle gas work divided by swept volume.
Constant crankcase pressure contributes zero net work over the closed volume
trajectory. Neither work nor torque includes mechanical friction or accessories.

Wall exchange has an explicit constant conductance `H` and wall temperature `Tw`.
At fixed mass and volume it removes:

```text
Q_wall_out = (U - mass*cv*Tw) * (1 - exp(-H*dt/(mass*cv)))
```

The sign may be negative when the wall heats the gas. This is a lumped thermal
approximation, not a fitted diesel wall-heat correlation.

## Charge boundaries and unburned fuel

The reacting fuel train runs only during the closed-valve interval from IVC to EVO.
At EVO, any unprepared, premixed, or diffusion fuel is transferred to a nonreacting
unburned tracer without changing total gas mass or energy. The tracer is carried
through exhaust, reverse flow, and later cycles. It is not silently discarded or
counted as burned. At the next IVC a fresh combustion session arms the next injection
train; gas pressure, temperature, oxygen, and tracer remain continuous.

This explicit approximation ends combustion at EVO and excludes oxidation of
carryover in subsequent cycles. It is intended for warm, predominantly completed
closed-valve combustion. Starvation tests verify accounting outside that intended
operating region; they do not validate smoke, misfire recovery, or residual-fuel
chemistry. The reported tracer inventory and exported tracer keep the omitted
reaction visible.

For zero initial fuel, cumulative ledgers satisfy:

```text
mass - initial mass = intake mass + exhaust mass + injected fuel
oxygen - initial oxygen = intake oxygen + exhaust oxygen - consumed oxygen
U - initial U = intake enthalpy + exhaust enthalpy + fuel sensible energy
                + chemical heat - wall heat out - volume work out
injected fuel = burned fuel + reacting fuel + retained unburned tracer
                + unburned tracer exported through both ports
```

## Scheduling, integration, and failures

Each `advance` publishes one frame. The default physics clock is 20,000 Hz, with
eight gas substeps; integer frame/substep indices construct absolute endpoints.
The cycle clock is derived from an integer cycle index and the held speed. Neither
clock advances by repeatedly adding a floating duration. Arbitrary positive RPM
does not have to divide the frame rate.

The gas grid splits at represented hydraulic knees, valve boundaries, and IVC.
Its ordering is: adiabatic move to midpoint volume; exhaust exchange; intake
exchange; combustion heat/mass/oxygen; wall exchange; adiabatic move to endpoint
volume. The combustion component additionally splits its held-condition solve at
ignition. Thermodynamic feedback is resolved on the gas grid; the coupling is a
numerical splitting approximation, not an exact solution of the coupled equations.

Endpoint events are dispatched after the preceding interval. At EVO the fuel
inventory is retired before subsequent open-valve flow. At IVC the completed cycle
is published before the next train is armed. Shared valve close/open timestamps
are one split boundary. During overlap, exhaust and intake exchange execute in
the declared order on each gas substep. Refinement resolves this splitting error;
neither port takes priority across the whole publication frame. Arriving at
injection start delivers zero mass until time advances.

Each explicit flow step must transport at most 2% of current cylinder mass and
2% of current internal energy as enthalpy. Exceeding either bound returns
`integration_step_too_large`; it does not clip mass flow. A finer gas grid may be
needed. The bound guards resolution, and is not a stability or accuracy proof.
Grid refinement remains necessary for a proposed operating point.

Advance allocates no memory and works on a tentative copy. Invalid controls,
combustion errors, underresolved flow, nonfinite/nonpositive state, and exhausted
clock representation leave all session state unchanged. No partial frame or cycle
is published. Identical frame/control sequences preserve exact state across
callback grouping; changing the integration grid is a convergence question.

## Verification and remaining integration

[`diesel_cylinder_v1_test.cpp`](../../tests/diesel_cylinder_v1_test.cpp) exercises:

- closed-port motoring against the independent `p*V^gamma` solution;
- sonic flow against the independent Mach-one nozzle equation and outgoing enthalpy;
- reverse intake flow with unburned-fuel transport;
- overlapping valve flow through the cylinder, conservation, and gas-grid convergence;
- repeated ignition, retained exhaust, settling, and equal-air load changes;
- oxygen starvation with conserved exported and retained unburned fuel;
- fuel cut, no replay, and IVC sampling of later fuel commands;
- pilot/main scheduling across nonintegral cycle/frame boundaries and callback regrouping;
- gas-grid convergence and transactional failure after partial internal work.

```bash
cmake -S . -B build -DCRANKWAVE_BUILD_TESTS=ON
cmake --build build --target crankwave_diesel_cylinder_v1_tests
ctest --test-dir build -R '^simulation.diesel_(combustion|cylinder)_v1$' --output-on-failure
mkdir -p artifacts/diesel-foundation
build/tests/crankwave_diesel_cylinder_v1_tests artifacts/diesel-foundation/repeated-cycles.csv
```

The optional CSV contains three synthetic load cases over ten cycles, including
frame pressure, temperature, oxygen, fuel inventory, heat, work, and signed port
mass transfers. It is a numerical diagnostic, not acoustic pressure or a render.

The [mechanical diesel sound path](MECHANICAL_DIESEL_SOUND.md) supplies an explicit
experimental authoring/bake adapter, fuel-aware source grid, and mechanical/exhaust
presentation. General EngineSession/C API integration, physical calibration, and
real-engine listening validation remain separate work. These numerical tests alone
make no acoustic fidelity claim.
