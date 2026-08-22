# CRANKWAVE container v1

## Scope

CRANKWAVE v1 is an crankwave-owned, deterministic, uncompressed carrier
for an already validated responsive package tree. It is deliberately independent
of any editor or service. The carrier does not define package JSON,
licensing, signing authority, runtime compatibility policy, compression, or audio
reconstruction semantics.

The ESO pack command requires one strictly validated package entry point at
`crankwave.json`:

```json
{
  "schema": "crankwave/crankwave-package",
  "version": 1,
  "engine_id": "example-engine",
  "runtime": {
    "kind": "responsive-audio",
    "manifest_path": "runtime.json",
    "manifest_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
  }
}
```

The object is closed to unknown fields. The engine ID uses lowercase letters,
digits, `.`, `_`, and `-`, begins and ends with a letter or digit, and is at most
128 bytes. `crankwave.json` itself is limited to 16 KiB. The runtime manifest is
an in-tree portable path other than `crankwave.json`; its digest binds the exact
manifest bytes. Package semantics below that manifest remain versioned by the
responsive runtime rather than the carrier.

All integers are unsigned little-endian. All offsets are absolute byte offsets from
the beginning of the file. A conforming file has no alignment padding, gaps, or
trailing bytes.

## Fixed 128-byte header

| Offset | Bytes | Field | v1 value or meaning |
|---:|---:|---|---|
| 0 | 8 | magic | ASCII `CRKWAVE1` |
| 8 | 2 | version | `1` |
| 10 | 2 | header byte count | `128` |
| 12 | 4 | flags | `0` |
| 16 | 4 | entry count | 1 through 8,192 |
| 20 | 4 | index-entry prefix byte count | `56` |
| 24 | 8 | index offset | `128` |
| 32 | 8 | index byte count | exact encoded index size |
| 40 | 8 | payload offset | `128 + index byte count` |
| 48 | 8 | payload byte count | sum of all entry byte counts |
| 56 | 8 | container byte count | exact file size |
| 64 | 32 | index SHA-256 | exact bytes in the index range |
| 96 | 32 | payload SHA-256 | exact bytes in the payload range |

Unknown versions, flags, header sizes, and index-prefix sizes fail closed.

## Canonical index

There is one variable-size index record per entry. Records are strictly sorted by
their path bytes. Each record consists of:

| Relative offset | Bytes | Field |
|---:|---:|---|
| 0 | 2 | path byte count |
| 2 | 2 | flags, always `0` |
| 4 | 4 | reserved, always `0` |
| 8 | 8 | absolute payload offset |
| 16 | 8 | payload byte count |
| 24 | 32 | payload SHA-256 |
| 56 | variable | path bytes, without a terminator |

Payload ranges occur in the same order as index records, are exactly contiguous,
and cover the complete payload region. An empty regular file is represented by a
zero-byte range and the SHA-256 of the empty byte string.

## Portable paths

Paths are canonical relative ASCII paths with `/` separators. Each path is at most
512 bytes; each segment is 1 through 127 bytes. A segment begins and ends with a
lowercase ASCII letter or digit and otherwise contains only lowercase letters,
digits, `.`, `_`, or `-`. Empty segments, `.`, `..`, absolute paths, backslashes,
hidden-file syntax, trailing dots, and Windows device names are rejected. Duplicate
paths are rejected.

Restricting the grammar instead of normalizing input makes one package tree map to
one byte identity on case-sensitive and case-insensitive hosts alike.

## Determinism and verification

Packing sorts entries by canonical path and concatenates their exact bytes without
timestamps, host paths, permissions, ownership, compression, random values, or
other environment-dependent metadata. Reordering source entries therefore does not
change output bytes.

Inspection verifies the exact structural bounds, canonical index, and index digest.
Full verification additionally verifies the aggregate payload digest and every
entry digest. A signature system may sign the complete container bytes, but signing
is intentionally outside this format and library.

The v1 bounds are 8,192 entries, 1 GiB per entry, and 4 GiB per container. Readers
must apply the bounds before allocation or iteration derived from untrusted fields.
