# Mechanical diesel I4

An authored warm, naturally aspirated, four-stroke direct-injection example with
four identical 90 × 100 mm cylinders, 18:1 compression, 1–3–4–2 firing order, and
one main hydraulic injection pulse. All parameters are explicit in `engine.json`.
This is a synthetic diesel sound design, not a measured real-engine calibration.
The authored valve windows overlap around exhaust top dead centre. Both valve
areas remain explicit; they replace the overly restrictive first audition setup.

```bash
mkdir -p artifacts/diesel-sound
build/crankwave bake-crankwave \
  --engine data/engines/mechanical-diesel-i4/engine.json \
  --output artifacts/diesel-sound/mechanical-diesel-i4.crankwave
node scripts/serve-crankwave-harness.mjs
```

Load the carrier in the harness. Manual mode offers RPM and Fueling; Drive mode
uses the harness's vehicle approximation. Native baking takes no external assets.
The resulting package includes generated pressure/flow sources and fixed-frequency
mechanical DSP, with 650–3,400 RPM coverage. Existing output files are not overwritten.

See [the model and playback contract](../../../docs/model/MECHANICAL_DIESEL_SOUND.md)
for equations, authoring, WAV export, provenance, supported routes, and limitations.
