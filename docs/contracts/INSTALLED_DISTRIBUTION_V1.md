# Installed distribution v1

An engine-sim-offline release is identified by its semantic
`release_identity` and the SHA-256 of its canonical `release.json`. Consumers pin
both. The semantic identity names the compatible product release; the manifest
digest binds the exact native executable, optional renderer pair, JavaScript
runtime, responsive baker, profiles, built-in assets, helpers, and contracts.

Release 1.1 adds the complete IR payload library and its separate semantic
`assets/ir-authoring-catalog.v1.json`. Authoring tools retrieve it through
`inspect-ir-catalog --result-format json`; they do not infer its installed path.
The command binds every exposed ID and SHA-256 to both this release identity and the
technical renderer catalog. The full palette stays outside engine JSON, which carries
only exact selected, route-used ID and SHA-256 declarations.

Resources live under the configured
`<datadir>/engine-sim-offline/<release_identity>/`. Executables and the frozen
workflow use the configured relative GNUInstallDirs layout, so moving the complete
prefix does not change any recorded bytes or hashes.

The same resource root contains `licenses/ENGINE-SIM-OFFLINE.txt`, which states the
proprietary ESO project terms, and `licenses/THIRD-PARTY-NOTICES.md`, which preserves
the upstream licenses and attributions for redistributed material. Both are ordinary
manifest-bound release members and contribute to the installed-input closure.

`release.json` uses schema
`engine-sim-offline/installed-distribution.v1`. Its `files` array is sorted by
portable distribution-relative path and records the exact byte count and lowercase
SHA-256 for every member except `release.json` and `release.json.sha256`. The latter
contains the lowercase SHA-256 of `release.json` followed by one LF. The manifest
also records the Git revision, the renderer source-closure digest, the exact
installed tools/runtime/assets input-closure digest, native renderer hash, optional
WebAssembly loader/module hashes and its independently observed source-closure
digest, and the bound VEHICLEENGINE bake workflow contract. Release cleanliness is
scoped to those product inputs: unrelated workbench/harness files and private notes
do not contaminate it, while a changed, untracked, or index-hidden selected input
fails the immutable-release classification.

The release also binds the native render diagnostic artifact introduced at
`c8d672b59e3046654ad5f818f31725798aef7ffa` without changing its published
boundary: role `diagnostics.engine-telemetry.v1`, kind `telemetry`, relative path
`telemetry/engine-telemetry.v1.ndjson`, and schema
`engine-sim-offline.engine-telemetry.ndjson.v1`.

An ordinary developer install may omit the externally built WebAssembly pair or may
come from a dirty source closure. Such an install is labelled
`incomplete_development_install` with `complete: false`. It must not be published as
a release. The `engine_sim_offline_distribution` target fails unless both renderer
files exist, both embedded renderer source stamps are clean and complete, and the
native and WebAssembly source-closure digests match exactly.

On success, that target emits
`distribution/<config>/engine-sim-offline-<release_identity>.tar` and its adjacent
`.tar.sha256` sidecar. The archive contains one same-named relocatable prefix. It is
assembled only from the manifest-bound file closure plus `release.json` and its
binding; unmanifested files in a staging prefix are excluded. Member order,
timestamps, numeric ownership, permissions, and tar format are normalized so two
assemblies from the same installed closure are byte-identical.

The installed `contracts/vehicleengine-bake-workflow.v1.json` freezes the public adapter
sequence: responsive bake, deterministic pack, then full verification. All three
steps receive the same absolute `deadline_unix_ms`; the native steps request their
stable JSON result envelope. Its default bake omits `--profile` and therefore binds
the versioned
`engine-redline-affine-v1` selection policy; `--profile` remains an explicit
override outside that default sequence. The contract defines distribution-relative
executables and typed placeholders, while leaving cache, scheduling, cancellation,
and publication policy to the calling adapter.

The installed responsive-bake launcher supplies the configured release identity to
plan, success, package-report, and failure records. It pins its installed assets,
renderer path, and IR helper; caller module/asset/helper/compiler overrides and their
ambient environment variables cannot redirect the frozen release workflow. Its own
missing-resource and runtime preflight failures emit the same stable JSON failure
record rather than shell-specific diagnostics.
