# Packaged Ural startup and driving

The user accepted the engaged-coast audition, then requested a bake that could be
played interactively in the harness with the recorded starter. The resulting
single-file package is:

`artifacts/diesel-sound/ural-harness-20260914/ural4320-mechanical-v8-drive.crankwave`

SHA256: `684d71d62d068697906eb7337643bce35e5afb940bc8d5f7e8260aecf2d3e88f`

Open `http://127.0.0.1:4173/`, choose that file, and press **Start engine** in Drive
mode. The engine uses the existing heavy recorded starter, catches with the
accepted 1,150 RPM pickup, and settles at 725 RPM. Use N for free revs and D for
driving; release the accelerator in D for coupled coast. Automatic shifts leave
at least six seconds between shift starts and release/re-engage over 1.5 seconds.
Stopping playback and starting again replays startup. Reset drive resets road
motion without replaying the starter. Manual mode plays the underlying RPM/fuel
engine directly.

## What is saved

The composite bake keeps every original native engine entry unchanged. It adds
`diesel-drive/runtime.json`, the canonical shared recorded starter manifest/PCM,
and the heavy treated starter PCM. The profile binds the native engine source
hash and includes pickup controls, starter treatment/playback settings, listening
gain, coast radiation settings, and the authored vehicle rig parameters.

The canonical starter loader and asset remain unchanged. The loader verifies the
original recording and then the separate treated payload; engine-specific pitch
and handoff overrides apply only to this package's playback session. No real Ural
reference recording is included. No starter is synthesized from scratch.

This is an optional driving package used by the updated harness. A plain native
`bake-crankwave --engine engine.json` still produces the core running engine.
`CrankwaveAudioEngine` also retains its RPM/fuel-only contract. Hosts use the
additional `DieselDriveSession` for startup and mechanical backdrive radiation,
and provide coupling/reverse-torque controls from their vehicle model. The
harness's `DieselCoupledDrivingModel` provides these for this listening setup.
It does not encode the 48-second performance as a fixed recording.

All audio assets and settings are inside the `.crankwave`; playback still needs
the updated JavaScript consumer. Older consumers can ignore the optional entries
and play only the original engine. No external starter URL or filesystem fixture
is fetched during playback. Building the composite package requires Node, ffmpeg,
the native CLI and the canonical shared starter retained in this repository.

## Coupled driving

The motored torque bank and casing response are the same implementation used by
the accepted coast audition. The host derives fueled cycle-mean torque from native
indicated work, subtracts the authored friction and road resistance, and uses
engine plus reflected vehicle inertia while the clutch is locked. Engine RPM
then follows wheel speed in either torque direction. On overrun the wheel-to-engine
reaction excites the existing motored-pressure-driven casing modes. Neutral and
shift disengagement remove that reaction. Idle fueling returns near 725 RPM.

The vehicle ratios, friction, road load, launch, shifts and neutral RPM response
are authored listening controls, **not measured Ural specifications**. The second
gear reflected inertia and road drag preserve the accepted coast model; other
gears reflect those loads through their authored ratios. This does not add a
transmission recording, exhaust restriction or compression-release brake.

## Reproduce

Choose a fresh output directory:

```bash
node tools/bake-diesel-drive.mjs build/crankwave \
  data/engines/ural4320-mechanical-v8/engine.json \
  data/engines/ural4320-mechanical-v8/drive.json \
  artifacts/diesel-sound/ural-harness-new
node scripts/serve-crankwave-harness.mjs
```

## Validation

- Native bake, container verification and JavaScript loading passed. Original
  engine members are hash-identical before and after packaging.
- Replaying the accepted 48-second controls through `DieselDriveSession` differs
  from the previous audition by at most `2.98e-8` PCM after its common gain
  (RMS error `3.27e-11`). The peak is 0.38015. The comparison excludes export-only
  endpoint fades; startup, treated recording, phase and coasting all match.
- Browser integration passed with real AudioContext/AudioWorklet output at
  48 kHz, including starter, pickup, idle, driving, automatic shifting, in-gear
  coast, neutral release and manual mode. Zero underruns were reported on the
  test machine. Chrome output was muted; these are playback checks, not a new
  subjective listening judgment.
- Unit tests check momentum against an independent closed-form solution, locked
  wheel/crank speed, neutral/shift release, stopping at fueled idle, shift dwell,
  profile rejection, and existing package/starter/audio-engine behavior.
- The original engine-only Ural still passes harness playback. The three relevant
  runtime CTest targets and the installed-distribution test pass; installation
  includes the additional playback modules.

Logs, `bake.json`, `check-replay.mjs`, `replay-check.json` and the full packaged
replay WAV are retained under the output directory above.
