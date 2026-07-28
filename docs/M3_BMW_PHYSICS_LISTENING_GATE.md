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

The physics-to-WAV pipeline took `12.421992373 s`; total command time was
`12.862447034 s`. These measurements exclude compilation.

No M4 or fidelity-upgrade work may begin until the user listens and accepts or rejects
this candidate.
