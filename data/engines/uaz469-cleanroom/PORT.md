# UAZ-469 / UMZ-451M MR port

This is an explicit declarative port of `engine-sim/assets/engines/uaz469_cleanroom.mr`, updated on 2026-09-08 (MR SHA-256 `102bb4fe7e14fa9a0c453ed859ae30ba40cc9e0d41e08dfba2e0cf5dc7a798ca`). Its byte-identical source snapshot, synthetic IR generator, reference EQ profile and source notes are in `source/`. The 44.1 kHz PCM16 exhaust IR is copied intact and bound by SHA-256. Its synthetic response uses smoothed reference magnitude measurements; no reference PCM or phase is included. No third-party UAZ engine script was introduced.

Preserved parameters include 92 mm bore/stroke, 6.7:1 compression, 1–2–4–3 firing at 0/180/360/540 degrees, crank/journal geometry, masses/inertias, the shared carburetor and exhaust, scaled flow-bench samples, 184/190-degree effective valve lobes, 8.5 mm lift, the distributor timing curve, combustion variation, 125 Nm / 220 RPM starter, 4,200 RPM redline and 4,500 RPM limiter. Presentation preserves the updated source route/cylinder gains, **0.00956941 IR gain**, **0.00012 derivative mix**, 0.08 air noise and 0.010 jitter. The idle plate position is now **0.99035**, targeting about **765 RPM** in the source simulator.

The updated response has 6,144 samples (139 ms), reference-informed
minimum-phase EQ, and 0.28 ms of causal pulse rounding. This revision restrains
upper rasp and trims some low-bass boom while retaining the pressure-led firing
beat. The generator is unchanged; the magnitude EQ profile and normalized WAV
changed together with the MR's matching IR gain. The exact copied IR SHA-256 is
`1bd5894b04539c7e95631afd5a1dacee94bc203a09c8b1db8a0bc6ba04d68323`.
Regeneration from the copied generator/profile reproduces the WAV byte-for-byte:

```sh
python3 data/engines/uaz469-cleanroom/source/generate_uaz469_ir.py \
  --eq-profile data/engines/uaz469-cleanroom/source/reference_eq.json \
  --output ~/tmp/uaz-regenerated-ir.wav
```

MR expressions are evaluated into physical quantities: chamber volume is swept cylinder volume divided by 5.7, exhaust area is π × 22² mm², deck height is 272 mm, and crank inertia is the sum of the three authored disk inertias. MR rod center-of-mass zero is a midpoint-relative offset; Crankwave's distance from the crank pin is 87.5 mm. The default MR turbulence/flame-speed function is explicit in JSON. Triangle-filter radii remain 1 mm, 500 RPM, and 5 for the three function families.

Crankwave requires a Chen-Flynn warm-running aggregate friction/accessory model. The port uses its explicit generic prior and accessory descriptor; those coefficients are not a measurement from the MR and do not claim identical free-running physics. The MR crank-friction field is retained as authored, but the aggregate loss executor has different ownership. Fuel density is omitted because it was application fuel-volume telemetry, not an audio/physics input. Crankwave's canonical conditioning cutoff and publication-gain conventions are explicit. The synthetic IR describes an artistic exterior exhaust character, not measured acoustics.

The JSON rig records the MR's 5.38 portal-axle reduction. The Godot project retains its previously authored 5.125 civil-axle gameplay physics. The audio port does not replace the wheel/vehicle solver.

## Reproduce

From the Crankwave root, with sufficient free home-directory storage:

```sh
python3 scripts/port-uaz469.py --source-root ../engine-sim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRANKWAVE_BUILD_TESTS=ON
cmake --build build --target crankwave_cli --parallel 8
build/crankwave render --engine data/engines/uaz469-cleanroom/engine.json \
  --scenario data/engines/uaz469-cleanroom/scenarios/held-765rpm.json \
  --asset-root data/engines/uaz469-cleanroom --output-directory ~/tmp/uaz-idle-check \
  --result-format json
build/crankwave bake-crankwave --engine data/engines/uaz469-cleanroom/engine.json \
  --asset-root data/engines/uaz469-cleanroom \
  --output artifacts/uaz469/uaz469.crankwave --result-format json
build/crankwave verify-crankwave --input artifacts/uaz469/uaz469.crankwave --result-format json
```

Output destinations must be new; the baker does not overwrite an existing carrier. `port-uaz469.py` pins the reviewed MR and IR hashes and rejects other revisions, including on a fresh checkout. Updating the source requires reviewing the explicit mapping and matching IR gain first.

The idle validation scenario runs 20 kHz physics and 192 kHz presentation. The current automatic native package profile is `interactive-preview-redline-v1`: its published captures use **10 kHz preview physics**, 192 kHz presentation, 33 held cells, six directional captures and four lifecycle captures. Keep that fidelity label; it is not a 20 kHz production-bake claim. The package is simulator-free and passes full carrier/member verification.

The Godot adapter preserves dry phase textures at 11 RPM anchors and three load lanes, with cyclic means and 48 residual variations. Its native runtime reconstructs them at 192 kHz, applies the fixed-time exhaust FIR and mastering, then decimates to 48 kHz. Exhaust resonances therefore do not shift with RPM. It uses that bank and the captured startup/shutdown performances, rather than the complete JavaScript directional-transient runtime. See the Godot project's `addons/crankwave/README.md` for the adapter contract and rebuild commands.
