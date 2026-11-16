# Mechanical diesel sound

Status: executable experimental native bake and simulator-free package playback.
The supplied I4 and Ural V8 are listening candidates, not calibrated real engines.
The first audition was rejected as a low-frequency buzz; the revised example uses
metered injection and an explicitly versioned articulation transfer.

The `mechanical-diesel-v1` branch of `crankwave/engine` generates warm four-stroke
direct-injection diesel sound, including authored pilot/main injection trains. It uses the
[diesel cylinder model](DIESEL_CYLINDER_V1.md) to bake pressure-force and exhaust-flow
sources, and a separate acoustic approximation to turn those sources into audio.
It requires no recorded engine samples or downloaded impulse responses.

## Use

The tracked [I4 example](../../data/engines/mechanical-diesel-i4/engine.json) supplies
all geometry, injection, gas, and presentation parameters. Its 48 operating points
cover 650–3,400 RPM and zero through full fueling.
The [Ural V8 example](../../data/engines/ural4320-mechanical-v8/README.md) covers
550–2,275 RPM with a separate exhaust path for each bank. Its reference recordings
are used for comparison only; the bake has no recording dependency.

```bash
cmake -S . -B build -DCRANKWAVE_BUILD_TESTS=ON
cmake --build build --target crankwave_cli
mkdir -p artifacts/diesel-sound
build/crankwave bake-crankwave \
  --engine data/engines/mechanical-diesel-i4/engine.json \
  --output artifacts/diesel-sound/mechanical-diesel-i4.crankwave
build/crankwave verify-crankwave \
  --input artifacts/diesel-sound/mechanical-diesel-i4.crankwave

node scripts/serve-crankwave-harness.mjs
```

Open the printed localhost URL and load the `.crankwave` file. Drive mode supplies
RPM and load from the harness's existing vehicle approximation. Its diesel adapter
adds authored no-load fueling, maintains firing idle with the pedal released, and
cuts fuel during overrun above idle. These audition controls do not identify a
real pump governor or friction map. Manual mode exposes
RPM and **Fueling**, with intake pressure as a separate observation. The irrelevant
air-throttle control is hidden for this runtime.

To export a constant operating point through the same playback implementation:

```bash
node tools/render-crankwave.mjs \
  artifacts/diesel-sound/mechanical-diesel-i4.crankwave \
  artifacts/diesel-sound/idle.wav 800 0.12 4
```

The tool accepts carrier, WAV path, RPM, normalized fueling/load, and optional
duration in seconds. It writes mono float32 WAV at 192 kHz and refuses to overwrite
an existing output. The native baker likewise publishes atomically without replacing
an existing carrier. Use another output filename after changing an engine.

This branch is admitted by `bake-crankwave` and the package consumer. It is not yet
admitted by the general `compile_engine`/EngineSession C++ and C APIs, `render`
scenario command, or live simulator workbench. The WAV utility renders the baked
package; it does not imply admission to those other session paths.

## Authored model and capture

[The schema branch](../../schemas/diesel-mechanical-engine.schema.json) is referenced
by the existing [engine schema](../../schemas/engine.schema.json). The native parser
rejects duplicate or unknown members, invalid references/IDs, invalid geometry or
valve windows, overlapping injection pulses, and malformed axes. SI units appear
in the field names. Physical and presentation parameter groups require explicit
provenance origin and notes.

One to twelve cylinder bindings share explicitly identical hardware. Each binding
has a stable ID, firing angle in `[0, 4*pi)`, fuel trim, and structural coupling
gain. The example fires cylinders 1–3–4–2 at 180-degree intervals. Up to eight
hydraulic pulses share a chamber's preparation, oxygen, and ignition state; the
mechanical example uses one main pulse. Its optional
`minimum_duration_fraction_01` (default 1) meters delivery duration as
`full_duration * (minimum + (1 - minimum) * fueling)` while conserving the requested
fuel mass. The example uses 0.08: at 12% fuel the pulse occupies 19.04% of the full
window. This represents a simple rack-delivery approximation, not pump hydraulics.
Gas integration rebuilds injection-knot events at every intake-valve closure.
The example's longer authored ignition delay allows a distinct premixed pressure
rise at light load. These remain tuning choices requiring reference calibration.
Pilot/main can be authored through the same
model, but no common-rail controller or calibration is claimed.
The [Golf-style common-rail preset](../../data/engines/golf16-common-rail-i4/README.md)
uses two pilots and a main pulse, with prescribed load-dependent boost boundaries.

The bake uses 20 kHz physics/capture and 32 gas substeps per frame. The finer gas
grid resolves heat-release feedback before sampling the physical source. At each
RPM/fuel anchor it waits for three consecutive settled cycles after the configured
minimum. The example requires 0.05% relative repeatability. Settling checks
complete-cycle work and peak pressure, plus temperature,
mass, oxygen, and unburned tracer **exactly at intake-valve closure**. Failure to
settle within the configured maximum is an error; no unsettled cell is published.

The stored physical channels are:

```text
pressure force [N]      = piston area * (cylinder pressure - crankcase pressure)
outward exhaust [kg/s]  = -exhaust port mass into cylinder / frame duration
```

These cross the one-way physics-to-presentation boundary. Pressure force is a
scalar piston-crown load, not net engine-body force, piston side force, or acoustic
pressure. Exhaust flow comes from the cylinder's executed valve exchange. The
first version has fixed boundary reservoirs and no pipe-wave feedback.
Version 12 also stores motored pressure force in N at each cell's own prescribed
intake/exhaust states. No version solves pipe-wave feedback.

Settled sources are aligned to firing TDC and linearly sampled onto an authored
power-of-two 720-degree grid, with pressure interpolated between physical frames
and exhaust using its interval-average flow. Every RPM/fuel cell is retained even
when its intake pressure equals another cell's pressure. Periodic source playback
approximates settled operation; it does not reproduce transient gas dynamics.

## Playback and acoustic approximation

The existing `CrankwaveAudioEngine` facade accepts the carrier and returns finite
mono PCM. `engine.loadCoordinate` is `normalized-fueling` for this model.
`load01` directly selects fuel fraction; `throttle01` remains the host's demand
input but has no separate acoustic effect in this steady diesel model. The driving
harness maps demand to its externally owned RPM/load trajectory. Zero fueling is
motoring, not governed firing idle.

For each output sample, the cursor integrates the host's RPM trajectory, applies
each cylinder's firing phase, and interpolates its pressure-force and exhaust
sources across RPM and trimmed fueling. Fuel trim is clamped at the full-fueling
edge of the baked domain. Phase continues across load and RPM changes. No gasoline
pressure-coordinate conversion or fictitious manifold pressure participates in
selection.

The version 2 presentation uses seeded per-cylinder/cycle variations in fuel
selection (up to ±10% in the example) and source timing (±0.7 crank degrees).
Fuel variation selects the physically baked pressure/flow grid. Timing spread is
an acoustic playback approximation; it shifts the captured cycle and does not
solve crankshaft speed fluctuations or re-integrate ignition delay. Parameters
crossfade during intake, away from firing, to avoid discontinuities. Stable IDs,
cycle indices, and the authored seed make resets and read partitioning repeatable.
Version 2 adds no broadband noise. Version 3 retains these variations and adds
the flow-dependent exhaust approximation described below.

Pressure forces retain their cylinder-specific phase, fuel trim, and scalar
structural coupling until summation. Version 2 applies an authored second-order
high-pass before the modal bank to reject slow pressure motion. A fixed bank of
band-pass resonators then
produces the block component, followed by a second-order low-pass filter. This
first transfer has the same modal shape for all cylinders, scaled by their coupling;
it is not a measured multichannel engine-block transfer matrix. Pressure-to-noise
transfer estimation is an established approach, as illustrated by
[Lee, Bolton, and Suh](https://docs.lib.purdue.edu/herrick/216/); that work does not
provide the example's authored coefficients.

The resonators use the unity-peak band-pass and low-pass coefficient equations in
the [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/), with transposed
direct form II state. Frequency is in Hz and Q controls damping. Each mode's gain
maps newtons to uncalibrated PCM. Coefficients are compiled at 192 kHz and remain
fixed while RPM changes. Structural ringing is therefore in physical time rather
than baked into a texture that changes pitch with RPM.

In versions 1 and 2, summed exhaust flow passes through an authored high-pass
(second-order at 90 Hz in the I4) and second-order low-pass, scaled by
`exhaust_gain_pcm_per_kg_s`. This is a low-order
audible exhaust coloration, not a solved far-field monopole or calibrated muffler.
Block and exhaust are summed, multiplied by the master gain, and passed through
an explicit artistic `tanh` saturation stage. An initial 20 ms fade suppresses
transport startup clicks. None of these presentation operations affects physics.

Version 3's optional `exhaust_system` keeps each cylinder's flow separate until
its authored branch receives it. Every cylinder must belong to exactly one branch.
Each branch uses a fractional delay in seconds and a low-pass filter in a feedback
loop with reflection magnitude at most 0.9. Branch gain and delay are independent
of RPM. These are stable, lossy delay effects with authored coefficients; they do
not solve pipe gas dynamics or identify the recorded vehicle's exhaust geometry.

The summed branch output is high-pass filtered. Its discrete time derivative
is multiplied by `radiation_gain_pcm_s2_per_kg` and mixed with the dry flow term.
This highlights changes in mass flow as an acoustic approximation. In addition,
each branch generates seeded noise whose amplitude follows its outward input
flow. The noise is high-pass filtered, scaled by `flow_noise_gain_pcm_per_kg_s`,
and included before the final exhaust low-pass. Zero input flow generates no new
noise, although filters and delays can retain a tail. This is an authored proxy
for flow texture, not calibrated turbulence. Reset clears delay/filter history
and restores each branch's seed; partitioning reads does not change the samples.

Version 5 adds `pressure_texture`. For each cylinder, it subtracts the zero-fuel
force capture at the same RPM and phase from the fueled capture. Positive increments
of that residual accumulate into an envelope with exponential decay in seconds.
Independent seeded noise, scaled by that envelope, excites the existing block
filters alongside separately weighted coherent residual and motoring compression.
Successive firings retain pressure-dependent energy but have different fine structure.
Fuel cut adds no combustion texture after the tail decays. The noise reference
rate is 192 kHz; amplitude scales with the square root of the processing-rate ratio.

`decay_s` is bounded to 1–50 ms, `coherent_gain` and `compression_gain` to 0–1,
`stochastic_gain` to 0–32, and `seed` to an unsigned 32-bit integer. Native and
JavaScript admission require both articulation and the exhaust system. The source
payload and its SI units are unchanged. This is pressure-driven stochastic acoustic
excitation, not solved turbulence, a mechanical-impact simulation, or added cylinder
energy. No recorded waveform or impulse response is used.

Version 6 adds optional `mechanical_texture`, mixed alongside the existing block
and exhaust paths before output saturation. It has two sources independent of
fueling. Each cylinder triggers a short pair of damped resonances at intake and
exhaust valve closure. Resonant frequencies vary by cylinder, closure amplitudes
vary deterministically by cycle, and tails decay in seconds. A quieter filtered
noise source follows a smooth intake-valve window. This breath is an acoustic
proxy; it is not an additional gas-flow capture. Neither source adds combustion
on fuel cut. Both use an authored square-root RPM gain relative to 750 RPM.

The native baker derives `mechanical_timing` from the same physical intake opening,
intake closure and exhaust closure angles used by the gas model. Mechanical timing
uses unperturbed crank phase, independent of the combustion timing spread. Valve
impacts use two modes at the authored frequency and 1.67 times that frequency,
with a shorter upper-mode decay. The breath path uses a 120 Hz high-pass and the
authored low-pass. Its bandwidth and cylinder-count normalization limit gain changes
with sample rate and cylinder count. These are authored sound sources, not solved
valve contact forces, lash, intake turbulence or accessory dynamics.

`valve_gain_pcm` is bounded to 0–0.5, `valve_frequency_hz` to 400–3,000 Hz,
`valve_decay_s` to 0.5–8 ms, `pumping_gain_pcm` to 0–0.2, and
`pumping_lowpass_hz` to 300–2,000 Hz; `seed` is an unsigned 32-bit integer.
Mechanical texture requires pressure texture and its dependencies. Muting both
new gains recovers version 5 PCM exactly. Diagnostics expose pre-master `valveRms`
and `pumpingRms` alongside block and exhaust levels. The physical source payload
remains unchanged, and old packages retain their existing sound.

Version 7 replaces the instantaneous valve kicks with a finite contact force.
`valve_attack_s` (0.25–4 ms) sets the duration of a unit-area Hann pulse feeding
each valve's two resonators. Its value and slope are zero at both ends, removing
the abrupt onset targeted by the crackle report from the first mechanical audition.
Duration lives in seconds, independently of RPM. Pending contact and resonance
state survive audio block boundaries and are cleared on reset. The intake breath,
combustion and exhaust paths are unchanged. Version 6 retains instantaneous
excitation for reproducible comparisons with its original carrier.

Version 8 adds `low_fuel_exhaust`, a downstream exhaust-only transfer for idle
and unbraked coasting. The existing tailpipe signal continuously feeds a quieter
low-pass path. Its mix rises smoothly as normalized fueling falls below
`fueling_threshold_01`, reaching the authored `gain` and `lowpass_hz` response at
zero fuel. Above the threshold, settled playback bypasses it exactly. An exponential
mix transition with time constant `transition_s` prevents sudden gain changes on
pedal release or fuel restoration. The quiet filter stays warm during bypass.

For this Ural, the threshold is 0.2, coast gain 0.16, low-pass 550 Hz, and transition
40 ms. At the audition's 0.12 idle fueling the mix is only partially active; loaded
revving retains its accepted transfer. Combustion, valve and intake-breath paths
are unaffected, so reducing exhaust pulses does not silence mechanical rotation.
This is an authored acoustic balance, not a change to gas captures or a physical
compression-release brake. The old transfer gave motored flow nearly as much
exhaust emphasis as loaded firing, which the user described as brake-like.

Admission bounds are 0.01–0.4 for the fuel threshold, 0–1 for coast gain,
250–1,500 Hz for the low-pass, and 5–200 ms for the transition time. This extension
requires version 7's smooth mechanical texture and all earlier dependencies.

Version 9 adds `coasting_balance` below a separate, lower fuel threshold. The
version 8 reduction left coasting too quiet and dominated by a low hum. This
extension instead uses a complementary first-order split of the original tailpipe
signal: a low component and the remainder. `bass_gain` scales the low component;
`movement_gain` scales the remainder. This retains upper exhaust-pulse detail while
keeping the low rumble restrained. Equal gains give a flat amplitude response.

For this Ural, the split is 180 Hz, bass gain 0.18 and movement gain 0.48. Below
0.06 fueling it also fades the existing valve and intake-breath sources toward
1.6 times their ordinary level. No new combustion, noise source or brake is added.
Both the exhaust replacement and mechanical gain use a 40 ms transition. At idle
fueling (0.12) and under load, settled playback bypasses this extension exactly,
preserving version 8's quieter idle and the accepted loaded sound.

The bounds are 0.01–0.12 for the fuel threshold, 0–1 for each exhaust gain,
80–400 Hz for the crossover, 1–2 for mechanical gain, and 5–200 ms for transition
time. This extension requires `low_fuel_exhaust` and its dependencies. Diagnostics
include the applied mechanical gain in valve and pumping RMS. The physical source
captures are unchanged; these controls specify an authored acoustic balance.

Version 10 adds `coasting_flow_texture` to retain broad exhaust movement between
firing events and during fuel cut. Per bank, the sum of positive captured cylinder
flows drives a first-order envelope with time constant `envelope_s`. Independent
seeded excitation is multiplied by that envelope, then passes through second-order
high-pass and low-pass filters and the existing bank gain. Approximate bandwidth
normalization keeps the authored gain usable across sample rates. The result is
added to the tailpipe after the earlier exhaust transfers, multiplied by version
9's existing smooth coasting amount. The filters, envelope and PRNG continue during
bypass; reset clears their states. Zero flow from rest produces silence, and a
stopped flow decays rather than leaving a permanent noise bed. This path models no
heat release, injection, valve restriction or engine brake.

The Ural uses gain 0.4 PCM per kg/s, a 250–1,800 Hz band and a 2 ms envelope.
The user identified gear changes around 0:08 in the publisher's exhaust and engine
microphone previews. Within those passages the 500–2,000 Hz spectral flatness
remains higher on coast than in version 9's audition. This motivates adding broad
flow texture rather than increasing the existing narrow tonal components. These
are independently edited previews with unknown RPM, fuel, processing and road
noise contributions, so this is an artistic approximation, not identification of
a measured turbulence source. It does not change the captured pressure/flow arrays.

Admission bounds are gain 0–4, high-pass 150–800 Hz, low-pass 800–3,000 Hz and an
envelope time constant of 0.5–20 ms, with a 32-bit integer seed. Native and runtime
admission additionally require low-pass frequency at least twice the high-pass.
This extension requires `coasting_balance` and its dependencies. `coastingFlowRms`
reports its contribution after the coasting fade, before master gain; it is also
included in `exhaustRms`. Prior version playback and settled fueled PCM are retained.

Version 11 adds `loaded_pressure_texture`, integrating the user-accepted softer
rev experiment. It maintains the original pressure-texture history and a second
envelope driven by the same positive fired-minus-motored pressure increments.
The second envelope decays with `decay_s`, then passes through first-order amplitude
smoothing with time constant `attack_s`. Both use the same deterministic per-cylinder
noise stream. This rounds the pulse onset without low-passing the final audio.

The strongest authored exhaust bank(s) receive stochastic radiation weight 1;
the other banks receive `secondary_bank_gain`. Weights are normalized by the sum
of squared structural cylinder gains, with a finite fallback for muted routing.
An approximate isolated-pulse energy correction of
`sqrt(base_decay * (decay_s + attack_s) / decay_s**2)` scales stochastic gain.
Coherent pressure and motored compression retain their original contributions.
This is an acoustic perspective approximation, not changed cylinder fueling.

The Ural uses 4 ms attack smoothing, 12 ms decay and secondary-bank gain 0.55.
A smoothstep target between 0.2 and 0.5 normalized fueling, followed by a 40 ms
exponential fade, blends the original and rounded pressure textures before the
existing block radiation filters. Both histories remain warm while bypassed.
Reset clears both envelopes and the load fade. Fully loaded playback reproduces
the accepted standalone rounded-rev audition; settled idle/coasting and old
carriers preserve their earlier PCM.

Admission bounds are decay 5–30 ms, attack 1–10 ms, secondary-bank gain 0.2–1,
starting fuel 0.12–0.4, full fuel 0.2–1 and transition 5–200 ms. Full fuel must
exceed starting fuel. This version requires `coasting_flow_texture` and its
dependencies. The existing `blockRms` diagnostic includes the mixed radiation;
exhaust, valve, intake-breath and coasting-flow diagnostics are unchanged.

The diesel cursor uses 3,840-frame batches. Streaming `process()` retains the
facade's one-batch latency rule, giving 20 ms of declared internal latency; the
browser ring adds its own lead. Offline `render()` has no added streaming delay.
Reset clears filter and phase history. Different offline read partitions produce
identical PCM; dense streaming controls preserve the submitted trajectories.
Peak and component RMS diagnostics exclude the inaudible filter warmup; the
reported frame count still includes those warmup frames.

## Carrier and identity

The CRANKWAVE v1 container and descriptor schema remain unchanged. The new declared
runtime kind is `diesel-mechanical-audio-v1`. Its `runtime.json` uses schema
`crankwave/diesel-mechanical-audio`. The original version 1 remains readable with
its original filters. Version 2 requires the presentation `articulation` object
and selects `pressure-force-modal-radiation-v2`; it adds high-pass frequencies,
cycle fuel/timing bounds, and a variation seed. Version 3 additionally requires
`exhaust_system` and selects `pressure-force-modal-radiation-v3`.
Version 5 additionally requires `pressure_texture` and selects
`pressure-force-modal-radiation-v5`. Version 4, the withdrawn idle-gain experiment,
is not admitted. Version 6 additionally requires `mechanical_texture` and the
natively derived `mechanical_timing`, and selects `pressure-force-modal-radiation-v6`.
Version 7 additionally requires `mechanical_texture.valve_attack_s` and selects
`pressure-force-modal-radiation-v7`. Version 8 additionally requires
`low_fuel_exhaust` and selects `pressure-force-modal-radiation-v8`.
Version 9 additionally requires `coasting_balance` and selects
`pressure-force-modal-radiation-v9`. Version 10 additionally requires
`coasting_flow_texture` and selects `pressure-force-modal-radiation-v10`.
Version 11 additionally requires `loaded_pressure_texture` and selects
`pressure-force-modal-radiation-v11`. Version 12 adds a matching motored-force
source per operating cell and selects `pressure-force-modal-radiation-v12`.
Version 13 permits rounded pressure radiation at idle and selects
`pressure-force-modal-radiation-v13`, retaining version 12's three-channel layout.
Versions 1–3 and 5–11 retain their existing playback paths and two-channel layout.
All versions bind:

- engine ID, method IDs, clocks, and the normalized-fueling coordinate;
- the exact authored engine source digest and parameter provenance;
- the native baker executable digest and admitted numeric policy, under the
  explicit `native-cpp-experimental` backend identity;
- cylinder bindings, acoustic parameters, and the complete ordered rectangular grid;
- the source binary's byte count, layout, and SHA-256.

### Prescribed boost boundaries (version 12)

`engine.operating_boundaries` is optional. When present it contains exactly one
`{rpm, fueling_01, intake, exhaust}` record per bake cell, in RPM-major and
fueling-minor order. Each reservoir has the same pressure, temperature and oxygen
fields and bounds as the cylinder's defaults. Missing, duplicate, reordered and
out-of-range cells are rejected. The complete version 11 presentation is required.

The baker executes each fueled cylinder at its authored reservoirs, then executes
a zero-fuel cylinder at those **same** reservoirs. The source layout becomes
`cell-phase-force-n-exhaust-kg-s-motored-force-n`: three interleaved float32 values
per phase, 12 bytes instead of 8. Fuel-cut cells reuse their fired force exactly.
Playback interpolates all three channels with the same RPM, fuel and phase weights.
The pressure texture subtracts that matching motored force. Subtracting the ambient
fuel-cut row instead would mistake extra compression from boost for combustion.
Displayed MAP comes from the same authored pressure grid.

This represents steady boosted operating points. It does not solve a compressor,
turbine, turbo lag, ECU injection schedule, rail pressure, EGR or aftertreatment.
The acoustic exhaust transfer remains authored. Without the map, presets that do
not opt into idle rounding preserve the legacy source layout and processing.
Tests compare the unchanged boundary cells
byte for byte and ensure boosted compression alone cannot excite combustion noise.

### Pressure rounding at idle (version 13)

The same pressure-driven envelope can be useful at common-rail idle. A
`loaded_pressure_texture.fueling_start_01` below 0.12 or `fueling_full_01` below 0.2
now explicitly selects version 13. Its admitted start range is 0–0.4 and full range
is 0.01–1, with full strictly greater than start. Older runtime versions retain
their original bounds. The historical field name remains; the DSP, seeded noise,
energy correction and fuel interpolation are unchanged.

Version 13 always captures a matching motored-force channel, including when the
boundary map is absent. Fuel cut still produces no combustion excitation. With
unchanged parameters, version 13 replays version 12 PCM exactly. Native tests check
version selection, mapped source identity and unchanged legacy force/flow; runtime
tests cover idle activation, fuel cut, layer isolation and partitioned replay.

The separate Golf idle-reference candidate uses 7 ms attack, 25 ms decay and a
0–0.12 fuel interval. Rounding is therefore also active above idle; matching its
loaded sound remains a listening task. This is an acoustic adjustment, not an ECU
injection or pressure-rise calibration. The previous Golf and Ural definitions
retain their existing versions and sound.

Version 14 optionally adds `presentation.inline_inertia`, a primary/secondary
slider-crank inertia approximation for equal reciprocating masses and parallel
inline cylinders. It requires the complete rounded-pressure presentation chain.
The baker derives `inertial_geometry.crank_radius_m` from half the cylinder stroke
and copies `connecting_rod_length_m` from the native cylinder definition.
Version 14 requires both fields; earlier manifests retain their admission contracts.

The force is `m r omega² sum(cos(theta - phase) + r / rod * cos(2(theta - phase)))`.
The sum uses physical firing phases without artistic cylinder radiation gains.
For an ideal I4 the primary terms cancel; the secondary terms reinforce at twice
crankshaft frequency. The force follows runtime crank angle, scales with squared
speed and continues during fuel cut. An authored high-pass, bandpass resonance,
low-pass and PCM/N gain radiate this force into the mix. It is not tied to a
combustion noise envelope and contains no reference audio.

Parameters and admitted ranges are: `reciprocating_mass_kg` 0.05–2 per cylinder,
`frequency_hz` 50–300, `q` 0.5–10, `gain_pcm_per_n` 0–0.001,
`highpass_hz` 50–300, and `lowpass_hz` 300–1,000 with low-pass at least twice
high-pass. This is a kinematic/acoustic approximation, not solved mount motion,
balance-shaft cancellation, angular-acceleration forcing, higher kinematic orders,
rocking moments or within-cycle crank-speed dynamics. Parallel cylinder axes are
an authoring assumption; it is not a V-engine inertia model. The effective mass
and acoustic transfer must not be interpreted as an identified vehicle measurement.

The [Golf ramp candidate](../../data/engines/golf16-common-rail-i4/RAMP_REFERENCE.md)
uses this layer to retain a low engine-order tone during rundown. Its existing
native source cells remain unchanged. Runtime tests cover force cancellation,
phase, squared-speed scaling, fuel independence, zero-gain legacy PCM equality and
deterministic partitioning. `inertiaRms` is exposed only for presets using the layer.

Version 15 adds optional `presentation.loaded_clatter` on the complete version 14
presentation chain. It is a separate fast combustion-radiation path, allowing the
existing idle/body texture to retain its softer envelope. Each cylinder's positive
increments of fueled-minus-motored native force excite an exponential decay followed
by a smooth attack filter. That envelope supplies a coherent pulse and independent
seeded noise to an authored structural modal bank. The input is taken before the
existing pressure texture; injection and all native source samples are unchanged.
This is an acoustic pressure-rise proxy, not solved injector, piston or bearing
contact forces. It contains no reference recording or measured impulse response.

The mix is the product of smoothstep RPM and fuel intervals, followed by an
exponential transition. Per-cylinder envelopes, PRNGs and radiation filters remain
warm while bypassed. Zero mix bypasses addition exactly; fresh idle/fuel-cut renders
therefore preserve the preceding PCM when those controls are below the thresholds.
After fuel release the previous excitation and mix decay. Identical fueled/motored
forces cannot generate this clatter even when the mix controls request it.

The required parameters and ranges are `rpm_start` 400–3,500, `rpm_full` 800–4,500,
`fueling_start_01` 0–0.8, `fueling_full_01` 0.01–1 (both intervals strictly positive),
`attack_s` 0.0001–0.002, `decay_s` 0.0005–0.008, `transition_s` 0.005–0.2,
`coherent_gain` 0–2, `stochastic_gain` 0–12, `highpass_hz` 200–1,500,
`lowpass_hz` 2,000–8,000 (at least twice high-pass), and unsigned 32-bit integer
`seed`. `modes` contains one to eight objects with `frequency_hz` 400–6,000,
`q` 0.5–15 and `gain_pcm_per_n` 0–0.0002. `loadedClatterRms` appears only when this
layer is present. The separately versioned admission rejects this field in older
packages. Native tests verify version selection and exact source preservation;
runtime tests check source dependence, decay, idle/coast bypass, other-layer
isolation, seeded replay, read partitioning and malformed controls.

The [Golf loaded-clatter candidate](../../data/engines/golf16-common-rail-i4/LOADED_CLATTER_REFERENCE.md)
tested this path for missing diesel character during rev-up, but was rejected in
listening for the wrong character. RPM and fuel
thresholds and radiation settings are authored controls, not measured ECU behavior.

Version 16 adds `cylinder_coupling` to every `loaded_clatter.modes` entry. Each
array must bind every engine cylinder exactly once with `{id, gain}`, where gain
is between −2 and 2. Coupling order does not matter; unknown IDs, duplicates,
missing cylinders and mixed coupled/uncoupled modes are rejected before playback.
The native baker selects version 16 when these bindings are present. Version 15
and earlier retain their existing admission and rendering paths.

Each cylinder's pressure-rise excitation is multiplied by its existing structural
gain and its mode-specific acoustic weight before summing into that mode's filters.
Thus upper resonances can have different cylinder contributions without changing
fuel balance, firing phases, the existing low body or other mechanical layers.
Signed weights describe acoustic polarity. They are authored radiation couplings,
not measured cylinder forces or an identification of the recorded microphone path.
The RPM/fuel fade and exact bypass behavior are inherited from version 15.

The [Golf upper-metal candidate](../../data/engines/golf16-common-rail-i4/UPPER_METAL_REFERENCE.md)
tests this after the user identified the real recording's 1,400–6,500 Hz texture
as the missing character. Tests cover mode-selective excitation, ID ordering,
signed and zero weights, unchanged native sources, legacy replay, bypass and
partitioned rendering. Listening remains the acceptance test for character.

`sources.f32le` stores cell-major, phase-major pairs of force and outward flow
(triples including motored force for versions 12–16) as
little-endian float32. `engine.json` retains the exact authored source. The package
loader verifies its authenticated entry digest against the runtime's source digest
without parsing engine physics during playback. Native publication verifies the
complete carrier and descriptor bindings before atomically publishing it. Browser
admission additionally verifies runtime semantics, all numeric samples, and the
source-payload binding.

This backend identity intentionally makes no clean-source gasoline parity claim.
Repeatability is scoped to identical source and executable under the admitted
native numeric environment. JavaScript rendering follows the consumer DSP path;
no cross-language, cross-engine bit-identity claim is made.

## Verification and limits

Native tests cover the diesel model parser, malformed inputs, settling, direct
fuel-to-work response, deterministic captures, cancellation, and carrier binding.
The underlying cylinder tests cover mass, oxygen, fuel, and energy conservation,
motoring compression, choked/reverse valve flow, repeated firing, fuel cut,
overlapping valve flow, nonintegral cycle/frame scheduling, and gas-grid convergence.
The Ural integration checks captured pressure at gas-exchange TDC and the audible
level on fuel cut to reject the first fixture's restrictive-valve surge.

JavaScript tests cover fuel selection at identical manifold pressure, finite audio,
separate block/exhaust energy, cycle variation and seed replay, exact offline replay,
streaming control/latency,
the resonator's independent frequency response, exhaust-delay causality at two
sample rates, bounded feedback decay, branch routing, seeded flow texture, and malformed authenticated
packages.
Pressure-texture tests cover silence for identical fueled/motored inputs, independent
cylinder excitation, varying successive burns, decay after fuel cut at two sample
rates, streaming controls, reset, read partitioning and malformed parameters.
The browser harness integration covers the real Worker/DSP/package path,
controls, network isolation, and output production, using its existing audio-device
test double. This is not subjective listening validation or a device-driver test.

The integration reports spectral balance and crest factor without perceptual
pass/fail thresholds. The earlier thresholds passed user-rejected audio and have
been removed. Engineering checks do not establish a convincing engine sound.

The first purported Soundsnap reference was a 6.2-second spoken session-expiry
message. Its spectra and all comparisons against it are invalid. A subsequent
ordinary page request followed by the public player request in the same HTTP
session returned the 90.7-second MP3. It and the publisher's startup previews
show substantially different low-speed balance and temporal texture from the
current candidate, but supply no measured RPM. The rejected idle-only gain
adjustment has been removed. Source-model and acoustic calibration remain open;
neither automated descriptors nor passing tests constitute listening validation.
Pressure-rise
spectral content matters to diesel noise, as discussed by
[Tung and Crocker](https://saemobilus.sae.org/papers/diesel-engine-noise-relationship-cylinder-pressure-820237),
but that work does not validate this example's coefficients.

The supplied sound remains an artistic warm direct-injection example. Cold start,
governor/pump hydraulics, turbocharging, prechambers, pipe waves,
residual-fuel oxidation, and physical piston-slap/injector/valvetrain impacts are
outside this model. It provides combustion-driven block clatter and exhaust from
procedural pressure/flow generation. It does not claim a measured engine match,
calibrated sound pressure, or complete startup/shutdown lifecycle support.
