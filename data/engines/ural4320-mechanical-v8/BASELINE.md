# Accepted diesel baseline

This checkpoints the pressure-texture version 5 implementation before further
idle-character and coasting-layer changes. The user described this version as
“much much better” and identified those two remaining areas for tuning.

The baseline consists of the tracked engine definition, native pressure/flow baker,
JavaScript runtime and the driving harness. Downloaded references and generated
audio remain local under the ignored `artifacts/` directory.

| Item | SHA-256 |
|---|---|
| engine_definition | `31f7eea58e123eeb5c3f37e3863157cb4f281e32f184a2e83ae647113a882809` |
| runtime | `7f1042c7b076268835aaa29bb724dc3d404b5cdb20399d6524cbc9b0114e326c` |
| carrier | `87c9d5b5664156673bab93531749196aab7e4af6feacdee24c63fbf5c868fd1e` |
| idle_725 | `9dd03577290fbc1447a090c895d1a18ab070e10c14563c973d5c95259fc4f081` |
| idle_to_revs | `8e6dd76d7d5a3c3886440ace0d472457d3520bfb3039fcd4ccb532b4b3335e82` |

The approved package is locally available at
`artifacts/diesel-sound/ural-pressure-texture/verified/ural4320-mechanical-v8.crankwave`.
Its listening copies are `idle-725-48k.wav` and `idle-to-revs-48k.wav` in the same
parent directory. The latter has four seconds at 650 RPM, acceleration to 2,100 RPM,
a one-second hold, fuel-cut deceleration, and a final return to idle (14 seconds).
The driving harness's authored idle is about 722.5 RPM; the separate idle clip uses
725 RPM. Native captures cover 550–2,275 RPM and zero through full fueling.

Rebuild from this branch using the commands in [README.md](README.md), choosing a
new output path. The carrier binds the exact baker executable digest, so another
build may have a different package hash; the recorded hash identifies the original
approved artifact. Reference audio is not required to bake or play the model.

Validation before checkpoint: 121 runtime tests, native diesel definition/bake,
Ural native bake-to-JavaScript playback, both diesel schemas, and the browser's
real Worker/DSP path with its existing audio-device test double. User acceptance
establishes this listening baseline; it does not establish a measured Ural match.
