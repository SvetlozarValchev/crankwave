#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::contract {

struct EngineSpec;
struct PresentationCalibration;
struct RenderScenario;

inline constexpr std::string_view kPcg32GeneratorMethodId =
    "pcg32_xsh_rr_64_32_binary64_v1";
inline constexpr std::string_view kComponentSeedDerivationMethodId =
    "sha256_length_prefixed_capture_component_pcg32_v1";
inline constexpr std::string_view kCaptureRandomKeyDomain =
    "engine-sim-offline-capture-random-key-v1";
inline constexpr std::string_view kCaptureComponentSeedDomain =
    "engine-sim-offline-capture-component-seed-v1";
inline constexpr std::uint64_t kMaximumCanonicalPcg32Stream =
    std::numeric_limits<std::uint64_t>::max() >> 1U;
inline constexpr std::size_t kNoComponentSeedOrdinal =
    std::numeric_limits<std::size_t>::max();

[[nodiscard]] const MethodIdentity &pcg32_generator_method_identity();
[[nodiscard]] const MethodIdentity &component_seed_derivation_method_identity();

// Resolved request authority for every stochastic subsystem in one render. The
// namespace is intentionally independent of the scenario ID: a profile may preserve
// an accepted seed domain while changing its user-facing scenario name, but that
// choice must remain explicit and provenance-bound.
struct ResolvedRandomnessPolicy {
    ResolvedValue<std::string> seed_namespace_id;
    ResolvedValue<MethodIdentity> generator;
    ResolvedValue<MethodIdentity> derivation;

    friend bool operator==(const ResolvedRandomnessPolicy &,
                           const ResolvedRandomnessPolicy &) = default;
};

enum class RandomComponentKind : std::uint8_t {
    unspecified,
    combustion,
    presentation_jitter,
    presentation_air_noise,
    starter,
};

// One stable stochastic component coordinate. Domain IDs use the canonical
// semantic-ID grammar. Index zero is valid; derivation preserves request order while
// making every coordinate's seed independent of that order.
struct ComponentSeedCoordinate {
    std::string domain_id;
    std::uint64_t component_index = 0;

    friend bool operator==(const ComponentSeedCoordinate &,
                           const ComponentSeedCoordinate &) = default;
};

// Inputs to the admitted PCG32 seeding procedure, not its post-seeding state and
// increment. stream is canonical before `(stream << 1) | 1` encoding.
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
    std::string seed_namespace_id;
    std::uint64_t public_seed = 0;
    std::vector<ComponentSeedCoordinate> ordered_components;

    friend bool operator==(const ComponentSeedDerivationRequest &,
                           const ComponentSeedDerivationRequest &) = default;
};

struct ComponentSeedDerivation {
    Sha256Digest capture_random_key_sha256;
    std::vector<DerivedComponentSeed> ordered_components;

    friend bool operator==(const ComponentSeedDerivation &,
                           const ComponentSeedDerivation &) = default;
};

enum class ComponentSeedDerivationErrorCode : std::uint8_t {
    invalid_seed_namespace_id,
    empty_component_inventory,
    invalid_component_domain_id,
    duplicate_component_coordinate,
    duplicate_pcg32_stream,
};

struct ComponentSeedDerivationError {
    ComponentSeedDerivationErrorCode code =
        ComponentSeedDerivationErrorCode::invalid_seed_namespace_id;
    std::size_t component_ordinal = kNoComponentSeedOrdinal;
    std::string path;
    std::string message;

    friend bool operator==(const ComponentSeedDerivationError &,
                           const ComponentSeedDerivationError &) = default;
};

using ComponentSeedDerivationResult =
    std::variant<ComponentSeedDerivation, ComponentSeedDerivationError>;

// Implements sha256_length_prefixed_capture_component_pcg32_v1 without consulting a
// fixture or mutable process state. The historical `capture_id` field name remains
// part of that frozen byte grammar; its value is the explicit seed namespace.
[[nodiscard]] ComponentSeedDerivationResult
derive_component_seeds(const ComponentSeedDerivationRequest &request);

struct ComponentSeed {
    RandomComponentKind kind = RandomComponentKind::unspecified;
    std::optional<CylinderId> cylinder_id;
    std::optional<RouteId> route_id;
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const ComponentSeed &, const ComponentSeed &) = default;
};

// The provisioned randomness plan is retained separately from the resolved policy so
// a manifest records both what was requested and the exact component lanes initialized
// by the admitted executors. It does not claim a runtime draw count or cadence.
struct RandomPlan {
    MethodIdentity generator;
    std::uint64_t public_seed = 0;
    MethodIdentity derivation;
    std::vector<ComponentSeed> component_seeds;

    friend bool operator==(const RandomPlan &, const RandomPlan &) = default;
};

[[nodiscard]] ValidationReport validate(const ResolvedRandomnessPolicy &policy,
                                        const ProvenanceLedger &provenance);

using RandomPlanCompilationResult = std::variant<RandomPlan, ValidationReport>;

// Compiles the only executable random plan from the admitted policy and stochastic
// consumers. Component order is ascending stable cylinder ID, then ascending stable
// route ID for air noise, then ascending stable route ID for jitter. Each derivation
// index is its nonzero stable owner ID minus one, so container reordering or insertion
// does not rekey an existing owner. This plan is the sole executable authority for
// combustion and presentation random-stream initialization; engine definitions do
// not cache scenario-derived seeds.
[[nodiscard]] RandomPlanCompilationResult
compile_random_plan(const ResolvedRandomnessPolicy &policy, const EngineSpec &engine,
                    const PresentationCalibration &presentation,
                    const RenderScenario &scenario);

} // namespace engine_sim_offline::contract
