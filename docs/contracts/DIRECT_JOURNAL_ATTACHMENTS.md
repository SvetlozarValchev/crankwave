# Journal attachment graph

Status: topology sub-slice 10B2; graph implemented, master-rod execution closed

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

`phase` is owner/master-rod-local: zero points from the master big end toward its wrist
pin, and positive rotation follows the authored positive-angle convention.
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
master-rod graph reaches capability admission and then fails explicitly at the
`master_rod` type because nested kinematics and reactions are not implemented in this
sub-slice. It cannot fall through to a direct-journal `std::get` or silently use direct
slider-crank geometry.

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
same resolver/runtime path. Master-rod execution remains closed until the next topology
slice adds one compiled kinematics plan, nested chamber geometry, inertia, and coupled
wall reactions.
