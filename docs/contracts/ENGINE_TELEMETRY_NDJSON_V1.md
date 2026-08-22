# Engine telemetry NDJSON v1

## Purpose and scope

Every successful finite native `render` transaction publishes a fixed mechanical
diagnostic stream alongside its audio artifacts. The stream is intended for trusted
authoring automation, including private candidate-evaluation loops. It is
not a user-selectable telemetry facility and it is not an LLM conclusion or score.

Authored `output.telemetry_channels` remains unsupported. The renderer, rather than
the model or user, owns this schema, cadence, path, and field inventory.

The artifact contract is fixed:

| Property | Value |
| --- | --- |
| role | `diagnostics.engine-telemetry.v1` |
| kind | `telemetry` |
| path | `telemetry/engine-telemetry.v1.ndjson` |
| schema | `crankwave.engine-telemetry.ndjson.v1` |
| diagnostic | `true` |
| required | yes |

The render manifest binds the exact byte count and SHA-256. A native render cannot
commit successfully without declaring, writing, validating, sealing, and hashing this
artifact in the same atomic transaction as every WAVE artifact.

This contract applies to successful finite native bake/render transactions. It does
not require an artifact from an open-ended session, an internal atlas capture, or a
failed transaction. Failed renders expose the existing typed `FailureContext` at the
library boundary; a future out-of-process CLI failure envelope is a separate contract.

## Sampling and ordering

The stream contains exactly one endpoint record for each native session block. The
current admitted clock is 400 physics frames at 20 kHz and 3,840 delivery frames at
192 kHz: one endpoint every 20 ms, or 50 Hz. Preparation and audible blocks are both
included. There is no second resampler or configurable telemetry cadence.

Records occur in this exact order:

1. one `header` record;
2. one `block` record per session block, in zero-based ordinal order;
3. immediately after a block, zero or more `cycle` records completed by that block;
4. one `footer` record.

Each completed cycle appears exactly once. Each record is one canonical JSON object
followed by exactly one LF byte. No blank lines or trailing data are admitted.

## Canonical encoding

- Object keys use the order specified below.
- UTF-8 strings must contain Unicode scalar values.
- Every 64-bit signed or unsigned integer is a quoted decimal string.
- 32-bit counts and schema versions are JSON integers.
- Masks are quoted, fixed-width lowercase hexadecimal: `0x` plus 8 digits for
  32-bit masks or 16 digits for 64-bit masks.
- Finite binary64 values use the implementation's shortest round-trip decimal form.
  Exponents use lowercase `e`, no `+`, and no redundant leading zero. Negative zero
  is normalized to `0`.
- Non-finite numbers, unknown enum values, unknown mask bits, malformed quantity
  semantics, impossible sidecars, discontinuous ranges, and count/order mismatches
  fail the entire render.
- Optionals are their encoded value or JSON `null`; keys are never omitted.

Frame ranges are half-open `[begin,end)`. Physics and delivery ranges are absolute
from session creation. `audition_frame_range` is `null` during preparation and is
zero-based from the first audible delivery frame otherwise.

## Header record

The header keys are:

```text
record_type
schema
schema_version
simulation_request_identity_v7_sha256
engine_id
scenario_id
execution_kind
motion_mode
physics_rate_hz { numerator, denominator }
delivery_rate_hz { numerator, denominator }
physics_frames_per_block
delivery_frames_per_block
preparation_block_count
audible_block_count
total_block_count
```

`record_type` is `header`, `schema_version` is `1`, and `execution_kind` is
`finite_scenario`. Rates use quoted decimal-string 64-bit components. The request
identity binds the exact resolved engine, scenario, random plan, and provenance used
by the render.

Admitted `motion_mode` values are:

```text
held_speed
prescribed_kinematic_sweep
held_dyno
load_target_held_capture
inertial_dyno
free_engine
free_vehicle
```

## Block record

The block keys are:

```text
record_type
block_ordinal
phase
physics_frame_range { begin, end }
delivery_frame_range { begin, end }
audition_frame_range null | { begin, end }
endpoint_session_time_s
endpoint_scenario_time_s
telemetry
event_counters
```

`record_type` is `block`; `phase` is `preparation` or `audible`.
`endpoint_session_time_s` is measured from session creation.
`endpoint_scenario_time_s` is measured from the preparation/audible boundary, so
preparation endpoints are non-positive and audible endpoints are positive.

### Endpoint telemetry

The `telemetry` keys are:

```text
physics_step_end
mean_intake_manifold_pressure_pa_abs
engine
held_dyno
free_vehicle
```

`physics_step_end` and `engine.step_end_index` must both equal the block's exclusive
physics-frame end.

The `engine` keys are:

```text
step_end_index
validity_mask
theta_rad
theta_cycle_rad
angular_speed_rad_s
angular_acceleration_rad_s2
engine_speed_rpm
requested_throttle_01
resolved_engine_throttle_01
intake_plate_position_01
main_flow_multiplier_01
ignition_enabled
fuel_enabled
starter_enabled
dyno_enabled
limiter_enabled
limiter_cut_active
requested_external_resisting_torque_nm
torque
```

The `torque` object contains these eight `TorqueValueNm` objects, in order:

```text
instantaneous_indicated_gas
pumping_partition
friction_pump_and_accessory
starter
instantaneous_net_shaft
cycle_mean_net_shaft
actuator
dyno_reaction
```

It then contains these four `QuantityValue` objects:

```text
cycle_work_j
net_bmep_pa
instantaneous_power_w
cycle_mean_power_w
```

A `TorqueValueNm` has keys
`value_nm,availability,completeness,unavailable_reason,included_terms_mask,omitted_terms_mask`.
A `QuantityValue` has keys
`value,availability,completeness,unavailable_reason`.

Availability is `available` or `unavailable`; completeness is `complete` or
`incomplete`. Unavailable reasons are:

```text
none
scenario_not_applicable
model_not_admitted
equivalent_inertia_missing
cycle_integration_not_admitted
not_settled
required_input_missing
```

The existing quantity contract remains authoritative: unavailable values use
canonical positive zero, are incomplete, and carry a non-`none` reason; available
values carry reason `none`; included and omitted term masks are disjoint; complete
torque values omit no required known terms.

`held_dyno` is non-null only for `held_dyno` motion and contains:

```text
target_engine_speed_rpm
maximum_absorbing_torque_nm
maximum_driving_torque_nm
required_actuator_torque_nm
applied_actuator_torque_nm
disposition
```

Its disposition is `tracking`, `absorbing_torque_limited`, or
`driving_torque_limited`.

`free_vehicle` is non-null only for `free_vehicle` motion and contains:

```text
vehicle_speed_m_s
vehicle_distance_m
selected_forward_gear_ordinal
clutch_engagement_01
service_brake_application_01
clutch_disposition
clutch_torque_capacity_nm
applied_average_clutch_torque_on_engine_nm
final_clutch_slip_rad_s
road_load_disposition
requested_road_load_force_n
applied_average_road_load_force_n
```

The selected gear and final clutch slip are nullable. Clutch dispositions are
`neutral`, `disengaged`, `engine_driving_torque_limited`,
`vehicle_backdrive_torque_limited`, and `tracking`. Road-load dispositions are
`moving`, `stopped_within_step`, and `held_at_rest`.

### Event counters

`event_counters` contains this fixed inventory:

```text
total_event_record_count
spark_crossing_count
limiter_transition_count
limiter_activation_count
limiter_release_count
limiter_transition_overspeed_refreshed_count
ignition_accepted_count
ignition_rejected_active_flame_count
ignition_rejected_no_fuel_count
ignition_rejected_mixture_low_count
ignition_rejected_mixture_high_count
flame_extinguished_intake_transfer_count
flame_extinguished_no_geometric_progress_count
```

Every field is a quoted decimal-string 64-bit count. Apart from the two limiter
subcounts, the leaf counters partition `total_event_record_count` exactly.
Activation plus release equals transition count. The overspeed-refreshed field is a
subcount of limiter transitions whose existing journal record carries that flag. The
current engine journal does not create a record for every same-state timer refresh;
the truthful transition-qualified name is intentional. The raw event stream is not
published by this schema.

## Cycle record

A cycle record has keys `record_type,emitting_block_ordinal,cycle`, with
`record_type` equal to `cycle`. Its `cycle` object contains:

```text
completed_cycle_ordinal
start_boundary
end_boundary
duration_s
mean_engine_speed_rpm
requested_throttle
resolved_engine_throttle
intake_plate_position
instantaneous_net_shaft
start_state_flags_mask
end_state_flags_mask
state_transition_flags_mask
```

Each boundary contains, in order:

```text
cycle_ordinal
left_physics_frame
right_physics_frame
fraction_from_left_01
theta_unwrapped_rad
time_s
delivery_frame
```

`cycle_ordinal` is a quoted signed decimal string; frame fields are quoted unsigned
decimal strings. Delivery-frame crossings are fractional by design.

Each control object contains
`time_weighted_mean_01,minimum_01,maximum_01,change_count`. The raw finite values are
preserved; a `1e-12` evidence tolerance admits unavoidable accumulation roundoff at a
range boundary without modifying the serialized value.

`instantaneous_net_shaft` contains
`angular_work_j,cycle_mean_torque_nm,availability,completeness,unavailable_reason,included_terms_mask,omitted_terms_mask`
and follows the same canonical quantity/mask rules as endpoint torque.

The state masks cover ignition, fuel, starter, dyno, limiter-enabled, and
limiter-cut-active state. Transition bits report any committed change over the
cycle's half-open `(start,end]` interval.

## Footer record

The footer keys are:

```text
record_type
block_count
preparation_block_count
audible_block_count
cycle_count
event_totals
final_physics_frame_end
final_delivery_frame_end
final_audition_frame_end
```

`record_type` is `footer`. `event_totals` uses the exact block-counter schema and is
the checked sum of every block. Counts and final horizons must agree with the header
and all preceding records.

## Authoring-agent interpretation boundary

This artifact answers mechanical questions: what controls were requested and
resolved, what RPM/load/torque resulted, whether the engine stalled or encountered a
limiter, what lifecycle state was active, whether a dyno or drivetrain saturated,
and which quantities were available and complete. A consumer should reduce it to a
compact probe report rather than place an unrestricted trace in a model prompt.

It does not identify subjective timbre. Statements such as "not raspy enough" or
"too deep" also require deterministic analysis of the bound audition WAVE (signal
health, absolute-frequency bands, RPM-normalized orders, modulation/transients, and
candidate/reference deltas) plus a perceptual judgment. Crankwave deliberately emits
mechanical facts here and leaves product intent, scoring, iteration policy, and
subjective conclusions to the authoring system.
