# Direct journal attachments

Status: implemented topology sub-slice 10B1

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

`type` is required and is the discriminator for the journal attachment union. Only
`crankshaft` is admitted in this sub-slice. A cylinder references exactly one journal;
it does not repeat that journal's crankshaft owner. The parsed authoring model retains
the attachment as a tagged variant as well, so adding another attachment kind does not
require reinterpreting the direct shape.

## Removed ambiguity

This is a destructive current-contract cleanup with no aliases or compatibility
decoder. The parser rejects these former fields as unknown:

- `crankshaft.journals`, which reciprocally repeated every journal attachment;
- `cylinder.crankshaft`, which repeated the selected journal's owner;
- `journal.master_journal` and `journal.slave_throw`;
- `connecting_rod.slave_throw`; and
- `cylinder.slave_journal`.

The removed master/slave names described several competing ownership paths but were
never executable. A later topology sub-slice may add a separate `master_rod` journal
variant; it must do so through this same discriminator and must not revive any removed
field.

## Behavioral boundary

This sub-slice changes authoring shape only. Direct journal IDs, crankshaft ownership,
phases, cylinder bindings, ordering, and resolved provenance are unchanged after
compilation. The accepted BMW PCM render is therefore required to remain byte-for-byte
identical across the migration.
