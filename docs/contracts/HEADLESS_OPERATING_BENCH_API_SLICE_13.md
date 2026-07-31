# Headless operating-bench API — slice 13

Status: implemented and frozen on 2026-07-31

Scope: publish already-executed starter, bounded held-dyno, and free-vehicle behavior
through the portable C++ session, exact C ABI, WASM, and browser runtime boundary
without changing simulation, excitation, routing, conditioning, convolution, or
mastering.

Implementation chain: contract `e4feebc`; timestamped control lanes `d10e9a3`;
native publication `cb5f7c2`; exact C ABI v4 `7d7effd`; WASM v4 `ad0f306`; Worker
protocol v2 `d519cda`.

## 1. Lifetime and ownership

This slice does not add another motion model or session lifetime.

- `free_engine` remains the only mode admitted for `open_ended` execution.
- `held_dyno` and `free_vehicle` remain exact `finite_scenario` procedures.
- Slice 14 owns the full operating-bench procedure and any separately specified
  lifetime extension it needs.
- Motion ownership cannot change inside a session. There is no live dyno-enable,
  dyno-disable, or mode-switch command. A constant held-dyno target is the hold
  operation.

## 2. Timestamped controls

Every new control uses the existing absolute delivery-frame clock, causal ceiling
projection to the physics clock, atomic batch admission, and right-continuous sticky
semantics. Preparation, late, post-horizon, unsupported-mode, and terminal commands
continue to fail explicitly.

`held_dyno` admits:

- selected throttle-controller demand;
- ignition enabled;
- fuel enabled;
- positive target engine speed in RPM;
- nonnegative maximum absorbing torque in N m;
- nonnegative maximum driving torque in N m.

`free_vehicle` admits:

- selected throttle-controller demand;
- ignition enabled;
- fuel enabled;
- limiter enabled;
- starter enabled only when the engine declares a cranking starter;
- selected forward-gear ordinal, where zero means neutral and `1..N` is the authored
  forward-gear order;
- clutch engagement in `[0, 1]`;
- service-brake application in `[0, 1]` only when the rig declares positive service-
  brake capacity.

`free_vehicle` does not admit the free-engine external resisting-torque command. Its
load is resolved by its clutch, vehicle, road-load, and service-brake model.

## 3. Capability discovery

The session descriptor publishes an explicit motion-mode enum and forward-gear count.
Callers must not infer motion ownership from a coincidental capability-bit combination.
The exact live-control mask remains authoritative for each command. The C ABI uses the
same one-based forward-gear ordinal as C++; it does not create an index or semantic-ID
compatibility layer.

Ordered gear descriptors publish stable ID, authored ordinal, ratio, and semantic ID.
The accepted BMW rig exposes ordinals `1..5` with ratios `4.21`, `2.49`, `1.66`,
`1.24`, and `1.00`.

## 4. One final-step telemetry record per block

Existing engine telemetry remains unchanged. The same record gains optional mode-owned
sidecars rather than changing the capture/artifact schema.

The held-dyno sidecar publishes:

- applied target RPM;
- maximum absorbing and driving torque;
- required and applied actuator torque;
- tracking, absorbing-limit, or driving-limit disposition.

The free-vehicle sidecar publishes:

- vehicle speed and distance;
- neutral or selected one-based forward-gear ordinal;
- applied clutch and service-brake commands;
- clutch disposition, torque capacity, signed average torque on the engine, and
  optional final slip;
- road-load disposition, requested resisting-force capacity, and actually applied
  average resisting force.

These are the committed final physics-step values for the returned block. They are not
estimated in JavaScript and are not reconstructed from audio.

C++ represents each sidecar with `std::optional`; C uses a canonical `0/1` presence
flag and requires an absent sidecar to be bytewise all-zero; JavaScript decodes absence
as `null`. Both are absent during preparation and for unrelated modes. After release,
exactly the sidecar owned by HeldDyno or FreeVehicle is present.

## 5. Exact-version ABI cut

The C ABI was replaced once by exact `ESO_C_API_VERSION == 4`, and the browser protocol
by exact `engine-sim-offline/browser-worker-v2`. There is no older-layout decoder,
deprecated symbol family, compatibility struct, singular-control protocol, or dual
runtime path. Worker control messages contain one nonempty atomic batch and produce one
`controls-result`.

## 6. Gate

Passed. Native C++, C, WASM, and browser-runtime gates demonstrate the same capability
masks, command timing, mode-owned telemetry, and explicit rejection behavior. The
pinned native/WASM parity fixture has semantic transcript SHA-256
`1493a854b9fef0905cb73f64c6b46c3993d7f7471ca15ee8d39a2eb44e89ab28` and bundle
hashes `535c4754edf41c0ea0adb83d5790e17bda384ab69be7df885941d6be992034cb`
and `53dea2aa3956ce2035290cb1ae49de9276f7bb16b15dedcba51e4c273edda73b`.

A fresh admitted Release renderer reproduced the accepted BMW recordings exactly:

- Held dyno: audition `487beafdd6eacd21cc81de01bc7b558e10861453839b1332a6fbd690de3f8496`,
  raw `8c08586d90d0f541388d4576bf8d47c2e58147224c0d9f8c9b7ccda71e043742`.
- Launch/shift: audition `4a096535cda350ee52426638005a4197ad4d998a83efbfa658ac17b84bdd2216`,
  raw `8ddbee737278851ab178e9b5afee0c6e9b12b7fa4613cc92ecf8d87b09273277`.
- Fifth-gear pull/lift: audition
  `a60070611a71d5208d0222f0aab85f019b51a17ec1216573e2c6814294d4599f`, raw
  `d970c9961db415f7025151aad1a38da5bee45d119b858abb786b15563401c2c8`.
