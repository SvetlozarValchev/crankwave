# IR authoring catalog generation and curation

`assets/builtin/ir-authoring-catalog.v1.json` is the semantic discovery surface for
trusted engine authoring. It is deliberately separate from
`assets/builtin/catalog.v1.json`, which is only the native runtime allowlist. A model
may discover and compare IRs through the authoring catalog, but production resolution
still requires an exact technical-catalog match.

## Exact source import

The 73 source WAVs were imported without decoding, normalizing, trimming, or changing
their containers. Each working-tree payload lives at:

```text
assets/builtin/ir-library/payloads/<source-sha256>.wav
```

Git LFS owns that narrow path. The installed distribution copies the same bytes to
`assets/payloads/<source-sha256>`, which is the existing content-addressed runtime
layout. Regenerate and verify with:

```sh
node tools/ir-authoring-catalog/generate.mjs \
  --import \
  --source-root /path/to/crankwave/assets/sound-library
node tools/ir-authoring-catalog/generate.mjs --check
node tools/ir-authoring-catalog/test.mjs
```

Import is allowed to add a missing content-addressed payload, but it refuses to
replace different bytes at an existing hash path. `--check` works from a clean ESO
checkout and does not require the upstream repository.

## What captions and tags mean

Every entry contains objective media, support, decay, spectrum, band-energy and
resonance measurements. Until someone performs a controlled listening review, its
short caption and tags are explicitly marked as `objective-measurement-summary-v1`
and `measurement-proxies-only-pending-human-audition`.

Terms such as `bright`, `dark`, `sharp`, `smooth`, `open`, `muffled`, `damped` and
`reverberant` are collection-relative measurement proxies. They are useful for
narrowing the search, but they are not claims that a person heard that quality. File
and family names never determine these tags. Recommended gain and wet-mix values are
level-matched search seeds, not prescribed mastering values.

Human curation uses a fixed dry engine probe, level-matched output and blinded IR
identity. Record an accepted review in
`assets/builtin/ir-library/perceptual-reviews.v1.json` with exactly:

```json
{
  "id": "smooth-39",
  "sha256": "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc",
  "caption": "A concise caption established by the controlled audition.",
  "tags": ["example-reviewed-tag"],
  "reviewer": "review-record-identifier"
}
```

The generator rejects a review when its hash is stale or its ID is unknown. This
prevents a subjective annotation from silently transferring to different audio.

## Authoring selection

The authoring decision is the exact triple:

```json
{
  "release_identity": "1.2.0",
  "id": "smooth-39",
  "sha256": "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"
}
```

Only the selected IRs actually referenced by presentation routes belong in the
engine JSON. Do not copy the complete 73-entry palette into the engine. The authored
asset uses the portable developer locator `assets/impulse-responses/<id>.wav`; this
URI is not production authority:

```json
{
  "presentation": {
    "assets": [
      {
        "id": "smooth-39",
        "kind": "impulse_response",
        "uri": "assets/impulse-responses/smooth-39.wav",
        "sha256": "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"
      }
    ],
    "routes": [
      {
        "route": "exhaust.example",
        "source_gain_linear": 1,
        "impulse_response": "smooth-39",
        "impulse_response_gain_linear": 0.001,
        "wet_mix_01": 1
      }
    ]
  }
}
```

The release identity is carried by the build request and catalog selection record,
not duplicated inside `presentation.assets`. Production built-in resolution ignores
the locator and admits only the exact ID and SHA-256 from the release-bound runtime
catalog. A developer who opts into `--asset-root` must materialize the exact WAV at
the relative locator.

## Rights

The upstream repository's recorded `LICENSE` is MIT and covers redistribution of the
imported exact-byte library subject to preservation of its copyright and permission
notice. Every entry records that license path and SHA-256 and has
`redistribution_status: permitted-with-license-notice`. Crankwave's installed
distribution ships the required notice separately from the project's MIT License.

## Runtime compatibility

Release 1.2.0 admits every catalog entry. Twenty-four PCM16 responses stay on the
unchanged v1 conversion and fixed-kernel branches. The other 49 responses use the
additive v2 extended conversion and uniform-partitioned convolution branches,
including the original PCM24 response. Branch selection is derived from decoded
media shape and complete meaningful support; neither the engine author nor the model
selects an implementation branch.

The per-entry compatibility object records the exact native and WASM conversion
method identities, convolution method identity, decoded and converted sizes, branch,
capacity and legacy byte-preservation status. The catalog is only published with all
73 entries passing the end-to-end native catalog sweep.
