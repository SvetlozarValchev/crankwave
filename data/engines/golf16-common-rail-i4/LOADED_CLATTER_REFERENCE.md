# Loaded rev-up clatter

Listening result: **rejected for the wrong character**. The stronger pulses and
matching band levels did not resolve the user's target. This candidate is retained
for comparison; see [the subsequent character diagnosis](CHARACTER_DIAGNOSIS.md).

`engine-loaded-clatter-candidate.json` addresses feedback that idle was somewhat
convincing but the identifying diesel clank disappeared during rev-up. It adds a
separate pressure-driven acoustic path in runtime version 15. The previous rattle
candidate and all its existing presentation settings remain intact.

The target remains the real Golf engine-microphone T5 recording documented in
[RAMP_REFERENCE.md](RAMP_REFERENCE.md). This candidate contains only procedural
audio. The source recording is included only in the explicitly labeled first
section of the comparison. The RPM trajectory is estimated from the reference;
fuel 0.65 on the rise and zero after 11 seconds are authored controls, not telemetry.

## Why the earlier change missed rev-up

The valve adjustment improved a mechanical layer but did not supply enough
combustion-associated midrange at high RPM. The existing pressure texture uses a
7 ms attack and 25 ms decay, while an I4 at 4,500 RPM has one firing every 6.67 ms.
That envelope can blur the collective firing pattern. Increasing its gain globally
would also change idle and the established body sound.

The added layer uses each cylinder's native fueled force minus its matching motored
force, sampled before the existing pressure texture. Positive increments accumulate
in a short decay envelope, smoothed by an attack filter. The envelope supplies
both a coherent excitation and independently seeded noise to structural resonators.
The input is pressure-derived; there are no new firing events, misfires, injector
click samples, combustion heat or changes to native forces.

This is an authored radiation proxy, not a unique identification of the recorded
sound, solved contact impacts, structural finite-element model or measured ECU map.
Its gain and resonances are acoustic controls, not calibrated sound pressure.

## Final settings

- RPM mix: smoothstep from 1,500 to 4,300 RPM.
- Fuel mix: smoothstep from 0.18 to 0.55, multiplied by the RPM mix.
- Mix transition: 40 ms; contact envelope attack 0.3 ms, decay 2.5 ms.
- Coherent gain 1, stochastic gain 1; seed 15983226.
- Radiation high-pass 450 Hz, low-pass 4,300 Hz.

| Resonance | Q | Gain, PCM/N |
| --- | ---: | ---: |
| 650 Hz | 3 | 0.000090 |
| 1,280 Hz | 5 | 0.000090 |
| 2,100 Hz | 4 | 0.000040 |
| 3,300 Hz | 3 | 0.000025 |

The short envelopes follow the baked pilot/main pressure rises and existing
cylinder phase/fuel variation. Histories and filters run while the mix is bypassed;
entry cannot start a cold resonator or repeat a noise fragment. Fresh idle and
settled fuel-cut output remain exactly equal to the previous preset. On release,
the existing excitation and mix decay rather than switching instantaneously.

## Comparison

In the high-rev interval 9.6–10.4 seconds, before the authored fuel release, these
band powers are relative to each excerpt's total power:

| Band | Real recording | Previous generator | New generator |
| --- | ---: | ---: | ---: |
| 500–1,000 Hz | −17.09 dB | −24.67 dB | −16.29 dB |
| 1,000–2,000 Hz | −16.95 dB | −23.84 dB | −18.10 dB |
| 2,000–4,000 Hz | −17.95 dB | −25.44 dB | −18.63 dB |
| 4,000–8,000 Hz | −25.56 dB | −30.12 dB | −25.39 dB |

Whole-excerpt RMS increases by only 0.18 dB from the previous generator. The
700–4,000 Hz envelope's coefficient of variation rises from 0.30 to 0.47; the
recording is 0.28. Its 95th-percentile/median envelope ratio rises from 1.63 to 2.16,
against 1.52 in the recording. The candidate therefore has more pronounced pulses;
the close band balance is not a complete match or a listening judgment. Individual
resonances, modulation and microphone perspective still differ. Listening approval
was negative: the user rejected the character. The old 10.5–11.3-second "peak" window crosses authored fuel release;
use the fully fueled 9.6–10.4 window when evaluating rev-up strength.

## Auditions

Under `artifacts/diesel-sound/golf-loaded-clatter/`:

- `real-before-after-high-revs-48k.wav`: 12.8 seconds. REAL recording at 0–4 s,
  PREVIOUS generator at 4.4–8.4 s, NEW generator at 8.8–12.8 s. Each is the 7–11 s
  rev-up excerpt, mono, matched once to −23 dBFS RMS with 20 ms endpoint fades.
  `comparison.json` records exact source paths, hashes, timing and constant gains.
- `ramp-audition/rev-rundown-48k.wav`: entirely GENERATED, 28 seconds, using the
  same estimated 1,300 → 4,470 → 1,300 RPM trajectory as before.
- `audition/idle-revs-coast-48k.wav`: entirely GENERATED, 18 seconds; 750 RPM idle
  0–4 s, rev to 3,200 at 4–8 s, held 8–10 s, rundown 10–14 s, idle 14–18 s.
- `high-rev-analysis.png`, `high-rev-metrics.json`, `d-spectrograms.png`,
  `d-levels.png`, `d-metrics.json`: retained spectral and envelope comparisons.
- `analysis/`: reproduction scripts and exploratory definitions. Configuration `d`
  is final. Its complete ramp PCM is exactly equal to the authenticated native
  carrier's output; `a`–`c` and `verified/` are earlier exploration only.

The full generated ramp has raw RMS 0.181363 and peak 0.664089. Its 48 kHz listening
copy applies one constant −10.171 dB gain to target −25 dBFS full-clip RMS, with
endpoint fades. There is no dynamic compression or per-stage gain matching after
synthesis. Existing runtime output saturation remains part of the generator.

## Verification and reproduction

Both engine schemas, the native sound-bake test, final package integration and all
162 runtime tests pass. New tests cover pressure-source dependence, silence for
identical fueled/motored inputs, decay, exact idle/coast bypass, zero-gain legacy
equality, other-layer isolation, seeded replay, partitioning and malformed inputs.

All 77 native force/flow/motored-force cells and physical/bake parameters match the
previous carrier exactly. Seven held cases, including loaded and fuel-cut 4,500 RPM,
are finite, unclipped and partition-invariant. Existing block, valve, inertia,
exhaust, intake and coast-flow diagnostics remain exactly equal. The previous
28-second ramp replays byte-for-byte after the runtime change. `verification.json`
records these checks and binds the final carrier:

`e962465347afabca7b6d08ffb687276ab251a240a64dfd0dc0f53d0bcd9cb076`.

From the repository root, using fresh output directories:

```bash
cmake --build build --target crankwave_cli
node web/tests/integration/mechanical-diesel.integration.mjs \
  build/crankwave data/engines/golf16-common-rail-i4/engine-loaded-clatter-candidate.json \
  artifacts/diesel-sound/golf-loaded-clatter-replay
node data/engines/golf16-common-rail-i4/ramp-audition.mjs \
  artifacts/diesel-sound/golf-loaded-clatter-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-ramp-match/reference-controls.json \
  artifacts/diesel-sound/golf-loaded-clatter-replay/ramp
CRANKWAVE_IDLE_RPM=750 node data/engines/golf16-common-rail-i4/audition.mjs \
  artifacts/diesel-sound/golf-loaded-clatter-replay/golf16-common-rail-i4.crankwave \
  artifacts/diesel-sound/golf-loaded-clatter-replay/audition
```
