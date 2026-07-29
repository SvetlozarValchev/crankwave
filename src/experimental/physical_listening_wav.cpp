#include "experimental/physical_listening_wav.hpp"

#include "artifacts/audition_wav_encoder.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace engine_sim_offline::experimental {
namespace {

constexpr std::uint64_t kMillisecondsPerSecond = 1'000;
constexpr std::uint64_t kEdgeFadeMilliseconds = 50;
constexpr std::size_t kPcmBlockFrameCount = 4'096;

[[nodiscard]] std::uint64_t
edge_fade_frame_count(std::uint32_t sample_rate_hz) noexcept {
    const auto numerator =
        static_cast<std::uint64_t>(sample_rate_hz) * kEdgeFadeMilliseconds;
    return (numerator + kMillisecondsPerSecond / 2U) / kMillisecondsPerSecond;
}

[[nodiscard]] std::string
wav_encoding_failure(std::string operation, const artifacts::WavEncodingError &error) {
    operation += " at ";
    operation += error.path;
    operation += ": ";
    operation += error.message;
    return operation;
}

[[nodiscard]] std::string path_text(const std::filesystem::path &path) {
    const auto text = path.string();
    return text.empty() ? std::string{"<empty path>"} : text;
}

} // namespace

PhysicalListeningWavResult
write_physical_listening_wav(const std::filesystem::path &output_path,
                             std::span<const double> coherent_pressure_pa,
                             std::uint32_t sample_rate_hz,
                             double pascals_per_full_scale, std::string title) {
    if (output_path.empty()) {
        return std::string{"physical listening WAVE output path is empty"};
    }
    if (coherent_pressure_pa.empty()) {
        return std::string{"physical listening WAVE requires at least one frame"};
    }
    if (sample_rate_hz == 0) {
        return std::string{"physical listening WAVE sample rate must be positive"};
    }
    if (!std::isfinite(pascals_per_full_scale) || pascals_per_full_scale <= 0.0) {
        return std::string{
            "physical listening WAVE Pa/full-scale calibration must be finite "
            "and positive"};
    }
    if (coherent_pressure_pa.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return std::string{"physical listening WAVE frame count exceeds uint64"};
    }

    const auto frame_count = static_cast<std::uint64_t>(coherent_pressure_pa.size());
    const auto fade_frame_count = edge_fade_frame_count(sample_rate_hz);
    if (fade_frame_count == 0) {
        return std::string{
            "physical listening WAVE sample rate cannot represent a 50 ms fade"};
    }
    if (fade_frame_count > frame_count / 2U) {
        return std::string{
            "physical listening WAVE must be at least 100 ms for two 50 ms edge "
            "fades"};
    }

    presentation::MasteringSettings mastering{frame_count, fade_frame_count,
                                              fade_frame_count, 1.0F};
    PhysicalListeningWavStats stats{};
    stats.frame_count = frame_count;

    for (std::uint64_t frame = 0; frame < frame_count; ++frame) {
        const auto pressure = coherent_pressure_pa[static_cast<std::size_t>(frame)];
        if (!std::isfinite(pressure)) {
            return std::string{"physical listening pressure sample "} +
                   std::to_string(frame) + " is non-finite";
        }

        const auto normalized = pressure / pascals_per_full_scale;
        const auto normalized_float = static_cast<float>(normalized);
        if (!std::isfinite(normalized) || !std::isfinite(normalized_float)) {
            return std::string{"physical listening calibrated sample "} +
                   std::to_string(frame) + " is outside finite Float32 range";
        }

        stats.peak_pressure_pa = std::max(stats.peak_pressure_pa, std::abs(pressure));
        stats.peak_normalized = std::max(stats.peak_normalized, std::abs(normalized));

        const auto fade_gain = presentation::audition_fade_gain(frame, mastering);
        const auto faded =
            static_cast<float>(static_cast<double>(normalized_float) * fade_gain);
        const auto quantized = presentation::quantize_pcm24(faded);
        stats.saturated_sample_count += quantized.saturated ? 1U : 0U;
    }

    if (stats.saturated_sample_count != 0) {
        return std::string{"physical listening WAVE calibration saturated "} +
               std::to_string(stats.saturated_sample_count) +
               " samples; increase pascals_per_full_scale";
    }

    const contract::AudioContract audio{
        {sample_rate_hz, 1},
        frame_count,
        "mono",
        "pcm_s24le",
    };
    artifacts::AuditionWaveMetadata metadata{
        "Coherent acoustic pressure; explicit Pa/full-scale calibration; 50 ms "
        "quarter-sine edge fades; no EQ, resampling, compression, convolution, "
        "impulse response, or synthetic noise",
        std::move(title),
        "engine-sim-offline physical listening",
    };
    auto encoder_result =
        artifacts::make_audition_wave_encoder(audio, std::move(metadata));
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&encoder_result)) {
        return wav_encoding_failure("cannot construct physical listening WAVE", *error);
    }
    auto encoder = std::get<artifacts::AuditionWaveEncoder>(std::move(encoder_result));

    std::error_code filesystem_error;
    const auto output_exists = std::filesystem::exists(output_path, filesystem_error);
    if (filesystem_error) {
        return std::string{"cannot inspect physical listening WAVE output "} +
               path_text(output_path) + ": " + filesystem_error.message();
    }
    if (output_exists) {
        return std::string{"physical listening WAVE output already exists: "} +
               path_text(output_path);
    }

    std::ofstream output{output_path,
                         std::ios::binary | std::ios::out | std::ios::trunc};
    if (!output.is_open()) {
        return std::string{"cannot create physical listening WAVE output: "} +
               path_text(output_path);
    }

    const auto fail_and_remove = [&](std::string message) {
        if (output.is_open()) {
            output.close();
        }
        std::error_code remove_error;
        std::filesystem::remove(output_path, remove_error);
        if (remove_error) {
            message += "; also failed to remove incomplete output: ";
            message += remove_error.message();
        }
        return PhysicalListeningWavResult{std::move(message)};
    };

    std::uint64_t written_byte_count = 0;
    std::string consumer_failure;
    const artifacts::WavChunkConsumer consume = [&](std::uint64_t byte_offset,
                                                    std::span<const std::byte> bytes) {
        if (byte_offset != written_byte_count) {
            consumer_failure =
                "physical listening WAVE encoder emitted a noncontiguous chunk";
            return false;
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            consumer_failure = "failed while writing physical listening WAVE output";
            return false;
        }
        written_byte_count += static_cast<std::uint64_t>(bytes.size());
        return true;
    };

    if (const auto status = encoder.begin(consume); status.has_value()) {
        auto message =
            wav_encoding_failure("cannot begin physical listening WAVE", *status);
        if (!consumer_failure.empty()) {
            message += ": " + consumer_failure;
        }
        return fail_and_remove(std::move(message));
    }

    std::array<std::int32_t, kPcmBlockFrameCount> pcm{};
    for (std::uint64_t first_frame = 0; first_frame < frame_count;) {
        const auto block_frame_count = static_cast<std::size_t>(
            std::min<std::uint64_t>(pcm.size(), frame_count - first_frame));
        for (std::size_t index = 0; index < block_frame_count; ++index) {
            const auto frame = first_frame + static_cast<std::uint64_t>(index);
            const auto normalized =
                coherent_pressure_pa[static_cast<std::size_t>(frame)] /
                pascals_per_full_scale;
            const auto fade_gain = presentation::audition_fade_gain(frame, mastering);
            const auto faded = static_cast<float>(
                static_cast<double>(static_cast<float>(normalized)) * fade_gain);
            const auto quantized = presentation::quantize_pcm24(faded);
            if (quantized.saturated) {
                return fail_and_remove(
                    "physical listening WAVE saturated during streaming");
            }
            pcm[index] = quantized.pcm24;
        }

        const auto status =
            encoder.write_pcm24(std::span{pcm}.first(block_frame_count), consume);
        if (status.has_value()) {
            auto message =
                wav_encoding_failure("cannot write physical listening WAVE", *status);
            if (!consumer_failure.empty()) {
                message += ": " + consumer_failure;
            }
            return fail_and_remove(std::move(message));
        }
        first_frame += static_cast<std::uint64_t>(block_frame_count);
    }

    if (const auto status = encoder.finish(consume); status.has_value()) {
        auto message =
            wav_encoding_failure("cannot finish physical listening WAVE", *status);
        if (!consumer_failure.empty()) {
            message += ": " + consumer_failure;
        }
        return fail_and_remove(std::move(message));
    }

    output.close();
    if (!output) {
        return fail_and_remove("failed while closing physical listening WAVE output");
    }
    if (written_byte_count != encoder.expected_byte_count()) {
        return fail_and_remove(
            "physical listening WAVE byte count differs from encoder contract");
    }

    return stats;
}

} // namespace engine_sim_offline::experimental
