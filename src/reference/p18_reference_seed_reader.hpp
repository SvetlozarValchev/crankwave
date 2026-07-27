#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>

namespace engine_sim_offline::reference {

inline constexpr std::size_t kP18ReferenceSeedHeaderBytes = 40;
inline constexpr std::size_t kP18ReferenceSeedPairBytes = 16;
inline constexpr std::uint32_t kP18ReferenceSeedCylinderCount = 6;
inline constexpr std::uint32_t kP18ReferenceSeedChannelCount = 2;
inline constexpr std::size_t kP18ReferenceSeedRouteCount =
    kP18ReferenceSeedChannelCount;
inline constexpr std::uint32_t kP18ReferenceCombustionSeedCount = 6;
inline constexpr std::uint32_t kP18ReferenceAirNoiseSeedCount = 2;
inline constexpr std::uint32_t kP18ReferenceJitterSeedCount = 2;
inline constexpr std::uint32_t kP18ReferenceStarterSeedCount = 1;
inline constexpr std::size_t kP18ReferenceSeedPairCount =
    kP18ReferenceCombustionSeedCount + kP18ReferenceAirNoiseSeedCount +
    kP18ReferenceJitterSeedCount + kP18ReferenceStarterSeedCount;
inline constexpr std::size_t kP18ReferenceSeedByteCount =
    kP18ReferenceSeedHeaderBytes +
    kP18ReferenceSeedPairCount * kP18ReferenceSeedPairBytes;
inline constexpr std::uint64_t kP18ReferenceMaximumPcgStream =
    std::numeric_limits<std::uint64_t>::max() >> 1U;
inline constexpr std::size_t kP18ReferenceNoSeedPairIndex =
    std::numeric_limits<std::size_t>::max();

enum class P18ReferenceSeedDecodeErrorCode : std::uint8_t {
    truncated_header,
    invalid_magic,
    unsupported_version,
    unsupported_cylinder_count,
    unsupported_channel_count,
    invalid_pair_size,
    invalid_combustion_count,
    invalid_air_noise_count,
    invalid_jitter_count,
    nonzero_reserved_header,
    truncated_payload,
    trailing_bytes,
    noncanonical_stream,
    duplicate_stream,
};

struct P18ReferenceSeedDecodeError {
    P18ReferenceSeedDecodeErrorCode code =
        P18ReferenceSeedDecodeErrorCode::truncated_header;
    std::size_t byte_offset = 0;
    std::size_t pair_index = kP18ReferenceNoSeedPairIndex;

    friend bool operator==(const P18ReferenceSeedDecodeError &,
                           const P18ReferenceSeedDecodeError &) = default;
};

struct P18ReferenceSeedPair {
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const P18ReferenceSeedPair &,
                           const P18ReferenceSeedPair &) = default;
};

struct P18ReferenceRouteSeeds {
    P18ReferenceSeedPair jitter;
    P18ReferenceSeedPair air_noise;

    friend bool operator==(const P18ReferenceRouteSeeds &,
                           const P18ReferenceRouteSeeds &) = default;
};

struct P18DecodedReferenceSeeds {
    std::array<P18ReferenceSeedPair, kP18ReferenceCombustionSeedCount> combustion{};
    std::array<P18ReferenceSeedPair, kP18ReferenceAirNoiseSeedCount> air_noise{};
    std::array<P18ReferenceSeedPair, kP18ReferenceJitterSeedCount> jitter{};
    P18ReferenceSeedPair starter{};

    // Repackages the fixture's air-then-jitter inventory into the source stage's
    // route-owned jitter-then-air shape without importing presentation types.
    [[nodiscard]] std::array<P18ReferenceRouteSeeds, kP18ReferenceSeedRouteCount>
    route_seeds() const noexcept;

    friend bool operator==(const P18DecodedReferenceSeeds &,
                           const P18DecodedReferenceSeeds &) = default;
};

using P18ReferenceSeedDecodeResult =
    std::variant<P18DecodedReferenceSeeds, P18ReferenceSeedDecodeError>;

// Strict, path-free decoder for the frozen ESOSEED1 v1 P1.8 inventory shape.
// It retains every lineage pair; route_seeds() exposes only the four generators
// executed by the current presentation stage. Input storage is borrowed only for
// this call.
[[nodiscard]] P18ReferenceSeedDecodeResult
decode_p18_reference_seeds(std::span<const std::byte> bytes);

} // namespace engine_sim_offline::reference
