#include "legacy_mechanics_primitives.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine_sim_offline::simulation {
namespace {

[[nodiscard]] std::size_t
closest_triangle_point(std::span<const LegacyTrianglePoint> points, double x) noexcept {
    std::size_t lower = 0;
    std::size_t upper = points.size() - 1U;

    if (x <= points[lower].x) {
        return lower;
    }
    if (x >= points[upper].x) {
        return upper;
    }

    while (lower + 1U < upper) {
        const std::size_t middle = (lower + upper) / 2U;
        if (x > points[middle].x) {
            lower = middle;
        } else if (x < points[middle].x) {
            upper = middle;
        } else {
            return middle;
        }
    }

    return (x - points[lower].x < points[upper].x - x) ? lower : upper;
}

[[nodiscard]] bool
finite_slider_crank_input(const CenteredSliderCrankCylinder &cylinder,
                          double theta_cycle_rad,
                          double angular_velocity_rad_s) noexcept {
    return cylinder.cylinder_id.valid() && std::isfinite(cylinder.geometric_tdc_rad) &&
           std::isfinite(cylinder.piston_area_m2) && cylinder.piston_area_m2 > 0.0 &&
           std::isfinite(cylinder.crank_radius_m) && cylinder.crank_radius_m > 0.0 &&
           std::isfinite(cylinder.connecting_rod_length_m) &&
           cylinder.connecting_rod_length_m > 0.0 &&
           std::isfinite(cylinder.clearance_volume_m3) &&
           std::isfinite(cylinder.ignition_wire_angle_rad) &&
           std::isfinite(theta_cycle_rad) && std::isfinite(angular_velocity_rad_s);
}

} // namespace

LegacyCylinderGeometry derive_legacy_cylinder_geometry(
    const double bore_m, const double crank_radius_m,
    const double connecting_rod_length_m, const double deck_height_m,
    const double piston_compression_height_m,
    const double head_chamber_volume_m3,
    const double piston_displacement_term_m3) noexcept {
    LegacyCylinderGeometry result;
    result.piston_area_m2 = kLegacyPi * bore_m * bore_m / 4.0;
    result.tdc_mechanism_height_m =
        crank_radius_m * std::cos(0.0) +
        std::sqrt(connecting_rod_length_m * connecting_rod_length_m);
    result.clearance_volume_m3 =
        head_chamber_volume_m3 - piston_displacement_term_m3 +
        result.piston_area_m2 *
            (deck_height_m - result.tdc_mechanism_height_m -
             piston_compression_height_m);
    result.fixed_geometry_volume_m3 =
        head_chamber_volume_m3 +
        result.piston_area_m2 *
            (deck_height_m - piston_compression_height_m);
    result.swept_volume_m3 =
        result.piston_area_m2 * (2.0 * crank_radius_m);
    result.compression_ratio =
        (result.clearance_volume_m3 + result.swept_volume_m3) /
        result.clearance_volume_m3;
    return result;
}

double legacy_positive_mod(double value, double modulus) noexcept {
    if (value < 0.0) {
        value = std::ceil(-value / modulus) * modulus + value;
    }

    return std::fmod(value, modulus);
}

double legacy_wrap_2pi(double value) noexcept {
    return legacy_positive_mod(value, 2.0 * kLegacyPi);
}

double legacy_wrap_4pi(double value) noexcept {
    return legacy_positive_mod(value, 4.0 * kLegacyPi);
}

LegacyDirectThrottleState evaluate_legacy_direct_throttle(
    const double requested_throttle_01, const double gamma,
    const double idle_throttle_plate_position_01) noexcept {
    // Keep the already accepted BMW gamma-2 path numerically identical while
    // allowing other authored engine-sim direct-linkage exponents.
    const double exponentiated =
        std::bit_cast<std::uint64_t>(gamma) ==
                std::bit_cast<std::uint64_t>(2.0)
            ? std::pow(requested_throttle_01, 2.0)
            : std::pow(requested_throttle_01, gamma);
    LegacyDirectThrottleState state;
    state.resolved_engine_throttle_01 = 1.0 - exponentiated;
    state.intake_plate_position_01 =
        idle_throttle_plate_position_01 *
        state.resolved_engine_throttle_01;
    state.main_flow_multiplier_01 =
        std::cos(kLegacyPi * state.intake_plate_position_01 / 2.0);
    return state;
}

double legacy_triangle_sample(std::span<const LegacyTrianglePoint> points, double x,
                              double radius) noexcept {
    if (points.empty()) {
        return 0.0;
    }
    if (x <= points.front().x) {
        return points.front().y;
    }
    if (x >= points.back().x) {
        return points.back().y;
    }

    const std::size_t closest = closest_triangle_point(points, x);
    double sum = 0.0;
    double total_weight = 0.0;

    for (std::size_t index = closest + 1U; index-- > 0U;) {
        const LegacyTrianglePoint &point = points[index];
        if (point.x > x) {
            continue;
        }
        if (std::abs(x - point.x) > radius) {
            break;
        }

        const double weight = (radius - std::abs(point.x - x)) / radius;
        sum += weight * point.y;
        total_weight += weight;
    }

    for (std::size_t index = closest; index < points.size(); ++index) {
        const LegacyTrianglePoint &point = points[index];
        if (point.x <= x) {
            continue;
        }
        if (std::abs(point.x - x) > radius) {
            break;
        }

        const double weight = (radius - std::abs(point.x - x)) / radius;
        sum += weight * point.y;
        total_weight += weight;
    }

    return total_weight != 0.0 ? sum / total_weight : 0.0;
}

CenteredSliderCrankSample
evaluate_centered_slider_crank(const CenteredSliderCrankCylinder &cylinder,
                               double theta_cycle_rad,
                               double angular_velocity_rad_s) noexcept {
    CenteredSliderCrankSample sample;
    if (!finite_slider_crank_input(cylinder, theta_cycle_rad, angular_velocity_rad_s)) {
        return sample;
    }

    sample.phase_rad = legacy_wrap_2pi(theta_cycle_rad - cylinder.geometric_tdc_rad);
    if (!std::isfinite(sample.phase_rad)) {
        return sample;
    }

    const double radicand =
        cylinder.connecting_rod_length_m * cylinder.connecting_rod_length_m -
        cylinder.crank_radius_m * cylinder.crank_radius_m * std::sin(sample.phase_rad) *
            std::sin(sample.phase_rad);
    if (!std::isfinite(radicand) || radicand <= 0.0) {
        return sample;
    }

    const double slider_position =
        cylinder.crank_radius_m * std::cos(sample.phase_rad) + std::sqrt(radicand);
    if (!std::isfinite(slider_position)) {
        return sample;
    }

    sample.piston_travel_m =
        cylinder.crank_radius_m + cylinder.connecting_rod_length_m - slider_position;
    if (!std::isfinite(sample.piston_travel_m)) {
        return sample;
    }

    sample.chamber_volume_m3 =
        cylinder.clearance_volume_m3 + cylinder.piston_area_m2 * sample.piston_travel_m;
    if (!std::isfinite(sample.chamber_volume_m3) || sample.chamber_volume_m3 <= 0.0) {
        return sample;
    }

    const double derivative_radicand =
        cylinder.connecting_rod_length_m * cylinder.connecting_rod_length_m -
        cylinder.crank_radius_m * cylinder.crank_radius_m * std::sin(sample.phase_rad) *
            std::sin(sample.phase_rad);
    if (!std::isfinite(derivative_radicand) || derivative_radicand <= 0.0) {
        return sample;
    }

    sample.dx_dtheta_m_per_rad =
        cylinder.crank_radius_m * std::sin(sample.phase_rad) +
        cylinder.crank_radius_m * cylinder.crank_radius_m * std::sin(sample.phase_rad) *
            std::cos(sample.phase_rad) / std::sqrt(derivative_radicand);
    if (!std::isfinite(sample.dx_dtheta_m_per_rad)) {
        return sample;
    }

    sample.dvolume_dtheta_m3_per_rad =
        cylinder.piston_area_m2 * sample.dx_dtheta_m_per_rad;
    sample.piston_speed_abs_m_s =
        std::abs(sample.dx_dtheta_m_per_rad * angular_velocity_rad_s);
    if (!std::isfinite(sample.dvolume_dtheta_m3_per_rad) ||
        !std::isfinite(sample.piston_speed_abs_m_s)) {
        return sample;
    }

    sample.valid = true;
    return sample;
}

} // namespace engine_sim_offline::simulation
