#include "engine_sim_offline/responsive/finite_capture.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "session/projected_engine_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <numbers>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::responsive {
namespace {

using Status = std::optional<FiniteResponsiveCaptureError>;

[[nodiscard]] FiniteResponsiveCaptureError
error(FiniteResponsiveCaptureErrorCode code, std::string detail_code,
      std::string message,
      std::optional<EngineSessionError> session_error = std::nullopt) {
    return {code, std::move(detail_code), std::move(message),
            std::move(session_error)};
}

[[nodiscard]] Status invalid_payload(std::string detail_code,
                                     std::string message) {
    return error(FiniteResponsiveCaptureErrorCode::invalid_payload,
                 std::move(detail_code), std::move(message));
}

[[nodiscard]] bool checked_add(const std::uint64_t left,
                               const std::uint64_t right,
                               std::uint64_t &result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(const std::uint64_t left,
                                    const std::uint64_t right,
                                    std::uint64_t &result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool equal_block_durations(
    const contract::RationalRateHz physics_rate,
    const std::uint32_t physics_frames_per_block,
    const contract::RationalRateHz delivery_rate,
    const std::uint32_t delivery_frames_per_block) noexcept {
    std::uint64_t left = 0U;
    std::uint64_t right = 0U;
    return checked_multiply(physics_frames_per_block, physics_rate.denominator,
                            left) &&
           checked_multiply(left, delivery_rate.numerator, left) &&
           checked_multiply(delivery_frames_per_block, delivery_rate.denominator,
                            right) &&
           checked_multiply(right, physics_rate.numerator, right) && left == right;
}

[[nodiscard]] bool bit_equal(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool valid_motion_mode(const EngineMotionMode mode) noexcept {
    switch (mode) {
    case EngineMotionMode::held_speed:
    case EngineMotionMode::prescribed_kinematic_sweep:
    case EngineMotionMode::held_dyno:
    case EngineMotionMode::load_target_held_capture:
    case EngineMotionMode::inertial_dyno:
    case EngineMotionMode::free_engine:
    case EngineMotionMode::free_vehicle:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_bus_kind(const EngineAudioBusKind kind) noexcept {
    switch (kind) {
    case EngineAudioBusKind::source_route_dry:
    case EngineAudioBusKind::source_route_configured_transfer:
    case EngineAudioBusKind::source_route_selected:
    case EngineAudioBusKind::engine_raw_master:
    case EngineAudioBusKind::engine_audition_master:
        return true;
    }
    return false;
}

[[nodiscard]] bool
valid_signal_disposition(const EngineAudioSignalDisposition value) noexcept {
    switch (value) {
    case EngineAudioSignalDisposition::active:
    case EngineAudioSignalDisposition::declared_silent:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_source_route_kind(const contract::SourceRouteKind kind) {
    switch (kind) {
    case contract::SourceRouteKind::unspecified:
    case contract::SourceRouteKind::exhaust_outlet:
    case contract::SourceRouteKind::intake_inlet:
    case contract::SourceRouteKind::mechanical_engine:
    case contract::SourceRouteKind::mechanical_starter:
        return true;
    }
    return false;
}

[[nodiscard]] bool
same_descriptor(const EngineAudioBusDescriptor &left,
                const EngineAudioBusDescriptor &right) noexcept {
    return left.id == right.id && left.kind == right.kind &&
           left.source_route_kind == right.source_route_kind &&
           left.route_id == right.route_id &&
           left.signal_disposition == right.signal_disposition &&
           left.channel_count == right.channel_count &&
           left.sample_rate == right.sample_rate;
}

[[nodiscard]] ResponsiveCaptureBusDescriptor
own_descriptor(const EngineAudioBusDescriptor &descriptor) {
    return {
        std::string{descriptor.id}, descriptor.kind, descriptor.source_route_kind,
        descriptor.route_id,       descriptor.signal_disposition,
        descriptor.channel_count,  descriptor.sample_rate,
    };
}

[[nodiscard]] EngineCycleStateFlagMask
state_flags(const contract::EngineCaptureSample &sample) noexcept {
    EngineCycleStateFlagMask result = 0U;
    const auto set = [&result](const bool enabled, const EngineCycleStateFlag flag) {
        if (enabled) {
            result |= engine_cycle_state_flag_mask(flag);
        }
    };
    set(sample.ignition_enabled, EngineCycleStateFlag::ignition_enabled);
    set(sample.fuel_enabled, EngineCycleStateFlag::fuel_enabled);
    set(sample.starter_enabled, EngineCycleStateFlag::starter_enabled);
    set(sample.dyno_enabled, EngineCycleStateFlag::dyno_enabled);
    set(sample.limiter_enabled, EngineCycleStateFlag::limiter_enabled);
    set(sample.limiter_cut_active, EngineCycleStateFlag::limiter_cut_active);
    return result;
}

[[nodiscard]] constexpr EngineCycleStateFlagMask
known_state_flags() noexcept {
    return engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::starter_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
}

[[nodiscard]] ResponsiveCaptureEndpoint
make_endpoint(const std::uint64_t delivery_frame,
              const EngineTelemetryFrame &telemetry) noexcept {
    return {
        delivery_frame,
        telemetry.engine.engine_speed_rpm,
        telemetry.mean_intake_manifold_pressure_pa_abs,
        telemetry.engine.requested_throttle_01,
        telemetry.engine.resolved_engine_throttle_01,
        telemetry.engine.theta_rad / (2.0 * std::numbers::pi_v<double>),
        state_flags(telemetry.engine),
    };
}

[[nodiscard]] bool finite(const double value) noexcept {
    return std::isfinite(value);
}

template <class... Values>
[[nodiscard]] bool all_finite(Values... values) noexcept {
    return (finite(static_cast<double>(values)) && ...);
}

[[nodiscard]] bool valid_held_dyno_disposition(
    const EngineHeldDynoDisposition disposition) noexcept {
    switch (disposition) {
    case EngineHeldDynoDisposition::tracking:
    case EngineHeldDynoDisposition::absorbing_torque_limited:
    case EngineHeldDynoDisposition::driving_torque_limited:
        return true;
    }
    return false;
}

[[nodiscard]] bool
valid_clutch_disposition(const EngineClutchDisposition disposition) noexcept {
    switch (disposition) {
    case EngineClutchDisposition::neutral:
    case EngineClutchDisposition::disengaged:
    case EngineClutchDisposition::engine_driving_torque_limited:
    case EngineClutchDisposition::vehicle_backdrive_torque_limited:
    case EngineClutchDisposition::tracking:
        return true;
    }
    return false;
}

[[nodiscard]] bool
valid_road_load_disposition(const EngineRoadLoadDisposition disposition) noexcept {
    switch (disposition) {
    case EngineRoadLoadDisposition::moving:
    case EngineRoadLoadDisposition::stopped_within_step:
    case EngineRoadLoadDisposition::held_at_rest:
        return true;
    }
    return false;
}

[[nodiscard]] Status validate_telemetry(const EngineTelemetryFrame &frame,
                                        const EngineMotionMode motion_mode,
                                        const std::uint64_t physics_end) {
    constexpr auto known_validity =
        contract::capture_validity_mask(contract::CaptureValidity::mechanism) |
        contract::capture_validity_mask(
            contract::CaptureValidity::thermodynamic_state) |
        contract::capture_validity_mask(contract::CaptureValidity::composition) |
        contract::capture_validity_mask(contract::CaptureValidity::gas_exchange) |
        contract::capture_validity_mask(contract::CaptureValidity::combustion) |
        contract::capture_validity_mask(contract::CaptureValidity::torque);

    const auto &engine = frame.engine;
    if (frame.physics_step_end != physics_end ||
        engine.step_end_index != physics_end) {
        return invalid_payload(
            "responsive-capture-endpoint-step-mismatch",
            "telemetry step indices must equal the block's exclusive physics end");
    }
    if ((engine.validity & ~known_validity) != 0U ||
        !all_finite(frame.mean_intake_manifold_pressure_pa_abs, engine.theta_rad,
                    engine.theta_cycle_rad, engine.angular_speed_rad_s,
                    engine.angular_acceleration_rad_s2, engine.engine_speed_rpm,
                    engine.requested_throttle_01,
                    engine.resolved_engine_throttle_01,
                    engine.intake_plate_position_01,
                    engine.main_flow_multiplier_01,
                    engine.requested_external_resisting_torque_nm)) {
        return invalid_payload(
            "responsive-capture-endpoint-nonfinite",
            "endpoint telemetry contains a non-finite value or unknown validity bit");
    }
    // A stopped lifecycle endpoint is valid. Pressure and normalized controls still
    // retain their ordinary physical domains at zero engine speed.
    if (!(frame.mean_intake_manifold_pressure_pa_abs > 0.0) ||
        engine.theta_cycle_rad < 0.0 ||
        engine.theta_cycle_rad >= 4.0 * std::numbers::pi_v<double> ||
        engine.requested_throttle_01 < 0.0 ||
        engine.requested_throttle_01 > 1.0 ||
        engine.resolved_engine_throttle_01 < 0.0 ||
        engine.resolved_engine_throttle_01 > 1.0 ||
        engine.intake_plate_position_01 < 0.0 ||
        engine.intake_plate_position_01 > 1.0 ||
        engine.main_flow_multiplier_01 < 0.0 ||
        engine.main_flow_multiplier_01 > 1.0 ||
        engine.requested_external_resisting_torque_nm < 0.0) {
        return invalid_payload(
            "responsive-capture-endpoint-domain-invalid",
            "endpoint telemetry lies outside its public physical domain");
    }
    if (!contract::validate(engine.torque).ok()) {
        return invalid_payload("responsive-capture-torque-invalid",
                               "endpoint torque telemetry is malformed");
    }

    const auto &torque = engine.torque;
    const std::array torque_availability{
        torque.instantaneous_indicated_gas.availability,
        torque.pumping_partition.availability,
        torque.friction_pump_and_accessory.availability,
        torque.starter.availability,
        torque.instantaneous_net_shaft.availability,
        torque.cycle_mean_net_shaft.availability,
        torque.actuator.availability,
        torque.dyno_reaction.availability,
        torque.cycle_work_j.availability,
        torque.net_bmep_pa.availability,
        torque.instantaneous_power_w.availability,
        torque.cycle_mean_power_w.availability,
    };
    const bool any_torque_available =
        std::ranges::any_of(torque_availability, [](const auto availability) {
            return availability == contract::Availability::available;
        });
    const bool torque_valid =
        (engine.validity &
         contract::capture_validity_mask(contract::CaptureValidity::torque)) != 0U;
    if (torque_valid != any_torque_available) {
        return invalid_payload(
            "responsive-capture-torque-validity-mismatch",
            "endpoint torque validity disagrees with quantity availability");
    }

    if ((frame.held_dyno.has_value() &&
         motion_mode != EngineMotionMode::held_dyno) ||
        (frame.free_vehicle.has_value() &&
         motion_mode != EngineMotionMode::free_vehicle)) {
        return invalid_payload(
            "responsive-capture-sidecar-mode-mismatch",
            "endpoint telemetry sidecar disagrees with the session motion mode");
    }
    if (frame.held_dyno.has_value()) {
        const auto &dyno = *frame.held_dyno;
        if (!all_finite(dyno.target_engine_speed_rpm,
                        dyno.maximum_absorbing_torque_nm,
                        dyno.maximum_driving_torque_nm,
                        dyno.required_actuator_torque_nm,
                        dyno.applied_actuator_torque_nm) ||
            !valid_held_dyno_disposition(dyno.disposition) ||
            torque.actuator.availability != contract::Availability::available ||
            torque.dyno_reaction.availability !=
                contract::Availability::available ||
            !bit_equal(torque.actuator.value_nm,
                       dyno.applied_actuator_torque_nm) ||
            !bit_equal(torque.dyno_reaction.value_nm,
                       -dyno.applied_actuator_torque_nm)) {
            return invalid_payload("responsive-capture-held-dyno-invalid",
                                   "held-dyno endpoint sidecar is malformed");
        }
    }
    if (frame.free_vehicle.has_value()) {
        const auto &vehicle = *frame.free_vehicle;
        if (!all_finite(vehicle.vehicle_speed_m_s, vehicle.vehicle_distance_m,
                        vehicle.clutch_engagement_01,
                        vehicle.service_brake_application_01,
                        vehicle.clutch_torque_capacity_nm,
                        vehicle.applied_average_clutch_torque_on_engine_nm,
                        vehicle.requested_road_load_force_n,
                        vehicle.applied_average_road_load_force_n) ||
            (vehicle.final_clutch_slip_rad_s.has_value() &&
             !finite(*vehicle.final_clutch_slip_rad_s)) ||
            !valid_clutch_disposition(vehicle.clutch_disposition) ||
            !valid_road_load_disposition(vehicle.road_load_disposition)) {
            return invalid_payload("responsive-capture-free-vehicle-invalid",
                                   "free-vehicle endpoint sidecar is malformed");
        }
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_boundary(
    const EngineCycleBoundaryEvidence &boundary) {
    if (!all_finite(boundary.fraction_from_left_01,
                    boundary.theta_unwrapped_rad, boundary.time_s,
                    boundary.delivery_frame) ||
        boundary.fraction_from_left_01 < 0.0 ||
        boundary.fraction_from_left_01 > 1.0 ||
        boundary.right_physics_frame < boundary.left_physics_frame) {
        return invalid_payload("responsive-capture-cycle-boundary-invalid",
                               "completed-cycle boundary evidence is malformed");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_control(
    const EngineCycleControlEvidence &control) {
    constexpr double tolerance = 1.0e-12;
    if (!all_finite(control.time_weighted_mean_01, control.minimum_01,
                    control.maximum_01) ||
        control.minimum_01 < -tolerance ||
        control.maximum_01 > 1.0 + tolerance ||
        control.minimum_01 > control.maximum_01 + tolerance ||
        control.time_weighted_mean_01 < control.minimum_01 - tolerance ||
        control.time_weighted_mean_01 > control.maximum_01 + tolerance) {
        return invalid_payload("responsive-capture-cycle-control-invalid",
                               "completed-cycle control evidence is malformed");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_cycle_torque(
    const EngineCycleNetShaftEvidence &evidence) {
    const contract::TorqueValueNm work{
        evidence.angular_work_j,     evidence.availability,
        evidence.completeness,       evidence.unavailable_reason,
        evidence.included_terms,     evidence.omitted_terms,
    };
    const contract::TorqueValueNm mean{
        evidence.cycle_mean_torque_nm, evidence.availability,
        evidence.completeness,         evidence.unavailable_reason,
        evidence.included_terms,       evidence.omitted_terms,
    };
    if (!finite(evidence.angular_work_j) ||
        !finite(evidence.cycle_mean_torque_nm) ||
        !contract::validate(work).ok() || !contract::validate(mean).ok()) {
        return invalid_payload("responsive-capture-cycle-torque-invalid",
                               "completed-cycle torque evidence is malformed");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_cycle(
    const EngineCompletedCycleEvidence &cycle,
    const std::uint64_t expected_cycle_ordinal,
    const std::uint64_t block_physics_begin,
    const std::uint64_t block_physics_end) {
    if (cycle.completed_cycle_ordinal != expected_cycle_ordinal) {
        return invalid_payload(
            "responsive-capture-cycle-order-mismatch",
            "completed cycles must appear exactly once in contiguous ordinal order");
    }
    if (auto issue = validate_boundary(cycle.start_boundary)) {
        return issue;
    }
    if (auto issue = validate_boundary(cycle.end_boundary)) {
        return issue;
    }
    if (cycle.start_boundary.cycle_ordinal ==
            std::numeric_limits<std::int64_t>::max() ||
        cycle.end_boundary.cycle_ordinal !=
            cycle.start_boundary.cycle_ordinal + 1 ||
        cycle.end_boundary.right_physics_frame < block_physics_begin ||
        cycle.end_boundary.right_physics_frame >= block_physics_end ||
        !all_finite(cycle.duration_s, cycle.mean_engine_speed_rpm) ||
        !(cycle.duration_s > 0.0) || cycle.mean_engine_speed_rpm < 0.0) {
        return invalid_payload("responsive-capture-cycle-invalid",
                               "completed-cycle interval evidence is malformed");
    }
    if (auto issue = validate_control(cycle.requested_throttle)) {
        return issue;
    }
    if (auto issue = validate_control(cycle.resolved_engine_throttle)) {
        return issue;
    }
    if (auto issue = validate_control(cycle.intake_plate_position)) {
        return issue;
    }
    if (auto issue = validate_cycle_torque(cycle.instantaneous_net_shaft)) {
        return issue;
    }
    const auto masks = cycle.start_state_flags | cycle.end_state_flags |
                       cycle.state_transition_flags;
    if ((masks & ~known_state_flags()) != 0U) {
        return invalid_payload("responsive-capture-cycle-state-mask-invalid",
                               "completed-cycle state mask has an unknown bit");
    }
    return std::nullopt;
}

[[nodiscard]] Status
validate_event_counters(const EngineEventCounters &counters) {
    std::uint64_t partition = 0U;
    const std::array leaf_counts{
        counters.spark_crossing_count,
        counters.limiter_transition_count,
        counters.ignition_accepted_count,
        counters.ignition_rejected_active_flame_count,
        counters.ignition_rejected_no_fuel_count,
        counters.ignition_rejected_mixture_low_count,
        counters.ignition_rejected_mixture_high_count,
        counters.flame_extinguished_intake_transfer_count,
        counters.flame_extinguished_no_geometric_progress_count,
    };
    for (const auto count : leaf_counts) {
        if (!checked_add(partition, count, partition)) {
            return invalid_payload("responsive-capture-event-counter-overflow",
                                   "block event counter partition overflowed");
        }
    }
    std::uint64_t limiter_transitions = 0U;
    if (!checked_add(counters.limiter_activation_count,
                     counters.limiter_release_count, limiter_transitions) ||
        limiter_transitions != counters.limiter_transition_count ||
        counters.limiter_transition_overspeed_refreshed_count >
            counters.limiter_transition_count ||
        partition != counters.total_event_record_count) {
        return invalid_payload("responsive-capture-event-counter-mismatch",
                               "block event counters do not partition exactly");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_endpoint_projection(
    const ResponsiveCaptureEndpoint &endpoint,
    const EngineTelemetryFrame &telemetry,
    const std::uint64_t delivery_end) {
    const auto expected = make_endpoint(delivery_end, telemetry);
    if (endpoint.delivery_frame != expected.delivery_frame ||
        !all_finite(endpoint.engine_speed_rpm,
                    endpoint.mean_intake_manifold_pressure_pa_abs,
                    endpoint.requested_throttle_01,
                    endpoint.resolved_engine_throttle_01,
                    endpoint.unwrapped_crank_revolutions) ||
        !bit_equal(endpoint.engine_speed_rpm, expected.engine_speed_rpm) ||
        !bit_equal(endpoint.mean_intake_manifold_pressure_pa_abs,
                   expected.mean_intake_manifold_pressure_pa_abs) ||
        !bit_equal(endpoint.requested_throttle_01,
                   expected.requested_throttle_01) ||
        !bit_equal(endpoint.resolved_engine_throttle_01,
                   expected.resolved_engine_throttle_01) ||
        !bit_equal(endpoint.unwrapped_crank_revolutions,
                   expected.unwrapped_crank_revolutions) ||
        endpoint.state_flags != expected.state_flags) {
        return invalid_payload(
            "responsive-capture-endpoint-projection-mismatch",
            "reduced endpoint does not exactly project its full telemetry frame");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_descriptor_shape(
    const FiniteResponsiveCapture &capture) {
    if (!contract::is_valid_semantic_id(capture.engine_id) ||
        !contract::is_valid_semantic_id(capture.scenario_id)) {
        return invalid_payload("responsive-capture-identity-invalid",
                               "engine and scenario IDs must be semantic IDs");
    }
    if (!contract::validate(capture.physics_rate).ok() ||
        !contract::validate(capture.delivery_rate).ok() ||
        capture.physics_frames_per_block == 0U ||
        capture.delivery_frames_per_block == 0U ||
        capture.total_block_count == 0U ||
        capture.preparation_block_count >= capture.total_block_count ||
        !valid_motion_mode(capture.motion_mode) ||
        !equal_block_durations(capture.physics_rate,
                               capture.physics_frames_per_block,
                               capture.delivery_rate,
                               capture.delivery_frames_per_block)) {
        return invalid_payload("responsive-capture-descriptor-invalid",
                               "finite capture descriptor or block duration is invalid");
    }

    std::uint64_t total_physics = 0U;
    std::uint64_t total_delivery = 0U;
    std::uint64_t audible_first = 0U;
    if (!checked_multiply(capture.total_block_count,
                          capture.physics_frames_per_block, total_physics) ||
        !checked_multiply(capture.total_block_count,
                          capture.delivery_frames_per_block, total_delivery) ||
        !checked_multiply(capture.preparation_block_count,
                          capture.delivery_frames_per_block, audible_first) ||
        audible_first > total_delivery ||
        capture.total_physics_frame_count != total_physics ||
        capture.total_delivery_frame_count != total_delivery ||
        capture.audible_first_delivery_frame != audible_first ||
        capture.audible_delivery_frame_count != total_delivery - audible_first) {
        return invalid_payload("responsive-capture-horizon-mismatch",
                               "capture frame horizons disagree with block counts");
    }
    if (capture.capacities.maximum_delivery_frames_per_process_call <
            capture.delivery_frames_per_block ||
        capture.capacities.maximum_telemetry_frames_per_process_call < 1U) {
        return invalid_payload("responsive-capture-capacity-mismatch",
                               "compiled capacities cannot carry a native block");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_bus_descriptor(
    const ResponsiveCaptureBusDescriptor &descriptor,
    const contract::RationalRateHz delivery_rate) {
    if (descriptor.id.empty() || !valid_bus_kind(descriptor.kind) ||
        !valid_source_route_kind(descriptor.source_route_kind) ||
        !valid_signal_disposition(descriptor.signal_disposition) ||
        descriptor.channel_count == 0U || descriptor.sample_rate != delivery_rate) {
        return invalid_payload("responsive-capture-bus-descriptor-invalid",
                               "captured audio bus descriptor is malformed");
    }
    return std::nullopt;
}

[[nodiscard]] FiniteResponsiveCaptureResult capture_impl(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> selected_bus_ids,
    const std::stop_token cancellation,
    const bool dry_route_projection) {
    if (selected_bus_ids.empty()) {
        return error(FiniteResponsiveCaptureErrorCode::invalid_request,
                     "responsive-capture-empty-bus-selection",
                     "at least one audio bus must be selected");
    }
    for (std::size_t index = 0U; index < selected_bus_ids.size(); ++index) {
        if (selected_bus_ids[index].empty() ||
            std::ranges::find(selected_bus_ids.begin(),
                              selected_bus_ids.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              selected_bus_ids[index]) !=
                selected_bus_ids.begin() + static_cast<std::ptrdiff_t>(index)) {
            return error(FiniteResponsiveCaptureErrorCode::invalid_request,
                         "responsive-capture-bus-selection-invalid",
                         "selected bus IDs must be nonempty and unique");
        }
    }
    if (cancellation.stop_requested()) {
        return error(FiniteResponsiveCaptureErrorCode::cancelled,
                     "responsive-capture-cancelled",
                     "capture was cancelled before session creation");
    }

    if (dry_route_projection) {
        const auto inputs =
            compile::detail::CompiledScenarioViewAccess::inputs(scenario);
        if (!std::holds_alternative<contract::HeldSpeed>(
                inputs.scenario.scenario.mode) &&
            !std::holds_alternative<contract::HeldDyno>(
                inputs.scenario.scenario.mode)) {
            return error(
                FiniteResponsiveCaptureErrorCode::invalid_request,
                "responsive-dry-projection-requires-held-capture",
                "dry presentation projection is admitted only for held or "
                "directional finite responsive capture");
        }
    }

    auto created =
        dry_route_projection
            ? session_detail::create_dry_projected_engine_session(
                  scenario, selected_bus_ids)
            : create_engine_session(scenario,
                                    EngineSessionExecutionKind::finite_scenario);
    if (auto *session_error = std::get_if<EngineSessionError>(&created)) {
        if (dry_route_projection &&
            session_error->detail_code.starts_with("session-dry-projection-")) {
            auto detail_code = session_error->detail_code;
            auto message = session_error->message;
            return error(FiniteResponsiveCaptureErrorCode::invalid_request,
                         std::move(detail_code), std::move(message),
                         std::move(*session_error));
        }
        return error(FiniteResponsiveCaptureErrorCode::session_failed,
                     "responsive-capture-session-create-failed",
                     "finite source session creation failed",
                     std::move(*session_error));
    }
    auto session = std::get<EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    const auto compiled_engine = scenario.engine();

    if (descriptor.execution_kind != EngineSessionExecutionKind::finite_scenario ||
        descriptor.engine_id != compiled_engine.id() ||
        descriptor.scenario_id != scenario.id() || descriptor.audio_buses.empty() ||
        descriptor.total_block_count == 0U ||
        descriptor.preparation_block_count >= descriptor.total_block_count ||
        descriptor.physics_frames_per_block == 0U ||
        descriptor.delivery_frames_per_block == 0U ||
        !contract::validate(descriptor.physics_rate).ok() ||
        !contract::validate(descriptor.delivery_rate).ok() ||
        !valid_motion_mode(descriptor.motion_mode) ||
        !equal_block_durations(descriptor.physics_rate,
                               descriptor.physics_frames_per_block,
                               descriptor.delivery_rate,
                               descriptor.delivery_frames_per_block)) {
        return error(FiniteResponsiveCaptureErrorCode::invalid_session,
                     "responsive-capture-session-descriptor-invalid",
                     "session descriptor is not a valid finite capture horizon");
    }

    for (std::size_t index = 0U; index < descriptor.audio_buses.size(); ++index) {
        const auto &bus = descriptor.audio_buses[index];
        if (bus.id.empty() || !valid_bus_kind(bus.kind) ||
            !valid_source_route_kind(bus.source_route_kind) ||
            !valid_signal_disposition(bus.signal_disposition) ||
            bus.channel_count == 0U || bus.sample_rate != descriptor.delivery_rate ||
            std::ranges::find(descriptor.audio_buses.begin(),
                              descriptor.audio_buses.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              bus.id, &EngineAudioBusDescriptor::id) !=
                descriptor.audio_buses.begin() +
                    static_cast<std::ptrdiff_t>(index)) {
            return error(FiniteResponsiveCaptureErrorCode::invalid_session,
                         "responsive-capture-session-bus-descriptor-invalid",
                         "session audio bus descriptors must be valid and unique");
        }
    }

    std::uint64_t total_physics = 0U;
    std::uint64_t total_delivery = 0U;
    std::uint64_t audible_first = 0U;
    if (!checked_multiply(descriptor.total_block_count,
                          descriptor.physics_frames_per_block, total_physics) ||
        !checked_multiply(descriptor.total_block_count,
                          descriptor.delivery_frames_per_block, total_delivery) ||
        !checked_multiply(descriptor.preparation_block_count,
                          descriptor.delivery_frames_per_block, audible_first) ||
        audible_first >= total_delivery ||
        descriptor.total_block_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        return error(FiniteResponsiveCaptureErrorCode::invalid_session,
                     "responsive-capture-session-horizon-overflow",
                     "session frame horizon is not addressable");
    }
    const auto audible_count = total_delivery - audible_first;

    std::vector<EngineAudioBusDescriptor> selected;
    selected.reserve(selected_bus_ids.size());
    for (const auto id : selected_bus_ids) {
        const auto found = std::ranges::find(
            descriptor.audio_buses, id, &EngineAudioBusDescriptor::id);
        if (found == descriptor.audio_buses.end()) {
            std::string available;
            for (const auto &bus : descriptor.audio_buses) {
                if (!available.empty()) {
                    available += ", ";
                }
                available += bus.id;
            }
            return error(FiniteResponsiveCaptureErrorCode::invalid_request,
                         "responsive-capture-bus-not-found",
                         "selected audio bus '" + std::string{id} +
                             "' is absent; available buses: " + available);
        }
        std::uint64_t sample_count = 0U;
        if (!checked_multiply(audible_count, found->channel_count, sample_count) ||
            sample_count > static_cast<std::uint64_t>(
                               std::numeric_limits<std::size_t>::max())) {
            return error(FiniteResponsiveCaptureErrorCode::invalid_session,
                         "responsive-capture-pcm-horizon-overflow",
                         "selected audio bus tape is not addressable");
        }
        selected.push_back(*found);
    }

    FiniteResponsiveCapture capture;
    capture.engine_id = descriptor.engine_id;
    capture.scenario_id = descriptor.scenario_id;
    capture.capacities = descriptor.capacities;
    capture.physics_rate = descriptor.physics_rate;
    capture.delivery_rate = descriptor.delivery_rate;
    capture.physics_frames_per_block = descriptor.physics_frames_per_block;
    capture.delivery_frames_per_block = descriptor.delivery_frames_per_block;
    capture.total_block_count = descriptor.total_block_count;
    capture.preparation_block_count = descriptor.preparation_block_count;
    capture.total_physics_frame_count = total_physics;
    capture.total_delivery_frame_count = total_delivery;
    capture.audible_first_delivery_frame = audible_first;
    capture.audible_delivery_frame_count = audible_count;
    capture.live_control_capabilities = descriptor.live_control_capabilities;
    capture.motion_mode = descriptor.motion_mode;
    capture.engine_provenance = compiled_engine.provenance();
    capture.scenario_provenance = scenario.provenance();

    capture.forward_gears.reserve(descriptor.forward_gears.size());
    for (const auto &gear : descriptor.forward_gears) {
        capture.forward_gears.push_back(
            {gear.id, gear.authored_ordinal, std::string{gear.semantic_id}, gear.ratio});
    }
    capture.selected_bus_ids.reserve(selected_bus_ids.size());
    capture.buses.reserve(selected_bus_ids.size());
    for (std::size_t index = 0U; index < selected_bus_ids.size(); ++index) {
        capture.selected_bus_ids.emplace_back(selected_bus_ids[index]);
        ResponsiveCaptureBus bus;
        bus.descriptor = own_descriptor(selected[index]);
        std::uint64_t sample_count = 0U;
        static_cast<void>(checked_multiply(audible_count,
                                           selected[index].channel_count,
                                           sample_count));
        bus.audible_interleaved_samples.reserve(
            static_cast<std::size_t>(sample_count));
        capture.buses.push_back(std::move(bus));
    }
    capture.blocks.reserve(static_cast<std::size_t>(descriptor.total_block_count));

    std::uint64_t processed_blocks = 0U;
    std::uint64_t completed_cycles = 0U;
    while (true) {
        if (cancellation.stop_requested()) {
            return error(FiniteResponsiveCaptureErrorCode::cancelled,
                         "responsive-capture-cancelled",
                         "capture was cancelled at a native block boundary");
        }
        auto next = session.process_block();
        if (auto *session_error = std::get_if<EngineSessionError>(&next)) {
            return error(FiniteResponsiveCaptureErrorCode::session_failed,
                         "responsive-capture-session-process-failed",
                         "finite source session failed during capture",
                         std::move(*session_error));
        }
        if (auto *completed = std::get_if<EngineSessionCompleted>(&next)) {
            capture.completion = std::move(*completed);
            if (processed_blocks != descriptor.total_block_count) {
                return error(FiniteResponsiveCaptureErrorCode::invalid_session,
                             "responsive-capture-session-ended-early",
                             "finite session completed before its declared horizon");
            }
            if (auto issue = validate_finite_responsive_capture(capture)) {
                return std::move(*issue);
            }
            return capture;
        }
        if (processed_blocks >= descriptor.total_block_count) {
            return error(FiniteResponsiveCaptureErrorCode::invalid_block,
                         "responsive-capture-session-overlong",
                         "finite session exceeded its declared block horizon");
        }

        const auto &block = std::get<EngineSessionBlockView>(next);
        const auto expected_phase =
            processed_blocks < descriptor.preparation_block_count
                ? EngineSessionBlockPhase::preparation
                : EngineSessionBlockPhase::audible;
        std::uint64_t expected_physics_begin = 0U;
        std::uint64_t expected_delivery_begin = 0U;
        std::uint64_t physics_end = 0U;
        std::uint64_t delivery_end = 0U;
        if (!checked_multiply(processed_blocks,
                              descriptor.physics_frames_per_block,
                              expected_physics_begin) ||
            !checked_multiply(processed_blocks,
                              descriptor.delivery_frames_per_block,
                              expected_delivery_begin) ||
            !checked_add(expected_physics_begin,
                         descriptor.physics_frames_per_block, physics_end) ||
            !checked_add(expected_delivery_begin,
                         descriptor.delivery_frames_per_block, delivery_end) ||
            block.block_ordinal() != processed_blocks ||
            block.phase() != expected_phase ||
            block.first_physics_frame() != expected_physics_begin ||
            block.physics_frame_count() != descriptor.physics_frames_per_block ||
            block.first_delivery_frame() != expected_delivery_begin ||
            block.delivery_frame_count() != descriptor.delivery_frames_per_block ||
            block.telemetry().size() != 1U) {
            return error(FiniteResponsiveCaptureErrorCode::invalid_block,
                         "responsive-capture-block-clock-invalid",
                         "session block clock, phase, or endpoint count is invalid");
        }

        const auto &telemetry = block.telemetry().front();
        if (auto issue =
                validate_telemetry(telemetry, descriptor.motion_mode, physics_end)) {
            issue->code = FiniteResponsiveCaptureErrorCode::invalid_block;
            return std::move(*issue);
        }
        if (auto issue = validate_event_counters(block.event_counters())) {
            issue->code = FiniteResponsiveCaptureErrorCode::invalid_block;
            return std::move(*issue);
        }
        for (const auto &cycle : block.cycle_evidence()) {
            if (auto issue = validate_cycle(cycle, completed_cycles,
                                            expected_physics_begin, physics_end)) {
                issue->code = FiniteResponsiveCaptureErrorCode::invalid_block;
                return std::move(*issue);
            }
            if (completed_cycles == std::numeric_limits<std::uint64_t>::max()) {
                return error(FiniteResponsiveCaptureErrorCode::invalid_block,
                             "responsive-capture-cycle-count-overflow",
                             "completed cycle count overflowed uint64");
            }
            ++completed_cycles;
        }

        for (std::size_t index = 0U; index < selected.size(); ++index) {
            const auto &expected = selected[index];
            const auto found = std::ranges::find(
                block.audio_buses(), expected.id,
                [](const EngineAudioBusBlockView &bus) {
                    return bus.descriptor.id;
                });
            if (found == block.audio_buses().end() ||
                std::ranges::count(block.audio_buses(), expected.id,
                                   [](const EngineAudioBusBlockView &bus) {
                                       return bus.descriptor.id;
                                   }) != 1 ||
                !same_descriptor(found->descriptor, expected)) {
                return error(FiniteResponsiveCaptureErrorCode::invalid_block,
                             "responsive-capture-block-bus-invalid",
                             "selected audio bus is missing, duplicated, or changed");
            }
            std::uint64_t expected_samples = 0U;
            if (!checked_multiply(block.delivery_frame_count(),
                                  expected.channel_count, expected_samples) ||
                expected_samples != found->samples.size() ||
                !std::ranges::all_of(found->samples,
                                     [](const float value) {
                                         return std::isfinite(value);
                                     })) {
                return error(FiniteResponsiveCaptureErrorCode::invalid_payload,
                             "responsive-capture-block-pcm-invalid",
                             "selected audio bus block has malformed or non-finite PCM");
            }
            if (expected_phase == EngineSessionBlockPhase::audible) {
                auto &tape = capture.buses[index].audible_interleaved_samples;
                tape.insert(tape.end(), found->samples.begin(), found->samples.end());
            }
        }

        ResponsiveCaptureBlock owned_block;
        owned_block.block_ordinal = block.block_ordinal();
        owned_block.phase = block.phase();
        owned_block.first_physics_frame = block.first_physics_frame();
        owned_block.physics_frame_count = block.physics_frame_count();
        owned_block.first_delivery_frame = block.first_delivery_frame();
        owned_block.delivery_frame_count = block.delivery_frame_count();
        owned_block.endpoint = make_endpoint(delivery_end, telemetry);
        owned_block.telemetry = telemetry;
        owned_block.completed_cycles.assign(block.cycle_evidence().begin(),
                                            block.cycle_evidence().end());
        owned_block.event_counters = block.event_counters();
        capture.blocks.push_back(std::move(owned_block));
        ++processed_blocks;
    }
}

} // namespace

std::optional<FiniteResponsiveCaptureError>
validate_finite_responsive_capture(const FiniteResponsiveCapture &capture) {
    if (auto issue = validate_descriptor_shape(capture)) {
        return issue;
    }
    if (!contract::validate(capture.engine_provenance).ok() ||
        !contract::validate(capture.scenario_provenance).ok()) {
        return invalid_payload("responsive-capture-provenance-invalid",
                               "compiler provenance ledger is malformed");
    }
    if (capture.forward_gears.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return invalid_payload("responsive-capture-forward-gears-invalid",
                               "forward gear inventory is not representable");
    }
    for (std::size_t index = 0U; index < capture.forward_gears.size(); ++index) {
        const auto &gear = capture.forward_gears[index];
        if (!gear.id.valid() || !contract::is_valid_semantic_id(gear.semantic_id) ||
            !finite(gear.ratio) || gear.ratio <= 0.0 ||
            std::ranges::find(capture.forward_gears.begin(),
                              capture.forward_gears.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              gear.authored_ordinal,
                              &ResponsiveCaptureForwardGear::authored_ordinal) !=
                capture.forward_gears.begin() +
                    static_cast<std::ptrdiff_t>(index)) {
            return invalid_payload("responsive-capture-forward-gears-invalid",
                                   "forward gear inventory is malformed or duplicated");
        }
    }

    if (capture.selected_bus_ids.empty() ||
        capture.selected_bus_ids.size() != capture.buses.size()) {
        return invalid_payload("responsive-capture-bus-shape-mismatch",
                               "selected bus IDs and captured tapes disagree");
    }
    for (std::size_t index = 0U; index < capture.selected_bus_ids.size(); ++index) {
        const auto &id = capture.selected_bus_ids[index];
        const auto &bus = capture.buses[index];
        if (id.empty() || id != bus.descriptor.id ||
            std::ranges::find(capture.selected_bus_ids.begin(),
                              capture.selected_bus_ids.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              id) !=
                capture.selected_bus_ids.begin() +
                    static_cast<std::ptrdiff_t>(index)) {
            return invalid_payload("responsive-capture-bus-order-invalid",
                                   "selected bus order is malformed or duplicated");
        }
        if (auto issue =
                validate_bus_descriptor(bus.descriptor, capture.delivery_rate)) {
            return issue;
        }
        std::uint64_t expected_samples = 0U;
        if (!checked_multiply(capture.audible_delivery_frame_count,
                              bus.descriptor.channel_count, expected_samples) ||
            expected_samples > static_cast<std::uint64_t>(
                                   std::numeric_limits<std::size_t>::max()) ||
            bus.audible_interleaved_samples.size() !=
                static_cast<std::size_t>(expected_samples) ||
            !std::ranges::all_of(bus.audible_interleaved_samples,
                                 [](const float value) {
                                     return std::isfinite(value);
                                 })) {
            return invalid_payload("responsive-capture-pcm-invalid",
                                   "audible bus tape is incomplete or non-finite");
        }
    }

    if (capture.total_block_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        capture.blocks.size() !=
            static_cast<std::size_t>(capture.total_block_count)) {
        return invalid_payload("responsive-capture-block-count-mismatch",
                               "retained blocks do not fill the declared horizon");
    }
    std::uint64_t expected_cycle_ordinal = 0U;
    for (std::size_t index = 0U; index < capture.blocks.size(); ++index) {
        const auto &block = capture.blocks[index];
        const auto ordinal = static_cast<std::uint64_t>(index);
        const auto expected_phase =
            ordinal < capture.preparation_block_count
                ? EngineSessionBlockPhase::preparation
                : EngineSessionBlockPhase::audible;
        std::uint64_t physics_begin = 0U;
        std::uint64_t delivery_begin = 0U;
        std::uint64_t physics_end = 0U;
        std::uint64_t delivery_end = 0U;
        if (!checked_multiply(ordinal, capture.physics_frames_per_block,
                              physics_begin) ||
            !checked_multiply(ordinal, capture.delivery_frames_per_block,
                              delivery_begin) ||
            !checked_add(physics_begin, capture.physics_frames_per_block,
                         physics_end) ||
            !checked_add(delivery_begin, capture.delivery_frames_per_block,
                         delivery_end) ||
            block.block_ordinal != ordinal || block.phase != expected_phase ||
            block.first_physics_frame != physics_begin ||
            block.physics_frame_count != capture.physics_frames_per_block ||
            block.first_delivery_frame != delivery_begin ||
            block.delivery_frame_count != capture.delivery_frames_per_block) {
            return invalid_payload("responsive-capture-block-order-invalid",
                                   "retained block ranges are not contiguous");
        }
        if (auto issue =
                validate_telemetry(block.telemetry, capture.motion_mode,
                                   physics_end)) {
            return issue;
        }
        if (auto issue = validate_endpoint_projection(block.endpoint,
                                                      block.telemetry,
                                                      delivery_end)) {
            return issue;
        }
        if (auto issue = validate_event_counters(block.event_counters)) {
            return issue;
        }
        for (const auto &cycle : block.completed_cycles) {
            if (auto issue = validate_cycle(cycle, expected_cycle_ordinal,
                                            physics_begin, physics_end)) {
                return issue;
            }
            if (expected_cycle_ordinal ==
                std::numeric_limits<std::uint64_t>::max()) {
                return invalid_payload("responsive-capture-cycle-count-overflow",
                                       "completed cycle count overflowed uint64");
            }
            ++expected_cycle_ordinal;
        }
    }

    if (capture.completion.block_count != capture.total_block_count ||
        capture.completion.physics_frame_count !=
            capture.total_physics_frame_count ||
        capture.completion.delivery_frame_count !=
            capture.total_delivery_frame_count ||
        capture.completion.live_controls_accepted) {
        return invalid_payload("responsive-capture-completion-mismatch",
                               "session completion disagrees with capture horizons");
    }
    if (capture.completion.held_speed_operating_point.has_value() &&
        !contract::validate(
             *capture.completion.held_speed_operating_point).ok()) {
        return invalid_payload("responsive-capture-held-result-invalid",
                               "held-speed terminal result is malformed");
    }
    if (capture.completion.inertial_dyno.has_value() &&
        !contract::validate(*capture.completion.inertial_dyno).ok()) {
        return invalid_payload("responsive-capture-dyno-result-invalid",
                               "inertial-dyno terminal result is malformed");
    }
    return std::nullopt;
}

FiniteResponsiveCaptureResult capture_finite_responsive_session(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> selected_bus_ids,
    const std::stop_token cancellation) {
    try {
        return capture_impl(scenario, selected_bus_ids, cancellation, false);
    } catch (const std::bad_alloc &) {
        return error(FiniteResponsiveCaptureErrorCode::resource_exhausted,
                     "responsive-capture-allocation-failed",
                     "finite responsive capture exhausted memory");
    } catch (const std::exception &caught) {
        return error(FiniteResponsiveCaptureErrorCode::internal_error,
                     "responsive-capture-exception",
                     "finite responsive capture threw: " +
                         std::string{caught.what()});
    } catch (...) {
        return error(FiniteResponsiveCaptureErrorCode::internal_error,
                     "responsive-capture-unknown-exception",
                     "finite responsive capture threw a non-standard exception");
    }
}

FiniteResponsiveCaptureResult capture_finite_responsive_dry_routes(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> selected_bus_ids,
    const std::stop_token cancellation) {
    try {
        return capture_impl(scenario, selected_bus_ids, cancellation, true);
    } catch (const std::bad_alloc &) {
        return error(FiniteResponsiveCaptureErrorCode::resource_exhausted,
                     "responsive-capture-allocation-failed",
                     "finite responsive capture exhausted memory");
    } catch (const std::exception &caught) {
        return error(FiniteResponsiveCaptureErrorCode::internal_error,
                     "responsive-capture-exception",
                     "finite responsive capture threw: " +
                         std::string{caught.what()});
    } catch (...) {
        return error(FiniteResponsiveCaptureErrorCode::internal_error,
                     "responsive-capture-unknown-exception",
                     "finite responsive capture threw a non-standard exception");
    }
}

} // namespace engine_sim_offline::responsive
