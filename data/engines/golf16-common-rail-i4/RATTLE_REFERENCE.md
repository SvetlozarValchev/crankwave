# Metallic-rattle candidate

`engine-rattle-candidate.json` responds to feedback that the preceding engine-mic
ramp candidate lacked the metallic “drrr” character. It uses the same native engine,
77-cell bake grid, runtime version 14 and reference controls. This is a separate
audition; `engine-ramp-candidate.json` and both earlier Golf presets are unchanged.

The selected T5 engine-microphone recording and separate T7 exterior idle source
are documented in [RAMP_REFERENCE.md](RAMP_REFERENCE.md) and
[IDLE_REFERENCE.md](IDLE_REFERENCE.md). The same public-preview hashes apply.
No recording PCM or measured impulse response is included in the candidate.

## Change

The valve layer previously had a quiet, rounded contact. Reducing contact duration
retains more of its resonant output. Each contact still uses a smooth Hann pulse;
its phase comes from the native valve timing, with the existing deterministic
per-event variation. Resonances overlap in time as engine speed increases.

| Valve parameter | Previous | Candidate |
| --- | ---: | ---: |
| Base resonance | 1,650 Hz | 1,250 Hz |
| Gain | 0.020 | 0.025 |
| Contact duration | 0.80 ms | 0.45 ms |
| Ring decay | 3.5 ms | 4.0 ms |

The upper pressure-driven block resonances are more selective, with less diffuse
energy at the top of the spectrum:

| Mode | Previous frequency / Q / gain PCM/N | Candidate frequency / Q / gain PCM/N |
| --- | --- | --- |
| Lower metallic | 1,050 Hz / 1.8 / 1.19e−6 | 1,280 Hz / 7 / 2.40e−6 |
| Middle metallic | 1,700 Hz / 2.2 / 8.50e−7 | 1,700 Hz / 4.5 / 1.20e−6 |
| Upper metallic | 2,550 Hz / 1.2 / 5.10e−7 | 2,550 Hz / 3.5 / 6.90e−7 |
| High detail | 4,600 Hz / 1.4 / 1.87e−7 | 4,600 Hz / 1.4 / 1.2155e−7 |

Modes below 1 kHz, the inertia layer, pressure-noise envelope, cylinder couplings,
intake, exhaust, coast controls and random seeds retain their previous settings.
These are authored acoustic resonances, not identified component measurements.
No new runtime mechanism or engine-physics change was required.

## Measurements and limits

Across the same 28-second ramp, isolated valve RMS rises from 0.006170 to 0.019026,
about 9.8 dB. Whole-clip raw RMS changes from 0.180008 to 0.180063, about 0.003 dB;
the inertia-driven body still dominates the total energy. Raw output peak is 0.60247.
At roughly 3,150 RPM on rundown, the 1–2 kHz band rises approximately 2.7 dB relative
to total power. Loaded 4–6.5 kHz energy falls by about 2.6 dB in the held comparison.

The 1–4 kHz envelope's coefficient of variation at held 3,137 RPM / 0.65 fuel moves
from 0.27 to 0.30, against about 0.32 in the source's 6.6–7.4-second rising segment.
This indicates more temporal structure in that band, not perceptual validation.
At idle it rises from 0.39 to 0.47 against 0.40 in the exterior reference. The
reference and held simulations are not phase-aligned, and the driving source's
RPM varies. Its fuel state is unknown. Idle and low-rundown 1–2 kHz energy remain
above the corresponding reference fractions; the upper rundown spectrum is still
incomplete. Listening approval is pending.

## Artifacts and verification

Under `artifacts/diesel-sound/golf-rattle/`:

- `audition/idle-revs-coast-48k.wav`: 18 seconds, idle 0–4, rev 4–8, held 8–10,
  rundown 10–14, idle 14–18. Same 750 → 3,200 → 750 RPM controls as before.
- `before-after-idle-revs-coast-48k.wav`: previous at 0–18 seconds; candidate at
  18.5–36.5 seconds. Each complete sweep is RMS-matched once to −23 dBFS. There is
  no per-stage leveling; `comparison.json` records hashes and gains.
- `ramp-audition/rev-rundown-48k.wav`: the 28-second reference-shaped RPM sweep,
  about 1,300 → 4,470 → 1,300 RPM, with the previously authored fuel controls.
- `held-analysis.png`, `held-metrics.json`, `c-spectrograms.png`, `c-levels.png`,
  `c-metrics.json`: waveform-envelope, spectral and level comparisons.
- `analysis/`: retained exploratory render, analysis, comparison and verification
  scripts. `c` denotes the final exploratory configuration. Its entire ramp PCM
  is exactly equal to the final authenticated native package's render.

Both engine schemas and native package integration pass, including all 77 settled
cells. Every native source sample, cylinder binding and physical/bake parameter
matches the preceding ramp candidate exactly. Six held idle/load/fuel-cut captures,
including 4,500 RPM, are finite, unclipped and partition-invariant. Inertia, exhaust,
intake and coast-flow diagnostics are exactly equal between versions. Runtime code
is unchanged in this revision; its earlier 159-test result is not a new test run.

Carrier SHA-256:
`93901f2199fd53cff16f93b03823562f9d7d5ce58ba81fc9cccf50de7160f7d9`.

## Reproduce

From the repository root with fresh output directories:

```bash
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/golf16-common-rail-i4/engine-rattle-candidate.json \
  artifacts/diesel-sound/golf-rattle-replay
CRANKWAVE_IDLE_RPM=750 node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-rattle-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-rattle-replay/audition
node data/engines/golf16-common-rail-i4/ramp-audition.mjs \
  artifacts/diesel-sound/golf-rattle-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-ramp-match/reference-controls.json \
  artifacts/diesel-sound/golf-rattle-replay/ramp
```
