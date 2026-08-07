#include "engine_sim_offline/contract/common.hpp"
#include "presentation/overlap_save_convolver.hpp"
#include "presentation/pcm16_ir_decoder.hpp"
#include "presentation/presentation_asset_compiler.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

std::vector<std::byte> read_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        throw std::runtime_error{"cannot open IR library payload"};
    }
    const auto end = input.tellg();
    if (end <= 0 || end > 1024 * 1024) {
        throw std::runtime_error{"IR library payload escaped the bounded envelope"};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != bytes.size()) {
        throw std::runtime_error{"IR library payload read was incomplete"};
    }
    return bytes;
}

contract::AudioAssetSpec make_asset(std::uint32_t id,
                                    const contract::Sha256Digest &digest,
                                    std::uint16_t bits_per_sample,
                                    std::size_t frame_count) {
    const contract::AudioMediaContract media{
        bits_per_sample == 16U ? contract::AudioSampleEncoding::pcm_s16le
                               : contract::AudioSampleEncoding::pcm_s24le,
        contract::AudioChannelLayout::mono,
        {presentation::kConfiguredIrSampleRateHz, 1U},
        static_cast<std::uint64_t>(frame_count),
    };
    return {
        contract::AudioAssetId{id},
        {"ir-library-entry", "runtime-sweep.semantic"},
        {"ir-library-entry-evidence", "runtime-sweep.evidence"},
        {digest, "runtime-sweep.digest"},
        {media, "runtime-sweep.media"},
    };
}

void run_sweep(const std::filesystem::path &directory) {
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator{directory}) {
        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);
    expect(paths.size() == 73U,
           "installed authoring IR library does not contain exactly 73 payloads");

    std::size_t pcm24_count = 0;
    std::size_t fixed_count = 0;
    std::size_t partitioned_count = 0;
    std::size_t maximum_partition_count = 0;
    std::size_t maximum_coefficient_count = 0;
    std::uint32_t runtime_id = 1U;
    for (const auto &path : paths) {
        const auto bytes = read_file(path);
        const auto digest = contract::sha256(bytes);
        expect(path.stem().string() == digest_hex(digest),
               "IR library payload filename does not bind its exact bytes");
        const auto decode_result = presentation::decode_pcm_ir_wave_v2(bytes);
        const auto *decoded =
            std::get_if<presentation::DecodedPcmIrV2>(&decode_result);
        expect(decoded != nullptr && !decoded->samples.empty() &&
                   decoded->meaningful_support_frames > 0U,
               "authoring IR library payload is not admitted by the v2 decoder");
        pcm24_count += decoded->bits_per_sample == 24U ? 1U : 0U;

        const auto asset = make_asset(runtime_id++, digest,
                                      decoded->bits_per_sample,
                                      decoded->samples.size());
        const contract::ResolvedValue<double> gain{
            0.001, "runtime-sweep.configured-gain"};
        const auto compiled_result = presentation::compile_presentation_asset(
            asset, {asset.id, bytes},
            presentation::hybrid_static_ir_conversion_method_identity(), gain);
        const auto *compiled =
            std::get_if<presentation::CompiledPresentationAsset>(&compiled_result);
        expect(compiled != nullptr && !compiled->coefficients().empty(),
               "authoring IR library payload did not compile through hybrid-v2");
        const auto kernel_result =
            presentation::compile_presentation_convolution_kernel(
                *compiled,
                presentation::hybrid_partitioned_convolution_method_identity());
        const auto *kernel = std::get_if<
            presentation::CompiledPresentationConvolutionKernel>(&kernel_result);
        expect(kernel != nullptr,
               "authoring IR library payload did not produce a v2 runtime kernel");

        if (kernel->kernel()) {
            ++fixed_count;
        } else {
            expect(kernel->partitioned_kernel() != nullptr &&
                       kernel->partitioned_kernel()->coefficient_count() ==
                           compiled->coefficients().size(),
                   "long IR kernel was truncated or misclassified");
            ++partitioned_count;
            maximum_partition_count =
                std::max(maximum_partition_count,
                         kernel->partitioned_kernel()->partition_count());
            maximum_coefficient_count =
                std::max(maximum_coefficient_count,
                         kernel->partitioned_kernel()->coefficient_count());
        }

        std::vector<double> impulse(
            dsp::PartitionedConvolutionLimits::partition_frame_count, 0.0);
        std::vector<double> output(impulse.size());
        impulse.front() = 1.0;
        presentation::CausalConfiguredIrConvolver convolver{
            kernel->runtime_kernel()};
        convolver.process(impulse, output);
        expect(std::ranges::all_of(output, [](double value) {
                   return std::isfinite(value);
               }),
               "authoring IR runtime kernel produced a non-finite first block");

        // The largest approved IR is also exercised with two independent kernel
        // instances (the conservative different-route-gain memory shape). In
        // wasm32 this test is linked at a fixed 128 MiB with memory growth
        // disabled, making allocation success part of admission.
        if (kernel->partitioned_kernel() != nullptr &&
            kernel->partitioned_kernel()->partition_count() == 101U) {
            auto independent_kernel =
                std::make_shared<const dsp::PartitionedConvolutionKernel>(
                    compiled->coefficients());
            presentation::CausalConfiguredIrConvolver second_route{
                dsp::RuntimeConvolutionKernel{std::move(independent_kernel)}};
            std::vector<double> second_output(impulse.size());
            second_route.process(impulse, second_output);
            expect(std::ranges::all_of(second_output, [](double value) {
                       return std::isfinite(value);
                   }),
                   "maximum IR failed the independent-kernel two-route runtime shape");
        }

        if (decoded->bits_per_sample == 16U &&
            decoded->samples.size() <=
                presentation::kMaximumConfiguredIrFrameCount &&
            compiled->coefficients().size() <=
                dsp::FixedConvolutionKernel::coefficient_count) {
            const auto legacy_result = presentation::compile_presentation_asset(
                asset, {asset.id, bytes},
                presentation::static_ir_conversion_method_identity(), gain);
            const auto *legacy =
                std::get_if<presentation::CompiledPresentationAsset>(&legacy_result);
            expect(legacy != nullptr &&
                       legacy->coefficient_f64le_identity() ==
                           compiled->coefficient_f64le_identity() &&
                       std::ranges::equal(legacy->coefficients(),
                                          compiled->coefficients()),
                   "hybrid-v2 changed a legacy IR coefficient byte");
            const auto legacy_kernel_result =
                presentation::compile_presentation_convolution_kernel(
                    *legacy,
                    presentation::fixed_overlap_save_convolution_method_identity());
            const auto *legacy_kernel = std::get_if<
                presentation::CompiledPresentationConvolutionKernel>(
                &legacy_kernel_result);
            expect(legacy_kernel != nullptr &&
                       legacy_kernel->spectrum_complex_f64le_identity() ==
                           kernel->spectrum_complex_f64le_identity(),
                   "hybrid-v2 changed a legacy fixed-kernel spectrum byte");
        }
    }

    expect(pcm24_count == 1U && fixed_count == 24U &&
               partitioned_count == 49U && maximum_partition_count == 101U &&
               maximum_coefficient_count == 384292U,
           "all-IR runtime compatibility classification changed unexpectedly");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected one IR payload directory"};
        }
        run_sweep(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "IR library runtime test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
