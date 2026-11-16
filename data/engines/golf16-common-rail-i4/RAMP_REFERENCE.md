# Golf engine-microphone rev and rundown candidate

The selected rev/rundown target is
[Pole Position Production's T5 engine-microphone mix](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t5-onbrd-1).
`engine-ramp-candidate.json` is a separate version 14 candidate built from
`idle-reference-candidate.json`. Both earlier Golf definitions remain unchanged.
The T7 exterior recording remains the idle reference. These microphone perspectives
have different acoustic transfers; the T5 driving recording does not reach idle.
The changes below are informed by waveform and spectrum analysis. Listening
approval is pending; the measurements do not establish a perceptual match.

## Source and controls

- Publisher: Pole Position Production, SoundCloud track 1119643405.
- Title: VW Golf Diesel - T5 - ONBRD - RAMPS - Drive - ENGINE - Mix.
- Public AAC preview decoded to 44.1 kHz stereo WAV, 28.07292517 seconds.
- WAV SHA-256: `04580414c33e50e724594ddad0110e584aa71b01c70677da9012043cf8110428`.
- Local file: `artifacts/diesel-sound/modern-rev-coast-reference/golf-engine-ramps.wav`.
- Rev-up: approximately 0–11 seconds; rundown: approximately 12–26 seconds.
- [Publisher vehicle page](https://pole.se/product/golf-variant-tdi-2019/):
  2019 Golf Variant, 1.6-litre inline-four turbo diesel.

The harmonic ladder suggests roughly 1,300 → 4,470 → 1,300 RPM, assuming a
four-stroke I4. Early in the recording the loudest line is twice the firing
frequency: at about one second, lines near 46, 92 and 138 Hz imply about 1,380 RPM,
not 2,760 RPM. A local harmonic-ladder fit, constrained by manually inspected
anchors and smoothed over time, supplies a 20 ms RPM trajectory. These are estimates,
not tachometer measurements. The authored fuel control is 0.65 before 11 seconds
and zero thereafter; no source telemetry establishes throttle, fuel cut or gear.

The procedural sweep uses the first 28 seconds of this estimated trajectory.
It reads only the controls and native carrier. No recording, measured impulse
response, pitch-shifted preview or fitted waveform is used as a sound source.
The reference audio appears only in the explicitly labeled comparison section.
The downloaded preview is not a licensed lossless library original; rights remain
with the publisher. Engine and exhaust previews have different edit points and
must not be assumed time-aligned.

## Physical hypothesis and implementation

At matching RPM during the rise and fall, the reference retains a strong low
engine-order tone while its upper detail changes. The previous candidate tied too
much of that body sound to combustion. Fuel cut therefore removed it abruptly.

This revision adds primary and secondary reciprocating inertia for parallel inline
cylinders. Slider-crank inertia is a plausible fuel-independent excitation;
[NPTEL's treatment of multicylinder engine unbalance](https://archive.nptel.ac.in/content/storage2/courses/112101096/Mod%205/Lec1/5.1_3.html)
describes forces arising from reciprocating masses. This does not uniquely identify
the recorded component: engine structure, mounts, accessories, intake/exhaust and
the microphone transfer can also contribute at these orders.

The runtime uses native crank radius 0.04025 m, authored rod length 0.137 m,
firing phases and an authored effective reciprocating mass of 0.45 kg per cylinder.
The primary and secondary approximation is

```
F = m r omega² sum[cos(theta - phase) + (r / rod) cos(2(theta - phase))]
```

For this ideal I4, the primary terms cancel and the secondary terms add, producing
twice crankshaft frequency (RPM / 30 Hz). Excitation follows the existing continuous
crank angle, grows with squared speed, and continues at zero fuel. Authored radiation
uses a 110 Hz bandpass, Q 2.5, gain 0.0004 PCM/N, 140 Hz high-pass and 500 Hz low-pass.
This is an acoustic approximation, without solved mounts, balance shafts, angular
acceleration forces, higher kinematic orders or within-cycle crank-speed dynamics.
The effective mass and transfer are not measured Golf parts or acoustic calibration.

Other changes in this candidate:

- Extend the bake grid to 4,500 RPM, giving 77 cells. The added row holds the 3,800
  RPM reservoir states; it does not infer extra boost or a measured high-speed map.
- Multiply block-mode gains at or above 550 Hz by 1.7 to retain mechanical detail
  alongside the stronger body tone.
- Set coast mechanical gain to 2 and movement gain to 1. Set coast flow texture
  gain to 1.2, high-pass to 200 Hz and low-pass to 1,000 Hz.

The original 70 native pressure/flow/motored-force cells and cylinder bindings
remain exactly equal to the idle candidate. Fueling physics, injection schedules
and original presets are unchanged. The new layer adds a reusable mechanism;
its radiation settings are specific to this candidate.

## Measured result and remaining differences

At approximately 3,150 RPM, compare 6.6–7.4 seconds on the rise with 16.6–17.4
seconds on the fall. Values are rundown minus rise, measured in 0.8-second windows:

| Change at matched RPM | Reference | Previous candidate | New candidate |
| --- | ---: | ---: | ---: |
| Overall RMS | +0.07 dB | −11.59 dB | +0.36 dB |
| 80–200 Hz band power | +0.28 dB | −14.17 dB | +0.76 dB |

The previous comparison uses the same extended grid and the same authored controls.
No per-window gain matching is applied. The spectrogram and level plots apply one
constant gain to each complete 28-second clip, preserving its internal dynamics.

The new body tone follows the reference's rise and rundown much more closely.
There are still clear differences: the late rundown near 2,100 RPM has too little
80–200 Hz energy, the upper detail above about 3 kHz drops more abruptly at fuel
release, and the candidate retains a fixed 382 Hz idle-tuning feature. Beginning
levels and harmonic balance also differ. Changing the unknown fuel schedule can
change these comparisons. Do not infer component identity or subjective quality
from the close overall level curve alone.

## Auditions and verification

Outputs under `artifacts/diesel-sound/golf-ramp-match/`:

- `ramp-audition/rev-rundown-48k.wav`: 28 seconds, entirely procedural, estimated
  source RPM trajectory. One constant gain, −25 dBFS full-clip target, 20 ms endpoint
  fades. Raw peak 0.60846; raw RMS 0.18001; listening gain −10.106 dB.
- `reference-then-candidate-48k.wav`: real recording at 0–28 seconds; 0.5-second
  gap; candidate at 28.5–56.5 seconds. Each complete section is RMS-matched once
  to −25 dBFS and downmixed to mono. `comparison.json` records the source hashes.
- `audition/idle-revs-coast-48k.wav`: separate 18-second control scenario;
  0–4 seconds idle at 750 RPM, 4–8 rise to 3,200, 8–10 held, 10–14 rundown,
  14–18 idle. Idle fuel returns below 950 RPM. This is not the source trajectory.
- `audition/before-after-idle-48k.wav`: five seconds of the previous idle candidate,
  a half-second gap, five seconds of this revision, with each section RMS-matched.
- `final-spectrograms.png`, `final-levels.png`, `final-metrics.json`:
  reference/previous/candidate comparisons.
- `verification.json`, native/integration/runtime logs, control trajectory and
  audition metadata bind the results to the generated carrier.

Carrier SHA-256:
`f8b6c8f72258f2f31a67f27284b74389b1de6021e2ead4bd60d7de17a51fd963`.

Both engine schemas, native bake test, all 77 settled cells, authenticated package
integration and all 159 runtime tests pass. Inertia tests check primary cancellation,
secondary phase, squared-speed scaling, zero-speed force, fuel-independent output,
zero-gain legacy equality, partition invariance and malformed parameter rejection.
The final native carrier produces exactly the exploratory candidate PCM. Prior v13
idle and 28-second sweep PCM remain exactly equal to pre-change captures. Six held
idle/load/fuel-cut operating points, including 4,500 RPM, are finite, unclipped and
exactly reproducible across render partitions.

## Reproduce

From the repository root, using fresh output directories:

```bash
cmake --build build --target crankwave_cli
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/golf16-common-rail-i4/engine-ramp-candidate.json \
  artifacts/diesel-sound/golf-ramp-replay
node data/engines/golf16-common-rail-i4/ramp-audition.mjs \
  artifacts/diesel-sound/golf-ramp-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-ramp-match/reference-controls.json \
  artifacts/diesel-sound/golf-ramp-replay/ramp
CRANKWAVE_IDLE_RPM=750 node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-ramp-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-ramp-replay/audition
```

The retained analysis scripts are under
`artifacts/diesel-sound/golf-ramp-match/analysis/`. RPM extraction requires the
downloaded reference; playback requires only its saved numeric controls and the
native carrier. The controls and audio artifacts are local, ignored outputs.
