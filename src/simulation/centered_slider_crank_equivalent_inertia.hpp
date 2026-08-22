#pragma once

#include "crankwave/contract/parity_model.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::simulation {

inline constexpr std::string_view
    kCenteredSliderCrankCycleMeanEquivalentInertiaMethodId =
        "centered-slider-crank-cycle-mean-equivalent-inertia-v2";
inline constexpr std::uint32_t
    kCenteredSliderCrankCycleMeanEquivalentInertiaMethodVersion = 2U;
inline constexpr std::string_view
    kCenteredSliderCrankRigidGroupCycleMeanEquivalentInertiaMethodId =
        "centered-slider-crank-rigid-group-cycle-mean-equivalent-inertia-v2";
inline constexpr std::uint32_t
    kCenteredSliderCrankRigidGroupCycleMeanEquivalentInertiaMethodVersion = 2U;
inline constexpr std::size_t kCenteredSliderCrankCycleMeanInertiaQuadraturePoints =
    4096U;
inline constexpr std::size_t kNoCenteredSliderCrankInertiaCylinder =
    std::numeric_limits<std::size_t>::max();

enum class CenteredSliderCrankCycleMeanInertiaIssue : std::uint8_t {
    missing_output_crankshaft,
    nonfinite_authored_crank_inertia,
    nonpositive_authored_crank_inertia,
    unsupported_cylinder_kinematics,
    nonfinite_crank_radius,
    nonpositive_crank_radius,
    nonfinite_connecting_rod_length,
    connecting_rod_not_longer_than_crank_radius,
    nonfinite_connecting_rod_center_of_mass,
    connecting_rod_center_of_mass_out_of_range,
    nonfinite_piston_mass,
    negative_piston_mass,
    nonfinite_connecting_rod_mass,
    negative_connecting_rod_mass,
    nonfinite_connecting_rod_inertia,
    negative_connecting_rod_inertia,
    nonfinite_derived_value,
    nonpositive_engine_equivalent_inertia,
};

struct CenteredSliderCrankCycleMeanInertiaError {
    CenteredSliderCrankCycleMeanInertiaIssue issue =
        CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_derived_value;
    std::size_t cylinder_index = kNoCenteredSliderCrankInertiaCylinder;

    friend bool operator==(const CenteredSliderCrankCycleMeanInertiaError &,
                           const CenteredSliderCrankCycleMeanInertiaError &) = default;
};

struct CenteredSliderCrankCycleMeanInertia {
    double authored_crank_inertia_kg_m2 = 0.0;
    double piston_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_rotation_inertia_kg_m2 = 0.0;
    double engine_equivalent_inertia_kg_m2 = 0.0;

    friend bool operator==(const CenteredSliderCrankCycleMeanInertia &,
                           const CenteredSliderCrankCycleMeanInertia &) = default;
};

using CenteredSliderCrankCycleMeanInertiaCalculation =
    std::variant<CenteredSliderCrankCycleMeanInertia,
                 CenteredSliderCrankCycleMeanInertiaError>;

[[nodiscard]] std::string_view
centered_slider_crank_cycle_mean_inertia_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
centered_slider_crank_cycle_mean_inertia_method_identity();

[[nodiscard]] std::string_view
centered_slider_crank_rigid_group_cycle_mean_inertia_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
centered_slider_crank_rigid_group_cycle_mean_inertia_method_identity();

// Returns the constant crank-referred inertia whose rotational kinetic energy is
// the full-cycle mean kinetic energy of the admitted centered slider-crank
// mechanism. Journal phase is immaterial to this full-cycle mean.
[[nodiscard]] CenteredSliderCrankCycleMeanInertiaCalculation
calculate_centered_slider_crank_cycle_mean_inertia(
    const contract::LegacyMechanismProfile &mechanism) noexcept;

// A compiled one-degree-of-freedom centered slider-crank mechanism. Cylinder
// phase is represented by geometric TDC so evaluation can consume the same
// unwrapped crank coordinate as the mechanics runtime.
struct CenteredSliderCrankConfigurationInertiaCylinderPlan {
    double geometric_tdc_rad = 0.0;
    double crank_radius_m = 0.0;
    double connecting_rod_length_m = 0.0;
    // Physical distance from the crank pin to the rod center of mass.
    double connecting_rod_center_of_mass_from_crank_pin_m = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;

    friend bool
    operator==(const CenteredSliderCrankConfigurationInertiaCylinderPlan &,
               const CenteredSliderCrankConfigurationInertiaCylinderPlan &) = default;
};

struct CenteredSliderCrankConfigurationInertiaPlan {
    double authored_crank_inertia_kg_m2 = 0.0;
    double attached_inertia_kg_m2 = 0.0;
    std::vector<CenteredSliderCrankConfigurationInertiaCylinderPlan> cylinders;

    friend bool
    operator==(const CenteredSliderCrankConfigurationInertiaPlan &,
               const CenteredSliderCrankConfigurationInertiaPlan &) = default;
};

enum class CenteredSliderCrankConfigurationInertiaIssue : std::uint8_t {
    nonfinite_plan_value,
    nonpositive_authored_crank_inertia,
    negative_attached_inertia,
    invalid_slider_crank_geometry,
    negative_mass_property,
    nonfinite_crank_angle,
    nonfinite_derived_value,
};

struct CenteredSliderCrankConfigurationInertiaError {
    CenteredSliderCrankConfigurationInertiaIssue issue =
        CenteredSliderCrankConfigurationInertiaIssue::nonfinite_derived_value;
    std::size_t cylinder_index = kNoCenteredSliderCrankInertiaCylinder;

    friend bool
    operator==(const CenteredSliderCrankConfigurationInertiaError &,
               const CenteredSliderCrankConfigurationInertiaError &) = default;
};

// M(theta) is the exact crank-referred kinetic-energy coefficient of the
// centered rigid mechanism. Its derivative supplies the velocity-dependent
// generalized torque in:
//
//   Q = M(theta) * alpha + 0.5 * dM/dtheta * omega^2
//
// Component fields make the derivation auditable without changing that
// one-degree-of-freedom contract.
struct CenteredSliderCrankConfigurationInertia {
    double authored_crank_inertia_kg_m2 = 0.0;
    double attached_inertia_kg_m2 = 0.0;
    double piston_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_rotation_inertia_kg_m2 = 0.0;
    double total_inertia_kg_m2 = 0.0;
    double piston_translation_derivative_kg_m2_per_rad = 0.0;
    double connecting_rod_translation_derivative_kg_m2_per_rad = 0.0;
    double connecting_rod_rotation_derivative_kg_m2_per_rad = 0.0;
    double total_derivative_kg_m2_per_rad = 0.0;

    friend bool operator==(const CenteredSliderCrankConfigurationInertia &,
                           const CenteredSliderCrankConfigurationInertia &) = default;
};

using CenteredSliderCrankConfigurationInertiaCalculation =
    std::variant<CenteredSliderCrankConfigurationInertia,
                 CenteredSliderCrankConfigurationInertiaError>;

// Evaluates analytic M(theta) and dM/dtheta for the compiled mechanism.
[[nodiscard]] CenteredSliderCrankConfigurationInertiaCalculation
evaluate_centered_slider_crank_configuration_inertia(
    const CenteredSliderCrankConfigurationInertiaPlan &plan,
    double crank_angle_rad) noexcept;

} // namespace crankwave::simulation
