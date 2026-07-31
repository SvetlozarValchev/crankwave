# Pristine engine-sim drivetrain oracle

Status: authoritative slice-12 parity boundary

Identified source:
`ange-yaghi/engine-sim@85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

This record freezes the meaningful vehicle and transmission behavior that the
clean-room runtime must reproduce. It records behavior and equations; it does not
authorize importing the upstream general-purpose rigid-body solver or its GUI.

## Executable source surface

The pristine vehicle has exactly six authored quantities:

- mass;
- drag coefficient;
- frontal area;
- differential ratio;
- tire radius;
- constant rolling-resistance force.

The pristine transmission has a maximum clutch torque and an ordered list of forward
gear ratios. Gear index `-1` is neutral. Clutch pressure is normalized with `0` fully
disengaged and `1` fully engaged.

These fields are consumed in:

- `include/vehicle.h:8-15` and `src/vehicle.cpp:20-26`;
- `include/transmission.h:10-14` and `src/transmission.cpp:26-30`.

There is no service-brake capacity, brake control, or brake constraint in the
identified source. The GUI's Space key only increases the clutch-pressure smoothing
time constant (`src/engine_sim_application.cpp:918-925`); it is not a vehicle brake.
Service braking in this product is therefore a declared greenfield operating-bench
extension, not pristine parity and not an inferred use of rolling resistance.

## Vehicle representation and road load

Pristine represents the translating vehicle as a virtual rotating body coupled to the
crank by a clutch constraint. For virtual inertia `I_v`, virtual angular speed
`omega_v`, and vehicle mass `m`, it reports:

```text
E_v = 0.5 * I_v * omega_v^2
speed = sqrt(2 * E_v / m)
```

(`src/vehicle.cpp:37-45`). The result is nonnegative even if the virtual shaft rotates
backwards. A linear force is converted to virtual-shaft torque as:

```text
torque_virtual = sqrt(I_v / m) * force_linear
```

(`src/vehicle.cpp:47-50`). Travelled distance integrates `speed * dt`
(`src/vehicle.cpp:29-31`).

At 25 degrees Celsius and one atmosphere, pristine fixes air density to:

```text
rho = AirMolecularMass * 1_atm / (R * 298.15_K)
```

It then forms the passive forward road load:

```text
F_road = rolling_resistance + 0.5 * rho * speed^2 * Cd * frontal_area
```

and constrains the virtual braking torque to `[-sqrt(I_v/m) * F_road, 0]`
(`src/vehicle_drag_constraint.cpp:47-58`). Under pristine's normal clockwise shaft
convention, the one-sided bound opposes forward motion without using passive road load
to drive the car backwards through rest. Scenario ambient conditions do not affect
this hard-coded density.

The clean runtime may store signed linear speed directly. For a selected forward gear
ratio `g`, differential ratio `d`, and tire radius `r`, the exact admitted reduction is:

```text
omega_vehicle_at_crank = speed * g * d / r
I_vehicle_at_crank = mass * (r / (g * d))^2
wheel_force = driveline_torque_at_crank * g * d / r
```

This is algebraically equivalent to pristine's virtual body while avoiding a
gear-dependent state representation. It also preserves linear vehicle speed across a
gear change by construction.

## Gear changes and clutch

Selecting forward gear `g` makes pristine's virtual inertia:

```text
f = tire_radius / (differential_ratio * g)
I_new = vehicle_mass * f^2
```

It preserves the virtual body's kinetic energy and rotation sign while replacing its
inertia (`src/transmission.cpp:59-81`). Because reported linear speed is derived from
the same energy, the gear change preserves vehicle speed. Selecting neutral leaves the
stored vehicle energy alone but disables clutch torque. There is no reverse gear;
shifts are instantaneous and do not automatically disengage the clutch.

The clutch constrains crank angular speed and the crank-referred vehicle-shaft speed
toward equality with Jacobian `[-1, +1]` and zero velocity bias
(`simple-2d-constraint-solver/src/clutch_constraint.cpp:18-46`). Its signed torque
bounds are:

```text
neutral:       [0, 0]
forward gear:  [-maximum_clutch_torque * clutch_position,
                 maximum_clutch_torque * clutch_position]
```

(`src/transmission.cpp:33-41`). The rigid solver converts those torque limits to
per-step impulse limits. There is no separate static/kinetic clutch curve, plate
temperature, gearbox efficiency, or additional transmission-loss model in the
identified source. Pristine also does not publish clutch reaction torque or slip;
those values are clean product telemetry derived from the real coupling state.

For the clean reduced two-inertia step, after predicting the unconstrained engine and
vehicle velocities, the torque on the engine needed to eliminate clutch slip over one
step is:

```text
T_lock = (omega_vehicle_predicted - omega_engine_predicted)
       / (dt * (1 / I_engine + 1 / I_vehicle_at_crank))

T_clutch_on_engine = clamp(
    T_lock,
    -maximum_clutch_torque * clutch_position,
     maximum_clutch_torque * clutch_position)
```

The equal-and-opposite driveline torque updates the vehicle. This is the one-degree
clean reduction of the bounded pristine clutch constraint, not a new slip curve or
friction experiment.

## Source operation order

Pristine installs the transmission clutch, vehicle virtual mass, passive road-load
constraint, dyno, and starter in the same rigid-body system
(`src/piston_engine_simulator.cpp:97-106,175-181`). Each simulation tick first solves
that system, then updates the engine, travelled distance, and clutch torque bounds
(`src/simulator.cpp:95-112`). As a result, control values written by the GUI immediately
before a tick are consumed by the next constraint solve, while transmission torque
bounds refreshed after a solve apply on the following solver step.

The optimized pristine solver treats these as velocity constraints; the `ks` and `kd`
members present on the individual clutch and road-load classes do not create a spring
or damper in the selected solver. On the very first pristine tick, the clutch object's
default unbounded limits are solved before neutral installs zero bounds. That is an
initialization defect, not desired parity behavior.

The clean runtime does not need to reproduce that defect, unrelated solver drift, or
GUI polling. It must declare one causal left-boundary operation order and keep control
projection, telemetry, and offline/WASM execution identical at that boundary.

## Slice-12 boundary

Slice 12 will implement this in separately reviewable changes:

1. forward vehicle state plus passive rolling and aerodynamic load;
2. an explicitly parameterized, one-sided service brake as a greenfield extension;
3. neutral and ordered forward gears;
4. the bounded clutch coupling above;
5. authored neutral, launch, shift, and fifth-gear checks using the unchanged accepted
   engine, excitation, routing, conditioning, convolution, and mastering path.

The first listening candidate must not change the engine gas model, mechanical-loss
authority, exhaust source, presentation, or accepted held-dyno behavior. A physically
incorrect result is fixed within this slice; it is not deferred to a future fidelity
phase.
