#include "engine_sim_offline/contract/randomness.hpp"

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

using namespace engine_sim_offline;
using namespace engine_sim_offline::contract;

constexpr std::uint64_t kBmwPublicSeed = UINT64_C(0x00c0ffee);

struct ExpectedSeed {
    std::string_view domain_id;
    std::uint64_t component_index;
    std::uint64_t initial_state;
    std::uint64_t stream;
};

constexpr std::array kBmwExpectedSeeds{
    ExpectedSeed{"combustion", 0, UINT64_C(0x6ba3d060370e05fa),
                 UINT64_C(0x3e13b1e68ef2f790)},
    ExpectedSeed{"combustion", 1, UINT64_C(0xb1ab9b6c6217bdf3),
                 UINT64_C(0x7681d4f9a6c78e3f)},
    ExpectedSeed{"combustion", 2, UINT64_C(0x0c2447917cd77f40),
                 UINT64_C(0x4c09e08d851104f5)},
    ExpectedSeed{"combustion", 3, UINT64_C(0xfc83080b6c8b1a98),
                 UINT64_C(0x686f68f85fd7d169)},
    ExpectedSeed{"combustion", 4, UINT64_C(0x1f0c63f1d677237b),
                 UINT64_C(0x3507d87731683125)},
    ExpectedSeed{"combustion", 5, UINT64_C(0xad811f42fb6dafa3),
                 UINT64_C(0x50900fae5afa96cf)},
    ExpectedSeed{"synth_air_noise", 0, UINT64_C(0x75bc579d4c90a640),
                 UINT64_C(0x7e4ef6200e7c70c1)},
    ExpectedSeed{"synth_air_noise", 1, UINT64_C(0x208e57f73615bd95),
                 UINT64_C(0x786d92e584c43b78)},
    ExpectedSeed{"synth_jitter", 0, UINT64_C(0x9e2b91cd0dc51cfc),
                 UINT64_C(0x1ae6ee3019603abb)},
    ExpectedSeed{"synth_jitter", 1, UINT64_C(0xdb7540a0c8b54d74),
                 UINT64_C(0x41ddcdeb066bf214)},
    ExpectedSeed{"starter", 0, UINT64_C(0xb4ea1fd6d9786b65),
                 UINT64_C(0x35db133a627daacd)},
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
    expect(digest_hex(derived.capture_random_key_sha256) ==
               "272121adec1fe448dd149b05990e477a816a7e6191352854d7a1dafd92183f5d",
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
