#include "simulation/one_level_master_rod_coupled_reaction.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace engine_sim_offline::simulation {

struct OneLevelMasterRodCoupledReactionWorkspaceAccess {
    static bool owner_matches(
        const OneLevelMasterRodCoupledReactionWorkspace &workspace,
        const CompiledOneLevelMasterRodArticulatedMechanism &mechanism,
        const std::span<const OneLevelMasterRodCompiledCylinderView> views) noexcept {
        return workspace.owner_mechanism_ == &mechanism &&
               workspace.owner_data_ == views.data() &&
               workspace.owner_count_ == views.size();
    }

    static void
    invalidate_all(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        workspace.invalidate_all();
    }

    static void
    invalidate_reaction(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        workspace.invalidate_reaction();
    }

    static std::vector<OneLevelMasterRodPistonWallFrictionStage> &
    stages(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.friction_stages_;
    }

    static const std::vector<OneLevelMasterRodPistonWallFrictionStage> &
    stages(const OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.friction_stages_;
    }

    static std::vector<OneLevelMasterRodCylinderReaction> &
    reactions(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.reactions_;
    }

    static std::vector<OneLevelMasterRodPlanarForce> &
    child_forces(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.accumulated_child_forces_;
    }

    static std::vector<double> &
    child_moments(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.accumulated_child_moments_nm_;
    }

    static std::vector<OneLevelMasterRodCylinderArticulatedState> &
    staged_state(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.staged_state_;
    }

    static const std::vector<OneLevelMasterRodCylinderArticulatedState> &
    staged_state(const OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.staged_state_;
    }

    static bool stage_published(
        const OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.stage_published_;
    }

    static void publish_stage(OneLevelMasterRodCoupledReactionWorkspace &workspace,
                              const double angular_speed_rad_s,
                              const double total_torque_nm) noexcept {
        workspace.staged_angular_speed_rad_s_ = angular_speed_rad_s;
        workspace.staged_total_generalized_friction_torque_nm_ = total_torque_nm;
        workspace.stage_published_ = true;
    }

    static double staged_angular_speed(
        const OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.staged_angular_speed_rad_s_;
    }

    static double staged_total_torque(
        const OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        return workspace.staged_total_generalized_friction_torque_nm_;
    }

    static void
    publish_reaction(OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
        workspace.reaction_published_ = true;
    }
};

namespace {

using WorkspaceAccess = OneLevelMasterRodCoupledReactionWorkspaceAccess;

[[nodiscard]] OneLevelMasterRodCoupledReactionError
error(const OneLevelMasterRodCoupledReactionIssue issue,
      const std::size_t cylinder_index = kNoOneLevelMasterRodReactionCylinder,
      const std::optional<EngineSimV1PistonWallIssue> friction_issue =
          std::nullopt) noexcept {
    return {issue, cylinder_index, friction_issue};
}

[[nodiscard]] bool finite(const OneLevelMasterRodPlanarPointState &point) noexcept {
    return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
           std::isfinite(point.dx_dtheta_m_per_rad) &&
           std::isfinite(point.dy_dtheta_m_per_rad) &&
           std::isfinite(point.d2x_dtheta2_m_per_rad2) &&
           std::isfinite(point.d2y_dtheta2_m_per_rad2);
}

[[nodiscard]] bool
finite(const OneLevelMasterRodCylinderArticulatedState &state) noexcept {
    return state.cylinder_id.valid() && finite(state.big_end) &&
           finite(state.wrist_pin) && finite(state.rod_center_of_mass) &&
           std::isfinite(state.rod_angle_rad) &&
           std::isfinite(state.rod_angle_first_derivative_rad_per_rad) &&
           std::isfinite(state.rod_angle_second_derivative_rad_per_rad2);
}

[[nodiscard]] bool finite(const OneLevelMasterRodPlanarForce force) noexcept {
    return std::isfinite(force.x_n) && std::isfinite(force.y_n);
}

[[nodiscard]] OneLevelMasterRodPlanarForce
add(const OneLevelMasterRodPlanarForce left,
    const OneLevelMasterRodPlanarForce right) noexcept {
    return {left.x_n + right.x_n, left.y_n + right.y_n};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
subtract(const OneLevelMasterRodPlanarForce left,
         const OneLevelMasterRodPlanarForce right) noexcept {
    return {left.x_n - right.x_n, left.y_n - right.y_n};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
scale(const double scalar, const OneLevelMasterRodPlanarForce vector) noexcept {
    return {scalar * vector.x_n, scalar * vector.y_n};
}

[[nodiscard]] double dot(const OneLevelMasterRodPlanarForce left,
                         const OneLevelMasterRodPlanarForce right) noexcept {
    return left.x_n * right.x_n + left.y_n * right.y_n;
}

[[nodiscard]] double cross(const OneLevelMasterRodPlanarForce left,
                           const OneLevelMasterRodPlanarForce right) noexcept {
    return left.x_n * right.y_n - left.y_n * right.x_n;
}

[[nodiscard]] OneLevelMasterRodPlanarForce
point_difference(const OneLevelMasterRodPlanarPointState &left,
                 const OneLevelMasterRodPlanarPointState &right) noexcept {
    return {left.x_m - right.x_m, left.y_m - right.y_m};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
acceleration(const OneLevelMasterRodPlanarPointState &point, const double omega_squared,
             const double angular_acceleration_rad_s2) noexcept {
    return {
        point.d2x_dtheta2_m_per_rad2 * omega_squared +
            point.dx_dtheta_m_per_rad * angular_acceleration_rad_s2,
        point.d2y_dtheta2_m_per_rad2 * omega_squared +
            point.dy_dtheta_m_per_rad * angular_acceleration_rad_s2,
    };
}

[[nodiscard]] bool same_binary64(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool
same_point_binary(const OneLevelMasterRodPlanarPointState &left,
                  const OneLevelMasterRodPlanarPointState &right) noexcept {
    return same_binary64(left.x_m, right.x_m) && same_binary64(left.y_m, right.y_m) &&
           same_binary64(left.dx_dtheta_m_per_rad, right.dx_dtheta_m_per_rad) &&
           same_binary64(left.dy_dtheta_m_per_rad, right.dy_dtheta_m_per_rad) &&
           same_binary64(left.d2x_dtheta2_m_per_rad2, right.d2x_dtheta2_m_per_rad2) &&
           same_binary64(left.d2y_dtheta2_m_per_rad2, right.d2y_dtheta2_m_per_rad2);
}

[[nodiscard]] bool
same_state_binary(const OneLevelMasterRodCylinderArticulatedState &left,
                  const OneLevelMasterRodCylinderArticulatedState &right) noexcept {
    return left.cylinder_id == right.cylinder_id &&
           same_point_binary(left.big_end, right.big_end) &&
           same_point_binary(left.wrist_pin, right.wrist_pin) &&
           same_point_binary(left.rod_center_of_mass, right.rod_center_of_mass) &&
           same_binary64(left.rod_angle_rad, right.rod_angle_rad) &&
           same_binary64(left.rod_angle_first_derivative_rad_per_rad,
                         right.rod_angle_first_derivative_rad_per_rad) &&
           same_binary64(left.rod_angle_second_derivative_rad_per_rad2,
                         right.rod_angle_second_derivative_rad_per_rad2);
}

[[nodiscard]] bool valid_compiled_cylinder(
    const OneLevelMasterRodCompiledCylinderView &cylinder) noexcept {
    const double axis_norm_squared = cylinder.bank_axis_x * cylinder.bank_axis_x +
                                     cylinder.bank_axis_y * cylinder.bank_axis_y;
    const double normal_norm_squared =
        cylinder.clockwise_normal_x * cylinder.clockwise_normal_x +
        cylinder.clockwise_normal_y * cylinder.clockwise_normal_y;
    const double orthogonality = cylinder.bank_axis_x * cylinder.clockwise_normal_x +
                                 cylinder.bank_axis_y * cylinder.clockwise_normal_y;
    return cylinder.cylinder_id.valid() && std::isfinite(cylinder.bank_axis_x) &&
           std::isfinite(cylinder.bank_axis_y) &&
           std::isfinite(cylinder.clockwise_normal_x) &&
           std::isfinite(cylinder.clockwise_normal_y) &&
           std::isfinite(axis_norm_squared) && std::isfinite(normal_norm_squared) &&
           std::isfinite(orthogonality) &&
           std::abs(axis_norm_squared - 1.0) <= 1.0e-12 &&
           std::abs(normal_norm_squared - 1.0) <= 1.0e-12 &&
           std::abs(orthogonality) <= 1.0e-12 &&
           std::isfinite(cylinder.piston_area_m2) && cylinder.piston_area_m2 > 0.0 &&
           std::isfinite(cylinder.piston_mass_kg) && cylinder.piston_mass_kg > 0.0 &&
           std::isfinite(cylinder.connecting_rod_length_m) &&
           cylinder.connecting_rod_length_m > 0.0 &&
           std::isfinite(cylinder.connecting_rod_mass_kg) &&
           cylinder.connecting_rod_mass_kg > 0.0 &&
           std::isfinite(cylinder.connecting_rod_inertia_kg_m2) &&
           cylinder.connecting_rod_inertia_kg_m2 > 0.0 &&
           std::isfinite(cylinder.connecting_rod_center_fraction_from_big_end) &&
           cylinder.connecting_rod_center_fraction_from_big_end >= 0.0 &&
           cylinder.connecting_rod_center_fraction_from_big_end <= 1.0;
}

[[nodiscard]] std::variant<OneLevelMasterRodCylinderReaction,
                           OneLevelMasterRodCoupledReactionError>
calculate_branch_unchecked(const OneLevelMasterRodCompiledCylinderView &cylinder,
                           const OneLevelMasterRodCylinderArticulatedState &state,
                           const double signed_axis_pressure_force_n,
                           const double signed_axis_friction_force_n,
                           const double angular_speed_rad_s,
                           const double angular_acceleration_rad_s2,
                           const OneLevelMasterRodPlanarForce child_force_sum_n,
                           const double child_moment_about_big_end_nm,
                           const std::size_t cylinder_index) noexcept {
    const OneLevelMasterRodPlanarForce axis{
        cylinder.bank_axis_x,
        cylinder.bank_axis_y,
    };
    const OneLevelMasterRodPlanarForce normal{
        cylinder.clockwise_normal_x,
        cylinder.clockwise_normal_y,
    };
    const OneLevelMasterRodPlanarForce rod =
        point_difference(state.wrist_pin, state.big_end);
    const double rod_length_squared = dot(rod, rod);
    const double rod_axis_projection = dot(rod, axis);
    if (!std::isfinite(rod_length_squared) || !(rod_length_squared > 0.0) ||
        !std::isfinite(rod_axis_projection) || !(rod_axis_projection > 0.0)) {
        return error(
            OneLevelMasterRodCoupledReactionIssue::singular_rod_axis_projection,
            cylinder_index);
    }

    const double omega_squared = angular_speed_rad_s * angular_speed_rad_s;
    const auto piston_acceleration =
        acceleration(state.wrist_pin, omega_squared, angular_acceleration_rad_s2);
    const auto rod_center_acceleration = acceleration(
        state.rod_center_of_mass, omega_squared, angular_acceleration_rad_s2);
    const double rod_angular_acceleration =
        state.rod_angle_second_derivative_rad_per_rad2 * omega_squared +
        state.rod_angle_first_derivative_rad_per_rad * angular_acceleration_rad_s2;
    const double rod_on_piston_axis_force =
        cylinder.piston_mass_kg * dot(piston_acceleration, axis) -
        (signed_axis_pressure_force_n + signed_axis_friction_force_n);
    const double moment_to_wrist =
        -cylinder.connecting_rod_inertia_kg_m2 * rod_angular_acceleration -
        cylinder.connecting_rod_center_fraction_from_big_end *
            cylinder.connecting_rod_mass_kg * cross(rod, rod_center_acceleration) +
        child_moment_about_big_end_nm;
    const double rod_normal_projection = dot(rod, normal);
    const double rod_on_piston_normal_force =
        (rod_normal_projection * rod_on_piston_axis_force - moment_to_wrist) /
        rod_axis_projection;
    const OneLevelMasterRodPlanarForce rod_on_piston =
        add(scale(rod_on_piston_normal_force, normal),
            scale(rod_on_piston_axis_force, axis));
    const double signed_wall_force =
        cylinder.piston_mass_kg * dot(piston_acceleration, normal) -
        rod_on_piston_normal_force;
    const OneLevelMasterRodPlanarForce parent_on_rod =
        subtract(add(scale(cylinder.connecting_rod_mass_kg, rod_center_acceleration),
                     rod_on_piston),
                 child_force_sum_n);
    const double wall_magnitude = std::abs(signed_wall_force);

    if (!std::isfinite(omega_squared) || !finite(piston_acceleration) ||
        !finite(rod_center_acceleration) || !std::isfinite(rod_angular_acceleration) ||
        !std::isfinite(rod_on_piston_axis_force) || !std::isfinite(moment_to_wrist) ||
        !std::isfinite(rod_normal_projection) ||
        !std::isfinite(rod_on_piston_normal_force) || !finite(rod_on_piston) ||
        !std::isfinite(signed_wall_force) || !finite(parent_on_rod) ||
        !std::isfinite(wall_magnitude)) {
        return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_derived_value,
                     cylinder_index);
    }
    return OneLevelMasterRodCylinderReaction{
        cylinder.cylinder_id,
        signed_wall_force,
        wall_magnitude,
        rod_on_piston,
        parent_on_rod,
        child_force_sum_n,
        child_moment_about_big_end_nm,
    };
}

} // namespace

OneLevelMasterRodCoupledReactionWorkspace::OneLevelMasterRodCoupledReactionWorkspace(
    const CompiledOneLevelMasterRodArticulatedMechanism &owner)
    : owner_mechanism_(&owner), owner_data_(owner.cylinder_views().data()),
      owner_count_(owner.cylinder_views().size()),
      friction_stages_(owner.cylinder_count()), reactions_(owner.cylinder_count()),
      accumulated_child_forces_(owner.cylinder_count()),
      accumulated_child_moments_nm_(owner.cylinder_count()),
      staged_state_(owner.cylinder_count()) {}

OneLevelMasterRodCoupledReactionWorkspace::OneLevelMasterRodCoupledReactionWorkspace(
    OneLevelMasterRodCoupledReactionWorkspace &&other) noexcept
    : owner_mechanism_(std::exchange(other.owner_mechanism_, nullptr)),
      owner_data_(std::exchange(other.owner_data_, nullptr)),
      owner_count_(std::exchange(other.owner_count_, 0U)),
      friction_stages_(std::move(other.friction_stages_)),
      reactions_(std::move(other.reactions_)),
      accumulated_child_forces_(std::move(other.accumulated_child_forces_)),
      accumulated_child_moments_nm_(std::move(other.accumulated_child_moments_nm_)),
      staged_state_(std::move(other.staged_state_)),
      staged_angular_speed_rad_s_(other.staged_angular_speed_rad_s_),
      staged_total_generalized_friction_torque_nm_(
          other.staged_total_generalized_friction_torque_nm_),
      stage_published_(std::exchange(other.stage_published_, false)),
      reaction_published_(std::exchange(other.reaction_published_, false)) {}

OneLevelMasterRodCoupledReactionWorkspace &
OneLevelMasterRodCoupledReactionWorkspace::operator=(
    OneLevelMasterRodCoupledReactionWorkspace &&other) noexcept {
    if (this != &other) {
        owner_mechanism_ = std::exchange(other.owner_mechanism_, nullptr);
        owner_data_ = std::exchange(other.owner_data_, nullptr);
        owner_count_ = std::exchange(other.owner_count_, 0U);
        friction_stages_ = std::move(other.friction_stages_);
        reactions_ = std::move(other.reactions_);
        accumulated_child_forces_ = std::move(other.accumulated_child_forces_);
        accumulated_child_moments_nm_ = std::move(other.accumulated_child_moments_nm_);
        staged_state_ = std::move(other.staged_state_);
        staged_angular_speed_rad_s_ = other.staged_angular_speed_rad_s_;
        staged_total_generalized_friction_torque_nm_ =
            other.staged_total_generalized_friction_torque_nm_;
        stage_published_ = std::exchange(other.stage_published_, false);
        reaction_published_ = std::exchange(other.reaction_published_, false);
    }
    return *this;
}

void OneLevelMasterRodCoupledReactionWorkspace::invalidate_all() noexcept {
    stage_published_ = false;
    reaction_published_ = false;
    staged_angular_speed_rad_s_ = 0.0;
    staged_total_generalized_friction_torque_nm_ = 0.0;
}

void OneLevelMasterRodCoupledReactionWorkspace::invalidate_reaction() noexcept {
    reaction_published_ = false;
}

OneLevelMasterRodCoupledReactionWorkspace
make_one_level_master_rod_coupled_reaction_workspace(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism) {
    return OneLevelMasterRodCoupledReactionWorkspace{mechanism};
}

OneLevelMasterRodFrictionStageCalculation
stage_one_level_master_rod_piston_wall_friction(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism,
    const OneLevelMasterRodArticulatedState &state,
    const std::span<const OneLevelMasterRodPistonWallBoundaryInput> boundaries,
    const double angular_speed_rad_s,
    OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
    WorkspaceAccess::invalidate_all(workspace);
    const auto views = mechanism.cylinder_views();
    if (!WorkspaceAccess::owner_matches(workspace, mechanism, views)) {
        return error(OneLevelMasterRodCoupledReactionIssue::workspace_owner_mismatch);
    }
    if (!std::isfinite(angular_speed_rad_s)) {
        return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_angular_speed);
    }
    if (state.cylinders.size() != views.size()) {
        return error(OneLevelMasterRodCoupledReactionIssue::incorrect_state_size);
    }
    if (boundaries.size() != views.size()) {
        return error(OneLevelMasterRodCoupledReactionIssue::incorrect_boundary_size);
    }

    auto &stages = WorkspaceAccess::stages(workspace);
    double total_torque_nm = 0.0;
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto &view = views[index];
        const auto &cylinder_state = state.cylinders[index];
        const auto &boundary = boundaries[index];
        if (!finite(cylinder_state)) {
            return error(
                OneLevelMasterRodCoupledReactionIssue::malformed_articulated_state,
                index);
        }
        if (view.cylinder_id != cylinder_state.cylinder_id ||
            view.cylinder_id != boundary.cylinder_id) {
            return error(
                OneLevelMasterRodCoupledReactionIssue::cylinder_identity_mismatch,
                index);
        }
        if (!std::isfinite(boundary.chamber_pressure_pa_abs) ||
            !std::isfinite(boundary.crankcase_pressure_pa_abs) ||
            !std::isfinite(boundary.retained_previous_wall_reaction_magnitude_n)) {
            return error(
                OneLevelMasterRodCoupledReactionIssue::nonfinite_boundary_value, index);
        }
        if (!(boundary.chamber_pressure_pa_abs > 0.0) ||
            !(boundary.crankcase_pressure_pa_abs > 0.0)) {
            return error(
                OneLevelMasterRodCoupledReactionIssue::nonpositive_absolute_pressure,
                index);
        }
        if (boundary.retained_previous_wall_reaction_magnitude_n < 0.0) {
            return error(
                OneLevelMasterRodCoupledReactionIssue::negative_retained_wall_reaction,
                index);
        }

        const double slider_derivative =
            cylinder_state.wrist_pin.dx_dtheta_m_per_rad * view.bank_axis_x +
            cylinder_state.wrist_pin.dy_dtheta_m_per_rad * view.bank_axis_y;
        const auto law_calculation =
            stage_engine_sim_v1_piston_wall_kinematic_friction({
                slider_derivative,
                angular_speed_rad_s,
                boundary.retained_previous_wall_reaction_magnitude_n,
            });
        if (const auto *law_error =
                std::get_if<EngineSimV1PistonWallError>(&law_calculation)) {
            return error(OneLevelMasterRodCoupledReactionIssue::friction_law_rejected,
                         index, law_error->issue);
        }
        const auto &law =
            std::get<EngineSimV1PistonWallKinematicFrictionStage>(law_calculation);
        const double pressure_differential =
            boundary.chamber_pressure_pa_abs - boundary.crankcase_pressure_pa_abs;
        const double pressure_force = -view.piston_area_m2 * pressure_differential;
        stages[index] = {
            boundary,
            slider_derivative,
            law.signed_slider_axis_velocity_m_s,
            pressure_force,
            law.friction_force_magnitude_n,
            law.signed_slider_axis_friction_force_n,
            law.generalized_friction_torque_nm,
        };
        total_torque_nm += law.generalized_friction_torque_nm;
        if (!std::isfinite(pressure_force) || !std::isfinite(total_torque_nm)) {
            return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_derived_value,
                         index);
        }
    }

    auto &staged_state = WorkspaceAccess::staged_state(workspace);
    for (std::size_t index = 0; index < views.size(); ++index) {
        staged_state[index] = state.cylinders[index];
    }
    WorkspaceAccess::publish_stage(workspace, angular_speed_rad_s, total_torque_nm);
    return OneLevelMasterRodFrictionStageResult{
        total_torque_nm,
        std::span<const OneLevelMasterRodPistonWallFrictionStage>{stages},
    };
}

namespace detail {

std::variant<OneLevelMasterRodCylinderReaction, OneLevelMasterRodCoupledReactionError>
calculate_one_level_master_rod_branch_reaction(
    const OneLevelMasterRodCompiledCylinderView &cylinder,
    const OneLevelMasterRodCylinderArticulatedState &state,
    const double signed_axis_pressure_force_n,
    const double signed_axis_friction_force_n, const double angular_speed_rad_s,
    const double angular_acceleration_rad_s2,
    const OneLevelMasterRodPlanarForce child_force_sum_n,
    const double child_moment_about_big_end_nm,
    const std::size_t cylinder_index) noexcept {
    if (!valid_compiled_cylinder(cylinder)) {
        return error(OneLevelMasterRodCoupledReactionIssue::invalid_compiled_cylinder,
                     cylinder_index);
    }
    if (!finite(state)) {
        return error(OneLevelMasterRodCoupledReactionIssue::malformed_articulated_state,
                     cylinder_index);
    }
    if (state.cylinder_id != cylinder.cylinder_id) {
        return error(OneLevelMasterRodCoupledReactionIssue::cylinder_identity_mismatch,
                     cylinder_index);
    }
    if (!std::isfinite(angular_speed_rad_s)) {
        return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_angular_speed,
                     cylinder_index);
    }
    if (!std::isfinite(angular_acceleration_rad_s2)) {
        return error(
            OneLevelMasterRodCoupledReactionIssue::nonfinite_angular_acceleration,
            cylinder_index);
    }
    if (!std::isfinite(signed_axis_pressure_force_n) ||
        !std::isfinite(signed_axis_friction_force_n) || !finite(child_force_sum_n) ||
        !std::isfinite(child_moment_about_big_end_nm)) {
        return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_derived_value,
                     cylinder_index);
    }
    return calculate_branch_unchecked(cylinder, state, signed_axis_pressure_force_n,
                                      signed_axis_friction_force_n, angular_speed_rad_s,
                                      angular_acceleration_rad_s2, child_force_sum_n,
                                      child_moment_about_big_end_nm, cylinder_index);
}

} // namespace detail

OneLevelMasterRodCoupledReactionCalculation
calculate_one_level_master_rod_coupled_reactions(
    const CompiledOneLevelMasterRodArticulatedMechanism &mechanism,
    const OneLevelMasterRodArticulatedState &state, const double angular_speed_rad_s,
    const double angular_acceleration_rad_s2,
    OneLevelMasterRodCoupledReactionWorkspace &workspace) noexcept {
    WorkspaceAccess::invalidate_reaction(workspace);
    const auto views = mechanism.cylinder_views();
    if (!WorkspaceAccess::owner_matches(workspace, mechanism, views)) {
        return error(OneLevelMasterRodCoupledReactionIssue::workspace_owner_mismatch);
    }
    if (!WorkspaceAccess::stage_published(workspace)) {
        return error(OneLevelMasterRodCoupledReactionIssue::missing_friction_stage);
    }
    if (state.cylinders.size() != views.size()) {
        return error(OneLevelMasterRodCoupledReactionIssue::incorrect_state_size);
    }
    if (!same_binary64(angular_speed_rad_s,
                       WorkspaceAccess::staged_angular_speed(workspace))) {
        return error(OneLevelMasterRodCoupledReactionIssue::staged_state_mismatch);
    }
    const auto &staged_state = WorkspaceAccess::staged_state(workspace);
    for (std::size_t index = 0; index < views.size(); ++index) {
        if (!same_state_binary(state.cylinders[index], staged_state[index])) {
            return error(OneLevelMasterRodCoupledReactionIssue::staged_state_mismatch,
                         index);
        }
    }
    if (!std::isfinite(angular_acceleration_rad_s2)) {
        return error(
            OneLevelMasterRodCoupledReactionIssue::nonfinite_angular_acceleration);
    }

    auto &child_forces = WorkspaceAccess::child_forces(workspace);
    auto &child_moments = WorkspaceAccess::child_moments(workspace);
    auto &reactions = WorkspaceAccess::reactions(workspace);
    const auto &stages = WorkspaceAccess::stages(workspace);
    for (std::size_t index = 0; index < views.size(); ++index) {
        child_forces[index] = {};
        child_moments[index] = 0.0;
    }

    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto &view = views[index];
        if (view.direct_root) {
            continue;
        }
        const auto &friction = stages[index];
        const auto calculation = calculate_branch_unchecked(
            view, staged_state[index], friction.signed_axis_pressure_force_n,
            friction.signed_axis_friction_force_n, angular_speed_rad_s,
            angular_acceleration_rad_s2, {}, 0.0, index);
        if (const auto *branch_error =
                std::get_if<OneLevelMasterRodCoupledReactionError>(&calculation)) {
            return *branch_error;
        }
        reactions[index] = std::get<OneLevelMasterRodCylinderReaction>(calculation);

        const std::size_t root_index = view.parent_root_index;
        const OneLevelMasterRodPlanarForce child_on_root =
            scale(-1.0, reactions[index].parent_on_rod_at_big_end_n);
        child_forces[root_index] = add(child_forces[root_index], child_on_root);
        const auto pin_from_root = point_difference(staged_state[index].big_end,
                                                    staged_state[root_index].big_end);
        child_moments[root_index] += cross(pin_from_root, child_on_root);
        if (!finite(child_forces[root_index]) ||
            !std::isfinite(child_moments[root_index])) {
            return error(OneLevelMasterRodCoupledReactionIssue::nonfinite_derived_value,
                         index);
        }
    }

    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto &view = views[index];
        if (!view.direct_root) {
            continue;
        }
        const auto &friction = stages[index];
        const auto calculation = calculate_branch_unchecked(
            view, staged_state[index], friction.signed_axis_pressure_force_n,
            friction.signed_axis_friction_force_n, angular_speed_rad_s,
            angular_acceleration_rad_s2, child_forces[index], child_moments[index],
            index);
        if (const auto *branch_error =
                std::get_if<OneLevelMasterRodCoupledReactionError>(&calculation)) {
            return *branch_error;
        }
        reactions[index] = std::get<OneLevelMasterRodCylinderReaction>(calculation);
    }

    WorkspaceAccess::publish_reaction(workspace);
    return OneLevelMasterRodCoupledReactionResult{
        WorkspaceAccess::staged_total_torque(workspace),
        std::span<const OneLevelMasterRodCylinderReaction>{reactions},
    };
}

} // namespace engine_sim_offline::simulation
