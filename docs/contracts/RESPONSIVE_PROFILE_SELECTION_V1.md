# Responsive Profile Selection V1

ESO responsive baking accepts either an explicit authored profile or an
automatic profile. Profile selection is an Engine Sim concern and does not
introduce downstream product-specific fields or behavior.

## Selection

When `--profile PROFILE.json` is present, ESO reads, validates, hashes, stages,
and uses those exact profile bytes. The existing
`crankwave/responsive-audio-bake-profile-v1` contract is unchanged.

When `--profile` is absent, ESO applies policy
`engine-redline-affine-v1` and emits profile ID
`interactive-preview-redline-v1`. No sibling file search, directory-dependent
fallback, engine identity table, or nearest-profile heuristic participates in
automatic selection.

The automatic profile is serialized as indented UTF-8 JSON with one final LF.
Those exact bytes supply `profile_sha256`, the bake cache identity, and the
staged `input/profile.json` used by all capture phases.

## RPM derivation

The reference profile has the following RPM points:

- anchors: `600, 700, 900, 1200, 1600, 2200, 3000, 4000, 5000, 6000, 6500`;
- outer domain: `550` through `6700` RPM;
- held-capture extension threshold: `700` RPM;
- elevated shutdown: `3000` RPM.

Let `R` be `engine.engine.limits.redline.value`, whose unit must be `rpm`.
Automatic selection requires `R >= 250`. The derived lower anchor is
`L = min(600, R / 5)`.

Every reference RPM `x` is mapped by:

```text
L + ((x - 600) / (6500 - 600)) * (R - L)
```

Mapped values are rounded to six decimal places. The first anchor is set to
the rounded `L`, and the final anchor preserves the engine's exact authored
redline value. The outer minimum is additionally clamped to at least 50 RPM,
matching the directional capture floor. All non-RPM profile values remain the
accepted interactive-preview-v1 values.

This produces exactly 11 anchors and therefore 33 held cells for every engine.
At a 6500 RPM redline, every RPM and lifecycle value is byte-for-value equal to
the accepted explicit profile except for its versioned automatic profile ID.

An engine below the automatic policy floor fails before cache or output
creation and may still use a compatible explicit `--profile`.

## Invariants

- The last capture anchor equals the declared engine redline.
- Anchors are finite, positive, and strictly increasing.
- The outer domain contains every anchor and remains within the directional
  capture envelope.
- Elevated shutdown is inside the derived domain and does not exceed redline.
- Selection depends only on the validated engine redline and this versioned
  policy.
- Engine identity, display name, path, input ordering, host resources, and
  installed location do not affect the derived profile.
- A policy change requires a new policy ID and automatic profile ID.
