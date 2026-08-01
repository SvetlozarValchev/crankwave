#pragma once

#include "simulation/bounded_clutch_coupling.hpp"
#include "simulation/forward_vehicle_road_load.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace engine_sim_offline::simulation::detail {

struct CoupledFreeVehicleDrivetrainInput {
    double engine_inertia_kg_m2 = 0.0;
    double predicted_engine_speed_rad_s = 0.0;
    double vehicle_mass_kg = 0.0;
    double predicted_vehicle_speed_m_s = 0.0;
    std::optional<ForwardGearReduction> selected_gear;
    double maximum_clutch_torque_nm = 0.0;
    double clutch_engagement_01 = 0.0;
    double drag_coefficient = 0.0;
    double frontal_area_m2 = 0.0;
    double rolling_resistance_force_n = 0.0;
    double maximum_service_brake_force_n = 0.0;
    double service_brake_application_01 = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const CoupledFreeVehicleDrivetrainInput &,
                           const CoupledFreeVehicleDrivetrainInput &) = default;
};

struct CoupledFreeVehicleDrivetrainStep {
    CoupledFreeVehicleDrivetrainInput input;
    BoundedClutchCouplingDisposition clutch_disposition =
        BoundedClutchCouplingDisposition::neutral;
    ForwardVehicleRoadLoadDisposition road_load_disposition =
        ForwardVehicleRoadLoadDisposition::held_at_rest;
    std::optional<double> predicted_clutch_slip_rad_s;
    std::optional<double> final_clutch_slip_rad_s;
    double clutch_torque_capacity_nm = 0.0;
    double road_load_force_capacity_n = 0.0;
    double applied_clutch_impulse_on_engine_nm_s = 0.0;
    double applied_average_clutch_torque_on_engine_nm = 0.0;
    double applied_road_load_impulse_n_s = 0.0;
    double applied_average_road_load_force_n = 0.0;
    double final_engine_speed_rad_s = 0.0;
    double final_vehicle_speed_m_s = 0.0;
    std::uint32_t projection_pass_count = 0U;

    friend bool operator==(const CoupledFreeVehicleDrivetrainStep &,
                           const CoupledFreeVehicleDrivetrainStep &) = default;
};

enum class CoupledFreeVehicleDrivetrainInputIssue : std::uint8_t {
    invalid_clutch_input,
    invalid_road_load_input,
    selected_gear_vehicle_mass_mismatch,
    nonfinite_derived_value,
    negative_derived_speed,
};

struct CoupledFreeVehicleDrivetrainInputError {
    CoupledFreeVehicleDrivetrainInputIssue issue =
        CoupledFreeVehicleDrivetrainInputIssue::nonfinite_derived_value;
    std::optional<BoundedClutchCouplingInputIssue> clutch_issue;
    std::optional<ForwardVehicleRoadLoadInputIssue> road_load_issue;

    friend bool operator==(const CoupledFreeVehicleDrivetrainInputError &,
                           const CoupledFreeVehicleDrivetrainInputError &) = default;
};

using CoupledFreeVehicleDrivetrainCalculation =
    std::variant<CoupledFreeVehicleDrivetrainStep,
                 CoupledFreeVehicleDrivetrainInputError>;

// Solves pristine engine-sim's transmission-clutch row and forward road-load row
// as one bounded system. The clutch impulse also respects the runtime's canonical
// nonnegative engine-speed boundary. Each step uses 128 projected Gauss-Seidel
// passes in the source row order (clutch, then road load), so a brake can hold a
// clutch-coupled vehicle and engine at rest instead of being bypassed by sequential
// subsystem updates.
[[nodiscard]] CoupledFreeVehicleDrivetrainCalculation
advance_coupled_free_vehicle_drivetrain(
    const CoupledFreeVehicleDrivetrainInput &input) noexcept;

} // namespace engine_sim_offline::simulation::detail
