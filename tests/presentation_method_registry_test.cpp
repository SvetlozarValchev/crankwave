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

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & 0x0fU];
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

using DescriptorAccessor = std::string_view (*)() noexcept;
using IdentityAccessor = const contract::MethodIdentity &(*)();

struct MethodCase {
    std::string_view expected_id;
    std::uint32_t expected_version;
    std::string_view expected_digest;
    DescriptorAccessor descriptor;
    IdentityAccessor identity;
};

[[nodiscard]] const std::array<MethodCase, 2> &method_cases() {
    static const std::array<MethodCase, 2> cases{{
        {
            presentation::kCalibratedPressurePublicationMethodId,
            presentation::kCalibratedPressurePublicationMethodVersion,
            "7b8f1e56cd6c35d2054937f9a9f4fe5481131fff48b73254414bfb8334ed986a",
            presentation::calibrated_pressure_publication_method_descriptor,
            presentation::calibrated_pressure_publication_method_identity,
        },
        {
            presentation::kCoherentTwoOutletAuditionMethodId,
            presentation::kCoherentTwoOutletAuditionMethodVersion,
            "a75d0f7d28f4c4ad602a8ad51715b50b435e8b72f8241474166ee38313f5c0d6",
            presentation::coherent_two_outlet_audition_method_descriptor,
            presentation::coherent_two_outlet_audition_method_identity,
        },
    }};
    return cases;
}

[[nodiscard]] std::array<const contract::MethodIdentity *, 2>
identity_fields(const presentation::PresentationMethodIdentities &identities) {
    return {
        &identities.calibrated_pressure_publication,
        &identities.coherent_two_outlet_audition,
    };
}

[[nodiscard]] contract::PresentationMethods exact_contract_methods() {
    const auto &implemented =
        presentation::implemented_presentation_method_identities();
    return {
        {implemented.calibrated_pressure_publication,
         "registry-test.calibrated-pressure-publication"},
        {implemented.coherent_two_outlet_audition,
         "registry-test.coherent-two-outlet-audition"},
    };
}

contract::ResolvedValue<contract::MethodIdentity> &
contract_method_field(contract::PresentationMethods &methods, std::size_t index) {
    if (index == 0) {
        return methods.calibrated_pressure_publication;
    }
    return methods.coherent_two_outlet_audition;
}

void test_descriptor_and_identity_goldens() {
    const auto &implemented =
        presentation::implemented_presentation_method_identities();
    const auto fields = identity_fields(implemented);
    for (std::size_t index = 0; index < method_cases().size(); ++index) {
        const auto &test = method_cases()[index];
        const auto descriptor = test.descriptor();
        expect_canonical_lf(descriptor, "method descriptor is not canonical LF text");
        const auto actual_digest = descriptor_digest(descriptor);
        if (digest_hex(actual_digest) != test.expected_digest) {
            std::cerr << test.expected_id << " descriptor SHA-256: "
                      << digest_hex(actual_digest) << '\n';
        }
        const auto expected_digest = digest_from_hex(test.expected_digest);
        const auto &identity = test.identity();
        expect(identity.id == test.expected_id &&
                   identity.version == test.expected_version &&
                   identity.configuration_sha256 == expected_digest &&
                   identity.configuration_sha256 == actual_digest,
               "presentation method identity or pinned digest changed");
        expect(*fields[index] == identity,
               "implemented registry field differs from its identity accessor");
    }
    expect(fields[0]->id != fields[1]->id &&
               fields[0]->configuration_sha256 != fields[1]->configuration_sha256,
           "presentation method identities are not distinct");
}

void test_exact_admission() {
    const auto exact = exact_contract_methods();
    expect(presentation::exactly_matches_implemented_presentation_methods(exact),
           "exact physical-pressure methods were not recognized");
    expect(presentation::admit_implemented_presentation_methods(exact).ok(),
           "exact physical-pressure methods were rejected");

    for (std::size_t index = 0; index < method_cases().size(); ++index) {
        auto drift = exact;
        ++contract_method_field(drift, index).value.version;
        expect(!presentation::exactly_matches_implemented_presentation_methods(drift),
               "version drift matched the implemented methods");
        expect(!presentation::admit_implemented_presentation_methods(drift).ok(),
               "version drift was admitted");

        drift = exact;
        contract_method_field(drift, index)
            .value.configuration_sha256.bytes.front() ^= 0xffU;
        expect(!presentation::exactly_matches_implemented_presentation_methods(drift),
               "descriptor-digest drift matched the implemented methods");
        expect(!presentation::admit_implemented_presentation_methods(drift).ok(),
               "descriptor-digest drift was admitted");
    }
}

} // namespace

int main() {
    try {
        test_descriptor_and_identity_goldens();
        test_exact_admission();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
