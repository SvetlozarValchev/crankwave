# M4 BMW exhaust topology correction

Status: **frozen for implementation; listening acceptance pending**

Decision owner: user

Frozen: 2026-07-29 by explicit user approval in the project thread

Scope: canonical BMW M52B28 M4 operating profile only

## 1. Decision

The M4 BMW operating profile must stop inheriting the P1.8 BMW asset's odd/even
exhaust split and `0.5 / 1.0` route-volume split. It must instead model the two
three-cylinder manifolds as adjacent cylinder groups with equal gross route
authority:

| M4 route | Route ID | Collector | Cylinders | Gas-path audio volume | Excitation audio volume |
|---|---:|---:|---|---:|---:|
| `exhaust.reference.0` | 1 | `exhaust.collector.0` / volume 21 | 1, 2, 3 | 1.0 | 1.0 |
| `exhaust.reference.1` | 2 | `exhaust.collector.1` / volume 22 | 4, 5, 6 | 1.0 | 1.0 |

The existing route IDs and semantic IDs remain the identities of the two current M4
route slots for this isolated correction. Keeping those slots is not a compatibility
surface: there is one canonical M4 profile before and after the change. Do not add an
old-topology switch, alternate profile, versioned alias, forwarding factory, or CLI
fallback.

This correction is necessary but not sufficient for production-quality exhaust
sound. The equal-route diagnostic was explicitly rejected as a finished sound because
it removed the false cadence while also exposing how nearly identical the two current
route transfer paths are. Rasp and metallic character must come from defensible
geometry, wave transfer, reflections, outlet/radiation behavior, and exhaust-system
coloration in the bounded M5 exhaust work. They must not be restored by unequal
cylinder groups or an arbitrary static route gain.

## 2. Diagnosed inherited defect

The current shared profile builder binds even cylinders to route 1 and odd cylinders
to route 2 in all three topology representations:

- gas collector edge selection in
  `src/profiles/bmw_m52b28_low_order_engine.cpp:358-372`;
- mechanism-cylinder exhaust route in
  `src/profiles/bmw_m52b28_low_order_engine.cpp:456-479`;
- excitation-cylinder route in
  `src/profiles/bmw_m52b28_low_order_engine.cpp:825-892`.

The same file gives route 1 an audio volume of `0.5` and route 2 a volume of `1.0`
in both the gas-path route records (`:644-647`) and excitation route records
(`:825-850`). The frozen oracle independently records the same assignment in
`reference/oracles/bmw-m52b28/SOURCE_MATRIX.md:138-146` and
`reference/fixtures/bmw-m52b28-p18/manifest.json:311-425`.

With the frozen firing order `1-5-3-6-2-4`, the inherited authoring produces:

| Crank angle in the 720-degree cycle | 0 | 120 | 240 | 360 | 480 | 600 |
|---|---:|---:|---:|---:|---:|---:|
| Firing cylinder | 1 | 5 | 3 | 6 | 2 | 4 |
| Inherited route volume | 1.0 | 1.0 | 1.0 | 0.5 | 0.5 | 0.5 |

The result is a deterministic `loud-loud-loud / quiet-quiet-quiet` envelope once per
four-stroke cycle. All six cylinders fire; the sound resembles a dropped-cylinder
condition because three consecutive firing events are authored approximately 6 dB
below the preceding three.

Read-only analysis of the accepted `1500 rpm / 0.85` held capture found:

- about `6.03 dB` route-RMS separation in the dry stems;
- about `6.03 dB` route-RMS separation after the common configured IR;
- six event-slot levels of approximately
  `0.00, -0.18, -0.10, -6.10, -6.17, -6.07 dB` in the dry sum;
- about `4.2 dB` strongest-half versus weakest-half separation remaining in the raw
  post-IR master; and
- energy in every firing slot, ruling out an actual omitted cylinder.

The two routes currently have the same effective audio length, the same 180-sample
delay, the same `smooth_39.wav` kernel and gain, and coherent positive summation. The
generic IR softens the modulation but does not create it and cannot repair it. It is
also not a documented physical BMW muffler model.

The local level-matched diagnostic set is retained at
`artifacts/listening/bmw-m52b28-route-balance-ab-ffcc45c/`. Changing only the route-0
volume from `0.5` to `1.0` nearly removed the six-event loping, which isolates the
static gain as the dominant cause. The user rejected that diagnostic as too smooth
and lacking the expected raspy character. Therefore neither the inherited imbalance
nor equalizing two otherwise duplicate paths is an accepted production answer.

## 3. Physical and listening evidence boundary

BMW lists distinct genuine M52-family [front exhaust manifold, part
11 62 1 740 730](https://shop.bmw.co.uk/product/genuine-exhaust-manifold-front-11-62-1-740-730)
and [rear exhaust manifold, part
11 62 1 740 731](https://shop.bmw.co.uk/product/genuine-exhaust-manifold-rear-11-62-1-740-731).
The two three-port front/rear parts, combined with consecutive cylinder numbering
along an inline-six, support the adjacent `1-2-3 / 4-5-6` grouping. This is an explicit
inference from the parts topology. It is not evidence for exact runner lengths,
diameters, catalyst construction, downstream merge geometry, or acoustic transfer
functions.

Under the corrected grouping, the same firing order alternates manifolds every
120 crank degrees:

```text
front: cylinder 1 at   0 degrees, cylinder 3 at 240, cylinder 2 at 480
rear:  cylinder 5 at 120 degrees, cylinder 6 at 360, cylinder 4 at 600
sum:   front, rear, front, rear, front, rear
```

The user's qualitative reference is this [custom-exhaust BMW inline-six
recording](https://www.youtube.com/watch?v=s2-drIC_pmM). It demonstrates the desired
combination of regular inline-six cadence, RPM-following rasp, and some metallic
custom-exhaust coloration. It is not a calibration recording: vehicle, exhaust
specification, microphone, processing, RPM trace, load, wind, road noise, and other
background sources are not controlled. The local comparison spectrogram is
`artifacts/listening/bmw-inline-six-reference-analysis-s2-drIC_pmM/reference-vs-route-balance-spectrogram.png`.

The physical rationale for later M5 work is also consistent with published exhaust
acoustics: branch geometry changes engine-order radiation in an exhaust Y-pipe
([SAE 2003-01-1657](https://saemobilus.sae.org/papers/influence-vehicle-exhaust-y-pipe-tailpipe-noise-2003-01-1657)),
while wave steepening and reflection interference can contribute metallic exhaust
noise ([SAE 871924](https://saemobilus.sae.org/papers/study-generation-mechanism-abnormal-exhaust-noise-871924)).
Those sources motivate a physical transfer model; they do not authorize tuning this
M4 correction to a particular spectrum.

## 4. M3 oracle preservation

The M3 parity profile `bmw-m52b28-legacy-low-order-v1` remains an isolated oracle. It
must continue to reproduce the frozen engine-sim route exactly, including:

- cylinders `2,4,6` on reference route 0 with audio volume `0.5`;
- cylinders `1,3,5` on reference route 1 with audio volume `1.0`;
- the matching gas collectors, excitation paths, delays, source equation, component
  seeds, and presentation; and
- every existing fixture byte, fixture digest, request identity, provenance self-seal,
  reference stem, and exact M3 parity expectation.

The frozen `SOURCE_MATRIX.md`, fixture manifest, fixture binaries, and M3 request/model
contracts must not be edited or regenerated to make the new M4 result agree with the
oracle. M3 preservation is test isolation, not product backward compatibility. No
production M4/M5 request, package, adapter, or runtime may select the M3 route as an
alternative sound.

The current profile builder shares most construction code between M3 and M4. The
implementation must branch the affected topology and volume values explicitly on the
already-present profile kind. It must not change the shared values first and then
weaken, delete, or regenerate M3 goldens.

## 5. Exact M4-only replacement contract

For `bmw-m52b28-low-order-operating-point-v1`, all representations of a cylinder's
exhaust destination must agree with this table:

| Cylinder | Mechanism exhaust route | Primary-to-collector destination | Excitation route |
|---:|---:|---:|---:|
| 1 | 1 | volume 21 | 1 |
| 2 | 1 | volume 21 | 1 |
| 3 | 1 | volume 21 | 1 |
| 4 | 2 | volume 22 | 2 |
| 5 | 2 | volume 22 | 2 |
| 6 | 2 | volume 22 | 2 |

The existing primary-to-collector edge identities remain owned by their cylinders;
only their collector endpoint changes where required. Route 1 continues to source
volume 21 through outlet edge 33. Route 2 continues to source volume 22 through outlet
edge 34. Each route owns exactly three cylinders, and no cylinder may feed both
collectors.

Both of these M4 fields must resolve to binary64 `1.0` for both routes:

```text
engine.physics.low-order-operating-point-v1
  .gas_path.exhaust_routes.<route>.audio_volume_linear

engine.physics.low-order-operating-point-v1
  .reference_excitation.routes.<route>.audio_volume_linear
```

The gas topology, mechanism topology, excitation topology, engine source-route
records, and presentation route binding must validate as one consistent graph. A
presentation stem labelled for route 1 must contain only the front group, and a stem
labelled for route 2 must contain only the rear group.

For isolation, this M4 correction does **not** change:

- cylinder count, identity/order, crank phasing, or firing order;
- combustion RNG state/stream, burn model, valve events, or gas-solver method;
- the current per-cylinder sound attenuation of `1.0`;
- the current gas primary and collector scalar parameters;
- the zero header-primary audio length, derived route length, 343 m/s delay rule, or
  resolved equal route delay;
- the empirical pressure-to-excitation equation or cylinder-count divisor;
- route DSP, common IR, wet/dry choice, master summation, audition gain, or mastering;
  or
- held-speed, idle-region, torque-sweep, and inertial-dyno scenario inputs.

Those unchanged approximations make the correction a controlled topology repair, not
an OEM exhaust model. M5 may replace them only through a separately frozen physical
contract and may not smuggle an arbitrary route imbalance back into M4.

The changed M4 leaves must no longer cite the P1.8 reference fixture as their physical
authority. Operating-profile provenance must content-bind this approved correction
record and identify the BMW front/rear part evidence above. The provenance wording
must preserve the inference and its limits; it must not claim measured BMW geometry.

## 6. Structural acceptance and invariants

The implementation checkpoint is complete only when all of the following hold:

1. Exact operating-profile tests pin the six-cylinder mapping, both `1.0` route
   volumes, matching gas/mechanism/excitation bindings, two three-cylinder collectors,
   and the alternating `1,2,1,2,1,2` route sequence under firing order
   `1-5-3-6-2-4`.
2. Mutating one cylinder back to odd/even routing, changing either volume by one
   binary64 value, cross-binding a collector, duplicating a cylinder, or leaving a
   cylinder unbound fails exact validation.
3. M3 fixture, mechanics, gas, excitation, renderer, request-identity, and provenance
   parity tests continue to pass without updating any M3 expected byte or digest.
4. Existing M4 operating-point mechanics, torque accounting, fixed-horizon sampling,
   held-speed, idle/low-load, and inertial-dyno tests continue to pass except for
   explicitly reviewed identity/hash refreshes caused by the canonical profile
   content change.
5. A focused signal diagnostic confirms zero authored route-volume delta and all six
   firing slots retain finite energy. It must not require measured route RMS to be
   identical: cylinder state and later physical transfer paths may legitimately make
   route energies differ.
6. The full clean test suite passes. No permanent waveform-similarity or subjective
   audio-score test is added.

These checks prove the graph and prevent recurrence of the diagnosed false cadence.
They do not prove that the sound is raspy, metallic, pleasant, authentic, or
production-ready.

## 7. Expected identity refresh

This is a canonical M4 content change, not a transparent patch. The implementation
must allow and explicitly review the resulting refresh of:

- the M4 operating-profile provenance bundle SHA-256;
- every canonical M4 simulation-request-v3 SHA-256 that embeds the profile;
- held-regression, held idle/low-load, torque-sweep, and inertial-dyno exact M4 test
  goldens;
- renderer source-closure identity when the changed source is in that closure;
- all newly rendered M4 manifest, stem, raw-master, audition-WAV, and sidecar hashes;
  and
- any evidence record whose content hash covers this contract or the changed profile.

New exact hashes are computed from the implemented canonical bytes, printed for
review, and then pinned. They must not be guessed, copied from the equal-gain
diagnostic, or made to retain old values by excluding topology fields from identity.

The semantic profile ID remains the sole greenfield M4 profile ID; do not introduce
`v2`, an old-ID alias, or dual decoding. Existing published artifacts remain immutable
historical outputs identified by their source commits and hashes. They are not
overwritten and are not evidence for the corrected profile. M3 identities and hashes
remain byte-for-byte unchanged as required by section 4.

## 8. Hard listening dependency

The topology correction may be committed after structural acceptance, but it is not
an accepted production-sound checkpoint by itself. The already-heard equal-volume
control established that removing the defect exposes an under-modelled exhaust path.
Therefore:

1. M4 throttle-application, lift, overrun, limiter, and other transient expansion
   remains paused.
2. The only fidelity work allowed to build on this checkpoint before listening is the
   first bounded M5 exhaust transfer/radiation slice needed to give the corrected
   front/rear routes physically justified distinction and RPM-tracking character.
3. That slice must preserve regular six-cylinder firing and may not use static route
   imbalance as a timbre control.
4. As soon as one complete candidate can produce idle-region, held-load, and natural
   inertial-dyno audio, render those clips concurrently from one clean commit. Publish
   raw reproducible outputs and explicitly labelled level-matched comparisons against
   the inherited baseline. Target approximately 30 seconds per clip and fail the gate
   above 60 seconds per clip.
5. Stop and wait for explicit user listening. Do not add further exhaust layers,
   intake/mechanical sources, transient behavior, or additional milestones until the
   user accepts the cadence and character or directs another isolated correction.

User listening, not a waveform metric, decides whether the first corrected physical
candidate has retained inline-six character without the false misfire envelope.
