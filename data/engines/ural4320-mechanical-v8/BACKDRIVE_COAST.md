# Engaged-coast listening experiment

The user described the final coast in the 48-second Ural sequence as floating
freely rather than being driven by the moving truck. The previous clip had only
RPM, fueling and a descriptive gear label: final RPM was prescribed to fall from
2,050 to 1,050 over nine seconds. The engine renderer received no distinction
between clutch-open rundown and engine backdrive. Its older coast tuning had
been compared with short gear-change gaps, not identified continuous second-gear
engine braking.

This separate experiment changes only the final overrun, starting at 39 seconds.
The recorded starter, pickup, stationary revs, first-gear pull, shift and second-gear
pull are retained. The user accepted this coast. It is now also available through
the optional [packaged harness driving setup](HARNESS_DRIVE.md).

## Coupled motion and torque

`backdrive-coast.mjs` constructs per-RPM motored gas torque tables from the verified
carrier's zero-fuel pressure-force samples. For each cylinder it multiplies force
by slider-crank `dx/dtheta`, using the authored stroke and rod length, then sums
all cylinders at their physical firing phases. It does not use microphone gains
or fuel-dependent combustion noise. The engine definition's exact hash must match
the native carrier before its geometry can be used. Integrated table torque agrees
with native indicated cycle work to within one percent at every RPM row.

At fuel cut, with rigid coupling and all quantities referred to crank speed:

```
(I_engine + I_vehicle_reflected) * d(omega)/dt = -(T_engine_drag + T_road)
T_backdrive = T_engine_drag + I_engine * d(omega)/dt
```

Engine drag includes the negative native cycle-mean gas torque plus authored
mechanical friction. The engine inertia is 3.2 kg m², reflected vehicle inertia
27 kg m², friction `35 + 0.025 * RPM` Nm, and reflected road drag 20 Nm. These are
an artistic test load, not measured Ural vehicle, gear ratio or engine-friction
specifications. Fuel release blends drag over the preceding 120 ms pedal-release
curve. After fuel cut the combined inertia loses energy through the drag terms;
wheel reaction supplies positive torque to the engine while vehicle momentum falls.

The resulting nine-second coast ends near **1,617 RPM**. This keeps the existing
engine mechanics and exhaust moving faster than the previous prescribed rundown.
The model uses fixed coupling, no extra exhaust restriction, compression-release
brake, restarted fueling, tire/road simulation, flexible shafts or backlash solver.
It is an audition-host experiment, not new native diesel physics.

## Mechanical reaction sound

The AC component of native motored gas torque also excites an authored casing
radiation path. It uses a 65 Hz high-pass, modes at 140 / 320 / 580 Hz with Q
.85 / 1.1 / .9 and gains .00015 / .00010 / .000045 PCM/Nm, then a 750 Hz low-pass.
Its gain follows reverse torque relative to 140 Nm and a 220 ms coupling takeup.
The same crank integration and fixed output latency as the native consumer align
this excitation to the running engine. Filters stay warm while their output is
exactly bypassed outside the final coast.

This is an authored acoustic hypothesis for the missing loaded mechanical body,
not a measured transmission transfer function. No free-running noise, new firing
events or reference audio are used in this path. Existing exhaust and mechanical
presentation parameters remain unchanged. Gear-load sensitivity is a reasonable
motivation for investigating radiation: [Joshi's gear-noise case study](https://saemobilus.sae.org/papers/gear-whine-noise-due-deformation-a-case-study-2017-01-1122)
reports effects of torque variation and loading. That engine-gear study does not
identify the missing sound or provide Ural parameters.

## Listen and reproduce

Files under `artifacts/diesel-sound/ural-backdrive-20260914/`:

- `coupled-with-reaction-lift-and-coast-48k.wav`: 10-second new lift/coast excerpt,
  beginning one second before lift.
- `before-then-coupled-coast-48k.wav`: previous excerpt 0–10 s, new at 10.5–20.5 s.
- `final/start-revs-first-second-coast-48k.wav`: full 48-second revised sequence;
  final lift at 39 s.
- `coupled-motion-only-lift-and-coast-48k.wav`: diagnostic using the same coupled
  RPM path with the extra casing radiation muted.

All listening copies use the preceding drive audition's single gain of
−5.678079962336634 dB, with endpoint fades only. No per-section normalization or
compression masks the changed coast level. The earlier `candidate/` render is
an intermediate retained experiment; `final/` is the validated result.

```bash
node data/engines/ural4320-mechanical-v8/starter-audition.mjs \
  artifacts/diesel-sound/ural-fresh-20260912/ural4320-mechanical-v8.crankwave \
  artifacts/diesel-sound/ural-backdrive-20260914/final surge drive coupled
```

Choose a fresh output directory. Replace `coupled` with `motion-only` for the
motion control, or `legacy` to reproduce the preceding prescribed rundown.
The original accepted version 11 carrier remains unchanged, SHA256
`1dedf91ae3d7238c756debe244460bafe50607306703eee1a258c295f9818b18`.
The coast model and radiation live in the audition host, not in that carrier or
the shared production runtime. The recorded starter remains the same reused asset.

## Validation

- All raw mixed PCM before 39 s is exactly preserved. The exported listening PCM
  matches through 38.9 s, before the resampler's transition support can differ.
- Starter PCM, source hashes, carrier bytes and native presentation are unchanged.
- Fuel is zero throughout settled overrun. RPM falls monotonically, coupling
  remains engaged and reflected wheel torque stays positive into the engine.
- The coupled-motion diagnostic has exactly the same controls as the full
  candidate; their only PCM difference is the explicit casing-radiation track.
- Focused tests compare the constant-drag model with its independent closed-form
  solution and check zero reaction for uncoupled inertia. Radiation tests verify
  no output with an open coupling, non-reverse torque, or no pressure excitation.
- Audio is finite with export headroom; the full listening peak is 0.3801.

Over 40–47 s, raw RMS changes from −23.14 dBFS in the previous clip to −20.97 dBFS
with coupled motion, then −19.50 dBFS with casing radiation. RPM differs in the
first comparison; these are diagnostics, not perceptual scores. Source-derived
components can interfere, so the added layer does not increase every frequency
or every instant. Listening determines whether the new feel is appropriate.

`verification.json`, `coast-comparison.png`, `compare.py`, `model-test.log` and
both render metadata/control files retain the checks. The final full WAV SHA256 is
`becfcbd7e0e86f839410c142c9547af788a57b2b041dfc166fc66fd28f32199f`.
