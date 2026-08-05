#include "presentation/master_dynamics.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using engine_sim_offline::presentation::MasterDynamics;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

void test_continuous_canonical_state() {
    MasterDynamics dynamics{1.0};
    float expected_peak = MasterDynamics::kInitialPeakSourceUnits;
    float expected_gain = MasterDynamics::kInitialGainLinear;
    for (int frame = 0; frame < 16; ++frame) {
        expect(dynamics.process(0.0F) == 0.0F,
               "silent master input produced non-silence");
        expected_peak = dynamics.peak_retention_per_frame() * expected_peak;
        const float requested_gain =
            std::clamp(MasterDynamics::kTargetPeakSourceUnits / expected_peak,
                       MasterDynamics::kMinimumGainLinear,
                       MasterDynamics::kMaximumGainLinear);
        expected_gain = dynamics.gain_retention_per_frame() * expected_gain +
                        std::bit_cast<float>(UINT32_C(0x39f10800)) *
                            requested_gain;
        expect(bits(dynamics.peak_source_units()) == bits(expected_peak) &&
                   bits(dynamics.gain_linear()) == bits(expected_gain),
               "master state did not remain continuous across samples");
    }

    expect(bits(dynamics.peak_retention_per_frame()) == UINT32_C(0x3f7ffef2) &&
               bits(dynamics.gain_retention_per_frame()) == UINT32_C(0x3f7fe1df),
           "canonical 192 kHz retention coefficients changed");
    expect(dynamics.peak_source_units() < MasterDynamics::kInitialPeakSourceUnits &&
               dynamics.gain_linear() < MasterDynamics::kInitialGainLinear,
           "master dynamics did not retain state across successive samples");
}

void test_volume_is_after_leveling_and_output_is_normalized() {
    MasterDynamics full_volume{1.0};
    MasterDynamics half_volume{0.5};

    const float full = full_volume.process(24'000.0F);
    const float half = half_volume.process(24'000.0F);
    expect(bits(full_volume.peak_source_units()) ==
                   bits(half_volume.peak_source_units()) &&
               bits(full_volume.gain_linear()) == bits(half_volume.gain_linear()),
           "volume was allowed to alter leveler state");
    expect(half > 0.0F && half < full && full < 1.0F,
           "soft-clipped master output was not normalized");

    MasterDynamics positive_extreme{std::numeric_limits<float>::max()};
    MasterDynamics negative_extreme{std::numeric_limits<float>::max()};
    const float normalized_bound = std::nextafter(1.0F, 0.0F);
    expect(positive_extreme.process(1.0F) == normalized_bound &&
               negative_extreme.process(-1.0F) == -normalized_bound,
           "soft limiting reached a hard-saturation endpoint");
}

void test_invalid_configuration_and_input_are_rejected() {
    expect_throw<std::invalid_argument>([] { MasterDynamics rejected{0.0}; },
                                        "zero master volume was accepted");

    MasterDynamics dynamics{1.0};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                dynamics.process(std::numeric_limits<float>::quiet_NaN()));
        },
        "non-finite master input was accepted");
}

} // namespace

int main() {
    try {
        test_continuous_canonical_state();
        test_volume_is_after_leveling_and_output_is_normalized();
        test_invalid_configuration_and_input_are_rejected();
    } catch (const std::exception &error) {
        std::cerr << "master dynamics test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
