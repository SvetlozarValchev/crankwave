#pragma once

namespace crankwave::simulation {

struct LegacyVtecSelectorThresholds {
    double minimum_engine_speed_rad_s = 0.0;
    double minimum_manifold_pressure_pa_abs = 0.0;
    double minimum_throttle_linkage_opening_01 = 0.0;

    friend bool operator==(const LegacyVtecSelectorThresholds &,
                           const LegacyVtecSelectorThresholds &) = default;
};

struct LegacyVtecSelectorInput {
    double engine_angular_speed_rad_s = 0.0;
    double manifold_pressure_pa_abs = 0.0;
    double throttle_linkage_opening_01 = 0.0;

    friend bool operator==(const LegacyVtecSelectorInput &,
                           const LegacyVtecSelectorInput &) = default;
};

// Admission owns finite/range validation. This pure predicate intentionally has no
// hysteresis or retained state and preserves pristine's three strict comparisons.
[[nodiscard]] bool
legacy_vtec_alternate_profile_active(const LegacyVtecSelectorThresholds &thresholds,
                                     const LegacyVtecSelectorInput &input) noexcept;

} // namespace crankwave::simulation
