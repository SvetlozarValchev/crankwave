#include "dsp/pcg32.hpp"
#include "dsp/causal_reconstruction_table.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace engine_sim_offline::dsp;

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

struct PcgVector {
    std::uint64_t initial_state;
    std::uint64_t stream;
    std::uint64_t seeded_state;
    std::uint64_t increment;
    std::array<std::uint32_t, 4> outputs;
};

void test_frozen_pcg32_raw_vectors() {
    constexpr std::array vectors{
        PcgVector{
            UINT64_C(0x75bc579d4c90a640),
            UINT64_C(0x7e4ef6200e7c70c1),
            UINT64_C(0xb89fac27ccfe7bca),
            UINT64_C(0xfc9dec401cf8e183),
            {UINT32_C(0xe0800827), UINT32_C(0xfc89fab2), UINT32_C(0xcd5dec73),
             UINT32_C(0x5303b744)},
        },
        PcgVector{
            UINT64_C(0x208e57f73615bd95),
            UINT64_C(0x786d92e584c43b78),
            UINT64_C(0xda0090ba0e642c7f),
            UINT64_C(0xf0db25cb098876f1),
            {UINT32_C(0x0298e8a8), UINT32_C(0x9de31d30), UINT32_C(0x0debcfb8),
             UINT32_C(0x2e547a87)},
        },
        PcgVector{
            UINT64_C(0x9e2b91cd0dc51cfc),
            UINT64_C(0x1ae6ee3019603abb),
            UINT64_C(0x4b403cc62ed140ae),
            UINT64_C(0x35cddc6032c07577),
            {UINT32_C(0x623402e1), UINT32_C(0xdd0b7d37), UINT32_C(0x5ee862d8),
             UINT32_C(0x69f404a3)},
        },
        PcgVector{
            UINT64_C(0xdb7540a0c8b54d74),
            UINT64_C(0x41ddcdeb066bf214),
            UINT64_C(0xdd09e32c1ea77fc2),
            UINT64_C(0x83bb9bd60cd7e429),
            {UINT32_C(0x2751b994), UINT32_C(0xdccceb62), UINT32_C(0xa06ee469),
             UINT32_C(0x983bda62)},
        },
    };

    for (const auto &vector : vectors) {
        Pcg32 generator{vector.initial_state, vector.stream};
        expect(generator.state() == vector.seeded_state,
               "PCG32 seeding state changed");
        expect(generator.increment() == vector.increment,
               "PCG32 odd increment changed");
        for (const auto expected : vector.outputs) {
            expect(generator.next_u32() == expected, "PCG32 raw output changed");
        }
    }
}

void test_frozen_pcg32_floating_draws() {
    Pcg32 jitter_0{UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)};
    expect(bits(jitter_0.uniform_double()) == UINT64_C(0x3fd88d00bee85be8) &&
               bits(jitter_0.uniform_double()) == UINT64_C(0x3fd7ba18b34fa024),
           "jitter uniform draw construction changed");

    Pcg32 jitter_1{UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)};
    expect(bits(jitter_1.uniform_double()) == UINT64_C(0x3fc3a8dccdccceb4) &&
               bits(jitter_1.uniform_double()) == UINT64_C(0x3fe40ddc8e60ef69),
           "second jitter uniform draw construction changed");

    Pcg32 air_0{UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)};
    expect(bits(air_0.uniform_signed_double()) == UINT64_C(0x3fe820020fe44fd4) &&
               bits(air_0.uniform_signed_double()) == UINT64_C(0x3fe3577b1a981dba),
           "air signed-uniform draw construction changed");

    Pcg32 air_1{UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)};
    expect(bits(air_1.uniform_signed_double()) == UINT64_C(0xbfef59c5d310e718) &&
               bits(air_1.uniform_signed_double()) == UINT64_C(0xbfec850c168d5c2c),
           "second air signed-uniform draw construction changed");
}

void test_pcg32_stream_bounds() {
    const auto maximum_stream = std::numeric_limits<std::uint64_t>::max() >> 1U;
    Pcg32 maximum{0, maximum_stream};
    expect(maximum.increment() == std::numeric_limits<std::uint64_t>::max(),
           "maximum valid PCG32 stream encoded incorrectly");
    expect_throw<std::invalid_argument>(
        [] {
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max() >> 1U;
            Pcg32 invalid{0, maximum + UINT64_C(1)};
        },
        "oversized PCG32 stream was accepted");
}

void test_frozen_reconstruction_coefficients() {
    const CausalReconstructionTable table;

    expect(bits(table.coefficient(0, 0)) == UINT64_C(0x8000000000000000) &&
               bits(table.coefficient(0, 1)) == UINT64_C(0x3e8a00a0e39b2e8b) &&
               bits(table.coefficient(0, 128)) == UINT64_C(0x3fee6666161e74a3) &&
               bits(table.coefficient(0, 129)) == UINT64_C(0x3fa97c64c9074291) &&
               bits(table.coefficient(0, 256)) == UINT64_C(0x8000000000000000),
           "reconstruction phase-zero coefficients changed");

    expect(bits(table.coefficient(2048, 1)) == UINT64_C(0xbe76401008a444a8) &&
               bits(table.coefficient(2048, 128)) == UINT64_C(0x3fe44f91724f0ecf) &&
               bits(table.coefficient(2048, 129)) == UINT64_C(0x3fe44dbed63be0f7) &&
               bits(table.coefficient(2048, 255)) == UINT64_C(0x3e7e9e9fda5dd463),
           "reconstruction half-phase coefficients changed");

    expect(bits(table.coefficient(4095, 1)) == UINT64_C(0xbe8b8de6a7d71d29) &&
               bits(table.coefficient(4095, 128)) == UINT64_C(0x3fa9a09b83114fb0) &&
               bits(table.coefficient(4095, 129)) == UINT64_C(0x3fee6666313df595) &&
               bits(table.coefficient(4095, 255)) == UINT64_C(0xbe87caf56de76726),
           "reconstruction final ordinary phase changed");
}

void test_exact_reconstruction_wrap_row() {
    const CausalReconstructionTable table;
    const auto phase_zero = table.phase_row(0);
    const auto wrap = table.phase_row(CausalReconstructionTable::phase_interval_count);
    expect(bits(wrap[0]) == UINT64_C(0),
           "reconstruction wrap row did not begin with positive zero");
    for (std::size_t tap = 1; tap < CausalReconstructionTable::tap_count; ++tap) {
        expect(bits(wrap[tap]) == bits(phase_zero[tap - 1]),
               "reconstruction wrap row was not an exact shifted copy");
    }
}

void test_reconstruction_bounds_fail_closed() {
    const CausalReconstructionTable table;
    expect_throw<std::out_of_range>(
        [&] { static_cast<void>(table.phase_row(CausalReconstructionTable::row_count)); },
        "out-of-range reconstruction phase was accepted");
    expect_throw<std::out_of_range>(
        [&] {
            static_cast<void>(table.coefficient(0, CausalReconstructionTable::tap_count));
        },
        "out-of-range reconstruction tap was accepted");
}

void run_tests() {
    test_frozen_pcg32_raw_vectors();
    test_frozen_pcg32_floating_draws();
    test_pcg32_stream_bounds();
    test_frozen_reconstruction_coefficients();
    test_exact_reconstruction_wrap_row();
    test_reconstruction_bounds_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "random/table test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
