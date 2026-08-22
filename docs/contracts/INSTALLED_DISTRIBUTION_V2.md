# Installed distribution v2

Installed distribution v2 is the fully native production boundary introduced by
release 1.2.0. It contains one relocatable `crankwave` executable. That
executable owns finite audition rendering and telemetry, native responsive cooking,
CRANKWAVE packing, inspection, verification, and IR-catalog inspection. Responsive
cooking does not launch Node.js, a compiler, a helper executable, or simulation
WebAssembly.

The immutable release identity is the pair of `release_identity` and the SHA-256 in
`release.json.sha256`. The semantic identity names a compatible ESO release; the
digest binds the exact executable, contracts, schemas, simulator-free browser
playback modules, built-in catalogs and payloads, responsive profiles, shared
recorded starter, licenses, and notices.

Resources live below the configured
`<datadir>/crankwave/<release_identity>/`. The executable discovers only
that prefix-relative resource root unless an explicit developer `--asset-root` is
provided. Moving the complete prefix does not alter recorded bytes or identities.

## Manifest

`release.json` has schema
`crankwave/installed-distribution.v2`. Its sorted `files` array records the
portable distribution-relative path, byte count, and lowercase SHA-256 of every
member except `release.json` and `release.json.sha256`. The sidecar is the lowercase
SHA-256 of `release.json` followed by one LF.

The manifest records:

- the exact native CLI and all six public command names;
- `production_runtime.kind: native`, with both Node and simulation-WASM requirements
  fixed to false;
- the simulator-free ESM playback resource directory and entry point;
- source, toolchain, and installed-input closure identities;
- the one-step native CRANKWAVE bake workflow and its digest; and
- the unchanged diagnostic telemetry role/schema boundary originating at commit
  `c8d672b59e3046654ad5f818f31725798aef7ffa`.

An ordinary dirty or noncanonical developer install is classified as
`incomplete_development_install` with `complete: false`. The publishable
`crankwave_distribution` target fails unless the native source and installed
resource-input closures are clean, share the recorded Git commit, and use the
canonical release toolchain. No external renderer pair participates in completeness.

## Product closure

The installed production closure includes all 73 content-addressed IR payloads and
`ir-authoring-catalog.v1.json`, the technical built-in asset catalog and accessory
payloads, the exact shared recorded starter, responsive profile resources, licenses,
schemas, contracts, and the simulator-free playback ESM closure.

It excludes the former responsive-bake shell launcher and JavaScript baker, the IR
spectrum helper, C-API JavaScript adapters, the WASM heap adapter, and the simulation
WASM loader/module. Those may remain source-tree development or compatibility-test
oracles but are not release members or production dependencies.

## Native workflow

`contracts/crankwave-bake-workflow.v2.json` freezes one command:

```text
bin/crankwave bake-crankwave \
  --engine {engine_json} \
  --output {new_crankwave_file} \
  --deadline-unix-ms {deadline_unix_ms} \
  --result-format json
```

The command applies `engine-redline-affine-v1`, cooks every required responsive
child, packs the carrier, verifies the completed carrier, and only then atomically
publishes the requested new path. Deadline or termination returns the stable CLI
failure envelope and leaves no incomplete output.

## Archives and tamper behavior

The distribution archive contains one same-named prefix and is assembled only from
the manifest-bound closure plus the manifest and sidecar. Tar member order,
timestamps, ownership, modes, and format are normalized. A missing, extra, changed,
or symlinked release member fails verification; unmanifested caller debris is never
included.
