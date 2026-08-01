# Direct bank-axis topology

Status: implemented topology sub-slice 10A
Reference authority: pristine Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

## Boundary

The current compiler and low-order executor admit direct, centered connecting rods on:

- one zero-angle `inline` bank;
- exactly two finite, distinct-angle `v_engine` banks;
- exactly two antipodal `opposed` banks; or
- one or more `custom` banks with explicit finite axes.

This is one current JSON contract change, not a compatibility layer. The public
resolved layout maps `opposed` to `flat_engine` and `custom` to `other`; every
non-inline resolved bank carries its authored axis explicitly.

The executable cylinder-axis-relative journal phase is derived uniformly for every
non-inline layout:

```text
effective journal phase = authored journal phase - authored bank axis
```

Inline execution retains its previously accepted authored journal phase unchanged.
Existing V-engine execution uses the same formula and method identity it used before
this sub-slice.

## Subaru EJ25 structural oracle

The clean-room package at `data/engines/subaru-ej25-cleanroom` is derived from
`assets/engines/atg-video-1/06_subaru_ej25.mr` at the pinned pristine commit. It keeps
the source engine's two `+90/-90 degree` banks, four distinct direct journals, source
cylinder order and bindings, equivalent bank-local heads and cams, shared intake,
paired exhausts, firing order, mechanism dimensions, flow tables, timing, starter, and
presentation controls.

The structural phase mapping is:

| Cylinder | Bank axis | Authored journal | Effective journal | Geometric TDC |
|---|---:|---:|---:|---:|
| 1 | +90 deg | 0 deg | -90 deg | 0 deg |
| 3 | +90 deg | 180 deg | +90 deg | 180 deg |
| 2 | -90 deg | 180 deg | +270 deg | 0 deg |
| 4 | -90 deg | 0 deg | +90 deg | 180 deg |

The geometric TDC column includes the source crank's 180-degree TDC reference and the
existing centered-slider-crank convention. Cylinders 1/2 and 3/4 must therefore remain
exact opposed-motion pairs during dynamic capture.

## Explicit adaptations

The fixture does not claim byte-for-byte source serialization:

- The source alternates `0.001/0.002 CFM` blowby. This checkpoint fixture still uses
  `0.001 CFM` for all four pistons so its accepted audio remains unchanged. The core
  now executes per-piston blowby; restoring these source values is intentionally a
  separate sound-bearing fixture change.
- Source zero-valued connecting-rod center-of-mass and piston wrist-pin fields are not
  authored because the current core does not execute them.
- Chen--Flynn coefficients, the accessory descriptor, head exhaust-runner
  cross-section area, and the 10 kHz physics/capture rate are executor policy, not
  measured EJ25 source facts.
- The source Impreza vehicle/transmission is outside this topology-only engine fixture.

## Acceptance and exclusions

`compiler.integration` keeps the existing V-engine resolved values and provenance
unchanged, compares execution-equivalent split heads against the shared-head PCM
contract, and admits a synthetic three-bank custom engine whose direct journal phases
cancel its three bank axes.

`simulation.subaru_ej25_topology` loads only authored JSON/assets through the generic
compiler, checks the EJ25 bank/journal mapping above, executes its bounded free-engine
scenario, validates every capture block, and proves both opposed cylinder pairs retain
identical mechanism motion across the exact 5,400-frame horizon.

This sub-slice does not admit master/slave journals, multiple crankshafts, separate
intakes, heterogeneous executable heads, or a new audio path. Those capabilities still
fail closed and require independent structural fixtures before admission.
