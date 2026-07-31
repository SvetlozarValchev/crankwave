# M2 data contract

Status: normative interface record for the current M2 data-contract checkbox

Applies to: authored and resolved configuration, scenario ownership, simulator
capture, typed render results, source/output completeness, and render evidence

This document describes the data contract implemented by the current C++ types and
validators. The headless API, fail-closed admission boundary, transaction protocol,
and CLI shell now exist and are recorded separately in
[`M2_RENDER_API.md`](M2_RENDER_API.md), while integer scheduling and bounded traversal
are recorded in [`M2_SCHEDULING.md`](M2_SCHEDULING.md). Bounded artifact encoding,
transactional directory publication, and the focused P1.8 DSP primitives are recorded
in [`M2_ARTIFACTS_DSP.md`](M2_ARTIFACTS_DSP.md). The M3 simulator, profile-specific
BMW request compiler, presentation stages, and M4 opaque public
simulation-to-acoustic job now exist. General authored resolution remains a separate
checkbox in [`PLAN.md`](../../PLAN.md).
The former isolated-reference input alternative is preserved only as historical
evidence in [`M2_MANIFEST_INPUTS.md`](M2_MANIFEST_INPUTS.md); the current manifest
input is simulation-only.

The governing physical, numerical, and provenance meanings remain in
[`MODEL.md`](../../MODEL.md). The exact BMW parity algorithm is separately fixed by
[`M3_PARITY_MODEL.md`](../model/M3_PARITY_MODEL.md).

## 1. Boundary and current status

The intended data flow is:

```text
AuthoredEngineDefinition          AuthoredPresentationCalibration
  values + claim IDs                values + claim IDs
              \                         /
               +---- ProvenanceLedger --+
                           |
             profile-specific request compiler      BMW implemented
                           v
EngineSpec + RenderScenario + PresentationCalibration
                 resolved values + resolution IDs
                           |
             render preflight/session boundary    implemented
                           |
              simulator publishes CaptureBlock   internal M3 path implemented
                           |
              excitation and presentation         opaque M4 job implemented
                           |
              artifacts + RenderManifest         atomic publication implemented
```

The current contract supplies:

- stable identity and validation types;
- authored and resolved engine/presentation records;
- the resolved scenario variants;
- explicit torque capability and telemetry vocabulary;
- typed success, unreachable-target, and failure records;
- a callback-scoped `CaptureBlockView`;
- source-matrix, presentation, artifact, and manifest schemas;
- generic validation and exact frozen BMW reference validation;
- the owning render-specification aggregate, admitted/fail-closed render entry point,
  opaque execution job, sink transaction protocol, and CLI shell recorded in
  `M2_RENDER_API.md`;
- the profile-specific resolved BMW request, M3 low-order simulator and excitation,
  internal presentation session, live execution-facts observation, and canonical
  completed simulation-manifest v6 encoder.

The current contract does not supply:

- general authored-to-resolved conversion;
- a serialized profile/scenario loader behind the CLI shell.

The concrete directory sink verifies streamed artifact payload hashes and publishes
encoded artifacts transactionally. WAV and complete capture-telemetry byte encoders
exist; `render()` invokes the audio transaction only after the complete route and its
matching manifest basis have been admitted.

A valid data object therefore means “internally consistent and admitted by this
schema,” not “rendered successfully” or “sounds correct.”

## 2. Authored values, resolved values, and provenance

`AuthoredValue<T>` carries a value and a `claim_id`. The claim identifies why that
value was authored: literature, official data, measurement, inference, fixture
evidence, an unverified legacy asset, a derivation, a scenario choice, calibration, or
an artistic choice. Authored validation requires the claim to exist in the associated
`ProvenanceLedger`; it does not silently fill missing values or perform resolution.

`ResolvedValue<T>` carries the executable value and a `resolution_id`. That ID must
refer to exactly one `ResolutionRecord`, whose `parameter_path` must equal the
validator's canonical path for that leaf. A resolution ID valid for one parameter
cannot be reused to legitimize a different parameter.

For example, the following are distinct bindings:

```text
scenario.ambient.pressure_pa_abs
scenario.crankcase.pressure_pa_abs
presentation.conditioning.air_noise_cutoff_hz
engine.cylinders.<cylinder-semantic-id>.bore_m
```

The angle-bracketed portion above describes the path pattern; a real path contains the
canonical semantic ID. Resolved identity-keyed engine, presentation, and physics
collections use semantic IDs in paths so reordering a vector does not change the
meaning of a leaf. Scenario variant leaves use their fixed `scenario.*` paths.

Canonical semantic IDs are nonempty ASCII identifiers beginning with a lower-case
letter or digit. Remaining characters may be lower-case letters, digits, `.`, `_`,
`-`, or `/`. Resolution IDs and parameter paths are unique within a ledger.

A `ResolutionRecord` also fixes:

- resolution mode: authored, declared default, derived, inferred, or calibrated;
- the supporting provenance claim;
- a versioned, configuration-hashed method for derived, inferred, and calibrated
  values;
- dependency parameter paths for derived values.

The ledger validates evidence references, content hashes where immutable evidence is
required, explicit rights disposition, claim/evidence links, uncertainty statements,
and acyclic resolution dependencies. Numeric/domain validation remains separate:
provenance can explain a value but cannot make an invalid value admissible.

Resolved engine and presentation records name the provenance schema against which
they were resolved. A ledger carries its caller-verified, content-addressed
`ProvenanceBundleRef`. `RenderManifestContent` is validated against a caller-supplied
ledger and must reproduce that exact bundle identity; it does not embed the ledger
itself.

## 3. Engine input and executable profile

`AuthoredEngineDefinition` uses semantic string references and `AuthoredValue` leaves
for engine identity, topology, geometry, methods, and the authored executable physics
profile. Its embedded ledger records the source of those choices.

`EngineSpec` is the executable counterpart. It uses nonzero stable numeric IDs for
engine, bank, cylinder, port, gas-volume, flow-edge, route, and asset identity while
retaining resolved semantic IDs for evidence and serialization. It contains:

- resolved engine identity, layout, cycle, ignition kind, and displacement;
- explicit cylinder, port, gas-volume, flow-edge, and source-route topology;
- versioned and configuration-hashed mechanism, valvetrain, gas, ignition,
  combustion, heat-transfer, loss, and excitation methods;
- one typed `ExecutablePhysicsProfile`;
- a resolved `TorqueCapability`.

Topology validators reject duplicate IDs, dangling references, invalid source-route
relationships, and cyclic source-route parenting. Resolved displacement is checked
against cylinder geometry.

The sole currently admitted executable-profile variant is
`LegacyLowOrderV1Profile`. Its presence in `EngineSpec` defines an exact typed
configuration boundary; it does not mean the M3 kernel has been implemented. Section
10 describes that boundary.

That profile is explicit composition rather than inheritance or a compatibility
facade. `LowOrderEngineCoreV1` owns the reusable mechanism, gas path, valvetrain,
ignition, fuel, combustion-stream, and excitation data.
`LegacyFixedCrankLossV1` separately owns the M3 fixed crank-friction magnitude and
both exhaustive loss-classification masks. The authored contract has the same
ownership split. This separation permits a later operating profile to reuse the
accepted low-order core without also inheriting M3's incomplete fixed-loss model.

## 4. Scenario variants and motion ownership

`RenderScenario` is already a resolved test-cell request. Common data include:

- scenario and engine-profile identity;
- ambient pressure, temperature, and humidity;
- fuel identity, heating value, and stoichiometric air/fuel ratio;
- initial gas, wall, coolant, and oil temperatures;
- crankcase absolute pressure and temperature;
- fixed-duration or fixed-horizon complete-cycle sampling preparation;
- a right-continuous operating-state timeline for ignition, fuel, starter, dyno, and
  limiter state;
- total duration and the retained half-open audible interval;
- reduced-rational physics, capture, source-processing, acoustic, and delivery rates;
- quality profile, bounded capture-frame capacity, and bounded event-journal record
  capacity;
- a public deterministic seed.

Preparation is causal history. For `FixedSettling`, warm-up plus settling ends exactly
at the audible start. For `FixedHorizonCycleSampling`, held/inertial operation retains
the same equality. A positive-speed FreeEngine instead retains the declared trailing
complete-cycle sample at its fixed horizon, may physically release there, and may run
dynamic hidden acquisition until an equal-or-later audible start. In every case the
audible interval ends at total duration, and cropping does not imply a state reset.
These relationships are rechecked after every duration is resolved to an integer
physics/stream frame, so binary64 near-equality cannot leave an undeclared boundary.

Exactly one tagged `ScenarioMode` owns crank motion:

| Variant | Authoritative request | Contract result meaning |
|---|---|---|
| `HeldSpeed` | Positive fixed RPM, initial crank angle, throttle | Motion is held; sampled cycle-mean torque and achieved load are results. |
| `PrescribedKinematicSweep` | RPM trajectory, initial angle, throttle trajectory, named kinematic method | The scenario imposes motion; an actuator result is the residual required to impose it. |
| `HeldDyno` | Initial RPM/angle, target-RPM trajectory, throttle trajectory, maximum absorbing/driving torque, named constraint method | The bounded actuator attempts the target; achieved RPM and the exact opposite dyno reaction remain results when either limit saturates. |
| `LoadTargetHeldCapture` | Fixed RPM, signed target net BMEP, tolerance, bounded throttle search, named search method | The search reports a reached target or a typed unreachable target; saturation is not disguised as success. |
| `InertialDyno` | Initial RPM/angle, equivalent inertia, throttle trajectory, passive brake curve, named crank-dynamics method | The dynamics owner advances RPM; the brake curve supplies resistance. |

Trajectories declare right-continuous-hold or linear interpolation, begin at time zero,
cover the requested interval, and carry a resolution record. Operating-state events
have stable IDs and strictly increasing times.

Engine/scenario compatibility is capability-gated. Held-speed and load-target
scenarios require an available, complete cycle-mean net-shaft torque form. `HeldDyno`
and `InertialDyno` require an available, complete instantaneous net-shaft torque form
and admitted mechanism inertia. A complete form in one time domain cannot stand in
for the other.
The scenario and engine profile IDs must match. The scenario fuel identity and lower
heating value must exactly match the executable profile's fuel identity and energy
density, so two conflicting fuels cannot enter one render. The legacy profile's
source-named molar ignition ratio is deliberately distinct from the scenario's
conventional stoichiometric air/fuel mass ratio.

## 5. Torque and load semantics

The internal load coordinate is signed `net_bmep_pa`. Negative values represent
motoring or overrun; a bare, unsigned “load” scalar is not part of the contract.
`target_net_bmep_pa`, achieved BMEP, throttle, actuator torque, and dyno reaction are
separate quantities.

`TorqueTelemetry` distinguishes:

- instantaneous indicated gas torque;
- the pumping diagnostic partition;
- the `friction_pump_and_accessory` aggregate and the distinct starter torque;
- instantaneous and cycle-mean net shaft torque;
- actuator torque and dyno reaction;
- cycle work, net BMEP, and instantaneous/cycle-mean power.

Dyno reaction is exactly the negative of actuator torque when both are available.
Pumping is a partition of the gas work, not a second term to subtract from a
full-cycle result.

Every quantity declares availability and completeness. An unavailable value is
canonical positive zero, is incomplete, and states a reason; downstream code must not
consume it as a measurement. Available values are finite, use the `none`
unavailability reason, and may be complete or explicitly incomplete. Torque values
additionally declare included and omitted term masks.

For every available named torque, included and omitted masks together must equal that
field's exact scope:

| Field | Engine-term scope |
|---|---|
| `instantaneous_indicated_gas` | indicated gas |
| `pumping_partition` | empty; it is a diagnostic partition of indicated gas, not another additive term |
| `friction_pump_and_accessory` | crank/ring/bearing/valvetrain friction, pump/oil, and accessory |
| `starter` | starter only |
| instantaneous/cycle-mean net shaft | every known physical engine term |
| actuator/dyno reaction | empty; both are outside engine net-torque ownership |

A complete named torque includes its entire scope and omits none. An incomplete named
torque still classifies the entire scope between included and omitted masks. Thus a
complete net value cannot pass by merely declaring no omissions while failing to name
all physical terms.

`TorqueCapability` describes instantaneous and cycle-mean net-shaft forms separately.
Each available `NetTorqueFormCapability` must classify every known physical term
exactly once: indicated gas, crank friction, ring friction, bearing friction,
valvetrain friction, pump/oil, accessory, and starter. It is complete exactly when
none is omitted. An unavailable form is canonical incomplete with both masks empty;
it must not preserve a classification that consumers could mistake for a result.
Equivalent inertia remains a separate capability.

The four-stroke work/BMEP identities and the motion sign convention are normative in
[`MODEL.md` §8](../../MODEL.md#8-scenario-and-torque-semantics); this schema names
their inputs and results but does not calculate them yet.

## 6. Reachability and render results

`RenderResult` is a tagged union:

- `RenderSuccess` contains a `RenderManifest` and, for a load-target result when
  applicable, a `ReachedTarget`;
- `UnreachableTarget` is its own typed non-success result;
- `RenderFailure` carries every other `FailureContext` plus retained input diagnostics
  when preflight or evidence-rights validation caused the rejection.

Caller cancellation uses the dedicated `FailureKind::cancelled`. It is observed only
at the deterministic boundaries recorded in `M2_SCHEDULING.md`; it is not relabelled
as a numerical or contract failure.

A reached target records target BMEP, achieved BMEP, signed error
`achieved - target`, tolerance, the selected settled candidate, and search evidence.
Its absolute error must be within tolerance.

An unreachable target records the same signed relationship, the deterministic nearest
settled feasible candidate, every active limiting bound, retained search evidence, the
complete request identity, and a failure context whose kind is `unreachable_target`.
Its error must be outside tolerance. A generic `RenderFailure` is forbidden from using
that failure kind.

Result validation is request-aware: it receives the original `RenderScenario`, and a
successful manifest must reproduce that scenario exactly. Only a load-target request
may carry reached-target evidence or return `UnreachableTarget`; its reported target,
tolerance, and search minimum/maximum must exactly reproduce the request's target,
tolerance, and throttle bounds.

Search evidence contains that throttle interval as
`requested_throttle_lower_bound_01`/`requested_throttle_upper_bound_01`, plus the
iteration count, stable candidate IDs, and all evaluated probes retained by the
current contract, with a hard limit of 256. The selected candidate must occur in that
evidence. An active throttle bound must exactly equal both the corresponding requested
search endpoint and the selected candidate's throttle. The current load-target
request authors no actuator-torque interval, so `ActiveReachabilityBound` can report
only lower or upper throttle saturation; actuator-limit kinds do not exist. The
deterministic nearest ordering is:

1. smallest absolute BMEP error;
2. smallest absolute actuator torque;
3. lowest throttle;
4. lowest stable candidate ID.

`FailureContext` identifies the failure class, model/profile, sample and step, scenario
time, crank angle, optional component IDs, state summary, attempted recovery, and
named tolerances. It provides diagnosis; it is not permission for a silent fallback.
An `invalid_specification` or `evidence_rights_failure` must also retain a nonempty
`ValidationReport` with recognized issue codes, paths, and messages.
Every non-success retains the complete resolved engine/presentation/scenario,
provenance ledger, selected source matrix, and canonical asset payload identities
(`AudioAssetId`, byte count, actual SHA-256), so result validation cannot silently
rebind a rejection to different inputs.

## 7. `CaptureBlockView`

`CaptureBlockView` is the one-way simulator-to-excitation boundary. It carries named
SI/control observables, not presentation state or a premixed “engine sound” signal.

It is deliberately a borrowed callback view:

- `CaptureLayoutView`, `CaptureBlockView`, `EventJournalView`, and
  `ReferenceParityBlockView` contain non-owning spans;
- their factories accept only contiguous, sized, exact-element lvalue ranges;
  temporary containers and temporary spans are rejected at compile time;
- the producer owns the referenced storage;
- storage remains alive and immutable only until the receiving callback returns;
- a consumer must not retain, enqueue, mutate, or pass a view across a thread
  boundary.

All entity arrays are frame-major:

```text
sample index = frame index * layout entity count + entity index
```

The layout fixes stable order and identity for cylinders, ports, gas volumes, flow
edges, and physical source routes. Validation against an `EngineSpec` and
`RenderScenario` binds the engine ID and profile, exact ordered entity topology,
edge endpoints, route source/emitter/parent relationships, capture rate, and declared
scenario frame/event transport capacities. Safe accessors return `nullptr` for an
out-of-range or unrepresentable index. Validation uses overflow-checked shape
arithmetic.

`CaptureClock` uses a reduced rational rate, integer sample/timestamp origins, and an
explicit pre-step or post-step phase. A block declares its current frame count, positive
frame capacity, and positive event-journal record capacity. Neither current count may
exceed its declared capacity, and both capacities bind exactly to the resolved scenario
quality. The phase fixes the exact sample-index-to-timestamp relationship. The block's
half-open integer sample interval must fit inside the scenario's resolved capture-frame
horizon: a final post-step timestamp may equal total duration, while a pre-step sample
at total duration is outside the run.

Per-frame data include:

- engine phase, angular state, RPM, control/linkage state, operating flags, limiter,
  and torque telemetry;
- per-cylinder mechanism, thermodynamic, composition, combustion, and indicated-gas
  torque observables;
- per-port pressure, temperature, signed mass flow, effective area/conductance, and
  valve lift;
- per-volume thermodynamic state, composition, energy, and planar momentum;
- per-edge signed mass flow;
- one typed payload per physical source route.

Validity masks state which observable families are meaningful. On an engine sample,
the torque-valid bit requires at least one torque or derived torque quantity to be
available; without it every torque quantity is unavailable. On a cylinder sample, the
torque-valid bit requires available indicated-gas torque with exactly the
indicated-gas term, while its absence requires that quantity to be unavailable.
Finite/domain checks are conditional where appropriate; zero-amount mixture has
canonical zero fractions, while a positive amount requires nonnegative fractions
summing to one within the declared tolerance.

The source-route payload is a tagged union:

- exhaust and intake routes publish `GasSourceRouteCaptureSample` with absolute
  pressure, temperature, signed mass flow, and effective area;
- engine and starter mechanical routes publish
  `MechanicalSourceRouteCaptureSample` with an engine-local force/torque wrench at
  the route's declared emitter anchor.

The payload tag must match the layout's physical `SourceRouteKind`. Diagnostic routes
are not admitted as physical capture routes.

Events use a compressed-row journal: `offsets` has `frame_count + 1` entries and
selects each frame's strictly ordered event records. Payloads distinguish spark
crossings, limiter transitions, accepted/rejected ignition, and flame extinction. The
complete event span cannot exceed the scenario's declared event-journal transport
capacity.

The optional `reference_parity` view is a narrow BMW M3 comparator extension carrying
filtered RPM and the exact per-cylinder legacy pressure proxies. When present it
activates the frozen 10 kHz post-step clock, sample/step relationship, and stable M3
event-order constraints. It is not the validation-only `ReferenceAuditBlock`, and it
is not a future production acoustic source contract.

## 8. Physical source routes and output buses

Physical source routes and output buses are different identity domains.

`RouteSpec` and `RouteIdentity` describe physical simulator/source topology. Their
kinds are exhaust outlet, intake inlet, engine mechanical, or starter mechanical.
Diagnostics are artifacts, not a fifth physical route kind, and remain a separate,
explicitly labelled output category. A source route has a stable `RouteId`, may bind a
physical source volume or emitter anchor, and may have an acyclic default parent
route.

`OutputBusRequirement` and `OutputBusRecord` describe downstream mixes such as raw or
audition masters. A bus has a semantic ID and `OutputBusKind`, but no `RouteId` and no
pretence of being a physical emitter. A master is therefore never accepted as a
substitute for a required source route.

Both domains own artifact roles explicitly. Every required audio artifact has exactly
one source-route or output-bus owner. A rendered route owns its required artifacts and
has no omission reason, and it must have an admitted presentation configuration. A
`not_applicable` route owns no artifacts, has no presentation configuration, and must
carry a policy-owned reason in the source matrix; the manifest must reproduce that
reason exactly. “Not implemented” is not made equivalent to `not_applicable`.

## 9. Source matrix and presentation calibration

### 9.1 `SourceMatrixContract`

A source matrix is an independently selected policy object. It fixes:

- matrix semantic ID and content digest;
- local-evaluation or distributable intent;
- required source-route identities, kinds, dispositions, and artifact roles;
- required output-bus identities, kinds, and artifact roles;
- required artifact kinds, audio media contracts, and diagnostic status;
- typed, stable IDs and rationales for known omissions.

`resolve_output_contract()` copies that policy into the manifest. Manifest validation
then requires exact equality with the selected matrix; the renderer cannot silently
drop a route, weaken a media contract, or relabel ownership.

The old M2/M3 BMW reference matrix is retained only as frozen historical evidence; it
is not a built-in production policy or an executable engine route. Its two
local-evaluation exhaust routes, diagnostic stems, reference masters, and explicit
omissions remain documented in
[`SOURCE_MATRIX.md`](../../reference/oracles/bmw-m52b28/SOURCE_MATRIX.md).
Current JSON compilation emits only the generic engine raw/audition output buses.

Production completeness remains governed by
[`PRODUCTION_SOURCE_MATRIX.md`](../PRODUCTION_SOURCE_MATRIX.md) and is later work.

### 9.2 `PresentationCalibration`

Presentation is one-way and downstream of physical capture/excitation.
`PresentationCalibration` resolves:

- exact schema version 2, with no schema-1 compatibility interpretation;
- versioned, configuration-hashed reconstruction, conditioning, IR-conversion,
  convolution, publication, and audition methods;
- jitter, derivative, and air-noise conditioning values;
- content-addressed audio assets with evidence identity and exact media shape;
- per-route IR asset, gain, and wet selection;
- publication calibration gain;
- ordered audition-route reduction, monitoring gain, and edge fades.

Those six method identities are the complete current presentation-algorithm
authority. Their configuration digests cover the executable block partition,
filter/kernel preparation, state, crop, tail, mastering, and publication conventions;
there is no second caller-authored algorithm record that can drift from execution.

The ordered audition route vector is an arithmetic reduction order, not merely a set.
Engine, scenario, and presentation profile IDs must agree. Assets must link to
content-addressed provenance evidence whose digest exactly matches the asset, route
and asset references must resolve, filter cutoffs must be below the
source-processing Nyquist rate, and fades must fit in the audible interval.
The current convolution-presentation validator admits configured exhaust routes only;
it does not falsely claim intake or mechanical presentation support.

The historical frozen BMW reference evidence separately pins its former P1.8 method
IDs and versions and the immutable complete renderer record at SHA-256
`0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008`,
conditioning constants, `smooth_39` asset identity/media/hash, the
calibration-route order (`exhaust.reference.0`, then `exhaust.reference.1`), IR
gain/wet selection, `2^-26` publication calibration, the same audition reduction
order, audition gain/fades, clock plan, and `[2 s, 17 s)` retained interval. Method
configuration digests in current execution come only from the implementation; the
historical record is oracle evidence, not an execution input or compatibility
surface.

The associated capture transport must hold the fixed 200-frame block and its
worst-case `19 * 200 = 3,800` event records. Method configuration digests remain
content identities supplied by the implementation; admission does not substitute a
hard-coded digest for them. Constants including the
3,840-frame source partition, 9,600-frame convolution limit, 65,536-point transform,
6,907-frame source support, 30,071-coefficient kernel identity, zero history,
continuous crop state, and no-tail policy are fixed by the content-addressed
[`P18_PRESENTATION_RENDERER.md`](../../reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md).
That record remains immutable audit evidence and is not accepted as current
configuration.

## 10. `RenderManifest`

`RenderManifest` deliberately separates reproducible content from execution facts.

`RenderManifestContent` contains:

- the complete resolved engine, presentation, randomness-policy, and scenario inputs;
- a content-addressed provenance-bundle reference;
- build/toolchain, loaded runtime-provider, compiled numeric-policy, floating-point,
  instruction-set, worker, and reduction identities;
- the resolved rate plan;
- public seed, generator/derivation methods, and typed component-owned random streams;
- the exact resolved output contract;
- one route record per resolved engine route;
- exact output-bus records;
- emitted artifact records with portable relative path, media shape, byte count, and
  payload SHA-256.

The current determinism envelope requires the admitted libstdc++, glibc
libm, and libgcc_s providers; numeric policy
`x86-64-v1-binary64-x87-extended-strict-v1`; ISA profile `x86-64-v1`; one serial
stable-order worker; strict IEEE-754 binary64, round-to-nearest/ties-to-even; no FMA
contraction; and no flush-to-zero or denormals-are-zero. Component seeds are typed and
owned by a cylinder or route
according to their stochastic role; duplicate kind/owner streams are rejected.
Executable method identities live once in the resolved engine, presentation,
randomness policy, scenario, and initialized random plan rather than in a second
manifest inventory that could silently drift. The random plan's generator and
derivation must equal the resolved policy. Its component inventory, order, and seed
pairs are independently recomputed from the explicit namespace, public seed, and
configured topology. The legacy-low-order profile's retained combustion initializations
are executable cache values only and must equal that canonical derivation.
Current combustion requires one initialized lane per cylinder; current presentation
jitter and air noise require one initialized lane of each kind per configured route
even at zero scale because those executors still instantiate them. Presentation
advances both route-owned generators; combustion draws only for accepted ignition
events. The plan does not claim runtime draw counts. Historical P1.8 evidence
separately pins its former generator, derivation, seed, and four route-owned
presentation stream pairs in
[`P18_PRESENTATION_RENDERER.md`](../../reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md).
Those records are audit evidence only; their generator is not admitted by the current
simulation contract.

Simulation-content validation cross-checks all four resolved inputs and scenario
compatibility, rates, public seed, source matrix, route/bus identity, artifact
ownership, exact delivery frame count, file shape, payload presence, evidence rights,
and distribution intent. Artifact paths must be normalized portable relative paths
without traversal, drive syntax, control characters, or case-insensitive duplicates.
Extra artifacts are allowed only when explicitly diagnostic. The former reference
validation route is retained only in the historical evidence record and cannot
fabricate a current manifest.

`ExecutionFacts` separately names run ID, canonical RFC 3339 UTC start, wall elapsed
time, host/CPU, thread/job counts, and optional peak resident memory. The field is
optional so
deterministic content can exist before execution, but validation of a completed
`RenderManifest` requires complete, positive execution facts. Execution is excluded
by `same_content_identity()`, so machine timing cannot change deterministic render
identity. The historical Linux reference tool collected those facts through a
private, one-shot single-render-job observer and validated/encoded its complete
manifest in memory. That tool and observer are retired and are not linked to the
still fail-closed public render route.

A content-valid manifest is still not a successful render. The public render boundary
now rejects valid inputs with `incomplete_source_route` until later renderer/sink work
creates payloads, computes hashes, publishes required outputs transactionally, and can
return the manifest only on success. A public `RenderSuccess` is valid only with the
complete simulation inputs. Historical fixture replay cannot manufacture public
success.

## 11. M2 contract versus M3 exact profile

M2 owns the general seams and the narrow accepted reference-presentation path:

- authored/resolved/provenance representation;
- scenario, torque, reachability, capture, route, bus, artifact, and manifest types;
- the frozen two-route reference source/output policy;
- the exact P1.8 presentation configuration record.

M2 fixture rendering is allowed to bypass physics only through the isolated,
test/reference audit adapter described by the frozen matrix. That adapter is not a
public render input and must not become a simulator dependency.

The `LegacyLowOrderV1Profile` schema already exists because an `EngineSpec` must name
the exact executable method and every input before physics implementation begins. It
composes a resolved, provenance-bound `LowOrderEngineCoreV1` with a
`LegacyFixedCrankLossV1`. Together they carry M3 data for:

- analytic mechanism and per-cylinder topology/parameters;
- restrictions, intake/head/exhaust gas paths;
- cam shapes and stable lobe bindings;
- firing order, timing, limiter, fuel, and flame-speed data;
- the M3 fixed crank-friction magnitude and exhaustive torque-loss classification;
- frozen reference excitation paths, ordering, gains, and delays.

Both authored and resolved profile validation check finite physical domains, stable
references, topology coverage, role-correct graph connectivity, duplicated-value
consistency, derived geometry and delay identities, restriction calibration and
coefficient identity, ordered tables, fuel/loss/excitation domains,
loss/capability agreement, method/profile identity, and provenance binding. The
exhaustive BMW M52 literal asset validator is intentionally deferred until the
canonical resolved BMW asset is implemented in M3; the normative values already live
in the M3 record, and this checkbox does not expose a partial validator under an
exact-sounding name.

That schema is not an implementation of `legacy_low_order_v1`. M3 begins only when the
later milestone implements the recorded mechanism, valvetrain, gas, ignition,
combustion, loss, torque, capture, and excitation behavior and drives the already
accepted M2 renderer from newly simulated state. It may not read the validation-only
audit buses or use a future phase to excuse a broken M2 renderer.

The profile name marks source-informed behavioral provenance, not a compatibility
mode, fallback codebase, or permission to preserve two implementations. Accepted
post-parity replacements follow the one-change listening gate in `MODEL.md` and remove
the superseded path.
