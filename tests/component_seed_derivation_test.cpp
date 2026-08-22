#include "crankwave/contract/randomness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::contract;

constexpr std::uint64_t kBmwPublicSeed = UINT64_C(0x00c0ffee);

struct ExpectedSeed {
    std::string_view domain_id;
    std::uint64_t component_index;
    std::uint64_t initial_state;
    std::uint64_t stream;
};

constexpr std::array kBmwExpectedSeeds{
    ExpectedSeed{"combustion", 0, UINT64_C(0x638e648507353211),
                 UINT64_C(0x09d037c702feb066)},
    ExpectedSeed{"combustion", 1, UINT64_C(0x8378fa334d97535e),
                 UINT64_C(0x45f2daf9901678c3)},
    ExpectedSeed{"combustion", 2, UINT64_C(0xb7f998bfc604914e),
                 UINT64_C(0x0ca67d0274c3a0a2)},
    ExpectedSeed{"combustion", 3, UINT64_C(0x0d3eccc412acfd53),
                 UINT64_C(0x1213a1f55f64b73e)},
    ExpectedSeed{"combustion", 4, UINT64_C(0x74774e667b6044c3),
                 UINT64_C(0x67c5e156e65edc4e)},
    ExpectedSeed{"combustion", 5, UINT64_C(0x8ee0a57a30066164),
                 UINT64_C(0x798f3346d42f6acd)},
    ExpectedSeed{"synth_air_noise", 0, UINT64_C(0x0d172bd0b6609980),
                 UINT64_C(0x40bb189b0ce745fe)},
    ExpectedSeed{"synth_air_noise", 1, UINT64_C(0xfe16c9e3ea44a31a),
                 UINT64_C(0x311ac0d4f16c0d57)},
    ExpectedSeed{"synth_jitter", 0, UINT64_C(0xa7cd663a89695273),
                 UINT64_C(0x0794616d5c2a0127)},
    ExpectedSeed{"synth_jitter", 1, UINT64_C(0x0fcabae05f0d4195),
                 UINT64_C(0x26228b61f534bb77)},
    ExpectedSeed{"starter", 0, UINT64_C(0xf13ae40d86d32229),
                 UINT64_C(0x615d43fd7f571709)},
};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] ComponentSeedDerivationRequest bmw_request() {
    ComponentSeedDerivationRequest request;
    request.seed_namespace_id = "baked.loaded_acceleration";
    request.public_seed = kBmwPublicSeed;
    request.ordered_components.reserve(kBmwExpectedSeeds.size());
    for (const auto &expected : kBmwExpectedSeeds) {
        request.ordered_components.push_back(
            {std::string{expected.domain_id}, expected.component_index});
    }
    return request;
}

[[nodiscard]] const ComponentSeedDerivation &
require_success(const ComponentSeedDerivationResult &result, const char *message) {
    const auto *derived = std::get_if<ComponentSeedDerivation>(&result);
    expect(derived != nullptr, message);
    return *derived;
}

[[nodiscard]] const ComponentSeedDerivationError &
require_error(const ComponentSeedDerivationResult &result,
              ComponentSeedDerivationErrorCode code, const char *message) {
    const auto *error = std::get_if<ComponentSeedDerivationError>(&result);
    expect(error != nullptr && error->code == code, message);
    return *error;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & UINT8_C(0x0f)]);
    }
    return result;
}

void test_exact_bmw_inventory() {
    const auto result = derive_component_seeds(bmw_request());
    const auto &derived = require_success(result, "BMW component seeds were rejected");
    const auto capture_key = digest_hex(derived.capture_random_key_sha256);
    if (capture_key !=
        "f3252023e25e84e450b4727d390032f71f3664ccd766cc6cefeb906c2117400d") {
        std::cerr << "BMW capture random key: " << capture_key << '\n';
        for (const auto &actual : derived.ordered_components) {
            std::cerr << actual.coordinate.domain_id << ' '
                      << actual.coordinate.component_index << " 0x" << std::hex
                      << actual.initialization.initial_state << " 0x"
                      << actual.initialization.stream << std::dec << '\n';
        }
    }
    expect(capture_key ==
               "f3252023e25e84e450b4727d390032f71f3664ccd766cc6cefeb906c2117400d",
           "BMW capture random key changed");
    expect(derived.ordered_components.size() == kBmwExpectedSeeds.size(),
           "BMW component seed count changed");
    for (std::size_t ordinal = 0; ordinal < kBmwExpectedSeeds.size(); ++ordinal) {
        const auto &expected = kBmwExpectedSeeds[ordinal];
        const auto &actual = derived.ordered_components[ordinal];
        expect(
            actual.coordinate ==
                    ComponentSeedCoordinate{std::string{expected.domain_id},
                                            expected.component_index} &&
                actual.initialization ==
                    Pcg32SeedInitialization{expected.initial_state, expected.stream} &&
                actual.initialization.stream <= kMaximumCanonicalPcg32Stream,
            "BMW component coordinate or seed pair changed");
    }
}

void test_mutation_and_domain_separation() {
    const auto canonical_result = derive_component_seeds(bmw_request());
    const auto canonical = require_success(canonical_result, "canonical setup failed");

    auto changed_namespace_request = bmw_request();
    changed_namespace_request.seed_namespace_id = "baked.loaded_acceleration.variant";
    const auto changed_namespace_result =
        derive_component_seeds(changed_namespace_request);
    const auto changed_namespace = require_success(
        changed_namespace_result, "changed seed namespace was rejected");
    expect(changed_namespace.capture_random_key_sha256 !=
                   canonical.capture_random_key_sha256 &&
               changed_namespace.ordered_components.front().initialization !=
                   canonical.ordered_components.front().initialization,
           "seed-namespace mutation did not separate the random domain");

    auto changed_public_seed_request = bmw_request();
    ++changed_public_seed_request.public_seed;
    const auto changed_public_seed_result =
        derive_component_seeds(changed_public_seed_request);
    const auto changed_public_seed =
        require_success(changed_public_seed_result, "changed public seed was rejected");
    expect(changed_public_seed.capture_random_key_sha256 !=
                   canonical.capture_random_key_sha256 &&
               changed_public_seed.ordered_components.front().initialization !=
                   canonical.ordered_components.front().initialization,
           "public-seed mutation did not separate the random domain");

    ComponentSeedDerivationRequest separated{
        "synthetic.domain-separation",
        7,
        {
            {"same_index_domain_a", 4},
            {"same_index_domain_b", 4},
            {"same_domain", 4},
            {"same_domain", 5},
        },
    };
    const auto separated_result = derive_component_seeds(separated);
    const auto &seeds =
        require_success(separated_result, "valid separated domains were rejected")
            .ordered_components;
    expect(seeds[0].initialization != seeds[1].initialization &&
               seeds[2].initialization != seeds[3].initialization,
           "component domain or index failed to separate PCG32 initialization");
}

void test_order_is_preserved_but_not_hashed_into_coordinates() {
    const auto forward_result = derive_component_seeds(bmw_request());
    const auto forward =
        require_success(forward_result, "forward ordering setup failed");
    auto reversed_request = bmw_request();
    std::reverse(reversed_request.ordered_components.begin(),
                 reversed_request.ordered_components.end());
    const auto reversed_result = derive_component_seeds(reversed_request);
    const auto reversed =
        require_success(reversed_result, "reversed component ordering was rejected");

    expect(reversed.capture_random_key_sha256 == forward.capture_random_key_sha256 &&
               reversed.ordered_components.size() == forward.ordered_components.size(),
           "component ordering changed the capture random key or inventory shape");
    for (std::size_t ordinal = 0; ordinal < reversed.ordered_components.size();
         ++ordinal) {
        expect(reversed.ordered_components[ordinal] ==
                   forward.ordered_components[forward.ordered_components.size() - 1 -
                                              ordinal],
               "derivation sorted coordinates or made their seeds order-dependent");
    }
}

void test_invalid_requests_fail_closed() {
    auto invalid_namespace = bmw_request();
    invalid_namespace.seed_namespace_id = "Invalid Namespace";
    const auto invalid_namespace_result = derive_component_seeds(invalid_namespace);
    const auto &namespace_error =
        require_error(invalid_namespace_result,
                      ComponentSeedDerivationErrorCode::invalid_seed_namespace_id,
                      "invalid seed namespace was accepted");
    expect(namespace_error.path == "seed_namespace_id" &&
               namespace_error.component_ordinal == kNoComponentSeedOrdinal,
           "invalid seed-namespace error lost its typed location");

    auto invalid_domain = bmw_request();
    invalid_domain.ordered_components[3].domain_id = "";
    const auto invalid_domain_result = derive_component_seeds(invalid_domain);
    const auto &domain_error =
        require_error(invalid_domain_result,
                      ComponentSeedDerivationErrorCode::invalid_component_domain_id,
                      "invalid component domain ID was accepted");
    expect(domain_error.path == "ordered_components[3].domain_id" &&
               domain_error.component_ordinal == 3,
           "invalid component-domain error lost its typed ordinal");

    auto duplicate = bmw_request();
    duplicate.ordered_components.push_back(duplicate.ordered_components[4]);
    const auto duplicate_result = derive_component_seeds(duplicate);
    const auto &duplicate_error =
        require_error(duplicate_result,
                      ComponentSeedDerivationErrorCode::duplicate_component_coordinate,
                      "duplicate component coordinate was accepted");
    expect(duplicate_error.component_ordinal ==
                   duplicate.ordered_components.size() - 1 &&
               duplicate_error.path == "ordered_components[11]",
           "duplicate component error did not identify the later input ordinal");

    auto empty = bmw_request();
    empty.ordered_components.clear();
    static_cast<void>(
        require_error(derive_component_seeds(empty),
                      ComponentSeedDerivationErrorCode::empty_component_inventory,
                      "empty component inventory was accepted"));

    ComponentSeedDerivationRequest maximum_values{
        "synthetic.maximum-values",
        std::numeric_limits<std::uint64_t>::max(),
        {{"maximum_index", std::numeric_limits<std::uint64_t>::max()}},
    };
    const auto maximum_values_result = derive_component_seeds(maximum_values);
    const auto &maximum_values_derived = require_success(
        maximum_values_result, "representable maximum seed/index values were rejected");
    expect(
        maximum_values_derived.ordered_components.front().coordinate ==
                maximum_values.ordered_components.front() &&
            maximum_values_derived.ordered_components.front().initialization.stream <=
                kMaximumCanonicalPcg32Stream,
        "maximum uint64 public seed/index did not survive bounded derivation");

    auto below_maximum_values = maximum_values;
    --below_maximum_values.public_seed;
    --below_maximum_values.ordered_components.front().component_index;
    const auto below_maximum_result = derive_component_seeds(below_maximum_values);
    const auto &below_maximum_derived = require_success(
        below_maximum_result, "values below the uint64 maximum were rejected");
    expect(maximum_values_derived.capture_random_key_sha256 !=
                   below_maximum_derived.capture_random_key_sha256 &&
               maximum_values_derived.ordered_components.front().initialization !=
                   below_maximum_derived.ordered_components.front().initialization,
           "uint64 maximum decimal conversion overflowed or truncated");
}

void run_tests() {
    test_exact_bmw_inventory();
    test_mutation_and_domain_separation();
    test_order_is_preserved_but_not_hashed_into_coordinates();
    test_invalid_requests_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "component-seed derivation test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
