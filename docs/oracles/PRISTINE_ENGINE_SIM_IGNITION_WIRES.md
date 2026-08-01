# Pristine engine-sim ignition-wire oracle

Status: source-audited parity authority for headless slice 10

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

## Executed topology

An ignition wire is a stateless fan-out connection, not an electrical-energy or
coil model. `scripting/include/ignition_wire_node.h` stores a set of
`(cylinder bank, bank-local cylinder index)` connections. A cylinder adds itself
to its selected wire in
`scripting/include/cylinder_bank_node.h::addCylinder()`.

During engine generation,
`scripting/include/ignition_module_node.h::generate()` visits each ordered
distributor post, then every cylinder connected to that post, and assigns the
post's crank angle to each cylinder's independent spark plug. Runtime ignition
thereafter evaluates every cylinder separately. Sharing one wire therefore means
that all connected cylinders receive the same firing angle and cross that angle
in the same physics step.

There is no wire-local state, resistance, dwell, coil-energy sharing, or discharge
competition to reproduce.

## Clean-room contract

The JSON keeps explicit wire objects, a wire reference on every cylinder, and one
ordered firing event per used wire. Compilation requires the declared, used, and
firing-event wire sets to match exactly. It then expands each firing post to its
connected cylinders in engine cylinder order, preserving the executable core's
one-entry-per-cylinder firing sequence.

When a wire fans out, every connected resolved public cylinder retains
`shared_ignition_wire_semantic_id` in addition to its firing angle. Distinct
one-cylinder wires normalize away as stateless, execution-equivalent authoring
objects. Consequently these declarations remain different canonical engine
identities:

- two cylinders sharing one wire at angle zero;
- two cylinders using distinct wires, both at angle zero.

No shipped `.mr` engine at the pinned commit connects more than one cylinder to a
wire. The representative parity fixture is therefore a synthetic inline twin
using the exact source semantics; it is not presented as an upstream sound oracle.

## Acceptance boundary

- One shared post expands to both cylinders in stable engine order.
- Shared and equal-angle split declarations retain different resolved identities.
- Their executable audio remains byte-identical because pristine assigns the same
  per-cylinder spark angles in both cases.
- Unused wires and duplicate firing posts remain rejected.
