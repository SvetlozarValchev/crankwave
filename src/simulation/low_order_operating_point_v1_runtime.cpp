#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

[[nodiscard]] contract::TorqueValueNm
available_torque(double value_nm, contract::TorqueTermMask included_terms) noexcept {
    return {
        value_nm,
        contract::Availability::available,
        contract::Completeness::complete,
        contract::QuantityUnavailableReason::none,
        included_terms,
        0,
    };
}

[[nodiscard]] contract::TorqueValueNm
unavailable_torque(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
        0,
        0,
    };
}

[[nodiscard]] contract::QuantityValue
unavailable_quantity(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
    };
}

[[nodiscard]] contract::TorqueTelemetry
operating_capture_torque(double indicated_gas_torque_nm) noexcept {
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        indicated_gas_torque_nm, contract::indicated_gas_torque_term_mask());
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.starter = available_torque(
        0.0, contract::torque_term_mask(contract::TorqueTerm::starter));
    result.instantaneous_net_shaft =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.cycle_mean_net_shaft =
        unavailable_torque(contract::QuantityUnavailableReason::required_input_missing);
    result.actuator = unavailable_torque(
        contract::QuantityUnavailableReason::equivalent_inertia_missing);
    result.dyno_reaction = unavailable_torque(
        contract::QuantityUnavailableReason::equivalent_inertia_missing);
    result.cycle_work_j = unavailable_quantity(
        contract::QuantityUnavailableReason::required_input_missing);
    result.net_bmep_pa = unavailable_quantity(
        contract::QuantityUnavailableReason::required_input_missing);
    result.instantaneous_power_w =
        unavailable_quantity(contract::QuantityUnavailableReason::model_not_admitted);
    result.cycle_mean_power_w = unavailable_quantity(
        contract::QuantityUnavailableReason::required_input_missing);
    return result;
}

[[nodiscard]] contract::OperatingPointBoundaryEvidence
public_boundary(const AdjacentCycleBlockBoundary &boundary) {
    return {
        boundary.interpolation.left_bracket_sample_index,
        boundary.interpolation.right_bracket_sample_index,
        boundary.interpolation.fraction_from_left_01,
        boundary.time_s,
        boundary.theta_rad,
    };
}

[[nodiscard]] contract::TorqueValueNm
complete_cycle_mean_torque(double value_nm, contract::TorqueTermMask terms) noexcept {
    return available_torque(value_nm, terms);
}

[[nodiscard]] contract::HeldSpeedCycleBlockEvidence
public_block(const AdjacentCycleBlockMean &block,
             std::span<const AdjacentCycleBlockPressureMeans> pressure_means,
             bool pressure_block_b, std::uint32_t cycles_per_block,
             double total_displacement_m3) {
    const double cycle_count = static_cast<double>(cycles_per_block);
    const double angle_range = cycle_count * kFourStrokeCycleRadians;
    const double displacement_range = cycle_count * total_displacement_m3;
    const double duration_s =
        block.range.end_boundary.time_s - block.range.start_boundary.time_s;

    std::vector<contract::MeanBoundaryPressurePa> pressures;
    pressures.reserve(pressure_means.size());
    for (const auto &pressure : pressure_means) {
        pressures.push_back({
            pressure.gas_volume_id,
            pressure_block_b ? pressure.block_b_mean_pressure_pa_abs
                             : pressure.block_a_mean_pressure_pa_abs,
        });
    }

    std::vector<contract::HeldSpeedCompletedCycleEvidence> completed_cycles;
    completed_cycles.reserve(block.completed_cycles.size());
    for (const auto &cycle : block.completed_cycles) {
        contract::HeldSpeedCompletedCycleEvidence completed_cycle{
            cycle.completed_cycle_ordinal,
            cycle.indicated_gas_work_j,
            cycle.positive_aggregate_loss_work_j,
            cycle.starter_work_j,
            cycle.brake_work_j,
            {},
        };
        completed_cycle.end_boundary_pressures.reserve(
            cycle.end_boundary_pressures.size());
        for (const auto &pressure : cycle.end_boundary_pressures) {
            completed_cycle.end_boundary_pressures.push_back({
                pressure.gas_volume_id,
                pressure.pressure_pa_abs,
            });
        }
        completed_cycles.push_back(std::move(completed_cycle));
    }

    return {
        {
            cycles_per_block,
            block.range.first_cycle_ordinal,
            block.range.last_cycle_ordinal,
            public_boundary(block.range.start_boundary),
            public_boundary(block.range.end_boundary),
        },
        std::move(completed_cycles),
        block.total_indicated_gas_work_j,
        block.total_positive_aggregate_loss_work_j,
        block.total_starter_work_j,
        block.total_brake_work_j,
        {
            complete_cycle_mean_torque(block.total_indicated_gas_work_j / angle_range,
                                       contract::indicated_gas_torque_term_mask()),
            complete_cycle_mean_torque(
                -block.total_positive_aggregate_loss_work_j / angle_range,
                contract::friction_pump_and_accessory_torque_term_mask()),
            complete_cycle_mean_torque(
                block.total_starter_work_j / angle_range,
                contract::torque_term_mask(contract::TorqueTerm::starter)),
            complete_cycle_mean_torque(block.total_brake_work_j / angle_range,
                                       contract::known_torque_term_mask()),
        },
        block.total_brake_work_j / displacement_range,
        block.total_brake_work_j / duration_s,
        std::move(pressures),
    };
}

[[nodiscard]] contract::HeldSpeedOperatingPointResult public_operating_point(
    const AdjacentCycleBlockConvergenceEvidence &evidence,
    const contract::Sha256Digest &simulation_request_identity_v2_sha256,
    const contract::HeldSpeedOperatingPointConditions &conditions) {
    const auto block_a =
        public_block(evidence.block_a, evidence.pressure_means, false,
                     evidence.cycles_per_block, conditions.total_displacement_m3);
    const auto block_b =
        public_block(evidence.block_b, evidence.pressure_means, true,
                     evidence.cycles_per_block, conditions.total_displacement_m3);
    return {
        simulation_request_identity_v2_sha256,
        conditions,
        std::string{contract::kGenericChenFlynnLowOrderModelPredictionApplicability},
        {
            evidence.method,
            evidence.cycles_per_block,
            evidence.eligibility_threshold_time_s,
            evidence.fixed_cutoff_time_s,
            std::move(block_a),
            std::move(block_b),
            evidence.block_b.range.last_cycle_ordinal,
            public_boundary(evidence.block_b.range.end_boundary),
            evidence.torque_residual_nm,
            evidence.cycle_mean_torque_tolerance_nm,
            evidence.pressure_residual_pa,
            evidence.pressure_tolerance_pa,
            evidence.limiting_gas_volume_id,
        },
    };
}

[[nodiscard]] contract::FailureKind
accounting_failure_kind(OperatingCycleAccountingErrorCode code) noexcept {
    switch (code) {
    case OperatingCycleAccountingErrorCode::nonpositive_pressure:
        return contract::FailureKind::nonphysical_state;
    case OperatingCycleAccountingErrorCode::nonfinite_pressure:
    case OperatingCycleAccountingErrorCode::quadrature_failure:
    case OperatingCycleAccountingErrorCode::aggregate_loss_failure:
    case OperatingCycleAccountingErrorCode::nonfinite_result:
        return contract::FailureKind::numerical_failure;
    case OperatingCycleAccountingErrorCode::invalid_plan:
    case OperatingCycleAccountingErrorCode::invalid_term_partition:
    case OperatingCycleAccountingErrorCode::invalid_cylinder_plan:
    case OperatingCycleAccountingErrorCode::invalid_gas_volume_plan:
    case OperatingCycleAccountingErrorCode::malformed_sample:
    case OperatingCycleAccountingErrorCode::engine_speed_mismatch:
    case OperatingCycleAccountingErrorCode::nonzero_placeholder_work:
    case OperatingCycleAccountingErrorCode::accounting_invariant_violation:
    case OperatingCycleAccountingErrorCode::moved_from:
        return contract::FailureKind::contract_violation;
    }
    return contract::FailureKind::contract_violation;
}

[[nodiscard]] std::string_view
accounting_detail_code(OperatingCycleAccountingErrorCode code) noexcept {
    switch (code) {
    case OperatingCycleAccountingErrorCode::nonpositive_pressure:
        return "operating-cycle-pressure-nonpositive";
    case OperatingCycleAccountingErrorCode::nonfinite_pressure:
        return "operating-cycle-pressure-nonfinite";
    case OperatingCycleAccountingErrorCode::quadrature_failure:
        return "operating-cycle-quadrature-failed";
    case OperatingCycleAccountingErrorCode::aggregate_loss_failure:
        return "operating-cycle-aggregate-loss-failed";
    case OperatingCycleAccountingErrorCode::nonfinite_result:
        return "operating-cycle-result-nonfinite";
    case OperatingCycleAccountingErrorCode::engine_speed_mismatch:
        return "operating-cycle-held-speed-disagreed";
    case OperatingCycleAccountingErrorCode::nonzero_placeholder_work:
        return "operating-cycle-placeholder-work-nonzero";
    case OperatingCycleAccountingErrorCode::accounting_invariant_violation:
        return "operating-cycle-accounting-invariant-failed";
    case OperatingCycleAccountingErrorCode::invalid_plan:
    case OperatingCycleAccountingErrorCode::invalid_term_partition:
    case OperatingCycleAccountingErrorCode::invalid_cylinder_plan:
    case OperatingCycleAccountingErrorCode::invalid_gas_volume_plan:
    case OperatingCycleAccountingErrorCode::malformed_sample:
    case OperatingCycleAccountingErrorCode::moved_from:
        return "operating-cycle-contract-violated";
    }
    return "operating-cycle-contract-violated";
}

[[nodiscard]] std::string
convergence_failure_summary(const AdjacentCycleBlockConvergenceError &error) {
    std::string summary =
        "fixed-cutoff convergence failed; error-code=" +
        std::to_string(static_cast<std::uint32_t>(error.code)) +
        "; retained-cycle-count=" + std::to_string(error.retained_cycle_count) +
        "; required-cycle-count=" + std::to_string(error.required_cycle_count);
    if (!error.evidence.has_value()) {
        return summary;
    }

    const auto &evidence = *error.evidence;
    summary +=
        "; block-a-first-ordinal=" +
        std::to_string(evidence.block_a.range.first_cycle_ordinal) +
        "; block-a-last-ordinal=" +
        std::to_string(evidence.block_a.range.last_cycle_ordinal) +
        "; block-b-first-ordinal=" +
        std::to_string(evidence.block_b.range.first_cycle_ordinal) +
        "; block-b-last-ordinal=" +
        std::to_string(evidence.block_b.range.last_cycle_ordinal) +
        "; torque-residual-binary64=" +
        std::to_string(std::bit_cast<std::uint64_t>(evidence.torque_residual_nm)) +
        "; torque-tolerance-binary64=" +
        std::to_string(
            std::bit_cast<std::uint64_t>(evidence.cycle_mean_torque_tolerance_nm)) +
        "; pressure-residual-binary64=" +
        std::to_string(std::bit_cast<std::uint64_t>(evidence.pressure_residual_pa)) +
        "; pressure-tolerance-binary64=" +
        std::to_string(std::bit_cast<std::uint64_t>(evidence.pressure_tolerance_pa)) +
        "; limiting-gas-volume-id=" +
        std::to_string(evidence.limiting_gas_volume_id.value);
    return summary;
}

} // namespace

LowOrderOperatingPointV1Runtime::LowOrderOperatingPointV1Runtime(
    OperatingCycleAccountant accountant,
    AdjacentCycleBlockConvergenceObserver convergence,
    std::vector<std::size_t> physical_gas_step_indices,
    std::vector<OperatingGasVolumePressureSample> pressure_samples,
    TransactionShape transaction_shape, std::uint64_t fixed_cutoff_frame_count,
    contract::Sha256Digest simulation_request_identity_v2_sha256,
    contract::HeldSpeedOperatingPointConditions conditions, std::string model_id,
    std::string profile_id, std::string scenario_id, contract::EngineId engine_id)
    : accountant_(std::move(accountant)), convergence_(std::move(convergence)),
      physical_gas_step_indices_(std::move(physical_gas_step_indices)),
      pressure_samples_(std::move(pressure_samples)),
      transaction_shape_(std::move(transaction_shape)),
      fixed_cutoff_frame_count_(fixed_cutoff_frame_count),
      simulation_request_identity_v2_sha256_(simulation_request_identity_v2_sha256),
      conditions_(std::move(conditions)), model_id_(std::move(model_id)),
      profile_id_(std::move(profile_id)), scenario_id_(std::move(scenario_id)),
      engine_id_(engine_id) {}

contract::FailureContext LowOrderOperatingPointV1Runtime::fault(
    contract::FailureKind kind, std::string detail_code, std::string state_summary,
    const LegacyMechanismStep *mechanics,
    std::optional<contract::GasVolumeId> gas_volume_id) const {
    const auto sample_index =
        mechanics != nullptr ? mechanics->sample_index : accepted_sample_count_;
    const auto step_end_index =
        mechanics != nullptr ? mechanics->step_end_index : accepted_sample_count_;
    const auto rate = conditions_.physics_rate_hz;
    const double time_s = rate.numerator == 0
                              ? 0.0
                              : static_cast<double>(step_end_index) *
                                    static_cast<double>(rate.denominator) /
                                    static_cast<double>(rate.numerator);
    return {
        kind,
        std::move(detail_code),
        model_id_,
        profile_id_,
        sample_index,
        step_end_index,
        time_s,
        mechanics != nullptr ? mechanics->theta_unwrapped_rad : 0.0,
        engine_id_,
        std::nullopt,
        std::nullopt,
        gas_volume_id,
        std::nullopt,
        std::nullopt,
        "scenario=" + scenario_id_ + "; " + std::move(state_summary),
        "none; operating-point simulation terminated without fallback",
        {},
    };
}

LowOrderOperatingPointV1AdvanceResult
LowOrderOperatingPointV1Runtime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

std::optional<contract::FailureContext>
LowOrderOperatingPointV1Runtime::validate_transaction(
    const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas) const {
    if (mechanics.sample_index != accepted_sample_count_ ||
        mechanics.step_end_index != accepted_sample_count_ + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick ||
        mechanics.rate != conditions_.physics_rate_hz || gas.rate != mechanics.rate) {
        return fault(contract::FailureKind::contract_violation,
                     "operating-step-clock-disagreed",
                     "operating policy received a noncontiguous or mismatched "
                     "mechanics+gas transaction",
                     &mechanics);
    }

    constexpr contract::OperatingState kHeldRunningState{
        true, true, false, true, false,
    };
    const bool exact_held_controls =
        std::bit_cast<std::uint64_t>(mechanics.engine_speed_rpm) ==
            std::bit_cast<std::uint64_t>(conditions_.engine_speed_rpm) &&
        std::bit_cast<std::uint64_t>(mechanics.requested_throttle_01) ==
            std::bit_cast<std::uint64_t>(conditions_.throttle_01);
    if (!exact_held_controls || mechanics.operating_state != kHeldRunningState) {
        return fault(
            contract::FailureKind::contract_violation,
            "operating-held-condition-disagreed",
            "mechanics transaction differs from the compiled held RPM, throttle, "
            "or fired held-running operating state",
            &mechanics);
    }
    if (!std::isfinite(mechanics.resolved_engine_throttle_01) ||
        !std::isfinite(mechanics.intake_plate_position_01) ||
        !std::isfinite(mechanics.main_flow_multiplier_01) ||
        !std::isfinite(mechanics.theta_unwrapped_rad) ||
        !std::isfinite(mechanics.theta_cycle_rad) ||
        !std::isfinite(gas.indicated_gas_torque_nm)) {
        return fault(
            contract::FailureKind::numerical_failure, "operating-step-scalar-nonfinite",
            "mechanics+gas transaction contains a nonfinite scalar", &mechanics);
    }

    const double expected_resolved_engine_throttle_01 =
        1.0 - std::pow(mechanics.requested_throttle_01, 2.0);
    const double expected_intake_plate_position_01 =
        0.994 * expected_resolved_engine_throttle_01;
    const double expected_main_flow_multiplier_01 =
        std::cos(kLegacyPi * expected_intake_plate_position_01 / 2.0);
    const bool exact_effective_throttle =
        std::bit_cast<std::uint64_t>(mechanics.resolved_engine_throttle_01) ==
            std::bit_cast<std::uint64_t>(expected_resolved_engine_throttle_01) &&
        std::bit_cast<std::uint64_t>(mechanics.intake_plate_position_01) ==
            std::bit_cast<std::uint64_t>(expected_intake_plate_position_01) &&
        std::bit_cast<std::uint64_t>(mechanics.main_flow_multiplier_01) ==
            std::bit_cast<std::uint64_t>(expected_main_flow_multiplier_01);
    if (!exact_effective_throttle) {
        return fault(
            contract::FailureKind::contract_violation,
            "operating-effective-throttle-disagreed",
            "mechanics effective-throttle lanes differ from the exact admitted "
            "legacy held-throttle transform",
            &mechanics);
    }

    const auto contains_limiter_transition = [](const auto &events) {
        for (const auto &event : events) {
            if (std::holds_alternative<contract::LimiterStateChanged>(event.payload)) {
                return true;
            }
        }
        return false;
    };
    if (mechanics.operating_state.limiter_enabled ||
        std::bit_cast<std::uint64_t>(mechanics.limiter_timer_s) !=
            std::bit_cast<std::uint64_t>(0.0) ||
        mechanics.limiter_cut_active || contains_limiter_transition(mechanics.events) ||
        contains_limiter_transition(gas.events)) {
        return fault(
            contract::FailureKind::contract_violation,
            "operating-disabled-limiter-disagreed",
            "held operating transaction requires disabled limiter state, canonical "
            "positive-zero timer, inactive cut, and no limiter transition event",
            &mechanics);
    }

    if (mechanics.cylinders.size() != transaction_shape_.cylinders.size() ||
        gas.cylinders.size() != transaction_shape_.cylinders.size() ||
        gas.gas_volumes.size() != transaction_shape_.gas_volumes.size() ||
        gas.flow_edges.size() != transaction_shape_.flow_edges.size() ||
        gas.exhaust_routes.size() != transaction_shape_.exhaust_routes.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "operating-transaction-shape-disagreed",
                     "mechanics+gas vector shapes differ from the compiled engine "
                     "topology",
                     &mechanics);
    }

    double canonical_indicated_gas_torque_nm = 0.0;
    for (std::size_t index = 0; index < transaction_shape_.cylinders.size(); ++index) {
        const auto &expected = transaction_shape_.cylinders[index];
        const auto &mechanism = mechanics.cylinders[index];
        const auto &cylinder = gas.cylinders[index];
        if (mechanism.cylinder_id != expected.cylinder_id ||
            mechanism.exhaust_route_id != expected.exhaust_route_id ||
            cylinder.cylinder_id != expected.cylinder_id ||
            cylinder.exhaust_route_id != expected.exhaust_route_id ||
            cylinder.intake_port_id != expected.intake_port_id ||
            cylinder.exhaust_port_id != expected.exhaust_port_id ||
            cylinder.intake_runner_volume_id != expected.intake_runner_volume_id ||
            cylinder.chamber_volume_id != expected.chamber_volume_id ||
            cylinder.exhaust_primary_volume_id != expected.exhaust_primary_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "operating-cylinder-shape-disagreed",
                         "cylinder transaction identity/topology differs at index=" +
                             std::to_string(index) + "; expected-cylinder-id=" +
                             std::to_string(expected.cylinder_id.value),
                         &mechanics);
        }
        if (!std::isfinite(cylinder.indicated_gas_torque_nm)) {
            return fault(contract::FailureKind::numerical_failure,
                         "operating-cylinder-torque-nonfinite",
                         "cylinder indicated-gas torque is nonfinite at index=" +
                             std::to_string(index),
                         &mechanics);
        }
        canonical_indicated_gas_torque_nm += cylinder.indicated_gas_torque_nm;
        if (!std::isfinite(canonical_indicated_gas_torque_nm)) {
            return fault(
                contract::FailureKind::numerical_failure,
                "operating-cylinder-torque-sum-nonfinite",
                "canonical positive-zero-seeded cylinder torque reduction became "
                "nonfinite at index=" +
                    std::to_string(index),
                &mechanics);
        }
    }
    if (std::bit_cast<std::uint64_t>(gas.indicated_gas_torque_nm) !=
        std::bit_cast<std::uint64_t>(canonical_indicated_gas_torque_nm)) {
        return fault(contract::FailureKind::contract_violation,
                     "operating-indicated-torque-reduction-disagreed",
                     "aggregate indicated-gas torque differs from the exact "
                     "positive-zero-seeded left-to-right cylinder reduction",
                     &mechanics);
    }

    for (std::size_t index = 0; index < transaction_shape_.gas_volumes.size();
         ++index) {
        const auto &expected = transaction_shape_.gas_volumes[index];
        const auto &actual = gas.gas_volumes[index];
        const bool expected_physical =
            expected.kind != contract::GasVolumeKind::atmosphere;
        if (actual.gas_volume_id != expected.id || actual.kind != expected.kind ||
            actual.physically_resolved != expected_physical) {
            return fault(contract::FailureKind::contract_violation,
                         "operating-gas-volume-shape-disagreed",
                         "gas-volume transaction identity/role differs at index=" +
                             std::to_string(index),
                         &mechanics, expected.id);
        }
    }

    for (std::size_t index = 0; index < transaction_shape_.flow_edges.size(); ++index) {
        const auto &expected = transaction_shape_.flow_edges[index];
        const auto &actual = gas.flow_edges[index];
        if (actual.flow_edge_id != expected.id ||
            actual.endpoint_0_volume_id != expected.endpoint_0_volume_id ||
            actual.endpoint_1_volume_id != expected.endpoint_1_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "operating-flow-edge-shape-disagreed",
                         "flow-edge transaction identity/topology differs at index=" +
                             std::to_string(index),
                         &mechanics);
        }
    }

    for (std::size_t index = 0; index < transaction_shape_.exhaust_routes.size();
         ++index) {
        const auto &expected = transaction_shape_.exhaust_routes[index];
        const auto &actual = gas.exhaust_routes[index];
        if (actual.route_id != expected.route_id ||
            actual.collector_volume_id != expected.collector_volume_id ||
            actual.collector_outlet_edge_id != expected.collector_outlet_edge_id) {
            return fault(
                contract::FailureKind::contract_violation,
                "operating-route-shape-disagreed",
                "exhaust-route transaction identity/topology differs at index=" +
                    std::to_string(index),
                &mechanics);
        }
    }

    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderOperatingPointV1Runtime::observe_completed_cycle(
    const OperatingCycleBoundaryCrossing &crossing,
    const LegacyMechanismStep &mechanics) {
    if (!crossing.completed_cycle.has_value() || operating_point_result_.has_value()) {
        return std::nullopt;
    }
    const auto &completed = *crossing.completed_cycle;
    std::vector<AdjacentCycleBlockPressure> pressures;
    pressures.reserve(crossing.boundary_pressures.size());
    for (const auto &pressure : crossing.boundary_pressures) {
        pressures.push_back({
            pressure.gas_volume_id,
            pressure.pressure_pa_abs,
        });
    }
    const auto result = convergence_.observe({
        completed.indicated_quadrature.completed_cycle_ordinal,
        {
            completed.indicated_quadrature.start_boundary,
            completed.indicated_quadrature.start_theta_rad,
            completed.indicated_quadrature.start_time_s,
        },
        {
            completed.indicated_quadrature.end_boundary,
            completed.indicated_quadrature.end_theta_rad,
            completed.indicated_quadrature.end_time_s,
        },
        completed.indicated_quadrature.indicated_gas_work_j,
        completed.aggregate_loss.positive_aggregate_loss_work_j,
        completed.starter_work_j,
        completed.brake_work_j,
        std::move(pressures),
    });
    if (const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result)) {
        const auto gas_volume_id =
            error->element_index < pressure_samples_.size()
                ? std::optional{pressure_samples_[error->element_index].gas_volume_id}
                : std::nullopt;
        return fault(error->code ==
                             AdjacentCycleBlockConvergenceErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "operating-convergence-observation-failed",
                     "complete-cycle convergence observer rejected accountant evidence",
                     &mechanics, gas_volume_id);
    }
    if (std::holds_alternative<AdjacentCycleBlockObservationClosed>(result)) {
        return fault(contract::FailureKind::contract_violation,
                     "operating-convergence-closed-before-cutoff",
                     "convergence observer closed before the fixed cutoff", &mechanics);
    }
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderOperatingPointV1Runtime::finalize_at_cutoff(
    const LegacyMechanismStep &mechanics) {
    const auto result = convergence_.finalize_at_fixed_cutoff();
    if (const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result)) {
        if (error->code ==
            AdjacentCycleBlockConvergenceErrorCode::insufficient_cycles) {
            return fault(
                contract::FailureKind::preparation_not_converged,
                std::string{contract::kPreparationInsufficientCyclesDetailCode},
                convergence_failure_summary(*error), &mechanics);
        }
        if (error->code == AdjacentCycleBlockConvergenceErrorCode::nonconverged) {
            if (!error->evidence.has_value()) {
                return fault(contract::FailureKind::contract_violation,
                             "operating-nonconvergence-evidence-missing",
                             "nonconverged fixed-cutoff result omitted its evaluated "
                             "residual evidence",
                             &mechanics);
            }
            const auto &evidence = *error->evidence;
            auto failure =
                fault(contract::FailureKind::preparation_not_converged,
                      std::string{contract::kPreparationNotConvergedDetailCode},
                      convergence_failure_summary(*error), &mechanics,
                      evidence.limiting_gas_volume_id);
            failure.tolerances = {
                {
                    std::string{contract::kCycleMeanTorqueResidualNmQuantityId},
                    evidence.torque_residual_nm,
                    evidence.cycle_mean_torque_tolerance_nm,
                },
                {
                    std::string{contract::kBoundaryPressureResidualPaQuantityId},
                    evidence.pressure_residual_pa,
                    evidence.pressure_tolerance_pa,
                },
            };
            return failure;
        }
        return fault(error->code ==
                             AdjacentCycleBlockConvergenceErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "operating-convergence-finalization-failed",
                     convergence_failure_summary(*error), &mechanics);
    }

    auto point =
        public_operating_point(std::get<AdjacentCycleBlockConverged>(result).evidence,
                               simulation_request_identity_v2_sha256_, conditions_);
    const auto report = contract::validate(point);
    if (!report.ok()) {
        const auto summary =
            report.issues.empty()
                ? std::string{"typed operating result failed without a diagnostic"}
                : "path=" + report.issues.front().path + "; " +
                      report.issues.front().message;
        return fault(contract::FailureKind::contract_violation,
                     "operating-result-construction-invalid", std::move(summary),
                     &mechanics);
    }
    operating_point_result_ = std::move(point);
    return std::nullopt;
}

LowOrderOperatingPointV1AdvanceResult
LowOrderOperatingPointV1Runtime::advance(const LegacyMechanismStep &mechanics,
                                         const LegacyLowOrderGasStep &gas) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (auto failure = validate_transaction(mechanics, gas); failure.has_value()) {
        return fail(std::move(*failure));
    }
    if (!operating_point_result_.has_value() &&
        mechanics.step_end_index > fixed_cutoff_frame_count_) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "operating-cutoff-was-skipped",
                          "simulation advanced beyond the fixed cutoff without "
                          "finalizing convergence",
                          &mechanics));
    }

    const auto capture_torque = operating_capture_torque(gas.indicated_gas_torque_nm);
    if (operating_point_result_.has_value()) {
        ++accepted_sample_count_;
        return LowOrderOperatingPointV1Step{capture_torque};
    }

    for (std::size_t index = 0; index < physical_gas_step_indices_.size(); ++index) {
        const auto gas_index = physical_gas_step_indices_[index];
        if (gas_index >= gas.gas_volumes.size()) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "operating-pressure-shape-disagreed",
                              "compiled physical gas-volume index is outside the "
                              "current gas transaction",
                              &mechanics, pressure_samples_[index].gas_volume_id));
        }
        const auto &volume = gas.gas_volumes[gas_index];
        if (!volume.physically_resolved ||
            volume.gas_volume_id != pressure_samples_[index].gas_volume_id) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "operating-pressure-identity-disagreed",
                              "current gas transaction differs from the compiled "
                              "physical pressure inventory",
                              &mechanics, pressure_samples_[index].gas_volume_id));
        }
        pressure_samples_[index].pressure_pa_abs = legacy_gas_pressure_pa(volume.cell);
    }

    const auto time_s = static_cast<double>(mechanics.step_end_index) *
                        static_cast<double>(mechanics.rate.denominator) /
                        static_cast<double>(mechanics.rate.numerator);
    const auto accounting = accountant_.advance({
        mechanics.sample_index,
        time_s,
        mechanics.theta_unwrapped_rad,
        mechanics.engine_speed_rpm,
        gas.indicated_gas_torque_nm,
        pressure_samples_,
    });
    if (const auto *error = std::get_if<OperatingCycleAccountingError>(&accounting)) {
        const auto gas_volume_id =
            error->element_index < pressure_samples_.size()
                ? std::optional{pressure_samples_[error->element_index].gas_volume_id}
                : std::nullopt;
        return fail(
            fault(accounting_failure_kind(error->code),
                  std::string{accounting_detail_code(error->code)},
                  "complete-cycle operating accountant rejected the core transaction",
                  &mechanics, gas_volume_id));
    }
    if (const auto *crossing =
            std::get_if<OperatingCycleBoundaryCrossing>(&accounting)) {
        if (auto failure = observe_completed_cycle(*crossing, mechanics);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
    }

    if (mechanics.step_end_index == fixed_cutoff_frame_count_) {
        if (auto failure = finalize_at_cutoff(mechanics); failure.has_value()) {
            return fail(std::move(*failure));
        }
    }
    ++accepted_sample_count_;
    return LowOrderOperatingPointV1Step{capture_torque};
}

bool LowOrderOperatingPointV1Runtime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderOperatingPointV1Runtime::finalized() const noexcept {
    return operating_point_result_.has_value();
}

std::uint64_t LowOrderOperatingPointV1Runtime::accepted_sample_count() const noexcept {
    return accepted_sample_count_;
}

std::uint64_t
LowOrderOperatingPointV1Runtime::fixed_cutoff_frame_count() const noexcept {
    return fixed_cutoff_frame_count_;
}

const std::optional<contract::HeldSpeedOperatingPointResult> &
LowOrderOperatingPointV1Runtime::operating_point_result() const noexcept {
    return operating_point_result_;
}

} // namespace engine_sim_offline::simulation
