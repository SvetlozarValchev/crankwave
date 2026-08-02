# Pristine engine-sim intake-mixture oracle

Status: source-audited parity authority for intake-local main-mixture metering

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`, read from the repository-local
object database at `../engine-sim`. Source citations below refer to that commit and
must be inspected with `git -C ../engine-sim show <authority>:<path>` rather than
the neighboring worktree's current branch or uncommitted files.

## Pristine ownership and execution

Pristine exposes two independently authored values with the same name and default:

- `es/objects/objects.mr:51-64` gives the engine-global fuel a
  `molecular_afr` default of `25 / 2`, or `12.5`.
- `es/objects/objects.mr:483-535` gives every intake its own
  `molecular_afr` default of `25 / 2` and forwards it to the native intake node.

`scripting/include/intake_node.h:21-40` copies the intake value into that unique
runtime `Intake`. `include/intake.h:10-37` and `src/intake.cpp:24-53` retain it as
intake-local state. Distinct authored intake objects can therefore carry distinct
values even though the engine has one fuel. Conversely,
`scripting/include/engine_node.h:117-130` constructs one engine-global `Fuel` and
passes it to every combustion chamber.

For intake-local source value `S_i`, `src/intake.cpp:60-100` constructs the main
boundary mixture in this written order:

```text
main_total_pseudo_air_fuel_ratio = (0.8 * S_i) * 4
main_air_fraction =
    main_total_pseudo_air_fuel_ratio /
    (1 + main_total_pseudo_air_fuel_ratio)

main_mix.fuel = 1 - main_air_fraction
main_mix.inert = 0.75 * main_air_fraction
main_mix.oxygen = 0.25 * main_air_fraction
```

The factor `4` converts the oxygen/fuel molar ratio into the source pseudo-air's
total air/fuel ratio because that pseudo-air is 25% oxygen. The factor `0.8` is the
main-path enrichment. Consequently the main boundary's actual oxygen/fuel ratio is
`0.8 * S_i`. With `S_i = 12.5`, the source constructs
`(fuel, inert, oxygen) = (1/41, 30/41, 10/41)` through the arithmetic above and the
mixture lambda is `0.8`.

The idle bypass is a separate metering path. It always uses total pseudo-air/fuel
ratio `2`, producing `(1/3, 1/2, 1/6)`, and never reads the intake molecular-AFR
value (`src/intake.cpp:70-97`). The local `current_afr` calculated at line 62 is
never consumed; there is no closed-loop mixture correction to preserve.

The mixture flows through the intake plenum and the cylinder-bound runner into the
chamber (`src/combustion_chamber.cpp:251-305`). At ignition, the chamber divides its
actual oxygen/fuel ratio by the engine-global fuel's molecular ratio for eligibility
and flame-speed calculations (`src/combustion_chamber.cpp:169-222` and
`src/fuel.cpp:51-64`). The source calls this quotient an equivalence ratio, although
its orientation is conventional lambda rather than conventional equivalence ratio.

Fuel molecular mass does not control intake metering. It converts reacted fuel moles
to mass before energy release (`src/combustion_chamber.cpp:338-342`) and converts
metered fuel moles for consumption telemetry (`src/engine.cpp:358-370`).

## Asset and history evidence

None of the 27 tracked `assets/engines/**/*.mr` files at the pinned commit contains
`molecular_afr`. Every source engine therefore inherits `12.5` for both the fuel and
every intake; there is no tracked source engine or recording with differing
per-intake values.

The local history clarifies the final arithmetic:

- `5ece6c5` introduced the intake parameter and mixture construction;
- `98b121d` changed the default to `12.5` and introduced the `* 4` conversion;
- `62854cf` made the boundary mixture consume that converted ratio; and
- `0a818aa` added the `0.8` enrichment and the separate idle ratio `2`.

No historical checkpoint links the intake value to the Fuel object. The duplicated
stoichiometric authority is therefore source behavior, but it is not a sound-backed
asset requirement.

## Greenfield mapping

The clean-room contract preserves the executed degree of freedom without duplicating
fuel chemistry:

- the selected fuel remains the sole owner of its positive stoichiometric molar
  ratio `S_f`;
- each intake authors one finite positive `main_mixture_lambda`, `lambda_i`; and
- main-boundary metering evaluates `(lambda_i * S_f) * 4` in that written order.

For the pinned source library, every intake authors `main_mixture_lambda: 0.8`. A
hypothetical source document with distinct intake value `S_i` maps without a second
stoichiometric authority as

```text
lambda_i = (0.8 * S_i) / S_f
```

and therefore retains the source's main-boundary composition. Lambda is deliberately
positive rather than restricted to `[0, 1]`: a value above one denotes a lean main
mixture and remains a valid metering request even if a later combustion gate rejects
the resulting chamber state.

The idle bypass remains an independent source-parity calibration. This checkpoint
does not derive it from `main_mixture_lambda`, silently apply lambda to both edges, or
claim that main-mixture richness changes fuel chemistry.

`src/gas_system.cpp:82-128` also hard-codes the source's
`25 O2 + 2 fuel` pseudo-reaction. General non-gasoline reaction chemistry requires an
explicit reaction model; a second intake stoichiometry field would not fix that
limitation.

## Deliberately excluded source behavior

- There is no public intake `molecular_afr` duplicate or compatibility alias.
- The unused `current_afr` calculation is not copied.
- The GUI's hard-coded octane/oxygen mass conversion in `src/engine.cpp:314-330` is
  presentation behavior, not intake-mixture authority.
- Different fuels on different intakes are not inferred. That would require coherent
  per-cylinder fuel and reaction ownership, not merely separate metering numbers.

## Acceptance boundary

1. Every existing engine authors `main_mixture_lambda: 0.8` and retains exact prior
   decoded PCM.
2. At `S_f = 12.5` and lambda `0.8`, the main mixture retains the exact source
   arithmetic and binary64 construction order.
3. A two-intake synthetic fixture proves each lane consumes its own lambda while both
   resolve the same fuel-owned stoichiometric ratio.
4. Changing one intake lambda changes only that lane's main boundary; the other main
   lane and both idle boundaries remain unchanged.
5. Missing, zero, negative, or nonfinite lambda fails authoring validation. An intake
   stoichiometric-AFR key remains an unknown field and fails rather than becoming a
   second authority.

## Closure evidence

The completed checkpoint executes the authored lambda through JSON parsing, physical
admission, resolved provenance, canonical request identity, gas-session admission,
and each independent runtime intake lane. The focused authoring, compiler, contract,
manifest, Shovelhead, and gas-runtime tests pass. The two-intake runtime differential
changes only lane 2 from lambda `0.8` to `1.0`, observes a lane-2 plenum-composition
change, and retains lane 1 bit-for-bit.

A clean BMW authored-JSON migration render changed only the canonical request identity
to `2566de1bd4ef34e849c499e66873d0cb0bd78001547ed01ba07c094e7fa99767`.
Its complete `8,640,586`-byte audition WAV remains byte-identical to the accepted
baseline, SHA-256
`f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552`.
Because the complete container is identical, decoded PCM is necessarily identical;
no listening gate is required for this checkpoint.
