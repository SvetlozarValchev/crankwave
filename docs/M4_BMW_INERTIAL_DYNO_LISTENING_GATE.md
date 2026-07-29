# M4 BMW inertial-dyno listening gate

Status: original artifact accepted by user listening on 2026-07-29; its historical
pre-matrix fixed-sample rerender is PCM-identical

Source commit: `f3657973f510dae7dd5c43e3458ccf34752562e6`

The originally accepted M4 BMW M52B28 inertial-dyno scenario holds the warm engine at
`1500 rpm`, then releases it at `0.85` throttle against a declared `40 N*m` passive
brake and `7.9 kg*m^2` total crank-referred test-cell inertia. Crank speed is advanced
from modeled shaft torque and inertia; it is neither prescribed nor clamped to the
listening target. This artifact predates the sole fixed-horizon sampling cutover; its
pre-matrix fixed-sample rerender and exact PCM comparison are retained as historical
evidence in
[`M4_FIXED_SAMPLE_AUDIO_REGRESSION.md`](M4_FIXED_SAMPLE_AUDIO_REGRESSION.md). The
inertial factory remains current, but neither artifact is claimed to carry the request
identity produced after the held-matrix operating-model authority update.

Listening file:

```text
artifacts/listening/bmw-m52b28-m4-inertial-dyno-1500-6500rpm/audio/
  master.reference.audition.wav
```

The audition WAVE SHA-256 is
`2ed325c5a031c6a5e19e5db24473d4898ddcefbff1141a65e4f22a3083a9890f`.
The historical transactional publication contains eight WAVE artifacts, its
then-current manifest, and its SHA-256 sidecar. The manifest SHA-256 is
`15ac8cef1da2d684288ab6a14f27b921d0c393a01bad003eed9198aa69fcda4a`.

The historical request-bound result reported:

- preparation release: frame `64400`, exactly `1500 rpm`;
- first `6500 rpm` target crossing: frame `207679`, `14.3279 s` after release;
- fixed-horizon end: frame `214400`, `6721.0670087908829 rpm`;
- full-horizon speed range: `1499.7261473849326` to
  `6721.0670087908829 rpm`;
- net shaft work: `2113012.1769847944 J`;
- passive-brake absorbed work: `253745.0670223138 J`;
- kinetic-energy change: `1859267.1099624322 J`; and
- signed energy residual: `4.8428773880004883e-08 J`.

The 15-second audible pull rendered in `14.875637786 s`; the complete command took
`15.050660916 s`. This is below the 30-second single-clip target on the acceptance
machine.

The user listened to the audition master and reported that it was "spot on." This
accepts the natural inertial pull as an M4 operating-regression point. It does not by
itself claim higher fidelity than M3, validate the modeled BMW torque curve, complete
the intake or mechanical production buses, or advance the project to M5.
