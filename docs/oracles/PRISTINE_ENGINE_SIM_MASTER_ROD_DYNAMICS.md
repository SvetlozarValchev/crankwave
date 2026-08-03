# Pristine engine-sim master-rod dynamics oracle

Status: source oracle frozen; public one-level master-rod FreeEngine accepted at
`2e5d70d` and HeldDyno accepted at `eb26214` on 2026-08-03; FreeVehicle remains closed.

This note freezes the source behavior that governs the clean-room one-level
master/slave dynamics checkpoint. The behavioral source pin is pristine engine-sim
commit `85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`. Later local commits in
`../engine-sim` do not broaden this scope.

## Executed source model

Pristine does not treat a radial master/slave mechanism as prescribed geometry. Each
piston and each connecting rod is a separate planar rigid body with piston mass, rod
mass, and authored rod moment of inertia. A direct rod's big end is constrained to its
crank journal. A slave rod's big end is constrained to a moving pin on its direct-root
master rod. Combustion force and cylinder-wall friction applied to every piston are
therefore transmitted through slave rod, master rod, and crank constraint reactions.

The starter, dynamometer, transmission, and vehicle constraints act on the same output
crank and do not bypass this articulated mechanism. The shipped radial-five and
radial-nine assets are one-level, one-crank examples; radial-five also authors a
vehicle and 1:1 transmission.

Source authority:

- `scripting/include/engine_node.h` and `cylinder_bank_node.h`: ordered construction,
  slave-to-master binding, and inherited crank ownership;
- `src/piston_engine_simulator.cpp`: rigid bodies, point constraints, master-first
  placement, starter, dyno, and transmission connection;
- `src/combustion_chamber.cpp`: pressure force and one-step-lagged wall friction;
- `src/engine.cpp`: default solver selection;
- pinned `atg_scs` commit `e009f4ff...`: diagonal rigid-body mass matrices, point-link
  Jacobians, NSV integration, and bounded Gauss-Seidel constraint projection.

There is no source scalar equivalent-inertia formula for a master rod. Its
configuration-dependent inertia emerges from the complete constrained rigid-body mass
matrix.

## Full-cycle geometry authority

Pristine `src/engine.cpp` also contains a separate displacement helper that probes a
radial linkage at 1,000 half-open angular samples. That helper supports derived engine
information; it is not the geometry executed by the rigid-body mechanics, and its
private rod-placement arithmetic is not identical to the runtime placement path. It
must therefore not become the clean-room chamber, stroke, or piston-travel oracle.

The clean-room authority is the already source-matched point evaluator derived from
`PistonEngineSimulator::placeCylinder` and
`ConnectingRod::getRodJournalPositionGlobal`. A direct root has analytic dead-center
extrema. A slave is admitted only when independent 4,096- and 8,192-point probe
lattices agree that its continuous `2*pi` piston path has exactly one maximum and one
minimum; the two analytic-derivative roots are then refined between adjacent binary64
angles. Missing, additional, tangent, or numerically ambiguous stationary points fail
closed rather than being flattened into a nominal twice-crank-throw stroke.

The immutable mechanism plan retains each cylinder's resolved position and chamber
extrema, swept stroke, swept displacement, and piston-axis path length per crank
revolution. At the geometry-derivation checkpoint, that subslice only derived and
source-bound those facts; it did not yet replace the public nominal displacement,
cycle accountant, or scenario inertia. The subsequent public FreeEngine closure is
recorded below.

## Clean one-degree-of-freedom reduction

The greenfield runtime retains its ideal one-degree-of-freedom crank coordinate rather
than importing pristine's iterative constraint solver. For every root and slave it
must derive planar big-end, wrist, and rod-COM positions `B`, `W`, and `G`; their first
and second crank-angle derivatives; and rod-angle derivatives `beta'` and `beta''`.
No nominal slave stroke may be reconstructed.

The exact kinetic-energy coefficient is:

```text
M(theta) = I_crank_group + I_attached
         + sum_pistons(mp * |W'|^2)
         + sum_rods(mr * |G'|^2 + Ir * beta'^2)

M'(theta) = 2 * sum_pistons(mp * W' dot W'')
          + 2 * sum_rods(mr * G' dot G'' + Ir * beta' * beta'')
```

Each piston and rod is counted exactly once. Slave pins are massless. Crank and
flywheel mass are not re-derived into rotational inertia; the authored crank inertia
is consumed once. The existing reduced crank law then remains:

```text
Q = M(theta) * alpha + 0.5 * M'(theta) * omega^2
```

Dynamic execution consumes `M(theta)` directly. Scenario preparation also needs one
constant engine-only inertia baseline, so the articulated mechanism owns a separate
cycle-mean method rather than reusing the centered-slider result. It evaluates one
compiled mechanism at 4,096 ascending midpoint samples over the canonical body-angle
interval `2*pi`, reuses one exact-size state scratch, and accumulates piston
translation, rod translation, and rod rotation separately. Each component sum is
divided once by 4,096; authored crank inertia is then added once in written order.
Attached scenario inertia, instantaneous total inertia, and `M'` are not averaged.

Sampling canonical body angle makes the result independent of the arbitrary crank-TDC
coordinate reference. The centered-slider method, identity, and arithmetic remain a
distinct direct-mechanism path. As with the full-cycle geometry record, the radial
cycle mean is initially derived and source-bound without opening a public dynamic mode
or changing torque, gas, capture, or audio execution.

## Coupled wall reactions

The direct-cylinder wall-reaction shortcut is not valid for the root of a master/slave
mechanism. Slave branches must be solved leaf-first. Each slave piston and rod yields
its big-end reaction; the equal and opposite load is applied at the corresponding pin
on the master rod. Every slave-pin force participates in the master rod's force and
moment balance before the master-piston wall reaction is resolved.

The magnitude of each resolved wall reaction is retained for the next physics step.
The following step applies pristine's existing Stribeck/Coulomb/viscous piston-friction
law against that retained magnitude and the current signed slider velocity. There is
no master-pin, rod-bearing, or extra radial friction term.

## Ordering and accepted idealization

Pristine advances rigid mechanics before its gas substeps, so chamber pressure and
wall reaction are left-boundary, one-step-lagged mechanics inputs. The clean-room
dynamic runtime preserves that causal boundary.

Pristine's solver is an approximate velocity projection with a bounded iterative
constraint solve. The accepted clean-room target is the corresponding ideal analytic
one-degree-of-freedom mechanism, not byte reproduction of solver drift or constraint
error. The existing full-cycle geometry certificate remains mandatory.

## Scope boundary

The complete parity checkpoint is scoped to one-crank, one-level master/slave dynamics
for the same FreeEngine, HeldDyno, and FreeVehicle families already owned by the dynamic
crank runtime. Prescribed radial execution remains intact. The first subsequent-status
record below closes FreeEngine only; the later HeldDyno closure is a separate gate and
does not claim FreeVehicle admission.

The following are not implied by pristine's shipped evidence and remain closed:

- nested or deeper master rods;
- master/slave mechanisms combined with multiple cranks;
- unequal secondary-crank TDC semantics or offset crank axes;
- independent, geared, counter-rotating, compliant, or backlash-coupled shafts; and
- clean-room-only multi-crank HeldSpeed, LoadTargetHeld, or InertialDyno modes.

Pristine's direct multi-crank implementation is only a co-phased 1:1 rigid group.
That meaningful direct-journal subset is already complete in this project; no new
multi-crank implementation is part of this checkpoint.

## Subsequent public FreeEngine closure

Commit `2e5d70d` opens only certified one-crank, one-level master-rod FreeEngine. The
public resolver selects the articulated cycle-mean inertia baseline, the engine profile
publishes complete operating torque and equivalent-inertia capability, and the runtime
uses the exact articulated `M(theta)`/`M'(theta)` reduction plus leaf-first coupled wall
reactions. Fixed warm preparation uses the per-cylinder-piston-travel Chen--Flynn
accountant and discards it at dynamic release; released motion continues under source
crank friction and the one-step-lagged piston-wall law. Public finite capture and
`EngineSession` complete through the unchanged gas, excitation, routing, IR, and
mastering path.

Master-rod HeldDyno and FreeVehicle remain rejected by the authored and resolved mode
gates. They are the remaining public dynamic families for this checkpoint, in that
order. Nested, multi-crank-master-rod, offset, geared, and compliant cases remain closed
under the scope boundary above.

The exact closure evidence is:

- The prescribed topology/byte fixture is `80 ms`, `46,698` bytes, and has SHA-256
  `58394f71ed58ee5af4434745e6f266c2cac7db33503848decbd61b9e8a9f0985` at
  `artifacts/listening/radial-free-engine-prescribed-control-2e5d70d/audio/master.engine.audition.wav`.
  Its short audition-format container is not a human-listening reference.
- The dynamic radial FreeEngine audition is `4.700 s`, `2,707,850` bytes, and has
  SHA-256 `6e6db58c74313634a7490326cab88f0f68763607dcb0bfe104bce6f8096888c` at
  `artifacts/listening/radial-free-engine-candidate-2e5d70d/audio/master.engine.audition.wav`.
  The user accepted it without a requested correction on 2026-08-03.
- The direct BMW regression guard is `15.000 s`, `8,640,688` bytes, and has its already
  accepted held-dyno SHA-256
  `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496` at
  `artifacts/listening/radial-free-engine-direct-guard-2e5d70d/audio/master.engine.audition.wav`.
  The user also confirmed that it still sounds correct.

This acceptance closes the FreeEngine subslice only. Master-rod HeldDyno must be opened,
regression-guarded against the direct BMW, rendered at a meaningful listening duration,
and accepted before FreeVehicle begins.

## Subsequent public HeldDyno closure

Commit `eb26214` opens certified one-crank, one-level master-rod `HeldDyno` through the
public resolver, finite capture, and `EngineSession`. It reuses the accepted
articulated mechanism state, `M(theta)`/`M'(theta)`, causal torque input, coupled wall
reactions, gas, excitation, routing, IR, and mastering paths. The bounded signed dyno
operation is named separately from the direct centered-slider composition:

- method ID: `bounded-held-dyno-speed-constraint-one-level-master-rod-v1`;
- version: `1`; and
- canonical descriptor SHA-256:
  `71b511ed6c5c2c29225cd96645e8c117dea4093cf965c22a76358f220b2bbe8b`.

The acceptance evidence is:

- The prescribed control is `5.500 s`, 192 kHz mono PCM24, with audition SHA-256
  `e446c0fd0f9348877dcdd59b32f59152912b13a68ecbc824f83881b3c9c599a9` at
  `artifacts/listening/radial-held-dyno-prescribed-control-eb26214/audio/master.engine.audition.wav`.
- The physical HeldDyno pull/lift candidate is `5.500 s`, 192 kHz mono PCM24, with
  audition SHA-256
  `e193d2e981a5d432928aa8596b6730df37c9cd6aa680332479d77eb24a636c72` at
  `artifacts/listening/radial-held-dyno-candidate-eb26214/audio/master.engine.audition.wav`.
  The user accepted it as sounding good on 2026-08-03.
- The direct BMW guard remains byte-exact at its already accepted audition SHA-256
  `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496` at
  `artifacts/listening/radial-held-dyno-direct-guard-eb26214/audio/master.engine.audition.wav`.

This closes the HeldDyno subslice only. Master-rod `FreeVehicle` remains rejected and
is the next public dynamic-family gate. Nested, multi-crank-master-rod, offset, geared,
and compliant cases remain closed under the scope boundary above.
