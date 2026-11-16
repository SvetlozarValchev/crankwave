# Ural-4320, 1980-era KamAZ-740 candidate

Independent listening candidate. The liked `ural4320-mechanical-v8` YaMZ
definition, driving profile, starter treatment and packaged baseline are unchanged.
This candidate is not yet approved by listening and is not a replacement baseline.

The engine is a naturally aspirated 10.85-litre mechanical V8. The KamAZ repair
and maintenance manual, printed page 21, gives 120 × 120 mm bore/stroke, 17:1
compression, firing order 1-5-4-2-6-3-7-8, and 154 kW / 210 PS at 2600 RPM.
Its valve diagram gives intake 13° BTDC / 49° ABDC and exhaust 66° BBDC /
10° ATDC. These replace the YaMZ geometry and authored valve windows in this
separate definition. The intake stays at atmospheric pressure; no turbo layer
is present.

The 650 RPM warm idle, connecting rod length, effective valve areas, thermal
coefficients, fixed injection timing, acoustic transfer and vehicle dynamics are
authored approximations. Full fuel is inferred as roughly 60 mg per event from
rated power and fuel consumption. This is a KamAZ-740/740.10 family approximation,
not a validated pressure/torque model or an exact reconstruction of a particular
1980 truck.

The acoustic experiment uses shorter bank delays, a less dominant right bank,
higher block/valve resonances and slightly less cycle variation. It retains the
accepted rounded loaded pressure excitation, smooth valve contacts, low-fuel
exhaust balance and fuel-independent mechanical motion. Source microphone
positions and operating conditions differ: the reference spectra are useful
for inspection, not proof of an acoustic match.

All running engine sound comes from the native gas-cycle bake and procedural
playback. The optional driving package reuses exactly the existing recorded
starter with its heavy EQ/rate treatment, followed by the fueled pickup overshoot.
The coast uses native motored gas work, reflected vehicle inertia and the
existing backdrive radiation; there is no added compression-release brake.

## Sources

- [KamAZ repair and maintenance manual, p.21](https://dvigatelkama.ru/wp-content/uploads/2016/09/Instruction_KAMAZ.pdf).
  The valve diagram was inspected directly. A downloaded copy and page image are
  in the artifact reference directory.
- [UralAZ engineers' history of the engine transition, printed p.44](https://www.booksite.ru/promych/2001/2001_12.pdf).
  Describes the earlier KamAZ-powered Ural and the transition to YaMZ in 1993.
- [Owner recording: «Звук двигателя камаз 740», КАМАZ TRANS 30](https://www.youtube.com/watch?v=epQ6fEGWxsQ),
  uploaded 2017-09-08, 202 seconds. Exact engine subvariant and truck year are
  unconfirmed. The local 78–90 second excerpt is unprocessed except mono decoding.
- [Additional uploader-labelled KamAZ 740 audio, Олександр Коляда](https://www.youtube.com/watch?v=WnC1l6o3EYM),
  uploaded 2020-02-08, 42 seconds. Static engine illustration; recording origin
  and exact variant are unconfirmed. Low-frequency contamination limits its use.

Reference audio is kept only for local comparison and is not embedded in the
generated engine. Source URLs, uploader metadata and download hashes are in
`artifacts/diesel-sound/ural-kamaz1980-20260914/reference/sources.json`.

## Bake and audition

From the repository root, choose output directories that do not already exist:

```sh
node tools/bake-diesel-drive.mjs build/crankwave \
  data/engines/ural4320-kamaz740-1980/engine.json \
  data/engines/ural4320-kamaz740-1980/drive.json \
  artifacts/diesel-sound/ural-kamaz1980-20260914/bake

node data/engines/ural4320-kamaz740-1980/audition.mjs \
  artifacts/diesel-sound/ural-kamaz1980-20260914/bake/ural4320-kamaz740-1980-drive.crankwave \
  artifacts/diesel-sound/ural-kamaz1980-20260914/audition \
  artifacts/diesel-sound/ural-harness-20260914/ural4320-mechanical-v8-drive.crankwave
```

The 48-second audition includes starter/pickup/idle (0–8 s), three stationary
revs (8–21 s), launch and first gear (21–30 s), a 1.5-second shift pause,
second gear (31.5–39 s), then a coupled coast (39–48 s). Revs and loaded pulls
use prescribed controls. The final coast is integrated from the packaged
backdrive profile and falls from 2400 to approximately 1985 RPM.

The comparison alternates **YaMZ then KamAZ**, four seconds each with a
half-second gap: idle starts at 0 / 4.5 seconds; loaded 1500 RPM at 9 / 13.5;
fuel cut at 1800 RPM at 18 / 22.5. These held comparisons use the packaged
gains, without per-section loudness matching. The fuel-cut comparison holds
RPM and applies the same authored 100 Nm reverse load to both engines; the
full performance demonstrates actual rundown. Only the comparison excerpt
edges are faded.

Load either engine's own `*-drive.crankwave` into the harness and select
**Start engine**. Each carrier contains its engine, driving profile and recorded
starter, so switching between them needs no shared tuning change.

## Validation, 2026-09-14

- Schema and native bake/package verification passed: 77 cells, 550–2800 RPM.
- Real Chrome AudioWorklet check passed: startup, pickup, idle, acceleration,
  shifts, coupled coast, neutral release and manual controls; zero underruns.
  The artifact-local copy of the existing harness check changes only its import
  location and idle assertions from 725 to 650 RPM.
- Full render: finite samples, peak 0.368, no clipping. Process-boundary jumps
  did not exceed ordinary within-block jumps; this is not a listening verdict.
- All 19 protected YaMZ/drive/runtime files retained their SHA-256 identities.
- New carrier SHA-256:
  `04bb0cccdade0ad63252f6c27115d2e93cdd7afb6b0997f3e241b4b4c277395f`.
- Preserved liked YaMZ carrier SHA-256:
  `684d71d62d068697906eb7337643bce35e5afb940bc8d5f7e8260aecf2d3e88f`.

Logs, control trace, spectrograms, signal checks and the protected-baseline hash
list are under `artifacts/diesel-sound/ural-kamaz1980-20260914/`.
