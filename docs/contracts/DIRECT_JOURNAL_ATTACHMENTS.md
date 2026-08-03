# Journal attachment graph

Status: topology sub-slice 10C2b8 preserved as history; certified one-crank,
one-level master-rod dynamics now execute through FreeEngine, HeldDyno, and
FreeVehicle, with the family accepted at `11d5853` on 2026-08-03

## Subsequent status

This document freezes the original topology sub-slice boundary. Commit `2e5d70d`
subsequently opened only the certified one-level master-rod `FreeEngine` path, with
analytic articulated inertia, per-cylinder piston-travel loss evidence, and coupled
leaf-first wall reactions. The authored 52,000-frame warm radial session completed and
its 4.7 s dynamic candidate was accepted by ear. The 80 ms prescribed fixture remains
a byte/topology regression, not a listening reference. `HeldDyno`, `FreeVehicle`, and
the other master-rod motion owners remain closed.

Commit `eb26214` then opened the certified one-crank, one-level master-rod `HeldDyno`
path under the distinct
`bounded-held-dyno-speed-constraint-one-level-master-rod-v1` method identity. Its
5.5 s prescribed control and 5.5 s physical candidate were rendered at 192 kHz mono
PCM24, and the user accepted the candidate as sounding good on 2026-08-03. The direct
BMW guard remained exact. At that checkpoint, the historical firewall described below
still rejected `FreeVehicle` and every other master-rod motion owner.

## Subsequent public FreeVehicle closure

Commit `11d5853` opens certified one-crank, one-level master-rod `FreeVehicle`. The
accepted articulated mechanics compose with the existing topology-neutral clutch,
transmission, road-load, and vehicle integrator; no radial-specific drivetrain model
was introduced.

The radial-five fixture carries pristine's authored `propellor` evaluation rig exactly:

- `100 lb` vehicle mass, drag coefficient `0.5`, and `705 in^2` frontal area;
- differential ratio `1`, raw tire radius `1.0` interpreted as `1 m`, and `300 N`
  rolling resistance; and
- `500 lb*ft` maximum clutch torque with `gear-1` at `1:1`.

The paired closure captures use the same initial and control schedule. One remains
open-clutch; the candidate engages `20%` clutch capacity for the isolated loaded
interval. The candidate's audition SHA-256 is
`8b2cc6620ef0e5e3f66ed18ebaf2990066fd813f9da1b11f97ee9eebf0f613eb`, and its raw
master SHA-256 is
`873f3ff548be267808ea78d6ec433dacb1d29a5434f591761913a5a8ece42c73`. A separate
repeat reproduced every candidate WAV byte-for-byte. The accepted radial FreeEngine
and HeldDyno guard trees and the accepted direct BMW launch guard tree also reproduced
all WAVs byte-for-byte. The user accepted the candidate as sounding good on
2026-08-03.

The open-clutch control has zero clutch impulse but is not sample-identical to
FreeEngine: the existing FreeVehicle path uses a semi-implicit crank-angle commit,
while FreeEngine uses its motion integral. The difference is an integrator boundary,
not road-load leakage through the open clutch.

The one-crank, one-level master-rod family is therefore complete for FreeEngine,
HeldDyno, and FreeVehicle. Nested attachments and offset, geared, or otherwise
independent multi-crank master-rod mechanisms remain intentionally closed unless
separate pristine evidence proves meaningful support.

## One ownership path

The greenfield engine JSON has one authoritative path from a cylinder to a crankshaft:

```text
cylinder.journal -> journal.crankshaft -> crankshaft
```

A currently executable direct journal is authored as:

```json
{
  "id": "main-pin",
  "type": "crankshaft",
  "crankshaft": "crank",
  "phase": {"value": 0, "unit": "deg"}
}
```

`type` is required and is the discriminator for the journal attachment union. A
cylinder references exactly one journal; it does not repeat that journal's crankshaft
owner.

## Master-rod attachment

A slave pin is a distinct journal variant:

```json
{
  "id": "slave-pin-2",
  "type": "master_rod",
  "master_cylinder": "cylinder-1",
  "throw_radius": {"value": 2.9, "unit": "in"},
  "phase": {"value": 72, "unit": "deg"}
}
```

Its crankshaft is derived through one authoritative path:

```text
slave cylinder -> master_rod journal -> master cylinder
               -> direct crankshaft journal -> crankshaft
```

`phase` is owner/master-rod-local. If `e` is the master big-end-to-wrist unit vector,
`Q` is the master big-end position, and `R(phi)` is positive counter-clockwise planar
rotation, the slave pin is `Q + throw_radius * R(phase) * e`. Phase zero therefore
points toward the wrist.
`throw_radius` is the positive distance from the master rod's big-end center to the
slave pin. The graph contract requires:

- every journal is referenced by at least one cylinder;
- a direct journal may be shared, while each `master_rod` journal has exactly one
  cylinder consumer;
- `master_cylinder` resolves and is not the consuming cylinder itself;
- the master cylinder uses a direct crankshaft journal, so attachment is exactly one
  level deep;
- cylinder-to-master cycles and acyclic nested master attachments are rejected; and
- journal phase is finite and master-rod throw radius is finite and positive.

The parser and direct-DTO compiler admission share this graph validation. A valid
master-rod graph resolves its stable master-cylinder ID, throw radius, and raw local
phase into a distinct core alternative. It has no direct-only stroke, crank radius,
or axis-relative journal phase to consume accidentally. The engine publishes
geometry-only capability: both net-torque forms and equivalent inertia are unavailable.
The shared mechanism-plan compiler selects a separate one-level master-rod alternative.
It retains exact root and slave identities, root global phase, slave local phase and
throw, bank axes, stable slave-to-root indices, chamber geometry, route bindings,
ignition angles, crank-group inertia/friction, and each piston/rod mass, rod inertia,
and physical COM distance. Its pure evaluator reproduces the pristine one-level
master/slave position and volume construction. The plan contains no slave stroke,
nominal displacement, clearance-volume shortcut, derived equivalent inertia, wall
reaction, or torque authority yet; retaining source inputs alone grants none of those
behaviors.

Before mechanics admission, a separate analytic primitive certifies each resolved root
or slave cylinder over a complete revolution without an angular sample grid. For a
root it uses the exact `L + r` maximum piston-axis position.
For a slave it uses the deliberately sufficient bounds `L_slave > r + throw` for
forward reach and `s <= L_slave + r + throw` for maximum piston-axis position. Both
paths evaluate the resulting minimum chamber volume in the pristine written order and
require it to be strictly positive. Scale-aware binary64 guards reject ULP-scale reach
or volume clearances instead of promising that numerically ambiguous geometry will
execute. A failed sufficient slave proof is reported as `not_certified`; it is not
mislabeled as proof that the authored linkage is impossible.

The mechanism-plan compiler invokes this certificate for every root and slave after
the complete one-level graph has been assembled. A slave is checked against the exact
direct-root driver selected by its stable master index. A failed proof returns one
bounded per-cylinder `unsupported_value` with the `not_certified` reason; no
uncertified immutable plan is released. Certification alone grants no execution
authority.

The mutable mechanics sample also has one tagged coordinate payload. Direct cylinders
own geometric TDC, wrapped centered-slider phase, TDC-relative piston travel, and its
angle derivative. A one-level master-rod alternative owns absolute bank-axis position
and its angle derivative instead. An unpopulated sample is explicitly empty. The
shared chamber volume, volume derivative, and absolute piston speed remain outside
that tag because gas and capture consume them for either geometry. The internal
mechanics session now emits the master-rod alternative only when it owns a finite
prescribed kinematic cursor. It evaluates the certified plan at the post-step body
angle, preserves stable cylinder and spark order, and rejects externally supplied
post-step crank motion. Completion remains terminal and stable across every advance
overload.

The internal gas compiler admits the same source-bound plan only for a finite
`PrescribedKinematicSweep`. Bore, piston area, fixed chamber geometry, and the fresh
body-angle-zero chamber sample come from that plan and its evaluator. Subsequent
chamber work and indicated torque consume the common mechanics volume and
`dV/dtheta`; the gas path never reconstructs a slave stroke, displacement, or
clearance shortcut. Cylinder and torque reductions retain authored order. This does
not grant crank-motion ownership.

The public engine compiler accepts a certified plan. Finite prescribed capture shares
that exact plan across mechanics and gas, then publishes the existing mechanism-neutral
capture, excitation, conditioning, IR, mastering, and audio buses. The canonical
radial-five fixture completes exactly 800 physics frames and four public audio blocks
with active gas sources and finite nonzero PCM. This is execution-path evidence, not a
claim that an 80 ms topology fixture is a production listening reference.

The resolution-only nominal stroke, compression, and displacement fields in
`EngineSpec` remain non-executable master-rod facts. Nested attachments and coupled
reactions are not implemented in this sub-slice.

The scenario firewalls define the public execution boundary. Authored master-rod
engines reject every mode except `external_speed` at `/mode/type` before baseline
inertia is queried, and the resolved contract admits only `PrescribedKinematicSweep`.
Mechanics and gas independently require a finite prescribed schedule. Dynamic crank
also retains its direct-plan gate, so held speed, dyno, free engine, free vehicle,
inertia, and torque-owning operation cannot consume radial coordinates accidentally.

## Removed ambiguity

This is a destructive current-contract cleanup with no aliases or compatibility
decoder. The parser rejects these former fields as unknown:

- `crankshaft.journals`, which reciprocally repeated every journal attachment;
- `cylinder.crankshaft`, which repeated the selected journal's owner;
- `journal.master_journal` and `journal.slave_throw`;
- `connecting_rod.slave_throw`; and
- `cylinder.slave_journal`.

The removed names described several competing ownership paths. The new `master_rod`
variant does not revive or alias any of them.

## Behavioral boundary

Direct journal IDs, crankshaft ownership, phases, cylinder bindings, ordering, and
resolved provenance remain unchanged. Existing direct engines continue through the
same resolver/runtime path and canonical request shape. At topology sub-slice 10C2b8,
master-rod public execution was limited to certified finite prescribed external-speed
motion; articulated inertia, coupled wall reactions, torque-owning operation, and
dynamic operating modes were deliberately later gates. The subsequent-status records
above now close only the evidenced one-crank, one-level FreeEngine, HeldDyno, and
FreeVehicle family. They do not relax the remaining topology exclusions.
