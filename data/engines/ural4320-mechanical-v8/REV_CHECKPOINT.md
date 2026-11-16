# Rounded rev checkpoint

The user accepted the softened bank-pulse experiment and requested a commit.
Version 11 integrates it on `tuning/diesel-rev-character`, retaining the earlier
coasting baseline `a1e7a26` on `baseline/diesel-flowing-coast`.

The fully loaded rev is byte-identical to the accepted `ural-rounded-rev` WAV.
The integrated load fade is new: below 0.2 fuel the earlier texture is retained,
above 0.5 the rounded texture is selected, with a smooth 40 ms transition between
them. Held idle/coast, settled trajectory idle/coast, physical captures and
exhaust/mechanical stem energies are verified unchanged.

| Item | SHA-256 |
|---|---|
| engine_definition | `575411dcb083feb73bd83e0fa2603280f14b544785e6c169649a21ba8fd26552` |
| runtime | `b0c59272c4c8769a0c6caaede5d132f83117c088cc82f84afa589c205683ef82` |
| carrier | `c2e4aa4235cd3908412aa0770f9cee6da2982a488009c5f6db2faece5fd65d8e` |
| idle_revs_coast_48k | `fe5b32efc562e0bb876c826429b2f6d75dd6e65b90b0f50ca42a54b7687cdb70` |

Local carrier: `artifacts/diesel-sound/ural-rounded-loaded/verified/ural4320-mechanical-v8.crankwave`.
Listening copy: `artifacts/diesel-sound/ural-rounded-loaded/idle-revs-coast-48k.wav`.
The complete 18-second audition uses the unchanged original master gain; idle
0–4 s, acceleration 4–8 s, hold 8–9 s, coast 9–15 s, final idle 15–18 s.
The five-second fully loaded rev and six-second gear-change audition are also
available in that directory, along with controls, render scripts and verification.

Validation: 146 runtime tests (40 diesel-specific), native definition/bake tests,
Ural native package integration, both authoring schemas, real browser Worker/DSP
with the existing audio-device test double, exact version 10 replay and exact
accepted rounded-rev replay. These checks establish implementation/replay behavior,
not a measured acoustic match. The full blended trajectory remains available for
further listening feedback. No reference recording is embedded in the package.

Rebuild with the commands in [README.md](README.md), choosing new output paths.
The carrier binds the baker executable digest, so a different build can produce a
different container hash. See [REV_CHARACTER.md](REV_CHARACTER.md) for the acoustic
investigation and [COASTING_BASELINE.md](COASTING_BASELINE.md) for the prior checkpoint.
