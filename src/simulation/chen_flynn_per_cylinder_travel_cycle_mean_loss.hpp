#pragma once

#include "crankwave/contract/common.hpp"
#include "simulation/chen_flynn_cycle_mean_loss.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <variant>

namespace crankwave::simulation {

inline constexpr std::string_view
    kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodId =
        "chen-flynn-per-cylinder-piston-travel-cycle-mean-aggregate-loss-v1";
inline constexpr std::uint32_t
    kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodVersion = 1U;

struct ChenFlynnPerCylinderPistonTravelInput {
    std::uint32_t stable_cylinder_id = 0;
    double swept_displacement_m3 = 0.0;
    double piston_axis_path_length_m_per_crank_revolution = 0.0;
    double peak_pressure_pa_abs = 0.0;

    friend bool operator==(const ChenFlynnPerCylinderPistonTravelInput &,
                           const ChenFlynnPerCylinderPistonTravelInput &) = default;
};

struct ChenFlynnPerCylinderPistonTravelCycleMeanLossInput {
    double engine_speed_rpm = 0.0;
    std::span<const ChenFlynnPerCylinderPistonTravelInput> cylinders;
};

// The work sum is authoritative. Aggregate pressure, speed, squared speed, and
// FMEP are retained as diagnostics of the exact heterogeneous reduction.
struct ChenFlynnPerCylinderPistonTravelCycleMeanLossResult {
    double total_swept_displacement_m3 = 0.0;
    double displacement_weighted_mean_piston_speed_m_s = 0.0;
    double displacement_weighted_mean_squared_piston_speed_m2_s2 = 0.0;
    double displacement_weighted_peak_pressure_pa_abs = 0.0;
    double displacement_weighted_peak_pressure_bar_abs = 0.0;
    double friction_mean_effective_pressure_bar = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double running_direction_cycle_mean_loss_torque_nm = 0.0;

    friend bool operator==(
        const ChenFlynnPerCylinderPistonTravelCycleMeanLossResult &,
        const ChenFlynnPerCylinderPistonTravelCycleMeanLossResult &) = default;
};

enum class ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode : std::uint8_t {
    nonfinite_plan_coefficient,
    negative_plan_coefficient,
    zero_loss_plan,
    nonfinite_engine_speed,
    nonpositive_engine_speed,
    empty_cylinder_input,
    invalid_cylinder_identity,
    unstable_cylinder_order,
    nonfinite_swept_displacement,
    nonpositive_swept_displacement,
    nonfinite_piston_axis_path_length,
    nonpositive_piston_axis_path_length,
    nonfinite_peak_pressure,
    nonpositive_peak_pressure,
    derived_overflow,
    derived_nonpositive_result,
    derived_precision_loss,
};

inline constexpr std::size_t
    kNoChenFlynnPerCylinderPistonTravelInputElement =
        std::numeric_limits<std::size_t>::max();

struct ChenFlynnPerCylinderPistonTravelCycleMeanLossError {
    ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode code =
        ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::zero_loss_plan;
    // Coefficient ordinal for plan errors, cylinder ordinal for cylinder errors,
    // and kNoChenFlynnPerCylinderPistonTravelInputElement otherwise.
    std::size_t element_index =
        kNoChenFlynnPerCylinderPistonTravelInputElement;

    friend bool operator==(
        const ChenFlynnPerCylinderPistonTravelCycleMeanLossError &,
        const ChenFlynnPerCylinderPistonTravelCycleMeanLossError &) = default;
};

using ChenFlynnPerCylinderPistonTravelCycleMeanLossCalculation =
    std::variant<ChenFlynnPerCylinderPistonTravelCycleMeanLossResult,
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossError>;

[[nodiscard]] std::string_view
chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_descriptor()
    noexcept;

[[nodiscard]] const contract::MethodIdentity &
chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity();

// Cylinders must be supplied in strictly increasing stable_cylinder_id order.
// No sorting, parallel reduction, nominal-stroke reconstruction, or path
// reconstruction occurs inside this calculation.
[[nodiscard]] ChenFlynnPerCylinderPistonTravelCycleMeanLossCalculation
calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
    const ChenFlynnCycleMeanLossPlan &plan,
    const ChenFlynnPerCylinderPistonTravelCycleMeanLossInput &input) noexcept;

} // namespace crankwave::simulation
