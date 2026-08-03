# Post-parity cylinder-lane and collector gate

Status: complete and byte-identical on 2026-08-03.

## Decision

The low-order exhaust source now has an explicit topology:

```text
per-cylinder pressure source
  -> identity-stable primary lane and residual primary delay
  -> authored-order collector fold
  -> one shared downstream route delay
  -> unchanged route reconstruction, conditioning, IR, mix, and publication
```

Commit `de4704e` first separated cylinder-lane production from the collector fold while
retaining the exact written-order arithmetic. Commit `3589070` then factored the old
per-cylinder total delay into a primary residual and a shared post-collector route
delay.

For cylinder `i` on route `r`, the compiler preserves the old integer arrival exactly:

```text
N_total(i)   = round((header(i) + route(r)) * capture_hz / propagation_speed)
N_route(r)   = round(route(r) * capture_hz / propagation_speed)
N_primary(i) = N_total(i) - N_route(r)
```

The residual is derived by subtraction rather than independently rounding the primary.
The focused regression uses a non-additive rounding case where the required residual is
20 samples but independently rounding the primary would produce 21; the complete
collector output still arrives at the preserved total sample 380.

## Exact-output evidence

Both clean Release renders used the canonical BMW M52TUB28 held-dyno pull/lift:

| Checkpoint | Artifact root | Wall time |
|---|---|---:|
| Explicit lane/collector fold | `artifacts/listening/fidelity-lanes-bmw-byte-identical-de4704e/` | `34.77 s` |
| Shared route delay after collector | `artifacts/listening/fidelity-collector-bmw-byte-identical-3589070/` | `35.28 s` |

At both checkpoints all eight WAVs match the accepted 20 kHz control byte for byte. The
final master identities remain:

- raw WAV SHA-256:
  `90737e00d6f56c6893f9afb23957ed1becc1aebe3571423c263210edb306dee7`;
- audition WAV SHA-256:
  `e77a236d2c94fb66b6dc2799e212b6cd0f4f9bd27643568e2f1dd379cb884dcf`;
- decoded signed-24-bit audition PCM SHA-256:
  `cb49e13910360525bbdcb8b004c8c8ad6cb3a8a10a6854782946c1962ff3fe04`.

The focused exhaust test passes in GNU Release and Clang Debug with ASan and UBSan. It
also pins positive-zero startup, two-block delay continuity, canonical cylinder-lane
identity, a three-lane non-associative authored collector order, and the non-additive
primary/route rounding case.

## Acceptance boundary

This is an architectural fidelity gate, not a claim that the sound improved. It removes
the structural need to carry common route propagation independently in every cylinder
lane, while deliberately retaining the accepted source equation, scalar operation
order, route conditioner, static IR, and mono publication.

No independent per-cylinder jitter, noise, speculative pipe filter, or radiation model
was added. A sound-bearing primary transfer still requires measured parameters or a
sourced model with its required geometry. The next isolated post-parity item is a
separately published intake source bus.
