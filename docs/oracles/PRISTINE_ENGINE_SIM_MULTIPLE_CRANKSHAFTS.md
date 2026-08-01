# Pristine engine-sim multiple-crankshaft oracle

This note freezes the source behavior used to design the clean-room
multiple-crankshaft checkpoints. The inspected upstream tree is
`../engine-sim` at commit `35dac075491addbdd7a58663a9d92c75480df4ce`.
Local asset and build changes in that checkout are not authority for this audit.

## Authored identity and construction order

Pristine's scripting graph permits an engine to receive more than one crankshaft.
`EngineNode::addCrankshaft()` appends the referenced node to an ordered vector, and
generation allocates that count and generates each crankshaft into the corresponding
runtime array position. Each crankshaft owns its own throw, crank and flywheel mass,
moment of inertia, position, TDC reference, friction torque, and ordered rod journals.

The clean-room JSON keeps that physical object identity explicit:

- `engine.crankshafts[]` remains authored order;
- every direct journal owns one `crankshaft` reference;
- a master-rod cylinder inherits the crankshaft reached through its direct-root master;
- `engine.output_crankshaft` selects the public output explicitly.

The explicit output reference is an intentional greenfield normalization. Pristine
implicitly makes array element zero the output shaft; this project does not encode that
choice as an undocumented array-position convention.

Source authority:

- `scripting/include/engine_node.h:44-51,84-100,134-136`;
- `scripting/include/crankshaft_node.h:21-40,44-53`;
- `scripting/include/cylinder_bank_node.h:35-46,71-97`.

## Cylinder-to-crank binding

A direct connecting rod is generated with the exact runtime crankshaft reached from
its referenced rod-journal node. A slave rod is first generated through its journal
node, then `connectRodAssemblies()` replaces its crankshaft pointer with the master
rod's crankshaft. Thus crank ownership is structural and is not inferred from cylinder
or bank order.

At solver construction, each direct rod's big-end constraint is attached to that
crankshaft body. A slave big-end constraint attaches to its master rod body instead.

Source authority:

- `scripting/include/cylinder_bank_node.h:35-46,71-93`;
- `src/piston_engine_simulator.cpp:195-217`.

## Output shaft and rigid group behavior

Pristine defines crankshaft array element zero as the output shaft. RPM, speed,
rotation direction, ignition timing, the starter, dynamometer, transmission clutch,
and several global cycle-angle consumers observe that shaft.

Each authored crankshaft still receives a separate rigid body, inertia, fixed-position
constraint, and friction constraint. Every non-output shaft is joined to the output
shaft by an unbounded clutch constraint. After each solver step, pristine additionally
copies the output shaft angle into every crank body under the comment `Correct drift
(temporary hack)`.

The resulting executed model is therefore one co-phased, 1:1 rigid crank group. It is
not a model of independently rotating shafts, gears, chains, phase offsets, compliance,
or backlash. Secondary crankshaft throw and journal phase affect cylinders bound to
that shaft; secondary inertia and friction affect the shared group dynamics.

Source authority:

- `src/engine.cpp:143-145,406-417`;
- `src/piston_engine_simulator.cpp:103-153,242-248`;
- `src/simulator.cpp:121-129`;
- `scripting/include/ignition_module_node.h:26-34`;
- `src/transmission.cpp:54`.

## Geometry boundary

Pristine exposes `position_x` and `position_y` per crankshaft and installs each fixed
body constraint at that location. Its shipped engine assets, however, do not provide a
multi-crank audio oracle, and the audited canonical assets use co-centered cranks.

The first clean-room execution checkpoint therefore admits co-centered crankshafts
only. Offset crank axes, independently rotating shafts, and geared or compliant
couplings require separate source-backed models and fixtures; they are not silently
approximated here.

## Incremental acceptance boundary

Multiple crankshafts are admitted in three narrow checkpoints:

1. stable crankshaft identities, explicit output selection, and resolved cylinder
   bindings are carried through the contract while execution remains gated to exactly
   one crankshaft;
2. a synthetic two-crank fixture is admitted only under prescribed kinematics, proving
   that cylinder lanes and canonical identity do not collapse;
3. dynamic modes aggregate the rigid group's authored inertia and friction in the same
   physical direction as pristine, with a separate dynamics fixture.

The prescribed A/B structural fixture should keep journal phase, cylinder geometry,
and aggregate physical values unchanged while moving one direct journal to a second
co-centered crankshaft. Its PCM may be byte-identical; its resolved identity must not
be. A later C fixture changes only secondary inertia or friction to prove that dynamic
execution consumes the second crank rather than merely serializing it.

No checkpoint claims a pristine multi-crank recording comparison: the audited
upstream assets provide none. Existing one-crank BMW PCM/WAV bytes remain the
non-regression authority throughout this work.
