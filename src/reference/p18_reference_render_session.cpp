#include "reference/p18_reference_render_session.hpp"

#include "artifacts/p18_audition_wav_encoder.hpp"
#include "artifacts/sha256_stream.hpp"
#include "dsp/p18_primitives.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/p18_mastering.hpp"
#include "presentation/p18_overlap_save_convolver.hpp"
#include "presentation/p18_source_stage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::reference {
namespace {

using artifacts::WavEncoder;
using artifacts::WavEncodingStatus;
using presentation::P18ConditionedSourceFrame;

constexpr std::size_t kInputFramesPerBlock =
    presentation::kP18PhysicsFramesPerMethodBlock;
constexpr std::size_t kSourceFramesPerBlock =
    presentation::kP18SourceFramesPerMethodBlock;
constexpr std::size_t kRouteCount = presentation::kP18ExhaustRouteCount;
constexpr std::size_t kStemCount = kRouteCount * 3;
constexpr std::size_t kFloatWaveCount = kStemCount + 1;

static_assert(kRouteCount == 2);
static_assert(kP18ReferenceSeedRouteCount == kRouteCount);
static_assert(kP18ReferenceAudioArtifactCount == kFloatWaveCount + 1);
static_assert(kP18ReferenceAuditRecordCount / kInputFramesPerBlock ==
              kP18ReferenceProcessedBlockCount);
static_assert(kP18ReferenceProcessedBlockCount ==
              kP18ReferenceWarmupBlockCount + kP18ReferencePublishedBlockCount);
static_assert(kP18ReferenceProcessedSourceFrameCount ==
              kP18ReferenceProcessedBlockCount * kSourceFramesPerBlock);
static_assert(kP18ReferenceWarmupSourceFrameCount ==
              kP18ReferenceWarmupBlockCount * kSourceFramesPerBlock);
static_assert(kP18ReferencePublishedSourceFrameCount ==
              kP18ReferencePublishedBlockCount * kSourceFramesPerBlock);
static_assert(kP18ReferencePublishedSourceFrameCount ==
              presentation::kP18AudibleFrameCount);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_0_dry) == 0);
static_assert(
    static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_0_configured_ir) == 1);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_0_selected) ==
              2);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_1_dry) == 3);
static_assert(
    static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_1_configured_ir) == 4);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::exhaust_1_selected) ==
              5);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::master_raw) == 6);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::master_audition) ==
              7);

struct RenderScratch {
    std::array<presentation::ExhaustExcitationFrame, kInputFramesPerBlock> input{};
    std::array<P18ConditionedSourceFrame, kSourceFramesPerBlock> conditioned{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> dry{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> configured_ir{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> selected{};
    std::array<std::array<float, kSourceFramesPerBlock>, kStemCount> stems{};
    std::array<float, kSourceFramesPerBlock> raw{};
    std::array<std::int32_t, kSourceFramesPerBlock> pcm24{};
    std::array<presentation::P18MasteredFrame, kSourceFramesPerBlock> mastered{};
    std::array<std::vector<std::byte>, 5> mastering_bytes{};

    RenderScratch() {
        for (std::size_t index = 0; index < 4; ++index) {
            mastering_bytes[index].resize(kSourceFramesPerBlock * 4);
        }
        mastering_bytes[4].resize(kSourceFramesPerBlock * 3);
    }
};

[[nodiscard]] std::size_t artifact_index(P18ReferenceAudioArtifact artifact) noexcept {
    return static_cast<std::size_t>(artifact);
}

[[nodiscard]] WavEncoder make_float_wave_encoder() {
    const contract::AudioContract audio{
        {192'000, 1}, kP18ReferencePublishedSourceFrameCount, "mono", "float32le"};
    auto result = artifacts::make_wav_encoder(audio, {16U * 1024U});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct P1.8 Float32 WAVE encoder: " +
                               error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] artifacts::P18AuditionWaveEncoder make_audition_wave_encoder() {
    auto result = artifacts::make_p18_audition_wave_encoder({16U * 1024U});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct P1.8 audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::P18AuditionWaveEncoder>(std::move(result));
}

void require_encoding_success(const WavEncodingStatus &status, const char *operation) {
    if (status.has_value()) {
        throw std::runtime_error{std::string{operation} + ": " + status->message};
    }
}

void store_u32le(std::span<std::byte, 4> output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::byte>(value & UINT32_C(0xff));
    output[1] = static_cast<std::byte>((value >> 8U) & UINT32_C(0xff));
    output[2] = static_cast<std::byte>((value >> 16U) & UINT32_C(0xff));
    output[3] = static_cast<std::byte>((value >> 24U) & UINT32_C(0xff));
}

[[nodiscard]] std::array<presentation::P18RouteConditioningSeeds, kRouteCount>
presentation_seeds(
    const std::array<P18ReferenceRouteSeeds, kP18ReferenceSeedRouteCount> &seeds) {
    std::array<presentation::P18RouteConditioningSeeds, kRouteCount> result{};
    for (std::size_t route = 0; route < result.size(); ++route) {
        result[route] = {
            {seeds[route].jitter.initial_state, seeds[route].jitter.stream},
            {seeds[route].air_noise.initial_state, seeds[route].air_noise.stream},
        };
    }
    return result;
}

void begin_encoders(std::array<WavEncoder, kFloatWaveCount> &encoders,
                    artifacts::P18AuditionWaveEncoder &audition,
                    const P18ReferenceAudioConsumers &consumers) {
    for (std::size_t index = 0; index < encoders.size(); ++index) {
        require_encoding_success(encoders[index].begin(consumers[index]),
                                 "P1.8 WAVE header emission failed");
    }
    require_encoding_success(
        audition.begin(
            consumers[artifact_index(P18ReferenceAudioArtifact::master_audition)]),
        "P1.8 audition WAVE prefix emission failed");
}

void write_published_block(RenderScratch &scratch, std::uint64_t audible_frame,
                           std::array<WavEncoder, kFloatWaveCount> &encoders,
                           artifacts::P18AuditionWaveEncoder &audition,
                           const P18ReferenceAudioConsumers &consumers,
                           std::array<artifacts::detail::Sha256Stream, 5> &hashes,
                           P18ReferenceRenderStats &stats) {
    for (std::size_t route = 0; route < kRouteCount; ++route) {
        const auto base = route * 3;
        for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
            scratch.stems[base][frame] =
                dsp::p18_publish_calibrated_float32(scratch.dry[route][frame]);
            scratch.stems[base + 1][frame] = dsp::p18_publish_calibrated_float32(
                scratch.configured_ir[route][frame]);
            scratch.stems[base + 2][frame] =
                dsp::p18_publish_calibrated_float32(scratch.selected[route][frame]);
        }
    }

    presentation::p18_master_reference_block(scratch.stems[2], scratch.stems[5],
                                             audible_frame, scratch.mastered);
    for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
        const auto &mastered = scratch.mastered[frame];
        scratch.raw[frame] = mastered.raw;
        scratch.pcm24[frame] = mastered.pcm24;
        const auto byte_offset = frame * 4;
        store_u32le(
            std::span<std::byte, 4>{scratch.mastering_bytes[0].data() + byte_offset, 4},
            std::bit_cast<std::uint32_t>(mastered.raw));
        store_u32le(
            std::span<std::byte, 4>{scratch.mastering_bytes[1].data() + byte_offset, 4},
            std::bit_cast<std::uint32_t>(mastered.monitor));
        store_u32le(
            std::span<std::byte, 4>{scratch.mastering_bytes[2].data() + byte_offset, 4},
            std::bit_cast<std::uint32_t>(mastered.faded));
        store_u32le(
            std::span<std::byte, 4>{scratch.mastering_bytes[3].data() + byte_offset, 4},
            static_cast<std::uint32_t>(mastered.s32));
        const auto encoded = presentation::p18_serialize_pcm24le(mastered.pcm24);
        std::copy(encoded.begin(), encoded.end(),
                  scratch.mastering_bytes[4].begin() +
                      static_cast<std::ptrdiff_t>(frame * 3));
        stats.saturation_count += mastered.saturated ? 1U : 0U;
        stats.faded_absolute_peak =
            std::max(stats.faded_absolute_peak, std::abs(mastered.faded));
    }

    for (std::size_t index = 0; index < hashes.size(); ++index) {
        hashes[index].update(scratch.mastering_bytes[index]);
    }
    for (std::size_t index = 0; index < kStemCount; ++index) {
        require_encoding_success(encoders[index].write_float32_interleaved(
                                     scratch.stems[index], consumers[index]),
                                 "P1.8 stem WAVE payload emission failed");
    }
    require_encoding_success(
        encoders[6].write_float32_interleaved(
            scratch.raw,
            consumers[artifact_index(P18ReferenceAudioArtifact::master_raw)]),
        "P1.8 raw-master WAVE payload emission failed");
    require_encoding_success(
        audition.write_pcm24(
            scratch.pcm24,
            consumers[artifact_index(P18ReferenceAudioArtifact::master_audition)]),
        "P1.8 audition WAVE payload emission failed");
}

} // namespace

P18ReferenceRenderStats render_p18_reference_audio(
    const P18DecodedReferenceAudit &audit,
    std::array<P18ReferenceRouteSeeds, kP18ReferenceSeedRouteCount> route_seeds,
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir,
    const P18ReferenceAudioConsumers &consumers) {
    if (audit.frames.size() != kP18ReferenceAuditRecordCount) {
        throw std::invalid_argument{
            "P1.8 render requires exactly 170,000 decoded audit frames"};
    }
    if (!configured_ir) {
        throw std::invalid_argument{"P1.8 render requires a configured IR kernel"};
    }
    if (std::any_of(consumers.begin(), consumers.end(),
                    [](const auto &consumer) { return !consumer; })) {
        throw std::invalid_argument{"P1.8 render requires eight output consumers"};
    }

    presentation::P18SourceStage source_stage{presentation_seeds(route_seeds)};
    presentation::P18CausalOverlapSaveConvolver convolvers[kRouteCount]{
        presentation::P18CausalOverlapSaveConvolver{configured_ir},
        presentation::P18CausalOverlapSaveConvolver{std::move(configured_ir)},
    };
    std::array<WavEncoder, kFloatWaveCount> encoders{
        make_float_wave_encoder(), make_float_wave_encoder(), make_float_wave_encoder(),
        make_float_wave_encoder(), make_float_wave_encoder(), make_float_wave_encoder(),
        make_float_wave_encoder(),
    };
    auto audition = make_audition_wave_encoder();
    auto scratch = std::make_unique<RenderScratch>();
    std::array<artifacts::detail::Sha256Stream, 5> hashes{};
    P18ReferenceRenderStats stats{};
    begin_encoders(encoders, audition, consumers);

    std::uint64_t audible_frame = 0;
    for (std::size_t block = 0; block < kP18ReferenceProcessedBlockCount; ++block) {
        const auto audit_offset = block * kInputFramesPerBlock;
        for (std::size_t frame = 0; frame < kInputFramesPerBlock; ++frame) {
            scratch->input[frame].route_values_engine_sim_source_unit =
                audit.frames[audit_offset + frame].pre_dsp_buses;
        }
        const auto input_view =
            presentation::ExhaustExcitationBlockView::borrow_for_callback(
                audit_offset, presentation::kP18ExcitationRateHz,
                presentation::kP18ReferenceRouteIds, scratch->input);
        const auto extent = source_stage.process(input_view, scratch->conditioned);
        if (extent.first_input_frame_index != audit_offset ||
            extent.first_source_frame_index != block * kSourceFramesPerBlock) {
            throw std::logic_error{"P1.8 source-stage extent lost continuity"};
        }

        for (std::size_t route = 0; route < kRouteCount; ++route) {
            for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
                scratch->dry[route][frame] =
                    scratch->conditioned[frame]
                        .route_values_engine_sim_source_unit[route];
            }
            convolvers[route].process(scratch->dry[route],
                                      scratch->configured_ir[route]);
            for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
                scratch->selected[route][frame] =
                    1.0 * scratch->configured_ir[route][frame] +
                    (1.0 - 1.0) * scratch->dry[route][frame];
            }
        }

        if (block >= kP18ReferenceWarmupBlockCount) {
            write_published_block(*scratch, audible_frame, encoders, audition,
                                  consumers, hashes, stats);
            audible_frame += kSourceFramesPerBlock;
        }
    }

    if (source_stage.next_input_frame_index() != kP18ReferenceAuditRecordCount ||
        source_stage.next_source_frame_index() !=
            kP18ReferenceProcessedSourceFrameCount ||
        audible_frame != kP18ReferencePublishedSourceFrameCount) {
        throw std::logic_error{"P1.8 render produced an incomplete frame interval"};
    }
    for (std::size_t index = 0; index < encoders.size(); ++index) {
        require_encoding_success(encoders[index].finish(consumers[index]),
                                 "P1.8 WAVE finalization failed");
        if (encoders[index].frames_written() !=
                kP18ReferencePublishedSourceFrameCount ||
            encoders[index].bytes_emitted() != kP18ReferenceFloatWaveByteCount) {
            throw std::logic_error{"P1.8 Float32 WAVE length changed"};
        }
    }
    require_encoding_success(
        audition.finish(
            consumers[artifact_index(P18ReferenceAudioArtifact::master_audition)]),
        "P1.8 audition WAVE finalization failed");
    if (audition.frames_written() != kP18ReferencePublishedSourceFrameCount ||
        audition.bytes_emitted() != kP18ReferenceAuditionWaveByteCount) {
        throw std::logic_error{"P1.8 audition WAVE length changed"};
    }

    stats.input_frame_count = kP18ReferenceAuditRecordCount;
    stats.processed_block_count = kP18ReferenceProcessedBlockCount;
    stats.warmup_block_count = kP18ReferenceWarmupBlockCount;
    stats.published_block_count = kP18ReferencePublishedBlockCount;
    stats.processed_source_frame_count = kP18ReferenceProcessedSourceFrameCount;
    stats.warmup_source_frame_count = kP18ReferenceWarmupSourceFrameCount;
    stats.published_source_frame_count = kP18ReferencePublishedSourceFrameCount;
    stats.raw_float32_payload_sha256 = hashes[0].finish();
    stats.monitoring_float32_payload_sha256 = hashes[1].finish();
    stats.faded_float32_payload_sha256 = hashes[2].finish();
    stats.s32le_payload_sha256 = hashes[3].finish();
    stats.pcm24le_payload_sha256 = hashes[4].finish();
    return stats;
}

} // namespace engine_sim_offline::reference
