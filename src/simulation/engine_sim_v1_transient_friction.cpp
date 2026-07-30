#include "simulation/engine_sim_v1_transient_friction.hpp"

#include <cmath>
#include <optional>

namespace engine_sim_offline::simulation {
namespace {

inline constexpr double kPristineRootTwo =
    1.41421356237309504880168872420969807856967187537694807317667973799;
inline constexpr double kPristineEuler =
    2.71828182845904523536028747135266249775724709369995;
inline constexpr double kPristineFrictionCoefficient = 0.06;
inline constexpr double kPristineBreakawayFrictionN = 50.0;
inline constexpr double kPristineBreakawayFrictionVelocityMPerS = 0.1;
inline constexpr double kPristineViscousFrictionCoefficientNPerMPerS = 20.0;
inline constexpr double kPristineZeroSpeedAttenuationLimitMPerS = 1.0e-3;

[[nodiscard]] EngineSimV1PistonWallError
piston_wall_error(EngineSimV1PistonWallIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] bool finite_plan(const EngineSimV1PistonWallCylinderPlan &plan) noexcept {
    return std::isfinite(plan.piston_area_m2) && std::isfinite(plan.crank_radius_m) &&
           std::isfinite(plan.connecting_rod_length_m) &&
           std::isfinite(plan.piston_mass_kg) &&
           std::isfinite(plan.connecting_rod_mass_kg) &&
           std::isfinite(plan.connecting_rod_inertia_kg_m2) &&
           std::isfinite(plan.crankcase_pressure_pa_abs);
}

[[nodiscard]] std::variant<double, EngineSimV1PistonWallError>
slider_axis_derivative(const EngineSimV1PistonWallStepInput &input) noexcept {
    const double sine = std::sin(input.phase_rad);
    const double cosine = std::cos(input.phase_rad);
    const double crank_radius_squared =
        input.plan.crank_radius_m * input.plan.crank_radius_m;
    const double radicand =
        input.plan.connecting_rod_length_m * input.plan.connecting_rod_length_m -
        crank_radius_squared * sine * sine;
    if (!std::isfinite(sine) || !std::isfinite(cosine) ||
        !std::isfinite(crank_radius_squared) || !std::isfinite(radicand) ||
        !(radicand > 0.0)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_derived_value);
    }
    const double root = std::sqrt(radicand);
    const double piston_travel_derivative =
        input.plan.crank_radius_m * sine + crank_radius_squared * sine * cosine / root;
    const double derivative = -piston_travel_derivative;
    if (!std::isfinite(root) || !std::isfinite(piston_travel_derivative) ||
        !std::isfinite(derivative)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_derived_value);
    }
    return derivative;
}

[[nodiscard]] std::optional<EngineSimV1PistonWallError>
validate_step_input(const EngineSimV1PistonWallStepInput &input) noexcept {
    if (!finite_plan(input.plan)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_plan_value);
    }
    if (!(input.plan.piston_area_m2 > 0.0)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonpositive_piston_area);
    }
    if (!(input.plan.crank_radius_m > 0.0) ||
        !(input.plan.connecting_rod_length_m > input.plan.crank_radius_m)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::invalid_slider_crank_geometry);
    }
    if (!(input.plan.piston_mass_kg > 0.0) ||
        !(input.plan.connecting_rod_mass_kg > 0.0) ||
        !(input.plan.connecting_rod_inertia_kg_m2 > 0.0)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonpositive_mass_property);
    }
    if (!(input.plan.crankcase_pressure_pa_abs > 0.0)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::nonpositive_crankcase_pressure);
    }
    if (!std::isfinite(input.phase_rad)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_phase);
    }
    if (!std::isfinite(input.angular_speed_rad_s)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_angular_speed);
    }
    if (!std::isfinite(input.chamber_pressure_pa_abs)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::nonfinite_chamber_pressure);
    }
    if (!(input.chamber_pressure_pa_abs > 0.0)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::nonpositive_chamber_pressure);
    }
    if (!std::isfinite(input.retained_previous_wall_reaction_magnitude_n)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::nonfinite_retained_wall_reaction);
    }
    if (input.retained_previous_wall_reaction_magnitude_n < 0.0) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::negative_retained_wall_reaction);
    }
    return std::nullopt;
}

struct Vector2 {
    double normal = 0.0;
    double axis = 0.0;
};

[[nodiscard]] double cross(Vector2 lhs, Vector2 rhs) noexcept {
    return lhs.normal * rhs.axis - lhs.axis * rhs.normal;
}

} // namespace

EngineSimV1CrankFrictionCalculation
calculate_engine_sim_v1_positive_speed_crank_friction(
    const EngineSimV1PositiveSpeedCrankFrictionPlan &plan) noexcept {
    if (!std::isfinite(plan.running_friction_torque_magnitude_nm)) {
        return EngineSimV1CrankFrictionError{
            EngineSimV1CrankFrictionIssue::nonfinite_running_friction_torque};
    }
    if (plan.running_friction_torque_magnitude_nm < 0.0) {
        return EngineSimV1CrankFrictionError{
            EngineSimV1CrankFrictionIssue::negative_running_friction_torque};
    }
    return EngineSimV1PositiveSpeedCrankFriction{
        -plan.running_friction_torque_magnitude_nm};
}

EngineSimV1PistonWallFrictionCalculation stage_engine_sim_v1_piston_wall_friction(
    const EngineSimV1PistonWallStepInput &input) noexcept {
    if (const auto error = validate_step_input(input); error.has_value()) {
        return *error;
    }
    const auto derivative_calculation = slider_axis_derivative(input);
    if (const auto *error =
            std::get_if<EngineSimV1PistonWallError>(&derivative_calculation)) {
        return *error;
    }
    const double slider_derivative = std::get<double>(derivative_calculation);
    const double signed_velocity = slider_derivative * input.angular_speed_rad_s;

    const double cylinder_wall_force =
        input.retained_previous_wall_reaction_magnitude_n;
    const double coulomb_force = kPristineFrictionCoefficient * cylinder_wall_force;
    const double stribeck_velocity =
        kPristineBreakawayFrictionVelocityMPerS * kPristineRootTwo;
    const double coulomb_velocity = kPristineBreakawayFrictionVelocityMPerS / 10.0;
    const double speed = std::abs(signed_velocity);

    const double term_0 = kPristineRootTwo * kPristineEuler *
                          (kPristineBreakawayFrictionN - coulomb_force);
    const double term_1 = speed / stribeck_velocity;
    const double term_2 = std::exp(-term_1 * term_1) * term_1;
    const double term_3 = coulomb_force * std::tanh(speed / coulomb_velocity);
    const double term_4 = kPristineViscousFrictionCoefficientNPerMPerS * speed;
    const double raw_friction_force = term_0 * term_2 + term_3 + term_4;

    const double attenuated_speed =
        std::fmin(std::abs(signed_velocity), kPristineZeroSpeedAttenuationLimitMPerS);
    const double attenuation =
        attenuated_speed / kPristineZeroSpeedAttenuationLimitMPerS;
    const double friction_force_magnitude = raw_friction_force * attenuation;
    const double signed_axis_friction_force =
        signed_velocity > 0.0 ? -friction_force_magnitude : friction_force_magnitude;
    const double generalized_friction_torque =
        signed_axis_friction_force * slider_derivative;

    if (!std::isfinite(slider_derivative) || !std::isfinite(signed_velocity) ||
        !std::isfinite(friction_force_magnitude) ||
        !std::isfinite(signed_axis_friction_force) ||
        !std::isfinite(generalized_friction_torque)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_derived_value);
    }
    return EngineSimV1PistonWallFrictionStage{
        input,
        slider_derivative,
        signed_velocity,
        friction_force_magnitude,
        signed_axis_friction_force,
        generalized_friction_torque,
    };
}

EngineSimV1PistonWallReactionCalculation
calculate_engine_sim_v1_next_piston_wall_reaction(
    const EngineSimV1PistonWallFrictionStage &stage,
    double angular_acceleration_rad_s2) noexcept {
    if (const auto error = validate_step_input(stage.input); error.has_value()) {
        return *error;
    }
    if (!std::isfinite(angular_acceleration_rad_s2)) {
        return piston_wall_error(
            EngineSimV1PistonWallIssue::nonfinite_angular_acceleration);
    }
    if (!std::isfinite(stage.slider_axis_derivative_m_per_rad) ||
        !std::isfinite(stage.signed_slider_axis_velocity_m_s) ||
        !std::isfinite(stage.friction_force_magnitude_n) ||
        !std::isfinite(stage.signed_slider_axis_friction_force_n) ||
        !std::isfinite(stage.generalized_friction_torque_nm)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::malformed_friction_stage);
    }

    const auto &plan = stage.input.plan;
    const double phase = stage.input.phase_rad;
    const double sine = std::sin(phase);
    const double cosine = std::cos(phase);
    const double radius = plan.crank_radius_m;
    const double radius_squared = radius * radius;
    const double rod_length = plan.connecting_rod_length_m;
    const double rod_length_squared = rod_length * rod_length;
    const double crank_pin_normal = radius * sine;
    const double root = std::sqrt(rod_length_squared - radius_squared * sine * sine);

    const double crank_pin_normal_first = radius * cosine;
    const double crank_pin_normal_second = -radius * sine;
    const double root_first = -(crank_pin_normal * crank_pin_normal_first) / root;
    const double root_second = -(crank_pin_normal_first * crank_pin_normal_first +
                                 crank_pin_normal * crank_pin_normal_second) /
                                   root -
                               (crank_pin_normal * crank_pin_normal *
                                crank_pin_normal_first * crank_pin_normal_first) /
                                   (root * root * root);

    const Vector2 crank_pin_first{
        crank_pin_normal_first,
        -radius * sine,
    };
    const Vector2 crank_pin_second{
        crank_pin_normal_second,
        -radius * cosine,
    };
    const Vector2 wrist_pin_first{
        0.0,
        -radius * sine + root_first,
    };
    const Vector2 wrist_pin_second{
        0.0,
        -radius * cosine + root_second,
    };
    const double omega_squared =
        stage.input.angular_speed_rad_s * stage.input.angular_speed_rad_s;
    const Vector2 crank_pin_acceleration{
        crank_pin_second.normal * omega_squared +
            crank_pin_first.normal * angular_acceleration_rad_s2,
        crank_pin_second.axis * omega_squared +
            crank_pin_first.axis * angular_acceleration_rad_s2,
    };
    const Vector2 wrist_pin_acceleration{
        wrist_pin_second.normal * omega_squared +
            wrist_pin_first.normal * angular_acceleration_rad_s2,
        wrist_pin_second.axis * omega_squared +
            wrist_pin_first.axis * angular_acceleration_rad_s2,
    };
    const Vector2 rod_center_acceleration{
        0.5 * (crank_pin_acceleration.normal + wrist_pin_acceleration.normal),
        0.5 * (crank_pin_acceleration.axis + wrist_pin_acceleration.axis),
    };
    const Vector2 rod{
        -crank_pin_normal,
        root,
    };
    const Vector2 rod_relative_acceleration{
        wrist_pin_acceleration.normal - crank_pin_acceleration.normal,
        wrist_pin_acceleration.axis - crank_pin_acceleration.axis,
    };
    const double rod_angular_acceleration =
        cross(rod, rod_relative_acceleration) / rod_length_squared;

    const double pressure_differential =
        stage.input.chamber_pressure_pa_abs - plan.crankcase_pressure_pa_abs;
    const double signed_axis_gas_force = -plan.piston_area_m2 * pressure_differential;
    const double signed_axis_external_force =
        signed_axis_gas_force + stage.signed_slider_axis_friction_force_n;
    const double rod_on_piston_axis_force =
        plan.piston_mass_kg * wrist_pin_acceleration.axis - signed_axis_external_force;
    const double rod_moment_balance =
        -plan.connecting_rod_inertia_kg_m2 * rod_angular_acceleration -
        (plan.connecting_rod_mass_kg * 0.5) * cross(rod, rod_center_acceleration);

    // In the local (normal, axis) basis:
    // cross(rod, axis) = rod.normal and cross(rod, normal) = -rod.axis.
    const double rod_on_piston_normal_force =
        (rod_moment_balance - rod_on_piston_axis_force * rod.normal) / -rod.axis;
    const double signed_wall_on_piston_force = -rod_on_piston_normal_force;
    const double wall_reaction_magnitude = std::abs(signed_wall_on_piston_force);

    if (!std::isfinite(sine) || !std::isfinite(cosine) ||
        !std::isfinite(radius_squared) || !std::isfinite(rod_length_squared) ||
        !std::isfinite(crank_pin_normal) || !std::isfinite(root) || !(root > 0.0) ||
        !std::isfinite(root_first) || !std::isfinite(root_second) ||
        !std::isfinite(omega_squared) || !std::isfinite(rod_angular_acceleration) ||
        !std::isfinite(pressure_differential) ||
        !std::isfinite(signed_axis_gas_force) ||
        !std::isfinite(signed_axis_external_force) ||
        !std::isfinite(rod_on_piston_axis_force) ||
        !std::isfinite(rod_moment_balance) ||
        !std::isfinite(rod_on_piston_normal_force) ||
        !std::isfinite(signed_wall_on_piston_force) ||
        !std::isfinite(wall_reaction_magnitude)) {
        return piston_wall_error(EngineSimV1PistonWallIssue::nonfinite_derived_value);
    }
    return EngineSimV1PistonWallReaction{
        signed_wall_on_piston_force,
        wall_reaction_magnitude,
    };
}

} // namespace engine_sim_offline::simulation
