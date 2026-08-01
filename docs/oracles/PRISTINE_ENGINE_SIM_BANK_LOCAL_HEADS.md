# Pristine engine-sim bank-local-head oracle

Status: source-audited parity authority for headless slice 10 H1

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`, read from the repository-local
object database at `../engine-sim`. The fixture source is
`assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr`; it must be read
with `git -C ../engine-sim show <authority>:<path>`, not from the neighboring
worktree's current branch or uncommitted files.

## Executed bank-local ownership

Pristine allocates one `CylinderHead` for every cylinder bank. During engine
generation, `scripting/include/engine_node.h` maps each authored bank head to
one runtime head. `scripting/include/cylinder_bank_node.h` then generates that
head with its bank-local ports and valvetrain and binds each bank-local cylinder
to its authored intake, exhaust, sound attenuation, and added primary length.
Each `CombustionChamber` selects the head by its piston's bank index.

The head is physical execution state, not only authoring or display metadata:

- chamber volume participates in instantaneous cylinder volume;
- runner volumes and cross-section areas construct that cylinder's intake and
  exhaust gas volumes and flow-edge geometry;
- the head-local valvetrain supplies valve lift, and the head-local intake and
  exhaust curves are triangle-sampled at that lift;
- the cylinder's head-local intake and exhaust bindings select the gas systems;
  and
- the head-local exhaust binding, per-cylinder sound attenuation, and primary
  length select and scale the exhaust source sent to the synthesizer.

The executable evidence is in `include/cylinder_head.h`,
`src/cylinder_head.cpp`, `src/combustion_chamber.cpp`, and
`src/piston_engine_simulator.cpp` at the pinned commit. A clean-room compiler
must therefore preserve one ordered, bank-keyed head profile and bind every
cylinder through its `bank_id`; collapsing unequal heads to a representative
profile is not source-faithful.

## Exact Shovelhead head data

The fixture names source `b0` as `front` and source `b1` as `rear`:

| Property | Front / `b0` | Rear / `b1` |
|---|---:|---:|
| Bank axis | -22.5 deg | +22.5 deg |
| Chamber volume | 100 cc | 100 cc |
| Intake runner volume | 100 cc | 100 cc |
| Intake runner cross-section | 20 cm2 | 20 cm2 |
| Exhaust runner volume | 100 cc | 100 cc |
| Exhaust runner cross-section | 20 cm2 | 20 cm2 |
| Port-flow multiplier | 2.0 | 1.0 |
| Intake cam center | 470 deg | 785 deg (65 deg modulo 720) |
| Exhaust cam center | 250 deg | 565 deg |

Both banks use the same harmonic lobe: 210 deg duration at 0.050 in, gamma
`0.9`, 0.400 in maximum lift, and 100 generated steps. The V-twin cam builder
uses 110 deg lobe separation, a 0.500 in base radius, and a 315 deg bank-to-bank
cam offset. H1 does not generalize cam profile execution; the already-admitted
per-cylinder cam association remains unchanged.

`es/part-library/parts/heads.mr::generic_small_engine_head` defines a 0.050 in
triangle radius and the following base samples. Lift is inches-thousandths and
flow is CFM calibrated at 28 inH2O through `k_28inH2O`:

| Lift (thou) | Intake 1x | Exhaust 1x |
|---:|---:|---:|
| 0 | 0 | 0 |
| 50 | 25 | 25 |
| 100 | 75 | 50 |
| 150 | 100 | 75 |
| 200 | 130 | 100 |
| 250 | 180 | 125 |
| 300 | 190 | 160 |
| 350 | 220 | 175 |
| 400 | 240 | 180 |
| 450 | 250 | 190 |
| 500 | 260 | 200 |
| 550 | 260 | 205 |
| 600 | 260 | 210 |
| 650 | 255 | 210 |
| 700 | 250 | 210 |

The rear head uses those values exactly. The front head multiplies every intake
and exhaust output by exactly two; lift coordinates and triangle radii do not
change. `flow_attenuation` is thus a source-name misnomer here: `2.0` increases
the physical port restriction coefficients. It is unrelated to audible route
gain.

## Exact route mapping

Both cylinders share the source intake: 1.5 L plenum, 10 cm2 plenum area,
`k_carb(100)` inlet, zero idle bypass, 0.991 closed-plate position, gamma `1`,
velocity decay `1`, and the pristine default 4 in / `k_carb(200)` runner.

| Bank | Cylinder bindings | Ignition post | Exhaust | Audible source values |
|---|---|---:|---|---|
| Front / `b0` | shared intake, front head | 0 deg | `exhaust0` | sound attenuation 1.0; audio volume 0.1 |
| Rear / `b1` | shared intake, rear head | 315 deg | `exhaust1` | sound attenuation 1.0; audio volume 0.2 |

Both exhausts use the same 10 L collector volume, `circle_area(2 in)` collector
area, `k_carb(100)` outlet and primary restrictions, 70 in physical primary,
velocity decay `0.75`, zero added per-cylinder primary length, and
`minimal_muffling_01`. Pristine applies audible source scaling after gas
execution; the 2x/1x physical port curves and 0.1/0.2 exhaust audio volumes must
remain independent inputs.

## Source fidelity and intentional fixture normalizations

- The source front and rear piston blowby values are respectively
  `k_28inH2O(0.2)` and `k_28inH2O(0.1)`. The accepted H1 audio used the real source
  rear value, 0.1 CFM at 28 inH2O, for both pistons. The current candidate restores
  the source split through two independently bound piston blowby lanes. Its dedicated
  equal-value/source-value listening comparison remains pending acceptance.
- `display_depth: 0.55` and rear `flip_display: true` affect pristine's GUI only
  and are omitted. The physical bank axes are retained.
- The source's 35 kHz simulation setting is not copied into engine identity.
  The fixture uses the current executor policy: 10 kHz physics/capture and
  192 kHz source/acoustic/delivery. H1 is a topology/profile-binding comparison,
  not a cross-rate numerical-parity claim.
- The source motorcycle and transmission are outside this head-only fixture.

## H1 gates

H1a changes singular resolved/runtime head storage into bank-keyed storage while
retaining the existing exact-equivalence admission gates. Every previously
accepted engine must keep byte-identical PCM; this proves the architectural
change alone did not alter execution.

H1b removes only the cross-bank chamber, same-kind port geometry/curve, and
intake-versus-exhaust triangle-radius equivalence gates needed by this fixture.
At the H1 checkpoint it did not admit unequal blowby, multiple intakes or
crankshafts, distinct same-role standard cam shapes, or multiple-head VTEC. Distinct
standard bank-local cams were admitted later under
`PRISTINE_ENGINE_SIM_BANK_LOCAL_CAMSHAFTS.md`; multiple intakes, co-phased rigid
multiple crankshafts, and unequal per-piston blowby were admitted later. Multi-head
VTEC remains closed.
There is one current contract only; no singular-head alias or legacy decoder is
added.

## Per-piston blowby source-restoration audition

This listening pair is separate from the H1 bank-head swap below. Both recordings use
the same six-second prescribed 1000--5000 RPM procedure and presentation. The only
engine-data change is `piston.front` blowby from 0.1 to the source-authored 0.2 CFM at
28 inH2O; `piston.rear` remains 0.1 CFM.

- equal-value control:
  `artifacts/listening/shovelhead-blowby-A-equal-0p1-0p1-1000-5000rpm-6s`
- source-value candidate:
  `artifacts/listening/shovelhead-blowby-B-source-0p2-0p1-1000-5000rpm-6s`
- control/candidate decoded PCM SHA-256:
  - master PCM24: `1a14b0b2bb211e194361d83505e3367b21c154909970c1b16590c71db40ff428` /
    `c5aa5b66e67748eaa33ffa2f88b4196ee08d38df0c7f2fc8c8c01ac744478da4`
  - front selected Float32:
    `c7ab66a77f6c6732bd40286bb38c8faf2c5b8c59bb17ed4de078d03700a908c3` /
    `a8c64ddd1e29dd8e75d38c8cab958b16d9c930fa08deff24ab8f2a27317d8ca2`
  - rear selected Float32:
    `58e0193182c2e748802b2edd0ba89d51c1c7c19bc9d39b27bf4c3caae1c310dd` /
    `0cf90124648b5c7a925e3256b7e861abd0c01598b22a879852cc1643f483d83f`

Status on 2026-08-02: candidate rendered; auditory acceptance pending. Do not begin
rod center-of-mass or wrist-pin work until this pair is accepted.
The Web workbench exposes the candidate as the finite
`harley-shovelhead-source-pull` preset; it runs the same 3-second settling and
6-second 1000--5000 RPM sweep and intentionally admits no live controls.

## A/B acceptance invariant

Variant A is the exact source head assignment: front intake/exhaust use the 2x
curves and rear intake/exhaust use the 1x curves. Variant B swaps only those four
port `flow_curve` references, making front 1x and rear 2x. Head geometry, cams,
mechanism, ignition, intake/exhaust routing, presentation, scenario, rates, and
seed remain byte-identical between the authored variants.

Both variants must compile, retain the expected bank-to-profile bindings, and
render deterministically. Repeated renders of one variant must be byte-identical;
A and B must not be byte-identical. The test also derives two diagnostic controls
without adding authored fixtures: one changes only the front profile so both banks
use 1x flow, and one changes only the rear profile so both banks use 2x flow. Each
must differ from A. Leaving the rear profile unchanged in the first control and the
front profile unchanged in the second excludes an executor that silently applies
only the last or first representative head, respectively.

There is no pristine upstream WAV oracle for this gate. The pinned source graph
and C++ consumption paths are the structural/numerical authority; these differential
results are clean-room execution invariants, not a claim of byte-for-byte audio
identity with pristine `engine-sim`.
