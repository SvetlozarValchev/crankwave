# Web operating bench — slice 14

Status: implemented and frozen on 2026-07-31

Scope: turn the slice-13 native/C ABI/WASM/Worker operating surface into a usable
browser bench without changing session physics, lifetime, audio routing, DSP, or WAV
publication.

Implementation chain: contract `755e5f6`; grouped repository procedures `a3b7ca5`;
descriptor-driven controls and telemetry `c3c2563`; finite-procedure lifecycle
`4f43eb3`.

## 1. One backend remains authoritative

- The UI uses `engine-sim-offline/browser-worker-v2`; it does not inspect scenario JSON
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
caller-provided document. Catalog metadata is navigation only; it is not a second
scenario contract. Existing free-rev, crank/catch, held-idle, inertial-dyno, VTEC, and
governor procedures remain available and are grouped by engine for selection.

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

- FreeEngine remains the only `open_ended` motion mode. **Stop** pauses it, **Start**
  resumes it, and **Restart** creates fresh state.
- HeldDyno, FreeVehicle, inertial-dyno, held, and prescribed procedures remain exact
  `finite_scenario` sessions. **Run procedure** starts one, completion is terminal, and
  **Run again** creates a fresh session.
- Slice 14 does not extend an authored horizon, loop a capture, or silently convert a
  finite mode to FreeEngine. A later continuous-dyno/vehicle lifetime must be designed
  and admitted explicitly.
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
- finite completion offers a fresh run and FreeEngine still pauses/resumes continuously;
- the existing canonical browser WAV hash, zero-startup-underrun gate, native/WASM
  parity bounds, and six accepted held-dyno/vehicle WAV hashes remain unchanged.

The browser gate loads all three BMW procedures, identifies HeldDyno and FreeVehicle
from their descriptors, renders neutral plus five ordered gears, submits
`3200 RPM / 800 N·m / 0 N·m` as one dyno batch, and submits second gear, 50% clutch,
and 25% service brake as one drivetrain batch. Returned sidecars report the applied
values. The finite launch reaches **Procedure complete**, then **Run again** starts a
fresh session. The canonical browser WAV remains 3,840,056 bytes with SHA-256
`2972cdad90d08d31ddfac3ca99a4efcda93a15db637d93abc2b7844085c3e4b2`, and the
workbench reports zero startup underruns.

No renderer source, engine/scenario data, C ABI, WASM core, Worker protocol, or audio
code changed in slice 14. The renderer source closure is therefore identical to the
slice-13 production proof, which reproduced all six accepted HeldDyno/FreeVehicle WAV
hashes byte-for-byte.
