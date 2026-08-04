# Post-parity exhaust-flow coupling listening gate

Status: retained by user listening on 2026-08-04 as audibly neutral.

## Isolated candidate

Commit `4eafff8` changes one existing sound-bearing operation: the filtered stochastic
air-noise term in each exhaust route is now modulated by measured exhaust-valve flow.
It does not add a new noise source or replace the accepted engine-sim-derived exhaust
source.

The control path used one fixed full-strength filtered-air term. The candidate uses:

```text
20 kHz signed exhaust-port mass flow for each cylinder
  -> absolute value before summing, so reversion cannot cancel forward flow
  -> the same primary and downstream integer propagation delays as the exhaust pulse
  -> stable sum by physical exhaust route
  -> causal reconstruction to 192 kHz
  -> divide by the route's summed authored maximum exhaust-flow-bench capacity
  -> clamp to [0, 1]
  -> scale only the pre-existing filtered-air-noise term
```

The reference is derived from each cylinder bank's authored exhaust-flow table. It is
not BMW-specific, adaptive, learned from the clip, or exposed as a tuning constant.
Presentation gain, cylinder attenuation, the historical cylinder-count divisor, and
distance gain do not participate in the physical flow sum.

This is only the valve-flow subgate of post-parity queue item 5. Combustion-work and
pressure-ratio coupling remain separate candidates with their own listening stops.

## Frozen systems

The candidate leaves the pressure-derived exhaust source, intake path, jitter timing,
derivative path, random seeds and draw cadence, air-noise filter, route IRs, route
mixes, mastering, fades, scenario, RPM trajectory, and output format unchanged. The
physics/capture clock remains 20 kHz and presentation remains mono PCM24 at 192 kHz.

## Listening comparison

The comparison uses `bmw-m52tub28-cleanroom` with the accepted 15-second held-dyno
pull and lift from 1,500 to 6,500 rpm:

| Clip | Purpose | Repository path | Complete-WAV SHA-256 |
|---|---|---|---|
| A | accepted full exhaust plus intake | `artifacts/listening/fidelity-flow-coupled-bmw-20khz-4eafff8/comparison/A-accepted-full-exhaust-plus-intake.wav` | `24e48c3e1eba564ee34fbb57080faeccf6ce491399213215cb70355f076d4b95` |
| B | flow-coupled full exhaust plus unchanged intake | `artifacts/listening/fidelity-flow-coupled-bmw-20khz-4eafff8/comparison/B-flow-coupled-full-exhaust-plus-intake.wav` | `ef35461c31a77906a260da15bdc6c442fcf5e628a031bc49e4806d728cfd3c0e` |
| C | accepted exhaust-only diagnostic | `artifacts/listening/fidelity-flow-coupled-bmw-20khz-4eafff8/comparison/C-accepted-exhaust-only.wav` | `e77a236d2c94fb66b6dc2799e212b6cd0f4f9bd27643568e2f1dd379cb884dcf` |
| D | flow-coupled exhaust-only diagnostic | `artifacts/listening/fidelity-flow-coupled-bmw-20khz-4eafff8/comparison/D-flow-coupled-exhaust-only.wav` | `8873d4aba6c64fabccc6a2196c2169a12629bc6413d407c8a68aa884f1f97e90` |

`A` is a hard link to the accepted active-intake master; neither A nor B was
re-encoded. `D` is a fixed-gain sum of the candidate's two selected exhaust stems,
using the same operation previously verified against C. No clip is independently
normalized or level-matched. A and B both measure `-17.4 dB` mean volume; their peaks
are respectively `-6.1 dBFS` and `-6.2 dBFS`. The A-minus-B residual is `-44.4 dB`
mean and peaks at `-29.6 dBFS`, confirming a real but deliberately narrow change.

## Listening verdict

The user reported that all four clips sounded identical. That agrees with the
low-level residual and is recorded as a neutral result: this gate does **not** claim a
noticeable quality increase. The candidate is retained because it removes one static
generic-noise authority in favor of an already simulated physical observable without
causing a perceived regression. It is foundation for later isolated item-5 work, not
evidence that item 5 as a whole is complete.

## Verification and decision boundary

The fresh Release render completed in `35.13 s`, below the 45-second gate. Five
focused capture/presentation tests pass. After the keep verdict, deterministic closure
advanced the active exact-audio baselines without deleting any historical oracle:

- the new generic M52B28 oracle is
  `reference/oracles/bmw-m52b28/bmw-m52b28-canonical-flow-coupled-20khz-4eafff8-dyno-1500-6500rpm.wav`;
  it is `8,640,586` bytes with complete-WAV SHA-256
  `220cd6760b4eb991cdac147a99dfcc0f345ddf9fdd86b7c1924e062d87cc2c5b` and decoded
  PCM24 SHA-256
  `c9276854e52c17178a293881bf61735d219a2f2f4467bdaf225398bea617d700`;
- all four affected native Release gates pass: engine session, authored JSON render,
  native presentation publication, and CLI render. The simulation-request identity
  remains `8c75e9bfa871e88d58031d52c5eb7278a27a9092e252c7a1fb09466755678ced`;
- native and WASM short-session PCM hashes are respectively
  `11c250655720015b848b65b57cb03840a1e15391b05679e7e182560a67dca171` and
  `5658b9947142a71c031a7444426bb4c2ea230144a0f3902855934730edfb40f3`.
  Telemetry, semantic, and input hashes remain exact; maximum and RMS cross-target PCM
  errors remain inside the unchanged `1e-8` and `1e-9` bounds; and
- the complete browser-workbench gate passes. Its exported WAV remains `3,840,056`
  bytes and now has SHA-256
  `f9be8f7de20844e44cce16f664b5d2ac337b6b740a676c96aaf9c7a3c898b467`, with zero
  startup underruns and all 22 packages verified.

The next sound-bearing change must still be one isolated, physically justified
subgate with this retained path as A and another immediate listening stop.
