# BMW M52B28 P1.8 reference fixture

This fixture is the trusted handoff between the pinned engine-sim P1.8 behavior and
the clean-room renderer work. It covers the complete 17-second fresh-process history
of `baked.loaded_acceleration`: one second of bootstrap, one second of loaded pre-roll,
and the 15-second audible pull.

The simulation fixture is deliberately split into two lanes:

- `reference-parity.bin` contains physical/control observations, resolved cylinder
  routing, and explicitly named P1.8 parity states/proxies needed to evaluate the
  frozen excitation model.
- `reference-audit.bin` contains redundant P1.8 pre-delay excitation, post-delay
  excitation, and the two exact pre-DSP exhaust buses. It is validation evidence for
  the M2 renderer only; production and M3 simulator targets must not link a reader for
  it.
- `component-seeds.bin` freezes the exact per-component PCG32 initialization used by
  the paired capture.
- `manifest.json` closes provenance, schema, control timing, artifact identity, and
  the byte-equivalence evidence.

The `presentation/` directory closes the other side of the M2 handoff:

- `smooth_39.wav` is the exact configured 44.1 kHz PCM16 impulse-response input.
- `smooth_39-192000hz-volume-0p001-f64le.bin` is the canonical headerless
  little-endian binary64 kernel produced by the frozen P1.8 conversion.
- `stems/` contains all six actively observed 192 kHz Float32 reference stems: dry,
  configured-IR, and selected output for both runtime exhaust routes.

The stored kernel and stems are expected outputs, not renderer shortcuts. The M2
reference test must regenerate the kernel from `smooth_39.wav`, render from the
pre-DSP audit buses, and compare its results with these files.
[`P18_PRESENTATION_RENDERER.md`](P18_PRESENTATION_RENDERER.md) freezes the equations,
state-update order, coefficient policies, random draws, actual block partition,
crop, and serialization needed to do that without consulting an engine-sim tree.

Starter, dyno, ignition, and fuel fields describe warm-up/control prehistory only.
No starter audio source is present in either captured bus.

All binary values are explicitly little-endian. Floating-point fields are IEEE-754
binary64. Records are post-step samples: `sample_index = k` describes the state after
simulation step `k + 1`. The complete `[0, 170000)` record interval must be processed
to warm causal state; only `[20000, 170000)` is audible.

The fixture was emitted by applying
[`p18-reference-audit.patch`](../../tooling/p18-reference-audit.patch) to engine-sim
commit `9617562a7a5615c2bf84c9ec39cd5ae25c560059` and building with
`ENGINE_SIM_REFERENCE_AUDIT_CAPTURE=ON`. The option defaults to `OFF`; no fixture
writer is part of the normal build.

Validate the fixture from the repository root:

```bash
python3 tools/validate_reference_fixture.py \
  reference/fixtures/bmw-m52b28-p18
```

The validator replays the pressure equation, each integer delay FIFO, and both
cylinder-order route accumulations from the parity lane and requires bit equality
with the audit lane. It also independently rederives every component seed, freezes the
canonical presentation identities independently of the mutable manifest, parses their
media contracts, validates the renderer-record identity, and checks the liked master.
For this capture, each selected stem is required to be byte-identical to its
configured-IR stem.

The accepted capture was produced in a temporary checkout of the pinned fork revision
and is fully described by `manifest.json`: source and submodule commits, asset hashes,
compiler and flags, exact capture arguments, deterministic seed derivation, fixture
schema, source-artifact hashes, and the four-way byte-equivalence result. The exact
observation patch is retained for auditability, not as production code. Neither the
legacy source tree nor the failed experimental implementation is an input to subsequent
clean-room rendering.

This is a narrow, exhaust-only parity fixture, not a production-complete sound set. It
contains no intake, mechanical, drivetrain, cabin, environment, or microphone source.
The IR and every derivative that contains it have unresolved distribution rights and
are explicitly `NOASSERTION`; the capsule remains local evaluation evidence and must
not be shipped until rights are cleared.
