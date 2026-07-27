# P1.8 isolated reference integration

Status: implemented; immediate user-listening hard stop

This checkpoint connects the already frozen P1.8 source stage, static IR conversion,
fixed convolution, mastering, and WAVE encoders without changing their algorithms. It
exists to prove the complete known-good BMW M52B28 exhaust presentation route before
new engine physics or offline-fidelity replacements begin.

## Boundary

`ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS` is off by default. When enabled it builds
one executable whose fixture readers, fixture preflight, render coordinator, digest
comparison, and local publisher are compiled directly into that executable. They are
not a library and are not linked by the public renderer, CLI, or future M3 simulation.

The executable reads only these fixed descendants of a caller-selected fixture root:

- `reference-audit.bin`
- `component-seeds.bin`
- `presentation/smooth_39.wav`

It does not read expected stems, the oracle master, `reference-parity.bin`, or a
manifest as render inputs. The three actual fixture files must pass their frozen
identities before rendering. The regenerated 30,071-coefficient IR and constructed
65,536-bin spectrum are compared afterward as diagnostics: drift makes the candidate
non-exact, but does not prevent a complete candidate from being heard.

## Execution

One session performs all 850 fixed `200 -> 3840` blocks. The first 100 blocks pass
through reconstruction, randomness, conditioning, and both continuous convolution
histories but are not published. The remaining 750 blocks produce 2,880,000 frames
for six Float32 stems, one Float32 raw master, and one PCM24 audition master. There is
no reset at the crop and no convolution tail flush.

The eight outputs are written into one private staging directory. Each encoder callback
is offset checked and incrementally hashed. Publication requires all exact byte counts,
sealed regular files, synchronized contents, and an exact staging inventory, then uses
an atomic no-replace directory rename. Oracle hash mismatch is diagnostic rather than
destructive: a complete candidate is published and clearly labelled for listening.
Incomplete or malformed output is never published.

## Frozen result

On Clang 21.1.8, x86-64 Linux, every complete artifact and every mastering intermediate
matches the frozen reference identity. The audition master is byte-identical to the
preserved liked oracle:

```text
f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb
```

GCC 13.3.0 also produces the same complete identities. This proves only the narrow
trace-driven exhaust presentation baseline. It does not claim new engine physics,
intake or mechanical sources, production completeness, distribution rights, or an
offline-fidelity improvement.

## Measured performance

Measurements on this PC on 2026-07-27, with Release mode, `-ffp-contract=off`, and the
complete eight-file no-overwrite publisher:

| Measurement | Result |
|---|---:|
| CMake configure, clean reference build | 0.20 s |
| Clean Clang reference target build, 3 build jobs | 12.59 s |
| Single Clang preflight | 0.069 s |
| Single Clang DSP plus eight-file write | 3.995 s |
| Single complete process wall time | 4.38 s |
| Single maximum resident memory | 33,108 KiB |
| Three concurrent complete process wall time | 4.49 s |
| Concurrent per-clip process wall time | 4.28–4.30 s |
| Concurrent throughput | 0.668 clips/s |
| GCC complete DSP plus write | 14.57 s |

All three concurrent outputs also passed every exact identity. Temporary benchmark
copies were removed after verification. Both the single and concurrent Clang results
are comfortably inside the approximately 30-second clip target; there was no measured
concurrency collapse at three jobs.

## Listening rule

The main audition file is `audio/master.reference.audition.wav`; the raw coherent sum
and selected route stems are published beside it. Work stops after publishing the
controlled set. Only the user's listening decision records acceptance or rejection and
permits the next checkpoint.
