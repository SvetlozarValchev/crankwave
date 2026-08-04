# Portable engine compile and session API

Status: portable C++ session, exact C ABI, fixed-memory WASM module, browser transport,
and HTML workbench implemented

Applies to: JSON engine authoring, immutable engine compilation, mutable simulation
sessions, native offline rendering, WASM preview, runtime control ownership, streaming
audio buses, telemetry, and fail-closed adapter behavior

This contract records the implemented native/WASM session boundary and browser adapter
around that same boundary. Future capabilities are called out explicitly. It is not a
compatibility surface for `.mr`, engine-sim, the failed offline fork, or historical
milestone-specific types.

## 1. Goals and non-goals

The implemented portable API:

- compiles versioned JSON engine/scenario definitions and their referenced assets into
  one immutable `CompiledScenario`;
- creates independent mutable sessions from a compiled scenario;
- processes one exact 20 ms method quantum without using wall time;
- accepts the currently implemented typed, timestamped controls where the scenario mode
  admits them;
- exposes named borrowed audio buses and bounded telemetry;
- rejects unsupported configuration and ownership conflicts instead of substituting a
  fallback sound;
- uses the same physics, excitation, and presentation implementation as the native
  bake adapter.

The exact-version C ABI and WASM build wrap that implementation. The Worker/ring
adapter and authoring UI likewise consume the ABI rather than introduce another
engine renderer.

Session creation has one mandatory execution-kind choice:

- `finite_scenario` executes the authored scenario as its exact finite recording
  recipe and may produce request-bound completion evidence;
- `open_ended` uses the same compiled scenario to initialize a continuous interactive
  FreeEngine, HeldDyno, or FreeVehicle bench, but has no elapsed-time completion
  horizon.

This choice is session lifetime policy rather than another sound model. There is no
default kind, synthetic long duration, finite-session loop, or compatibility overload.

The first implementation does not promise:

- `.mr` parsing or syntax compatibility;
- mutation of arbitrary engine geometry while a solver is running;
- byte-identical floating-point output between native and WASM targets;
- a complete vehicle, tire, road, or transmission simulation;
- that the full simulator will always meet an audio callback deadline;
- a second reduced-fidelity realtime renderer.

## 2. One implementation in native and WASM

```text
engine JSON + scenario JSON + assets
        |
        v
compile package
        |
        v
immutable CompiledScenario
        |
        +---------------------------+
        |                           |
        v                           v
native EngineSession          WASM EngineSession
unpaced process_block loop    Worker-paced process_block loop
        |                           |
        v                           v
WAV/telemetry sink            bounded PCM ring
                                    |
                                    v
                              AudioWorklet
                                    |
                                    v
                              Web Audio graph
```

Both `EngineSession` boxes are implemented by the same C++ sources. They select their
lifetime explicitly and otherwise differ only in pacing and publication:

- Native offline rendering creates `finite_scenario`, calls `process_block()` as
  quickly as the machine allows, and sends the resulting blocks to artifact encoders.
- Interactive browser playback creates `open_ended`; the Worker calls the same C ABI
  `process` entry only far enough ahead to keep a bounded playback ring supplied.
- The AudioWorklet consumes already-produced audio. It does not contain another engine
  model.

The native bake boundary is a publication adapter over this session API. It owns the
unpaced loop and artifact transaction but has no separately maintained simulation or
presentation path.

## 3. Contract layers

Three inputs have deliberately different lifetimes.

### 3.1 Engine definition

`authoring::EnginePackageDocument` is authored JSON. It contains durable engine and default
presentation data:

- identity and schema version;
- banks, cylinders, crankshaft, journals, firing order, and inertias;
- cylinder, piston, connecting-rod, and combustion geometry;
- cylinder heads, ports, valves, cam profiles, and valvetrain;
- one or more intake systems, runners, plenums, restrictions, and per-cylinder
  assignments;
- one or more exhaust systems, primary paths, collectors, restrictions, and
  per-cylinder assignments;
- fuel, ignition, timing, limiter, starter, combustion, friction, pumping, and thermal
  parameters;
- physical and presentation source-route declarations;
- default route gains, conditioning, impulse-response references, and audition mix;
- explicit methods and quality-independent model choices.

Every physical numeric key includes its unit in the key name or uses a schema-defined
unit that cannot vary by document. References use stable semantic IDs. Array position
does not define identity.

The JSON schema preserves capability parity where useful, not the structure or names
of the old scripting language. Engine-specific behavior remains data rather than a
BMW, Honda, or Toyota code branch.

### 3.2 Session configuration

The compiled scenario is an immutable authored recording and bench-initialization
recipe. It selects one operating context without changing the engine:

- one motion-ownership mode;
- audio delivery sample rate and requested bus layout;
- internal quality/resolution profile;
- ambient and initial thermal conditions;
- initial crank angle, RPM, and operating state;
- deterministic public seed;
- bounded delivery-block, caller control-command, and returned-telemetry capacities;
- test-cell inertia, brake curve, or held-speed controller where the selected mode
  requires them;
- optional audition-only drivetrain context.

A `free_engine` mode does not author a replacement total engine inertia. Its optional
`attached_inertia` is a nonnegative crank-referred addition, and its optional
`external_resisting_torque` is a nonnegative right-continuous trajectory. Omission of
either resolves to canonical positive zero. The compiler derives the engine baseline
with the versioned mechanism-family cycle-mean kinetic-energy method—centered slider
crank for direct journals or articulated one-level master rod for that admitted
topology—and resolves the runtime total as baseline plus attachment.

`quality.process_block_capacity_frames` is measured in delivery-rate PCM frames. The
current method requires capacity for at least 3,840 frames and always returns exactly
3,840 delivery frames per successful block. A larger authored capacity does not change
that method quantum. `quality.event_queue_capacity` bounds caller-authored live control
commands; it is not the internal combustion/event journal. The unfortunately named
`quality.telemetry_capacity_frames` bounds telemetry records returned by one process
call; the current session returns one record.

Changing the motion mode, delivery rate, quality profile, seed, bus layout, initial
state, or bounded capacities requires creating a new session.

The scenario's duration and post-release trajectories remain authoritative for
`finite_scenario`. They do not force an interactive session to stop, nor do they make
the browser repeat a finite recipe. The required session execution kind owns that
lifetime decision.

### 3.3 Live controls

Live controls are typed, timestamped commands sent after session creation. They do not
modify the engine document or compiled scenario. The implemented payloads are:

- selected throttle-controller demand in `[0, 1]`: direct linkage demand for a direct
  controller, or the normalized minimum-to-maximum speed command for a governor;
- ignition enabled;
- fuel enabled;
- starter enabled;
- limiter enabled;
- nonnegative external resisting torque in N m;
- positive held-dyno target engine speed in RPM;
- nonnegative held-dyno maximum absorbing torque in N m;
- nonnegative held-dyno maximum driving torque in N m;
- selected one-based forward-gear ordinal, with zero meaning neutral;
- clutch engagement in `[0, 1]`;
- service-brake application in `[0, 1]`.

An inertial-dyno session admits throttle, ignition, and fuel. A FreeEngine session adds
limiter state and external resisting torque, and adds starter state only when the
compiled engine declares a positive cranking starter. HeldDyno admits throttle,
ignition, fuel, target RPM, and both signed-direction torque limits. FreeVehicle admits
throttle, ignition, fuel, limiter, gear, and clutch; it adds starter only for a cranking
starter and service brake only for a rig with positive brake capacity. HeldDyno and
FreeVehicle expose those controls in both finite capture and open-ended bench sessions.
Prescribed live RPM, load-following commands, presentation monitoring, lifecycle
commands, and motion-mode transitions are not live session capabilities.

No untyped string-to-value property mutation enters the processing path.

## 4. Lifecycle

### 4.1 Compile

The caller parses JSON and supplies every referenced asset byte.
`compile_engine()` validates and resolves an `authoring::EnginePackageDocument` into an
immutable `CompiledEngine`. `compile_scenario()` validates and resolves an
`authoring::ScenarioDocument` against that exact engine and returns a
`CompiledScenario` that retains it.

Compilation performs no filesystem, URL, browser, thread, or process-global access.
Session creation compiles random, simulation, excitation, resampling,
presentation-asset, and convolution state before processing begins.

A `CompiledScenario`:

- is immutable after successful compilation;
- owns or content-addresses every byte needed to create a session;
- is safe to share across independent sessions;
- exposes IDs, stable-ID assignments, provenance, the retained `CompiledEngine`, assets
  through that engine, and session capacities;
- retains the immutable resolved contracts and identities used internally by sessions
  and native baking;
- contains no mutable process state.

### 4.2 Create

The implemented entry point is:

```cpp
EngineSessionCreateResult create_engine_session(
    const compile::CompiledScenario &scenario,
    EngineSessionExecutionKind execution_kind);
```

`execution_kind` is required and has exactly two values:

- `EngineSessionExecutionKind::finite_scenario`;
- `EngineSessionExecutionKind::open_ended`.

Session creation validates the selected lifetime, capabilities, and ownership mode,
compiles method-owned simulation, excitation, DSP, and IR-kernel state, reserves
bounded processing storage, and returns a mutable `EngineSession`. Open-ended
execution is admitted for `FreeEngine`, `HeldDyno`, and `FreeVehicle`; requesting it
for a capture-only motion owner fails session creation rather than substituting finite
execution.

One session:

- belongs to one caller and is not reentrant;
- owns all mutable physics, excitation, presentation, resampler, control, and telemetry
  state;
- creates no hidden global thread pool;
- performs no file or network I/O;
- never observes wall time;
- is independent from every other session created from the same compiled scenario.

### 4.3 Preparation

A new session starts from the initial state and preparation policy compiled into its
scenario. There is no reset operation on the implemented session.

`process_block()` returns preparation blocks with
`EngineSessionBlockPhase::preparation`; the native artifact publisher discards those
blocks and begins publication at the first `audible` block. The descriptor reports the
exact preparation block count, so this boundary is explicit rather than an implicit
warm-up. Here `preparation` means **pre-audible history**. For a positive-speed dynamic
bench mode, its fixed-horizon held preparation may end earlier; the selected motion
owner then advances dynamically and authored controls continue to execute until the
audible boundary.

Live commands may be queued before processing begins, but any command whose absolute
delivery-frame target falls inside the preparation interval is rejected with
`unavailable_during_preparation`. The first legal target is
`preparation_block_count * 3840`.

Both execution kinds run the same authored pre-audible history, preserving crank, gas,
combustion, random, filter, convolution, and resampler state. A positive-speed
dynamic bench mode is held through its fixed horizon, then physically releases without
a reset; held-speed and inertial modes retain release-at-audible equality. At the later
audible handoff, `open_ended` resolves one right-continuous snapshot of every applicable
authored operating-state, throttle-controller-demand, FreeEngine external-resistance,
HeldDyno target/limit, and FreeVehicle gear/clutch/service-brake lane. The declared
positive-zero default participates when the optional FreeEngine lane is omitted. A
boundary exactly at the audible handoff participates; later boundaries belong to the
finite recording procedure. Each snapshot remains in force until a live command
replaces its lane.

### 4.4 Enqueue controls

The implemented operation is:

```cpp
std::optional<EngineControlRejection> EngineSession::enqueue_controls(
    std::span<const EngineControlCommand> commands);
```

Every command contains:

- an absolute delivery-frame index relative to the session origin;
- a strictly increasing caller sequence number;
- one of the twelve implemented typed control payloads.

Delivery-frame indices, not milliseconds or wall-clock timestamps, are authoritative.
The selected numerical method defines the causal projection from a delivery frame to
its internal physics/control clock. A command that cannot be represented under that
method is rejected during enqueue or session creation.

Commands must have nondecreasing delivery frames, strictly increasing unique sequence
numbers, and must target a frame that can still be generated by `process_block()`. A
late command, a full command queue, an invalid payload, a preparation target, a
finite-session post-horizon target, a terminal session, or a command not owned by the
selected mode is rejected atomically. Open-ended execution has no authored upper
horizon check; its timestamps remain subject to exact clock projection and checked
counter bounds. The session never silently applies a command “as soon as possible.”

Live controls are right-continuous and sticky. Once accepted, a live value supersedes
the corresponding release-snapshot lane until another live command for that lane is
applied.

### 4.5 Process

The implemented operation is:

```cpp
EngineSessionProcessResult EngineSession::process_block();
```

One successful `process_block()` call:

- advances exactly 400 physics frames at 20,000 Hz and 3,840 delivery frames at
  192,000 Hz: one 20 ms method quantum;
- returns borrowed planar `float32` spans for every advertised bus;
- advances all internal clocks by integer/rational schedule state;
- consumes controls causally over that half-open physics/delivery interval;
- returns one bounded telemetry record containing the final engine-capture sample of
  the block;
- performs no JSON parsing, asset decoding, filesystem access, network access, or
  thread creation;
- returns only bounded borrowed PCM/telemetry spans and never accumulates
  duration-sized output;
- does not select a different model because a deadline is near.

The 3,840-frame size is currently part of the executable method, not a caller transport
choice. It matches the existing FFT/resampling arithmetic and the accepted single-path
20 kHz rate gate. The descriptor publishes both exact block sizes. A
scenario with `process_block_capacity_frames < 3840` is rejected; a larger capacity
does not authorize a different call size.
The decision, control, candidate, and repeat evidence are recorded in
[`../POST_PARITY_FIDELITY_RATE_GATE.md`](../POST_PARITY_FIDELITY_RATE_GATE.md); 10 kHz
remains historical parity evidence rather than another session mode.

For `finite_scenario`, `process_block()` returns blocks until the exact authored
horizon is exhausted, then returns a stable `EngineSessionCompleted`. For
`open_ended`, elapsed scenario time never produces `EngineSessionCompleted`;
successful calls continue the same mutable physical and DSP state until the owner
destroys the session or a typed processing failure occurs. A terminal processing error
is stable on later calls. Completion and diagnostic alternatives are owning values and
may allocate when copied across the C++ boundary. The admitted layout is frozen during
compilation; the block-varying capture validator, simulation, excitation, presentation,
and caller-buffer C ABI perform no allocation on a warmed successful block. Owning
diagnostics are constructed only after a block has already failed the allocation-free
admission check.

Open-ended means unbounded by authored elapsed time, not unbounded memory. Processing
retains the same fixed block storage, bounded command queue, bounded telemetry, and
constant-size streaming state as finite execution; it accumulates no duration-sized
PCM or capture history. Frame and block counters advance with checked `uint64_t`
arithmetic. Exhausting that clock is a typed terminal resource failure before another
block advances, not a hidden maximum-duration completion.

For a completed finite session,
`EngineSessionCompleted::live_controls_accepted` records whether the run diverged from
the authored control trajectory. If it is true, scenario-request-bound held-speed and
inertial-dyno result evidence is withheld instead of being mislabeled with the authored
request identity. The block telemetry and PCM still describe the executed controlled
session. A later command-journal identity may admit authoritative controlled-run
results; the current API does not fabricate one.

### 4.6 Destroy

Destroying a session releases only that session's mutable state. There is no implemented
drain or reusable-session reset operation. A different initial state or another run
requires a new session from the immutable compiled scenario.

Browser **Stop** is not destruction or physical completion: it pauses Worker
production while preserving the session. **Start** resumes that exact session, and
**Restart** deliberately destroys it and creates a fresh open-ended session.

## 5. Motion and control ownership

Throttle, load, and RPM cannot all be simultaneous authoritative commands. Every
compiled scenario selects exactly one mode.

| Mode | Current live-session status | Session results |
|---|---|---|
| `held_speed` | Authored fixed target executes; live commands are rejected | operating-point evidence, engine telemetry, audio |
| `prescribed_kinematic_sweep` | Authored trajectory executes; live commands are rejected | authored RPM trajectory, engine telemetry, audio |
| `held_dyno` | Finite or open-ended execution admits live throttle, ignition, fuel, target RPM, and maximum absorbing/driving torque after preparation | achieved crank RPM, signed actuator and opposite dyno reaction, mode sidecar, engine telemetry, audio |
| `load_target_held_capture` | Authored RPM/load target executes; live commands are rejected | converged operating-point evidence, engine telemetry, audio |
| `inertial_dyno` | Finite-scenario execution admits live throttle, ignition, and fuel after preparation | simulated RPM trajectory, dyno result evidence, engine telemetry, audio |
| `free_engine` | Finite or open-ended execution admits live selected-controller demand, ignition, fuel, limiter, and external resisting torque after the audible handoff | simulated crank RPM, requested and governor-resolved throttle, requested external resisting torque, engine telemetry, audio |
| `free_vehicle` | Finite or open-ended execution admits live selected-controller demand, ignition, fuel, limiter, selected gear, and clutch; starter and service brake are capability-gated by engine/rig data | simulated crank RPM, clutch and road-load state, vehicle speed/distance, mode sidecar, engine telemetry, audio |

`open_ended` is the interactive lifetime for the three dynamic operating-bench modes.
Their separately created `finite_scenario` sessions remain the exact authored capture
procedures.

`external_speed` is appropriate for a host game or editor scrubber that already owns
drivetrain RPM. The full simulator still calculates achieved load from its physical
state. A host-provided “load” may be retained as explicitly non-authoritative transient
or presentation context, but it cannot overwrite the simulator's achieved physical
load.

A later compiled-package audio follower may authoritatively consume RPM and a
versioned host load coordinate. That is a different capability from the full physics
session and must identify its load normalization and coast semantics.

### 5.1 FreeEngine inertia and neutral calibration

FreeEngine compilation resolves three distinct values:

```text
engine_baseline_inertia_kg_m2
attached_inertia_kg_m2
total_equivalent_inertia_kg_m2
```

The first is derived by the selected mechanism-family method:
`centered-slider-crank-cycle-mean-equivalent-inertia-v2` for direct journals or the
articulated one-level master-rod cycle-mean method for that admitted topology. The
second is authored or declared-default positive zero, and the third is their versioned
exact cycle-mean reference sum. Both methods evaluate rod-center translation at the
authored physical distance from crank pin, with omission deriving the exact midpoint.
Free-running dynamics evaluates matching analytic `M(theta)` and `dM/dtheta` from the
same engine mechanism and adds the attachment to the instantaneous `M(theta)`; the
reference sum validates that the same mechanism and attachment were compiled.
The external resisting-torque lane is independent and defaults to positive zero; it
is not used to duplicate engine losses.

The BMW M52B28 resolves an engine baseline of
`0.2108686520185204 kg*m^2`. Its neutral fixture has zero attached inertia and zero
external resistance. A headless interactive-recipe smoke requires its first
7,000-rpm crossing in `0.44`--`0.50 s`. That recipe begins with a short part-throttle
preparation, so the range is not the controlled pristine-engine-sim ablation envelope.

This interactive gate does not claim coastdown parity. Gas-exchange pumping already
contributes through cylinder pressure-volume torque. FreeEngine applies the authored
pristine crank-friction magnitude and the pristine C++ piston-wall law with its
previous-step wall-reaction dependency. A full authored-COM centered-slider inverse
dynamics replaces the legacy constraint solver for that reaction; an omitted rod COM
resolves to the exact midpoint. Direct held and coast traces keep its resulting
one-step friction-force difference below `0.4%` L1.
The generic Chen--Flynn accountant certifies only fixed warm preparation and is
discarded at dynamic release. It does not advance FreeEngine RPM, and released cycle
integration remains explicitly unavailable. Free-running mechanics evaluates pristine's
configuration-dependent centered-slider inertia equation at each left boundary. The
controlled response gate passes: WOT differs from pristine by `0.0134 s`, every coast
crossing by at most `0.0049 s`, and the long natural-balance mean by `1.079 RPM`. The
frozen pristine oracle remains the authority for each subsequent mechanics slice.

The certified one-level master-rod `FreeEngine`, `HeldDyno`, and `FreeVehicle` paths
use the corresponding articulated configuration inertia and leaf-first coupled wall
reactions. Their warm accountant uses each cylinder's certified swept displacement and
piston travel rather than substituting one common stroke. The radial-five 52,000-frame
FreeEngine procedure completes through the public session API. Commit `eb26214` also
admits its HeldDyno session under
`bounded-held-dyno-speed-constraint-one-level-master-rod-v1`, whose canonical
descriptor has SHA-256
`71b511ed6c5c2c29225cd96645e8c117dea4093cf965c22a76358f220b2bbe8b`.
Commit `11d5853` admits the corresponding FreeVehicle session. It retains
`nonnegative-speed-free-engine-one-level-master-rod-v1` as its crank-dynamics method,
derives `scenario.mode.engine_baseline_inertia_kg_m2` through
`one-level-master-rod-cycle-mean-equivalent-inertia-v1`, and identifies its composed
runtime as `low-order-free-vehicle-one-level-master-rod-v1`. The existing
`forward-vehicle-road-load-v1`, `bounded-forward-clutch-coupling-v1`, and
`bounded-forward-vehicle-drivetrain-pgs-v1` identities remain topology-neutral.

The source-backed radial loaded candidate's audition SHA-256 is
`8b2cc6620ef0e5e3f66ed18ebaf2990066fd813f9da1b11f97ee9eebf0f613eb`, and the user
accepted it on 2026-08-03. Its matched open-clutch control records zero clutch impulse
but is not sample-identical to FreeEngine because the existing FreeVehicle path commits
crank angle semi-implicitly while FreeEngine commits its motion integral. That small
numerical distinction is not road-load or clutch-load leakage. Direct/BMW execution
and request identities remain unchanged. Nested attachments and offset, geared, or
otherwise independent multi-crank master-rod mechanisms remain closed.

### 5.2 Remaining mode controls

Live imposed/prescribed RPM, generic brake or dyno-enable commands, load-following
targets, and motion-mode changes are not implemented. HeldDyno target RPM and its
separate absorbing/driving limits are implemented and do not switch ownership: a
constant target is the hold operation. Any future control must preserve the ownership
rules above and be capability-described rather than accepted by an untyped generic
payload. Changing motion mode requires a new session unless a later explicit
transition is designed and implemented.

### 5.3 Gear and clutch

Gear ratios, final drive, wheel inertia, and road load belong to the reusable
engine-package `rig`, not the physical engine object. A `free_vehicle` scenario selects
that rig and owns crank motion through the coupled drivetrain.

The session publishes the rig's ordered forward-gear descriptors. Timestamped
selected-gear ordinal, clutch engagement, and capability-gated service-brake
application are implemented FreeVehicle controls. They remain session operating state,
not structural engine-definition fields.

## 6. Current mutation boundary

There are three practical mutation boundaries:

| Class | Meaning |
|---|---|
| `implemented live command` | Timestamped command may change it in an existing session under its admitted mode. |
| `session_recreate` | The compiled scenario is reusable, but a new session is required. |
| `program_recompile` | JSON or asset change must compile a new immutable program. |

Current classification:

| Field family | Class |
|---|---|
| selected throttle-controller demand | implemented live command for `held_dyno`, `inertial_dyno`, `free_engine`, and `free_vehicle` after preparation; direct means linkage demand, governor means normalized speed demand |
| ignition/fuel enable | implemented live command for `held_dyno`, `inertial_dyno`, `free_engine`, and `free_vehicle` after preparation |
| limiter enable | implemented live command for `free_engine` and `free_vehicle` after preparation |
| external resisting torque | implemented live command for `free_engine` after preparation |
| starter enable | implemented live command for `free_engine` and `free_vehicle` after preparation when the compiled engine declares a cranking starter |
| held-dyno target RPM and absorbing/driving limits | implemented live commands for finite or open-ended `held_dyno` after preparation |
| selected gear and clutch engagement | implemented live commands for finite or open-ended `free_vehicle` after preparation |
| service-brake application | implemented live command for finite or open-ended `free_vehicle` after preparation when the rig declares positive brake capacity |
| prescribed RPM, load-following target, generic dyno enable, or mode transition | not implemented as public live commands |
| audition master, route monitor gain, mute, IR wet mix | not implemented as live commands |
| motion ownership mode and initial state | `session_recreate` |
| FreeEngine attached inertia or authored external resisting-torque trajectory | `session_recreate` |
| delivery sample rate, output bus layout, capacities, quality, seed | `session_recreate` |
| ambient or initial thermal state | `session_recreate` |
| banks, cylinders, firing order, crank geometry, inertias | `program_recompile` |
| intake/exhaust topology and per-cylinder routing | `program_recompile` |
| runner, plenum, primary, collector, and pipe geometry | `program_recompile` |
| valve/cam profiles and port/flow curves | `program_recompile` |
| fuel, combustion, friction, heat-transfer, and model methods | `program_recompile` |
| ignition timing map and limiter definition | `program_recompile` |
| impulse-response asset or convolution method | `program_recompile` |
| source-route graph and authored default calibration | `program_recompile` |

There is currently no generic parameter-descriptor mutation API, no route-monitoring
override, no session reset, and no cross-session state transfer. A future editor can
compile an inactive replacement and create a second session before swapping or
crossfading at an adapter-owned delivery boundary. Compilation failure must leave the
playing session untouched. Physical geometry remains a recompile, never a gain-like
preview control.

## 7. Audio bus contract

The session descriptor exposes stable bus descriptors:

```text
EngineAudioBusDescriptor
  id
  bus kind
  physical source-route kind
  optional source-route ID
  signal disposition
  channel count
  delivery sample rate
```

Bus IDs are semantic and are not inferred from vector position. Route-local bus roles
are source-generic; each descriptor separately carries its physical `SourceRouteKind`
and optional `RouteId`. The implemented, mono, 192 kHz buses are:

- for every admitted source route: dry, configured-transfer, and selected signals;
- a raw master;
- an audition master.

`signal disposition` is exact, not advisory. `active` means the bus is backed by the
implemented signal path. `declared_silent` means the topology is public but this
checkpoint intentionally publishes zeros; clients must not mistake its existence for
implemented audible fidelity.

Every successful block returns all advertised buses as borrowed `float32` spans of
exactly 3,840 samples. The spans remain valid only until the next enqueue/process
operation, session move, or session destruction. File publishers must consume or copy
them before advancing the session.

Exhaust routes, admitted intake routes, and both masters are `active`. Each active
intake route is backed by captured plenum pressure on the canonical 20 kHz capture
clock, ambient-pressure subtraction, causal reconstruction to 192 kHz, deterministic
10 Hz DC removal, and its authored source gain. Its dry, configured-transfer, and
selected buses are identical because intake uses no exhaust random conditioning,
impulse response, or convolution. The selected intake bus participates once in the
ordered raw and audition masters.

This is the sole intake implementation. The prior `declared_silent` intake checkpoint
is historical evidence rather than a runtime option or compatibility path. It was
superseded after the 2026-08-04 A/B/C listening gate documented in
[`POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md`](../POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md).
Mechanical route kinds remain reserved and unadmitted until a real signal
implementation has its own acceptance evidence.
The sole portable boundary is C ABI v6; there are no earlier-version aliases or
compatibility decoders. Caller-selected bus subsets and caller-owned output buffers are
also not part of the implemented C++ surface.

The audition master is a convenience listening mix. Game hosts should normally consume
separate buses and own spatial placement, distance attenuation, occlusion,
environmental reverb, and final mix policy.

The portable processing format is finite `float32`. File encoders choose PCM depth and
container independently. NaN, infinity, or an internal amplitude-bound violation is a
typed processing failure, never silently clipped into validity.

## 8. Telemetry and capability discovery

After session creation, `EngineSession::descriptor()` returns:

- engine and scenario IDs;
- caller control-command and returned-telemetry capacities;
- the exact 20 kHz physics/capture and 192 kHz delivery rates;
- the exact 400/3,840 frames per block;
- the explicit execution kind;
- preparation block count;
- the exact authored total block count for `finite_scenario`, or canonical
  `0` for `open_ended`;
- the explicit seven-way motion mode;
- ordered forward-gear descriptors with stable ID, authored ordinal, ratio, and
  semantic ID when the selected rig has a transmission;
- all audio bus descriptors;
- the exact bit mask of live controls admitted by this session mode.

The execution kind is the discriminator. Callers must not infer open-ended execution
from the zero count, and JavaScript adapters expose the open-ended total as `null`
rather than infinity or an invented duration.

Each block identifies its ordinal, preparation/audible phase, and exact half-open
physics and delivery ranges. It returns one `EngineTelemetryFrame`: the final
`EngineCaptureSample` of that block plus its physics-step end. That capture carries its
own validity mask and currently available crank angle, angular motion/RPM, requested
and resolved throttle, intake command, ignition/fuel/starter/dyno/limiter state,
requested FreeEngine external resisting torque, and torque telemetry. For a governor,
requested throttle is the normalized speed demand while resolved throttle is the
stateful actuator output actually sent through the linkage.

The frame also carries nullable mode-owned sidecars. HeldDyno publishes its applied
target and limits, required/applied actuator torque, and constraint disposition.
FreeVehicle publishes vehicle speed/distance, selected gear, applied clutch/brake,
clutch torque/slip/disposition, and requested/applied road load. Both sidecars are
absent during preparation and for nonapplicable modes; exactly the selected mode's
sidecar is present after release. The accepted BMW vehicle descriptor publishes five
gears with ordinals `1..5` and ratios `4.21`, `2.49`, `1.66`, `1.24`, and `1.00`.

The authored `telemetry_capacity_frames` is a returned-record bound. It is not an audio
frame count and it does not reserve the internal combustion event journal. The current
method requires at least one record and returns exactly one per successful block.
Caller-selected telemetry channels, per-bus meters, event streams, and dropped-record
counters remain future capabilities.

Browser transport statistics such as ring fill, callback underruns, worker lead, and
estimated wall-clock realtime factor belong to the browser adapter. They are not
physical engine telemetry.

## 9. Deterministic offline execution

`bake()` explicitly creates a fresh `finite_scenario` session and runs the authored
recipe to completion without queuing additional controls. A caller driving a finite
`EngineSession` directly may enqueue typed commands before each affected block. The
finite horizon returns `EngineSessionCompleted`; it does not reset or drain the
session. The native publisher discards blocks explicitly marked as preparation and
encodes audible blocks. The execution-kind choice does not change any established
finite bake, WAV, manifest, or deterministic-completion claim.

Deterministic execution requires:

- immutable compiled-scenario and asset identities;
- complete session configuration;
- explicit seed and initial state;
- integer-frame controls with stable sequence numbers;
- versioned clock projection and interpolation methods;
- no wall-clock or audio-device input;
- the exact 400/3,840-frame executable method quantum;
- a recorded build, target, numeric runtime, and method identity.

The same build, target, numeric runtime, compiled scenario, configuration, and command
stream must be byte-stable. The admitted native target uses the existing SysV x87
extended accumulator; wasm32 uses IEEE binary128 and identifies that difference in the
two affected presentation method IDs. Native and WASM are therefore independently
byte-stable, not presumed byte-identical. The headless parity gate requires exact
topology, clocks, controls, discrete telemetry, and completion state, then applies
predeclared tight numeric bounds to continuous telemetry and core Float32 PCM.

The browser preview is not authoritative artifact evidence. A downloadable browser
WAV may be useful for iteration, but a production manifest identifies whether it came
from the native authoritative renderer or a separately admitted WASM numeric
environment.

## 10. Portable ABI

[`c_api.h`](../../include/engine_sim_offline/c_api.h) is the one implemented foreign
runtime boundary. It exposes one exact ABI version rather than a family of legacy
layouts:

```text
strict engine JSON + caller asset bytes -> compiled engine handle
compiled engine + strict scenario JSON -> compiled scenario handle
compiled scenario + required execution kind -> mutable session handle
timestamped typed controls -> bounded session queue
session process -> caller-owned PCM buses + POD telemetry
```

The creation call is:

```c
eso_create_session(context, scenario, execution_kind, out_session);
```

There is no form that omits `execution_kind`.

The implemented ABI:

- the sole accepted exact version is `ESO_C_API_VERSION == 6`, with no older-layout
  decoder or compatibility symbol family;
- no C++ exception crosses the boundary;
- every call returns an explicit status;
- parse/compile diagnostics and related locations are copied into caller-owned buffers;
- JSON appears only at compile time;
- audio, controls, and telemetry use fixed-layout structs and bounded views;
- ABI sizes, endianness, enum values, and the exact version are queryable;
- handles carry context, kind, slot, and non-wrapping generation checks so stale and
  wrong-kind handles fail;
- a session owns the immutable compiled scenario/engine storage it needs, so parent
  handles may be released after session creation;
- `eso_create_session` requires exactly
  `ESO_SESSION_EXECUTION_FINITE_SCENARIO` or
  `ESO_SESSION_EXECUTION_OPEN_ENDED`;
- the session descriptor reports that kind and uses canonical
  `total_block_count == 0` only for open-ended execution;
- the descriptor reports one of seven motion modes, and gear query/copy functions
  expose the ordered forward-gear inventory;
- twelve capability bits and control kinds share one fixed-layout command; gear uses
  `id_value`, while the other payload fields must retain their canonical zero values;
- `eso_session_telemetry_t` wraps engine telemetry with HeldDyno and FreeVehicle
  presence flags and sidecars; every absent sidecar is bytewise all-zero;
- requested PCM and telemetry buffers are completely preflighted before the session
  advances;
- successful control conversion and block processing use session-owned bounded scratch;
- WASM linear-memory growth is disabled;
- the core owns no DOM, Web Audio, filesystem, URL, fetch, or JavaScript object.

The Emscripten module is a 128 MiB fixed-memory wasm32 build with C++ WebAssembly
exceptions contained behind the C boundary, no filesystem, no native thread, and no
second JavaScript implementation. The JavaScript wrapper may offer promises around
compile and session creation, but `eso_session_process` remains synchronous and
preserves the exact method quantum.

The reproducible gate is [`verify-wasm-parity.sh`](../../scripts/verify-wasm-parity.sh).
It uses the pinned Emscripten 6.0.4 container digest recorded by the script, smoke-tests
all public module exports and the fixed memory, runs the wasm32 binary128 admission
test, and drives an 18-block controlled BMW fixture through the C ABI on both targets.
The fixture has exact semantic transcript SHA-256
`cfa562577ff0d42da60f30b6b010626cb85ceccc48d0863342b600d241b17286`.
Its native and WASM bundle hashes are respectively
`68fa71c94942a4ed0efcdb03ba44183c9f903542bfc62f03f40a5854b7331ede` and
`005e63eb509e79ca686634f0bc198e9ad2c3f0a5311e211234d8072232d29952`.
Across 7,680 audition samples, observed maximum absolute Float32 PCM error is
`7.450580596923828e-9` and RMS error is `7.063297896598868e-10`; the checked ceilings
and each target's exact telemetry/PCM hashes live in
[`parity_expectations.json`](../../tests/wasm/parity_expectations.json).

## 11. Browser adapter

The implemented browser transport has this architecture:

```text
main/UI thread
  - fetch JSON and assets
  - edit and validate authoring input
  - post rebuild requests and live commands
  - display posted telemetry, adapter statistics, and failures

dedicated Worker
  - instantiate WASM
  - compile and atomically replace CompiledScenario
  - own the open-ended live EngineSession
  - render exact 3,840-frame/20 ms session blocks ahead of playback
  - adapt the selected canonical bus from 192 kHz to the AudioContext rate
  - write the bounded device-rate PCM ring
  - run fresh finite-scenario sessions for authored-capture WAV export

AudioWorklet
  - read PCM ring
  - consume 128-frame Web Audio render quanta
  - apply bounded fade-to-zero and recovery ramps
  - count genuine streaming underruns
```

The exact transport identifier is `engine-sim-offline/browser-worker-v3`. Every build
request carries its required execution kind explicitly; the Worker and browser runtime
pass it unchanged to the C API and never infer lifetime from scenario JSON. Build
results publish the motion mode, exact capability list, and copied forward-gear descriptors;
telemetry messages carry the nullable HeldDyno/FreeVehicle sidecars decoded from the C
ABI rather than reconstructed in JavaScript.

The full simulator does not run in the AudioWorklet. The Worker produces one exact
3,840-frame session block at a time; at 192 kHz that is 20 ms. A versioned Worker-side
129-tap windowed-sinc adapter converts the selected canonical bus to the actual
`AudioContext.sampleRate` and writes device-rate frames to the shared PCM ring. The
AudioWorklet merely consumes that ring in each callback's actual frame count. This
preserves the executable method quantum without pretending that the simulator supports
arbitrary callback-sized calls or moving DSP into JavaScript.

Startup has an explicit `preparing` state. The Worker processes bounded four-block
turns, completes the scenario's preparation, and fills the requested playback lead
before it publishes the shared ring to the UI. It marks the ring `streaming` only
after that priming gate. A paused, drained, or replaced session is therefore not
misreported as a streaming underrun. Resume re-primes a ring that no longer has the
requested lead. Once released, the open-ended session continues the same crank, gas,
random, filter, convolution, and resampler state; it is not a loop of the authored
finite clip.

The Worker maintains a bounded lead selected by the adapter. A control request carries
a nonempty atomic batch. Every command has an absolute delivery-frame target that must
not already have been generated and must not fall inside preparation; one default
timestamp is resolved once for the whole batch when callers omit it. The Worker passes
the complete batch to one C enqueue operation and returns one `controls-result` before
processing the containing 20 ms block. Audio already in the ring cannot be changed
retroactively, so ring fill plus one method quantum defines the measured control lead;
the UI displays that latency rather than claiming zero-latency response.

PCM transport is one fixed-capacity single-producer/single-consumer
`SharedArrayBuffer` ring with atomic read/write indices and counters. Controls,
telemetry, errors, and low-rate adapter statistics use structured `postMessage`
traffic. They do not share or mutate PCM ownership, and there are no unused
control/telemetry ring protocols. The AudioWorklet never waits for the Worker.

Browser authored-capture export creates a fresh unpaced `finite_scenario` session
through the same C API and selects the same canonical bus. It executes the finite
scenario recipe and serializes deterministic mono Float32 WAVE bytes in memory. This
operation is separate from the open-ended interactive session: controls accepted
after the authored horizon cannot be silently truncated or represented as part of that
finite capture. Exporting an arbitrary interactive interval requires a separately
bounded recorder or a future explicit replay extent; the current contract does not
call the live session complete. The preview WAV is target-specific WASM evidence;
authoritative PCM24 artifact publication and manifests remain native-adapter
responsibilities.

Browser requirements:

- AudioWorklet is used only in a secure context;
- shared-memory operation requires cross-origin isolation;
- the server supplies `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` or an explicitly validated equivalent;
- the adapter verifies `crossOriginIsolated` before creating shared transport;
- audio starts only after the required user gesture;
- the accepted transport accommodates the current 128-frame Web Audio render quantum
  while checking each callback's actual frame count rather than overrunning a view;
- output-rate conversion occurs in the Worker after the canonical 192 kHz master and
  before the shared device-delivery ring;
- no asset fetch, JSON parse, WASM compilation, memory growth, blocking lock, or
  unbounded logging occurs in the AudioWorklet callback.

If cross-origin isolation or AudioWorklet is unavailable, the harness reports an
unsupported capability instead of silently using `ScriptProcessorNode` or another
renderer. There is no MessagePort-copy audio fallback or JavaScript engine renderer.

The real-module integration exports 7,680 canonical Float32 samples with SHA-256
`7362df73a4f3bc0925fad36285f499d2c51f2b61d4dccf8a743f45a42f4728c9`.
The headless Chrome gate exports the complete 3,840,056-byte BMW warm-running
free-rev Float32 WAVE and pins its current SHA-256 in the executable browser test,
continues past the authored 5.5-second horizon, verifies Stop/Start state continuity
and fresh Restart state, and reports zero startup underrun frames/events.
The reproducible gate is
[`verify-browser-workbench.sh`](../../scripts/verify-browser-workbench.sh).

Relevant platform references:

- [MDN: AudioWorklet](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorklet)
- [MDN: AudioWorkletProcessor.process()](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorkletProcessor/process)
- [MDN: COOP and cross-origin isolation](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Cross-Origin-Opener-Policy)
- [Emscripten: Wasm Audio Worklets API](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html)

## 12. Failure semantics

Failures are typed by lifecycle stage and carry a stable code, human message, and
JSON or contract path where applicable.

### 12.1 Compile failure

Examples include malformed JSON, unsupported schema or method, duplicate IDs, dangling
routing, invalid units/ranges, missing assets, asset hash mismatch, unsupported
topology, or cost/memory bounds beyond the selected target.

No partial compiled scenario is returned. If compilation was a live editor rebuild,
the active compiled scenario and session continue unchanged.

### 12.2 Session-creation failure

Examples include unsupported motion mode, output rate, bus set, quality, initial state,
capacity, unknown execution kind, or an `open_ended` request for a capture-only mode.
No session is returned and the compiled scenario remains reusable.

### 12.3 Control rejection

Examples include a past timestamp, duplicate sequence, queue overflow, out-of-range
value, unsupported event, or ownership conflict. Rejection does not modify the queued
command stream.

### 12.4 Processing failure

Examples include non-finite physical state, solver failure, internal buffer-bound
violation, invalid caller audio storage, or an invariant violation. Processing failure
is terminal for that session. The same error remains observable on later calls. The core
never returns fabricated audio, stale repeated samples, a tone, or a lower-fidelity
method.

### 12.5 Browser underrun

An AudioWorklet underrun is an adapter pacing failure, not a physics result. The
worklet:

1. consumes every available complete frame;
2. applies a bounded click-safe ramp to zero;
3. emits silence for the unavailable remainder;
4. increments an atomic underrun counter;
5. never repeats stale PCM.

The Worker may refill after a transient underrun, but the UI keeps the fault visible.
A recurring underrun fails the realtime-preview performance gate. It does not alter
offline render quality.

### 12.6 Adapter replacement failure

The core itself does not swap sessions. The browser adapter compiles and creates a
replacement before committing it, then disposes the previous program. A parse,
validation, asset, or session-creation failure restores the prior adapter state and
leaves the previous program/session available. Successful replacement deliberately
starts a fresh session; no physical state transfer, seamless crossfade, or hidden
partial mutation is implied.

## 13. HTML authoring workbench

The implemented workbench provides:

- raw engine and scenario JSON editors, local open/save/format actions, a destructured
  scalar property inspector, and explicit **Apply & rebuild**;
- declared-asset discovery, file selection, and path-addressed validation/compile
  diagnostics;
- engine/scenario identity, current dirty/rebuild state, selected publication bus, and
  the exact capabilities reported by the compiled session;
- capability-gated throttle, ignition, fuel, momentary starter, limiter, FreeEngine
  external resistance, HeldDyno target/limits, and FreeVehicle gear/clutch/brake
  controls; related dyno and drivetrain values use atomic Worker batches;
- continuous FreeEngine, HeldDyno, and FreeVehicle start/stop/restart actions, while
  capture-only modes retain finite-procedure run/pause/fresh-replay actions;
- RPM, torque, power, recent telemetry trace, returned HeldDyno/FreeVehicle state,
  simulation realtime factor, measured worker lead, ring fill, and real
  callback-underrun counters;
- deterministic downloadable Float32 WAV export of the authored finite capture from a
  fresh `finite_scenario` session using the same C ABI.

The workbench groups repository scenarios by engine and includes the accepted BMW
held-dyno pull/lift, vehicle launch/shift, and fifth-gear pull/lift procedures. It
renders its explicit mode from the descriptor and its neutral/forward options from the
published gear inventory. The UI does not invent imposed-RPM/load controls, display
fake per-bus meters, or mutate structural JSON directly inside a running solver. Route
selection creates a fresh session. Structural edits become active only after an
explicit successful rebuild.

The full browser gate compiles the BMW fixture, exports the complete authored
warm-running free-rev capture, starts and primes open-ended playback, applies throttle,
continues past the authored horizon, verifies Stop/Start/Restart, selects another
route, and reports zero startup underruns on the development PC.

The operating-bench runtime gate additionally exercises actual open-ended WASM
HeldDyno and FreeVehicle sessions. It verifies capability masks `455` and `3631`, the
five BMW forward gears, a three-command atomic batch, null preparation sidecars,
populated released sidecars, and rejection of an invalid-gear batch without partial
admission.

The workbench UI gate applies one three-value dyno batch and one three-value drivetrain
batch, observes the exact returned sidecars, and verifies stop/resume/fresh-restart
semantics for the continuous benches. The existing canonical finite-export browser WAV
hash remains unchanged.

## 14. Implementation order

The authoritative execution sequence is [`PLAN.md`](../../PLAN.md). The native
session/bake cutover is checkpoint 8; C ABI and WASM are checkpoint 9; the
Worker/ring/AudioWorklet transport and initial workbench are checkpoint 10; and the
operating-mode/control/telemetry publication is slice 13. All are implemented and
sealed. Slice 14 adds the visible full operating bench and named procedures. Slice 15
owns the wider canonical capture-procedure library. Later control capabilities remain
fail-closed until individually implemented.

Each step preserves one implementation path. No temporary browser synthesizer,
pre-recorded engine loop, or compatibility parser becomes a production dependency.
