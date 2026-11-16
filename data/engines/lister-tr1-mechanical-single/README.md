# Lister TR1-style mechanical single

A separate procedural diesel for checking variety alongside the accepted Ural V8.
It uses the existing version 11 renderer with one cylinder, smaller geometry,
different injection timing, a single exhaust outlet and its own acoustic tuning.
The intended character is a spaced single-cylinder chug with exposed mechanical
texture. Perceptual approval is pending listening feedback.

## Reference and hardware basis

[Benboncan's “Lister Startup And Idle.wav”](https://freesound.org/people/Benboncan/sounds/89724/)
is a 19.931-second field recording of a stationary Lister firing and then idling
as its centrifugal governor engages. The ordinary public player supplies an MP3
preview. The recording does **not** identify a TR1, cylinder count or RPM. It is
a character reference, not evidence that this preset reproduces that exact engine.
The recording is by **Benboncan**, licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The working hardware comes from the manufacturer's
[TR Industrial Engines technical sheet](https://listerpetter.com/wp-content/uploads/2025/10/10739-TR-Industrial-Engines-TDS.pdf),
printed pages 4–6, and the mechanically injected TR1 research setup in
[Fayad's 2017 thesis](https://etheses.bham.ac.uk/id/eprint/7919/1/Fayad17PhD.pdf),
printed page 74 / PDF page 99.

| Parameter | Working value | Basis |
| --- | --- | --- |
| Cylinders | One, four-stroke | TR1 technical sheet and research setup |
| Bore × stroke | 98.42 × 101.6 mm | Manufacturer |
| Displacement | 0.773 L | Manufacturer |
| Compression ratio | 15.5:1 | Manufacturer |
| Injection / aspiration | Direct injection / naturally aspirated | Manufacturer |
| RPM grid | 850–2,500 | Manufacturer idle minimum and variable-speed range |
| Start of injection | 22° BTDC | Thesis test setup; approximate hydraulic timing here |
| Full fuel | 29 mg per cycle | Inferred from published fuel consumption and density |

The manufacturer gives 1.6 L/h at 1,500 RPM and 2.5 L/h at 2,500 RPM with
0.84 kg/L fuel. One firing per two revolutions implies about 29.9 and 28.0 mg
per firing respectively. The constant 29 mg approximation does not reproduce
a measured pump delivery map. Valve windows/areas, rod length, thermal conditions
and combustion coefficients remain authored, as recorded in `engine.json`.

## What makes this different

At 850 RPM, the single fires about 7.08 times per second. The Ural preset's eight
cylinders fire about 48.3 times per second at its 725 RPM audition idle. At the
same 1,500 RPM, those rates are 12.5 versus 100 firings per second. This changes
the temporal structure before acoustic filtering.

The single's modes cover 300–3,100 Hz, with a shorter single lossy exhaust path,
an 80 Hz exhaust high-pass and its own block/exhaust balance. The initial draft
was dominated by low exhaust energy. Tuning reduced that emphasis and brought
the block and valve texture forward. Smooth valve contacts and rounded loaded
pressure envelopes reuse the Ural work; the single has no inter-bank weighting
contrast. Fuel cut retains valve, intake and flow-driven exhaust texture.

The carrier contains generated pressure/flow and authored parameters. It embeds
no reference recording or measured impulse response. This is a warm acoustic
approximation: no cold start, solved mechanical governor, within-cycle speed
ripple, cooling fan model or validated TR1 torque curve is claimed. The reference's
starting/governor sequence is not simulated. Use manual RPM/Fueling controls in
the harness; its vehicle driving model is not a stationary-engine governor.

## Reproduce and listen

From the repository root, using fresh output directories:

```bash
mkdir -p artifacts/diesel-sound/lister-tr1-variety
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/lister-tr1-mechanical-single/engine.json \
  artifacts/diesel-sound/lister-tr1-variety/verified
node data/engines/lister-tr1-mechanical-single/audition.mjs \
  artifacts/diesel-sound/lister-tr1-variety/verified/lister-tr1-mechanical-single.crankwave \
  artifacts/diesel-sound/lister-tr1-variety/audition \
  artifacts/diesel-sound/ural-rounded-loaded/verified/ural4320-mechanical-v8.crankwave
```

The optional last argument adds the Ural comparison; `ffmpeg` is required to
export the 48 kHz listening files. The main 18-second audition contains idle
at 0–4 s, acceleration at 4–8 s, held 2,000 RPM at 8–10 s, prescribed fuel-cut
deceleration at 10–14 s, and idle at 14–18 s. `controls.json` records every input.
The listening copy uses a single gain for the whole clip, targeted at −23 dBFS
RMS with peak headroom. The raw output is also saved.

The comparison has four five-second sections separated by half a second:

| Time | Engine / operating point |
| --- | --- |
| 0–5 s | Ural idle, 725 RPM / 0.12 fueling |
| 5.5–10.5 s | Single idle, 850 RPM / 0.12 fueling |
| 11–16 s | Ural loaded, 1,500 RPM / 0.65 fueling |
| 16.5–21.5 s | Single loaded, 1,500 RPM / 0.65 fueling |

Each comparison section uses a constant gain targeted at −25 dBFS RMS, with
20 ms boundary fades. No compression, per-cycle leveling or additional sound is
applied. `audition.json` records the gains, levels and carrier hashes. RMS matching
is a comparison aid, not a calibrated perceived-loudness or microphone model.

## Initial verification

Both engine schemas, all 49 native operating cells, package integrity, exact
held-state replay, fuel response, gas-exchange TDC pressure and swept playback
passed the existing integration check. Browser Worker playback also passed the
harness integration test (which uses an AudioContext test double). These checks
establish runtime operation, not subjective sound quality or a measured match.

Initial carrier SHA-256:
`a5e7a3a8e5a7f27db4f4d8ea516e0ade36af134f60db9b1b44c74a34bcbfd2b8`.
The accepted Ural comparison carrier is
`c2e4aa4235cd3908412aa0770f9cee6da2982a488009c5f6db2faece5fd65d8e`.
The implementation starts from commit `be92a8a` on branch `tuning/diesel-variety`.
