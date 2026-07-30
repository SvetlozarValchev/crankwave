#include "simulation/low_order_inertial_dyno_v1_runtime.hpp"

#include "simulation/legacy_gas_primitives.hpp"

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
    return {value_nm,
            contract::Availability::available,
            contract::Completeness::complete,
            contract::QuantityUnavailableReason::none,
            terms,
            0};
}

[[nodiscard]] contract::TorqueValueNm
unavailable_torque(contract::QuantityUnavailableReason reason) noexcept {
    return {0.0,
            contract::Availability::unavailable,
            contract::Completeness::incomplete,
            reason,
            0,
            0};
}

[[nodiscard]] contract::QuantityValue available_quantity(double value) noexcept {
    return {value, contract::Availability::available, contract::Completeness::complete,
            contract::QuantityUnavailableReason::none};
}

[[nodiscard]] contract::TorqueTelemetry
released_capture_torque(const InertialCrankStepResult &motion,
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
    result.instantaneous_net_shaft = available_torque(
        motion.held_total_crank_torque_nm, contract::known_torque_term_mask());
    result.cycle_mean_net_shaft = available_torque(
        latest_cycle.cycle_mean_brake_torque_nm, contract::known_torque_term_mask());
    result.actuator = available_torque(-motion.applied_brake_torque_nm, 0);
    result.dyno_reaction = available_torque(motion.applied_brake_torque_nm, 0);
    result.cycle_work_j = available_quantity(latest_cycle.brake_work_j);
    result.net_bmep_pa =
        available_quantity(latest_cycle.net_brake_mean_effective_pressure_pa);
    result.instantaneous_power_w = available_quantity(
        motion.held_total_crank_torque_nm * motion.initial_state.angular_speed_rad_s);
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
        return "inertial-cycle-pressure-nonpositive";
    case OperatingCycleAccountingErrorCode::nonfinite_pressure:
        return "inertial-cycle-pressure-nonfinite";
    case OperatingCycleAccountingErrorCode::quadrature_failure:
        return "inertial-cycle-quadrature-failed";
    case OperatingCycleAccountingErrorCode::aggregate_loss_failure:
        return "inertial-cycle-aggregate-loss-failed";
    case OperatingCycleAccountingErrorCode::nonfinite_result:
        return "inertial-cycle-result-nonfinite";
    default:
        return "inertial-cycle-contract-violated";
    }
}

[[nodiscard]] contract::FailureKind
dynamics_failure_kind(InertialCrankDynamicsErrorCode code) noexcept {
    switch (code) {
    case InertialCrankDynamicsErrorCode::stall:
        return contract::FailureKind::nonphysical_state;
    case InertialCrankDynamicsErrorCode::invalid_input:
        return contract::FailureKind::numerical_failure;
    case InertialCrankDynamicsErrorCode::invalid_configuration:
    case InertialCrankDynamicsErrorCode::brake_curve_out_of_domain:
        return contract::FailureKind::contract_violation;
    }
    return contract::FailureKind::contract_violation;
}

} // namespace

LowOrderInertialDynoV1Runtime::LowOrderInertialDynoV1Runtime(
    LowOrderOperatingPointV1Runtime preparation, OperatingCycleAccountant accountant,
    InertialCrankDynamics dynamics, std::vector<std::size_t> physical_gas_step_indices,
    std::vector<OperatingGasVolumePressureSample> pressure_samples,
    contract::RationalRateHz rate, std::uint64_t expected_sample_count,
    std::uint64_t release_frame_index, double initial_engine_speed_rpm,
    double initial_theta_rad, double target_engine_speed_rpm,
    contract::Sha256Digest simulation_request_identity_v3_sha256,
    std::string brake_curve_resolution_id, contract::MethodIdentity brake_torque_method,
    contract::MethodIdentity crank_dynamics_method, std::string model_id,
    std::string profile_id, std::string scenario_id, contract::EngineId engine_id)
    : preparation_(std::move(preparation)), accountant_(std::move(accountant)),
      dynamics_(std::move(dynamics)),
      physical_gas_step_indices_(std::move(physical_gas_step_indices)),
      pressure_samples_(std::move(pressure_samples)), rate_(rate),
      expected_sample_count_(expected_sample_count),
      release_frame_index_(release_frame_index),
      step_s_(static_cast<double>(rate.denominator) /
              static_cast<double>(rate.numerator)),
      initial_engine_speed_rpm_(initial_engine_speed_rpm),
      target_engine_speed_rpm_(target_engine_speed_rpm),
      crank_state_{initial_theta_rad,
                   initial_engine_speed_rpm * std::numbers::pi_v<double> / 30.0},
      minimum_engine_speed_rpm_(initial_engine_speed_rpm),
      maximum_engine_speed_rpm_(initial_engine_speed_rpm),
      release_angular_speed_rad_s_(crank_state_.angular_speed_rad_s),
      simulation_request_identity_v3_sha256_(simulation_request_identity_v3_sha256),
      brake_curve_resolution_id_(std::move(brake_curve_resolution_id)),
      brake_torque_method_(std::move(brake_torque_method)),
      crank_dynamics_method_(std::move(crank_dynamics_method)),
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id) {}

contract::FailureContext LowOrderInertialDynoV1Runtime::fault(
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
        "none; inertial-dyno simulation terminated without fallback",
        {},
    };
}

LowOrderInertialDynoV1AdvanceResult
LowOrderInertialDynoV1Runtime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

std::optional<contract::FailureContext>
LowOrderInertialDynoV1Runtime::update_accounting(const LegacyMechanismStep &mechanics,
                                                 const LegacyLowOrderGasStep &gas) {
    if (mechanics.sample_index != accepted_sample_count_ ||
        mechanics.step_end_index != accepted_sample_count_ + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick || mechanics.rate != rate_ ||
        gas.rate != rate_ || !std::isfinite(gas.indicated_gas_torque_nm)) {
        return fault(contract::FailureKind::contract_violation,
                     "inertial-step-transaction-disagreed",
                     "inertial policy received a noncontiguous or malformed "
                     "mechanics+gas transaction",
                     &mechanics);
    }

    for (std::size_t index = 0; index < physical_gas_step_indices_.size(); ++index) {
        const auto gas_index = physical_gas_step_indices_[index];
        if (gas_index >= gas.gas_volumes.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "inertial-pressure-shape-disagreed",
                         "compiled physical gas-volume index is outside the current "
                         "gas transaction",
                         &mechanics, pressure_samples_[index].gas_volume_id);
        }
        const auto &volume = gas.gas_volumes[gas_index];
        if (!volume.physically_resolved ||
            volume.gas_volume_id != pressure_samples_[index].gas_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "inertial-pressure-identity-disagreed",
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
                     "variable-speed cycle accountant rejected the committed core "
                     "transaction",
                     &mechanics, gas_volume_id);
    }
    const auto *crossing = std::get_if<OperatingCycleBoundaryCrossing>(&accounting);
    if (crossing == nullptr || !crossing->completed_cycle.has_value()) {
        return std::nullopt;
    }

    latest_completed_cycle_ = *crossing->completed_cycle;
    const auto &cycle = *latest_completed_cycle_;
    const double represented_angle_rad = cycle.indicated_quadrature.end_theta_rad -
                                         cycle.indicated_quadrature.start_theta_rad;
    if (!std::isfinite(represented_angle_rad) || !(represented_angle_rad > 0.0)) {
        return fault(contract::FailureKind::contract_violation,
                     "inertial-loss-cycle-angle-disagreed",
                     "completed aggregate-loss cycle had no finite positive span",
                     &mechanics);
    }
    // OperatingCompletedCycle already proves a four-stroke boundary crossing. Its
    // independently rounded absolute boundary angles are evidence, not a second
    // denominator authority; subtracting them need not be bit-equal to 4*pi.
    const double lagged_loss_torque_nm =
        -cycle.aggregate_loss.positive_aggregate_loss_work_j / kFourStrokeCycleRadians;
    if (!std::isfinite(lagged_loss_torque_nm) || lagged_loss_torque_nm > 0.0) {
        return fault(contract::FailureKind::numerical_failure,
                     "inertial-lagged-loss-nonfinite",
                     "completed Chen-Flynn loss did not produce a finite resisting "
                     "cycle-mean torque",
                     &mechanics);
    }
    applied_lagged_loss_torque_nm_ = lagged_loss_torque_nm;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderInertialDynoV1Runtime::finalize_result(const LegacyMechanismStep &mechanics) {
    if (inertial_dyno_result_.has_value()) {
        return std::nullopt;
    }
    const double end_rpm = crank_state_.angular_speed_rad_s * kRpmPerRadianPerSecond;
    const double kinetic_energy_change_j =
        0.5 * dynamics_.equivalent_inertia_kg_m2() *
        ((crank_state_.angular_speed_rad_s * crank_state_.angular_speed_rad_s) -
         (release_angular_speed_rad_s_ * release_angular_speed_rad_s_));
    const double residual_j =
        (released_net_shaft_work_j_ - released_passive_brake_work_j_) -
        kinetic_energy_change_j;
    contract::InertialDynoResult result{
        simulation_request_identity_v3_sha256_,
        initial_engine_speed_rpm_,
        target_engine_speed_rpm_,
        initial_engine_speed_rpm_,
        end_rpm,
        minimum_engine_speed_rpm_,
        maximum_engine_speed_rpm_,
        release_frame_index_,
        expected_sample_count_,
        first_target_reached_frame_index_,
        dynamics_.equivalent_inertia_kg_m2(),
        brake_curve_resolution_id_,
        brake_torque_method_,
        crank_dynamics_method_,
        {
            released_net_shaft_work_j_,
            released_passive_brake_work_j_,
            kinetic_energy_change_j,
            residual_j,
        },
    };
    const auto report = contract::validate(result);
    if (!report.ok()) {
        const auto summary =
            report.issues.empty()
                ? std::string{"typed inertial result failed without a diagnostic"}
                : "path=" + report.issues.front().path + "; " +
                      report.issues.front().message;
        return fault(contract::FailureKind::contract_violation,
                     "inertial-result-construction-invalid", summary, &mechanics);
    }
    inertial_dyno_result_ = std::move(result);
    return std::nullopt;
}

LowOrderInertialDynoV1AdvanceResult
LowOrderInertialDynoV1Runtime::advance(LowOrderEngineCoreV1Runtime &core) {
    return advance(core, {});
}

LowOrderInertialDynoV1AdvanceResult LowOrderInertialDynoV1Runtime::advance(
    LowOrderEngineCoreV1Runtime &core,
    const LiveControlOverrides &overrides) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (accepted_sample_count_ == expected_sample_count_) {
        if (!inertial_dyno_result_.has_value() || !core.completed()) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "inertial-completion-state-disagreed",
                              "inertial policy and shared core did not complete the "
                              "same fixed horizon"));
        }
        return LowOrderEngineCoreV1Completed{accepted_sample_count_};
    }

    if (accepted_sample_count_ < release_frame_index_) {
        if (overrides.any()) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "inertial-live-controls-during-held-preparation",
                "live throttle, ignition, and fuel overrides are not admitted while "
                "the inertial session is producing fixed held-speed evidence"));
        }
        const double omega =
            initial_engine_speed_rpm_ * std::numbers::pi_v<double> / 30.0;
        // Preparation is a true held-speed test-cell constraint. Use the core's
        // compiled constant-speed lane so its boundary evidence retains the exact
        // held-motion arithmetic; external motion begins only at release.
        auto core_result = core.advance(overrides);
        if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
            return fail(*failure);
        }
        if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
            return fail(
                fault(contract::FailureKind::contract_violation,
                      "inertial-core-premature-completion",
                      "shared core completed during fixed-horizon preparation"));
        }
        const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
        const auto &mechanics = core_step.mechanics.get();
        const auto &gas = core_step.gas.get();
        auto preparation = preparation_.advance(mechanics, gas);
        if (const auto *failure = std::get_if<contract::FailureContext>(&preparation)) {
            return fail(*failure);
        }
        if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
            return fail(std::move(*failure));
        }
        previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
        crank_state_ = {mechanics.theta_unwrapped_rad, omega};
        ++accepted_sample_count_;

        if (accepted_sample_count_ == release_frame_index_ &&
            (!preparation_.finalized() ||
             !preparation_.operating_point_result().has_value() ||
             !applied_lagged_loss_torque_nm_.has_value() ||
             !latest_completed_cycle_.has_value())) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "inertial-release-state-incomplete",
                "release requires fixed-sample held preparation plus one completed "
                "Chen-Flynn loss cycle",
                &mechanics));
        }
        return LowOrderInertialDynoV1StepView{
            std::cref(mechanics), std::cref(gas),
            std::get<LowOrderOperatingPointV1Step>(preparation).capture_torque};
    }

    if (!previous_indicated_gas_torque_nm_.has_value() ||
        !applied_lagged_loss_torque_nm_.has_value() ||
        !latest_completed_cycle_.has_value()) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "inertial-causal-input-missing",
                          "released motion requires prior committed indicated torque "
                          "and completed one-cycle-lag loss"));
    }

    const double applied_indicated = *previous_indicated_gas_torque_nm_;
    const double applied_loss = *applied_lagged_loss_torque_nm_;
    const auto motion_calculation = dynamics_.advance({
        crank_state_,
        applied_indicated + applied_loss,
        step_s_,
    });
    if (const auto *error =
            std::get_if<InertialCrankDynamicsError>(&motion_calculation)) {
        return fail(fault(
            dynamics_failure_kind(error->code), "inertial-crank-dynamics-failed",
            "rigid-crank step rejected input; error-code=" +
                std::to_string(static_cast<std::uint32_t>(error->code)) +
                "; domain-issue=" +
                std::to_string(static_cast<std::uint32_t>(error->domain_issue)) +
                "; rejected-rpm=" +
                std::to_string(error->angular_speed_rad_s * kRpmPerRadianPerSecond)));
    }
    const auto &motion = std::get<InertialCrankStepResult>(motion_calculation);
    const double post_step_rpm =
        motion.final_state.angular_speed_rad_s * kRpmPerRadianPerSecond;
    auto core_result =
        core.advance({post_step_rpm, motion.angular_displacement_rad}, overrides);
    if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
        return fail(*failure);
    }
    if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "inertial-core-premature-completion",
                          "shared core completed before the inertial fixed horizon"));
    }
    const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
    const auto &mechanics = core_step.mechanics.get();
    const auto &gas = core_step.gas.get();

    released_net_shaft_work_j_ +=
        motion.held_total_crank_torque_nm * motion.angular_displacement_rad;
    released_passive_brake_work_j_ +=
        motion.applied_brake_torque_nm * motion.angular_displacement_rad;
    if (!std::isfinite(released_net_shaft_work_j_) ||
        !std::isfinite(released_passive_brake_work_j_)) {
        return fail(fault(contract::FailureKind::numerical_failure,
                          "inertial-work-accumulation-nonfinite",
                          "released work accumulation became nonfinite", &mechanics));
    }

    // Telemetry for this frame describes the exact zero-order-held torque interval
    // that produced its motion. The newly committed gas torque and any newly
    // completed loss cycle become causal input only for the following step.
    const auto capture_torque = released_capture_torque(
        motion, *latest_completed_cycle_, applied_indicated, applied_loss);
    if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
        return fail(std::move(*failure));
    }
    previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
    crank_state_ = motion.final_state;
    minimum_engine_speed_rpm_ =
        std::min(minimum_engine_speed_rpm_, mechanics.engine_speed_rpm);
    maximum_engine_speed_rpm_ =
        std::max(maximum_engine_speed_rpm_, mechanics.engine_speed_rpm);
    if (!first_target_reached_frame_index_.has_value() &&
        mechanics.engine_speed_rpm >= target_engine_speed_rpm_) {
        first_target_reached_frame_index_ = mechanics.step_end_index;
    }
    ++accepted_sample_count_;
    if (accepted_sample_count_ == expected_sample_count_) {
        if (auto failure = finalize_result(mechanics); failure.has_value()) {
            return fail(std::move(*failure));
        }
    }
    return LowOrderInertialDynoV1StepView{std::cref(mechanics), std::cref(gas),
                                          capture_torque};
}

bool LowOrderInertialDynoV1Runtime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderInertialDynoV1Runtime::finalized() const noexcept {
    return inertial_dyno_result_.has_value();
}

bool LowOrderInertialDynoV1Runtime::held_preparation_active() const noexcept {
    return accepted_sample_count_ < release_frame_index_;
}

std::uint64_t LowOrderInertialDynoV1Runtime::accepted_sample_count() const noexcept {
    return accepted_sample_count_;
}

std::uint64_t LowOrderInertialDynoV1Runtime::release_frame_index() const noexcept {
    return release_frame_index_;
}

const std::optional<contract::InertialDynoResult> &
LowOrderInertialDynoV1Runtime::inertial_dyno_result() const noexcept {
    return inertial_dyno_result_;
}

} // namespace engine_sim_offline::simulation
