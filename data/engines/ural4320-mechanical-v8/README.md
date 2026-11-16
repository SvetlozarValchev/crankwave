# Ural-4320 mechanical V8 candidate

A procedural warm diesel candidate aimed at an exhaust-microphone perspective.
The user supplied Ural-4320 recordings after rejecting the initial generic diesel
as a buzz. [Pole Position's recording page](https://pole.se/product/ural-4320/)
identifies its vehicle as a 14.9-litre V8. Its
[Soundsnap exhaust recording](https://www.soundsnap.com/ural_4320_truck_onboard_turning_on_medium_speed_driving_stopping_and_turning_off_exhaust_perspective)
is the intended comparison reference. The separate
[SmartSound/Zapsplat recording](https://www.zapsplat.com/music/ural-4320-driving-and-accelerating/)
does not establish the same engine variant.

The working hardware is a YaMZ-238M2-style V8. The manufacturer's
[2010 operation manual](https://sdsyar.ru/instructions/rukovodstvo_expl_YAMZ_236-m2-238m2-2010.pdf)
(printed pages 6, 30 and 38) supplies the 130 × 140 mm geometry, 16.5:1 compression,
1–5–4–2–6–3–7–8 firing order, and pump delivery/timing basis recorded in
`engine.json`'s provenance. Static pump timing approximates hydraulic injection
timing here. Dynamic advance, rod length, valve areas/windows, thermal conditions,
combustion coefficients and acoustic paths remain authored approximations.
The recording does not establish this exact engine model or its calibration.

The cylinder admits overlapping valve windows. This fixture uses intake
340–596° and exhaust 124–380° from firing TDC, with authored effective areas.
These replace the first audition's restrictive windows, which recompressed
exhaust near gas-exchange TDC and produced an excessive surge on fuel cut.
Both ports conserve mass, oxygen and enthalpy during overlap; tests also check
the result as the gas integration grid is refined.

The presentation keeps the two banks separate through lossy delay paths, combines
dry flow with a response to changes in flow, and adds seeded, flow-dependent
texture. Version 5 separates combustion pressure from motoring compression before
exciting the block. Each cylinder's positive pressure increments drive an 18 ms
decaying stochastic envelope, with separate coherent and compression contributions.
This changes the fine structure between burns instead of repeating the same sharp
waveform. Broadly damped modes avoid the rejected idle's concentrated ringing.

The exhaust uses a 30 Hz high-pass, 1,300 Hz low-pass, and separate bank delays with
negative reflections. A dominant bank represents the chosen exhaust perspective.
Flow texture occupies a lower band, and the output gain leaves headroom under load.
All audio derives from generated pressure/flow, crank-timed impacts and seeded excitation;
no recording, reference waveform table or measured impulse response is embedded.

Version 7 preserves the accepted version 5 combustion and exhaust parameters and
adds a separate valvetrain and intake layer. Valve-closing events use the existing
340–596° intake and 124–380° exhaust windows. The first mechanical audition's
instantaneous impacts are the main suspect in the reported crackles; this version feeds the
valve resonances with smooth 1.2 ms contact pulses, a 650 Hz base resonance and
1.8 ms decay. The intake breath stays quieter than the valve layer. Both remain
active at zero fueling while the crank rotates. These are authored acoustic
approximations; valve contact forces and intake turbulence are not physically
simulated. Versions 5 and 6 remain playable with their original sound. See
[BASELINE.md](BASELINE.md) for the preserved approved package and checkpoint.

Version 8 reduces the exhaust emphasis at idle and on fuel-cut coasting, which
the user described as sounding brake-like. A quieter 550 Hz exhaust path is mixed
in below 0.2 fueling, with a 40 ms transition time. Idle receives a partial mix;
zero fuel uses the full coasting response. The accepted combustion and mechanical
layers remain active, and loaded revving bypasses this extra transfer. The existing
gas model and physical pressure/flow captures are unchanged.

Version 9 restores coasting movement after version 8 became too quiet and hollow.
Below 0.06 fueling it mixes the original exhaust through separate low-rumble and
upper-pulse gains, split at 180 Hz, and brings the existing valve and intake layers
forward by 1.6 times. It keeps the 40 ms fades and the smooth valve-contact model.
Idle and loaded playback retain version 8's sound; physical captures are unchanged.

Version 10 adds a separate coasting exhaust texture after comparison with the
user-selected gear-change passages around 0:08 in the publisher's
[exhaust preview](https://soundcloud.com/polepositionproduction/ural-4320-onboard-driving)
and [engine preview](https://soundcloud.com/polepositionproduction/ural-4320-engine-mics-driving).
Version 9's coast retained concentrated mechanical tones but too little broad flow
texture. Each bank's positive captured exhaust flow now drives an independent
seeded excitation, smoothed over 2 ms and filtered to 250–1,800 Hz, with gain 0.4.
The existing coasting fade blends it in below 0.06 fueling. Idle, loaded output,
valve contacts, compression and the coherent exhaust paths retain their previous
parameters. This is an authored turbulence approximation; the previews do not
provide synchronized microphones or measured fuel, brake or RPM traces.

The earlier idle-only gain adjustment was rejected and removed. This definition
uses the corrected reference to guide a new excitation and exhaust response.
It remains an experimental acoustic approximation, not a measured Ural reproduction.

Version 11 integrates the softer rev texture accepted after the first bank-pulse
experiment was too sharp. Under load, stochastic combustion radiation uses a
4 ms rounded attack, 12 ms decay and energy-normalized dominant/secondary bank
weights of 1:0.55. It fades in between 0.2 and 0.5 fueling with a 40 ms transition.
The quieter idle and coasting retain their accepted response; coherent pressure,
exhaust flow, valve contacts, intake breath and the physical captures are unchanged.
See [REV_CHARACTER.md](REV_CHARACTER.md) for the reference analysis and listening
feedback that led to this choice, and [COASTING_BASELINE.md](COASTING_BASELINE.md)
for the earlier accepted checkpoint.

```bash
mkdir -p artifacts/diesel-sound
build/crankwave bake-crankwave \
  --engine data/engines/ural4320-mechanical-v8/engine.json \
  --output artifacts/diesel-sound/ural4320-mechanical-v8.crankwave
node tools/render-crankwave.mjs \
  artifacts/diesel-sound/ural4320-mechanical-v8.crankwave \
  artifacts/diesel-sound/ural-idle.wav 650 0.12 4
node scripts/serve-crankwave-harness.mjs
```

Load the carrier in the harness. Its 63 operating points cover 550–2,275 RPM and
zero through full fueling. Manual mode selects RPM and Fueling. Drive mode uses
an authored vehicle/idle-fueling approximation, not a measured Ural governor.
Existing output paths are never overwritten by the bake or WAV utility.

The first Soundsnap download was misidentified: its 6.2 seconds contain a spoken
session-expiry message, not engine audio. All comparisons against that file are
invalid. Opening the page and requesting its public player stream within the same
HTTP session subsequently returned the 90.7-second MP3. Publisher startup previews
provide additional microphone perspectives. The corrected stream guides the current
acoustic tuning through spectral, temporal and audio-embedding comparisons; its RPM
is unmeasured. Those descriptors do not establish subjective sound quality.
Reference files remain local analysis material, outside the tracked definition.
Numerical descriptors do not establish a perceptual match.
Physical cold starting, stopping and a complete vehicle lifecycle are not implemented.

See [the model and playback contract](../../../docs/model/MECHANICAL_DIESEL_SOUND.md)
for supported paths, pressure and mechanical texture, exhaust semantics, validation
and limits.

The [packaged harness driving setup](HARNESS_DRIVE.md) includes the reused heavy
recorded starter, harder pickup and accepted engaged coast in a single file.
It uses the updated harness's Start engine button and authored heavy-truck driving
controls. The [original starter audition](STARTER_AUDITION.md) remains reproducible.
