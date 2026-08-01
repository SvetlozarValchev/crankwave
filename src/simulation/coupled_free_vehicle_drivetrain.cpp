#include "simulation/coupled_free_vehicle_drivetrain.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace engine_sim_offline::simulation::detail {
namespace {

constexpr std::uint32_t kProjectionPassCount = 128U;

[[nodiscard]] CoupledFreeVehicleDrivetrainInputError
clutch_error(BoundedClutchCouplingInputIssue issue) noexcept {
    return {
        CoupledFreeVehicleDrivetrainInputIssue::invalid_clutch_input,
        issue,
        std::nullopt,
    };
}

[[nodiscard]] CoupledFreeVehicleDrivetrainInputError
road_load_error(ForwardVehicleRoadLoadInputIssue issue) noexcept {
    return {
        CoupledFreeVehicleDrivetrainInputIssue::invalid_road_load_input,
        std::nullopt,
        issue,
    };
}

[[nodiscard]] CoupledFreeVehicleDrivetrainInputError
derived_error(CoupledFreeVehicleDrivetrainInputIssue issue) noexcept {
    return {issue, std::nullopt, std::nullopt};
}

} // namespace

CoupledFreeVehicleDrivetrainCalculation advance_coupled_free_vehicle_drivetrain(
    const CoupledFreeVehicleDrivetrainInput &input) noexcept {
    const auto clutch_calculation = advance_bounded_clutch_coupling({
        input.engine_inertia_kg_m2,
        input.predicted_engine_speed_rad_s,
        input.predicted_vehicle_speed_m_s,
        input.selected_gear,
        input.maximum_clutch_torque_nm,
        input.clutch_engagement_01,
        input.duration_s,
    });
    if (const auto *error =
            std::get_if<BoundedClutchCouplingInputError>(&clutch_calculation)) {
        return clutch_error(error->issue);
    }
    const auto &isolated_clutch =
        std::get<BoundedClutchCouplingStep>(clutch_calculation);

    const auto road_load_calculation = advance_forward_vehicle_road_load({
        input.vehicle_mass_kg,
        input.drag_coefficient,
        input.frontal_area_m2,
        input.rolling_resistance_force_n,
        input.maximum_service_brake_force_n,
        input.service_brake_application_01,
        input.predicted_vehicle_speed_m_s,
        input.duration_s,
    });
    if (const auto *error =
            std::get_if<ForwardVehicleRoadLoadInputError>(&road_load_calculation)) {
        return road_load_error(error->issue);
    }
    const auto &isolated_road_load =
        std::get<ForwardVehicleRoadLoadStep>(road_load_calculation);

    if (input.selected_gear.has_value() &&
        std::bit_cast<std::uint64_t>(input.selected_gear->input.vehicle_mass_kg) !=
            std::bit_cast<std::uint64_t>(input.vehicle_mass_kg)) {
        return derived_error(CoupledFreeVehicleDrivetrainInputIssue::
                                 selected_gear_vehicle_mass_mismatch);
    }

    const double clutch_impulse_capacity_nm_s =
        isolated_clutch.torque_capacity_nm * input.duration_s;
    const double road_load_impulse_capacity_n_s =
        isolated_road_load.requested_resisting_force_n * input.duration_s;
    if (!std::isfinite(clutch_impulse_capacity_nm_s) ||
        !std::isfinite(road_load_impulse_capacity_n_s)) {
        return derived_error(
            CoupledFreeVehicleDrivetrainInputIssue::nonfinite_derived_value);
    }

    double clutch_impulse_on_engine_nm_s = 0.0;
    double road_load_impulse_n_s = 0.0;
    std::optional<double> predicted_clutch_slip_rad_s;
    std::optional<double> final_clutch_slip_rad_s;
    double final_engine_speed_rad_s = input.predicted_engine_speed_rad_s;
    double final_vehicle_speed_m_s = input.predicted_vehicle_speed_m_s;

    if (input.selected_gear.has_value()) {
        const double reduction =
            input.selected_gear->crank_speed_per_vehicle_speed_rad_per_m;
        const double inverse_effective_inertia =
            1.0 / input.engine_inertia_kg_m2 +
            reduction * reduction / input.vehicle_mass_kg;
        const double predicted_slip = input.predicted_engine_speed_rad_s -
                                      reduction * input.predicted_vehicle_speed_m_s;
        if (!std::isfinite(inverse_effective_inertia) ||
            !(inverse_effective_inertia > 0.0) || !std::isfinite(predicted_slip)) {
            return derived_error(
                CoupledFreeVehicleDrivetrainInputIssue::nonfinite_derived_value);
        }
        predicted_clutch_slip_rad_s = predicted_slip;

        const double predicted_engine_angular_momentum_nm_s =
            input.engine_inertia_kg_m2 * input.predicted_engine_speed_rad_s;
        const double engine_stop_clutch_impulse_nm_s =
            predicted_engine_angular_momentum_nm_s == 0.0
                ? 0.0
                : -predicted_engine_angular_momentum_nm_s;
        const double clutch_impulse_lower_bound_nm_s =
            clutch_impulse_capacity_nm_s == 0.0
                ? 0.0
                : std::max(-clutch_impulse_capacity_nm_s,
                           engine_stop_clutch_impulse_nm_s);
        if (!std::isfinite(predicted_engine_angular_momentum_nm_s) ||
            !std::isfinite(engine_stop_clutch_impulse_nm_s) ||
            !std::isfinite(clutch_impulse_lower_bound_nm_s)) {
            return derived_error(
                CoupledFreeVehicleDrivetrainInputIssue::nonfinite_derived_value);
        }

        for (std::uint32_t pass = 0U; pass < kProjectionPassCount; ++pass) {
            const double unconstrained_clutch_impulse =
                -(predicted_slip +
                  road_load_impulse_n_s * reduction / input.vehicle_mass_kg) /
                inverse_effective_inertia;
            clutch_impulse_on_engine_nm_s =
                clutch_impulse_capacity_nm_s == 0.0
                    ? 0.0
                    : std::clamp(unconstrained_clutch_impulse,
                                 clutch_impulse_lower_bound_nm_s,
                                 clutch_impulse_capacity_nm_s);

            const double forward_momentum_after_clutch_n_s =
                input.vehicle_mass_kg * input.predicted_vehicle_speed_m_s -
                clutch_impulse_on_engine_nm_s * reduction;
            road_load_impulse_n_s = std::clamp(forward_momentum_after_clutch_n_s, 0.0,
                                               road_load_impulse_capacity_n_s);
        }

        final_engine_speed_rad_s =
            clutch_impulse_on_engine_nm_s == engine_stop_clutch_impulse_nm_s
                ? 0.0
                : input.predicted_engine_speed_rad_s +
                      clutch_impulse_on_engine_nm_s / input.engine_inertia_kg_m2;
        const double forward_momentum_after_clutch_n_s =
            input.vehicle_mass_kg * input.predicted_vehicle_speed_m_s -
            clutch_impulse_on_engine_nm_s * reduction;
        final_vehicle_speed_m_s =
            road_load_impulse_n_s == forward_momentum_after_clutch_n_s
                ? 0.0
                : (forward_momentum_after_clutch_n_s - road_load_impulse_n_s) /
                      input.vehicle_mass_kg;
        final_clutch_slip_rad_s =
            final_engine_speed_rad_s - reduction * final_vehicle_speed_m_s;
    } else {
        road_load_impulse_n_s = isolated_road_load.applied_resisting_impulse_n_s;
        final_vehicle_speed_m_s = isolated_road_load.final_speed_m_s;
    }

    const double applied_average_clutch_torque_on_engine_nm =
        clutch_impulse_on_engine_nm_s / input.duration_s;
    const double applied_average_road_load_force_n =
        road_load_impulse_n_s / input.duration_s;
    if (!std::isfinite(clutch_impulse_on_engine_nm_s) ||
        !std::isfinite(road_load_impulse_n_s) ||
        !std::isfinite(applied_average_clutch_torque_on_engine_nm) ||
        !std::isfinite(applied_average_road_load_force_n) ||
        !std::isfinite(final_engine_speed_rad_s) ||
        !std::isfinite(final_vehicle_speed_m_s) ||
        (final_clutch_slip_rad_s.has_value() &&
         !std::isfinite(*final_clutch_slip_rad_s))) {
        return derived_error(
            CoupledFreeVehicleDrivetrainInputIssue::nonfinite_derived_value);
    }
    if (final_engine_speed_rad_s < 0.0 || std::signbit(final_engine_speed_rad_s) ||
        final_vehicle_speed_m_s < 0.0 || std::signbit(final_vehicle_speed_m_s)) {
        return derived_error(
            CoupledFreeVehicleDrivetrainInputIssue::negative_derived_speed);
    }

    auto clutch_disposition = BoundedClutchCouplingDisposition::neutral;
    if (input.selected_gear.has_value()) {
        if (input.clutch_engagement_01 == 0.0) {
            clutch_disposition = BoundedClutchCouplingDisposition::disengaged;
        } else if (clutch_impulse_on_engine_nm_s == -clutch_impulse_capacity_nm_s) {
            clutch_disposition =
                BoundedClutchCouplingDisposition::engine_driving_torque_limited;
        } else if (clutch_impulse_on_engine_nm_s == clutch_impulse_capacity_nm_s) {
            clutch_disposition =
                BoundedClutchCouplingDisposition::vehicle_backdrive_torque_limited;
        } else {
            clutch_disposition = BoundedClutchCouplingDisposition::tracking;
        }
    }

    auto road_load_disposition = ForwardVehicleRoadLoadDisposition::moving;
    if (final_vehicle_speed_m_s == 0.0) {
        road_load_disposition =
            input.predicted_vehicle_speed_m_s == 0.0
                ? ForwardVehicleRoadLoadDisposition::held_at_rest
                : ForwardVehicleRoadLoadDisposition::stopped_within_step;
    }

    return CoupledFreeVehicleDrivetrainStep{
        input,
        clutch_disposition,
        road_load_disposition,
        predicted_clutch_slip_rad_s,
        final_clutch_slip_rad_s,
        isolated_clutch.torque_capacity_nm,
        isolated_road_load.requested_resisting_force_n,
        clutch_impulse_on_engine_nm_s,
        applied_average_clutch_torque_on_engine_nm,
        road_load_impulse_n_s,
        applied_average_road_load_force_n,
        final_engine_speed_rad_s,
        final_vehicle_speed_m_s,
        kProjectionPassCount,
    };
}

} // namespace engine_sim_offline::simulation::detail
