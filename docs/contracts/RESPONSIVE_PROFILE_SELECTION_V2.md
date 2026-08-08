# Responsive profile selection v2

The installed native `bake-revengine` command has one deterministic profile policy:
`engine-redline-affine-v1`, producing profile ID
`interactive-preview-redline-v1`. It does not accept an installed profile override,
search sibling files, or select a nearest fixed-redline profile.

The installed `profiles/interactive-preview-v1.json` resource preserves the 6500-RPM
reference grid used to define the affine policy. It is an authoring/reference
resource, not a second production execution path.

For an admitted engine redline `R >= 250 rpm`, let `L = min(600, R / 5)`. Reference
RPM `x` maps to:

```text
L + ((x - 600) / (6500 - 600)) * (R - L)
```

The policy derives exactly 11 strictly increasing RPM anchors, retains the fixed
coast/mid/power lanes, clamps the outer minimum to the native directional-capture
floor, keeps elevated shutdown inside the derived domain, and binds every field in a
versioned SHA-256 identity. The final anchor equals the engine's exact authored
redline. Engines below the policy floor fail before output creation.

Changing any derivation rule requires a new policy ID and automatic profile ID.
