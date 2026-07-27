#include "dsp/p18_fixed_fft.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "presentation/p18_overlap_save_convolver.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace engine_sim_offline;
using presentation::P18CausalOverlapSaveConvolver;

constexpr std::size_t kReferenceBlockFrames = 3840;

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

std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::runtime_error{"invalid test digest"};
}

std::array<std::uint8_t, 32> digest_bytes(std::string_view value) {
    if (value.size() != 64) {
        throw std::runtime_error{"invalid test digest length"};
    }
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>((hex_nibble(value[2 * index]) << 4U) |
                                                  hex_nibble(value[2 * index + 1]));
    }
    return result;
}

void append_u16le(std::vector<std::byte> &bytes, std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xffU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)});
}

void append_f64le(std::vector<std::byte> &bytes, double value) {
    const auto raw = std::bit_cast<std::uint64_t>(value);
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        bytes.push_back(std::byte{static_cast<unsigned char>((raw >> shift) & 0xffU)});
    }
}

std::vector<std::byte> serialize_f64le(std::span<const double> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size() * sizeof(double));
    for (const double value : values) {
        append_f64le(bytes, value);
    }
    return bytes;
}

std::vector<std::byte>
serialize_complex_f64le(std::span<const std::complex<double>> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size() * 2 * sizeof(double));
    for (const auto value : values) {
        append_f64le(bytes, value.real());
        append_f64le(bytes, value.imag());
    }
    return bytes;
}

void expect_sha256(std::span<const std::byte> bytes, std::string_view expected,
                   const char *message) {
    expect(contract::sha256(bytes).bytes == digest_bytes(expected), message);
}

std::vector<double> make_synthetic_kernel() {
    std::vector<double> coefficients(dsp::P18FixedConvolutionKernel::coefficient_count);
    std::uint32_t state = UINT32_C(0x6a09e667);
    for (double &coefficient : coefficients) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const std::int64_t centered =
            static_cast<std::int64_t>(state) - INT64_C(2147483648);
        coefficient = static_cast<double>(centered) * 0x1p-47;
    }
    return coefficients;
}

std::vector<double> make_nontrivial_input() {
    std::vector<double> input(2 * kReferenceBlockFrames);
    std::uint32_t state = UINT32_C(0x31415926);
    for (double &sample : input) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const std::int64_t centered =
            static_cast<std::int64_t>(state) - INT64_C(2147483648);
        sample = static_cast<double>(centered) * 0x1p-31;
    }
    return input;
}

std::vector<double> direct_causal_convolution(std::span<const double> input,
                                              std::span<const double> coefficients) {
    std::vector<double> output(input.size(), 0.0);
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const std::size_t retained = std::min(frame + 1, coefficients.size());
        for (std::size_t tap = 0; tap < retained; ++tap) {
            output[frame] += coefficients[tap] * input[frame - tap];
        }
    }
    return output;
}

void expect_near_direct(std::span<const double> actual,
                        std::span<const double> expected, std::span<const double> input,
                        std::span<const double> coefficients, const char *message) {
    expect(actual.size() == expected.size() && actual.size() == input.size(), message);
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        long double absolute_product_sum = 0.0L;
        const std::size_t retained = std::min(frame + 1, coefficients.size());
        for (std::size_t tap = 0; tap < retained; ++tap) {
            absolute_product_sum +=
                std::abs(static_cast<long double>(coefficients[tap]) *
                         static_cast<long double>(input[frame - tap]));
        }
        const long double tolerance =
            4096.0L * static_cast<long double>(std::numeric_limits<double>::epsilon()) *
            std::max(1.0L, absolute_product_sum);
        expect(std::abs(static_cast<long double>(actual[frame]) -
                        static_cast<long double>(expected[frame])) <= tolerance,
               message);
    }
}

std::vector<double>
process_two_reference_blocks(P18CausalOverlapSaveConvolver &convolver,
                             std::span<const double> input) {
    expect(input.size() == 2 * kReferenceBlockFrames,
           "two-block test input extent changed");
    std::vector<double> output(input.size());
    convolver.process(input.first(kReferenceBlockFrames),
                      std::span<double>{output}.first(kReferenceBlockFrames));
    convolver.process(input.subspan(kReferenceBlockFrames),
                      std::span<double>{output}.subspan(kReferenceBlockFrames));
    return output;
}

void test_plan_tables_and_shape_validation(
    const std::shared_ptr<const dsp::P18FixedFftPlan> &plan) {
    expect(dsp::P18FixedFftLimits::transform_bit_count == 16 &&
               dsp::P18FixedFftLimits::transform_length == 65536 &&
               dsp::P18FixedFftLimits::forward_root_count == 32768,
           "P1.8 fixed FFT limits changed");

    struct ReversalProbe {
        std::size_t input;
        std::size_t expected;
    };
    constexpr std::array reversals{
        ReversalProbe{1, 0x8000},      ReversalProbe{2, 0x4000},
        ReversalProbe{3, 0xc000},      ReversalProbe{0x1234, 0x2c48},
        ReversalProbe{0xa55a, 0x5aa5},
    };
    for (const auto &probe : reversals) {
        expect(plan->reversed_index(probe.input) == probe.expected,
               "P1.8 FFT reversal probe changed");
    }

    std::vector<std::byte> reversal_bytes;
    reversal_bytes.reserve(dsp::P18FixedFftLimits::transform_length * 2);
    for (std::size_t index = 0; index < dsp::P18FixedFftLimits::transform_length;
         ++index) {
        append_u16le(reversal_bytes,
                     static_cast<std::uint16_t>(plan->reversed_index(index)));
    }
    expect_sha256(reversal_bytes,
                  "4207deb2ff150a2cd03ee0609908c02c9d3cc10739ba60c44000caca7b00a841",
                  "P1.8 FFT reversal-table identity changed");

    struct RootProbe {
        std::size_t index;
        std::uint64_t real;
        std::uint64_t imaginary;
    };
    constexpr std::array roots{
        RootProbe{0, UINT64_C(0x3ff0000000000000), UINT64_C(0x8000000000000000)},
        RootProbe{1, UINT64_C(0x3feffffffd885867), UINT64_C(0xbf1921fb539ecf31)},
        RootProbe{8192, UINT64_C(0x3fe6a09e667f3bcd), UINT64_C(0xbfe6a09e667f3bcc)},
        RootProbe{16384, UINT64_C(0x3c91a62633145c07), UINT64_C(0xbff0000000000000)},
        RootProbe{32767, UINT64_C(0xbfeffffffd885867), UINT64_C(0xbf1921fb539ec565)},
    };
    std::vector<std::complex<double>> all_roots;
    all_roots.reserve(dsp::P18FixedFftLimits::forward_root_count);
    for (std::size_t index = 0; index < dsp::P18FixedFftLimits::forward_root_count;
         ++index) {
        all_roots.push_back(plan->forward_root(index));
    }
    for (const auto &probe : roots) {
        expect(std::bit_cast<std::uint64_t>(all_roots[probe.index].real()) ==
                       probe.real &&
                   std::bit_cast<std::uint64_t>(all_roots[probe.index].imag()) ==
                       probe.imaginary,
               "P1.8 FFT root probe changed");
    }
    const auto root_bytes = serialize_complex_f64le(all_roots);
    expect_sha256(root_bytes,
                  "81258be5f2c6c4b1e4db901dba822ad57e99d03db30da7331449e783dc2588ff",
                  "P1.8 FFT root-table identity changed");

    std::vector<std::complex<double>> wrong_size(
        dsp::P18FixedFftLimits::transform_length - 1);
    expect_throw<std::invalid_argument>(
        [&] { plan->forward(wrong_size); },
        "P1.8 FFT accepted a noncanonical transform extent");
    std::vector<std::complex<double>> nonfinite(
        dsp::P18FixedFftLimits::transform_length);
    nonfinite.back() = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    expect_throw<std::domain_error>([&] { plan->inverse(nonfinite); },
                                    "P1.8 FFT accepted a non-finite transform input");
    expect_throw<std::out_of_range>(
        [&] {
            static_cast<void>(
                plan->reversed_index(dsp::P18FixedFftLimits::transform_length));
        },
        "P1.8 FFT exposed an out-of-range reversal entry");
    expect_throw<std::out_of_range>(
        [&] {
            static_cast<void>(
                plan->forward_root(dsp::P18FixedFftLimits::forward_root_count));
        },
        "P1.8 FFT exposed an out-of-range root");
}

void test_kernel_and_exact_streaming_hashes(
    const std::shared_ptr<const dsp::P18FixedFftPlan> &plan,
    const std::vector<double> &coefficients,
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    const auto coefficient_bytes = serialize_f64le(coefficients);
    expect_sha256(coefficient_bytes,
                  "4a8c86a21eac92a19674e4a11464cf1522a5ceb1eea2a17f5f9e1a5606b1b328",
                  "synthetic P1.8 convolution kernel input changed");
    const auto spectrum_bytes = serialize_complex_f64le(kernel->spectrum());
    expect_sha256(spectrum_bytes,
                  "e8e04dae619e0cd64ea1e87aa3b9f169ef96ca9e387a0db9d719c20084b32457",
                  "P1.8 fixed kernel spectrum identity changed");
    expect(kernel->plan() == plan,
           "P1.8 convolution kernel did not retain the shared immutable plan");

    std::vector<double> impulse_input(2 * kReferenceBlockFrames, 0.0);
    impulse_input.front() = 1.0;
    P18CausalOverlapSaveConvolver impulse{kernel};
    const auto impulse_output = process_two_reference_blocks(impulse, impulse_input);
    expect_sha256(serialize_f64le(impulse_output),
                  "54bd23a001b1fb10077483dd745b5e8fec4277e14b30d376baac5d8968396f80",
                  "P1.8 two-block impulse convolution identity changed");
    const double tolerance = 4096.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t frame = 0; frame < impulse_output.size(); ++frame) {
        expect(std::abs(impulse_output[frame] - coefficients[frame]) <= tolerance,
               "P1.8 overlap-save convolution changed causal orientation");
    }

    const auto input = make_nontrivial_input();
    expect_sha256(serialize_f64le(input),
                  "5f8023d137263fe8af866f33727336f4df090a89e422df588fa0e18ee34e1a51",
                  "P1.8 nontrivial convolution input changed");
    P18CausalOverlapSaveConvolver reference{kernel};
    const auto expected = process_two_reference_blocks(reference, input);
    expect_sha256(serialize_f64le(expected),
                  "6b224c395e2ed32a7d5d2883b658127edae9a204ac0bf649f455299cb13932f4",
                  "P1.8 two-block convolution output identity changed");

    P18CausalOverlapSaveConvolver interleaved{kernel};
    P18CausalOverlapSaveConvolver unrelated{kernel};
    std::vector<double> actual(input.size());
    std::vector<double> unrelated_input(kReferenceBlockFrames, 0.25);
    std::vector<double> unrelated_output(kReferenceBlockFrames);
    interleaved.process(std::span<const double>{input}.first(kReferenceBlockFrames),
                        std::span<double>{actual}.first(kReferenceBlockFrames));
    unrelated.process(unrelated_input, unrelated_output);
    interleaved.process(std::span<const double>{input}.subspan(kReferenceBlockFrames),
                        std::span<double>{actual}.subspan(kReferenceBlockFrames));
    expect(actual == expected,
           "interleaved P1.8 route convolution shared mutable history");

    P18CausalOverlapSaveConvolver in_place{kernel};
    auto aliased = input;
    in_place.process(std::span<const double>{aliased}.first(kReferenceBlockFrames),
                     std::span<double>{aliased}.first(kReferenceBlockFrames));
    in_place.process(std::span<const double>{aliased}.subspan(kReferenceBlockFrames),
                     std::span<double>{aliased}.subspan(kReferenceBlockFrames));
    expect(aliased == expected,
           "in-place P1.8 overlap-save processing changed output bits");
}

void test_long_history_tail(const std::shared_ptr<const dsp::P18FixedFftPlan> &plan) {
    std::vector<double> coefficients(dsp::P18FixedConvolutionKernel::coefficient_count,
                                     0.0);
    coefficients[0] = 0.75;
    coefficients[1234] = -0.125;
    coefficients.back() = 0.03125;
    auto kernel =
        std::make_shared<const dsp::P18FixedConvolutionKernel>(coefficients, plan);
    P18CausalOverlapSaveConvolver convolver{kernel};

    constexpr std::size_t count =
        P18CausalOverlapSaveConvolver::maximum_block_frame_count;
    std::vector<double> input(count, 0.0);
    std::vector<double> output(count);
    input.front() = 1.0;
    convolver.process(input, output);
    const double tolerance = 4096.0 * std::numeric_limits<double>::epsilon();
    expect(std::abs(output[0] - 0.75) <= tolerance &&
               std::abs(output[1234] + 0.125) <= tolerance,
           "P1.8 overlap-save convolution changed sparse causal taps");

    input.assign(count, 0.0);
    convolver.process(input, output);
    expect(std::abs(output.front()) <= tolerance,
           "P1.8 convolution introduced a block-boundary head");
    convolver.process(input, output);
    expect(std::abs(output.back()) <= tolerance,
           "P1.8 convolution advanced its late sparse tail");
    convolver.process(input, output);
    constexpr std::size_t tail_frame =
        (dsp::P18FixedConvolutionKernel::coefficient_count - 1) - 3 * count;
    expect(std::abs(output[tail_frame] - coefficients.back()) <= tolerance &&
               std::abs(output[tail_frame - 1]) <= tolerance &&
               std::abs(output[tail_frame + 1]) <= tolerance,
           "P1.8 convolution lost or shifted its continuous late tail");
}

void test_alternate_partitions_and_partial_overlap(
    const std::shared_ptr<const dsp::P18FixedFftPlan> &plan) {
    std::vector<double> coefficients(dsp::P18FixedConvolutionKernel::coefficient_count,
                                     0.0);
    coefficients[0] = 0.5;
    coefficients[1] = -0.25;
    coefficients[2] = 0.125;
    coefficients[3] = 0.0625;
    auto kernel =
        std::make_shared<const dsp::P18FixedConvolutionKernel>(coefficients, plan);
    const std::vector<double> input{1.0, 2.0, -3.0, 4.0, 0.5, -0.25, 8.0, -2.0, 1.5};
    const auto direct = direct_causal_convolution(input, coefficients);

    P18CausalOverlapSaveConvolver contiguous{kernel};
    std::vector<double> contiguous_output(input.size());
    contiguous.process(input, contiguous_output);
    expect_near_direct(contiguous_output, direct, input, coefficients,
                       "P1.8 contiguous convolution diverged from direct oracle");

    auto process_split = [&](P18CausalOverlapSaveConvolver &convolver) {
        std::vector<double> output(input.size());
        convolver.process(std::span<const double>{input}.subspan(0, 3),
                          std::span<double>{output}.subspan(0, 3));
        convolver.process(std::span<const double>{input}.subspan(3, 4),
                          std::span<double>{output}.subspan(3, 4));
        convolver.process(std::span<const double>{input}.subspan(7, 2),
                          std::span<double>{output}.subspan(7, 2));
        return output;
    };
    P18CausalOverlapSaveConvolver split{kernel};
    const auto split_output = process_split(split);
    expect_near_direct(split_output, direct, input, coefficients,
                       "P1.8 split convolution lost continuous history");
    P18CausalOverlapSaveConvolver repeated{kernel};
    expect(process_split(repeated) == split_output,
           "identical P1.8 convolution partitions changed output bits");

    P18CausalOverlapSaveConvolver partial_overlap{kernel};
    std::vector<double> overlapping(input.size() + 1);
    std::copy(input.begin(), input.end(), overlapping.begin());
    partial_overlap.process(std::span<const double>{overlapping}.first(input.size()),
                            std::span<double>{overlapping}.subspan(1, input.size()));
    expect(std::equal(overlapping.begin() + 1, overlapping.end(),
                      contiguous_output.begin()),
           "partially overlapping P1.8 spans changed output bits");
    expect_near_direct(std::span<const double>{overlapping}.subspan(1), direct, input,
                       coefficients,
                       "partially overlapping P1.8 spans corrupted input");
}

void test_failures_are_transactional(
    const std::shared_ptr<const dsp::P18FixedFftPlan> &plan,
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    expect_throw<std::invalid_argument>(
        [] {
            P18CausalOverlapSaveConvolver invalid{
                std::shared_ptr<const dsp::P18FixedConvolutionKernel>{}};
        },
        "P1.8 convolver accepted a null immutable kernel");

    std::vector<double> short_kernel(
        dsp::P18FixedConvolutionKernel::coefficient_count - 1, 0.0);
    expect_throw<std::invalid_argument>(
        [&] { dsp::P18FixedConvolutionKernel invalid{short_kernel, plan}; },
        "P1.8 convolution kernel accepted the wrong coefficient count");
    std::vector<double> nonfinite_kernel(
        dsp::P18FixedConvolutionKernel::coefficient_count, 0.0);
    nonfinite_kernel.back() = std::numeric_limits<double>::infinity();
    expect_throw<std::domain_error>(
        [&] { dsp::P18FixedConvolutionKernel invalid{nonfinite_kernel, plan}; },
        "P1.8 convolution kernel accepted a non-finite coefficient");

    P18CausalOverlapSaveConvolver candidate{kernel};
    const std::vector<double> initial_history(candidate.history().begin(),
                                              candidate.history().end());
    std::vector<double> one_input{1.0};
    std::vector<double> mismatched_output(2, 17.0);
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(one_input, mismatched_output); },
        "P1.8 convolver accepted mismatched input/output lengths");
    std::span<const double> empty_input;
    std::span<double> empty_output;
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(empty_input, empty_output); },
        "P1.8 convolver accepted an empty block");
    std::vector<double> oversized(
        P18CausalOverlapSaveConvolver::maximum_block_frame_count + 1, 0.0);
    expect_throw<std::length_error>([&] { candidate.process(oversized, oversized); },
                                    "P1.8 convolver used an oversized-block fallback");
    std::vector<double> nonfinite_input{1.0, std::numeric_limits<double>::infinity()};
    std::vector<double> sentinel{17.0, 19.0};
    expect_throw<std::domain_error>(
        [&] { candidate.process(nonfinite_input, sentinel); },
        "P1.8 convolver accepted non-finite input");
    expect(sentinel == std::vector<double>({17.0, 19.0}) &&
               std::equal(candidate.history().begin(), candidate.history().end(),
                          initial_history.begin()),
           "preflight failure changed P1.8 convolution output or history");

    std::vector<double> overflowing(kReferenceBlockFrames,
                                    std::numeric_limits<double>::max());
    std::vector<double> overflow_output(kReferenceBlockFrames, 23.0);
    expect_throw<std::domain_error>(
        [&] { candidate.process(overflowing, overflow_output); },
        "P1.8 convolution accepted non-finite transform arithmetic");
    expect(std::all_of(overflow_output.begin(), overflow_output.end(),
                       [](double value) { return value == 23.0; }) &&
               std::equal(candidate.history().begin(), candidate.history().end(),
                          initial_history.begin()),
           "arithmetic failure changed P1.8 convolution output or history");

    const auto valid = make_nontrivial_input();
    std::vector<double> candidate_output(kReferenceBlockFrames);
    std::vector<double> fresh_output(kReferenceBlockFrames);
    candidate.process(std::span<const double>{valid}.first(kReferenceBlockFrames),
                      candidate_output);
    P18CausalOverlapSaveConvolver fresh{kernel};
    fresh.process(std::span<const double>{valid}.first(kReferenceBlockFrames),
                  fresh_output);
    expect(candidate_output == fresh_output,
           "failed P1.8 convolution changed the next valid block");
}

void run_tests() {
    auto plan = std::make_shared<const dsp::P18FixedFftPlan>();
    test_plan_tables_and_shape_validation(plan);
    const auto coefficients = make_synthetic_kernel();
    auto kernel =
        std::make_shared<const dsp::P18FixedConvolutionKernel>(coefficients, plan);
    test_kernel_and_exact_streaming_hashes(plan, coefficients, kernel);
    test_long_history_tail(plan);
    test_alternate_partitions_and_partial_overlap(plan);
    test_failures_are_transactional(plan, kernel);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "P1.8 fixed FFT/convolution test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
