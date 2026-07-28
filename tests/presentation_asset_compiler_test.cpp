#include "presentation/presentation_asset_compiler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;

constexpr std::size_t kSyntheticSupportFrameCount = 6907;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void append_fourcc(std::vector<std::byte> &bytes, std::string_view value) {
    expect(value.size() == 4, "test FOURCC is malformed");
    for (const char character : value) {
        bytes.push_back(std::byte{static_cast<unsigned char>(character)});
    }
}

void append_u16le(std::vector<std::byte> &bytes, std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xffU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)});
}

void append_u32le(std::vector<std::byte> &bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)});
    }
}

void write_u16le(std::vector<std::byte> &bytes, std::size_t offset,
                 std::uint16_t value) {
    bytes[offset] = std::byte{static_cast<unsigned char>(value & 0xffU)};
    bytes[offset + 1] = std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)};
}

std::vector<std::byte> make_synthetic_pcm16_wave() {
    std::vector<std::int16_t> samples(kSyntheticSupportFrameCount, 0);
    samples[0] = 32767;
    samples[31] = -12345;
    samples.back() = 101;

    const auto data_size = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::byte> bytes;
    bytes.reserve(44 + data_size);
    append_fourcc(bytes, "RIFF");
    append_u32le(bytes, 36U + data_size);
    append_fourcc(bytes, "WAVE");
    append_fourcc(bytes, "fmt ");
    append_u32le(bytes, 16);
    append_u16le(bytes, 1);
    append_u16le(bytes, 1);
    append_u32le(bytes, kConfiguredIrSampleRateHz);
    append_u32le(bytes, kConfiguredIrSampleRateHz * 2U);
    append_u16le(bytes, 2);
    append_u16le(bytes, 16);
    append_fourcc(bytes, "data");
    append_u32le(bytes, data_size);
    for (const std::int16_t sample : samples) {
        append_u16le(bytes, static_cast<std::uint16_t>(sample));
    }
    return bytes;
}

std::vector<std::byte> read_bounded_file(const std::string &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        throw std::runtime_error{"cannot open configured IR test input: " + path};
    }
    const auto end = input.tellg();
    if (end < 0 || end > 100000) {
        throw std::runtime_error{"configured IR test input has an invalid size"};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != bytes.size()) {
        throw std::runtime_error{"could not read the complete configured IR input"};
    }
    return bytes;
}

struct Fixture {
    contract::AudioAssetSpec asset;
    struct OwnedPayload {
        contract::AudioAssetId id;
        std::vector<std::byte> bytes;
    } payload;
    contract::MethodIdentity method;
    contract::ResolvedValue<double> gain;
};

Fixture make_fixture() {
    auto bytes = make_synthetic_pcm16_wave();
    const contract::Sha256Digest digest = contract::sha256(bytes);
    const contract::AudioMediaContract media{
        contract::AudioSampleEncoding::pcm_s16le,
        contract::AudioChannelLayout::mono,
        {kConfiguredIrSampleRateHz, 1},
        kSyntheticSupportFrameCount,
    };
    return {
        {
            contract::AudioAssetId{7},
            {"synthetic-static-ir", "asset-semantic-resolution"},
            {"synthetic-static-ir-evidence", "asset-evidence-resolution"},
            {digest, "asset-digest-resolution"},
            {media, "asset-media-resolution"},
        },
        {contract::AudioAssetId{7}, std::move(bytes)},
        static_ir_conversion_method_identity(),
        {0.001, "route-ir-gain-resolution"},
    };
}

PresentationAssetPayloadView payload_view(const Fixture &fixture) {
    return {fixture.payload.id, fixture.payload.bytes};
}

const CompiledPresentationAsset &
expect_compiled(const PresentationAssetCompileResult &result, const char *message) {
    const auto *compiled = std::get_if<CompiledPresentationAsset>(&result);
    expect(compiled != nullptr, message);
    return *compiled;
}

const PresentationAssetCompileError &
expect_error(const PresentationAssetCompileResult &result,
             PresentationAssetCompileErrorCode expected, const char *message) {
    const auto *error = std::get_if<PresentationAssetCompileError>(&result);
    expect(error != nullptr && error->code == expected, message);
    return *error;
}

const CompiledPresentationConvolutionKernel &
expect_compiled_kernel(const PresentationConvolutionKernelCompileResult &result,
                       const char *message) {
    const auto *compiled = std::get_if<CompiledPresentationConvolutionKernel>(&result);
    expect(compiled != nullptr, message);
    return *compiled;
}

const PresentationConvolutionKernelCompileError &
expect_kernel_error(const PresentationConvolutionKernelCompileResult &result,
                    PresentationConvolutionKernelCompileErrorCode expected,
                    const char *message) {
    const auto *error = std::get_if<PresentationConvolutionKernelCompileError>(&result);
    expect(error != nullptr && error->code == expected, message);
    return *error;
}

std::span<const std::byte> as_bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte *>(value.data()), value.size()};
}

std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const std::uint8_t byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

void test_valid_asset_and_deterministic_identities() {
    const Fixture fixture = make_fixture();
    const auto first_result = compile_presentation_asset(
        fixture.asset, payload_view(fixture), fixture.method, fixture.gain);
    const auto &first = expect_compiled(
        first_result, "valid synthetic presentation asset was rejected");
    const auto first_kernel_result = compile_presentation_convolution_kernel(
        first, fixed_overlap_save_convolution_method_identity());
    const auto &first_kernel = expect_compiled_kernel(
        first_kernel_result, "valid synthetic convolution kernel was rejected");

    expect(first.raw_payload_identity() ==
                   contract::AssetPayloadIdentity{
                       fixture.payload.id,
                       static_cast<std::uint64_t>(fixture.payload.bytes.size()),
                       contract::sha256(fixture.payload.bytes),
                   } &&
               first.source_media() == fixture.asset.media.value &&
               first.conversion_method() == fixture.method &&
               first.configured_gain() == fixture.gain &&
               first.meaningful_support_frame_count() == kSyntheticSupportFrameCount,
           "compiled presentation asset did not retain its resolved request identity");
    expect(
        first.coefficients().size() == dsp::FixedConvolutionKernel::coefficient_count &&
            first_kernel.kernel() != nullptr &&
            first.coefficient_f64le_identity().byte_count == 240568 &&
            first_kernel.spectrum_complex_f64le_identity().byte_count == 1048576 &&
            !first.coefficient_f64le_identity().payload_sha256.is_zero() &&
            !first_kernel.spectrum_complex_f64le_identity().payload_sha256.is_zero() &&
            first_kernel.key().raw_payload_identity == first.raw_payload_identity() &&
            first_kernel.key().conversion_method == first.conversion_method() &&
            first_kernel.key().configured_gain_binary64_bits ==
                std::bit_cast<std::uint64_t>(first.configured_gain().value) &&
            first_kernel.key().coefficient_f64le_identity ==
                first.coefficient_f64le_identity() &&
            first_kernel.key().convolution_method ==
                fixed_overlap_save_convolution_method_identity(),
        "compiled presentation asset has an invalid kernel or identity shape");

    const auto second_result = compile_presentation_asset(
        fixture.asset, payload_view(fixture), fixture.method, fixture.gain);
    const auto &second = expect_compiled(
        second_result, "repeated synthetic presentation compilation failed");
    const auto second_kernel_result = compile_presentation_convolution_kernel(
        second, fixed_overlap_save_convolution_method_identity());
    const auto &second_kernel = expect_compiled_kernel(
        second_kernel_result, "repeated synthetic kernel compilation failed");
    expect(second.raw_payload_identity() == first.raw_payload_identity() &&
               second.coefficient_f64le_identity() ==
                   first.coefficient_f64le_identity() &&
               second_kernel.spectrum_complex_f64le_identity() ==
                   first_kernel.spectrum_complex_f64le_identity() &&
               std::ranges::equal(second.coefficients(), first.coefficients()),
           "presentation asset compilation is not deterministic");

    const auto &supported = static_ir_conversion_method_identity();
    expect(supported.configuration_sha256 ==
               contract::sha256(as_bytes(kStaticIrConversionMethodDescriptor)),
           "production static-IR method descriptor and identity disagree");

    expect(
        digest_hex(first.coefficient_f64le_identity().payload_sha256) ==
                "7cd3aa1b07013c56f8d079bdfafe37b8befb7741dc8934132615bf74ffa6259a" &&
            digest_hex(first_kernel.spectrum_complex_f64le_identity().payload_sha256) ==
                "1d9bf2a40378e2f5975a1ffc4f387e04fb5ea0534033923dd3200587b1a81ce7",
        "synthetic converted or convolution identity changed");
}

void test_identity_and_hash_rejection() {
    {
        auto fixture = make_fixture();
        fixture.asset.id = {};
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::invalid_asset_id,
                     "zero asset ID was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.payload.id = contract::AudioAssetId{8};
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::payload_id_mismatch,
                     "mismatched payload ID was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.payload.bytes.clear();
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::missing_payload,
                     "empty payload was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.payload.bytes.resize(kMaximumConfiguredIrContainerByteCount + 1);
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::payload_container_too_large,
                     "oversized configured-IR container was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.payload.bytes.back() ^= std::byte{1};
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::payload_sha256_mismatch,
                     "mismatched payload hash was accepted");
    }
}

void test_media_rejection() {
    {
        auto fixture = make_fixture();
        fixture.asset.media.value.encoding = contract::AudioSampleEncoding::float32le;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_media_contract,
                     "unsupported declared asset encoding was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.asset.media.value.channel_layout =
            static_cast<contract::AudioChannelLayout>(99);
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_media_contract,
                     "unsupported declared channel layout was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.asset.media.value.sample_rate = {48000, 1};
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_media_contract,
                     "unsupported declared sample rate was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.asset.media.value.frame_count = 0;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_media_contract,
                     "zero declared media frame count was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.asset.media.value.frame_count = kMaximumConfiguredIrFrameCount + 1;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_media_contract,
                     "oversized declared media frame count was accepted");
    }
    {
        auto fixture = make_fixture();
        --fixture.asset.media.value.frame_count;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::media_frame_count_mismatch,
                     "declared and decoded media frame counts were allowed to differ");
    }
    {
        auto fixture = make_fixture();
        write_u16le(fixture.payload.bytes, 22, 2);
        fixture.asset.content_sha256.value = contract::sha256(fixture.payload.bytes);
        const auto result = compile_presentation_asset(
            fixture.asset, payload_view(fixture), fixture.method, fixture.gain);
        const auto &error = expect_error(
            result, PresentationAssetCompileErrorCode::pcm16_wave_decode_failed,
            "unsupported payload channel layout escaped PCM16 decoding");
        const auto *decode_error = std::get_if<Pcm16IrDecodeError>(&error.detail);
        expect(decode_error != nullptr &&
                   decode_error->code ==
                       Pcm16IrDecodeErrorCode::unsupported_channel_count,
               "asset compiler discarded the typed PCM16 decode error");
    }
}

void test_method_and_gain_rejection() {
    {
        auto fixture = make_fixture();
        fixture.method.id += "-different";
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_conversion_method,
                     "different IR-conversion method ID was accepted");
    }
    {
        auto fixture = make_fixture();
        ++fixture.method.version;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_conversion_method,
                     "different IR-conversion method version was accepted");
    }
    {
        auto fixture = make_fixture();
        fixture.method.configuration_sha256.bytes[0] ^= 1U;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::unsupported_conversion_method,
                     "different IR-conversion configuration was accepted");
    }
    for (const double invalid_gain :
         std::array{-1.0, std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN(), -0.0}) {
        auto fixture = make_fixture();
        fixture.gain.value = invalid_gain;
        expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                                fixture.method, fixture.gain),
                     PresentationAssetCompileErrorCode::invalid_impulse_response_gain,
                     "invalid route impulse-response gain was accepted");
    }
}

void test_fixed_kernel_shape_rejection() {
    auto fixture = make_fixture();
    write_u16le(fixture.payload.bytes, fixture.payload.bytes.size() - 2, 0);
    fixture.asset.content_sha256.value = contract::sha256(fixture.payload.bytes);
    const auto asset_result = compile_presentation_asset(
        fixture.asset, payload_view(fixture), fixture.method, fixture.gain);
    const auto &asset = expect_compiled(
        asset_result, "short but valid converted IR asset was rejected");
    expect_kernel_error(
        compile_presentation_convolution_kernel(
            asset, fixed_overlap_save_convolution_method_identity()),
        PresentationConvolutionKernelCompileErrorCode::unsupported_coefficient_shape,
        "IR support incompatible with the current fixed kernel was accepted");
}

void test_convolution_method_rejection() {
    const Fixture fixture = make_fixture();
    const auto asset_result = compile_presentation_asset(
        fixture.asset, payload_view(fixture), fixture.method, fixture.gain);
    const auto &asset =
        expect_compiled(asset_result, "valid asset for method rejection was rejected");

    for (std::size_t mutation = 0; mutation < 3; ++mutation) {
        auto method = fixed_overlap_save_convolution_method_identity();
        if (mutation == 0) {
            method.id += "-different";
        } else if (mutation == 1) {
            ++method.version;
        } else {
            method.configuration_sha256.bytes[0] ^= 1U;
        }
        expect_kernel_error(compile_presentation_convolution_kernel(asset, method),
                            PresentationConvolutionKernelCompileErrorCode::
                                unsupported_convolution_method,
                            "different convolution method identity was accepted");
    }
}

void test_empty_meaningful_support_rejection() {
    auto fixture = make_fixture();
    std::fill(fixture.payload.bytes.begin() + 44, fixture.payload.bytes.end(),
              std::byte{0});
    fixture.asset.content_sha256.value = contract::sha256(fixture.payload.bytes);
    expect_error(compile_presentation_asset(fixture.asset, payload_view(fixture),
                                            fixture.method, fixture.gain),
                 PresentationAssetCompileErrorCode::empty_meaningful_support,
                 "all-subthreshold configured IR was accepted");
}

void test_canonical_bmw_asset(const std::string &path) {
    auto bytes = read_bounded_file(path);
    const auto raw_sha256 = contract::sha256(bytes);
    expect(bytes.size() == 78602 &&
               digest_hex(raw_sha256) ==
                   "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc",
           "canonical BMW configured-IR input identity changed");

    const contract::AudioAssetSpec asset{
        contract::AudioAssetId{1},
        {"bmw-m52b28-configured-ir", "asset-semantic-resolution"},
        {"bmw-m52b28-configured-ir-evidence", "asset-evidence-resolution"},
        {raw_sha256, "asset-digest-resolution"},
        {
            {
                contract::AudioSampleEncoding::pcm_s16le,
                contract::AudioChannelLayout::mono,
                {kConfiguredIrSampleRateHz, 1},
                33705,
            },
            "asset-media-resolution",
        },
    };
    const Fixture::OwnedPayload payload{asset.id, std::move(bytes)};
    const contract::ResolvedValue<double> gain{0.001, "route-ir-gain-resolution"};
    const auto result =
        compile_presentation_asset(asset, {payload.id, payload.bytes},
                                   static_ir_conversion_method_identity(), gain);
    const auto &compiled =
        expect_compiled(result, "canonical BMW configured IR was rejected");
    const auto kernel_result = compile_presentation_convolution_kernel(
        compiled, fixed_overlap_save_convolution_method_identity());
    const auto &kernel = expect_compiled_kernel(
        kernel_result, "canonical BMW convolution kernel was rejected");
    expect(compiled.meaningful_support_frame_count() == 6907 &&
               digest_hex(compiled.coefficient_f64le_identity().payload_sha256) ==
                   "940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598" &&
               digest_hex(kernel.spectrum_complex_f64le_identity().payload_sha256) ==
                   "a1a12fc0224ecdf824e402562ed6b5fd31d915278693a8cfaaeea41a5cf957d2",
           "canonical BMW configured-IR kernel identity changed");
}

void run_tests(const std::string &configured_ir_path) {
    test_valid_asset_and_deterministic_identities();
    test_identity_and_hash_rejection();
    test_media_rejection();
    test_method_and_gain_rejection();
    test_fixed_kernel_shape_rejection();
    test_convolution_method_rejection();
    test_empty_meaningful_support_rejection();
    test_canonical_bmw_asset(configured_ir_path);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected exactly one configured-IR input path"};
        }
        run_tests(argv[1]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "presentation asset compiler test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
