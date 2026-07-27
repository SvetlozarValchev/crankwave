# P1.8 isolated reference integration

Status: implemented and user-accepted on 2026-07-27

This checkpoint connects the already frozen P1.8 source stage, static IR conversion,
fixed convolution, mastering, and WAVE encoders without changing their algorithms. It
exists to prove the complete known-good BMW M52B28 exhaust presentation route before
new engine physics or offline-fidelity replacements begin.

## Boundary

`ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS` is off by default. When enabled it builds
one executable whose fixture readers, fixture preflight, render coordinator, digest
comparison, and local publisher remain private to the reference target. Its immutable
catalog is a private support library built only for tests or this opt-in tool; none of
these components are linked by the public renderer, CLI, or future M3 simulation.
A separate private renderer-determinism target records build-owned source, compiler,
and target facts and rejects dirty or unavailable source. It also observes the actual
loaded libstdc++, glibc libm, and libgcc_s providers without loading a missing library.
It now owns the compiled numeric policy and admits the calling thread's CPU and
floating-point state. These split checkpoints are exercised by focused tests only;
they are not yet one complete runtime determinism identity and are not yet consumed by
the reference executable.

## Loaded runtime admission

The supported provider envelope is Linux LP64 ELF64 little-endian x86-64 with the
expected `libstdc++.so.6`, `libm.so.6`, and `libgcc_s.so.1` SONAMEs. Admission binds
each provider to its in-memory GNU build ID, stable mapped inode, whole-file SHA-256,
file size, and the provider-relative address of every selected versioned symbol. The
identity excludes loader paths, basenames, absolute addresses, device/inode values,
and timestamps, so ASLR and moving identical provider files do not change content
identity.

The exact selected symbols are `__cxa_throw@CXXABI_1.3`,
`__muldc3@GCC_4.0.0`, and `ceil`, `cos`, `floor`, `roundl`, `sin`, `sincos`, and
`tan` at `GLIBC_2.2.5`. Default, exact-version, and provider-handle lookups must agree,
and each address must lie in the expected provider's executable load segment. Every
non-writable loaded segment is compared with the stable opened file, while W+X or text
relocations are rejected. Tests prove live admission, preloaded-math interposition
rejection, and rejection when libstdc++/libgcc are statically linked. The incremental
SHA-256 implementation now belongs to the contract foundation rather than being
duplicated or making determinism depend on the artifact layer.

Provider files must remain quiescent during admission. Atomic replacement, unlink,
symlink/inode mismatch, and a stable in-place byte mismatch fail closed. Hostile
concurrent truncation of an already mapped system DSO is outside this in-process
observer's scope; guaranteeing that case would require isolating the entire dynamic
loader interaction in another process.

## Numeric environment admission

The canonical numeric build policy is
`x86-64-v1-binary64-x87-extended-strict-v1`. CMake owns this exact private option
tail:

```text
-march=x86-64 -mtune=generic -mfpmath=sse -mno-avx -mno-avx2 -mno-fma
-fno-lto -fexcess-precision=standard -fno-fast-math -ffp-contract=off
```

It is applied without propagation to every first-party target in the isolated P1.8
output closure and to the public render implementation being prepared for M3. IPO is
disabled on each covered target and the final reference link receives `-fno-lto`.
An embedding consumer's translation units do not inherit this ISA policy.

Configure-time assertions enumerate the complete first-party closure, require the
single exact option group, require generic and per-configuration target IPO to be
disabled, and require the final no-LTO link option. Focused probes also compile the
generated-zero branch and prove that the private attestation include path does not
reach a public consumer.

Only a GNU-front-end GCC or Clang build for Linux x86-64 SysV LP64 receives the
policy. The same CMake module both attaches its options and generates a private
attestation header. That header overrides any same-named caller macro, records only
zero or one, and is compile-time checked against the public policy text. The tracked
module and template are part of the renderer source digest. Unsupported builds remain
buildable but are unmarked and cannot pass admission.

Admission is read-only and local to the calling thread. Before any renderer identity
can use it, the observer requires enabled CPUID and the x86-64-v1 feature baseline;
IEEE-754 binary32 and binary64; the SysV 80-bit extended `long double` format;
`FLT_EVAL_METHOD == 0`; round-to-nearest/ties-to-even; masked MXCSR exceptions with
FTZ, DAZ, and AMD misalignment-mask mode disabled; and masked x87 exceptions with
extended precision and nearest rounding. Sticky exception flags and extra CPU
capabilities do not change identity. Observation and every rejection preserve the
floating-point registers, CPUID setting, MXCSR plus x87 sticky status, and `errno`.
The raw snapshot retains the complete x87 status word for before/after mutation
detection, but validation deliberately excludes its sticky exception bits from the
canonical numeric identity.

This marker attests the numeric option tail, not source cleanliness or provider
identity. The next checkpoint must require the source stamp, loaded-runtime identity,
and numeric admission together on the render thread; none can substitute for another.

Preflight opens, bounds, streams, and hashes exactly these fixed descendants of a
caller-selected fixture root:

- `manifest.json`
- `reference-parity.bin`
- `reference-audit.bin`
- `component-seeds.bin`
- `P18_PRESENTATION_RENDERER.md`
- `presentation/smooth_39.wav`
- `presentation/smooth_39-192000hz-volume-0p001-f64le.bin`

Only the audit, component seeds, and configured IR are retained and decoded as render
inputs. The manifest, parity evidence, algorithm record, and kernel comparator are
verified lineage only; their bytes are never supplied to DSP. Expected stems and the
oracle master remain unopened output comparators. The regenerated 30,071-coefficient
IR must equal the independently observed kernel-comparator identity, and the
constructed 65,536-bin spectrum remains a diagnostic comparator.

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
| Seven-file lineage preflight, GCC catalog checkpoint | 0.171 s |

All three concurrent outputs also passed every exact identity. Temporary benchmark
copies were removed after verification. Both the single and concurrent Clang results
are comfortably inside the approximately 30-second clip target; there was no measured
concurrency collapse at three jobs.

A like-for-like full-render preflight with the complete numeric option tail retained
all eight exact identities under both compilers. GCC wall time was 20.49 s versus
20.29 s without the tail (about 1%); Clang was 4.81 s versus 4.58 s (about 5%). Both
remain below the 30-second target, and the audition WAVE identity did not change.

## Listening rule

The main audition file is `audio/master.reference.audition.wav`; the raw coherent sum
and selected route stems are published beside it. Work stops after publishing the
controlled set. Only the user's listening decision records acceptance or rejection and
permits the next checkpoint.

The user accepted the byte-identical candidate rendered from commit
`9cc0cd8f1129b14de157082ad6e66407b548041c`. Acceptance is limited to this downstream
presentation route and does not claim engine-physics parity or higher fidelity.
