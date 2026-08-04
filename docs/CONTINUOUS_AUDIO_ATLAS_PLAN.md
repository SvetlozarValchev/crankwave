# Continuous engine-audio atlas plan

Status: moving-segment proof accepted as internal evidence; interactive baked B is not
yet a user-facing deliverable.

Date: 2026-08-05

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

## User-facing acceptance target

A finite or scripted scenario is never the product acceptance surface. Such scenarios
may drive independent baker captures and held-out evidence, but the user-facing gate
is the existing **BMW M52TUB28 Interactive free rev** session with live throttle,
ignition, starter, limiter, and external-load controls. Source A and baked B must be
manually switchable there while the same live session continues.

The accepted fifth-gear slice below proves one representation lane only. It must not
be described as interactive baked audio or as a completed WASM product milestone.
Interactive B requires sufficient stationary, rising, falling, load, state, and
handoff coverage; outside that coverage the implementation is incomplete, even when
its fail-safe Source A fallback is correct.

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

The atlas retains continuous performances. Capture-rig motion state such as
HeldDyno ownership is not an atlas engine-state bit; ignition, fuel, starter, limiter,
and limiter-cut state are. Exact 720-degree boundaries remain useful
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

The first moving-segment proof retains the simulator's native 192 kHz Float32 monitor
PCM. There is no native 48 kHz converter yet, and sample-rate conversion may not be
hidden inside the representation change. A compact 48 kHz atlas becomes a separate
immediate A/B gate after the continuous cursor is accepted.

For ordinary constant-state moving lanes, current EngineSession telemetry supplies an
exact frame-addressed knot at every 20 ms block end plus fractional crank-cycle
boundaries. Capture adds the mean intake-manifold pressure already present in the gas
state; throttle may never be substituted for missing measured load. Higher-rate
discrete transition evidence is deferred with the transient/lifecycle slices that need
it.

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

## Accepted moving-segment gate

The first independently captured BMW moving atlas and its untouched held-out
fifth-gear run completed the step-6 gate on 2026-08-04. The audition retained
18.04 seconds from 1600.855 to 3816.172 RPM. Source A and atlas B remained separate;
B advanced only through the chronological atlas cursor and never copied held-out A.
The user accepted the result as "sounds identical almost," which is the intended
result for a representation-preservation gate.

The sealed evidence is exact:

- Source A WAV SHA-256: `c365c816b82e3afc7a42b0c9bffbdab2e2f0485156aece340137e86e190595bc`.
- Atlas B WAV SHA-256: `91e8b803cb07ee19e434680f25419b0e628ca0bbce9dc137587159907397d4be`.
- Atlas PCM SHA-256: `303efa06bdec9306ac06738fd6f42451135b1443e60fd78c3768df6e424e806b`.
- Compiled engine provenance: `1d81da0057a42fa37f138ef54881d431007409c1a2b90c58b62008f6bd3ae4cf`.
- Sealed renderer source closure: `4bafabed95d6c3401f5119c482edaa91a9a2f263eb6d3219a6bdfce6dbba85dd`.

Two provenance-only rebuilds left the complete atlas PCM and both accepted audition
WAVs byte-for-byte unchanged. The current native lane capture takes about 52.4
seconds on the development PC, so build-time parallelization remains future work;
it is not being mixed into the next sound-representation experiment.

## Replacement sequence

1. Replace this plan and name the rejected architecture. No PCM change.
2. Add the sole atlas authoring and package contract; remove the complete rejected
   package baker, decoder, follower, and web integration in the same commit. Direct
   Source A remains available throughout.
3. Add chronological moving-band capture and exact state-timeline publication.
4. Add the single-cursor moving-segment runtime.
5. Add the atlas web integration while retaining the existing comparison mixer,
   shared resampler, ring, worklet, and direct Source A path.
6. Bake an independently captured BMW atlas and audition a held-out fifth-gear pull.
   Stop for user acceptance.
7. Spike stationary idle, fast free-rev rise, and throttle-lift/fall material, then
   prove their handoffs internally without presenting a scripted run as the product.
8. Attach the accepted coverage to the BMW interactive free-rev preset and stop for
   live browser A/B acceptance under free controls.
9. Add remaining load/state and lifecycle performances one at a time.
10. Profile package size, build time, runtime CPU, and adapter delivery only after the
   audio representation is accepted.

The first implementation slice ended at step 6 and is accepted. It does not claim
free-rev, steady, partial-load, or lifecycle completeness before those representations
exist and have been heard. The next deliverable gate is not another scripted audition:
it is baked B operating in the existing interactive free-rev session.
