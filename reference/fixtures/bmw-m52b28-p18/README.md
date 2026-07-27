# BMW M52B28 P1.8 reference fixture

This fixture is the trusted handoff between the pinned engine-sim P1.8 behavior and
the clean-room renderer work. It covers the complete 17-second fresh-process history
of `baked.loaded_acceleration`: one second of bootstrap, one second of loaded pre-roll,
and the 15-second audible pull.

It is deliberately split into two lanes:

- `reference-parity.bin` contains the clean physical/control observations and resolved
  cylinder routing needed to evaluate a replacement excitation model.
- `reference-audit.bin` contains redundant P1.8 pre-delay excitation, post-delay
  excitation, and the two exact pre-DSP exhaust buses. It is validation evidence for
  the M2 renderer only; production and M3 simulator targets must not link a reader for
  it.
- `component-seeds.bin` freezes the exact per-component PCG32 initialization used by
  the paired capture.
- `manifest.json` closes provenance, schema, control timing, artifact identity, and
  the byte-equivalence evidence.

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
with the audit lane. It also independently rederives every component seed and
automatically checks all three file hashes from `manifest.json`.

The accepted capture was produced in a temporary checkout of the pinned fork revision
and is fully described by `manifest.json`: source and submodule commits, asset hashes,
compiler and flags, exact capture arguments, deterministic seed derivation, fixture
schema, source-artifact hashes, and the four-way byte-equivalence result. The exact
observation patch is retained for auditability, not as production code. Neither the
legacy source tree nor the failed experimental implementation is an input to subsequent
clean-room rendering.

This is a narrow, exhaust-only parity fixture, not a production-complete sound set. It
contains no intake, mechanical, drivetrain, cabin, environment, or microphone source.
The linked listening oracle includes an impulse response with unresolved distribution
rights, so the capsule remains local evaluation evidence until rights are cleared.
