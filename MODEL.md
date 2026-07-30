# Engine Sim Offline model contract

Status: normative pre-implementation record

Applies to: M2 renderer, M3 BMW parity, M4 operating-point test cells, and the
admission of later fidelity upgrades

Date: 2026-07-28

## 1. Purpose and claim boundary

This document defines what the simulator means before physics implementation begins.
It fixes the physical/numerical boundaries, signs, clocks, model maturity, failure
semantics, and evidence rules that code must obey.

The project deliberately has three different claims:

| Gate | Claim |
|---|---|
| M2 | A clean renderer reproduces the frozen, narrow P1.8 presentation route from captured pre-DSP buses. It makes no new engine-physics claim. |
| M3 | A source-informed clean simulator reaches an audible BMW baseline comparable to the liked engine-sim oracle. It is a low-order parity model, not an offline-fidelity result. |
| M5/M6 | One accepted approximation at a time is replaced by an established, better-supported model, and the complete required production routes are accepted by listening. |

M3 does not introduce a new combustion law, wall-heat correlation, gas formulation,
friction model, pressure-wave solver, and source model simultaneously. It first
re-expresses the behavior that produced the known-good baseline behind explicit
boundaries. Otherwise a failed render would again be impossible to diagnose.

No model is called “higher fidelity” because it costs more or cites a paper. The claim
requires applicable inputs, numerical verification, validation evidence appropriate to
the intended use, controlled A/B renders, acceptable performance, and user listening.

## 2. Authorities and contradiction rule

The following records have distinct authority:

1. `PLAN.md` owns scope, milestones, performance limits, and listening stops.
2. `reference/oracles/bmw-m52b28/SOURCE_MATRIX.md` owns the frozen M2/M3 reference
   route and omissions.
3. `docs/PRODUCTION_SOURCE_MATRIX.md` owns production source completeness.
4. The BMW fixture `manifest.json` owns resolved reference values, identities, seeds,
   timing, and hashes.
5. `reference/oracles/bmw-m52b28/PROVENANCE.md` owns source lineage, rights, evidence,
   and unknowns.
6. `P18_PRESENTATION_RENDERER.md` owns exact P1.8 reference-renderer behavior.
7. `docs/model/M2_P18_ARTIFACT_MASTERING.md` owns the repository-selected artifact
   paths and exact raw/audition mastering behavior derived from the frozen stems and
   oracle.
8. `docs/contracts/M2_MANIFEST_INPUTS.md` records the historical typed distinction
   between complete simulation inputs and isolated reference-presentation lineage; it
   is evidence history, not a current API alternative.
9. `docs/contracts/M3_BMW_REQUEST.md` owns the concrete resolved engine/scenario
   identities, fixed-rate RPM representation, and explicit parity-scenario metadata.
10. `docs/contracts/M4_SIMULATION_MANIFEST_WIRE.md` owns the complete current
    simulation-manifest byte grammar, including the resolved randomness policy.
11. `docs/model/M4_OPERATING_POINT_MODEL.md` owns exact cycle quadrature, held-speed
    torque-accounting, convergence, and applicability rules introduced by M4.
12. This document owns model meaning and admission; the focused contract records own
    concrete C++ types, schemas, and API signatures.

If two authorities conflict, implementation stops and the contradiction is recorded.
Code must not silently choose the easier interpretation. The liked oracle is
behavioral evidence, not proof that its parameters or algorithms represent a physical
BMW accurately.

## 3. Provenance vocabulary

Every resolved model parameter has one of these origins:

| Origin | Meaning |
|---|---|
| `literature` | Taken from an identified primary or authoritative technical source within its stated domain. |
| `bmw_official` | Published by BMW for an identified engine/vehicle variant. |
| `measurement` | Direct observation with the instrument, calibration, conditions, uncertainty, and sample identity recorded. |
| `inferred` | Estimated from identified evidence by a recorded inference whose ambiguity and uncertainty remain explicit. |
| `reference_fixture` | Directly observed or resolved in the frozen P1.8 capsule. |
| `legacy_asset_unverified` | Present in the engine-sim asset but lacking demonstrated physical provenance. |
| `derived` | Calculated from identified inputs with the equation recorded. |
| `scenario` | Chosen operating/test-cell condition, not engine identity. |
| `calibrated` | Fitted to identified physical evidence for a stated domain, objective, and uncertainty. |
| `calibrated_m3` | Fitted only to reach the narrow M3 parity target. |
| `artistic` | Deliberate presentation choice with no physical claim. |

`calibrated_m3` never becomes physical BMW data merely because it sounds acceptable.
`legacy_asset_unverified` never becomes `bmw_official` through repeated use. A scalar
without an origin is invalid input.

For each admitted subsystem, its record states:

- stable method ID and version;
- intended domain and explicit omissions;
- equations and source;
- whether the engine-sim concept is reproduced, re-expressed, or replaced;
- state, initialization, units, signs, clocks, and update order;
- parameter origins;
- numerical method, tolerances, and failure behavior;
- published observables and any observable-to-excitation mapping;
- verification, validation, and listening evidence.

## 4. Boundary and dependency direction

```text
scenario and resolved engine data
              |
              v
coupled engine physics
  mechanism / valves / gas / combustion / torque
              |
              v
CaptureBlock
  timestamped named SI observables at physical locations
              |
              v
excitation
  observable -> uncalibrated route source
              |
              v
one-way radiation and presentation
  source conditioning / exterior projection / coloration
              |
              v
delivery
  crop / calibration / stems / masters / serialization
```

Intake, runner, primary, collector, and junction pressure waves are upstream physics
when they can affect filling, residual gas, backpressure, or torque. They are not
presentation reverb. Exterior radiation may be physical but is one-way downstream.
Microphone response, static IR coloration, listening gain, and mastering are
presentation and never feed back into physics.

`CaptureBlock` is the final bidirectional-physics boundary. It is also the replacement
seam: an accepted downstream renderer remains fixed while an upstream physical model
is changed, or accepted upstream captures remain fixed while one downstream stage is
changed.

No public/core API accepts an undocumented “engine sound” scalar. Stable per-cylinder,
per-port, and per-route identities survive until an explicit aggregation stage.

## 5. Units and signs

Internal physical quantities use SI as defined by the
[BIPM SI Brochure](https://www.bipm.org/en/publications/si-brochure):

- pressure in Pa, explicitly named `_pa_abs` or `_pa_gauge`;
- temperature in K;
- mass in kg, amount only where a declared molar model requires mol;
- length in m, area in m², volume in m³;
- time in s, frequency in Hz, angle in rad;
- energy in J, power in W, torque in N·m;
- signed mass flow in kg/s along a named edge direction.

Global unwrapped crank angle `theta_rad` increases in the engine's running direction.
Telemetry may additionally expose `theta_cycle_rad = wrap(theta_rad, 4*pi)`. For
cylinder `i`, local four-stroke angle is:

```text
theta_i = wrap_4pi(theta_rad - firing_offset_i)
```

For newly admitted engine definitions, firing TDC is local zero. The nominal local
intervals are power `[0, pi)`, exhaust `[pi, 2*pi)`, intake `[2*pi, 3*pi)`, and
compression `[3*pi, 4*pi)`. Valve overlap and spark are angle events and may cross
these descriptive boundaries. The frozen M3 asset is an explicit parity exception:
its ignition origin and geometric piston TDC differ by 30 crank degrees. Its companion
derives piston phase from crank/journal geometry rather than from ignition wiring and
records both identities.

Positive edge flow follows the edge's declared source-to-sink direction. Positive heat
enters the modeled gas. Positive boundary work is work performed by the gas on the
mechanism. Positive shaft torque drives increasing crank angle. These signs are
serialized, never inferred from a field name such as `load`.

## 6. Time, events, and capture semantics

Simulation time is independent of wall time. Each scheduled fixed-rate control,
capture, and output clock uses an integer/rational index grid; clocks do not advance
through repeated floating addition in new production models. Adaptive internal solver
endpoints need not lie on a fixed-rate grid, but their requested observation/event
boundaries do. Half-open intervals are used throughout.

- Scenario/control state is right-continuous and applies to the segment beginning at
  its boundary.
- An angle event in a new admitted model occurs when its unwrapped location lies in
  `(theta_old, theta_new]`. Equality on wrapped angles is forbidden.
- A new model splits a step at each known discontinuity. M3's source-parity ignition
  quantization is an explicit exception: it detects the crossing after the 10 kHz
  mechanism step and applies ignition before that step's gas substeps, matching the
  reference update order. Event-split ignition is a later isolated integrator change.
- Simultaneous events use a documented stable priority and stable identity.
- Warm-up and pre-roll are causal state history. Audible cropping never resets a
  delay, random stream, filter, integrator, or controller.
- M3 publishes its reference-parity capture at exactly 10,000 Hz, post-step. Record
  `k`, beginning at zero, represents state after advancing from `k/10000 s` to
  `(k+1)/10000 s` and is timestamped `(k+1)/10000 s`. That convention matches the
  fixture's post-step meaning even though the historical timestamp values themselves
  used repeated addition and new internal substeps may later be finer.
- M3 initializes the ignition saved-angle state from the declared angle at `t=0` and
  preserves the source's endpoint rule. In the normal reference direction the
  step-quantized interval is `[theta_saved, theta_current)` after wrap adjustment; in
  reverse it is `[theta_current, theta_saved)`. Thus an event exactly at the saved
  initial angle fires on the first step that moves away from it, while equality at the
  new endpoint waits for the next step.

The historical fixture retains its recorded repeated-addition timestamps and unusual
post-step meaning for parity. New simulation clocks use integer indices; they do not
copy that timestamp implementation.

At one scheduled timestamp, the stable outer priority is: apply right-continuous
scenario transitions; resolve an imposed trajectory or advance the sole dynamics
owner; update mechanism/control state; dispatch model events by event class and stable
component identity; complete admitted physics substeps; publish capture/output
records. A model-specific companion may refine a class but cannot reverse this outer
order silently. M3's exact staggered exception and cylinder/route traversal are fixed
in its companion record.

## 7. Numerical and determinism contract

The numerical baseline follows IEEE 754 binary64 semantics
([IEEE 754-2019](https://standards.ieee.org/ieee/754/6210/)):

- round to nearest, ties to even;
- no fast math, reassociation, or implicit FMA contraction;
- stable traversal and reduction order;
- finite input/state/output at every public boundary;
- explicit, method-specific subnormal handling;
- no hidden dependence on wall-clock order or callback partitioning.

Determinism means identical inputs, seed, model/profile IDs, build, compiler,
instruction set, math libraries, and declared thread topology produce identical
payload artifacts and the same content-identity portion of the render manifest.
Cross-toolchain byte identity is not claimed until demonstrated. A render manifest
records that determinism envelope. Wall elapsed time, host identity, utilization, and
other measured execution facts belong to a separate performance report, or to a
clearly marked non-reproducible manifest section excluded from content hashes and
deterministic comparisons. They cannot alter rendered samples.

Randomness uses a versioned project-owned generator, domain-separated seeds, and stable
semantic component IDs. A component owns its stream. Rejected/adaptive solver attempts
must not consume accepted-path random draws. Parallel scheduling cannot change stream
ownership or reduction order. The historical isolated M2 replay recorded
`p18_reference_pcg32_v1`; it is evidence, not a current method. Simulation renders
instead admit the exact
project-owned `pcg32_xsh_rr_64_32_binary64_v1` generator and
`sha256_length_prefixed_capture_component_pcg32_v1` derivation. Admission recompiles
the complete initialized stream inventory from the explicit seed namespace, scenario
public seed, and stable component topology; retained BMW combustion values are cache
values that must match, not a second seed authority. Canonical plan order and seed
coordinates use stable numeric owner IDs, not mutable container ordinals. The current
combustion and conditioning methods record their lanes even when a coefficient is
zero because those executors still instantiate them. Presentation advances both
route-owned generators; combustion draws only for accepted ignition events. Runtime
draw count and cadence are execution evidence, not claims made by the preflight plan.
Every later stochastic method declares a separate identity and draw cadence when its
behavior differs.

At render-session start, the implementation verifies round-to-nearest/ties-to-even.
Flush-to-zero and denormals-are-zero are disabled unless a named admitted operation
owns an explicit cleanup rule. The effective floating-point, instruction-set, libm,
compiler, and contraction environment is recorded.

One universal integrator is not imposed. Every admitted model declares whether it uses
a fixed explicit step, event-split step, implicit solve, root localization, or another
method. Adaptive solvers must define absolute/relative tolerances per state scale,
accepted/rejected-step semantics, maximum work, and terminal failures. Guidance such
as the [SUNDIALS usage notes](https://computing.llnl.gov/projects/sundials/usage-notes)
informs solver operation; it does not select a solver by itself.

Numerical quality profiles adjust resolution/tolerance within one model and code path.
They cannot silently select a different physical model. A state that becomes
non-finite, has non-positive required mass/internal energy/volume, or persistently
fails convergence terminates the render with context. Hidden “make it survive” clamps
are forbidden. A reproduced legacy bound is allowed only when named as part of the M3
behavioral model and tested at that seam.

Supported caller sink chunking and concurrent job scheduling cannot change simulation
samples, telemetry, stems, or masters. A method may own a normative internal partition
when arithmetic depends on it—for example M2's frozen 3,840-source-frame presentation
blocks—but that partition is not inferred from caller callback sizes.

The M2 scheduler resolves each binary64 scenario boundary to an integral frame index
once and then operates exclusively on integer indices. Its plan and cursor are
constant-sized with respect to render duration. A method-owned capture partition is
distinct from the scenario transport capacity: the former fixes arithmetic/callback
boundaries, while the latter bounds borrowed frame and event-journal storage. The
current session scheduler is serial. Cancellation is polled only between complete
method blocks, including once after the final block before publication.

## 8. Scenario and torque semantics

Exactly one owner advances or imposes crank motion:

```text
net_engine_torque + actuator_torque
    = equivalent_inertia(theta) * angular_acceleration
      + 0.5 * d(equivalent_inertia)/dtheta * angular_speed^2
```

For a constant equivalent inertia, the derivative term is zero. `net_engine_torque`
includes gas, pumping, friction, accessory, and starter contributions as separately
reported terms, but excludes the test-cell/load actuator. Starter torque is an engine
auxiliary contribution, not dyno load. Dyno reaction torque is the negative of
actuator torque.

| Scenario | Crank-motion owner | Load/actuator meaning |
|---|---|---|
| prescribed trajectory | The scenario supplies `theta`, `omega`, and `alpha`; no crank dynamics integrator also advances them. | Actuator torque is the algebraic residual required to impose that trajectory. |
| held-speed/load-target | The scenario holds `theta`/`omega` on its declared speed trajectory while the controller searches a bounded engine actuator such as throttle. | Test-cell actuator torque is the algebraic residual; target reachability is reported separately. |
| inertial dyno | The dynamics integrator alone advances `theta` and `omega`. | Declared brake/resistance supplies actuator torque; no trajectory simultaneously overwrites motion. |
| free engine | The dynamics integrator alone advances `theta` and `omega` from engine torque and the mechanism's configuration-dependent crank-referred inertia. | An optional nonnegative external resisting-torque trajectory is subtracted from engine torque; omission means positive zero. |

A prescribed-RPM sweep therefore measures the actuator needed to impose the path; it
does not directly measure a steady torque curve.

For a four-stroke cycle:

```text
cycle_work_j = integral_over_4pi(net_shaft_torque_nm dtheta)
cycle_mean_net_torque_nm = cycle_work_j / (4*pi)
net_bmep_pa = cycle_work_j / total_displacement_m3
            = 4*pi*cycle_mean_net_torque_nm / total_displacement_m3
instantaneous_power_w = net_shaft_torque_nm * angular_speed_rad_s
cycle_mean_power_w = cycle_work_j / (t1 - t0)
                   = cycle_mean_net_torque_nm * (4*pi/(t1 - t0))
```

Only at steady speed may the last factor be replaced by the steady RPM-derived angular
speed. Pumping torque is a diagnostic partition of full-cycle indicated gas torque,
never an additional shaft term.

Telemetry distinguishes:

- instantaneous indicated gas torque;
- pumping work/torque, derived from the declared gas-exchange portion;
- friction/pump/accessory torque;
- starter torque;
- instantaneous net shaft torque;
- cycle-mean net shaft torque;
- actuator torque and dyno reaction.

Full-cycle cylinder `p dV` already includes gas-exchange pumping. It must not be
subtracted a second time. Bare `load` is not an internal physical variable. Control
uses signed `target_net_bmep_pa` and reports `achieved_net_bmep_pa`.

The four scenario modes in `PLAN.md` are semantic variants, not flags that can be
combined arbitrarily. A load-target request either reaches its target within declared
tolerance or returns an unreachable result with the nearest achieved state; it never
pretends a saturated throttle solved the request.

## 9. M2 reference presentation model

M2 bypasses physics and reads only the two
`legacy_reference.exhaust_bus_pre_dsp` fixture lanes through a test-only adapter. The
complete normative renderer is
[`P18_PRESENTATION_RENDERER.md`](reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md).
The implemented fixture-free source-stage boundary and ownership rules are recorded
in [`M2_P18_SOURCE_STAGE.md`](docs/model/M2_P18_SOURCE_STAGE.md).
The strict configured-IR decode and exact static conversion boundary are recorded in
[`M2_P18_IR_CONVERSION.md`](docs/model/M2_P18_IR_CONVERSION.md).
The isolated fixed-topology transform, immutable configured-IR spectrum, and causal
overlap-save ownership boundary are recorded in
[`M2_P18_CONVOLUTION.md`](docs/model/M2_P18_CONVOLUTION.md).
Its output unit remains `engine_sim_source_unit`, not Pa or SPL.

The M2 route includes the exact causal reconstruction, stochastic conditioning,
derivative/noise mixture, configured IR, calibration, semantic stems, and listening
master. These are behavioral-reference operations. In particular:

- jitter, derivative mixture, air noise, and the configured IR are empirical/artistic
  conditioning;
- pressure weights, delay, and legacy route gains are upstream in the frozen
  observable-to-excitation stage and are already present in M2's captured audit buses;
- `smooth_39.wav` is unknown-provenance presentation coloration;
- the two routes are runtime reference buses, not demonstrated exhaust outlets;
- passing M2 validates renderer behavior and audibility, not engine physics.

The audit schema/reader and its fixture adapter exist only in a reference/test target.
The internal renderer seam accepts the typed, callback-scoped
`ExhaustExcitationBlockView` required by the frozen source matrix: M2's test adapter
and M3's physical excitation stage both drive that one stateful session
implementation. The public `render` API, CLI, engine/scenario specification, and
production manifest cannot accept an audit file or a caller-supplied prebuilt
excitation bus. M3 and production targets link neither the audit reader nor its
adapter. An external comparison executable may read both sets of outputs but may not
feed M3 from the audit lane.

## 10. M3 source-informed parity model

M3 is the minimum complete upstream path required to drive the accepted M2 renderer
from newly simulated state. It preserves the source-informed low-order behavior that
matters to the oracle, while putting every approximation behind a replaceable seam.
Its stable method ID is `legacy_low_order_v1`. The exact initialization, equations,
constants, event rules, state transitions, and operation order are normatively closed
by [`docs/model/M3_PARITY_MODEL.md`](docs/model/M3_PARITY_MODEL.md); this section fixes
the architectural meaning and admission boundary. A quality profile cannot substitute
another method ID. “Legacy” identifies the behavioral provenance; it is not copied
engine-sim code, a compatibility API, a runtime fallback, or a second maintained
implementation. It is the sole M3 kernel, and accepted subsystem replacements delete
the superseded implementation.

The reference schedule is fixed:

```text
mechanism/control rate = 10,000 Hz
mechanism step = 100 us
gas substeps per mechanism step = 8
gas substep = 12.5 us
CaptureBlock publication = once after the complete mechanism step
```

For each 10 kHz step, M3 applies the right-continuous scenario controls, advances the
prescribed/analytic mechanism state, resolves the direct throttle, updates filtered
RPM, detects ignition crossings, updates cylinder volume/history and valve
conductance, then performs eight gas substeps. Within each gas
substep it processes collectors in route order, the shared intake, then chambers in
stable cylinder order. Each collector resets its finite ambient boundary, evaluates
the outlet edge, applies an excess-velocity bound, and performs its
directional-dynamic-pressure momentum update. The intake evaluates its main and idle
ambient edges in that order, then applies its excess-velocity bound and the same class
of momentum update.

A chamber substep applies wall heat and blowby first. It then evaluates
plenum-to-runner, intake-valve, exhaust-valve, and primary-to-collector edges in that
order. The source excess-velocity bounds are interleaved after those edges exactly as
specified in §10.4; runner, cylinder, and primary directional-dynamic-pressure updates
follow. The reference damping, intake-flow flame cancellation, flow accounting, and
combustion progress occur last. The reference capture/excitation is evaluated only
after all eight substeps. State is not reset between control blocks or at the audible
crop.

M3 starts from the declared fresh-process state and simulates the complete one-second
bootstrap plus one-second pre-roll before the retained 15-second interval. The fixture
does not contain enough internal gas, flame, mechanism, or random state to initialize
M3 at audible frame zero. A later snapshot shortcut is admissible only after it
serializes all causal state and is demonstrated equivalent.

For the canonical parity/isolation run only, the physical lane's full 170,000-record
post-step engine-speed sequence is the prescribed kinematic trajectory; crank angle is
advanced from the declared initial phase by the companion record's fixed-step rule and
checked against the captured angle. This is ordinary scenario input, not pressure,
excitation, or audit-bus replay. It intentionally removes the legacy starter/dyno/
constraint controller from the comparison so the clean simulator's newly generated
gas terms align sample-for-sample. General authored prescribed trajectories use the
same public scenario semantics and do not depend on the fixture.

### 10.1 Mechanism

The generic legacy constraint graph is deliberately replaced by an analytic rigid,
centered slider-crank with no pin offset, deformation, or torsional modes. This is the
one controlled M3 departure from the source's warm-started iterative constraint solve;
it is not described as numerically parity-neutral. It removes constraint drift and
the solver reaction formerly consumed by piston-wall friction. Before valve or gas
work is admitted, an isolated mechanism gate checks cylinder volume, `dV/dtheta`,
dead-center phase, firing offsets, initial phase, and the prescribed angle/RPM
trajectory. A failed gate stops M3 rather than being compensated downstream. For bore
`B`, crank radius `r`, rod length `l`, piston area `A_p = pi*B^2/4`, crank reference
`theta_tdc`, and journal angle `theta_journal_i`:

```text
theta_geometric_tdc_i =
    wrap_2pi(theta_tdc + theta_journal_i - pi/2)
phi_i = wrap_2pi(theta_cycle - theta_geometric_tdc_i)

s(phi_i) = r*cos(phi_i) + sqrt(l^2 - r^2*sin(phi_i)^2)
x(phi_i) = r + l - s(phi_i)              # piston travel down from TDC
V(phi_i) = V_clearance + A_p*x(phi_i)

dx/dphi_i =
    r*sin(phi_i)
    + r^2*sin(phi_i)*cos(phi_i)
      / sqrt(l^2 - r^2*sin(phi_i)^2)
dV/dtheta = A_p*dx/dphi_i

gas_torque_i = (p_cylinder_abs - p_crankcase_abs) * dV/dtheta
```

This is a controlled analytic replacement around the same asset geometry, not an
empirical sound generator. The fixed BMW geometry remains
`legacy_asset_unverified` for parity. The asset derives a compression ratio near
11.102:1. That does not match the commonly reported 10.2:1 nominal value, whose
primary provenance is not yet admitted here; BMW's archive does at least identify the
[E36 328i as a 2,793 cc inline six](https://www.bmwgroup-classic.com/en/models/bmw-classics/product-description-page.ad-464-10.bmw-328i-e36.html).
M3 does not silently correct the asset. Variant selection, compression-ratio
correction, and VANOS are later isolated changes.

The source applied cylinder pressure as piston force inside its constraint solve; it
did not evaluate the analytic torque equation above. M3 therefore treats that equation
as the clean mechanism's post-step indicated-torque definition, not a claim of
source-dyno torque parity.

For non-prescribed FreeEngine scenarios, engine inertia is not authored again in the
scenario. The versioned
`centered-slider-crank-cycle-mean-equivalent-inertia-v1` method evaluates 4,096
uniform midpoint samples over one slider-crank revolution. It adds the authored crank
inertia once to the full-cycle mean piston translation, connecting-rod center
translation, and connecting-rod rotation kinetic-energy contributions. This produces
one constant crank-referred cycle-mean reference without introducing a general
constraint solver solely to identify and validate the authored mechanism.

The optional scenario `attached_inertia` is a nonnegative crank-referred addition and
defaults to canonical positive zero. The compiler resolves

```text
free_engine_total_inertia
    = engine_baseline_inertia + attached_inertia
```

under the versioned `free-engine-equivalent-inertia-sum-v1` method, and the runtime
retains that total as an exact cycle-mean reference. During free-running motion the
runtime instead evaluates the centered mechanism's analytic `M(theta)` and
`dM/dtheta`, adds the constant attachment to `M(theta)`, and solves
`Q = M(theta)*alpha + 0.5*dM/dtheta*omega^2` at each left boundary. The optional
`external_resisting_torque` trajectory likewise defaults to one right-continuous
positive-zero point. It is an external test-rig load, not a second engine-friction
term.

For the BMW M52B28 fixture, the engine-derived baseline and neutral total are both
`0.2108686520185204 kg*m^2`, and neutral external resistance is zero. The regression
smoke for the interactive recipe requires its first 7,000-rpm crossing in
`0.44`--`0.50 s`. Its short part-throttle preparation differs from the controlled
pristine ablation and therefore guards that recipe rather than claiming transient
parity. The controlled pristine gate separately holds the same source boundary
conditions and now places WOT within `0.0130 s`, every coast crossing within
`0.0049 s`, and long natural-balance mean within `1.182 RPM`. Gas-exchange pumping is
already part of indicated cylinder pressure-volume work; it is not a missing second
loss term. Pristine crank friction and piston-wall friction are the sole applied
FreeEngine loss authority; the generic one-cycle-lagged Chen--Flynn result remains
cycle evidence only.

### 10.2 Fixed valvetrain and conductance

M3 preserves the asset's 100-step harmonic-cam construction—one center sample plus
99 symmetric pairs, 199 stored samples—and its triangle interpolation. Evaluating the
analytic curve directly would change valve effective conductance and therefore the
sound; that is a later isolated replacement. The baseline has no lash, compliance,
bounce, hydraulic element, or variable cam timing. The authored table generator uses
maximum lift `L_max`, 50-thousandths lift `L_50 = 1.27 mm`, lobe exponent
`gamma_lobe`, 50-thousandths duration `D_50`, and `N=100`:

```text
D_50_crank_rad = D_50_crank_deg*pi/180
angle_50_cam_rad = D_50_crank_rad/4
s = (2*L_50/L_max)^(1/gamma_lobe) - 1
k = acos(s) / angle_50_cam_rad
extent = pi/k
sample_step = extent/(N - 5)

sample[0] = (0, L_max)
for i in 1..N-1:
    x = i*sample_step
    y = 0                                      when x >= extent
        L_max*(0.5 + 0.5*cos(k*x))^gamma_lobe otherwise
    add (-x, y) and (+x, y)
```

The frozen BMW asset uses `L_max=9 mm`, `L_50=1.27 mm`,
`D_50=210 crank-deg`, and `gamma_lobe=0.8`. Runtime cam lift is the frozen
radius-`sample_step` triangle-weighted interpolation of that generated grid, including
its endpoint behavior. The separate pinned intake/exhaust flow-versus-lift tables use
their own 1 mm-radius triangle interpolation. Those tables are effective flow-bench
conductances, not geometric port areas and not verified BMW data.

M3 retains the legacy source-unit SCFM definition and reference pressure-drop
conversions exactly for parity. It does not substitute a generic volumetric SCFM
conversion. The next contract commit records the resolved tables and converted
conductances as data with provenance.

The BMW direct-throttle path is also frozen behavior, not a generic throttle-area
model. For requested command `u`:

```text
linkage_position = 1 - u^2
plate_position = 0.994 * linkage_position
main_flow_multiplier = cos(pi * plate_position / 2)
```

The main 500-source-cfm restriction and parallel 0.1-source-cfm idle restriction are
then evaluated independently. At `u=0.85`, `plate_position=0.275835` and the main
multiplier is approximately `0.907593272464`. A later physical throttle model requires
actual bore/plate geometry and discharge data; it cannot reinterpret `u` silently.

### 10.3 Gas topology and low-order state

The M3 topology is:

```text
ambient
  -> throttle plus parallel idle bypass
  -> shared intake plenum
  -> six runner volumes
  -> six intake restrictions
  -> six variable-volume cylinders
  -> six exhaust restrictions
  -> six primary volumes
  -> two collector volumes

collector outlet edge is bidirectional;
its declared positive orientation is ambient(endpoint 0) -> collector(endpoint 1)
```

The M3 gas state intentionally reproduces the source-informed calorically perfect
pseudo-gas representation: amount, thermal energy, fixed degrees of freedom,
fuel/inert/O2 molar fractions, volume, and a two-component planar bulk-momentum vector.
Every finite gas volume owns that vector: the plenum, all runners, cylinders,
primaries, and both collectors. The shared plenum materially uses both dimensions
because ambient entry is along `(0,-1)` while runner departure is along `(1,0)`;
collinear cells still retain both components. Static pressure and temperature derive
from that state. Transport is bidirectional and transfers amount, mixture, thermal
energy, and momentum in stable edge order.

With molar amount `n`, stored thermal energy `E`, volume `V`, five degrees of freedom,
and the legacy air molar mass:

```text
gamma = 1.0 + (2.0/5.0) = 1.4
p_static_abs = E / (0.5 * 5 * V)
temperature = E / (0.5 * 5 * n * R)
mass = n * M_air
```

Bulk kinetic energy is tracked through momentum but is not included in the pressure
equation. A volume change applies the source-informed work update before publishing
the new pressure. The exact operation order, mixture reaction, and explicit legacy
energy/mole bounds are part of the M3 method version.

This is not yet the later production thermodynamic claim. M3 retains identified
legacy bounds/update semantics required for parity instead of mixing in a new
conservative control-volume formulation. A later control-volume replacement must use
explicit mass/species/internal-energy balances such as those documented for an
[ideal-gas control-volume reactor](https://cantera.org/stable/reference/reactors/ideal-gas-reactor.html),
and must be accepted separately.

### 10.4 Compressible restrictions and planar momentum

M3 retains the source-informed reversible compressible restriction relation with the
higher endpoint driving-pressure side selected as upstream, explicit
choked/subcritical branches, and zero flow at equal pressure. For each endpoint, the
driving pressure is its static pressure plus the directional dynamic-pressure proxy
facing inward along that endpoint's signed edge direction; neither endpoint silently
uses static pressure alone. The restriction coefficient is the converted flow-bench
conductance, not a silently invented port area.

For upstream static-plus-directional pressure `p_u`, downstream pressure `p_d`,
upstream temperature `T_u`, ratio `r=p_d/p_u`, and `gamma=1.4`:

```text
r_critical = (2/(gamma + 1))^(gamma/(gamma - 1))

F(r) =
    sqrt(gamma)
    * (2/(gamma + 1))^((gamma + 1)/(2*(gamma - 1)))
        when r <= r_critical

    sqrt(fmax(
        (2*gamma/(gamma - 1))
        * r^(1/gamma)
        * (r^(1/gamma) - r),
        0
    ))
        otherwise

molar_flow_rate = k_flow * p_u / sqrt(R*T_u) * F(r)
```

The substep amount is `dn = dt*molar_flow_rate`, explicitly bounded to
`[0, 0.9*n_upstream]` by the M3 parity method. That 90% bound is a legacy numerical
behavior, not physical validation. The source computes a two-volume
pressure-equilibrium candidate but does not apply it; M3 must not accidentally turn
that dead calculation into a second bound. Transferred mixture and per-mole thermal
energy come from the selected upstream volume. Proportional source momentum is
transferred, then directional momentum associated with the displaced fraction volume
is added at each side's declared cross-section, capped at that volume's sound speed.
Bulk-energy changes are reconciled into stored thermal energy in the same stable
order. Negative stored energy is explicitly floored only in this named parity
operation.

The baseline must retain this named planar bulk-momentum state in every finite gas
volume. A scalar zero-dimensional pressure state cannot reproduce intake turning or
the fixture's distinct forward/reverse directional dynamic pressures. Reconstructing
velocity as `mass_flow/(density*area)` would be a new model, not parity.

For a directional component `u_dir > 0`, the published parity proxy is the
isentropic stagnation-minus-static relation:

```text
a^2 = gamma * p_static_abs / density
M^2 = u_dir^2 / a^2
q_directional =
    p_static_abs
    * ((1 + 0.5*(gamma - 1)*M^2)^(gamma/(gamma - 1)) - 1)
```

The M3 five-degree-of-freedom gas evaluates the exponent through its frozen ordered
polynomial/square-root form. The relation itself is consistent with the
[NASA isentropic-flow equations](https://www.grc.nasa.gov/www/k-12/airplane/isentrop.html);
the lumped momentum evolution remains a `reference_fixture` behavioral proxy, not a
validated pipe-wave model.

Momentum transfer, the self directional-dynamic-pressure impulse, sonic limiting, and
decay are individually named parity operations. The self impulse integrates only the
opposed directional dynamic-pressure proxies on the rectangular control-volume faces;
it is not a static-pressure gradient solve. These operations cannot be “simplified”
before M3 listening. The intended M5 replacement is a coupled one-dimensional
compressible pipe/junction/boundary model, not algebraic decoration of a scalar
pressure trace.

The chamber edge/limit order is frozen:

```text
plenum -> runner flow
bound runner excess velocity
runner -> cylinder flow
bound runner, then cylinder excess velocity
cylinder -> primary flow
bound cylinder, then primary excess velocity
primary -> collector flow
directional-dynamic-pressure update runner(beta=0.1),
    cylinder(beta=0.5), primary(beta=1)
linear velocity decay cylinder, then primary, each tau=0.01 s
```

Each excess-velocity bound converts removed bulk kinetic energy into stored thermal
energy immediately. It is not a single sonic clamp after the dynamic-pressure update.
Collectors and the intake perform their own edge, excess-velocity, and
directional-dynamic-pressure operations earlier in the same gas substep. The runner
factor is `beta=0.1`, the primary and collector factors are `beta=1`, and the
intake-plenum factor is `beta=0.1`. The collector and intake ambient reservoirs reset
to
101,325 Pa, 298.15 K, zero momentum, and their declared fresh mixtures before their
respective boundary edges. Blowby uses the distinct equilibrium-bounded
finite-volume-to-environment operation; it does not use the two-volume 90% bound.
The exact energy reconciliation and stable ordering remain part of
`legacy_low_order_v1`.

### 10.5 Ignition, mixture, and combustion

M3 reproduces the reference asset's fixed ignition curve, rev limit, mixture gate,
per-cylinder ignition event, deterministic component stream, fuel reaction
stoichiometry, flame-speed input functions, and geometric propagating-flame burn.
It does not introduce a Wiebe burn law in M3.

Ignition mode belongs to the engine/combustion-model selection. The current engine
contract selects `spark_ignition`; its gasoline fuel record therefore contains no
second compression-ignition Boolean. The disabled diesel knob from the development
fork was never evaluated by this implementation and is not retained as schema data.
A future compression-ignition engine requires an explicit admitted combustion model
and its actual parameters, not a switch on the fuel record.

The source-named ignition quantity `p_o2/p_fuel/molecular_afr` is accepted only in
`[0.5, 1.9]`; the name is preserved for parity even though it is not the conventional
equivalence-ratio orientation. `ignite` reads the previous thermodynamic state before
the chamber volume update, while its `lastVolume` geometry already reflects the
post-mechanism position. An accepted ignition consumes exactly one `uniformDouble`
variate—two PCG32 state advances—from that cylinder's frozen stream to resolve
efficiency; rejected ignition attempts consume none.

At ignition, the eligible mixture (`globalMix`), diagnostic `total_n`, current
geometric volume, burning efficiency, and flame speed are snapshotted. `total_n` is not
the amount used for each subsequent burn increment: each increment multiplies newly
swept flame-volume fraction by the cylinder's current molar amount after that
substep's heat, blowby, and valve flows, then reacts it against the snapshotted
mixture. The 25 O2 + 2 C8H16 -> 16 CO2 + 18 H2O bookkeeping, limiting-reactant bounds,
fuel molar mass, fuel energy density, and energy insertion are frozen parity
operations.

The flame-speed inputs include the mean of the 256-slot crank-cycle history of absolute
piston speed, initialized exactly by the companion record. A parallel 256-slot
cylinder-pressure history is maintained for diagnostics. The source passed its maximum
to the flame-speed function, but that function ignored both firing-pressure arguments
and used a fixed pressure-adjustment factor of one; M3 does not invent the missing
dependency. The legacy flame advances radially and axially at its snapshotted flame
speed; incremental swept flame volume determines reacted amount, fuel energy release,
and mixture products. Expansion carries the prior axial travel before the next
increment. Absolute intake-valve flow above `1e-9` source mol per gas substep
extinguishes the event before combustion for that substep. Every bound and gate in
this path is part of `legacy_low_order_v1`.

The fuel parameters and flame-speed curves are `legacy_asset_unverified`; fixture
audio cannot validate heat release physically. A single-Wiebe model is an established
empirical candidate, not an automatic upgrade. The Wiebe literature emphasizes that
its parameters are identified for an application
([Ghojel's review](https://doi.org/10.1243/14680874JER06510)). It may enter M5 only
with declared calibration evidence and as one isolated A/B change.

Cycle variation is exactly the deterministic legacy behavior required by the captured
per-cylinder streams. New broadband or cycle noise cannot be added to hide a pressure
model mismatch.

### 10.6 Wall heat, friction, and pumping

M3 retains the legacy cylinder-only wall-energy path:

```text
wall_temperature = 363.15 K
surface_area =
    pi*bore*(cylinder_volume/piston_area)
    + 2*piston_area
heat_transfer_coefficient = 100 W/(m^2*K)
Q_to_gas = (wall_temperature - gas_temperature)
           * surface_area
           * heat_transfer_coefficient
           * dt
```

Other control volumes are adiabatic. Blowby remains a separately declared calibrated
restriction to a 101,325 Pa, 298.15 K crankcase boundary. This is an approximation
preserved for parity, not a fidelity claim.

Woschni's established empirical cylinder heat-transfer correlation
([SAE 670931](https://doi.org/10.4271/670931)) is an M5 candidate. It is not inserted
until its units, motored reference pressure, wall assumptions, and coefficients are
resolved and it can be evaluated as the sole changed subsystem.

M3 reports gas torque from pressure and analytic `dV/dtheta` plus the asset's fixed
crank friction separately. It deliberately omits the source's code-default
piston-wall-friction path because that path consumed a constraint-solver wall reaction
which no longer exists; inventing an analytic surrogate would be a second, unvalidated
change. The prescribed M3 trajectory makes this omission irrelevant to gas/audio, but
M3 does not claim net-torque parity. It declares the omitted piston/ring, bearing,
valvetrain, oil, pump, and accessory losses. The component friction model of Sandoval
and Heywood
([SAE 2003-01-0725](https://doi.org/10.4271/2003-01-0725)) is a later candidate, not
an assumed truth without its required inputs.

The later held-operating-point work introduced Chen--Flynn as a generic cycle-mean
power-prediction closure. Reusing that result one completed cycle late to advance
FreeEngine RPM was not pristine transient parity. The active correction will compute
the source wall reaction for the already admitted centered mechanism and apply the
source friction law instead; it will not add source friction on top of Chen--Flynn.

Gas-exchange work is integrated from the same cylinder pressure-volume path and is not
double-counted. In the prescribed sweep, friction affects the reported incomplete net
torque but crank RPM remains scenario-owned.

M3 has no admitted equivalent-inertia function or derivative, so its prescribed
actuator torque is explicitly unavailable rather than guessed or copied from the
fixture's dyno lane. M3 still publishes indicated gas torque and its named incomplete
net torque. The full prescribed-scenario actuator result becomes mandatory only after
those mechanics are closed; this limitation cannot be hidden when labeling torque or
power. This M3 prescribed-motion limitation is distinct from the later operating-
profile FreeEngine method described above.

### 10.7 Frozen reference excitation

M3 publishes the required static and directional primary-pressure terms through
`CaptureBlock.reference_parity`. The already accepted excitation stage then evaluates,
in exact recorded order:

```text
a = min(abs(filtered_engine_speed_rpm), 40) / 40

x_i = a*a*a * 1600
    * (primary_static_pressure_pa_abs - 101325
       + 0.1*dynamic_pressure_forward_pa
       + 0.1*dynamic_pressure_reverse_pa)

d_i = frozen_integer_delay(x_i)

reference_bus[route(i)] +=
    cylinder_sound_attenuation
    * ((route_audio_volume*d_i)/6)
    * (1/(total_audio_length_m*total_audio_length_m))
```

The exact delay, grouping, routes, constants, and filtered-RPM recurrence are owned by
the fixture and frozen source matrix. The result is uncalibrated
`engine_sim_source_unit`. This empirical excitation is M3's parity boundary, not
production outlet radiation.

M3 uses the exact renderer implementation accepted at the M2 listening stop. Its
manifest pins that renderer's method/config identity and the actual committed
artifact identities.
There is no M3-specific IR, gain, normalization, DSP retune, alternate block
partition, or alternate master. Any renderer change returns to an M2 fixture A/B and
another listening stop before upstream work resumes.

## 11. CaptureBlock meaning

`CaptureBlock` is a bounded, timestamped batch of physical/control observables. It:

- uses stable engine, cylinder, port, volume, edge, and route identities;
- declares sample time/rate and pre/post-step meaning;
- carries SI values and explicit validity/state flags;
- contains enough history-independent meaning for downstream excitation;
- never contains presentation-filter state, microphone coloration, or a mixed waveform.

The narrow M3 extension `CaptureBlock.reference_parity` may expose the exact filtered
RPM and directional legacy proxies required by the frozen comparison. These fields are
not promoted into the future production physical contract. Production additions
include signed mass flow, temperature, area, valve state, and genuine physical
locations required by accepted source/radiation models.

The `ReferenceAuditBlock` is not a `CaptureBlock`. It is redundant validation evidence
and can exist only in reference/test tooling.

## 12. Reachability and failure classes

A render returns success only when its declared scenario and output contract complete.
Non-success is typed:

| Failure | Meaning |
|---|---|
| invalid specification | Missing, contradictory, dimensionally invalid, or unsupported resolved input. |
| unreachable target | A valid load/speed target cannot be reached within declared control bounds. |
| cancelled | The caller requested cancellation at a deterministic block boundary; no success artifacts are published. |
| event/schedule violation | Event ordering, interval coverage, or stable identity is invalid. |
| nonphysical state | Required mass, energy, volume, temperature, pressure, or composition leaves its admitted domain. |
| numerical failure | Convergence, root localization, step limit, overflow, or finite-value contract fails. |
| incomplete source route | A matrix-required route lacks its required physical/excitation model. |
| evidence/rights failure | A required asset/model lacks the declared provenance or distribution permission for the requested output. |
| artifact publication failure | A required sink cannot stage, seal, or atomically publish the declared output. |
| contract violation | A forbidden reference adapter, hidden fallback, model switch, or mismatched manifest is detected. |

Errors include model/profile ID, scenario time, crank angle, component identity, state
summary, attempted recovery, and relevant tolerances. There is no silent fallback to a
tone, one-shot, old renderer, alternate physics model, or clipped output.

Required sinks publish transactionally. On non-success, no success manifest or final
required artifact becomes visible. Opt-in partial diagnostics live under an explicitly
failed run identity with failure status and hashes; they cannot be consumed as an
accepted render.

The sink lifecycle is `idle -> begun -> committed` or `idle -> begun -> aborted`.
Preflight rejection and failed begin leave the sink idle. A pre-commit failure after
begin is followed by abort. Commit is a terminal atomic attempt: success publishes;
failure discards staging and is already aborted. Only successful commit may make the
complete required artifact set and success manifest visible. Sink calls are serial and
non-reentrant, borrowed byte views are callback-scoped, and sink chunking cannot
determine simulation or DSP partitioning.

An unreachable target reports the deterministic nearest feasible state, signed error
`achieved - target`, every active limiting bound, and the controller/search evidence.
“Nearest” minimizes absolute signed error over the declared feasible control domain.
The current load-target contract declares a throttle interval, not an actuator-torque
interval. Equal-error candidates select lower residual actuator magnitude, then lower
throttle, then the stable candidate identity. Saturation is a result, not success
disguised by tolerance.

## 13. Verification, validation, and listening

These terms are not interchangeable, following the distinction summarized by
[ASME VVUQ](https://www.asme.org/codes-standards/publications-information/verification-validation-uncertainty):

- verification asks whether code solves the documented equations/algorithm;
- validation asks whether the model adequately represents physical reality for an
  intended use;
- parity asks whether a boundary matches the frozen behavioral reference;
- listening asks whether the resulting audio is acceptable.

M2 verification includes exact kernel/stem/master comparisons in the pinned numerical
environment. M3 verification includes analytic identities, conservation/accounting
checks applicable to the admitted legacy model, event order, determinism, and
CaptureBlock comparison. M3 validation is deliberately limited: the oracle is not real
BMW measurement data. M3 acceptance is behavioral parity plus user listening.

M3 has two diagnostic hard stops before its listening candidate:

1. Before valvetrain/gas layering, the analytic mechanism must pass the isolated
   geometry, derivative, phase, and prescribed-trajectory gate in §10.1.
2. Before any downstream tuning or acceptance WAV, report per-cylinder phase/event
   alignment and static/forward/reverse primary-pressure comparison against the full
   170,000-record fixture, then compare both reconstructed excitation buses before the
   already accepted renderer.

The comparison report declares scales, tolerances, errors, correlations, and any
uncompared state. Metrics do not certify sound, but a gross source mismatch or route
loss blocks the candidate; it cannot be hidden with gain, IR, noise, normalization, or
a promise that a later phase will repair it.

Metrics may detect NaNs, clipping, DC faults, discontinuities, route loss, timing
errors, spectral/envelope regressions, or excessive cost. They do not certify sound.
Every audible gate supplies the previous accepted and candidate full mixes, affected
stems, raw and separately labelled level-matched copies, identical scenario/control
data, routing, hashes, and performance. The user accepts or rejects by ear.

## 14. Upgrade admission and removal

After M3 acceptance, one subsystem changes at a time:

1. Record one observed deficiency.
2. Name the established replacement and its applicable evidence.
3. Resolve every required input and provenance category.
4. Predict measurable, audible, and performance effects.
5. Hold all other accepted subsystems fixed.
6. Run focused verification and convergence checks.
7. Render the canonical pull and every affected regression scenario.
8. Stop for controlled listening.
9. Accept and delete the superseded path, or reject and redesign.

Candidate directions include:

- event-split fixed integration where event timing demonstrates need;
- adaptive integration as a separate later change where convergence demonstrates need;
- measured/identified valve effective area;
- conservative species/internal-energy control volumes;
- identified burn laws or predictive combustion models;
- Woschni or a better validated heat-transfer model;
- component friction;
- one-dimensional runner/pipe wave dynamics while holding the accepted
  junction/boundary treatment fixed, unless an established solver is demonstrably
  inseparable;
- junction and boundary-loss treatment as its own accepted change;
- evidence-backed observable-to-source excitation while holding physics and radiation
  fixed;
- exterior radiation while holding physics and source excitation fixed;
- deterministic, physically motivated cycle variation;
- startup, shutdown, limiter, fuel cut, and other operating transients.

Books such as Heywood's
[Internal Combustion Engine Fundamentals](https://www.mheducation.com/highered/mhp/product/internal-combustion-engine-fundamentals-2e.html),
Blair's
[Design and Simulation of Four-Stroke Engines](https://saemobilus.sae.org/books/design-simulation-four-stroke-engines-r-186),
and Winterbone and Pearson's
[Theory of Engine Manifold Design](https://www.wiley.com/en-us/Theory+of+Engine+Manifold+Design%3A+Wave+Action+Methods+for+IC+Engines-p-9781860582097)
define established foundations and model domains. They do not remove the need for
engine-specific inputs, verification, validation, or listening.

An accepted replacement removes the old implementation. Git and accepted artifacts
provide rollback; runtime compatibility switches do not.

## 15. Production source completeness

M2/M3's two reference buses cannot become production exhaust outlets by renaming them.
Production requires the routes and evidence in `docs/PRODUCTION_SOURCE_MATRIX.md`,
including:

- sourced exhaust topology and outlet radiation;
- documented intake inlet source/radiation;
- evidence-backed mechanical engine source;
- starter child route while engaged;
- raw stems, audition stems, coherent full mixes, and route-solo/mute evidence;
- stable physical identities, locations, signed flow, temperatures, areas, geometry,
  boundary conditions, and rights-cleared assets.

Generic noise, one-shots, the oracle IR, or the legacy two-route split cannot satisfy a
missing route. A production package is not emitted until every matrix-required source
is complete and the final listening stop is accepted.
