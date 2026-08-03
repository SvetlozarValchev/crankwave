# M5 exhaust acoustic network

Status: **prescribed-flow linear acoustic concept rejected; recovery required**

Listening decision owner: user

Architecture author: agent. The user approved continuing the bounded M5
investigation, but the wholesale source/conditioning/IR replacement and pre-acceptance
production cutover were not separately presented for explicit approval.

Frozen: 2026-07-29 after the accepted M4 BMW operating-point work and the rejected
equal-route listening diagnostic

Scope: first one-way physical exhaust-acoustic replacement for the canonical ECE
left-hand-drive 1995--1998 E36 BMW 328i / M52B28 evaluation profile

## 1. Deficiency and replacement decision

The current M4 operating profile reaches the accepted engine-sim baseline through an
empirical path:

```text
10 kHz primary pressure proxy
  -> RPM-cubed/static-and-dynamic pressure formula
  -> integer delay and inverse-length-squared gain
  -> 10-to-192 kHz reconstruction
  -> stochastic jitter, derivative mix, and multiplicative air noise
  -> the same generic smooth_39.wav IR on both routes
  -> two-route sum
```

That path is useful only as the frozen M3 oracle. It does not represent six primary
pipes, pressure-wave reflections, three-way junctions, an exhaust outlet, or exterior
radiation. The inherited unequal route gain created a false dropped-cylinder cadence.
Correcting the routes to equal authority removed that defect but exposed nearly
duplicate transfer paths; the user heard the result as too smooth and missing the
expected RPM-following rasp and metallic custom-exhaust character.

The first M5 replacement is one complete, passive, small-signal acoustic network:

```text
six 80 kHz exhaust-valve source histories
  -> band-limited 192 kHz volume-velocity sources
  -> six bidirectional cylindrical-primary waveguides
  -> front/rear lossless pressure-continuity junctions
  -> two bidirectional downstream waveguides
  -> two causal unflanged-pipe radiation terminations
  -> two compact-source far-field pressure stems
```

This is the minimum closed network in which pipe propagation, junction scattering,
outlet reflection, and radiated pressure have physical meanings. Implementing only a
delay line without a termination, or only a radiation differentiator without the
returned wave, would not be the cited acoustic model.

For the M4 operating profile this network replaces, rather than layers over, the
empirical reference excitation, stochastic route conditioner, and `smooth_39.wav`.
There is no old/new request flag, quality mode, profile alias, or compatibility path.
The M3 oracle remains reference-only evidence. Listening A/B uses the immutable
accepted WAV or a separate build of its accepted commit, never a production switch.

## 2. Claim boundary

This checkpoint claims a physically dimensioned **one-way linear acoustic projection**
from the existing low-order gas solver. It does not yet claim:

- waveguide pressure feeding back into cylinder filling, pumping work, or torque;
- nonlinear finite-amplitude wave steepening, shocks, or exhaust blow-down CFD;
- mean-flow convection, vortex shedding, jet mixing noise, or flow-dependent outlet
  impedance;
- viscothermal loss calibrated for a particular pipe wall, temperature field, or gas
  composition;
- catalyst, crossover, resonator, rear-silencer, exhaust-flap, shell-radiation, or
  underbody transfer;
- measured OEM BMW primary lengths or internal diameters; or
- a calibrated microphone, room, road-load recording, or complete production mix.

The existing low-order gas volumes and restrictions remain the sole owners of engine
backpressure for this slice. The acoustic network consumes their valve-transfer
observables after each accepted gas substep and cannot mutate simulation state. A later
coupled one-dimensional gas-dynamics replacement must remove that split rather than
feeding this downstream pressure back as an extra correction.

The network must nevertheless sound acceptable as a standalone exhaust candidate.
Audible throbbing, false misfire cadence, unstable ringing, poppery discontinuities,
air-only noise, or generic buzz rejects this model now; later intake, mechanical, or
muffler work cannot excuse it.

## 3. Source capture contract

At the rejected M5 checkpoint, the ordinary `PortCaptureSample` was a post-step 10 kHz
observation. The first M5 design incorrectly inferred that its summed mass flow
established at most 5 kHz of physical source bandwidth, then exposed an exhaust-only
substep lane without changing the accepted gas calculation:

```text
outer capture rate       = 10,000 Hz
gas substeps per outer   = 8
source interval rate     = 80,000 Hz
source interval duration = 12.5 microseconds
ports per interval       = 6, in canonical exhaust-port identity order
records per 200-frame block = 200 * 8 * 6 = 9,600
```

Each record describes the valve transfer interval just executed and contains:

- cylinder and exhaust-port identity through the block layout and fixed lane order;
- chamber and primary absolute pressure and temperature immediately before transfer;
- signed transferred amount and interval-mean mass flow, positive cylinder to primary;
- the actual upstream density and sound speed selected by transfer direction;
- valve lift and molar-flow conductance;
- geometric effective area with explicit availability rather than fabricated zero;
- outer sample index, substep ordinal, and a rational 80 kHz post-interval clock.

The existing gas kernel uses one admitted pseudo-gas molecular mass and heat-capacity
ratio. Therefore, for this slice only:

```text
M       = 0.02897 kg/mol
gamma   = 1.4
R       = 8.31446261815324 J/(mol K)
Rspec   = R / M
mdot    = transferred_amount_mol * M / dt_substep
rho_src = p_upstream / (Rspec * T_upstream)
c_src   = sqrt(gamma * Rspec * T_upstream)
U_src   = mdot / rho_src
```

`U_src` is signed cubic metres per second into the primary. These gas-property values
are a truthful projection of the retained low-order gas model, not a claim that burned
exhaust has dry-air composition. A later mixture-property model changes the gas model
and source projection together.

The listening rejection proved that the eight records are not eight independently
time-resolved physical states. Chamber volume, mechanism angle, and valve state advance
only at the 10 kHz outer boundary and remain frozen across all eight gas transfers.
Publishing those relaxation iterations at 80 kHz creates discontinuities every outer
frame and fixed 10/20/30 kHz clock lines. They are numerical integration structure,
not resolved valve-flow bandwidth. This invalidates the first source contract.

Capture records are appended only after the corresponding transfer and all affected
cells pass the existing finite/physical checks. A failed gas substep publishes no
partial acoustic block. Block validation pins shape, identity, sign, exact clock,
finite values, positive density/sound speed, and continuity across block boundaries.

## 4. Source reconstruction and upstream boundary

Each of the six `U_src` histories is reconstructed from 80 kHz to 192 kHz by one
continuous causal windowed-sinc polyphase resampler:

```text
ratio                    = 192000 / 80000 = 12 / 5
taps                     = 257
fractional phase rows    = 4,096 plus the shifted wrap row
window                   = Kaiser, beta 12, with exact +0.0 endpoint overrides
cutoff                   = 0.95 of the 80 kHz source Nyquist
per-phase DC gain        = 1
arithmetic               = binary64, fixed traversal order
history                  = zero at render start, never reset at crop/block boundaries
```

Source record `i` is observed at the post-interval time `(i + 1) / 80000`. Acoustic
frame `m` is observed at `m / 192000`. While consuming source record `i`, the
resampler first emits the acoustic frames in
`[i / 80000, (i + 1) / 80000)` from the previously committed history and only then
commits record `i` at its timestamp. A record therefore cannot influence an acoustic
frame preceding that record. This clock convention is part of the reconstruction
contract, not a crop adjustment.

The 257-point Kaiser beta-12 window is constructed first, then taps `0` and `256` are
overridden with exact positive zero before serial per-phase DC normalization. The
4,097th table row is exactly `{+0.0, phase_0[0..255]}`. These endpoint and wrap rules
close the finite causal support and preserve exact unit DC gain; this is deliberately
Kaiser-derived rather than an unmodified stock Kaiser window.

One 200-frame outer block therefore maps exactly from 1,600 source intervals to 3,840
acoustic frames. The causal group delay is retained through preparation and cropping;
it is not trimmed by advancing one route differently from another.

At a primary's upstream end, the reconstructed prescribed volume flow drives a Norton
boundary. With `a` the wave arriving back from the primary, `b` the wave launched into
it, and `Zc` its characteristic impedance:

```text
b = a + Zc * U_src
```

Thus `(b-a)/Zc = U_src`. When the captured flow is zero, the boundary reflects with
coefficient `+1`, the rigid closed-port limit. During an open-valve interval this is a
prescribed-flow approximation: returned acoustic pressure does not change the captured
flow. That limitation is explicit and is the reason this checkpoint is not coupled
backpressure physics.

No RPM-cubed gain, arbitrary cylinder divisor, inverse-square exhaust-length gain,
synthetic air-noise draw, jitter, or derivative mix remains in this source mapping.

## 5. Uniform cylindrical waveguides

Each duct is a fixed, uniform, rigid-walled cylindrical plane-wave section. Its
authored acoustic inner area `A`, reference static pressure `p0`, reference temperature
`T0`, propagation-loss coefficient `alpha`, and length `L` resolve:

```text
rho0 = p0 / (Rspec * T0)
c0   = sqrt(gamma * Rspec * T0)
Zc   = rho0 * c0 / A
D    = L * 192000 / c0                 acoustic frames
g    = exp(-alpha * L)                 one-way amplitude survival
```

Pressure and volume velocity decompose into traveling waves:

```text
p = p+ + p-
U = (p+ - p-) / Zc
```

Every direction owns an independent fixed fractional-delay line. `D` must be at least
two frames. Its integer part is a circular buffer and its fractional part uses causal
linear interpolation. Linear interpolation is deliberately chosen for the first slice
because its coefficients are non-negative and sum to one, so it cannot amplify a
frequency. The admitted propagation loss requires finite `alpha >= 0`, hence
`0 < g <= 1`. No delay or loss parameter changes during a render.

This fixed-temperature, constant-loss line is less complete than established
viscothermal/mean-flow pipe models. It is still a controlled physical replacement for
the old single integer delay: it preserves two directions, characteristic impedance,
geometry-dependent travel time, and returned reflections. Frequency-dependent wall
loss is a later isolated hypothesis after the lossless/junction/radiation network is
heard.

The plane-wave validity report computes `ka = 2*pi*f*a/c0`. Results above the first
non-planar circular-duct cutoff `ka = 1.841` are labelled outside the conservative
single-mode domain and cannot be cited as validated pipe acoustics. They may remain in
the delivered WAV for listening, but the report gives the corresponding frequency for
every authored radius and temperature.

## 6. Front and rear three-way junctions

Each BMW manifold group is a four-port acoustic junction: three primaries plus one
downstream pipe. For each connected duct `i`, let `a_i` be the pressure wave arriving
at the junction, `b_i` the wave leaving it, and `Y_i = 1/Zc_i`. The ideal compact
junction is:

```text
pJ  = 2 * sum(Y_i * a_i) / sum(Y_i)
b_i = pJ - a_i
```

This enforces one common junction pressure and `sum(U_i) = 0` under the sign convention
that every connected flow is positive toward the junction. With positive
characteristic impedances its scattering matrix is passive. Tests compare the update
against the analytic matrix, pressure continuity, flow conservation, impulse energy,
and arbitrary block partitioning.

The exact bindings are:

| Junction | Exhaust ports/cylinders | Downstream outlet route |
|---|---|---|
| front | 1, 2, 3 | `exhaust.outlet.front` |
| rear | 4, 5, 6 | `exhaust.outlet.rear` |

Under firing order `1-5-3-6-2-4`, excitations alternate front/rear. No static route
imbalance or odd/even grouping may be used as a timbre control.

The junction is compact and lossless in this slice. Merge angles, finite junction
volume, separation, and nonlinear branch loss are unknown BMW geometry and remain a
separate later replacement.

## 7. Causal unflanged outlet and radiated pressure

The downstream end uses the causal low-order pressure-reflection approximation of
Silva, Guillemain, Kergomard, Mallaroni, and Norris for an unflanged sharp-edged rigid
cylindrical pipe. With standard `exp(+j omega t)` convention, `s = j omega`, outlet
radius `a`, and `tau = a/c0`:

```text
R(s) = -(1 + n1*tau*s) / (1 + d1*tau*s + d2*tau^2*s^2)
n1 = 0.167
d1 = 1.393
d2 = 0.457
```

The continuous model is converted once per outlet to a binary64 biquad with the
bilinear transform `s = 2*fs*(1-z^-1)/(1+z^-1)`. The implementation verifies every
pole is inside the unit circle and samples the entire digital Nyquist interval to
reject a compiled termination whose `|R|` exceeds one beyond numerical tolerance.
The reflected wave is `p- = R(z) p+` and the outlet volume velocity is:

```text
U_out = (p+ - p-) / Zc
```

The source paper's causal approximations match their plane-mode references within
about 8% for `ka <= 2`. Its explicit assumptions exclude mean axial flow, nonlinear
effects, and vortex shedding; M5 records those omissions rather than calling this a
complete exhaust termination. The 2015 corrigendum changes the paper's derived
radiation-impedance equation, not the reflection form used here.

Each outlet stem is pressure in pascals at a declared one-metre reference point using
the compact monopole projection:

```text
p_rad(r,t) = rho_ambient / (4*pi*r) * dU_out(t-r/c_ambient)/dt
```

The derivative is the causal backward difference at 192 kHz. A fixed fractional delay
implements `r/c_ambient`; it is never removed by crop alignment. This is a coherent
virtual free-field observation, not a microphone or vehicle cabin. A declared
`pa_per_full_scale` calibration converts Pa to the raw WAV domain. Audition copies may
apply one common reported gain; route-specific hiding gains are forbidden.

Sources:

- [Silva et al., *Approximation formulae for the acoustic radiation impedance of a
  cylindrical pipe*](https://arxiv.org/abs/0811.3625)
- [Silva et al., 2015 corrigendum](https://doi.org/10.1016/j.jsv.2014.10.001)
- [Pang, Rebandt, and Qatu, *Influence of Vehicle Exhaust Y-Pipe on Tailpipe
  Noise*](https://doi.org/10.4271/2003-01-1657)

## 8. BMW evidence and declared evaluation geometry

The physical identity is narrowed to the ECE left-hand-drive 1995--1998 E36 328i.
BMW/ETK evidence supports separate front and rear three-port manifolds and a dual-pipe
downstream assembly. It does **not** publish the acoustic centerline and inner geometry
required by the solver.

Relevant evidence includes:

- BMW front manifold `11 62 1 740 730` and rear manifold `11 62 1 740 731`;
- the [ECE manifold parts diagram](https://www.realoem.com/bmw/enUS/showparts?diagId=11_6070&id=CF22-EUR-01_1995_E36_BMW_328i);
- the [ECE exhaust-system diagram](https://www.realoem.com/bmw/enUS/showparts?diagId=18_0244&id=BK71-EUR-07_1994_E36_BMW_328i), whose dual-pipe support is marked
  `D=45MM/50MM`; and
- SAE 2003-01-1657, which reports that equal Y branches retain firing-order harmonic
  content while unequal branches increase half-order content in a six-cylinder
  application.

The inherited asset's 20-inch primary, 50-litre collector volume, four-inch collector
diameter, and derived 17.96-metre audio length have no demonstrated BMW measurement
authority and do not enter this assembly.

Exact OEM primary centerlines/IDs, wall thickness, collector dimensions, pipe
assignment, catalyst substrate, crossover, resonator, rear-silencer chambers, flap
state, and tailpipe IDs remain `measurement_required`. The first listening candidate
is therefore explicitly a **declared equal-primary twin-open-pipe test-cell/custom
exhaust**, not the stock E36 exhaust:

| Parameter | Initial value | Provenance and meaning |
|---|---:|---|
| six primary acoustic IDs | 0.042 m | inferred evaluation value; not an OEM dimension |
| six primary centerline lengths | 0.300 m equal | scenario/evaluation choice; equality prevents invented half-order roughness |
| two downstream acoustic IDs | 0.046 m | inferred from nominal 50 mm custom/dual-pipe hardware with unverified wall allowance |
| two downstream centerline lengths | 1.500 m equal | scenario/evaluation choice for open-pipe test cell; not an E36 component measurement |
| primary reference temperature | 800 K | inferred warm loaded test-cell state |
| downstream reference temperature | 600 K | inferred warm loaded test-cell state |
| reference static pressure | scenario ambient | scenario |
| one-way amplitude loss | 0.10 Np/m | conservative inferred damping; not calibrated BMW wall loss |
| outlet | unflanged sharp-edged open pipe | declared evaluation termination |
| observation distance | 1.0 m | declared free-field QA projection |
| ambient observation temperature | scenario ambient | scenario |
| raw-WAV calibration | 256 Pa per full scale | declared evaluation headroom convention; about 142 dB SPL peak at the one-metre reference, not measured BMW data |

All six primaries and both downstream pipes are pairwise equal in the initial
candidate. Rasp must emerge from valve-flow timing, physical propagation, junction and
outlet reflections, and radiation—not from invented cylinder inequality.

A focused sensitivity run may vary all six primary lengths together over
`0.20/0.30/0.38 m` and all six IDs together over `0.036/0.042/0.046 m`. These are
labelled geometry experiments using the same solver, not alternative algorithms or
OEM claims. One candidate is selected only by explicit listening; arbitrary
per-cylinder tuning is forbidden without measurement.

The stock silencer is not approximated by a straight pipe or generic IR. A stock
exhaust profile requires measured geometry, a validated multiport transfer model, or
an identified rights-cleared transfer measurement in a later isolated checkpoint.

## 9. Expected result and cost

Compared with accepted M4, the candidate is expected to produce:

- regular six-event cadence with alternating front/rear outlet activity;
- geometry-following resonances and reflected detail instead of two duplicate static
  IR paths;
- additional RPM-locked upper-band energy from preserving 80 kHz valve-transfer
  timing and physical volume-flow differentiation;
- no synthetic broadband air layer and no static `0.5/1.0` route coloration;
- two outlet stems in Pa whose sum is coherent and independently inspectable; and
- a timbre change under a whole-geometry sensitivity sweep without changing firing
  order or source gain.

The new cost is six 80-to-192 kHz reconstructions, sixteen directional delay lines,
two four-port scattering updates, two termination biquads, two derivatives, and two
observer delays per acoustic frame. Memory remains bounded by the resampler histories
and physical delays. This is expected to be small beside the existing 80 kHz gas
substeps and far cheaper than nonlinear CFD/MOC.

The 15-second pull target remains at or below about 30 seconds on this PC. A clean
single clip over 60 seconds, any material concurrent-throughput collapse, or any
unbounded allocation/retained capture history blocks listening acceptance.

## 10. Focused verification

The implementation commits stay smaller than the audible cutover and cover:

1. Exact six-port/eight-substep capture shape, clock, sign, SI validity, transactional
   failure, and block continuity.
2. Exact 1,600-to-3,840 reconstruction cadence, DC preservation, band rejection, and
   partition equality.
3. Integer/fractional matched-line delay, bounded gain, impulse timing, and partition
   equality for each waveguide.
4. Analytic junction scattering, pressure continuity, flow conservation, passivity,
   and exact `1-2-3 / 4-5-6` ownership.
5. Outlet DC reflection, stable poles, bounded reflection magnitude, reference-distance
   scaling, zero radiated DC, and no second derivative in presentation.
6. Complete session continuity, route identity, finite output, no clipping/DC fault,
   exact duration, and deterministic reproduction.
7. One clean public render with the M5 source closure and no link/runtime dependency
   on the M3 excitation, route conditioner, or `smooth_39.wav` asset.

These checks can reject structural/numerical failures. Spectra and order tracks may
confirm predicted changes, but no metric certifies sound quality.

## 11. Cutover and hard listening stop

Implementation proceeds as reviewable commits: capture lane, authored assembly,
resampler, waveguide, junction, termination/radiation, session, production cutover,
then evidence. Sound-affecting work does not share a commit with unrelated cleanup.

At cutover the M4 operating profile has one route:

```text
LowOrderCaptureSession
  -> ExhaustAcousticSession
  -> 192 kHz outlet-pressure blocks
  -> crop / common calibration / stems / coherent master / WAV
```

Presentation accepts the already-radiated outlet pressure directly. It must not apply
the legacy source derivative, stochastic air noise, jitter, or generic configured IR.
The two old `exhaust.reference.*` production labels become
`exhaust.outlet.front/rear`; exact request, provenance, manifest, and artifact
identities refresh honestly. No decoder or alias accepts the old production shape.

As soon as this complete path can render, run the geometry sensitivity as a bounded
diagnostic, select at most one candidate, then render concurrently from one clean
commit:

- 700 rpm held idle region;
- 1,500 rpm / 0.10 throttle;
- 1,500 rpm / 0.85 throttle;
- 3,000 rpm / 0.85 throttle; and
- the natural inertial dyno climb.

Publish raw Pa-calibrated stems/master, separately labelled common-gain listening
copies, route solos, route-muted deltas, manifests, hashes, and single/concurrent
timings. Compare against the immutable accepted M4 files under the same audible crop.

The first complete matrix rendered with unity audition gain and exposed PCM24
saturation before listening: the largest raw-master magnitude was `1.545777` at the
3,000 rpm / 0.25-throttle point, while the dyno and 3,000 rpm / 0.85-throttle point
also exceeded full scale. The physical Float32 pressure stems and raw master remain
unchanged. The sole audition transform therefore uses one explicit `0.5` monitoring
gain for every scenario, leaving about `2.23 dBFS` of peak headroom over this frozen
matrix without limiting, normalization, or scenario-specific gain.

Then **stop**. Do not add a silencer, nonlinear layer, intake, mechanical sound,
transients, cycle variation, or another M5 hypothesis until the user accepts or rejects
the cadence and exhaust character by ear. Acceptance removes the superseded M4
production excitation/presentation implementation from non-reference targets.
Rejection keeps the structural/capture evidence only if it is independently correct,
records the audible failure, and redesigns the isolated acoustic model rather than
building later work on it.

## 12. Listening rejection and forensic result

The user rejected the complete `32fb288` candidate on 2026-07-29 as bad, synthetic,
and robotic. The prior `0.5` audition gain removed PCM24 clipping but did not change
the Float32 physical stems or their timbre. Read-only comparison against the accepted
M4 held and dyno artifacts isolated two audible failures upstream of mastering:

1. At 3,000 rpm / 0.85 throttle, approximately 97% of the 30 Hz--20 kHz M5 master
   energy is above 5 kHz, versus approximately 0.03% in accepted M4. Its 10 kHz and
   20 kHz lines are respectively about 13 dB and 17 dB above the 150 Hz firing
   fundamental. Across the dyno, the dominant spectral bin remains at approximately
   20 kHz in 84 of 86 analysis frames rather than rising with RPM.
2. The front and rear stems are equal-level, near-duplicate waveforms delayed by one
   firing interval. At loaded held points their best-delay correlation is
   approximately 0.95--0.99. Their coherent master therefore cancels half-firing
   structure and repeats the remaining firing waveform almost perfectly.

The first failure follows directly from the false source-bandwidth premise above. The
80-to-192 kHz resampler admits content to approximately 38 kHz, and the compact
far-field formula then differentiates outlet volume velocity. The differentiation is
valid for a smooth volume-flow history but strongly emphasizes the injected outer-step
discontinuities. The fixed lines also lie above the current downstream ducts'
approximately 6.2 kHz single-plane-mode limit and outside the cited outlet
approximation's declared `ka <= 2` accuracy range.

No evidence identified a valve-flow sign, cylinder-to-route mapping, junction
scattering, outlet polarity, crop, stem sum, or mastering-gain error. The ideal
prescribed-flow Norton boundary, one-way acoustic coupling, identical evaluation
geometry, and coherent same-point outlet projection remain model limitations, but
they do not explain away the fixed solver-clock carrier.

Post-process-only diagnostics then removed the radiation derivative, restricted the
full master to the plane-wave validity band, and isolated one outlet. The user heard
both the validity-band full master and the validity-band route solo as a synthetic
sound wave rather than an engine. Therefore the fixed solver-clock carrier and
coherent twin-route comb are confirmed defects, but removing them is not sufficient.

The foundational failure is the source/model boundary: total quasi-steady valve
volume flow is imposed as an infinite-authority Norton source into a deterministic
linear network of ideal pipes and junctions. It does not preserve the accepted
pressure-source character and it does not solve the coupled nonlinear cylinder,
valve, mean-flow, and duct dynamics that would make this a defensible physical
replacement. Higher-rate kinematics or a validity low-pass would make the same
synthetic concept numerically cleaner, not turn it into an engine.

The associated process failure was treating a mathematically complete acoustic
network as one audible hypothesis. In fact the attempt simultaneously changed:

- valve/port source semantics from primary pressure to total valve volume flow;
- excitation mapping and source impedance;
- route grouping and gain;
- resampling and bandwidth;
- pipe, junction, and boundary transfer;
- radiation/presentation; and
- the generic IR, jitter, and air-noise conditioning.

The small implementation commits made those equations reviewable but did not make the
audible experiment incremental. No playable candidate existed until all changes were
combined. Commit `33b3ad8` then removed the accepted renderer before user acceptance,
misapplying the greenfield/no-compatibility requirement. A greenfield product may have
one current implementation, but the candidate must first pass the plan's temporary
A/B listening gate.

The corrective gate above is superseded. Recovery now requires:

1. restore the tracked `4b65127`/`ffcc45c` last-good audible floor as the sole current
   renderer, with no old/new switch;
2. remove the rejected M5 runtime rather than retaining it as a future production
   option;
3. retain this document and the diagnostic artifacts only as failure evidence;
4. freeze a research-backed coupled source/propagation replacement before
   implementation; and
5. change and audition one physical seam at a time against the exact tracked
   last-good dyno, retaining the accepted implementation until the user accepts the
   replacement by ear.

The exact baseline is pinned in
[`../M4_BMW_LAST_GOOD_AUDIO_BASELINE.md`](../M4_BMW_LAST_GOOD_AUDIO_BASELINE.md).
