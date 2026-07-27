#include "reference/p18_reference_render_session.hpp"

#include "artifacts/p18_audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
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
#include <limits>
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

struct P18ReferenceSchedule {
    std::size_t input_frame_count = 0;
    std::size_t processed_block_count = 0;
    std::size_t warmup_block_count = 0;
    std::size_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t warmup_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;
    contract::RationalRateHz capture_rate;
    std::array<contract::RouteId, kRouteCount> route_ids{};
};

[[nodiscard]] P18ReferenceSchedule require_reference_schedule() {
    const auto &catalog = p18_reference_catalog_v1();
    const auto &capture = catalog.expected_capture;
    const contract::RationalRateHz source_rate{
        presentation::P18CausalReconstruction::kSourceRate, 1};
    if (capture.expected_rates.physics != presentation::kP18ExcitationRateHz ||
        capture.expected_rates.capture != presentation::kP18ExcitationRateHz ||
        capture.expected_rates.source_processing != source_rate ||
        capture.expected_rates.acoustic != source_rate ||
        capture.expected_rates.delivery != source_rate) {
        throw std::logic_error{
            "P1.8 catalog rates differ from the implemented source stage"};
    }
    if (capture.expected_record_count != kP18ReferenceAuditRecordCount ||
        capture.expected_consumed_start_record != kP18ReferenceAuditIntervalStart ||
        capture.expected_consumed_end_record_exclusive !=
            kP18ReferenceAuditIntervalEndExclusive ||
        capture.expected_consumed_start_record != 0 ||
        capture.expected_consumed_end_record_exclusive !=
            capture.expected_record_count ||
        capture.expected_audible_start_record <
            capture.expected_consumed_start_record ||
        capture.expected_audible_end_record_exclusive !=
            capture.expected_consumed_end_record_exclusive ||
        capture.expected_physics_frames_per_block != kInputFramesPerBlock ||
        capture.expected_source_frames_per_block != kSourceFramesPerBlock) {
        throw std::logic_error{
            "P1.8 catalog window differs from the strict decoder or source stage"};
    }

    const auto input_frames = capture.expected_consumed_end_record_exclusive -
                              capture.expected_consumed_start_record;
    const auto warmup_input_frames =
        capture.expected_audible_start_record - capture.expected_consumed_start_record;
    const auto published_input_frames = capture.expected_audible_end_record_exclusive -
                                        capture.expected_audible_start_record;
    if (input_frames % kInputFramesPerBlock != 0 ||
        warmup_input_frames % kInputFramesPerBlock != 0 ||
        published_input_frames % kInputFramesPerBlock != 0) {
        throw std::logic_error{
            "P1.8 catalog window is not aligned to complete method blocks"};
    }

    const auto processed_blocks = input_frames / kInputFramesPerBlock;
    const auto warmup_blocks = warmup_input_frames / kInputFramesPerBlock;
    const auto published_blocks = published_input_frames / kInputFramesPerBlock;
    const auto source_frames_for = [](std::uint64_t blocks) {
        if (blocks >
            std::numeric_limits<std::uint64_t>::max() / kSourceFramesPerBlock) {
            throw std::logic_error{"P1.8 catalog source-frame horizon overflows"};
        }
        return blocks * kSourceFramesPerBlock;
    };
    const auto processed_source_frames = source_frames_for(processed_blocks);
    const auto warmup_source_frames = source_frames_for(warmup_blocks);
    const auto published_source_frames = source_frames_for(published_blocks);
    if (processed_blocks != warmup_blocks + published_blocks ||
        capture.expected_total_source_frame_count != processed_source_frames ||
        capture.expected_audible_source_start_frame != warmup_source_frames ||
        capture.expected_audible_source_end_frame_exclusive !=
            processed_source_frames ||
        capture.expected_delivery_frame_count != published_source_frames ||
        published_source_frames != presentation::kP18AudibleFrameCount ||
        published_source_frames != artifacts::kP18AuditionWaveFrameCount ||
        processed_blocks > std::numeric_limits<std::size_t>::max() ||
        warmup_blocks > std::numeric_limits<std::size_t>::max() ||
        published_blocks > std::numeric_limits<std::size_t>::max() ||
        input_frames > std::numeric_limits<std::size_t>::max()) {
        throw std::logic_error{
            "P1.8 catalog source-frame schedule is internally inconsistent"};
    }

    P18ReferenceSchedule schedule{
        static_cast<std::size_t>(input_frames),
        static_cast<std::size_t>(processed_blocks),
        static_cast<std::size_t>(warmup_blocks),
        static_cast<std::size_t>(published_blocks),
        processed_source_frames,
        warmup_source_frames,
        published_source_frames,
        capture.expected_rates.capture,
        {},
    };
    for (std::size_t index = 0; index < catalog.expected_routes.size(); ++index) {
        const auto &route = catalog.expected_routes[index];
        if (static_cast<std::size_t>(route.route) != index ||
            route.expected_route_id != presentation::kP18ReferenceRouteIds[index]) {
            throw std::logic_error{
                "P1.8 route catalog differs from the implemented source stage"};
        }
        schedule.route_ids[index] = route.expected_route_id;
    }
    for (const auto &audio : catalog.expected_audio) {
        if (catalog.find_expected_audio(audio.audio) != &audio) {
            throw std::logic_error{
                "P1.8 audio catalog is not exhaustive canonical enum order"};
        }
    }
    return schedule;
}

[[nodiscard]] const P18ExpectedAudioComparator &
require_expected_audio(P18ReferenceAudioArtifact artifact) {
    const auto *expected = p18_reference_catalog_v1().find_expected_audio(artifact);
    if (expected == nullptr) {
        throw std::logic_error{"P1.8 audio catalog lookup failed"};
    }
    return *expected;
}

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

[[nodiscard]] std::size_t artifact_index(P18ReferenceAudioArtifact artifact) {
    const auto &catalog = p18_reference_catalog_v1();
    const auto &expected = require_expected_audio(artifact);
    return static_cast<std::size_t>(&expected - catalog.expected_audio.data());
}

[[nodiscard]] WavEncoder make_float_wave_encoder() {
    const auto &capture = p18_reference_catalog_v1().expected_capture;
    const contract::AudioContract audio{capture.expected_rates.delivery,
                                        capture.expected_delivery_frame_count, "mono",
                                        "float32le"};
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
                           std::array<contract::detail::Sha256Stream, 5> &hashes,
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
    const auto schedule = require_reference_schedule();
    if (audit.frames.size() != schedule.input_frame_count) {
        throw std::invalid_argument{
            "P1.8 render input differs from the catalog capture interval"};
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
    std::array<contract::detail::Sha256Stream, 5> hashes{};
    P18ReferenceRenderStats stats{};
    begin_encoders(encoders, audition, consumers);

    std::uint64_t audible_frame = 0;
    for (std::size_t block = 0; block < schedule.processed_block_count; ++block) {
        const auto audit_offset = block * kInputFramesPerBlock;
        for (std::size_t frame = 0; frame < kInputFramesPerBlock; ++frame) {
            scratch->input[frame].route_values_engine_sim_source_unit =
                audit.frames[audit_offset + frame].pre_dsp_buses;
        }
        const auto input_view =
            presentation::ExhaustExcitationBlockView::borrow_for_callback(
                audit_offset, schedule.capture_rate, schedule.route_ids,
                scratch->input);
        const auto extent = source_stage.process(input_view, scratch->conditioned);
        if (extent.first_input_frame_index != audit_offset ||
            extent.first_source_frame_index != block * kSourceFramesPerBlock) {
            throw std::logic_error{"P1.8 source-stage extent lost continuity"};
        }
        stats.input_frame_count += static_cast<std::uint64_t>(extent.input_frame_count);
        ++stats.processed_block_count;
        stats.processed_source_frame_count +=
            static_cast<std::uint64_t>(extent.source_frame_count);

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

        if (block < schedule.warmup_block_count) {
            ++stats.warmup_block_count;
            stats.warmup_source_frame_count +=
                static_cast<std::uint64_t>(extent.source_frame_count);
        } else {
            write_published_block(*scratch, audible_frame, encoders, audition,
                                  consumers, hashes, stats);
            audible_frame += kSourceFramesPerBlock;
            ++stats.published_block_count;
            stats.published_source_frame_count +=
                static_cast<std::uint64_t>(extent.source_frame_count);
        }
    }

    if (stats.input_frame_count != schedule.input_frame_count ||
        stats.processed_block_count != schedule.processed_block_count ||
        stats.warmup_block_count != schedule.warmup_block_count ||
        stats.published_block_count != schedule.published_block_count ||
        stats.processed_source_frame_count != schedule.processed_source_frame_count ||
        stats.warmup_source_frame_count != schedule.warmup_source_frame_count ||
        stats.published_source_frame_count != schedule.published_source_frame_count ||
        source_stage.next_input_frame_index() != schedule.input_frame_count ||
        source_stage.next_source_frame_index() !=
            schedule.processed_source_frame_count ||
        audible_frame != schedule.published_source_frame_count) {
        throw std::logic_error{"P1.8 render produced an incomplete frame interval"};
    }
    for (std::size_t index = 0; index < encoders.size(); ++index) {
        require_encoding_success(encoders[index].finish(consumers[index]),
                                 "P1.8 WAVE finalization failed");
        const auto &expected =
            require_expected_audio(static_cast<P18ReferenceAudioArtifact>(index));
        if (encoders[index].frames_written() != schedule.published_source_frame_count ||
            encoders[index].bytes_emitted() != expected.expected_byte_count) {
            throw std::logic_error{"P1.8 Float32 WAVE length changed"};
        }
    }
    require_encoding_success(
        audition.finish(
            consumers[artifact_index(P18ReferenceAudioArtifact::master_audition)]),
        "P1.8 audition WAVE finalization failed");
    const auto &expected_audition =
        require_expected_audio(P18ReferenceAudioArtifact::master_audition);
    if (audition.frames_written() != schedule.published_source_frame_count ||
        audition.bytes_emitted() != expected_audition.expected_byte_count) {
        throw std::logic_error{"P1.8 audition WAVE length changed"};
    }

    stats.raw_float32_payload_sha256 = hashes[0].finish();
    stats.monitoring_float32_payload_sha256 = hashes[1].finish();
    stats.faded_float32_payload_sha256 = hashes[2].finish();
    stats.s32le_payload_sha256 = hashes[3].finish();
    stats.pcm24le_payload_sha256 = hashes[4].finish();
    return stats;
}

} // namespace engine_sim_offline::reference
