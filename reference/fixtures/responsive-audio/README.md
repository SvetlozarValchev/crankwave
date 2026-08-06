# Responsive audio preview fixtures

These packages are the browser-workbench reference set for responsive baked-B
auditioning. They contain phase-aligned held texture, directional sharp-gesture
material, and each engine's declared presentation transfer. The BMW M52TU
package additionally contains the user-auditioned starter, first-fire handoff,
and key-off lifecycle performances.

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

The optional shared semi-truck starter recording used during local research is
not included: its source license is unverified and redistribution is explicitly
unauthorized. The distributable M52TU package retains its engine-derived starter
loop and lifecycle audio. A private shared starter layer may be configured only
through `shared_recorded_starter_package_path` in a local package manifest.
