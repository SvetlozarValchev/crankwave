# P1.8 isolated reference integration

Status: audio route user-accepted on 2026-07-27; bounded transactional reference
integration implemented

This checkpoint connects the already frozen P1.8 source stage, static IR conversion,
fixed convolution, mastering, and WAVE encoders without changing their algorithms. It
exists to prove the complete known-good BMW M52B28 exhaust presentation route before
new engine physics or offline-fidelity replacements begin.

## Boundary

`CRANKWAVE_BUILD_REFERENCE_TOOLS` is off by default. When enabled it builds
one executable. Fixture readers, fixture preflight, the audit-to-excitation adapter,
the immutable comparator catalog, and reference-manifest construction remain private
to that opt-in integration. The stateful presentation session is a separate
fixture-free target: it accepts only typed `ExhaustExcitationBlockView` blocks,
presentation seeds, an immutable IR kernel, and a generic `RenderSink`. It neither
includes nor links the fixture decoder and is the accepted acoustic boundary that M3
physics will drive. The public renderer and CLI link neither reference path while
public `render()` remains fail-closed.
A separate private renderer-determinism target records build-owned source, compiler,
and target facts and rejects dirty or unavailable source. It also observes the actual
loaded libstdc++, glibc libm, and libgcc_s providers without loading a missing library.
It owns the compiled numeric policy and admits the calling thread's CPU and
floating-point state. One sealed private envelope now requires all three identities
together and exposes their exact reference-manifest projection. This envelope is
exercised by focused tests and consumed only by the isolated reference executable; it
remains outside the public renderer and CLI.

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
identity. The complete private envelope below requires all three on the render thread;
none can substitute for another.

## Complete renderer identity

The production identity entry point accepts no arguments and has no mutable cache.
It first observes and admits the calling thread's numeric environment, then obtains
the build-owned source stamp, then observes the already-loaded runtime providers.
A source failure skips runtime observation because no successful envelope remains
possible. Numeric preflight failure skips both later observers.

After every attempted source/runtime sequence, a second raw numeric observation is the
final observer operation. Exact before/after comparison includes CPU-query evidence,
MXCSR, x87 control, and the complete x87 status word. Any difference returns the two
typed snapshots and outranks a simultaneous source or runtime error because that error
was observed under an unstable transaction. Stable failures retain the source,
runtime-provider, or numeric observer's original typed error. The composition restores
the caller's `errno`; it does not alter or normalize floating-point or CPUID state.

Only the private composer can construct the successful immutable value. It retains
the complete source stamp, rich loaded-provider evidence, and canonical numeric
identity, plus one exact `contract::DeterminismEnvelope` projection. That projection
copies the Git object ID, renderer-source closure digest, compiler and target facts,
all three canonical provider identities, numeric-policy and ISA IDs, and strict
floating-point facts. Execution identity is fixed to one worker and
`serial-stable-order`. Focused tests prove the production API has zero parameters,
the exact observer order and error precedence, sticky-status mutation detection,
caller `errno` preservation, and rejection of a worker-local rounding change without
changing the main thread.

The pure scripted composer retained for focused observer-order tests marks every
successful envelope as non-production. Only the zero-argument live entry point can
mark an envelope as a production observation, and deterministic manifest-content
construction rejects the scripted form. Thus synthetic source/runtime/numeric
observers can exercise error semantics without becoming an alternate publication
identity path.

## Reference-tool identity integration

After validating only the argument count, the isolated reference executable obtains
the zero-argument renderer identity before it parses a fixture path, creates a
publication root, or performs DSP. Numeric, source, provider, or state-change
rejection therefore fails without creating an output tree. The executable link
retains its selected libm provider explicitly so provider admission cannot depend on
incidental linker reachability.

The command accepts exactly the fixture root, publication root, and publication name.
It has no source-revision argument or alternate publisher. The sealed identity's
exact projection—full Git object ID, renderer source-closure digest, compiler and
target facts, all three runtime-provider identities, numeric-policy and ISA IDs,
floating-point facts, and serial execution identity—is written into the canonical
manifest rather than reconstructed as report text. This same build-owned value is the
only renderer identity accepted by deterministic reference-manifest content
construction.

## Method and provenance identity

The private reference support target now pins the 11 method configuration identities
represented by the reference manifest: audit reader, audit adapter, excitation seam,
PCG32 generator, recorded seed derivation, reconstruction, conditioning, configured-IR
conversion, convolution, Float32 stem publication, and audition mastering. Each
identity retains canonical descriptor bytes, a separately pinned content digest, and
the catalog-owned stable ID/version. Focused tests recompute every descriptor digest
and reject drift or duplication. The seed derivation describes the origin of the
verified seed inventory; replay reads that inventory and does not falsely claim to
derive it again.

A separate sealed provenance value can be constructed only from
`P18VerifiedReferenceLineage`. It copies all seven observed file digests into
repository-relative, local-evaluation-only evidence records, builds three cited
fixture claims, and owns exactly the 28 authored resolutions required by the executed
presentation calibration. Catalog expectations remain validation comparators and
cannot be passed to the factory as observations.

The provenance bundle digest canonically covers the complete ledger except its own
digest field. Its versioned binary grammar uses explicit little-endian lengths and
integers, presence tags, raw hashes, enum tags, vector order, and normalized signed
zero for the only optional binary64 field. The real BMW fixture produces pinned bundle
digest
`76dfb503bc1852f1a1d11f739c612d11e4ad4c05ee0ab5c5eda873a35e04f60d`.
The method/provenance checkpoint itself constructs no render manifest and publishes
no file.
At the method/provenance checkpoint, the matrix passed 33/33 tests under GCC 13.3
Release, 33/33 under Clang 21.1.8 Release, and 31/31 under Clang ASan/UBSan.

## Deterministic reference-manifest content

The private reference path now has a distinct deterministic-content boundary after
the eight audio artifacts have been sealed. Its sole factory is:

```cpp
[[nodiscard]] P18ReferenceManifestContent
make_p18_reference_manifest_content(
    const P18LoadedReferenceFixture &,
    const P18SealedPresentationEvidence &,
    const determinism::RendererDeterminismEnvelope &);
```

Those three evidence-bearing inputs supply only:

- the verified loaded fixture, including its independently observed seven-file
  lineage and decoded component-seed inventory;
- the build-owned `RendererDeterminismEnvelope`; and
- the private-construction `P18SealedPresentationEvidence`, which owns all eight
  actual whole-file records only after the session streamed and sink-sealed them.

The factory constructs `P18ReferenceProvenance` from the fixture lineage itself.
Successful construction returns a sealed `P18ReferenceManifestContent` that owns both
that provenance and the validated `contract::RenderManifestContent`, retaining the
exact ledger used for validation so production session code can consume both
together. Its const accessors return copyable contract values; the meaningful
invariant is that an arbitrary copied pair cannot be constructed as a successfully
validated `P18ReferenceManifestContent`.

Expected lineage and audio records in `P18ReferenceCatalogV1` remain comparators and
authored-policy records. They are not accepted as observations. The content boundary
also rejects a caller-created vector of `ArtifactRecord`; possessing that transport
shape does not prove that one session wrote and sealed the corresponding files. The
factory compares every evidence record's role, path, media contract, diagnostic flag,
size, and digest with the source matrix and frozen comparator and copies the actual
record only when it matches. Expected hashes are never substituted for observations.

The fixture identity copies the observed byte count and complete-file digest of each
of the seven lineage files. The executed randomness copies only the four decoded
presentation streams in canonical air-noise semantic routes 0/1 (route IDs 1/2) then
jitter semantic routes 0/1 (route IDs 1/2) order. All 11 represented methods use the
pinned content-derived identities. Presentation algorithm/IR leaves cite the sealed
provenance resolutions and use the observed algorithm-record and configured-IR
identities. The frozen source matrix supplies the output policy, route/bus ownership,
media contracts, diagnostics, and omissions, while each artifact's role, relative
path, byte count, and payload digest come from its actual sealed record. The
determinism member is copied exactly from the sealed renderer envelope rather than
reconstructed from report text or caller values. The envelope must also carry the
production-observation mark that only the zero-argument live observer can set; the
scripted test composer is rejected.

The resulting schema-version-2 `RenderManifestContent` must validate against the
sealed provenance ledger and the exact BMW reference source matrix before it can be
retained. Identical sealed inputs produce identical deterministic content. A missing
or unsealed artifact, invalid executed seed inventory, substituted comparator
identity, or cross-record validation failure prevents successful construction.

The deterministic-content boundary deliberately stops before run-specific work. It
does not observe
`ExecutionFacts`, complete a `RenderManifest`, call the canonical JSON encoder, write
the manifest or sidecar, or commit the output transaction. Observation, completion,
encoding, and publication are owned by the session/integration boundary below. The
fixture adapter and reference input alternative remain absent from public renderer,
CLI, and M3 physics dependency graphs, so replay cannot impersonate a successful
physics render.

## Execution facts and complete in-memory manifest

Execution observation is implemented by a generic internal Linux component, not by
the P1.8 fixture reader. Its zero-argument production entry owns one render job and
returns a move-only observation. Finish consumes that observation and seals
`ObservedExecutionFacts`; its private constructor prevents caller-created facts and
pure observer-test values from acquiring the completion type.

The observation interval begins after output preflight, sink begin, and all eight
declarations, immediately before the first WAVE header is written. It ends only after
all eight audio artifacts have been sealed. The observer uses kernel
randomness for `render-run-<128-bit hex>`, `CLOCK_REALTIME` for the fixed-nanosecond
UTC start, `CLOCK_BOOTTIME` for positive elapsed wall time, bounded `uname` and CPUID
identity after an `ARCH_GET_CPUID` enabled-state check, `_SC_NPROCESSORS_ONLN`, a
bounded `/proc/self/status` thread snapshot, and checked Linux `ru_maxrss` conversion.
It preserves caller `errno`, fails closed on required observation errors, and never
accepts a caller host, clock, topology, job count, or run ID.

`complete_p18_reference_manifest()` accepts only the previously established
`P18ReferenceManifestContent` and production-observed facts. It creates a complete
typed `RenderManifest`, validates it against the exact retained provenance ledger and
BMW source matrix, then invokes the already-frozen reference-v2 canonical encoder.
The returned `P18CompletedReferenceManifest` owns the provenance, typed manifest, and
canonical bytes as one in-memory value.

The owning session then requires the complete manifest's artifact and execution
observations to equal its own sealed evidence and revalidates the whole manifest at
the commit boundary. `DirectoryRenderSink` writes the retained canonical manifest and
its SHA-256 sidecar beside the eight WAVs, verifies the exact ten-file staged tree,
and performs one atomic no-replace publication. There are no diagnostic report files
and no second transaction implementation. Public `render()` continues to reject the
otherwise valid request before touching its sink.

A clean Clang 21.1.8 Release build of transactional implementation commit
`5cad9daca18a9f1805eab4f18343187e8f6dd185` embedded source-closure digest
`bfda2c8390e5d8059948205f1d3ee195d4c4b3f997e5db5f1dab781b7539f03a`
and passed the complete 35-test matrix. Its exact transaction reproduced all eight
expected audio hashes; the audition WAVE remained byte-identical to the liked oracle
at `f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb`.
The live execution interval was 4.005 seconds, the complete command took 4.413
seconds, the canonical manifest was 20,269 bytes, and maximum resident memory was
36,068 KiB.

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

The fixture-only adapter maps each already-decoded 200-frame audit block, unscaled and
in route order, into the typed excitation seam. The session target itself never reads
or links the fixture. Each encoder callback is offset checked and independently
hashed by both the session evidence and sink. Cancellation is observed only before
begin, between complete blocks, and once before finalization.

The eight outputs, canonical manifest, and sidecar are written into one private
staging directory. Publication requires exact accepted artifact hashes, all
source-matrix routing and bus ownership, live execution facts, sealed regular files,
synchronized contents, and the exact ten-file inventory, then uses an atomic
no-replace directory rename. Because this checkpoint wraps the already accepted
renderer, any oracle mismatch fails closed and publishes nothing. Incomplete,
malformed, cancelled, or mismatched output is never published.

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

Earlier accepted-renderer measurements on this PC on 2026-07-27 used Release mode,
`-ffp-contract=off`, and the predecessor eight-WAV staging path:

| Measurement | Result |
|---|---:|
| Transactional Clang DSP, eight WAV writes, and seals | 4.015 s |
| Transactional Clang complete command | 4.413 s |
| Transactional manifest bytes / published files | 20,269 / 10 |
| Transactional maximum resident memory | 36,068 KiB |
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
and selected route stems are published beside it. The user already accepted this
byte-identical acoustic route. The next mandatory listening stop occurs only after M3
replaces the trace adapter with clean-slate physics while keeping this presentation
boundary unchanged.

The user accepted the byte-identical candidate rendered from commit
`9cc0cd8f1129b14de157082ad6e66407b548041c`. Acceptance is limited to this downstream
presentation route and does not claim engine-physics parity or higher fidelity.
