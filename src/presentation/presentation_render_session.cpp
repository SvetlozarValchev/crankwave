#include "presentation/presentation_render_session.hpp"

#include "artifacts/audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/mastering.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <algorithm>
#include <array>
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
constexpr std::size_t kProcessedBlockCount = 850;
constexpr std::size_t kWarmupBlockCount = 100;
constexpr std::size_t kPublishedBlockCount = kProcessedBlockCount - kWarmupBlockCount;
constexpr std::uint64_t kProcessedInputFrameCount =
    kProcessedBlockCount * kInputFramesPerBlock;
constexpr std::uint64_t kProcessedSourceFrameCount =
    kProcessedBlockCount * kSourceFramesPerBlock;
constexpr std::uint64_t kWarmupSourceFrameCount =
    kWarmupBlockCount * kSourceFramesPerBlock;
constexpr std::uint64_t kPublishedSourceFrameCount =
    kPublishedBlockCount * kSourceFramesPerBlock;
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
static_assert(kPublishedSourceFrameCount == artifacts::kAuditionWaveFrameCount);
static_assert(kPublishedSourceFrameCount == kAudibleFrameCount);

[[nodiscard]] constexpr std::size_t index(AudioIndex value) noexcept {
    return static_cast<std::size_t>(value);
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

[[nodiscard]] bool is_float_audio(const contract::AudioContract &audio) noexcept {
    return audio.sample_rate == contract::RationalRateHz{192000, 1} &&
           audio.frame_count == kPublishedSourceFrameCount &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "float32le";
}

[[nodiscard]] bool is_audition_audio(const contract::AudioContract &audio) noexcept {
    return audio.sample_rate == contract::RationalRateHz{192000, 1} &&
           audio.frame_count == kPublishedSourceFrameCount &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "pcm_s24le";
}

void validate_plan(const PresentationRenderPlan &plan) {
    if (plan.output_contract.required_artifacts.size() !=
        kPresentationAudioArtifactCount) {
        throw std::invalid_argument{
            "presentation requires exactly eight output-contract artifacts"};
    }

    std::unordered_set<std::string_view> roles;
    std::unordered_set<std::string_view> paths;
    for (std::size_t artifact_index = 0; artifact_index < plan.audio_artifacts.size();
         ++artifact_index) {
        const auto &pending = plan.audio_artifacts[artifact_index];
        const auto &required = plan.output_contract.required_artifacts[artifact_index];
        if (pending.role.empty() || pending.relative_path.empty() ||
            pending.kind != contract::ArtifactKind::audio ||
            !pending.audio.has_value() || pending.role != required.role ||
            pending.kind != required.kind || pending.audio != required.audio ||
            pending.diagnostic != required.diagnostic ||
            !roles.insert(pending.role).second ||
            !paths.insert(pending.relative_path).second) {
            throw std::invalid_argument{
                "presentation artifact plan differs from its ordered output "
                "contract"};
        }
        const bool media_matches = artifact_index < kFloatWaveCount
                                       ? is_float_audio(*pending.audio)
                                       : is_audition_audio(*pending.audio);
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

[[nodiscard]] WavEncoder make_float_wave_encoder(const PresentationRenderPlan &plan,
                                                 std::size_t artifact_index) {
    auto result = artifacts::make_wav_encoder(
        *plan.audio_artifacts[artifact_index].audio, {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct Float32 WAVE encoder: " +
                               error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] std::array<WavEncoder, kFloatWaveCount>
make_float_wave_encoders(const PresentationRenderPlan &plan) {
    return {
        make_float_wave_encoder(plan, 0), make_float_wave_encoder(plan, 1),
        make_float_wave_encoder(plan, 2), make_float_wave_encoder(plan, 3),
        make_float_wave_encoder(plan, 4), make_float_wave_encoder(plan, 5),
        make_float_wave_encoder(plan, 6),
    };
}

[[nodiscard]] artifacts::AuditionWaveEncoder make_audition_wave_encoder() {
    auto result = artifacts::make_audition_wave_encoder({kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::AuditionWaveEncoder>(std::move(result));
}

[[nodiscard]] std::array<CausalOverlapSaveConvolver, kRouteCount> make_convolvers(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &configured_ir) {
    if (!configured_ir) {
        throw std::invalid_argument{"presentation requires a configured IR kernel"};
    }
    return {
        CausalOverlapSaveConvolver{configured_ir},
        CausalOverlapSaveConvolver{configured_ir},
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

[[nodiscard]] std::logic_error
manifest_validation_error(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return std::logic_error{"complete publication manifest failed validation"};
    }
    const auto &first = report.issues.front();
    return std::logic_error{"complete publication manifest is invalid at " +
                            first.path + ": " + first.message};
}

} // namespace

class PresentationRenderSession::Implementation final {
  public:
    Implementation(RenderSink &sink, PresentationRenderPlan plan,
                   std::array<RouteConditioningSeeds, kRouteCount> route_seeds,
                   std::shared_ptr<const dsp::FixedConvolutionKernel> configured_ir,
                   RenderControl control)
        : sink_(sink), plan_(validated_plan(std::move(plan))),
          control_(std::move(control)), source_stage_(std::move(route_seeds)),
          convolvers_(make_convolvers(configured_ir)),
          encoders_(make_float_wave_encoders(plan_)),
          audition_(make_audition_wave_encoder()),
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
        if (stats_.processed_block_count >= kProcessedBlockCount) {
            abort_once();
            throw std::invalid_argument{
                "presentation received more than 850 input blocks"};
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
                    scratch_->selected[route][frame] =
                        1.0 * scratch_->configured_ir[route][frame] +
                        (1.0 - 1.0) * scratch_->dry[route][frame];
                }
            }

            const auto completed_block = stats_.processed_block_count - 1;
            if (completed_block < kWarmupBlockCount) {
                ++stats_.warmup_block_count;
                stats_.warmup_source_frame_count +=
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
                        kPublishedSourceFrameCount ||
                    encoders_[artifact_index].bytes_emitted() !=
                        observations_[artifact_index].byte_count) {
                    throw std::logic_error{"Float32 WAVE length changed"};
                }
            }
            require_encoding_success(
                audition_.finish(consumers_[index(AudioIndex::master_audition)]),
                "audition WAVE finalization failed");
            if (audition_.frames_written() != kPublishedSourceFrameCount ||
                audition_.bytes_emitted() !=
                    observations_[index(AudioIndex::master_audition)].byte_count) {
                throw std::logic_error{"audition WAVE length changed"};
            }

            std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount>
                records{};
            for (std::size_t artifact_index = 0; artifact_index < records.size();
                 ++artifact_index) {
                const auto &pending = plan_.audio_artifacts[artifact_index];
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

            sealed_artifacts_ = records;
            sealed_execution_ = execution.facts();
            state_ = PresentationRenderSessionState::sealed;
            return {stats_, std::move(records), std::move(execution)};
        } catch (...) {
            abort_once();
            throw;
        }
    }

    void commit(const SealedPresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix) {
        if (state_ != PresentationRenderSessionState::sealed ||
            !sealed_artifacts_.has_value() || !sealed_execution_.has_value()) {
            abort_once();
            throw std::logic_error{
                "presentation commit requires this session's sealed evidence"};
        }

        try {
            if (evidence.artifacts() != *sealed_artifacts_ ||
                evidence.execution().facts() != *sealed_execution_ ||
                manifest.execution !=
                    std::optional<contract::ExecutionFacts>{*sealed_execution_} ||
                manifest.content.output_contract != plan_.output_contract ||
                manifest.content.artifacts.size() != sealed_artifacts_->size() ||
                !std::equal(manifest.content.artifacts.begin(),
                            manifest.content.artifacts.end(),
                            sealed_artifacts_->begin())) {
                throw std::logic_error{
                    "commit manifest differs from this session's sealed "
                    "artifact or execution evidence"};
            }
            const auto report = contract::validate(manifest, provenance, source_matrix);
            if (!report.ok()) {
                throw manifest_validation_error(report);
            }
        } catch (...) {
            abort_once();
            throw;
        }

        // RenderSink commit is the terminal attempt. Once called, its contract owns
        // cleanup on both success and failure, so this session must never call abort.
        commit_attempted_ = true;
        try {
            const auto status = sink_.commit(manifest);
            if (status.has_value()) {
                state_ = PresentationRenderSessionState::aborted;
                throw sink_error("could not commit presentation", *status);
            }
            state_ = PresentationRenderSessionState::committed;
        } catch (...) {
            if (state_ != PresentationRenderSessionState::committed) {
                state_ = PresentationRenderSessionState::aborted;
            }
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

        for (const auto &pending : plan_.audio_artifacts) {
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
        const auto &role = plan_.audio_artifacts[artifact_index].role;
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
                scratch_->stems[base][frame] =
                    dsp::publish_calibrated_float32(scratch_->dry[route][frame]);
                scratch_->stems[base + 1][frame] = dsp::publish_calibrated_float32(
                    scratch_->configured_ir[route][frame]);
                scratch_->stems[base + 2][frame] =
                    dsp::publish_calibrated_float32(scratch_->selected[route][frame]);
            }
        }

        master_block(scratch_->stems[index(AudioIndex::exhaust_0_selected)],
                     scratch_->stems[index(AudioIndex::exhaust_1_selected)],
                     audible_frame_, scratch_->mastered);
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
        if (stats_.input_frame_count != kProcessedInputFrameCount ||
            stats_.processed_block_count != kProcessedBlockCount ||
            stats_.warmup_block_count != kWarmupBlockCount ||
            stats_.published_block_count != kPublishedBlockCount ||
            stats_.processed_source_frame_count != kProcessedSourceFrameCount ||
            stats_.warmup_source_frame_count != kWarmupSourceFrameCount ||
            stats_.published_source_frame_count != kPublishedSourceFrameCount ||
            source_stage_.next_input_frame_index() != kProcessedInputFrameCount ||
            source_stage_.next_source_frame_index() != kProcessedSourceFrameCount ||
            audible_frame_ != kPublishedSourceFrameCount) {
            throw std::logic_error{
                "presentation produced an incomplete frame interval"};
        }
    }

    void abort_once() noexcept {
        if (!transaction_begun_ || commit_attempted_ ||
            state_ == PresentationRenderSessionState::committed ||
            state_ == PresentationRenderSessionState::aborted) {
            return;
        }
        sink_.abort();
        state_ = PresentationRenderSessionState::aborted;
    }

    RenderSink &sink_;
    PresentationRenderPlan plan_;
    RenderControl control_;
    ExhaustSourceStage source_stage_;
    std::array<CausalOverlapSaveConvolver, kRouteCount> convolvers_;
    std::array<WavEncoder, kFloatWaveCount> encoders_;
    artifacts::AuditionWaveEncoder audition_;
    std::unique_ptr<RenderScratch> scratch_;
    std::array<ArtifactObservation, kPresentationAudioArtifactCount> observations_;
    std::array<artifacts::WavChunkConsumer, kPresentationAudioArtifactCount> consumers_;
    std::optional<execution::LinuxExecutionFactsObservation> execution_observation_;
    PresentationRenderStats stats_;
    std::optional<std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount>>
        sealed_artifacts_;
    std::optional<contract::ExecutionFacts> sealed_execution_;
    std::uint64_t audible_frame_ = 0;
    PresentationRenderSessionState state_ = PresentationRenderSessionState::active;
    bool transaction_begun_ = false;
    bool commit_attempted_ = false;
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

PresentationRenderSession::PresentationRenderSession(
    RenderSink &sink, PresentationRenderPlan plan,
    std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> route_seeds,
    std::shared_ptr<const dsp::FixedConvolutionKernel> configured_ir,
    RenderControl control)
    : implementation_(std::make_unique<Implementation>(
          sink, std::move(plan), std::move(route_seeds), std::move(configured_ir),
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

void PresentationRenderSession::commit(
    const SealedPresentationEvidence &evidence,
    const contract::RenderManifest &manifest,
    const contract::ProvenanceLedger &provenance,
    const contract::SourceMatrixContract &source_matrix) {
    implementation_->commit(evidence, manifest, provenance, source_matrix);
}

PresentationRenderSessionState PresentationRenderSession::state() const noexcept {
    return implementation_->state();
}

} // namespace engine_sim_offline::presentation
