# M3 BMW physics listening gate

Status: canonical pull rendered; user listening acceptance pending

Source commit: `f0e0848`
Render date: 2026-07-28

The clean-slate BMW M52B28 simulation and capture-to-excitation stage rendered the
canonical 15-second, 192 kHz pull through the unchanged accepted presentation
renderer. This is a local-evaluation candidate, not a production publication, and it
does not write a production manifest.

The renderer input was the retained output of the live
`CapturedExhaustExcitationSession`. The frozen reference audit was used only as a
comparator and was not supplied to presentation. All diagnostic gates passed before
presentation began.

Raw listening candidate:

```text
artifacts/listening/bmw-m52b28-m3-physics-candidate-f0e0848/audio/
  bmw-m52b28-m3-candidate-listening.wav
SHA-256: a120a6053cf9450871726c1b6ad8cea3705254cf5d9f2812e7b6c85274f30d67
```

## Controlled listening pair

Listen to these two files first. Both are 15-second, mono, 192 kHz, 24-bit PCM
renders through the same presentation path:

```text
A-bmw-m52b28-accepted-oracle.wav
SHA-256: f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb

B-bmw-m52b28-m3-candidate-level-matched.wav
SHA-256: dce696abff7c75a48f48831ed4b47ed47f80bd9c9ee2f8f1340ea1d5a534e19f
```

`A` is an exact copy of the accepted oracle. `B` is the live clean-slate physics
candidate attenuated by `0.030190 dB` so both files measure `-12.749124 dBFS` RMS.
The unadjusted candidate measures `-12.718934 dBFS` RMS and `-3.472748 dBFS`
peak; the oracle measures `-12.749124 dBFS` RMS and `-3.513945 dBFS` peak.
Their level-matched difference measures `-65.093071 dBFS` RMS with a
`-54.460900 dBFS` peak. These figures establish a controlled comparison, not an
audible-quality verdict.

## Physics and routing gates

- `170000` simulation frames were processed in `850` bounded blocks, with no
  timing-index or control mismatches and no non-finite output.
- The maximum circular crank-angle error was
  `3.5527136788005009e-15 rad`; all `3075` ignition crossings were accepted.
- Both exhaust routes were sustained and bipolar. Candidate-to-oracle route
  correlations were `0.9999985322` and `0.9999985226`; normalized RMSE was
  `0.00260983` and `0.00260515`.
- The frozen oracle reproduced its own pre-delay, post-delay, and route buses with
  zero binary64 bit mismatches. This audit verified the comparator; it was not an
  input to candidate rendering.

The route-isolated dry and configured stems, raw master, and complete numerical
report are stored beside the listening pair. The selected route hashes are:

```text
exhaust-route-1-candidate-selected.wav
SHA-256: 33e0aa8dda0b75c7e18f3db840013aee20db98609e79ab9f0aa14dab26b6f094

exhaust-route-2-candidate-selected.wav
SHA-256: 78cb12cd067153a18ac3d666f62db4e50a62b88bf73578779ccd36c714e3b28b
```

## Performance

The physics-to-WAV pipeline took `12.421992373 s`; total command time was
`12.862447034 s`, with `175244 KiB` maximum resident memory. Physics and excitation
took `8.505016762 s`; presentation took `3.908372072 s`. These measurements exclude
compilation and remain below the hard 60-second gate and the 30-second product target.

No M4 or fidelity-upgrade work may begin until the user listens and accepts or rejects
this candidate.
