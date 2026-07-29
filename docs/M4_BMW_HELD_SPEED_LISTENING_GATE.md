# M4 BMW held-speed listening gate

Status: historical one-point artifact accepted by user listening on 2026-07-29;
its pre-matrix fixed-sample bridge was PCM-identical and is now deleted from the
current API

The originally accepted M4 operating-regression clip holds the BMW M52B28 at
`3000 rpm` and `0.85` throttle for 15 audible seconds. It uses the accepted exhaust
presentation without a second audio path. This artifact predates the fixed-horizon
sampling cutover. Its subsequent one-point bridge and exact PCM comparison are
recorded as historical evidence in
[`M4_FIXED_SAMPLE_AUDIO_REGRESSION.md`](M4_FIXED_SAMPLE_AUDIO_REGRESSION.md).
The sole current held-listening surface is the pending four-point matrix frozen in
[`M4_BMW_HELD_REGRESSION_MATRIX.md`](M4_BMW_HELD_REGRESSION_MATRIX.md); neither this
scenario nor the bridge remains constructible through a current factory or CLI alias.

Listening file:

```text
artifacts/listening/bmw-m52b28-m4-held-3000rpm/audio/
  master.reference.audition.wav
```

The audition WAVE SHA-256 is
`52184f23e6451b6f7be831cb10fc36c5c1d3631d922f909aec78eca84fa1c201`.
The historical publication contains eight WAVE artifacts, its then-current manifest,
and its SHA-256 sidecar. It was rendered from clean commit
`8b933ce341d7596a274ca4605c61641c1572513a` in 13.20 seconds of render time and
13.36 seconds end to end.

The historical request-bound result reported:

- net shaft torque: `354.88038627572666 N*m`;
- net BMEP: `1596661.5658663868 Pa`;
- mean shaft power: `111488.96131535909 W`;
- adjacent-block torque residual: `0.14219052207965888 N*m`; and
- phase-aligned boundary-pressure residual: `870.20266385539435 Pa`.

Listening acceptance approves this clip as an audio regression point. It does not
approve the reported torque as manufacturer-plausible: `354.88 N*m` is a warning to
be resolved by the planned held-point sweep and landmark comparison.

The subsequently accepted natural dyno climb is recorded in
[`M4_BMW_INERTIAL_DYNO_LISTENING_GATE.md`](M4_BMW_INERTIAL_DYNO_LISTENING_GATE.md).
The M3 prescribed-RPM pull remains an accepted comparator but is not relabelled as an
M4 natural pull.
