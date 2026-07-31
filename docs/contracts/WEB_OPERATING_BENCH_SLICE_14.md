# Web operating bench — slice 14

Status: implemented on 2026-07-31; amended for explicit session lifetime

Scope: turn the slice-13 native/C ABI/WASM/Worker operating surface into a usable
browser bench. Session lifetime is an explicit authoring choice independent of motion
mode. FreeEngine, HeldDyno, and FreeVehicle admit both finite procedure and continuous
bench execution; finite authored capture/export remains a separate fresh session.
Physics, audio routing, DSP, and WAV publication are unchanged.

Implementation chain: contract `755e5f6`; grouped repository procedures `a3b7ca5`;
descriptor-driven controls and telemetry `c3c2563`; initial finite-procedure lifecycle
`4f43eb3`; current contract revision completes open HeldDyno/FreeVehicle lifetime
through the unchanged execution-kind boundary.

## 1. One backend remains authoritative

- The UI uses `engine-sim-offline/browser-worker-v3`; it does not inspect scenario JSON
  to invent capabilities, calculate vehicle/dyno state, or synthesize audio.
- The built descriptor's motion mode, exact control list, and forward-gear descriptors
  decide which widgets exist and whether they are enabled.
- Telemetry values come from the returned engine record and nullable HeldDyno or
  FreeVehicle sidecar. The UI does not estimate them from RPM or audio.
- Structural JSON edits still require **Apply & rebuild**. There is no property-mutation
  protocol and no compatibility path.

## 2. Named procedures

The repository catalog gains the three accepted BMW M52TUB28 procedures:

- held-dyno pull, hold, lift, and unforced overrun;
- neutral launch with clutch take-up and first-to-second shift;
- already-moving fifth-gear pull and lift.

They load ordinary engine/scenario JSON through the same editor and compiler as a
caller-provided document. Catalog URLs are navigation metadata; the declared default
execution kind is required session-request metadata outside scenario JSON, not a
second scenario contract. Existing free-rev, crank/catch, held-idle, inertial-dyno,
VTEC, and governor procedures remain available and are grouped by engine for selection.

## 3. Capability-driven controls

The existing throttle, ignition, fuel, limiter, starter, and FreeEngine external-load
widgets remain. Slice 14 adds:

- HeldDyno target RPM, maximum absorbing torque, and maximum driving torque;
- FreeVehicle neutral/forward-gear selection from the exact published inventory;
- FreeVehicle clutch engagement and capability-gated service-brake application.

Values use the public units and ranges. Gear zero is displayed as neutral; every
positive option carries its authored ordinal, semantic ID, and ratio. A control is
enabled only while the session is running and its exact capability is present. Related
values may be submitted as one atomic Worker batch; there is no generic string/value
mutation, live dyno-enable switch, or motion-mode transition.

The authored release state seeds a widget before released telemetry exists. After
release, the matching returned telemetry becomes the display authority unless a newer
control for that field is awaiting its first causally affected block.

## 4. Lifetime and procedure semantics

- A visible selector sends the exact required execution kind through UI, Worker,
  browser runtime, and C API. Motion mode is never used to infer lifetime, and there is
  no malformed-JSON or compatibility fallback.
- Interactive free-rev presets default to `open_ended`. Named cold-start, load-step,
  dyno, vehicle, and canonical-capture procedures default to `finite_scenario`.
- Users may rebuild an admitted FreeEngine, HeldDyno, or FreeVehicle scenario as
  `open_ended`. **Stop** then pauses the same state, **Start** resumes it, and
  **Restart** creates fresh state with the same motion owner.
- Open execution follows authored initialization through the audible handoff and then
  holds the applicable operating, throttle, dyno, or drivetrain snapshot until live
  controls replace it. Later authored procedure events are not replayed into the bench.
- Inertial-dyno, held, prescribed, and other capture-only modes remain exact
  `finite_scenario` sessions. Their completion is terminal and a new run creates fresh
  state.
- No interactive mode extends or loops a finite capture, and no mode is converted to
  FreeEngine. HeldDyno and FreeVehicle retain their own motion ownership continuously.
- WAV export remains a fresh unpaced execution of the authored finite scenario. Live
  UI changes are not mislabeled as part of that authored capture.

## 5. Mode telemetry

The common RPM, torque, power, and trace remain. HeldDyno additionally displays target
RPM, required and applied actuator torque, both limits, and disposition. FreeVehicle
additionally displays speed, distance, selected gear, clutch engagement/torque/slip and
disposition, service-brake application, and requested/applied road load. Before release
or in another mode, unavailable sidecars render as unavailable rather than zero.

## 6. Gate

Passed. The actual fixed-memory WASM workbench demonstrates:

- the three named BMW procedures compile from repository JSON;
- mode and lifetime labels come from the descriptor;
- HeldDyno and FreeVehicle widgets exactly match their advertised capability masks;
- the five BMW gears render in authored order;
- admitted controls reach returned mode telemetry, while a non-admitted widget is not
  actionable;
- after explicit open-ended admission, all three dynamic benches pause/resume
  continuously and restart from fresh authored initialization;
- the existing canonical browser WAV hash, zero-startup-underrun gate, native/WASM
  parity bounds, and six accepted held-dyno/vehicle WAV hashes remain unchanged.

The browser gate loads all three BMW procedures, identifies HeldDyno and FreeVehicle
from their descriptors, renders neutral plus five ordered gears, submits
`3200 RPM / 800 N·m / 0 N·m` as one dyno batch, and submits second gear, 50% clutch,
and 25% service brake as one drivetrain batch. Returned sidecars report the applied
values. After explicit open-ended rebuilds, both dynamic sessions remain nonterminal
until stopped or restarted. The
canonical finite-export browser WAV remains 3,840,056 bytes with SHA-256
`2972cdad90d08d31ddfac3ca99a4efcda93a15db637d93abc2b7844085c3e4b2`, and the
workbench reports zero startup underruns.

No renderer source, engine/scenario data, C ABI, WASM core, or audio code changed in
this correction. Worker v3 adds the required execution-kind field with no old-layout
decoder. The renderer source closure remains identical to the slice-13 production
proof, which reproduced all six accepted HeldDyno/FreeVehicle WAV hashes byte-for-byte.
