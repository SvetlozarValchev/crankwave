# Post-parity mechanical-engine fidelity plan

Status: planned; sound-bearing work is blocked on an engine-specific structural
transfer asset.

Date: 2026-08-04

## Decision

The next production-source family is `mechanical.engine`. Its first isolated audible
component will be combustion- and compression-induced engine-structure radiation,
driven by the six existing BMW cylinder-pressure lanes. `mechanical.starter`, piston
side thrust/slap, valvetrain contact, timing drive, and accessories remain later,
independent gates.

The canonical clocks do not change:

```text
physics and capture       20,000 Hz
source and presentation  192,000 Hz
method block              400 capture frames -> 3,840 source frames
```

This plan does not authorize a generic mechanical oscillator, pulse train, noise bed,
resonator bank, or reused exhaust impulse response.

## First physical excitation

For each cylinder `i`, retain one identity-stable axial pressure-force lane:

```text
A_i       = pi * bore_i^2 / 4
F_i(t)    = A_i * (p_cylinder_i(t) - p_crankcase(t))
```

The bore, absolute cylinder pressure, crankcase pressure, cylinder identity, crank
state, and operating state are already resolved or captured. The force therefore
changes with the simulated compression, combustion, load, RPM, ignition, and fuel
state rather than with an unrelated sound control. It also remains meaningful while
motoring or coasting.

The six lanes remain separate until the declared structural transfer. The mono
engine-local monitoring projection is then:

```text
p_monitor(t) = sum_i h_i(t) * x_i(t)
```

where each `x_i` is either cylinder pressure or pressure force and every transfer
`h_i` declares the matching input quantity and units. The first mono output is one row
of a MIMO-capable transfer matrix; it must not be implemented by summing cylinders
before transfer or by assigning one anonymous filter to the aggregate.

The accepted causal 20 kHz-to-192 kHz reconstruction is reused. It does not create
content above the 10 kHz Nyquist limit imposed by 20 kHz capture, and this slice does
not synthesize such content. Raw `dp/dt` or `d2p/dt2` may be diagnostic quantities but
are not acoustic transfer models and cannot be mixed directly into the master.

## Evidence basis

The source/transfer/radiation separation and engine-specific transfer requirement are
established practice:

- [Lee, Bolton, and Suh, *A Procedure for Estimating the Combustion Noise Transfer
  Matrix of a Diesel Engine* (2008)](https://docs.lib.purdue.edu/herrick/216/)
  identifies an engine-platform-specific matrix from individual cylinder pressures to
  radiated sound and preserves inter-cylinder correlation and variation.
- [Kanda, Okubo, and Yonezawa, SAE 900014
  (1990)](https://doi.org/10.4271/900014) separates the physical excitation, force
  transfer paths, and radiating engine surfaces.
- [Shu, Wei, and Han, SAE 2005-01-2486
  (2005)](https://doi.org/10.4271/2005-01-2486) experimentally identifies a cylinder-
  pressure-to-radiated-noise transfer function rather than treating pressure itself as
  microphone audio.

These references establish the method shape, not BMW calibration. Their engines and
parameters are not substitutes for M52 data.

## Hard blocker: no BMW structural transfer exists

The repository currently has no rights-cleared, calibrated BMW M52/M52TU structural
transfer asset. `smooth_39.wav` is an exhaust presentation IR and is explicitly
ineligible.

Before the sound-bearing route begins, provide one of:

1. a measured, per-cylinder M52 pressure/force-to-radiated-pressure transfer matrix;
   or
2. an identified M52 structural and radiation model with sourced geometry, modal
   frequencies, modal masses/participation, damping, radiation behavior, and
   calibration.

Each transfer input must record:

- its cylinder and physical application path;
- input and output quantities and units;
- measurement or identification method, locations, orientation, microphone distance,
  operating condition, phase/delay convention, and sample rate;
- content hash, provenance, and redistribution rights; and
- absolute calibration or an explicitly bounded calibration uncertainty.

A common guessed IR, a visually copied response curve, or a plausible-sounding modal
bank does not satisfy this requirement. If the asset is unavailable, work stops after
the exact-audio capture checkpoint rather than producing a provisional buzz and
claiming a later phase will repair it.

## Two checkpoints

### A. Topology and capture, no audio change

- Add the typed `mechanical.engine` component inventory and stable per-cylinder
  pressure/force diagnostic seam.
- Preserve cylinder identity, units, capture clock, sample phase, and block continuity.
- Do not collapse the component lanes into the route-level body wrench before the
  structural transfer.
- Keep the production master on the accepted exhaust-plus-intake route set until the
  required transfer asset is admitted. Do not retain a runtime silent/active switch.
- Prove every existing exhaust, intake, raw-master, and audition-master WAV byte for
  byte against the accepted control.

This checkpoint may be implemented before the transfer asset exists. It is an SI
capture foundation, not a listening candidate and not a claim that
`mechanical.engine` is production-complete.

### B. One sound-bearing structural route

- Add a typed pressure/force-to-acoustic transfer-matrix asset contract. Do not
  overload the untyped exhaust-IR meaning.
- Reconstruct each cylinder lane to 192 kHz and apply its matching causal transfer.
- Preserve per-cylinder transfer outputs as development diagnostics, then reduce them
  in fixed cylinder order into one `mechanical.engine` route.
- Publish the normal dry, configured-transfer, and selected route stems and include
  the selected route once in both engine masters.
- Leave exhaust, intake, combustion physics, randomness, mastering, and all other
  mechanical mechanisms frozen.

There is one accepted production path after this checkpoint. No compatibility mode or
optional old/new renderer remains.

## Listening gate

Produce all four 15-second BMW held-dyno comparisons from one render:

| Clip | Contents |
|---|---|
| A | Accepted exhaust-plus-intake control with mechanical muted; its accepted PCM must match exactly. |
| B | Six-cylinder pressure/force diagnostic solo at a declared fixed monitoring scale; explicitly not microphone audio. |
| C | Transferred `mechanical.engine` solo. |
| D | Full exhaust plus intake plus transferred mechanical route. |

All clips have the same crop, clock, frame count, fades, and polarity convention. A
and D use the same master gain. No clip uses independent normalization, automatic
leveling, limiting, saturation, soft clipping, or a repair EQ. The current accepted
bake takes about 39 seconds on the development PC; the complete one-render comparison
targets no more than 45 seconds there.

After technical rejection checks pass, stop immediately for listening. Do not begin
starter, side-thrust, valvetrain, accessory, UI, or other fidelity work first. A tonal
buzz, whine, artificial pulse train, loss of engine character, or degraded full mix
rejects the candidate; another subsystem may not be layered on to hide it.

## Explicit piston-slap boundary

The simulator already evaluates a constrained piston-wall side reaction for its
friction model, but that is not a piston-slap model. The current BMW authoring does not
provide piston secondary-motion clearance, skirt profile, wrist-pin offset,
lubrication/contact stiffness, or damping. The runtime also retains only reaction
magnitude at the current public seam.

Research models piston slap as secondary piston motion and impact/contact followed by
the structure's measured response; see [Cho, Ahn, and Kim, *Journal of Sound and
Vibration* 255(2)](https://doi.org/10.1006/jsvi.2001.4152) and [*The Piston Slap Force
Reconstruction of Diesel Engine Using WOA-VMD and Deconvolution*, Sensors 24(12)](https://doi.org/10.3390/s24123833).
Side thrust/slap therefore remains a later isolated component with its own authored
data, transfer evidence, comparison, and listening stop. It cannot be named or implied
by this first pressure-driven slice.

## Completion boundary

Acceptance of this slice establishes only the first modeled contribution inside
`mechanical.engine`. The route is not production-complete until the BMW mechanism
inventory required by [`PRODUCTION_SOURCE_MATRIX.md`](PRODUCTION_SOURCE_MATRIX.md)
classifies every materially radiating engine-local mechanism as modeled or genuinely
not applicable. `mechanical.starter` remains a separate child route and is not
applicable to the already-running held-dyno audition.
