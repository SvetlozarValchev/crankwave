#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <span>

namespace engine_sim_offline::simulation {

inline constexpr double kLegacyPi = 3.14159265359;
inline constexpr double kLegacyRpmScale = 0.104719755;

[[nodiscard]] double legacy_positive_mod(double value, double modulus) noexcept;
[[nodiscard]] double legacy_wrap_2pi(double value) noexcept;
[[nodiscard]] double legacy_wrap_4pi(double value) noexcept;

struct LegacyTrianglePoint {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const LegacyTrianglePoint &,
                           const LegacyTrianglePoint &) = default;
};

// The points are nonempty and sorted by x, and radius is finite and positive.
// Those properties belong to profile admission rather than the hot sampler.
[[nodiscard]] double legacy_triangle_sample(std::span<const LegacyTrianglePoint> points,
                                            double x, double radius) noexcept;

struct CenteredSliderCrankCylinder {
    contract::CylinderId cylinder_id;
    double geometric_tdc_rad = 0.0;
    double piston_area_m2 = 0.0;
    double crank_radius_m = 0.0;
    double connecting_rod_length_m = 0.0;
    double clearance_volume_m3 = 0.0;
    double ignition_wire_angle_rad = 0.0;

    friend bool operator==(const CenteredSliderCrankCylinder &,
                           const CenteredSliderCrankCylinder &) = default;
};

struct CenteredSliderCrankSample {
    double phase_rad = 0.0;
    double piston_travel_m = 0.0;
    double chamber_volume_m3 = 0.0;
    double dx_dtheta_m_per_rad = 0.0;
    double dvolume_dtheta_m3_per_rad = 0.0;
    double piston_speed_abs_m_s = 0.0;
    bool valid = false;

    friend bool operator==(const CenteredSliderCrankSample &,
                           const CenteredSliderCrankSample &) = default;
};

[[nodiscard]] CenteredSliderCrankSample
evaluate_centered_slider_crank(const CenteredSliderCrankCylinder &cylinder,
                               double theta_cycle_rad,
                               double angular_velocity_rad_s) noexcept;

} // namespace engine_sim_offline::simulation
