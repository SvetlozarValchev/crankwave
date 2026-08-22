# IR authoring catalog v1

## Boundary

`ir-authoring-catalog.v1.json` is the semantic palette presented to trusted engine
authoring tools and AI authoring agents. It is distinct from
`assets/catalog.v1.json`, which is the renderer's minimal technical allowlist.
The authoring catalog explains the choices; the technical catalog resolves only an
exact asset kind, stable ID, and SHA-256 to verified bytes.

Each installed release carries the authoring artifact at
`<datadir>/crankwave/<release_identity>/assets/ir-authoring-catalog.v1.json`.
Its schema is `crankwave/ir-authoring-catalog.v1`. The catalog and all
content-addressed WAV payloads are members of the installed `release.json` closure.
Their exact bytes therefore contribute to the release binding digest.

Consumers do not discover this filesystem path. They invoke:

```text
crankwave inspect-ir-catalog --result-format json
```

Success uses `crankwave.cli-result.v1` and returns the exact catalog as a
JSON object together with its byte SHA-256 and entry count. The command fails closed
when the catalog is missing or malformed, its `release_identity` differs from the
running Crankwave release, or an exposed `id` + `sha256` pair is not admitted as audio by
the adjacent technical catalog.

## Selection contract

An authoring decision is the exact tuple:

```text
Crankwave release_identity + IR id + IR sha256
```

The caller first pins the installed Crankwave release and `release.json` binding, then
queries that same executable. It may use captions, perceptual tags, measurements,
recommended ranges, compatibility state, provenance, and rights status to select an
entry. It must retain the selected release, ID, and digest in its authoring evidence.
It must not reconstruct an ID from a filename or carry a selection across a different
Crankwave release without querying and validating it again.

Only the selected, route-used assets belong in `engine.json`. The complete palette
does not. For example:

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
        "route": "exhaust.front",
        "source_gain_linear": 0.5,
        "impulse_response": "smooth-39",
        "impulse_response_gain_linear": 0.001,
        "wet_mix_01": 1.0
      }
    ]
  }
}
```

The canonical catalog-authored URI is
`assets/impulse-responses/<id>.wav`. It is a portable relative developer locator,
not selection authority. The production catalog resolver ignores `uri`; it requires
the exact authored ID and SHA-256 and resolves `assets/payloads/<sha256>`. An explicit
developer asset-root workflow must materialize the exact selected bytes at that
relative engine-package path. Existing engine JSON remains valid and retains the same
resolution behavior.

Crankwave rejects unused asset declarations. An authoring agent must therefore add one
asset definition for each distinct selected IR, reference it from at least one
presentation route, and remove definitions no route uses. Multiple routes may share
one selected definition or select different catalog entries.

## Meaning and limits

The entry caption and tags are controlled authoring guidance, not a promise that an
IR alone will produce a perceptual property. Objective measurements describe the IR
bytes under the catalog's fixed measurement method. The audible result also depends
on engine topology, source routing, route gain, IR gain, wet mix, conditioning, and
mastering. An agent should use the catalog to form a causal first choice and confirm
that choice with a short level-matched audition.

Compatibility is release-specific. A consumer may select only entries whose state is
`selectable` in the queried release. Non-selectable entries may remain visible to
explain the complete approved source palette, but must not be emitted into an engine
for that release.

Rights and provenance fields are evidence and policy inputs. The bundled library's
explicit `MIT` status is based on the upstream repository license recorded by path,
commit and SHA-256. Redistribution is permitted with that license notice, which the
installed Crankwave distribution carries in `licenses/THIRD-PARTY-NOTICES.md`. Consumers
must apply the explicit rights status and notice requirement rather than infer terms
from a filename, source path or caption.
