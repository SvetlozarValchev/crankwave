# Engine JSON capability matrix

Status: design and implementation inventory  
Pristine reference: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`  
Clean-room inventory date: 2026-07-31

## Answer

No. The clean-room core does **not** yet execute all of pristine `engine-sim`'s engine
configurability.

The repository now has a strict product JSON schema/parser and a generic immutable
compiler for the currently executable low-order slice: mechanism, gas path, fixed and
sampled/VTEC valvetrains, direct and governed throttle control, ignition, fuel, dynamic
nonempty cylinder and exhaust-route sets, one-bank inline or exactly two-bank V layouts,
presentation, scenarios, deterministic assets, and randomness. Compilation is
data-driven and has no engine-name branch. Important original capabilities such as
multiple crankshafts, other bank topologies, heterogeneous physical heads, separate
intakes, slave journals, and general multi-head/VTEC topology remain represented but
fail closed until their executor support exists. Execution-equivalent bank-local heads,
ports, standard valvetrains, and cams now normalize to the unchanged shared low-order
profile. Vehicle and transmission rig objects parse and resolve, but no drivetrain
executor consumes them.
There is no executable C++ engine factory; product engines enter only through JSON
compilation.

The JSON cutover is an authoring and packaging change. It must reproduce the accepted
renderer before it is allowed to change an audio algorithm. The implemented browser
workbench compiles those same documents through the fixed-memory WASM C ABI, streams
one selected canonical bus through a bounded PCM ring, and exports a fresh unpaced
session. It does not expand the executor's admitted engine or live-control capability.

## What was audited

The four files in the question are necessary, but they are not the complete source of
truth:

| Pristine source | What it tells us |
|---|---|
| `es/engine_sim.mr` | Public module surface and imported libraries. |
| `es/objects/objects.mr` | Public object names, authoring defaults, convenience wrappers, and object composition. |
| `es/actions/actions.mr` | Graph-building operations: add banks, crankshafts, journals, cylinders, lobes, samples, wires, and gears. |
| `scripting/src/language_rules.cpp` | Registration of private backend node types. It is a registry, not the public contract by itself. |
| `scripting/include/*_node.h` and each node's `registerInputs()` | Actual MR-to-C++ input wiring and therefore whether a declared input reaches the backend. |
| `src/*.cpp` and `include/*.h` | Runtime consumption, identity/sharing behavior, interpolation, and fields that are declared but unused. |
| `es/part-library/**` and canonical engine assets | Examples of intended composition and realistic combinations, not additional primitive capabilities. |
| GUI/application control code | Master/wet/HF/noise controls, starter, ignition, dyno, clutch, gear, and throttle that are not engine-definition fields. |

This matrix uses the pristine Ange commit above, not the neighboring fork's additions.

## Contract boundary

The transport is one engine JSON document, conventionally named `engine.json`. Its
sections have separate logical ownership:

| Package section / request | Owns | Must not own |
|---|---|---|
| Required `engine` | Physical engine identity, topology, dimensions, flow restrictions, mechanism, fuel, combustion, valvetrain, ignition, controllers, starter capability, and physical source-route geometry. | Listening level, room/exhaust IR selection, vehicle, transmission, current throttle/RPM/load, or render duration. |
| Required `presentation` | Per-cylinder and per-route audible gains, IR assets and gains, convolution wet mix, conditioning, source/stem publication, and default monitoring mix. | Physical manifold geometry or live operating state. |
| Optional `rig` | Vehicle, transmission, clutch capability, dyno/load model, ambient and thermal defaults, and optional default test procedures. | Engine internals or current control values. |
| Session/render request | Operating mode, initial state, rates/quality, seed, duration, held point or trajectory, and output selection. | Reusable engine construction. |
| Timestamped live controls | Throttle request, ignition/fuel/starter state, gear/clutch, and the mode-specific RPM/load/dyno command. | Structural edits to the compiled engine graph. |

### Operating-mode authority

RPM, load, and throttle cannot all be independent authoritative inputs:

| Mode | Host supplies | Simulator resolves |
|---|---|---|
| Free engine/vehicle | Throttle, ignition, starter, gear, clutch, optional external resisting torque | RPM and produced torque |
| Held dyno | Throttle and target RPM | Required dyno torque and engine state |
| Load-target held point | RPM and target load/BMEP | Throttle |
| External RPM follower | RPM trajectory and throttle | Cylinder state and reaction torque telemetry |

For `free_engine`, the scenario may declare nonnegative `attached_inertia` and an
`external_resisting_torque` trajectory. Both are optional and default to canonical
positive zero. The compiler derives engine baseline inertia from the engine mechanism
using the versioned cycle-mean centered slider-crank kinetic-energy method, adds only
the declared attachment, and retains the resolved total as a cycle-mean contract
reference. Free-running crank dynamics evaluates analytic configuration-dependent
mechanism inertia and its derivative, with the same attachment added to the
instantaneous value. The BMW M52B28 neutral baseline is
`0.2108686520185204 kg*m^2`, with no attached inertia or external resistance.

The current browser workbench executes all admitted authored scenario modes.
`inertial_dyno` exposes live throttle, ignition, and fuel; `free_engine` additionally
exposes limiter state and external resisting torque. Interactive held-RPM targets and
external-RPM following remain future session capabilities. Preview is still realtime
audio: the same block processor runs ahead of the audio device rather than tying
simulation steps to UI frames.

## Authoring, units, curves, and graph semantics

### Authoring and compilation

1. There is one current schema, identified by the exact root strings
   `engine-sim-offline/engine` and `engine-sim-offline/scenario`. Other identifiers fail;
   there is no numeric version field, translator, or legacy migration layer.
2. Authored JSON is data, not a programming language. It has no imports, aliases,
   expressions, executable nodes, or method chaining.
3. Reuse is represented by stable IDs and references. External tools/templates may
   generate JSON, but the runtime never evaluates an MR-like language.
4. A compiler validates schema, dimensions, reference integrity, topology, curves,
   physical bounds, and assets, then emits one immutable canonical-SI resolved model.
5. Defaults and authoring conveniences are expanded during compilation. Diagnostics
   identify both the object ID and JSON Pointer of the authored error.
6. Structural edits produce a new compiled definition and a new session. The browser
   adapter atomically retains the current program on compile/session-creation failure
   and replaces it with a fresh session after success; it does not crossfade or transfer
   physical state. Active inertial-dyno sessions expose throttle, ignition-enabled, and
   fuel-enabled controls. Active `free_engine` sessions expose those controls plus
   limiter-enabled and external-resisting-torque controls.

### Identity and graph rules

- Every bank, crankshaft, journal, piston/rod assembly, cylinder, head, camshaft,
  valvetrain, intake, exhaust route, ignition wire, curve, controller, and asset has a
  unique stable string ID within its namespace.
- References preserve authored object sharing. Six cylinders referencing one intake
  mean one shared manifold; copied intake objects remain distinct and are not currently
  executable. The shared low-order head profile is the one explicit exception: separate
  bank-local heads may normalize only after exact execution-equivalence admission;
  differing definitions fail closed instead of being silently merged.
- Ordered lists remain ordered where order affects behavior: crankshafts, cylinders,
  lobes, firing order, exhaust accumulation, and transmission gears.
- A cylinder explicitly references its bank, crankshaft/journal, piston, rod, intake,
  exhaust route, ignition wire, intake/exhaust ports, and optional slave journal.
- References must resolve, forbidden cycles are rejected, and every connected physical
  object must be reachable from the engine root.
- The authoring graph can represent broader sharing than the current executor admits.
  Current execution accepts one crankshaft, one shared intake, and either one
  zero-angle inline bank or exactly two finite, distinct-angle V banks. Banks may share
  one head/valvetrain or use execution-equivalent bank-local heads and standard
  valvetrains; single-head VTEC remains admitted. Direct journals and exhaust routes
  may be shared; identity is never inferred from array position. Other graph shapes
  fail closed.
- Compiler-assigned dense numeric IDs and deterministic reduction order are resolved
  artifacts; they are never authored API identities.

### Unit rules

- Dimensionless values may be JSON numbers. Every dimensional value is authored as
  `{"value": number, "unit": string}` and resolved to SI.
- Angles must declare `deg` or `rad`; RPM is not treated as a plain number.
- The minimum unit vocabulary covers length, area, volume, mass, inertia, torque,
  pressure, time, angular speed, vehicle speed, density, molar mass, energy per mass,
  and frequency.
- Flow-bench ratings are not generic SI flow. They retain their calibration basis, for
  example
  `{"value": 200, "unit": "cfm", "standard": "carburetor_1p5_inhg"}`
  or `standard: "port_28_inh2o"`, and compile to the corresponding restriction.
- Fractions use an explicit `_01` semantic name and must lie in `[0, 1]`. Linear gains
  are not decibels; any dB authoring convenience must be converted explicitly.
- Unit conversion happens once in the compiler. The simulation, WASM boundary, hashes,
  and resolved diagnostics use canonical SI.

### Curve rules

Curves are first-class objects with:

- stable ID;
- declared input and output dimensions;
- ordered, finite samples;
- an explicit evaluation method;
- method parameters such as triangle filter radius;
- explicit below-domain and above-domain behavior.

The original parity evaluator is `triangle_weighted_samples`: samples within the
declared radius are triangle-weighted and normalized, and values outside the sample
domain clamp to the first/last sample. Duplicate sample abscissas are rejected at the
new authoring boundary rather than preserving ambiguous insertion behavior.

Flow tables, ignition timing, fuel turbulence/flame-speed response, and authored
sampled cam lobes use curve references. The fixed-cam compiler admits both
`harmonic_cam_lobe`, which it expands to an ordinary sampled table, and
`sampled_cam_lobe`, which executes the referenced angle-to-lift samples directly. The
generator is not a separate runtime graph language.

## Status legend

The status labels below describe executable capability, not whether the strict JSON
transport can spell a field. The compiler accepts only the supported subset and reports
the remainder as explicit capability diagnostics:

| Status | Meaning |
|---|---|
| **Typed core** | A reusable typed seam and runtime support exist, but no public JSON authoring path exists. |
| **Low-order executed** | The behavior executes in the current low-order model, sometimes within a deliberately narrow admitted topology. |
| **Partial** | Some representation or execution exists, but not the pristine general capability. |
| **Missing** | No general representation/execution exists yet. |
| **Excluded** | Deliberately not part of the new product contract. |

## Engine definition capability matrix

### Root, controller, and engine-wide physical configuration

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Engine name and reusable parameter preset | `engine.identity` and authoring templates | **Typed core** with strict JSON compilation; reusable template tooling is later | BMW compiles to the frozen execution values without changing audio. |
| Redline | `engine.limits.redline` | **Low-order executed** in ignition profile | Limiter boundary and declared redline remain distinct and validated. |
| Starter speed and torque | `engine.starter` with `type: "cranking"`; `mechanically_disengaged` explicitly declares no executable cranking capability | **Low-order executed** in `free_engine` with the pristine unilateral target-speed torque constraint; held and dyno modes require the starter disengaged | BMW crank/catch fixture demonstrates torque, target speed, engagement, and caller-owned release. |
| Direct throttle linkage gamma | `engine.throttle_controllers[]` with `type: "direct"` and `engine.throttle_controller` selecting one | **Low-order executed**; the selected controller's finite positive gamma reaches the command-to-plate mapping | A sweep proves the authored command-to-plate curve. |
| Governor (`min_speed`, `max_speed`, signed `min_v`/`max_v`, `k_s`, `k_d`, `gamma`) | `engine.throttle_controllers[]` with `type: "governor"`; velocity bounds are not throttle-output fractions | **Low-order executed** with pristine's persistent explicit-Euler update; public normalized throttle demand selects the target between minimum and maximum speed, and telemetry retains requested and resolved throttle separately | Kohler CH750 V-twin settles at the requested 2,740 RPM before a deterministic 12 N m load/unload step. |
| Chen--Flynn cycle-mean loss coefficients and required oil temperature (clean-room extension, not a pristine capability) | `engine.losses` with `type: "chen_flynn_cycle_mean"` | **Low-order executed** in operating-point accounting and as one-cycle-lagged inertial-dyno resistance; `free_engine` retains the cycle-mean evidence but does not apply it to crank motion | Generic compilation selects the registered method and derives torque-term accounting without an engine-name branch; do not call it pristine parity. |
| Accessory-configuration URI and content hash | `engine.accessory_configurations[]` referenced by the selected loss model | **Metadata/evidence only**; current execution records identity and digest but does not interpret payload bytes | Do not expose it as a power/audio tuning surface until a typed accessory-load schema is executed. |
| Dyno min/max/hold step defaults | `rig.dyno_defaults`, not engine physics | **Partial**; package JSON parses and resolves the defaults with provenance, but they do not schedule a run or drive a UI sequence | Native and browser UI generate the same held-point sequence. |
| Simulation frequency | Session/render quality | **Typed core** through render rates | Identical resolved rates reach native offline and WASM sessions. |

### Cranktrain, banks, and cylinders

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Multiple crankshafts attached to one engine | `engine.crankshafts[]` plus references | **Missing**; executable profile has one crank assembly | Two-crank structural fixture compiles and steps without identity collapse. |
| Crank throw, crank/flywheel mass, inertia, friction torque, TDC reference | Crankshaft physical fields | **Low-order executed/Partial**; positive-speed FreeEngine applies the authored crank-friction magnitude as the pristine saturated rotation constraint and the source one-step-lagged piston-wall law through centered inverse dynamics | BMW resolved-value comparison, direct pristine wall-reaction trace comparison, and WOT/coast response check. |
| Rod journals with arbitrary phase | `engine.journals[]` | **Low-order executed/Partial**; a dynamic cylinder set may reference and share declared direct journals with arbitrary finite phase, while master/slave journals remain unsupported | V8 and direct shared-journal fixtures preserve phases and reference identity. |
| Slave journals and rod `slave_throw` | Journal/rod references and physical fields | **Missing** | Master/slave V-twin geometry and uneven firing fixture. |
| Connecting-rod mass, inertia, center of mass, length | `engine.connecting_rods[]` | **Partial**; mass/inertia/length exist in BMW profile, center of mass is absent | Resolved mechanism quantities and inertial torque are verified. |
| Piston mass, blowby, compression height, wrist-pin position, displacement term | `engine.pistons[]` and gas-path blowby restriction | **Partial**; BMW has mass, blowby, compression height, displacement term; wrist-pin position is absent | BMW geometry/clearance comparison and blowby flow check. |
| Banks with angle, bore, and deck height | `engine.banks[]` | **Low-order executed/Partial**; execution admits one zero-angle inline bank or exactly two finite, distinct-angle V banks, including authored bank bore/deck geometry; opposed and other arbitrary layouts remain unsupported | Inline, V8, and direct-journal V-twin fixtures preserve bank geometry and axis-relative mechanics. |
| Arbitrary cylinder-to-bank/crank/journal/intake/exhaust/wire connections | Explicit cylinder references | **Partial**; dynamic cylinders, two-bank V bindings, direct shared journals, distinct wires, firing order, and exhaust sharing execute, while the profile still requires one crankshaft and one shared intake; bank-local heads/standard valvetrains must be execution-equivalent | Toyota and direct-journal V-twin fixtures prove the admitted non-inline connection patterns. |
| Per-cylinder primary length | Physical exhaust path in `engine`, compiled to propagation delay | **Low-order executed** | Resolved length/delay comparison at each supported sample rate. |

### Gas exchange, manifolds, heads, and exhaust

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Multiple, shareable intake objects | `engine.intakes[]` referenced by cylinders | **Partial**; graph supports IDs but executable profiles require exactly one shared intake | Shared and split-intake fixtures retain distinct states. |
| Intake plenum volume/area and runner length | Intake physical fields | **Low-order executed** | BMW resolved-value and held-point regression. |
| Main, idle-bypass, and runner restrictions | Calibrated intake restrictions | **Low-order executed** | Both flow-bench calibration standards resolve deterministically. |
| Idle throttle plate position | Intake physical/control field | **Low-order executed** | Closed-command idle flow remains nonzero and bounded. |
| Intake molecular AFR | Fuel/mixture ownership, referenced by intake if model requires it | **Partial**; scenario/fuel owns stoichiometric AFR | Compiler rejects conflicting duplicated AFR authority. |
| Intake runner velocity decay | Intake gas-exchange method parameter | **Low-order executed** | BMW gas-state regression. |
| Shareable heads per bank | `engine.heads[]` and bank references | **Partial/Low-order executed** for one shared head or execution-equivalent bank-local heads with standard valvetrains; heterogeneous heads and multi-head VTEC fail closed | Shared-head and split-head V fixtures resolve to the exact same executable contract and PCM; Kohler retains its two source heads. |
| Chamber and intake/exhaust runner volume/area | Head physical fields | **Low-order executed**; bank-local values must be bit-identical while the runtime retains one shared coefficient profile | BMW clearance/gas-volume comparison and split-head rejection fixtures. |
| Arbitrary intake/exhaust port-flow curves | Head curve references | **Typed core/Low-order executed** | Curves preserve units, radius, clamping, and sampled values. |
| Multiple, shareable exhaust systems | `engine.exhausts[]` and cylinder route references | **Low-order executed** for a dynamic nonempty exhaust set; cylinders may share routes and copied exhaust objects remain independent | Inline-six, V8, and V-twin grouping fixtures prove sharing and independent collectors. |
| Collector length/area, outlet restriction, primary length/restriction, velocity decay | Exhaust physical fields | **Low-order executed** | BMW resolved graph and gas-state regression. |
| `exhaust.collector_volume` convenience | Compiler authoring convenience deriving `collector_length = collector_volume / collector_area` | **Low-order executed**; exactly one of explicit collector length or collector volume is required | Explicit length and derived length compile to the same resolved model. |
| Executable gas volumes and flow edges | Compiler-derived from authored intakes, heads/ports, cylinders, exhausts, and their stable references | **Low-order executed/Partial** within the admitted one-crank, one-intake, shared-or-equivalent-head, inline/two-bank-V slice | Authors state each physical fact once; the compiler deterministically constructs and validates the supported internal graph. |

### Camshafts and valvetrain

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Cam advance, base radius, sampled lobe profile | `engine.camshafts[]` and angle-to-length `engine.curves[]` referenced by `sampled_cam_lobe` | **Low-order executed** for standard fixed valvetrains; sampled profiles retain source triangle weighting, clamping, advance, and crank phasing | Sampled and generated-equivalent lobes resolve and render byte-identically. |
| Arbitrary lobe centerlines added to a cam | Ordered cam lobe references by cylinder/port and centerline | **Low-order executed/Partial** | Firing-independent lobe ordering survives compilation. |
| Harmonic lobe generator parameters | Compiler-side curve generator | **Low-order executed** internally | Generator golden samples match the accepted BMW profile. |
| Standard intake/exhaust cam valvetrain | `engine.valvetrains[]`, `type: "standard"` | **Low-order executed** for shared or bank-local standard valvetrains; same-role bank-local cams require exact advance, base radius, and lobe shape while retaining per-cylinder centerlines | BMW valve-lift traces remain unchanged; shared/split V forms produce byte-identical PCM. |
| VTEC base and alternate intake/exhaust cams | `engine.valvetrains[]`, `type: "vtec"` | **Low-order executed**; the base and alternate pairs compile independently through the same fixed-cam sampler, and one coherent pair serves both ports for every cylinder in a gas frame | Honda fixture selects all four authored cams correctly while fixed-cam engines remain unchanged. |
| VTEC RPM, absolute manifold-pressure, and resolved throttle-linkage-opening thresholds | `activation.minimum_engine_speed`, `activation.minimum_manifold_pressure_abs`, and `activation.minimum_throttle_linkage_opening_01` | **Low-order executed** with pristine's strict, stateless three-predicate selector; pristine's stored-but-unused `min_speed` is deliberately absent from the greenfield JSON contract | Honda transition matrix covers all three executed predicates at equality and on either side; vehicle speed cannot affect selection because no dead compatibility field exists. |

### Ignition and fuel

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Timing curve and filter radius | Ignition curve reference | **Low-order executed** with typed sampled curve | BMW timing values and evaluator behavior match. |
| Rev limiter speed and cut duration | `engine.ignition.limiter` | **Low-order executed** | Cut/re-enable event timing at the boundary is deterministic. |
| Ignition wires and arbitrary firing angles/order | Explicit wire objects, cylinder references, and ordered firing map | **Low-order executed**; a dynamic cylinder set uses distinct cylinder-owned wires and a generic firing map that covers every wire exactly once | Toyota and V-twin firing sequences compile and render correctly. |
| Fuel name/ID, molecular mass, energy density, molecular AFR | `engine.fuels[]` | **Low-order executed** | Gasoline fixture resolves every physical field once. |
| Fuel density | Fuel physical field | **Missing** | Unit conversion and resolved-value fixture. |
| Turbulence-to-flame-speed curve and radius | Fuel curve reference | **Low-order executed** with typed curve | Original curve evaluation fixtures pass. |
| Maximum efficiency, randomness, low-efficiency attenuation, turbulence/dilution limits | Fuel/combustion fields | **Low-order executed** | Seeded BMW combustion regression and bound validation. |
| Per-cylinder deterministic random streams | Resolved compiler/session detail derived from public seed and IDs | **Typed core** | Reordering unrelated JSON objects does not change a cylinder's stream. |

## Presentation capability matrix

These fields affect what is heard and must not be hidden inside physical manifold
objects merely because pristine MR placed some of them there.

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Per-cylinder `sound_attenuation` | `presentation.cylinder_routes[]` gain | **Low-order executed** in excitation paths | Equal/inherited gain A/B remains an explicit authoring choice. |
| Per-exhaust `audio_volume` | `presentation.routes[]` source gain | **Low-order executed** in excitation route | Route solo and full mix prove exact routing/gain. |
| Exhaust/primary length attenuation and delay | Physical length in engine; resolved propagation in presentation/excitation | **Typed core/Low-order executed** | Delay and inverse-length behavior are visible in resolved diagnostics. |
| IR filename/asset and IR volume | Content-addressed presentation asset and route gain | **Low-order executed** as static verified-asset conversion and per-route convolution | Decode/resample/hash and route convolution fixtures. |
| Convolution wet level | Presentation default; live-safe override | **Low-order executed** as a static per-route wet mix; no live override exists yet | Dry, wet, and mixed route auditions. |
| Engine `hf_gain` | Presentation derivative/HF conditioning default | **Low-order executed** as static derivative conditioning | Zero and accepted BMW settings A/B without changing physics. |
| Engine `jitter` | Presentation conditioning default plus deterministic seed | **Low-order executed** as static deterministic route conditioning | Same seed is reproducible; zero removes modulation. |
| Engine `noise` and GUI air-noise mix | Presentation conditioning default/live-safe override | **Low-order executed** as static deterministic filtered-air conditioning; no live override exists yet | Noise solo and zero-noise regression. |
| GUI master volume | Session monitor/output gain | **Partial**; audition monitoring gain exists | It never changes physical stems or canonical raw capture. |
| One channel per unique exhaust object | Compiler derives source routes from explicit shared exhaust identity | **Low-order executed** for the dynamic exhaust-route set | Shared object produces one route; copied objects remain separate. |
| Excitation pressure combination, scale, low-speed ramp, propagation constant, delay rate, and accumulation policy | Selected excitation-method configuration, generated by the compiler; physical lengths and audible gains remain authored | **Low-order executed** constants behind a typed core | Generic method selection reproduces the accepted values without exposing implementation calibration or branching on engine identity. |

Pristine `engine-sim` does not expose a separately audible intake bus. The clean-room
production source plan's intake, mechanical-engine, and mechanical-starter routes are
intentional future extensions, not missing pristine parity fields. They remain separate
route kinds so they can be added without replacing the accepted exhaust renderer.
The executed IR, wet, HF/derivative, jitter, and noise settings above are fixed when a
session is built; they do not imply a timestamped presentation-mutation API.

## Rig, session, and live-control capability matrix

The current executable session quantum is exact: 200 physics frames at 10 kHz become
3,840 delivery frames at 192 kHz, or 20 ms per `process_block()` call.
`quality.process_block_capacity_frames` is delivery-frame capacity and must be at least
3,840; a larger value does not change the current quantum.
`quality.event_queue_capacity` bounds caller-authored timestamped control commands, not
the internal simulation event journal. `quality.telemetry_capacity_frames` bounds
records returned per call; the current session emits one final-engine-sample telemetry
record per block.

| Pristine capability | Intended owner | Current status | Acceptance requirement |
|---|---|---|---|
| Vehicle mass, drag coefficient, frontal area, differential ratio, tire radius, rolling-resistance force | Package `rig.vehicle` | **Partial**; strict JSON parses and resolves the data, IDs, units, and provenance, but no vehicle dynamics consume it | BMW fifth-gear coast/load fixture. |
| Dyno minimum/maximum speed and hold-step defaults | Package `rig.dyno_defaults`; scenarios remain authoritative for an actual run | **Partial**; strict JSON parses and resolves reusable defaults, but no UI/default procedure executes them | Native and browser tools generate the same explicit held-point requests. |
| Transmission max clutch torque and ordered gear ratios | Package `rig.transmission` | **Partial**; strict JSON parses and resolves clutch torque and authored gear order, but no transmission/drivetrain executes them | Gear order, clutch slip, and shaft-speed fixture. |
| Held speed and prescribed RPM sweep | Session/render request | **Partial**; constant held-speed execution exists, while prescribed/external RPM following remains unavailable | Existing BMW held points and a future dyno listening pull. |
| Load-target held capture | Session/render request | **Low-order executed** | Converged target/tolerance result. |
| Inertial dyno with inertia and brake curve | Session/render request | **Low-order executed** | Existing BMW inertial pull and torque evidence. |
| Ambient pressure/temperature, initial gas/wall state, crankcase, fuel, seed, render rates, preparation | Session/render request and rig defaults | **Typed core and executed** | Native and WASM resolve the same request identity. |
| Relative humidity, coolant temperature, and oil temperature | Session/render request metadata/applicability conditions | **Admission/evidence only** in the current low-order executor; oil temperature must match the loss-profile condition | Do not present these as live sound or power controls until an implemented subsystem consumes them. |
| Quality telemetry capacity | Session output allocation policy | **Low-order executed** as returned-record capacity; each exact block currently returns one final engine-capture record, while authored telemetry-channel selection still fails closed | Browser transport preserves the same record boundary without confusing it with PCM or the internal event journal. |
| Ignition, fuel, starter, dyno/limiter enable events | Timestamped session controls | **Partial**; ignition and fuel execute live in inertial-dyno and `free_engine`, while limiter and a capability-gated momentary starter execute live in `free_engine`; dyno enable remains unavailable as a public live control | Block-boundary and in-block control timing fixtures; unsupported mode/control combinations remain rejected. |
| Realtime throttle | Timestamped live controls | **Partial**; public absolute-delivery-frame throttle executes in inertial-dyno and `free_engine` sessions after preparation, and the browser Worker exposes that transport | Audible throttle response without restart in both admitted live modes. |
| Realtime external resisting torque | Timestamped live controls | **Low-order executed** for `free_engine`, including the public session API and browser Worker; other modes reject it | An in-block load step changes FreeEngine acceleration and telemetry without restarting the session. |
| Realtime RPM/load mode command | Mode-specific timestamped control | **Missing** as a public live API | RPM follower and held-dyno browser fixtures. |
| Gear and clutch controls | Timestamped live controls | **Missing** with vehicle/transmission | Vehicle fixture shifts under load. |
| Realtime-safe presentation knobs | Timestamped parameter controls | **Missing** as public API despite typed defaults | Click-free gain/wet/HF/noise changes. |
| Master/stems/telemetry block output | Session block result | **Low-order executed** as public borrowed route dry/IR/selected buses, raw/audition masters, and one telemetry record per exact 3,840-frame block; the browser Worker publishes one selected bus through the shared PCM ring and posts telemetry, while authored bus/telemetry subset selection and simultaneous browser stem publication remain absent | Route selection reaches the exact named core bus; 128-frame AudioWorklet pulls never change the 3,840-frame engine-session quantum. |

## Deliberate exclusions

| Excluded pristine/fork surface | Reason |
|---|---|
| MR modules, imports, nodes, aliases, expression evaluation, chaining, constants, and general arithmetic | The new API is declarative JSON, not another programming language. |
| Private `__engine_sim__*` names and channel wrappers | Backend ABI detail, not user capability. |
| Crank/bank `position_x`, `position_y`, `display_depth`, and head `flip_display` | Original GUI visualization only. A future editor owns its own layout metadata. |
| Application themes, colors, fonts, and settings | UI product state, not engine data. |
| `piston_parameters.wrist_pin_location` | Dead duplicate; `wrist_pin_position` is the live pristine input. |
| `cylinder_friction_parameters` nodes | Declared but not connected to the pristine cylinder backend. |
| `intake.throttle_gamma` | Deprecated/unused intake field; direct throttle linkage gamma is the supported controller capability. |
| Fork-only diesel/CI fuel fields, including auto-ignition, premixed/diffusion multipliers, and `lbv_multiplier` | Not present in pristine Ange `engine-sim`; they require a separately designed combustion model rather than accidental compatibility. |
| Named Chevy/other part-library presets and the bundled IR catalog as schema capabilities | They are assets/templates. They may be converted and distributed separately. |
| Accidental implementation/channel behavior | Bugs are not contract. Parity claims cover intended audible behavior and explicit accepted fixtures. |

The current internal parity profile still contains at least one fork-derived fuel member;
that does not admit it to the public JSON contract. It should disappear when the generic
fuel contract replaces the current low-order spark-ignition method family.

## Required acceptance fixtures

Full original configurability is not claimed until these small, capability-focused
fixtures pass. They avoid a large test matrix while covering the graph shapes that the
BMW inline-six cannot.

During cutover checkpoints 1-10, only compiler/graph and deterministic behavior checks
are blocking. The listening column is a post-cutover gate when the user is available;
it does not block checkpoints 1-10.

| Fixture | Capabilities it must prove | Post-cutover listening |
|---|---|---|
| BMW M52B28 clean-room baseline | Inline bank, six cylinders, shared intake/head, two shared exhaust routes, flow/timing/flame curves, fixed valvetrain, gains, IRs, held points, and dyno pull | Compare JSON-compiled held and dyno clips with the accepted C++-profile baseline. |
| Toyota 3UR-FE V8 | Two authored bank angles, direct shared journals, generic firing order, and grouped/shared exhaust systems | Route solos plus full dyno pull; cadence and grouping accepted by ear. |
| Honda B18C5 | Standard/alternate cams and the executed pristine RPM, manifold-pressure, and resolved linkage-opening predicates; the dead source `min_speed` input is intentionally absent | Below, transition, and above-VTEC clips; no unrelated renderer change. |
| Master/slave-journal V-twin | Shared crank journal, slave journal/throw, bank angles, unequal firing intervals | Low/high held points and short climb demonstrate correct cadence. |
| Governed small-engine V-twin | Governor parameters, starter/crank/catch, idle restriction, bank-local heads/ports/cams, and load response | Start, governed hold, and load-step clip. |
| BMW fifth-gear rig | Vehicle, transmission, clutch, differential, tire, drag, and rolling load | Realtime/offline fifth-gear climb agrees in RPM/torque trajectory and sound. |

Cutover checkpoints 1-10 use two narrow checks:

1. compiler check: JSON resolves to the expected IDs, SI values, references, and curve
   samples;
2. behavior check: a short deterministic trace covers the capability being added and
   preserves the accepted renderer path.

After cutover, each sound-affecting fixture adds the bounded listening check shown above.

Schema/compiler work may not replace excitation, conditioning, convolution, or source
models. Any later fidelity experiment is isolated behind the accepted baseline, changes
one audible system at a time, and is retained only after an A/B listening stop.
