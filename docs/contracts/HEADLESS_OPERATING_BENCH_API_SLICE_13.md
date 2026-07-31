# Headless operating-bench API — slice 13

Status: frozen implementation contract

Scope: publish already-executed starter, bounded held-dyno, and free-vehicle behavior
through the portable C++ session, exact C ABI, WASM, and browser runtime boundary
without changing simulation, excitation, routing, conditioning, convolution, or
mastering.

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

## 5. Exact-version ABI cut

The C ABI version is replaced once for this slice. There is no older-layout decoder,
deprecated symbol family, compatibility struct, or dual runtime path. Native, WASM,
Worker, and browser tests must all consume the same new layout before the cut is
complete.

## 6. Gate

The slice passes when native C++, C, WASM, and browser-runtime tests demonstrate the
same capability mask, command timing, mode-owned telemetry, and explicit rejection
behavior. The accepted held-dyno and free-vehicle finite recordings must remain
byte-identical because this slice changes publication only.
