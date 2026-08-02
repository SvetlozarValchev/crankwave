#pragma once

#include "simulation/mechanism_kinematics_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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
    incorrect_state_scratch_size,
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

inline constexpr std::size_t kNoOneLevelMasterRodParentCylinder =
    std::numeric_limits<std::size_t>::max();

// Prevalidated immutable mechanics facts shared by the articulated inertia and
// coupled-reaction reductions. The cylinder axis points from the crank toward the
// head. The normal is its clockwise perpendicular, so cross(normal, axis) = +1.
// A direct root has parent_root_index == kNoOneLevelMasterRodParentCylinder; a
// slave names its direct root's stable cylinder index.
struct OneLevelMasterRodCompiledCylinderView {
    contract::CylinderId cylinder_id;
    std::size_t parent_root_index = kNoOneLevelMasterRodParentCylinder;
    double bank_axis_x = 0.0;
    double bank_axis_y = 0.0;
    double clockwise_normal_x = 0.0;
    double clockwise_normal_y = 0.0;
    double piston_area_m2 = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_length_m = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;
    double connecting_rod_center_fraction_from_big_end = 0.0;
    bool direct_root = false;

    friend bool operator==(const OneLevelMasterRodCompiledCylinderView &,
                           const OneLevelMasterRodCompiledCylinderView &) = default;
};

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

// Immutable, compile-once execution form. Compilation validates every plan field,
// certifies full-cycle geometry, and flattens each cylinder's resolved root driver,
// geometry, and mass properties. Per-tick methods perform no allocation, identity
// search, topology resolution, or full-cycle certification.
class CompiledOneLevelMasterRodArticulatedMechanism final {
  public:
    CompiledOneLevelMasterRodArticulatedMechanism(
        const CompiledOneLevelMasterRodArticulatedMechanism &) = default;
    CompiledOneLevelMasterRodArticulatedMechanism(
        CompiledOneLevelMasterRodArticulatedMechanism &&) noexcept = default;
    CompiledOneLevelMasterRodArticulatedMechanism &
    operator=(const CompiledOneLevelMasterRodArticulatedMechanism &) = delete;
    CompiledOneLevelMasterRodArticulatedMechanism &
    operator=(CompiledOneLevelMasterRodArticulatedMechanism &&) noexcept = delete;
    ~CompiledOneLevelMasterRodArticulatedMechanism() = default;

    [[nodiscard]] std::size_t cylinder_count() const noexcept;

    [[nodiscard]] std::span<const OneLevelMasterRodCompiledCylinderView>
    cylinder_views() const noexcept;

    // Allocates exact-size scratch outside the tick path. A caller may instead
    // resize its own state once to cylinder_count().
    [[nodiscard]] OneLevelMasterRodArticulatedState make_state_scratch() const;

    // Fills exact-size caller-owned scratch. nullopt means success; an error leaves
    // no valid state contract. This method never allocates.
    [[nodiscard]] std::optional<OneLevelMasterRodConfigurationInertiaError>
    evaluate_articulated_state(
        double crank_angle_theta_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept;

    // Exact canonical-body-angle entry point for the dynamic mechanics handoff.
    // The scalar value is used without a theta/reference round trip while its AD
    // derivatives remain dpsi/dtheta=-1 and d2psi/dtheta2=0.
    [[nodiscard]] std::optional<OneLevelMasterRodConfigurationInertiaError>
    evaluate_articulated_state_at_body_angle_psi(
        double body_angle_psi_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept;

    // Evaluates shared articulated state into scratch and reduces that exact state
    // to M/M-prime. This method never allocates.
    [[nodiscard]] OneLevelMasterRodConfigurationInertiaCalculation
    evaluate_configuration_inertia(
        double attached_inertia_kg_m2, double crank_angle_theta_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept;

    [[nodiscard]] OneLevelMasterRodConfigurationInertiaCalculation
    evaluate_configuration_inertia_at_body_angle_psi(
        double attached_inertia_kg_m2, double body_angle_psi_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept;

  private:
    struct Cylinder {
        OneLevelMasterRodDriver driver;
        OneLevelMasterRodCylinder geometry;
        double piston_mass_kg = 0.0;
        double connecting_rod_mass_kg = 0.0;
        double connecting_rod_inertia_kg_m2 = 0.0;
        double connecting_rod_center_of_mass_from_big_end_m = 0.0;
        OneLevelMasterRodSlavePin slave_pin;
        bool direct_root = false;
    };

    CompiledOneLevelMasterRodArticulatedMechanism(
        double crank_tdc_reference_rad, double authored_crank_inertia_kg_m2,
        std::vector<Cylinder> cylinders,
        std::vector<OneLevelMasterRodCompiledCylinderView> cylinder_views) noexcept;

    [[nodiscard]] OneLevelMasterRodConfigurationInertiaCalculation
    reduce_configuration_inertia(
        double attached_inertia_kg_m2,
        const OneLevelMasterRodArticulatedState &state) const noexcept;

    double crank_tdc_reference_rad_ = 0.0;
    double authored_crank_inertia_kg_m2_ = 0.0;
    std::vector<Cylinder> cylinders_;
    std::vector<OneLevelMasterRodCompiledCylinderView> cylinder_views_;

    friend std::variant<CompiledOneLevelMasterRodArticulatedMechanism,
                        OneLevelMasterRodConfigurationInertiaError>
    compile_one_level_master_rod_articulated_mechanism(
        const OneLevelMasterRodMechanismKinematicsPlan &plan);
};

using OneLevelMasterRodArticulatedMechanismCompilation =
    std::variant<CompiledOneLevelMasterRodArticulatedMechanism,
                 OneLevelMasterRodConfigurationInertiaError>;

[[nodiscard]] OneLevelMasterRodArticulatedMechanismCompilation
compile_one_level_master_rod_articulated_mechanism(
    const OneLevelMasterRodMechanismKinematicsPlan &plan);

using OneLevelMasterRodArticulatedStateCalculation =
    std::variant<OneLevelMasterRodArticulatedState,
                 OneLevelMasterRodConfigurationInertiaError>;

// Convenience validating wrappers. They compile and allocate on every call and
// are intended for tests and non-tick diagnostics. Runtime callers use the
// compiled object's scratch-taking methods above.
[[nodiscard]] OneLevelMasterRodArticulatedStateCalculation
evaluate_one_level_master_rod_articulated_state(
    const OneLevelMasterRodMechanismKinematicsPlan &plan,
    double crank_angle_theta_rad) noexcept;

// Evaluates analytic M(theta) and dM/dtheta. crank_angle_theta_rad is the same
// increasing engine crank coordinate represented by LegacyMechanismStep's
// theta_unwrapped_rad; the plan's legacy body angle is therefore
// crank_tdc_reference_rad - crank_angle_theta_rad. All first and second planar
// derivatives are obtained analytically by second-order automatic differentiation.
// This overload is the convenience validating wrapper; tick paths compile once.
[[nodiscard]] OneLevelMasterRodConfigurationInertiaCalculation
evaluate_one_level_master_rod_configuration_inertia(
    const OneLevelMasterRodMechanismKinematicsPlan &plan, double attached_inertia_kg_m2,
    double crank_angle_theta_rad) noexcept;

} // namespace engine_sim_offline::simulation
