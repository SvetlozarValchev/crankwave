#include "simulation/legacy_vtec_selector.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace engine_sim_offline::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

constexpr LegacyVtecSelectorThresholds kThresholds{
    600.0,
    84'000.0,
    0.3,
};

constexpr LegacyVtecSelectorInput kActiveInput{
    650.0,
    90'000.0,
    0.8,
};

void test_all_three_strict_thresholds_are_required() {
    expect(legacy_vtec_alternate_profile_active(kThresholds, kActiveInput),
           "three inputs above their thresholds did not select the alternate cams");

    auto input = kActiveInput;
    input.manifold_pressure_pa_abs = kThresholds.minimum_manifold_pressure_pa_abs;
    expect(!legacy_vtec_alternate_profile_active(kThresholds, input),
           "manifold-pressure equality did not retain the base cams");

    input = kActiveInput;
    input.engine_angular_speed_rad_s = kThresholds.minimum_engine_speed_rad_s;
    expect(!legacy_vtec_alternate_profile_active(kThresholds, input),
           "engine-speed equality did not retain the base cams");

    input = kActiveInput;
    input.throttle_linkage_opening_01 = kThresholds.minimum_throttle_linkage_opening_01;
    expect(!legacy_vtec_alternate_profile_active(kThresholds, input),
           "effective-throttle equality did not retain the base cams");
}

void test_engine_speed_uses_absolute_magnitude() {
    auto input = kActiveInput;
    input.engine_angular_speed_rad_s = -kActiveInput.engine_angular_speed_rad_s;
    expect(legacy_vtec_alternate_profile_active(kThresholds, input),
           "negative engine speed with sufficient magnitude did not select the "
           "alternate cams");

    input.engine_angular_speed_rad_s = -kThresholds.minimum_engine_speed_rad_s;
    expect(!legacy_vtec_alternate_profile_active(kThresholds, input),
           "negative engine-speed equality did not retain the base cams");
}

void test_selector_has_no_latched_state() {
    expect(legacy_vtec_alternate_profile_active(kThresholds, kActiveInput),
           "initial active selector evaluation failed");

    auto inactive = kActiveInput;
    inactive.throttle_linkage_opening_01 =
        std::nextafter(kThresholds.minimum_throttle_linkage_opening_01, 0.0);
    expect(!legacy_vtec_alternate_profile_active(kThresholds, inactive),
           "selector retained an active result after an input fell below threshold");

    expect(legacy_vtec_alternate_profile_active(kThresholds, kActiveInput),
           "selector retained an inactive result after all inputs recovered");
}

} // namespace

int main() {
    try {
        test_all_three_strict_thresholds_are_required();
        test_engine_speed_uses_absolute_magnitude();
        test_selector_has_no_latched_state();
    } catch (const std::exception &error) {
        std::cerr << "Legacy VTEC selector test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
