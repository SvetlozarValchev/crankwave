#pragma once

#include "simulation/mechanism_kinematics_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

inline constexpr std::size_t kNoOneLevelMasterRodInertiaCylinder =
    std::numeric_limits<std::size_t>::max();

enum class OneLevelMasterRodConfigurationInertiaIssue : std::uint8_t {
    nonfinite_crank_angle,
    nonfinite_attached_inertia,
    negative_attached_inertia,
    invalid_plan_identity,
    invalid_rigid_crank_group,
    empty_cylinder_set,
    invalid_cylinder_identity,
    invalid_cylinder_topology,
    nonfinite_plan_value,
    invalid_mass_property,
    invalid_center_of_mass,
    invalid_kinematics,
    invalid_slave_attachment,
    uncertified_full_cycle_geometry,
    nonfinite_derived_value,
    nonpositive_total_inertia,
};

struct OneLevelMasterRodConfigurationInertiaError {
    OneLevelMasterRodConfigurationInertiaIssue issue =
        OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value;
    std::size_t cylinder_index = kNoOneLevelMasterRodInertiaCylinder;

    friend bool
    operator==(const OneLevelMasterRodConfigurationInertiaError &,
               const OneLevelMasterRodConfigurationInertiaError &) = default;
};

struct OneLevelMasterRodPlanarPointState {
    double x_m = 0.0;
    double y_m = 0.0;
    double dx_dtheta_m_per_rad = 0.0;
    double dy_dtheta_m_per_rad = 0.0;
    double d2x_dtheta2_m_per_rad2 = 0.0;
    double d2y_dtheta2_m_per_rad2 = 0.0;

    friend bool operator==(const OneLevelMasterRodPlanarPointState &,
                           const OneLevelMasterRodPlanarPointState &) = default;
};

// Whole-mechanism rigid-body geometry in stable plan cylinder order. B is the
// crank pin for a direct root and the master-rod attachment pin for a slave; W is
// the corresponding wrist pin; G uses the authored COM distance from B. These
// exact derivatives are shared by inertia and later coupled-reaction reductions.
struct OneLevelMasterRodCylinderArticulatedState {
    contract::CylinderId cylinder_id;
    OneLevelMasterRodPlanarPointState big_end;
    OneLevelMasterRodPlanarPointState wrist_pin;
    OneLevelMasterRodPlanarPointState rod_center_of_mass;
    double rod_angle_rad = 0.0;
    double rod_angle_first_derivative_rad_per_rad = 0.0;
    double rod_angle_second_derivative_rad_per_rad2 = 0.0;

    friend bool operator==(const OneLevelMasterRodCylinderArticulatedState &,
                           const OneLevelMasterRodCylinderArticulatedState &) = default;
};

struct OneLevelMasterRodArticulatedState {
    std::vector<OneLevelMasterRodCylinderArticulatedState> cylinders;

    friend bool operator==(const OneLevelMasterRodArticulatedState &,
                           const OneLevelMasterRodArticulatedState &) = default;
};

using OneLevelMasterRodArticulatedStateCalculation =
    std::variant<OneLevelMasterRodArticulatedState,
                 OneLevelMasterRodConfigurationInertiaError>;

// Resolves every rigid body's planar configuration and its first two analytic
// derivatives with respect to increasing crank theta. It is intentionally public
// so inertia and reaction calculations can share one geometry authority.
[[nodiscard]] OneLevelMasterRodArticulatedStateCalculation
evaluate_one_level_master_rod_articulated_state(
    const OneLevelMasterRodMechanismKinematicsPlan &plan,
    double crank_angle_theta_rad) noexcept;

// M(theta) is the exact one-degree-of-freedom kinetic-energy coefficient for the
// rigid crank group, every translating piston, and every root or slave connecting
// rod in the plan. The derivative is with respect to the same increasing crank
// coordinate passed to the evaluator and supplies the velocity-dependent term in
//
//   Q = M(theta) * alpha + 0.5 * dM/dtheta * omega^2.
//
// The component fields retain the physical summation boundary: the rigid crank
// group is included once, and each cylinder contributes one piston and one rod.
struct OneLevelMasterRodConfigurationInertia {
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

    friend bool operator==(const OneLevelMasterRodConfigurationInertia &,
                           const OneLevelMasterRodConfigurationInertia &) = default;
};

using OneLevelMasterRodConfigurationInertiaCalculation =
    std::variant<OneLevelMasterRodConfigurationInertia,
                 OneLevelMasterRodConfigurationInertiaError>;

// Evaluates analytic M(theta) and dM/dtheta. crank_angle_theta_rad is the same
// increasing engine crank coordinate represented by LegacyMechanismStep's
// theta_unwrapped_rad; the plan's legacy body angle is therefore
// crank_tdc_reference_rad - crank_angle_theta_rad. All first and second planar
// derivatives are obtained analytically by second-order automatic differentiation.
[[nodiscard]] OneLevelMasterRodConfigurationInertiaCalculation
evaluate_one_level_master_rod_configuration_inertia(
    const OneLevelMasterRodMechanismKinematicsPlan &plan, double attached_inertia_kg_m2,
    double crank_angle_theta_rad) noexcept;

} // namespace engine_sim_offline::simulation
