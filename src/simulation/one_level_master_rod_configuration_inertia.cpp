#include "simulation/one_level_master_rod_configuration_inertia.hpp"

#include "simulation/legacy_mechanics_primitives.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

struct SecondOrderScalar {
    double value = 0.0;
    double first = 0.0;
    double second = 0.0;
};

struct SecondOrderPoint {
    SecondOrderScalar x;
    SecondOrderScalar y;
};

struct Linkage {
    const OneLevelMasterRodDriver *driver = nullptr;
    const OneLevelMasterRodCylinder *cylinder = nullptr;
    bool direct_root = false;
};

[[nodiscard]] OneLevelMasterRodConfigurationInertiaError
error(const OneLevelMasterRodConfigurationInertiaIssue issue,
      const std::size_t cylinder_index = kNoOneLevelMasterRodInertiaCylinder) noexcept {
    return {issue, cylinder_index};
}

[[nodiscard]] bool finite_positive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool finite(const SecondOrderScalar &value) noexcept {
    return std::isfinite(value.value) && std::isfinite(value.first) &&
           std::isfinite(value.second);
}

[[nodiscard]] bool finite(const SecondOrderPoint &point) noexcept {
    return finite(point.x) && finite(point.y);
}

[[nodiscard]] bool same_binary64(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] SecondOrderScalar add(const SecondOrderScalar left,
                                    const SecondOrderScalar right) noexcept {
    return {
        left.value + right.value,
        left.first + right.first,
        left.second + right.second,
    };
}

[[nodiscard]] SecondOrderPoint add(const SecondOrderPoint &left,
                                   const SecondOrderPoint &right) noexcept {
    return {add(left.x, right.x), add(left.y, right.y)};
}

[[nodiscard]] SecondOrderScalar subtract(const SecondOrderScalar left,
                                         const SecondOrderScalar right) noexcept {
    return {
        left.value - right.value,
        left.first - right.first,
        left.second - right.second,
    };
}

[[nodiscard]] SecondOrderPoint subtract(const SecondOrderPoint &left,
                                        const SecondOrderPoint &right) noexcept {
    return {subtract(left.x, right.x), subtract(left.y, right.y)};
}

[[nodiscard]] SecondOrderScalar multiply(const SecondOrderScalar left,
                                         const SecondOrderScalar right) noexcept {
    return {
        left.value * right.value,
        left.first * right.value + left.value * right.first,
        left.second * right.value + 2.0 * left.first * right.first +
            left.value * right.second,
    };
}

[[nodiscard]] SecondOrderScalar scale(const SecondOrderScalar value,
                                      const double scalar) noexcept {
    return {
        value.value * scalar,
        value.first * scalar,
        value.second * scalar,
    };
}

[[nodiscard]] SecondOrderPoint scale(const SecondOrderPoint &point,
                                     const double scalar) noexcept {
    return {scale(point.x, scalar), scale(point.y, scalar)};
}

[[nodiscard]] SecondOrderScalar sine(const SecondOrderScalar value) noexcept {
    const double sine_value = std::sin(value.value);
    const double cosine_value = std::cos(value.value);
    return {
        sine_value,
        cosine_value * value.first,
        -sine_value * value.first * value.first + cosine_value * value.second,
    };
}

[[nodiscard]] SecondOrderScalar cosine(const SecondOrderScalar value) noexcept {
    const double sine_value = std::sin(value.value);
    const double cosine_value = std::cos(value.value);
    return {
        cosine_value,
        -sine_value * value.first,
        -cosine_value * value.first * value.first - sine_value * value.second,
    };
}

[[nodiscard]] std::optional<SecondOrderScalar>
square_root(const SecondOrderScalar value) noexcept {
    if (!finite(value) || !(value.value > 0.0)) {
        return std::nullopt;
    }
    const double root = std::sqrt(value.value);
    const double first = value.first / (2.0 * root);
    const double root_cubed = root * root * root;
    const double second =
        value.second / (2.0 * root) - value.first * value.first / (4.0 * root_cubed);
    const SecondOrderScalar result{root, first, second};
    return finite(result) ? std::optional<SecondOrderScalar>{result} : std::nullopt;
}

[[nodiscard]] SecondOrderPoint
crank_pin(const OneLevelMasterRodDriver &driver,
          const SecondOrderScalar body_angle_psi_rad) noexcept {
    const SecondOrderScalar angle{
        body_angle_psi_rad.value + driver.crank_journal_global_phase_rad,
        body_angle_psi_rad.first,
        body_angle_psi_rad.second,
    };
    return {
        scale(cosine(angle), driver.crank_radius_m),
        scale(sine(angle), driver.crank_radius_m),
    };
}

[[nodiscard]] std::optional<SecondOrderScalar>
slider_axis_position(const SecondOrderPoint &driven_point, const double bank_angle_rad,
                     const double connecting_rod_length_m) noexcept {
    const double axis_angle_rad = bank_angle_rad + kLegacyPi / 2.0;
    const double axis_x = std::cos(axis_angle_rad);
    const double axis_y = std::sin(axis_angle_rad);
    if (!std::isfinite(axis_angle_rad) || !std::isfinite(axis_x) ||
        !std::isfinite(axis_y)) {
        return std::nullopt;
    }
    const SecondOrderScalar projection =
        add(scale(driven_point.x, axis_x), scale(driven_point.y, axis_y));
    const SecondOrderScalar norm_squared =
        add(multiply(driven_point.x, driven_point.x),
            multiply(driven_point.y, driven_point.y));
    const SecondOrderScalar radicand = add(
        subtract(SecondOrderScalar{connecting_rod_length_m * connecting_rod_length_m,
                                   0.0, 0.0},
                 norm_squared),
        multiply(projection, projection));
    const auto root = square_root(radicand);
    if (!root.has_value()) {
        return std::nullopt;
    }
    const SecondOrderScalar position = add(projection, *root);
    if (!finite(position) || position.value < 0.0) {
        return std::nullopt;
    }
    return position;
}

[[nodiscard]] std::optional<SecondOrderPoint>
slider_wrist_pin(const SecondOrderPoint &driven_point, const double bank_angle_rad,
                 const double connecting_rod_length_m) noexcept {
    const auto position =
        slider_axis_position(driven_point, bank_angle_rad, connecting_rod_length_m);
    if (!position.has_value()) {
        return std::nullopt;
    }
    const double axis_angle_rad = bank_angle_rad + kLegacyPi / 2.0;
    const SecondOrderPoint wrist{
        scale(*position, std::cos(axis_angle_rad)),
        scale(*position, std::sin(axis_angle_rad)),
    };
    return finite(wrist) ? std::optional<SecondOrderPoint>{wrist} : std::nullopt;
}

[[nodiscard]] std::optional<SecondOrderPoint>
slave_big_end(const OneLevelMasterRodDriver &driver,
              const OneLevelMasterRodSlavePin &attachment,
              const SecondOrderPoint &crank_journal,
              const SecondOrderPoint &master_wrist) noexcept {
    const SecondOrderPoint master_direction =
        scale(subtract(master_wrist, crank_journal),
              1.0 / driver.master_connecting_rod_length_m);
    const double phase_cosine = std::cos(attachment.local_phase_rad);
    const double phase_sine = std::sin(attachment.local_phase_rad);
    const SecondOrderPoint rotated{
        subtract(scale(master_direction.x, phase_cosine),
                 scale(master_direction.y, phase_sine)),
        add(scale(master_direction.x, phase_sine),
            scale(master_direction.y, phase_cosine)),
    };
    const SecondOrderPoint result =
        add(crank_journal, scale(rotated, attachment.throw_radius_m));
    return finite(result) ? std::optional<SecondOrderPoint>{result} : std::nullopt;
}

[[nodiscard]] const OneLevelMasterRodCylinder &
geometry_of(const OneLevelMasterRodMechanismCylinderPlan &planned) noexcept {
    return std::visit(
        [](const auto &kinematics) -> const OneLevelMasterRodCylinder & {
            return kinematics.cylinder;
        },
        planned.kinematics);
}

[[nodiscard]] Linkage linkage_at(const OneLevelMasterRodMechanismKinematicsPlan &plan,
                                 const std::size_t cylinder_index) noexcept {
    const auto &kinematics = plan.cylinders[cylinder_index].kinematics;
    if (const auto *root = std::get_if<OneLevelMasterRodDirectRootPlan>(&kinematics)) {
        return {&root->driver, &root->cylinder, true};
    }
    const auto &slave = std::get<OneLevelMasterRodSlaveAttachmentPlan>(kinematics);
    const auto &master = std::get<OneLevelMasterRodDirectRootPlan>(
        plan.cylinders[slave.master_cylinder_index].kinematics);
    return {&master.driver, &slave.cylinder, false};
}

[[nodiscard]] bool valid_driver(const OneLevelMasterRodDriver &driver) noexcept {
    return finite_positive(driver.crank_radius_m) &&
           std::isfinite(driver.crank_journal_global_phase_rad) &&
           std::isfinite(driver.master_bank_angle_rad) &&
           finite_positive(driver.master_connecting_rod_length_m) &&
           driver.crank_radius_m < driver.master_connecting_rod_length_m;
}

[[nodiscard]] bool
valid_cylinder_geometry(const OneLevelMasterRodCylinder &cylinder) noexcept {
    if (!cylinder.cylinder_id.valid() || !std::isfinite(cylinder.bank_angle_rad) ||
        !finite_positive(cylinder.connecting_rod_length_m) ||
        !finite_positive(cylinder.piston_area_m2) ||
        !finite_positive(cylinder.deck_height_m) ||
        !finite_positive(cylinder.piston_compression_height_m) ||
        !std::isfinite(cylinder.piston_wrist_pin_position_m) ||
        cylinder.piston_wrist_pin_position_m < 0.0 ||
        !finite_positive(cylinder.head_chamber_volume_m3) ||
        !std::isfinite(cylinder.piston_displacement_term_m3)) {
        return false;
    }
    const auto *slave = std::get_if<OneLevelMasterRodSlavePin>(&cylinder.journal);
    return slave == nullptr || (finite_positive(slave->throw_radius_m) &&
                                std::isfinite(slave->local_phase_rad));
}

[[nodiscard]] std::optional<OneLevelMasterRodConfigurationInertiaError>
validate_plan(const OneLevelMasterRodMechanismKinematicsPlan &plan) noexcept {
    if (!plan.engine_id.valid() ||
        !contract::is_valid_semantic_id(plan.engine_profile_id) ||
        !plan.output_crankshaft_id.valid()) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::invalid_plan_identity);
    }
    if (!std::isfinite(plan.crank_tdc_reference_rad)) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::nonfinite_plan_value);
    }
    if (plan.rigid_crank_group.crankshaft_count != 1U ||
        !finite_positive(plan.rigid_crank_group.authored_crank_inertia_kg_m2) ||
        !std::isfinite(plan.rigid_crank_group.running_friction_torque_magnitude_nm) ||
        plan.rigid_crank_group.running_friction_torque_magnitude_nm < 0.0) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::invalid_rigid_crank_group);
    }
    if (plan.cylinders.empty()) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::empty_cylinder_set);
    }
    if (plan.cylinders.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max())) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::invalid_cylinder_topology);
    }

    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto &planned = plan.cylinders[index];
        const auto &geometry = geometry_of(planned);
        bool duplicate_cylinder_id = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            duplicate_cylinder_id =
                duplicate_cylinder_id ||
                geometry_of(plan.cylinders[prior]).cylinder_id == geometry.cylinder_id;
        }
        if (!geometry.cylinder_id.valid() || duplicate_cylinder_id) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::invalid_cylinder_identity,
                index);
        }
        if (!planned.crankshaft_id.valid() ||
            planned.crankshaft_id != plan.output_crankshaft_id ||
            !planned.bank_id.valid() || !planned.chamber_volume_id.valid() ||
            !planned.exhaust_route_id.valid()) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::invalid_cylinder_topology,
                index);
        }
        if (!finite_positive(planned.bore_m) ||
            !finite_positive(planned.piston_area_m2) ||
            !finite_positive(planned.fixed_geometry_volume_m3) ||
            !std::isfinite(planned.ignition_wire_angle_rad)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_plan_value,
                index);
        }
        if (!finite_positive(planned.piston_mass_kg) ||
            !finite_positive(planned.connecting_rod_mass_kg) ||
            !finite_positive(planned.connecting_rod_inertia_kg_m2)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::invalid_mass_property,
                index);
        }
        const double reconstructed_piston_area_m2 =
            kLegacyPi * planned.bore_m * planned.bore_m / 4.0;
        if (!valid_cylinder_geometry(geometry) ||
            !same_binary64(planned.piston_area_m2, reconstructed_piston_area_m2) ||
            !same_binary64(planned.piston_area_m2, geometry.piston_area_m2)) {
            return error(OneLevelMasterRodConfigurationInertiaIssue::invalid_kinematics,
                         index);
        }
        const double reconstructed_fixed_volume_m3 =
            geometry.head_chamber_volume_m3 +
            geometry.piston_area_m2 *
                (geometry.deck_height_m - geometry.piston_wrist_pin_position_m -
                 geometry.piston_compression_height_m);
        if (!same_binary64(planned.fixed_geometry_volume_m3,
                           reconstructed_fixed_volume_m3)) {
            return error(OneLevelMasterRodConfigurationInertiaIssue::invalid_kinematics,
                         index);
        }
        if (!std::isfinite(planned.connecting_rod_center_of_mass_from_big_end_m) ||
            planned.connecting_rod_center_of_mass_from_big_end_m < 0.0 ||
            planned.connecting_rod_center_of_mass_from_big_end_m >
                geometry.connecting_rod_length_m) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::invalid_center_of_mass,
                index);
        }

        const auto &kinematics = planned.kinematics;
        const OneLevelMasterRodDriver *driver = nullptr;
        if (const auto *root =
                std::get_if<OneLevelMasterRodDirectRootPlan>(&kinematics)) {
            if (!std::holds_alternative<OneLevelMasterRodRootJournal>(
                    root->cylinder.journal) ||
                !valid_driver(root->driver) ||
                !same_binary64(root->cylinder.bank_angle_rad,
                               root->driver.master_bank_angle_rad) ||
                !same_binary64(root->cylinder.connecting_rod_length_m,
                               root->driver.master_connecting_rod_length_m)) {
                return error(
                    OneLevelMasterRodConfigurationInertiaIssue::invalid_kinematics,
                    index);
            }
            driver = &root->driver;
        } else {
            const auto &slave =
                std::get<OneLevelMasterRodSlaveAttachmentPlan>(kinematics);
            if (!std::holds_alternative<OneLevelMasterRodSlavePin>(
                    slave.cylinder.journal) ||
                slave.master_cylinder_index >= plan.cylinders.size()) {
                return error(OneLevelMasterRodConfigurationInertiaIssue::
                                 invalid_slave_attachment,
                             index);
            }
            const auto *master_root = std::get_if<OneLevelMasterRodDirectRootPlan>(
                &plan.cylinders[slave.master_cylinder_index].kinematics);
            if (master_root == nullptr || !valid_driver(master_root->driver)) {
                return error(OneLevelMasterRodConfigurationInertiaIssue::
                                 invalid_slave_attachment,
                             index);
            }
            driver = &master_root->driver;
        }
        if (driver == nullptr ||
            !certify_one_level_master_rod_full_cycle(*driver, geometry).admitted()) {
            return error(OneLevelMasterRodConfigurationInertiaIssue::
                             uncertified_full_cycle_geometry,
                         index);
        }
    }
    return std::nullopt;
}

[[nodiscard]] OneLevelMasterRodPlanarPointState
public_point(const SecondOrderPoint &point) noexcept {
    return {
        point.x.value, point.y.value,  point.x.first,
        point.y.first, point.x.second, point.y.second,
    };
}

[[nodiscard]] bool finite(const OneLevelMasterRodPlanarPointState &point) noexcept {
    return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
           std::isfinite(point.dx_dtheta_m_per_rad) &&
           std::isfinite(point.dy_dtheta_m_per_rad) &&
           std::isfinite(point.d2x_dtheta2_m_per_rad2) &&
           std::isfinite(point.d2y_dtheta2_m_per_rad2);
}

} // namespace

CompiledOneLevelMasterRodArticulatedMechanism::
    CompiledOneLevelMasterRodArticulatedMechanism(
        const double crank_tdc_reference_rad, const double authored_crank_inertia_kg_m2,
        std::vector<Cylinder> cylinders,
        std::vector<OneLevelMasterRodCompiledCylinderView> cylinder_views) noexcept
    : crank_tdc_reference_rad_(crank_tdc_reference_rad),
      authored_crank_inertia_kg_m2_(authored_crank_inertia_kg_m2),
      cylinders_(std::move(cylinders)), cylinder_views_(std::move(cylinder_views)) {}

std::size_t
CompiledOneLevelMasterRodArticulatedMechanism::cylinder_count() const noexcept {
    return cylinders_.size();
}

std::span<const OneLevelMasterRodCompiledCylinderView>
CompiledOneLevelMasterRodArticulatedMechanism::cylinder_views() const noexcept {
    return cylinder_views_;
}

OneLevelMasterRodArticulatedState
CompiledOneLevelMasterRodArticulatedMechanism::make_state_scratch() const {
    OneLevelMasterRodArticulatedState result;
    result.cylinders.resize(cylinders_.size());
    return result;
}

OneLevelMasterRodArticulatedMechanismCompilation
compile_one_level_master_rod_articulated_mechanism(
    const OneLevelMasterRodMechanismKinematicsPlan &plan) {
    if (const auto validation_error = validate_plan(plan);
        validation_error.has_value()) {
        return *validation_error;
    }

    std::vector<CompiledOneLevelMasterRodArticulatedMechanism::Cylinder> cylinders;
    std::vector<OneLevelMasterRodCompiledCylinderView> cylinder_views;
    cylinders.reserve(plan.cylinders.size());
    cylinder_views.reserve(plan.cylinders.size());
    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto linkage = linkage_at(plan, index);
        OneLevelMasterRodSlavePin slave_pin;
        if (!linkage.direct_root) {
            slave_pin = std::get<OneLevelMasterRodSlavePin>(linkage.cylinder->journal);
        }
        const auto &planned = plan.cylinders[index];
        cylinders.push_back({
            *linkage.driver,
            *linkage.cylinder,
            planned.piston_mass_kg,
            planned.connecting_rod_mass_kg,
            planned.connecting_rod_inertia_kg_m2,
            planned.connecting_rod_center_of_mass_from_big_end_m,
            slave_pin,
            linkage.direct_root,
        });
        const double axis_angle_rad =
            linkage.cylinder->bank_angle_rad + kLegacyPi / 2.0;
        const double axis_x = std::cos(axis_angle_rad);
        const double axis_y = std::sin(axis_angle_rad);
        const std::size_t parent_root_index =
            linkage.direct_root
                ? kNoOneLevelMasterRodParentCylinder
                : std::get<OneLevelMasterRodSlaveAttachmentPlan>(planned.kinematics)
                      .master_cylinder_index;
        cylinder_views.push_back({
            linkage.cylinder->cylinder_id,
            parent_root_index,
            axis_x,
            axis_y,
            axis_y,
            -axis_x,
            planned.piston_area_m2,
            planned.piston_mass_kg,
            linkage.cylinder->connecting_rod_length_m,
            planned.connecting_rod_mass_kg,
            planned.connecting_rod_inertia_kg_m2,
            planned.connecting_rod_center_of_mass_from_big_end_m /
                linkage.cylinder->connecting_rod_length_m,
            linkage.direct_root,
        });
    }
    return CompiledOneLevelMasterRodArticulatedMechanism{
        plan.crank_tdc_reference_rad,
        plan.rigid_crank_group.authored_crank_inertia_kg_m2,
        std::move(cylinders),
        std::move(cylinder_views),
    };
}

std::optional<OneLevelMasterRodConfigurationInertiaError>
CompiledOneLevelMasterRodArticulatedMechanism::evaluate_articulated_state(
    const double crank_angle_theta_rad,
    OneLevelMasterRodArticulatedState &scratch) const noexcept {
    if (!std::isfinite(crank_angle_theta_rad)) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::nonfinite_crank_angle);
    }
    if (scratch.cylinders.size() != cylinders_.size()) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::incorrect_state_scratch_size);
    }

    // Positive dynamic-crank theta advances opposite pristine engine-sim's body
    // angle psi. The compiled reference makes this the same unwrapped coordinate as
    // LegacyMechanismStep::theta_unwrapped_rad.
    const double body_angle_psi_rad = crank_tdc_reference_rad_ - crank_angle_theta_rad;
    if (!std::isfinite(body_angle_psi_rad)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    }
    return evaluate_articulated_state_at_body_angle_psi(body_angle_psi_rad, scratch);
}

std::optional<OneLevelMasterRodConfigurationInertiaError>
CompiledOneLevelMasterRodArticulatedMechanism::
    evaluate_articulated_state_at_body_angle_psi(
        const double canonical_body_angle_psi_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept {
    if (!std::isfinite(canonical_body_angle_psi_rad)) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::nonfinite_crank_angle);
    }
    if (scratch.cylinders.size() != cylinders_.size()) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::incorrect_state_scratch_size);
    }
    const SecondOrderScalar body_angle_psi_rad{
        canonical_body_angle_psi_rad,
        -1.0,
        0.0,
    };
    if (!finite(body_angle_psi_rad)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    }

    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        const auto &compiled = cylinders_[index];
        const auto &driver = compiled.driver;
        const auto &cylinder = compiled.geometry;
        const SecondOrderPoint journal = crank_pin(driver, body_angle_psi_rad);
        const auto master_wrist =
            slider_wrist_pin(journal, driver.master_bank_angle_rad,
                             driver.master_connecting_rod_length_m);
        if (!finite(journal) || !master_wrist.has_value()) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                index);
        }

        SecondOrderPoint big_end = journal;
        SecondOrderPoint wrist = *master_wrist;
        if (!compiled.direct_root) {
            const auto calculated_big_end =
                slave_big_end(driver, compiled.slave_pin, journal, *master_wrist);
            if (!calculated_big_end.has_value()) {
                return error(
                    OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                    index);
            }
            big_end = *calculated_big_end;
            const auto calculated_wrist = slider_wrist_pin(
                big_end, cylinder.bank_angle_rad, cylinder.connecting_rod_length_m);
            if (!calculated_wrist.has_value()) {
                return error(
                    OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                    index);
            }
            wrist = *calculated_wrist;
        }

        const double center_distance_m =
            compiled.connecting_rod_center_of_mass_from_big_end_m;
        const double midpoint_m = 0.5 * cylinder.connecting_rod_length_m;
        const SecondOrderPoint rod_center =
            center_distance_m == midpoint_m
                ? scale(add(big_end, wrist), 0.5)
                : add(big_end,
                      scale(subtract(wrist, big_end),
                            center_distance_m / cylinder.connecting_rod_length_m));
        const SecondOrderPoint rod_vector = subtract(wrist, big_end);
        const double rod_length_squared = rod_vector.x.value * rod_vector.x.value +
                                          rod_vector.y.value * rod_vector.y.value;
        const double angle_first_numerator = rod_vector.x.value * rod_vector.y.first -
                                             rod_vector.y.value * rod_vector.x.first;
        const double length_squared_first =
            2.0 * (rod_vector.x.value * rod_vector.x.first +
                   rod_vector.y.value * rod_vector.y.first);
        const double angle_second_numerator = rod_vector.x.value * rod_vector.y.second -
                                              rod_vector.y.value * rod_vector.x.second;
        if (!finite(big_end) || !finite(wrist) || !finite(rod_center) ||
            !finite_positive(rod_length_squared) ||
            !std::isfinite(angle_first_numerator) ||
            !std::isfinite(length_squared_first) ||
            !std::isfinite(angle_second_numerator)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                index);
        }
        const double rod_angle_rad = std::atan2(rod_vector.y.value, rod_vector.x.value);
        const double rod_angle_first = angle_first_numerator / rod_length_squared;
        const double rod_angle_second =
            (angle_second_numerator - rod_angle_first * length_squared_first) /
            rod_length_squared;

        OneLevelMasterRodCylinderArticulatedState state{
            cylinder.cylinder_id,     public_point(big_end), public_point(wrist),
            public_point(rod_center), rod_angle_rad,         rod_angle_first,
            rod_angle_second,
        };
        if (!finite(state.big_end) || !finite(state.wrist_pin) ||
            !finite(state.rod_center_of_mass) || !std::isfinite(state.rod_angle_rad) ||
            !std::isfinite(state.rod_angle_first_derivative_rad_per_rad) ||
            !std::isfinite(state.rod_angle_second_derivative_rad_per_rad2)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                index);
        }
        scratch.cylinders[index] = std::move(state);
    }
    return std::nullopt;
}

OneLevelMasterRodConfigurationInertiaCalculation
CompiledOneLevelMasterRodArticulatedMechanism::evaluate_configuration_inertia(
    const double attached_inertia_kg_m2, const double crank_angle_theta_rad,
    OneLevelMasterRodArticulatedState &scratch) const noexcept {
    if (!std::isfinite(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_attached_inertia);
    }
    if (attached_inertia_kg_m2 < 0.0 || std::signbit(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::negative_attached_inertia);
    }
    if (const auto state_error =
            evaluate_articulated_state(crank_angle_theta_rad, scratch);
        state_error.has_value()) {
        return *state_error;
    }
    return reduce_configuration_inertia(attached_inertia_kg_m2, scratch);
}

OneLevelMasterRodConfigurationInertiaCalculation
CompiledOneLevelMasterRodArticulatedMechanism::
    evaluate_configuration_inertia_at_body_angle_psi(
        const double attached_inertia_kg_m2, const double body_angle_psi_rad,
        OneLevelMasterRodArticulatedState &scratch) const noexcept {
    if (!std::isfinite(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_attached_inertia);
    }
    if (attached_inertia_kg_m2 < 0.0 || std::signbit(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::negative_attached_inertia);
    }
    if (const auto state_error =
            evaluate_articulated_state_at_body_angle_psi(body_angle_psi_rad, scratch);
        state_error.has_value()) {
        return *state_error;
    }
    return reduce_configuration_inertia(attached_inertia_kg_m2, scratch);
}

OneLevelMasterRodConfigurationInertiaCalculation
CompiledOneLevelMasterRodArticulatedMechanism::reduce_configuration_inertia(
    const double attached_inertia_kg_m2,
    const OneLevelMasterRodArticulatedState &scratch) const noexcept {
    OneLevelMasterRodConfigurationInertia result;
    result.authored_crank_inertia_kg_m2 = authored_crank_inertia_kg_m2_;
    result.attached_inertia_kg_m2 = attached_inertia_kg_m2;
    for (std::size_t index = 0; index < scratch.cylinders.size(); ++index) {
        const auto &compiled = cylinders_[index];
        const auto &cylinder = scratch.cylinders[index];
        const auto &wrist = cylinder.wrist_pin;
        const auto &center = cylinder.rod_center_of_mass;
        const double piston_translation_inertia =
            compiled.piston_mass_kg *
            (wrist.dx_dtheta_m_per_rad * wrist.dx_dtheta_m_per_rad +
             wrist.dy_dtheta_m_per_rad * wrist.dy_dtheta_m_per_rad);
        const double rod_translation_inertia =
            compiled.connecting_rod_mass_kg *
            (center.dx_dtheta_m_per_rad * center.dx_dtheta_m_per_rad +
             center.dy_dtheta_m_per_rad * center.dy_dtheta_m_per_rad);
        const double rod_rotation_inertia =
            compiled.connecting_rod_inertia_kg_m2 *
            cylinder.rod_angle_first_derivative_rad_per_rad *
            cylinder.rod_angle_first_derivative_rad_per_rad;
        const double piston_translation_derivative =
            2.0 * compiled.piston_mass_kg *
            (wrist.dx_dtheta_m_per_rad * wrist.d2x_dtheta2_m_per_rad2 +
             wrist.dy_dtheta_m_per_rad * wrist.d2y_dtheta2_m_per_rad2);
        const double rod_translation_derivative =
            2.0 * compiled.connecting_rod_mass_kg *
            (center.dx_dtheta_m_per_rad * center.d2x_dtheta2_m_per_rad2 +
             center.dy_dtheta_m_per_rad * center.d2y_dtheta2_m_per_rad2);
        const double rod_rotation_derivative =
            2.0 * compiled.connecting_rod_inertia_kg_m2 *
            cylinder.rod_angle_first_derivative_rad_per_rad *
            cylinder.rod_angle_second_derivative_rad_per_rad2;
        if (!std::isfinite(piston_translation_inertia) ||
            !std::isfinite(rod_translation_inertia) ||
            !std::isfinite(rod_rotation_inertia) ||
            !std::isfinite(piston_translation_derivative) ||
            !std::isfinite(rod_translation_derivative) ||
            !std::isfinite(rod_rotation_derivative)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                index);
        }
        result.piston_translation_inertia_kg_m2 += piston_translation_inertia;
        result.connecting_rod_translation_inertia_kg_m2 += rod_translation_inertia;
        result.connecting_rod_rotation_inertia_kg_m2 += rod_rotation_inertia;
        result.piston_translation_derivative_kg_m2_per_rad +=
            piston_translation_derivative;
        result.connecting_rod_translation_derivative_kg_m2_per_rad +=
            rod_translation_derivative;
        result.connecting_rod_rotation_derivative_kg_m2_per_rad +=
            rod_rotation_derivative;
        if (!std::isfinite(result.piston_translation_inertia_kg_m2) ||
            !std::isfinite(result.connecting_rod_translation_inertia_kg_m2) ||
            !std::isfinite(result.connecting_rod_rotation_inertia_kg_m2) ||
            !std::isfinite(result.piston_translation_derivative_kg_m2_per_rad) ||
            !std::isfinite(
                result.connecting_rod_translation_derivative_kg_m2_per_rad) ||
            !std::isfinite(result.connecting_rod_rotation_derivative_kg_m2_per_rad)) {
            return error(
                OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value,
                index);
        }
    }

    result.total_inertia_kg_m2 = result.authored_crank_inertia_kg_m2 +
                                 result.attached_inertia_kg_m2 +
                                 result.piston_translation_inertia_kg_m2 +
                                 result.connecting_rod_translation_inertia_kg_m2 +
                                 result.connecting_rod_rotation_inertia_kg_m2;
    result.total_derivative_kg_m2_per_rad =
        result.piston_translation_derivative_kg_m2_per_rad +
        result.connecting_rod_translation_derivative_kg_m2_per_rad +
        result.connecting_rod_rotation_derivative_kg_m2_per_rad;
    if (!std::isfinite(result.total_inertia_kg_m2) ||
        !std::isfinite(result.total_derivative_kg_m2_per_rad)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    }
    if (!(result.total_inertia_kg_m2 > 0.0)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonpositive_total_inertia);
    }
    return result;
}

OneLevelMasterRodArticulatedStateCalculation
evaluate_one_level_master_rod_articulated_state(
    const OneLevelMasterRodMechanismKinematicsPlan &plan,
    const double crank_angle_theta_rad) noexcept {
    if (!std::isfinite(crank_angle_theta_rad)) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::nonfinite_crank_angle);
    }
    try {
        auto compilation = compile_one_level_master_rod_articulated_mechanism(plan);
        if (const auto *compilation_error =
                std::get_if<OneLevelMasterRodConfigurationInertiaError>(&compilation)) {
            return *compilation_error;
        }
        const auto &compiled =
            std::get<CompiledOneLevelMasterRodArticulatedMechanism>(compilation);
        auto scratch = compiled.make_state_scratch();
        if (const auto evaluation_error =
                compiled.evaluate_articulated_state(crank_angle_theta_rad, scratch);
            evaluation_error.has_value()) {
            return *evaluation_error;
        }
        return scratch;
    } catch (...) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    }
}

OneLevelMasterRodConfigurationInertiaCalculation
evaluate_one_level_master_rod_configuration_inertia(
    const OneLevelMasterRodMechanismKinematicsPlan &plan,
    const double attached_inertia_kg_m2, const double crank_angle_theta_rad) noexcept {
    if (!std::isfinite(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_attached_inertia);
    }
    if (attached_inertia_kg_m2 < 0.0 || std::signbit(attached_inertia_kg_m2)) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::negative_attached_inertia);
    }
    if (!std::isfinite(crank_angle_theta_rad)) {
        return error(OneLevelMasterRodConfigurationInertiaIssue::nonfinite_crank_angle);
    }
    try {
        auto compilation = compile_one_level_master_rod_articulated_mechanism(plan);
        if (const auto *compilation_error =
                std::get_if<OneLevelMasterRodConfigurationInertiaError>(&compilation)) {
            return *compilation_error;
        }
        const auto &compiled =
            std::get<CompiledOneLevelMasterRodArticulatedMechanism>(compilation);
        auto scratch = compiled.make_state_scratch();
        return compiled.evaluate_configuration_inertia(attached_inertia_kg_m2,
                                                       crank_angle_theta_rad, scratch);
    } catch (...) {
        return error(
            OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    }
}

} // namespace engine_sim_offline::simulation
