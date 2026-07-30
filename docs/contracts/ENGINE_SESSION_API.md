# Portable engine compile and session API

Status: portable native C++ session core implemented; C ABI, WASM, browser transport,
and HTML harness remain future work

Applies to: JSON engine authoring, immutable engine compilation, mutable simulation
sessions, native offline rendering, WASM preview, runtime control ownership, streaming
audio buses, telemetry, and fail-closed adapter behavior

This contract records the implemented native session boundary and the intended browser
adapter around that same boundary. Future capabilities are called out explicitly. It is
not a compatibility surface for `.mr`, engine-sim, the failed offline fork, or
historical milestone-specific types.

## 1. Goals and non-goals

The implemented native API:

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

The planned C ABI, WASM build, Worker/ring adapter, and authoring UI will wrap this
implementation rather than introduce another engine renderer.

The first implementation does not promise:

- `.mr` parsing or syntax compatibility;
- mutation of arbitrary engine geometry while a solver is running;
- byte-identical floating-point output between native and WASM targets;
- a complete vehicle, tire, road, or transmission simulation;
- that the full simulator will always meet an audio callback deadline;
- a second reduced-fidelity realtime renderer.

## 2. One implementation, native now and browser next

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

The native left-hand path is implemented. The browser right-hand path is the next
adapter and does not exist yet. Offline and interactive execution differ only in
pacing and publication:

- Native offline rendering calls `process_block()` as quickly as the machine allows and
  sends the resulting blocks to artifact encoders.
- The browser Worker will call the same `process_block()` function only far enough
  ahead to keep a bounded playback ring supplied.
- The future AudioWorklet will consume already-produced audio. It will not contain
  another engine model.

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

The compiled scenario selects one execution context without changing the engine:

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

`quality.process_block_capacity_frames` is measured in delivery-rate PCM frames. The
current method requires capacity for at least 3,840 frames and always returns exactly
3,840 delivery frames per successful block. A larger authored capacity does not change
that method quantum. `quality.event_queue_capacity` bounds caller-authored live control
commands; it is not the internal combustion/event journal. The unfortunately named
`quality.telemetry_capacity_frames` bounds telemetry records returned by one process
call; the current session returns one record.

Changing the motion mode, delivery rate, quality profile, seed, bus layout, initial
state, or bounded capacities requires creating a new session.

### 3.3 Live controls

Live controls are typed, timestamped commands sent after session creation. They do not
modify the engine document or compiled scenario. The implemented payloads are:

- requested throttle in `[0, 1]`;
- ignition enabled;
- fuel enabled.

The current session admits those live payloads only for `inertial_dyno` ownership.
Starter, limiter, imposed RPM, brake/dyno targets, gear/clutch, presentation monitoring,
and lifecycle commands are not live session capabilities yet. Their presence in an
authored offline scenario does not imply a corresponding live command.

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
    const compile::CompiledScenario &scenario);
```

Session creation validates the selected capabilities and ownership mode, compiles
method-owned simulation, excitation, DSP, and IR-kernel state, reserves bounded
processing storage, and returns a mutable `EngineSession`.

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
warm-up.

Live commands may be queued before processing begins, but any command whose absolute
delivery-frame target falls inside the preparation interval is rejected with
`unavailable_during_preparation`. The first legal target is
`preparation_block_count * 3840`.

### 4.4 Enqueue controls

The implemented operation is:

```cpp
std::optional<EngineControlRejection> EngineSession::enqueue_controls(
    std::span<const EngineControlCommand> commands);
```

Every command contains:

- an absolute delivery-frame index relative to the session origin;
- a strictly increasing caller sequence number;
- one of the three implemented typed control payloads.

Delivery-frame indices, not milliseconds or wall-clock timestamps, are authoritative.
The selected numerical method defines the causal projection from a delivery frame to
its internal physics/control clock. A command that cannot be represented under that
method is rejected during enqueue or session creation.

Commands must have nondecreasing delivery frames, strictly increasing unique sequence
numbers, and must target a frame that can still be generated by `process_block()`. A
late command, a full command queue, an invalid throttle, a preparation or post-horizon
target, a terminal session, or a command not owned by the selected mode is rejected
atomically. The session never silently applies it “as soon as possible.”

### 4.5 Process

The implemented operation is:

```cpp
EngineSessionProcessResult EngineSession::process_block();
```

One successful `process_block()` call:

- advances exactly 200 physics frames at 10,000 Hz and 3,840 delivery frames at
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
choice. It matches the existing FFT/resampling arithmetic and is required to preserve
the accepted BMW byte output. The descriptor publishes both exact block sizes. A
scenario with `process_block_capacity_frames < 3840` is rejected; a larger capacity
does not authorize a different call size.

`process_block()` returns blocks until the compiled finite horizon is exhausted, then
returns a stable `EngineSessionCompleted`. A terminal processing error is likewise
stable on later calls. Completion and diagnostic alternatives are owning values and may
allocate when copied across the public boundary. The current internal capture-contract
validator also uses bounded transient allocations per block; checkpoint 9 removes
those before admitting the fixed-memory WASM boundary.

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

## 5. Motion and control ownership

Throttle, load, and RPM cannot all be simultaneous authoritative commands. Every
compiled scenario selects exactly one mode.

| Mode | Current live-session status | Session results |
|---|---|---|
| prescribed/external speed | Authored trajectory executes; live commands are rejected | authored RPM trajectory, engine telemetry, audio |
| held speed/load-target held | Authored target executes; live commands are rejected | operating-point evidence, engine telemetry, audio |
| `inertial_dyno` | Live throttle, ignition, and fuel commands are admitted after preparation | simulated RPM trajectory, dyno result evidence, engine telemetry, audio |

`external_speed` is appropriate for a host game or editor scrubber that already owns
drivetrain RPM. The full simulator still calculates achieved load from its physical
state. A host-provided “load” may be retained as explicitly non-authoritative transient
or presentation context, but it cannot overwrite the simulator's achieved physical
load.

A later compiled-package audio follower may authoritatively consume RPM and a
versioned host load coordinate. That is a different capability from the full physics
session and must identify its load normalization and coast semantics.

### 5.1 Future mode controls

Live imposed RPM, held-RPM targets, brake torque, dyno controller targets, and motion
mode changes are not implemented. When added, they must preserve the ownership rules
above and be capability-described rather than accepted by an untyped generic payload.
Changing motion mode will require a new session unless a later explicit transition is
designed and implemented.

### 5.2 Gear and clutch

Gear ratios, final drive, wheel inertia, and road load do not belong in
`authoring::EnginePackageDocument`.

The implemented session API has no gear or clutch live payload. A future
`gear_index`/`clutch_01` context must remain separate from engine definition and may
only have mechanical effect when an explicit test-cell drivetrain owns that behavior.

An eventual road-audition model is a separate test-cell definition composed with the
compiled scenario. It must declare which subsystem owns crank motion and equivalent
inertia.

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
| throttle request | implemented live command for `inertial_dyno` after preparation |
| ignition/fuel enable | implemented live command for `inertial_dyno` after preparation |
| starter/limiter enable | not implemented as live commands |
| mode-owned RPM, brake, or controller target | not implemented as live commands |
| gear/clutch context | not implemented as live commands |
| audition master, route monitor gain, mute, IR wet mix | not implemented as live commands |
| motion ownership mode and initial state | `session_recreate` |
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
  optional exhaust route ID
  channel count
  delivery sample rate
```

Bus IDs are semantic and are not inferred from vector position. The implemented,
mono, 192 kHz buses are:

- for every exhaust outlet route: dry, configured-IR, and configured-selected signals;
- a raw master;
- an audition master.

Every successful block returns all advertised buses as borrowed `float32` spans of
exactly 3,840 samples. The spans remain valid only until the next enqueue/process
operation, session move, or session destruction. File publishers must consume or copy
them before advancing the session.

Intake and mechanical buses are not currently advertised. Their absence is an explicit
missing capability, not a silent placeholder. Caller-selected bus subsets and
caller-owned output buffers are also not part of the implemented C++ surface.

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
- the exact 10 kHz physics and 192 kHz delivery rates;
- the exact 200/3,840 frames per block;
- total and preparation block counts;
- all audio bus descriptors;
- whether this session admits the implemented live controls.

Each block identifies its ordinal, preparation/audible phase, and exact half-open
physics and delivery ranges. It returns one `EngineTelemetryFrame`: the final
`EngineCaptureSample` of that block plus its physics-step end. That capture carries its
own validity mask and currently available crank angle, angular motion/RPM, requested
and resolved throttle, intake command, ignition/fuel/starter/dyno/limiter state, and
torque telemetry.

The authored `telemetry_capacity_frames` is a returned-record bound. It is not an audio
frame count and it does not reserve the internal combustion event journal. The current
method requires at least one record and returns exactly one per successful block.
Caller-selected telemetry channels, per-bus meters, event streams, and dropped-record
counters remain future capabilities.

Browser transport statistics such as ring fill, callback underruns, worker lead, and
estimated wall-clock realtime factor belong to the browser adapter. They are not
physical engine telemetry.

## 9. Deterministic offline execution

`bake()` creates a fresh session and runs it to completion without queuing additional
controls. A caller driving `EngineSession` directly may enqueue typed commands before
each affected block. The finite horizon returns `EngineSessionCompleted`; it does not
reset or drain the session. The native publisher discards blocks explicitly marked as
preparation and encodes audible blocks.

Deterministic execution requires:

- immutable compiled-scenario and asset identities;
- complete session configuration;
- explicit seed and initial state;
- integer-frame controls with stable sequence numbers;
- versioned clock projection and interpolation methods;
- no wall-clock or audio-device input;
- the exact 200/3,840-frame executable method quantum;
- a recorded build, target, numeric runtime, and method identity.

The same build, target, numeric runtime, compiled scenario, configuration, and command
stream must be byte-stable. Native and WASM builds are not presumed byte-identical
because their math libraries, compiler lowering, SIMD, and runtime environments may
differ. Cross-target equivalence is accepted only under separately declared numeric
and listening tolerances.

The browser preview is not authoritative artifact evidence. A downloadable browser
WAV may be useful for iteration, but a production manifest identifies whether it came
from the native authoritative renderer or a separately admitted WASM numeric
environment.

## 10. Portable ABI

The semantic API is implemented in C++. A C ABI for WASM and other foreign runtimes is
future work. It should expose opaque generation-checked handles around the implemented
lifecycle, conceptually:

```text
api_version
compile_engine_package
destroy_compiled_scenario
query_compiled_scenario
create_session
destroy_session
enqueue_control_batch
process_session_block
copy_last_diagnostics
```

Future ABI rules:

- no C++ exception crosses the boundary;
- every call returns an explicit status;
- diagnostics are copied into caller-owned buffers;
- JSON appears only at compile time;
- audio, controls, and telemetry use fixed-layout structs and bounded views;
- sizes, alignments, endianness, enum values, and schema versions are explicit;
- handles are generation-checked so stale handles fail;
- WASM linear-memory growth is disabled while exported buffer views are active;
- the core owns no DOM, Web Audio, filesystem, URL, fetch, or JavaScript object.

The JavaScript wrapper may offer promises around compile and session creation, but the
future `process_session_block` call remains synchronous and preserves the exact method
quantum.

## 11. Future browser adapter

The browser adapter is not implemented yet. Its required architecture is:

```text
main/UI thread
  - fetch JSON and assets
  - edit and validate authoring input
  - send rebuild requests and live commands
  - display telemetry and failures

dedicated Worker
  - instantiate WASM
  - compile CompiledScenario
  - own EngineSession
  - render exact 3,840-frame/20 ms session blocks ahead of playback
  - adapt the canonical master from 192 kHz to the AudioContext rate
  - write device-rate PCM and telemetry rings

AudioWorklet
  - read PCM ring
  - consume 128-frame Web Audio render quanta
  - apply final click-safe mute/master/crossfade
  - report underruns
```

The full simulator does not run in the AudioWorklet. The Worker produces one exact
3,840-frame session block at a time; at 192 kHz that is 20 ms. A versioned Worker-side
output-rate adapter converts only the canonical master to the actual
`AudioContext.sampleRate` and writes device-rate frames to the shared PCM ring. The
AudioWorklet merely consumes that ring in the callback's actual frame count. This
preserves the executable method quantum without pretending that the simulator supports
arbitrary callback-sized calls.

The Worker maintains a bounded lead selected by the adapter. A UI command carries an
absolute delivery-frame target that must not already have been generated and must not
fall inside preparation. The Worker must enqueue it before processing the containing
20 ms block. Audio already in the ring cannot be changed retroactively, so ring fill
plus one method quantum defines the measured control lead; the UI must display that
latency rather than claim zero-latency response.

The PCM transport will be a single-producer/single-consumer ring in
`SharedArrayBuffer`, with atomic read/write indices. Controls and telemetry will use
separate bounded rings so UI traffic cannot corrupt PCM ownership. The AudioWorklet
never waits for the Worker.

Browser requirements:

- AudioWorklet is used only in a secure context;
- shared-memory operation requires cross-origin isolation;
- the server supplies `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` or an explicitly validated equivalent;
- the adapter verifies `crossOriginIsolated` before creating shared transport;
- audio starts only after the required user gesture;
- the initial accepted transport uses the current 128-frame Web Audio render quantum
  and still checks each callback's actual frame count rather than overrunning a view;
- output-rate conversion occurs in the Worker after the canonical 192 kHz master and
  before the shared device-delivery ring;
- no asset fetch, JSON parse, WASM compilation, memory growth, blocking lock, or
  unbounded logging occurs in the AudioWorklet callback.

A MessagePort-copy fallback may be implemented for diagnostics, but it is not the
accepted low-latency path and must identify its added latency and allocation behavior.
If cross-origin isolation or AudioWorklet is unavailable, the harness reports an
unsupported capability instead of silently using `ScriptProcessorNode` or another
renderer.

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
or capacity. No session is returned and the compiled scenario remains reusable.

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

### 12.6 Future replacement failure

The implemented core does not swap sessions. A future adapter may make an inactive
replacement audible only after complete compilation, session creation, preparation,
and an explicit adapter-owned swap. Failure at any earlier step must leave the current
session untouched; no state transfer is implied.

## 13. Future minimum HTML harness

The harness is not implemented yet. Its first useful surface contains:

- one raw JSON editor and an explicit **Apply/rebuild** action;
- path-addressed compile diagnostics;
- engine/scenario identity and current dirty/rebuild state;
- authored scenario mode selection;
- the currently admitted live throttle, ignition, and fuel controls for inertial dyno;
- RPM input or display according to the selected ownership mode;
- read-only named-bus meters and adapter-owned monitoring;
- start, stop, new-session/restart, and canonical dyno-pull actions;
- simulation realtime factor, worker lead, ring fill, callback underrun, and control
  latency display;
- downloadable preview WAV rendered by the same session API in an unpaced Worker job.

The harness does not need a complete visual engine-sim GUI before it can accelerate
sound authoring. Its first acceptance gate is that one BMW JSON definition compiles,
the established listening baseline is reproduced through the portable session, and
throttle/ignition/dyno interaction remains clean without underruns on the development
PC.

## 14. Implementation order

The authoritative execution sequence is [`PLAN.md`](../../PLAN.md). The native
session/bake cutover is checkpoint 8; C ABI and WASM are checkpoint 9; the
Worker/ring/AudioWorklet transport and workbench are checkpoint 10. Later control
capabilities remain fail-closed until they are individually implemented.

Each step preserves one implementation path. No temporary browser synthesizer,
pre-recorded engine loop, or compatibility parser becomes a production dependency.
