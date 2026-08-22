#pragma once

#include <cstdint>
#include <variant>

namespace crankwave::simulation {

struct CrankwavePositiveSpeedCrankFrictionPlan {
    double running_friction_torque_magnitude_nm = 0.0;

    friend bool operator==(const CrankwavePositiveSpeedCrankFrictionPlan &,
                           const CrankwavePositiveSpeedCrankFrictionPlan &) = default;
};

struct CrankwavePositiveSpeedCrankFriction {
    // Signed crank torque for the admitted positive-running direction.
    double torque_nm = 0.0;

    friend bool operator==(const CrankwavePositiveSpeedCrankFriction &,
                           const CrankwavePositiveSpeedCrankFriction &) = default;
};

enum class CrankwaveCrankFrictionIssue : std::uint8_t {
    nonfinite_running_friction_torque,
    negative_running_friction_torque,
};

struct CrankwaveCrankFrictionError {
    CrankwaveCrankFrictionIssue issue =
        CrankwaveCrankFrictionIssue::nonfinite_running_friction_torque;

    friend bool operator==(const CrankwaveCrankFrictionError &,
                           const CrankwaveCrankFrictionError &) = default;
};

using CrankwaveCrankFrictionCalculation =
    std::variant<CrankwavePositiveSpeedCrankFriction, CrankwaveCrankFrictionError>;

// Pristine engine-sim represents crank friction as a zero-speed rotation constraint
// with symmetric torque limits. At every positive speed admitted by the current
// FreeEngine runtime the constraint saturates in the running-opposite direction.
[[nodiscard]] CrankwaveCrankFrictionCalculation
calculate_crankwave_positive_speed_crank_friction(
    const CrankwavePositiveSpeedCrankFrictionPlan &plan) noexcept;

// The executable pristine engine-sim piston-friction defaults are C++ defaults.
// The similarly named .mr nodes are not connected to CombustionChamber in the
// pinned source revision.
struct CrankwavePistonWallCylinderPlan {
    double piston_area_m2 = 0.0;
    double crank_radius_m = 0.0;
    double connecting_rod_length_m = 0.0;
    double connecting_rod_center_of_mass_from_crank_pin_m = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;
    double crankcase_pressure_pa_abs = 0.0;

    friend bool operator==(const CrankwavePistonWallCylinderPlan &,
                           const CrankwavePistonWallCylinderPlan &) = default;
};

// One left-boundary cylinder sample. The retained wall reaction is the magnitude
// published by the preceding whole physics step. Keeping it in this input makes
// pristine engine-sim's explicit one-step lag visible to the caller.
struct CrankwavePistonWallStepInput {
    CrankwavePistonWallCylinderPlan plan;
    double phase_rad = 0.0;
    double angular_speed_rad_s = 0.0;
    double chamber_pressure_pa_abs = 0.0;
    double retained_previous_wall_reaction_magnitude_n = 0.0;

    friend bool operator==(const CrankwavePistonWallStepInput &,
                           const CrankwavePistonWallStepInput &) = default;
};

// Friction is staged before crank acceleration is known. Its generalized torque
// can therefore participate in the current rigid-crank step while the next wall
// reaction remains a result for the following physics step.
struct CrankwavePistonWallFrictionStage {
    CrankwavePistonWallStepInput input;
    double slider_axis_derivative_m_per_rad = 0.0;
    double signed_slider_axis_velocity_m_s = 0.0;
    double friction_force_magnitude_n = 0.0;
    double signed_slider_axis_friction_force_n = 0.0;
    double generalized_friction_torque_nm = 0.0;

    friend bool operator==(const CrankwavePistonWallFrictionStage &,
                           const CrankwavePistonWallFrictionStage &) = default;
};

struct CrankwavePistonWallReaction {
    // Signed normal force applied by the cylinder wall to the piston.
    double signed_wall_on_piston_force_n = 0.0;
    double wall_reaction_magnitude_n = 0.0;

    friend bool operator==(const CrankwavePistonWallReaction &,
                           const CrankwavePistonWallReaction &) = default;
};

enum class CrankwavePistonWallIssue : std::uint8_t {
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

struct CrankwavePistonWallError {
    CrankwavePistonWallIssue issue =
        CrankwavePistonWallIssue::nonfinite_derived_value;

    friend bool operator==(const CrankwavePistonWallError &,
                           const CrankwavePistonWallError &) = default;
};

using CrankwavePistonWallFrictionCalculation =
    std::variant<CrankwavePistonWallFrictionStage, CrankwavePistonWallError>;
using CrankwavePistonWallReactionCalculation =
    std::variant<CrankwavePistonWallReaction, CrankwavePistonWallError>;

// Geometry-independent execution of pristine's piston-wall friction law. The
// caller supplies the exact derivative of outward piston-axis position with
// respect to its increasing crank coordinate. Direct and articulated mechanisms
// share this single written-order authority.
struct CrankwavePistonWallKinematicFrictionInput {
    double slider_axis_derivative_m_per_rad = 0.0;
    double angular_speed_rad_s = 0.0;
    double retained_previous_wall_reaction_magnitude_n = 0.0;

    friend bool
    operator==(const CrankwavePistonWallKinematicFrictionInput &,
               const CrankwavePistonWallKinematicFrictionInput &) = default;
};

struct CrankwavePistonWallKinematicFrictionStage {
    CrankwavePistonWallKinematicFrictionInput input;
    double signed_slider_axis_velocity_m_s = 0.0;
    double friction_force_magnitude_n = 0.0;
    double signed_slider_axis_friction_force_n = 0.0;
    double generalized_friction_torque_nm = 0.0;

    friend bool
    operator==(const CrankwavePistonWallKinematicFrictionStage &,
               const CrankwavePistonWallKinematicFrictionStage &) = default;
};

using CrankwavePistonWallKinematicFrictionCalculation =
    std::variant<CrankwavePistonWallKinematicFrictionStage,
                 CrankwavePistonWallError>;

[[nodiscard]] CrankwavePistonWallKinematicFrictionCalculation
stage_crankwave_piston_wall_kinematic_friction(
    const CrankwavePistonWallKinematicFrictionInput &input) noexcept;

// Evaluates the exact pristine friction constants, formula, branches, and written
// operation order against the retained previous-step wall reaction.
[[nodiscard]] CrankwavePistonWallFrictionCalculation
stage_crankwave_piston_wall_friction(
    const CrankwavePistonWallStepInput &input) noexcept;

// Resolves the ideal centered-slider-crank wall reaction at the same left
// boundary as stage.input. The result is retained for the following step; it must
// never be fed back into the supplied stage.
[[nodiscard]] CrankwavePistonWallReactionCalculation
calculate_crankwave_next_piston_wall_reaction(
    const CrankwavePistonWallFrictionStage &stage,
    double angular_acceleration_rad_s2) noexcept;

} // namespace crankwave::simulation
