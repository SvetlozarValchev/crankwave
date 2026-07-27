# BMW M52B28 reference source-completeness matrix

Status: **frozen v1**

Decision owner: user

Approved: 2026-07-27 by explicit user confirmation in the project thread

Scope: M2 fixture-renderer acceptance and M3 BMW parity only

This matrix defines exactly what must be present to reproduce and evaluate the liked
engine-sim exhaust route. It does not define production-complete engine audio. Changing
this frozen matrix requires another explicit user approval.

## Boundaries and isolation lanes

The reference path deliberately uses two fixture lanes:

```text
CaptureBlock.reference_parity ──> frozen parity excitation ──┐
                                                             ├─> compare two buses
ReferenceAuditBlock (validation only) ───────────────────────┘

ReferenceAuditBlock buses ──> M2 fixture adapter ──> accepted renderer ──> stems/master
clean M3 CaptureBlock ──> accepted excitation ─────> accepted renderer ──> stems/master
```

1. `CaptureBlock.reference_parity` contains timestamped per-cylinder observables and
   resolved route metadata before excitation, source conditioning, resampling, IR, or
   mixing.
2. `ReferenceAuditBlock` contains redundant P1.8 excitation and two-bus outputs. It is
   validation evidence, not a simulator output contract.
3. `ExhaustExcitationBlock` is the common two-bus renderer input. The M2-only fixture
   adapter may construct it from an audit trace. In M3, only the already-tested
   physical-term-to-excitation path may construct it.
4. Reference stems contain the two audio-rate exhaust routes after declared
   presentation processing.
5. The audition master is a deterministic listening mix of those stems. It is not a new
   sound source.

The audit adapter and `ReferenceAuditBlock` reader must live in a reference/test target
that the M3 simulator and production renderer entry point do not link. M3 acceptance
must be rendered from newly simulated physical terms; replaying either captured audit
bus is forbidden.

The `legacy_reference.*` fields, `exhaust.reference.*` stems, 192 kHz delivery
requirement, oracle IR, and reference master are confined to this evaluation capsule.
No production package, adapter, or runtime may depend on them.

## Required physical/reference-parity lane

| Item | Cardinality | Requirement and role |
|---|---:|---|
| `sample_index` | 1 per record | Monotonic, gap-free record identity. |
| `time_s`, `dt_s` | 1 per record | Explicit simulation time and step with a declared before/after-step timestamp convention; no inferred wall-clock timing. |
| `engine_speed_rpm` | 1 per record | Physical engine-speed output and prescribed-sweep audit. |
| `reference_parity.filtered_engine_speed_rpm` | 1 per record | Exact stateful P1.8 filtered value used by the low-speed attenuation. It must not be approximated from the physical RPM trace. |
| `crank_angle_rad` | 1 per record | Wrapped convention and zero reference declared; used for phase/event auditing even though the legacy excitation equation does not consume it. |
| operating state | 1 per record or lossless event stream | Requested throttle, ignition enabled, and fuel enabled. Constant values may be represented by manifest intervals. |
| cylinder identity | 6 static records | Stable cylinder ID, firing identity/order, and target exhaust-route ID. |
| `exhaust_primary.static_pressure_pa_abs` | 6 per record | Physical absolute static pressure at each cylinder's exhaust runner/primary control volume. |
| `reference_parity.dynamic_pressure_forward_pa` | 6 per record | P1.8 directional, non-negative dynamic-pressure term for the positive runner direction. |
| `reference_parity.dynamic_pressure_reverse_pa` | 6 per record | P1.8 directional, non-negative dynamic-pressure term for the reverse runner direction. |
| `reference_parity.reference_atmosphere_pa` | 1 static value | Exactly 101325 Pa for P1.8 gauge-pressure derivation; distinct from the scenario's physical ambient input. |
| route geometry | 6 static records | Exact source-field identities and resolved values for `CylinderHead::headerPrimaryLength`, `ExhaustSystem::length`, their sum, and the 343 m/s legacy propagation speed. |
| delay contract | 6 static records | Exact resolved integer sample delay, 10 kHz rate, nearest-integer rounding, zero initialization, continuous FIFO state, and write-then-read behavior. |
| legacy gains | 6 static records plus 2 route records | Resolved cylinder sound attenuation, route audio volume, cylinder-count divisor, inverse-square exponent, and all empirical constants. |
| arithmetic contract | 1 static record | Cylinder accumulation order 0 through 5, route order, binary64 grouping/rounding environment, and subnormal policy required for an exact audit comparison. |
| determinism metadata | 1 per fixture | Source revision, resolved engine/scenario hashes, seed and component seed derivation, all rates, record count, endianness/encoding, and data hashes. |

For this BMW, `CylinderHead::headerPrimaryLength` resolves to zero. The asset's
20-inch `ExhaustSystem::primary_tube_length` participates in gas-flow plumbing but is
not used by the P1.8 audio delay. The other delay term is `ExhaustSystem::length`,
derived separately from exhaust volume and collector cross-section. The fixture stores
the source identities so those two quantities cannot be accidentally conflated.

The filtered-speed audit/input state is initialized to zero and updated in the exact
P1.8 call order on every physics step:

```text
alpha = dt_s / (100 + dt_s)
filtered_rpm =
    alpha * previous_filtered_rpm + (1 - alpha) * engine_speed_rpm
```

The fixture records the resulting value so neither M2 nor M3 can substitute raw RPM or
a conventional smoothing filter.

## Required validation-only lane

| Item | Cardinality | Requirement and role |
|---|---:|---|
| `legacy_reference.excitation_pre_delay` | 6 per record | Derived audit value before the cylinder path delay. It is redundant by design. |
| `legacy_reference.excitation_post_delay` | 6 per record | Derived audit value after the exact delay and before route gain/accumulation. |
| `legacy_reference.exhaust_bus_pre_dsp` | 2 per record | Staged bus values immediately before the synthesizer. These are authoritative only for isolating the M2 presentation renderer. |
| audit identity | 1 per fixture | Observation-patch revision/hash, emitting build identity, field schema, and independent hashes for each lane. |

The fixture must store SI values, not merely the `.mr` unit-system numbers. Gauge
pressure is derived as:

```text
gauge_pressure_pa =
    exhaust_primary.static_pressure_pa_abs - reference_atmosphere_pa
```

The reference path does not consume signed mass flow, temperature, or port area.
Those fields must not be fabricated to make this fixture look more physical. The
clean-slate simulator and later source models may add genuine physical observables; the
production matrix governs the claims made from them.

## Frozen-route equation to reproduce

For cylinder `i`, the P1.8 reference route computes:

```text
a = min(abs(reference_parity.filtered_engine_speed_rpm), 40) / 40

x_i = a^3 * 1600
    * (gauge_pressure_pa
       + 0.1 * dynamic_pressure_forward_pa
       + 0.1 * dynamic_pressure_reverse_pa)

d_i = delay(x_i, total_exhaust_length_m / 343_m_per_s, 10_000_Hz)

bus[route(i)] += cylinder_sound_attenuation
    * route_audio_volume
    * d_i
    / 6
    / total_exhaust_length_m^2
```

The pressure terms are physical-model observables. The `1600`, `1.0/0.1/0.1` mixture,
40 RPM attenuation threshold, authored sound attenuation/audio volume, and
inverse-length-squared gain form an empirical engine-sim excitation mapping. The result
has an engine-sim source unit, not calibrated pascals at a microphone. We reproduce it
to isolate parity; we do not relabel it as a higher-fidelity acoustic model.

For the preserved BMW asset:

| Reference route | Cylinder identities | Authored audio volume | Configured IR |
|---|---|---:|---|
| exhaust 0 | 2, 4, 6 | 0.5 | `smooth_39.wav`, volume 0.001 |
| exhaust 1 | 1, 3, 5 | 1.0 | `smooth_39.wav`, volume 0.001 |

Resolved values from the fixture metadata are authoritative if a name-to-index
disagreement is discovered.

## Closed P1.8 presentation recipe

The renderer is not considered reproduced by implementing stages with similar names.
The fixture manifest and `MODEL.md` must close every state, coefficient policy, and
resolved value below:

| Stage | Frozen reference contract |
|---|---|
| Initialization and warm state | Fresh process and zero-initialized DSP at simulation step 0. Process the full one-second bootstrap and one-second pre-roll through delay, resampler, jitter/RNG, DC, derivative, air-noise filter, and convolution state. Keep output only from the half-open simulation-step interval `[20,000, 170,000)`. No stage resets at the audible boundary. |
| Physics-to-source conversion | 10 kHz to 192 kHz, binary64, causal 257-tap/4,096-phase Kaiser-windowed-sinc polyphase FIR, beta 12, cutoff 0.95 of physics Nyquist, per-phase unity DC, linear phase interpolation, 128-physics-sample uncompensated group delay, continuous zero-initialized history. Exact method identity: `kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_cutoff0p95_source_nyquist_unity_dc_binary64_v2`. |
| Jitter | Exact P1.8 `JitterFilter` algorithm at 192 kHz; amount 0.5, noise cutoff 10 kHz, stochastic scaling relative to 96 kHz, and fractional-delay range 0–40 source frames. Record the resolved per-route PCG32 initial state/stream after capture/component-domain derivation from public seed `0xC0FFEE`. |
| DC and derivative | Exact P1.8 recurrences in binary64; DC time constant `1 / (20*pi)` seconds, initial DC and derivative states zero at process start, and derivative mix represented by the resolved Float32 value `0.0099999997764825821`. State remains continuous across blocks and the audible boundary. |
| Multiplicative air noise | Exact P1.8 `pcg32_xsh_rr` draw and Butterworth-low-pass implementation; amount 1.0, cutoff 2 kHz, amplitude scaling relative to 96 kHz, resolved per-route state/stream recorded, binary64 processing, then exact P1.8 subnormal cleanup. |
| IR preparation | Both routes use `smooth_39.wav`, SHA-256 `75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc`, authored volume 0.001. Apply 6,907 of 33,705 source samples and convert 44.1 kHz to 30,071 samples at 192 kHz using `blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_binary64_v3`; recorded rate-conversion gain is 0.2296875. Preserve the exact resulting binary64 kernel hash. |
| IR convolution and selection | Exact causal overlap-save radix-2 DIT FFT binary64 topology, zero-initialized continuous history, no flush at block boundaries, no algorithmic latency, maximum block 9,600 frames, and convolution/wet amount 1.0. Exact method identity: `causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1`. |
| Delivery | Acoustic and delivery clocks both 192 kHz, so post-propagation decimator stage count is zero. Convert the three semantic variants to Float32 only after propagation. |
| Source calibration | Multiply every dry, configured-IR, and selected wet/dry variant by exactly `2^-26` after variant generation and before headroom checks/serialization. No normalization, AGC, limiter, or saturation. |
| Capture selection | Preserve exactly 2,880,000 frames for the 15-second interval; no phase alignment, looping, stitching, or post-capture resampling. Record all latency and crop conventions. |

An exact algorithm/configuration record may reference the pinned P1.8 source symbols
and independently verified coefficients rather than duplicating implementation text in
this matrix. It must still serialize the effective configuration and state identities
needed to reproduce the route without reading an external working tree.

## Required reference presentation outputs

| Output | Required content |
|---|---|
| `exhaust.reference.0.dry` | Exhaust route 0's dry variant after fixed source calibration; it bypasses the IR. Diagnostic stem; Float32 or more precise. |
| `exhaust.reference.1.dry` | Same boundary for exhaust route 1. |
| `exhaust.reference.0.configured_ir` | Route 0 after exact configured-IR filtering and fixed source calibration, before wet/dry selection. |
| `exhaust.reference.1.configured_ir` | Same boundary for route 1. |
| `exhaust.reference.0.selected` | Route 0's selected configured-IR/wet-dry variant after fixed source calibration. For the liked capture this is 100% configured IR. |
| `exhaust.reference.1.selected` | Same boundary for route 1. |
| `master.reference.raw` | Phase-coherent sum of the two selected stems before listening gain and edge fades. |
| `master.reference.audition` | `master.reference.raw`, fixed ×128 gain, 20 ms quarter-sine entrance/exit fades, mono PCM24 at 192 kHz. |

The declared P1.8 source-conditioning order is causal 10 kHz-to-192 kHz conversion,
fractional-delay jitter, DC removal, derivative mixture, multiplicative low-pass air
noise, and subnormal cleanup. Configured-IR/wet-dry generation follows. Fixed `2^-26`
source calibration is applied after variant generation and before serialization and
headroom checks.

Every evaluation set also provides separately labelled, level-matched A/B copies when
useful. A level-matched copy is never substituted for a raw stem or the reproducible
audition master.

## Explicit omissions

| Route or behavior | Reference status |
|---|---|
| Intake | Absent. No intake source may be inferred from the master. |
| Mechanical engine | Absent as a separate route. |
| Starter | Absent from this already-running pull. |
| Transmission/drivetrain | Absent; the transmission was neutral. |
| Tire/road/vehicle | Absent. |
| Cabin, environment, microphone placement, spatial field | Absent. The static IR is coloration, not a documented physical scene. |
| Startup, shutdown, idle, overrun, fuel cut, limiter | Not exercised by this scenario. |

Passing M2 or M3 therefore means “audibly comparable to this narrow exhaust-only
oracle,” not “production complete” and not “higher fidelity.”

## Fixture and acceptance rules

- The trace must be generated from the pinned P1.8 source content with an audited,
  observation-only capture patch, or another user-approved, fully identified reference
  build. The capture patch and build are hashed, and the uninstrumented versus
  instrumented audio outputs must be compared to show that observation did not alter
  the signal path.
- The fixture normally begins at fresh-process simulation step zero and carries the
  complete two-second bootstrap/pre-roll prehistory. A capture-boundary state snapshot
  is an allowed fallback only if it serializes every delay, resampler, RNG, jitter,
  DC/derivative, noise-filter, convolution, clock, and counter state and is independently
  shown equivalent. Zero-initializing at audible-frame zero is forbidden.
- The fixture must include the physical terms and the redundant
  `legacy_reference.exhaust_bus_pre_dsp` values. A post-conditioned dry stem is not a
  substitute for this boundary.
- Re-evaluating the documented equation and routing from the captured terms must match
  the captured audit buses bit-for-bit where the same arithmetic is used; any
  unavoidable floating-point difference is reported in ULPs and explained.
- On a newly captured reference run, the clean renderer and reference renderer are
  compared numerically at every available stem boundary.
- M3 must build its excitation buses from its newly simulated
  `CaptureBlock.reference_parity` terms through the already-validated equation. The M2
  audit-bus fixture adapter must be absent from that target and execution manifest.
- The original exact generator binary and its exact pre-DSP trace were not preserved.
  Therefore a reconstructed run is not assumed byte-identical to the preserved oracle.
  Its relation to the oracle is reported, and only the user's controlled listening
  accepts the M2 renderer and later M3 parity.
- A synthetic tone or noise can test individual DSP invariants, but cannot satisfy the
  BMW fixture, renderer, or listening gate.
- The unresolved `smooth_39.wav` rights make this route local evaluation evidence only.
  It cannot become a shippable production dependency without resolved rights.

## Approval requested

Freezing this proposal means approving:

1. the physical/reference-parity lane plus the mechanically isolated, redundant
   `legacy_reference.*` validation lane;
2. exactly two exhaust reference routes and the documented audition master;
3. the exact warm-state and P1.8 DSP recipe as the M2 renderer target;
4. the explicit absence of intake, mechanical, drivetrain, and other production routes
   from parity claims.
