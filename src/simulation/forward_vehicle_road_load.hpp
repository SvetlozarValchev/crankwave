#pragma once

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation::detail {

enum class ForwardVehicleRoadLoadDisposition : std::uint8_t {
    moving,
    stopped_within_step,
    held_at_rest,
};

struct ForwardVehicleRoadLoadInput {
    double vehicle_mass_kg = 0.0;
    double drag_coefficient = 0.0;
    double frontal_area_m2 = 0.0;
    double rolling_resistance_force_n = 0.0;
    double maximum_service_brake_force_n = 0.0;
    double service_brake_application_01 = 0.0;
    double initial_speed_m_s = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const ForwardVehicleRoadLoadInput &,
                           const ForwardVehicleRoadLoadInput &) = default;
};

struct ForwardVehicleRoadLoadStep {
    ForwardVehicleRoadLoadInput input;
    ForwardVehicleRoadLoadDisposition disposition =
        ForwardVehicleRoadLoadDisposition::held_at_rest;
    double air_density_kg_m3 = 0.0;
    double aerodynamic_drag_force_n = 0.0;
    double passive_road_load_force_n = 0.0;
    double service_brake_force_n = 0.0;
    double requested_resisting_force_n = 0.0;
    double applied_resisting_impulse_n_s = 0.0;
    double applied_average_resisting_force_n = 0.0;
    double final_speed_m_s = 0.0;

    friend bool operator==(const ForwardVehicleRoadLoadStep &,
                           const ForwardVehicleRoadLoadStep &) = default;
};

enum class ForwardVehicleRoadLoadInputIssue : std::uint8_t {
    nonfinite_vehicle_mass,
    nonpositive_vehicle_mass,
    nonfinite_drag_coefficient,
    negative_drag_coefficient,
    nonfinite_frontal_area,
    negative_frontal_area,
    nonfinite_rolling_resistance,
    negative_rolling_resistance,
    nonfinite_maximum_service_brake_force,
    negative_maximum_service_brake_force,
    nonfinite_service_brake_application,
    service_brake_application_out_of_range,
    nonfinite_initial_speed,
    negative_initial_speed,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct ForwardVehicleRoadLoadInputError {
    ForwardVehicleRoadLoadInputIssue issue =
        ForwardVehicleRoadLoadInputIssue::nonfinite_derived_value;

    friend bool operator==(const ForwardVehicleRoadLoadInputError &,
                           const ForwardVehicleRoadLoadInputError &) = default;
};

using ForwardVehicleRoadLoadCalculation =
    std::variant<ForwardVehicleRoadLoadStep, ForwardVehicleRoadLoadInputError>;

// Advances a forward-only vehicle under pristine engine-sim's passive road-load
// equation plus the explicitly declared service-brake extension. The resisting
// impulse is unilateral and cannot reverse the vehicle through rest.
[[nodiscard]] ForwardVehicleRoadLoadCalculation
advance_forward_vehicle_road_load(const ForwardVehicleRoadLoadInput &input) noexcept;

} // namespace engine_sim_offline::simulation::detail
