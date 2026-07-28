#include "randomness/component_seed_derivation.hpp"

#include "contract/sha256_stream.hpp"

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

namespace engine_sim_offline::randomness {
namespace {

static_assert(std::numeric_limits<std::size_t>::max() <=
              std::numeric_limits<std::uint64_t>::max());

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

void append_length_prefixed_utf8(contract::detail::Sha256Stream &hasher,
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
[[nodiscard]] contract::Sha256Digest
digest_components(std::string_view domain,
                  std::array<DigestField, FieldCount> fields) noexcept {
    std::sort(fields.begin(), fields.end(),
              [](const auto &left, const auto &right) { return left.key < right.key; });

    contract::detail::Sha256Stream hasher;
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

[[nodiscard]] std::array<char, 64>
lower_hex(const contract::Sha256Digest &digest) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    std::array<char, 64> result{};
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

[[nodiscard]] std::uint64_t read_u64_be(const contract::Sha256Digest &digest,
                                        std::size_t byte_offset) noexcept {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result = (result << 8U) | digest.bytes[byte_offset + index];
    }
    return result;
}

[[nodiscard]] contract::Sha256Digest
derive_capture_random_key(std::string_view capture_scenario_id,
                          std::uint64_t public_seed) noexcept {
    std::array<char, 20> public_seed_storage{};
    const auto public_seed_text = decimal_text(public_seed, public_seed_storage);
    return digest_components(
        kCaptureRandomKeyDomain,
        std::array{
            DigestField{"seed_derivation", kCaptureComponentSeedDerivationId},
            DigestField{"public_seed", public_seed_text},
            DigestField{"capture_id", capture_scenario_id},
        });
}

[[nodiscard]] Pcg32SeedInitialization
derive_component_seed(const contract::Sha256Digest &capture_random_key,
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

} // namespace

ComponentSeedDerivationResult
derive_component_seeds(const ComponentSeedDerivationRequest &request) {
    if (!contract::is_valid_semantic_id(request.capture_scenario_id)) {
        return derivation_error(
            ComponentSeedDerivationErrorCode::invalid_capture_scenario_id,
            "capture_scenario_id",
            "capture/scenario ID must be a canonical semantic ID");
    }
    if (request.ordered_components.empty()) {
        return derivation_error(
            ComponentSeedDerivationErrorCode::empty_component_inventory,
            "ordered_components",
            "component seed derivation requires at least one component coordinate");
    }

    for (std::size_t ordinal = 0; ordinal < request.ordered_components.size();
         ++ordinal) {
        if (!contract::is_valid_semantic_id(
                request.ordered_components[ordinal].domain_id)) {
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
        derive_capture_random_key(request.capture_scenario_id, request.public_seed);
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

} // namespace engine_sim_offline::randomness
