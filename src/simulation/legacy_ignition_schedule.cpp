#include "simulation/legacy_ignition_schedule.hpp"

#include "simulation/legacy_mechanics_primitives.hpp"

#include <cmath>

namespace engine_sim_offline::simulation {

LegacyIgnitionCrossingDecision
evaluate_legacy_ignition_crossing(double saved_angle_rad, double current_angle_rad,
                                  double spark_angle_rad,
                                  double omega_legacy_rad_s) noexcept {
    auto adjusted_current = current_angle_rad;
    auto adjusted_spark = spark_angle_rad;
    bool crossed = false;

    if (omega_legacy_rad_s < 0.0) {
        if (adjusted_current < saved_angle_rad) {
            adjusted_current += 4.0 * kLegacyPi;
            adjusted_spark += 4.0 * kLegacyPi;
        }
        crossed =
            adjusted_spark >= saved_angle_rad && adjusted_spark < adjusted_current;
    } else {
        if (adjusted_current > saved_angle_rad) {
            adjusted_current -= 4.0 * kLegacyPi;
            adjusted_spark -= 4.0 * kLegacyPi;
        }
        crossed =
            adjusted_spark >= adjusted_current && adjusted_spark < saved_angle_rad;
    }

    return {crossed, adjusted_current, adjusted_spark};
}

LegacyLimiterUpdate update_legacy_limiter(double timer_s, double step_s,
                                          double omega_legacy_rad_s,
                                          double limiter_speed_rpm,
                                          double limiter_hold_s) noexcept {
    LegacyLimiterUpdate result;
    result.old_active = timer_s > 0.0;

    timer_s -= step_s;
    const auto limiter_speed_rad_s = limiter_speed_rpm * kLegacyRpmScale;
    if (std::abs(omega_legacy_rad_s) > limiter_speed_rad_s) {
        timer_s = limiter_hold_s;
        result.overspeed_refreshed = true;
    }
    if (timer_s < 0.0) {
        timer_s = 0.0;
    }

    result.timer_s = timer_s;
    result.new_active = timer_s > 0.0;
    return result;
}

} // namespace engine_sim_offline::simulation
