# Golf 1.6 TDI-style common-rail diesel

A separate modern passenger-car diesel audition: a 1.6-litre four-cylinder with
two pilot injections, a main injection, prescribed load-dependent boost, restrained
mechanical clatter and a single silenced exhaust path. Its intended character is
smoother and smaller than the accepted Ural, with a more regular cadence than the
Lister single. Listening approval is pending.

The current experiment is `engine-upper-metal-candidate.json`, following the
user's identification of the real recording's **upper metallic texture** as the
missing character. It adds restrained, short pressure-driven accents with
mode-specific cylinder radiation weights. See [the upper-metal revision](UPPER_METAL_REFERENCE.md)
for the real/generated listening comparison, measured differences and verification.
Listening approval is pending; previous definitions remain available.

The preceding `engine-loaded-clatter-candidate.json` was rejected for the wrong
character. Its similar pulse on every firing motivated the
[cycle-pattern diagnosis](CHARACTER_DIAGNOSIS.md). It is retained as a comparison,
not an accepted baseline. See [its record](LOADED_CLATTER_REFERENCE.md).

The preceding audition is `engine-rattle-candidate.json`, which brings
forward valve-contact radiation and refocuses the upper block resonances after
feedback that the ramp candidate lacked metallic rattle. See
[the rattle revision](RATTLE_REFERENCE.md) for the exact changes, comparison WAVs
and verification. It retains the ramp candidate's physics, inertia and exhaust.

## Reference

The selected rev/rundown baseline is
[Pole Position Production's T5 onboard engine-microphone ramp](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t5-onbrd-1):
rev-up approximately 0–11 s, rundown 12–26 s. The separate
`engine-ramp-candidate.json` adds version 14 inline reciprocating inertia and
extends the bake grid to 4,500 RPM. [The ramp analysis](RAMP_REFERENCE.md) documents
the physical hypothesis, measured comparisons, remaining differences, auditions
and reproduction. Earlier definitions are preserved; listening approval is pending.

The idle baseline is now
[Pole Position Production's T7 exterior idle and blips preview](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t7-ext-idle).
Use approximately 0–2.5 s or 5.5–8.5 s for idle. Its firing cadence suggests about
750 RPM. [The idle analysis](IDLE_REFERENCE.md) records the source hash, spectrum,
pulse-envelope comparison, remaining differences and reproduction commands.
`idle-reference-candidate.json` is a separate experimental version 13 preset;
`engine.json` retains the preceding clanking version unchanged.

[Listen to Pole Position Production's engine-microphone preview](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t6-onbrd-steady).
The publisher identifies the vehicle as a
[2019 Golf Variant TDI with a 1.6-litre inline-four turbo diesel](https://pole.se/product/golf-variant-tdi-2019/).
The preview is titled “VW Golf Diesel - T6 - ONBRD - STEADY RPMS - Drive - ENGINE -
Right - PZM”, track 1119643381. Its decoded public preview is 15.395 seconds.
This is a driving recording; it does not identify idle RPM, engine code or ECU maps.
All rights remain with the publisher. No preview audio or measured impulse response
is embedded in this preset or its generated carrier.

The earlier EA288 1.6 TDI geometry is documented in Volkswagen's
[Self-study Programme 891143C, printed page 20](https://www.vaglinks.com/Docs/SSP/VWUSA.COM_SSP_891143C_2014_Golf.pdf):
79.5 mm bore, 80.5 mm stroke, 1,598 cm³, four cylinders, four valves per cylinder,
16.2:1 compression. That sheet describes CLHA, not the exact recorded 2019 engine.
Volkswagen describes the 1.6 TDI's use of common rail and multiple injections in its
[engine overview](https://www.volkswagen-newsroom.com/en/the-new-golf-estate-international-driving-presentation-2480/the-new-golf-estate-engines-and-transmissions-2486).
The preset is a family-inspired approximation, not an identified Golf calibration.

## What changes physically

- 1–3–4–2 firing at 180° intervals; about 26.7 firings/s at the audition's 800 RPM idle.
- Two 2 mg full-load pilots at 18° and 9° BTDC, followed by a 38 mg main at 1° BTDC.
  All three masses scale with requested fueling; durations also meter with fuel.
- A prescribed intake/exhaust reservoir map over 750–3,800 RPM and seven fuel anchors.
  Intake pressure remains ambient at light load and fuel cut, reaching 211.3 kPa
  absolute at full fuel in the middle of the RPM range.
- Separate native motored-force captures at each charge boundary. Version 12 uses
  these to keep boost compression out of the combustion-noise excitation.

Injection timings and quantities, rod length, effective valve windows, reservoir
maps, combustion coefficients and thermal values are authored. No torque curve,
rail hydraulics, ECU, EGR, turbo speed/lag, compressor/turbine map, cold start,
within-cycle crankshaft dynamics or physical aftertreatment acoustics is claimed.
The host prescribes RPM and fuel, including a smooth idle-fuel return after coast.

A controlled native comparison kept geometry, boundaries and total fuel equal,
moving the pilot fuel into the main pulse for the control. The maximum pressure
rise over 0–40° ATDC changed as follows; these are model results, not measurements:

| RPM / fuel fraction | All-main, bar/degree | Pilot/main, bar/degree |
| --- | ---: | ---: |
| 850 / 0.12 | 9.61 | 5.70 |
| 1,800 / 0.12 | 10.88 | 4.34 |
| 850 / 1.00 | 11.37 | 9.70 |
| 1,800 / 1.00 | 8.79 | 7.20 |

## Acoustic tuning

The first modern audition was judged to have the general vibration but insufficient
layers and character. Its total spectral energy was dominated by bass; reducing
block gain to match that fraction removed useful perceptual detail. The revised
preset compares temporal structure and isolated layers as well as overall energy.

- Cylinder acoustic couplings are approximately 0.846, 0.662, 0.586 and 0.754 in
  firing order. This retains a gentle whole-engine pattern in pressure, valve and
  intake radiation. The previous 1.0/0.56/0.38/0.78 contrast produced flapping.
  Reduce deviations from their mean to 40%, then normalize to preserve the sum of
  squared couplings. Fuel trims, injection and native sources are unchanged.
- The 550–3,600 Hz block modes have more selective resonances, a 220 Hz high-pass,
  a 4,400 Hz low-pass and more coherent pressure contribution. Combustion texture
  retains a smooth 3 ms attack and 8 ms loaded decay.
- Valve contacts use a smooth 0.65 ms Hann pulse and 1,650/2,755 Hz resonances.
  The previous 1.8 ms contact strongly attenuated its own 1,050 Hz resonance:
  the isolated valve output mainly added low vibration. Shorter, lower-amplitude
  contacts restore the upper mechanical detail without instantaneous impulses.
  The current small clanking adjustment raises contact gain from 0.028 to 0.035
  and ring decay from 1.8 to 2.2 ms. Attack, resonant frequencies, the reduced
  cylinder contrast and the other acoustic layers retain their previous settings.
- Valve-window-gated intake breath is stronger and extends to 2,000 Hz. Exhaust
  flow detail extends to 1,400 Hz, with its dry gain and primary delay retained.

The reference's upper-band envelope contains slower repeating patterns alongside
its strongest firing-frequency vibration. The asymmetric structural coupling is an
artistic listening perspective informed by that observation, not an identified
microphone position or measured engine transfer matrix. No random misfire, extra
fuel, reference PCM, turbo whistle or free-running hiss is added. The native pilot
softening and load-dependent boundaries remain as described above. The reduced-modulation pass was judged closer to the target, with a request for
slightly more clanking. Listening approval of the small valve-layer lift is pending.

At 1,800 RPM / 0.65 fuel, projecting the 400–4,000 Hz envelope onto the 15 Hz
whole-engine cycle gives block modulation depth 0.425 → 0.172 and intake depth
0.454 → 0.186. Their RMS levels stay within 0.2%; isolated exhaust PCM is identical.
This checks the targeted reduction without using lower volume to hide the layers.
The envelope check is a diagnostic, not a guarantee of perceptual approval.

## Reproduce

From the repository root, using fresh output directories:

```bash
cmake --build build --target crankwave_cli
mkdir -p artifacts/diesel-sound/golf-clank
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/golf16-common-rail-i4/engine.json \
  artifacts/diesel-sound/golf-clank/verified
node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-clank/verified/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-clank/audition \
  artifacts/diesel-sound/ural-rounded-loaded/verified/ural4320-mechanical-v8.crankwave
```

The optional Ural argument adds a comparison. A further optional argument can
point to the previous Golf carrier (for this revision,
`artifacts/diesel-sound/golf-balanced-character/verified/golf16-common-rail-i4.crankwave`).
It adds `before-after-idle-48k.wav` and `before-after-loaded-48k.wav`: five seconds
before, half a second of silence, then five seconds after. Each section is matched
to -25 dBFS RMS, with the gains recorded in the audition metadata.

The script uses the production JS
runtime and ffmpeg to export 48 kHz WAVs. The main audition is 18 seconds:
0–4 idle, 4–8 acceleration, 8–10 held 3,200 RPM, 10–14 coast, 14–18 idle.
Its listening copy applies one constant gain for the whole clip. Raw PCM and every
20 ms control input are saved. Held idle and loaded clips are normalized separately
for inspection. There is no per-cycle leveling or audio added after synthesis.
Set `CRANKWAVE_IDLE_RPM=750` for the idle-reference candidate. The default remains
800 RPM for reproducing the preceding auditions; the selected RPM is recorded in
`audition.json` and used for both sides of a before/after comparison.

`ural-then-modern-comparison-48k.wav` contains four five-second sections with
half-second gaps: Ural idle (0 s), modern idle (5.5 s), Ural loaded at 1,500 RPM
(11 s), modern loaded at 1,800 RPM (16.5 s). Each section is RMS-matched to -25 dBFS
with peak headroom and 20 ms endpoint fades; this does not imply equal real SPL.

## Verification

Both engine schemas, all 70 settled operating cells, native package verification,
fuel response, gas-exchange TDC pressure, deterministic replay and swept playback
passed. The 155 runtime tests include third-channel admission, interpolation and
suppression of false combustion noise from boosted compression. Native tests check
boundary-map admission and exact preservation where reservoir states agree.
The browser Worker test uses an AudioContext test double and verifies displayed MAP
against the loaded carrier, including boosted MAP.

Rebaking the accepted Ural with the extended baker produced identical native source
samples and identical idle/load/coast PCM. The original UAZ's 14 recorded file hashes
remain unchanged. The character revisions verify that native source samples stay identical to the
initial modern carrier. The clanking pass also checks that the block, exhaust,
intake and coasting-flow diagnostics are unchanged from the reduced-modulation
carrier; only valve gain and ring decay change.
Source recordings, isolated layer captures, test logs and the final carrier hash
are under `artifacts/diesel-sound/golf-clank/` and
`artifacts/diesel-sound/modern-reference/`; `audition/audition.json` binds the WAVs to
the carrier and records listening gains.
