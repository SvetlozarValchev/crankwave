# Pristine engine-sim separate-intake oracle

Status: source-audited parity authority for headless slice 10 H2

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`, read from the repository-local
object database at `../engine-sim`. Every citation below is pinned to that commit;
the authority must be inspected with
`git -C ../engine-sim show <authority>:<path>`, not from the neighboring
worktree's current branch or uncommitted files.

## Executed identity and mapping

Pristine discovers intakes through cylinder references. `EngineNode::buildEngine()`
inserts every cylinder's `IntakeNode *` into a `std::set`, uses the number of unique
node pointers as `Engine::Parameters::intakeCount`, allocates that many runtime
`Intake` objects, and maps each unique node pointer to one element of the runtime
array. See
[`scripting/include/engine_node.h:25-50`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/include/engine_node.h#L25-L50),
[`scripting/include/engine_node.h:53-67`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/include/engine_node.h#L53-L67),
and
[`src/engine.cpp:56-82`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/engine.cpp#L56-L82).
`EngineContext` retains the exact node-pointer-to-runtime-pointer association; lookup
does not copy or merge intake state
([`scripting/src/engine_context.cpp:49-59`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/src/engine_context.cpp#L49-L59)).

The source identity rule is therefore:

- cylinders referencing the same authored intake node share one runtime `Intake` and
  one plenum state;
- cylinders referencing distinct authored intake nodes receive distinct runtime
  `Intake` objects, even when every parameter is equal; and
- pristine's array index is assigned by iteration over a set of raw pointers. That is
  an implementation mapping, not a portable authored semantic order.

The clean-room contract preserves the first two rules but replaces raw pointer
identity/order with explicit stable `IntakeId` values in canonical semantic-ID order,
independent of authored array order. Equal parameters must never cause two declared
intake identities to collapse.

## Exact cylinder binding

Each pristine bank-local cylinder stores its selected `IntakeNode *` alongside its
piston, rod, exhaust, and ignition wiring
([`scripting/include/cylinder_bank_node.h:21-29`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/include/cylinder_bank_node.h#L21-L29)).
During generation, the node is resolved through `EngineContext` and installed in the
matching cylinder slot of that bank's `CylinderHead`
([`scripting/include/cylinder_bank_node.h:96-110`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/include/cylinder_bank_node.h#L96-L110),
[`src/cylinder_head.cpp:83-91`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/cylinder_head.cpp#L83-L91)).

The binding is physically consumed. `CombustionChamber` retrieves its own cylinder's
intake, takes that intake's runner restriction and runner length during initialization,
and later connects that intake's plenum gas system to the cylinder's runner
([`src/combustion_chamber.cpp:61-65`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/combustion_chamber.cpp#L61-L65),
[`src/combustion_chamber.cpp:78-92`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/combustion_chamber.cpp#L78-L92),
[`src/combustion_chamber.cpp:251-277`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/combustion_chamber.cpp#L251-L277)).
Separate-intake execution must consequently retain an explicit cylinder-to-intake
binding all the way through resolved topology and runtime lane selection.

## Global command, lane-local response

Pristine has one engine throttle controller. For the direct linkage, caller command
`u` and linkage exponent `gamma` produce the internal closedness scalar

```text
closure = 1 - pow(u, gamma)
```

and `DirectThrottleLinkage::update()` sends that scalar to the engine
([`src/direct_throttle_linkage.cpp:20-28`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/direct_throttle_linkage.cpp#L20-L28)).
`Engine::setThrottle()` broadcasts the same scalar to every runtime intake
([`src/engine.cpp:147-153`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/engine.cpp#L147-L153)).
There is no independent per-intake throttle command in this authority.

The response to that common command is nevertheless lane-local. Each `Intake` owns
its plenum and atmosphere gas systems plus its own main, idle-bypass, and runner
restriction coefficients, idle plate position, geometry, and velocity decay
([`include/intake.h:8-37`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/include/intake.h#L8-L37),
[`src/intake.cpp:24-54`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/intake.cpp#L24-L54)).
For lane `i`, pristine evaluates

```text
plate_i           = idle_plate_i * closure
main_multiplier_i = cos(pi * plate_i / 2)
effective_main_k_i = main_multiplier_i * main_restriction_k_i
```

before applying that lane's unattenuated idle-bypass restriction and plenum velocity
decay
([`include/intake.h:48-52`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/include/intake.h#L48-L52),
[`src/intake.cpp:60-100`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/intake.cpp#L60-L100)).
The cylinder path separately uses the bound intake's runner restriction and applies
that same intake's decay to its runner state
([`src/combustion_chamber.cpp:259-305`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/combustion_chamber.cpp#L259-L305)).

The `.mr` intake input named `throttle_gamma` is not a lane-local controller:
`IntakeNode` reads it into `m_throttleGammaUnused`, explicitly marked deprecated
([`scripting/include/intake_node.h:30-52`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/scripting/include/intake_node.h#L30-L52)).
The executed gamma belongs to the engine's throttle object. The clean-room model must
not invent per-intake gamma behavior from this dead source field.

## Fluid-step ordering

Within every pristine fluid substep, execution order is exactly:

```text
all exhaust systems, in runtime exhaust order
all intakes, in runtime intake order
all cylinders, in runtime cylinder order
```

The loops are adjacent and ordered that way in
[`src/piston_engine_simulator.cpp:302-318`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/piston_engine_simulator.cpp#L302-L318).
This order is observable because each operation mutates gas state. The clean-room
runtime must retain exhaust-to-intakes-to-cylinders ordering, with intakes visited in
stable `IntakeId` order. It must not interleave each intake immediately before its
bound cylinders.

## Aggregate pressure and GUI behavior

`Engine::getManifoldPressure()` sums every intake plenum's absolute pressure and
divides by the intake count
([`src/engine.cpp:305-312`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/engine.cpp#L305-L312)).
The VTEC selector consumes that arithmetic mean as one engine-global predicate
([`src/vtec_valvetrain.cpp:61-66`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/vtec_valvetrain.cpp#L61-L66)).
Separate intakes therefore do not select VTEC independently. The clean-room mean must
sum stable intake order and divide by the exact lane count; a one-intake engine remains
the identity case.

The throttle-plate graphic is different and deliberately non-authoritative.
`Engine::getThrottlePlateAngle()` reads only `m_intakes[0]`, and `ThrottleDisplay`
draws that returned angle
([`src/engine.cpp:159-161`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/engine.cpp#L159-L161),
[`src/throttle_display.cpp:93-104`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/throttle_display.cpp#L93-L104)).
That first-intake shortcut is GUI presentation only; it is not evidence for a
singleton physics model or for collapsing other intake lanes.

## Audio authority boundary

Pristine does not synthesize a direct intake source. Its synthesizer input-channel
count equals the number of exhaust systems
([`src/simulator.cpp:204-212`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/simulator.cpp#L204-L212)).
`PistonEngineSimulator::writeToSynthesizer()` derives each source from exhaust-runner
pressure, delays and scales it through the selected exhaust system, accumulates only
an exhaust staging buffer, and submits that buffer to the synthesizer
([`src/piston_engine_simulator.cpp:370-412`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/piston_engine_simulator.cpp#L370-L412)).

Separate intake state can still change the exhaust sound indirectly through cylinder
filling, combustion, and exhaust flow. It does not create an intake microphone or
intake bus. Consequently there is no pristine intake-source WAV oracle and no pinned
upstream multi-intake WAV to reproduce byte for byte. H2 uses the pinned source graph
and consumption paths as its physical/topological authority, plus deterministic
clean-room differential audio gates. Those gates are not a claim of byte-identical
audio with pristine `engine-sim`.

## Shovelhead/TRX520 diagnostic fixture

The pinned Shovelhead source declares one intake and binds that same node to both the
front and rear cylinders
([`assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr:86-94`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr#L86-L94),
[`assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr:121-140`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr#L121-L140)).
The pinned TRX520 source supplies a second source-backed intake tuning point
([`assets/engines/atg-video-1/01_honda_trx520.mr:65-72`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/assets/engines/atg-video-1/01_honda_trx520.mr#L65-L72)).
Both inherit the pristine default 4 in runner and `k_carb(200)` runner restriction
([`es/objects/objects.mr:483-495`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/es/objects/objects.mr#L483-L495)).

| Intake value | Shovelhead source | TRX520 source |
|---|---:|---:|
| Plenum volume | 1.5 L | 1.5 L |
| Plenum cross-section | 10 cm2 | 10 cm2 |
| Main restriction | `k_carb(100)` | `k_carb(100)` |
| Idle-bypass restriction | `k_carb(0)` | `k_carb(0)` |
| Runner length | 4 in | 4 in |
| Runner restriction | `k_carb(200)` | `k_carb(200)` |
| Idle plate position | 0.991 | 0.993 |
| Velocity decay | 1.0 | 0.5 |

`k_carb(x)` means the coefficient calibrated to `x` SCFM at 1.5 inHg pressure drop,
1 atm upstream pressure, and 25 C in pristine's gas model
([`src/gas_system.cpp:173-181`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/gas_system.cpp#L173-L181)).
The resolved diagnostic values are `0.0015 m3`, `0.001 m2`, `0.1016 m`,
`main_k = 0.003185063142548517`, `idle_k = 0`, and
`runner_k = 0.006370126285097034`.

H2's representative fixture has three controlled variants:

- **A, shared source:** the source-faithful Shovelhead intake `intake` is bound to both
  cylinders, so both exchange gas with one plenum state.
- **B, equal split:** `intake` and `intake.rear` are distinct runtime identities but
  both have the complete Shovelhead parameter set.
- **C, differentiated split:** geometry and all three restrictions remain equal to B;
  only `intake.rear` takes the TRX520 idle plate `0.993` and velocity decay `0.5`.

Canonical clean-room semantic-ID order is `intake` as `IntakeId{1}`, then
`intake.rear` as `IntakeId{2}`. Each owns a distinct plenum, main-throttle edge, and
idle-bypass edge. The front cylinder binds ID 1 and the rear cylinder binds ID 2. This
stable semantic order is a clean-room contract decision replacing pristine's
raw-pointer set order; it is not attributed to the `.mr` runtime.

The bounded diagnostic runs at 1,500 rpm with direct-linkage gamma `1` and command
`u = 0.25`. RPM does not enter this plate calculation. Using pristine's binary64
constant `pi = 3.14159265359`
([`include/constants.h:6`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/include/constants.h#L6)):

```text
closure = 1 - pow(0.25, 1) = 0.75

front plate = 0.991 * 0.75 = 0.74325
front main multiplier
    = cos(3.14159265359 * 0.74325 / 2)
    = 0.39245751759429343

rear C plate = 0.993 * 0.75 = 0.74475
rear C main multiplier
    = cos(3.14159265359 * 0.74475 / 2)
    = 0.39028927288738607
```

In B, both lanes use the front value `0.39245751759429343`. In C, the rear main
multiplier is lower by `0.002168244706907352` (about `0.5524788313900499%` of the
front value), before multiplying the common `k_carb(100)` coefficient. These are exact
diagnostic plate/multiplier expectations, not target molar-flow values: actual flow
also depends on evolving pressure, temperature, composition, and velocity. The
`1.0` versus `0.5` decay difference likewise changes state over time and is not a
second static restriction multiplier: pristine multiplies the pressure-derived
momentum correction by this beta rather than multiplying existing velocity directly
([`src/gas_system.cpp:283-319`](https://github.com/ange-yaghi/engine-sim/blob/85f7c3b959a908ed5232ede4f1a4ac7eafe6b630/src/gas_system.cpp#L283-L319)).

## H2 acceptance boundary

- H2a changes singleton resolved/runtime intake storage into ordered intake lanes while
  retaining the one-intake compiler admission gate. Every previously admitted engine,
  including the BMW authority, must retain byte-identical PCM.
- H2b lifts only the exact-one-intake gate needed by this fixture. A and B must differ:
  two equal-valued but identity-distinct plenums must not collapse into the one shared
  state. Repeated B renders must be byte-identical. B and C must differ, proving that
  the rear lane consumes its own plate and decay rather than a representative first or
  last intake profile.
- Resolved identities, topology objects, and cylinder bindings are checked separately
  from audio so a coincidental PCM difference cannot conceal wrong graph ownership.
- The throttle command remains engine-global, VTEC continues to use arithmetic-mean
  manifold pressure, fluid ordering remains exhausts then intakes then cylinders, and
  no intake audio source is added.
- Multiple crankshafts and other topology families remain separate checkpoints. There
  is one current contract only; no singleton-intake alias or compatibility decoder is
  introduced.
