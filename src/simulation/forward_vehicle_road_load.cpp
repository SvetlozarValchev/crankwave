#include "simulation/forward_vehicle_road_load.hpp"

#include <algorithm>
#include <cmath>

namespace engine_sim_offline::simulation::detail {
namespace {

constexpr double kAirMolecularMassKgPerMol = 28.97e-3;
constexpr double kStandardPressurePa = 101325.0;
constexpr double kUniversalGasConstantJPerMolK = 8.31446261815324;
constexpr double kPristineRoadLoadTemperatureK = 298.15;
constexpr double kPristineAirDensityKgM3 =
    kAirMolecularMassKgPerMol * kStandardPressurePa /
    (kUniversalGasConstantJPerMolK * kPristineRoadLoadTemperatureK);

[[nodiscard]] ForwardVehicleRoadLoadInputError
input_error(ForwardVehicleRoadLoadInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] bool negative_or_negative_zero(double value) noexcept {
    return value < 0.0 || std::signbit(value);
}

} // namespace

ForwardVehicleRoadLoadCalculation
advance_forward_vehicle_road_load(const ForwardVehicleRoadLoadInput &input) noexcept {
    using Issue = ForwardVehicleRoadLoadInputIssue;
    if (!std::isfinite(input.vehicle_mass_kg)) {
        return input_error(Issue::nonfinite_vehicle_mass);
    }
    if (!(input.vehicle_mass_kg > 0.0)) {
        return input_error(Issue::nonpositive_vehicle_mass);
    }
    if (!std::isfinite(input.drag_coefficient)) {
        return input_error(Issue::nonfinite_drag_coefficient);
    }
    if (negative_or_negative_zero(input.drag_coefficient)) {
        return input_error(Issue::negative_drag_coefficient);
    }
    if (!std::isfinite(input.frontal_area_m2)) {
        return input_error(Issue::nonfinite_frontal_area);
    }
    if (negative_or_negative_zero(input.frontal_area_m2)) {
        return input_error(Issue::negative_frontal_area);
    }
    if (!std::isfinite(input.rolling_resistance_force_n)) {
        return input_error(Issue::nonfinite_rolling_resistance);
    }
    if (negative_or_negative_zero(input.rolling_resistance_force_n)) {
        return input_error(Issue::negative_rolling_resistance);
    }
    if (!std::isfinite(input.maximum_service_brake_force_n)) {
        return input_error(Issue::nonfinite_maximum_service_brake_force);
    }
    if (negative_or_negative_zero(input.maximum_service_brake_force_n)) {
        return input_error(Issue::negative_maximum_service_brake_force);
    }
    if (!std::isfinite(input.service_brake_application_01)) {
        return input_error(Issue::nonfinite_service_brake_application);
    }
    if (negative_or_negative_zero(input.service_brake_application_01) ||
        input.service_brake_application_01 > 1.0) {
        return input_error(Issue::service_brake_application_out_of_range);
    }
    if (!std::isfinite(input.initial_speed_m_s)) {
        return input_error(Issue::nonfinite_initial_speed);
    }
    if (negative_or_negative_zero(input.initial_speed_m_s)) {
        return input_error(Issue::negative_initial_speed);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(Issue::nonpositive_duration);
    }

    const double speed_squared = input.initial_speed_m_s * input.initial_speed_m_s;
    const double aerodynamic_drag_force_n = 0.5 * kPristineAirDensityKgM3 *
                                            speed_squared * input.drag_coefficient *
                                            input.frontal_area_m2;
    const double passive_road_load_force_n =
        input.rolling_resistance_force_n + aerodynamic_drag_force_n;
    const double service_brake_force_n =
        input.maximum_service_brake_force_n * input.service_brake_application_01;
    const double requested_resisting_force_n =
        passive_road_load_force_n + service_brake_force_n;
    const double available_resisting_impulse_n_s =
        requested_resisting_force_n * input.duration_s;
    const double initial_forward_momentum_n_s =
        input.vehicle_mass_kg * input.initial_speed_m_s;
    const double applied_resisting_impulse_n_s =
        std::min(available_resisting_impulse_n_s, initial_forward_momentum_n_s);
    const double applied_average_resisting_force_n =
        applied_resisting_impulse_n_s / input.duration_s;
    const bool stopped = applied_resisting_impulse_n_s == initial_forward_momentum_n_s;
    const double final_speed_m_s =
        stopped ? 0.0
                : (initial_forward_momentum_n_s - applied_resisting_impulse_n_s) /
                      input.vehicle_mass_kg;

    if (!std::isfinite(kPristineAirDensityKgM3) ||
        !std::isfinite(aerodynamic_drag_force_n) ||
        !std::isfinite(passive_road_load_force_n) ||
        !std::isfinite(service_brake_force_n) ||
        !std::isfinite(requested_resisting_force_n) ||
        !std::isfinite(available_resisting_impulse_n_s) ||
        !std::isfinite(initial_forward_momentum_n_s) ||
        !std::isfinite(applied_resisting_impulse_n_s) ||
        !std::isfinite(applied_average_resisting_force_n) ||
        !std::isfinite(final_speed_m_s)) {
        return input_error(Issue::nonfinite_derived_value);
    }

    ForwardVehicleRoadLoadDisposition disposition =
        ForwardVehicleRoadLoadDisposition::moving;
    if (stopped) {
        disposition = input.initial_speed_m_s == 0.0
                          ? ForwardVehicleRoadLoadDisposition::held_at_rest
                          : ForwardVehicleRoadLoadDisposition::stopped_within_step;
    }
    return ForwardVehicleRoadLoadStep{
        input,
        disposition,
        kPristineAirDensityKgM3,
        aerodynamic_drag_force_n,
        passive_road_load_force_n,
        service_brake_force_n,
        requested_resisting_force_n,
        applied_resisting_impulse_n_s,
        applied_average_resisting_force_n,
        final_speed_m_s,
    };
}

} // namespace engine_sim_offline::simulation::detail
