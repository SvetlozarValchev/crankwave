# Pristine engine-sim VTEC oracle

Status: source-audited parity authority for headless slice 8

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

## Executed ownership

Pristine does not own one engine-wide VTEC selector. Each `CylinderHeadNode`
generates the valvetrain referenced by that head, and each
`VtecValvetrainNode::generate()` call allocates a fresh `VtecValvetrain` plus
four fresh base/alternate camshafts (`scripting/include/cylinder_head_node.h:23-35`,
`scripting/include/vtec_valvetrain_node.h:17-45`). The resulting selector retains:

- its own base and alternate intake/exhaust camshaft pointers;
- its own RPM, manifold-pressure, and throttle-opening thresholds; and
- a pointer to the shared engine state.

Separate bank-local heads may therefore use standard and VTEC valvetrains in the
same engine, or use distinct VTEC valvetrains with different cams and thresholds.
Two VTEC banks can consequently select different profiles when their local
thresholds straddle the same sensed engine state. The shipped Honda fixture has
only one bank, so this general topology follows from the executed construction path,
not from a multi-bank production calibration.

## Executed selector

Pristine `VtecValvetrain::isVtecEnabled()` selects the alternate intake and
exhaust cams belonging to that valvetrain when all three strict predicates are true:

```text
mean intake-manifold absolute pressure > pressure threshold
abs(crank angular speed)              > engine-speed threshold
1 - resolved throttle closedness      > linkage-opening threshold
```

Equality does not activate the alternate cams. The selector has no hysteresis,
blend, delay, debounce, or retained state. Intake and exhaust do not have separate
activation thresholds: both query the same predicate, although each role owns its
own base and alternate camshaft.

The values come from current engine state:

- `Engine::getManifoldPressure()` averages the absolute pressure of every
  intake;
- `Engine::getSpeed()` returns the absolute output-crank angular speed;
- `Engine::getThrottle()` returns the linkage-resolved throttle closedness, not
  the caller's unshaped throttle request.

These three values are engine-global inputs. In particular, every bank-local
selector receives the same arithmetic mean across all intake plenums; it does not
read only the intake bound to its own cylinders. The selector thresholds and chosen
camshafts remain valvetrain-local.

## Exact evaluation timing

Pristine does not explicitly evaluate one engine-wide selector once per frame.
After the rigid-mechanics step and throttle-linkage update, it visits combustion
chambers in engine-cylinder order (`src/simulator.cpp:95-149`,
`src/piston_engine_simulator.cpp:283-318`). Each chamber caches intake conductance
and then exhaust conductance. Those two lift queries independently call the same
head-local `isVtecEnabled()` predicate (`src/combustion_chamber.cpp:226-233`,
`src/cylinder_head.cpp:51-67`, `src/vtec_valvetrain.cpp:37-66`).

The three sensed inputs do not change between those per-port calls: mechanics and
throttle have already committed, and intake/cylinder fluid substeps occur only after
every chamber has cached both conductances. The intake and exhaust results for one
selector are therefore coherent despite the repeated source calls. A clean-room
implementation may snapshot the pure predicate once per bank-local valvetrain at the
physics-step boundary and reuse that decision for its cylinders and both ports; this
is numerically equivalent to the source order, not a claim that pristine literally
made one call. The snapshot follows the scenario's admitted physics clock. It is not
fixed at 10 kHz; the pinned Honda asset authors a 20 kHz simulation frequency.

The clean-room runtime takes the arithmetic mean across all admitted intake-plenum
pressures in canonical intake order; one intake remains the identity case. It then
evaluates each bank-local selector against that shared mean and the current global
speed and linkage position before valve sampling.

## Deliberately absent input

Pristine accepts and stores `min_speed`, but `VtecValvetrain` never reads it.
Vehicle speed is not an activation predicate. The greenfield JSON contract
therefore omits this dead field instead of preserving it as compatibility data
or inventing behavior for it.

The public activation keys are:

```json
{
  "minimum_engine_speed": {"value": 5800, "unit": "rpm"},
  "minimum_manifold_pressure_abs": {
    "value": 84393.05666666664,
    "unit": "Pa"
  },
  "minimum_throttle_linkage_opening_01": 0.3
}
```

The pressure threshold is the pristine default `1 atm - 5 inHg`, expressed as
absolute pressure. For the Honda fixture's direct linkage with gamma `2`, the
caller command must exceed `sqrt(0.3)` before the resolved opening predicate can
be true.

## Listening fixture

The source reference is
`assets/engines/atg-video-1/05_honda_vtec.mr`, a Honda B18C5 inline four:

| Cam role | Duration at 0.050 in | Maximum lift |
|---|---:|---:|
| Base intake | 210 deg | 6.9 mm |
| Base exhaust | 190 deg | 6.5 mm |
| Alternate intake | 240 deg | 11.5 mm |
| Alternate exhaust | 232 deg | 10.5 mm |

The transition fixture is a full-throttle 5,000--8,000 rpm inertial pull across
the 5,800 rpm selector boundary, accompanied by held checks at 5,400 and 7,000
rpm. The renderer, source routing, conditioning, impulse responses, and mastering
remain unchanged during this slice.

### Explicit Honda direction normalization

The pinned Honda asset couples a negative starter speed to reverse crank
rotation. Its ignition advance, cam centers, and firing phases are consequently
authored in that direction. The clean-room runtime exposes positive RPM and a
forward public crank-cycle coordinate, so signed crank-referenced angles must be
reflected rather than copied literally.

A literal clean-room transcription cannot produce the requested physical pull.
With the dyno brake removed and aggregate loss reduced nearly to zero, crank
speed fell from 5,000 to 4,665 rpm over the 15-second released horizon. Correcting
only the cam polarity restored strong positive work; retaining the negative
timing polarity still left the engine near 4,910 rpm after 15 seconds.

The Honda package retains every authored magnitude and the 1-3-4-2 firing
sequence while normalizing all direction-dependent angles:

```text
timing advance:   -source timing
cam lobe center: (-source center) modulo 720 degrees
firing phases:    cylinder 1=0, cylinder 3=180,
                  cylinder 4=360, cylinder 2=540 degrees
```

This is a package correction, not a VTEC-selector or simulation-algorithm change.
With the original aggregate loss policy, 10 kg*m2 equivalent inertia, and 25 N*m
passive brake restored, the physical fixture crosses its 8,000 rpm target near
the end of the 15-second released horizon. Base/alternate lobe shapes, selector
thresholds, gas execution, audio routing, and mastering remain unchanged.
