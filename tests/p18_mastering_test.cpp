#include "artifacts/p18_audition_wav_encoder.hpp"
#include "engine_sim_offline/artifacts/wav_encoder.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "presentation/p18_mastering.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;
using namespace engine_sim_offline::presentation;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

std::uint8_t hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    throw std::runtime_error{"invalid test hex digit"};
}

std::vector<std::byte> bytes_from_hex(std::string_view text) {
    expect(text.size() % 2 == 0, "test hex byte text has odd length");
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        bytes.push_back(static_cast<std::byte>((hex_digit(text[index]) << 4U) |
                                               hex_digit(text[index + 1])));
    }
    return bytes;
}

std::array<std::uint8_t, 32> digest_bytes(std::string_view text) {
    const auto bytes = bytes_from_hex(text);
    expect(bytes.size() == 32, "test digest does not contain 32 bytes");
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = std::to_integer<std::uint8_t>(bytes[index]);
    }
    return result;
}

void expect_sha256(std::span<const std::byte> bytes, std::string_view expected,
                   const char *message) {
    expect(contract::sha256(bytes).bytes == digest_bytes(expected), message);
}

void append_u32le(std::vector<std::byte> &bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & UINT32_C(0xff)));
    }
}

void append_u64le(std::vector<std::byte> &bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & UINT64_C(0xff)));
    }
}

std::uint32_t read_u32le(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4; ++index) {
        result |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (8U * index);
    }
    return result;
}

void test_gain_and_fade_boundaries() {
    constexpr std::array golden{
        std::pair{UINT64_C(0), UINT64_C(0x0000000000000000)},
        std::pair{UINT64_C(1), UINT64_C(0x3f3acee9e6f0d0f9)},
        std::pair{UINT64_C(2), UINT64_C(0x3f4acee9c14f807a)},
        std::pair{UINT64_C(426), UINT64_C(0x3fc6314d8dfc1b0f)},
        std::pair{UINT64_C(1583), UINT64_C(0x3fe34da873b8103d)},
        std::pair{UINT64_C(3838), UINT64_C(0x3fefffff4c544ff4)},
        std::pair{UINT64_C(3839), UINT64_C(0x3fefffffd31513de)},
        std::pair{UINT64_C(3840), UINT64_C(0x3ff0000000000000)},
    };
    for (const auto &[index, expected_bits] : golden) {
        expect(std::bit_cast<std::uint64_t>(p18_quarter_sine_gain(index)) ==
                   expected_bits,
               "P1.8 quarter-sine gain bits changed");
    }

    std::vector<std::byte> table_bytes;
    table_bytes.reserve((kP18FadeFrameCount + 1) * sizeof(double));
    for (std::uint64_t index = 0; index <= kP18FadeFrameCount; ++index) {
        append_u64le(table_bytes,
                     std::bit_cast<std::uint64_t>(p18_quarter_sine_gain(index)));
    }
    expect_sha256(table_bytes,
                  "25514f82ee4ebfcf9e15c6aa0807f40f636ab0edc59500bfcf2ca066e0d4d476",
                  "P1.8 complete quarter-sine table changed");

    expect(p18_audition_fade_gain(0) == p18_quarter_sine_gain(0) &&
               p18_audition_fade_gain(3839) == p18_quarter_sine_gain(3839) &&
               p18_audition_fade_gain(3840) == 1.0 &&
               p18_audition_fade_gain(2'876'160) == 1.0 &&
               p18_audition_fade_gain(2'876'161) == p18_quarter_sine_gain(3839) &&
               p18_audition_fade_gain(2'879'999) == p18_quarter_sine_gain(1) &&
               p18_audition_fade_gain(2'879'999) != 0.0,
           "P1.8 frame-indexed fade boundaries changed");
    expect_throw<std::out_of_range>(
        [] { static_cast<void>(p18_quarter_sine_gain(3841)); },
        "P1.8 quarter-sine accepted an out-of-range index");
    expect_throw<std::out_of_range>(
        [] { static_cast<void>(p18_audition_fade_gain(2'880'000)); },
        "P1.8 fade accepted a frame beyond the audible interval");
}

void test_quantizer_vectors_and_sentinels() {
    struct QuantizerVector {
        std::uint32_t input_bits;
        std::int32_t s32;
        std::int32_t pcm24;
        std::array<std::byte, 3> bytes;
        bool saturated;
    };
    constexpr std::array vectors{
        QuantizerVector{
            0x00000000U, 0, 0, {std::byte{0}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{
            0x80000000U, 0, 0, {std::byte{0}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{
            0x2f800000U, 0, 0, {std::byte{0}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{
            0x30400000U, 2, 0, {std::byte{0}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{
            0x33ff8000U, 256, 1, {std::byte{1}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{
            0x34004000U, 256, 1, {std::byte{1}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{0xb0400000U,
                        -2,
                        -1,
                        {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}},
                        false},
        QuantizerVector{
            0x33800000U, 128, 0, {std::byte{0}, std::byte{0}, std::byte{0}}, false},
        QuantizerVector{0xb3800000U,
                        -128,
                        -1,
                        {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}},
                        false},
        QuantizerVector{0x3f7fffffU,
                        2'147'483'520,
                        8'388'607,
                        {std::byte{0xff}, std::byte{0xff}, std::byte{0x7f}},
                        false},
        QuantizerVector{0x3f800000U,
                        2'147'483'647,
                        8'388'607,
                        {std::byte{0xff}, std::byte{0xff}, std::byte{0x7f}},
                        true},
        QuantizerVector{0xbf7fffffU,
                        -2'147'483'520,
                        -8'388'608,
                        {std::byte{0}, std::byte{0}, std::byte{0x80}},
                        false},
        QuantizerVector{0xbf800000U,
                        std::numeric_limits<std::int32_t>::min(),
                        -8'388'608,
                        {std::byte{0}, std::byte{0}, std::byte{0x80}},
                        true},
    };
    for (const auto &vector : vectors) {
        const auto result = p18_quantize_pcm24(std::bit_cast<float>(vector.input_bits));
        expect(result.s32 == vector.s32 && result.pcm24 == vector.pcm24 &&
                   result.saturated == vector.saturated &&
                   p18_serialize_pcm24le(result.pcm24) == vector.bytes,
               "P1.8 Float32-to-S32-to-PCM24 vector changed");
    }

    struct Sentinel {
        std::uint64_t frame;
        std::uint32_t monitor_bits;
        std::uint32_t faded_bits;
        std::int32_t pcm24;
    };
    constexpr std::array sentinels{
        Sentinel{426, 0x3ccf2426U, 0x3b8fa800U, 36'776},
        Sentinel{1583, 0x3d45b3efU, 0x3cee853fU, 244'244},
    };
    for (const auto &sentinel : sentinels) {
        const float monitor = std::bit_cast<float>(sentinel.monitor_bits);
        const float raw = monitor * 0x1p-7F;
        const auto result = p18_master_reference_frame(raw, 0.0F, sentinel.frame);
        expect(std::bit_cast<std::uint32_t>(result.monitor) == sentinel.monitor_bits &&
                   std::bit_cast<std::uint32_t>(result.faded) == sentinel.faded_bits &&
                   result.pcm24 == sentinel.pcm24 && !result.saturated,
               "P1.8 binary64-fade sentinel changed or rounded gain prematurely");
    }

    expect_throw<std::domain_error>(
        [] {
            static_cast<void>(
                p18_quantize_pcm24(std::numeric_limits<float>::quiet_NaN()));
        },
        "P1.8 quantizer accepted NaN");
    expect_throw<std::domain_error>(
        [] {
            static_cast<void>(
                p18_quantize_pcm24(std::numeric_limits<float>::infinity()));
        },
        "P1.8 quantizer accepted infinity");
    expect_throw<std::out_of_range>(
        [] { static_cast<void>(p18_serialize_pcm24le(8'388'608)); },
        "P1.8 PCM24 serializer accepted a positive out-of-range code");
    expect_throw<std::out_of_range>(
        [] { static_cast<void>(p18_serialize_pcm24le(-8'388'609)); },
        "P1.8 PCM24 serializer accepted a negative out-of-range code");
}

void test_mastering_block_partitioning_and_transactionality() {
    const std::array route_0{0.25F, -0.25F, 0x1p-20F, -0x1p-20F, 0.0F};
    const std::array route_1{-0.125F, 0.125F, 0x1p-21F, -0x1p-21F, -0.0F};
    std::array<P18MasteredFrame, route_0.size()> contiguous{};
    p18_master_reference_block(route_0, route_1, 3838, contiguous);

    std::array<P18MasteredFrame, route_0.size()> split{};
    p18_master_reference_block(std::span{route_0}.first(2), std::span{route_1}.first(2),
                               3838, std::span{split}.first(2));
    p18_master_reference_block(std::span{route_0}.subspan(2),
                               std::span{route_1}.subspan(2), 3840,
                               std::span{split}.subspan(2));
    expect(split == contiguous,
           "P1.8 mastering result depends on caller block partitioning");
    for (std::size_t index = 0; index < route_0.size(); ++index) {
        expect(contiguous[index] == p18_master_reference_frame(
                                        route_0[index], route_1[index], 3838 + index),
               "P1.8 block result differs from its exposed per-frame result");
    }

    std::array<P18MasteredFrame, 2> sentinel{
        P18MasteredFrame{17.0F, 17.0F, 17.0, 17.0F, 17, 17, true},
        P18MasteredFrame{19.0F, 19.0F, 19.0, 19.0F, 19, 19, true},
    };
    const auto unchanged = sentinel;
    const std::array valid{0.0F, 0.0F};
    const std::array late_nan{0.0F, std::numeric_limits<float>::quiet_NaN()};
    expect_throw<std::domain_error>(
        [&] { p18_master_reference_block(valid, late_nan, 0, sentinel); },
        "P1.8 mastering block accepted late non-finite input");
    expect(sentinel == unchanged, "failed P1.8 mastering block changed caller output");

    const std::array<float, 1> maximum{std::numeric_limits<float>::max()};
    std::array<P18MasteredFrame, 1> one_output{};
    expect_throw<std::domain_error>(
        [&] { p18_master_reference_block(maximum, maximum, 0, one_output); },
        "P1.8 mastering accepted an overflowing Float32 route sum");
    expect_throw<std::invalid_argument>(
        [&] {
            p18_master_reference_block(route_0, std::span{route_1}.first(4), 0,
                                       contiguous);
        },
        "P1.8 mastering accepted mismatched block lengths");
    expect_throw<std::out_of_range>(
        [&] {
            p18_master_reference_block(std::span{route_0}.first(2),
                                       std::span{route_1}.first(2), 2'879'999,
                                       std::span{contiguous}.first(2));
        },
        "P1.8 mastering accepted a block beyond the audible interval");

    std::span<const float> empty_input;
    std::span<P18MasteredFrame> empty_output;
    p18_master_reference_block(empty_input, empty_input, kP18AudibleFrameCount,
                               empty_output);
}

struct ByteCollector {
    explicit ByteCollector(std::size_t maximum_chunk_bytes)
        : maximum_chunk_bytes(maximum_chunk_bytes) {}

    bool consume(std::uint64_t offset, std::span<const std::byte> chunk) {
        if (offset != bytes.size() || chunk.empty() ||
            chunk.size() > maximum_chunk_bytes) {
            valid = false;
            return false;
        }
        maximum_seen = std::max(maximum_seen, chunk.size());
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return true;
    }

    std::size_t maximum_chunk_bytes;
    std::size_t maximum_seen = 0;
    bool valid = true;
    std::vector<std::byte> bytes;
};

P18AuditionWaveEncoder require_audition_encoder(P18AuditionWaveEncoderResult result) {
    if (const auto *failure = std::get_if<WavEncodingError>(&result)) {
        throw std::runtime_error{"valid P1.8 audition encoder rejected: " +
                                 failure->path + ": " + failure->message};
    }
    return std::get<P18AuditionWaveEncoder>(std::move(result));
}

WavEncoder require_wav_encoder(WavEncoderResult result) {
    if (const auto *failure = std::get_if<WavEncodingError>(&result)) {
        throw std::runtime_error{"valid WAVE encoder rejected: " + failure->path +
                                 ": " + failure->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

std::vector<std::byte> encode_zero_audition(std::size_t output_partition,
                                            bool split_input) {
    auto encoder =
        require_audition_encoder(make_p18_audition_wave_encoder({output_partition}));
    ByteCollector collector{output_partition};
    collector.bytes.reserve(kP18AuditionWaveByteCount);
    const WavChunkConsumer consume = [&](auto offset, auto bytes) {
        return collector.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "valid P1.8 audition prefix emission failed");
    const std::vector<std::int32_t> zeroes(kP18AuditionWaveFrameCount, 0);
    if (!split_input) {
        expect(!encoder.write_pcm24(zeroes, consume).has_value(),
               "contiguous P1.8 audition payload failed");
    } else {
        constexpr std::array<std::size_t, 5> pattern{1, 3839, 3840, 9600, 137};
        std::size_t offset = 0;
        std::size_t pattern_index = 0;
        while (offset < zeroes.size()) {
            const auto count = std::min(pattern[pattern_index++ % pattern.size()],
                                        zeroes.size() - offset);
            expect(
                !encoder.write_pcm24(std::span{zeroes}.subspan(offset, count), consume)
                     .has_value(),
                "partitioned P1.8 audition payload failed");
            offset += count;
        }
    }
    expect(!encoder.finish(consume).has_value() && encoder.finished() &&
               collector.valid && collector.maximum_seen <= output_partition &&
               collector.bytes.size() == kP18AuditionWaveByteCount,
           "P1.8 audition encoder final state or bounded output changed");
    return collector.bytes;
}

void test_exact_prefix_and_streaming_encoder() {
    const auto expected = bytes_from_hex(
        "5249464626d7830057415645666d742028000000feff010000ee020000ca0800030018"
        "0016001800040000000100000000001000800000aa00389b714c495354e2000000494e"
        "464f49434d548c000000313530302d363530302052504d206f76657220313520733b20"
        "383525206566666f72743b20636f686572656e742073756d206f662074776f206c696e"
        "656172207765742f64727920657868617573742062757365733b206669786564207831"
        "3238206d6f6e69746f72696e67206761696e3b206e6f206c696d69746572206f722063"
        "6f6d70726573736f7200494e414d2c000000424d57204d35324232382066696674682d"
        "676561722d6571756976616c656e742064796e6f20737765657000495346540e000000"
        "4c61766636302e31362e313030006461746100d68300");
    const auto prefix = p18_audition_wave_prefix();
    expect(expected.size() == 302 && std::ranges::equal(prefix, expected),
           "P1.8 audition prefix differs byte-for-byte from the frozen contract");
    expect_sha256(prefix,
                  "781f526e8df5ae102c5af9c3a6c5734fe2fa5e45f4ca03e0c1b04d59e3ebd16b",
                  "P1.8 audition prefix digest changed");
    expect_sha256(prefix.subspan<12, 48>(),
                  "1b5b3995d7f2eddec82cdb694cbcae10980703b7bd02860b6e2f790553a429d5",
                  "P1.8 audition fmt chunk digest changed");
    expect_sha256(prefix.subspan<60, 234>(),
                  "8b9ed60efbe263602b146dd56ec810e01e90995b95a92796c9bd9342e399cadf",
                  "P1.8 audition LIST chunk digest changed");

    const auto contiguous = encode_zero_audition(65'536, false);
    const auto partitioned = encode_zero_audition(257, true);
    const auto alternately_partitioned = encode_zero_audition(4'093, true);
    expect(contiguous == partitioned && contiguous == alternately_partitioned,
           "P1.8 audition bytes depend on valid input/output callback partitions");

    auto invalid_sample =
        require_audition_encoder(make_p18_audition_wave_encoder({17}));
    ByteCollector invalid_bytes{17};
    const WavChunkConsumer invalid_consume = [&](auto offset, auto bytes) {
        return invalid_bytes.consume(offset, bytes);
    };
    expect(!invalid_sample.begin(invalid_consume).has_value(),
           "P1.8 invalid-sample preflight setup failed");
    const auto prefix_size = invalid_bytes.bytes.size();
    const std::array invalid_codes{0, 8'388'608};
    const auto invalid_status =
        invalid_sample.write_pcm24(invalid_codes, invalid_consume);
    expect(invalid_status.has_value() &&
               invalid_status->code == WavEncodingErrorCode::sample_out_of_range &&
               invalid_bytes.bytes.size() == prefix_size && invalid_sample.failed(),
           "invalid P1.8 PCM24 block emitted bytes or did not fail terminally");

    auto incomplete = require_audition_encoder(make_p18_audition_wave_encoder({302}));
    ByteCollector incomplete_bytes{302};
    const WavChunkConsumer incomplete_consume = [&](auto offset, auto bytes) {
        return incomplete_bytes.consume(offset, bytes);
    };
    expect(!incomplete.begin(incomplete_consume).has_value(),
           "P1.8 incomplete-stream setup failed");
    const auto incomplete_status = incomplete.finish(incomplete_consume);
    expect(incomplete_status.has_value() &&
               incomplete_status->code == WavEncodingErrorCode::frame_count_mismatch &&
               incomplete.failed(),
           "incomplete P1.8 audition stream was accepted");

    auto rejected = require_audition_encoder(make_p18_audition_wave_encoder({1}));
    std::size_t calls = 0;
    const WavChunkConsumer reject = [&](auto, auto) { return ++calls < 7; };
    const auto rejected_status = rejected.begin(reject);
    expect(rejected_status.has_value() &&
               rejected_status->code == WavEncodingErrorCode::callback_rejected &&
               rejected.failed(),
           "callback-rejected P1.8 audition prefix was accepted");

    auto payload_rejected =
        require_audition_encoder(make_p18_audition_wave_encoder({3}));
    std::uint64_t accepted_offset = 0;
    bool reject_second_payload_chunk = false;
    const WavChunkConsumer reject_payload = [&](std::uint64_t offset,
                                                std::span<const std::byte> bytes) {
        if (offset != accepted_offset || bytes.size() > 3) {
            return false;
        }
        if (offset >= kP18AuditionWavePrefixByteCount) {
            if (reject_second_payload_chunk) {
                return false;
            }
            reject_second_payload_chunk = true;
        }
        accepted_offset += bytes.size();
        return true;
    };
    expect(!payload_rejected.begin(reject_payload).has_value(),
           "P1.8 payload callback-rejection setup failed");
    const std::array two_codes{1, 2};
    const auto payload_rejected_status =
        payload_rejected.write_pcm24(two_codes, reject_payload);
    expect(payload_rejected_status.has_value() &&
               payload_rejected_status->code ==
                   WavEncodingErrorCode::callback_rejected &&
               payload_rejected.failed() && payload_rejected.frames_written() == 0 &&
               payload_rejected.bytes_emitted() == kP18AuditionWavePrefixByteCount + 3,
           "callback-rejected P1.8 payload committed frames or remained reusable");

    expect(
        std::holds_alternative<WavEncodingError>(make_p18_audition_wave_encoder({0})) &&
            std::holds_alternative<WavEncodingError>(
                make_p18_audition_wave_encoder({65'537})),
        "P1.8 audition encoder accepted an unbounded callback partition");
}

std::vector<std::byte> read_exact_file(const std::string &path,
                                       std::size_t expected_size) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error{"cannot open frozen selected stem: " + path};
    }
    const auto size = stream.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) != expected_size) {
        throw std::runtime_error{"frozen selected stem has the wrong byte count: " +
                                 path};
    }
    std::vector<std::byte> bytes(expected_size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"failed to read frozen selected stem: " + path};
    }
    return bytes;
}

std::vector<float> decode_selected_stem(const std::vector<std::byte> &wave) {
    const auto expected_header = bytes_from_hex(
        "5249464632c8af0057415645666d7420120000000300010000ee020000b80b00040020"
        "000000666163740400000000f22b006461746100c8af00");
    expect(wave.size() == 11'520'058 && expected_header.size() == 58 &&
               std::equal(expected_header.begin(), expected_header.end(), wave.begin()),
           "frozen selected stem has the wrong Float32 WAVE representation");
    std::vector<float> samples(kP18AudibleFrameCount);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] =
            std::bit_cast<float>(read_u32le(wave, 58 + index * sizeof(float)));
        expect(std::isfinite(samples[index]),
               "frozen selected stem contains non-finite Float32 input");
    }
    return samples;
}

void test_frozen_master_identities(const std::string &route_0_path,
                                   const std::string &route_1_path) {
    const auto route_0_wave = read_exact_file(route_0_path, 11'520'058);
    const auto route_1_wave = read_exact_file(route_1_path, 11'520'058);
    expect_sha256(route_0_wave,
                  "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32",
                  "frozen route-0 selected stem identity changed");
    expect_sha256(route_1_wave,
                  "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d",
                  "frozen route-1 selected stem identity changed");
    const auto route_0 = decode_selected_stem(route_0_wave);
    const auto route_1 = decode_selected_stem(route_1_wave);

    std::vector<float> raw;
    std::vector<std::int32_t> pcm24;
    std::vector<std::byte> raw_bytes;
    std::vector<std::byte> monitor_bytes;
    std::vector<std::byte> faded_bytes;
    std::vector<std::byte> s32_bytes;
    std::vector<std::byte> pcm24_bytes;
    raw.reserve(kP18AudibleFrameCount);
    pcm24.reserve(kP18AudibleFrameCount);
    raw_bytes.reserve(kP18AuditionWaveDataByteCount / 3 * 4);
    monitor_bytes.reserve(kP18AuditionWaveDataByteCount / 3 * 4);
    faded_bytes.reserve(kP18AuditionWaveDataByteCount / 3 * 4);
    s32_bytes.reserve(kP18AuditionWaveDataByteCount / 3 * 4);
    pcm24_bytes.reserve(kP18AuditionWaveDataByteCount);

    constexpr std::size_t block_size = 9'600;
    std::vector<P18MasteredFrame> block(block_size);
    std::uint64_t saturation_count = 0;
    float peak = 0.0F;
    for (std::size_t offset = 0; offset < route_0.size(); offset += block_size) {
        const auto count = std::min(block_size, route_0.size() - offset);
        p18_master_reference_block(std::span{route_0}.subspan(offset, count),
                                   std::span{route_1}.subspan(offset, count), offset,
                                   std::span{block}.first(count));
        for (const auto &frame : std::span{block}.first(count)) {
            raw.push_back(frame.raw);
            pcm24.push_back(frame.pcm24);
            append_u32le(raw_bytes, std::bit_cast<std::uint32_t>(frame.raw));
            append_u32le(monitor_bytes, std::bit_cast<std::uint32_t>(frame.monitor));
            append_u32le(faded_bytes, std::bit_cast<std::uint32_t>(frame.faded));
            append_u32le(s32_bytes, static_cast<std::uint32_t>(frame.s32));
            const auto encoded = p18_serialize_pcm24le(frame.pcm24);
            pcm24_bytes.insert(pcm24_bytes.end(), encoded.begin(), encoded.end());
            saturation_count += frame.saturated ? 1U : 0U;
            peak = std::max(peak, std::abs(frame.faded));
        }
    }

    expect_sha256(raw_bytes,
                  "fe2475249df2f6a51b2c82c8251493216db1a1ec094a7a0c5577a11c430f410f",
                  "P1.8 raw-master payload identity changed");
    expect_sha256(monitor_bytes,
                  "0a2abe8ea8f166c1022efda26c57e5ad4eda5e7cb515100a5e6d16eb465318db",
                  "P1.8 monitoring-gain payload identity changed");
    expect_sha256(faded_bytes,
                  "af194389df2ba20ab9d1bc5e3f97735afbb6c76d4c215a7ac2e1d45ecd3a3633",
                  "P1.8 faded payload identity changed");
    expect_sha256(s32_bytes,
                  "b0505bc9a81cfdcea0256ff6e5731ac2a1f58f90f43911bc84d799926151d924",
                  "P1.8 pre-truncation S32 payload identity changed");
    expect_sha256(pcm24_bytes,
                  "2153869958bb924e4eda277a37e95eab1abb7c29aa9fa389c1fa8f879e7bfdcf",
                  "P1.8 audition PCM24 payload identity changed");
    expect(saturation_count == 0 && std::bit_cast<std::uint32_t>(peak) == 0x3f2ad253U,
           "P1.8 audition saturation count or frozen peak changed");

    const contract::AudioContract raw_contract{
        {192'000, 1}, kP18AudibleFrameCount, "mono", "float32le"};
    auto raw_encoder = require_wav_encoder(make_wav_encoder(raw_contract, {16'384}));
    ByteCollector raw_wave{16'384};
    raw_wave.bytes.reserve(11'520'058);
    const WavChunkConsumer collect_raw = [&](auto offset, auto bytes) {
        return raw_wave.consume(offset, bytes);
    };
    expect(!raw_encoder.begin(collect_raw).has_value() &&
               !raw_encoder.write_float32_interleaved(raw, collect_raw).has_value() &&
               !raw_encoder.finish(collect_raw).has_value(),
           "existing Float32 WAVE encoder failed the P1.8 raw master");
    expect_sha256(raw_wave.bytes,
                  "2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146",
                  "P1.8 raw-master complete WAVE identity changed");

    auto audition_encoder =
        require_audition_encoder(make_p18_audition_wave_encoder({16'384}));
    ByteCollector audition_wave{16'384};
    audition_wave.bytes.reserve(kP18AuditionWaveByteCount);
    const WavChunkConsumer collect_audition = [&](auto offset, auto bytes) {
        return audition_wave.consume(offset, bytes);
    };
    expect(!audition_encoder.begin(collect_audition).has_value() &&
               !audition_encoder.write_pcm24(pcm24, collect_audition).has_value() &&
               !audition_encoder.finish(collect_audition).has_value(),
           "exact P1.8 audition WAVE encoder failed the frozen master");
    expect_sha256(audition_wave.bytes,
                  "f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb",
                  "P1.8 audition complete WAVE identity changed");
}

void run_tests(int argc, char **argv) {
    test_gain_and_fade_boundaries();
    test_quantizer_vectors_and_sentinels();
    test_mastering_block_partitioning_and_transactionality();
    test_exact_prefix_and_streaming_encoder();
    if (argc == 3) {
        test_frozen_master_identities(argv[1], argv[2]);
    } else if (argc != 1) {
        throw std::runtime_error{
            "expected either no arguments or route-0 and route-1 selected stem paths"};
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        run_tests(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "P1.8 mastering test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
