# Journal attachment graph

Status: topology sub-slice 10C2b1; graph and typed resolved core implemented,
master-rod execution closed

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
The shared mechanism-plan compiler then rejects execution explicitly. Nested
kinematics and reactions are not implemented in this sub-slice. It cannot become a
public compiled engine or silently execute direct slider-crank geometry. The
resolution-only nominal stroke, compression, and displacement fields in `EngineSpec`
are not executable master-rod facts; the geometry sub-slice must avoid using them for
accounting when its narrow prescribed-motion gate opens.

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
same resolver/runtime path and canonical request shape. Master-rod execution remains
closed until the next topology slice supplies authoritative nested chamber geometry.
That first gate is limited to prescribed external-speed motion; articulated inertia,
coupled wall reactions, and dynamic operating modes remain later, separately gated
work.
