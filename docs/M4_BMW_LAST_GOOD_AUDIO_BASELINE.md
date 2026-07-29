# BMW M52B28 last-good audio baseline

Status: **authoritative last-good listening baseline**

Decision owner: user

Scope: the BMW M52B28 natural 1,500--6,500 rpm dyno sound immediately before the
equal-route experiment and rejected M5 acoustic replacement

## 1. Listening authority

The user identifies this exact file as the last result that still sounded like an
engine:

```text
reference/oracles/bmw-m52b28/
  bmw-m52b28-last-good-ffcc45c-dyno-1500-6500rpm.wav
```

It is a byte-identical tracked copy of:

```text
artifacts/listening/bmw-m52b28-route-balance-ab-ffcc45c/
  05-dyno-1500-6500rpm-A-inherited.wav
```

and of the original published artifact:

```text
artifacts/listening/bmw-m52b28-route-balance-a-inherited-dyno-ffcc45c/
  audio/master.reference.audition.wav
```

Exact identity:

| Field | Value |
|---|---|
| WAV SHA-256 | `87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444` |
| Byte count | `8,640,578` |
| Encoding | mono PCM signed 24-bit little-endian |
| Sample rate | `192,000 Hz` |
| Duration | `15.000000 s` |
| Scenario | `bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2` |
| Manifest build commit | `ffcc45c8412f0235429dd6cb1ebe9d7f97159abd` |
| Manifest source closure | `5415c0a21bfcbe9ad379ad48b15972f7424dbe9156269c087d699178d5e1c6b3` |
| Sound-producing implementation commit | `4b651273e007553a793762f847ba237a9ee3396f` |

`ffcc45c` and the commits between `4b65127` and it changed evidence and
documentation, not the sound-producing implementation. The complete original
manifest remains beside the ignored render and pins the compiler, numeric policy,
resolved engine/scenario, routes, DSP, seeds, stems, and payload hashes.

This baseline is an audible floor, not a claim of physical BMW accuracy. It retains
the inherited odd/even cylinder routing and `0.5 / 1.0` route-level imbalance that
creates some false roughness. The user nevertheless judges it substantially more
engine-like than either subsequent direction.

## 2. First rejected departure

The adjacent comparison is:

```text
artifacts/listening/bmw-m52b28-route-balance-ab-ffcc45c/
  06-dyno-1500-6500rpm-B-equal-routes-level-matched.wav
```

Its SHA-256 is
`318d3ecd8037149dcb8e782af0ce1e516a978d52e10d18feb97b5a84ec2960b5`.
It changes the inherited `0.5` route to equal gross level and then level-matches the
result. The user reports that it removed character, felt overly smooth, and no longer
felt like a real engine. It is a diagnostic, not an accepted baseline.

Commit `101832d` then made equal `1.0 / 1.0` authority and the physically supported
front/rear `1-2-3 / 4-5-6` grouping canonical in code. That structural correction was
never accepted by listening and must not be mistaken for the last-good checkpoint.

## 3. Timeline after the baseline

All times are Europe/Sofia local time on 2026-07-29:

| Time | Commit/artifact | Effect |
|---|---|---|
| 15:23 | `05-...-A-inherited.wav` | Last-good file retained from `ffcc45c`. |
| 15:23 | `06-...-B-equal-routes-level-matched.wav` | First rejected smoothing diagnostic. |
| 15:52 | `9dbc2c6` | Documented the proposed M4 topology correction; no audio code changed. |
| 16:05 | `101832d` | Made equal routes and adjacent front/rear groups canonical; first sound-authoring departure from the baseline. |
| 16:15 | `81386e3` | Removed a dead compression-ignition flag; not intended to affect audio. |
| 16:41 | `d739ddb` | Froze the M5 physical-exhaust design. |
| 16:52--17:45 | `66fce0e` through `0f1388b` | Added the acoustic contracts, ideal wave network, 80-to-192 kHz reconstruction, outlet radiation, substep capture, session, and BMW evaluation geometry behind the still-active M4 renderer. |
| 19:11 | `33b3ad8` | Deleted the inherited excitation/presentation production path and cut the BMW renderer over to M5 outlet pressure. This is where production audio changed to the radically synthetic result. |
| 19:16 | `4d3569e` | Reduced common audition gain to remove clipping; physical stems and bad timbre were unchanged. |
| 19:24 | `32fb288` | Re-sealed identities/provenance; physical audio was unchanged. |
| 23:51 | `335130d` | Recorded the M5 listening rejection and blocked M6. |

The M5 implementation added roughly 9,300 lines and removed roughly 4,300 relative
to `81386e3`. It did not incrementally enhance the baseline: it replaced the source
observable, routing/presentation contract, transfer model, radiation, artifacts, and
manifest together.

## 4. Verified recovery

Commit `3f5fa1c` mechanically restored the complete accepted runtime, contract,
manifest, build, and test cluster from `ffcc45c` as the sole implementation. It
removed the rejected M5 acoustic runtime rather than retaining a selectable legacy
path.

The clean recovery commit passed all `68 / 68` tests and rendered this natural dyno:

```text
artifacts/listening/bmw-m52b28-recovered-last-good-3f5fa1c/
  audio/master.reference.audition.wav
```

The render completed in `15.25 s`. Its complete WAVE file, including metadata, is
byte-for-byte identical to the tracked listening oracle:

```text
87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444
```

The user listened to this recovered render on 2026-07-30 and explicitly reaccepted
it. Commit `3f5fa1c` and the hash above are therefore the current audible return point,
not merely a mechanically reconstructed historical candidate.

No cleanup or new sound work was included in this proof. In particular, the
previously approved dead compression-ignition-field cleanup remains a separate future
change so it cannot be hidden inside the audible recovery.

## 5. Recovery and future comparison rule

Recovery means making the `4b65127`/`ffcc45c` sound-producing behavior the sole
current path again, not adding a legacy switch or maintaining two implementations.
The unrelated removal of the dead compression-ignition flag may remain. The rejected
M5 runtime is not a foundation for further fidelity work; its documentation remains
as failure evidence.

Every later sound-affecting experiment must:

1. begin from the tracked last-good WAV above;
2. replace one source or transfer seam only;
3. publish an immediately level-matched natural-dyno A/B;
4. retain the last-good implementation until the user accepts B by ear; and
5. never infer acceptance from waveform metrics, structural tests, or a promised
   later phase.
