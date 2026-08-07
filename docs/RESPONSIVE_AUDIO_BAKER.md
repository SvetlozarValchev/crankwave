# Responsive audio baker

`tools/responsive-audio-baker/bake.mjs` is the tracked standalone entry point
for producing the responsive preview package used by the browser runtime. It
promotes the accepted research implementation without depending on `.work`
inventories, hand-selected engine IDs, or already-baked fixture audio.

The command requires Node.js 20.11 or newer. Native CMake installs a relocatable
Node launcher named `engine-sim-offline-responsive-bake`; it is resource discovery
and version-checking glue, not a native child-process bake implementation.

## Build and bake

From a clean checkout, fetch the repository's Git LFS assets, assemble the
content-addressed built-in asset bundle, and build the WASM renderer once:

```bash
git lfs pull
cmake -S . -B build
cmake --build build --target engine_sim_offline_builtin_assets
scripts/build-workbench.sh
```

Then validate the derived plan without starting the renderer:

```bash
node tools/responsive-audio-baker/bake.mjs \
  --engine data/engines/bmw-m52tub28-cleanroom/engine.json \
  --profile tools/responsive-audio-baker/profiles/interactive-preview-v1.json \
  --output .work/responsive-bakes/m52tu \
  --cache .work/responsive-bake-cache \
  --builtin-assets build/generated/engine-sim-offline-assets \
  --module .work/browser-workbench/build/engine-sim-offline.js \
  --jobs 24 \
  --plan
```

Remove `--plan` to perform the bake. The output directory must not already
exist. `--module` selects an adjacent
`engine-sim-offline.js`/`engine-sim-offline.wasm` pair, `--ir-dumper` can select
a prebuilt IR helper, and `--cxx` selects the compiler used to build that small
helper on a cache miss. A real bake requires `--module` unless a conventional
installed/build-tree pair is discoverable; there is no hidden `.work` default.
When `--profile` is omitted, the baker looks for
`responsive-audio-bake-profile.json` beside the engine JSON.

Without `--jobs`, the baker uses up to 24 workers, bounded by the host's reported
parallelism. A full cold bake of the final hardened 11-anchor profile measured
46.57 seconds with 24 workers on the measured 32-vCPU host. Separate cold/warm
capture-cache checks published byte-for-byte identical trees across worker counts.
That result is
environment-specific, not a duration guarantee; smaller or differently loaded
workers may take longer. `--jobs` is one global renderer-worker ceiling, not a
per-stage value. On a cold bake the independent held and directional captures
share a single longest-scenario-first queue under that ceiling. Held package
assembly then precedes directional assembly because the directional manifest
binds the exact held provenance; lifecycle capture begins only after both
packages are present.

The emitted tree contains:

```text
runtime.json
revengine.json
bake-report.json
held/
directional/
lifecycle/                 # when enabled by the profile
shared-recorded-starter/   # when enabled by the profile
```

`runtime.json` is directly loadable by the responsive browser runtime.
`revengine.json` binds those exact bytes, so the complete output is directly
accepted by the separate container packer. Before publication, the baker
preflights the complete output against the REVENGINE v1 portable-tree limits,
including entry count, total size, path and segment syntax/length, regular-file
requirements, and descriptor/runtime hash binding:

```bash
build/engine-sim-offline pack-revengine \
  --package-directory .work/responsive-bakes/m52tu \
  --output .work/m52tu.revengine
build/engine-sim-offline verify-revengine --input .work/m52tu.revengine
```

## Identity, cache, and failure behavior

The baker namespaces reusable capture artifacts by SHA-256 identities derived
from the exact engine and profile bytes, baker sources and their transitive
runtime JavaScript closure, the renderer loader and WASM bytes, resolved asset
bytes, the shared recorded starter tree when selected, and the exact
Node/V8/ICU/platform/architecture/endianness execution identity. A change to any
of those inputs cannot silently reuse stale PCM. Separate engines and profiles
can share one `--cache` root. A per-identity `.active` owner registry rejects
concurrent writers to the same capture set. On Linux it removes a dead owner
only when its machine/hostname scope matches and either its boot identity is
from an earlier boot or its matching PID namespace reports the recorded
PID/start-time pair absent. Foreign, malformed, or otherwise unverifiable owners
fail closed instead of using an age-based lease.

The packaged `bake-report.json` contains portable input identities, results, and
the execution-runtime identity that bounds deterministic cache reuse; it does
not embed host paths, hostnames, or worker-count settings.
Planning output and other stdout diagnostics may include host paths and `--jobs`
values because they describe the current invocation rather than package
content. Renderer-free plan mode reports `cache_identity_complete: false` and
leaves the final cache identity and namespace `null`; those values become exact
only after a real bake snapshots the renderer and IR helper.

Publication is an atomic directory rename. A failed job leaves its sibling
`.NAME.staging-PID` directory for diagnosis and never creates the requested
output path. Lifecycle `runs/` scratch is retained on failure for diagnosis and
removed after successful package publication; reusable lifecycle candidates are
not removed. Re-running with the same cache reuses only captures whose complete
identity and payload hashes still match.

## Current contract and limitations

This command reproduces the accepted 10 kHz-physics, 192 kHz-delivery
interactive preview representation: 48-cycle held textures over the fixed
coast/mid/power lane grid, rising/falling directional transients, generated
starter/startup/idle shutdown, optional shared recorded starter, and a
profile-defined elevated-RPM shutdown. Lifecycle capture is an audition-specific
presentation layer; it does not claim to be a general physical simulation of
starting or shutdown behavior. This is not the canonical 20 kHz renderer
artifact path and does not claim a production fidelity admission.

An engine JSON may live outside the source checkout. Asset authority comes only
from an exact `(kind, id, sha256)` match in the built-in catalog; authored URI
locations are not trusted by this command. Authorized payload bytes are checked
and staged into the bake's content-addressed cache before rendering. The shared
recorded starter is likewise read only from the installed/build bundle's
`runtime-audio/shared-recorded-starter` tree.

The RPM grid is profile-owned, but the baker also enforces the engine JSON's
declared redline: neither the highest held anchor nor the elevated-shutdown
capture may exceed it. The bundled 6500-RPM profile is therefore appropriate only
for engines whose declared range includes that grid. Lower-speed engines need a
compatible sibling or explicitly selected profile; the baker never silently
applies the bundled grid. Directional transient capture is the deliberate guard
exception: it disables the limiter and sweeps to 1.05 times the highest anchor
to cover the profile's outer interpolation domain. That guard is representation
coverage, not an admitted held or lifecycle operating point.

The initial standalone contract intentionally requires:

- a complete installed/build bundle or explicit `--builtin-assets` root;
- exactly one authored impulse response;
- one selected, hash-verified accessory configuration;
- the accepted three load lanes and 10 kHz preview physics rate;
- a compatible prebuilt WASM renderer.

Those are explicit validation failures rather than per-engine exceptions. The
profile schema is `schemas/responsive-audio-bake-profile.schema.json`.

Run the focused contract tests with:

```bash
node --test tools/responsive-audio-baker/test.mjs
```

`tools/responsive-audio-baker/testdata/smoke-profile.json` reduces the RPM grid
to two anchors so maintainers can exercise every publishing stage quickly. It
is structural test data, not an audition-quality profile.

## Installed distribution

Build and install the standalone native portion with:

```bash
cmake -S . -B build-native
cmake --build build-native --target engine_sim_offline_distribution
cmake --install build-native --prefix /absolute/prefix
```

With the default GNUInstallDirs values, the stable installed resource layout is:

```text
bin/
  engine-sim-offline
  engine-sim-offline-responsive-bake
libexec/engine-sim-offline/
  dump-ir-spectrum
share/engine-sim-offline/
  package.json
  assets/
  schemas/responsive-audio-bake-profile.schema.json
  tools/responsive-audio-baker/
    bake.mjs
    dump-ir-spectrum.cpp
    internal/
      bake-contract.mjs
      directional-transients.mjs
      held-texture.mjs
      lifecycle.mjs
    profiles/interactive-preview-v1.json
  web/runtime/
    c-api-abi.js
    c-api-client.js
    c-api-errors.js
    c-api-session.js
    wasm-heap.js
  renderer/                         # optional
    engine-sim-offline.js
    engine-sim-offline.wasm
```

The five files under `web/runtime` are the exact transitive JavaScript dependency
closure of the baker stages; unrelated browser runtime modules are deliberately not
installed. The IR-helper source remains a resource because its bytes participate in
the baker cache identity, while normal installed execution selects the prebuilt
helper and does not compile repository sources.

Relative custom `CMAKE_INSTALL_BINDIR`, `CMAKE_INSTALL_LIBEXECDIR`, and
`CMAKE_INSTALL_DATADIR` values are supported. The launcher and native catalog
resolver retain only relative locations, so moving the complete prefix preserves
discovery.

Plan mode is complete with the native install alone:

```bash
/absolute/prefix/bin/engine-sim-offline-responsive-bake \
  --engine /absolute/path/to/engine.json \
  --profile /absolute/prefix/share/engine-sim-offline/tools/responsive-audio-baker/profiles/interactive-preview-v1.json \
  --output /absolute/path/to/new-package \
  --cache /absolute/path/to/cache \
  --plan
```

If `--profile` is omitted, the engine JSON must have a sibling file named
`responsive-audio-bake-profile.json`; the launcher does not silently select a
global RPM grid for an arbitrary engine.

A real bake requires an Emscripten-generated module pair. A native CMake build does
not produce that pair. Either pass a compatible module with `--module`, set
`ENGINE_SIM_OFFLINE_WASM_MODULE`, or configure the native distribution with a
completed external build directory:

```bash
cmake -S . -B build-native \
  -DENGINE_SIM_OFFLINE_INSTALL_WASM_DIRECTORY=/absolute/path/to/wasm-build
```

CMake validates that this directory contains both `engine-sim-offline.js` and
`engine-sim-offline.wasm`, exposes the optional
`engine_sim_offline_install_wasm_inputs` target, and installs the pair under
`renderer/`. The installed launcher then selects it automatically. This provisioning
step does not weaken the renderer's existing numeric or source-identity admission.
