#include "presentation/p18_conditioning.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace engine_sim_offline::presentation;

constexpr P18Pcg32Seed kRoute0Jitter{
    UINT64_C(0x9e2b91cd0dc51cfc),
    UINT64_C(0x1ae6ee3019603abb),
};
constexpr P18Pcg32Seed kRoute0Air{
    UINT64_C(0x75bc579d4c90a640),
    UINT64_C(0x7e4ef6200e7c70c1),
};
constexpr P18Pcg32Seed kRoute1Jitter{
    UINT64_C(0xdb7540a0c8b54d74),
    UINT64_C(0x41ddcdeb066bf214),
};
constexpr P18Pcg32Seed kRoute1Air{
    UINT64_C(0x208e57f73615bd95),
    UINT64_C(0x786d92e584c43b78),
};

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

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

double synthetic_input(std::size_t frame) {
    const auto centered = static_cast<int>(frame % 7) - 3;
    return static_cast<double>(centered) * 0.125;
}

struct ConditioningProbe {
    std::size_t frame;
    std::uint64_t jittered;
    std::uint64_t filtered_air;
    std::uint64_t conditioned;
};

void expect_probe(const P18ConditioningResult &actual,
                  const ConditioningProbe &expected, const char *message) {
    expect(bits(actual.jittered_engine_sim_source_unit) == expected.jittered &&
               bits(actual.filtered_air_noise) == expected.filtered_air &&
               bits(actual.conditioned_engine_sim_source_unit) == expected.conditioned,
           message);
}

void expect_same_result_bits(const P18ConditioningResult &actual,
                             const P18ConditioningResult &expected,
                             const char *message) {
    expect(bits(actual.jittered_engine_sim_source_unit) ==
                   bits(expected.jittered_engine_sim_source_unit) &&
               bits(actual.filtered_air_noise) == bits(expected.filtered_air_noise) &&
               bits(actual.conditioned_engine_sim_source_unit) ==
                   bits(expected.conditioned_engine_sim_source_unit),
           message);
}

void test_route_conditioning_goldens_and_rng_consumption() {
    constexpr std::array route_0_probes{
        ConditioningProbe{30, UINT64_C(0xbfc326b97e9b83a8),
                          UINT64_C(0x3fb298c27b8300f8), UINT64_C(0xc071f479ef2018a7)},
        ConditioningProbe{31, UINT64_C(0xbfd62e976b399584),
                          UINT64_C(0x3fb4bd45284796f6), UINT64_C(0xc077a37fc7f79722)},
        ConditioningProbe{40, UINT64_C(0x3f825b4a5c180880),
                          UINT64_C(0x3fc6ba7eef92867e), UINT64_C(0x407f2cc63c1714ce)},
        ConditioningProbe{127, UINT64_C(0x3fc7bac695cdee58),
                          UINT64_C(0xbfd2bb02eca84209), UINT64_C(0x40734f6723b28365)},
    };
    constexpr std::array route_1_probes{
        ConditioningProbe{30, UINT64_C(0xbfcac49f8ec94600),
                          UINT64_C(0xbfbd59017396a230), UINT64_C(0x405abe7a71db24e4)},
        ConditioningProbe{40, UINT64_C(0xbfca2ed46c080800),
                          UINT64_C(0xbfc21d48b08d3626), UINT64_C(0x406cabc0f4850ad3)},
        ConditioningProbe{127, UINT64_C(0xbfd091071557cd40),
                          UINT64_C(0xbfab23a72898c76b), UINT64_C(0x40652e1c6ae78de3)},
    };

    P18RouteConditioner route_0{kRoute0Jitter, kRoute0Air};
    P18RouteConditioner route_1{kRoute1Jitter, kRoute1Air};
    std::size_t route_0_probe = 0;
    std::size_t route_1_probe = 0;
    for (std::size_t frame = 0; frame < 3840; ++frame) {
        const auto input = synthetic_input(frame);
        const auto result_0 = route_0.process(input);
        const auto result_1 = route_1.process(input);
        if (frame < 30) {
            expect(bits(result_0.conditioned_engine_sim_source_unit) == UINT64_C(0),
                   "route 0 P1.8 conditioning warm-up ceased being positive zero");
        }
        if (route_0_probe < route_0_probes.size() &&
            route_0_probes[route_0_probe].frame == frame) {
            expect_probe(result_0, route_0_probes[route_0_probe],
                         "route 0 P1.8 conditioning golden changed");
            ++route_0_probe;
        }
        if (route_1_probe < route_1_probes.size() &&
            route_1_probes[route_1_probe].frame == frame) {
            expect_probe(result_1, route_1_probes[route_1_probe],
                         "route 1 P1.8 conditioning golden changed");
            ++route_1_probe;
        }
    }

    expect(route_0_probe == route_0_probes.size() &&
               route_1_probe == route_1_probes.size(),
           "P1.8 conditioning test did not visit every golden probe");
    expect(route_0.jitter_rng_state() == UINT64_C(0x38c474f6f27476ae) &&
               route_0.air_noise_rng_state() == UINT64_C(0x6410d400ea8049ca) &&
               route_1.jitter_rng_state() == UINT64_C(0x90c28d44851509c2) &&
               route_1.air_noise_rng_state() == UINT64_C(0x2a5d082701ba5e7f),
           "P1.8 conditioning did not consume one jitter and air draw per frame");
}

void test_nonfinite_input_is_rejected_before_mutation() {
    P18RouteConditioner candidate{kRoute0Jitter, kRoute0Air};
    const auto initial_jitter_state = candidate.jitter_rng_state();
    const auto initial_air_state = candidate.air_noise_rng_state();
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                candidate.process(std::numeric_limits<double>::quiet_NaN()));
        },
        "P1.8 conditioner accepted a non-finite input");
    expect(candidate.jitter_rng_state() == initial_jitter_state &&
               candidate.air_noise_rng_state() == initial_air_state,
           "P1.8 conditioner advanced RNG state for a rejected input");

    P18RouteConditioner fresh{kRoute0Jitter, kRoute0Air};
    for (std::size_t frame = 0; frame < 128; ++frame) {
        const auto input = synthetic_input(frame);
        const auto candidate_result = candidate.process(input);
        const auto fresh_result = fresh.process(input);
        expect_same_result_bits(candidate_result, fresh_result,
                                "rejected P1.8 conditioning input changed later state");
    }
}

void test_interleaved_sessions_remain_independent() {
    P18RouteConditioner contiguous{kRoute1Jitter, kRoute1Air};
    P18RouteConditioner interleaved{kRoute1Jitter, kRoute1Air};
    P18RouteConditioner unrelated{kRoute0Jitter, kRoute0Air};

    std::array<P18ConditioningResult, 128> expected{};
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
        expected[frame] = contiguous.process(synthetic_input(frame));
    }
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
        static_cast<void>(unrelated.process(-synthetic_input(frame)));
        expect_same_result_bits(
            interleaved.process(synthetic_input(frame)), expected[frame],
            "interleaving another session changed P1.8 route state");
    }
}

void run_tests() {
    test_route_conditioning_goldens_and_rng_consumption();
    test_nonfinite_input_is_rejected_before_mutation();
    test_interleaved_sessions_remain_independent();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "P1.8 conditioning test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
