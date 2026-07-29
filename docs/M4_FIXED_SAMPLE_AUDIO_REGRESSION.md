# M4 fixed-sample audio regression

Status: passed; held and inertial audition PCM unchanged
Recorded: 2026-07-29

## Outcome

The fixed-horizon production path was rendered for the two one-point requests that
existed at the time of the incompatible preparation/result cutover. The WAVE
containers changed because their request, manifest, and renderer metadata changed.
Decoding each audition master to interleaved signed 16-bit PCM produced exactly the
same bytes as the corresponding user-accepted artifact.

| Clip | Accepted PCM SHA-256 | Current PCM SHA-256 | Result |
|---|---|---|---|
| Held `3000 rpm`, throttle `0.85` | `37564183879408a8b0b0192f92022ba4d121b457f71da3c75305e2ee3ab9bf16` | `37564183879408a8b0b0192f92022ba4d121b457f71da3c75305e2ee3ab9bf16` | identical |
| Inertial `1500–6500 rpm`, throttle `0.85` | `dce1964b853e51c3767bf5174b869970d0f038c2518b15d064fd41f4b34c6900` | `dce1964b853e51c3767bf5174b869970d0f038c2518b15d064fd41f4b34c6900` | identical |

The comparison command for each file was:

```text
ffmpeg -v error -i <audition.wav> -map 0:a:0 -f s16le -acodec pcm_s16le - | sha256sum
```

## Historical pre-matrix held bridge

This request and its generic `held` CLI selection were subsequently deleted by the
four-point held operating-regression cutover. The artifact is retained only as
evidence that fixed-horizon sampling itself did not change the accepted audio signal;
it is not a current factory, scenario, or listening entry point.

- source commit: `531a496ec35e756ceb5d6ddc153e5ae8d41b8bea`
- scenario: `bmw-m52b28-held-3000rpm-listening-v2`
- fixed preparation horizon / trailing sample: `3.22 s` / `32` cycles
- sampled cycles: `47..78`
- sampled net shaft torque / BMEP: `354.80929101468683 N*m` /
  `1596341.6973269931 Pa`
- output:
  `artifacts/listening/bmw-m52b28-m4-held-fixed-sample-531a496`
- audition WAVE SHA-256:
  `fdcc1e9d199c55f5302f2a3c3b753719b68f6e08e83864b3470692badb1b3943`
- manifest v6 SHA-256:
  `440c07ef4d16dfb4dda6ebfe258e7187fcdfc2407608748d60aa0e4cdff60cab`
- render / command time: `12.825 s` / `12.997 s`

## Historical pre-matrix inertial render

The inertial factory remains current, but this exact artifact predates the later
operating-model authority update made for the held matrix. It is retained as PCM
regression evidence, not presented as a render of the current request identity.

- source commit: `9a15e422376d8cbc270e34deaff6573e0a03a3d4`
- scenario: `bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2`
- fixed preparation horizon / trailing sample: `6.44 s` / `32` cycles
- release / first target / end frames: `64400` / `207679` / `214400`
- end speed: `6721.0670087908829 rpm`
- output:
  `artifacts/listening/bmw-m52b28-m4-inertial-dyno-fixed-sample-9a15e42`
- audition WAVE SHA-256:
  `87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444`
- manifest v6 SHA-256:
  `d5db2ec1f0074aebd845cc09017073b250af9a654081cdaef349805f302aab77`
- render / command time: `15.759 s` / `15.931 s`

## Meaning

This proves the M4 fixed-horizon sampling replacement did not change either accepted
audio signal. It does not claim new fidelity. The next separate gate is a held
RPM/throttle matrix that broadens the operating regression set and stops for user
listening.
