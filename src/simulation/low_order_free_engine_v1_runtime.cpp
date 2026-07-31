#include "simulation/low_order_free_engine_v1_runtime.hpp"

#include "simulation/engine_sim_v1_starter_motor.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

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
available_classified_torque(double value_nm, contract::TorqueTermMask included_terms,
                            contract::TorqueTermMask omitted_terms) noexcept {
    return {
        value_nm,
        contract::Availability::available,
        omitted_terms == 0 ? contract::Completeness::complete
                           : contract::Completeness::incomplete,
        contract::QuantityUnavailableReason::none,
        included_terms,
        omitted_terms,
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
available_incomplete_quantity(double value) noexcept {
    return {
        value,
        contract::Availability::available,
        contract::Completeness::incomplete,
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
preparation_capture_torque(double indicated_gas_torque_nm,
                           double applied_source_friction_torque_nm) noexcept {
    const auto crank_friction =
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto piston_friction =
        contract::torque_term_mask(contract::TorqueTerm::piston_ring_friction);
    const auto applied_friction_terms = crank_friction | piston_friction;
    const auto starter = contract::torque_term_mask(contract::TorqueTerm::starter);
    const auto applied_net_terms =
        contract::indicated_gas_torque_term_mask() | applied_friction_terms | starter;
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        indicated_gas_torque_nm, contract::indicated_gas_torque_term_mask());
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory = available_classified_torque(
        applied_source_friction_torque_nm, applied_friction_terms,
        contract::friction_pump_and_accessory_torque_term_mask() &
            ~applied_friction_terms);
    result.starter = available_torque(0.0, starter);
    result.instantaneous_net_shaft = available_classified_torque(
        indicated_gas_torque_nm + applied_source_friction_torque_nm, applied_net_terms,
        contract::known_torque_term_mask() & ~applied_net_terms);
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

[[nodiscard]] contract::TorqueTelemetry released_capture_torque(
    double held_upstream_engine_torque_nm, double held_resisting_torque_nm,
    double initial_angular_speed_rad_s, double applied_indicated_gas_torque_nm,
    double applied_source_friction_torque_nm,
    double applied_starter_torque_nm) noexcept {
    const auto crank_friction =
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto piston_friction =
        contract::torque_term_mask(contract::TorqueTerm::piston_ring_friction);
    const auto applied_friction_terms = crank_friction | piston_friction;
    const auto starter = contract::torque_term_mask(contract::TorqueTerm::starter);
    const auto applied_net_terms =
        contract::indicated_gas_torque_term_mask() | applied_friction_terms | starter;
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        applied_indicated_gas_torque_nm, contract::indicated_gas_torque_term_mask());
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory = available_classified_torque(
        applied_source_friction_torque_nm, applied_friction_terms,
        contract::friction_pump_and_accessory_torque_term_mask() &
            ~applied_friction_terms);
    result.starter = available_torque(applied_starter_torque_nm, starter);
    result.instantaneous_net_shaft = available_classified_torque(
        held_upstream_engine_torque_nm, applied_net_terms,
        contract::known_torque_term_mask() & ~applied_net_terms);
    result.cycle_mean_net_shaft = unavailable_torque(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.actuator = available_torque(-held_resisting_torque_nm, 0);
    result.dyno_reaction = available_torque(held_resisting_torque_nm, 0);
    result.cycle_work_j = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.net_bmep_pa = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.instantaneous_power_w = available_incomplete_quantity(
        held_upstream_engine_torque_nm * initial_angular_speed_rad_s);
    result.cycle_mean_power_w = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
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
    ScenarioControlCursor control_cursor,
    std::optional<OperatingCycleAccountant> accountant,
    std::optional<FixedHorizonCycleSampler> sampler,
    std::vector<std::size_t> physical_gas_step_indices,
    std::vector<OperatingGasVolumePressureSample> pressure_samples,
    CenteredSliderCrankConfigurationInertiaPlan configuration_inertia_plan,
    std::vector<LowOrderFreeEngineV1PistonWallCylinderPlan> piston_wall_cylinders,
    contract::RationalRateHz rate, LowOrderExecutionExtent execution_extent,
    std::uint64_t release_frame_index, double initial_engine_speed_rpm,
    double initial_theta_rad, bool cold_bootstrap,
    double applied_positive_speed_crank_friction_torque_nm,
    double starter_maximum_torque_nm, double starter_target_speed_rad_s,
    std::string model_id, std::string profile_id, std::string scenario_id,
    contract::EngineId engine_id)
    : control_cursor_(std::move(control_cursor)), accountant_(std::move(accountant)),
      sampler_(std::move(sampler)),
      physical_gas_step_indices_(std::move(physical_gas_step_indices)),
      pressure_samples_(std::move(pressure_samples)),
      configuration_inertia_plan_(std::move(configuration_inertia_plan)),
      piston_wall_cylinders_(std::move(piston_wall_cylinders)),
      piston_wall_boundary_phase_rad_(piston_wall_cylinders_.size()),
      piston_wall_boundary_pressure_pa_abs_(piston_wall_cylinders_.size()),
      retained_piston_wall_reaction_magnitude_n_(piston_wall_cylinders_.size(), 0.0),
      piston_wall_stages_(piston_wall_cylinders_.size()),
      candidate_piston_wall_reaction_magnitude_n_(piston_wall_cylinders_.size()),
      next_piston_wall_boundary_phase_rad_(piston_wall_cylinders_.size()),
      next_piston_wall_boundary_pressure_pa_abs_(piston_wall_cylinders_.size()),
      rate_(rate), execution_extent_(execution_extent),
      release_frame_index_(release_frame_index),
      step_s_(static_cast<double>(rate.denominator) /
              static_cast<double>(rate.numerator)),
      initial_engine_speed_rpm_(initial_engine_speed_rpm),
      applied_positive_speed_crank_friction_torque_nm_(
          applied_positive_speed_crank_friction_torque_nm),
      starter_maximum_torque_nm_(starter_maximum_torque_nm),
      starter_target_speed_rad_s_(starter_target_speed_rad_s),
      piston_wall_boundary_angular_speed_rad_s_(initial_engine_speed_rpm *
                                                kLegacyRpmScale),
      crank_state_{initial_theta_rad,
                   initial_engine_speed_rpm * std::numbers::pi_v<double> / 30.0},
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id),
      preparation_finalized_(cold_bootstrap) {
    if (cold_bootstrap) {
        previous_indicated_gas_torque_nm_ = 0.0;
    }
    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        piston_wall_boundary_phase_rad_[index] = legacy_wrap_2pi(
            initial_theta_rad - piston_wall_cylinders_[index].geometric_tdc_rad);
        piston_wall_boundary_pressure_pa_abs_[index] =
            piston_wall_cylinders_[index].initial_chamber_pressure_pa_abs;
    }
}

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
LowOrderFreeEngineV1Runtime::stage_piston_wall_friction() {
    if (piston_wall_boundary_index_ != accepted_sample_count_ ||
        piston_wall_cylinders_.empty() ||
        piston_wall_boundary_phase_rad_.size() != piston_wall_cylinders_.size() ||
        piston_wall_boundary_pressure_pa_abs_.size() != piston_wall_cylinders_.size() ||
        retained_piston_wall_reaction_magnitude_n_.size() !=
            piston_wall_cylinders_.size() ||
        piston_wall_stages_.size() != piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-piston-wall-state-disagreed",
                     "piston-wall state is not aligned with the current left "
                     "physics boundary");
    }

    double total_torque_nm = 0.0;
    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto &cylinder = piston_wall_cylinders_[index];
        const auto calculation = stage_engine_sim_v1_piston_wall_friction({
            cylinder.friction,
            piston_wall_boundary_phase_rad_[index],
            piston_wall_boundary_angular_speed_rad_s_,
            piston_wall_boundary_pressure_pa_abs_[index],
            retained_piston_wall_reaction_magnitude_n_[index],
        });
        if (const auto *error = std::get_if<EngineSimV1PistonWallError>(&calculation)) {
            return fault(
                contract::FailureKind::numerical_failure,
                "free-engine-piston-wall-friction-stage-failed",
                "source piston-wall friction rejected the current left boundary; "
                "issue=" +
                    std::to_string(static_cast<std::uint32_t>(error->issue)),
                nullptr, cylinder.chamber_volume_id);
        }
        piston_wall_stages_[index] =
            std::get<EngineSimV1PistonWallFrictionStage>(calculation);
        total_torque_nm += piston_wall_stages_[index].generalized_friction_torque_nm;
    }
    if (!std::isfinite(total_torque_nm)) {
        return fault(contract::FailureKind::numerical_failure,
                     "free-engine-piston-wall-torque-nonfinite",
                     "summed source piston-wall generalized torque is nonfinite");
    }
    applied_piston_wall_friction_torque_nm_ = total_torque_nm;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::calculate_next_piston_wall_reactions(
    double angular_acceleration_rad_s2) {
    if (piston_wall_stages_.size() != piston_wall_cylinders_.size() ||
        candidate_piston_wall_reaction_magnitude_n_.size() !=
            piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-piston-wall-candidate-shape-disagreed",
                     "piston-wall stage and candidate inventories differ");
    }
    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto calculation = calculate_engine_sim_v1_next_piston_wall_reaction(
            piston_wall_stages_[index], angular_acceleration_rad_s2);
        if (const auto *error = std::get_if<EngineSimV1PistonWallError>(&calculation)) {
            return fault(
                contract::FailureKind::numerical_failure,
                "free-engine-piston-wall-reaction-failed",
                "source-derived centered piston-wall reaction rejected the current "
                "left boundary; issue=" +
                    std::to_string(static_cast<std::uint32_t>(error->issue)),
                nullptr, piston_wall_cylinders_[index].chamber_volume_id);
        }
        candidate_piston_wall_reaction_magnitude_n_[index] =
            std::get<EngineSimV1PistonWallReaction>(calculation)
                .wall_reaction_magnitude_n;
    }
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::commit_next_piston_wall_boundary(
    const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas) {
    if (mechanics.sample_index != piston_wall_boundary_index_ ||
        mechanics.step_end_index != piston_wall_boundary_index_ + 1U ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        mechanics.cylinders.size() != piston_wall_cylinders_.size() ||
        next_piston_wall_boundary_phase_rad_.size() != piston_wall_cylinders_.size() ||
        next_piston_wall_boundary_pressure_pa_abs_.size() !=
            piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-piston-wall-boundary-shape-disagreed",
                     "committed mechanics and gas do not match the compiled "
                     "piston-wall inventory",
                     &mechanics);
    }

    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto &plan = piston_wall_cylinders_[index];
        if (plan.mechanism_cylinder_index >= mechanics.cylinders.size() ||
            plan.chamber_gas_step_index >= gas.gas_volumes.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "free-engine-piston-wall-boundary-index-invalid",
                         "compiled piston-wall index is outside the committed "
                         "mechanics or gas transaction",
                         &mechanics, plan.chamber_volume_id);
        }
        const auto &mechanism = mechanics.cylinders[plan.mechanism_cylinder_index];
        const auto &chamber = gas.gas_volumes[plan.chamber_gas_step_index];
        if (mechanism.cylinder_id != plan.cylinder_id || !chamber.physically_resolved ||
            chamber.gas_volume_id != plan.chamber_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "free-engine-piston-wall-boundary-identity-disagreed",
                         "committed mechanics or chamber identity differs from its "
                         "compiled piston-wall binding",
                         &mechanics, plan.chamber_volume_id);
        }
        const double pressure_pa_abs = legacy_gas_pressure_pa(chamber.cell);
        if (!std::isfinite(mechanism.phase_rad) || !std::isfinite(pressure_pa_abs) ||
            !(pressure_pa_abs > 0.0)) {
            return fault(contract::FailureKind::numerical_failure,
                         "free-engine-piston-wall-boundary-nonphysical",
                         "next piston-wall phase or chamber pressure is nonphysical",
                         &mechanics, plan.chamber_volume_id);
        }
        next_piston_wall_boundary_phase_rad_[index] = mechanism.phase_rad;
        next_piston_wall_boundary_pressure_pa_abs_[index] = pressure_pa_abs;
    }
    if (!std::isfinite(mechanics.angular_speed_rad_s) ||
        mechanics.angular_speed_rad_s < 0.0 ||
        (mechanics.angular_speed_rad_s == 0.0 &&
         std::signbit(mechanics.angular_speed_rad_s))) {
        return fault(contract::FailureKind::nonphysical_state,
                     "free-engine-piston-wall-speed-invalid",
                     "next piston-wall boundary requires canonical nonnegative "
                     "angular speed",
                     &mechanics);
    }

    retained_piston_wall_reaction_magnitude_n_ =
        candidate_piston_wall_reaction_magnitude_n_;
    piston_wall_boundary_phase_rad_ = next_piston_wall_boundary_phase_rad_;
    piston_wall_boundary_pressure_pa_abs_ = next_piston_wall_boundary_pressure_pa_abs_;
    piston_wall_boundary_angular_speed_rad_s_ = mechanics.angular_speed_rad_s;
    piston_wall_boundary_index_ = mechanics.step_end_index;
    return std::nullopt;
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
    if (!sampler_.has_value()) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-preparation-sampler-missing",
                     "warm preparation observed a cycle without a compiled sampler",
                     &mechanics);
    }
    const auto result = sampler_->observe({
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

    if (!accountant_.has_value()) {
        return std::nullopt;
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
    const auto accounting = accountant_->advance({
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
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderFreeEngineV1Runtime::finalize_preparation(
    const LegacyMechanismStep &mechanics) {
    if (!sampler_.has_value() || !accountant_.has_value()) {
        return fault(contract::FailureKind::contract_violation,
                     "free-engine-preparation-components-missing",
                     "warm preparation requires its cycle accountant and sampler",
                     &mechanics);
    }
    auto result = sampler_->finalize_at_fixed_horizon();
    if (const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result)) {
        return fault(error->code == FixedHorizonCycleSamplingErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "free-engine-preparation-sampling-finalization-failed",
                     sampling_failure_summary(*error), &mechanics);
    }
    (void)std::get<FixedHorizonCycleSampled>(result);
    if (!previous_indicated_gas_torque_nm_.has_value() ||
        !latest_completed_cycle_.has_value()) {
        return fault(
            contract::FailureKind::contract_violation,
            "free-engine-release-state-incomplete",
            "release requires fixed-horizon trailing-cycle evidence, prior committed "
            "indicated torque, and one completed observed cycle",
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
    const auto terminal_sample_count = execution_extent_.finite_physics_frame_count();
    if (terminal_sample_count.has_value() &&
        accepted_sample_count_ == *terminal_sample_count) {
        if (!terminal_completed_ || !preparation_finalized_ ||
            !control_cursor_.completed() || !core.completed()) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "free-engine-completion-state-disagreed",
                              "free-engine policy, controls, and shared core did not "
                              "complete the same fixed horizon"));
        }
        return LowOrderEngineCoreV1Completed{accepted_sample_count_};
    }
    if (!terminal_sample_count.has_value() &&
        accepted_sample_count_ == std::numeric_limits<std::uint64_t>::max()) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-engine-frame-counter-overflow",
                          "open-ended FreeEngine exhausted its uint64 physics "
                          "clock"));
    }

    const auto controls = control_cursor_.next();
    if (!controls.has_value() || controls->sample_index != accepted_sample_count_ ||
        controls->step_end_index != accepted_sample_count_ + 1U) {
        const bool overflow = control_cursor_.clock_overflowed();
        return fail(fault(
            contract::FailureKind::contract_violation,
            overflow ? "free-engine-frame-counter-overflow"
                     : "free-engine-control-step-disagreed",
            overflow
                ? "open-ended FreeEngine control cursor exhausted its uint64 "
                  "physics clock"
                : "free-engine load cursor lost the next contiguous physics step"));
    }
    if (auto failure = stage_piston_wall_friction(); failure.has_value()) {
        return fail(std::move(*failure));
    }

    if (accepted_sample_count_ < release_frame_index_) {
        if (overrides.any()) {
            return fail(
                fault(contract::FailureKind::contract_violation,
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
        if (auto failure = calculate_next_piston_wall_reactions(0.0);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
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
        if (!exact_held_controls || state.starter_enabled || state.dyno_enabled) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "free-engine-held-condition-disagreed",
                "preparation transaction differs from the compiled RPM, throttle, "
                "or starter-off and dyno-off free-engine state",
                &mechanics));
        }
        if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
            return fail(std::move(*failure));
        }
        if (auto failure = commit_next_piston_wall_boundary(mechanics, gas);
            failure.has_value()) {
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
            preparation_capture_torque(
                gas.indicated_gas_torque_nm,
                applied_positive_speed_crank_friction_torque_nm_ +
                    applied_piston_wall_friction_torque_nm_)};
    }

    if (!preparation_finalized_ || !previous_indicated_gas_torque_nm_.has_value()) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-engine-causal-input-missing",
                          "released motion requires finalized preparation and prior "
                          "committed indicated torque"));
    }

    const double applied_indicated = *previous_indicated_gas_torque_nm_;
    const bool starter_enabled = overrides.has_starter_enabled
                                     ? overrides.starter_enabled
                                     : controls->operating_state.starter_enabled;
    if (starter_enabled &&
        (!(starter_maximum_torque_nm_ > 0.0) || !(starter_target_speed_rad_s_ > 0.0))) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-engine-starter-capability-missing",
                          "starter engagement requires a compiled cranking starter "
                          "with positive maximum torque and target speed"));
    }
    const double applied_external_resisting_torque_nm =
        overrides.has_external_resisting_torque_nm
            ? overrides.external_resisting_torque_nm
            : controls->external_resisting_torque_nm;
    const double applied_crank_friction_torque_nm =
        crank_state_.angular_speed_rad_s > 0.0 || starter_enabled
            ? applied_positive_speed_crank_friction_torque_nm_
            : std::clamp(
                  applied_external_resisting_torque_nm -
                      (applied_indicated + applied_piston_wall_friction_torque_nm_),
                  applied_positive_speed_crank_friction_torque_nm_,
                  -applied_positive_speed_crank_friction_torque_nm_);
    const double applied_source_friction =
        applied_crank_friction_torque_nm + applied_piston_wall_friction_torque_nm_;
    const double starter_off_upstream_engine_torque_nm =
        applied_indicated + applied_source_friction;
    const auto inertia_calculation =
        evaluate_centered_slider_crank_configuration_inertia(
            configuration_inertia_plan_, crank_state_.theta_rad);
    if (const auto *error = std::get_if<CenteredSliderCrankConfigurationInertiaError>(
            &inertia_calculation)) {
        return fail(
            fault(contract::FailureKind::numerical_failure,
                  "free-engine-configuration-inertia-failed",
                  "centered-slider configuration inertia rejected the current left "
                  "boundary; issue=" +
                      std::to_string(static_cast<std::uint32_t>(error->issue)) +
                      "; cylinder-index=" + std::to_string(error->cylinder_index)));
    }
    const auto &inertia =
        std::get<CenteredSliderCrankConfigurationInertia>(inertia_calculation);
    auto motion_calculation =
        detail::advance_nonnegative_speed_configuration_dependent_crank_zoh({
            inertia.total_inertia_kg_m2,
            inertia.total_derivative_kg_m2_per_rad,
            crank_state_,
            starter_off_upstream_engine_torque_nm,
            applied_external_resisting_torque_nm,
            step_s_,
        });
    if (const auto *error = std::get_if<
            detail::NonnegativeSpeedConfigurationDependentCrankZohInputError>(
            &motion_calculation)) {
        return fail(
            fault(contract::FailureKind::numerical_failure,
                  "free-engine-crank-dynamics-failed",
                  "nonnegative-speed configuration-dependent crank step rejected "
                  "input; issue=" +
                      std::to_string(static_cast<std::uint32_t>(error->issue))));
    }
    auto motion = std::get<detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
        motion_calculation);
    double applied_starter_torque_nm = 0.0;
    if (starter_enabled) {
        const auto starter_calculation = calculate_engine_sim_v1_starter_motor_torque({
            true,
            starter_target_speed_rad_s_,
            motion.unconstrained_predicted_final_angular_speed_rad_s,
            starter_maximum_torque_nm_,
            inertia.total_inertia_kg_m2,
            step_s_,
        });
        if (const auto *error =
                std::get_if<EngineSimV1StarterMotorInputError>(&starter_calculation)) {
            return fail(
                fault(contract::FailureKind::contract_violation,
                      "free-engine-starter-motor-failed",
                      "engaged source-faithful starter rejected its resolved "
                      "capability or crank input; issue=" +
                          std::to_string(static_cast<std::uint32_t>(error->issue))));
        }
        applied_starter_torque_nm =
            std::get<EngineSimV1StarterMotorTorque>(starter_calculation)
                .applied_crank_torque_nm;
        if (applied_starter_torque_nm > 0.0) {
            motion_calculation =
                detail::advance_nonnegative_speed_configuration_dependent_crank_zoh({
                    inertia.total_inertia_kg_m2,
                    inertia.total_derivative_kg_m2_per_rad,
                    crank_state_,
                    starter_off_upstream_engine_torque_nm + applied_starter_torque_nm,
                    applied_external_resisting_torque_nm,
                    step_s_,
                });
            if (const auto *error = std::get_if<
                    detail::NonnegativeSpeedConfigurationDependentCrankZohInputError>(
                    &motion_calculation)) {
                return fail(fault(
                    contract::FailureKind::numerical_failure,
                    "free-engine-starter-crank-dynamics-failed",
                    "starter-assisted nonnegative crank step rejected input; "
                    "issue=" +
                        std::to_string(static_cast<std::uint32_t>(error->issue))));
            }
            motion =
                std::get<detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
                    motion_calculation);
        }
    }
    if (auto failure =
            calculate_next_piston_wall_reactions(motion.angular_acceleration_rad_s2);
        failure.has_value()) {
        return fail(std::move(*failure));
    }
    const double post_step_rpm =
        motion.final_state.angular_speed_rad_s * kRpmPerRadianPerSecond;
    auto resolved_overrides = overrides;
    resolved_overrides.has_external_resisting_torque_nm = true;
    resolved_overrides.external_resisting_torque_nm =
        applied_external_resisting_torque_nm;
    resolved_overrides.has_starter_enabled = true;
    resolved_overrides.starter_enabled = starter_enabled;
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
    // gas state becomes causal input only for the following frame.
    const auto capture_torque = released_capture_torque(
        motion.input.held_upstream_engine_torque_nm,
        motion.input.held_resisting_torque_nm,
        motion.input.initial_state.angular_speed_rad_s, applied_indicated,
        applied_source_friction, applied_starter_torque_nm);
    if (mechanics.engine_speed_rpm > 0.0) {
        if (auto failure = update_accounting(mechanics, gas); failure.has_value()) {
            return fail(std::move(*failure));
        }
    }
    if (auto failure = commit_next_piston_wall_boundary(mechanics, gas);
        failure.has_value()) {
        return fail(std::move(*failure));
    }
    previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
    crank_state_ = motion.final_state;
    ++accepted_sample_count_;
    if (terminal_sample_count.has_value() &&
        accepted_sample_count_ == *terminal_sample_count) {
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
