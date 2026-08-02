# Journal attachment graph

Status: topology sub-slice 10C2b8; certified one-level master-rod geometry executes
through public finite prescribed capture and audio; torque-owning motion remains closed

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
same resolver/runtime path and canonical request shape. Master-rod public execution is
limited to certified finite prescribed external-speed motion;
articulated inertia, coupled wall reactions, torque-owning operation, and dynamic
operating modes remain later, separately gated work.
