# Continuous engine-audio atlas plan

Status: accepted architecture; implementation has not started.

Date: 2026-08-04

## Decision

The responsive one-cycle package and follower are rejected. They are not a legacy
format and will not receive a compatibility decoder. The replacement is one current
continuous audio-atlas contract, one baker, and one runtime.

The simulator remains the source oracle. Baking records its final authored monitor
buses together with the exact operating-state timeline. The lightweight runtime does
not approximate combustion or calculate drivetrain motion; it follows recorded
material from host RPM, load, throttle, state, and lifecycle events.

The accepted dense single-clock experiment is evidence, not production code. It
proved that the source simulation and baked-delivery premise are sound during a slow
loaded pull, while also confirming that isolated 720-degree grains are not a complete
product representation.

## Why the first package was rejected

The first implementation diverged from the empirically accepted engine-audio-lab
method in several material ways:

1. Live simulator cycle boundaries repeatedly interrupted a continuously retimed
   baked cursor. Those two clocks disagreed by as much as 31,685 delivery frames.
2. The package retained one cycle every 25 RPM and disabled neighboring-row
   variation. During the fifth-gear proof, the same waveform repeated 6--11 times.
3. Directional sources used an approximately 6.4-second exponential time constant,
   producing about 19 RPM of evolution inside each retained cycle. The accepted
   dense proof used a 36-second time constant and approximately 4 RPM rows.
4. Runtime load routing fell back to requested throttle rather than an exact
   PCM-aligned manifold/load timeline.
5. Tests established finite, unclipped, gap-free PCM with broadly plausible level.
   They did not establish faithful sound.

Changing only the boundary owner removed one distortion but exposed the frozen-row
loop. A disposable 4 RPM, slowly captured, bounded-variation, source-owned-clock
package sounded better in the same fifth-gear audition. That closes the forensic
question, but the lab's one-cycle method is still known to repeat at true steady RPM
and to lose the history of fast free-rev transitions.

## Product boundary

The baker consumes:

- one compiled engine and its required authored assets;
- a declared operating domain and capture policy;
- deterministic public randomness; and
- the exact final monitor buses intended for delivery.

The atlas runtime consumes:

```text
rpm
rpm slope
throttle
load / manifold pressure
ignition, fuel, starter, limiter
discrete engine state
lifecycle events
```

It outputs ordered audio buses. Power remains a later package output; the audio atlas
does not become an engine or drivetrain simulator.

## Representation

The atlas retains continuous performances. Exact 720-degree boundaries remain useful
metadata for phase measurement and safe handoffs, but normal running never copies and
repeats one isolated cycle as its fundamental unit.

### Moving segments

A moving segment is a continuous rising or falling performance over an overlapping
RPM band at one load/state anchor and one normalized RPM-slope range. Its PCM is kept
chronological. The timeline records exact audible-frame RPM, RPM slope, throttle,
manifold/load coordinates, torque when available, crank phase, and discrete state.

Several overlapping RPM bands and normalized-slope anchors may be captured
independently and concurrently. Overlap supplies contextual handoff material without
turning the whole range into one serial multi-minute job.

### Stationary tiles

A stationary tile is a long, settled, contiguous multi-cycle recording at an
RPM/load/state anchor. The complete retained interval is the repetition unit. A long
head/tail transition may loop the tile; it is never chopped back into independent
cycles.

Stationary tiles are added only where the declared runtime domain can dwell. The
first moving-segment audition intentionally precedes this work.

### Transient and lifecycle performances

Fast throttle application, lift, startup, shutdown, limiter, cam-profile transitions,
and other discontinuous history-dependent behavior use continuous performances with
measured entry/exit state. Fast free rev is represented by causal trajectory lanes,
not by forcing a steady or slow-moving segment to follow it.

## Sole package contract

The logical manifest schema is `engine-sim-offline/audio-atlas`. Its first slice has
this shape; fields are replaced in place when the greenfield contract changes:

```json
{
  "schema": "engine-sim-offline/audio-atlas",
  "id": "engine-atlas-id",
  "engine": "engine-id",
  "public_seed": "unsigned-integer",
  "audio": {"sample_rate_hz": 48000, "buses": []},
  "domain": {
    "minimum_rpm": 700,
    "maximum_rpm": 6500,
    "load_coordinate": [-1, 1]
  },
  "moving_segments": [],
  "stationary_tiles": [],
  "transient_performances": [],
  "lifecycle_performances": [],
  "artifacts": [],
  "provenance": {}
}
```

Each moving segment declares:

- direction, load/state anchor, and normalized RPM-slope envelope;
- captured and usable RPM/frame ranges, including overlapping guards;
- one immutable PCM artifact per bus;
- an exact audio-frame state timeline;
- exact fractional crank-cycle boundaries as metadata;
- source/configuration/renderer identities and payload hashes; and
- the handoff envelopes with which it was admitted.

Each stationary tile declares its settled state envelope, complete multi-cycle loop
range, transition range, and the same bus/timeline/provenance identities.

There is no `audio-package` decoder, v1/v2 switch, scheduler option, or legacy event
mode after replacement.

## Runtime algorithm

Normal running has exactly one time owner: the active atlas source cursor.

1. Select a segment causally from current RPM, normalized RPM slope, load, direction,
   and discrete state. Future controls or target-scenario samples are unavailable.
2. Seed its cursor at the nearest admitted source state and preserve measured crank
   phase.
3. Advance continuously. The instantaneous source step is corrected by
   `live_rpm / source_rpm_at_cursor` so engine order follows the host crank rate.
4. Retain chronological source evolution. Do not select a new source every cycle and
   do not accept live cycle events as normal-running seam triggers.
5. When the state leaves a segment's admitted envelope, choose an overlapping segment
   and perform one bounded, phase-preserving handoff. Both cursors remain continuous
   for that handoff; neither is repeatedly restarted.
6. At low normalized slope, enter a stationary tile. Loop its complete retained
   interval with the admitted long transition. Leave it through one moving-segment
   handoff.

During the web A/B proof, exact EngineSession state blocks are latency-aligned to A's
delivered PCM. A production game supplies the same logical state on its own clock.
Exact native cycle evidence may seed phase, validate capture, or mark lifecycle
events; it never owns normal-running atlas transitions.

## Bake strategy and wall-time boundary

The 4 RPM exhaustive proof produced four lanes in 183 seconds and a 201 MiB evidence
package. That is not an acceptable default product bake.

The production baker therefore:

- splits moving coverage into overlapping RPM bands;
- renders independent load, slope, band, tile, and event jobs concurrently;
- publishes only required delivery buses and compact timelines;
- starts with the smallest declared operating atlas;
- evaluates separate held-out performances; and
- adds a band, slope lane, stationary tile, or transient only where held-out residual
  evidence identifies missing coverage.

The target remains approximately 30 seconds of wall time for a typical engine build
on the development PC. A package may explicitly declare a larger build class, but the
baker may not silently turn every engine into a serial exhaustive capture.

## Admission and listening gates

Numerical checks can reject a package; they cannot certify that it sounds right.

Fail-closed admission rejects:

- a second normal-running boundary clock;
- repeated isolated-unit runs;
- non-monotone or discontinuous source timelines;
- missing state/load/slope coverage in the declared domain;
- phase, pitch, engine-order, modulation, loudness-envelope, gap, silence, clip, or
  payload-identity violations; and
- use of training/capture samples from the held-out audition performance.

Every sound-bearing slice ends with separate, manually selectable A and B files or
live paths. Combined A/B audio is not an audition substitute.

## Replacement sequence

1. Replace this plan and name the rejected architecture. No PCM change.
2. Add the sole atlas authoring and package contract; remove the old package contract
   in the same commit.
3. Add chronological moving-band capture and exact state-timeline publication.
4. Add the single-cursor moving-segment runtime; remove the old follower in the same
   commit.
5. Replace the web package integration while retaining the existing comparison mixer,
   shared resampler, ring, worklet, and direct Source A path.
6. Bake an independently captured BMW atlas and audition a held-out fifth-gear pull.
   Stop for user acceptance.
7. Add stationary tiles, then fast transient lanes, then load/state transitions, each
   behind its own immediate A/B listening gate.
8. Add lifecycle performances one at a time.
9. Profile package size, build time, runtime CPU, and adapter delivery only after the
   audio representation is accepted.

The first implementation slice ends at step 6. It does not claim free-rev, steady,
partial-load, or lifecycle completeness before those representations exist and have
been heard.
