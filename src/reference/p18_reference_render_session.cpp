#include "reference/p18_reference_render_session.hpp"

#include "artifacts/p18_audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
#include "dsp/p18_primitives.hpp"
#include "presentation/p18_mastering.hpp"
#include "presentation/p18_overlap_save_convolver.hpp"

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
static_assert(kP18PresentationAudioArtifactCount == kFloatWaveCount + 1);
static_assert(kPublishedSourceFrameCount == artifacts::kP18AuditionWaveFrameCount);
static_assert(kPublishedSourceFrameCount == presentation::kP18AudibleFrameCount);

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

void validate_plan(const P18PresentationSessionPlan &plan) {
    if (plan.output_contract.required_artifacts.size() !=
        kP18PresentationAudioArtifactCount) {
        throw std::invalid_argument{
            "P1.8 presentation requires exactly eight output-contract artifacts"};
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
                "P1.8 presentation artifact plan differs from its ordered output "
                "contract"};
        }
        const bool media_matches = artifact_index < kFloatWaveCount
                                       ? is_float_audio(*pending.audio)
                                       : is_audition_audio(*pending.audio);
        if (!media_matches) {
            throw std::invalid_argument{
                "P1.8 presentation artifact media differs from the accepted "
                "renderer"};
        }
    }
}

[[nodiscard]] P18PresentationSessionPlan
validated_plan(P18PresentationSessionPlan plan) {
    validate_plan(plan);
    return plan;
}

[[nodiscard]] WavEncoder make_float_wave_encoder(const P18PresentationSessionPlan &plan,
                                                 std::size_t artifact_index) {
    auto result = artifacts::make_wav_encoder(
        *plan.audio_artifacts[artifact_index].audio, {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct P1.8 Float32 WAVE encoder: " +
                               error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] std::array<WavEncoder, kFloatWaveCount>
make_float_wave_encoders(const P18PresentationSessionPlan &plan) {
    return {
        make_float_wave_encoder(plan, 0), make_float_wave_encoder(plan, 1),
        make_float_wave_encoder(plan, 2), make_float_wave_encoder(plan, 3),
        make_float_wave_encoder(plan, 4), make_float_wave_encoder(plan, 5),
        make_float_wave_encoder(plan, 6),
    };
}

[[nodiscard]] artifacts::P18AuditionWaveEncoder make_audition_wave_encoder() {
    auto result = artifacts::make_p18_audition_wave_encoder({kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct P1.8 audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::P18AuditionWaveEncoder>(std::move(result));
}

[[nodiscard]] std::array<presentation::P18CausalOverlapSaveConvolver, kRouteCount>
make_convolvers(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &configured_ir) {
    if (!configured_ir) {
        throw std::invalid_argument{
            "P1.8 presentation requires a configured IR kernel"};
    }
    return {
        presentation::P18CausalOverlapSaveConvolver{configured_ir},
        presentation::P18CausalOverlapSaveConvolver{configured_ir},
    };
}

void require_encoding_success(const WavEncodingStatus &status, const char *operation) {
    if (status.has_value()) {
        throw std::runtime_error{std::string(operation) + ": " + status->message};
    }
}

struct RenderScratch {
    std::array<P18ConditionedSourceFrame, kSourceFramesPerBlock> conditioned{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> dry{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> configured_ir{};
    std::array<std::array<double, kSourceFramesPerBlock>, kRouteCount> selected{};
    std::array<std::array<float, kSourceFramesPerBlock>, kStemCount> stems{};
    std::array<float, kSourceFramesPerBlock> raw{};
    std::array<std::int32_t, kSourceFramesPerBlock> pcm24{};
    std::array<presentation::P18MasteredFrame, kSourceFramesPerBlock> mastered{};
};

struct ArtifactObservation {
    contract::detail::Sha256Stream hash;
    std::uint64_t byte_count = 0;
};

struct FinishedSession {
    P18PresentationRenderStats stats;
    std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount> artifacts;
    execution::ObservedExecutionFacts execution;
};

[[nodiscard]] std::logic_error
manifest_validation_error(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return std::logic_error{"P1.8 complete publication manifest failed validation"};
    }
    const auto &first = report.issues.front();
    return std::logic_error{"P1.8 complete publication manifest is invalid at " +
                            first.path + ": " + first.message};
}

} // namespace

class P18PresentationSession::Implementation final {
  public:
    Implementation(
        RenderSink &sink, P18PresentationSessionPlan plan,
        std::array<presentation::P18RouteConditioningSeeds, kRouteCount> route_seeds,
        std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir,
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

    void process(presentation::ExhaustExcitationBlockView input) {
        if (state_ != P18PresentationSessionState::active) {
            abort_once();
            throw std::logic_error{
                "P1.8 presentation input requires an active session"};
        }
        if (stats_.processed_block_count >= kProcessedBlockCount) {
            abort_once();
            throw std::invalid_argument{
                "P1.8 presentation received more than 850 input blocks"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{
                "P1.8 presentation cancelled between complete input blocks"};
        }

        try {
            const auto extent = source_stage_.process(input, scratch_->conditioned);
            const auto expected_block =
                static_cast<std::uint64_t>(stats_.processed_block_count);
            const auto expected_input_frame = expected_block * kInputFramesPerBlock;
            const auto expected_source_frame = expected_block * kSourceFramesPerBlock;
            if (extent.first_input_frame_index != expected_input_frame ||
                extent.first_source_frame_index != expected_source_frame) {
                throw std::logic_error{
                    "P1.8 source-stage extent lost session continuity"};
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
        if (state_ != P18PresentationSessionState::active) {
            abort_once();
            throw std::logic_error{
                "P1.8 presentation finalization requires an active session"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{"P1.8 presentation cancelled before finalization"};
        }

        try {
            require_complete_schedule();
            for (std::size_t artifact_index = 0; artifact_index < encoders_.size();
                 ++artifact_index) {
                require_encoding_success(
                    encoders_[artifact_index].finish(consumers_[artifact_index]),
                    "P1.8 WAVE finalization failed");
                if (encoders_[artifact_index].frames_written() !=
                        kPublishedSourceFrameCount ||
                    encoders_[artifact_index].bytes_emitted() !=
                        observations_[artifact_index].byte_count) {
                    throw std::logic_error{"P1.8 Float32 WAVE length changed"};
                }
            }
            require_encoding_success(
                audition_.finish(consumers_[index(AudioIndex::master_audition)]),
                "P1.8 audition WAVE finalization failed");
            if (audition_.frames_written() != kPublishedSourceFrameCount ||
                audition_.bytes_emitted() !=
                    observations_[index(AudioIndex::master_audition)].byte_count) {
                throw std::logic_error{"P1.8 audition WAVE length changed"};
            }

            std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount>
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
                require_sink_success("could not seal P1.8 presentation artifact",
                                     sink_.seal_artifact(records[artifact_index]));
            }

            if (!execution_observation_.has_value()) {
                throw std::logic_error{
                    "P1.8 presentation lost its execution observation"};
            }
            auto execution =
                require_execution_finish(std::move(*execution_observation_));
            execution_observation_.reset();

            sealed_artifacts_ = records;
            sealed_execution_ = execution.facts();
            state_ = P18PresentationSessionState::sealed;
            return {stats_, std::move(records), std::move(execution)};
        } catch (...) {
            abort_once();
            throw;
        }
    }

    void commit(const P18SealedPresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix) {
        if (state_ != P18PresentationSessionState::sealed ||
            !sealed_artifacts_.has_value() || !sealed_execution_.has_value()) {
            abort_once();
            throw std::logic_error{
                "P1.8 presentation commit requires this session's sealed evidence"};
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
                    "P1.8 commit manifest differs from this session's sealed "
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
                state_ = P18PresentationSessionState::aborted;
                throw sink_error("could not commit P1.8 presentation", *status);
            }
            state_ = P18PresentationSessionState::committed;
        } catch (...) {
            if (state_ != P18PresentationSessionState::committed) {
                state_ = P18PresentationSessionState::aborted;
            }
            throw;
        }
    }

    [[nodiscard]] P18PresentationSessionState state() const noexcept {
        return state_;
    }

  private:
    void begin() {
        if (control_.stop_token.stop_requested()) {
            throw std::runtime_error{
                "P1.8 presentation cancelled before transaction begin"};
        }
        require_sink_success("could not begin P1.8 presentation transaction",
                             sink_.begin_transaction(plan_.output_contract));
        transaction_begun_ = true;

        for (const auto &pending : plan_.audio_artifacts) {
            require_sink_success("could not declare P1.8 presentation artifact",
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
                "P1.8 WAVE header emission failed");
        }
        require_encoding_success(
            audition_.begin(consumers_[index(AudioIndex::master_audition)]),
            "P1.8 audition WAVE prefix emission failed");
    }

    [[nodiscard]] bool consume_artifact_bytes(std::size_t artifact_index,
                                              std::uint64_t byte_offset,
                                              std::span<const std::byte> bytes) {
        if (artifact_index >= observations_.size() ||
            state_ != P18PresentationSessionState::active) {
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
                    dsp::p18_publish_calibrated_float32(scratch_->dry[route][frame]);
                scratch_->stems[base + 1][frame] = dsp::p18_publish_calibrated_float32(
                    scratch_->configured_ir[route][frame]);
                scratch_->stems[base + 2][frame] = dsp::p18_publish_calibrated_float32(
                    scratch_->selected[route][frame]);
            }
        }

        presentation::p18_master_reference_block(
            scratch_->stems[index(AudioIndex::exhaust_0_selected)],
            scratch_->stems[index(AudioIndex::exhaust_1_selected)], audible_frame_,
            scratch_->mastered);
        for (std::size_t frame = 0; frame < kSourceFramesPerBlock; ++frame) {
            const auto &mastered = scratch_->mastered[frame];
            scratch_->raw[frame] = mastered.raw;
            scratch_->pcm24[frame] = mastered.pcm24;
        }
        for (std::size_t stem = 0; stem < kStemCount; ++stem) {
            require_encoding_success(encoders_[stem].write_float32_interleaved(
                                         scratch_->stems[stem], consumers_[stem]),
                                     "P1.8 stem WAVE payload emission failed");
        }
        require_encoding_success(
            encoders_[index(AudioIndex::master_raw)].write_float32_interleaved(
                scratch_->raw, consumers_[index(AudioIndex::master_raw)]),
            "P1.8 raw-master WAVE payload emission failed");
        require_encoding_success(
            audition_.write_pcm24(scratch_->pcm24,
                                  consumers_[index(AudioIndex::master_audition)]),
            "P1.8 audition WAVE payload emission failed");
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
                "P1.8 presentation produced an incomplete frame interval"};
        }
    }

    void abort_once() noexcept {
        if (!transaction_begun_ || commit_attempted_ ||
            state_ == P18PresentationSessionState::committed ||
            state_ == P18PresentationSessionState::aborted) {
            return;
        }
        sink_.abort();
        state_ = P18PresentationSessionState::aborted;
    }

    RenderSink &sink_;
    P18PresentationSessionPlan plan_;
    RenderControl control_;
    presentation::P18SourceStage source_stage_;
    std::array<presentation::P18CausalOverlapSaveConvolver, kRouteCount> convolvers_;
    std::array<WavEncoder, kFloatWaveCount> encoders_;
    artifacts::P18AuditionWaveEncoder audition_;
    std::unique_ptr<RenderScratch> scratch_;
    std::array<ArtifactObservation, kP18PresentationAudioArtifactCount> observations_;
    std::array<artifacts::WavChunkConsumer, kP18PresentationAudioArtifactCount>
        consumers_;
    std::optional<execution::LinuxExecutionFactsObservation> execution_observation_;
    P18PresentationRenderStats stats_;
    std::optional<
        std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount>>
        sealed_artifacts_;
    std::optional<contract::ExecutionFacts> sealed_execution_;
    std::uint64_t audible_frame_ = 0;
    P18PresentationSessionState state_ = P18PresentationSessionState::active;
    bool transaction_begun_ = false;
    bool commit_attempted_ = false;
};

const P18PresentationRenderStats &
P18SealedPresentationEvidence::stats() const noexcept {
    return stats_;
}

const std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount> &
P18SealedPresentationEvidence::artifacts() const noexcept {
    return artifacts_;
}

const execution::ObservedExecutionFacts &
P18SealedPresentationEvidence::execution() const noexcept {
    return execution_;
}

P18PresentationSession::P18PresentationSession(
    RenderSink &sink, P18PresentationSessionPlan plan,
    std::array<presentation::P18RouteConditioningSeeds,
               presentation::kP18ExhaustRouteCount>
        route_seeds,
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir,
    RenderControl control)
    : implementation_(std::make_unique<Implementation>(
          sink, std::move(plan), std::move(route_seeds), std::move(configured_ir),
          std::move(control))) {}

P18PresentationSession::~P18PresentationSession() = default;

void P18PresentationSession::process(presentation::ExhaustExcitationBlockView input) {
    implementation_->process(input);
}

P18SealedPresentationEvidence P18PresentationSession::finish() {
    auto finished = implementation_->finish();
    return {std::move(finished.stats), std::move(finished.artifacts),
            std::move(finished.execution)};
}

void P18PresentationSession::commit(
    const P18SealedPresentationEvidence &evidence,
    const contract::RenderManifest &manifest,
    const contract::ProvenanceLedger &provenance,
    const contract::SourceMatrixContract &source_matrix) {
    implementation_->commit(evidence, manifest, provenance, source_matrix);
}

P18PresentationSessionState P18PresentationSession::state() const noexcept {
    return implementation_->state();
}

} // namespace engine_sim_offline::reference
