# M4 BMW held idle-region and low-load listening gate

Status: awaiting user listening
Recorded: 2026-07-29

## Execution

The two frozen selectors were launched concurrently as separate processes from clean
source commit `4b651273e007553a793762f847ba237a9ee3396f`. Each process owned a
fresh request and a distinct new publication directory. Both manifests identify the
same renderer source closure:
`5415c0a21bfcbe9ad379ad48b15972f7424dbe9156269c087d699178d5e1c6b3`.

Both renders finished in the same approximately `20.1 s` concurrent wall window.
Their individual production render times were `19.849 s` and `19.879 s`, below the
project's approximately 30-second per-clip target.

| Point | Request-v3 SHA-256 | Cycles | Net torque (N*m) | Net BMEP (Pa) | Power (W) | Render (s) |
|---|---|---:|---:|---:|---:|---:|
| 700 rpm / 0.0 throttle | `4f821ebaec87cd94356089bba60d53ac4f1122054db6fda5ef5253c18d0003ed` | 42–73 | 27.185830724270154 | 122313.24336946222 | 1992.8254730419858 | 19.849035492 |
| 1500 rpm / 0.10 throttle | `59b3a547bf5f268ee38c043522c1ae1c2ebbb3ad184088621a9740152756db78` | 127–158 | 56.980108694037874 | 256362.29301208412 | 8950.41453344201 | 19.879361463 |

The `700 rpm` result confirms the known physical caveat rather than hiding it: this
engine still produces about `+27.19 N*m` at closed command over the frozen sample, so
the brake dyno absorbs torque to hold the requested speed. This is an idle-region
audio capture, not a self-regulated or unloaded free idle.

The implementation was committed before publication. A clean rebuild and the full
suite passed `68/68` tests in `27.95 s`, including the public BMW render. The focused
request test pins both exact request identities and cycle ranges, exercises both
simulation samples, and reproduces the 700-rpm operating result independently. No
permanent full-WAV end-to-end test was added.

## Listening files

- Held 700-rpm idle-region condition:
  `artifacts/listening/bmw-m52b28-held-idle-region-rpm700-throttle0-4b65127/audio/master.reference.audition.wav`
- Held 1500-rpm low-load condition:
  `artifacts/listening/bmw-m52b28-held-low-load-rpm1500-throttle0p10-4b65127/audio/master.reference.audition.wav`
- Previously accepted 1500-rpm / 0.85 comparison anchor:
  `artifacts/listening/bmw-m52b28-held-regression-rpm1500-throttle0p85-5c284f7/audio/master.reference.audition.wav`

Each new audition file decodes without error as one `15.000000 s`, mono, `192000 Hz`,
signed 24-bit PCM stream. Exact publication identities are:

| Point | Audition WAVE SHA-256 | Manifest v6 SHA-256 |
|---|---|---|
| 700 / 0.0 | `b2594e8cfd0bec4141a3fa39b934fe01044a9c5aaa37df928a5bb1d13bbebd9f` | `0532252a85c44617e7a648cfe3ea6384cdf852d450628ea96a451aeb4f8df346` |
| 1500 / 0.10 | `3d3832e25a07c66383a2a690d279543a6f8610d1049e8f52634fc5b0fd947ce5` | `db83f009636337c7e8818d36af95330f8fb64b7d50626a98ce48b038e6522930` |

Both manifest sidecars match their manifest bytes, and each atomic publication has
the exact eight declared audio artifacts plus manifest and sidecar. The float raw
masters contain zero NaNs, infinities, or denormals; `ffmpeg astats` reported DC
offset `0.000000` for each. The audition-master peaks are `-44.06 dBFS` at 700 rpm
and `-30.85 dBFS` at 1500 rpm, so
neither clips. Their large level difference is an uncorrected result of the common
accepted presentation; no point-specific normalization, gain, EQ, noise, or IR was
introduced.

## Listening decision

Please judge the 700-rpm file for clean, cycle-resolved low-speed behavior, then compare
the 1500-rpm / 0.10 file with the accepted 1500-rpm / 0.85 anchor for a believable
lower-load change. The first file will be substantially quieter, so playback gain may
need to be raised.

Work stops here. Acceptance would close only this held idle-region/low-load audio gate.
It would not claim self-regulated free idle, BMW torque calibration, load-target
solving, transients, physical intake/mechanical sources, or an M5 fidelity improvement.
