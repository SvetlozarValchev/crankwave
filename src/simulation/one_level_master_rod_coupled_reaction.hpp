#pragma once

#include "simulation/crankwave_transient_friction.hpp"
#include "simulation/one_level_master_rod_configuration_inertia.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace crankwave::simulation {

inline constexpr std::size_t kNoOneLevelMasterRodReactionCylinder =
    std::numeric_limits<std::size_t>::max();

struct OneLevelMasterRodPlanarForce {
    double x_n = 0.0;
    double y_n = 0.0;

    friend bool operator==(const OneLevelMasterRodPlanarForce &,
                           const OneLevelMasterRodPlanarForce &) = default;
};

enum class OneLevelMasterRodCoupledReactionIssue : std::uint8_t {
    workspace_owner_mismatch,
    missing_friction_stage,
    staged_state_mismatch,
    incorrect_state_size,
    incorrect_boundary_size,
    cylinder_identity_mismatch,
    invalid_compiled_cylinder,
    invalid_parent_root,
    nonfinite_angular_speed,
    nonfinite_angular_acceleration,
    nonfinite_boundary_value,
    nonpositive_absolute_pressure,
    negative_retained_wall_reaction,
    malformed_articulated_state,
    friction_law_rejected,
    singular_rod_axis_projection,
    nonfinite_derived_value,
};

struct OneLevelMasterRodCoupledReactionError {
    OneLevelMasterRodCoupledReactionIssue issue =
        OneLevelMasterRodCoupledReactionIssue::nonfinite_derived_value;
    std::size_t cylinder_index = kNoOneLevelMasterRodReactionCylinder;
    std::optional<CrankwavePistonWallIssue> friction_issue;

    friend bool operator==(const OneLevelMasterRodCoupledReactionError &,
                           const OneLevelMasterRodCoupledReactionError &) = default;
};

struct OneLevelMasterRodPistonWallBoundaryInput {
    contract::CylinderId cylinder_id;
    double chamber_pressure_pa_abs = 0.0;
    double crankcase_pressure_pa_abs = 0.0;
    double retained_previous_wall_reaction_magnitude_n = 0.0;

    friend bool operator==(const OneLevelMasterRodPistonWallBoundaryInput &,
                           const OneLevelMasterRodPistonWallBoundaryInput &) = default;
};

struct OneLevelMasterRodPistonWallFrictionStage {
    OneLevelMasterRodPistonWallBoundaryInput boundary;
    double slider_axis_derivative_m_per_rad = 0.0;
    double signed_slider_axis_velocity_m_s = 0.0;
    double signed_axis_pressure_force_n = 0.0;
    double friction_force_magnitude_n = 0.0;
    double signed_axis_friction_force_n = 0.0;
    double generalized_friction_torque_nm = 0.0;

    friend bool operator==(const OneLevelMasterRodPistonWallFrictionStage &,
                           const OneLevelMasterRodPistonWallFrictionStage &) = default;
};

struct OneLevelMasterRodCylinderReaction {
    contract::CylinderId cylinder_id;
    double signed_wall_on_piston_force_n = 0.0;
    double wall_reaction_magnitude_n = 0.0;
    OneLevelMasterRodPlanarForce rod_on_piston_at_wrist_n;
    OneLevelMasterRodPlanarForce parent_on_rod_at_big_end_n;
    // Nonzero only for a direct root. These are the accumulated forces applied by
    // its slave rods and their accumulated moment about the root big end.
    OneLevelMasterRodPlanarForce accumulated_child_force_on_rod_n;
    double accumulated_child_moment_about_big_end_nm = 0.0;

    friend bool operator==(const OneLevelMasterRodCylinderReaction &,
                           const OneLevelMasterRodCylinderReaction &) = default;
};

// A workspace is bound to one exact compiled-mechanism instance. Its storage is
// allocated only by the factory and never resized by staging or reaction solves.
// It is intentionally opaque: callers can observe only spans published by a
// successful transaction. Moving a workspace transfers ownership and invalidates
// the moved-from object.
class OneLevelMasterRodCoupledReactionWorkspace final {
  public:
    OneLevelMasterRodCoupledReactionWorkspace(
        const OneLevelMasterRodCoupledReactionWorkspace &) = delete;
    OneLevelMasterRodCoupledReactionWorkspace &
    operator=(const OneLevelMasterRodCoupledReactionWorkspace &) = delete;
    OneLevelMasterRodCoupledReactionWorkspace(
        OneLevelMasterRodCoupledReactionWorkspace &&other) noexcept;
    OneLevelMasterRodCoupledReactionWorkspace &
    operator=(OneLevelMasterRodCoupledReactionWorkspace &&other) noexcept;
    ~OneLevelMasterRodCoupledReactionWorkspace() = default;

  private:
    explicit OneLevelMasterRodCoupledReactionWorkspace(
        const CompiledOneLevelMasterRodArticulatedMechanism &owner);

    void invalidate_all() noexcept;
    void invalidate_reaction() noexcept;

    const CompiledOneLevelMasterRodArticulatedMechanism *owner_mechanism_ = nullptr;
    const OneLevelMasterRodCompiledCylinderView *owner_data_ = nullptr;
    std::size_t owner_count_ = 0;
    std::vector<OneLevelMasterRodPistonWallFrictionStage> friction_stages_;
    std::vector<OneLevelMasterRodCylinderReaction> reactions_;
    std::vector<OneLevelMasterRodPlanarForce> accumulated_child_forces_;
    std::vector<double> accumulated_child_moments_nm_;
    std::vector<OneLevelMasterRodCylinderArticulatedState> staged_state_;
    double staged_angular_speed_rad_s_ = 0.0;
    double staged_total_generalized_friction_torque_nm_ = 0.0;
    bool stage_published_ = false;
    bool reaction_published_ = false;

    friend OneLevelMasterRodCoupledReactionWorkspace
    make_one_level_master_rod_coupled_reaction_workspace(
        const CompiledOneLevelMasterRodArticulatedMechanism &mechanism);
    friend struct OneLevelMasterRodCoupledReactionWorkspaceAccess;
};

struct OneLevelMasterRodFrictionStageResult {
    double total_generalized_friction_torque_nm = 0.0;
    std::span<const OneLevelMasterRodPistonWallFrictionStage> cylinders;
};

struct OneLevelMasterRodCoupledReactionResult {
    double total_generalized_friction_torque_nm = 0.0;
    std::span<const OneLevelMasterRodCylinderReaction> cylinders;
};

using OneLevelMasterRodFrictionStageCalculation =
    std::variant<OneLevelMasterRodFrictionStageResult,
                 OneLevelMasterRodCoupledReactionError>;
using OneLevelMasterRodCoupledReactionCalculation =
    std::variant<OneLevelMasterRodCoupledReactionResult,
                 OneLevelMasterRodCoupledReactionError>;

[[nodiscard]] OneLevelMasterRodCoupledReactionWorkspace
make_one_level_master_rod_coupled_reaction_workspace(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism);

// Published spans refer into workspace storage and are invalidated by the next
// operation on that workspace, successful or not.
[[nodiscard]] OneLevelMasterRodFrictionStageCalculation
stage_one_level_master_rod_piston_wall_friction(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism,
    const OneLevelMasterRodArticulatedState &state,
    std::span<const OneLevelMasterRodPistonWallBoundaryInput> boundaries,
    double angular_speed_rad_s,
    OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept;

// Resolve accepts only the exact state, angular speed, and compiled mechanism
// whose successful stage transaction currently owns the workspace. Its published
// wall magnitudes belong exclusively to the following physics boundary.
[[nodiscard]] OneLevelMasterRodCoupledReactionCalculation
calculate_one_level_master_rod_coupled_reactions(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism,
    const OneLevelMasterRodArticulatedState &state, double angular_speed_rad_s,
    double angular_acceleration_rad_s2,
    OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept;

namespace detail {

// Validated single-branch entry point for focused tests. Child force is the sum
// applied by child rods to this rod; child moment is about this rod's big end.
[[nodiscard]] std::variant<OneLevelMasterRodCylinderReaction,
                           OneLevelMasterRodCoupledReactionError>
calculate_one_level_master_rod_branch_reaction(
    const OneLevelMasterRodCompiledCylinderView &cylinder,
    const OneLevelMasterRodCylinderArticulatedState &state,
    double signed_axis_pressure_force_n, double signed_axis_friction_force_n,
    double angular_speed_rad_s, double angular_acceleration_rad_s2,
    OneLevelMasterRodPlanarForce child_force_sum_n,
    double child_moment_about_big_end_nm,
    std::size_t cylinder_index = kNoOneLevelMasterRodReactionCylinder) noexcept;

} // namespace detail

} // namespace crankwave::simulation
