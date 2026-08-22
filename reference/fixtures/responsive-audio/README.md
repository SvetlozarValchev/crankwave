# Responsive audio preview fixtures

These packages are the browser-workbench reference set for responsive baked-B
auditioning. They contain phase-aligned held texture, directional sharp-gesture
material, each engine's declared presentation transfer, and engine-specific
starter, first-fire handoff, and key-off lifecycle performances. The lifecycle
packages are audition candidates: their generic admission behavior was accepted
by ear before promotion, while engine-specific refinement remains iterative.

The fixtures are deliberately identified as 10 kHz-physics previews. They are
reference outputs, not a claim that the native 20 kHz atlas baker is complete.
Their manifests bind every binary payload by SHA-256 and record the engine and
renderer identities used for capture.

The captured renderer source closure remains `5287982a…`; it is not relabeled
as the current build. Each root package instead carries the same reviewed,
hash-bound parity report for the single `5287982a…` to `c3a90571…` edge. That
report compares 21 deterministic cases across all 10 engines (14,315 blocks and
54,942,720 mono frames) and found byte-identical PCM and normalized telemetry.
The browser admits only this code-registered edge; any future renderer closure
fails closed until separately proven.

Every lifecycle also uses the single canonical `shared-recorded-starter`
fixture. It is derived from Ika.Komura's `Car not starting.wav`, dedicated to
the public domain under CC0 1.0. The source MP3 and derived 192 kHz mono payload
retain independent SHA-256 identities and reproducible crop, EQ, level, and
resampling metadata. Source A and baked B receive the same recorded layer after
their engine-specific paths.
