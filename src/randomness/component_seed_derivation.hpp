#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::randomness {

inline constexpr std::string_view kCaptureComponentSeedDerivationId =
    "sha256_length_prefixed_capture_component_pcg32_v1";
inline constexpr std::string_view kCaptureRandomKeyDomain =
    "engine-sim-offline-capture-random-key-v1";
inline constexpr std::string_view kCaptureComponentSeedDomain =
    "engine-sim-offline-capture-component-seed-v1";
inline constexpr std::uint64_t kMaximumCanonicalPcg32Stream =
    std::numeric_limits<std::uint64_t>::max() >> 1U;
inline constexpr std::size_t kNoComponentSeedOrdinal =
    std::numeric_limits<std::size_t>::max();

// One stable stochastic component coordinate. Domain IDs use the same canonical
// semantic-ID grammar as the rest of the resolved model. Index zero is valid; callers
// choose and preserve the topology order supplied to the derivation request.
struct ComponentSeedCoordinate {
    std::string domain_id;
    std::uint64_t component_index = 0;

    friend bool operator==(const ComponentSeedCoordinate &,
                           const ComponentSeedCoordinate &) = default;
};

// These are inputs to the PCG32 seeding procedure, not its post-seeding state and
// increment. The stream is always canonical before `(stream << 1) | 1` encoding.
struct Pcg32SeedInitialization {
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const Pcg32SeedInitialization &,
                           const Pcg32SeedInitialization &) = default;
};

struct DerivedComponentSeed {
    ComponentSeedCoordinate coordinate;
    Pcg32SeedInitialization initialization;

    friend bool operator==(const DerivedComponentSeed &,
                           const DerivedComponentSeed &) = default;
};

struct ComponentSeedDerivationRequest {
    std::string capture_scenario_id;
    std::uint64_t public_seed = 0;
    std::vector<ComponentSeedCoordinate> ordered_components;

    friend bool operator==(const ComponentSeedDerivationRequest &,
                           const ComponentSeedDerivationRequest &) = default;
};

struct ComponentSeedDerivation {
    contract::Sha256Digest capture_random_key_sha256;
    std::vector<DerivedComponentSeed> ordered_components;

    friend bool operator==(const ComponentSeedDerivation &,
                           const ComponentSeedDerivation &) = default;
};

enum class ComponentSeedDerivationErrorCode : std::uint8_t {
    invalid_capture_scenario_id,
    empty_component_inventory,
    invalid_component_domain_id,
    duplicate_component_coordinate,
    duplicate_pcg32_stream,
};

struct ComponentSeedDerivationError {
    ComponentSeedDerivationErrorCode code =
        ComponentSeedDerivationErrorCode::invalid_capture_scenario_id;
    std::size_t component_ordinal = kNoComponentSeedOrdinal;
    std::string path;
    std::string message;

    friend bool operator==(const ComponentSeedDerivationError &,
                           const ComponentSeedDerivationError &) = default;
};

using ComponentSeedDerivationResult =
    std::variant<ComponentSeedDerivation, ComponentSeedDerivationError>;

// Implements sha256_length_prefixed_capture_component_pcg32_v1 without consulting a
// fixture or mutable process state. Every string is hashed as its UTF-8 bytes preceded
// by an unsigned big-endian u64 byte count. Named fields are sorted lexicographically;
// the returned component inventory remains in request order.
[[nodiscard]] ComponentSeedDerivationResult
derive_component_seeds(const ComponentSeedDerivationRequest &request);

} // namespace engine_sim_offline::randomness
