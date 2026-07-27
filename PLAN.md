# Engine Sim Offline: source-informed clean-slate plan

Status: planning gate  
Branch: `clean-room/bmw-baseline`  
Date: 2026-07-27

## 1. Outcome

Build an offline-native engine simulator and audio renderer that:

- first reaches a narrow, known-good BMW M52 audible baseline;
- then replaces one approximation at a time with an established higher-fidelity method;
- renders deterministic WAV stems, a full mix, and physical telemetry without a GUI or
  audio device;
- calculates torque now, although the first public product capability is audio;
- later feeds responsive packages to lightweight Unity, Roblox, and Web runtimes.

“Bake” initially means running the expensive simulation off the realtime clock and
rendering one scenario to WAV plus telemetry. Multidimensional package compilation and
the lightweight runtime are later gates.

This is a source-informed rewrite, not a formal isolated clean-room process. Engine-sim
is a behavioral and conceptual oracle, not a runtime dependency. Production code is
not copied from the original or failed implementations. Equations, empirical constants,
data, assets, fixtures, and licenses receive explicit provenance.

This repository is the single implementation source during this work. The external
product document currently anticipates migration into `car-engine-studio`; later
integration must move or consume this implementation, never create a second maintained
copy.

## 2. Product contract

```text
AuthoredEngineDefinition
  sourced facts + confidence + inferences + defaults + artistic choices
                |
              resolve
                |
EngineSpec + Calibration + RenderScenario
                |
      heavy offline simulator
                |
 typed, per-cylinder/per-port physical observables
                |
       presentation acoustics
                |
 WAV stems + full mix + telemetry + manifest
```

The authored definition retains provenance; the simulator consumes resolved executable
inputs.

### Scenario ownership

Throttle, load, and RPM cannot all be independent commands.

| Scenario mode | Authoritative inputs | Results |
|---|---|---|
| Held speed | fixed RPM, throttle, and operating state | settled net torque and achieved load |
| Prescribed kinematic sweep | RPM trajectory, throttle, and operating state | actuator torque and inertia-corrected net torque |
| Load-target held capture | fixed RPM, target load, and operating state | solved throttle, achieved load, or unreachable-target error |
| Inertial dyno | throttle and brake/resistance | RPM trajectory and torque |

Every scenario also declares ambient pressure/temperature, fuel, initial thermal state,
warm-up/settling policy, ignition/events, duration, rates, quality, and deterministic
seed.

### Load and torque

- The internal achieved-load coordinate is signed net brake mean effective pressure,
  `net_bmep_pa`, including negative motoring/overrun.
- Load control distinguishes `target_net_bmep_pa` from `achieved_net_bmep_pa`; fired,
  fuel-cut, ignition-off, and motored states are explicit. It reports unreachable
  targets rather than silently clamping them.
- Package normalization, clamping, coast semantics, and the host-facing coordinate must
  be versioned before state captures are designed.
- Telemetry distinguishes instantaneous indicated torque, instantaneous net shaft
  torque, cycle-mean net shaft torque, and dyno reaction torque.
- Mapped power derives from cycle-mean net shaft torque and angular speed.
- Torque-map results declare ambient, thermal, fuel, accessory, and settling assumptions.
- A prescribed accelerating sweep accounts for inertia using the declared sign
  convention `net engine torque + actuator torque = equivalent inertia × angular
  acceleration`; raw dyno reaction is not mislabeled as steady net torque.

A future audio-follower runtime accepts RPM and achieved load; throttle is
non-authoritative transient context. A future torque-aware runtime accepts RPM and
throttle and returns torque; it cannot also accept authoritative achieved load.

### Current outputs

- The stems required by the current gate's frozen source-completeness matrix.
- A full mix made only from those declared stems.
- Relevant pressure, signed mass flow, valve, combustion, phase, RPM, torque, controller,
  and event telemetry.
- A manifest with resolved inputs, provenance link, commit, seed, rates, elapsed time,
  hardware/threads, and file hashes.

### Non-goals for the first slice

- No `.mr`, Piranha, engine-sim GUI, visualization, realtime pacing, or audio device.
- No compatibility with the failed implementation or permanent old/new modes.
- No complete vehicle, tire, suspension, road, or game controller.
- No gearbox embedded in an engine asset; a fifth-gear road pull is only an audition
  scenario.
- No general constraint solver where analytic crank-slider mechanics suffice.
- No package compiler/runtime until the required audible gate explicitly permits it.

## 3. Upgrade-ready architecture

```text
virtual test cell
  -> mechanism and valvetrain
  -> control volumes, gas flow, and physical pressure waves
  -> ignition, combustion, heat transfer
  -> torque, inertia, friction, pumping
  -> CaptureBlock
  -> excitation
  -> presentation propagation/radiation
  -> convolution/resampling/output
```

Rules:

- Boundaries use named SI quantities, explicit units/rates, and channel identities.
- No subsystem communicates through one undocumented “engine sound” scalar.
- Per-cylinder and per-port signals remain separate until an explicit acoustic stage.
- Engine-specific behavior is data, not executable branches.
- Physical, inferred, empirical, and artistic values remain distinguishable.
- One render session owns all mutable state and its seed; there are no mutable globals.
- Simulation time is independent of wall time, callbacks, and output sample rate.
- Capture and audio stages stream bounded blocks.
- Callers own job concurrency; sessions contain no hidden global thread pool.
- Intake/exhaust pressure waves and backpressure are physics and may affect filling and
  torque. Microphone, radiation, IR, and presentation DSP are one-way downstream.
- Internal simulator/capture contracts may evolve freely until the production-listening
  gate. A later runtime consumes compiled packages, not high-rate simulator state.
- The first headless surface is `render(spec, scenario, sink)` plus a thin CLI.

These seams permit isolated replacements such as better integration, combustion,
heat transfer, valve/port flow, 1D runner/pipe dynamics, torsional crank behavior,
source excitation, and propagation. They do not justify a generic plugin framework or
placeholder implementations.

## 4. Engine scope

The parity slice contains only what the BMW M52 pull requires:

- naturally aspirated four-stroke spark ignition;
- one rigid crankshaft with inline cylinders;
- fixed cam profiles;
- throttle, gas exchange, ignition, combustion, friction, and pumping;
- intake/exhaust paths and the exact reference-equivalent audible routes;
- one prescribed-RPM/throttle scenario.

After parity, held-speed/inertial test cells, idle/load coverage, starter, limiter,
overrun, and shutdown arrive as separate vertical slices.

Later fixtures:

- Honda B18C5 tests data-driven alternate valvetrain/control behavior and coherent
  sound/torque change; it is not a special code path.
- Toyota 3UR-FE tests V8 banks, firing order, and exhaust grouping.
- Forced induction, diesel, rotary, two-stroke, complex VVT, and powertrain ownership
  are later capabilities. Their required pressure/flow/source seams must remain
  possible, and their ordering conflict with the external roadmap must be resolved
  before generalization.

## 5. BMW oracle

Known-good reference:

```text
/home/cbethax/depot/dev/engine-sim-offline-failed/engine-sim/workspace/listening/
  bmw-m52b28-5th-gear-equivalent-dyno-1500-6500rpm.wav
```

- SHA-256:
  `f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb`
- Signed 24-bit PCM, mono, 192 kHz, 15 seconds, 1500–6500 RPM.

The reference capsule will make a self-contained copy and record verified scenario,
engine, revision, modification, parameter, asset, and license provenance. Verified
facts, inferences, and unknowns remain separate; missing traces, controls, or seeds are
never fabricated.

The fixture must cross the intended `CaptureBlock` seam, before presentation acoustics;
a post-mix WAV alone does not isolate the renderer. If no trustworthy fixture can be
recovered, work pauses for an explicit alternative-isolation decision. Physics,
excitation, and presentation acoustics are not first introduced together. A synthetic
tone/noise fixture may test DSP but cannot be presented as engine-audio progress.

The oracle is not presumed physically perfect. It is the minimum audible level the
rewrite must not lose before offline-fidelity replacements begin.

## 6. Evidence and acceptance

Before physics code, `MODEL.md` records for each subsystem:

- equation/method and authoritative source;
- the engine-sim concept reproduced or deliberately replaced;
- unit, sign, state, and boundary conventions;
- physical, sourced, inferred, and calibrated constants;
- the exact physical-observable-to-excitation mapping.

No unexplained acoustic weights or temporary tone/noise generators enter the accepted
engine path.

M1 proposes and the user explicitly approves two source-completeness matrices:

- M3: the oracle's two runtime reference exhaust routes and master, plus every
  additional route proven to exist in the oracle; all known omissions are explicit;
- production: exhaust, intake, mechanical, and master buses, with the evidence-backed
  model required to claim each one.

A manifest cannot make a gate easier by silently omitting a required bus. Changing
either matrix requires another explicit user approval.

Every audible candidate includes:

- previous accepted and candidate full mixes;
- affected stems;
- raw files plus separately labelled level-matched listening copies;
- identical scenario, RPM trajectory, sample rate, channel layout, and master processing;
- declared alignment, routing report, commit, seed, rates, elapsed time, and hashes;
- checks for NaNs, clipping, DC faults, discontinuities, duration, and RPM trajectory.

Metrics detect regressions; only listening approves sound. Poppery, crackling, generic
throbbing, air-only/buzz-only output, missing/mislabeled routes, destructive phase
cancellation, or material audible regression is failure regardless of metrics.

Hard rules:

- Every audible gate ends in a playable, end-to-end full mix.
- No knowingly incomplete route becomes the accepted default.
- No rejected candidate becomes a dependency for later work.
- No future milestone excuses broken current audio.
- Work stops at each listening gate until the user accepts or rejects it.
- An A/B switch exists only during evaluation; after acceptance the superseded
  implementation is removed. Git and accepted artifacts provide rollback.
- Before an upgrade, record one deficiency, established replacement, expected
  measurable/audible effect, compute cost, and deciding A/B scenarios.

## 7. Performance and verification

The standard benchmark is the 15-second BMW pull.

- Target: about 30 seconds or less per production-quality clip on this PC.
- Record build time separately from render latency.
- Record single-job latency and fixed-count concurrent throughput separately.
- A standard render over 60 seconds, or material concurrency collapse, blocks acceptance
  until profiled and explicitly approved by the user.
- Resolution controls may tune one implementation; they must not become two codebases.

Tests remain narrow:

- kinematics, dead centers, derivatives, firing/event order;
- sealed/flowing mass-energy sanity and flow/choke boundaries;
- burn fraction/energy and torque/work/power identities;
- convolver/resampler impulse behavior;
- deterministic reproduction;
- one finite, non-silent, correctly routed BMW smoke render.

Spectra, loudness, envelopes, and timing may flag changes but cannot certify sound.

## 8. Milestones and hard stops

Each checkbox is one reviewable, green commit. Split the checkbox before starting if it
cannot fit one commit.

### M0 — Plan

- [x] Establish `clean-room/bmw-baseline`.
- [x] Write, review, and commit this plan before implementation.

Exit: scope, risks, ownership, and stopping rules are explicit.

### M1 — Reference capsule

- [x] Preserve the BMW oracle and verified metadata in a self-contained capsule.
- [x] Record engine/scenario/revision/asset/license provenance and unknowns.
- [x] Propose the reference and production source-completeness matrices.
- [x] Stop for user review and freeze the approved matrices.
- [x] Capture a trustworthy BMW source/telemetry fixture at the `CaptureBlock` seam.
- [ ] If that fixture cannot be recovered, stop for an alternative-isolation decision.

Exit: the oracle is identifiable and replayable without depending on the failed tree.

### M2 — Model record and foundation

- [x] Create the minimal C++20/CMake project.
- [ ] Write `MODEL.md` before physics implementation.
- [ ] Define authored/resolved inputs, tagged scenarios, torque/load semantics,
      reachability reporting, `CaptureBlock`, and `RenderManifest`.
- [ ] Implement `render(spec, scenario, sink)` and the CLI shell.
- [ ] Implement deterministic scheduling and bounded streaming.
- [ ] Implement telemetry/WAV sinks and focused DSP tests.
- [ ] Drive the complete approved acoustic route from the BMW fixture.
- [ ] Render the fixture-based full mix against the oracle and stop for listening.

Exit: the user accepts the trace-driven renderer. A synthetic fixture makes no
sound-quality claim, and M3 cannot begin without this acoustic acceptance.

### M3 — BMW parity

- [ ] Implement BMW crank-slider mechanics and event scheduling.
- [ ] Implement the required fixed-profile valvetrain.
- [ ] Implement the minimum sourced gas, ignition, combustion, friction, and pumping
      models.
- [ ] Publish required pressure, flow, phase, and torque observables.
- [ ] Connect clean-slate physics to the already accepted renderer without changing
      that renderer.
- [ ] Render the canonical 15-second pull.
- [ ] Produce controlled oracle/candidate files plus routing and performance reports.
- [ ] Stop for user listening.

Exit: the user accepts the rewrite as at least comparable to the oracle. No fidelity
replacement starts before acceptance.

### M4 — Operating regression set

- [ ] Add the held-speed brake dyno.
- [ ] Render a modest BMW torque sweep with declared conditions and sourced plausibility
      bounds.
- [ ] Render low/middle/high RPM and load clips, then stop for user listening.
- [ ] Add the inertial dyno.
- [ ] Render its natural BMW pull, then stop for user listening.
- [ ] Add stable idle/low-load behavior.
- [ ] Render idle/low-load behavior, then stop for user listening.
- [ ] Add throttle application, lift, and ordinary-overrun behavior.
- [ ] Render those transients, then stop for user listening.
- [ ] Freeze the pull and affected clips as the minimum regression set.

Exit: fidelity changes cannot overfit one pull, and torque is characterized rather than
silently deferred.

### M5 — Isolated offline-fidelity upgrades

For each hypothesis:

- [ ] Document one deficiency, established replacement, expected result, and cost.
- [ ] Replace one subsystem behind an existing physical seam.
- [ ] Run focused invariant/convergence checks.
- [ ] Render the canonical pull and every affected regression clip against the previous
      accepted commit.
- [ ] Stop for user listening.
- [ ] Accept and remove the old path, or reject and redesign.

Hypotheses remain separate: integration/event handling, valve/port flow, control-volume
thermodynamics, burn law, wall heat transfer, runner/pipe waves, junction/boundary
losses, excitation, presentation propagation/radiation, cycle variation, and operating
transients.

Exit: only user-accepted replacements remain, and the user confirms that the accumulated
BMW result is meaningfully higher fidelity than M3 across the regression set. Otherwise
the project remains at parity and does not claim or proceed as an offline-fidelity
success.

### M6 — Production-listenable BMW set

- [ ] Complete and verify the required exhaust buses; render stems/full mix and stop for
      user listening.
- [ ] Complete the documented intake source model; render stem/full-mix A/B and stop for
      user listening.
- [ ] Complete the documented mechanical source model; render stem/full-mix A/B and
      stop for user listening.
- [ ] Add neutral/free-rev behavior; render and stop for user listening.
- [ ] Add explicit fuel cut distinct from ordinary overrun; render and stop for user
      listening.
- [ ] Add starter/crank/catch; render and stop for user listening.
- [ ] Add authored shutdown and distinct ignition-off rundown; render and stop for user
      listening.
- [ ] Add limiter entry, sustain, and exit; render and stop for user listening.
- [ ] Render the final full-load pull and light/high-load held points across the range.
- [ ] Validate full-mix routing, phase, and level.
- [ ] Report single and concurrent performance.
- [ ] Present the complete production set and stop for final BMW listening.

Exit: the user accepts the complete set at the declared performance budget.

### M7 — Responsive package proof

- [ ] Resolve move-versus-consume integration with `car-engine-studio`; do not duplicate.
- [ ] Design a minimal capture space from accepted BMW behavior.
- [ ] Compile and locally verify an actual versioned BMW audio package.
- [ ] Implement an audio-follower reference runtime accepting RPM, achieved load,
      throttle context, RPM slope, ignition, starter, gear/clutch context, and events.
- [ ] Return separate buses; the host owns placement, attenuation, occlusion,
      environmental reverb, and final mixing.
- [ ] Demonstrate responsive reconstruction without high-rate runtime physics.
- [ ] Stop for user listening.

Exit: runtime compatibility is demonstrated rather than inferred. Product signing and
permanent offline verification live in the selected production integration location.

### M8 — Generalization

- [ ] Add Honda alternate-valvetrain behavior and verify torque/sound coherence.
- [ ] Stop for Honda listening.
- [ ] Add Toyota V8 topology/exhaust grouping without degrading BMW.
- [ ] Stop for Toyota listening.

Exit: the accepted architecture generalizes beyond the inline-six.

### M9 — Later runtime capabilities

- [ ] Add torque-aware mode only after torque calibration and ownership are ready.
- [ ] Add engine-dynamics or powertrain ownership only through separate capabilities.
- [ ] Add signing/permanent verification in the selected product integration location.

Exit: hosts can opt into authoritative torque/dynamics without ambiguous RPM/load
ownership.

## 9. Commit and artifact policy

- Work on the dedicated branch until a merge is requested.
- Commit exactly one checklist item; split oversized items first.
- Never mix sound-affecting work with unrelated refactoring.
- Keep build/tests green at code commit boundaries.
- Never rewrite or discard user-owned history in the original or failed directories.
- Record every user-accepted listening commit.
- Keep anonymous scratch renders out of source history.
- Retain accepted artifacts and manifests deliberately; do not grow another anonymous
  multi-gigabyte artifact tree.
- Always provide a direct filesystem link to the latest listening set.

## 10. Success

- Baseline success: the BMW M3 parity gate passes by ear.
- Offline-fidelity success: the M6 BMW set passes by ear within the approved budget,
  with no rejected or knowingly incomplete path underneath it.
- Product-path success: M7 proves an actual responsive package/runtime without high-rate
  game-side physics.
- Generalization success: Honda and Toyota do not require replacing the accepted
  architecture.
- Torque-runtime success is reserved for M9, not inferred from simulator telemetry.
