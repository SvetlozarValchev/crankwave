# UAZ-469 reference-3m with mechanical texture

A separate presentation experiment based on `uaz469-reference-3m`. This directory
contains an exact snapshot of the original authoring files plus
`mechanical-tuning.json` and a reproducible audition. The original directory and
carrier remain untouched. The physical engine ID stays `uaz469-reference-3m`
because the existing gasoline simulation captures are retained.

## Reuse from the diesel work

| Component | Reuse in this experiment |
| --- | --- |
| Smooth valve contacts | The exact `DieselMechanicalTexture` implementation, driven by the UAZ cam windows and firing phases |
| Crank-timed intake breath | The same seeded source, with amplitude scaled by the gasoline carrier's captured manifold pressure |
| Contact smoothing and deterministic variation | Retained; the added layer follows the facade's crank clock, warmup and presentation latency |
| Pressure-driven block radiation | Requires gasoline per-cylinder pressure captures and a suitable motoring reference; not present in this carrier |
| Diesel exhaust transfer and coasting settings | The UAZ already has a tuned exhaust response; this experiment retains that response and its gasoline load behavior |

The adapter does not change spark combustion, carburetor parameters, mixture,
ignition timing or exhaust captures. Closed throttle uses the gasoline package's
low-manifold-pressure running material. It does not become diesel fuel cut.
The mechanical layer is added after the existing exhaust presentation, so valve
contacts do not pass through the exhaust's impulse response.

The current tuning uses 1.6 ms smooth valve contacts, an 850 Hz base valve
resonance, 2.2 ms decay and a quieter intake layer. Breath amplitude is multiplied
by `(MAP / 101325)^0.75`, capped at one. These are authored acoustic choices, not
measured UAZ contact forces or intake turbulence. Valve amplitude continues to
follow crank speed when the throttle closes.

Cam duration at 0.050-inch reference lift is shorter than the full opening window.
The attachment tool derives the zero-lift support of the native harmonic lobe
formula, applies cam advance and removes each cylinder's firing offset. This is
an acoustic event-time estimate; it does not solve lash, contact force or the
native sampled-lobe interpolation. The tool currently accepts fixed harmonic
cams with common windows relative to firing, and rejects unsupported layouts.

## Playback and reproduction

The separate carrier loads normally through the updated JavaScript
`CrankwaveAudioEngine` and web harness. It carries its configuration in the
optional `runtime.json.mechanical_texture` field. The original package does not
enable that field, and its rendered PCM is preserved.

This is not automatically supported by the Godot adapter, older JavaScript
consumers, direct `HeldTexturePresentationRuntimeCursor` consumers or native CLI
rendering. Those hosts need the corresponding mixing adapter. Existing recorded
startup/shutdown assets are retained; the new layer applies to facade running
playback, including its limiter cuts.

From the repository root, with fresh output paths:

```sh
mkdir -p artifacts/uaz469-mechanical-reuse
node tools/bake-mechanical-texture.mjs \
  artifacts/uaz469-reference-3m/uaz469.crankwave \
  data/engines/uaz469-reference-3m-mechanical/engine.json \
  data/engines/uaz469-reference-3m-mechanical/mechanical-tuning.json \
  artifacts/uaz469-mechanical-reuse/package \
  artifacts/uaz469-mechanical-reuse/uaz469-reference-3m-mechanical.crankwave
node data/engines/uaz469-reference-3m-mechanical/audition.mjs \
  artifacts/uaz469-reference-3m/uaz469.crankwave \
  artifacts/uaz469-mechanical-reuse/uaz469-reference-3m-mechanical.crankwave \
  artifacts/uaz469-mechanical-reuse/audition
node scripts/serve-crankwave-harness.mjs
```

The attachment tool pins both the reviewed base carrier and authoring file hashes.
It repacks with the official native carrier writer, verifies the result, and
checks every existing payload except the two intentionally updated manifests.
It does not rebake or relabel the source simulation. `PORT.md` and the other copied
source notes describe the original port; use the commands above for this variant.

`audition.mjs` requires ffmpeg. Each comparison uses constant gain to match RMS
levels and 20 ms boundary fades. No compression or cycle leveling is applied.
The script saves raw sweeps, exact controls, measured levels and applied gains.

| Audition | Contents |
| --- | --- |
| `idle-comparison-48k.wav` | Original at 0–5 s; mechanical variant at 5.5–10.5 s |
| `coast-comparison-48k.wav` | Same before/after order, held 2,000 RPM with closed throttle |
| `sweep-comparison-48k.wav` | Original 18 s sweep, 0.5 s gap, then the variant |
| `after-sweep-48k.wav` | Idle 0–4 s, acceleration 4–8 s, hold 8–9 s, closed-throttle deceleration 9–14 s, idle 14–18 s |

The sweep prescribes RPM and load for comparison. It is not a solved UAZ vehicle
trajectory. The user rejected this variant as too diesel-like. It is retained as
an opt-in comparison; the original gasoline carrier remains the baseline.

## Verification and identities

The 146 existing runtime tests and six new adapter tests pass. New tests cover
configuration admission, exact reuse of the shared contact source, partial batch
continuity, deterministic reset, pressure-dependent breath, and alignment with
the facade's streaming latency. The original idle, loaded and coasting WAVs
replay byte-for-byte. All 14 original authoring/carrier hashes are preserved.
Native package verification, browser Worker playback and the installed-runtime
distribution check also pass. The browser test uses an AudioContext test double;
it verifies package loading and Worker DSP rather than physical audio output.

The new raw 18-second sweep peaks at approximately 0.706; the listening copies
have additional headroom. The idle and coasting additions change total RMS by
about 0.5–0.6 dB before the comparison matching. Those numerical checks do not
establish subjective sound quality.

Original carrier:
`d736ea73ff706349e81ea11a1000472e40309e3a1592cf77342dc0ca617dd3b7`.

Mechanical carrier:
`94483da9cdbb4d0cdc25d79c5b5f0bd9c25c17222689ab0a2f3aa7a10a728f8c`.

The separate `artifacts/uaz469-mechanical-reuse/` directory contains the preserved
hash list, packing log, native/browser checks, WAVs and listening report.
