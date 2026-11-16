# Source assets

The source checkout keeps code, catalogs, and fixture metadata in Git. Large
audio and binary payloads live in a versioned GitHub Release attachment, so
public clones do not consume the repository owner's Git LFS bandwidth.

## Setup

With Node.js 20+ and GNU tar installed, run from the repository root:

```bash
node scripts/source-assets.mjs fetch
```

This downloads the attachment pinned in `source-assets.lock.json`, verifies its
SHA-256 and size, checks its complete file list, and verifies each extracted
file before restoring the original paths. Existing files with the expected
checksums are reused. Modified local assets are never overwritten: move a
changed file aside before fetching its original version.

Run the same setup before native tests, WASM parity tests, or building the
browser workbench. A source ZIP needs the same setup as a Git clone. Git LFS
and a GitHub token are not required. CI uses this command and caches the archive.

## Offline use

Copy the attachment named in the lock file into `.work/source-assets/`, then
run `fetch`. If all files already exist, `fetch` performs no network requests.
To verify the installed payloads without ever downloading:

```bash
node scripts/source-assets.mjs verify
```

Downloaded files are ignored by Git. Catalogs, licenses, and provenance
documents remain tracked, and the lock file records every asset's exact bytes.
Installed distributions include their runtime assets as before.

## Updating assets

1. Fetch the current assets and make the intended changes to the local files.
2. Choose a new asset release tag; do not replace an attachment used by an
   existing commit. Pack all existing assets, optionally adding new paths:

   ```bash
   node scripts/source-assets.mjs pack source-assets-YYYY-MM-DD [new-asset-path ...]
   ```

   The command writes a deterministic archive under
   `.work/source-assets/releases/<tag>/`, verifies it, and updates the lock file.
   To remove an asset, remove its entry from the lock file before packing.
   New asset paths must be under `assets/` or `reference/`; add a Git ignore rule
   if their extension is not already covered.
3. Upload the archive as an attachment to a GitHub release with that tag in
   `SvetlozarValchev/crankwave`. Use an asset-specific tag rather than `v*.*.*`,
   and do not mark it as the latest application release. Publish the attachment
   before merging the lock-file change so anonymous users and fork CI can fetch it.
4. Run `verify` and the affected native/browser tests, then commit the lock file
   and any catalog or code changes. The payload files themselves stay out of Git.

The migration preserves old commits and tags. Checking out an older revision
that still uses LFS can still consume LFS bandwidth; the new setup applies to
revisions containing this manifest.
