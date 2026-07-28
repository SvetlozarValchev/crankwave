#include "dsp/source_conditioning_primitives.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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

std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

void test_frozen_low_pass_coefficients() {
    const FourthOrderLowPass jitter{10000.0, kConditionedSourceRateHz};
    const auto &j = jitter.coefficients();
    expect(bits(j.a0) == UINT64_C(0x3ff8978a6f333f13) &&
               bits(j.a1) == UINT64_C(0xc0092c4db522bd0e) &&
               bits(j.a2) == UINT64_C(0x400e48bc73169953) &&
               bits(j.a3) == UINT64_C(0xc0006f859c71d349) &&
               bits(j.a4) == UINT64_C(0x3fdb17708abd3f6a) &&
               bits(j.numerator_scale) == UINT64_C(0x3f3fae65b361cfd8),
           "10 kHz low-pass coefficients diverged from the frozen record");

    const FourthOrderLowPass air{2000.0, kConditionedSourceRateHz};
    const auto &a = air.coefficients();
    expect(bits(a.a0) == UINT64_C(0x3ff16dc259aad6e0) &&
               bits(a.a1) == UINT64_C(0xc00ea1c3764dc23d) &&
               bits(a.a2) == UINT64_C(0x40160176c2620086) &&
               bits(a.a3) == UINT64_C(0xc00c1f1df6143338) &&
               bits(a.a4) == UINT64_C(0x3feaf7f2ff589845) &&
               bits(a.numerator_scale) == UINT64_C(0x3eb1b070634db3df),
           "2 kHz low-pass coefficients diverged from the frozen record");
}

std::vector<double> filter_in_chunks(FourthOrderLowPass &filter,
                                     const std::vector<double> &input,
                                     const std::vector<std::size_t> &chunks) {
    std::vector<double> output;
    output.reserve(input.size());
    std::size_t offset = 0;
    for (const auto count : chunks) {
        expect(offset + count <= input.size(), "test chunk exceeded input");
        for (std::size_t i = 0; i < count; ++i) {
            output.push_back(filter.process(input[offset + i]));
        }
        offset += count;
    }
    expect(offset == input.size(), "test chunks did not cover input");
    return output;
}

void test_low_pass_impulse_and_chunk_invariance() {
    std::vector<double> input(23, 0.0);
    input[0] = 1.0;
    input[7] = -0.25;
    input[18] = 0.5;

    FourthOrderLowPass contiguous{2000.0, kConditionedSourceRateHz};
    const auto expected = filter_in_chunks(contiguous, input, {input.size()});
    expect(bits(expected[0]) == UINT64_C(0x3eb1b070634db3df) &&
               bits(expected[1]) == UINT64_C(0x3ee14fa2c1365178) &&
               bits(expected[2]) == UINT64_C(0x3f00d8b8f46e6712) &&
               bits(expected[3]) == UINT64_C(0x3f166ce31ffd8a40),
           "low-pass impulse recurrence changed");

    FourthOrderLowPass partitioned{2000.0, kConditionedSourceRateHz};
    const auto actual = filter_in_chunks(partitioned, input, {1, 2, 4, 3, 8, 5});
    expect(actual.size() == expected.size(),
           "partitioned low-pass output size changed");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        expect(bits(actual[i]) == bits(expected[i]),
               "low-pass state changed at a caller chunk boundary");
    }
}

void test_dc_removal_state_and_chunk_invariance() {
    DcRemoval dc{kConditionedSourceTimeStepS, kDcRemovalTimeConstantS};
    expect(bits(dc.alpha()) == UINT64_C(0x3f357088f45745f4),
           "DC-removal alpha diverged from the frozen record");

    const auto first = dc.process(1.0);
    const auto second = dc.process(1.0);
    expect(bits(first) == UINT64_C(0x3feffd51eee17517) &&
               bits(second) == UINT64_C(0x3feffaa41737e835),
           "DC-removal step response changed");

    const std::array input{0.5, -0.25, 0.75, 1.0, -1.0, 0.0, 0.125};
    DcRemoval contiguous{kConditionedSourceTimeStepS, kDcRemovalTimeConstantS};
    DcRemoval partitioned{kConditionedSourceTimeStepS, kDcRemovalTimeConstantS};
    std::array<double, input.size()> expected{};
    std::array<double, input.size()> actual{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        expected[i] = contiguous.process(input[i]);
    }
    for (std::size_t i = 0; i < 2; ++i) {
        actual[i] = partitioned.process(input[i]);
    }
    for (std::size_t i = 2; i < input.size(); ++i) {
        actual[i] = partitioned.process(input[i]);
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        expect(bits(actual[i]) == bits(expected[i]),
               "DC-removal state changed at a caller chunk boundary");
    }
}

void test_backward_derivative_state_and_chunk_invariance() {
    BackwardDerivative derivative{kConditionedSourceTimeStepS};
    expect(derivative.process(1.0) == 192000.0 && derivative.process(1.0) == 0.0 &&
               derivative.process(-0.5) == -288000.0,
           "backward-derivative impulse behavior changed");

    const std::array input{0.0, 0.25, 1.0, -1.0, -1.0, 0.5};
    BackwardDerivative contiguous{kConditionedSourceTimeStepS};
    BackwardDerivative partitioned{kConditionedSourceTimeStepS};
    std::array<double, input.size()> expected{};
    std::array<double, input.size()> actual{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        expected[i] = contiguous.process(input[i]);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        actual[i] = partitioned.process(input[i]);
    }
    for (std::size_t i = 4; i < input.size(); ++i) {
        actual[i] = partitioned.process(input[i]);
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        expect(bits(actual[i]) == bits(expected[i]),
               "derivative state changed at a caller chunk boundary");
    }
}

void test_cleanup_and_publication_order() {
    const auto denormal = std::numeric_limits<double>::denorm_min();
    expect(bits(cleanup_conditioned_sample(denormal)) == UINT64_C(0) &&
               bits(cleanup_conditioned_sample(-denormal)) == UINT64_C(0),
           "subnormal cleanup did not produce exact positive zero");

    const double negative_zero = -0.0;
    expect(bits(cleanup_conditioned_sample(negative_zero)) ==
               UINT64_C(0x8000000000000000),
           "cleanup changed a non-subnormal signed zero");
    expect(bits(cleanup_conditioned_sample(1.25)) == bits(1.25),
           "cleanup changed a normal value");

    expect(bits(publish_calibrated_float32(1.0, kSourcePublicationCalibration)) ==
                   UINT32_C(0x32800000) &&
               bits(publish_calibrated_float32(-0.5, kSourcePublicationCalibration)) ==
                   UINT32_C(0xb2000000) &&
               bits(publish_calibrated_float32(0.1, kSourcePublicationCalibration)) ==
                   UINT32_C(0x30cccccd),
           "Float32 publication/calibration order changed");
}

void test_invalid_and_nonfinite_inputs_fail_closed() {
    expect_throw<std::invalid_argument>(
        [] { FourthOrderLowPass filter{0.0, kConditionedSourceRateHz}; },
        "zero cutoff constructed a low-pass");
    expect_throw<std::invalid_argument>(
        [] {
            FourthOrderLowPass filter{kConditionedSourceRateHz / 2.0,
                                      kConditionedSourceRateHz};
        },
        "Nyquist cutoff constructed a low-pass");
    expect_throw<std::invalid_argument>(
        [] { DcRemoval dc{0.0, kDcRemovalTimeConstantS}; },
        "zero time step constructed DC removal");
    expect_throw<std::invalid_argument>([] { BackwardDerivative derivative{0.0}; },
                                        "zero time step constructed a derivative");

    FourthOrderLowPass filter{2000.0, kConditionedSourceRateHz};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(filter.process(std::numeric_limits<double>::quiet_NaN()));
        },
        "non-finite low-pass input was accepted");
    DcRemoval dc{kConditionedSourceTimeStepS, kDcRemovalTimeConstantS};
    expect_throw<std::domain_error>(
        [&] { static_cast<void>(dc.process(std::numeric_limits<double>::infinity())); },
        "non-finite DC-removal input was accepted");
    BackwardDerivative derivative{kConditionedSourceTimeStepS};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                derivative.process(-std::numeric_limits<double>::infinity()));
        },
        "non-finite derivative input was accepted");
    expect_throw<std::domain_error>(
        [] {
            static_cast<void>(
                cleanup_conditioned_sample(std::numeric_limits<double>::quiet_NaN()));
        },
        "non-finite cleanup input was accepted");
    expect_throw<std::domain_error>(
        [] {
            static_cast<void>(publish_calibrated_float32(
                std::numeric_limits<double>::max(), kSourcePublicationCalibration));
        },
        "overflowing Float32 publication was accepted");
    expect_throw<std::invalid_argument>(
        [] { static_cast<void>(publish_calibrated_float32(1.0, 0.0)); },
        "zero publication calibration gain was accepted");
    expect_throw<std::domain_error>(
        [] {
            static_cast<void>(publish_calibrated_float32(
                1.0, std::numeric_limits<double>::quiet_NaN()));
        },
        "non-finite publication calibration gain was accepted");
}

void run_tests() {
    test_frozen_low_pass_coefficients();
    test_low_pass_impulse_and_chunk_invariance();
    test_dc_removal_state_and_chunk_invariance();
    test_backward_derivative_state_and_chunk_invariance();
    test_cleanup_and_publication_order();
    test_invalid_and_nonfinite_inputs_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "DSP primitive test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
