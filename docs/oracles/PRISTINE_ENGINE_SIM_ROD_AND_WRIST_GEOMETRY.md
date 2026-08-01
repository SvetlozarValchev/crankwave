# Pristine engine-sim rod-COM and wrist-pin oracle

Status: source-audited parity authority for the rod-COM and wrist-pin checkpoint

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`, read from the repository-local
object database at `../engine-sim`. Source citations below refer to that commit and
must be inspected with `git -C ../engine-sim show <authority>:<path>` rather than
the neighboring worktree's current branch or uncommitted files.

## Pristine authoring and execution

`es/objects/objects.mr:208-244` exposes connecting-rod `center_of_mass` and
forwards it to the native rod node. `scripting/include/connecting_rod_node.h:57-62`
reads it into `ConnectingRod::Parameters::centerOfMass`.

The pristine runtime does not execute that value as a shifted center of mass.
`src/connecting_rod.cpp:40-45` defines the two rod anchors as

```text
big_end_local    = -length / 2 + center_of_mass
little_end_local = +length / 2 - center_of_mass
```

Consequently their midpoint is always the rigid-body origin and their separation is
`length - 2 * center_of_mass`. A nonzero value shortens or lengthens the effective
pin-to-pin rod while leaving its mass center at the midpoint. It does not move the
center of mass along a rod of fixed length.

This is also inconsistent with pristine placement. In
`src/piston_engine_simulator.cpp:234-280`, initial cylinder placement solves the
crank-pin-to-wrist-pin geometry with the unmodified authored `length`, then locates
the rod through the altered big-end anchor. The constraint links at
`src/piston_engine_simulator.cpp:129-160` subsequently use the two altered anchors.
A nonzero value therefore begins with a `2 * center_of_mass` pin-separation error for
the constraint solver to correct. This dormant behavior is an implementation bug,
not a compatibility contract, and the clean-room core intentionally does not clone
it.

`es/objects/objects.mr:247-276` exposes piston `wrist_pin_position` and forwards it
to the native piston node. The adjacent `wrist_pin_location` member is dead: it is
declared on `piston_parameters` but is absent from `_piston` and is never forwarded.
`scripting/include/piston_node.h:36-41` binds the live field, and
`src/piston.cpp:25-33` stores it.

Pristine uses the live value `w` as an axial local anchor on the piston. Both the
cylinder-wall and rod-link constraints attach at piston-local `(0, w)` in
`src/piston_engine_simulator.cpp:117-135`. Let `d` be the bank axis pointing outward
from the crank center toward the deck. `src/cylinder_bank.cpp:23-33` defines that
axis, while pristine initializes the piston orientation to `bank_angle + pi`.
Transforming `(0, w)` therefore gives

```text
wrist_pin_world = piston_reference_world - w * d
piston_reference_world = wrist_pin_world + w * d
```

`src/combustion_chamber.cpp:120-131` measures chamber sweep from the piston reference,
so the corresponding analytic volume is

```text
V = piston_area *
        (deck_height - wrist_pin_axis_position - w - compression_height)
    + head_chamber_volume - piston_displacement_term
```

The offset is constant on a fixed bank. It changes clearance volume by
`-piston_area * w`, but it does not change wrist-pin velocity, acceleration, piston
translational inertia, stroke, or `dV/dtheta`. It is an axial datum offset, not a
lateral pin offset.

## No nonzero source or audio oracle

At the pinned pristine commit, all 25 tracked engine-asset `center_of_mass` values
are exactly zero. All 25 tracked `wrist_pin_position` values are also zero: 22 are
written as `0.0` and three as `0 * units.mm`. The locally available repository
history contains no tracked engine asset with a nonzero value for either field.
Examples include BMW `assets/engines/bmw/M52B28.mr:215-228`, Shovelhead
`assets/engines/atg-video-1/03_harley_davidson_shovelhead.mr:72-84`, and radial five
`assets/engines/atg-video-1/08_radial_5.mr:88-100`.

There is therefore no pristine nonzero recording, authored engine, or observed
solver trace that can serve as an auditory authority. Zero-valued source assets prove
only midpoint-rod and zero-wrist behavior. Synthetic nonzero cases validate the
declared clean-room semantics; they are not source-audio parity claims.

## Clean-room semantics

The greenfield JSON contract uses explicit physical meanings rather than preserving
the ambiguous MR spelling:

- `connecting_rods[].center_of_mass_from_crank_pin` is the physical distance `c`
  from crank-pin center to rod center of mass along an authored rod of unchanged
  pin-to-pin length `L`. Omission resolves to exactly `L / 2`.
- With `lambda = c / L`, rod-center position and derivatives are
  `G = (1-lambda) * C + lambda * W`, with the same affine weighting for `G'` and
  `G''`. Rod translational inertia is `m * |G'|^2`; its angular derivative is
  `2 * m * dot(G', G'')`. The authored rod moment of inertia remains about `G`.
- `pistons[].wrist_pin_position` is the nonnegative axial offset `w` that places the
  piston reference outward from the wrist-pin center. Omission resolves to exact
  positive zero. It participates in clearance/fixed geometry through the volume
  equation above and does not alter kinematic derivatives.

The cycle-mean mechanism methods are versioned as
`centered-slider-crank-cycle-mean-equivalent-inertia-v2` and
`centered-slider-crank-rigid-group-cycle-mean-equivalent-inertia-v2`. Version 2 uses
the authored rod-center fraction and retains an exact midpoint fast path, so engines
that omit both new values keep their previous numerical mechanics and PCM. Request
and manifest identities intentionally change with the method version; that identity
change must not be mistaken for an audible change.

Admission requires `0 <= c <= L`, finite nonnegative `w`, and positive derived
chamber volumes. Non-midpoint rod centers remain closed for master-rod mechanisms;
their dynamic mass ownership needs a separate oracle and method. No legacy decoder,
old MR-offset interpretation, or `wrist_pin_location` alias is introduced.

## Validation strategy

1. Every accepted engine that omits both fields must retain exact prior mechanics,
   telemetry, and decoded PCM under the midpoint/zero defaults.
2. Explicit midpoint and zero values must execute identically to omission, apart
   from provenance and request identity that truthfully record authored versus
   derived values.
3. Synthetic direct-rod cases exercise `c = 0`, `c = L / 2`, `c = L`, and one
   asymmetric interior value against the affine center, cycle-mean inertia,
   configuration inertia, inertia derivative, and transient rod-moment formulas.
4. A synthetic wrist case must change clearance and fixed geometry by exactly
   `-piston_area * w` while preserving stroke and all crank-angle derivatives.
5. Nonfinite/out-of-range rod centers, negative/nonfinite wrist offsets, nonpositive
   chamber volume, and non-midpoint master-rod cases must fail closed before runtime.

Because accepted source assets are zero-valued, this checkpoint is gated by exact
baseline output comparison and the synthetic numerical cases above. It does not need
or justify a subjective nonzero sound audition.

## Closure evidence

The checkpoint closed on 2026-08-02 with no sound-bearing change:

- the BMW authored-JSON migration render retained its accepted PCM24 span and exact
  complete audition-WAV SHA-256
  `f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552`;
- the accepted source-value Shovelhead procedure and the checkpoint render at
  `artifacts/listening/shovelhead-rod-wrist-default-identity-check-1000-5000rpm-6s`
  have byte-identical complete master WAV files, SHA-256
  `62c3d0ddf1d8b38b6707fcc7a28804162cb32101f8f9e530a435a63e1a045073`;
- their decoded master PCM24, front selected Float32, and rear selected Float32 hashes
  are respectively
  `c5aa5b66e67748eaa33ffa2f88b4196ee08d38df0c7f2fc8c8c01ac744478da4`,
  `a8c64ddd1e29dd8e75d38c8cab958b16d9c930fa08deff24ab8f2a27317d8ca2`,
  and `0cf90124648b5c7a925e3256b7e861abd0c01598b22a879852cc1643f483d83f`.

The request/manifest identity changes intentionally because resolved physical fields
and the v2 inertia method are now part of the declared computation. That administrative
identity change is separate from the exact output comparison above.
