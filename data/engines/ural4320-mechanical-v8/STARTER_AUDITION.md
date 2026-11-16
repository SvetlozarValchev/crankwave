# Ural treatment of the existing recorded starter

The starter treatment, pickup and accepted engaged coast are now preserved in
an optional [single-file harness driving package](HARNESS_DRIVE.md). The notes
below retain the original offline audition workflow and its earlier status.

The user requested a heavier starter leading into the accepted Ural engine and
clarified that the existing starter recording should be reused. This audition
uses that recording only for the starter; no synthesized motor, compression
strokes, clunks or procedural starter noise are mixed in.

The original shared source is `shared-recorded-starter-cc0-v1`, “Car not
starting.wav” by Ika.Komura, already retained under
`reference/fixtures/responsive-audio/shared-recorded-starter/`. Its canonical
manifest and PCM are authenticated by `loadSharedRecordedStarterRuntime` before
processing. The source's existing rights/provenance record is copied to the
candidate metadata.

The Ural audition applies these local overrides:

- Base playback rate 0.74 (about −5.2 semitones, also slowing the recorded cadence),
  rising smoothly toward 0.9 during the catch transition.
- 32 Hz high-pass, +4 dB at 180 Hz, −2 dB at 520 Hz, −3 dB at 2,400 Hz,
  and a 4,300 Hz low-pass. The complete EQ output is matched once to the original
  source RMS before playback.
- A 14 ms onset, 55 ms RPM smoothing, and a 220 ms starter handoff.

Playback reuses `SharedRecordedStarterCursor` with an audition-local copy of the
verified package and settings. The source asset, its strict loader, the shared
runtime defaults and the accepted Ural `.crankwave` remain unchanged. This is a
listening candidate, not yet a serialized diesel lifecycle feature.

The running engine comes from the freshly baked accepted version 11 Ural carrier
at `artifacts/diesel-sound/ural-fresh-20260912/ural4320-mechanical-v8.crankwave`,
SHA256 `1dedf91ae3d7238c756debe244460bafe50607306703eee1a258c295f9818b18`.
Native running starts at 550 RPM. The original gentle pickup reached only 795 RPM;
the user requested a harder catch with a little rev before settling. The current
default `surge` raises fueling to .62 and RPM to 1,150 within 300 ms, lets RPM fall
through 690, then settles at 725 within 1.6 seconds. A 55 ms onset fade replaces
the preceding 160 ms fade. The recorded starter PCM and its treatment are unchanged.
These are authored RPM/fueling controls of the existing engine; no extra pickup
or starter sound layer is synthesized. The
starter handoff is aligned to the running engine's fixed audio latency. This is
an authored transition, not a battery or cold-start simulation; the starter's
lower-RPM telemetry is not fed outside the native capture grid.

## Listening files

The latest pickup revision is under
`artifacts/diesel-sound/ural-start-pickup-20260914/audition/`. The preceding gentle
version remains under `artifacts/diesel-sound/ural-recorded-starter-20260914/audition/`.
Both contain:

- `default-then-heavy-starter-48k.wav`: default recorded starter 0–4 s, same
  recording with the Ural treatment 4.4–8.4 s. Each is matched once to −25 dBFS RMS.
- `start-and-idle-48k.wav`: seven seconds, reused starter into native Ural pickup
  and idle. The revised pickup peaks around 3.35 s and settles around 4.65 s.
- `start-idle-revs-coast-48k.wav`: 24 seconds. Crank begins .4 s, running sound
  arrives around 3.05 s, idle to 8 s, rev 8–12 s, hold 12–13 s, coast 13–19 s,
  idle to 24 s.

The complete audition uses one constant gain to −23 dBFS RMS. No section-by-section
leveling or compression is applied. The short clip preserves the full audition's
gain. Listening approval of the heavier treatment is pending.

`artifacts/diesel-sound/ural-start-pickup-20260914/before-then-pickup-48k.wav`
compares the preceding start at 0–7 s with the revised pickup at 7.4–14.4 s. A
single common gain preserves the pickup's increase in strength. The 3.05–3.65 s
catch window is approximately 6 dB stronger, while settled idle RMS differs by
only .012 dB. The starter track and pre-catch mix are sample-identical. Controls
after 5 s match the previous audition; engine samples differ because the new
pickup accumulates a different crank phase. `verification.json` records these checks.

## Reproduce and checks

For the requested continuous start-to-drive performance:

```bash
node data/engines/ural4320-mechanical-v8/starter-audition.mjs \
  artifacts/diesel-sound/ural-fresh-20260912/ural4320-mechanical-v8.crankwave \
  artifacts/diesel-sound/ural-start-drive-20260914/audition surge drive
```

The resulting `start-revs-first-second-coast-48k.wav` is 48 seconds:
starter/pickup/idle 0–8 s, three separate stationary revs with pauses 8–21 s,
pull away and accelerate in first 21–30 s, shift pause 30–31.5 s,
accelerate in second 31.5–39 s, coast in second 39–48 s. Stationary revs peak at
1,300, 1,650 and 1,900 RPM with short fuel requests. Gear pulls use sustained
fueling; the shift includes fuel cut and a drop from 2,050 to approximately
1,150 RPM. Final overrun cuts fuel and ends around 1,050 RPM.

This performance prescribes RPM/fueling and the timing of clutch engagement. Gear
labels record its intended sequence; no vehicle, transmission or road model is
solved. It does not use the harness's UAZ vehicle parameters. The engine renders
continuously without resetting at revs or shifts, and no gear-change recording or
new sound layer is added. `controls.json` stores every 20 ms control and stage;
`audition.json` stores the authored keyframes and timeline.

The first eight seconds of raw mixed PCM exactly replay the preceding pickup
audition, and the entire recorded starter is unchanged. The full listening file
is normalized once to −23 dBFS RMS, so its overall gain differs from the shorter
audition. `verification.json` checks three rev peaks, nine seconds in first,
1.5 seconds shifting, 7.5 seconds in second, fuel-cut overrun with audible engine
output, exact source preservation and finite output with headroom. `sequence.png`
plots RPM, fuel and audio level. These files and `verify.py` are retained under
`artifacts/diesel-sound/ural-start-drive-20260914/`.

To reproduce the preceding 24-second pickup audition:

```bash
node data/engines/ural4320-mechanical-v8/starter-audition.mjs \
  artifacts/diesel-sound/ural-fresh-20260912/ural4320-mechanical-v8.crankwave \
  artifacts/diesel-sound/ural-start-pickup-20260914/audition
```

Choose a fresh output directory. Requires Node and ffmpeg; reference recordings
of the real Ural are not dependencies. The optional pickup argument is `surge`
(default) or `gentle`; a further sequence argument is `standard` (default) or
`drive`. The script checks
canonical source hashes,
unchanged source/carrier bytes, exact starter replay across different read sizes,
complete starter release and that adding the starter leaves the underlying running
PCM untouched after four seconds. All audio
is finite with export headroom. `audition.json`, `controls.json` and
`audio-checks.json` retain provenance and validation.

The isolated A/B has effectively equal RMS (difference below .002 dB). Its measured
spectral centroid changes from approximately 1,539 to 789 Hz, and relative
60–300 Hz energy rises by about 5.9 dB. These confirm the intended pitch/EQ change;
perceived weight still requires listening.

A subsequent [engaged-coast experiment](BACKDRIVE_COAST.md) addresses feedback
that the final rundown sounded free-floating. It preserves the sequence before
39 s, couples the final RPM fall to reflected vehicle inertia, and tests a
pressure-torque-driven mechanical response during backdrive. Pass `coupled` after
`surge drive` to render it, `motion-only` to isolate its RPM effect, or `legacy`
(the default) for the original prescribed coast.
