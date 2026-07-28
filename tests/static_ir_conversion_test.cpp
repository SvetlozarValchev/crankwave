#include "dsp/static_ir_conversion.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "presentation/pcm16_ir_decoder.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

constexpr std::array<std::uint8_t, 32> kConfiguredIrInputSha256{
    0x75, 0xde, 0x9d, 0xb4, 0x70, 0x63, 0x39, 0x56, 0x65, 0xd3, 0x6b,
    0x6d, 0x42, 0x32, 0xf4, 0x77, 0xaa, 0xe3, 0x85, 0xfe, 0xaa, 0x9b,
    0xa1, 0x58, 0x35, 0x3f, 0xbd, 0xaf, 0x12, 0x2d, 0xb5, 0xcc,
};
constexpr std::array<std::uint8_t, 32> kConfiguredIrKernelSha256{
    0x94, 0x0e, 0x3f, 0x58, 0x5c, 0xbd, 0xf3, 0x4d, 0xf6, 0xe9, 0x07,
    0x3d, 0xb6, 0x29, 0xc0, 0x2b, 0x58, 0x5d, 0x6e, 0x09, 0xc4, 0xd3,
    0xc3, 0x1a, 0x39, 0x3e, 0xb3, 0x75, 0x9f, 0x35, 0x75, 0x98,
};
constexpr std::uint64_t kConfiguredIrGainBits = UINT64_C(0x3f50624dd2f1a9fc);

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

std::vector<std::byte> read_bounded_file(const std::string &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        throw std::runtime_error{"cannot open configured IR test input: " + path};
    }
    const auto end = input.tellg();
    if (end < 0 || end > 100000) {
        throw std::runtime_error{"configured IR test input has an invalid size"};
    }
    const auto size = static_cast<std::size_t>(end);
    std::vector<std::byte> bytes(size);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != bytes.size()) {
        throw std::runtime_error{"configured IR test input changed while reading"};
    }
    return bytes;
}

std::vector<std::byte> serialize_f64le(std::span<const double> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size() * sizeof(double));
    for (const auto value : values) {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            bytes.push_back(
                std::byte{static_cast<unsigned char>((raw >> shift) & 0xffU)});
        }
    }
    return bytes;
}

void test_half_up_counts_and_synthetic_impulse() {
    struct CountVector {
        std::size_t source;
        std::size_t target;
    };
    constexpr std::array counts{
        CountVector{1, 4},        CountVector{2, 9},          CountVector{3, 13},
        CountVector{6907, 30071}, CountVector{33705, 146743},
    };
    for (const auto &count : counts) {
        expect(dsp::static_ir_target_count(count.source) == count.target,
               "static IR half-up target count changed");
    }

    constexpr std::array<std::int16_t, 1> impulse{32767};
    const double gain = std::bit_cast<double>(kConfiguredIrGainBits);
    const auto output = dsp::convert_static_ir(impulse, 1, gain);
    constexpr std::array<std::uint64_t, 4> expected{
        UINT64_C(0x3f360746d7de5396),
        UINT64_C(0x3f3422d4e59779e2),
        UINT64_C(0x3f2e19b83af959ec),
        UINT64_C(0x3f20a47ee1a85b08),
    };
    expect(output.size() == expected.size(),
           "single-sample IR output count changed");
    for (std::size_t index = 0; index < output.size(); ++index) {
        expect(bits(output[index]) == expected[index],
               "single-sample IR conversion bits changed");
    }

    const auto zero_gain = dsp::convert_static_ir(impulse, 1, 0.0);
    for (const auto coefficient : zero_gain) {
        expect(coefficient == 0.0,
               "zero configured gain did not produce a zero kernel");
    }
}

void test_invalid_inputs_fail_before_conversion() {
    constexpr std::array<std::int16_t, 3> source{101, 0, 0};
    expect_throw<std::invalid_argument>(
        [] { static_cast<void>(dsp::static_ir_target_count(0)); },
        "zero IR support produced a target count");
    expect_throw<std::invalid_argument>(
        [] {
            static_cast<void>(dsp::static_ir_target_count(
                dsp::StaticIrConversionLimits::maximum_source_frame_count + 1));
        },
        "oversized IR support produced a target count");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(dsp::convert_static_ir({}, 1, 0.001)); },
        "empty IR source was accepted");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(dsp::convert_static_ir(source, 0, 0.001)); },
        "zero meaningful support was accepted");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(dsp::convert_static_ir(source, 2, 0.001)); },
        "incorrect meaningful support was accepted");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(dsp::convert_static_ir(source, 4, 0.001)); },
        "meaningful support beyond the source was accepted");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(dsp::convert_static_ir(source, 1, -0.001)); },
        "negative configured IR gain was accepted");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(dsp::convert_static_ir(
                source, 1, std::numeric_limits<double>::infinity()));
        },
        "non-finite configured IR gain was accepted");

    std::vector<std::int16_t> oversized(
        dsp::StaticIrConversionLimits::maximum_source_frame_count + 1, 0);
    oversized.front() = 101;
    expect_throw<std::length_error>(
        [&] { static_cast<void>(dsp::convert_static_ir(oversized, 1, 0.001)); },
        "oversized decoded IR source was accepted");
}

void test_canonical_kernel_identity(const std::string &path) {
    const auto wave_bytes = read_bounded_file(path);
    expect(wave_bytes.size() == 78602 &&
               contract::sha256(wave_bytes).bytes == kConfiguredIrInputSha256,
           "configured IR input identity changed");

    const auto decoded_result = presentation::decode_pcm16_ir_wave(wave_bytes);
    const auto *decoded = std::get_if<presentation::DecodedPcm16Ir>(&decoded_result);
    expect(decoded != nullptr && decoded->samples.size() == 33705 &&
               decoded->meaningful_support_frames == 6907,
           "configured IR media shape or meaningful support changed");

    const auto coefficients =
        dsp::convert_static_ir(decoded->samples, decoded->meaningful_support_frames,
                                   std::bit_cast<double>(kConfiguredIrGainBits));
    expect(coefficients.size() == 30071,
           "configured IR coefficient count changed");

    struct CoefficientProbe {
        std::size_t index;
        std::uint64_t expected_bits;
    };
    constexpr std::array probes{
        CoefficientProbe{0, UINT64_C(0x3ed25cabc018c0c8)},
        CoefficientProbe{1, UINT64_C(0x3ed3c70846adc122)},
        CoefficientProbe{2, UINT64_C(0x3ed39442e8723512)},
        CoefficientProbe{5205, UINT64_C(0x3f2f377960f5cbd9)},
        CoefficientProbe{30070, UINT64_C(0xbe868a1932e75950)},
    };
    for (const auto &probe : probes) {
        expect(bits(coefficients[probe.index]) == probe.expected_bits,
               "configured IR coefficient probe changed");
    }

    const auto canonical_bytes = serialize_f64le(coefficients);
    expect(canonical_bytes.size() == 240568 &&
               contract::sha256(canonical_bytes).bytes == kConfiguredIrKernelSha256,
           "regenerated configured-IR kernel identity changed");
}

void run_tests(const std::string &configured_ir_path) {
    test_half_up_counts_and_synthetic_impulse();
    test_invalid_inputs_fail_before_conversion();
    test_canonical_kernel_identity(configured_ir_path);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected exactly one configured-IR input path"};
        }
        run_tests(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "static IR conversion test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
