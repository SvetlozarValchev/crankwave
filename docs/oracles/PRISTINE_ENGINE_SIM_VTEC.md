# Pristine engine-sim VTEC oracle

Status: source-audited parity authority for headless slice 8

Authority: Ange Yaghi `engine-sim` commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`

## Executed selector

Pristine `VtecValvetrain::isVtecEnabled()` selects the alternate intake and
exhaust cams when all three strict predicates are true:

```text
mean intake-manifold absolute pressure > pressure threshold
abs(crank angular speed)              > engine-speed threshold
1 - resolved throttle closedness      > linkage-opening threshold
```

Equality does not activate the alternate cams. The selector has no hysteresis,
blend, delay, debounce, or retained state. The same decision selects both
alternate camshafts.

The values come from current engine state:

- `Engine::getManifoldPressure()` averages the absolute pressure of every
  intake;
- `Engine::getSpeed()` returns the absolute output-crank angular speed;
- `Engine::getThrottle()` returns the linkage-resolved throttle closedness, not
  the caller's unshaped throttle request.

The clean-room runtime has one admitted shared intake in this slice, so its mean
manifold pressure is the retained left-boundary pressure of that intake plenum.
It evaluates the selector once per 10 kHz gas frame before valve sampling. Every
cylinder in that frame therefore sees one coherent base or alternate cam pair.

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

### Explicit Honda authoring normalization

The pinned Honda asset is internally inconsistent with the advance and
crank-cycle conventions used by the known-working engine packages:

- its timing table contains `-25` through `-40` degrees even though pristine
  `IgnitionModule` treats the sampled value as advance and subtracts it from the
  firing angle;
- it places intake centers with `360 - center` and exhaust centers with
  `360 + center`, opposite the working cam phasing.

A literal clean-room transcription cannot produce the requested physical pull.
With the dyno brake removed and aggregate loss reduced nearly to zero, crank
speed fell from 5,000 to 4,665 rpm over the 15-second released horizon. Correcting
only the cam polarity restored strong positive work; retaining the negative
timing polarity still left the engine near 4,910 rpm after 15 seconds.

The Honda package therefore retains every authored magnitude and firing offset
but normalizes the two polarities:

```text
timing advance:  +25 ... +40 degrees
intake centers:  360 + center - cylinder firing offset
exhaust centers: 360 - center - cylinder firing offset
```

This is a package correction, not a VTEC-selector or simulation-algorithm change.
With the original aggregate loss policy, 10 kg*m2 equivalent inertia, and 25 N*m
passive brake restored, the physical fixture crosses its 8,000 rpm target near
the end of the 15-second released horizon. Base/alternate lobe shapes, selector
thresholds, gas execution, audio routing, and mastering remain unchanged.
