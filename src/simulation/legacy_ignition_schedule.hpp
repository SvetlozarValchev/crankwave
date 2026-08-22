#pragma once

namespace crankwave::simulation {

// Exact source-parity crossing result. The adjusted values are retained because they
// are normative event evidence, not merely temporaries used to decide `crossed`.
struct LegacyIgnitionCrossingDecision {
    bool crossed = false;
    double adjusted_current_angle_rad = 0.0;
    double adjusted_spark_angle_rad = 0.0;

    friend bool operator==(const LegacyIgnitionCrossingDecision &,
                           const LegacyIgnitionCrossingDecision &) = default;
};

[[nodiscard]] LegacyIgnitionCrossingDecision
evaluate_legacy_ignition_crossing(double saved_angle_rad, double current_angle_rad,
                                  double spark_angle_rad,
                                  double omega_legacy_rad_s) noexcept;

struct LegacyLimiterUpdate {
    double timer_s = 0.0;
    bool old_active = false;
    bool new_active = false;
    bool overspeed_refreshed = false;

    friend bool operator==(const LegacyLimiterUpdate &,
                           const LegacyLimiterUpdate &) = default;
};

// Performs only the post-crossing limiter update. Whether crossings are tested is
// owned by the caller because ignition enable and the old exact-zero timer state are
// evaluated before this operation.
[[nodiscard]] LegacyLimiterUpdate update_legacy_limiter(double timer_s, double step_s,
                                                        double omega_legacy_rad_s,
                                                        double limiter_speed_rpm,
                                                        double limiter_hold_s) noexcept;

} // namespace crankwave::simulation
