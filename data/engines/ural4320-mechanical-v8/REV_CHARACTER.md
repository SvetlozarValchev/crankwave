# Rev-up gargle investigation

The user accepted version 10, then requested its baseline commit before
investigating the obvious gargle in the SoundCloud rev-up references.
The accepted sound is commit `a1e7a26`, branch `baseline/diesel-flowing-coast`.
Investigation continues on `tuning/diesel-rev-character`.
See [COASTING_BASELINE.md](COASTING_BASELINE.md) for the accepted artifact hashes.

## Working diagnosis

The strongest supported explanation is engine-cycle pulse grouping carried in
the midrange: the broad sound swells and falls with the uneven pulses of one V8
bank. This identifies a likely acoustic mechanism for the user's “gargle”; it
does not uniquely identify every physical source in the mixed recording.

The reference exhaust preview has a strong envelope repetition near 16 Hz and
multiples of that rate across 150–450, 450–1,000 and 1,000–2,200 Hz. The engine
preview has a similar component near 14–16 Hz. In the accepted synthetic sound,
the lower-frequency body carries appreciable cycle rhythm, while the upper
combustion texture has much weaker shared modulation. Total envelope variation
alone does not capture this distinction: randomized fluctuation and repeating
bank pulses can have similar variance.

The native source already contains the bank grouping. The authored right bank
fires at 0°, 180°, 270° and 450° in a 720° cycle, giving cyclic intervals of
180°, 90°, 180° and 270°. Its captured outward flow therefore varies strongly
once per complete engine cycle. The whole V8 still fires every 90°. Summing
similarly weighted texture from all eight cylinders obscures the individual
bank's uneven pattern. This is a reason to investigate radiation/mixing before
changing bore, compression, injection timing or the existing firing sequence.

Irregular per-bank firing and exhaust propagation can introduce pulsation
components beyond the whole engine firing frequency; see the publisher abstract
of [Hosomi, SAE 1999-01-1651](https://saemobilus.sae.org/papers/analysis-pulsation-inside-pipe-study-exhaust-sound-characteristics-v-type-8-cylinder-engine-study-optimized-front-pipe-junction-structure-1999-01-1651).
This supports the general mechanism, not a measured diagnosis of this Ural.

## Reference evidence

The analyzed public previews are:

- [Ural 4320 Onboard Driving Exhaust Mics](https://soundcloud.com/polepositionproduction/ural-4320-onboard-driving), analysis window 6–8.4 s.
- [Ural 4320 Engine Mics Driving](https://soundcloud.com/polepositionproduction/ural-4320-engine-mics-driving), analysis window 5–7.4 s.
- Accepted 18-second synthetic audition, analysis window 6.2–8.6 s.

Hilbert amplitude envelopes are calculated after band filtering. They are
normalized by their own mean for modulation analysis. At the low-rate upper-band
modulation peak, the exhaust recording is about 15 dB above its broad envelope
spectral floor and the engine recording about 8 dB; the current audition is about
3 dB. Mid/upper envelope coherence at those peaks is about 0.72, 0.66 and 0.25,
respectively. Different recording windows and unknown RPM prevent interpreting
these as calibrated matching scores. They support investigating synchronized
pulse texture rather than using global brightness or noise level as a proxy.

## Controlled experiments

All experiments use a separate authored five-second 900–2,100 RPM rev at 0.75
fuel. They run against cloned runtime data and leave the tracked engine/runtime
unchanged. Playback comparisons apply one constant gain to each entire clip,
matching -18 dBFS RMS without flattening its temporal envelope.

1. Shortening the combustion stochastic decay from 18 to 6 ms, with approximate
   energy compensation, barely changes the once-per-cycle modulation. Pulse
   duration alone does not explain the missing pattern.
2. Increasing the existing bank-flow-gated exhaust noise brings the shared bank
   modulation forward, but also increases broad noise. This is a diagnostic of
   the available flow rhythm, not a selected final gain.
3. Giving stochastic combustion radiation the existing exhaust bank proportions,
   with energy normalization and 6 ms decay, makes the cycle grouping clear in
   the measured mid/upper envelopes. The final controlled variant changes only
   stochastic combustion radiation; coherent pressure, exhaust flow, valve
   contacts and intake breath are unchanged. This separates bank weighting from
   a general mechanical-volume change.

The controlled variant is a listening probe, not a released version 11 carrier.
Its loaded-only scope has not been implemented in production. Any eventual change
must preserve the accepted idle/coasting response and smooth contact excitation,
then be verified through carrier admission, replay and transition checks.

## Local analysis artifacts

`artifacts/diesel-sound/ural-rev-analysis/` contains:

- `reference-exhaust-rev-midrange-48k.wav` and the corresponding engine version:
  original reference seconds 3–8 filtered to 450–1,800 Hz, with a single gain to
  -18 dBFS RMS. These isolates expose the rhythmic midrange for user identification.
- `bank-pulse-diagnostic-before-after-48k.wav`: accepted sound 0–5 s, silence
  5–5.5 s, controlled bank-combustion experiment 5.5–10.5 s.
- Original-gain reference excerpts, plots, modulation summaries and `listening.json`.

The scripts and raw diagnostic WAVs are under `.work/diesel/rev-gargle/`.
No reference PCM has been embedded in an engine or carrier. This investigation
adds no production DSP changes or new subjective approval claim. The baseline's
142 runtime tests and native/browser verification remain recorded in its artifact
folder; they were not rerun for this analysis-only work.

## Feedback: individual pulses too sharp

The user rejected the first bank-combustion comparison as having explicit
“pulses/bangs/too sharp.” Increased cycle modulation was therefore insufficient:
the short 6 ms decay and 5:1 bank contrast exposed individual firing attacks.
This is a listening rejection, despite the earlier modulation metrics improving.

The follow-up remains an isolated WAV experiment. It uses a 4 ms first-order
smoothing stage on each cylinder's stochastic amplitude envelope, a 12 ms decay
and opposite-bank gain 0.55 before energy normalization. The longer, rounded
excitation overlaps between firings. Audio filters, coherent pressure, exhaust,
valve contacts and intake breath retain their accepted values. It has no
production runtime or carrier changes.

In the 450–2,200 Hz envelope, the 99th-percentile amplitude relative to its mean
falls from 2.95 in the rejected experiment to 1.99, near the baseline's 1.93.
The corresponding envelope-rise descriptor falls from about 1,686 to 1,052 per
second, near baseline 1,048. The once-per-cycle modulation peak remains stronger
than baseline (9.3 versus 3.3 dB above its envelope spectral floor, compared with
13.6 dB in the rejected version). These descriptors support a softer, less exposed
pulse pattern but do not establish listening acceptance.

Artifacts are in `artifacts/diesel-sound/ural-rounded-rev/`:
`baseline-then-rounded-48k.wav` plays the accepted five-second rev, a half-second
gap, then the rounded experiment. `rounded-rev-48k.wav` plays only the experiment.
Both use one constant gain per full clip to -18 dBFS RMS. The scripts, 192 kHz
originals and verification records are retained. Verification checks finite
output, exact reset replay, byte-identical accepted baseline rendering and
unchanged exhaust/valve/intake stem energies. Listening feedback is pending.

## Integration after acceptance

The user described the rounded follow-up as “yes better” and asked to commit it.
Version 11 now integrates that exact fully loaded texture through the optional
`loaded_pressure_texture` presentation object. The preceding WAV-only notes
refer to the diagnostic stage; the current tracked engine/runtime include this
integration.

The unchanged version 10 pressure texture remains the idle/coasting path. A
smoothstep between 0.2 and 0.5 fueling, with a 40 ms exponential transition,
blends in the rounded texture under load. Both envelope histories remain warm,
share the same deterministic noise stream, and reset together. Coherent pressure,
exhaust and mechanical layers keep their previous parameters and signals.
The fully loaded five-second rev is checked byte-for-byte against the accepted
rounded diagnostic. Held idle/coast, settled low-fuel trajectory sections and
version 10 replay are also checked for exact preservation.

The integrated package and full idle/rev/coast listening copies are under
`artifacts/diesel-sound/ural-rounded-loaded/`. The original accepted coasting
baseline remains `a1e7a26` on `baseline/diesel-flowing-coast`. Reference audio is
used only for analysis; none is embedded in the generated engine package.
