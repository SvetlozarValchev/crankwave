#include "engine_sim_offline/contract/randomness.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "sha256_stream.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract {
namespace {

inline constexpr std::string_view kPcg32GeneratorMethodDescriptor =
    "engine-sim-offline.randomness-method-configuration.v1\n"
    "method=pcg32_xsh_rr_64_32_binary64_v1\n"
    "version=1\n"
    "operation=deterministic-pcg-xsh-rr-64-32-with-binary64-draws\n"
    "initial_state_input=unsigned-64-bit-integer\n"
    "stream_selector_input=unsigned-64-bit-integer-in-range-0-through-"
    "9223372036854775807\n"
    "state_arithmetic=unsigned-64-bit-modulo-2^64\n"
    "state_multiplier=6364136223846793005\n"
    "increment=(stream_selector-left-shift-1)-bitwise-or-1\n"
    "seed_step_1=state-positive-zero;perform-one-next-u32-and-discard\n"
    "seed_step_2=state-plus-equals-initial_state-modulo-2^64\n"
    "seed_step_3=perform-one-next-u32-and-discard\n"
    "next_u32_old_state=state-before-update\n"
    "next_u32_state=old_state-times-multiplier-plus-increment-modulo-2^64\n"
    "next_u32_xorshifted=uint32(((old_state-right-shift-18)-xor-old_state)-"
    "right-shift-27)\n"
    "next_u32_rotation=uint32(old_state-right-shift-59)\n"
    "next_u32_output=uint32-rotate-right-xorshifted-by-rotation-modulo-32\n"
    "uniform_binary64_draw_count=2-next-u32-draws-in-order\n"
    "uniform_binary64_integer=((uint64(draw0-right-shift-5))-left-shift-26)-"
    "bitwise-or-uint64(draw1-right-shift-6)\n"
    "uniform_binary64_output=binary64(integer)-times-binary64-2^-53\n"
    "uniform_binary64_range=[0,1)-with-step-2^-53\n"
    "uniform_signed_binary64_output=binary64-2-times-uniform_binary64-minus-"
    "binary64-1-in-written-order\n"
    "uniform_signed_binary64_range=[-1,1-2^-52]-with-step-2^-52\n"
    "binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-"
    "no-daz\n";

inline constexpr std::string_view kComponentSeedDerivationMethodDescriptor =
    "engine-sim-offline.randomness-method-configuration.v1\n"
    "method=sha256_length_prefixed_capture_component_pcg32_v1\n"
    "version=1\n"
    "operation=domain-separated-seed-namespace-and-component-pcg32-"
    "initialization\n"
    "semantic_id=nonempty-ascii;first-byte-lowercase-a-z-or-digit;remaining-"
    "bytes-lowercase-a-z-digit-dot-underscore-hyphen-or-slash\n"
    "string_encoding=utf8-compatible-input-bytes\n"
    "length_prefix=unsigned-u64-big-endian-byte-count\n"
    "hash_item=length-prefix-then-exact-string-bytes\n"
    "named_field_order=ascending-raw-ascii-field-key-bytes\n"
    "uint64_text=base-10-ascii-with-no-leading-zero-except-zero\n"
    "sha256=project-owned-fips-180-4-sha-256\n"
    "seed_derivation_field_value=sha256_length_prefixed_capture_component_"
    "pcg32_v1\n"
    "capture_random_key_domain=engine-sim-offline-capture-random-key-v1\n"
    "capture_random_key_items=domain-then-fields-capture_id-public_seed-seed_"
    "derivation\n"
    "capture_id_value=resolved-seed-namespace-id\n"
    "capture_random_key_output=complete-32-byte-sha256-digest\n"
    "component_seed_domain=engine-sim-offline-capture-component-seed-v1\n"
    "capture_random_key_sha256_text=64-lowercase-hex-digits\n"
    "component_seed_items=domain-then-fields-capture_random_key_sha256-"
    "component_domain-component_index\n"
    "component_initial_state=component-digest-bytes-0-through-7-read-u64-"
    "big-endian\n"
    "component_stream=(component-digest-bytes-8-through-15-read-u64-big-"
    "endian)-bitwise-and-0x7fffffffffffffff\n"
    "coordinate_identity=component-domain-plus-component-index\n"
    "coordinate_order=caller-order-preserved-and-not-hashed-into-individual-"
    "coordinate\n"
    "empty_inventory=rejected\n"
    "duplicate_coordinate=rejected\n"
    "duplicate_derived_stream-selector=rejected\n";

[[nodiscard]] consteval bool
canonical_lf_descriptor(std::string_view descriptor) noexcept {
    if (descriptor.empty() || descriptor.back() != '\n') {
        return false;
    }
    for (const char character : descriptor) {
        if (character == '\r' || character == '\0') {
            return false;
        }
    }
    return true;
}

static_assert(canonical_lf_descriptor(kPcg32GeneratorMethodDescriptor));
static_assert(canonical_lf_descriptor(kComponentSeedDerivationMethodDescriptor));

[[nodiscard]] Sha256Digest descriptor_digest(std::string_view descriptor) noexcept {
    return sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

struct DigestField {
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] ComponentSeedDerivationError
derivation_error(ComponentSeedDerivationErrorCode code, std::string path,
                 std::string message,
                 std::size_t component_ordinal = kNoComponentSeedOrdinal) {
    return {code, component_ordinal, std::move(path), std::move(message)};
}

void append_length_prefixed_utf8(detail::Sha256Stream &hasher,
                                 std::string_view value) noexcept {
    const auto byte_count = static_cast<std::uint64_t>(value.size());
    std::array<std::byte, 8> encoded_length{};
    for (std::size_t index = 0; index < encoded_length.size(); ++index) {
        encoded_length[index] = static_cast<std::byte>(
            byte_count >> (56U - static_cast<unsigned>(index) * 8U));
    }
    hasher.update(encoded_length);
    hasher.update(std::as_bytes(std::span{value.data(), value.size()}));
}

template <std::size_t FieldCount>
[[nodiscard]] Sha256Digest
digest_components(std::string_view domain,
                  std::array<DigestField, FieldCount> fields) noexcept {
    std::sort(fields.begin(), fields.end(),
              [](const auto &left, const auto &right) { return left.key < right.key; });

    detail::Sha256Stream hasher;
    append_length_prefixed_utf8(hasher, domain);
    for (const auto &field : fields) {
        append_length_prefixed_utf8(hasher, field.key);
        append_length_prefixed_utf8(hasher, field.value);
    }
    return hasher.finish();
}

template <class Unsigned>
[[nodiscard]] std::string_view decimal_text(Unsigned value,
                                            std::array<char, 20> &storage) noexcept {
    const auto converted =
        std::to_chars(storage.data(), storage.data() + storage.size(), value, 10);
    if (converted.ec != std::errc{}) {
        return {};
    }
    return {storage.data(), static_cast<std::size_t>(converted.ptr - storage.data())};
}

[[nodiscard]] std::array<char, 64> lower_hex(const Sha256Digest &digest) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    std::array<char, 64> result{};
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

[[nodiscard]] std::uint64_t read_u64_be(const Sha256Digest &digest,
                                        std::size_t byte_offset) noexcept {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result = (result << 8U) | digest.bytes[byte_offset + index];
    }
    return result;
}

[[nodiscard]] Sha256Digest
derive_capture_random_key(std::string_view seed_namespace_id,
                          std::uint64_t public_seed) noexcept {
    std::array<char, 20> public_seed_storage{};
    const auto public_seed_text = decimal_text(public_seed, public_seed_storage);
    return digest_components(
        kCaptureRandomKeyDomain,
        std::array{
            DigestField{"seed_derivation", kComponentSeedDerivationMethodId},
            DigestField{"public_seed", public_seed_text},
            DigestField{"capture_id", seed_namespace_id},
        });
}

[[nodiscard]] Pcg32SeedInitialization
derive_component_seed(const Sha256Digest &capture_random_key,
                      const ComponentSeedCoordinate &coordinate) noexcept {
    const auto random_key_hex = lower_hex(capture_random_key);
    std::array<char, 20> component_index_storage{};
    const auto component_index_text =
        decimal_text(coordinate.component_index, component_index_storage);
    const auto digest = digest_components(
        kCaptureComponentSeedDomain,
        std::array{
            DigestField{"component_index", component_index_text},
            DigestField{"component_domain", coordinate.domain_id},
            DigestField{"capture_random_key_sha256",
                        std::string_view{random_key_hex.data(), random_key_hex.size()}},
        });
    return {
        read_u64_be(digest, 0),
        read_u64_be(digest, 8) & kMaximumCanonicalPcg32Stream,
    };
}

[[nodiscard]] std::string component_path(std::size_t ordinal,
                                         std::string_view suffix = {}) {
    auto path = "ordered_components[" + std::to_string(ordinal) + "]";
    if (!suffix.empty()) {
        path += '.';
        path += suffix;
    }
    return path;
}

struct PlannedComponent {
    RandomComponentKind kind = RandomComponentKind::unspecified;
    std::optional<CylinderId> cylinder_id;
    std::optional<RouteId> route_id;
    ComponentSeedCoordinate coordinate;
};

void append_derivation_error(ValidationReport &report,
                             const ComponentSeedDerivationError &error) {
    auto code = ContractIssueCode::inconsistent_semantics;
    switch (error.code) {
    case ComponentSeedDerivationErrorCode::invalid_seed_namespace_id:
    case ComponentSeedDerivationErrorCode::invalid_component_domain_id:
        code = ContractIssueCode::invalid_value;
        break;
    case ComponentSeedDerivationErrorCode::empty_component_inventory:
        code = ContractIssueCode::missing_value;
        break;
    case ComponentSeedDerivationErrorCode::duplicate_component_coordinate:
    case ComponentSeedDerivationErrorCode::duplicate_pcg32_stream:
        code = ContractIssueCode::duplicate_identity;
        break;
    }
    report.add(code, "derivation." + error.path, error.message);
}

} // namespace

const MethodIdentity &pcg32_generator_method_identity() {
    static const MethodIdentity identity{
        std::string{kPcg32GeneratorMethodId},
        1,
        descriptor_digest(kPcg32GeneratorMethodDescriptor),
    };
    return identity;
}

const MethodIdentity &component_seed_derivation_method_identity() {
    static const MethodIdentity identity{
        std::string{kComponentSeedDerivationMethodId},
        1,
        descriptor_digest(kComponentSeedDerivationMethodDescriptor),
    };
    return identity;
}

ValidationReport validate(const ResolvedRandomnessPolicy &policy,
                          const ProvenanceLedger &provenance) {
    ValidationReport report;
    detail::validate_resolved_value(report, policy.seed_namespace_id, provenance,
                                    "randomness.seed_namespace_id");
    detail::validate_resolved_value(report, policy.generator, provenance,
                                    "randomness.generator");
    detail::validate_resolved_value(report, policy.derivation, provenance,
                                    "randomness.derivation");

    detail::require(report, is_valid_semantic_id(policy.seed_namespace_id.value),
                    ContractIssueCode::invalid_value,
                    "randomness.seed_namespace_id.value",
                    "seed namespace ID must be canonical");
    detail::append_prefixed(report, validate(policy.generator.value),
                            "randomness.generator.value");
    detail::append_prefixed(report, validate(policy.derivation.value),
                            "randomness.derivation.value");
    detail::require(report, policy.generator.value == pcg32_generator_method_identity(),
                    ContractIssueCode::unsupported_value, "randomness.generator.value",
                    "generator must exactly match the implemented PCG32 method");
    detail::require(
        report, policy.derivation.value == component_seed_derivation_method_identity(),
        ContractIssueCode::unsupported_value, "randomness.derivation.value",
        "derivation must exactly match the implemented component-seed method");
    return report;
}

ComponentSeedDerivationResult
derive_component_seeds(const ComponentSeedDerivationRequest &request) {
    if (!is_valid_semantic_id(request.seed_namespace_id)) {
        return derivation_error(
            ComponentSeedDerivationErrorCode::invalid_seed_namespace_id,
            "seed_namespace_id", "seed namespace ID must be a canonical semantic ID");
    }
    if (request.ordered_components.empty()) {
        return derivation_error(
            ComponentSeedDerivationErrorCode::empty_component_inventory,
            "ordered_components",
            "component seed derivation requires at least one component coordinate");
    }

    for (std::size_t ordinal = 0; ordinal < request.ordered_components.size();
         ++ordinal) {
        if (!is_valid_semantic_id(request.ordered_components[ordinal].domain_id)) {
            return derivation_error(
                ComponentSeedDerivationErrorCode::invalid_component_domain_id,
                component_path(ordinal, "domain_id"),
                "component domain must be a canonical semantic ID", ordinal);
        }
    }

    std::vector<std::size_t> sorted_ordinals(request.ordered_components.size());
    std::iota(sorted_ordinals.begin(), sorted_ordinals.end(), std::size_t{0});
    std::sort(
        sorted_ordinals.begin(), sorted_ordinals.end(), [&](auto left, auto right) {
            const auto &left_coordinate = request.ordered_components[left];
            const auto &right_coordinate = request.ordered_components[right];
            if (left_coordinate.domain_id != right_coordinate.domain_id) {
                return left_coordinate.domain_id < right_coordinate.domain_id;
            }
            if (left_coordinate.component_index != right_coordinate.component_index) {
                return left_coordinate.component_index <
                       right_coordinate.component_index;
            }
            return left < right;
        });
    for (std::size_t index = 1; index < sorted_ordinals.size(); ++index) {
        const auto prior = sorted_ordinals[index - 1];
        const auto current = sorted_ordinals[index];
        if (request.ordered_components[prior] == request.ordered_components[current]) {
            return derivation_error(
                ComponentSeedDerivationErrorCode::duplicate_component_coordinate,
                component_path(current),
                "component domain/index coordinate is duplicated", current);
        }
    }

    ComponentSeedDerivation result;
    result.capture_random_key_sha256 =
        derive_capture_random_key(request.seed_namespace_id, request.public_seed);
    result.ordered_components.reserve(request.ordered_components.size());
    for (std::size_t ordinal = 0; ordinal < request.ordered_components.size();
         ++ordinal) {
        const auto &coordinate = request.ordered_components[ordinal];
        const auto initialization =
            derive_component_seed(result.capture_random_key_sha256, coordinate);
        const auto duplicate_stream = std::find_if(
            result.ordered_components.begin(), result.ordered_components.end(),
            [&](const auto &prior) {
                return prior.initialization.stream == initialization.stream;
            });
        if (duplicate_stream != result.ordered_components.end()) {
            return derivation_error(
                ComponentSeedDerivationErrorCode::duplicate_pcg32_stream,
                component_path(ordinal),
                "derived PCG32 stream aliases an earlier component coordinate",
                ordinal);
        }
        result.ordered_components.push_back({coordinate, initialization});
    }
    return result;
}

RandomPlanCompilationResult
compile_random_plan(const ResolvedRandomnessPolicy &policy, const EngineSpec &engine,
                    const PresentationCalibration &presentation,
                    const RenderScenario &scenario) {
    ValidationReport report;
    detail::require(report, is_valid_semantic_id(policy.seed_namespace_id.value),
                    ContractIssueCode::invalid_value, "seed_namespace_id",
                    "seed namespace ID must be canonical");
    detail::require(report, policy.generator.value == pcg32_generator_method_identity(),
                    ContractIssueCode::unsupported_value, "generator",
                    "random plan requires the exact implemented PCG32 generator");
    detail::require(
        report, policy.derivation.value == component_seed_derivation_method_identity(),
        ContractIssueCode::unsupported_value, "derivation",
        "random plan requires the exact implemented component-seed derivation");
    detail::require(report, !engine.physics_profile.valueless_by_exception(),
                    ContractIssueCode::unsupported_value, "engine.physics_profile",
                    "random-plan compilation requires a populated engine profile");

    if (!report.ok()) {
        return report;
    }
    detail::require(
        report,
        presentation.routes.size() <=
            (std::numeric_limits<std::size_t>::max() - engine.cylinders.size()) / 2U,
        ContractIssueCode::unsupported_value, "component_inventory",
        "provisioned stochastic component inventory size is not representable");
    if (!report.ok()) {
        return report;
    }
    std::vector<const CylinderSpec *> cylinders;
    cylinders.reserve(engine.cylinders.size());
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        detail::require(report, cylinder.id.valid(), ContractIssueCode::invalid_value,
                        "engine.cylinders[" + std::to_string(index) + "].id",
                        "random component owner must have a stable nonzero ID");
        cylinders.push_back(&cylinder);
    }
    std::ranges::sort(cylinders, {},
                      [](const auto *cylinder) { return cylinder->id.value; });

    std::vector<const RoutePresentation *> routes;
    routes.reserve(presentation.routes.size());
    for (std::size_t index = 0; index < presentation.routes.size(); ++index) {
        const auto &route = presentation.routes[index];
        detail::require(report, route.route_id.valid(),
                        ContractIssueCode::invalid_value,
                        "presentation.routes[" + std::to_string(index) + "].route_id",
                        "random component owner must have a stable nonzero ID");
        const auto engine_route =
            std::ranges::find(engine.routes, route.route_id, &RouteSpec::id);
        detail::require(
            report, engine_route != engine.routes.end(),
            ContractIssueCode::dangling_reference,
            "presentation.routes[" + std::to_string(index) + "].route_id",
            "random component owner must resolve to an engine source route");
        if (engine_route != engine.routes.end() &&
            engine_route->kind.value == SourceRouteKind::exhaust_outlet) {
            routes.push_back(&route);
        }
    }
    std::ranges::sort(routes, {},
                      [](const auto *route) { return route->route_id.value; });
    if (!report.ok()) {
        return report;
    }

    std::vector<PlannedComponent> components;
    components.reserve(cylinders.size() + routes.size() * 2U);
    for (const auto *cylinder : cylinders) {
        components.push_back({
            RandomComponentKind::combustion,
            cylinder->id,
            std::nullopt,
            {"combustion", static_cast<std::uint64_t>(cylinder->id.value - 1U)},
        });
    }
    for (const auto *route : routes) {
        components.push_back({
            RandomComponentKind::presentation_air_noise,
            std::nullopt,
            route->route_id,
            {"synth_air_noise", static_cast<std::uint64_t>(route->route_id.value - 1U)},
        });
    }
    for (const auto *route : routes) {
        components.push_back({
            RandomComponentKind::presentation_jitter,
            std::nullopt,
            route->route_id,
            {"synth_jitter", static_cast<std::uint64_t>(route->route_id.value - 1U)},
        });
    }

    RandomPlan plan{
        policy.generator.value,
        scenario.public_seed.value,
        policy.derivation.value,
        {},
    };
    ComponentSeedDerivationRequest request;
    request.seed_namespace_id = policy.seed_namespace_id.value;
    request.public_seed = scenario.public_seed.value;
    request.ordered_components.reserve(components.size());
    for (const auto &component : components) {
        request.ordered_components.push_back(component.coordinate);
    }

    auto derivation_result = derive_component_seeds(request);
    if (const auto *error =
            std::get_if<ComponentSeedDerivationError>(&derivation_result)) {
        append_derivation_error(report, *error);
        return report;
    }
    const auto &derived = std::get<ComponentSeedDerivation>(derivation_result);
    plan.component_seeds.reserve(derived.ordered_components.size());
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto &component = components[index];
        const auto &initialization = derived.ordered_components[index].initialization;
        plan.component_seeds.push_back({
            component.kind,
            component.cylinder_id,
            component.route_id,
            initialization.initial_state,
            initialization.stream,
        });
    }
    return plan;
}

} // namespace engine_sim_offline::contract
