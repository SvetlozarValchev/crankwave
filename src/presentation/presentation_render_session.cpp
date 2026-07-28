#include "presentation/presentation_render_session.hpp"

#include "artifacts/audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/mastering.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine_sim_offline::presentation {
namespace {

using artifacts::WavEncoder;
using artifacts::WavEncodingStatus;
constexpr std::size_t kInputFramesPerBlock = kExcitationFramesPerMethodBlock;
constexpr std::size_t kSourceFramesPerBlock = kSourceFramesPerMethodBlock;
constexpr std::size_t kRouteCount = kExhaustExcitationRouteCount;
constexpr std::size_t kStemCount = kRouteCount * 3;
constexpr std::size_t kFloatWaveCount = kStemCount + 1;
constexpr std::size_t kMaximumWaveChunkBytes = 16U * 1024U;

enum class AudioIndex : std::size_t {
    exhaust_0_dry = 0,
    exhaust_0_configured_ir = 1,
    exhaust_0_selected = 2,
    exhaust_1_dry = 3,
    exhaust_1_configured_ir = 4,
    exhaust_1_selected = 5,
    master_raw = 6,
    master_audition = 7,
};

static_assert(kRouteCount == 2);
static_assert(kPresentationAudioArtifactCount == kFloatWaveCount + 1);

[[nodiscard]] constexpr std::size_t index(AudioIndex value) noexcept {
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint64_t checked_frame_product(std::uint64_t block_count,
                                                  std::size_t frames_per_block,
                                                  const char *description) {
    if (block_count > std::numeric_limits<std::uint64_t>::max() /
                          static_cast<std::uint64_t>(frames_per_block)) {
        throw std::invalid_argument{std::string(description) + " overflows uint64"};
    }
    return block_count * static_cast<std::uint64_t>(frames_per_block);
}

[[nodiscard]] std::uint64_t
published_block_count(const PresentationRenderPlan &plan) noexcept {
    return plan.timeline.total_block_count - plan.timeline.pre_audible_block_count;
}

[[nodiscard]] std::uint64_t
processed_input_frame_count(const PresentationRenderPlan &plan) {
    return checked_frame_product(plan.timeline.total_block_count, kInputFramesPerBlock,
                                 "presentation input horizon");
}

[[nodiscard]] std::uint64_t
processed_source_frame_count(const PresentationRenderPlan &plan) {
    return checked_frame_product(plan.timeline.total_block_count, kSourceFramesPerBlock,
                                 "presentation source horizon");
}

[[nodiscard]] std::uint64_t
pre_audible_source_frame_count(const PresentationRenderPlan &plan) {
    return checked_frame_product(plan.timeline.pre_audible_block_count,
                                 kSourceFramesPerBlock,
                                 "presentation pre-audible horizon");
}

[[nodiscard]] std::uint64_t
published_source_frame_count(const PresentationRenderPlan &plan) {
    return checked_frame_product(published_block_count(plan), kSourceFramesPerBlock,
                                 "presentation audible horizon");
}

[[nodiscard]] ExhaustSourceRouteIds
source_route_ids(const PresentationRenderPlan &plan) noexcept {
    return {plan.routes[0].route_id, plan.routes[1].route_id};
}

[[nodiscard]] std::array<RouteConditioningSeeds, kRouteCount>
source_route_seeds(const PresentationRenderPlan &plan) noexcept {
    return {
        plan.routes[0].conditioning_seeds,
        plan.routes[1].conditioning_seeds,
    };
}

[[nodiscard]] std::array<PendingArtifact, kPresentationAudioArtifactCount>
ordered_audio_artifacts(const PresentationRenderPlan &plan) {
    return {
        plan.routes[0].artifacts.dry,           plan.routes[0].artifacts.configured_ir,
        plan.routes[0].artifacts.selected,      plan.routes[1].artifacts.dry,
        plan.routes[1].artifacts.configured_ir, plan.routes[1].artifacts.selected,
        plan.audition.raw_master_artifact,      plan.audition.audition_master_artifact,
    };
}

[[nodiscard]] std::array<std::size_t, kRouteCount>
audition_route_indices(const PresentationRenderPlan &plan) {
    std::array<std::size_t, kRouteCount> indices{};
    for (std::size_t selected = 0; selected < indices.size(); ++selected) {
        const auto found = std::find_if(
            plan.routes.begin(), plan.routes.end(), [&](const auto &route) {
                return route.route_id == plan.audition.selected_route_ids[selected];
            });
        if (found == plan.routes.end()) {
            throw std::logic_error{
                "validated audition route is absent from the render plan"};
        }
        indices[selected] = static_cast<std::size_t>(found - plan.routes.begin());
    }
    return indices;
}

[[nodiscard]] std::runtime_error sink_error(std::string_view operation,
                                            const RenderSinkError &error) {
    return std::runtime_error{std::string(operation) + ": " + error.detail_code + ": " +
                              error.message};
}

void require_sink_success(std::string_view operation, const RenderSinkStatus &status) {
    if (status.has_value()) {
        throw sink_error(operation, *status);
    }
}

[[nodiscard]] std::runtime_error
execution_error(std::string_view operation,
                const execution::LinuxExecutionFactsError &error) {
    auto message = std::string(operation) + " rejected at " + error.component + ": " +
                   error.message;
    if (error.system_error != 0) {
        message += " (system error " + std::to_string(error.system_error) + ")";
    }
    return std::runtime_error{std::move(message)};
}

[[nodiscard]] execution::LinuxExecutionFactsObservation require_execution_begin() {
    auto result = execution::begin_single_job_linux_execution();
    if (auto *observation =
            std::get_if<execution::LinuxExecutionFactsObservation>(&result)) {
        return std::move(*observation);
    }
    throw execution_error("could not begin presentation execution observation",
                          std::get<execution::LinuxExecutionFactsError>(result));
}

[[nodiscard]] execution::ObservedExecutionFacts
require_execution_finish(execution::LinuxExecutionFactsObservation &&observation) {
    auto result = execution::finish_single_job_linux_execution(std::move(observation));
    if (auto *facts = std::get_if<execution::ObservedExecutionFacts>(&result)) {
        return std::move(*facts);
    }
    throw execution_error("could not finish presentation execution observation",
                          std::get<execution::LinuxExecutionFactsError>(result));
}

[[nodiscard]] bool is_float_audio(const contract::AudioContract &audio,
                                  std::uint64_t expected_frame_count) noexcept {
    return audio.sample_rate == contract::RationalRateHz{192000, 1} &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "float32le";
}

[[nodiscard]] bool is_audition_audio(const contract::AudioContract &audio,
                                     std::uint64_t expected_frame_count) noexcept {
    return audio.sample_rate == contract::RationalRateHz{192000, 1} &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "pcm_s24le";
}

void validate_plan(const PresentationRenderPlan &plan) {
    if (plan.timeline.total_block_count == 0 ||
        plan.timeline.pre_audible_block_count >= plan.timeline.total_block_count) {
        throw std::invalid_argument{
            "presentation timeline requires a positive audible block interval"};
    }
    if (plan.timeline.tail_policy != PresentationTailPolicy::truncate_at_timeline_end) {
        throw std::invalid_argument{
            "presentation timeline requires an explicit supported tail policy"};
    }
    static_cast<void>(processed_input_frame_count(plan));
    static_cast<void>(processed_source_frame_count(plan));
    static_cast<void>(pre_audible_source_frame_count(plan));
    const auto audible_frames = published_source_frame_count(plan);
    if (plan.audition.mastering.audible_frame_count() != audible_frames) {
        throw std::invalid_argument{
            "presentation mastering horizon differs from the timeline"};
    }
    if (!std::isfinite(plan.publication_calibration_gain_linear) ||
        plan.publication_calibration_gain_linear <= 0.0) {
        throw std::invalid_argument{
            "presentation publication gain must be finite and positive"};
    }

    for (std::size_t route = 0; route < plan.routes.size(); ++route) {
        const auto &configured = plan.routes[route];
        if (!configured.route_id.valid() || configured.route_semantic_id.empty() ||
            !configured.configured_ir || !std::isfinite(configured.wet_mix_01) ||
            configured.wet_mix_01 < 0.0 || configured.wet_mix_01 > 1.0) {
            throw std::invalid_argument{
                "presentation route requires identities, an IR, and wet mix in [0, 1]"};
        }
        for (std::size_t prior = 0; prior < route; ++prior) {
            if (configured.route_id == plan.routes[prior].route_id ||
                configured.route_semantic_id == plan.routes[prior].route_semantic_id) {
                throw std::invalid_argument{
                    "presentation route identities must be distinct"};
            }
        }

        const auto requirement = std::ranges::find(
            plan.output_contract.required_source_routes, configured.route_semantic_id,
            &contract::SourceRouteRequirement::semantic_id);
        const std::array<std::string_view, 3> artifact_roles{
            configured.artifacts.dry.role,
            configured.artifacts.configured_ir.role,
            configured.artifacts.selected.role,
        };
        if (requirement == plan.output_contract.required_source_routes.end() ||
            requirement->kind != contract::SourceRouteKind::exhaust_outlet ||
            requirement->disposition != contract::RouteDisposition::rendered ||
            requirement->artifact_roles.size() != artifact_roles.size() ||
            !std::equal(requirement->artifact_roles.begin(),
                        requirement->artifact_roles.end(), artifact_roles.begin())) {
            throw std::invalid_argument{
                "presentation route artifacts differ from their source-route owner"};
        }
    }
    if (plan.audition.selected_route_ids[0] == plan.audition.selected_route_ids[1]) {
        throw std::invalid_argument{"presentation audition routes must be distinct"};
    }
    for (const auto selected : plan.audition.selected_route_ids) {
        if (std::ranges::none_of(plan.routes, [&](const auto &route) {
                return route.route_id == selected;
            })) {
            throw std::invalid_argument{
                "presentation audition route is absent from the route plan"};
        }
    }

    if (plan.output_contract.required_source_routes.size() != kRouteCount ||
        plan.output_contract.required_output_buses.size() != 2 ||
        plan.output_contract.required_artifacts.size() !=
            kPresentationAudioArtifactCount) {
        throw std::invalid_argument{
            "presentation requires exactly two routes, two buses, and eight artifacts"};
    }

    const auto raw_bus = std::ranges::find_if(
        plan.output_contract.required_output_buses, [](const auto &bus) {
            return bus.kind == contract::OutputBusKind::master_engine_raw ||
                   bus.kind == contract::OutputBusKind::master_reference_raw;
        });
    const auto audition_bus = std::ranges::find_if(
        plan.output_contract.required_output_buses, [](const auto &bus) {
            return bus.kind == contract::OutputBusKind::master_engine_audition ||
                   bus.kind == contract::OutputBusKind::master_reference_audition;
        });
    if (raw_bus == plan.output_contract.required_output_buses.end() ||
        audition_bus == plan.output_contract.required_output_buses.end() ||
        raw_bus == audition_bus || raw_bus->artifact_roles.size() != 1 ||
        audition_bus->artifact_roles.size() != 1 ||
        raw_bus->artifact_roles.front() != plan.audition.raw_master_artifact.role ||
        audition_bus->artifact_roles.front() !=
            plan.audition.audition_master_artifact.role) {
        throw std::invalid_argument{
            "presentation master artifacts differ from their output-bus owners"};
    }

    const auto audio_artifacts = ordered_audio_artifacts(plan);
    std::unordered_set<std::string_view> roles;
    std::unordered_set<std::string_view> paths;
    for (std::size_t artifact_index = 0; artifact_index < audio_artifacts.size();
         ++artifact_index) {
        const auto &pending = audio_artifacts[artifact_index];
        const auto required =
            std::ranges::find(plan.output_contract.required_artifacts, pending.role,
                              &contract::ArtifactRequirement::role);
        if (pending.role.empty() || pending.relative_path.empty() ||
            pending.kind != contract::ArtifactKind::audio ||
            !pending.audio.has_value() ||
            required == plan.output_contract.required_artifacts.end() ||
            pending.kind != required->kind || pending.audio != required->audio ||
            pending.diagnostic != required->diagnostic ||
            !roles.insert(pending.role).second ||
            !paths.insert(pending.relative_path).second) {
            throw std::invalid_argument{
                "presentation artifact plan differs from its output contract"};
        }
        const bool media_matches =
            artifact_index < kFloatWaveCount
                ? is_float_audio(*pending.audio, audible_frames)
                : is_audition_audio(*pending.audio, audible_frames);
        if (!media_matches) {
            throw std::invalid_argument{
                "presentation artifact media differs from the accepted "
                "renderer"};
        }
    }
}

[[nodiscard]] PresentationRenderPlan validated_plan(PresentationRenderPlan plan) {
    validate_plan(plan);
    return plan;
}

[[nodiscard]] WavEncoder make_float_wave_encoder(
    const std::array<PendingArtifact, kPresentationAudioArtifactCount> &audio_artifacts,
    std::size_t artifact_index) {
    auto result = artifacts::make_wav_encoder(*audio_artifacts[artifact_index].audio,
                                              {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct Float32 WAVE encoder: " +
                               error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] std::array<WavEncoder, kFloatWaveCount> make_float_wave_encoders(
    const std::array<PendingArtifact, kPresentationAudioArtifactCount>
        &audio_artifacts) {
    return {
        make_float_wave_encoder(audio_artifacts, 0),
        make_float_wave_encoder(audio_artifacts, 1),
        make_float_wave_encoder(audio_artifacts, 2),
        make_float_wave_encoder(audio_artifacts, 3),
        make_float_wave_encoder(audio_artifacts, 4),
        make_float_wave_encoder(audio_artifacts, 5),
        make_float_wave_encoder(audio_artifacts, 6),
    };
}

[[nodiscard]] artifacts::AuditionWaveEncoder make_audition_wave_encoder(
    const PresentationRenderPlan &plan,
    const std::array<PendingArtifact, kPresentationAudioArtifactCount>
        &audio_artifacts) {
    auto result = artifacts::make_audition_wave_encoder(
        *audio_artifacts[index(AudioIndex::master_audition)].audio,
        plan.audition.metadata, {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::AuditionWaveEncoder>(std::move(result));
}

[[nodiscard]] std::array<CausalOverlapSaveConvolver, kRouteCount>
make_convolvers(const PresentationRenderPlan &plan) {
    return {
        CausalOverlapSaveConvolver{plan.routes[0].configured_ir},
        CausalOverlapSaveConvolver{plan.routes[1].configured_ir},
    };
}

void require_encoding_success(const WavEncodingStatus &status, const char *operation) {
    if (status.has_value()) {
        throw std::runtime_error{std::string(operation) + ": " + status->message};
    }
}

struct RenderScratch {
    std::array<ConditionedSourceFrame, kSourceFramesPerBlock> conditioned{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> dry{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> configured_ir{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> selected{};
    std::array<std::array<float, kSourceFramesPerBlock>, kStemCount> stems{};
    std::array<float, kSourceFramesPerBlock> raw{};
    std::array<std::int32_t, kSourceFramesPerBlock> pcm24{};
    std::array<MasteredFrame, kSourceFramesPerBlock> mastered{};
};

struct ArtifactObservation {
    contract::detail::Sha256Stream hash;
    std::uint64_t byte_count = 0;
};

struct FinishedSession {
    PresentationRenderStats stats;
    std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> artifacts;
    execution::ObservedExecutionFacts execution;
};

} // namespace

class PresentationRenderSession::Implementation final {
  public:
    Implementation(RenderSink &sink, PresentationRenderPlan plan, RenderControl control)
        : sink_(sink), plan_(validated_plan(std::move(plan))),
          audio_artifacts_(ordered_audio_artifacts(plan_)),
          control_(std::move(control)),
          audition_route_indices_(audition_route_indices(plan_)),
          source_stage_(source_route_ids(plan_), source_route_seeds(plan_)),
          convolvers_(make_convolvers(plan_)),
          encoders_(make_float_wave_encoders(audio_artifacts_)),
          audition_(make_audition_wave_encoder(plan_, audio_artifacts_)),
          scratch_(std::make_unique<RenderScratch>()) {
        try {
            begin();
        } catch (...) {
            // A throwing constructor does not run ~Implementation. Close a
            // successfully begun sink transaction here before member unwinding.
            abort_once();
            throw;
        }
    }

    ~Implementation() {
        abort_once();
    }

    void process(ExhaustExcitationBlockView input) {
        if (state_ != PresentationRenderSessionState::active) {
            abort_once();
            throw std::logic_error{"presentation input requires an active session"};
        }
        if (stats_.processed_block_count >= plan_.timeline.total_block_count) {
            abort_once();
            throw std::invalid_argument{
                "presentation received more blocks than its timeline"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{
                "presentation cancelled between complete input blocks"};
        }

        try {
            const auto extent = source_stage_.process(input, scratch_->conditioned);
            const auto expected_block =
                static_cast<std::uint64_t>(stats_.processed_block_count);
            const auto expected_input_frame = expected_block * kInputFramesPerBlock;
            const auto expected_source_frame = expected_block * kSourceFramesPerBlock;
            if (extent.first_input_frame_index != expected_input_frame ||
                extent.first_source_frame_index != expected_source_frame) {
                throw std::logic_error{"source-stage extent lost session continuity"};
            }

            stats_.input_frame_count +=
                static_cast<std::uint64_t>(extent.input_frame_count);
            ++stats_.processed_block_count;
            stats_.processed_source_frame_count +=
                static_cast<std::uint64_t>(extent.source_frame_count);

            for (std::size_t route = 0; route < kRouteCount; ++route) {
                for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
                    scratch_->dry[route][frame] =
                        scratch_->conditioned[frame]
                            .route_values_engine_sim_source_unit[route];
                }
                convolvers_[route].process(scratch_->dry[route],
                                           scratch_->configured_ir[route]);
                for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
                    const double wet_mix = plan_.routes[route].wet_mix_01;
                    scratch_->selected[route][frame] =
                        wet_mix * scratch_->configured_ir[route][frame] +
                        (1.0 - wet_mix) * scratch_->dry[route][frame];
                }
            }

            const auto completed_block = stats_.processed_block_count - 1;
            if (completed_block < plan_.timeline.pre_audible_block_count) {
                ++stats_.pre_audible_block_count;
                stats_.pre_audible_source_frame_count +=
                    static_cast<std::uint64_t>(extent.source_frame_count);
            } else {
                write_published_block();
                audible_frame_ += kSourceFramesPerBlock;
                ++stats_.published_block_count;
                stats_.published_source_frame_count +=
                    static_cast<std::uint64_t>(extent.source_frame_count);
            }
        } catch (...) {
            abort_once();
            throw;
        }
    }

    [[nodiscard]] FinishedSession finish() {
        if (state_ != PresentationRenderSessionState::active) {
            abort_once();
            throw std::logic_error{
                "presentation finalization requires an active session"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{"presentation cancelled before finalization"};
        }

        try {
            require_complete_schedule();
            for (std::size_t artifact_index = 0; artifact_index < encoders_.size();
                 ++artifact_index) {
                require_encoding_success(
                    encoders_[artifact_index].finish(consumers_[artifact_index]),
                    "WAVE finalization failed");
                if (encoders_[artifact_index].frames_written() !=
                        published_source_frame_count(plan_) ||
                    encoders_[artifact_index].bytes_emitted() !=
                        observations_[artifact_index].byte_count) {
                    throw std::logic_error{"Float32 WAVE length changed"};
                }
            }
            require_encoding_success(
                audition_.finish(consumers_[index(AudioIndex::master_audition)]),
                "audition WAVE finalization failed");
            if (audition_.frames_written() != published_source_frame_count(plan_) ||
                audition_.bytes_emitted() !=
                    observations_[index(AudioIndex::master_audition)].byte_count) {
                throw std::logic_error{"audition WAVE length changed"};
            }

            std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount>
                records{};
            for (std::size_t artifact_index = 0; artifact_index < records.size();
                 ++artifact_index) {
                const auto &pending = audio_artifacts_[artifact_index];
                auto &observed = observations_[artifact_index];
                records[artifact_index] = {
                    pending.role,       pending.kind,        pending.relative_path,
                    pending.audio,      observed.byte_count, observed.hash.finish(),
                    pending.diagnostic,
                };
                require_sink_success("could not seal presentation artifact",
                                     sink_.seal_artifact(records[artifact_index]));
            }

            if (!execution_observation_.has_value()) {
                throw std::logic_error{"presentation lost its execution observation"};
            }
            auto execution =
                require_execution_finish(std::move(*execution_observation_));
            execution_observation_.reset();

            state_ = PresentationRenderSessionState::sealed;
            return {stats_, std::move(records), std::move(execution)};
        } catch (...) {
            abort_once();
            throw;
        }
    }

    [[nodiscard]] PresentationRenderSessionState state() const noexcept {
        return state_;
    }

  private:
    void begin() {
        if (control_.stop_token.stop_requested()) {
            throw std::runtime_error{"presentation cancelled before transaction begin"};
        }
        require_sink_success("could not begin presentation transaction",
                             sink_.begin_transaction(plan_.output_contract));
        transaction_begun_ = true;

        for (const auto &pending : audio_artifacts_) {
            require_sink_success("could not declare presentation artifact",
                                 sink_.declare_artifact(pending));
        }
        for (std::size_t artifact_index = 0; artifact_index < consumers_.size();
             ++artifact_index) {
            consumers_[artifact_index] =
                [this, artifact_index](std::uint64_t byte_offset,
                                       std::span<const std::byte> bytes) -> bool {
                return consume_artifact_bytes(artifact_index, byte_offset, bytes);
            };
        }

        // This is the exact execution interval start: preflight, transaction setup,
        // and declarations are complete; the first WAVE bytes and all DSP follow.
        execution_observation_.emplace(require_execution_begin());
        for (std::size_t artifact_index = 0; artifact_index < encoders_.size();
             ++artifact_index) {
            require_encoding_success(
                encoders_[artifact_index].begin(consumers_[artifact_index]),
                "WAVE header emission failed");
        }
        require_encoding_success(
            audition_.begin(consumers_[index(AudioIndex::master_audition)]),
            "audition WAVE prefix emission failed");
    }

    [[nodiscard]] bool consume_artifact_bytes(std::size_t artifact_index,
                                              std::uint64_t byte_offset,
                                              std::span<const std::byte> bytes) {
        if (artifact_index >= observations_.size() ||
            state_ != PresentationRenderSessionState::active) {
            return false;
        }
        auto &observed = observations_[artifact_index];
        if (byte_offset != observed.byte_count ||
            bytes.size() >
                std::numeric_limits<std::uint64_t>::max() - observed.byte_count) {
            return false;
        }
        const auto &role = audio_artifacts_[artifact_index].role;
        const auto status = sink_.write_artifact_chunk({role, byte_offset, bytes});
        if (status.has_value()) {
            return false;
        }
        observed.hash.update(bytes);
        observed.byte_count += static_cast<std::uint64_t>(bytes.size());
        return true;
    }

    void write_published_block() {
        for (std::size_t route = 0; route < kRouteCount; ++route) {
            const auto base = route * 3;
            for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
                scratch_->stems[base][frame] = dsp::publish_calibrated_float32(
                    scratch_->dry[route][frame],
                    plan_.publication_calibration_gain_linear);
                scratch_->stems[base + 1][frame] = dsp::publish_calibrated_float32(
                    scratch_->configured_ir[route][frame],
                    plan_.publication_calibration_gain_linear);
                scratch_->stems[base + 2][frame] = dsp::publish_calibrated_float32(
                    scratch_->selected[route][frame],
                    plan_.publication_calibration_gain_linear);
            }
        }

        master_block(scratch_->stems[audition_route_indices_[0] * 3 + 2],
                     scratch_->stems[audition_route_indices_[1] * 3 + 2],
                     audible_frame_, plan_.audition.mastering, scratch_->mastered);
        for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
            const auto &mastered = scratch_->mastered[frame];
            scratch_->raw[frame] = mastered.raw;
            scratch_->pcm24[frame] = mastered.pcm24;
        }
        for (std::size_t stem = 0; stem < kStemCount; ++stem) {
            require_encoding_success(encoders_[stem].write_float32_interleaved(
                                         scratch_->stems[stem], consumers_[stem]),
                                     "stem WAVE payload emission failed");
        }
        require_encoding_success(
            encoders_[index(AudioIndex::master_raw)].write_float32_interleaved(
                scratch_->raw, consumers_[index(AudioIndex::master_raw)]),
            "raw-master WAVE payload emission failed");
        require_encoding_success(
            audition_.write_pcm24(scratch_->pcm24,
                                  consumers_[index(AudioIndex::master_audition)]),
            "audition WAVE payload emission failed");
    }

    void require_complete_schedule() const {
        const auto expected_input_frames = processed_input_frame_count(plan_);
        const auto expected_source_frames = processed_source_frame_count(plan_);
        const auto expected_pre_audible_frames = pre_audible_source_frame_count(plan_);
        const auto expected_published_frames = published_source_frame_count(plan_);
        if (stats_.input_frame_count != expected_input_frames ||
            stats_.processed_block_count != plan_.timeline.total_block_count ||
            stats_.pre_audible_block_count != plan_.timeline.pre_audible_block_count ||
            stats_.published_block_count != published_block_count(plan_) ||
            stats_.processed_source_frame_count != expected_source_frames ||
            stats_.pre_audible_source_frame_count != expected_pre_audible_frames ||
            stats_.published_source_frame_count != expected_published_frames ||
            source_stage_.next_input_frame_index() != expected_input_frames ||
            source_stage_.next_source_frame_index() != expected_source_frames ||
            audible_frame_ != expected_published_frames) {
            throw std::logic_error{
                "presentation produced an incomplete frame interval"};
        }
    }

    void abort_once() noexcept {
        if (!transaction_begun_ || state_ == PresentationRenderSessionState::aborted) {
            return;
        }
        sink_.abort();
        state_ = PresentationRenderSessionState::aborted;
    }

    RenderSink &sink_;
    PresentationRenderPlan plan_;
    std::array<PendingArtifact, kPresentationAudioArtifactCount> audio_artifacts_;
    RenderControl control_;
    std::array<std::size_t, kRouteCount> audition_route_indices_;
    ExhaustSourceStage source_stage_;
    std::array<CausalOverlapSaveConvolver, kRouteCount> convolvers_;
    std::array<WavEncoder, kFloatWaveCount> encoders_;
    artifacts::AuditionWaveEncoder audition_;
    std::unique_ptr<RenderScratch> scratch_;
    std::array<ArtifactObservation, kPresentationAudioArtifactCount> observations_;
    std::array<artifacts::WavChunkConsumer, kPresentationAudioArtifactCount> consumers_;
    std::optional<execution::LinuxExecutionFactsObservation> execution_observation_;
    PresentationRenderStats stats_;
    std::uint64_t audible_frame_ = 0;
    PresentationRenderSessionState state_ = PresentationRenderSessionState::active;
    bool transaction_begun_ = false;
};

const PresentationRenderStats &SealedPresentationEvidence::stats() const noexcept {
    return stats_;
}

const std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> &
SealedPresentationEvidence::artifacts() const noexcept {
    return artifacts_;
}

const execution::ObservedExecutionFacts &
SealedPresentationEvidence::execution() const noexcept {
    return execution_;
}

PresentationRenderSession::PresentationRenderSession(RenderSink &sink,
                                                     PresentationRenderPlan plan,
                                                     RenderControl control)
    : implementation_(std::make_unique<Implementation>(sink, std::move(plan),
                                                       std::move(control))) {}

PresentationRenderSession::~PresentationRenderSession() = default;

void PresentationRenderSession::process(ExhaustExcitationBlockView input) {
    implementation_->process(input);
}

SealedPresentationEvidence PresentationRenderSession::finish() {
    auto finished = implementation_->finish();
    return {std::move(finished.stats), std::move(finished.artifacts),
            std::move(finished.execution)};
}

PresentationRenderSessionState PresentationRenderSession::state() const noexcept {
    return implementation_->state();
}

} // namespace engine_sim_offline::presentation
