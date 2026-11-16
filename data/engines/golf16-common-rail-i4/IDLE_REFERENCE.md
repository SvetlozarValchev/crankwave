# Golf idle reference and candidate

The target is the sustained body sound and softer mechanical firing pattern in
[Pole Position Production's T7 exterior idle and blips recording](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t7-ext-idle).
The previous T6 engine-mounted recording is driving, and cannot validate idle.
The analysis supports reducing the dominant low exhaust harmonics, restoring
low-mid body resonance, and allowing pressure-driven texture to overlap between
firings. Increasing clank gain alone does not address these differences.

The separate `idle-reference-candidate.json` implements that direction. It is an
audition candidate, not an approved replacement for `engine.json`. Judgments here
are based on waveform, spectrum and modulation measurements; subjective listening
approval remains with the user.

## Reference baseline

- Publisher: Pole Position Production; SoundCloud track 1119643360.
- Title: VW Golf Diesel - T7 - EXT - IDLE - Blips - MS Decoded - RSM191.
- Decoded public preview: 13.3980499 seconds, 44.1 kHz stereo.
- WAV SHA-256: `e4983763891773841d11c1744ae7b90a511750b5ff5b2436f4a4c38641f9a6ce`.
- Analyzed sections: 0.4–2.4 seconds and 5.6–8.4 seconds.
- Firing cadence is approximately 25 Hz, consistent with about 750 RPM for a
  four-stroke I4. This is inferred from harmonics, not a recorded tachometer value.
- Local source and acquisition metadata: `artifacts/diesel-sound/modern-idle-reference/`.

The preview is an exterior microphone perspective, including the vehicle's acoustic
transfer, surroundings and recording chain. The engine family is identified by the
publisher; this recording does not identify the exact ECU state, injection schedule,
microphone-to-component transfer or individual noise source. No recording or
measured impulse response is embedded in the procedural candidate. The public
preview remains attributed to its publisher and is used for comparison.

## Measurements and diagnosis

Both reference segments and generated signals are downmixed to mono and analyzed
at 24 kHz with equal -25 dBFS RMS. The displayed spectra use one-second Hann/Welch
windows and 1/6-octave averaging. The spectrogram uses 512 samples, a 32-sample hop,
and the same -95 to -45 dBFS/Hz color scale for each signal. A separate longer-window
spectrogram examines the low-mid frequencies across the complete idle/blips take.

Envelope analysis band-passes each region, low-passes squared amplitude at 120 Hz,
and takes its square root. Modulation depth is the amplitude of the envelope's
component at the firing frequency divided by mean envelope level. Analysis removes
window edges. These diagnostics describe balance and timing; they are not a
perceptual similarity score or an estimate of real sound pressure level.

| Measurement | Reference, two idle sections | Previous at 750 RPM | Candidate at 750 RPM |
| --- | ---: | ---: | ---: |
| 20–80 Hz share of signal power | 5.7–8.2% | 53.7% | 7.4% |
| 200–500 Hz share of signal power | 37.4–47.8% | 6.6% | 41.5% |
| 200–600 Hz envelope, firing-rate depth | 0.05–0.12 | 0.72 | 0.19 |
| 1.5–4 kHz envelope, firing-rate depth | 0.33–0.37 | 0.20 | 0.32 |
| 200–600 Hz envelope, whole-cycle depth | 0.03–0.05 | 0.05 | 0.17 |

At the previous audition's 800 RPM, low bass still accounts for 32.5% and 200–500 Hz
only 6.0%. Changing RPM alone therefore cannot repair the balance. Isolated layers
show that the old exhaust dominates low-frequency energy, while the upper mix has
substantial intake noise and brief valve contacts. Individual pressure-texture bursts
decay strongly before the next firing. The reference has a more sustained low-mid
bed, with identifiable but less sharply separated upper mechanical events.

A strong feature near 382 Hz stays close to that frequency while firing harmonics
sweep during the throttle blips. This suggests a contribution other than a simple
fixed firing order, but does not uniquely distinguish structural resonance,
accessory noise or a recording/environment feature. The candidate uses a damped
pressure-excited resonance as a testable approximation. It does not add an arbitrary
continuous tone or claim to have identified that component.

Across seven broad bands, mean absolute level error against the two-section average
falls from 3.86 dB to 0.85 dB. The remaining whole-cycle modulation becomes worse,
as shown explicitly in the table. A good spectrum alone is insufficient evidence
that the sound matches.

## Candidate implementation

- Add pressure-driven body modes at 130, 300 and 382 Hz, with Q 4, 6 and 30.
  Keep the native cylinder sources, cylinder couplings and fuel trims unchanged.
- Use a 100 Hz block high-pass and 6.5 kHz low-pass. Retain the principal upper
  modes and move the weakest high mode to 4.6 kHz.
- Raise exhaust high-pass from 60 to 200 Hz. The low-fuel path uses a 250 Hz
  low-pass, gain 0.9 and threshold 0.3. This reduces the lowest harmonics and sharp
  low-mid exhaust pulses while retaining rotating-engine flow.
- Activate rounded stochastic pressure radiation by 0.12 fuel, using a 7 ms attack
  and 25 ms decay. A first trial that changed only modal balance made the bursts
  sharper; smoothing their excitation is necessary as well as spectral shaping.
- Reduce valve gain from 0.035 to 0.020, extend contact from 0.65 to 0.8 ms and ring
  decay from 2.2 to 3.5 ms. Reduce intake-proxy gain from 0.020 to 0.012. The aim is
  to retain mechanical detail while reducing short secondary pulses and masking.

The previous runtime admission rules restricted rounding to above-idle fuel levels.
Version 13 explicitly allows the existing rounding envelope at idle. It retains
the v12 source layout and DSP: no new noise generator, sample playback or extra
impact event. Versions 11–12 retain their old admission limits. Existing definitions
continue to select their previous versions and reproduce the same sound.

## Remaining differences and next decisions

The 200–600 Hz region still has too much whole-cycle movement, while some upper
detail is stronger and more regular than the reference. The current modal bank
shares one combustion excitation and cylinder weighting. If listening confirms a
remaining low-mid wobble, the next useful change is separate excitation/decay and
cylinder weighting for sustained body radiation and sharper combustion detail.
Further global gain or noise increases would not resolve that separation.

RPM is prescribed at 750; a governor and within-cycle crank-speed ripple are not
solved. Those could explain some difference in harmonic width and cadence, but
these short recordings do not identify an appropriate physical calibration.
Additional steady idle recordings from engine and exhaust microphones would help
separate mechanical sources from perspective and background contributions.

The candidate's rounding remains active above idle, and its new body modes also
affect revving/coasting. The full sweep is supplied for that check. Matching the
idle metrics does not establish a match under load or justify promoting the preset.

## Listen and inspect

All paths below are relative to `artifacts/diesel-sound/golf-idle-match/`:

- `reference-before-candidate-48k.wav`: reference 0–2.8 s, previous 3.2–6.0 s,
  candidate 6.4–9.2 s. Each section is mono and independently matched to -25 dBFS
  RMS with 20 ms boundary fades. No pitch shifting, time stretching, EQ, denoising,
  compression or looping is applied to these listening copies.
- `audition/idle-48k.wav`: five seconds of candidate idle.
- `audition/idle-revs-coast-48k.wav`: 0–4 s idle, 4–8 s acceleration to 3200 RPM,
  8–10 s held, 10–14 s coast and return of idle fuel, 14–18 s idle.
- `after-spectrograms.png`, `after-analysis.png`, `reference-lowmid-over-blips.png`.
- `after-metrics.json`, `comparison.json`, `verification.json`, `analyze.py`.

The comparison contains the source recording only in its labeled reference section.
The candidate and its standalone audition are generated entirely by Crankwave.

## Reproduce and verify

Use fresh output directories:

```bash
cmake --build build --target crankwave_cli crankwave_diesel_sound_tests -j4
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/golf16-common-rail-i4/idle-reference-candidate.json \
  artifacts/diesel-sound/golf-idle-match-reproduction
CRANKWAVE_IDLE_RPM=750 node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-idle-match-reproduction/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-idle-match-reproduction-audition \
  artifacts/diesel-sound/ural-rounded-loaded/verified/ural4320-mechanical-v8.crankwave \
  artifacts/diesel-sound/golf-clank/verified/golf16-common-rail-i4.crankwave
```

Both schemas, native tests, 156 runtime tests and the full candidate integration
passed. The package contains 70 settled cells. Candidate native sources and
cylinder bindings are byte-identical to the preceding Golf. Old idle WAVs generated
before the runtime change reproduce byte-for-byte afterwards. The candidate also
passes deterministic partitioned playback, fuel-cut behavior and finite/unclipped
captures at idle, load and coast. These checks establish implementation correctness,
not subjective audio approval.

Candidate carrier SHA-256:
`d1192f396c0cedbdb2e29e6c889b0b3bb6a36a4f836f03249301d086ced13043`.
