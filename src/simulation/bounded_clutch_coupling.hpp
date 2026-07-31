#pragma once

#include <cstdint>
#include <optional>
#include <variant>

namespace engine_sim_offline::simulation::detail {

struct ForwardGearReductionInput {
    double vehicle_mass_kg = 0.0;
    double gear_ratio = 0.0;
    double differential_ratio = 0.0;
    double tire_radius_m = 0.0;

    friend bool operator==(const ForwardGearReductionInput &,
                           const ForwardGearReductionInput &) = default;
};

struct ForwardGearReduction {
    ForwardGearReductionInput input;
    double crank_speed_per_vehicle_speed_rad_per_m = 0.0;
    double crank_reflected_vehicle_inertia_kg_m2 = 0.0;
    double wheel_force_per_crank_torque_n_per_nm = 0.0;

    friend bool operator==(const ForwardGearReduction &,
                           const ForwardGearReduction &) = default;
};

enum class ForwardGearReductionInputIssue : std::uint8_t {
    nonfinite_vehicle_mass,
    nonpositive_vehicle_mass,
    nonfinite_gear_ratio,
    nonpositive_gear_ratio,
    nonfinite_differential_ratio,
    nonpositive_differential_ratio,
    nonfinite_tire_radius,
    nonpositive_tire_radius,
    nonfinite_derived_value,
};

struct ForwardGearReductionInputError {
    ForwardGearReductionInputIssue issue =
        ForwardGearReductionInputIssue::nonfinite_derived_value;

    friend bool operator==(const ForwardGearReductionInputError &,
                           const ForwardGearReductionInputError &) = default;
};

using ForwardGearReductionCalculation =
    std::variant<ForwardGearReduction, ForwardGearReductionInputError>;

[[nodiscard]] ForwardGearReductionCalculation
calculate_forward_gear_reduction(const ForwardGearReductionInput &input) noexcept;

enum class BoundedClutchCouplingDisposition : std::uint8_t {
    neutral,
    disengaged,
    tracking,
    engine_driving_torque_limited,
    vehicle_backdrive_torque_limited,
};

struct BoundedClutchCouplingInput {
    double engine_inertia_kg_m2 = 0.0;
    double predicted_engine_speed_rad_s = 0.0;
    double predicted_vehicle_speed_m_s = 0.0;
    std::optional<ForwardGearReduction> selected_gear;
    double maximum_clutch_torque_nm = 0.0;
    double clutch_engagement_01 = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const BoundedClutchCouplingInput &,
                           const BoundedClutchCouplingInput &) = default;
};

struct BoundedClutchCouplingStep {
    BoundedClutchCouplingInput input;
    BoundedClutchCouplingDisposition disposition =
        BoundedClutchCouplingDisposition::neutral;
    std::optional<double> predicted_slip_rad_s;
    std::optional<double> final_slip_rad_s;
    double torque_capacity_nm = 0.0;
    double required_torque_on_engine_nm = 0.0;
    double applied_torque_on_engine_nm = 0.0;
    double applied_wheel_force_n = 0.0;
    double final_engine_speed_rad_s = 0.0;
    double final_vehicle_speed_m_s = 0.0;

    friend bool operator==(const BoundedClutchCouplingStep &,
                           const BoundedClutchCouplingStep &) = default;
};

enum class BoundedClutchCouplingInputIssue : std::uint8_t {
    nonfinite_engine_inertia,
    nonpositive_engine_inertia,
    nonfinite_predicted_engine_speed,
    negative_predicted_engine_speed,
    nonfinite_predicted_vehicle_speed,
    negative_predicted_vehicle_speed,
    invalid_selected_gear,
    nonfinite_maximum_clutch_torque,
    negative_maximum_clutch_torque,
    nonfinite_clutch_engagement,
    clutch_engagement_out_of_range,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct BoundedClutchCouplingInputError {
    BoundedClutchCouplingInputIssue issue =
        BoundedClutchCouplingInputIssue::nonfinite_derived_value;

    friend bool operator==(const BoundedClutchCouplingInputError &,
                           const BoundedClutchCouplingInputError &) = default;
};

using BoundedClutchCouplingCalculation =
    std::variant<BoundedClutchCouplingStep, BoundedClutchCouplingInputError>;

// Applies pristine engine-sim's bounded relative-speed clutch reduction to one
// predicted engine/vehicle boundary. Positive torque acts on the engine; the wheel
// receives the exact equal-and-opposite driveline force. Neutral transmits zero.
[[nodiscard]] BoundedClutchCouplingCalculation
advance_bounded_clutch_coupling(const BoundedClutchCouplingInput &input) noexcept;

} // namespace engine_sim_offline::simulation::detail
