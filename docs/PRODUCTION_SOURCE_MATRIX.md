# Production offline source/render completeness matrix

Status: **frozen v1**

Decision owner: user

Approved: 2026-07-27 by explicit user confirmation in the project thread

Scope: M6 canonical offline source/render evidence; first implementation is the BMW
M52B28

This matrix prevents a good exhaust demo from being mislabeled as complete engine
audio. It freezes required route identities and evidence standards, not one permanent
algorithm. A better model may add physical fields or internal diagnostic substems that
do not change the frozen outward routes. Changing required route identity, cardinality,
or meaning—or deleting, merging away, or weakening a route—requires explicit user
approval and a new matrix revision.

Passing this matrix does not certify the later continuous audio atlas. M7 separately
owns the bounded RPM/load/state capture space, reconstruction representation,
interpolation/transitions, reference runtime, portability budgets, and platform
conformance. Several good WAVs are necessary M6 evidence, not proof of interactive
runtime behavior.

## Artifact ownership

- The offline simulator/baker owns physical observables, canonical high-fidelity
  source-route stems, scenario telemetry, and render evidence.
- The atlas baker owns compact reconstruction data, stable route mapping,
  capabilities, and provenance.
- A thin runtime owns deterministic reconstruction from live state into declared audio
  buses.
- The host game owns emitter placement, attenuation, occlusion, environment/reverb, and
  final platform mix.
- Raw and audition masters in this matrix are reproducible QA/listening derivatives.
  They are not authoritative runtime inputs and do not replace separately routable
  exhaust, intake, and mechanical outputs.

Canonical M6 evidence preserves every required route. A later platform representation
may encode, downmix, or reduce buses only when its capability declares the limitation
and maps every derived output back to stable canonical route IDs. That future platform
choice cannot retroactively make a missing canonical route complete.

## Route rules

- A **required** route has its own named stem on a declared common clock before it enters
  a parent bus or master. Time coherence preserves documented physical propagation
  delay and phase; it does not force zero-delay/zero-phase alignment. A route cannot
  exist only as an undocumented contribution to another stem.
- Each accepted route has a stable ID, role, default parent, polarity, gain, delay, and
  optional emitter anchor. Internal diagnostic splits may be added freely; changing a
  publicly routable output requires a capability/schema version.
- A route may be `not_applicable` only because a resolved engine topology or scenario
  truly lacks that source. The manifest must say why. “Not implemented” is not
  `not_applicable`.
- A physically quiet route remains present and correctly routed. A missing file,
  fabricated filler, or an always-silent placeholder does not pass completeness. An
  applicable route must make a credible, user-audible contribution in at least one
  deciding scenario; epsilon-level output does not count.
- Every deciding set includes each route solo, the complete calibrated mix, and a
  route-muted delta mix. Relative gains need physical/measurement/calibration
  provenance and cannot be turned down merely to hide a defective route. No universal
  loudness number substitutes for listening.
- Each stochastic term has a documented physical role, deterministic seed, expected
  behavior, and calibration. Generic noise is not a source model.
- A recorded component may be deliberately authored only with identity, rights,
  operating-condition, calibration, and routing provenance. It may not masquerade as
  simulated physics or silently replace a missing model.

## Physics and one-way presentation boundary

Intake/exhaust runner, plenum, header, collector, and boundary pressure-wave dynamics
are simulator physics upstream of `CaptureBlock`. Their pressure and backpressure can
affect cylinder filling, pumping work, and torque. A downstream IR, radiation filter, or
microphone projection cannot be used as evidence that these coupled physical waves were
modeled.

`CaptureBlock` publishes the named SI observables at explicit physical locations.
Excitation and radiation/presentation then consume them one-way. Presentation cannot
feed microphone, room, or arbitrary mastered audio back into engine physics.

## Mandatory M6 source routes and evidence outputs

| Route | Cardinality and boundary | Required evidence | Scenario applicability |
|---|---|---|---|
| `exhaust.outlet[*]` | Preserve per-cylinder/per-port observables through explicit physical header/collector wave routing; after `CaptureBlock`, emit one stem per resolved exterior outlet or independently radiating route. | Separately documented coupled pipe-wave/backpressure physics and one-way outlet excitation/radiation; SI pressure, temperature, signed mass flow, area, geometry, cylinder/phase identity, boundary conditions, and sourced/calibrated parameters required by those models. | Mandatory for the running BMW pull, held points, free rev, lift/overrun, fuel cut, limiter, startup catch, and rundown while the route is physically active. |
| `intake.inlet[*]` | Preserve runner/plenum/throttle/inlet observables through explicit physical induction-wave routing; after `CaptureBlock`, emit one stem per resolved independently radiating inlet. | Separately documented coupled induction-wave/filling physics and one-way inlet excitation/radiation; SI pressure, temperature, signed mass flow, effective areas, geometry, throttle/valve state, cylinder/phase identity, boundary conditions, and parameter provenance required by those models. | Mandatory for the same running scenarios. It may naturally become quiet on closed-throttle or stopped intervals, but cannot be omitted. |
| `mechanical.engine` | Preserve evidence-backed component excitations until their declared engine-local structural/radiating combination; emit at least one engine-mechanical stem and retain diagnosable component outputs during development. | Engine-specific mechanism inventory plus documented excitation and transfer models tied to applicable physical events such as cylinder force/pressure, reciprocating and rotational acceleration, valvetrain/contact events, and block/head/cover/accessory response with sourced or calibrated parameters. Vehicle mount-to-chassis/body radiation is outside this core route. A periodic pulse or broadband-noise bed alone does not qualify. | Mandatory whenever the engine is moving, including motored crank, fired operation, coast, fuel cut, limiter, and rundown. |
| `mechanical.starter` | Separate child stem before the mechanical parent/master when a starter is fitted and engaged. | Documented motor/engagement source or an explicitly authored, licensed, condition-matched recording; starter speed/engagement state and handoff timing remain observable. | Required for starter/crank/catch scenarios; explicitly `not_applicable` for an already-running dyno pull. |
| `master.engine.raw` | Deterministic, time-coherent default QA mix of every applicable required route, before audition-only gain or loudness matching. It is not the sole production output. | Complete routing graph, gains, polarities, physical/monitoring delays, filters, nonlinear stages, automation, channel layout, and parameter provenance. Independently located sources use a declared canonical engine-local virtual monitoring projection. The master must be reproducible from preserved stems plus the declared deterministic processing graph. | Mandatory for every rendered scenario. |
| `master.engine.audition` | Listening derivative of the raw master with declared edge fades and monitoring gain/normalization. | Exact transform and loudness/peak report; never used as the input to a later physical stage or mislabeled as raw. | Mandatory in each listening gate. |

The resolved topology determines counts. A single-intake engine may have one intake
stem; a multi-outlet exhaust may have several. The BMW reference oracle's two empirical
exhaust buses do not, by themselves, prove the physical production outlet topology.
BMW production authoring must source and declare that topology before this gate passes.

Before `mechanical.engine` can pass, the BMW has a reviewed inventory of materially
radiating engine-local mechanisms. Every entry is `modeled`, genuinely
`not_applicable`, or `deferred`; a material `deferred` mechanism blocks production
completeness. The inventory need not force one public stem per mechanism, but each
modeled contribution remains diagnosable during development.

## Conditionally mandatory topology routes

The naturally aspirated BMW marks these `not_applicable`. An engine containing the
hardware cannot pass by folding it invisibly into the core routes:

| Topology | Required result |
|---|---|
| Turbocharger | Separately inspectable compressor and turbine contributions, plus wastegate when fitted, before their declared intake/exhaust parent buses. |
| Supercharger | Separately inspectable compressor/drive contribution before its declared intake/mechanical parents. |
| Bypass or blow-off valve | Separate event-capable contribution when fitted and active, routed to its declared induction parent. |
| Other independently radiating engine hardware | Stable named route or child stem whenever its contribution is material to the claimed engine/build character. |

Adding a new topology family freezes its outward route contract, required observables,
evidence standard, routing, and scenario coverage through the same process as the core
routes. Its physical model remains versioned and replaceable behind those seams through
the documented invariant and listening gates.

## Controls and events are not substitute sound layers

Throttle, achieved load, RPM, ignition, fuel delivery, starter engagement, limiter
state, and shutdown are simulator/scenario state. Their audible consequences must
propagate through the required physical source routes. Fuel cut, limiter, or shutdown
must not be implemented merely by overlaying a generic one-shot sound.

An explicitly authored recording or event layer is possible only as a declared product
choice with provenance and listening approval. It does not excuse missing exhaust,
intake, or mechanical behavior underneath it.

For these offline scenarios, achieved load means the plan's signed
`net_bmep_pa`/`achieved_net_bmep_pa`; throttle remains separate transient context.
Manifests also record signed RPM slope. M7 later versions the host-facing normalized
coordinate and proves live interpolation; this matrix must still cover negative
motoring/coast load and both accelerating and decelerating transitions.

## Required operating coverage

| Behavior | Required route result |
|---|---|
| Full-load prescribed pull | Exhaust, intake, engine mechanical, raw master, and audition master are present and materially represented. |
| Low/mid/high RPM and achieved load | Same routes; their response changes coherently with signed `achieved_net_bmep_pa`, throttle context, and RPM rather than only gain or pitch shifting one clip. |
| Fired idle, low load, and idle recovery | Same routes remain stable and retain cycle character without generic looping, dropouts, or uncontrolled drift; return from a lift or startup catch is continuous. |
| Neutral/free rev | Same routes under the declared low-inertia/load scenario. |
| Throttle application and ordinary lift/overrun | Same routes remain continuous across positive/negative RPM slope and into signed coast/negative achieved load. |
| Explicit fuel cut | Distinct control state; source routes respond to changed combustion/gas exchange without disappearing from the package. |
| Starter/crank/catch | Starter child, engine mechanical, intake, exhaust, and masters appear when physically applicable; handoff is continuous. |
| Shutdown and ignition-off rundown | Main routes persist through evolving gas exchange and mechanical rundown until they naturally become silent. |
| Limiter entry/sustain/exit | Declared limiter control changes the normal routes; no generic limiter overlay substitutes for them. |

## Evidence required to claim a route complete

Each mandatory route must have all of the following:

1. a `MODEL.md` equation or method record supported by authoritative engineering
   literature, validated measurements/an identified dataset, or the explicitly
   permitted recorded-component path;
2. an explicit simulator-observable-to-excitation mapping with units, signs, sample
   locations, rates, and channel identities;
3. provenance and distributable rights for physical data, acoustic assets, recorded
   components, inferred values, calibration, and artistic choices;
4. deterministic seed and state ownership;
5. source, propagation/radiation, routing, and presentation boundaries that can be
   inspected independently;
6. finite/material-contribution, timing, continuity, clipping, DC, phase, and routing
   checks;
7. raw stems, the raw master, separately labelled level-matched audition copies, a
   manifest, elapsed time, and hashes;
8. controlled route-solo, full-mix, and route-muted-delta listening accepted by the
   user.

Tests and metrics can reject a route but cannot certify that it sounds correct.

## Engine-asset exclusions

The following do not enter the engine master unless a later user-approved product
matrix adds them:

- gearbox, differential, clutch, and other drivetrain radiation;
- tires, road, suspension, and aerodynamic noise;
- vehicle mount-to-chassis/body radiation;
- cabin, world/environment reverberation, occlusion, distance attenuation, and final
  game mix;
- vehicle-speed behavior masquerading as engine RPM/load behavior.

Gear, clutch, and shift events may arrive as optional host context for interpreting
load or selecting engine transitions; they do not make drivetrain radiation part of
the engine-audio capability. A later `powertrain` capability may own drivetrain
sources without making them hidden dependencies of this engine asset.

Optional cabin/exterior perspectives may later be declared presentation capabilities
or presets. They cannot replace the physical core routes or take ownership of the
host's dynamic occlusion, environment, and final mix.

## Approval requested

Freezing this proposal means approving:

1. this as the canonical M6 offline source/render gate, with the continuous atlas
   runtime remaining a separate M7 gate;
2. exhaust, intake, and engine mechanical as non-optional source routes, plus the
   raw/audition masters as non-optional M6 QA outputs for a running BMW;
3. starter as a separately inspectable mechanical child route when that scenario is
   rendered;
4. physical intake/exhaust waves remaining upstream of one-way excitation/presentation;
5. state changes flowing through those routes instead of generic one-shot substitutes;
6. drivetrain and environment remaining outside the engine asset;
7. a route being complete only when its model evidence, materially audible contribution,
   solo/mute/full-mix evidence, routing checks, and user listening all pass.
