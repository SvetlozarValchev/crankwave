#include "simulation/cycle_accounting_method_registry.hpp"

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
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & UINT8_C(0x0f)];
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
            "four-stroke-piecewise-linear-cycle-quadrature-v1",
            1,
            "57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac",
            simulation::four_stroke_piecewise_linear_cycle_quadrature_method_descriptor,
            simulation::four_stroke_piecewise_linear_cycle_quadrature_method_identity,
        },
        {
            "chen-flynn-cycle-mean-aggregate-loss-v1",
            1,
            "6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489",
            simulation::chen_flynn_cycle_mean_aggregate_loss_method_descriptor,
            simulation::chen_flynn_cycle_mean_aggregate_loss_method_identity,
        },
    }};
    return cases;
}

[[nodiscard]] std::array<const contract::MethodIdentity *, 2>
identity_fields(const simulation::CycleAccountingMethodIdentities &identities) {
    return {
        &identities.cycle_quadrature,
        &identities.aggregate_loss,
    };
}

struct ContractMethods {
    contract::EngineSpec engine;
    contract::LowOrderOperatingPointV1Profile profile;
};

[[nodiscard]] ContractMethods exact_contract_methods() {
    const auto &implemented =
        simulation::implemented_cycle_accounting_method_identities();
    ContractMethods result;
    result.profile.cycle_quadrature = {
        implemented.cycle_quadrature,
        "registry-test.cycle-quadrature",
    };
    result.engine.methods.losses = {
        implemented.aggregate_loss,
        "registry-test.aggregate-loss",
    };
    return result;
}

[[nodiscard]] contract::MethodIdentity &contract_method_field(ContractMethods &methods,
                                                              std::size_t index) {
    switch (index) {
    case 0:
        return methods.profile.cycle_quadrature.value;
    case 1:
        return methods.engine.methods.losses.value;
    default:
        throw std::out_of_range{"cycle-accounting method index is out of range"};
    }
}

[[nodiscard]] constexpr std::array<std::string_view, 2> contract_method_paths() {
    return {
        "engine.physics_profile.cycle_quadrature.value",
        "engine.methods.losses.value",
    };
}

void expect_exact_rejection(const ContractMethods &methods,
                            std::string_view expected_path) {
    expect(!simulation::exactly_matches_implemented_cycle_accounting_methods(
               methods.engine, methods.profile),
           "mutated cycle-accounting method set passed the exact predicate");
    const auto report = simulation::admit_implemented_cycle_accounting_methods(
        methods.engine, methods.profile);
    expect(report.issues.size() == 1,
           "single cycle-accounting method mutation did not produce one issue");
    expect(report.issues.front().code ==
                   contract::ContractIssueCode::unsupported_value &&
               report.issues.front().path == expected_path,
           "cycle-accounting method mutation issue has the wrong code or path");
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

void test_exact_method_identities() {
    for (const auto &method : method_cases()) {
        const auto descriptor = method.descriptor();
        const auto actual_digest = descriptor_digest(descriptor);
        const auto expected_digest = digest_from_hex(method.expected_digest);
        expect_canonical_lf(descriptor,
                            "cycle-accounting descriptor is not canonical LF text");
        if (actual_digest != expected_digest) {
            std::cerr << method.expected_id
                      << " descriptor SHA-256: " << digest_hex(actual_digest) << '\n';
        }
        expect(actual_digest == expected_digest,
               "cycle-accounting descriptor digest changed");

        const auto &identity = method.identity();
        expect(identity.id == method.expected_id &&
                   identity.version == method.expected_version &&
                   identity.configuration_sha256 == expected_digest,
               "cycle-accounting method identity changed");
        expect(contract::validate(identity).ok(),
               "cycle-accounting method identity is not contract-valid");
        expect(&identity == &method.identity(),
               "cycle-accounting method identity storage is not stable");
    }
}

void test_registry_shape_and_product_neutrality() {
    const auto &first = simulation::implemented_cycle_accounting_method_identities();
    const auto &second = simulation::implemented_cycle_accounting_method_identities();
    expect(&first == &second, "implemented cycle-accounting method set is not stable");

    const auto fields = identity_fields(first);
    expect(fields[0] == &method_cases()[0].identity() &&
               fields[1] == &method_cases()[1].identity(),
           "identity accessors do not reference the implemented singleton");
    expect(fields[0]->id != fields[1]->id &&
               fields[0]->configuration_sha256 != fields[1]->configuration_sha256,
           "cycle-accounting registry contains duplicate authority");

    constexpr std::array forbidden_tokens{"m4", "bmw", "fixture", "profile_id"};
    for (const auto &method : method_cases()) {
        const auto lower_id = ascii_lower(method.identity().id);
        const auto lower_descriptor = ascii_lower(method.descriptor());
        for (const std::string_view token : forbidden_tokens) {
            expect(lower_id.find(token) == std::string::npos &&
                       lower_descriptor.find(token) == std::string::npos,
                   "cycle-accounting authority contains product-specific text");
        }
    }
}

void test_exact_admission_and_all_identity_mutations() {
    auto exact = exact_contract_methods();
    expect(simulation::exactly_matches_implemented_cycle_accounting_methods(
               exact.engine, exact.profile),
           "implemented cycle-accounting method set failed the exact predicate");
    expect(simulation::admit_implemented_cycle_accounting_methods(exact.engine,
                                                                  exact.profile)
               .ok(),
           "implemented cycle-accounting method set failed admission");

    exact.profile.cycle_quadrature.resolution_id =
        "different-cycle-quadrature-resolution";
    exact.engine.methods.losses.resolution_id = "different-aggregate-loss-resolution";
    expect(simulation::exactly_matches_implemented_cycle_accounting_methods(
               exact.engine, exact.profile) &&
               simulation::admit_implemented_cycle_accounting_methods(exact.engine,
                                                                      exact.profile)
                   .ok(),
           "method admission incorrectly included provenance references");

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
    test_exact_method_identities();
    test_registry_shape_and_product_neutrality();
    test_exact_admission_and_all_identity_mutations();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "cycle-accounting method registry test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
