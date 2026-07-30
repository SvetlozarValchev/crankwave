#include "simulation/low_order_free_engine_v1_runtime.hpp"

#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <bit>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;
constexpr double kRpmPerRadianPerSecond = 30.0 / std::numbers::pi_v<double>;

[[nodiscard]] contract::TorqueValueNm
available_torque(double value_nm, contract::TorqueTermMask terms) noexcept {
    return {
        value_nm,
        contract::Availability::available,
        contract::Completeness::complete,
        contract::QuantityUnavailableReason::none,
        terms,
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

[[nodiscard]] contract::QuantityValue available_quantity(double value) noexcept {
    return {
        value,
        contract::Availability::available,
        contract::Completeness::complete,
        contract::QuantityUnavailableReason::none,
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
preparation_capture_torque(double indicated_gas_torque_nm) noexcept {
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
    result.actuator =
        unavailable_torque(contract::QuantityUnavailableReason::required_input_missing);
    result.dyno_reaction =
        unavailable_torque(contract::QuantityUnavailableReason::required_input_missing);
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

[[nodiscard]] contract::TorqueTelemetry
released_capture_torque(const detail::PositiveSpeedRigidCrankZohStep &motion,
                        const OperatingCompletedCycle &latest_cycle,
                        double applied_indicated_gas_torque_nm,
                        double applied_lagged_loss_torque_nm) noexcept {
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        applied_indicated_gas_torque_nm, contract::indicated_gas_torque_term_mask());
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory =
        available_torque(applied_lagged_loss_torque_nm,
                         contract::friction_pump_and_accessory_torque_term_mask());
    result.starter = available_torque(
        0.0, contract::torque_term_mask(contract::TorqueTerm::starter));
    result.instantaneous_net_shaft =
        available_torque(motion.input.held_upstream_engine_torque_nm,
                         contract::known_torque_term_mask());
    result.cycle_mean_net_shaft = available_torque(
        latest_cycle.cycle_mean_brake_torque_nm, contract::known_torque_term_mask());
    result.actuator = available_torque(-motion.input.held_resisting_torque_nm, 0);
    result.dyno_reaction = available_torque(motion.input.held_resisting_torque_nm, 0);
    result.cycle_work_j = available_quantity(latest_cycle.brake_work_j);
    result.net_bmep_pa =
        available_quantity(latest_cycle.net_brake_mean_effective_pressure_pa);
    result.instantaneous_power_w =
        available_quantity(motion.input.held_upstream_engine_torque_nm *
                           motion.input.initial_state.angular_speed_rad_s);
    result.cycle_mean_power_w =
        available_quantity(latest_cycle.cycle_mean_brake_power_w);
    return result;
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
    default:
        return contract::FailureKind::contract_violation;
    }
}

[[nodiscard]] std::string_view
accounting_detail_code(OperatingCycleAccountingErrorCode code) noexcept {
    switch (code) {
    case OperatingCycleAccountingErrorCode::nonpositive_pressure:
        return "free-engine-cycle-pressure-nonpositive";
    case OperatingCycleAccountingErrorCode::nonfinite_pressure:
        return "free-engine-cycle-pressure-nonfinite";
    case OperatingCycleAccountingErrorCode::quadrature_failure:
        return "free-engine-cycle-quadrature-failed";
    case OperatingCycleAccountingErrorCode::aggregate_loss_failure:
        return "free-engine-cycle-aggregate-loss-failed";
    case OperatingCycleAccountingErrorCode::nonfinite_result:
        return "free-engine-cycle-result-nonfinite";
    default:
        return "free-engine-cycle-contract-violated";
    }
}

[[nodiscard]] std::string
sampling_failure_summary(const FixedHorizonCycleSamplingError &error) {
    return "fixed-horizon cycle sampling failed; error-code=" +
           std::to_string(static_cast<std::uint32_t>(error.code)) +
           "; retained-cycle-count=" + std::to_string(error.retained_cycle_count) +
           "; required-cycle-count=" + std::to_string(error.required_cycle_count);
}

} // namespace

LowOrderFreeEngineV1Runtime::LowOrderFreeEngineV1Runtime(
    ScenarioControlCursor control_cursor, OperatingCycleAccountant accountant,
    FixedHorizonCycleSampler sampler,
    std::vector<std::size_t> physical_gas_step_indices,
    std::vector<OperatingGasVolumePressureSample> pressure_samples,
    contract::RationalRateHz rate, std::uint64_t expected_sample_count,
    std::uint64_t release_frame_index, double initial_engine_speed_rpm,
    double initial_theta_rad, double equivalent_inertia_kg_m2, std::string model_id,
    std::string profile_id, std::string scenario_id, contract::EngineId engine_id)
    : control_cursor_(std::move(control_cursor)), accountant_(std::move(accountant)),
      sampler_(std::move(sampler)),
      physical_gas_step_indices_(std::move(physical_gas_step_indices)),
      pressure_samples_(std::move(pressure_samples)), rate_(rate),
      expected_sample_count_(expected_sample_count),
      release_frame_index_(release_frame_index),
      step_s_(static_cast<double>(rate.denominator) /
              static_cast<double>(rate.numerator)),
      initial_engine_speed_rpm_(initial_engine_speed_rpm),
      equivalent_inertia_kg_m2_(equivalent_inertia_kg_m2),
      crank_state_{initial_theta_rad,
                   initial_engine_speed_rpm * std::numbers::pi_v<double> / 30.0},
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id) {}

contract::FailureContext LowOrderFreeEngineV1Runtime::fault(
    contract::FailureKind kind, std::string detail_code, std::string state_summary,
    const LegacyMechanismStep *mechanics,
    std::optional<contract::GasVolumeId> gas_volume_id) const {
    const auto sample_index =
        mechanics != nullptr ? mechanics->sample_index : accepted_sample_count_;
    const auto step_end_index =
        mechanics != nullptr ? mechanics->step_end_index : accepted_sample_count_;
    return {
        kind,
        std::move(detail_code),
        model_id_,
        profile_id_,
        sample_index,
        step_end_index,
        static_cast<double>(step_end_index) * static_cast<double>(rate_.denominator) /
            static_cast<double>(rate_.numerator),
        mechanics != nullptr ? mechanics->theta_unwrapped_rad : crank_state_.theta_rad,
        engine_id_,
        std::nullopt,
        std::nullopt,
        gas_volume_id,
        std::nullopt,
        std::nullopt,
        "scenario=" + scenario_id_ + "; " + std::move(state_summary),
        "none; free-engine simulation terminated without fallback",
        {},
    };
}

LowOrderFreeEngineV1AdvanceResult
LowOrderFreeEngineV1Runtime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::observe_preparation_cycle(
    const OperatingCycleBoundaryCrossing &crossing,
    const LegacyMechanismStep &mechanics) {
    if (!crossing.completed_cycle.has_value()) {
        return std::nullopt;
    }
    const auto &completed = *crossing.completed_cycle;
    std::vector<FixedHorizonCyclePressure> pressures;
    pressures.reserve(crossing.boundary_pressures.size());
    for (const auto &pressure : crossing.boundary_pressures) {
        pressures.push_back({
            pressure.gas_volume_id,
            pressure.pressure_pa_abs,
        });
    }
    const auto result = sampler_.observe({
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
    if (const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result)) {
        const auto gas_volume_id =
            error->element_index < pressure_samples_.size()
                ? std::optional{pressure_samples_[error->element_index].gas_volume_id}
                : std::nullopt;
        return fault(error->code == FixedHorizonCycleSamplingErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "free-engine-preparation-sampling-observation-failed",
                     "fixed-horizon sampler rejected complete-cycle accountant "
                     "evidence",
                     &mechanics, gas_volume_id);
    }
    if (std::holds_alternative<FixedHorizonCycleObservationClosed>(result)) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-preparation-sampler-closed-early",
                     "fixed-horizon sampler closed before free-engine release",
                     &mechanics);
    }
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::update_accounting(const LegacyMechanismStep &mechanics,
                                               const LegacyLowOrderGasStep &gas) {
    if (mechanics.sample_index != accepted_sample_count_ ||
        mechanics.step_end_index != accepted_sample_count_ + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick || mechanics.rate != rate_ ||
        gas.rate != rate_ || !std::isfinite(gas.indicated_gas_torque_nm)) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-step-transaction-disagreed",
                     "free-engine policy received a noncontiguous or malformed "
                     "mechanics+gas transaction",
                     &mechanics);
    }

    for (std::size_t index = 0; index < physical_gas_step_indices_.size(); ++index) {
        const auto gas_index = physical_gas_step_indices_[index];
        if (gas_index >= gas.gas_volumes.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "free-engine-pressure-shape-disagreed",
                         "compiled physical gas-volume index is outside the current "
                         "gas transaction",
                         &mechanics, pressure_samples_[index].gas_volume_id);
        }
        const auto &volume = gas.gas_volumes[gas_index];
        if (!volume.physically_resolved ||
            volume.gas_volume_id != pressure_samples_[index].gas_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "free-engine-pressure-identity-disagreed",
                         "current gas transaction differs from the compiled physical "
                         "pressure inventory",
                         &mechanics, pressure_samples_[index].gas_volume_id);
        }
        pressure_samples_[index].pressure_pa_abs = legacy_gas_pressure_pa(volume.cell);
    }

    const double time_s = static_cast<double>(mechanics.step_end_index) *
                          static_cast<double>(rate_.denominator) /
                          static_cast<double>(rate_.numerator);
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
        return fault(accounting_failure_kind(error->code),
                     std::string{accounting_detail_code(error->code)},
                     "variable-speed cycle accountant rejected the committed "
                     "free-engine transaction",
                     &mechanics, gas_volume_id);
    }

    const auto *crossing = std::get_if<OperatingCycleBoundaryCrossing>(&accounting);
    if (crossing == nullptr || !crossing->completed_cycle.has_value()) {
        return std::nullopt;
    }
    if (!preparation_finalized_) {
        if (auto failure = observe_preparation_cycle(*crossing, mechanics);
            failure.has_value()) {
            return failure;
        }
    }

    latest_completed_cycle_ = *crossing->completed_cycle;
    const auto &cycle = *latest_completed_cycle_;
    const double represented_angle_rad = cycle.indicated_quadrature.end_theta_rad -
                                         cycle.indicated_quadrature.start_theta_rad;
    if (!std::isfinite(represented_angle_rad) || !(represented_angle_rad > 0.0)) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-loss-cycle-angle-disagreed",
                     "completed aggregate-loss cycle had no finite positive span",
                     &mechanics);
    }
    const double lagged_loss_torque_nm =
        -cycle.aggregate_loss.positive_aggregate_loss_work_j / kFourStrokeCycleRadians;
    if (!std::isfinite(lagged_loss_torque_nm) || lagged_loss_torque_nm > 0.0) {
        return fault(contract::FailureKind::numerical_failure,
                     "free-engine-lagged-loss-nonfinite",
                     "completed aggregate loss did not produce a finite resisting "
                     "cycle-mean torque",
                     &mechanics);
    }
    applied_lagged_loss_torque_nm_ = lagged_loss_torque_nm;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::finalize_preparation(
    const LegacyMechanismStep &mechanics) {
    auto result = sampler_.finalize_at_fixed_horizon();
    if (const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result)) {
        return fault(error->code == FixedHorizonCycleSamplingErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "free-engine-preparation-sampling-finalization-failed",
                     sampling_failure_summary(*error), &mechanics);
    }
    (void)std::get<FixedHorizonCycleSampled>(result);
    if (!previous_indicated_gas_torque_nm_.has_value() ||
        !applied_lagged_loss_torque_nm_.has_value() ||
        !latest_completed_cycle_.has_value()) {
        return fault(
            contract::FailureKind::contract_violation,
            "free-engine-release-state-incomplete",
            "release requires fixed-horizon trailing-cycle evidence, prior committed "
            "indicated torque, and one completed aggregate-loss cycle",
            &mechanics);
    }
    preparation_finalized_ = true;
    return std::nullopt;
}

LowOrderFreeEngineV1AdvanceResult
LowOrderFreeEngineV1Runtime::advance(LowOrderEngineCoreV1Runtime &core) {
    return advance(core, {});
}

LowOrderFreeEngineV1AdvanceResult
LowOrderFreeEngineV1Runtime::advance(LowOrderEngineCoreV1Runtime &core,
                                     const LiveControlOverrides &overrides) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (accepted_sample_count_ == expected_sample_count_) {
        if (!terminal_completed_ || !preparation_finalized_ ||
            !control_cursor_.completed() || !core.completed()) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "free-engine-completion-state-disagreed",
                              "free-engine policy, controls, and shared core did not "
                              "complete the same fixed horizon"));
        }
        return LowOrderEngineCoreV1Completed{accepted_sample_count_};
    }

    const auto controls = control_cursor_.next();
    if (!controls.has_value() || controls->sample_index != accepted_sample_count_ ||
        controls->step_end_index != accepted_sample_count_ + 1U) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-engine-control-step-disagreed",
                          "free-engine load cursor lost the next contiguous physics "
                          "step"));
    }

    if (accepted_sample_count_ < release_frame_index_) {
        if (overrides.any()) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "free-engine-live-controls-during-held-preparation",
                "live throttle, ignition, fuel, limiter, and external-resistance "
                "overrides are not admitted during fixed held preparation"));
        }
        const double omega =
            initial_engine_speed_rpm_ * std::numbers::pi_v<double> / 30.0;
        const double held_angular_displacement_rad =
            initial_engine_speed_rpm_ * kLegacyRpmScale * step_s_;
        auto resolved_overrides = overrides;
        resolved_overrides.has_external_resisting_torque_nm = true;
        resolved_overrides.external_resisting_torque_nm =
            controls->external_resisting_torque_nm;
        auto core_result =
            core.advance({initial_engine_speed_rpm_, held_angular_displacement_rad},
                         resolved_overrides);
        if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
            return fail(*failure);
        }
        if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "free-engine-core-premature-completion",
                              "shared core completed during fixed held preparation"));
        }
        const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
        const auto &mechanics = core_step.mechanics.get();
        const auto &gas = core_step.gas.get();
        const bool exact_held_controls =
            std::bit_cast<std::uint64_t>(mechanics.engine_speed_rpm) ==
                std::bit_cast<std::uint64_t>(initial_engine_speed_rpm_) &&
            std::bit_cast<std::uint64_t>(mechanics.requested_throttle_01) ==
                std::bit_cast<std::uint64_t>(controls->requested_throttle) &&
            mechanics.operating_state == controls->operating_state;
        const auto &state = mechanics.operating_state;
        if (!exact_held_controls || !state.ignition_enabled || !state.fuel_enabled ||
            state.starter_enabled || state.dyno_enabled) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "free-engine-held-condition-disagreed",
                "preparation transaction differs from the compiled RPM, throttle, "
                "or warm fired free-engine state",
                &mechanics));
        }
        if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
            return fail(std::move(*failure));
        }
        previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
        crank_state_ = {mechanics.theta_unwrapped_rad, omega};
        ++accepted_sample_count_;
        if (accepted_sample_count_ == release_frame_index_) {
            if (auto failure = finalize_preparation(mechanics); failure.has_value()) {
                return fail(std::move(*failure));
            }
        }
        return LowOrderFreeEngineV1StepView{
            std::cref(mechanics), std::cref(gas),
            preparation_capture_torque(gas.indicated_gas_torque_nm)};
    }

    if (!preparation_finalized_ || !previous_indicated_gas_torque_nm_.has_value() ||
        !applied_lagged_loss_torque_nm_.has_value() ||
        !latest_completed_cycle_.has_value()) {
        return fail(
            fault(contract::FailureKind::contract_violation,
                  "free-engine-causal-input-missing",
                  "released motion requires finalized preparation, prior committed "
                  "indicated torque, and completed one-cycle-lag loss"));
    }

    const double applied_indicated = *previous_indicated_gas_torque_nm_;
    const double applied_loss = *applied_lagged_loss_torque_nm_;
    const double applied_external_resisting_torque_nm =
        overrides.has_external_resisting_torque_nm
            ? overrides.external_resisting_torque_nm
            : controls->external_resisting_torque_nm;
    const auto motion_calculation = detail::advance_positive_speed_rigid_crank_zoh({
        equivalent_inertia_kg_m2_,
        crank_state_,
        applied_indicated + applied_loss,
        applied_external_resisting_torque_nm,
        step_s_,
    });
    if (const auto *error = std::get_if<detail::PositiveSpeedRigidCrankZohInputError>(
            &motion_calculation)) {
        return fail(
            fault(contract::FailureKind::numerical_failure,
                  "free-engine-crank-dynamics-failed",
                  "positive-speed rigid-crank step rejected input; issue=" +
                      std::to_string(static_cast<std::uint32_t>(error->issue))));
    }
    if (const auto *stall =
            std::get_if<detail::PositiveSpeedRigidCrankZohStall>(&motion_calculation)) {
        return fail(fault(
            contract::FailureKind::nonphysical_state, "free-engine-crank-stalled",
            "external resistance reached zero speed before the end of the physics "
            "step; stop-time-s=" +
                std::to_string(stall->stall_time_s) +
                "; stop-theta-rad=" + std::to_string(stall->stall_theta_rad)));
    }
    const auto &motion =
        std::get<detail::PositiveSpeedRigidCrankZohStep>(motion_calculation);
    const double post_step_rpm =
        motion.final_state.angular_speed_rad_s * kRpmPerRadianPerSecond;
    auto resolved_overrides = overrides;
    resolved_overrides.has_external_resisting_torque_nm = true;
    resolved_overrides.external_resisting_torque_nm =
        applied_external_resisting_torque_nm;
    auto core_result = core.advance({post_step_rpm, motion.angular_displacement_rad},
                                    resolved_overrides);
    if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
        return fail(*failure);
    }
    if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-engine-core-premature-completion",
                          "shared core completed before the free-engine horizon"));
    }
    const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
    const auto &mechanics = core_step.mechanics.get();
    const auto &gas = core_step.gas.get();

    // This frame reports exactly the prior committed engine torque and current
    // right-continuous external resistance used for its motion. Newly committed
    // gas and loss state become causal input only for the following frame.
    const auto capture_torque = released_capture_torque(
        motion, *latest_completed_cycle_, applied_indicated, applied_loss);
    if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
        return fail(std::move(*failure));
    }
    previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
    crank_state_ = motion.final_state;
    ++accepted_sample_count_;
    if (accepted_sample_count_ == expected_sample_count_) {
        terminal_completed_ = true;
    }
    return LowOrderFreeEngineV1StepView{std::cref(mechanics), std::cref(gas),
                                        capture_torque};
}

bool LowOrderFreeEngineV1Runtime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderFreeEngineV1Runtime::finalized() const noexcept {
    return terminal_completed_ && !faulted();
}

bool LowOrderFreeEngineV1Runtime::held_preparation_active() const noexcept {
    return accepted_sample_count_ < release_frame_index_;
}

std::uint64_t LowOrderFreeEngineV1Runtime::accepted_sample_count() const noexcept {
    return accepted_sample_count_;
}

std::uint64_t LowOrderFreeEngineV1Runtime::release_frame_index() const noexcept {
    return release_frame_index_;
}

} // namespace engine_sim_offline::simulation
