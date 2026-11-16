# UAZ-469 petrol engine sound

An original Engine Simulator model of the early UMZ-451M-family, carbureted
2.45-litre OHV inline four. The sound follows the supplied real-engine
reference, with a heavy firing beat, rounded bass clatter, and restrained
upper rasp. The final voicing gives more weight to 60–250 Hz and softens
1–4 kHz following listening feedback. It includes a four-speed transmission
and a UAZ-469 vehicle with portal-axle reduction in high transfer range.

## Run

The engine is selected in `assets/main.mr`; the standalone entry is
`assets/uaz469_main.mr`. To select it again later, use
`import "engines/uaz469_cleanroom.mr"` in `assets/main.mr`, alongside the
existing library/theme imports and `main()` call. Press **Enter**
to reload, **A** for ignition, and hold **S** until it catches, then release.
Start at closed throttle; **Q/W/E/R** control throttle in the desktop app.

The user-designated **Candidate 01** is preserved independently in
`assets/engines/uaz469_candidate_01.mr`, with standalone entry
`assets/uaz469_candidate_01_main.mr`. Its WAV, EQ, generator, and validation
manifest are frozen under `assets/sound-library/uaz469/candidate_01/`.
It is the bass-focused version saved before further rasp reduction.

The separate `assets/engines/uaz469_reference_3m.mr` variant targets the
reference video around 3:00. Its standalone entry is
`assets/uaz469_reference_3m_main.mr`; its independent filter and comparison
notes are in `assets/sound-library/uaz469/reference_3m/`. The original
`uaz469_cleanroom.mr` and its sound assets remain the preserved version.

The WebAssembly asset list includes the entry, engine, and exhaust WAV. After
rebuilding the WebAssembly target, callers can select
`/engine-sim/assets/uaz469_main.mr` through `esw_create`.

## Provenance and specification

The engine-specific graph and acoustic filter were written for this project.
No external UAZ simulator implementation, engine mod, or measured impulse
response was used. The user-supplied [UAZ Martorelly video by ALEX F.](https://www.youtube.com/watch?v=GDxEeeixRAY)
was analysed at 0:55–0:59 and 1:13–1:16 to guide cadence and magnitude EQ.
Only smoothed frequency-band measurements enter the acoustic profile; no
recorded waveform, phase, engine loop, or reference sample enters the assets.
This implementation uses the repository's existing
engine language, compiler, physics, and synthesizer; it is not a replacement
for that software or a claim of a formally audited clean-room process.

The [factory UAZ-469 operating manual, reproduced in the 1985 military
edition](https://djvu.online/file/g5jchgos1oDSL), printed pages 12–13 and 55,
provides the shared engine-family dimensions and warm-idle specification.
The [1976 UAZ-469/469B technical book by Orlov, Varchenko and
Vinokurov](https://djvu.online/file/OWQKOgdUHw5ix), printed pages 3 and 6,
identifies the earlier UMZ-451M and its vehicle/transmission specification.
The 1985 manual describes the later 414-family engine, not the earlier model.

| Public reference | Model |
| --- | --- |
| Four-stroke petrol inline four, overhead valves | Four cylinders, one bank, effective OHV valve curves |
| 92 mm bore × 92 mm stroke, nominal 2,445 cc | 92 × 92 mm; geometric displacement is approximately 2,446 cc |
| Compression ratio 6.7:1 | Clearance volume calculated from swept volume |
| Firing order 1-2-4-3 | 0°, 180°, 360°, 540° over a 720° cycle |
| 75 metric hp / 55.2 kW at 4,000 rpm | Calibration reference, not a certified output |
| 17 kgf·m / 166.7 Nm at 2,200–2,500 rpm | Calibration reference |
| Factory warm idle 550–600 rpm | Set to approximately 765 rpm to follow the supplied audio reference; no governor |
| Ratios 4.124 / 2.641 / 1.58 / 1.0; axle total 5.38 | Four forward gears; high transfer range |

Port flow, effective cam profiles, spark advance, runner and exhaust geometry,
reciprocating/rotating masses, combustion variation, and acoustic parameters
are estimates calibrated in the simulator. The video is an acoustic target,
not a calibrated measurement or verification of the exact engine fitted to
that vehicle. The model does not separately synthesize valve lash, fan,
gearbox, choke, or vacuum
advance. Its 4,500 rpm ignition cut is a simulator safeguard; the original
contact ignition did not have this electronic limiter.

## Synthetic exhaust response

`tools/generate_uaz469_ir.py` creates an original response using damped modes,
filtered deterministic noise, and short decay envelopes. The base uses
240/500/920/1,550/2,250/3,150 Hz modes with 2–5 ms decay. It contains no engine
loops: pulse rate and throttle response come from the running simulation.

The generator then applies `assets/sound-library/uaz469/reference_eq.json` to
the base magnitude response and constructs a causal minimum-phase filter.
The profile uses smoothed third-octave reference differences as a starting
point, with the final balance chosen for a rounded clatter. Relative
to the preceding resonance-corrected version, Candidate 01's filter raises
80–250 Hz by approximately 2–3.5 dB and reduces 1.5–4 kHz by approximately
6–10 dB. Its 25 Hz response changes by less than 0.1 dB. The 500–1,000 Hz
range is also restrained to avoid replacing the rasp with a hollow buzz.
The current working filter trims that bass emphasis by approximately 2.5 dB
at 25–63 Hz, 2 dB at 80 Hz, 1.5 dB at 100 Hz, and 0.5 dB at 160 Hz, fading
to zero by 250 Hz. This modest trim preserves the restrained midrange and
treble response while easing the boom of the pressure-led working version.
The working engine uses derivative gain 0.00012, noise 0.08, and jitter
0.010. These reduce the edge boost and synthetic grain before convolution;
Candidate 01 retains 0.0020, 0.14, and 0.030 respectively. Combustion variation and firing intervals still
provide the pulse texture; this EQ does not turn the petrol model into a
physical diesel model.

The 1,550 Hz mode uses strength 0.016 and 5 ms decay; the 2,250 Hz mode uses
0.009 and 4.5 ms. A finer comparison exposed an excessive narrow 1,550 Hz
peak in the earlier 0.034 / 11 ms mode. These revisions reduce the filter's
response at 1,550 Hz by 10.0 dB and at 2,250 Hz by 4.0 dB relative to that
version, with about 0.2 dB change at the lowest firing harmonics.

After that EQ, a positive, unit-sum Hann kernel rounds the pulse attack.
`attack_smoothing_ms: 0.28` produces 13 causal taps at 44.1 kHz. Relative to
the preceding EQ version, the compensated filter changes the 25/51 Hz bass
by less than 0.1 dB, reduces 1,550 Hz by 1.0 dB, 3 kHz by 3.9 dB, and 4 kHz
by 7.2 dB. This keeps the lower metallic ring while softening the sharper
upper tones. It adds about 0.14 ms of delay, not a separate echo.

The resulting WAV is mono PCM16 at 44,100 Hz with 6,144 samples (139 ms),
within the synthesizer's 10,000-tap limit. Its gain is compensated by the
engine's `impulse_response.volume`. Default regeneration requires NumPy;
`--base-only` uses only Python's standard library and emits the previous
uncorrected 4,096-sample response. Reference audio is not required to regenerate:

```sh
python3 tools/generate_uaz469_ir.py
```

## Reference analysis

At 0:55–0:59, spectral peaks near 25.5 and 51 Hz indicate approximately
765 rpm if the first peak is the firing frequency of a four-stroke inline
four. This is an inference from audio, not tachometer data. The earlier model
idled near 571 rpm (19 firing pulses per second). At 1:13–1:16, the reference
firing frequency rises to roughly 70 Hz, corresponding to about 2,100 rpm.
The preview now uses a gradual small throttle opening instead of large blips.

Compared at similar RPM, the earlier sound concentrated too much energy in
120–500 Hz and too little in 1–4 kHz. The reference's upper-frequency envelope
also varies at the firing cadence. Its low beat and pulse-modulated upper
texture guide this correction; increasing random rattle alone would not
address the tonal mismatch.

`tools/analyze_uaz469_reference.py` measures both windows independently after
normalizing their energy over 20–8,000 Hz. It reports broad-band and
third-octave differences, crest factor, and upper-band envelope variation,
and produces a comparison plot. It requires NumPy, SciPy, and Matplotlib.
For a locally available reference excerpt starting at video time 0:50:

```sh
python3 tools/analyze_uaz469_reference.py \
  --reference build-uaz469-reference/reference-50-80.wav --reference-offset 50 \
  --simulation build-uaz469-preview/uaz469-preview.wav \
  --output build-uaz469-reference/final-analysis
```

The recording includes microphone response, codec effects, and background
sound, especially between idle harmonics and above 4 kHz. Those differences
are not a reason to inject continuous hiss into the model. Spectrum matching
alone does not establish a perceptual match, and this engine does not have a
separate valve-train or mechanical-rattle source.

In Candidate 01's production renders, 1.5–4 kHz energy relative to the 60–250 Hz
body is 10.2 dB lower at idle, 9.9 dB lower during the gentle rev, and 9.7 dB
lower during sustained overrun than in the preceding version. Normalized
sample-to-sample slope is lower by 7.8–8.5 dB across those windows. These
ratios measure a change in balance and attack, independently of overall
playback gain. This intentionally softer balance follows listening feedback;
minimizing spectral error against all the sound in the video is not the goal.

Before that bass trim, the working-engine source change reduced 350–1,200 Hz buzz
relative to 60–250 Hz body by 2.8 dB at idle, 9.7 dB during the gentle rev,
and 9.0 dB during sustained overrun versus Candidate 01. Its 1.5–4 kHz/body
ratio falls by 0.6, 4.6, and 7.4 dB respectively. The smaller upper-band change
at idle is a limit of this adjustment; it is not a claim to have eliminated
all rasp. Candidate 01's separately loaded preview was verified to produce
exactly the saved PCM, and its archived generator reproduces its frozen WAV.

### Remaining source and pulse-texture differences

The running-engine input in `PistonEngineSimulator::writeToSynthesizer()`
comes from exhaust-runner pressure and dynamic pressure. `Synthesizer`
adds derivative gain, jitter, pressure-modulated noise, and convolution.
This build has no separate audible source for the engine block, valve gear,
engine mounts, or body-panel vibration. Real vehicle sound includes distinct
engine-surface, intake/exhaust, and vibration-transmission contributions;
see [HBK's powertrain source/path description, page 3](https://www.bksv.com/doc/bg1800.pdf).
Their absence is a plausible part of the remaining mismatch, but the video
does not establish which physical structure contributes a particular sound.

The analyser writes `texture-comparison.png` and resolves 1.2–3.8 kHz tones
with a 16,384-point Welch spectrum, independently normalized to the band
mean. It also reports the upper-band envelope after 5 ms smoothing. Earlier
comparisons exposed both an excessive narrow resonance and a more strongly
pulsed upper-band envelope than the reference. The current EQ reduces the
level of that upper texture; it does not add an independent mechanical source
or claim a chassis simulation. Background noise and differing rev trajectories
also affect the metrics. These are numerical comparisons, not direct listening
or a verified perceptual match.

## Preview and validation

Build the production native driver, then render the preview:

```sh
cmake -S . -B build-web-native -DCMAKE_BUILD_TYPE=Release \
  -DENGINE_SIM_BUILD_APP=OFF -DENGINE_SIM_BUILD_TESTS=OFF \
  -DENGINE_SIM_BUILD_BAKER=OFF -DDISCORD_ENABLED=OFF -DDTV=OFF \
  -DPIRANHA_ENABLED=ON -DENGINE_SIM_NATURAL_GAS_RESONANCE=OFF
cmake --build build-web-native --target engine-sim-web-parity-native --parallel
python3 tools/render_uaz469_preview.py
```

The renderer writes into the ignored `build-uaz469-preview/` directory:

- `uaz469-preview.wav`: 24 seconds of cranking, free idle, one gentle rev,
  recovery to idle, and ignition-off shutdown. No dyno hold is used.
- `uaz469-loaded.wav`: startup followed by full-throttle held points from
  1,000 to 4,000 rpm, each lasting three seconds.
- `uaz469-coast.wav`: loaded running followed by closed-throttle overrun
  from 2,200 to 1,000 rpm at 11–16 seconds, then free idle and shutdown.
  A motor-driven dyno approximates the wheels driving the engine; this is
  an acoustic diagnostic, not a simulated vehicle coast-down.
- Matching control schedules, raw PCM, JSONL telemetry, and `validation.json`.

Checks cover starter release, sustained combustion, idle RPM, acceleration,
idle recovery, shutdown silence, positive loaded torque, audio duration, and
peak amplitude. The coast check also requires negative torque and low
manifold pressure during overrun. The report records the seed, PCM hashes, and measured dyno
points. These validate this simulator's behavior, not acoustic fidelity to a
physical UAZ. Results use the damped gas model; the optional natural-resonance
build changes the sound and requires separate validation.

The native damped build passed all checks with seeds 469 and 470. Seed 469
idled at 764 rpm, reached 2,215 rpm on the gentle rev, recovered to 763 rpm,
and produced zero PCM after shutdown. Loaded results were 163.0 Nm at
2,200 rpm, 165.9 Nm at 2,500 rpm, and 54.3 kW near 4,000 rpm. Peak PCM16
amplitudes were 13,192 for the preview, 27,445 loaded, and 21,677 during the
coast scenario, all below saturation. Overrun produced -27.1 Nm at a mean
manifold pressure of 14.4 kPa. Seed 470 idled at 765 rpm and passed the same
functional and audio checks. The final IR regenerated byte-for-byte.

WebAssembly compilation was not validated in this workspace: the existing
build refers to the missing `/tmp/engine-sim-emsdk` toolchain. Restore the
pinned toolchain described in `tools/web/README.md` before rebuilding browser
artifacts; existing `.js/.wasm/.data` files do not contain this new engine.
