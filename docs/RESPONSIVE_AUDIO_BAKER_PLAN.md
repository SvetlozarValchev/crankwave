# Responsive audio baker and live A/B plan

Status: first normal-running package vertical slice implemented and browser-verified;
paused at the required user-listening gate. The source renderer remains frozen at the
accepted `b098e8e` baseline.

Date: 2026-08-04

## Decision

Product work now moves from open-ended source-fidelity experiments to the responsive
offline baker. The accepted simulator remains the source oracle. A package player is a
derived delivery product and must never become a second engine simulator.

The existing native `bake()` operation renders one finite scenario into route WAVs,
masters, telemetry, and a manifest. The new baker compiles several controlled source
performances into a compact package that a lightweight runtime can follow from host
RPM, load, throttle, direction, ignition, starter, limiter, and event state.

No source-audio algorithm changes in this milestone. Any direct-session PCM change is
a regression. Fidelity queue items remain parked unless a separately sourced,
isolated candidate is opened later.

## Established reconstruction oracle

The method oracle is the sibling `engine-audio-lab` working tree at Git commit
`2ec9344361e307afd881a13c85f01b77b37e21f7`, principally its `README.md`,
`src/lab-types.ts`, `src/bake.worker.ts`, `src/baked-player.ts`, and
`src/baked-player.worklet.ts`. It established the following useful rules through
repeated live-versus-baked listening:

1. capture controlled, continuous power, intermediate-load, and coast tapes rather
   than pitch-shifting one acceleration recording into every operating state;
2. retain complete 720-degree four-stroke units with source context on both edges;
3. assign directional units to one uniform cycle-frequency/RPM grid while preserving
   measured source RPM and cycle-local load coordinates;
4. preserve one shared crank-phase scheduler across all running layers;
5. interpolate only between adjacent coherent load planes with linear-amplitude
   weights; equal-power blending of coherent recordings creates an audible level hump;
6. use a dense settled-idle pool rather than stretching the lowest directional row;
7. use small deterministic, exactly zero-mean neighboring-row variation to avoid a
   short repeated selector rhythm;
8. keep startup/catch, limiter, shutdown, and other discontinuous lifecycle behavior
   as native-rate event performances with measured transition checkpoints;
9. validate raw source tapes and individual planes before judging the complete blend;
10. compare the live source and baked reconstruction under the same controls, with
    listening as the final acceptance gate.

This project adopts those principles, not the lab's historical v4-v9 compatibility
surface. There is one current package contract and one player. While the project is
greenfield, an incompatible improvement replaces that contract and its fixtures
atomically; no legacy decoder or old/new playback switch is retained.

## Improvements available because we own the simulator

The lab had to infer cycle phase by latency-shifting block RPM telemetry and
integrating it. `EngineCaptureSample` already contains exact `theta_rad`,
`theta_cycle_rad`, operating state, and torque at the 20 kHz capture clock. The package
baker will derive exact ordered 720-degree boundary crossings from those samples and
project them once onto the rendered bus clock. The physical crossing is then shifted
by exactly 128 capture ticks, or 1,228.8 frames at 192 kHz, to account for the common
linear-phase delay of the causal reconstruction filter. That fractional shift is the
only common signal-alignment correction: authored propagation, route conditioning,
and IR phase remain in the continuously rendered tapes. Correlation remains a
diagnostic; it is not the phase authority.

The simulator also already preserves exhaust and intake route identities. Package
material remains bus-addressable so a future host can position intake and outlets as
separate emitters. `master.engine.audition` is the first end-to-end A/B monitor, not the
only production payload.

## Product flow

```text
engine.json + package-bake.json + referenced assets
                         |
                 validate and compile
                         |
       independent controlled source sessions in parallel
          power rise | part-load rise | coast fall | idle
          startup/catch | limiter | shutdown
                         |
       exact 720-degree marks + cycle-local operating coordinates
                         |
          uniform bank assignment + event seam extraction
                         |
       package manifest + bus tapes + immutable payload hashes
                         |
             lightweight package follower
                         |
           source/baked A/B in the same web harness
```

## Current package contract

The first contract is a directory bundle. A later container may pack the same logical
members without changing player behavior.

The manifest owns:

- engine, package, renderer-build, bake-plan, and source-scenario identities;
- one declared package sample rate and four-stroke cycle size;
- ordered bus identities and source-route metadata;
- power, part-load, coast, and idle tape references and payload hashes per bus;
- exact unit start/end frames, canonical selector RPM, measured source RPM, signed
  cycle-average load/torque, throttle, direction, and discrete engine state;
- uniform-grid spacing, edge guards, assignment error, and selector padding;
- lifecycle-event tape references, checkpoints, loop seams, and handoff metadata;
- deterministic selector seed; the single current follower defines and tests its
  accepted smoothing/overlap behavior rather than exposing versioned playback knobs;
  and
- the complete source provenance needed to reproduce the package.

Audio payloads are stored compactly for delivery and decoded to Float32 by the player.
Canonical 192 kHz render artifacts remain reproducible evidence rather than game
payloads. Package resampling and quantization receive their own source-versus-package
null and listening checks; they must not be hidden inside the cycle-reconstruction
audition.

## Runtime contract

The follower consumes host state:

```text
rpm
signed load (or the package's declared load coordinate)
throttle
rpm slope/direction
ignition, fuel, starter, limiter
discrete engine state and lifecycle events when declared
```

It outputs the package's ordered audio buses. It does not calculate drivetrain RPM,
simulate combustion, or publish authoritative power. The host remains responsible for
vehicle physics, emitter placement, distance, occlusion, room/cabin response, and the
final mix.

Normal running uses one shared continuously integrated crank clock. Each new unit
latches its source row and adjacent-plane weights at a declared boundary, overlaps the
successor with complementary short edges, and preserves the running bed continuously
through state transitions. Load mixing is adjacent-plane linear amplitude because the
captured planes are coherent recordings of the same firing sequence.

## Web source/baked A/B

The existing workbench gains one audition selector:

```text
A - direct EngineSession source
B - baked package follower
```

One EngineSession remains authoritative for physical telemetry and direct Source A
audio. B consumes that same returned RPM/load/state timeline after the declared
transport delay; it never runs a second vehicle or engine model. The user compares
them by manually selecting exclusive Source A or exclusive Baked B. Switching changes
gain routing only, so both paths continue advancing and neither one restarts.

A and B enter one shared canonical-clock comparison mixer before one shared device
resampler, AudioWorklet ring, and output graph. Neither side may have a private cabin,
distance, EQ, dynamics, normalization, or `AudioContext` path. This deliberately fixes
an engine-audio-lab harness limitation: its audible live and baked buttons controlled
different downstream graphs, so its raw paired captures were stronger evidence than
its default speaker A/B routing.

The harness reports source/package identities, selected rows and load planes, source
RPM, playback ratio, active event ownership, ring fill, underruns, uncovered frames,
exact-silent frames, clipping, and A/B level delta. A and B remain computationally
phase-locked, but only the user-selected signal is audible in both output channels.

The first named A/B procedures are:

1. steady holds at low/mid/high RPM on each captured load plane;
2. a slow rising and falling sweep across row and load boundaries;
3. throttle application and lift through power/part-load/coast;
4. settled idle and an idle-to-running transition; and
5. lifecycle events only after their isolated package assets are admitted.

### Temporary realtime preview clock

The browser's BMW source/baked preset temporarily uses the dedicated
`warm-running-free-rev-700rpm-10khz-preview.json` scenario. Only its physics and
capture clocks are 10 kHz; source processing, acoustics, delivery, and the shared A/B
output path remain at 192 kHz. All canonical scenarios and the already baked B package
remain 20 kHz captures.

This is an audition aid, not a new cooker-quality decision. Running the 20 kHz Source
A simulator and the B follower together missed the browser's realtime deadline and
produced audible output-ring underruns. The preview restores the historically proven
10 kHz source clock so perceptual A/B work can continue without first entering an
optimization project. Its browser gate requires zero shared-output underruns.

Because the existing package's cycle alignment was measured from a 20 kHz capture,
this mixed-clock preview is suitable for exclusive manual listening but is not a
sample-aligned null-test or transient-timing authority. The preset is explicitly
labelled `Source A 10 kHz / Baked B 20 kHz`, isolated from the canonical scenario,
and can be removed as one checkpoint when 20 kHz realtime execution is revisited.

## Implementation sequence and commit gates

1. **Plan and method boundary.** Record this decision and the adopted/rejected lab
   behavior. No PCM changes.
2. **Exact cycle evidence.** Publish callback-scoped exact cycle crossings and
   cycle-local operating summaries from the existing session transaction. Existing
   audio remains byte-identical.
3. **Package contract.** Add strict package-bake authoring, validation, in-memory
   package types, deterministic encoding, and a small synthetic fixture. No player.
4. **Controlled BMW sources.** Author independent power, part-load, coast, and idle
   captures. Run them concurrently, reject partial promotion, and retain raw tapes for
   inspection.
5. **Uniform cycle bank.** Assign exact complete units to one padded grid, prove
   uniqueness/context/load ordering, and publish one BMW package transaction.
6. **Reference follower.** Implement the shared-clock unit scheduler, deterministic
   neighbor bag, adjacent linear load interpolation, idle pool, and click-safe edges.
7. **Workbench A/B.** Feed live source state to the follower, add exclusive manual A/B
   routing and diagnostics, and preserve the existing direct-session workbench
   unchanged when no package is loaded.
8. **First listening stop.** Compare raw tapes, isolated planes, steady holds, and
   rising/falling controls. Do not add lifecycle events or platform adapters until the
   user accepts the normal-running reconstruction.
9. **Lifecycle assets.** Add startup/catch, limiter, and shutdown one at a time, with an
   immediate source/baked audition after each event path.
10. **Delivery.** Measure package size/load time/CPU, then expose the same follower core
    to Unity first and Roblox/Web adapters afterward.

Each numbered sound-bearing gate is one coherent commit and stops for listening. Tests
may reject discontinuities, missing coverage, clipping, silence, or identity drift;
they cannot certify perceptual equivalence.

## Performance boundary

The existing six-scenario BMW native batch completes in approximately 30.4 seconds on
the development PC. Package captures remain independent jobs and should run
concurrently within explicit CPU and memory limits. The initial normal-running package
targets roughly that wall-time class; expanding the RPM/load/event domain must not turn
one package into a serial multi-minute bake without first profiling and changing the
capture strategy.

The first clean Release benchmark of the four 20-second BMW source recipes completed
successfully in parallel in 40.26 seconds wall time and 158.97 aggregate CPU seconds.
The generic scenario publisher wrote 442 MiB because it retained every diagnostic bus;
the first package retains only four mono audition tapes, approximately 43 MiB at the
temporary 192 kHz Float32 reconstruction gate. This establishes capture computation as
the present wall-time constraint and keeps the desired 30-second class as a measured
optimization target rather than an assumed property.

## First audition boundary

The first audition occurs when one BMW package can follow live RPM and load across its
normal-running domain in the workbench. Required evidence is:

- unchanged direct-session A output;
- the exact package and source identities;
- raw power, part-load, coast, and idle tapes;
- A/B steady holds and one shared rising/falling control trace;
- zero follower scheduling gaps, exact-silent running frames, clipping, and invalid row
  or load-plane transitions; the temporary 10 kHz browser preview must also report
  zero shared output-ring underruns; and
- clickable paths or a documented local URL for immediate listening.

Work stops at that point for user audition. A later lifecycle or fidelity phase may
not be used to excuse a defective normal-running reconstruction.

## First audition implementation evidence

The first vertical slice uses package
`bmw-m52tub28-cleanroom-normal-running` with exact engine and payload hash admission.
The workbench always advances one EngineSession. Every source block supplies both the
direct A samples and the RPM/load/cycle clock consumed by B; selecting Source A or
Baked B changes only the exclusive output route. There is no split, sum, alternating,
or independently simulated comparison mode.

The 2026-08-04 headless browser run performed a real button-driven
Source A -> Baked B -> Source A switch while the BMW session remained running. Its B
diagnostic at 1,478 rpm reported a nonzero peak, zero clips, zero uncovered frames,
and zero exact-silent normal-running frames. The full runtime unit suite and repository
workbench smoke also passed. Listening remains the acceptance authority, so lifecycle
assets and delivery adapters remain blocked on the user's audition.
