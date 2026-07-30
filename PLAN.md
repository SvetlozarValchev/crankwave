# Engine Sim Offline: greenfield product cutover

Status: active — operating-bench parity after the JSON/runtime cutover

Branch: `clean-room/bmw-baseline`

Date: 2026-07-30

Current checkpoint: **operating bench 1 — executable rig and control contract**

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

`scenario.json` owns one operating request:

- held-speed, prescribed-speed, or inertial-dyno ownership;
- throttle and other control trajectories;
- ignition, fuel-cut, limiter, startup/shutdown, and other events only when implemented;
- ambient, fuel, initial thermal state, preparation, duration, rates, quality, and seed;
- output selection and render destination policy that is not part of the engine asset.

RPM, throttle, and load cannot all be authoritative simultaneously. Each scenario mode
declares which values are commands and which are results. Interactive sessions use the
same rule: imposed-RPM operation, internally dynamic operation, and a later
load-following mode are distinct capabilities.

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
simulation request SHA-256: 8cb2a5a7584b3f8e53a57b453b5e39986cb45e32723e76affea12cba31b5a816
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
`8cb2a5a7584b3f8e53a57b453b5e39986cb45e32723e76affea12cba31b5a816`
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
`a93959fcc8c7eb83e466b488fcf729a7d2917dd6501724a38a3bf10af99729a8`;
native/WASM bundle hashes are
`2cc6fe9d1fe4537827476bf6582544acaa92a628c67e78ff8d6149624962a28e` and
`19d7c36657f561cf5559ff16459d4e64e2cec3ff90e35d193ca88531b77f6b85`.
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
  a dyno-run command, and WAV export through the same session.
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
The full 15-second BMW browser dyno exports a 11,520,056-byte Float32 WAVE with
SHA-256
`69f9a94faa6c5dcef56acd8f8de9d60266b0d51d9983f9406014d44bb6d3ca13`;
its 11,520,000-byte PCM payload has SHA-256
`15064e6b093aecf8fe7184f2f0eeb9bae324a5b16d6e3867dbfb9695794bda0f`.
The development-PC headless run completes that export in under 15 seconds. Live
startup completes 333 bounded preparation/lead-fill blocks in about 4.6 seconds,
publishes 10,544 device frames for a requested 9,600-frame lead, and records zero
startup underrun frames or events. The browser gate also proves admitted throttle,
ignition, and fuel controls, route selection, failed-rebuild retention, stop/restart,
cross-origin-isolation headers, and exact WAV framing:
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

The prior M4--M9 sequence is retired. The first post-cutover milestone is the complete
operating bench described in section 9. The two BMW inertial-dyno scenarios are
regression fixtures for the accepted renderer; they are not a substitute for a free
engine, dyno controller, drivetrain, or capture harness.

After operating-bench parity:

- add intake, mechanical, starter, and other source buses only with real implemented
  source models;
- author Honda and Toyota JSON assets to prove alternate valvetrain, bank, firing, and
  exhaust topology without engine-specific code;
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
- Commit exactly one numbered checkpoint; split a checkpoint before coding if it cannot
  be reviewed as one coherent change.
- Never mix sound-affecting work with plumbing or unrelated refactoring.
- Keep the build and focused tests green at every commit boundary.
- Do not rewrite or discard user-owned history in the original or failed repositories.
- Keep anonymous scratch renders out of source history.
- Retain only deliberate fixtures and accepted artifacts; do not grow an anonymous
  multi-gigabyte artifact tree.
- Record the exact output path and hash for every retained automated fixture.

## 9. Operating-bench parity

### 9.1 Product boundary

Recreate the behavior of the real `engine-audio-lab` bench on this clean-room core,
without importing its legacy ABI or simulator implementation. The behavioral oracle
is:

- manual ignition, momentary starter, throttle, dyno enable/hold/target/torque;
- neutral and ordered forward gears, clutch engagement, service brake, and vehicle
  response;
- engine, dyno, clutch, gear, speed, and load telemetry;
- named startup, idle, free-rev, loaded-pull, lift/overrun, limiter, and shutdown
  procedures;
- an unpaced bake that runs the same controls through the same session.

The browser is a client of this contract. It must not synthesize RPM, vehicle motion,
load, or dyno behavior in JavaScript.

### 9.2 Motion ownership

Each session has one explicit motion owner:

| Mode | Caller controls | Runtime resolves |
|---|---|---|
| Free engine | throttle, ignition, starter, external resisting torque | crank RPM, torque, stall/idle/limiter state |
| Free vehicle | throttle, ignition, starter, gear, clutch, brake | crank RPM, clutch torque/slip, vehicle speed and load |
| Held dyno | throttle, target RPM, maximum absorbing/driving torque | dyno reaction, achieved RPM and engine state |
| Inertial dyno | throttle and the declared passive brake/inertia protocol | crank RPM and target-crossing evidence |
| External RPM follower | RPM trajectory and throttle | reaction torque and engine state |

No mode may hide a prescribed pitch ramp behind a “natural” or “dyno” label.

### 9.3 Vertical slices and commit boundaries

1. Compile immutable rig, vehicle, transmission, dyno-default, and mode-control data
   from the current JSON vocabulary. Remove any parsed-but-never-executable ambiguity.
2. Implement neutral free-engine dynamics with live ignition, starter, throttle,
   stall/idle, limiter, and external resisting load.
3. Publish free-engine controls and telemetry through `EngineSession`, the C ABI, and
   WASM. Stop for a BMW start/idle/free-rev listening checkpoint.
4. Implement the bounded held-dyno controller with live target RPM and torque limit.
   Run target-driven pull, lift, and ordinary overrun through it; stop for listening.
5. Implement vehicle inertia and road load, transmission ratios, clutch torque/slip,
   gears, and service brake. Stop for a neutral/launch/shift/fifth-gear pull check.
6. Publish drivetrain controls and vehicle telemetry through the same native/WASM
   boundary.
7. Replace the current restart-only dyno button with the full operating bench and
   named scenarios. The UI must capability-gate real backend functions, not infer them
   from generic live-control support.
8. Add unpaced capture procedures for crank, startup/catch, settled idle, loaded
   rise, part load, coast fall, neutral limiter, limiter lift/recovery, and shutdown.
9. Freeze the accepted procedures and recordings as the minimum operating regression
   set before resuming fidelity experiments.

Every sound-bearing slice keeps the existing BMW renderer, routing, conditioning, IR,
and mastering unchanged. Listen immediately after the one intended behavior changes;
do not stack later slices to excuse a bad result.
