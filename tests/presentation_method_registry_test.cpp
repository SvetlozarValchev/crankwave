#include "presentation/presentation_method_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
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

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(digest.bytes.size() * 2);
    for (const auto byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

void expect_canonical_lf(std::string_view descriptor, const char *message) {
    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           message);
}

using DescriptorAccessor = std::string_view (*)() noexcept;
using IdentityAccessor = const contract::MethodIdentity &(*)();

struct MethodCase {
    std::string_view expected_id;
    std::uint32_t expected_version;
    std::string_view expected_digest;
    DescriptorAccessor descriptor;
    IdentityAccessor identity;
};

[[nodiscard]] const std::array<MethodCase, 6> &method_cases() {
    static const std::array<MethodCase, 6> cases{{
        {
            "causal-kaiser-sinc-257tap-4096phase-20000-to-192000-binary64-v3",
            3,
            "6b77060b6cb91cb40f2d65a1dc9b90407bfe3ce00485347005a4442c79ffa828",
            presentation::causal_reconstruction_method_descriptor,
            presentation::causal_reconstruction_method_identity,
        },
        {
            "route-jitter-dc-derivative-flow-coupled-air-noise-binary64-v2",
            2,
            "d1fc9ea280b4ca9452d033cf0cb85f98f7c0d3bfb424e96f9eb98af97b38745e",
            presentation::route_conditioning_method_descriptor,
            presentation::route_conditioning_method_identity,
        },
        {
            "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-v1",
            1,
            "f006bfcad24e587e3aa6edd06f860ccdc8b22ad567c7f3c717a9e9b469b5144a",
            presentation::static_ir_conversion_method_descriptor,
            presentation::static_ir_conversion_method_identity,
        },
        {
            "fixed-causal-overlap-save-radix2-dit-fft-65536-binary64-v1",
            1,
            "438893081f95e279aa6c472baaa880f88ec82c9832156397298bd7df086977dd",
            presentation::fixed_overlap_save_convolution_method_descriptor,
            presentation::fixed_overlap_save_convolution_method_identity,
        },
        {
            "typed-gas-route-wet-selection-float32-wave-publication-20000-to-192000-"
            "20ms-clock-v5",
            5,
            "f21f8963a9990dd93f975694b28671eceb022c4eb45f4d85d0b9d1a88ec0b57f",
            presentation::route_stem_publication_method_descriptor,
            presentation::route_stem_publication_method_identity,
        },
        {
            "ordered-n-route-serial-float32-quarter-sine-pcm24-wave-master-v3",
            3,
            "d93315ed08f94cd3fc2e583306503d116b1d3bc5caa04864c79a20d24f916423",
            presentation::ordered_route_audition_method_descriptor,
            presentation::ordered_route_audition_method_identity,
        },
    }};
    return cases;
}

[[nodiscard]] std::array<const contract::MethodIdentity *, 6>
identity_fields(const presentation::PresentationMethodIdentities &identities) {
    return {
        &identities.reconstruction,
        &identities.conditioning,
        &identities.impulse_response_conversion,
        &identities.convolution,
        &identities.publication,
        &identities.audition_mix,
    };
}

[[nodiscard]] contract::PresentationMethods exact_contract_methods() {
    const auto &implemented =
        presentation::implemented_presentation_method_identities();
    return {
        {implemented.reconstruction, "registry-test.reconstruction"},
        {implemented.conditioning, "registry-test.conditioning"},
        {implemented.impulse_response_conversion, "registry-test.ir-conversion"},
        {implemented.convolution, "registry-test.convolution"},
        {implemented.publication, "registry-test.publication"},
        {implemented.audition_mix, "registry-test.audition"},
    };
}

[[nodiscard]] contract::MethodIdentity &
contract_method_field(contract::PresentationMethods &methods, std::size_t index) {
    switch (index) {
    case 0:
        return methods.reconstruction.value;
    case 1:
        return methods.conditioning.value;
    case 2:
        return methods.impulse_response_conversion.value;
    case 3:
        return methods.convolution.value;
    case 4:
        return methods.publication.value;
    case 5:
        return methods.audition_mix.value;
    default:
        throw std::out_of_range{"presentation method field index is out of range"};
    }
}

[[nodiscard]] constexpr std::array<std::string_view, 6> contract_method_paths() {
    return {
        "presentation.methods.reconstruction.value",
        "presentation.methods.conditioning.value",
        "presentation.methods.impulse_response_conversion.value",
        "presentation.methods.convolution.value",
        "presentation.methods.publication.value",
        "presentation.methods.audition_mix.value",
    };
}

void expect_exact_rejection(const contract::PresentationMethods &methods,
                            std::string_view expected_path) {
    expect(!presentation::exactly_matches_implemented_presentation_methods(methods),
           "mutated presentation method set passed the exact predicate");
    const auto report = presentation::admit_implemented_presentation_methods(methods);
    expect(report.issues.size() == 1,
           "single presentation method mutation did not produce one issue");
    expect(report.issues.front().code ==
                   contract::ContractIssueCode::unsupported_value &&
               report.issues.front().path == expected_path,
           "presentation method mutation issue has the wrong code or path");
}

[[nodiscard]] std::string ascii_lower(std::string_view input) {
    std::string result{input};
    for (char &character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

void test_all_exact_method_identities() {
    for (const auto &method : method_cases()) {
        const auto descriptor = method.descriptor();
        const auto expected_digest = digest_from_hex(method.expected_digest);
        expect_canonical_lf(descriptor,
                            "presentation descriptor is not canonical LF text");
        const auto actual_digest = descriptor_digest(descriptor);
        if (actual_digest != expected_digest) {
            throw std::runtime_error{"presentation descriptor digest changed for " +
                                     std::string{method.expected_id} + ": " +
                                     digest_hex(actual_digest)};
        }

        const auto &identity = method.identity();
        expect(identity.id == method.expected_id &&
                   identity.version == method.expected_version &&
                   identity.configuration_sha256 == expected_digest,
               "presentation method identity changed");
        expect(contract::validate(identity).ok(),
               "presentation method identity is not contract-valid");
        expect(&identity == &method.identity(),
               "presentation method identity storage is not stable");
    }
}

void test_singleton_shape_and_stability() {
    const auto &first = presentation::implemented_presentation_method_identities();
    const auto &second = presentation::implemented_presentation_method_identities();
    expect(&first == &second, "implemented presentation method set is not stable");

    const auto fields = identity_fields(first);
    for (std::size_t index = 0; index < fields.size(); ++index) {
        expect(fields[index] == &method_cases()[index].identity(),
               "identity accessor does not reference the implemented singleton");
    }
}

void test_registry_has_no_duplicates_or_product_coupling() {
    const auto &identities = presentation::implemented_presentation_method_identities();
    const auto fields = identity_fields(identities);
    for (std::size_t left = 0; left < fields.size(); ++left) {
        for (std::size_t right = left + 1; right < fields.size(); ++right) {
            expect(fields[left]->id != fields[right]->id,
                   "presentation registry contains a duplicate method ID");
            expect(fields[left]->configuration_sha256 !=
                       fields[right]->configuration_sha256,
                   "presentation registry contains a duplicate method digest");
        }
    }

    constexpr std::array forbidden_tokens{"p18", "bmw", "fixture", "scenario_id"};
    for (const auto &method : method_cases()) {
        const auto lower_id = ascii_lower(method.identity().id);
        const auto lower_descriptor = ascii_lower(method.descriptor());
        for (const std::string_view token : forbidden_tokens) {
            expect(lower_id.find(token) == std::string::npos &&
                       lower_descriptor.find(token) == std::string::npos,
                   "production presentation authority contains product-specific "
                   "text");
        }
    }
}

void test_exact_admission_and_all_field_mutations() {
    auto exact = exact_contract_methods();
    expect(presentation::exactly_matches_implemented_presentation_methods(exact),
           "implemented presentation method set failed the exact predicate");
    expect(presentation::admit_implemented_presentation_methods(exact).ok(),
           "implemented presentation method set failed admission");

    exact.reconstruction.resolution_id = "different-provenance-reference";
    expect(presentation::exactly_matches_implemented_presentation_methods(exact) &&
               presentation::admit_implemented_presentation_methods(exact).ok(),
           "method admission incorrectly included provenance references in method "
           "identity");

    const auto paths = contract_method_paths();
    for (std::size_t index = 0; index < paths.size(); ++index) {
        {
            auto methods = exact_contract_methods();
            contract_method_field(methods, index).id += "-mutation";
            expect_exact_rejection(methods, paths[index]);
        }
        {
            auto methods = exact_contract_methods();
            ++contract_method_field(methods, index).version;
            expect_exact_rejection(methods, paths[index]);
        }
        {
            auto methods = exact_contract_methods();
            contract_method_field(methods, index).configuration_sha256.bytes.front() ^=
                UINT8_C(0x80);
            expect_exact_rejection(methods, paths[index]);
        }
    }
}

void run_tests() {
    test_all_exact_method_identities();
    test_singleton_shape_and_stability();
    test_registry_has_no_duplicates_or_product_coupling();
    test_exact_admission_and_all_field_mutations();
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
