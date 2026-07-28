#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>

namespace engine_sim_offline::simulation {

struct ChenFlynnCycleMeanLossPlan {
    double constant_fmep_bar = 0.0;
    double peak_pressure_coefficient = 0.0;
    double mean_piston_speed_coefficient_bar_s_per_m = 0.0;
    double mean_piston_speed_squared_coefficient_bar_s2_per_m2 = 0.0;

    friend bool operator==(const ChenFlynnCycleMeanLossPlan &,
                           const ChenFlynnCycleMeanLossPlan &) = default;
};

struct ChenFlynnCylinderPeakPressureInput {
    std::uint32_t stable_cylinder_id = 0;
    double displacement_m3 = 0.0;
    double peak_pressure_pa_abs = 0.0;

    friend bool operator==(const ChenFlynnCylinderPeakPressureInput &,
                           const ChenFlynnCylinderPeakPressureInput &) = default;
};

struct ChenFlynnCycleMeanLossInput {
    double engine_speed_rpm = 0.0;
    double stroke_m = 0.0;
    std::span<const ChenFlynnCylinderPeakPressureInput> cylinders;
};

// This is an aggregate loss result only. It does not claim brake/net torque,
// completeness, an instantaneous friction waveform, or component-level losses.
struct ChenFlynnCycleMeanLossResult {
    double total_displacement_m3 = 0.0;
    double mean_piston_speed_m_s = 0.0;
    double displacement_weighted_peak_pressure_pa_abs = 0.0;
    double displacement_weighted_peak_pressure_bar_abs = 0.0;
    double friction_mean_effective_pressure_bar = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double running_direction_cycle_mean_loss_torque_nm = 0.0;

    friend bool operator==(const ChenFlynnCycleMeanLossResult &,
                           const ChenFlynnCycleMeanLossResult &) = default;
};

enum class ChenFlynnCycleMeanLossErrorCode : std::uint8_t {
    nonfinite_plan_coefficient,
    negative_plan_coefficient,
    zero_loss_plan,
    nonfinite_engine_speed,
    nonpositive_engine_speed,
    nonfinite_stroke,
    nonpositive_stroke,
    empty_cylinder_input,
    invalid_cylinder_identity,
    unstable_cylinder_order,
    nonfinite_cylinder_displacement,
    nonpositive_cylinder_displacement,
    nonfinite_peak_pressure,
    nonpositive_peak_pressure,
    derived_overflow,
    derived_nonpositive_result,
    derived_precision_loss,
};

inline constexpr std::size_t kNoChenFlynnInputElement =
    std::numeric_limits<std::size_t>::max();

struct ChenFlynnCycleMeanLossError {
    ChenFlynnCycleMeanLossErrorCode code =
        ChenFlynnCycleMeanLossErrorCode::zero_loss_plan;
    // Coefficient ordinal for plan errors, cylinder ordinal for cylinder errors,
    // and kNoChenFlynnInputElement when the error is not element-specific.
    std::size_t element_index = kNoChenFlynnInputElement;

    friend bool operator==(const ChenFlynnCycleMeanLossError &,
                           const ChenFlynnCycleMeanLossError &) = default;
};

using ChenFlynnCycleMeanLossCalculation =
    std::variant<ChenFlynnCycleMeanLossResult, ChenFlynnCycleMeanLossError>;

// Cylinders must be supplied in strictly increasing stable_cylinder_id order.
// Summation follows that declared order with no sorting or parallel reduction.
[[nodiscard]] ChenFlynnCycleMeanLossCalculation
calculate_chen_flynn_cycle_mean_loss(const ChenFlynnCycleMeanLossPlan &plan,
                                     const ChenFlynnCycleMeanLossInput &input) noexcept;

} // namespace engine_sim_offline::simulation
