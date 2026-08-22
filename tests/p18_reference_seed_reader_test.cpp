#include "reference/p18_reference_seed_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace crankwave::reference;
using Bytes = std::vector<std::byte>;

constexpr std::array<P18ReferenceSeedPair, kP18ReferenceSeedPairCount> kPinnedPairs{
    P18ReferenceSeedPair{UINT64_C(0x6ba3d060370e05fa), UINT64_C(0x3e13b1e68ef2f790)},
    P18ReferenceSeedPair{UINT64_C(0xb1ab9b6c6217bdf3), UINT64_C(0x7681d4f9a6c78e3f)},
    P18ReferenceSeedPair{UINT64_C(0x0c2447917cd77f40), UINT64_C(0x4c09e08d851104f5)},
    P18ReferenceSeedPair{UINT64_C(0xfc83080b6c8b1a98), UINT64_C(0x686f68f85fd7d169)},
    P18ReferenceSeedPair{UINT64_C(0x1f0c63f1d677237b), UINT64_C(0x3507d87731683125)},
    P18ReferenceSeedPair{UINT64_C(0xad811f42fb6dafa3), UINT64_C(0x50900fae5afa96cf)},
    P18ReferenceSeedPair{UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
    P18ReferenceSeedPair{UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
    P18ReferenceSeedPair{UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
    P18ReferenceSeedPair{UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
    P18ReferenceSeedPair{UINT64_C(0xb4ea1fd6d9786b65), UINT64_C(0x35db133a627daacd)},
};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void write_u32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] =
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)};
    }
}

void write_u64(Bytes &bytes, std::size_t offset, std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        bytes[offset + shift / 8U] =
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)};
    }
}

Bytes canonical_seed_bytes() {
    Bytes bytes(kP18ReferenceSeedByteCount);
    constexpr char magic[] = "ESOSEED1";
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = std::byte{static_cast<unsigned char>(magic[index])};
    }
    write_u32(bytes, 8, 1);
    write_u32(bytes, 12, kP18ReferenceSeedCylinderCount);
    write_u32(bytes, 16, kP18ReferenceSeedChannelCount);
    write_u32(bytes, 20, static_cast<std::uint32_t>(kP18ReferenceSeedPairBytes));
    write_u32(bytes, 24, kP18ReferenceCombustionSeedCount);
    write_u32(bytes, 28, kP18ReferenceAirNoiseSeedCount);
    write_u32(bytes, 32, kP18ReferenceJitterSeedCount);
    write_u32(bytes, 36, 0);
    for (std::size_t pair_index = 0; pair_index < kPinnedPairs.size(); ++pair_index) {
        const std::size_t offset =
            kP18ReferenceSeedHeaderBytes + pair_index * kP18ReferenceSeedPairBytes;
        write_u64(bytes, offset, kPinnedPairs[pair_index].initial_state);
        write_u64(bytes, offset + 8, kPinnedPairs[pair_index].stream);
    }
    return bytes;
}

const P18DecodedReferenceSeeds &
expect_decoded(const P18ReferenceSeedDecodeResult &result, const char *message) {
    const auto *decoded = std::get_if<P18DecodedReferenceSeeds>(&result);
    expect(decoded != nullptr, message);
    return *decoded;
}

void expect_error(const Bytes &bytes, P18ReferenceSeedDecodeErrorCode expected_code,
                  std::size_t expected_offset, const char *message,
                  std::size_t expected_pair = kP18ReferenceNoSeedPairIndex) {
    const auto result = decode_p18_reference_seeds(bytes);
    const auto *error = std::get_if<P18ReferenceSeedDecodeError>(&result);
    expect(error != nullptr && error->code == expected_code &&
               error->byte_offset == expected_offset &&
               error->pair_index == expected_pair,
           message);
}

void test_exact_decode_and_owned_inventory(Bytes &bytes) {
    auto result = decode_p18_reference_seeds(bytes);
    const auto &decoded =
        expect_decoded(result, "canonical component seeds were rejected");
    expect(decoded.combustion.front() == kPinnedPairs[0] &&
               decoded.combustion.back() == kPinnedPairs[5] &&
               decoded.air_noise[0] == kPinnedPairs[6] &&
               decoded.air_noise[1] == kPinnedPairs[7] &&
               decoded.jitter[0] == kPinnedPairs[8] &&
               decoded.jitter[1] == kPinnedPairs[9] &&
               decoded.starter == kPinnedPairs[10],
           "component seed domain ordering changed");

    const auto routes = decoded.route_seeds();
    expect(routes[0] == P18ReferenceRouteSeeds{kPinnedPairs[8], kPinnedPairs[6]} &&
               routes[1] == P18ReferenceRouteSeeds{kPinnedPairs[9], kPinnedPairs[7]},
           "route seed adapter did not produce jitter-then-air route order");

    write_u64(bytes, kP18ReferenceSeedHeaderBytes, 0);
    expect(std::get<P18DecodedReferenceSeeds>(result).combustion.front() ==
               kPinnedPairs.front(),
           "seed decoder retained borrowed input storage");
    write_u64(bytes, kP18ReferenceSeedHeaderBytes, kPinnedPairs.front().initial_state);
}

void test_header_rejections(Bytes &bytes) {
    Bytes short_header(bytes.begin(),
                       bytes.begin() + kP18ReferenceSeedHeaderBytes - 1U);
    expect_error(short_header, P18ReferenceSeedDecodeErrorCode::truncated_header,
                 short_header.size(), "truncated seed header was accepted");

    const auto original_magic = bytes[0];
    bytes[0] = std::byte{'X'};
    expect_error(bytes, P18ReferenceSeedDecodeErrorCode::invalid_magic, 0,
                 "invalid seed magic was accepted");
    bytes[0] = original_magic;

    struct HeaderMutation {
        std::size_t offset;
        std::uint32_t invalid_value;
        std::uint32_t valid_value;
        P18ReferenceSeedDecodeErrorCode code;
    };
    constexpr HeaderMutation mutations[]{
        {8, 2, 1, P18ReferenceSeedDecodeErrorCode::unsupported_version},
        {12, 5, kP18ReferenceSeedCylinderCount,
         P18ReferenceSeedDecodeErrorCode::unsupported_cylinder_count},
        {16, 3, kP18ReferenceSeedChannelCount,
         P18ReferenceSeedDecodeErrorCode::unsupported_channel_count},
        {20, 8, kP18ReferenceSeedPairBytes,
         P18ReferenceSeedDecodeErrorCode::invalid_pair_size},
        {24, 5, kP18ReferenceCombustionSeedCount,
         P18ReferenceSeedDecodeErrorCode::invalid_combustion_count},
        {28, 1, kP18ReferenceAirNoiseSeedCount,
         P18ReferenceSeedDecodeErrorCode::invalid_air_noise_count},
        {32, 1, kP18ReferenceJitterSeedCount,
         P18ReferenceSeedDecodeErrorCode::invalid_jitter_count},
        {36, 1, 0, P18ReferenceSeedDecodeErrorCode::nonzero_reserved_header},
    };
    for (const auto &mutation : mutations) {
        write_u32(bytes, mutation.offset, mutation.invalid_value);
        expect_error(bytes, mutation.code, mutation.offset,
                     "invalid exact seed header field was accepted");
        write_u32(bytes, mutation.offset, mutation.valid_value);
    }
}

void test_payload_and_stream_rejections(Bytes &bytes) {
    const std::byte final_byte = bytes.back();
    bytes.pop_back();
    expect_error(bytes, P18ReferenceSeedDecodeErrorCode::truncated_payload,
                 bytes.size(), "truncated exact seed payload was accepted");
    bytes.push_back(final_byte);

    bytes.push_back(std::byte{0});
    expect_error(bytes, P18ReferenceSeedDecodeErrorCode::trailing_bytes,
                 kP18ReferenceSeedByteCount,
                 "trailing bytes after exact seed payload were accepted");
    bytes.pop_back();

    constexpr std::size_t second_pair_stream_offset =
        kP18ReferenceSeedHeaderBytes + kP18ReferenceSeedPairBytes + 8;
    write_u64(bytes, second_pair_stream_offset,
              kP18ReferenceMaximumPcgStream + UINT64_C(1));
    expect_error(bytes, P18ReferenceSeedDecodeErrorCode::noncanonical_stream,
                 second_pair_stream_offset,
                 "noncanonical PCG stream selector was accepted", 1);
    write_u64(bytes, second_pair_stream_offset, kPinnedPairs[1].stream);

    write_u64(bytes, second_pair_stream_offset, kPinnedPairs[0].stream);
    expect_error(bytes, P18ReferenceSeedDecodeErrorCode::duplicate_stream,
                 second_pair_stream_offset,
                 "duplicate component stream selector was accepted", 1);
    write_u64(bytes, second_pair_stream_offset, kPinnedPairs[1].stream);
}

Bytes read_exact_fixture(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(input.is_open(), "could not open pinned component-seeds.bin");
    const auto size = input.tellg();
    expect(size == static_cast<std::streamoff>(kP18ReferenceSeedByteCount),
           "pinned component-seeds.bin has unexpected size");
    input.seekg(0);
    Bytes bytes(kP18ReferenceSeedByteCount);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "pinned component-seeds.bin read was incomplete");
    return bytes;
}

void test_pinned_fixture(const std::string &path) {
    const auto bytes = read_exact_fixture(path);
    const auto result = decode_p18_reference_seeds(bytes);
    const auto &decoded =
        expect_decoded(result, "pinned component-seeds.bin failed strict decode");
    expect(decoded.air_noise == std::array{kPinnedPairs[6], kPinnedPairs[7]} &&
               decoded.jitter == std::array{kPinnedPairs[8], kPinnedPairs[9]},
           "pinned presentation seed values or fixture domain order changed");
    expect(decoded.route_seeds() ==
               std::array{
                   P18ReferenceRouteSeeds{kPinnedPairs[8], kPinnedPairs[6]},
                   P18ReferenceRouteSeeds{kPinnedPairs[9], kPinnedPairs[7]},
               },
           "pinned route seed ownership changed");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2,
               "usage: p18_reference_seed_reader_test <component-seeds.bin>");
        auto synthetic = canonical_seed_bytes();
        test_exact_decode_and_owned_inventory(synthetic);
        test_header_rejections(synthetic);
        test_payload_and_stream_rejections(synthetic);
        test_pinned_fixture(argv[1]);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
