#include "simulation/low_order_dynamic_crank_runtime.hpp"

#include "simulation/crankwave_starter_motor.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace crankwave::simulation {
namespace {

constexpr double kRpmPerRadianPerSecond = 30.0 / std::numbers::pi_v<double>;

[[nodiscard]] bool same_articulated_coordinate(const double left,
                                               const double right) noexcept {
    constexpr double absolute_tolerance = 3.0e-15;
    constexpr double relative_tolerance = 2.0e-14;
    return std::isfinite(left) && std::isfinite(right) &&
           std::abs(left - right) <=
               absolute_tolerance + relative_tolerance * std::abs(right);
}

[[nodiscard]] std::optional<contract::GasVolumeId> chamber_for_mechanism_index(
    const std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> &bindings,
    const std::size_t mechanism_cylinder_index) noexcept {
    const auto found = std::ranges::find(
        bindings, mechanism_cylinder_index,
        &LowOrderDynamicCrankPistonWallCylinderPlan::mechanism_cylinder_index);
    return found == bindings.end()
               ? std::nullopt
               : std::optional<contract::GasVolumeId>{found->chamber_volume_id};
}

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

[[nodiscard]] contract::TorqueTelemetry held_dyno_capture_torque(
    double applied_indicated_gas_torque_nm, double applied_source_friction_torque_nm,
    double applied_actuator_torque_nm, double initial_angular_speed_rad_s) noexcept {
    const auto crank_friction =
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto piston_friction =
        contract::torque_term_mask(contract::TorqueTerm::piston_ring_friction);
    const auto applied_friction_terms = crank_friction | piston_friction;
    const auto starter = contract::torque_term_mask(contract::TorqueTerm::starter);
    const auto applied_net_terms =
        contract::indicated_gas_torque_term_mask() | applied_friction_terms | starter;
    const double engine_torque_nm =
        applied_indicated_gas_torque_nm + applied_source_friction_torque_nm;
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        applied_indicated_gas_torque_nm, contract::indicated_gas_torque_term_mask());
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory = available_classified_torque(
        applied_source_friction_torque_nm, applied_friction_terms,
        contract::friction_pump_and_accessory_torque_term_mask() &
            ~applied_friction_terms);
    result.starter = available_torque(0.0, starter);
    result.instantaneous_net_shaft = available_classified_torque(
        engine_torque_nm, applied_net_terms,
        contract::known_torque_term_mask() & ~applied_net_terms);
    result.cycle_mean_net_shaft = unavailable_torque(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.actuator = available_torque(applied_actuator_torque_nm, 0);
    result.dyno_reaction = available_torque(-applied_actuator_torque_nm, 0);
    result.cycle_work_j = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.net_bmep_pa = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.instantaneous_power_w =
        available_incomplete_quantity(engine_torque_nm * initial_angular_speed_rad_s);
    result.cycle_mean_power_w = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    return result;
}

[[nodiscard]] contract::TorqueTelemetry free_vehicle_capture_torque(
    double held_upstream_engine_torque_nm, double initial_angular_speed_rad_s,
    double applied_indicated_gas_torque_nm, double applied_source_friction_torque_nm,
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
    result.actuator = unavailable_torque(
        contract::QuantityUnavailableReason::scenario_not_applicable);
    result.dyno_reaction = unavailable_torque(
        contract::QuantityUnavailableReason::scenario_not_applicable);
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
        return "dynamic-crank-cycle-pressure-nonpositive";
    case OperatingCycleAccountingErrorCode::nonfinite_pressure:
        return "dynamic-crank-cycle-pressure-nonfinite";
    case OperatingCycleAccountingErrorCode::quadrature_failure:
        return "dynamic-crank-cycle-quadrature-failed";
    case OperatingCycleAccountingErrorCode::aggregate_loss_failure:
        return "dynamic-crank-cycle-aggregate-loss-failed";
    case OperatingCycleAccountingErrorCode::nonfinite_result:
        return "dynamic-crank-cycle-result-nonfinite";
    default:
        return "dynamic-crank-cycle-contract-violated";
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

LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime::
    LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime(
        CompiledOneLevelMasterRodArticulatedMechanism mechanism,
        const double attached_inertia,
        const double initial_configuration_body_angle_psi,
        std::vector<OneLevelMasterRodPistonWallBoundaryInput> boundaries)
    : articulated_mechanism(
          std::make_unique<CompiledOneLevelMasterRodArticulatedMechanism>(
              std::move(mechanism))),
      articulated_state(articulated_mechanism->make_state_scratch()),
      coupled_reaction_workspace(
          make_one_level_master_rod_coupled_reaction_workspace(*articulated_mechanism)),
      piston_wall_boundaries(std::move(boundaries)),
      attached_inertia_kg_m2(attached_inertia),
      configuration_body_angle_psi_rad(initial_configuration_body_angle_psi == 0.0
                                           ? 0.0
                                           : initial_configuration_body_angle_psi) {}

LowOrderDynamicCrankRuntime::LowOrderDynamicCrankRuntime(
    ScenarioControlCursor control_cursor, SharedMechanismKinematicsPlan mechanism_plan,
    std::optional<OperatingCycleAccountant> accountant,
    std::optional<FixedHorizonCycleSampler> sampler,
    std::vector<std::size_t> physical_gas_step_indices,
    std::vector<OperatingGasVolumePressureSample> pressure_samples,
    LowOrderDynamicCrankMechanismRuntime mechanism_runtime,
    std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders,
    contract::RationalRateHz rate, LowOrderExecutionExtent execution_extent,
    std::uint64_t release_frame_index, double initial_engine_speed_rpm,
    double initial_theta_rad, bool cold_bootstrap,
    double applied_positive_speed_crank_friction_torque_nm,
    double starter_maximum_torque_nm, double starter_target_speed_rad_s,
    std::optional<HeldDynoMotionPlan> held_dyno_motion,
    std::optional<FreeVehicleMotionPlan> free_vehicle_motion, std::string model_id,
    std::string profile_id, std::string scenario_id, contract::EngineId engine_id)
    : control_cursor_(std::move(control_cursor)),
      mechanism_plan_(std::move(mechanism_plan)), accountant_(std::move(accountant)),
      sampler_(std::move(sampler)),
      physical_gas_step_indices_(std::move(physical_gas_step_indices)),
      pressure_samples_(std::move(pressure_samples)),
      mechanism_runtime_(std::move(mechanism_runtime)),
      piston_wall_cylinders_(std::move(piston_wall_cylinders)),
      piston_wall_boundary_pressure_pa_abs_(piston_wall_cylinders_.size()),
      retained_piston_wall_reaction_magnitude_n_(piston_wall_cylinders_.size(), 0.0),
      candidate_piston_wall_reaction_magnitude_n_(piston_wall_cylinders_.size()),
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
      held_dyno_motion_(std::move(held_dyno_motion)),
      free_vehicle_motion_(std::move(free_vehicle_motion)),
      crank_state_{initial_theta_rad,
                   initial_engine_speed_rpm * std::numbers::pi_v<double> / 30.0},
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id),
      preparation_finalized_(cold_bootstrap) {
    if (cold_bootstrap) {
        previous_indicated_gas_torque_nm_ = 0.0;
    }
    auto *direct = std::get_if<LowOrderDynamicCrankDirectCenteredMechanismRuntime>(
        &mechanism_runtime_);
    const bool direct_phase_shape =
        direct != nullptr &&
        direct->piston_wall_boundary_phase_rad.size() == piston_wall_cylinders_.size();
    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        if (direct_phase_shape &&
            piston_wall_cylinders_[index].direct_centered.has_value()) {
            direct->piston_wall_boundary_phase_rad[index] = legacy_wrap_2pi(
                initial_theta_rad -
                piston_wall_cylinders_[index].direct_centered->geometric_tdc_rad);
        }
        piston_wall_boundary_pressure_pa_abs_[index] =
            piston_wall_cylinders_[index].initial_chamber_pressure_pa_abs;
    }
    if (direct != nullptr) {
        direct->piston_wall_boundary_angular_speed_rad_s =
            initial_engine_speed_rpm * kLegacyRpmScale;
    }
}

contract::FailureContext LowOrderDynamicCrankRuntime::fault(
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
        "none; dynamic-crank simulation terminated without fallback",
        {},
    };
}

LowOrderDynamicCrankAdvanceResult
LowOrderDynamicCrankRuntime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::stage_piston_wall_friction() {
    if (piston_wall_boundary_index_ != accepted_sample_count_ ||
        piston_wall_cylinders_.empty() ||
        piston_wall_boundary_pressure_pa_abs_.size() != piston_wall_cylinders_.size() ||
        retained_piston_wall_reaction_magnitude_n_.size() !=
            piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-piston-wall-state-disagreed",
                     "piston-wall state is not aligned with the current left "
                     "physics boundary");
    }

    if (auto *direct = std::get_if<LowOrderDynamicCrankDirectCenteredMechanismRuntime>(
            &mechanism_runtime_)) {
        if (direct->piston_wall_stages.size() != piston_wall_cylinders_.size() ||
            direct->piston_wall_boundary_phase_rad.size() !=
                piston_wall_cylinders_.size() ||
            direct->next_piston_wall_boundary_phase_rad.size() !=
                piston_wall_cylinders_.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-piston-wall-state-disagreed",
                         "direct piston-wall stage inventory differs from its "
                         "compiled cylinder inventory");
        }
        double total_torque_nm = 0.0;
        for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
            const auto &cylinder = piston_wall_cylinders_[index];
            if (!cylinder.direct_centered.has_value()) {
                return fault(contract::FailureKind::contract_violation,
                             "dynamic-crank-direct-friction-plan-missing",
                             "direct-centered mechanism cylinder has no tagged "
                             "source-friction plan",
                             nullptr, cylinder.chamber_volume_id);
            }
            const auto calculation = stage_crankwave_piston_wall_friction({
                cylinder.direct_centered->friction,
                direct->piston_wall_boundary_phase_rad[index],
                direct->piston_wall_boundary_angular_speed_rad_s,
                piston_wall_boundary_pressure_pa_abs_[index],
                retained_piston_wall_reaction_magnitude_n_[index],
            });
            if (const auto *error =
                    std::get_if<CrankwavePistonWallError>(&calculation)) {
                return fault(
                    contract::FailureKind::numerical_failure,
                    "dynamic-crank-piston-wall-friction-stage-failed",
                    "source piston-wall friction rejected the current left boundary; "
                    "issue=" +
                        std::to_string(static_cast<std::uint32_t>(error->issue)),
                    nullptr, cylinder.chamber_volume_id);
            }
            direct->piston_wall_stages[index] =
                std::get<CrankwavePistonWallFrictionStage>(calculation);
            total_torque_nm +=
                direct->piston_wall_stages[index].generalized_friction_torque_nm;
        }
        if (!std::isfinite(total_torque_nm)) {
            return fault(contract::FailureKind::numerical_failure,
                         "dynamic-crank-piston-wall-torque-nonfinite",
                         "summed source piston-wall generalized torque is nonfinite");
        }
        applied_piston_wall_friction_torque_nm_ = total_torque_nm;
        return std::nullopt;
    }

    auto &radial = std::get<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(
        mechanism_runtime_);
    radial.cached_configuration_inertia.reset();
    const auto cylinder_views =
        radial.articulated_mechanism != nullptr
            ? radial.articulated_mechanism->cylinder_views()
            : std::span<const OneLevelMasterRodCompiledCylinderView>{};
    if (radial.articulated_mechanism == nullptr ||
        radial.articulated_mechanism->cylinder_count() !=
            piston_wall_cylinders_.size() ||
        cylinder_views.size() != piston_wall_cylinders_.size() ||
        radial.articulated_state.cylinders.size() != piston_wall_cylinders_.size() ||
        radial.piston_wall_boundaries.size() != piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-master-rod-state-disagreed",
                     "compiled articulated mechanism, state, boundary, and chamber "
                     "inventories differ");
    }

    const auto inertia_calculation =
        radial.articulated_mechanism->evaluate_configuration_inertia_at_body_angle_psi(
            radial.attached_inertia_kg_m2, radial.configuration_body_angle_psi_rad,
            radial.articulated_state);
    if (const auto *error = std::get_if<OneLevelMasterRodConfigurationInertiaError>(
            &inertia_calculation)) {
        return fault(
            contract::FailureKind::numerical_failure,
            "dynamic-crank-master-rod-configuration-inertia-failed",
            "articulated configuration inertia rejected the current left boundary; "
            "issue=" +
                std::to_string(static_cast<std::uint32_t>(error->issue)) +
                "; cylinder-index=" + std::to_string(error->cylinder_index));
    }
    radial.cached_configuration_inertia =
        std::get<OneLevelMasterRodConfigurationInertia>(inertia_calculation);

    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto &cylinder = piston_wall_cylinders_[index];
        if (cylinder.direct_centered.has_value()) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-master-rod-friction-plan-disagreed",
                         "articulated mechanism cylinder retained a direct-centered "
                         "source-friction plan",
                         nullptr, cylinder.chamber_volume_id);
        }
        const auto mechanism_index = cylinder.mechanism_cylinder_index;
        bool mechanism_index_repeated = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            mechanism_index_repeated =
                mechanism_index_repeated ||
                piston_wall_cylinders_[prior].mechanism_cylinder_index ==
                    mechanism_index;
        }
        if (mechanism_index >= cylinder_views.size() || mechanism_index_repeated ||
            cylinder_views[mechanism_index].cylinder_id != cylinder.cylinder_id ||
            radial.articulated_state.cylinders[mechanism_index].cylinder_id !=
                cylinder.cylinder_id) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-master-rod-binding-disagreed",
                         "articulated piston-wall binding is not a one-to-one mapping "
                         "onto compiled mechanism order",
                         nullptr, cylinder.chamber_volume_id);
        }
        radial.piston_wall_boundaries[mechanism_index] = {
            cylinder.cylinder_id,
            piston_wall_boundary_pressure_pa_abs_[index],
            cylinder.crankcase_pressure_pa_abs,
            retained_piston_wall_reaction_magnitude_n_[index],
        };
    }
    const auto stage_calculation = stage_one_level_master_rod_piston_wall_friction(
        *radial.articulated_mechanism, radial.articulated_state,
        radial.piston_wall_boundaries, crank_state_.angular_speed_rad_s,
        radial.coupled_reaction_workspace);
    if (const auto *error =
            std::get_if<OneLevelMasterRodCoupledReactionError>(&stage_calculation)) {
        const auto gas_volume_id =
            chamber_for_mechanism_index(piston_wall_cylinders_, error->cylinder_index);
        return fault(
            contract::FailureKind::numerical_failure,
            "dynamic-crank-master-rod-friction-stage-failed",
            "articulated piston-wall friction rejected the current left boundary; "
            "issue=" +
                std::to_string(static_cast<std::uint32_t>(error->issue)) +
                "; cylinder-index=" + std::to_string(error->cylinder_index),
            nullptr, gas_volume_id);
    }
    applied_piston_wall_friction_torque_nm_ =
        std::get<OneLevelMasterRodFrictionStageResult>(stage_calculation)
            .total_generalized_friction_torque_nm;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::configuration_inertia(
    LowOrderDynamicCrankConfigurationInertiaView &output) {
    if (const auto *direct =
            std::get_if<LowOrderDynamicCrankDirectCenteredMechanismRuntime>(
                &mechanism_runtime_)) {
        const auto inertia_calculation =
            evaluate_centered_slider_crank_configuration_inertia(
                direct->configuration_inertia_plan, crank_state_.theta_rad);
        if (const auto *error =
                std::get_if<CenteredSliderCrankConfigurationInertiaError>(
                    &inertia_calculation)) {
            return fault(
                contract::FailureKind::numerical_failure,
                "dynamic-crank-configuration-inertia-failed",
                "centered-slider configuration inertia rejected the current left "
                "boundary; issue=" +
                    std::to_string(static_cast<std::uint32_t>(error->issue)) +
                    "; cylinder-index=" + std::to_string(error->cylinder_index));
        }
        const auto &inertia =
            std::get<CenteredSliderCrankConfigurationInertia>(inertia_calculation);
        output = {
            inertia.total_inertia_kg_m2,
            inertia.total_derivative_kg_m2_per_rad,
        };
        return std::nullopt;
    }

    const auto &radial =
        std::get<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(
            mechanism_runtime_);
    if (!radial.cached_configuration_inertia.has_value()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-master-rod-inertia-transaction-missing",
                     "released articulated crank motion requires the configuration "
                     "inertia staged at its current left boundary");
    }
    output = {
        radial.cached_configuration_inertia->total_inertia_kg_m2,
        radial.cached_configuration_inertia->total_derivative_kg_m2_per_rad,
    };
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::calculate_next_piston_wall_reactions(
    double angular_acceleration_rad_s2) {
    if (candidate_piston_wall_reaction_magnitude_n_.size() !=
        piston_wall_cylinders_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-piston-wall-candidate-shape-disagreed",
                     "piston-wall stage and candidate inventories differ");
    }

    if (const auto *direct =
            std::get_if<LowOrderDynamicCrankDirectCenteredMechanismRuntime>(
                &mechanism_runtime_)) {
        if (direct->piston_wall_stages.size() != piston_wall_cylinders_.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-piston-wall-candidate-shape-disagreed",
                         "direct piston-wall stage and candidate inventories differ");
        }
        for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
            const auto calculation = calculate_crankwave_next_piston_wall_reaction(
                direct->piston_wall_stages[index], angular_acceleration_rad_s2);
            if (const auto *error =
                    std::get_if<CrankwavePistonWallError>(&calculation)) {
                return fault(
                    contract::FailureKind::numerical_failure,
                    "dynamic-crank-piston-wall-reaction-failed",
                    "source-derived centered piston-wall reaction rejected the current "
                    "left boundary; issue=" +
                        std::to_string(static_cast<std::uint32_t>(error->issue)),
                    nullptr, piston_wall_cylinders_[index].chamber_volume_id);
            }
            candidate_piston_wall_reaction_magnitude_n_[index] =
                std::get<CrankwavePistonWallReaction>(calculation)
                    .wall_reaction_magnitude_n;
        }
        return std::nullopt;
    }

    auto &radial = std::get<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(
        mechanism_runtime_);
    const auto calculation = calculate_one_level_master_rod_coupled_reactions(
        *radial.articulated_mechanism, radial.articulated_state,
        crank_state_.angular_speed_rad_s, angular_acceleration_rad_s2,
        radial.coupled_reaction_workspace);
    if (const auto *error =
            std::get_if<OneLevelMasterRodCoupledReactionError>(&calculation)) {
        const auto gas_volume_id =
            chamber_for_mechanism_index(piston_wall_cylinders_, error->cylinder_index);
        return fault(
            contract::FailureKind::numerical_failure,
            "dynamic-crank-master-rod-reaction-failed",
            "leaf-first articulated piston-wall reaction rejected the current left "
            "boundary; issue=" +
                std::to_string(static_cast<std::uint32_t>(error->issue)) +
                "; cylinder-index=" + std::to_string(error->cylinder_index),
            nullptr, gas_volume_id);
    }
    const auto &reactions =
        std::get<OneLevelMasterRodCoupledReactionResult>(calculation).cylinders;
    if (reactions.size() != candidate_piston_wall_reaction_magnitude_n_.size()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-master-rod-reaction-shape-disagreed",
                     "articulated reaction result differs from the compiled chamber "
                     "inventory");
    }
    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto mechanism_index =
            piston_wall_cylinders_[index].mechanism_cylinder_index;
        if (mechanism_index >= reactions.size() ||
            reactions[mechanism_index].cylinder_id !=
                piston_wall_cylinders_[index].cylinder_id) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-master-rod-reaction-binding-disagreed",
                         "articulated reaction result does not match its compiled "
                         "piston-wall binding",
                         nullptr, piston_wall_cylinders_[index].chamber_volume_id);
        }
        candidate_piston_wall_reaction_magnitude_n_[index] =
            reactions[mechanism_index].wall_reaction_magnitude_n;
    }
    // Root big-end K is retained only in the reaction diagnostics. The existing
    // gas torque plus staged generalized friction already provide the mechanism's
    // generalized forces; adding K here would count the same load twice.
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::commit_next_piston_wall_boundary(
    const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas,
    const double exact_angular_speed_rad_s) {
    auto *direct = std::get_if<LowOrderDynamicCrankDirectCenteredMechanismRuntime>(
        &mechanism_runtime_);
    auto *radial = std::get_if<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(
        &mechanism_runtime_);
    if (mechanics.sample_index != piston_wall_boundary_index_ ||
        mechanics.step_end_index != piston_wall_boundary_index_ + 1U ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        mechanics.cylinders.size() != piston_wall_cylinders_.size() ||
        next_piston_wall_boundary_pressure_pa_abs_.size() !=
            piston_wall_cylinders_.size() ||
        (direct == nullptr) == (radial == nullptr) ||
        (direct != nullptr && direct->next_piston_wall_boundary_phase_rad.size() !=
                                  piston_wall_cylinders_.size())) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-piston-wall-boundary-shape-disagreed",
                     "committed mechanics and gas do not match the compiled "
                     "piston-wall inventory",
                     &mechanics);
    }

    for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
        const auto &plan = piston_wall_cylinders_[index];
        if (plan.mechanism_cylinder_index >= mechanics.cylinders.size() ||
            plan.chamber_gas_step_index >= gas.gas_volumes.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-piston-wall-boundary-index-invalid",
                         "compiled piston-wall index is outside the committed "
                         "mechanics or gas transaction",
                         &mechanics, plan.chamber_volume_id);
        }
        const auto &mechanism = mechanics.cylinders[plan.mechanism_cylinder_index];
        const auto &chamber = gas.gas_volumes[plan.chamber_gas_step_index];
        if (mechanism.cylinder_id != plan.cylinder_id || !chamber.physically_resolved ||
            chamber.gas_volume_id != plan.chamber_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-piston-wall-boundary-identity-disagreed",
                         "committed mechanics or chamber identity differs from its "
                         "compiled piston-wall binding",
                         &mechanics, plan.chamber_volume_id);
        }
        const double pressure_pa_abs = legacy_gas_pressure_pa(chamber.cell);
        if (direct != nullptr) {
            const auto *coordinates =
                std::get_if<DirectCylinderCoordinates>(&mechanism.coordinates);
            if (coordinates == nullptr) {
                return fault(contract::FailureKind::contract_violation,
                             "dynamic-crank-piston-wall-coordinate-kind-disagreed",
                             "direct dynamic piston-wall reaction requires "
                             "direct-cylinder coordinates",
                             &mechanics, plan.chamber_volume_id);
            }
            if (!std::isfinite(coordinates->phase_rad) ||
                !std::isfinite(pressure_pa_abs) || !(pressure_pa_abs > 0.0)) {
                return fault(contract::FailureKind::numerical_failure,
                             "dynamic-crank-piston-wall-boundary-nonphysical",
                             "next piston-wall phase or chamber pressure is "
                             "nonphysical",
                             &mechanics, plan.chamber_volume_id);
            }
            direct->next_piston_wall_boundary_phase_rad[index] = coordinates->phase_rad;
        } else {
            const auto *coordinates =
                std::get_if<OneLevelMasterRodCoordinates>(&mechanism.coordinates);
            if (coordinates == nullptr) {
                return fault(contract::FailureKind::contract_violation,
                             "dynamic-crank-piston-wall-coordinate-kind-disagreed",
                             "articulated dynamic piston-wall reaction requires "
                             "one-level master-rod coordinates",
                             &mechanics, plan.chamber_volume_id);
            }
            if (!std::isfinite(coordinates->piston_axis_position_m) ||
                !std::isfinite(coordinates->piston_axis_derivative_m_per_rad) ||
                !std::isfinite(pressure_pa_abs) || !(pressure_pa_abs > 0.0)) {
                return fault(contract::FailureKind::numerical_failure,
                             "dynamic-crank-master-rod-boundary-nonphysical",
                             "next articulated piston coordinate or chamber pressure "
                             "is nonphysical",
                             &mechanics, plan.chamber_volume_id);
            }
        }
        next_piston_wall_boundary_pressure_pa_abs_[index] = pressure_pa_abs;
    }
    const double committed_angular_speed_rad_s =
        direct != nullptr ? mechanics.angular_speed_rad_s : exact_angular_speed_rad_s;
    if (!std::isfinite(committed_angular_speed_rad_s) ||
        committed_angular_speed_rad_s < 0.0 ||
        (committed_angular_speed_rad_s == 0.0 &&
         std::signbit(committed_angular_speed_rad_s))) {
        return fault(contract::FailureKind::nonphysical_state,
                     "dynamic-crank-piston-wall-speed-invalid",
                     "next piston-wall boundary requires canonical nonnegative "
                     "angular speed",
                     &mechanics);
    }

    if (radial != nullptr) {
        const auto views =
            radial->articulated_mechanism != nullptr
                ? radial->articulated_mechanism->cylinder_views()
                : std::span<const OneLevelMasterRodCompiledCylinderView>{};
        if (!std::isfinite(mechanics.body_angle_psi_rad) ||
            radial->articulated_mechanism == nullptr ||
            views.size() != piston_wall_cylinders_.size() ||
            radial->articulated_state.cylinders.size() !=
                piston_wall_cylinders_.size()) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-master-rod-canonical-state-invalid",
                         "articulated commit requires finite canonical mechanics body "
                         "angle and exact-size state scratch",
                         &mechanics);
        }
        if (const auto state_error =
                radial->articulated_mechanism
                    ->evaluate_articulated_state_at_body_angle_psi(
                        mechanics.body_angle_psi_rad, radial->articulated_state);
            state_error.has_value()) {
            return fault(
                contract::FailureKind::numerical_failure,
                "dynamic-crank-master-rod-canonical-state-failed",
                "articulated state rejected the committed canonical mechanics angle; "
                "issue=" +
                    std::to_string(static_cast<std::uint32_t>(state_error->issue)) +
                    "; cylinder-index=" + std::to_string(state_error->cylinder_index),
                &mechanics);
        }
        for (std::size_t index = 0; index < piston_wall_cylinders_.size(); ++index) {
            const auto &binding = piston_wall_cylinders_[index];
            const auto mechanism_index = binding.mechanism_cylinder_index;
            if (mechanism_index >= views.size()) {
                return fault(
                    contract::FailureKind::contract_violation,
                    "dynamic-crank-master-rod-canonical-binding-invalid",
                    "articulated commit binding is outside compiled mechanism order",
                    &mechanics, binding.chamber_volume_id);
            }
            const auto *coordinates = std::get_if<OneLevelMasterRodCoordinates>(
                &mechanics.cylinders[mechanism_index].coordinates);
            const auto &view = views[mechanism_index];
            const auto &articulated_cylinder =
                radial->articulated_state.cylinders[mechanism_index];
            const auto &wrist = articulated_cylinder.wrist_pin;
            const double position_m =
                wrist.x_m * view.bank_axis_x + wrist.y_m * view.bank_axis_y;
            const double derivative_m_per_rad =
                wrist.dx_dtheta_m_per_rad * view.bank_axis_x +
                wrist.dy_dtheta_m_per_rad * view.bank_axis_y;
            if (view.cylinder_id != binding.cylinder_id ||
                articulated_cylinder.cylinder_id != binding.cylinder_id ||
                coordinates == nullptr ||
                !same_articulated_coordinate(position_m,
                                             coordinates->piston_axis_position_m) ||
                !same_articulated_coordinate(
                    derivative_m_per_rad,
                    coordinates->piston_axis_derivative_m_per_rad)) {
                return fault(
                    contract::FailureKind::numerical_failure,
                    "dynamic-crank-master-rod-canonical-state-disagreed",
                    "articulated M/friction geometry disagrees with the committed "
                    "mechanics body-angle authority",
                    &mechanics, binding.chamber_volume_id);
            }
        }
    }

    retained_piston_wall_reaction_magnitude_n_ =
        candidate_piston_wall_reaction_magnitude_n_;
    if (direct != nullptr) {
        direct->piston_wall_boundary_phase_rad =
            direct->next_piston_wall_boundary_phase_rad;
        direct->piston_wall_boundary_angular_speed_rad_s =
            mechanics.angular_speed_rad_s;
    } else {
        radial->configuration_body_angle_psi_rad = mechanics.body_angle_psi_rad;
        radial->cached_configuration_inertia.reset();
    }
    piston_wall_boundary_pressure_pa_abs_ = next_piston_wall_boundary_pressure_pa_abs_;
    piston_wall_boundary_index_ = mechanics.step_end_index;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::observe_preparation_cycle(
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
                     "dynamic-crank-preparation-sampler-missing",
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
                     "dynamic-crank-preparation-sampling-observation-failed",
                     "fixed-horizon sampler rejected complete-cycle accountant "
                     "evidence",
                     &mechanics, gas_volume_id);
    }
    if (std::holds_alternative<FixedHorizonCycleObservationClosed>(result)) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-preparation-sampler-closed-early",
                     "fixed-horizon sampler closed before dynamic-crank release",
                     &mechanics);
    }
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::update_preparation_accounting(
    const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas) {
    if (mechanics.sample_index != accepted_sample_count_ ||
        mechanics.step_end_index != accepted_sample_count_ + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick || mechanics.rate != rate_ ||
        gas.rate != rate_ || !std::isfinite(gas.indicated_gas_torque_nm)) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-step-transaction-disagreed",
                     "dynamic-crank policy received a noncontiguous or malformed "
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
                         "dynamic-crank-pressure-shape-disagreed",
                         "compiled physical gas-volume index is outside the current "
                         "gas transaction",
                         &mechanics, pressure_samples_[index].gas_volume_id);
        }
        const auto &volume = gas.gas_volumes[gas_index];
        if (!volume.physically_resolved ||
            volume.gas_volume_id != pressure_samples_[index].gas_volume_id) {
            return fault(contract::FailureKind::contract_violation,
                         "dynamic-crank-pressure-identity-disagreed",
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
                     "dynamic-crank transaction",
                     &mechanics, gas_volume_id);
    }

    const auto *crossing = std::get_if<OperatingCycleBoundaryCrossing>(&accounting);
    if (crossing == nullptr || !crossing->completed_cycle.has_value()) {
        return std::nullopt;
    }
    if (auto failure = observe_preparation_cycle(*crossing, mechanics);
        failure.has_value()) {
        return failure;
    }

    latest_completed_cycle_ = *crossing->completed_cycle;
    return std::nullopt;
}

std::optional<contract::FailureContext>
LowOrderDynamicCrankRuntime::finalize_preparation(
    const LegacyMechanismStep &mechanics) {
    if (!sampler_.has_value() || !accountant_.has_value()) {
        return fault(contract::FailureKind::contract_violation,
                     "dynamic-crank-preparation-components-missing",
                     "warm preparation requires its cycle accountant and sampler",
                     &mechanics);
    }
    auto result = sampler_->finalize_at_fixed_horizon();
    if (const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result)) {
        return fault(error->code == FixedHorizonCycleSamplingErrorCode::nonfinite_result
                         ? contract::FailureKind::numerical_failure
                         : contract::FailureKind::contract_violation,
                     "dynamic-crank-preparation-sampling-finalization-failed",
                     sampling_failure_summary(*error), &mechanics);
    }
    (void)std::get<FixedHorizonCycleSampled>(result);
    if (!previous_indicated_gas_torque_nm_.has_value() ||
        !latest_completed_cycle_.has_value()) {
        return fault(
            contract::FailureKind::contract_violation,
            "dynamic-crank-release-state-incomplete",
            "release requires fixed-horizon trailing-cycle evidence, prior committed "
            "indicated torque, and one completed observed cycle",
            &mechanics);
    }
    preparation_finalized_ = true;
    // The accountant and sampler certify only the held warm-preparation state.
    // Released motion uses the pristine instantaneous friction path and reports
    // cycle integration as unavailable, so no preparation observer may terminate
    // stopped, starter-driven, dyno, or drivetrain motion after this boundary.
    accountant_.reset();
    sampler_.reset();
    latest_completed_cycle_.reset();
    return std::nullopt;
}

LowOrderDynamicCrankAdvanceResult
LowOrderDynamicCrankRuntime::advance(LowOrderEngineCoreV1Runtime &core) {
    return advance(core, {});
}

LowOrderDynamicCrankAdvanceResult
LowOrderDynamicCrankRuntime::advance(LowOrderEngineCoreV1Runtime &core,
                                     const LiveControlOverrides &overrides) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    const auto terminal_sample_count = execution_extent_.finite_physics_frame_count();
    const bool held_dyno = held_dyno_motion_.has_value();
    const bool free_vehicle = free_vehicle_motion_.has_value();
    if (terminal_sample_count.has_value() &&
        accepted_sample_count_ == *terminal_sample_count) {
        if (!terminal_completed_ || !preparation_finalized_ ||
            !control_cursor_.completed() || !core.completed()) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "dynamic-crank-completion-state-disagreed",
                              "dynamic-crank policy, controls, and shared core did not "
                              "complete the same fixed horizon"));
        }
        return LowOrderEngineCoreV1Completed{accepted_sample_count_};
    }
    if (!terminal_sample_count.has_value() &&
        accepted_sample_count_ == std::numeric_limits<std::uint64_t>::max()) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "dynamic-crank-frame-counter-overflow",
                          "open-ended dynamic-crank execution exhausted its uint64 "
                          "physics "
                          "clock"));
    }

    const auto controls = control_cursor_.next();
    if (!controls.has_value() || controls->sample_index != accepted_sample_count_ ||
        controls->step_end_index != accepted_sample_count_ + 1U) {
        const bool overflow = control_cursor_.clock_overflowed();
        return fail(fault(
            contract::FailureKind::contract_violation,
            overflow ? "dynamic-crank-frame-counter-overflow"
                     : "dynamic-crank-control-step-disagreed",
            overflow
                ? "open-ended dynamic-crank control cursor exhausted its uint64 "
                  "physics clock"
                : "dynamic-crank load cursor lost the next contiguous physics step"));
    }
    if (auto failure = stage_piston_wall_friction(); failure.has_value()) {
        return fail(std::move(*failure));
    }

    // Fixed preparation is an initialization hold, not an observed actuator step.
    // HeldDyno validation requires its target lane to remain at this exact speed
    // through release; actuator/reaction telemetry begins with released motion.
    if (accepted_sample_count_ < release_frame_index_) {
        if (overrides.any()) {
            return fail(
                fault(contract::FailureKind::contract_violation,
                      "dynamic-crank-live-controls-during-held-preparation",
                      "live control overrides are not admitted during fixed held "
                      "preparation"));
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
                              "dynamic-crank-core-premature-completion",
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
        if (!exact_held_controls || state.starter_enabled ||
            state.dyno_enabled != held_dyno) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "dynamic-crank-held-condition-disagreed",
                "preparation transaction differs from the compiled RPM, throttle, "
                "or starter-off motion-owner state",
                &mechanics));
        }
        if (auto failure = update_preparation_accounting(mechanics, gas);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
        if (auto failure = commit_next_piston_wall_boundary(mechanics, gas, omega);
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
        return LowOrderDynamicCrankStepView{
            std::cref(mechanics), std::cref(gas),
            preparation_capture_torque(
                gas.indicated_gas_torque_nm,
                applied_positive_speed_crank_friction_torque_nm_ +
                    applied_piston_wall_friction_torque_nm_)};
    }

    if (!preparation_finalized_ || !previous_indicated_gas_torque_nm_.has_value()) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "dynamic-crank-causal-input-missing",
                          "released motion requires finalized preparation and prior "
                          "committed indicated torque"));
    }

    const bool has_vehicle_override = overrides.has_vehicle_selected_forward_gear ||
                                      overrides.has_vehicle_clutch_engagement ||
                                      overrides.has_vehicle_service_brake_application;
    if (free_vehicle && (overrides.has_external_resisting_torque_nm ||
                         overrides.has_dyno_target_engine_speed_rpm ||
                         overrides.has_dyno_maximum_absorbing_torque_nm ||
                         overrides.has_dyno_maximum_driving_torque_nm)) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "free-vehicle-live-controls-not-admitted",
                          "FreeVehicle does not admit external-resistance or held-dyno "
                          "live controls"));
    }
    if (!free_vehicle && has_vehicle_override) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "vehicle-live-controls-without-vehicle",
                          "gear, clutch, and service-brake controls require "
                          "FreeVehicle motion ownership"));
    }

    const double applied_indicated = *previous_indicated_gas_torque_nm_;
    const bool starter_enabled = overrides.has_starter_enabled
                                     ? overrides.starter_enabled
                                     : controls->operating_state.starter_enabled;
    if (starter_enabled &&
        (!(starter_maximum_torque_nm_ > 0.0) || !(starter_target_speed_rad_s_ > 0.0))) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "dynamic-crank-starter-capability-missing",
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
    LowOrderDynamicCrankConfigurationInertiaView inertia;
    if (auto failure = configuration_inertia(inertia); failure.has_value()) {
        return fail(std::move(*failure));
    }

    if (held_dyno) {
        auto &dyno = *held_dyno_motion_;
        const bool target_sample_available =
            !dyno.target_engine_speed_rpm.empty() &&
            (accepted_sample_count_ < dyno.target_engine_speed_rpm.size() ||
             execution_extent_.is_open_ended());
        if (starter_enabled || overrides.has_external_resisting_torque_nm ||
            !target_sample_available) {
            return fail(
                fault(contract::FailureKind::contract_violation,
                      "held-dyno-control-state-disagreed",
                      "bounded held dyno requires starter off, no external-resistance "
                      "override, and one target sample for the current physics step"));
        }
        const auto authored_target_index = std::min<std::uint64_t>(
            accepted_sample_count_, dyno.target_engine_speed_rpm.size() - 1U);
        const double target_rpm =
            overrides.has_dyno_target_engine_speed_rpm
                ? overrides.dyno_target_engine_speed_rpm
                : dyno.target_engine_speed_rpm[static_cast<std::size_t>(
                      authored_target_index)];
        const double maximum_absorbing_torque_nm =
            overrides.has_dyno_maximum_absorbing_torque_nm
                ? overrides.dyno_maximum_absorbing_torque_nm
                : dyno.maximum_absorbing_torque_nm;
        const double maximum_driving_torque_nm =
            overrides.has_dyno_maximum_driving_torque_nm
                ? overrides.dyno_maximum_driving_torque_nm
                : dyno.maximum_driving_torque_nm;
        const auto calculation = detail::advance_bounded_dyno_constraint({
            inertia.total_inertia_kg_m2,
            inertia.total_derivative_kg_m2_per_rad,
            crank_state_,
            starter_off_upstream_engine_torque_nm,
            target_rpm * std::numbers::pi_v<double> / 30.0,
            maximum_absorbing_torque_nm,
            maximum_driving_torque_nm,
            step_s_,
        });
        if (const auto *error =
                std::get_if<detail::BoundedDynoConstraintInputError>(&calculation)) {
            return fail(
                fault(contract::FailureKind::numerical_failure,
                      "held-dyno-constraint-input-invalid",
                      "bounded dyno constraint rejected the current step; issue=" +
                          std::to_string(static_cast<std::uint32_t>(error->issue))));
        }
        if (const auto *stall =
                std::get_if<detail::BoundedDynoConstraintStall>(&calculation)) {
            return fail(fault(
                contract::FailureKind::nonphysical_state, "held-dyno-crank-stalled",
                "bounded dyno actuator could not prevent a positive-speed stall; "
                "predicted-rad-s=" +
                    std::to_string(stall->predicted_final_angular_speed_rad_s)));
        }
        const auto &motion = std::get<detail::BoundedDynoConstraintStep>(calculation);
        if (auto failure = calculate_next_piston_wall_reactions(
                motion.angular_acceleration_rad_s2);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
        auto resolved_overrides = overrides;
        resolved_overrides.has_external_resisting_torque_nm = true;
        resolved_overrides.external_resisting_torque_nm =
            std::max(0.0, -motion.applied_actuator_torque_nm);
        resolved_overrides.has_starter_enabled = true;
        resolved_overrides.starter_enabled = false;
        auto core_result = core.advance(
            {motion.final_state.angular_speed_rad_s * kRpmPerRadianPerSecond,
             motion.angular_displacement_rad},
            resolved_overrides);
        if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
            return fail(*failure);
        }
        if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "held-dyno-core-premature-completion",
                              "shared core completed before the held-dyno horizon"));
        }
        const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
        const auto &mechanics = core_step.mechanics.get();
        const auto &gas = core_step.gas.get();
        const auto capture_torque =
            held_dyno_capture_torque(applied_indicated, applied_source_friction,
                                     motion.applied_actuator_torque_nm,
                                     motion.input.initial_state.angular_speed_rad_s);
        if (auto failure = commit_next_piston_wall_boundary(
                mechanics, gas, motion.final_state.angular_speed_rad_s);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
        previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
        crank_state_ = motion.final_state;
        dyno.last_state = HeldDynoRuntimeStateView{
            target_rpm,
            maximum_absorbing_torque_nm,
            maximum_driving_torque_nm,
            motion.required_actuator_torque_nm,
            motion.applied_actuator_torque_nm,
            motion.disposition,
        };
        ++accepted_sample_count_;
        if (terminal_sample_count.has_value() &&
            accepted_sample_count_ == *terminal_sample_count) {
            terminal_completed_ = true;
        }
        return LowOrderDynamicCrankStepView{std::cref(mechanics), std::cref(gas),
                                            capture_torque};
    }

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
                  "dynamic-crank-crank-dynamics-failed",
                  "nonnegative-speed configuration-dependent crank step rejected "
                  "input; issue=" +
                      std::to_string(static_cast<std::uint32_t>(error->issue))));
    }
    auto motion = std::get<detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
        motion_calculation);
    double applied_starter_torque_nm = 0.0;
    if (starter_enabled) {
        const auto starter_calculation = calculate_crankwave_starter_motor_torque({
            true,
            starter_target_speed_rad_s_,
            motion.unconstrained_predicted_final_angular_speed_rad_s,
            starter_maximum_torque_nm_,
            inertia.total_inertia_kg_m2,
            step_s_,
        });
        if (const auto *error =
                std::get_if<CrankwaveStarterMotorInputError>(&starter_calculation)) {
            return fail(
                fault(contract::FailureKind::contract_violation,
                      "dynamic-crank-starter-motor-failed",
                      "engaged source-faithful starter rejected its resolved "
                      "capability or crank input; issue=" +
                          std::to_string(static_cast<std::uint32_t>(error->issue))));
        }
        applied_starter_torque_nm =
            std::get<CrankwaveStarterMotorTorque>(starter_calculation)
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
                    "dynamic-crank-starter-crank-dynamics-failed",
                    "starter-assisted nonnegative crank step rejected input; "
                    "issue=" +
                        std::to_string(static_cast<std::uint32_t>(error->issue))));
            }
            motion =
                std::get<detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
                    motion_calculation);
        }
    }

    if (free_vehicle) {
        auto &vehicle = *free_vehicle_motion_;
        while (vehicle.next_selected_gear_boundary < vehicle.selected_gear.size() &&
               vehicle.selected_gear[vehicle.next_selected_gear_boundary].step_index <=
                   accepted_sample_count_) {
            vehicle.current_gear_index =
                vehicle.selected_gear[vehicle.next_selected_gear_boundary].gear_index;
            ++vehicle.next_selected_gear_boundary;
        }
        while (vehicle.next_clutch_boundary < vehicle.clutch_engagement_01.size() &&
               vehicle.clutch_engagement_01[vehicle.next_clutch_boundary].step_index <=
                   accepted_sample_count_) {
            vehicle.current_clutch_engagement_01 =
                vehicle.clutch_engagement_01[vehicle.next_clutch_boundary].value;
            ++vehicle.next_clutch_boundary;
        }
        while (vehicle.next_service_brake_boundary <
                   vehicle.service_brake_application_01.size() &&
               vehicle.service_brake_application_01[vehicle.next_service_brake_boundary]
                       .step_index <= accepted_sample_count_) {
            vehicle.current_service_brake_application_01 =
                vehicle
                    .service_brake_application_01[vehicle.next_service_brake_boundary]
                    .value;
            ++vehicle.next_service_brake_boundary;
        }
        if (overrides.has_vehicle_selected_forward_gear) {
            const auto ordinal = overrides.vehicle_selected_forward_gear_ordinal;
            if (ordinal > vehicle.gears.size()) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "free-vehicle-live-gear-out-of-range",
                    "live forward-gear ordinal exceeds the compiled transmission"));
            }
            vehicle.current_gear_index = ordinal == 0U
                                             ? std::optional<std::size_t>{}
                                             : std::optional<std::size_t>{ordinal - 1U};
        }
        if (overrides.has_vehicle_clutch_engagement) {
            vehicle.current_clutch_engagement_01 =
                overrides.vehicle_clutch_engagement_01;
        }
        if (overrides.has_vehicle_service_brake_application) {
            if (!(vehicle.maximum_service_brake_force_n > 0.0)) {
                return fail(
                    fault(contract::FailureKind::contract_violation,
                          "free-vehicle-live-service-brake-unavailable",
                          "live service-brake control requires positive compiled brake "
                          "capacity"));
            }
            vehicle.current_service_brake_application_01 =
                overrides.vehicle_service_brake_application_01;
        }

        std::optional<detail::ForwardGearReduction> selected_gear;
        if (vehicle.current_gear_index.has_value()) {
            if (*vehicle.current_gear_index >= vehicle.gears.size()) {
                return fail(fault(contract::FailureKind::contract_violation,
                                  "free-vehicle-gear-state-invalid",
                                  "compiled selected-gear index is outside the "
                                  "forward-gear inventory"));
            }
            selected_gear = vehicle.gears[*vehicle.current_gear_index].reduction;
        }
        const auto drivetrain_calculation =
            detail::advance_coupled_free_vehicle_drivetrain({
                inertia.total_inertia_kg_m2,
                motion.final_state.angular_speed_rad_s,
                vehicle.vehicle_mass_kg,
                vehicle.vehicle_speed_m_s,
                selected_gear,
                vehicle.maximum_clutch_torque_nm,
                vehicle.current_clutch_engagement_01,
                vehicle.drag_coefficient,
                vehicle.frontal_area_m2,
                vehicle.rolling_resistance_force_n,
                vehicle.maximum_service_brake_force_n,
                vehicle.current_service_brake_application_01,
                step_s_,
            });
        if (const auto *error =
                std::get_if<detail::CoupledFreeVehicleDrivetrainInputError>(
                    &drivetrain_calculation)) {
            return fail(
                fault(contract::FailureKind::numerical_failure,
                      "free-vehicle-drivetrain-step-failed",
                      "coupled clutch and road-load projection rejected the current "
                      "left boundary; issue=" +
                          std::to_string(static_cast<std::uint32_t>(error->issue))));
        }
        const auto &drivetrain =
            std::get<detail::CoupledFreeVehicleDrivetrainStep>(drivetrain_calculation);
        const double angular_displacement_rad =
            drivetrain.final_engine_speed_rad_s * step_s_;
        const double final_theta_rad =
            crank_state_.theta_rad + angular_displacement_rad;
        const double angular_acceleration_rad_s2 =
            (drivetrain.final_engine_speed_rad_s - crank_state_.angular_speed_rad_s) /
            step_s_;
        const double final_vehicle_distance_m =
            vehicle.vehicle_distance_m + drivetrain.final_vehicle_speed_m_s * step_s_;
        if (!std::isfinite(angular_displacement_rad) ||
            !std::isfinite(final_theta_rad) ||
            !std::isfinite(angular_acceleration_rad_s2) ||
            !std::isfinite(final_vehicle_distance_m) ||
            final_vehicle_distance_m < vehicle.vehicle_distance_m) {
            return fail(fault(contract::FailureKind::numerical_failure,
                              "free-vehicle-state-commit-failed",
                              "semi-implicit engine or vehicle state commit became "
                              "nonfinite or reversed forward distance"));
        }
        if (auto failure =
                calculate_next_piston_wall_reactions(angular_acceleration_rad_s2);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
        auto resolved_overrides = overrides;
        resolved_overrides.has_external_resisting_torque_nm = true;
        resolved_overrides.external_resisting_torque_nm = 0.0;
        resolved_overrides.has_starter_enabled = true;
        resolved_overrides.starter_enabled = starter_enabled;
        auto core_result =
            core.advance({drivetrain.final_engine_speed_rad_s * kRpmPerRadianPerSecond,
                          angular_displacement_rad},
                         resolved_overrides);
        if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
            return fail(*failure);
        }
        if (std::holds_alternative<LowOrderEngineCoreV1Completed>(core_result)) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "free-vehicle-core-premature-completion",
                              "shared core completed before the FreeVehicle "
                              "horizon"));
        }
        const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
        const auto &mechanics = core_step.mechanics.get();
        const auto &gas = core_step.gas.get();
        const auto capture_torque = free_vehicle_capture_torque(
            motion.input.held_upstream_engine_torque_nm,
            motion.input.initial_state.angular_speed_rad_s, applied_indicated,
            applied_source_friction, applied_starter_torque_nm);
        if (auto failure = commit_next_piston_wall_boundary(
                mechanics, gas, drivetrain.final_engine_speed_rad_s);
            failure.has_value()) {
            return fail(std::move(*failure));
        }
        previous_indicated_gas_torque_nm_ = gas.indicated_gas_torque_nm;
        crank_state_ = {final_theta_rad, drivetrain.final_engine_speed_rad_s};
        vehicle.vehicle_speed_m_s = drivetrain.final_vehicle_speed_m_s;
        vehicle.vehicle_distance_m = final_vehicle_distance_m;
        vehicle.last_clutch_impulse_on_engine_nm_s =
            drivetrain.applied_clutch_impulse_on_engine_nm_s;
        vehicle.last_road_load_impulse_n_s = drivetrain.applied_road_load_impulse_n_s;
        vehicle.last_clutch_slip_rad_s = drivetrain.final_clutch_slip_rad_s;
        vehicle.last_clutch_disposition = drivetrain.clutch_disposition;
        vehicle.last_clutch_torque_capacity_nm = drivetrain.clutch_torque_capacity_nm;
        vehicle.last_applied_average_clutch_torque_on_engine_nm =
            drivetrain.applied_average_clutch_torque_on_engine_nm;
        vehicle.last_road_load_disposition = drivetrain.road_load_disposition;
        vehicle.last_requested_road_load_force_n =
            drivetrain.road_load_force_capacity_n;
        vehicle.last_applied_average_road_load_force_n =
            drivetrain.applied_average_road_load_force_n;
        vehicle.has_committed_drivetrain_step = true;
        ++accepted_sample_count_;
        if (terminal_sample_count.has_value() &&
            accepted_sample_count_ == *terminal_sample_count) {
            terminal_completed_ = true;
        }
        return LowOrderDynamicCrankStepView{std::cref(mechanics), std::cref(gas),
                                            capture_torque};
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
                          "dynamic-crank-core-premature-completion",
                          "shared core completed before the dynamic-crank horizon"));
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
    if (auto failure = commit_next_piston_wall_boundary(
            mechanics, gas, motion.final_state.angular_speed_rad_s);
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
    return LowOrderDynamicCrankStepView{std::cref(mechanics), std::cref(gas),
                                        capture_torque};
}

bool LowOrderDynamicCrankRuntime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderDynamicCrankRuntime::finalized() const noexcept {
    return terminal_completed_ && !faulted();
}

bool LowOrderDynamicCrankRuntime::held_preparation_active() const noexcept {
    return accepted_sample_count_ < release_frame_index_;
}

std::uint64_t LowOrderDynamicCrankRuntime::accepted_sample_count() const noexcept {
    return accepted_sample_count_;
}

std::optional<FreeVehicleRuntimeStateView>
LowOrderDynamicCrankRuntime::free_vehicle_state() const noexcept {
    if (!free_vehicle_motion_.has_value()) {
        return std::nullopt;
    }
    const auto &vehicle = *free_vehicle_motion_;
    std::optional<std::uint32_t> selected_forward_gear_ordinal;
    if (vehicle.current_gear_index.has_value() &&
        *vehicle.current_gear_index < vehicle.gears.size()) {
        selected_forward_gear_ordinal =
            static_cast<std::uint32_t>(*vehicle.current_gear_index + 1U);
    }
    return FreeVehicleRuntimeStateView{
        vehicle.has_committed_drivetrain_step,
        crank_state_.angular_speed_rad_s * kRpmPerRadianPerSecond,
        vehicle.vehicle_speed_m_s,
        vehicle.vehicle_distance_m,
        selected_forward_gear_ordinal,
        vehicle.current_clutch_engagement_01,
        vehicle.current_service_brake_application_01,
        vehicle.last_clutch_disposition,
        vehicle.last_clutch_torque_capacity_nm,
        vehicle.last_applied_average_clutch_torque_on_engine_nm,
        vehicle.last_clutch_slip_rad_s,
        vehicle.last_road_load_disposition,
        vehicle.last_requested_road_load_force_n,
        vehicle.last_applied_average_road_load_force_n,
    };
}

std::optional<HeldDynoRuntimeStateView>
LowOrderDynamicCrankRuntime::held_dyno_state() const noexcept {
    return held_dyno_motion_.has_value() ? held_dyno_motion_->last_state : std::nullopt;
}

std::uint64_t LowOrderDynamicCrankRuntime::release_frame_index() const noexcept {
    return release_frame_index_;
}

} // namespace crankwave::simulation
