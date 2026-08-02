#include "simulation/one_level_master_rod_kinematics.hpp"

#include "simulation/legacy_mechanics_primitives.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kBinary64CertificateGuard =
    128.0 * std::numeric_limits<double>::epsilon();
constexpr std::size_t kFullCyclePrimaryProbeCount = 4096U;
constexpr std::size_t kFullCycleConfirmationProbeCount = 8192U;
constexpr std::size_t kFullCycleStationaryRefinementSteps = 80U;

struct Dual {
    double value = 0.0;
    double derivative = 0.0;
};

struct DualPoint {
    Dual x;
    Dual y;
};

struct ResolvedStationaryPoint {
    double piston_axis_position_m = 0.0;
    double chamber_volume_m3 = 0.0;
};

enum class StationaryTransition {
    positive_to_negative,
    negative_to_positive,
};

struct StationaryBracket {
    StationaryTransition transition = StationaryTransition::positive_to_negative;
    double left_angle_rad = 0.0;
    double right_angle_rad = 0.0;
    bool exact_zero = false;
};

struct StationaryIsolation {
    OneLevelMasterRodFullCycleGeometryIssue issue =
        OneLevelMasterRodFullCycleGeometryIssue::stationary_point_isolation_ambiguous;
    std::array<StationaryBracket, 2> brackets{};
    std::size_t bracket_count = 0U;
    double sampled_minimum_position_m = std::numeric_limits<double>::infinity();
    double sampled_maximum_position_m = -std::numeric_limits<double>::infinity();
    bool valid = false;
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

[[nodiscard]] bool
finite_driver_values(const OneLevelMasterRodDriver &driver) noexcept {
    return finite_positive(driver.crank_radius_m) &&
           std::isfinite(driver.crank_journal_global_phase_rad) &&
           std::isfinite(driver.master_bank_angle_rad) &&
           finite_positive(driver.master_connecting_rod_length_m);
}

[[nodiscard]] bool finite_cylinder(const OneLevelMasterRodCylinder &cylinder) noexcept {
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

[[nodiscard]] bool opposite_nonzero_signs(const double left,
                                          const double right) noexcept {
    return left != 0.0 && right != 0.0 && std::signbit(left) != std::signbit(right);
}

[[nodiscard]] std::optional<ResolvedStationaryPoint>
refine_stationary_point(const OneLevelMasterRodDriver &driver,
                        const OneLevelMasterRodCylinder &cylinder,
                        double left_angle_rad, double right_angle_rad,
                        const double derivative_residual_guard_m) noexcept {
    auto left = evaluate_one_level_master_rod(driver, cylinder, left_angle_rad, 0.0);
    auto right = evaluate_one_level_master_rod(driver, cylinder, right_angle_rad, 0.0);
    if (!left.valid || !right.valid ||
        !opposite_nonzero_signs(left.piston_axis_derivative_m_per_rad,
                                right.piston_axis_derivative_m_per_rad)) {
        return std::nullopt;
    }

    for (std::size_t step = 0; step < kFullCycleStationaryRefinementSteps; ++step) {
        const double midpoint_angle_rad =
            left_angle_rad + (right_angle_rad - left_angle_rad) / 2.0;
        if (midpoint_angle_rad == left_angle_rad ||
            midpoint_angle_rad == right_angle_rad) {
            break;
        }
        const auto midpoint =
            evaluate_one_level_master_rod(driver, cylinder, midpoint_angle_rad, 0.0);
        if (!midpoint.valid) {
            return std::nullopt;
        }
        if (midpoint.piston_axis_derivative_m_per_rad == 0.0) {
            return ResolvedStationaryPoint{
                midpoint.piston_axis_position_m,
                midpoint.chamber_volume_m3,
            };
        }
        if (std::signbit(midpoint.piston_axis_derivative_m_per_rad) ==
            std::signbit(left.piston_axis_derivative_m_per_rad)) {
            left_angle_rad = midpoint_angle_rad;
            left = midpoint;
        } else {
            right_angle_rad = midpoint_angle_rad;
            right = midpoint;
        }
    }

    if (std::nextafter(left_angle_rad, right_angle_rad) != right_angle_rad ||
        !opposite_nonzero_signs(left.piston_axis_derivative_m_per_rad,
                                right.piston_axis_derivative_m_per_rad)) {
        return std::nullopt;
    }

    // The exact root need not be representable. Retain the adjacent endpoint with
    // the smaller analytic residual; an exact tie selects the lower body angle.
    const bool select_right = std::abs(right.piston_axis_derivative_m_per_rad) <
                              std::abs(left.piston_axis_derivative_m_per_rad);
    const auto &selected = select_right ? right : left;
    if (!std::isfinite(derivative_residual_guard_m) ||
        !(derivative_residual_guard_m > 0.0) ||
        std::abs(selected.piston_axis_derivative_m_per_rad) >
            derivative_residual_guard_m) {
        return std::nullopt;
    }
    return ResolvedStationaryPoint{
        selected.piston_axis_position_m,
        selected.chamber_volume_m3,
    };
}

[[nodiscard]] StationaryTransition classify_transition(const double left,
                                                       const double right) noexcept {
    return left > 0.0 && right < 0.0 ? StationaryTransition::positive_to_negative
                                     : StationaryTransition::negative_to_positive;
}

template <std::size_t ProbeCount>
[[nodiscard]] StationaryIsolation
isolate_slave_stationary_points(const OneLevelMasterRodDriver &driver,
                                const OneLevelMasterRodCylinder &cylinder,
                                const double derivative_ambiguity_guard_m) noexcept {
    StationaryIsolation isolation;
    constexpr double probe_count = static_cast<double>(ProbeCount);
    const double two_pi = 2.0 * std::numbers::pi_v<double>;
    const double probe_step_rad = two_pi / probe_count;
    std::array<double, ProbeCount> derivatives{};
    for (std::size_t index = 0; index < derivatives.size(); ++index) {
        const double angle_rad =
            (static_cast<double>(index) + 0.5) * two_pi / probe_count;
        const auto sample =
            evaluate_one_level_master_rod(driver, cylinder, angle_rad, 0.0);
        if (!sample.valid || !std::isfinite(sample.piston_axis_derivative_m_per_rad)) {
            isolation.issue =
                OneLevelMasterRodFullCycleGeometryIssue::invalid_evaluator_sample;
            return isolation;
        }
        derivatives[index] = sample.piston_axis_derivative_m_per_rad;
        isolation.sampled_minimum_position_m = std::min(
            isolation.sampled_minimum_position_m, sample.piston_axis_position_m);
        isolation.sampled_maximum_position_m = std::max(
            isolation.sampled_maximum_position_m, sample.piston_axis_position_m);
    }

    for (std::size_t index = 0; index < derivatives.size(); ++index) {
        const std::size_t previous_index =
            index == 0U ? derivatives.size() - 1U : index - 1U;
        const std::size_t next_index = (index + 1U) % derivatives.size();
        const double derivative = derivatives[index];
        if (derivative == 0.0) {
            if (!opposite_nonzero_signs(derivatives[previous_index],
                                        derivatives[next_index])) {
                return isolation;
            }
            continue;
        }
        if (std::abs(derivative) <= derivative_ambiguity_guard_m &&
            !opposite_nonzero_signs(derivatives[previous_index], derivative) &&
            !opposite_nonzero_signs(derivative, derivatives[next_index])) {
            return isolation;
        }
    }

    const auto append_bracket = [&](const StationaryBracket &bracket) {
        if (isolation.bracket_count >= isolation.brackets.size()) {
            return false;
        }
        isolation.brackets[isolation.bracket_count++] = bracket;
        return true;
    };
    for (std::size_t index = 0; index < derivatives.size(); ++index) {
        const std::size_t previous_index =
            index == 0U ? derivatives.size() - 1U : index - 1U;
        const std::size_t next_index = (index + 1U) % derivatives.size();
        const double left_angle_rad =
            (static_cast<double>(index) + 0.5) * two_pi / probe_count;
        if (derivatives[index] == 0.0) {
            if (!append_bracket({classify_transition(derivatives[previous_index],
                                                     derivatives[next_index]),
                                 left_angle_rad, left_angle_rad, true})) {
                return isolation;
            }
            continue;
        }
        if (opposite_nonzero_signs(derivatives[index], derivatives[next_index]) &&
            !append_bracket(
                {classify_transition(derivatives[index], derivatives[next_index]),
                 left_angle_rad, left_angle_rad + probe_step_rad, false})) {
            return isolation;
        }
    }

    if (isolation.bracket_count != isolation.brackets.size() ||
        isolation.brackets[0].transition == isolation.brackets[1].transition) {
        isolation.issue = isolation.bracket_count < isolation.brackets.size()
                              ? OneLevelMasterRodFullCycleGeometryIssue::
                                    insufficient_stationary_points
                              : OneLevelMasterRodFullCycleGeometryIssue::
                                    stationary_point_isolation_ambiguous;
        return isolation;
    }
    isolation.valid = true;
    return isolation;
}

[[nodiscard]] double
stationary_bracket_center(const StationaryBracket &bracket) noexcept {
    return bracket.exact_zero
               ? bracket.left_angle_rad
               : bracket.left_angle_rad +
                     (bracket.right_angle_rad - bracket.left_angle_rad) / 2.0;
}

[[nodiscard]] double periodic_angle_distance(double left, double right,
                                             const double period) noexcept {
    left = std::fmod(left, period);
    right = std::fmod(right, period);
    const double distance = std::abs(left - right);
    return std::min(distance, period - distance);
}

[[nodiscard]] const StationaryBracket *
find_transition(const StationaryIsolation &isolation,
                const StationaryTransition transition) noexcept {
    const auto found = std::find_if(
        isolation.brackets.begin(), isolation.brackets.end(),
        [&](const auto &bracket) { return bracket.transition == transition; });
    return found == isolation.brackets.end() ? nullptr : &*found;
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
        cylinder.piston_area_m2 *
        (cylinder.deck_height_m - position->value -
         cylinder.piston_wrist_pin_position_m - cylinder.piston_compression_height_m);
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

OneLevelMasterRodFullCycleCheck certify_one_level_master_rod_full_cycle(
    const OneLevelMasterRodDriver &driver,
    const OneLevelMasterRodCylinder &cylinder) noexcept {
    constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();
    OneLevelMasterRodFullCycleCheck check{
        OneLevelMasterRodFullCycleReason::invalid_geometry,
        unavailable,
        unavailable,
    };
    if (!finite_driver_values(driver) || !finite_cylinder(cylinder)) {
        return check;
    }

    const auto *slave = std::get_if<OneLevelMasterRodSlavePin>(&cylinder.journal);
    if (slave == nullptr &&
        (!same_binary64(cylinder.bank_angle_rad, driver.master_bank_angle_rad) ||
         !same_binary64(cylinder.connecting_rod_length_m,
                        driver.master_connecting_rod_length_m))) {
        return check;
    }

    const double master_reach_margin_m =
        driver.master_connecting_rod_length_m - driver.crank_radius_m;
    double maximum_axis_position_m =
        driver.master_connecting_rod_length_m + driver.crank_radius_m;
    check.forward_reach_margin_m = master_reach_margin_m;
    if (slave != nullptr) {
        const double driven_point_radius_bound_m =
            driver.crank_radius_m + slave->throw_radius_m;
        const double slave_reach_margin_m =
            cylinder.connecting_rod_length_m - driven_point_radius_bound_m;
        check.forward_reach_margin_m =
            std::min(master_reach_margin_m, slave_reach_margin_m);
        maximum_axis_position_m =
            cylinder.connecting_rod_length_m + driven_point_radius_bound_m;
    }
    if (!std::isfinite(check.forward_reach_margin_m) ||
        !std::isfinite(maximum_axis_position_m)) {
        check.reason = OneLevelMasterRodFullCycleReason::invalid_geometry;
        check.forward_reach_margin_m = unavailable;
        return check;
    }
    const double reach_scale_m =
        slave == nullptr
            ? std::max(driver.master_connecting_rod_length_m, driver.crank_radius_m)
            : std::max({driver.master_connecting_rod_length_m, driver.crank_radius_m,
                        cylinder.connecting_rod_length_m, slave->throw_radius_m});
    const double numerical_reach_guard_m = kBinary64CertificateGuard * reach_scale_m;
    if (!(check.forward_reach_margin_m > numerical_reach_guard_m)) {
        check.reason = OneLevelMasterRodFullCycleReason::reachability_not_certified;
        return check;
    }

    // Preserve pristine CombustionChamber::getVolume() written order. For a
    // direct root, max(s) = Lm + r exactly. For a slave, |P| <= r + t gives the
    // sufficient bound max(s) <= Ls + r + t.
    const double sweep_volume_lower_bound_m3 =
        cylinder.piston_area_m2 *
        (cylinder.deck_height_m - maximum_axis_position_m -
         cylinder.piston_wrist_pin_position_m - cylinder.piston_compression_height_m);
    check.minimum_chamber_volume_m3 = sweep_volume_lower_bound_m3 +
                                      cylinder.head_chamber_volume_m3 -
                                      cylinder.piston_displacement_term_m3;
    if (!std::isfinite(check.minimum_chamber_volume_m3)) {
        check.reason = OneLevelMasterRodFullCycleReason::invalid_geometry;
        check.forward_reach_margin_m = unavailable;
        check.minimum_chamber_volume_m3 = unavailable;
        return check;
    }
    const double volume_scale_m3 =
        cylinder.piston_area_m2 *
            (std::abs(cylinder.deck_height_m) + std::abs(maximum_axis_position_m) +
             std::abs(cylinder.piston_wrist_pin_position_m) +
             std::abs(cylinder.piston_compression_height_m)) +
        std::abs(cylinder.head_chamber_volume_m3) +
        std::abs(cylinder.piston_displacement_term_m3);
    const double numerical_volume_guard_m3 =
        kBinary64CertificateGuard * volume_scale_m3;
    if (!std::isfinite(numerical_volume_guard_m3)) {
        check.reason = OneLevelMasterRodFullCycleReason::invalid_geometry;
        check.forward_reach_margin_m = unavailable;
        check.minimum_chamber_volume_m3 = unavailable;
        return check;
    }
    if (!(check.minimum_chamber_volume_m3 > numerical_volume_guard_m3)) {
        check.reason = OneLevelMasterRodFullCycleReason::chamber_volume_not_certified;
        return check;
    }

    check.reason = OneLevelMasterRodFullCycleReason::admitted;
    return check;
}

OneLevelMasterRodFullCycleGeometryCalculation
calculate_one_level_master_rod_full_cycle_geometry(
    const OneLevelMasterRodDriver &driver,
    const OneLevelMasterRodCylinder &cylinder) noexcept {
    const auto certificate = certify_one_level_master_rod_full_cycle(driver, cylinder);
    if (!certificate.admitted()) {
        return OneLevelMasterRodFullCycleGeometryIssue::full_cycle_not_certified;
    }

    if (std::holds_alternative<OneLevelMasterRodRootJournal>(cylinder.journal)) {
        // A direct root has exact dead-center extrema. Keep this path analytic so
        // its geometry never inherits the slave stationary-point lattice.
        const double minimum_position_m =
            driver.master_connecting_rod_length_m - driver.crank_radius_m;
        const double maximum_position_m =
            driver.master_connecting_rod_length_m + driver.crank_radius_m;
        const double swept_stroke_m = maximum_position_m - minimum_position_m;

        // Preserve pristine CombustionChamber::getVolume() written order at both
        // exact dead centers.
        const double minimum_position_sweep_volume_m3 =
            cylinder.piston_area_m2 * (cylinder.deck_height_m - minimum_position_m -
                                       cylinder.piston_wrist_pin_position_m -
                                       cylinder.piston_compression_height_m);
        const double maximum_chamber_volume_m3 = minimum_position_sweep_volume_m3 +
                                                 cylinder.head_chamber_volume_m3 -
                                                 cylinder.piston_displacement_term_m3;
        const double maximum_position_sweep_volume_m3 =
            cylinder.piston_area_m2 * (cylinder.deck_height_m - maximum_position_m -
                                       cylinder.piston_wrist_pin_position_m -
                                       cylinder.piston_compression_height_m);
        const double minimum_chamber_volume_m3 = maximum_position_sweep_volume_m3 +
                                                 cylinder.head_chamber_volume_m3 -
                                                 cylinder.piston_displacement_term_m3;
        const double swept_displacement_m3 = cylinder.piston_area_m2 * swept_stroke_m;
        const double piston_axis_path_length_m_per_crank_revolution =
            swept_stroke_m + swept_stroke_m;

        if (!finite_positive(minimum_position_m) ||
            !finite_positive(maximum_position_m) || !finite_positive(swept_stroke_m) ||
            !finite_positive(minimum_chamber_volume_m3) ||
            !finite_positive(maximum_chamber_volume_m3) ||
            !finite_positive(swept_displacement_m3) ||
            !finite_positive(piston_axis_path_length_m_per_crank_revolution) ||
            minimum_position_m >= maximum_position_m ||
            minimum_chamber_volume_m3 > maximum_chamber_volume_m3) {
            return OneLevelMasterRodFullCycleGeometryIssue::nonfinite_derived_geometry;
        }
        return OneLevelMasterRodFullCycleGeometry{
            2U,
            minimum_position_m,
            maximum_position_m,
            swept_stroke_m,
            minimum_chamber_volume_m3,
            maximum_chamber_volume_m3,
            swept_displacement_m3,
            piston_axis_path_length_m_per_crank_revolution,
        };
    }

    const auto *slave = std::get_if<OneLevelMasterRodSlavePin>(&cylinder.journal);
    if (slave == nullptr) {
        return OneLevelMasterRodFullCycleGeometryIssue::full_cycle_not_certified;
    }

    const double derivative_scale_m =
        std::max({driver.crank_radius_m, driver.master_connecting_rod_length_m,
                  cylinder.connecting_rod_length_m, slave->throw_radius_m});
    const double derivative_ambiguity_guard_m =
        kBinary64CertificateGuard * derivative_scale_m;
    if (!std::isfinite(derivative_ambiguity_guard_m) ||
        !(derivative_ambiguity_guard_m > 0.0)) {
        return OneLevelMasterRodFullCycleGeometryIssue::nonfinite_derived_geometry;
    }

    const auto primary = isolate_slave_stationary_points<kFullCyclePrimaryProbeCount>(
        driver, cylinder, derivative_ambiguity_guard_m);
    if (!primary.valid) {
        return primary.issue;
    }
    const auto confirmation =
        isolate_slave_stationary_points<kFullCycleConfirmationProbeCount>(
            driver, cylinder, derivative_ambiguity_guard_m);
    if (!confirmation.valid) {
        return confirmation.issue;
    }

    const double two_pi = 2.0 * std::numbers::pi_v<double>;
    constexpr std::array transitions{
        StationaryTransition::positive_to_negative,
        StationaryTransition::negative_to_positive,
    };
    std::array<ResolvedStationaryPoint, 2> stationary_points{};
    for (std::size_t index = 0; index < transitions.size(); ++index) {
        const auto *primary_bracket = find_transition(primary, transitions[index]);
        const auto *confirmation_bracket =
            find_transition(confirmation, transitions[index]);
        if (primary_bracket == nullptr || confirmation_bracket == nullptr ||
            periodic_angle_distance(stationary_bracket_center(*primary_bracket),
                                    stationary_bracket_center(*confirmation_bracket),
                                    two_pi) >
                two_pi / static_cast<double>(kFullCyclePrimaryProbeCount)) {
            return OneLevelMasterRodFullCycleGeometryIssue::
                stationary_point_isolation_ambiguous;
        }

        if (confirmation_bracket->exact_zero) {
            const auto sample = evaluate_one_level_master_rod(
                driver, cylinder, confirmation_bracket->left_angle_rad, 0.0);
            if (!sample.valid || sample.piston_axis_derivative_m_per_rad != 0.0) {
                return OneLevelMasterRodFullCycleGeometryIssue::
                    stationary_point_isolation_ambiguous;
            }
            stationary_points[index] = {
                sample.piston_axis_position_m,
                sample.chamber_volume_m3,
            };
        } else {
            const auto stationary = refine_stationary_point(
                driver, cylinder, confirmation_bracket->left_angle_rad,
                confirmation_bracket->right_angle_rad, derivative_ambiguity_guard_m);
            if (!stationary.has_value()) {
                return OneLevelMasterRodFullCycleGeometryIssue::
                    stationary_point_isolation_ambiguous;
            }
            stationary_points[index] = *stationary;
        }
    }

    const double minimum_position_m =
        std::min(stationary_points[0].piston_axis_position_m,
                 stationary_points[1].piston_axis_position_m);
    const double maximum_position_m =
        std::max(stationary_points[0].piston_axis_position_m,
                 stationary_points[1].piston_axis_position_m);
    const double minimum_chamber_volume_m3 = std::min(
        stationary_points[0].chamber_volume_m3, stationary_points[1].chamber_volume_m3);
    const double maximum_chamber_volume_m3 = std::max(
        stationary_points[0].chamber_volume_m3, stationary_points[1].chamber_volume_m3);
    const double position_containment_guard_m =
        kBinary64CertificateGuard * derivative_scale_m;
    if (primary.sampled_minimum_position_m <
            minimum_position_m - position_containment_guard_m ||
        primary.sampled_maximum_position_m >
            maximum_position_m + position_containment_guard_m ||
        confirmation.sampled_minimum_position_m <
            minimum_position_m - position_containment_guard_m ||
        confirmation.sampled_maximum_position_m >
            maximum_position_m + position_containment_guard_m) {
        return OneLevelMasterRodFullCycleGeometryIssue::
            stationary_point_isolation_ambiguous;
    }

    const double swept_stroke_m = maximum_position_m - minimum_position_m;
    const double swept_displacement_m3 = cylinder.piston_area_m2 * swept_stroke_m;
    const double piston_axis_path_length_m_per_crank_revolution =
        swept_stroke_m + swept_stroke_m;
    if (!finite_positive(minimum_position_m) || !finite_positive(maximum_position_m) ||
        !finite_positive(swept_stroke_m) ||
        !finite_positive(minimum_chamber_volume_m3) ||
        !finite_positive(maximum_chamber_volume_m3) ||
        !finite_positive(swept_displacement_m3) ||
        !finite_positive(piston_axis_path_length_m_per_crank_revolution) ||
        minimum_position_m >= maximum_position_m ||
        minimum_chamber_volume_m3 > maximum_chamber_volume_m3) {
        return OneLevelMasterRodFullCycleGeometryIssue::nonfinite_derived_geometry;
    }

    return OneLevelMasterRodFullCycleGeometry{
        2U,
        minimum_position_m,
        maximum_position_m,
        swept_stroke_m,
        minimum_chamber_volume_m3,
        maximum_chamber_volume_m3,
        swept_displacement_m3,
        piston_axis_path_length_m_per_crank_revolution,
    };
}

} // namespace engine_sim_offline::simulation
