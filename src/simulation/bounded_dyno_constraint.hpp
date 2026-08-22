#pragma once

#include "crankwave/contract/common.hpp"
#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include <cstdint>
#include <string_view>
#include <variant>

namespace crankwave::simulation {

inline constexpr std::string_view kBoundedHeldDynoConstraintMethodId =
    "bounded-held-dyno-speed-constraint";
inline constexpr std::uint32_t kBoundedHeldDynoConstraintMethodVersion = 1U;
inline constexpr std::string_view kBoundedHeldDynoOneLevelMasterRodConstraintMethodId =
    "bounded-held-dyno-speed-constraint-one-level-master-rod-v1";
inline constexpr std::uint32_t
    kBoundedHeldDynoOneLevelMasterRodConstraintMethodVersion = 1U;

[[nodiscard]] std::string_view
bounded_held_dyno_constraint_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
bounded_held_dyno_constraint_method_identity();
[[nodiscard]] std::string_view
bounded_held_dyno_one_level_master_rod_constraint_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
bounded_held_dyno_one_level_master_rod_constraint_method_identity();

namespace detail {

enum class BoundedDynoConstraintDisposition : std::uint8_t {
    tracking,
    absorbing_torque_limited,
    driving_torque_limited,
};

// One reduced crank step under the same velocity-constraint semantics used by
// pristine engine-sim's dynamometer. Positive actuator torque drives the crank;
// negative actuator torque absorbs it.
struct BoundedDynoConstraintInput {
    double instantaneous_inertia_kg_m2 = 0.0;
    double inertia_derivative_kg_m2_per_rad = 0.0;
    PositiveSpeedRigidCrankState initial_state;
    double held_upstream_engine_torque_nm = 0.0;
    double target_angular_speed_rad_s = 0.0;
    double maximum_absorbing_torque_nm = 0.0;
    double maximum_driving_torque_nm = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const BoundedDynoConstraintInput &,
                           const BoundedDynoConstraintInput &) = default;
};

struct BoundedDynoConstraintStep {
    BoundedDynoConstraintInput input;
    PositiveSpeedRigidCrankState final_state;
    BoundedDynoConstraintDisposition disposition =
        BoundedDynoConstraintDisposition::tracking;
    double required_actuator_torque_nm = 0.0;
    double applied_actuator_torque_nm = 0.0;
    double velocity_inertia_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double angular_displacement_rad = 0.0;

    friend bool operator==(const BoundedDynoConstraintStep &,
                           const BoundedDynoConstraintStep &) = default;
};

enum class BoundedDynoConstraintInputIssue : std::uint8_t {
    nonfinite_instantaneous_inertia,
    nonpositive_instantaneous_inertia,
    nonfinite_inertia_derivative,
    nonfinite_theta,
    nonfinite_initial_angular_speed,
    nonpositive_initial_angular_speed,
    nonfinite_upstream_engine_torque,
    nonfinite_target_angular_speed,
    negative_target_angular_speed,
    nonfinite_maximum_absorbing_torque,
    negative_maximum_absorbing_torque,
    nonfinite_maximum_driving_torque,
    negative_maximum_driving_torque,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct BoundedDynoConstraintInputError {
    BoundedDynoConstraintInputIssue issue =
        BoundedDynoConstraintInputIssue::nonfinite_derived_value;

    friend bool operator==(const BoundedDynoConstraintInputError &,
                           const BoundedDynoConstraintInputError &) = default;
};

// Reverse crank motion is outside the admitted positive-speed held-dyno model.
// A saturated actuator that cannot prevent it reports the exact predicted state.
struct BoundedDynoConstraintStall {
    BoundedDynoConstraintInput input;
    double required_actuator_torque_nm = 0.0;
    double applied_actuator_torque_nm = 0.0;
    double velocity_inertia_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double predicted_final_angular_speed_rad_s = 0.0;
    double stall_time_s = 0.0;
    double stall_theta_rad = 0.0;

    friend bool operator==(const BoundedDynoConstraintStall &,
                           const BoundedDynoConstraintStall &) = default;
};

using BoundedDynoConstraintCalculation =
    std::variant<BoundedDynoConstraintStep, BoundedDynoConstraintInputError,
                 BoundedDynoConstraintStall>;

[[nodiscard]] BoundedDynoConstraintCalculation
advance_bounded_dyno_constraint(const BoundedDynoConstraintInput &input) noexcept;

} // namespace detail
} // namespace crankwave::simulation
