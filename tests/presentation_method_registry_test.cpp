#include "presentation/presentation_method_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {

using namespace engine_sim_offline;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::runtime_error{"invalid pinned digest digit"};
}

[[nodiscard]] contract::Sha256Digest digest_from_hex(std::string_view value) {
    expect(value.size() == 64, "pinned digest has the wrong length");
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (hex_nibble(value[index * 2]) << 4U) | hex_nibble(value[index * 2 + 1]));
    }
    return result;
}

[[nodiscard]] contract::Sha256Digest descriptor_digest(std::string_view descriptor) {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

void expect_canonical_lf(std::string_view descriptor, const char *message) {
    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           message);
}

void test_static_ir_method_identity() {
    constexpr std::string_view expected_id =
        "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-v1";
    constexpr std::uint32_t expected_version = 1;
    const auto expected_digest = digest_from_hex(
        "9cd79fd80ebf77a1e1272dc60e2826dd38759969000fa93dc4e09e006431afc3");

    expect_canonical_lf(presentation::kStaticIrConversionMethodDescriptor,
                        "static IR descriptor is not canonical LF text");
    expect(descriptor_digest(presentation::kStaticIrConversionMethodDescriptor) ==
               expected_digest,
           "static IR descriptor digest changed");

    const auto &identity = presentation::static_ir_conversion_method_identity();
    expect(identity.id == expected_id && identity.version == expected_version &&
               identity.configuration_sha256 == expected_digest,
           "static IR method identity changed");
    expect(contract::validate(identity).ok(),
           "static IR method identity is not contract-valid");
    expect(&identity == &presentation::static_ir_conversion_method_identity(),
           "static IR method identity storage is not stable");
}

void test_fixed_overlap_save_method_identity() {
    constexpr std::string_view expected_id =
        "fixed-causal-overlap-save-radix2-dit-fft-65536-binary64-v1";
    constexpr std::uint32_t expected_version = 1;
    const auto expected_digest = digest_from_hex(
        "cd80ebe898fb9782d630468b6187f9768e2a54bedf06d216b8127ba3fd9c2d3c");

    expect_canonical_lf(presentation::kFixedOverlapSaveConvolutionMethodDescriptor,
                        "fixed convolution descriptor is not canonical LF text");
    expect(
        descriptor_digest(presentation::kFixedOverlapSaveConvolutionMethodDescriptor) ==
            expected_digest,
        "fixed convolution descriptor digest changed");

    const auto &identity =
        presentation::fixed_overlap_save_convolution_method_identity();
    expect(identity.id == expected_id && identity.version == expected_version &&
               identity.configuration_sha256 == expected_digest,
           "fixed convolution method identity changed");
    expect(contract::validate(identity).ok(),
           "fixed convolution method identity is not contract-valid");
    expect(&identity == &presentation::fixed_overlap_save_convolution_method_identity(),
           "fixed convolution method identity storage is not stable");
}

void test_registry_has_no_duplicates_or_fixture_coupling() {
    const auto &conversion = presentation::static_ir_conversion_method_identity();
    const auto &convolution =
        presentation::fixed_overlap_save_convolution_method_identity();
    expect(conversion.id != convolution.id &&
               conversion.configuration_sha256 != convolution.configuration_sha256,
           "presentation method registry contains a duplicate authority");

    constexpr std::array forbidden{"p18", "bmw", "fixture", "scenario_id"};
    for (const std::string_view token : forbidden) {
        expect(presentation::kStaticIrConversionMethodDescriptor.find(token) ==
                       std::string_view::npos &&
                   presentation::kFixedOverlapSaveConvolutionMethodDescriptor.find(
                       token) == std::string_view::npos,
               "production method descriptor contains fixture-specific text");
    }
}

void run_tests() {
    test_static_ir_method_identity();
    test_fixed_overlap_save_method_identity();
    test_registry_has_no_duplicates_or_fixture_coupling();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "presentation method registry test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
