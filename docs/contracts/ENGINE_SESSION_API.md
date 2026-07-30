# Portable engine compile and session API

Status: proposed greenfield contract; not yet implemented

Applies to: JSON engine authoring, immutable engine compilation, mutable simulation
sessions, native offline rendering, WASM preview, runtime control ownership, streaming
audio buses, telemetry, and fail-closed adapter behavior

This contract defines one simulation and presentation implementation that can run
unpaced for an offline bake or incrementally for an interactive preview. It is not a
compatibility surface for `.mr`, engine-sim, the failed offline fork, or historical
milestone-specific types.

## 1. Goals and non-goals

The API must:

- compile a versioned JSON engine definition and its referenced assets into one
  immutable executable program;
- create any number of independent mutable sessions from that program;
- process bounded audio blocks without using wall time;
- accept timestamped controls with explicit RPM and load ownership;
- expose named audio buses and bounded telemetry;
- use the same physics, excitation, and presentation code in native offline and WASM
  interactive execution;
- reject unsupported configuration and ownership conflicts instead of substituting a
  fallback sound;
- permit an authoring UI to rebuild an engine without corrupting the currently
  playing session.

The first implementation does not promise:

- `.mr` parsing or syntax compatibility;
- mutation of arbitrary engine geometry while a solver is running;
- byte-identical floating-point output between native and WASM targets;
- a complete vehicle, tire, road, or transmission simulation;
- that the full simulator will always meet an audio callback deadline;
- a second reduced-fidelity realtime renderer.

## 2. One implementation, two pacing adapters

```text
engine JSON + assets
        |
        v
compile_engine()
        |
        v
immutable EngineProgram
        |
        +---------------------------+
        |                           |
        v                           v
native EngineSession          WASM EngineSession
unpaced process() loop        worker-paced process() loop
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

Offline and interactive execution differ only in pacing and publication:

- Native offline rendering calls `process()` as quickly as the machine allows and
  sends the resulting blocks to artifact encoders.
- The browser worker calls the same `process()` function only far enough ahead to
  keep a bounded playback ring supplied.
- The AudioWorklet consumes already-produced audio. It does not contain another
  engine model.

The existing whole-render boundary may become a convenience adapter over this session
API. It must not retain a separately maintained simulation or presentation path.

## 3. Contract layers

Three inputs have deliberately different lifetimes.

### 3.1 Engine definition

`EngineDefinitionDocument` is authored JSON. It contains durable engine and default
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

`SessionConfiguration` selects one execution context without changing the engine:

- one motion-ownership mode;
- audio delivery sample rate and requested bus layout;
- internal quality/resolution profile;
- ambient and initial thermal conditions;
- initial crank angle, RPM, and operating state;
- deterministic public seed;
- bounded process, event, and telemetry capacities;
- test-cell inertia, brake curve, or held-speed controller where the selected mode
  requires them;
- optional audition-only drivetrain context.

Changing the motion mode, delivery rate, quality profile, seed, bus layout, initial
state, or bounded capacities creates a new session. It is not a hot control.

### 3.3 Live controls

Live controls are typed, timestamped commands sent after session creation. They do not
modify the engine document or compiled program. Commands include, where admitted by
the selected mode:

- requested throttle;
- ignition, fuel, starter, dyno, and limiter enable state;
- externally imposed RPM;
- brake torque, dyno target, or load-controller target;
- clutch position and selected gear as test-cell or presentation context;
- route monitoring gain, route mute, impulse-response wet mix, and audition master
  gain;
- explicit startup, shutdown, shift, and reset-related events supported by the
  compiled engine.

No untyped string-to-value property mutation enters the processing path.

## 4. Lifecycle

### 4.1 Compile

Conceptually:

```cpp
CompileEngineResult compile_engine(
    std::span<const std::byte> engine_json,
    const EngineAssetProvider &assets,
    const CompileEngineOptions &options);
```

Compilation:

1. parses the declared JSON schema version;
2. validates types, units, ranges, identities, and references;
3. validates complete cylinder, intake, exhaust, ignition, and source routing;
4. resolves declared defaults and records each resolution;
5. verifies and decodes referenced assets;
6. compiles flow tables, valve/cam data, timing schedules, resampling state,
   convolution kernels, and other immutable method data;
7. calculates memory and per-block cost bounds;
8. returns either an immutable `EngineProgram` or path-bearing diagnostics.

Compilation may allocate, decode files, hash assets, and use worker threads supplied by
the adapter. It never runs on the audio rendering thread.

An `EngineProgram`:

- is immutable after successful compilation;
- owns or content-addresses every byte needed to create a session;
- is safe to share across independent sessions;
- exposes engine, route, bus, capability, and parameter descriptors;
- carries the exact schema, method, asset, and program identities required for
  manifests;
- contains no mutable process state.

### 4.2 Create

Conceptually:

```cpp
CreateSessionResult create_session(
    std::shared_ptr<const EngineProgram> program,
    const SessionConfiguration &configuration);
```

Session creation validates the selected capabilities and ownership mode, reserves all
bounded processing storage, creates per-session DSP and random state, and returns a
mutable `EngineSession`.

One session:

- belongs to one caller and is not reentrant;
- owns all mutable physics, excitation, presentation, resampler, control, and telemetry
  state;
- creates no hidden global thread pool;
- performs no file or network I/O;
- never observes wall time;
- is independent from every other session created from the same program.

### 4.3 Reset and prime

`reset(initial_state)` returns a session to a declared state and clears queued
controls, event journals, filter histories, resampler histories, and accumulated
telemetry. It does not preserve undocumented acoustic tails.

If a method needs causal preparation, the caller chooses one explicit policy:

- process and retain the preparation audio;
- process preparation and discard it before the audible frame;
- restore a validated, method-compatible state snapshot.

There is no implicit warm-up of an unspecified duration.

### 4.4 Enqueue controls

Conceptually:

```cpp
ControlResult enqueue_controls(
    EngineSession &session,
    std::span<const ControlCommand> commands);
```

Every command contains:

- an absolute delivery-frame index relative to the session origin;
- a strictly increasing caller sequence number;
- one typed control or event payload;
- an explicit interpolation or ramp policy when the value is continuous.

Delivery-frame indices, not milliseconds or wall-clock timestamps, are authoritative.
The selected numerical method defines the causal projection from a delivery frame to
its internal physics/control clock. A command that cannot be represented under that
method is rejected during enqueue or session creation.

Commands must be ordered and must not target a frame already generated by
`process()`. A late command, a full command queue, an invalid range, or a command not
owned by the selected mode is rejected. The session never silently applies it “as
soon as possible.”

### 4.5 Process

Conceptually:

```cpp
ProcessResult process(
    EngineSession &session,
    std::uint32_t delivery_frame_count,
    AudioBusBlockSet output,
    TelemetryBlock *telemetry);
```

`process()`:

- advances exactly the requested number of delivery frames or returns a typed failure;
- accepts any positive frame count up to the session's declared maximum;
- writes planar `float32` samples to every requested bus;
- advances all internal clocks by integer/rational schedule state;
- consumes commands causally over the requested half-open frame interval;
- returns bounded telemetry and event records for that same interval;
- performs no JSON parsing, asset decoding, filesystem access, network access, or
  thread creation;
- performs no unbounded allocation after session creation;
- does not select a different model because a deadline is near.

Caller block size is transport, not model resolution. Given the same program,
configuration, reset state, and command stream, splitting a horizon into different
valid `process()` calls must not change the resulting sample or telemetry sequence on
the same declared build and numeric runtime.

### 4.6 Drain and destroy

An explicit `begin_drain()` prevents new physical controls and requests any declared
presentation tail. `process()` continues until it returns `drained`. Tail length is
bounded and reported by the compiled program.

Destroying a session releases only that session's mutable state. Destroying an engine
program is legal only after all sessions and pending replacements release it.

## 5. Motion and control ownership

Throttle, load, and RPM cannot all be simultaneous authoritative commands. Every
session selects exactly one mode.

| Mode | Authoritative live inputs | Session results | Rejected conflicts |
|---|---|---|---|
| `external_speed` | RPM trajectory, throttle, operating-state events | torque, achieved load, audio | brake or dyno commands that claim RPM ownership |
| `held_speed` | held RPM or held-RPM target, throttle, operating-state events | required actuator/dyno reaction, torque, achieved load, audio | external RPM trajectory and inertial brake ownership |
| `inertial_dyno` | throttle, operating-state events, brake/dyno controls | simulated RPM trajectory, torque, achieved load, audio | externally imposed RPM |

`external_speed` is appropriate for a host game or editor scrubber that already owns
drivetrain RPM. The full simulator still calculates achieved load from its physical
state. A host-provided “load” may be retained as explicitly non-authoritative transient
or presentation context, but it cannot overwrite the simulator's achieved physical
load.

A later compiled-package audio follower may authoritatively consume RPM and a
versioned host load coordinate. That is a different capability from the full physics
session and must identify its load normalization and coast semantics.

### 5.1 Dyno controls

`held_speed` may expose a dyno-enable control and target RPM owned by its speed
controller. `inertial_dyno` may expose brake torque, a brake-curve multiplier, or a
versioned dyno controller target. The exact admitted control set is returned by the
session descriptor.

Disabling a dyno does not silently switch motion mode. Any such change requires a new
session or an explicitly modeled state transition supported by that mode.

### 5.2 Gear and clutch

Gear ratios, final drive, wheel inertia, and road load do not belong in
`EngineDefinitionDocument`.

The minimal API permits `gear_index` and `clutch_01` as context/events:

- in `external_speed`, they may drive shift sounds or presentation behavior, while the
  host remains responsible for sending the resulting RPM;
- in `held_speed` and `inertial_dyno`, they have no mechanical effect unless the
  session explicitly includes an audition test-cell drivetrain;
- selecting an unsupported gear is rejected rather than interpreted approximately.

An eventual road-audition model is a separate test-cell definition composed with the
engine program. It must declare which subsystem owns crank motion and equivalent
inertia.

## 6. Hot controls versus rebuilds

The API exposes parameter descriptors with one of three mutation classes:

| Class | Meaning |
|---|---|
| `hot` | Timestamped command may change it in an existing session. |
| `session_recreate` | Engine program is reusable, but a new session is required. |
| `program_recompile` | JSON or asset change must compile a new immutable program. |

Minimum classification:

| Field family | Class |
|---|---|
| throttle request | `hot` |
| ignition/fuel/starter/limiter enable | `hot` |
| mode-owned RPM, brake, or controller target | `hot` |
| gear/clutch context | `hot` when supported |
| audition master, route monitor gain, mute, IR wet mix | `hot`, with declared ramping |
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

An authored default can also have a hot preview override. For example, changing a
route monitoring gain is hot, while saving that value into `engine.json` produces a
new program identity on the next compile. A physical exhaust length is never
misrepresented as a gain-like hot control.

When JSON changes, the adapter compiles an inactive replacement. A successful
replacement may:

1. stop and reset at an explicit boundary; or
2. create and prime a second session, then crossfade at the delivery boundary.

State transfer between different programs is forbidden unless a versioned method
validates that the relevant state layouts and meanings are compatible. Compilation
failure leaves the currently playing program untouched.

## 7. Audio bus contract

The engine program exposes stable bus descriptors:

```text
BusDescriptor
  id
  source kind
  channel count
  delivery sample rate
  presentation disposition
  tail bound
```

Bus IDs are semantic and are not inferred from vector position. The supported source
kinds include:

- one or more exhaust outlet routes;
- one or more intake inlet routes;
- engine mechanical routes;
- starter mechanical routes;
- a raw declared master;
- an audition master.

Only routes actually implemented and admitted by the compiled program are advertised.
An absent intake or mechanical model is an absent capability, not a silent bus.

The session configuration selects an ordered subset of advertised buses. `process()`
receives one correctly sized planar buffer per selected bus and writes exactly the
requested frame range. A mismatched bus ID, channel count, sample rate, pointer range,
or capacity fails before the session advances.

The audition master is a convenience listening mix. Game hosts should normally consume
separate buses and own spatial placement, distance attenuation, occlusion,
environmental reverb, and final mix policy.

The portable processing format is finite `float32`. File encoders choose PCM depth and
container independently. NaN, infinity, or an internal amplitude-bound violation is a
typed processing failure, never silently clipped into validity.

## 8. Telemetry and capability discovery

Before session creation, the caller may query the program for:

- engine and schema identity;
- supported motion modes;
- source and audio bus descriptors;
- supported live controls and their ranges, units, interpolation, and mutation class;
- required and maximum block sizes;
- internal and admissible delivery rates;
- tail, memory, and estimated cost bounds;
- torque and load telemetry capabilities.

Each processed block may return bounded:

- delivery and physics frame ranges;
- requested and resolved throttle;
- RPM, angular speed, crank angle, and RPM slope;
- instantaneous and cycle-mean torque forms that the method supports;
- achieved signed net BMEP and mapped power where supported;
- ignition, fuel, starter, limiter, and dyno state;
- selected gear and clutch context;
- per-bus peak, RMS, and fault flags;
- typed combustion, limiter, starter, shutdown, and shift events;
- cumulative simulation fault and dropped-telemetry counts.

Telemetry availability is capability-described. A missing torque model does not return
zero torque. Event and telemetry buffers are caller-owned and bounded; overflow is
reported with the exact dropped count and does not overwrite memory.

Browser transport statistics such as ring fill, callback underruns, worker lead, and
estimated wall-clock realtime factor belong to the browser adapter. They are not
physical engine telemetry.

## 9. Deterministic offline execution

Offline rendering constructs an engine program and session, resets them once, queues
the complete scenario command stream on integer delivery frames, and calls
`process()` until the declared horizon and optional tail are complete.

Deterministic execution requires:

- immutable program and asset identities;
- complete session configuration;
- explicit seed and initial state;
- integer-frame controls with stable sequence numbers;
- versioned clock projection and interpolation methods;
- no wall-clock or audio-device input;
- no callback-size-dependent numerical method;
- a recorded build, target, numeric runtime, and method identity.

The same build, target, numeric runtime, program, configuration, and command stream
must be byte-stable. Native and WASM builds are not presumed byte-identical because
their math libraries, compiler lowering, SIMD, and runtime environments may differ.
Cross-target equivalence is accepted only under separately declared numeric and
listening tolerances.

The browser preview is not authoritative artifact evidence. A downloadable browser
WAV may be useful for iteration, but a production manifest identifies whether it came
from the native authoritative renderer or a separately admitted WASM numeric
environment.

## 10. Portable ABI

The semantic API is implemented in C++. Native embedders may use typed C++ wrappers.
WASM and other foreign runtimes use a thin versioned C ABI with opaque integer handles.
Conceptually it provides:

```text
api_version
compile_engine_json
destroy_engine_program
query_engine_program
create_session
destroy_session
reset_session
enqueue_control_batch
process_session
begin_session_drain
copy_last_diagnostics
```

ABI rules:

- no C++ exception crosses the boundary;
- every call returns an explicit status;
- diagnostics are copied into caller-owned buffers;
- JSON appears only at compile time;
- audio, controls, and telemetry use fixed-layout structs and caller-owned spans;
- sizes, alignments, endianness, enum values, and schema versions are explicit;
- handles are generation-checked so stale handles fail;
- WASM linear-memory growth is disabled while exported buffer views are active;
- the core owns no DOM, Web Audio, filesystem, URL, fetch, or JavaScript object.

The JavaScript wrapper may offer promises around compile and session creation, but
`process_session` itself is synchronous.

## 11. Browser adapter

The initial browser architecture is:

```text
main/UI thread
  - fetch JSON and assets
  - edit and validate authoring input
  - send rebuild requests and live commands
  - display telemetry and failures

dedicated Worker
  - instantiate WASM
  - compile EngineProgram
  - own EngineSession
  - render bounded chunks ahead of playback
  - write PCM and telemetry rings

AudioWorklet
  - read PCM ring
  - copy to Web Audio outputs
  - apply final click-safe mute/master/crossfade
  - report underruns
```

The full simulator does not initially run in the AudioWorklet. This avoids making a
variable-cost physics step responsible for the browser's hard audio callback deadline.
The Worker maintains a bounded lead selected by the adapter, initially expected to be
approximately 20–50 ms on the development PC. The UI displays the measured queued
control latency rather than claiming zero-latency response.

The PCM transport is a single-producer/single-consumer ring in
`SharedArrayBuffer`, with atomic read/write indices. Controls and telemetry use
separate bounded rings so UI traffic cannot corrupt PCM ownership. The AudioWorklet
never waits for the Worker.

Browser requirements:

- AudioWorklet is used only in a secure context;
- shared-memory operation requires cross-origin isolation;
- the server supplies `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` or an explicitly validated equivalent;
- the adapter verifies `crossOriginIsolated` before creating shared transport;
- audio starts only after the required user gesture;
- the implementation uses each callback's actual frame count rather than hardcoding
  128;
- the Worker resamples from the engine's admitted internal/presentation rate to the
  actual `AudioContext.sampleRate`;
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

No partial program is returned. If compilation was a live editor rebuild, the active
program and session continue unchanged.

### 12.2 Session-creation failure

Examples include unsupported motion mode, output rate, bus set, quality, initial state,
or capacity. No session is returned and the engine program remains reusable.

### 12.3 Control rejection

Examples include a past timestamp, duplicate sequence, queue overflow, out-of-range
value, unsupported event, or ownership conflict. Rejection does not modify the queued
command stream.

### 12.4 Processing failure

Examples include non-finite physical state, solver failure, internal buffer-bound
violation, invalid caller audio storage, or an invariant violation. Processing failure
is terminal for that session unless its code explicitly declares reset recovery. The
core never returns fabricated audio, stale repeated samples, a tone, or a lower-fidelity
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

### 12.6 Replacement failure

An inactive replacement program or session becomes audible only after complete
compilation, session creation, preparation, and an explicit swap command. Failure at
any earlier step leaves the current session untouched. A swap that requires reset is
labelled as a reset; it is not disguised as continuous state transfer.

## 13. Minimum HTML harness

The first useful harness contains:

- one raw JSON editor and an explicit **Apply/rebuild** action;
- path-addressed compile diagnostics;
- engine/program identity and current dirty/rebuild state;
- `external_speed`, `held_speed`, and `inertial_dyno` mode selection where admitted;
- throttle, ignition, fuel, starter, limiter, and dyno controls;
- RPM input or display according to the selected ownership mode;
- optional gear/clutch context clearly separated from engine authoring;
- per-bus mute, monitoring gain, meter, and master wet/dry controls;
- start, stop, reset, and canonical dyno-pull actions;
- simulation realtime factor, worker lead, ring fill, callback underrun, and control
  latency display;
- downloadable preview WAV rendered by the same session API in an unpaced Worker job.

The harness does not need a complete visual engine-sim GUI before it can accelerate
sound authoring. Its first acceptance gate is that one BMW JSON definition compiles,
the established listening baseline is reproduced through the portable session, and
throttle/ignition/dyno interaction remains clean without underruns on the development
PC.

## 14. Implementation order

1. Freeze the neutral JSON capability schema and path-addressed diagnostics.
2. Refactor the current accepted BMW path behind immutable `EngineProgram` and
   mutable block-streaming `EngineSession`.
3. Prove an offline render through repeated `process()` calls matches the accepted
   native listening baseline.
4. Add the versioned C ABI without browser types in the core.
5. Build the Worker, shared PCM transport, and minimal AudioWorklet consumer.
6. Add the HTML harness and explicit authoring rebuild flow.
7. Add interactive external-speed and inertial-dyno controls.
8. Measure callback underruns, worker lead, control latency, native render time, and
   concurrent throughput.

Each step preserves one implementation path. No temporary browser synthesizer,
pre-recorded engine loop, or compatibility parser becomes a production dependency.
