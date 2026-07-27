#include "reference/p18_reference_seed_reader.hpp"

#include <algorithm>

namespace engine_sim_offline::reference {
namespace {

constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kCylinderCountOffset = 12;
constexpr std::size_t kChannelCountOffset = 16;
constexpr std::size_t kPairBytesOffset = 20;
constexpr std::size_t kCombustionCountOffset = 24;
constexpr std::size_t kAirNoiseCountOffset = 28;
constexpr std::size_t kJitterCountOffset = 32;
constexpr std::size_t kReservedOffset = 36;

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] bool has_magic(std::span<const std::byte> bytes) noexcept {
    constexpr char kMagic[] = "ESOSEED1";
    for (std::size_t index = 0; index < 8; ++index) {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(kMagic[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] P18ReferenceSeedDecodeError
error(P18ReferenceSeedDecodeErrorCode code, std::size_t byte_offset,
      std::size_t pair_index = kP18ReferenceNoSeedPairIndex) noexcept {
    return {code, byte_offset, pair_index};
}

[[nodiscard]] P18ReferenceSeedPair read_pair(std::span<const std::byte> bytes,
                                             std::size_t pair_index) noexcept {
    const std::size_t offset =
        kP18ReferenceSeedHeaderBytes + pair_index * kP18ReferenceSeedPairBytes;
    return {read_u64_le(bytes, offset), read_u64_le(bytes, offset + 8)};
}

} // namespace

std::array<P18ReferenceRouteSeeds, kP18ReferenceSeedRouteCount>
P18DecodedReferenceSeeds::route_seeds() const noexcept {
    return {
        P18ReferenceRouteSeeds{jitter[0], air_noise[0]},
        P18ReferenceRouteSeeds{jitter[1], air_noise[1]},
    };
}

P18ReferenceSeedDecodeResult
decode_p18_reference_seeds(std::span<const std::byte> bytes) {
    if (bytes.size() < kP18ReferenceSeedHeaderBytes) {
        return error(P18ReferenceSeedDecodeErrorCode::truncated_header, bytes.size());
    }
    if (!has_magic(bytes)) {
        return error(P18ReferenceSeedDecodeErrorCode::invalid_magic, 0);
    }
    if (read_u32_le(bytes, kVersionOffset) != 1U) {
        return error(P18ReferenceSeedDecodeErrorCode::unsupported_version,
                     kVersionOffset);
    }
    if (read_u32_le(bytes, kCylinderCountOffset) != kP18ReferenceSeedCylinderCount) {
        return error(P18ReferenceSeedDecodeErrorCode::unsupported_cylinder_count,
                     kCylinderCountOffset);
    }
    if (read_u32_le(bytes, kChannelCountOffset) != kP18ReferenceSeedChannelCount) {
        return error(P18ReferenceSeedDecodeErrorCode::unsupported_channel_count,
                     kChannelCountOffset);
    }
    if (read_u32_le(bytes, kPairBytesOffset) != kP18ReferenceSeedPairBytes) {
        return error(P18ReferenceSeedDecodeErrorCode::invalid_pair_size,
                     kPairBytesOffset);
    }
    if (read_u32_le(bytes, kCombustionCountOffset) !=
        kP18ReferenceCombustionSeedCount) {
        return error(P18ReferenceSeedDecodeErrorCode::invalid_combustion_count,
                     kCombustionCountOffset);
    }
    if (read_u32_le(bytes, kAirNoiseCountOffset) != kP18ReferenceAirNoiseSeedCount) {
        return error(P18ReferenceSeedDecodeErrorCode::invalid_air_noise_count,
                     kAirNoiseCountOffset);
    }
    if (read_u32_le(bytes, kJitterCountOffset) != kP18ReferenceJitterSeedCount) {
        return error(P18ReferenceSeedDecodeErrorCode::invalid_jitter_count,
                     kJitterCountOffset);
    }
    if (read_u32_le(bytes, kReservedOffset) != 0U) {
        return error(P18ReferenceSeedDecodeErrorCode::nonzero_reserved_header,
                     kReservedOffset);
    }
    if (bytes.size() < kP18ReferenceSeedByteCount) {
        return error(P18ReferenceSeedDecodeErrorCode::truncated_payload, bytes.size());
    }
    if (bytes.size() > kP18ReferenceSeedByteCount) {
        return error(P18ReferenceSeedDecodeErrorCode::trailing_bytes,
                     kP18ReferenceSeedByteCount);
    }

    std::array<P18ReferenceSeedPair, kP18ReferenceSeedPairCount> pairs{};
    for (std::size_t pair_index = 0; pair_index < pairs.size(); ++pair_index) {
        pairs[pair_index] = read_pair(bytes, pair_index);
        const std::size_t stream_offset =
            kP18ReferenceSeedHeaderBytes + pair_index * kP18ReferenceSeedPairBytes + 8;
        if (pairs[pair_index].stream > kP18ReferenceMaximumPcgStream) {
            return error(P18ReferenceSeedDecodeErrorCode::noncanonical_stream,
                         stream_offset, pair_index);
        }
        const auto duplicate = std::find_if(
            pairs.begin(), pairs.begin() + static_cast<std::ptrdiff_t>(pair_index),
            [&](const P18ReferenceSeedPair &candidate) {
                return candidate.stream == pairs[pair_index].stream;
            });
        if (duplicate != pairs.begin() + static_cast<std::ptrdiff_t>(pair_index)) {
            return error(P18ReferenceSeedDecodeErrorCode::duplicate_stream,
                         stream_offset, pair_index);
        }
    }

    P18DecodedReferenceSeeds decoded;
    std::copy_n(pairs.begin(), decoded.combustion.size(), decoded.combustion.begin());
    std::copy_n(pairs.begin() + decoded.combustion.size(), decoded.air_noise.size(),
                decoded.air_noise.begin());
    std::copy_n(pairs.begin() + decoded.combustion.size() + decoded.air_noise.size(),
                decoded.jitter.size(), decoded.jitter.begin());
    decoded.starter = pairs.back();
    return decoded;
}

} // namespace engine_sim_offline::reference
