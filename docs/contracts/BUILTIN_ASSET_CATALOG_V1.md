# Built-in asset catalog v1

## Purpose

The native `render` command must be usable with only an engine JSON, scenario JSON,
and new output directory. Its default asset resolver therefore consumes an installed
or build-tree content bundle rather than interpreting repository-relative engine URIs.
This is the production-oriented CLI path. `--asset-root` remains an explicit developer
override for testing authored local URIs.

Catalog membership is a technical runtime allowlist. It does not by itself claim,
grant, or encode copyright, license, provenance, or redistribution rights. Those
claims remain separate evidence and release-policy concerns.

## Bundle layout

```text
engine-sim-offline-assets/
  catalog.v1.json
  payloads/
    <64-lowercase-hex-sha256>
  runtime-audio/
    shared-recorded-starter/
      runtime.json
      audio/
        recorded-starter.cropped.192000hz.mono.f32le
```

The tracked catalog source is `assets/builtin/catalog.v1.json`. Its exact schema ID is
`engine-sim-offline/builtin-asset-catalog.v1`:

```json
{
  "schema": "engine-sim-offline/builtin-asset-catalog.v1",
  "assets": [
    {
      "kind": "audio",
      "id": "smooth-39",
      "sha256": "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"
    }
  ]
}
```

The root contains exactly `schema` and `assets`. Every entry contains exactly `kind`,
`id`, and `sha256`. Admitted kinds are `audio` and `accessory-configuration`. IDs use
the engine authoring contract's 1--128 byte stable-ID spelling. Digests are exactly 64
lowercase hexadecimal digits. Duplicate exact triples are invalid; the same kind and
ID may intentionally bind multiple content revisions when their digests differ.

## Resolution

For every audio and accessory-configuration declaration, default resolution requires
an authored SHA-256 and an exact catalog match on:

1. asset kind;
2. stable asset ID;
3. authored SHA-256.

The authored `uri` is not consulted on this path. The payload locator is derived only
as `payloads/<sha256>`, traversed beneath the already-opened catalog directory without
following symbolic links. The resolver reads exact bounded bytes and verifies that
their computed SHA-256 equals the content-addressed filename before compilation.

Missing authored hashes are engine-data errors. Missing catalog coverage, incomplete
installed payloads, malformed catalogs, and payload hash mismatches fail closed as
typed unavailable conditions. Existing per-file, aggregate-byte, and declared-asset
limits still apply.

## Discovery and packaging

The Linux CLI observes its current executable path and probes, in order:

1. `<executable-directory>/engine-sim-offline-assets/catalog.v1.json` for a staged
   build-tree bundle;
2. the configured GNUInstallDirs datadir, relative to the executable directory, for
   an installed prefix.

CMake assembles the first layout as a dependency of the CLI and copies it beside the
linked executable. `cmake --install` installs the executable to the configured bindir
and installs the catalog plus content-addressed payloads beneath
`<datadir>/engine-sim-offline/assets`. CMake records only the relative path from the
configured bindir to that location; changing a relative `CMAKE_INSTALL_DATADIR` is
therefore supported and moving the complete installed prefix does not invalidate
discovery. The tracked catalog, staged payload map, and engine JSON hashes must
advance together; focused native tests load and compile every tracked engine package
through the generated bundle.

The shared recorded starter is standard runtime content but is not declared by an
individual engine and therefore is not an entry in `catalog.v1.json`. Its own strict,
hash-binding `runtime.json` carries the audio contract and rights/provenance evidence.
The responsive baker resolves it beneath
`<bundle-root>/runtime-audio/shared-recorded-starter`; the bundle root may be selected
explicitly with `--builtin-assets`, discovered beside its renderer module or in a
conventional build tree, or supplied from an installed prefix. It never requires a
`reference/fixtures/...` source-tree fallback. Generated responsive packages may copy
this verified package beside their engine-specific output so their existing relative
runtime reference remains self-contained.

## Developer override

Supplying `--asset-root <directory>` deliberately bypasses catalog lookup and resolves
each authored local URI beneath that directory using the existing confined native
filesystem adapter. It is useful for developing new engine assets before catalog
admission. It is opt-in, is never inferred from the engine path, and does not weaken
the compiler's authored-hash verification.
