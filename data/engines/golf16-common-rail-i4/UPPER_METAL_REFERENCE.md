# Upper metallic texture: separate listening candidate

The user identified the real recording's **1,400–6,500 Hz texture** as the missing
character during rev-up. `engine-upper-metal-candidate.json` follows that choice.
It is an experiment pending listening feedback, not an accepted baseline.
`engine.json` and every preceding candidate remain unchanged.

## Target and diagnosis

The source is Pole Position Production's
[T5 onboard engine-microphone Golf ramp](https://soundcloud.com/polepositionproduction/vw-golf-diesel-t5-onbrd-1),
locally `artifacts/diesel-sound/modern-rev-coast-reference/golf-engine-ramps.wav`.
SHA256: `04580414c33e50e724594ddad0110e584aa71b01c70677da9012043cf8110428`.
The target excerpt is 7–11 s, during the high part of the rise. The first section
of the new upper-band comparison is byte-for-byte PCM-identical to the filtered
real excerpt the user selected. Filtering selects frequencies, not a uniquely
identified physical component.

The rejected version 15 addition emphasized a similar pulse at every firing.
[Cycle-envelope analysis](CHARACTER_DIAGNOSIS.md) instead found a brief upper-band
accent and a less uniform pattern over four firings in the source. A frequency
spectrum or overall roughness statistic alone did not capture that difference.
Absolute cycle phase is arbitrary, and the recording does not identify a cylinder.

## Change

Use the preceding version 14 rattle candidate as the base. Preserve its complete
presentation, native gas capture, injection, fuel trims, firing phases, inertia,
exhaust, intake, valves and coasting controls. Add one separate upper radiation
path using the version 16 extension to `loaded_clatter`:

- Positive increments of each cylinder's fueled-minus-motored native force excite
  a 0.15 ms attack filter and 0.8 ms decay envelope.
- The excitation is seeded noise with stochastic gain 2 and coherent gain 0.
  It is tied to combustion pressure rise, with no free-running impact clock or
  reference samples.
- Four fixed resonances at 1,700 / 2,400 / 3,650 / 5,000 Hz, all Q3, have PCM/N
  gains 0.0000225 / 0.0000075 / 0.0000048 / 0.0000024. High-pass 1,400 Hz and
  low-pass 6,500 Hz restrain leakage. Filters are not brick walls.
- Each mode has its own complete cylinder-ID coupling array. In firing order
  1–3–4–2 these weights are `[1,.3,.3,.3]`, `[1,.2,.25,.2]`,
  `[1,.06,.12,.1]`, `[1,.06,.1,.12]`, multiplied by the existing structural gains.
  These are acoustic weights, not additional fuel or inferred cylinder defects.
- Smooth RPM activation from 1,500 to 3,800 and fueling from .18 to .55, with a
  40 ms transition. Fresh idle and fuel-cut controls bypass exactly. At release,
  the upper addition decays while the existing coast mechanics and flow continue.

The exploratory long-decay version produced a broad swell. Strong short-envelope
versions overemphasized the accent. The final candidate reduces those short-event
mode gains substantially. Their configurations and renders are retained under the
artifact directory; they are not offered as accepted variants.

Version 16 binds cylinder weights by ID before each mode's filters. Native and
runtime admission require every cylinder exactly once on every mode; missing,
unknown, duplicate and mixed-format bindings are rejected. Version 15 retains its
original sum-before-filter rendering path. No existing preset opts into this change.

## Measurements and limits

Over 7–11 s, using the diagnostic timing method described above:

| Metric | Real | Rejected v15 | New candidate |
| --- | ---: | ---: | ---: |
| 1,400–2,800 Hz firing/whole-cycle modulation power, dB | −0.55 | +8.92 | +1.29 |
| 2,800–5,600 Hz firing/whole-cycle modulation power, dB | +2.44 | +10.71 | +1.03 |
| 1,400–2,800 Hz average cycle-envelope CV | .131 | .330 | .167 |
| 2,800–5,600 Hz average cycle-envelope CV | .209 | .393 | .182 |

These statistics support a less overbearing cyclic accent; they do not establish
that the generated timbre is perceptually correct. The total upper energy remains
lower than the source relative to the full mix: −23.68 versus −18.42 dB in
1,400–2,800 Hz, and −26.43 versus −23.24 dB in 2,800–5,600 Hz. The underlying low
body also differs. The final choice prioritizes a restrained temporal character
experiment over forcing a broad spectral-level match that previously failed in
listening. The filtered comparison independently levels each excerpt, so use the
full engine clip to judge how much texture comes through in the mix.

The reference-driven 28 s render uses the same saved, inferred RPM path as previous
candidates (approximately 1,300–4,470 RPM), with authored .65 fueling before 11 s
and zero after. This recording is not an idle source, and its actual pedal/fuel
state is unknown. The separate 18 s audition includes 750 RPM idle.

## Listen

Under `artifacts/diesel-sound/golf-upper-metal/`:

- `real-then-generated-upper-48k.wav`: **REAL** upper-band excerpt at 0–4 s,
  **GENERATED** upper-band excerpt at 4.4–8.4 s.
- `real-then-generated-full-48k.wav`: the same order, full-band excerpts.
- `previous-then-generated-upper-48k.wav` and `previous-then-generated-full-48k.wav`:
  preceding v14 rattle candidate, then this candidate, with the same timing.
- `ramp-audition/rev-rundown-48k.wav`: **GENERATED**, full 28 s rise and rundown.
- `audition/idle-revs-coast-48k.wav`: **GENERATED**, 18 s, idle 0–4, rise 4–8,
  held 8–10, coast 10–14, idle 14–18.

Comparisons are mono, each complete four-second excerpt matched once to −25 dBFS
RMS with 20 ms endpoint fades and 0.4 s silence between sections. The 28 s listening
copy uses one constant gain to −25 dBFS RMS; the 18 s copy uses −23 dBFS RMS. No
per-cycle leveling, compression or recording samples are applied to generated audio.
`comparison.json` and audition metadata record hashes, controls and gains.

## Verification and reproduction

The native version 16 carrier has 77 settled operating cells and SHA256
`940a2fab5d01821317e303a07706815ec72b7ab057ce36a9c382edf65ec79d6a`.
Native source arrays and previous presentation are unchanged from v14. Fresh
idle/light-fuel/settled-coast PCM and existing layer diagnostics are identical;
loaded high RPM adds the new layer. Partitioned playback is deterministic. The
native-package render exactly reproduces the exploratory render. Replaying the
previous v14 and rejected v15 full sweeps through the extended runtime preserves
both WAVs exactly. These checks are recorded in `verification.json`.

All 165 runtime tests pass. Native `diesel.sound_bake` passes, the project builds,
and native integration verifies the carrier and operating grid. Both new and old
Golf definitions validate against the authoring schema. New tests exercise
mode-selective cylinder excitation, signed/zero couplings, ID ordering, strict
version admission, native source preservation, bypass and split-read replay.

From the repository root, use fresh output directories:

```bash
cmake --build build -j 4
node web/tests/integration/mechanical-diesel.integration.mjs build/crankwave \
  data/engines/golf16-common-rail-i4/engine-upper-metal-candidate.json \
  artifacts/diesel-sound/golf-upper-metal/verified
node data/engines/golf16-common-rail-i4/ramp-audition.mjs \
  artifacts/diesel-sound/golf-upper-metal/verified/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-ramp-match/reference-controls.json \
  artifacts/diesel-sound/golf-upper-metal/ramp-audition
CRANKWAVE_IDLE_RPM=750 node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-upper-metal/verified/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-upper-metal/audition
```

Analysis and comparison scripts are retained in `artifacts/diesel-sound/golf-upper-metal/analysis/`.
Their module imports assume execution from `.work/diesel/golf-upper-metal/`; see
that directory's README for reproduction details. The recordings remain external
reference material and are not dependencies of the generated carrier.
