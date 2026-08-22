# Crankwave documentation

This directory contains current product contracts and technical references that
still explain executable behavior. Completed plans, listening diaries, rejected
experiments, and superseded contract revisions belong in Git history rather than
the active documentation set.

## Start here

- [`../README.md`](../README.md) — build, render, bake, playback, and integration
  overview.
- [`../MODEL.md`](../MODEL.md) — simulation, numerical, provenance, and model
  admission rules.
- [`CRANKWAVE_AUDIO_BRIDGE.md`](CRANKWAVE_AUDIO_BRIDGE.md) — simulator-free
  package playback API.

## Current public contracts

- [`contracts/BUILTIN_ASSET_CATALOG_V1.md`](contracts/BUILTIN_ASSET_CATALOG_V1.md)
- [`contracts/ENGINE_JSON_CAPABILITY_MATRIX.md`](contracts/ENGINE_JSON_CAPABILITY_MATRIX.md)
- [`contracts/ENGINE_SESSION_API.md`](contracts/ENGINE_SESSION_API.md)
- [`contracts/ENGINE_TELEMETRY_NDJSON_V1.md`](contracts/ENGINE_TELEMETRY_NDJSON_V1.md)
- [`contracts/CLI_RESULT_V1.md`](contracts/CLI_RESULT_V1.md)
- [`contracts/CRANKWAVE_CONTAINER_V1.md`](contracts/CRANKWAVE_CONTAINER_V1.md)
- [`contracts/INSTALLED_DISTRIBUTION_V2.md`](contracts/INSTALLED_DISTRIBUTION_V2.md)
- [`contracts/IR_AUTHORING_CATALOG_V1.md`](contracts/IR_AUTHORING_CATALOG_V1.md)
- [`contracts/M4_SIMULATION_MANIFEST_WIRE.md`](contracts/M4_SIMULATION_MANIFEST_WIRE.md)
- [`contracts/RESPONSIVE_PROFILE_SELECTION_V2.md`](contracts/RESPONSIVE_PROFILE_SELECTION_V2.md)

Machine-readable schemas under [`../schemas`](../schemas) are authoritative for
their wire formats. The version suffixes above name current public protocol
versions; they are not retained product-name compatibility layers.

## Internal technical references

The remaining files under [`model`](model) and milestone-prefixed contracts
describe algorithms or frozen oracle behavior still exercised by the current
implementation. Files under [`oracles`](oracles) record the pinned upstream
behavior used by the clean-room implementation. They are technical provenance,
not alternative product paths or current project plans.

Documentation for removed behavior is recovered through Git history. Do not add
new progress logs, local artifact inventories, or one-off acceptance reports here;
capture stable behavior in a current contract, test, schema, or source comment.
