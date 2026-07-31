#include "simulation/one_level_master_rod_kinematics.hpp"

#include "simulation/legacy_mechanics_primitives.hpp"

#include <bit>
#include <cmath>
#include <optional>

namespace engine_sim_offline::simulation {
namespace {

struct Dual {
    double value = 0.0;
    double derivative = 0.0;
};

struct DualPoint {
    Dual x;
    Dual y;
};

[[nodiscard]] Dual add(const Dual left, const Dual right) noexcept {
    return {left.value + right.value, left.derivative + right.derivative};
}

[[nodiscard]] Dual subtract(const Dual left, const Dual right) noexcept {
    return {left.value - right.value, left.derivative - right.derivative};
}

[[nodiscard]] Dual multiply(const Dual left, const Dual right) noexcept {
    return {
        left.value * right.value,
        left.derivative * right.value + left.value * right.derivative,
    };
}

[[nodiscard]] Dual scale(const Dual value, const double scalar) noexcept {
    return {value.value * scalar, value.derivative * scalar};
}

[[nodiscard]] Dual dual_sine(const Dual value) noexcept {
    return {std::sin(value.value), std::cos(value.value) * value.derivative};
}

[[nodiscard]] Dual dual_cosine(const Dual value) noexcept {
    return {std::cos(value.value), -std::sin(value.value) * value.derivative};
}

[[nodiscard]] std::optional<Dual> dual_square_root(const Dual value) noexcept {
    if (!std::isfinite(value.value) || !std::isfinite(value.derivative) ||
        value.value <= 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(value.value);
    const double derivative = value.derivative / (2.0 * root);
    if (!std::isfinite(root) || !std::isfinite(derivative)) {
        return std::nullopt;
    }
    return Dual{root, derivative};
}

[[nodiscard]] bool finite_positive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool same_binary64(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool finite_driver(const OneLevelMasterRodDriver &driver) noexcept {
    return finite_positive(driver.crank_radius_m) &&
           std::isfinite(driver.crank_journal_global_phase_rad) &&
           std::isfinite(driver.master_bank_angle_rad) &&
           finite_positive(driver.master_connecting_rod_length_m) &&
           driver.crank_radius_m < driver.master_connecting_rod_length_m;
}

[[nodiscard]] bool finite_cylinder(const OneLevelMasterRodCylinder &cylinder) noexcept {
    if (!cylinder.cylinder_id.valid() || !std::isfinite(cylinder.bank_angle_rad) ||
        !finite_positive(cylinder.connecting_rod_length_m) ||
        !finite_positive(cylinder.piston_area_m2) ||
        !finite_positive(cylinder.deck_height_m) ||
        !finite_positive(cylinder.piston_compression_height_m) ||
        !finite_positive(cylinder.head_chamber_volume_m3) ||
        !std::isfinite(cylinder.piston_displacement_term_m3)) {
        return false;
    }
    const auto *slave = std::get_if<OneLevelMasterRodSlavePin>(&cylinder.journal);
    return slave == nullptr || (finite_positive(slave->throw_radius_m) &&
                                std::isfinite(slave->local_phase_rad));
}

[[nodiscard]] DualPoint crank_pin(const OneLevelMasterRodDriver &driver,
                                  const Dual body_angle_psi_rad) noexcept {
    const Dual angle{
        body_angle_psi_rad.value + driver.crank_journal_global_phase_rad,
        body_angle_psi_rad.derivative,
    };
    return {
        scale(dual_cosine(angle), driver.crank_radius_m),
        scale(dual_sine(angle), driver.crank_radius_m),
    };
}

[[nodiscard]] std::optional<Dual>
slider_axis_position(const DualPoint point, const double bank_angle_rad,
                     const double connecting_rod_length_m) noexcept {
    const double axis_angle_rad = bank_angle_rad + kLegacyPi / 2.0;
    const double axis_x = std::cos(axis_angle_rad);
    const double axis_y = std::sin(axis_angle_rad);
    const Dual projection = add(scale(point.x, axis_x), scale(point.y, axis_y));
    const Dual norm_squared =
        add(multiply(point.x, point.x), multiply(point.y, point.y));
    const Dual radicand =
        add(subtract(Dual{connecting_rod_length_m * connecting_rod_length_m, 0.0},
                     norm_squared),
            multiply(projection, projection));
    const auto root = dual_square_root(radicand);
    if (!root.has_value()) {
        return std::nullopt;
    }
    const Dual position = add(projection, *root);
    if (!std::isfinite(position.value) || !std::isfinite(position.derivative) ||
        position.value < 0.0) {
        return std::nullopt;
    }
    return position;
}

[[nodiscard]] std::optional<DualPoint>
slave_pin(const OneLevelMasterRodDriver &driver,
          const OneLevelMasterRodSlavePin &attachment,
          const DualPoint crank_journal) noexcept {
    const auto master_position =
        slider_axis_position(crank_journal, driver.master_bank_angle_rad,
                             driver.master_connecting_rod_length_m);
    if (!master_position.has_value()) {
        return std::nullopt;
    }
    const double master_axis_angle_rad = driver.master_bank_angle_rad + kLegacyPi / 2.0;
    const DualPoint master_wrist{
        scale(*master_position, std::cos(master_axis_angle_rad)),
        scale(*master_position, std::sin(master_axis_angle_rad)),
    };
    const DualPoint big_end_to_wrist{
        scale(subtract(master_wrist.x, crank_journal.x),
              1.0 / driver.master_connecting_rod_length_m),
        scale(subtract(master_wrist.y, crank_journal.y),
              1.0 / driver.master_connecting_rod_length_m),
    };
    const double cosine = std::cos(attachment.local_phase_rad);
    const double sine = std::sin(attachment.local_phase_rad);
    const DualPoint rotated{
        subtract(scale(big_end_to_wrist.x, cosine), scale(big_end_to_wrist.y, sine)),
        add(scale(big_end_to_wrist.x, sine), scale(big_end_to_wrist.y, cosine)),
    };
    return DualPoint{
        add(crank_journal.x, scale(rotated.x, attachment.throw_radius_m)),
        add(crank_journal.y, scale(rotated.y, attachment.throw_radius_m)),
    };
}

} // namespace

OneLevelMasterRodSample evaluate_one_level_master_rod(
    const OneLevelMasterRodDriver &driver, const OneLevelMasterRodCylinder &cylinder,
    const double body_angle_psi_rad, const double angular_speed_rad_s) noexcept {
    OneLevelMasterRodSample sample;
    if (!finite_driver(driver) || !finite_cylinder(cylinder) ||
        !std::isfinite(body_angle_psi_rad) || !std::isfinite(angular_speed_rad_s)) {
        return sample;
    }

    // Increasing engine cycle angle moves engine-sim's crank body angle in the
    // negative direction.
    const Dual psi{body_angle_psi_rad, -1.0};
    const DualPoint journal = crank_pin(driver, psi);
    DualPoint driven_point = journal;
    if (const auto *slave = std::get_if<OneLevelMasterRodSlavePin>(&cylinder.journal)) {
        const auto calculated = slave_pin(driver, *slave, journal);
        if (!calculated.has_value()) {
            return sample;
        }
        driven_point = *calculated;
    } else if (!same_binary64(cylinder.bank_angle_rad, driver.master_bank_angle_rad) ||
               !same_binary64(cylinder.connecting_rod_length_m,
                              driver.master_connecting_rod_length_m)) {
        return sample;
    }

    const auto position = slider_axis_position(driven_point, cylinder.bank_angle_rad,
                                               cylinder.connecting_rod_length_m);
    if (!position.has_value()) {
        return sample;
    }

    // Preserve pristine CombustionChamber::getVolume() written order.
    const double sweep_volume_m3 =
        cylinder.piston_area_m2 * (cylinder.deck_height_m - position->value -
                                   cylinder.piston_compression_height_m);
    sample.piston_axis_position_m = position->value;
    sample.piston_axis_derivative_m_per_rad = position->derivative;
    sample.chamber_volume_m3 = sweep_volume_m3 + cylinder.head_chamber_volume_m3 -
                               cylinder.piston_displacement_term_m3;
    sample.dvolume_dtheta_m3_per_rad = -cylinder.piston_area_m2 * position->derivative;
    sample.piston_speed_abs_m_s = std::abs(position->derivative * angular_speed_rad_s);
    if (!std::isfinite(sample.piston_axis_position_m) ||
        !std::isfinite(sample.piston_axis_derivative_m_per_rad) ||
        !finite_positive(sample.chamber_volume_m3) ||
        !std::isfinite(sample.dvolume_dtheta_m3_per_rad) ||
        !std::isfinite(sample.piston_speed_abs_m_s)) {
        return OneLevelMasterRodSample{};
    }
    sample.valid = true;
    return sample;
}

} // namespace engine_sim_offline::simulation
