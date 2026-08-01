#pragma once

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation {

struct EngineSimV1PositiveSpeedCrankFrictionPlan {
    double running_friction_torque_magnitude_nm = 0.0;

    friend bool operator==(const EngineSimV1PositiveSpeedCrankFrictionPlan &,
                           const EngineSimV1PositiveSpeedCrankFrictionPlan &) = default;
};

struct EngineSimV1PositiveSpeedCrankFriction {
    // Signed crank torque for the admitted positive-running direction.
    double torque_nm = 0.0;

    friend bool operator==(const EngineSimV1PositiveSpeedCrankFriction &,
                           const EngineSimV1PositiveSpeedCrankFriction &) = default;
};

enum class EngineSimV1CrankFrictionIssue : std::uint8_t {
    nonfinite_running_friction_torque,
    negative_running_friction_torque,
};

struct EngineSimV1CrankFrictionError {
    EngineSimV1CrankFrictionIssue issue =
        EngineSimV1CrankFrictionIssue::nonfinite_running_friction_torque;

    friend bool operator==(const EngineSimV1CrankFrictionError &,
                           const EngineSimV1CrankFrictionError &) = default;
};

using EngineSimV1CrankFrictionCalculation =
    std::variant<EngineSimV1PositiveSpeedCrankFriction, EngineSimV1CrankFrictionError>;

// Pristine engine-sim represents crank friction as a zero-speed rotation constraint
// with symmetric torque limits. At every positive speed admitted by the current
// FreeEngine runtime the constraint saturates in the running-opposite direction.
[[nodiscard]] EngineSimV1CrankFrictionCalculation
calculate_engine_sim_v1_positive_speed_crank_friction(
    const EngineSimV1PositiveSpeedCrankFrictionPlan &plan) noexcept;

// The executable pristine engine-sim piston-friction defaults are C++ defaults.
// The similarly named .mr nodes are not connected to CombustionChamber in the
// pinned source revision.
struct EngineSimV1PistonWallCylinderPlan {
    double piston_area_m2 = 0.0;
    double crank_radius_m = 0.0;
    double connecting_rod_length_m = 0.0;
    double connecting_rod_center_of_mass_from_crank_pin_m = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;
    double crankcase_pressure_pa_abs = 0.0;

    friend bool operator==(const EngineSimV1PistonWallCylinderPlan &,
                           const EngineSimV1PistonWallCylinderPlan &) = default;
};

// One left-boundary cylinder sample. The retained wall reaction is the magnitude
// published by the preceding whole physics step. Keeping it in this input makes
// pristine engine-sim's explicit one-step lag visible to the caller.
struct EngineSimV1PistonWallStepInput {
    EngineSimV1PistonWallCylinderPlan plan;
    double phase_rad = 0.0;
    double angular_speed_rad_s = 0.0;
    double chamber_pressure_pa_abs = 0.0;
    double retained_previous_wall_reaction_magnitude_n = 0.0;

    friend bool operator==(const EngineSimV1PistonWallStepInput &,
                           const EngineSimV1PistonWallStepInput &) = default;
};

// Friction is staged before crank acceleration is known. Its generalized torque
// can therefore participate in the current rigid-crank step while the next wall
// reaction remains a result for the following physics step.
struct EngineSimV1PistonWallFrictionStage {
    EngineSimV1PistonWallStepInput input;
    double slider_axis_derivative_m_per_rad = 0.0;
    double signed_slider_axis_velocity_m_s = 0.0;
    double friction_force_magnitude_n = 0.0;
    double signed_slider_axis_friction_force_n = 0.0;
    double generalized_friction_torque_nm = 0.0;

    friend bool operator==(const EngineSimV1PistonWallFrictionStage &,
                           const EngineSimV1PistonWallFrictionStage &) = default;
};

struct EngineSimV1PistonWallReaction {
    // Signed normal force applied by the cylinder wall to the piston.
    double signed_wall_on_piston_force_n = 0.0;
    double wall_reaction_magnitude_n = 0.0;

    friend bool operator==(const EngineSimV1PistonWallReaction &,
                           const EngineSimV1PistonWallReaction &) = default;
};

enum class EngineSimV1PistonWallIssue : std::uint8_t {
    nonfinite_plan_value,
    nonpositive_piston_area,
    invalid_slider_crank_geometry,
    nonpositive_mass_property,
    nonpositive_crankcase_pressure,
    nonfinite_phase,
    nonfinite_angular_speed,
    nonfinite_chamber_pressure,
    nonpositive_chamber_pressure,
    nonfinite_retained_wall_reaction,
    negative_retained_wall_reaction,
    nonfinite_angular_acceleration,
    malformed_friction_stage,
    nonfinite_derived_value,
};

struct EngineSimV1PistonWallError {
    EngineSimV1PistonWallIssue issue =
        EngineSimV1PistonWallIssue::nonfinite_derived_value;

    friend bool operator==(const EngineSimV1PistonWallError &,
                           const EngineSimV1PistonWallError &) = default;
};

using EngineSimV1PistonWallFrictionCalculation =
    std::variant<EngineSimV1PistonWallFrictionStage, EngineSimV1PistonWallError>;
using EngineSimV1PistonWallReactionCalculation =
    std::variant<EngineSimV1PistonWallReaction, EngineSimV1PistonWallError>;

// Evaluates the exact pristine friction constants, formula, branches, and written
// operation order against the retained previous-step wall reaction.
[[nodiscard]] EngineSimV1PistonWallFrictionCalculation
stage_engine_sim_v1_piston_wall_friction(
    const EngineSimV1PistonWallStepInput &input) noexcept;

// Resolves the ideal centered-slider-crank wall reaction at the same left
// boundary as stage.input. The result is retained for the following step; it must
// never be fed back into the supplied stage.
[[nodiscard]] EngineSimV1PistonWallReactionCalculation
calculate_engine_sim_v1_next_piston_wall_reaction(
    const EngineSimV1PistonWallFrictionStage &stage,
    double angular_acceleration_rad_s2) noexcept;

} // namespace engine_sim_offline::simulation
