# Engine JSON capability matrix

Status: headless executable parity accepted for the explicitly admitted scope;
wider authoring/application inventory remains intentionally partial

Pristine reference: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`  
Clean-room inventory date: 2026-08-04

## Answer

Two answers are required. **Headless executable parity is complete for the explicitly
admitted, source-demonstrated topology and operating-capability scope**, with the
seven-procedure Slice 16 listening gate accepted on 2026-08-03. **Full pristine
authoring/application-surface parity is neither complete nor the product target**:
`.mr` and GUI behavior, fuel-consumption display, unproven mechanism combinations, and
remaining post-parity fidelity extensions are separately gated. The accepted 20 kHz
rate cutover is the sole production/cooker path. Unsupported
configurations remain fail-closed.

The repository now has a strict product JSON schema/parser and a generic immutable
compiler for the currently executable low-order slice: mechanism, gas path, fixed and
sampled/VTEC valvetrains, direct and governed throttle control, ignition, fuel, dynamic
nonempty cylinder and exhaust-route sets, and direct centered rods on inline, V,
opposed, or custom explicit bank axes, plus presentation, scenarios, deterministic
assets, and randomness. Compilation is data-driven and has no engine-name branch.
Co-centered, co-phased multiple crankshafts now execute as one rigid dynamic group;
independent, offset, geared, compliant, or backlash-coupled crankshafts and nested or
multi-crank master/slave topology remain unexecuted. One-level master/slave journals
have a strict validated graph contract and execute under finite prescribed motion and
their separately identified articulated `FreeEngine`, `HeldDyno`, and `FreeVehicle`
paths.
Bank-local heads and ports resolve into explicit ordered per-bank chamber/runner/flow
profiles, with cylinders bound by BankId. Their chamber volume, runner geometry, flow
curves, and intake/exhaust flow radii may differ. Standard bank-local camshafts may
likewise retain distinct same-role profiles, advance, and base radius; each physical
camshaft still requires one exact shared profile, advance, and base radius across its
own lobes. Each referenced piston retains an independently resolved and executed
28-inH2O calibrated CFM blowby restriction. Vehicle and transmission rig
objects parse, resolve, and execute through the finite forward-only FreeVehicle
drivetrain; its operating state is published through the same native/C/WASM/Worker
boundary.
There is no executable C++ engine factory; product engines enter only through JSON
compilation.

The JSON cutover is an authoring and packaging change. It must reproduce the accepted
renderer before it is allowed to change an audio algorithm. The implemented browser
workbench compiles those same documents through the fixed-memory WASM C ABI, streams
one selected canonical bus through a bounded PCM ring, and exports a fresh unpaced
session. Its adapter exposes only capabilities reported by the compiled session; it
does not invent an additional JavaScript executor.

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
| Timestamped live controls | Throttle request; ignition, fuel, starter, and limiter state; FreeEngine external resistance; HeldDyno target RPM and absorbing/driving limits; and FreeVehicle gear, clutch, and capability-gated service brake. | Structural edits to the compiled engine graph or a motion-owner transition. |

### Operating-mode authority

RPM, load, and throttle cannot all be independent authoritative inputs:

| Mode | Host supplies | Simulator resolves |
|---|---|---|
| `held_speed` | Authored fixed RPM and throttle | Engine state and reaction-torque evidence |
| `prescribed_kinematic_sweep` | Authored RPM trajectory and throttle | Cylinder state, audio, and complete instantaneous indicated-gas torque; shaft/reaction torque, power, work, and cycle evidence remain explicitly unavailable |
| `held_dyno` | Throttle, ignition/fuel state, positive target RPM, and nonnegative absorbing/driving limits | Achieved RPM, required/applied actuator torque, dyno reaction, and limit disposition |
| `load_target_held_capture` | Fixed RPM and target load/BMEP | Converged throttle and operating-point evidence |
| `inertial_dyno` | Throttle and ignition/fuel state around the compiled passive brake/inertia protocol | Crank RPM and target-crossing evidence |
| `free_engine` | Throttle, ignition/fuel/limiter state, capability-gated starter, and external resisting torque | Crank RPM, torque, stall, and limiter-cut state |
| `free_vehicle` | Throttle, ignition/fuel/limiter state, capability-gated starter, gear, clutch, and capability-gated service brake | Crank RPM, clutch torque/slip, vehicle speed/distance, and road load |

For `free_engine`, the scenario may declare nonnegative `attached_inertia` and an
`external_resisting_torque` trajectory. Both are optional and default to canonical
positive zero. The compiler derives engine baseline inertia from the engine mechanism
using the mechanism-family versioned cycle-mean kinetic-energy method—centered slider
crank for direct journals or articulated one-level master rod for that admitted
topology—adds only the declared attachment, and retains the resolved total as a
cycle-mean contract reference. Free-running crank dynamics evaluates the matching
analytic configuration-dependent mechanism inertia and its derivative, with the same
attachment added to the instantaneous value. The BMW M52B28 neutral baseline is
`0.2108686520185204 kg*m^2`, with no attached inertia or external resistance.

The current browser runtime executes all admitted authored scenario modes. Its v3
Worker protocol exposes the same twelve capability-gated controls as the native
session and submits each nonempty control group as one atomic batch. `inertial_dyno`
exposes live throttle, ignition, and fuel; `free_engine` additionally exposes limiter,
external resistance, and a capability-gated starter. `held_dyno` exposes throttle,
ignition, fuel, target RPM, and both torque limits. `free_vehicle` exposes throttle,
ignition, fuel, limiter, ordered gear, and clutch, plus starter and service brake only
when their hardware exists. Session lifetime is selected explicitly rather than
inferred from those modes: FreeEngine, HeldDyno, and FreeVehicle admit open-ended
interactive sessions while fresh finite sessions remain authoritative for capture.
Live prescribed/external-RPM following and motion-mode transitions remain future
capabilities. Preview is still realtime audio: the same block processor runs ahead of
the audio device rather than tying simulation steps to UI frames. The workbench now
renders descriptor-gated dyno/drivetrain widgets, returned mode telemetry, and grouped
named procedures. A dynamic mode selected as an interactive bench uses continuous
start/stop/restart; finite procedures use run/pause/fresh-replay semantics and are
never silently looped.

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
  mean one shared manifold; copied intake objects remain distinct executable manifolds
  with independent plenum state. Separate bank-local heads materialize as separate
  executable profiles; their chamber, runner, and flow values remain distinct and each
  cylinder selects one through its BankId.
- Ordered lists remain ordered where order affects behavior: crankshafts, cylinders,
  lobes, firing order, exhaust accumulation, and transmission gears. Intake execution
  instead uses canonical semantic-ID order, independent of authored array order.
- A cylinder explicitly references its bank, one journal, piston, rod, intake, exhaust
  route, ignition wire, and intake/exhaust ports. A direct journal uses the required
  `type: "crankshaft"` variant and owns the crankshaft reference, so the cylinder's
  crankshaft is derived through exactly one path. A `type: "master_rod"` journal owns
  `master_cylinder`, positive `throw_radius`, and local `phase`; its crankshaft derives
  through that master cylinder's direct journal.
- References must resolve, forbidden cycles are rejected, and every connected physical
  object must be reachable from the engine root.
- The authoring graph can represent broader sharing than the current executor admits.
  Current compilation carries authored-order crankshaft identities, an explicit
  output-crankshaft selection, and exact cylinder bindings. Finite prescribed motion
  admits a co-phased group of one or more crankshafts with direct journals. FreeEngine,
  HeldDyno, and FreeVehicle additionally execute that co-centered, co-phased group as
  one rigid dynamic degree of freedom; HeldSpeed, LoadTargetHeld, and InertialDyno
  still require exactly one crankshaft. Execution accepts one or
  more reachable shared or distinct intakes, an independent calibrated blowby
  restriction for each referenced piston, and direct centered rods on one zero-angle
  inline bank, exactly two
  finite distinct-angle V banks, exactly two antipodal opposed banks, or one or more
  custom banks with explicit finite axes. Banks
  may share one head/valvetrain or use physically distinct bank-local heads and
  standard valvetrains with distinct same-role physical cam profiles; single-head
  VTEC remains admitted. Direct journals and exhaust
  routes may be shared. A master-rod journal must have exactly one cylinder consumer,
  attach to a cylinder on a direct journal, and remain one level deep. Certified
  one-level master-rod geometry executes under prescribed motion and its separately
  gated articulated `FreeEngine`, `HeldDyno`, and `FreeVehicle` paths. Nested
  attachments and offset, geared, or otherwise independent multi-crank master/slave
  mechanisms remain closed. Identity is never inferred from array position. Other
  graph shapes fail closed.
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
| Starter speed and torque | `engine.starter` with `type: "cranking"`; `mechanically_disengaged` explicitly declares no executable cranking capability | **Low-order executed** in `free_engine` and `free_vehicle` with the pristine unilateral target-speed torque constraint; held-speed and dyno-owned modes require the starter disengaged | BMW crank/catch fixture demonstrates torque, target speed, engagement, caller-owned release, and capability-gated publication. |
| Direct throttle linkage gamma | `engine.throttle_controllers[]` with `type: "direct"` and `engine.throttle_controller` selecting one | **Low-order executed**; the selected controller's finite positive gamma reaches the command-to-plate mapping | A sweep proves the authored command-to-plate curve. |
| Governor (`min_speed`, `max_speed`, signed `min_v`/`max_v`, `k_s`, `k_d`, `gamma`) | `engine.throttle_controllers[]` with `type: "governor"`; velocity bounds are not throttle-output fractions | **Low-order executed** with pristine's persistent explicit-Euler update; public normalized throttle demand selects the target between minimum and maximum speed, and telemetry retains requested and resolved throttle separately | Kohler CH750 V-twin settles at the requested 2,740 RPM before a deterministic 12 N m load/unload step. |
| Chen--Flynn cycle-mean loss coefficients and required oil temperature (clean-room extension, not a pristine capability) | `engine.losses` with `type: "chen_flynn_cycle_mean"` | **Low-order executed** in operating-point accounting and as one-cycle-lagged inertial-dyno resistance; dynamic FreeEngine/HeldDyno/FreeVehicle use it only to certify fixed warm preparation, discard the accountant at release, and publish released cycle integration as unavailable | Generic compilation selects the registered method and derives torque-term accounting without an engine-name branch; do not call it pristine parity. |
| Accessory-configuration URI and content hash | `engine.accessory_configurations[]` referenced by the selected loss model | **Metadata/evidence only**; current execution records identity and digest but does not interpret payload bytes | Do not expose it as a power/audio tuning surface until a typed accessory-load schema is executed. |
| Dyno min/max/hold step defaults | `rig.dyno_defaults`, not engine physics | **Partial**; package JSON parses and resolves the defaults with provenance, but they do not schedule a run or drive a UI sequence | Native and browser UI generate the same held-point sequence. |
| Simulation frequency | Session/render request; `rates.physics` owns the outer solver cadence and `rates.capture` owns observation cadence | **Low-order executed headlessly**; mechanics and gas derive their step from the scenario physics rate, low-order capture requires the same rate, excitation consumes that capture clock, and propagation delay is resolved against it. The accepted production/cooker path is 20 kHz physics/capture to 192 kHz source processing, acoustics, and delivery | Historical 10 kHz parity fixtures remain frozen evidence; the accepted 20 kHz BMW control/candidate gate and exact repeat establish the single canonical production rate. |

### Cranktrain, banks, and cylinders

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Multiple crankshafts attached to one engine | Authored-order `engine.crankshafts[]`, required `engine.output_crankshaft`, and journal-owned references | **Low-order executed** for finite prescribed direct-journal motion and for co-centered, co-phased rigid-group FreeEngine, HeldDyno, and FreeVehicle dynamics. Public and resolved contracts preserve stable crank identities, explicit output selection, and cylinder bindings. HeldSpeed, LoadTargetHeld, InertialDyno, unequal TDC references, and multi-crank master/slave mechanisms remain closed | The split-crank V-twin one-crank/two-crank fixtures retain distinct request identities but produce exact telemetry and PCM when aggregate inertia/friction and geometry match. Secondary-only inertia and friction mutations each change dynamic motion in the expected direction. |
| Crank throw, crank/flywheel mass, inertia, friction torque, TDC reference | Crankshaft physical fields | **Low-order executed/Partial**; positive-speed dynamics apply the authored running friction as the pristine saturated rotation constraint and the source one-step-lagged piston-wall law through centered inverse dynamics. A rigid group sums authored rotational inertia and friction in authored order; it does not derive rotational inertia from crank/flywheel mass | BMW resolved-value comparison, direct pristine wall-reaction trace comparison, WOT/coast response check, and split-crank aggregate/mutation fixture. |
| Rod journals with arbitrary phase | `engine.journals[]` tagged union; the direct variant requires `type: "crankshaft"`, `crankshaft`, and `phase` | **Low-order executed/Partial**; a dynamic cylinder set may reference and share direct journals with arbitrary finite phase | V8 and direct shared-journal fixtures preserve phases and reference identity. |
| Master/slave rod attachment | `engine.journals[]` `type: "master_rod"` variant with `master_cylinder`, positive `throw_radius`, and finite local `phase`; retired `master_journal`, `slave_throw`, and `slave_journal` fields remain forbidden | **Low-order executed for finite prescribed motion, `FreeEngine`, `HeldDyno`, and `FreeVehicle`** on a certified one-crank, one-level mechanism as of `11d5853`; references, exact-one consumer, connectivity, one-level direct master, and cycle rules validate. The immutable plan distinguishes roots from slave attachments and reproduces pristine one-level geometry without nominal slave stroke or displacement. Analytic articulated configuration inertia, cycle-mean inertia, per-cylinder piston-travel loss evidence, and leaf-first coupled wall reactions provide complete operating torque/inertia capability. Radial FreeVehicle uses the existing `nonnegative-speed-free-engine-one-level-master-rod-v1` crank identity and `one-level-master-rod-cycle-mean-equivalent-inertia-v1` baseline-inertia provenance; the existing road-load, clutch, and drivetrain identities remain topology-neutral, with runtime model ID `low-order-free-vehicle-one-level-master-rod-v1`. Radial HeldDyno retains its distinct `bounded-held-dyno-speed-constraint-one-level-master-rod-v1` identity. `HeldSpeed`, `LoadTargetHeld`, `InertialDyno`, nested attachments, and offset, geared, or otherwise independent multi-crank master/slave mechanisms remain closed | Canonical pristine-derived radial-five JSON preserves five banks, four slave pins, firing/cam/route associations, doubled head-flow tables, exact IR identity, and the source-backed propeller evaluation rig. Its 800-frame prescribed regression and full 52,000-frame warm `FreeEngine` and `FreeVehicle` sessions complete with finite nonzero PCM. The 4.7 s FreeEngine, 5.5 s HeldDyno, and 4.7 s loaded FreeVehicle candidates are accepted; the FreeVehicle audition SHA-256 is `8b2cc6620ef0e5e3f66ed18ebaf2990066fd813f9da1b11f97ee9eebf0f613eb`, and the accepted direct BMW guards remain exact. |
| Connecting-rod mass, inertia, center of mass, length | `engine.connecting_rods[]` | **Low-order executed/Partial**; direct-journal and one-level master-rod dynamics execute the physical crank-pin-to-COM distance in cycle-mean/configuration inertia and piston-wall inverse dynamics; omission resolves to the exact midpoint | Exact midpoint baseline output plus synthetic non-midpoint direct and articulated inertia, derivative, and coupled Newton--Euler reaction proofs. |
| Piston mass, blowby, compression height, wrist-pin position, displacement term | `engine.pistons[]`; blowby resolves into each bound cylinder's mechanism parameters and runtime gas lane | **Low-order executed** for admitted direct and prescribed/`FreeEngine`/`HeldDyno`/`FreeVehicle` one-level master/slave mechanisms. Wrist-pin position is an axial piston-datum offset that changes clearance/fixed volume but not wrist-pin motion; omission resolves to exact zero. Independent per-piston 28-inH2O CFM blowby also executes | Exact default baseline output, direct and master/slave constant-volume-offset proofs, exact flow-calibration admission, unequal synthetic blowby resolution, lane-local runtime differential, and the accepted Shovelhead source-value audition. |
| Banks with angle, bore, and deck height | `engine.banks[]` | **Low-order executed/Partial**; execution admits one zero-angle inline bank, exactly two finite distinct-angle V banks, exactly two antipodal opposed banks, or custom explicit finite axes, including authored bank bore/deck geometry. Direct centered rods and certified one-level master-rod axes execute in their admitted modes | Inline, Toyota V8, Subaru EJ25 opposed, synthetic three-axis custom, synthetic inline master-rod, and radial-five fixtures preserve bank geometry and axis-relative mechanics. |
| Arbitrary cylinder-to-bank/journal/intake/exhaust/wire connections | Explicit cylinder references; crank ownership derives through the referenced journal | **Partial**; dynamic cylinders, direct bank-axis and bank-local-head/cam/VTEC bindings, direct shared journals, prescribed and articulated-`FreeEngine`/`HeldDyno`/`FreeVehicle` one-level master/slave bindings, prescribed and rigid-dynamic co-phased multi-crank bindings, shared or distinct intakes and ignition wires, firing order, and exhaust sharing execute. Multi-crank master/slave remains closed | Toyota V8, Subaru EJ25, Shovelhead V-twin, radial five, synthetic three-axis custom, shared-wire inline-twin, separate-intake, split-crank, and split-bank VTEC fixtures prove the admitted connection patterns. |
| Per-cylinder primary length | Physical exhaust path in `engine`; session compilation combines it with route length, excitation propagation speed, and the admitted capture clock | **Low-order executed** without an engine-owned delay-rate duplicate or cached scenario-specific delay count | Historical 10 kHz and accepted 20 kHz delay comparisons prove `round(((header+route)/speed)*capture_hz)` while the frozen parity evidence remains exact. |

### Gas exchange, manifolds, heads, and exhaust

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Multiple, shareable intake objects | `engine.intakes[]` referenced by cylinders | **Low-order executed**; public compilation preserves stable ordered identity, shared references, independent plenum state for distinct objects, global throttle fan-out, lane-local restrictions/decay, and explicit cylinder bindings | Pristine-derived Shovelhead A/shared, B/equal-valued split, and C/differentiated split fixtures prove that value-equal objects are not deduplicated, lane bindings survive compilation, repeated renders are deterministic, and both topology and lane parameters affect PCM. |
| Intake plenum volume/area and runner length | Intake physical fields | **Low-order executed** | BMW resolved-value and held-point regression. |
| Main, idle-bypass, and runner restrictions | Calibrated intake restrictions | **Low-order executed** | Both flow-bench calibration standards resolve deterministically. |
| Idle throttle plate position | Intake physical/control field | **Low-order executed** | Closed-command idle flow remains nonzero and bounded. |
| Source intake molecular AFR / main-mixture richness | Fuel remains the sole stoichiometric-chemistry authority; each `engine.intakes[]` object owns a finite positive `main_mixture_lambda` | **Low-order executed**; source `0.8 * intake_molecular_afr` is normalized to `main_mixture_lambda * fuel_stoichiometric_ratio`, while idle-bypass mixture remains separate | Existing engines author lambda `0.8` with exact prior PCM; a two-intake differential proves lane-local lambda; missing/nonpositive/nonfinite lambda and any duplicate intake stoichiometric-AFR field fail closed. |
| Intake runner velocity decay | Intake gas-exchange method parameter | **Low-order executed** | BMW gas-state regression. |
| Shareable heads per bank | `engine.heads[]` and bank references | **Low-order executed** for shared or physically distinct bank-local heads with standard or VTEC valvetrains, including mixed standard/VTEC heads, distinct same-role cam profiles, and distinct VTEC thresholds | Shared/split exact-PCM comparison, Kohler source topology, the pristine-derived Shovelhead A/B bank-flow fixture, counterfactual bank-local cam differential probes, and split-bank VTEC selection fixture. |
| Chamber and intake/exhaust runner volume/area | Bank-keyed executable head profiles derived from head/port physical fields; the exhaust port exclusively owns exhaust-primary area | **Low-order executed** per bank. The deleted cylinder-chamber and exhaust-system-area duplicates have no compatibility aliases | BMW clearance/gas-volume comparison, shared/split exact-PCM comparison, and focused two-bank runner/primary runtime regression. |
| Arbitrary intake/exhaust port-flow curves | Bank-head port curve references | **Typed core/Low-order executed** per bank | Curves preserve units, independent intake/exhaust radii, clamping, sampled values, and cylinder BankId bindings. |
| Multiple, shareable exhaust systems | `engine.exhausts[]` and cylinder route references | **Low-order executed** for a dynamic nonempty exhaust set; cylinders may share routes and copied exhaust objects remain independent | Inline-six, V8, and V-twin grouping fixtures prove sharing and independent collectors. |
| Collector length/area, outlet restriction, primary length/restriction, velocity decay | Exhaust physical fields | **Low-order executed** | BMW resolved graph and gas-state regression. |
| `exhaust.collector_volume` convenience | Compiler authoring convenience deriving `collector_length = collector_volume / collector_area` | **Low-order executed**; exactly one of explicit collector length or collector volume is required | Explicit length and derived length compile to the same resolved model. |
| Executable gas volumes and flow edges | Compiler-derived from authored intakes, heads/ports, cylinders, exhausts, and their stable references | **Low-order executed/Partial** within the admitted rigid co-phased crank group, multi-intake, bank-local-head topology | Authors state each physical fact once; the compiler deterministically constructs and validates the supported internal graph. |

### Camshafts and valvetrain

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Cam advance, base radius, sampled lobe profile | `engine.camshafts[]` and angle-to-length `engine.curves[]` referenced by `sampled_cam_lobe` | **Low-order executed** for standard fixed valvetrains; sampled profiles retain source triangle weighting, clamping, advance, and crank phasing | Sampled and generated-equivalent lobes resolve and render byte-identically. |
| Arbitrary lobe centerlines added to a cam | Ordered cam lobe references by cylinder/port and centerline | **Low-order executed/Partial** | Firing-independent lobe ordering survives compilation. |
| Harmonic lobe generator parameters | Compiler-side curve generator | **Low-order executed** internally | Generator golden samples match the accepted BMW profile. |
| Standard intake/exhaust cam valvetrain | `engine.valvetrains[]`, `type: "standard"` | **Low-order executed** for shared or bank-local standard valvetrains. Distinct physical same-role cams compile in canonical engine-cylinder first-use order with an explicit profile binding on every lobe; each individual cam still requires one exact shared profile across its own lobes | BMW valve-lift traces and PCM remain unchanged; shared/equal split V forms produce byte-identical PCM; counterfactual front-only and rear-only cam changes produce distinct full-pipeline PCM while a GUI-only base-radius change does not. |
| VTEC base and alternate intake/exhaust cams | `engine.valvetrains[]`, `type: "vtec"` owned by each referenced head | **Low-order executed**; base and alternate roles compile independently through the same fixed-cam sampler, and each VTEC bank selects one coherent intake/exhaust pair. Standard banks in a mixed engine remain on their base cams | The one-bank Honda selects all four authored cams exactly; a split-bank fixture proves simultaneous alternate/base choices without collapsing to a representative head; fixed-cam engines remain unchanged. |
| VTEC RPM, absolute manifold-pressure, and resolved throttle-linkage-opening thresholds | Each VTEC valvetrain owns `activation.minimum_engine_speed`, `activation.minimum_manifold_pressure_abs`, and `activation.minimum_throttle_linkage_opening_01` | **Low-order executed** per bank with pristine's strict, stateless three-predicate selector. All banks sense the same engine-global output speed, arithmetic-mean manifold pressure, and resolved linkage opening; thresholds remain head-local. Pristine's stored-but-unused `min_speed` is deliberately absent | Honda transition matrix covers all three predicates at equality and on either side; a two-selector fixture proves distinct bank thresholds and a mixed standard/VTEC fixture proves absence of an invented selector. |

### Ignition and fuel

| Pristine capability | Intended JSON ownership | Current status | Acceptance requirement |
|---|---|---|---|
| Timing curve and filter radius | Ignition curve reference | **Low-order executed** with typed sampled curve | BMW timing values and evaluator behavior match. |
| Rev limiter speed and cut duration | `engine.ignition.limiter` | **Low-order executed** | Cut/re-enable event timing at the boundary is deterministic. |
| Ignition wires and arbitrary firing angles/order | Explicit wire objects, cylinder references, and ordered firing map | **Low-order executed**; each declared wire is used, has exactly one ordered firing post, and fans out to one or more cylinders. Public cylinders retain shared-wire group identity while one-cylinder wire objects normalize to their firing angles; the executable core expands posts to stable cylinder order | Toyota and V-twin firing sequences compile and render correctly; shared-wire and equal-angle split-wire twins retain different canonical identities but byte-identical PCM. |
| Fuel name/ID, molecular mass, energy density, molecular AFR | `engine.fuels[]` | **Low-order executed** | Gasoline fixture resolves every physical field once. |
| Fuel density | Fuel-volume-consumption telemetry, if that product output is later required | **Intentionally excluded from the simulation contract**; pristine consumes density only when its GUI converts accumulated fuel mass into displayed volume, not in combustion, torque, RPM, or audio | Define a fuel-consumption telemetry contract before adding it; do not imply a physics or sound control. |
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
| Exhaust/primary length attenuation and delay | Physical length in engine; excitation-session compilation derives the discrete delay from the scenario-owned capture clock | **Typed core/Low-order executed**; propagation speed remains method configuration, while no duplicate engine-level delay rate or cached delay sample count exists | Delay and inverse-length behavior are visible in resolved diagnostics; the sole executable cooker derives its counts from the exact 20 kHz capture clock. |
| IR filename/asset and IR volume | Content-addressed presentation asset and route gain | **Low-order executed** as static verified-asset conversion and per-route convolution | Decode/resample/hash and route convolution fixtures. |
| Convolution wet level | Presentation default; live-safe override | **Low-order executed** as a static per-route wet mix; no live override exists yet | Dry, wet, and mixed route auditions. |
| Engine `hf_gain` | Presentation derivative/HF conditioning default | **Low-order executed** as static derivative conditioning | Zero and accepted BMW settings A/B without changing physics. |
| Engine `jitter` | Presentation conditioning default plus deterministic seed | **Low-order executed** as static deterministic route conditioning | Same seed is reproducible; zero removes modulation. |
| Engine `noise` and GUI air-noise mix | Presentation conditioning default/live-safe override | **Low-order executed** as static deterministic filtered-air conditioning; no live override exists yet | Noise solo and zero-noise regression. |
| GUI master volume | Session monitor/output gain | **Partial**; audition monitoring gain exists | It never changes physical stems or canonical raw capture. |
| One channel per unique exhaust object | Compiler derives source routes from explicit shared exhaust identity | **Low-order executed** for the dynamic exhaust-route set | Shared object produces one route; copied objects remain separate. |
| One channel per unique intake object | Compiler derives `intake_inlet` source routes from explicit intake identity | **Post-parity active**; captured absolute plenum pressure is ambient-referenced, reconstructed from 20 kHz to 192 kHz, DC-removed at 10 Hz, and multiplied by the authored route gain. Intake uses no exhaust randomness, conditioning, IR, or convolution; its three stems are an identity transfer | The 2026-08-04 BMW A/B/C gate preserves every accepted exhaust WAV byte for byte and accepts the intake solo and full mix. |
| Excitation pressure combination, scale, low-speed ramp, propagation constant, and accumulation policy | Selected excitation-method configuration, generated by the compiler; physical lengths and audible gains remain authored; the capture block supplies cadence | **Low-order executed** behind a typed core at the admitted scenario capture rate | The sole executable cooker derives excitation and delay from exact 20 kHz capture rather than a hidden literal or compatibility selector. |

Pristine `engine-sim` does not expose a separately audible intake bus. The clean-room
session exposes each admitted intake as an active, separately addressable pressure
route and includes it once in the ordered masters. The implementation is the sole
production intake path; the earlier `declared_silent` topology checkpoint is not an
option or compatibility path. Exhaust routes and masters remain active and retain
their accepted renderer byte for byte. Mechanical-engine and mechanical-starter
remain reserved future route kinds and currently fail compilation. The intake
decision and exact listening evidence are recorded in
[`POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md`](../POST_PARITY_FIDELITY_ACTIVE_INTAKE_LISTENING_GATE.md).
The executed IR, wet, HF/derivative, jitter, and noise settings above are fixed when a
session is built; they do not imply a timestamped presentation-mutation API.

## Rig, session, and live-control capability matrix

The current executable session quantum is exact: 400 physics frames at 20 kHz become
3,840 delivery frames at 192 kHz, or 20 ms per `process_block()` call.
`quality.process_block_capacity_frames` is delivery-frame capacity and must be at least
3,840; a larger value does not change the current quantum.
This is the sole admitted production/cooker presentation path in native, WASM, and
browser execution. Historical 10 kHz fixtures remain parity evidence only; there is
no legacy rate selector or parallel compatibility mode. The accepted rate decision is
recorded in
[`../POST_PARITY_FIDELITY_RATE_GATE.md`](../POST_PARITY_FIDELITY_RATE_GATE.md).
`quality.event_queue_capacity` bounds caller-authored timestamped control commands, not
the internal simulation event journal. `quality.telemetry_capacity_frames` bounds
records returned per call; the current session emits one final-step session-telemetry
record per block, containing the engine sample and nullable HeldDyno/FreeVehicle
sidecars.

Slice 13 introduced this operating surface through the portable C++ session. The
current sole boundary is exact C ABI v7, the fixed-memory WASM wrapper, and
`engine-sim-offline/browser-worker-v4`. The
descriptor identifies one of seven motion modes and publishes an ordered forward-gear
inventory where applicable. The BMW FreeVehicle fixture exposes five descriptors in
authored order with ratios `4.21`, `2.49`, `1.66`, `1.24`, and `1.00`. Both mode
sidecars are absent during preparation and for nonapplicable modes; after release the
one owned by HeldDyno or FreeVehicle is present. C uses explicit presence fields and an
all-zero absent POD, while JavaScript maps absence to `null`.

| Pristine capability | Intended owner | Current status | Acceptance requirement |
|---|---|---|---|
| Vehicle mass, drag coefficient, frontal area, differential ratio, tire radius, rolling-resistance force, optional maximum service-brake force | Package `rig.vehicle` | **Low-order executed** for finite capture and open-ended forward-only `free_vehicle` sessions; passive road load follows the frozen pristine law and the service brake is an explicit greenfield extension | BMW held-brake launch and fifth-gear pull/lift fixtures; accepted held-dyno audio remains byte-identical. |
| Dyno minimum/maximum speed and hold-step defaults | Package `rig.dyno_defaults`; scenarios remain authoritative for an actual run | **Partial**; strict JSON parses and resolves reusable defaults, but no UI/default procedure executes them | Native and browser tools generate the same explicit held-point requests. |
| Transmission max clutch torque and ordered gear ratios | Package `rig.transmission` | **Low-order executed and published** for neutral and ordered forward gears through the exact bounded clutch-then-road coupled solve; native/C/WASM/Worker descriptors expose stable ID, authored ordinal, ratio, and semantic ID | Brake-hold, locked reflected-inertia, neutral, launch, first-to-second, and exact five-gear BMW inventory fixtures. |
| Held speed and prescribed RPM sweep | Session/render request | **Partial**; authored held-speed and prescribed-kinematic execution exist, while live external RPM following remains unavailable | Existing BMW held points and prescribed sweeps; a future external-follower contract. |
| Bounded held dyno | Session/render request and timestamped mode controls | **Low-order executed and published** for finite target-RPM captures and open-ended benches with separate absorbing/driving torque limits; target and both limits are live through native/C ABI v7/WASM/Worker v3 | BMW pull, exact hold, lift, zero-drive overrun, same-boundary command batch, nullable sidecar, reaction telemetry, and listening gate. |
| Load-target held capture | Session/render request | **Low-order executed** | Converged target/tolerance result. |
| Inertial dyno with inertia and brake curve | Session/render request | **Low-order executed** | Existing BMW inertial pull and torque evidence. |
| Ambient pressure/temperature, initial gas/wall state, crankcase, fuel, seed, render rates, preparation | Session/render request and rig defaults | **Typed core and executed** | Native and WASM resolve the same request identity. |
| Relative humidity, coolant temperature, and oil temperature | Session/render request metadata/applicability conditions | **Admission/evidence only** in the current low-order executor; oil temperature must match the loss-profile condition | Do not present these as live sound or power controls until an implemented subsystem consumes them. |
| Quality telemetry capacity | Session output allocation policy | **Low-order executed** as returned-record capacity; each exact block returns one final engine sample plus nullable mode sidecars, while authored telemetry-channel selection still fails closed | Browser transport preserves the same record boundary and nullability without confusing it with PCM or the internal event journal. |
| Ignition, fuel, starter, dyno/limiter enable events | Timestamped session controls | **Partial**; ignition and fuel execute live in inertial-dyno, `free_engine`, `held_dyno`, and `free_vehicle`; limiter executes in the two free modes; starter is capability-gated in `free_engine` and `free_vehicle`. Motion ownership is immutable, so there is deliberately no dyno-enable/mode-switch command; a constant target is HeldDyno hold | Block-boundary and in-block control timing fixtures; unsupported mode/control combinations remain rejected atomically. |
| Realtime throttle | Timestamped live controls | **Low-order executed**; public absolute-delivery-frame throttle executes in inertial-dyno, `free_engine`, `held_dyno`, and `free_vehicle` after preparation, and Worker v3 exposes the same atomic transport | Audible throttle response without restart in every mode that advertises the capability. |
| Realtime external resisting torque | Timestamped live controls | **Low-order executed** for `free_engine`, including the public session API and browser Worker; other modes reject it | An in-block load step changes FreeEngine acceleration and telemetry without restarting the session. |
| HeldDyno target RPM and torque limits | Mode-specific timestamped controls | **Low-order executed and published** through EngineSession, C ABI v7, WASM, and Worker v3 with exact capability bits and final-step telemetry | One atomic same-boundary target/absorbing/driving batch changes the first released step and reports its applied values and disposition. |
| External/prescribed RPM or load-following command | A future mode-specific timestamped control | **Missing** as a public live API; authored prescribed and load-target modes remain separate finite motion owners | External follower and load-coordinate contracts must define ownership before browser exposure. |
| Gear, clutch, and service-brake controls | Mode-authored and timestamped `free_vehicle` controls | **Low-order executed and published**; gear zero means neutral, positive values are published authored ordinals, clutch is `[0,1]`, and brake is capability-gated by positive rig capacity | An out-of-inventory gear rejects its complete batch; an admitted same-boundary gear/clutch/brake batch reaches exact final-step drivetrain telemetry. |
| Realtime-safe presentation knobs | Timestamped parameter controls | **Missing** as public API despite typed defaults | Click-free gain/wet/HF/noise changes. |
| Master/stems/telemetry block output | Session block result | **Low-order executed** as public borrowed route dry/IR/selected buses, raw/audition masters, and one session-telemetry record per exact 3,840-frame block; Worker v3 publishes one selected bus through the shared PCM ring and posts the engine sample plus nullable HeldDyno/FreeVehicle sidecar, while authored bus/telemetry subset selection and simultaneous browser stem publication remain absent | Route selection reaches the exact named core bus; mode sidecars match the final physics step; 128-frame AudioWorklet pulls never change the 3,840-frame engine-session quantum. |

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
| Fuel density and GUI fuel-volume display | Pristine uses density for application telemetry only; neither affects engine physics or audio, and this product does not reproduce the native GUI. |
| Named Chevy/other part-library presets and the bundled IR catalog as schema capabilities | They are assets/templates. They may be converted and distributed separately. |
| Accidental implementation/channel behavior | Bugs are not contract. Parity claims cover intended audible behavior and explicit accepted fixtures. |

The current internal parity profile still contains at least one fork-derived fuel member;
that does not admit it to the public JSON contract. It should disappear when the generic
fuel contract replaces the current low-order spark-ignition method family.

## Required acceptance fixtures

These capability-focused fixtures define the accepted executable scope. They avoid a
large test matrix while covering the graph shapes that the BMW inline-six cannot.

During cutover checkpoints 1-10, only compiler/graph and deterministic behavior checks
were blocking. Slice 16 subsequently rendered and accepted the representative
fixed-cam inline/V, VTEC, governed, master/slave, and drivetrain procedures; exact
paths and hashes are frozen in
[`../SLICE_16_PARITY_LISTENING_GATE.md`](../SLICE_16_PARITY_LISTENING_GATE.md).

| Fixture | Capabilities it must prove | Post-cutover listening |
|---|---|---|
| BMW M52B28 clean-room baseline | Inline bank, six cylinders, shared intake/head, two shared exhaust routes, flow/timing/flame curves, fixed valvetrain, gains, IRs, held points, and dyno pull | Compare JSON-compiled held and dyno clips with the accepted C++-profile baseline. |
| Toyota 3UR-FE V8 | Two authored bank angles, direct shared journals, generic firing order, and grouped/shared exhaust systems | Route solos plus full dyno pull; cadence and grouping accepted by ear. |
| Subaru EJ25 flat-four | Two antipodal authored bank axes, four distinct direct journals, bank-local equivalent heads/cams, and exact opposed mechanism pairs | Structural/runtime fixture only for this topology slice; add listening when it becomes a catalog engine. |
| Honda B18C5 | Standard/alternate cams and the executed pristine RPM, manifold-pressure, and resolved linkage-opening predicates; the dead source `min_speed` input is intentionally absent | Below, transition, and above-VTEC clips; no unrelated renderer change. |
| Radial five | One direct master cylinder, four owner-local slave pins, five bank axes, unequal piston motion, generic firing/cam associations, differentiated exhaust routes, and articulated `FreeEngine`/`HeldDyno`/`FreeVehicle` motion | Keep the 80 ms prescribed file as a byte/topology regression only. The 4.7 s warm FreeEngine procedure is accepted. The accepted HeldDyno gate uses a 5.5 s 192 kHz mono PCM24 prescribed control and physical pull/lift candidate; their audition SHA-256 values are `e446c0fd0f9348877dcdd59b32f59152912b13a68ecbc824f83881b3c9c599a9` and `e193d2e981a5d432928aa8596b6730df37c9cd6aa680332479d77eb24a636c72`. Commit `11d5853` adds the source-backed propeller/direct-drive FreeVehicle gate; its accepted 4.7 s 20%-clutch candidate has audition SHA-256 `8b2cc6620ef0e5e3f66ed18ebaf2990066fd813f9da1b11f97ee9eebf0f613eb`. |
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
