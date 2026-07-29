# M4 BMW held operating-regression listening gate

Status: accepted by user listening on 2026-07-29
Recorded: 2026-07-29

## Execution

All four frozen held selectors were launched concurrently as separate processes from
clean source commit `5c284f78e79e3717007a78bbf8beef4217b274cc`. Every process
used its own new output directory and committed one complete ten-file publication.
The common renderer source-closure SHA-256 is
`20739a2322b270d8c54a538626acdc79409992293a41db800747145543feab5c`.

The concurrent batch completed in `15.8 s` of observed wall time. Individual render
times were `15.34..15.56 s`, and complete command times were `15.51..15.73 s`. This
measures four clips in parallel; it is not the sum of four sequential jobs.

| Point | Request-v3 SHA-256 | Cycles | Net torque (N*m) | Net BMEP (Pa) | Power (W) | Render (s) |
|---|---|---:|---:|---:|---:|---:|
| 1500 rpm / 0.85 throttle | `1174f63f1f86113e9ccb487443d3e3621e5689e630744fccf6926c4fcedf3062` | 47–78 | 229.56569353731581 | 1032851.4448462899 | 36060.094775338344 | 15.559662698 |
| 3000 rpm / 0.25 throttle | `d4cc35ab0a31f73e7c4f9c1d4f371af759652a9839763b256c1ae270ef353d6a` | 127–158 | 187.19684309648505 | 842227.45517261024 | 58809.622637331289 | 15.553841678 |
| 3000 rpm / 0.85 throttle | `11d8894182627ab53be0326c685fb78cacdc4fda6a758ebff64c29e95a83cb30` | 127–158 | 355.0789600617328 | 1597554.9799415683 | 111551.34510961543 | 15.342188825 |
| 6500 rpm / 0.85 throttle | `c648e36ebeb24d55d91586350b20cd9e087138bc0b388f79cb510ba39a31b190` | 315–346 | 315.47540528221413 | 1419372.4817433956 | 214737.296472255 | 15.538238569 |

## Listening files

- 1500 rpm / 0.85 throttle:
  `artifacts/listening/bmw-m52b28-held-regression-rpm1500-throttle0p85-5c284f7/audio/master.reference.audition.wav`
- 3000 rpm / 0.25 throttle:
  `artifacts/listening/bmw-m52b28-held-regression-rpm3000-throttle0p25-5c284f7/audio/master.reference.audition.wav`
- 3000 rpm / 0.85 throttle:
  `artifacts/listening/bmw-m52b28-held-regression-rpm3000-throttle0p85-5c284f7/audio/master.reference.audition.wav`
- 6500 rpm / 0.85 throttle:
  `artifacts/listening/bmw-m52b28-held-regression-rpm6500-throttle0p85-5c284f7/audio/master.reference.audition.wav`

Each audition file decodes without error as one `15.000000 s`, mono, `192000 Hz`
stream. Exact publication identities are:

| Point | Audition WAVE SHA-256 | Manifest v6 SHA-256 |
|---|---|---|
| 1500 / 0.85 | `876b82ed924b1731b0c5d43ed35ecd5d513d418861fc786f83f8f0981d7c53bc` | `cdde47dac0237dd4ee2350d7fb79758e92c70ffcf1c75cba61bb9963f432355a` |
| 3000 / 0.25 | `1128a3d2ee71e15f19b4b01f89273c2ac4403cf1e0dee438ef5d645ec7061b92` | `c2a194cb3dce68eebf95f3eaddc5f0003c626c8113ae19104db6e00a5bf76496` |
| 3000 / 0.85 | `86053d8d8118a94962a2522f1b93448be5fe9cd037b9b06c4d8b43ede18804f4` | `cecd37f565ea92c3972df2ac24a6c8a45f1cf3ca6857eafdcef46c593cc1763e` |
| 6500 / 0.85 | `7f68d58b75193e31075ce7888da21e735d2939db5e9376cace80ce889d2c926d` | `bbd86d03e4cdded74ede33f94f875c9e54e53d9eff60a3b2072e507df8912081` |

Every manifest hash matches its published sidecar. The manifest scenario IDs match
the four frozen token-to-point mappings exactly, closing the CLI selection check with
the actual renderer rather than a parser-only assertion.

## Listening acceptance and claim

These are exhaust operating-regression clips through the currently accepted
presentation. They contain neither an independent physical intake source nor an
independent mechanical source, and they do not claim new M5 fidelity or a complete
production engine mix.

The user listened to the published set and reported that it "sounds good." This
accepts all four clips as the M4 held operating-regression baseline and closes the
matrix listening stop. The acceptance covers audible coherence across the frozen RPM
and throttle cross; it does not turn the current exhaust-only presentation into an
intake/mechanical mix or claim new offline fidelity.
