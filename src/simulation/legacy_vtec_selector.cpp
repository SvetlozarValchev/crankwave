#include "simulation/legacy_vtec_selector.hpp"

#include <cmath>

namespace engine_sim_offline::simulation {

bool legacy_vtec_alternate_profile_active(
    const LegacyVtecSelectorThresholds &thresholds,
    const LegacyVtecSelectorInput &input) noexcept {
    return input.manifold_pressure_pa_abs >
               thresholds.minimum_manifold_pressure_pa_abs &&
           std::abs(input.engine_angular_speed_rad_s) >
               thresholds.minimum_engine_speed_rad_s &&
           input.throttle_linkage_opening_01 >
               thresholds.minimum_throttle_linkage_opening_01;
}

} // namespace engine_sim_offline::simulation
