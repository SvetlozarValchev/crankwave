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
