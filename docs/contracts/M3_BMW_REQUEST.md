# M3 BMW M52B28 resolved request

Status: normative pre-implementation contract

Request version: `bmw-m52b28-parity-request-v1`

Physics method: `legacy_low_order_v1` version 1

## 1. Boundary

This record closes the exact `EngineSpec` and `RenderScenario` consumed by the M3
parity simulator. The equations and effective engine values remain owned by
[`M3_PARITY_MODEL.md`](../model/M3_PARITY_MODEL.md), whose current SHA-256 is:

```text
435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c
```

This request does not contain presentation configuration. M3 physics produces typed
capture and excitation; the later M3 connection checkpoint supplies that excitation
to the already accepted presentation implementation without changing it. Keeping the
request at the engine/scenario boundary prevents the temporary P1.8 replay label from
becoming an engine API.

The public simulator library receives an already materialized request and never reads
the reference fixture. A reference-only evaluation composition may:

1. verify and decode `reference-parity.bin` and `component-seeds.bin`;
2. extract only `reference-parity.bin`'s RPM lane into the typed trajectory below;
3. compare the decoded combustion seed pairs with the sealed parity-profile values
   below;
4. retain crank angle and pressure fields solely in a comparator object;
5. pass the sealed engine/scenario request to the simulator.

Captured crank angle, filtered RPM, pressure, excitation, and audit buses are never
scenario inputs.

## 2. Stable request identity

| Field | Exact value |
|---|---|
| Engine numeric ID | `1` |
| Engine semantic ID | `bmw-m52b28` |
| Engine profile ID | `bmw-m52b28-legacy-low-order-v1` |
| Engine display name | `BMW M52B28` |
| Scenario ID | `bmw-m52b28-reference-pull-v1` |
| Engine and scenario schema version | `1` |
| Provenance schema ID | `engine-sim-offline.m3-bmw-provenance.v1` |
| Provenance bundle ID | `bmw-m52b28-m3-request-provenance-v1` |

All eight engine method selections are `legacy_low_order_v1`, version 1, with the
content hash of `M3_PARITY_MODEL.md` above as their configuration identity. The
prescribed-motion method is
`fixed-rate-post-step-rpm-binary64-v1`, version 1, with this request record's eventual
committed content hash as its configuration identity.

Derived request leaves use the following version-1 method identities. Each method's
configuration identity is the content hash of `M3_PARITY_MODEL.md`; their dependency
paths remain part of each provenance resolution rather than being hidden in the ID:

| Method ID | Derived request value |
|---|---|
| `geometric-cylinder-displacement-std-pi-v1` | Generic `EngineSpec` displacement |
| `legacy-slider-crank-compression-ratio-v1` | Per-cylinder compression ratio |
| `legacy-flow-constant-v1` | Restriction/flow-table `resolved_k` |
| `collector-volume-over-area-v1` | Exhaust collector/system length |
| `legacy-pseudo-gas-mass-afr-v1` | Scenario mass-AFR metadata |
| `scenario-audible-duration-subtraction-v1` | Audible duration from total and start |

Propagation delay is no longer a derived engine-request leaf. The engine retains
physical header and route length plus the excitation method's propagation speed; the
excitation session resolves the discrete delay against the admitted scenario capture
clock when the session is compiled.

The parity profile owns exactly six typed, resolved
`LegacyCombustionRandomStream` request values in runtime-cylinder order. Their
collection is
`LegacyLowOrderV1Profile::core.combustion_random_streams`; for each row, the resolved
fields are `<base>.pcg32_initial_state` and `<base>.pcg32_stream`. The C++ ownership
split does not insert `core` into the canonical parameter paths:

| Cylinder | Request base path | `pcg32_initial_state` | `pcg32_stream` |
|---:|---|---:|---:|
| 1 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-1` | `0x6ba3d060370e05fa` | `0x3e13b1e68ef2f790` |
| 2 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-2` | `0xb1ab9b6c6217bdf3` | `0x7681d4f9a6c78e3f` |
| 3 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-3` | `0x0c2447917cd77f40` | `0x4c09e08d851104f5` |
| 4 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-4` | `0xfc83080b6c8b1a98` | `0x686f68f85fd7d169` |
| 5 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-5` | `0x1f0c63f1d677237b` | `0x3507d87731683125` |
| 6 | `engine.physics.legacy-low-order-v1.combustion_random_streams.cylinder-6` | `0xad811f42fb6dafa3` | `0x50900fae5afa96cf` |

Each row binds its semantic cylinder to the corresponding typed `CylinderId`.
Admission requires exactly one row per cylinder and the PCG32 sequence-selector
bound `stream <= 2^63-1` on `pcg32_stream`; `pcg32_initial_state` retains its full
unsigned 64-bit domain. These values are sealed engine-request data, not hidden
session or simulator state. The production-neutral BMW request factory accepts only
the owned RPM vector as fixture-derived input and constructs these six exact pairs
directly from this normative request. A reference-only integration may verify and
decode `component-seeds.bin` to compare its pairs with the constructed request; it
never supplies those values to the factory or simulator. Neither production layer
opens that fixture, derives replacement pairs from the public seed, or links its
reader at render time.

## 3. Stable topology identities

There is one bank:

```text
BankId 1 = bank.inline-1
```

Cylinders retain runtime traversal order `1..6`. Each cylinder has intake port
`2*i-1` and exhaust port `2*i`. Their semantic IDs are `cylinder-i`,
`intake-port-i`, and `exhaust-port-i`.

Gas-volume IDs are:

```text
1  atmosphere
2  intake.plenum

for cylinder i=1..6:
    3 + 3*(i-1)  intake.runner.i
    4 + 3*(i-1)  cylinder.i
    5 + 3*(i-1)  exhaust.primary.i

21 exhaust.collector.0
22 exhaust.collector.1
```

Flow-edge IDs are:

```text
1 flow.main-throttle: atmosphere -> intake.plenum
2 flow.idle-bypass:   atmosphere -> intake.plenum

for cylinder i=1..6, base=3+5*(i-1):
    base+0  flow.plenum-to-runner.i:
            intake.plenum -> intake.runner.i
    base+1  flow.intake-valve.i:
            intake.runner.i -> cylinder.i
    base+2  flow.exhaust-valve.i:
            cylinder.i -> exhaust.primary.i
    base+3  flow.primary-to-collector.i:
            exhaust.primary.i -> associated collector
    base+4  flow.blowby.i:
            cylinder.i -> atmosphere

33 flow.collector-outlet.0: atmosphere -> exhaust.collector.0
34 flow.collector-outlet.1: atmosphere -> exhaust.collector.1
```

The source routes are deliberately the two frozen evaluation routes only:

| `RouteId` | Semantic ID | Collector | Cylinders | Route gain |
|---:|---|---:|---|---:|
| 1 | `exhaust.reference.0` | 21 | 2, 4, 6 | `0.5` |
| 2 | `exhaust.reference.1` | 22 | 1, 3, 5 | `1.0` |

This numeric mapping is repository-local. Fixture route index 0 maps to `RouteId 1`;
fixture route index 1 maps to `RouteId 2`.

Three nearby displacement values are intentionally distinct and must not be
cross-substituted:

| Owner/use | Arithmetic authority | Exact value |
|---|---|---:|
| `EngineSpec` field `engine.total_displacement_m3` | Geometric sum of cylinder swept volumes using `std::numbers::pi` | `0.0027930517982299274 m3` |
| `legacy_low_order_v1` runtime analytic volumes, clearance, torque, and BMEP | `pi_l = 3.14159265359` in the arithmetic order fixed by `M3_PARITY_MODEL.md` | `0.0027930517982301117 m3` |
| Reference comparator only | Source fixture's sampled constraint-mechanism observation | `0.0027930477143328905 m3` |

The `EngineSpec` value satisfies generic geometry validation. The legacy runtime
value remains the executable parity-method result. The sampled fixture value is
comparator/provenance evidence only and never initializes either representation.

## 4. Fixed-rate prescribed RPM

The parity pull uses a dedicated fixed-rate trajectory value rather than expanding
the evidence into 170,000 keyframes:

```text
FixedRateRpmTrajectory
    rate                  = 10000/1 Hz
    first_step_index      = 0
    semantics             = post_step_rpm
    sample_count          = 170000
    samples               = owned vector<binary64>
    samples_f64le_sha256  =
      b6206910b9c7b19694e35e08a3c5d8450f03cfdcbf43e818eb2a0606927d8cda
```

The hash is over exactly 1,360,000 bytes: each RPM sample serialized as its IEEE-754
binary64 bit pattern in little-endian order, with no header. Request validation
recomputes the hash, rejects non-finite or negative samples, and requires the rate,
first index, and sample count to match the 17-second physics horizon. The manifest
will encode the rate, semantics, count, and hash rather than duplicate the entire
sample vector as decimal JSON.

`reference-parity.bin` remains the evidence container:

```text
file SHA-256:
19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c
```

The trajectory resolution cites that complete content-addressed evidence. A core
simulator or production render target must not link its decoder.

## 5. Scenario values

The scenario is `PrescribedKinematicSweep` with:

| Field | Exact value |
|---|---:|
| Total duration | `17 s` |
| Audible interval | `[2 s, 17 s)` |
| Fixed warm-up | `1 s` |
| Fixed settling | `1 s` |
| Physics/capture rate | `10000/1 Hz` |
| Source/acoustic/delivery rate | `192000/1 Hz` |
| Public seed | `12648430` (`0xC0FFEE`) |
| Initial cycle angle | `120 * (3.14159265359/180) rad` |
| Quality ID/version | `legacy-low-order-parity-v1`, version `1` |
| Capture block capacity | exactly `200` frames |
| Event journal capacity | exactly `3800` records (`19*200`) |

Throttle is a right-continuous keyframed trajectory:

```text
(0.0 s, 0.18)
(0.9 s, 0.12)
(1.0 s, 0.85)
```

Operating state is right-continuous:

| Time | Event ID | Ignition | Fuel | Starter | Dyno | Limiter configured |
|---:|---|---|---|---|---|---|
| `0.0 s` | `direction-acquisition` | off | on | on | off | on |
| `0.8 s` | `direction-lock` | off | on | on | on | on |
| `0.9 s` | `ignition-handoff` | on | on | off | on | on |

No state change occurs at 1.0 or 2.0 seconds; only throttle changes at 1.0. The
limiter is configured throughout because the source ignition module owns it, although
the recorded trajectory never reaches its 8,000 RPM threshold.

The resolved ignition profile deliberately uses angular-speed units for its timing
table while retaining RPM for operating limits:

| Resolved request field/path | Unit | Exact construction/value |
|---|---|---|
| `engine.physics.legacy-low-order-v1.ignition.timing_curve_triangle_radius_rad_s` | `rad/s` | `1000*0.104719755 rad/s` |
| `engine.physics.legacy-low-order-v1.ignition.timing_curve.<sample-id>.angular_speed_rad_s` | `rad/s` | Each source `rpm_point*0.104719755` |
| `engine.physics.legacy-low-order-v1.ignition.timing_curve.<sample-id>.timing_advance_rad` | `rad` | The corresponding source timing advance |
| `engine.physics.legacy-low-order-v1.ignition.limiter_speed_rpm` | `rpm` | `8000 rpm` |
| `engine.physics.legacy-low-order-v1.ignition.declared_redline_rpm` | `rpm` | `7000 rpm` |

Neither the timing-table abscissae nor its triangle radius are RPM fields. Conversely,
the limiter and declared redline remain RPM values and are not converted in the
sealed request.

The inherited fuel curve also carries its interpolation support explicitly:
`engine.physics.legacy-low-order-v1.fuel.turbulence_to_flame_speed_ratio_triangle_radius`
is the dimensionless binary64 value `5.0`. It is resolved request data alongside the
curve samples, not a simulator constant.

## 6. Explicit ambient, fuel, and thermal metadata

The low-order parity method consumes dry idealized gas state and does not consume
humidity, coolant temperature, oil temperature, or the scenario's conventional
mass-AFR field. They remain explicit rather than being fabricated later:

| Field | Exact value | Meaning |
|---|---:|---|
| Ambient pressure | `101325 Pa abs` | Consumed |
| Ambient temperature | `298.15 K` | Consumed |
| Relative humidity | `0` | Declared dry-gas default; not consumed |
| Initial gas temperature | `298.15 K` | Consumed |
| Wall temperature | `363.15 K` | Consumed |
| Coolant temperature | `363.15 K` | Metadata-only parity default |
| Oil temperature | `363.15 K` | Metadata-only parity default |
| Crankcase pressure | `101325 Pa abs` | Consumed |
| Crankcase temperature | `298.15 K` | Consumed |
| Fuel ID | `gasoline-legacy-engine-sim-v1` | Must match engine profile |
| Lower heating value | `48.1e6 J/kg` | Consumed |
| Stoichiometric mass AFR | `(12.5/0.25)*(0.02897/0.100)` = `14.484999999999998` | Derived pseudo-gas metadata; not consumed |

These metadata values do not become verified BMW facts. The humidity/coolant/oil
choices are `declared_default`; the AFR is `derived`; the remaining source-derived
values stay `legacy_asset_unverified`.

## 7. Torque capability

The included mask contains only:

```text
indicated_gas | crank_friction
```

The omitted mask contains:

```text
piston_ring_friction | bearing_friction | valvetrain_friction |
pump_and_oil | accessory | starter
```

The instantaneous net-shaft form is available but incomplete, with the included and
omitted masks above. The cycle-mean form is unavailable, canonical incomplete, and
has empty masks. Equivalent inertia is unavailable. The prescribed sweep is
therefore admissible, while held-speed, load-target, and inertial-dyno claims remain
inadmissible.

## 8. Provenance and acceptance

The ledger includes content-addressed evidence for the legacy BMW asset, inherited
defaults, `M3_PARITY_MODEL.md`, fixture manifest, parity evidence, component-seed
evidence, and the preserved engine-sim MIT notice. Legacy engine values remain
`legacy_asset_unverified`; fixture trajectory values remain `reference_fixture`;
scenario choices remain `scenario`; calculated values use derived resolutions with
explicit direct dependencies.

The immutable evidence identities are:

| Evidence ID | Locator | Revision | SHA-256 | Rights |
|---|---|---|---|---|
| `m3-parity-model-record` | `docs/model/M3_PARITY_MODEL.md` | repository content | `435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c` | permitted |
| `legacy-bmw-m52b28-asset` | `assets/engines/bmw/M52B28.mr` | `9617562a7a5615c2bf84c9ec39cd5ae25c560059` | `2c7746f82e86cc22b0ab243f61e7fb8c155c3abf1084ad7b6f8bfee3d4e875a9` | no assertion |
| `legacy-engine-sim-objects` | `es/objects/objects.mr` | `9617562a7a5615c2bf84c9ec39cd5ae25c560059` | `f8214983c816f0a2e8d2cf1d733d10ea7adfbdaef94965b9c969c49e35e14dba` | no assertion |
| `legacy-performer-intake` | `es/part-library/parts/intakes.mr` | `9617562a7a5615c2bf84c9ec39cd5ae25c560059` | `f2331225d54ec56da44b5b658cfd51b859eb77c9bc5700a8dcace7513f5f8b1e` | no assertion |
| `reference-fixture-manifest` | `reference/fixtures/bmw-m52b28-p18/manifest.json` | repository content | `52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07` | local evaluation only |
| `reference-parity-evidence` | `reference/fixtures/bmw-m52b28-p18/reference-parity.bin` | repository content | `19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c` | local evaluation only |
| `reference-component-seed-evidence` | `reference/fixtures/bmw-m52b28-p18/component-seeds.bin` | repository content | `ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f` | local evaluation only |
| `engine-sim-mit-notice` | `reference/oracles/bmw-m52b28/engine-sim-MIT.txt` | repository content | `9f64449d4ef2db6b57d6af9d36e5eca5b6de3ece2db14a847ae71d4e0dcbff15` | permitted |

The ledger also content-addresses this request record after it is committed. Its
canonical bundle digest is computed over the complete ledger while excluding only the
digest field itself; callers cannot supply it.

Before physics implementation, the constructed request must:

1. pass the generic provenance, engine, scenario, and engine/scenario validators;
2. pass a narrow mutation-sensitive BMW request validator that checks every identity,
   table, scalar, ordering, capability, trajectory identity, and control boundary
   fixed here and in `M3_PARITY_MODEL.md`;
3. prove the reference decoder is absent from the simulator target's dependency graph;
4. leave public `render()` fail-closed until the later physics-to-presentation
   integration checkpoint.

This checkpoint proves only that the intended input is complete and unambiguous. It
does not claim physics, sound, power, or public-render success.
