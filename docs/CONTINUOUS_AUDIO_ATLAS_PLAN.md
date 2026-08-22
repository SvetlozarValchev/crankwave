# Continuous engine-audio atlas plan

Status: held phase-texture plus sharp-transient browser experiment accepted by ear;
the implementation is still a BMW-specific research spike and is not merge-ready.

Date: 2026-08-06

## Decision

The responsive one-cycle package and follower are rejected. They are not a legacy
format and will not receive a compatibility decoder. The replacement is one current
continuous audio-atlas contract, one baker, and one runtime.

The simulator remains the source oracle. Baking records its final authored monitor
buses together with the exact operating-state timeline. The lightweight runtime does
not approximate combustion or calculate drivetrain motion; it follows recorded
material from host RPM, load, throttle, state, and lifecycle events.

The later held phase-texture experiment supersedes one part of that conclusion.
Isolated 720-degree material can participate when each operating cell retains a bank
of source cycles, aligns them to a shared crank phase, and separates a stable phase
mean from boundary-zero residual variation. What remains rejected is one frozen cycle
per cell, a live-cycle follower, or any architecture in which an isolated cycle is the
entire representation.

The accepted candidate has two layers:

1. A held phase-texture lattice provides the normal-running bed. The current BMW
   experiment has 10 RPM anchors by three load lanes, two dry exhaust buses, and 48
   source cycles per cell. Runtime interpolation operates on phase-aligned means and
   residual power; deterministic shuffled residual selection avoids adjacent repeats.
2. A sharp-transient layer adds causal throttle-attack and lift material before the
   shared presentation transfer and master stage. The runtime does not switch to a
   separately mastered transient recording.

This is an architecture decision, not an admission of the scratch implementation.
The generator, package, runtime modules, and browser integration currently live under
ignored research/build directories and contain BMW- and URL-specific assumptions.
They must be promoted as one generic contract and implementation, not copied into the
tracked tree verbatim.

## User-facing acceptance target

A finite or scripted scenario is never the product acceptance surface. Such scenarios
may drive independent baker captures and held-out evidence, but the user-facing gate
is the existing **BMW M52TUB28 Interactive free rev** session with live throttle,
ignition, starter, limiter, and external-load controls. Source A and baked B must be
manually switchable there while the same live session continues.

The accepted fifth-gear slice below proved one chronological representation lane. The
new phase-texture candidate has also been heard in the interactive free-rev and dyno
presets under live A/B switching. It is still not a completed WASM product milestone:
coverage and fidelity at settled idle and the first throttle-attack interval remain
below the source, and the accepted scratch package is not reproducible from tracked
source in a clean checkout.

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

Capture-rig motion state such as HeldDyno ownership is not an atlas engine-state bit;
ignition, fuel, starter, limiter, and limiter-cut state are. Exact crank-cycle
boundaries are authored metadata used to construct phase-coherent material. Runtime
phase is owned by the host RPM clock, not by simulator callbacks or a prerecorded
vehicle trajectory.

### Held phase-texture cells

A normal-running cell is a settled multi-cycle capture at an RPM, measured-load, and
discrete-state anchor. All audible buses share one phase solution. The retained bank
is decomposed into:

- a phase-aligned mean cycle, interpolated across neighboring operating cells;
- boundary-zero residual cycles, selected without adjacent repetition; and
- residual-energy metadata, interpolated separately from the mean waveform.

The mean preserves deterministic engine order while the residual bank preserves the
non-identical pulse texture that a frozen loop loses. Residual selection is seeded by
public package randomness and is independent per retained source unit where doing so
does not break inter-bus phase. Cell interpolation must use authored RPM and measured
load coordinates; requested throttle is not a substitute for load.

The bank size, RPM/load grid, phase period, source units, buses, and presentation
transfer are package data. The BMW experiment's 10-by-3 grid, 48 cycles, two exhaust
buses, 720-degree period, 192 kHz delivery, and RPM bounds are evidence values, not
runtime constants.

### Transient and lifecycle performances

Fast throttle application, lift, startup, shutdown, limiter, cam-profile transitions,
and other discontinuous history-dependent behavior use causal source-derived
performances or residual layers with measured entry/exit state. They are detected
from present and past host state only, mixed into the dry bus domain, and pass through
the same presentation transfer and master as the held texture. Fast free rev is not a
prerecorded vehicle scenario and the runtime never knows its future target RPM.

## Sole package contract

The sole logical manifest schema remains `crankwave/audio-atlas`, paired with
the sole authoring schema `crankwave/atlas-bake`. The tracked schemas still
describe the earlier moving-segment proof and deliberately reject phase-texture and
transient payloads. Therefore the research package cannot yet be admitted by the
tracked product.

The next contract commit replaces those moving-only fields in place. The replacement
must declare, without BMW assumptions:

- the RPM/load/discrete-state domain and cell coordinates;
- phase period, phase resolution, retained bank size, and interpolation policy;
- immutable payload artifacts for every ordered bus, with hashes and encoding;
- the shared cross-bus phase solution, mean cycles, boundary-zero residual banks,
  and residual-energy metadata;
- causal transient and lifecycle layers with entry/exit envelopes;
- presentation transfer and master parameters applied exactly once; and
- source, configuration, renderer/toolchain, and bake-input provenance.

There is no `audio-package` decoder, moving-atlas compatibility branch,
representation-version switch, v1/v2 scheduler option, or legacy event mode after
replacement. This is a greenfield project: the old contract, baker, loader, cursor,
fixtures, and web dispatch are removed in the same change that installs the admitted
replacement.

## Runtime algorithm

Normal running has exactly one time owner: a continuous crank-phase accumulator driven
by host RPM.

1. Locate neighboring admitted cells from current RPM, measured load, and discrete
   state. Future controls or target-scenario samples are unavailable.
2. Interpolate their phase-aligned means while preserving crank phase and inter-bus
   phase relationships.
3. Select boundary-zero residual units from deterministic shuffled banks. Interpolate
   residual power separately and normalize the mixed result without flattening load
   contrast.
4. Advance phase continuously from host RPM. Changing RPM changes phase velocity; it
   does not restart a grain or jump to a prerecorded point in a dyno run.
5. Detect throttle attack, lift, and later lifecycle/state changes causally. Blend the
   admitted source-derived layer in the dry domain, then run the combined buses
   through one presentation transfer and one master stage.
6. Fail closed or retain direct Source A when the requested state leaves declared
   coverage. Do not silently clamp an unsupported state and call it complete.

During the web A/B proof, exact EngineSession state blocks are latency-aligned to A's
delivered PCM. A production game supplies the same logical state on its own clock.
Exact native cycle evidence may seed phase, validate capture, or mark lifecycle
events; it never owns normal-running runtime transitions.

## Bake strategy and wall-time boundary

The 4 RPM exhaustive moving proof produced four lanes in 183 seconds and a 201 MiB
evidence package. That is not an acceptable default product bake. The accepted BMW
phase-texture experiment captures 30 independently renderable cells with concurrency
six in about 50.7 seconds and publishes an approximately 48 MiB package. This is a
large improvement and a valid research build, but it has not yet met the approximately
30-second ordinary-engine target.

The production baker therefore:

- renders independent RPM/load/state cells and event jobs concurrently;
- retains enough phase-aligned cycles for non-repeating texture, with bank size an
  authored quality/build-class choice;
- publishes only required dry buses, compact phase metadata, and one presentation
  description;
- starts with the smallest declared operating domain;
- evaluates separate held-out performances; and
- adds an anchor, residual capacity, transient, or lifecycle layer only where held-out
  residual evidence identifies missing coverage.

The research package retains 192 kHz Float32 material. Any production sample-rate or
encoding change is a separately audible gate; it may not be hidden inside contract
promotion. EngineSession capture must publish fractional crank-cycle boundaries and
the measured intake-manifold/load coordinate already present in the gas state.
Throttle may never be substituted for missing measured load.

The target remains approximately 30 seconds of wall time for a typical engine build
on the development PC. A package may explicitly declare a larger build class, but the
baker may not silently turn every engine into a serial exhaustive capture.

## Admission and listening gates

Numerical checks can reject a package; they cannot certify that it sounds right.

Fail-closed admission rejects:

- a second normal-running boundary clock;
- a frozen or short-period isolated-unit run where the retained bank promises
  variation;
- discontinuous phase, non-zero residual boundaries, or broken inter-bus alignment;
- missing state/load/slope coverage in the declared domain;
- phase, pitch, engine-order, modulation, loudness-envelope, gap, silence, clip, or
  payload-identity violations; and
- use of training/capture samples from the held-out audition performance.

Every sound-bearing slice ends with separate, manually selectable A and B files or
live paths. Combined A/B audio is not an audition substitute.

## Accepted held phase-texture experiment

On 2026-08-05 the user accepted both **BMW M52TUB28 · Live A/B · held texture +
sharp transients** and **BMW M52TUB28 · Dyno A/B · held texture + sharp transients**
by ear, including sharp throttle attack, lift, and a steady climb. Independent browser
routing probes confirmed that B used the baked command, production mixer, resampler,
ring, and worklet path. The B package contains only captured dry front/rear exhaust
payloads; it does not contain or copy the held-out Source A master.

The held-out dyno result is strong: zero-frame best lag, waveform correlation
`0.99142`, 10 ms envelope correlation `0.97096`, log-spectrum correlation `0.999918`,
level-matched spectral distance `0.423 dB`, and RMS delta `+0.057 dB`. This supports
promotion of the representation, not wholesale promotion of the implementation.

The same independent capture exposes the remaining audible-risk areas:

- the hard free-rev rise has waveform correlation `0.7595`, envelope correlation
  `0.6449`, and level-matched spectral distance `1.19 dB`;
- its first 250 ms attack falls to waveform correlation `0.338`, envelope correlation
  `0.424`, and spectral distance `2.83 dB`; and
- settled idle before and after the gesture differs by `+2.0 dB` and `-2.74 dB` RMS,
  with spectral distances `8.62 dB` and `7.43 dB`.

Those numbers do not overrule the user's positive listening result. They prevent us
from calling a BMW-specific scratch implementation robust when a second gesture or
engine may expose the mismatch more strongly.

## Merge blockers

The candidate becomes merge-ready only after all of the following are true:

1. The tracked authoring/package schemas express phase-texture, transient, lifecycle,
   presentation, payload identity, and provenance, and validators fail closed.
2. One tracked baker and one tracked runtime replace the moving-only implementation;
   no experimental schema or compatibility decoder remains.
3. RPM/load grids, bounds, phase period, bank size, sample rates, bus identities,
   transfer lengths, and engine identity come from the package/authoring contract.
   There are no BMW constants in generic code.
4. The web loader dispatches from the sole validated manifest, never from URL
   substrings. A clean checkout can build the workbench and reproduce the admitted
   package from tracked inputs.
5. Settled idle and the first throttle-attack interval are represented deliberately,
   then pass a fresh interactive source/baked audition. The dyno evidence remains a
   regression gate.
6. A second curated engine can bake, validate, load, and run through the generic path.
   This is an architecture check, not permission for metrics to replace listening.
7. Package size, peak memory, capture concurrency, and wall time are measured in the
   tracked tool. Missing the 30-second target must be explicit rather than hidden.

## Historical accepted moving-segment gate

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
seconds on the development PC. This evidence remains sealed as a regression oracle,
but its moving-only contract is superseded by the phase-texture decision above.

## Replacement sequence

1. Record this superseding decision and the honest evidence/blockers. No PCM change.
2. Replace the moving-only authoring/package schemas in place and remove the rejected
   contract's fixtures and decoder surface in the same commit.
3. Promote a generic held-cell capture and phase-texture publisher using the tracked
   EngineSession capture boundary and measured load.
4. Promote the generic phase-texture cursor, residual selection, transient layer, and
   single presentation path. Remove every engine-, route-, rate-, and URL-specific
   dispatch assumption.
5. Integrate the sole manifest into the existing comparison mixer, resampler, ring,
   worklet, and direct Source A web path.
6. Publish the BMW fixture from tracked inputs and preserve the accepted dyno as a
   held-out regression gate.
7. Correct settled idle and first-attack coverage, then stop for the user's live
   interactive A/B audition under free controls.
8. Bake and load a second curated engine through the same generic path.
9. Add remaining state and lifecycle coverage one audible slice at a time.
10. Profile and reduce package size, wall time, peak memory, and adapter CPU without
    changing the accepted sound behind a silent optimization.

The research experiment has completed the representation-discovery gate. Production
promotion begins at step 2; it is not complete merely because the ignored BMW browser
build sounds good on the gestures heard so far.
