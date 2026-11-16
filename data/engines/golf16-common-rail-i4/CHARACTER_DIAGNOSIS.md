# Character diagnosis after the loaded-clatter rejection

The user rejected the version 15 audition as the wrong character. Its higher
midrange level and passing engineering checks did not establish a convincing
match. It is retained for comparison. No further synthesis change is made in
this diagnostic pass; the perceptual target is being clarified with filtered
excerpts of the real recording.

## What the new analysis checks

The previous comparison used broad spectral energy and envelope variation. This
pass examines **where that variation occurs in the engine cycle**, independently
within 350–700, 700–1,400, 1,400–2,800 and 2,800–5,600 Hz bands.

For the 7–11-second source interval, the dominant firing-frequency ridge is tracked
using complex demodulation about the existing estimated RPM curve and a 12 Hz
low-pass. Each band's analytic amplitude envelope is resampled to 128 samples per
firing interval. Modulation spectra and averages over four firing intervals are
then computed. An assumed four-stroke I4 has four firings per 720-degree cycle;
one quarter of the firing rate therefore corresponds to one full engine cycle.
Absolute phase is arbitrary and no specific cylinder is identified.

This is a diagnostic estimate, not tachometer-based order tracking. It uses the
strong low tone as a timing reference. A synthetic chirp with known quarter-firing
and firing-rate modulation verifies the power-ratio calculation to within 0.2 dB.
Results are also inspected separately over 7–9 and 9–11 seconds to check that the
main discrepancy is not confined to one short event.

## Observed mismatch

The rejected addition has strong modulation at **every firing**, particularly in
the upper bands. The source has a less uniform pattern across the full cycle.
For 7–11 seconds, firing-rate modulation power relative to quarter-firing-rate
modulation power is approximately:

| Band | Real Golf | Rejected clatter |
| --- | ---: | ---: |
| 1,400–2,800 Hz | −0.5 dB | +9.0 dB |
| 2,800–5,600 Hz | +2.5 dB | +10.7 dB |

The same difference appears in both halves of the interval. The source's average
350–700 Hz cycle profile also has substantially more variation than the earlier
generator (coefficient of variation 0.28 versus 0.084), while the rejected addition
overemphasizes the repeated upper-band pulses. Matching a single overall envelope
statistic cannot describe those differences.

The source is stereo, but downmix cancellation does not appear to explain this
particular discrepancy: in 7–11 seconds, left/right correlation is 0.956 overall.
Mid-versus-left band levels differ by less than 0.5 dB in the inspected 30–350,
350–1,400 and 1,400–6,500 Hz bands. This does not establish spatial equivalence.

The current renderer weights each cylinder with one structural gain, sums their
excitation, and sends that sum through a shared modal bank. The added clatter does
the same with its own shorter envelope. This constrains the relative cylinder
contributions across frequency. **Band-dependent cylinder-to-mode coupling** is
a plausible next experiment: different cylinders can excite different modes with
different strengths and signs, instead of every band receiving the same relative
cylinder pattern. This is a hypothesis, not an identified physical cause or a
guarantee that it matches the user's missing character. The recording alone does
not uniquely separate combustion, valvetrain, accessories and transfer paths.

Research supports examining timing, spectral distribution and masking together:
[Yun and Lee (2017)](https://doi.org/10.1016/j.jsv.2017.03.005) describe diesel knock
evaluation using temporal and frequency masking, and
[Hastings' diesel sound-quality work](https://docs.lib.purdue.edu/dissertations/AAI3154642/)
examines event timing, amplitude variation, transfer paths and tonal components.
These studies motivate the diagnosis; neither supplies a measured Golf calibration.

## Real-recording excerpts for clarification

Files under `artifacts/diesel-sound/golf-character-diagnosis/` are **all extracted
from the real Golf recording**, not the generator:

- `real-full.wav`: mono 7–11-second excerpt.
- `real-body-350-1400.wav`: the same excerpt, band-passed to 350–1,400 Hz.
- `real-upper-1400-6500.wav`: the same excerpt, band-passed to 1,400–6,500 Hz.
- `real-reference-bands.wav`: full at 0–4 s, lower band at 4.4–8.4 s, upper band
  at 8.8–12.8 s.

Each complete excerpt is matched once to −25 dBFS RMS, with 20 ms endpoint fades.
Bandpass filtering uses a fourth-order Butterworth filter forward and backward for
diagnosis. These are frequency bands, **not isolated physical engine components**.
Source WAV hash remains
`04580414c33e50e724594ddad0110e584aa71b01c70677da9012043cf8110428`.
`reference-bands.json` records provenance and listening gains.

`order-analysis.png/json`, `order-early.png/json`, `order-late.png/json` retain the
measurements. Scripts are under the artifact directory's `analysis/` folder and
run from the repository root. No runtime or preset is changed by this diagnostic
pass. The user subsequently selected **upper metallic texture (1,400–6,500 Hz)**.
The separate [upper-metal candidate](UPPER_METAL_REFERENCE.md) follows that choice;
this document retains the diagnosis that motivated it.
