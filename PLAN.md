# Engine Sim Offline: greenfield product cutover

Status: active — headless parity completion

Branch: `clean-room/bmw-baseline`

Date: 2026-08-02

Current checkpoint: **post-slice-15 parity closure — dynamic master/slave and
remaining meaningful multi-crank cases**

This roadmap supersedes the previous BMW-first M4--M9 roadmap. Historical milestone
documents remain useful evidence, but they do not authorize current implementation
work or define the product architecture.

## 1. Outcome

Build one engine-audio implementation with this product flow:

```text
engine.json + scenario.json + referenced assets
                         |
                  parse and validate
                         |
               resolve and compile once
                         |
          CompiledEngine + CompiledScenario
                         |
                    EngineSession
                         |
        timestamped controls and process_block()
                         |
             PCM buses + physical telemetry
                  /                     \
       unpaced native bake          WASM block stream
              |                           |
     WAV/manifest/CLI          Worker -> ring buffer -> AudioWorklet
                                          |
                                  interactive HTML UI
```

The same compiled engine, mutable session, and block-processing path serve offline
rendering and interactive playback. “Offline” means the simulation is independent of
wall time and may run faster or slower than realtime; it does not mean a separate
sound model or a multi-minute mandatory bake.

The first public capability is engine audio. Existing torque and physical telemetry
remain available where the model already computes them. Authoritative game-facing
power, compiled lightweight audio packages, and Unity/Roblox adapters are later
products built on this boundary.

## 2. Greenfield rules

- There is one production path and one current input contract.
- There are no legacy modes, old/new switches, schema translators, aliases, deprecated
  fields, or backward-compatibility layers. Old input fails clearly.
- `.mr`, Piranha, the engine-sim GUI, and code from the failed fork are not production
  dependencies.
- Engine-specific behavior is JSON data, never a BMW/Honda/Toyota branch in the
  simulator.
- Product JSON is an authoring API, not a serialization of current C++ implementation
  structs. Internal types and method identities may change without changing the
  authoring vocabulary.
- Every JSON field is classified as **executed**, **derived**, **metadata**, or
  **unsupported**. A field must not appear to tune sound or power if the runtime merely
  stores, validates, or hashes it.
- Unsupported topology or behavior fails during compilation. It is not accepted and
  silently approximated.
- Physical geometry and state use named SI quantities. Artistic presentation values
  such as route gain, wet mix, microphone/IR choice, and monitoring gain remain
  explicitly separate.
- One `EngineSession` owns all mutable simulation, DSP, random, and control state.
  There are no mutable globals or hidden thread pools.
- Simulation time is independent of callbacks, wall time, and the audio device rate.
- The simulator, excitation, and presentation stages stream bounded blocks. Offline
  rendering is just an unpaced loop over those blocks.
- The native and WASM products use the same C++ simulation and DSP. JavaScript does not
  reimplement either.
- Callers own concurrency. Independent clips and sessions may run concurrently.

## 3. Product input contract

`engine.json` owns the reusable engine asset:

- identity and display metadata;
- cycle, layout, banks, cylinders, crank phasing, and firing order;
- bore, crank throw, rod/piston/head geometry, and the references from which stroke
  and compression are derived;
- intake/exhaust volumes, ports, manifolds, and route bindings; the compiler derives
  executable gas volumes and flow edges rather than asking authors to duplicate them;
- valvetrain, ignition, combustion, gas exchange, heat, friction, and accessory
  parameters actually supported by the compiled model;
- excitation and presentation routes, route gains, conditioning, IR/asset references,
  publication buses, and audition mix;
- optional source/provenance annotations that do not burden normal authoring.

`scenario.json` owns one finite operating and recording request:

- exactly one of the seven implemented motion owners: `held_speed`,
  `prescribed_kinematic_sweep`, `held_dyno`, `load_target_held_capture`,
  `inertial_dyno`, `free_engine`, or `free_vehicle`;
- throttle and other control trajectories;
- ignition, fuel-cut, limiter, startup/shutdown, and other events only when implemented;
- ambient, fuel, initial thermal state, preparation, duration, rates, quality, and seed;
- output selection and render destination policy that is not part of the engine asset.

A FreeEngine request does not replace the engine's inertia with a scenario total.
`attached_inertia` is an optional nonnegative crank-referred addition, and
`external_resisting_torque` is an optional nonnegative right-continuous trajectory.
Omitting either means canonical positive zero.

RPM, throttle, and load cannot all be authoritative simultaneously. Each scenario mode
declares which values are commands and which are results. Interactive sessions use the
same rule: imposed-RPM operation, internally dynamic operation, and a later
load-following mode are distinct capabilities.

Session creation separately requires `finite_scenario` or `open_ended`.
`finite_scenario` executes the JSON recipe and horizon exactly. `open_ended` executes
the authored pre-audible history through the audible handoff, holds the
right-continuous control snapshot, and continues the same mutable
crank/gas/random/filter/convolution state until paused, restarted, destroyed, or
faulted. FreeEngine, HeldDyno, and FreeVehicle implement that lifetime; none loops a
finite recipe or reports elapsed-time completion. A separate `finite_scenario` session
remains authoritative for exact authored capture and export.

Asset references are relative to the engine asset and are content-verified. Resolution
generates stable IDs, derived quantities, method selections, and provenance internally;
authors do not hand-write claim IDs, implementation hashes, or resolved C++ records.

## 4. BMW migration fixture

The accepted BMW is retained only as an automated migration fixture:

```text
reference/oracles/bmw-m52b28/
  bmw-m52b28-last-good-ffcc45c-dyno-1500-6500rpm.wav
```

The accepted historical WAV SHA-256 is:

```text
87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444
```

Its sound-bearing PCM24 `data` chunk is 8,640,000 bytes with SHA-256:

```text
176010069c88c99a3cc8262099fa5f02eba3af9517b1c92e148d88ace869756f
```

The different PCM hash
`2153869958bb924e4eda277a37e95eab1abb7c29aa9fa389c1fa8f879e7bfdcf`
belongs to the older tracked `bmw-m52b28-5th-gear-equivalent` recording and is
not the user-approved `ffcc45c` migration oracle.

The fixture proves that plumbing and architecture changes preserve the accepted sound.
It does not define the JSON vocabulary, impose a six-cylinder/two-route product limit,
or remain as an executable BMW factory.

During checkpoints 2--5, the hardcoded BMW factory existed only as a migration-test
oracle, never as an alternate production input. Checkpoint 6 deleted that factory and
its exact BMW validators and provenance builders after resolved identity and PCM
equality were proven. The tracked historical WAV, JSON-compiled request/WAV goldens,
and focused automated test remain. The old BMW-specific provenance and request hash
did not become compatibility targets.

Core 192 kHz PCM and the encoded PCM24 `data` chunk must remain byte-identical through
the cutover. The generic WAV container receives a new deterministic golden because its
INFO metadata truthfully carries the new generic presentation and source-matrix IDs;
retaining obsolete IDs merely to reproduce the historical whole-file hash is forbidden.
Browser device-rate conversion is compared before that final adapter; an AudioContext
resampler is not expected to reproduce a 192 kHz WAV container.

The current generic identities are:

```text
simulation request SHA-256: e01b872b91f142ef65633783a736cbea79169e99b4c45e8df6d68ffa481369d6
audition WAV byte count:    8640586
audition WAV SHA-256:       f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552
```

Checkpoint 7 truthfully renamed the route-publication and audition-mix methods from
fixed two-route identities to ordered N-route identities. This metadata-only change
added 14 bytes to the WAVE container; the accepted PCM24 `data` chunk did not change.
Operating-bench checkpoint 1 added the resolved E36 evaluation rig to package
provenance. That changed the request identity above without changing the WAVE or PCM.

## 5. Ten cutover checkpoints

Each numbered checkpoint is one reviewable commit. A checkpoint is complete only when
the focused tests and its stated gate pass. Do not combine checkpoints, and do not mix
sound-model changes into this cutover.

### 1. Freeze the product boundary

- Replace the historical milestone roadmap with this roadmap.
- Record the product JSON/session/WASM ownership rules and the BMW fixture identity.
- Inventory current input fields as executed, derived, metadata, unsupported, or absent.

Gate: the intended public vocabulary and every known unsupported capability are
explicit; no production code changes.

### 2. Add the JSON authoring layer

- Add plain product-facing engine, presentation, asset, and scenario DTOs.
- Add strict JSON parsing with precise paths and errors.
- Reject unknown fields, stale schema identifiers, non-finite numbers, invalid units,
  dangling references, and unsupported enum values.
- Keep parsing free of simulation, filesystem publication, and BMW-specific logic.

Gate: focused parser tests cover a minimal valid engine and representative invalid
documents; rendering is unchanged.

### 3. Add generic resolution and compilation

- Compile authoring DTOs into the existing resolved engine, presentation, randomness,
  source-policy, asset, and scenario contracts.
- Generate derived identities and provenance internally.
- Establish one portable `CompiledEngine` boundary suitable for native and WASM use.
- Fail closed when a syntactically valid request names an unimplemented capability.

Gate: one synthetic non-BMW input within the currently admitted executable topology
resolves deterministically without an engine-name branch. Configurations outside that
topology fail with explicit capability diagnostics; rendering is still unchanged.

### 4. Migrate the accepted BMW to JSON

- Mechanically express the current accepted BMW engine, presentation, assets, and dyno
  scenario in the product JSON contract.
- Compare an execution-value projection of the JSON-compiled inputs with the temporary
  hardcoded factory. Resolution IDs and provenance identities are deliberately excluded
  from that projection.
- Establish a stable request-identity golden produced by the generic JSON compiler.
- Do not retune, normalize, “correct,” or reinterpret any sound-producing value.

Gate: the two authoring routes produce identical execution values and byte-identical
PCM plus PCM24 `data` chunks. The JSON path has deterministic new request and whole-WAV
identities; it does not emulate old BMW/MR/fixture provenance solely to preserve an
obsolete container hash.

### 5. Make the native JSON renderer real

- Implement a thin CLI over the JSON compiler and existing renderer.
- Admit explicit engine, scenario, asset-root, and output-directory arguments.
- Keep directory publication, WAV encoding, manifests, and native execution evidence
  outside the portable engine core.

Gate: a clean CLI render of the JSON BMW reproduces the accepted PCM24 `data` bytes,
the deterministic generic WAV golden, and a valid manifest.

### 6. Remove executable engine profiles

- Delete the hardcoded BMW factories, exact BMW admission validators, and profile
  provenance builder from the production dependency graph.
- Remove temporary profile selection and all remaining compatibility-shaped entry
  points.
- Make JSON compilation the only way production code creates an engine.

Gate: the normal build contains no executable BMW profile path, and the JSON-only clean
render retains exact accepted PCM plus its deterministic generic container.

### 7. Generalize the executable engine

- Replace exact six-cylinder, inline-layout, two-exhaust-route, and fixed publication
  assumptions with session-owned vectors and ID bindings.
- Generalize bank/cylinder/head/cam/intake/exhaust/manifold/route ownership needed by the
  authoring contract.
- Preserve deterministic ordering and the exact arithmetic order for the BMW fixture.
- Do not add a new sound algorithm, new source bus, or speculative physical model here.
- Reject configurations requiring behavior not yet executed by the simulator.

Gate: a structurally different small engine reaches the executable boundary, while the
BMW PCM remains byte-identical and its generic WAV remains deterministic.

Completed evidence: a directly authored inline twin with one exhaust/presentation route
executes the complete compiled render path. The BMW retains PCM24 `data` SHA-256
`176010069c88c99a3cc8262099fa5f02eba3af9517b1c92e148d88ace869756f`; its truthful
ordered-N-route container is 8,640,586 bytes with SHA-256
`f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552`.

### 8. Establish `EngineSession`

- Expose immutable compiled configuration separately from mutable session state.
- Add a bounded `process_block()` API accepting timestamped control/event changes and
  returning PCM buses plus telemetry.
- Route the existing simulation -> capture -> excitation -> presentation chain through
  that API.
- Implement native offline rendering as an unpaced loop over the same session.
- Treat structural/config edits as compile-and-create-session operations. The current
  inertial-dyno session admits timestamped throttle, ignition, and fuel commands only
  after preparation; other live commands fail closed.

Gate: the old whole-render orchestration is gone, native block and offline paths are one
implementation, and the BMW PCM remains byte-identical for every tested block and for
the complete render.

Completed evidence: `CompiledScenario -> EngineSession::process_block()` is the sole
simulation/excitation/presentation execution path. The native `bake()` adapter drives
that session unpaced and owns only deterministic evidence plus transactional artifact
publication. The former public `render()` API, opaque whole-render job, fused
presentation session, and standalone render scheduler are deleted.

The BMW session exposes 1,072 exact 20 ms blocks: 322 preparation and 750 audible.
Every audible block quantizes byte-for-byte to the accepted 8,640,000-byte PCM24
payload with SHA-256
`176010069c88c99a3cc8262099fa5f02eba3af9517b1c92e148d88ace869756f`.
The clean native bake retains simulation-request SHA-256
`df65b324c78d43c1b651ab69d217483d3b489199fbb1c5a2a4ad9da7e2143efd`
and the 8,640,586-byte audition WAVE SHA-256
`f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552`.

Timestamped throttle, ignition, and fuel controls are causally projected into the
inertial-dyno physics clock. Preparation, late, post-horizon, and terminal commands
fail explicitly. A controlled run withholds authored-request-bound dyno evidence until
a later command-journal identity exists. Simulation owns neutral control values while
the session layer owns ordering and projection, so the portable target graph remains
acyclic.

### 9. Compile and verify WASM

- Isolate the portable core from Linux execution inspection, filesystem sinks, native
  threads, and process-global assumptions.
- Expose JSON compilation, session creation/destruction, control submission,
  `process_block()`, PCM retrieval, telemetry, and structured errors.
- Use preallocated/bounded memory across the realtime block boundary.
- Remove the current transient per-block capture-contract validation allocations before
  admitting the fixed-memory WASM target.
- Add a headless WASM parity runner; do not implement a second JavaScript simulator or
  DSP path.

Gate: native and WASM produce the same frame/event topology and deterministic
telemetry for the same control schedule. Core PCM is compared headlessly against
predeclared tight sample-error bounds; each target retains its own exact hash. Exact
cross-target equality is required only if the admitted numeric runtimes actually
produce it.

Completed evidence: [`c_api.h`](include/engine_sim_offline/c_api.h) is the one
exact-version foreign-runtime boundary. It owns context/kind/slot/generation-checked
engine, scenario, and session handles; performs transactional strict JSON
parse-and-compile; exposes typed controls, caller-owned PCM/telemetry, and structured
diagnostics; and contains all C++ exceptions. A live session retains the immutable
compiled inputs it needs after parent handles are released. Caller buffer capacity is
preflighted before advancement, and successful control submission plus warmed
`process_block()` use only preallocated bounded storage.

The browser module is an Emscripten wasm32 build with fixed 128 MiB memory, memory
growth and filesystem disabled, and the same C++ compiler/session/DSP closure as
native. Native retains its exact SysV x87 method identities; wasm32 identifies its
IEEE binary128 extended operations separately and fails closed on any other
long-double format.

The pinned 18-block parity fixture proves exact bus topology, clocks, control
acceptance/rejection, discrete telemetry, and completion state. Semantic transcript
SHA-256 is
`1493a854b9fef0905cb73f64c6b46c3993d7f7471ca15ee8d39a2eb44e89ab28`;
native/WASM bundle hashes are
`535c4754edf41c0ea0adb83d5790e17bda384ab69be7df885941d6be992034cb` and
`53dea2aa3956ce2035290cb1ae49de9276f7bb16b15dedcba51e4c273edda73b`.
Across 7,680 audition samples, maximum absolute PCM error is
`1.862645149230957e-9` and RMS error is `2.1807662361359516e-11`, both within the
predeclared tight bounds. The reproducible gate is
[`scripts/verify-wasm-parity.sh`](scripts/verify-wasm-parity.sh).

### 10. Build the interactive HTML workbench

- Load, edit, validate, and save engine/scenario JSON.
- Provide ignition, throttle, operating-mode, RPM/load/test-cell, and event controls
  only for capabilities the core currently executes.
- Run simulation ahead in a Worker, exchange bounded blocks through a ring buffer, and
  keep the AudioWorklet limited to device delivery.
- Add start/stop, underrun/load diagnostics, route/stem selection, essential telemetry,
  a dyno-run command, and WAV export through a fresh finite session on the same API.
- Adapt from core rate to AudioContext rate only after the canonical master output.
- Recompile and replace the session explicitly when an engine-structure parameter is
  edited; do not mutate an invalid half-compiled engine.

Gate: the browser can load the JSON BMW, react to controls, run the canonical dyno, and
export core-identical PCM without depending on the native CLI.

Completed evidence: the staged workbench loads and edits the strict engine/scenario
documents, resolves their declared assets, and atomically replaces the active compiled
program only after a successful rebuild. A dedicated Worker owns the fixed-memory WASM
module and the same C ABI/session used by the native renderer. It primes a bounded
single-producer/single-consumer shared PCM ring before publishing it, converts the
canonical 192 kHz selected bus through one versioned 129-tap device-rate resampler, and
leaves the AudioWorklet responsible only for bounded delivery and click-safe
silence/recovery.

The real-module integration fixture exports 7,680 canonical WASM Float32 samples with
SHA-256
`77484393b278ec40a84b4bde7d2dae31f01894e94a6a47cd17fd16ccb1787413`.
The complete BMW warm-running free-rev browser capture exports a 3,840,056-byte
Float32 WAVE whose current SHA-256 is pinned by the browser integration gate rather
than duplicated in this roadmap.
The browser gate also proves open-ended playback beyond its authored 5.5-second
horizon, admitted throttle and external-resistance controls, Stop/Start state
continuity, fresh Restart state, route selection, cross-origin-isolation headers,
zero startup underruns, and exact WAV framing:
[`scripts/verify-browser-workbench.sh`](scripts/verify-browser-workbench.sh).

## 6. Cutover acceptance policy

This is an architecture/plumbing cutover, not a sound-fidelity experiment. There are no
user listening pauses between checkpoints 1--10. Automated request-identity, block,
PCM/data-chunk, deterministic generic-WAV, and telemetry checks are the acceptance
evidence. If sound-bearing byte identity breaks, work stops at that checkpoint and the
regression is fixed; it is not deferred to a future phase or offered as a listening
candidate.

Tests remain proportional:

- parser/compiler validation at the new public boundaries;
- one structurally different generic-engine compilation case;
- the BMW request/PCM/data-chunk migration fixture and generic WAV golden;
- native/WASM block parity;
- smoke coverage for CLI and browser assembly.

Do not build a large perceptual-metric or end-to-end fixture matrix during plumbing.
Listening resumes only after checkpoint 10 when a later change intentionally affects
the sound model, source set, tuning, or presentation.

## 7. After the cutover

The prior M4--M9 sequence is retired. The post-cutover milestone is **headless
executable parity** with pristine Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`. The operating bench in section 9 is the
first vertical slice of that target, not its final boundary. The two BMW inertial-dyno
scenarios remain regression fixtures for the accepted renderer; they are not a
substitute for general engine topology, vehicle dynamics, or live test-cell controls.

Parity means reproducing meaningful executable capability and behavior behind the
current JSON, `EngineSession`, native, C ABI, and WASM boundaries. It does **not** mean
copying the `.mr` language, private backend names, the native animated GUI, themes,
display-only geometry, dead fields, or compatibility layers. The Web workbench remains
a thin authoring, control, listening, and diagnostics client.

After headless executable parity:

- add intake, mechanical, starter, and other source buses only with real implemented
  source models; these are fidelity extensions because pristine engine-sim renders
  exhaust sources only;
- improve fidelity one established subsystem at a time, with immediate A/B listening
  only for that sound-affecting change;
- compile accepted behavior into lightweight audio-follower packages for game runtimes;
- expose authoritative torque/power only after its ownership and calibration contract
  is ready.

No future phase may justify knowingly broken intermediate audio. A sound-affecting
replacement remains isolated behind an existing seam until its focused candidate is
accepted; rejected experiments are removed rather than retained as alternate modes.

## 8. Commit and artifact policy

- Work on the dedicated branch until a merge is requested.
- Commit one coherent bullet or subslice at a time; split a numbered checkpoint before
  coding when it contains independently reviewable behavior.
- Never mix sound-affecting work with plumbing or unrelated refactoring.
- Keep the build and focused tests green at every commit boundary.
- Do not rewrite or discard user-owned history in the original or failed repositories.
- Keep anonymous scratch renders out of source history.
- Retain only deliberate fixtures and accepted artifacts; do not grow an anonymous
  multi-gigabyte artifact tree.
- Record the exact output path and hash for every retained automated fixture.

## 9. Headless executable parity

### 9.1 Product boundary

Recreate pristine engine-sim's meaningful executable engine and operating behavior on
this clean-room core without importing its implementation, `.mr` runtime, or native
GUI. The operating-bench portion of the behavioral oracle is:

- manual ignition, momentary starter, throttle, and held-dyno target plus separate
  absorbing/driving torque limits;
- neutral and ordered forward gears, clutch engagement, service brake, and vehicle
  response;
- engine, dyno, clutch, gear, speed, and load telemetry;
- named startup, idle, free-rev, loaded-pull, lift/overrun, limiter, and shutdown
  procedures;
- an unpaced bake that runs the same controls through the same session.

The remaining engine-definition parity surface includes shareable
heads/intakes/valvetrains, general bank/crank/journal connections, multiple
crankshafts, and master/slave rod geometry. Configuration that reached pristine physics
or output is in scope. Display-only fields, dead inputs, and source-library presets are
not.

The browser is a client of this contract. It must not synthesize RPM, vehicle motion,
load, or dyno behavior in JavaScript.

Live browser playback creates an open-ended session for an admitted FreeEngine,
HeldDyno, or FreeVehicle motion owner. **Stop** pauses production and **Start** resumes
the same state; **Restart** creates fresh state.
Authored-capture export creates a separate finite-scenario session. Arbitrary live
controls beyond the authored horizon are not silently truncated into that export.

### 9.2 Motion ownership

Each session has one explicit motion owner:

| Mode | Caller controls | Runtime resolves |
|---|---|---|
| `held_speed` | authored fixed RPM; no live controls | engine state, torque, and held-point evidence |
| `prescribed_kinematic_sweep` | authored RPM trajectory; no live controls | reaction torque, engine state, and audio |
| `held_dyno` | throttle, ignition, fuel, target RPM, maximum absorbing torque, and maximum driving torque | dyno reaction, achieved RPM, and engine state |
| `load_target_held_capture` | authored RPM and load target; no live controls | converged throttle and held-point evidence |
| `inertial_dyno` | throttle, ignition, and fuel | crank RPM and target-crossing evidence |
| `free_engine` | throttle, ignition, fuel, limiter, external resisting torque, and conditional starter | crank RPM, torque, and stall/idle/limiter state |
| `free_vehicle` | throttle, ignition, fuel, limiter, gear, clutch, conditional starter, and conditional service brake | crank RPM, clutch torque/slip, vehicle speed, distance, and road load |

No mode may hide a prescribed pitch ramp behind a “natural” or “dyno” label.

For FreeEngine, the compiler derives the engine baseline with the versioned
cycle-mean centered slider-crank kinetic-energy method, adds optional
`attached_inertia`, and retains the sum as an exact cycle-mean reference. The runtime
evaluates analytic configuration-dependent `M(theta)` and `dM/dtheta` from the same
mechanism and adds the constant attachment at every left boundary. The BMW M52B28
baseline is `0.2108686520185204 kg*m^2`; its neutral fixture has zero attached inertia
and zero external resistance. The existing interactive-scenario smoke requires its
full-throttle 1,500-to-7,000-rpm crossing in `0.44`--`0.50 s`.

That smoke starts from a short part-throttle preparation and is not the controlled
pristine ablation oracle. It guards the interactive recipe only. Gas-exchange pumping
is already present inside the cylinder pressure-volume work. FreeEngine motion now
uses pristine engine-sim's authored crank friction and executable one-step-lagged
piston-wall law. Its clean centered inverse-dynamics wall reaction has passed direct
held and coast comparison. The exact centered-slider configuration-inertia equation
has also replaced the former cycle-mean approximation in free-running motion. The
controlled response gate passes: WOT differs by `0.0134 s`, and every coast crossing
by at most `0.0049 s`; the long natural-balance mean is `1,043.032 RPM` versus
pristine `1,041.953 RPM`. FreeEngine also uses pristine's semi-implicit crank-step
ordering: update `omega`, then advance `theta` with that new speed. The mechanics
checkpoint is ready for native/WASM verification and listening.

Pristine engine-sim commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630` is the transient behavioral authority.
The controlled BMW evidence is frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_M52_DYNAMICS.md`](docs/oracles/PRISTINE_ENGINE_SIM_M52_DYNAMICS.md).
FreeEngine may have exactly one applied mechanical-loss authority. Chen--Flynn must
not be stacked with source friction or used to advance FreeEngine RPM. It may remain
temporarily as held-speed prediction evidence until the later power contract either
names that role explicitly or removes it.

### 9.3 Vertical slices and commit boundaries

1. Compile immutable rig, vehicle, transmission, dyno-default, and mode-control data
   from the current JSON vocabulary. Remove any parsed-but-never-executable ambiguity.
   The rig and warm-running free-engine request contracts are complete.
2. Implement positive-RPM neutral free-engine dynamics with live ignition, fuel,
   throttle, limiter, and external resisting torque. Derive engine baseline inertia,
   add only explicit attached inertia, default the neutral external resistance to
   zero, and preserve the accepted renderer. The initial gas-plus-Chen--Flynn motion
   closure was provisional and is no longer the FreeEngine motion authority.
3. Publish those free-engine controls and physical telemetry through `EngineSession`,
   the C ABI, and WASM. Stop for a BMW warm-running free-rev listening checkpoint.
4. Replace Chen--Flynn as the FreeEngine motion authority with pristine engine-sim's
   authored running-direction crank friction and its executable one-step-lagged
   piston-wall friction. Restore its configuration-dependent centered-slider crank
   inertia and exact step ordering without changing gas, friction, or audio in the
   same commit. Preserve gas-exchange pumping inside indicated torque, match the frozen
   WOT/coast/natural-balance oracle, and stop for listening. Intermediate crank-only,
   piston-enabled, inertia, and step-order commits must match their named pristine
   evidence; they are not unexplained candidate sound models.
5. Add normal stopped/stalled crank state and non-fired cranking as a separate
   mechanics slice.
6. Add mechanically engaged starter torque and stop for a BMW crank/catch listening
   checkpoint.
7. Execute arbitrary sampled cam profiles without changing the accepted generated
   harmonic-cam path.
8. Execute VTEC base/alternate intake and exhaust cam selection using pristine's
   actually consumed RPM, manifold-pressure, and throttle conditions. Do not reproduce
   the stored-but-unused `min_speed` input as fake behavior. Stop for Honda transition
   listening.
9. Execute the governor throttle controller and verify a governed small engine across
   a load step.
10. Generalize heads, intakes, valvetrains, banks, crankshafts, direct/shared ignition
    wires, and master/slave journals in isolated topology commits. Each new family must
    have a structurally representative fixture while the accepted existing engines
    remain unchanged.
11. Implement the bounded held-dyno controller with live target RPM and torque limit.
    Run target-driven pull, lift, and ordinary overrun through it; stop for listening.
12. Implement vehicle inertia and road load, transmission ratios, clutch torque/slip,
    gears, and service brake. Stop for a neutral/launch/shift/fifth-gear pull check.
13. Publish starter, dyno, drivetrain, and remaining API-relevant telemetry and controls
    through the same native/C ABI/WASM boundary.
14. Replace the current restart-only dyno button with the capability-driven operating
    bench and named scenarios. Complete its interactive lifetime by admitting
    open-ended HeldDyno and FreeVehicle sessions; the UI must capability-gate real
    backend functions, not infer them from generic live-control support.
15. Add unpaced capture procedures for crank, startup/catch, settled idle, loaded
   rise, part load, coast fall, neutral limiter, limiter lift/recovery, and shutdown.
16. Freeze representative fixed-cam inline/V, VTEC, governed, master/slave, and
    drivetrain procedures and recordings as the minimum parity regression set before
    resuming fidelity experiments.

Current progress: slices 1--9 and 11--15 are executable and accepted for the currently
admitted topology. Slice 10 remains open for the isolated general-topology commits
listed below, and Slice 16 remains the final curated parity recording gate. Slice 14's
capability-driven UI and continuous FreeEngine, HeldDyno, and FreeVehicle lifetimes
are accepted. Slice 15 is accepted. Slice 12's pristine
vehicle/transmission equations and explicit non-parity service-brake boundary are
frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_DRIVETRAIN.md`](docs/oracles/PRISTINE_ENGINE_SIM_DRIVETRAIN.md).
Slice 6 reaches JSON, runtime, native
session, exact C ABI, WASM, and the Web workbench; the BMW crank/catch checkpoint was
accepted. Slice 7 admits arbitrary sampled fixed-cam profiles while preserving the
generated harmonic path. Slice 8 executes the three consumed pristine VTEC predicates
without the dead `min_speed` input; its Honda transition checkpoint was accepted and
its source audit is frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_VTEC.md`](docs/oracles/PRISTINE_ENGINE_SIM_VTEC.md).
Slice 9 executes pristine's persistent governor update and exposes normalized speed
demand through the same native/WASM throttle command. The Kohler CH750 fixture now
releases at `1.5 s`, settles before a `12 N m` load at `15 s`, unloads at `20 s`, and
publishes only the `14--30 s` listening interval. Its source buses are byte-identical
to that interval from a full-acquisition render; only the audition file's intentional
clip-relative fades differ. Slice 10 now admits source-faithful bank-local heads,
ports, standard valvetrains, and cams. The executable contract/runtime retain one
ordered chamber/runner/flow profile per bank, use separate intake/exhaust flow radii,
and bind each cylinder by BankId. Bank-local chamber volume, runner geometry, and flow
tables, standard same-role cam profiles, and each referenced piston's calibrated
blowby restriction may differ. Standard cams resolve into deterministic first-use
profile pools with explicit cylinder/lobe bindings, while each physical cam still
requires one exact shared profile across its own lobes. Post-slice parity closure now
executes per-head VTEC ownership as canonical bank-keyed selectors; mixed
standard/VTEC heads and distinct thresholds no longer collapse to one representative
valvetrain. The Shovelhead now binds source-authored 0.2/0.1 CFM front/rear piston
restrictions through independent runtime blowby lanes. Its equal-value control and
source-value candidate were accepted. The pristine-derived
Shovelhead A/B fixture swaps only its four
bank-local port-flow references; repeat A renders are byte-identical and A differs
from B. Two generated controls additionally leave the front or rear profile unchanged
while changing only the other bank; both differ from A, excluding collapse to either
the first or last representative profile. Counterfactual front-only and rear-only
intake-cam probes likewise produce distinct full-pipeline PCM, while changing only the
GUI base radius preserves exact PCM. The accepted BMW PCM remains the unchanged
authoring check for this cutover. The Kohler fixture has been
restored to two heads, four ports, two valvetrains, and four one-cylinder cams. Its
complete resolved request and all five published WAV files are byte-identical to the
accepted shared-head checkpoint. Direct
centered rods now execute on inline, V, opposed, and custom explicit bank axes. A
pristine-derived Subaru EJ25 fixture preserves its antipodal banks, direct journal
bindings, and opposed mechanism pairs across a bounded runtime capture. More general
ignition wires now preserve their public sharing identity and execute pristine's
stateless one-post-to-many-cylinder fan-out. A shared-wire inline twin and an
equal-angle split-wire twin retain different canonical engine identities while
producing byte-identical PCM. The first separate-intake checkpoint replaced the
resolved and runtime singleton with stable intake identities, ordered per-intake gas
profiles, and explicit cylinder-to-intake lanes while deliberately retaining the
one-intake compiler gate. The second checkpoint lifts only that gate: every authored
intake must be reachable, distinct objects retain independent plenum state even when
their values match, global throttle fans out through lane-local plate/restriction
parameters, and cylinders keep their explicit intake bindings. The pristine-derived
Shovelhead diagnostic compares a shared manifold, two equal-valued distinct manifolds,
and a rear lane differentiated only by source-backed idle-plate and runner-decay
values. It is a topology/behavior fixture rather than a pristine audio oracle because
pristine ships no multi-intake WAV and exposes no intake audio source. The
source authority is frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_SEPARATE_INTAKES.md`](docs/oracles/PRISTINE_ENGINE_SIM_SEPARATE_INTAKES.md).
The single-intake BMW PCM remains byte-exact. The first isolated crankshaft
checkpoint carries authored-order crank identities, an explicit output-crank
selection, and exact direct/master-root cylinder bindings through the public engine,
resolved mechanism, provenance, request-v6 identity, and manifest-v9 wire. The second
checkpoint admits co-phased multiple-crank direct journals under finite prescribed
kinematics. A semantically named V-twin A/B fixture decomposes one crank into two while
preserving aggregate authored values, cylinder geometry, journal phase, and exact PCM;
its request identity changes and its rear cylinder retains the secondary-crank binding.
Changing only the explicit output selection also changes identity. The third checkpoint
reduces that same co-centered, co-phased group to one dynamic degree of freedom for
FreeEngine, HeldDyno, and FreeVehicle. It sums only authored rotational inertia and
running friction in resolved authored-crank order; crank and flywheel masses are not
re-derived or double-counted. An aggregate-equivalent one-crank/two-crank coast fixture
produces exact engine telemetry, session telemetry, and audition PCM while retaining
distinct request identities. Separate mutations of only the secondary inertia and only
the secondary friction change the resolved motion in the expected direction. Unequal
TDC references, cross-crank master/slave declarations, HeldSpeed, LoadTargetHeld, and
InertialDyno remain closed. The source boundary is frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_MULTIPLE_CRANKSHAFTS.md`](docs/oracles/PRISTINE_ENGINE_SIM_MULTIPLE_CRANKSHAFTS.md).
The ignition source
authority is frozen in
[`docs/oracles/PRISTINE_ENGINE_SIM_IGNITION_WIRES.md`](docs/oracles/PRISTINE_ENGINE_SIM_IGNITION_WIRES.md).
Exhaust-primary cross-section ownership is singular: the referenced bank head's
exhaust port owns runner/primary area. The former
execution-inert duplicate on each exhaust system was deleted from the greenfield JSON
contract without an alias.

The direct journal contract is now a single tagged attachment path:
`cylinder.journal -> journal.crankshaft`. Every current engine uses required
`type: "crankshaft"`; reciprocal crankshaft journal lists, repeated cylinder crank
references, and the old competing master/slave fields were removed without aliases.
The `type: "master_rod"` alternative now owns a master-cylinder reference, positive
throw radius, and owner-local phase. Its one-level attachment graph validates exact
consumers, connectivity, direct-root ownership, nesting, and cycles through the same
parser/direct-DTO firewall. Resolution preserves the stable master-cylinder ID and
throw radius beside the raw owner-local phase. The resolved mechanism core is now a
fail-closed tagged union: a direct cylinder alone owns stroke, crank radius, and
axis-relative journal phase, while a master-rod cylinder owns only its master ID,
throw, and local phase. Direct canonical request bytes and provenance order remain
unchanged. A mechanism containing a master rod truthfully publishes no net-torque or
equivalent-inertia capability. Its pure one-level root/slave evaluator now matches the
pristine position, derivative, chamber-volume, and volume-derivative construction,
including the source bank-axis and phase conventions. Negative slider solutions fail
closed.

Capture construction now compiles one immutable, source-bound mechanism-kinematics
plan and shares that exact object with mechanics, gas, and dynamic-crank execution.
The three former direct-cylinder geometry reconstructions and the mechanics-only
copied cylinder view are removed. Scenario baseline inertia resolves through the same
compiler. Exact source-field binding rejects a stale same-ID plan, and a moved-from
mechanics session fails terminally rather than dereferencing an empty plan. Existing
direct slider-crank formulas, floating-point evaluation order, torque-accounting
displacement, resolved request identity, and accepted audio remain the unchanged
authority. A separate immutable one-level master-rod plan retains each direct root,
slave pin, stable master index, bank axis, chamber/route binding, and ignition angle
without publishing slave stroke, displacement accounting, inertia, reactions, or
torque. Exact root bounds and deliberately sufficient slave bounds certify full-cycle
reachability and positive chamber volume before this plan is released.

The existing direct mechanism now admits `prescribed_kinematic_sweep` through the
public scenario contract and finite capture path. It reuses the established kinematic
schedule, mechanics, gas, excitation, and presentation core. Its dedicated capture
policy publishes only complete instantaneous indicated-gas torque; shaft/reaction
torque, power, work, and cycle evidence remain explicitly unavailable, and live
overrides are rejected. Master-rod geometry now enters this same public prescribed
path without changing held-speed or dynamic-crank behavior.

The mechanism-plan compiler applies the full-cycle certificate to every assembled root
and slave, resolving each slave through its stable direct-root index. Canonical
radial-five margins and three geometries that are valid at one point but fail later in
the revolution are regression fixtures. Point-valid geometry that later becomes
unreachable or nonpositive-volume is rejected at its bounded cylinder path.

The mechanics-cylinder sample separates direct centered-slider
coordinates from one-level master-rod axis coordinates with an explicit tagged union
and empty state. Existing direct mechanics populates the direct alternative at the
same statement boundary, while dynamic piston-wall reaction requires that alternative
before reading phase. Prescribed radial mechanics emits the radial alternative in
stable cylinder and spark order. Gas and capture consume only common volume, volume
derivative, and piston-speed fields, so neither reconstructs nominal slave stroke or
clearance geometry.

The authored scenario resolver and resolved scenario contract retain the radial mode
firewall ahead of execution: a master-rod engine admits only authored
`external_speed`, resolved as `PrescribedKinematicSweep`. Other authored modes fail at
`/mode/type` before direct-only baseline inertia is queried, and programmatic callers
receive the equivalent `mode` rejection. Mechanics and gas independently require a
finite prescribed schedule, while dynamic crank retains its direct-plan gate.

The pristine-derived radial-five fixture now exercises this boundary through public
engine/scenario compilation, an exact 800-frame capture with active two-route gas
sources, and a four-block `EngineSession` with finite nonzero PCM on the existing
presentation path. It locks five bank axes, one root and four slave pins, firing/cam/
route associations, the source's doubled head-flow tables, and exact IR bytes. This is
structural and execution parity evidence; longer low/high holds and a short climb are
still required before the representative radial recording set is accepted by ear.

Slice 11 now executes a finite authored `held_dyno` request through a bounded signed
velocity constraint while reusing the accepted gas, source-friction,
configuration-inertia, routing, conditioning, IR, and mastering paths. The BMW gate
pulls from 1,500 to 6,500 RPM, holds, lifts, and then overruns with zero permitted dyno
driving torque, so a falling engine is never forced back onto an authored pitch lane.
Its tests and clean production render pass; the user accepted the listening result in
[`docs/HEADLESS_BMW_HELD_DYNO_LISTENING_GATE.md`](docs/HEADLESS_BMW_HELD_DYNO_LISTENING_GATE.md).

Slice 12 now executes finite authored forward-vehicle requests with neutral and
ordered forward gears, bounded clutch slip, passive rolling/aerodynamic road load,
and an explicitly authored one-sided service brake. Its self-contained resolved
request has no hidden rig pointer, and the exact clutch-then-road projection is method
identity bound. The accepted held-dyno raw and audition WAVs remain byte-identical.
Warm-preparation cycle accounting now ends at dynamic release, matching the published
unavailable released-cycle telemetry and preventing an obsolete observer from
terminally rejecting a valid brake-held, first-gear engine shutdown at the
positive-RPM/equal-binary64-angle stall edge.
The user accepted the clean BMW neutral/launch/first-to-second and already-moving
fifth-gear pull/lift recordings in
[`docs/BMW_FREE_VEHICLE_LISTENING_GATE.md`](docs/BMW_FREE_VEHICLE_LISTENING_GATE.md).

Slice 13 publishes the already-executed operating behavior without changing its sound
path. Commit `d10e9a3` adds timestamped held-dyno and drivetrain lanes, `cb5f7c2`
publishes the seven-way motion descriptor, exact capability masks, BMW's ordered
five-gear inventory, and nullable held-dyno/free-vehicle telemetry sidecars through
the native session, `7d7effd` replaces the exact C boundary with ABI v4, `ad0f306`
exposes the same contract through WASM, and `d519cda` initially transported atomic
control batches through Worker protocol v2. The current exact Worker protocol is v3,
which requires the caller to choose finite or open-ended execution explicitly. Native,
C, WASM parity, and full browser gates pass. Fresh
clean-Release renders of the accepted held-dyno, launch/shift, and fifth-gear fixtures
retain all six raw/audition WAV hashes byte-for-byte. Slice 14 now owns the visible full
operating bench and named procedures.

Slice 14 turns that exact backend into a visible capability-driven bench without
changing physics or audio. Commit `a3b7ca5` groups the existing repository
procedures and adds the accepted BMW held-dyno, launch/shift, and fifth-gear runs;
`c3c2563` adds descriptor-gated dyno/drivetrain controls plus mode-owned telemetry;
and `4f43eb3` initially distinguished the continuous FreeEngine bench from finite
procedures. The operating-bench completion admits open lifetimes for FreeEngine,
HeldDyno, and FreeVehicle, freezes each authored control snapshot at the audible
handoff, and keeps exact finite procedures and capture/export as separate fresh
sessions. A visible selector sends that exact choice end to end; named procedures
default to finite and interactive free-rev presets default to open-ended. The actual
WASM UI gate
applies atomic dyno and drivetrain batches, observes their returned sidecars, verifies
pause/resume/fresh-restart behavior, retains the canonical finite-export WAV hash, and
reports zero startup underruns. The frozen contract and evidence are in
[`docs/contracts/WEB_OPERATING_BENCH_SLICE_14.md`](docs/contracts/WEB_OPERATING_BENCH_SLICE_14.md).

Slice 15 defines six finite BMW M52TUB28 procedures that cover all nine canonical
capture roles: non-fired crank, startup/catch, settled idle, loaded rise, part load,
unforced coast, neutral limiter, lift/recovery, and shutdown. Three existing accepted
procedures are reused and three focused JSON scenarios are new. Fresh native and WASM
sessions execute the exact authored timelines without changing live-bench lifetime or
audio. All six native jobs completed concurrently in approximately `30.4 s`, bounded
by the existing settled-idle clip. The procedure map, measured runtime evidence, and
local proof hashes are in
[`docs/contracts/CANONICAL_UNPACED_CAPTURE_PROCEDURES_SLICE_15.md`](docs/contracts/CANONICAL_UNPACED_CAPTURE_PROCEDURES_SLICE_15.md).

Every sound-bearing slice keeps the existing BMW renderer, routing, conditioning, IR,
and mastering unchanged. Listen immediately after the one intended behavior changes;
do not stack later slices to excuse a bad result.

### 9.4 Parity-closure queue

The architecture cutover and operating-bench slices are complete. Finish the remaining
engine-definition parity in this order, with one coherent commit per bullet:

1. execute each referenced piston's authored blowby restriction independently
   (**complete; source-value Shovelhead free-rev accepted by user listening on
   2026-08-02**);
2. execute connecting-rod center of mass and piston wrist-pin geometry where pristine
   physics consumes them (**complete; physical rod COM and axial wrist-pin semantics
   are source-audited, synthetic closed forms pass, and accepted BMW/Shovelhead
   default-field output remains exact on 2026-08-02**);
3. complete intake-local molecular-AFR ownership without duplicating fuel authority
   (**complete; fuel remains the sole stoichiometric authority, every intake executes
   an explicit positive main-mixture lambda, a two-intake differential proves
   lane-local metering, and the accepted BMW complete WAV remains byte-identical on
   2026-08-02**);
4. generalize physics, capture, excitation, and propagation-delay rate ownership while
   retaining exact output for every existing 10 kHz fixture (**complete; scenario
   physics owns solver and gas step duration, capture/excitation inherit its admitted
   clock, propagation delay is session-resolved from physical path time, focused
   20 kHz execution proves 50 us mechanics/capture and 360-vs-180-sample equal-time
   delay, and the accepted BMW complete WAV remains byte-identical on 2026-08-02**);
5. execute bank-local VTEC selectors through the already admitted bank-local head and
   cam-profile topology (**complete; pristine per-head ownership is normalized to
   canonical bank-keyed selectors, mixed standard/VTEC and two distinct VTEC banks
   execute without a representative-head collapse, all 84 core tests and both clean
   BMW render gates
   pass, and every current Honda transition route/raw/audition WAV remains
   byte-identical; audition SHA-256
   `1dfc14b8dbfa364837d513d89ef6cf1fc5d4fbf2a21ebd54fc198b04421098ce`
   in control `honda-b18c5-bank-local-vtec-control-fb20770` and candidate
   `honda-b18c5-bank-local-vtec-candidate-6b2d127` on 2026-08-02**);
6. execute dynamic one-level master/slave mechanisms and only the remaining meaningful
   multiple-crankshaft cases supported by pristine (**in progress; the pristine
   constrained-rigid-body path and leaf-to-root reaction ownership are frozen in
   `docs/oracles/PRISTINE_ENGINE_SIM_MASTER_ROD_DYNAMICS.md`; the immutable radial plan
   now retains source-bound crank inertia/friction and every piston/rod mass, rod
   inertia, and COM in `b5b5411`; analytic articulated configuration inertia and
   coupled wall reactions landed through `9104e9e`, and `c8127bd` executes that
   mechanism in the private cold dynamic-crank path; `d9a7176` additionally derives
   and source-binds each cylinder's certified full-cycle extrema, swept displacement,
   and piston travel without opening a public mode or downstream consumer; its Release
   radial prescribed and BMW direct held-dyno candidates reproduce all 16 control WAVs
   byte-for-byte, including audition SHA-256
   `58394f71ed58ee5af4434745e6f266c2cac7db33503848decbd61b9e8a9f0985` and
   `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496`
   respectively; the already executed co-phased direct multi-crank subset is complete,
   while unproven offset/geared/nested/multi-crank-master-rod cases remain explicitly
   closed**);
7. make native audition saturation explicit, then render, listen to, and freeze Slice
   16's representative fixed-cam inline/V, VTEC, governed, master/slave, and drivetrain
   procedures;
8. declare headless executable parity complete only after the Slice 16 listening gate
   is accepted.

An implementation that leaves PCM unchanged is proved headlessly. Any intentional or
unexpected PCM change stops the queue: render the unchanged baseline and the candidate
from the same authored procedure, publish both paths and hashes, and audition them
before beginning the next sound-bearing bullet. A later milestone may not be used to
explain or conceal a rejected intermediate sound.

### 9.5 Verified post-parity fidelity queue

The production-fidelity work begins only after parity closure. Each item is an isolated
candidate with the accepted parity renderer as its control:

1. audition a higher physical source rate with propagation delays derived from the
   actual capture clock; retain the existing windowed-sinc reconstruction;
2. preserve per-cylinder lanes through primary transfer and merge them only at the
   physical collector, beginning with a byte-identical architectural refactor;
3. add a separately published intake source bus;
4. add mechanical/valvetrain/block and starter buses independently;
5. couple combustion variation and flow noise to combustion work, valve flow, and
   pressure ratio rather than granting static generic jitter/noise sole authority;
6. add measured or explicit catalyst, resonator, muffler, and outlet transfer with
   temperature/load state, retaining the accepted static IR as the control until a
   candidate is accepted;
7. preserve simultaneous mono source/route stems for host-owned 3D emitters, adding
   stereo, cabin, or microphone-array delivery only for products that require it.

This queue does not include a resampler rewrite, removal of an 8 kHz master filter,
removal of adaptive leveling/soft clipping, or an upgrade from 44.1 kHz PCM16 output:
the current renderer already uses windowed-sinc reconstruction, has no such master
filter or dynamics processor, and publishes 192 kHz Float32/PCM24 audio. Mono 44.1 kHz
PCM16 is currently an input-IR restriction, not the engine output format.
