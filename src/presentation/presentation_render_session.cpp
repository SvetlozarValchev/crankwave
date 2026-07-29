#include "presentation/presentation_render_session.hpp"

#include "acoustics/exhaust_acoustic_session.hpp"
#include "artifacts/audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
#include "presentation/mastering.hpp"

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
#include <vector>

namespace engine_sim_offline::presentation {
namespace {

using artifacts::WavEncoder;
using artifacts::WavEncodingStatus;

constexpr contract::RationalRateHz kAcousticRate{192'000, 1};
constexpr std::size_t kOutletCount = kPresentationExhaustOutletCount;
constexpr std::size_t kFloatWaveCount = 3;
constexpr std::size_t kMaximumWaveChunkBytes = 16U * 1024U;

enum class AudioIndex : std::size_t {
    outlet_front_pressure = 0,
    outlet_rear_pressure = 1,
    master_raw = 2,
    master_audition = 3,
};

static_assert(kOutletCount == 2);
static_assert(kPresentationAudioArtifactCount == kFloatWaveCount + 1);

[[nodiscard]] constexpr std::size_t index(AudioIndex value) noexcept {
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint64_t
published_frame_count(const PresentationRenderPlan &plan) noexcept {
    return plan.timeline.total_acoustic_frame_count -
           plan.timeline.pre_audible_frame_count;
}

[[nodiscard]] std::array<PendingArtifact, kPresentationAudioArtifactCount>
ordered_audio_artifacts(const PresentationRenderPlan &plan) {
    return {
        plan.outlets[0].pressure_stem_artifact,
        plan.outlets[1].pressure_stem_artifact,
        plan.master.raw_master_artifact,
        plan.master.audition_master_artifact,
    };
}

[[nodiscard]] std::string sink_failure_message(std::string_view operation,
                                               const RenderSinkError &error) {
    return std::string(operation) + ": " + error.detail_code + ": " + error.message;
}

void require_sink_success(std::string_view operation, const RenderSinkStatus &status) {
    if (status.has_value()) {
        throw PresentationSinkFailure{operation, *status};
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
    return audio.sample_rate == kAcousticRate &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "float32le";
}

[[nodiscard]] bool is_audition_audio(const contract::AudioContract &audio,
                                     std::uint64_t expected_frame_count) noexcept {
    return audio.sample_rate == kAcousticRate &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" &&
           audio.sample_encoding_id == "pcm_s24le";
}

void validate_plan(const PresentationRenderPlan &plan) {
    if (plan.timeline.total_acoustic_frame_count == 0 ||
        plan.timeline.pre_audible_frame_count >=
            plan.timeline.total_acoustic_frame_count) {
        throw std::invalid_argument{
            "presentation timeline requires a positive audible frame interval"};
    }
    if (plan.timeline.tail_policy != PresentationTailPolicy::truncate_at_timeline_end) {
        throw std::invalid_argument{
            "presentation timeline requires an explicit supported tail policy"};
    }
    const auto audible_frames = published_frame_count(plan);
    if (plan.master.mastering.audible_frame_count() != audible_frames) {
        throw std::invalid_argument{
            "presentation mastering horizon differs from the acoustic timeline"};
    }
    if (!std::isfinite(plan.pa_per_full_scale) || plan.pa_per_full_scale <= 0.0) {
        throw std::invalid_argument{
            "presentation Pa-per-full-scale calibration must be finite and positive"};
    }

    for (std::size_t outlet = 0; outlet < plan.outlets.size(); ++outlet) {
        const auto &configured = plan.outlets[outlet];
        if (!configured.route_id.valid() ||
            !contract::is_valid_semantic_id(configured.route_semantic_id)) {
            throw std::invalid_argument{
                "presentation outlet requires canonical route identities"};
        }
        for (std::size_t prior = 0; prior < outlet; ++prior) {
            if (configured.route_id == plan.outlets[prior].route_id ||
                configured.route_semantic_id ==
                    plan.outlets[prior].route_semantic_id) {
                throw std::invalid_argument{
                    "presentation outlet identities must be distinct"};
            }
        }

        const auto requirement = std::ranges::find(
            plan.output_contract.required_source_routes,
            configured.route_semantic_id,
            &contract::SourceRouteRequirement::semantic_id);
        if (requirement == plan.output_contract.required_source_routes.end() ||
            requirement->kind != contract::SourceRouteKind::exhaust_outlet ||
            requirement->disposition != contract::RouteDisposition::rendered ||
            !requirement->disposition_reason.empty() ||
            requirement->artifact_roles.size() != 1 ||
            requirement->artifact_roles.front() !=
                configured.pressure_stem_artifact.role) {
            throw std::invalid_argument{
                "presentation outlet artifact differs from its source-route owner"};
        }
    }

    if (plan.output_contract.required_source_routes.size() != kOutletCount ||
        plan.output_contract.required_output_buses.size() != 2 ||
        plan.output_contract.required_artifacts.size() !=
            kPresentationAudioArtifactCount) {
        throw std::invalid_argument{
            "presentation requires exactly two outlets, two buses, and four artifacts"};
    }

    const auto raw_bus = std::ranges::find(
        plan.output_contract.required_output_buses,
        contract::OutputBusKind::master_engine_raw,
        &contract::OutputBusRequirement::kind);
    const auto audition_bus = std::ranges::find(
        plan.output_contract.required_output_buses,
        contract::OutputBusKind::master_engine_audition,
        &contract::OutputBusRequirement::kind);
    if (raw_bus == plan.output_contract.required_output_buses.end() ||
        audition_bus == plan.output_contract.required_output_buses.end() ||
        raw_bus == audition_bus || raw_bus->artifact_roles.size() != 1 ||
        audition_bus->artifact_roles.size() != 1 ||
        raw_bus->artifact_roles.front() != plan.master.raw_master_artifact.role ||
        audition_bus->artifact_roles.front() !=
            plan.master.audition_master_artifact.role) {
        throw std::invalid_argument{
            "presentation master artifacts differ from their engine-bus owners"};
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
        if (!contract::is_valid_semantic_id(pending.role) ||
            pending.relative_path.empty() ||
            pending.kind != contract::ArtifactKind::audio ||
            !pending.audio.has_value() ||
            required == plan.output_contract.required_artifacts.end() ||
            pending.kind != required->kind || pending.audio != required->audio ||
            pending.diagnostic != required->diagnostic || pending.diagnostic ||
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
                "presentation artifact media differs from the physical pressure "
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
    };
}

[[nodiscard]] artifacts::AuditionWaveEncoder make_audition_wave_encoder(
    const PresentationRenderPlan &plan,
    const std::array<PendingArtifact, kPresentationAudioArtifactCount>
        &audio_artifacts) {
    auto result = artifacts::make_audition_wave_encoder(
        *audio_artifacts[index(AudioIndex::master_audition)].audio,
        plan.master.audition_metadata, {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::AuditionWaveEncoder>(std::move(result));
}

[[nodiscard]] float calibrated_pressure_float32(double pressure_pa,
                                                double pa_per_full_scale) {
    if (!std::isfinite(pressure_pa)) {
        throw std::domain_error{"radiated outlet pressure was non-finite"};
    }
    const double full_scale = pressure_pa / pa_per_full_scale;
    if (!std::isfinite(full_scale)) {
        throw std::domain_error{"Pa calibration produced non-finite output"};
    }
    const float published = static_cast<float>(full_scale);
    if (!std::isfinite(published)) {
        throw std::domain_error{
            "Pa calibration overflowed during binary64-to-Float32 publication"};
    }
    return published;
}

struct RenderScratch {
    RenderScratch() {
        for (auto &outlet : outlets) {
            outlet.reserve(kMaximumPresentationInputFramesPerBlock);
        }
        raw.reserve(kMaximumPresentationInputFramesPerBlock);
        pcm24.reserve(kMaximumPresentationInputFramesPerBlock);
        mastered.reserve(kMaximumPresentationInputFramesPerBlock);
    }

    std::array<std::vector<float>, kOutletCount> outlets;
    std::vector<float> raw;
    std::vector<std::int32_t> pcm24;
    std::vector<MasteredFrame> mastered;
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
    Implementation(RenderSink &sink, PresentationRenderPlan plan, RenderControl control)
        : sink_(sink), plan_(validated_plan(std::move(plan))),
          audio_artifacts_(ordered_audio_artifacts(plan_)),
          control_(std::move(control)),
          encoders_(make_float_wave_encoders(audio_artifacts_)),
          audition_(make_audition_wave_encoder(plan_, audio_artifacts_)),
          scratch_(std::make_unique<RenderScratch>()) {
        try {
            begin();
        } catch (...) {
            // A throwing constructor does not run ~Implementation. Close a
            // successfully begun sink transaction before member unwinding.
            abort_once();
            throw;
        }
    }

    ~Implementation() {
        abort_once();
    }

    void process(const acoustics::ExhaustAcousticPressureBlock &input) {
        if (state_ != PresentationRenderSessionState::active) {
            abort_once();
            throw std::logic_error{"presentation input requires an active session"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{
                "presentation cancelled between complete acoustic blocks"};
        }

        try {
            const auto frame_count = validate_input(input);
            const auto block_begin = stats_.input_frame_count;
            const auto block_end =
                block_begin + static_cast<std::uint64_t>(frame_count);
            const auto preparation_end = plan_.timeline.pre_audible_frame_count;
            const auto preparation_frames =
                block_begin < preparation_end
                    ? std::min(block_end, preparation_end) - block_begin
                    : 0U;
            const auto first_published_input_frame =
                static_cast<std::size_t>(preparation_frames);
            const auto audible_frames = frame_count - first_published_input_frame;

            stage_published_block(input, first_published_input_frame, audible_frames);
            if (audible_frames != 0U) {
                write_published_block();
            }

            stats_.input_frame_count = block_end;
            ++stats_.processed_block_count;
            stats_.pre_audible_frame_count += preparation_frames;
            stats_.published_frame_count +=
                static_cast<std::uint64_t>(audible_frames);
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
                        published_frame_count(plan_) ||
                    encoders_[artifact_index].bytes_emitted() !=
                        observations_[artifact_index].byte_count) {
                    throw std::logic_error{"Float32 WAVE length changed"};
                }
            }
            require_encoding_success(
                audition_.finish(consumers_[index(AudioIndex::master_audition)]),
                "audition WAVE finalization failed");
            if (audition_.frames_written() != published_frame_count(plan_) ||
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
                    "commit manifest differs from this session's sealed artifact or "
                    "execution evidence"};
            }
            const auto report = contract::validate(manifest, provenance, source_matrix);
            if (!report.ok()) {
                throw manifest_validation_error(report);
            }
        } catch (...) {
            abort_once();
            throw;
        }

        // RenderSink::commit is the terminal attempt. Once called, the sink owns
        // cleanup on both outcomes, so this session must never issue a later abort.
        commit_attempted_ = true;
        try {
            require_sink_success("could not commit presentation",
                                 sink_.commit(manifest));
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
        // and declarations are complete; the first WAVE bytes and all signal work
        // follow.
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

    [[nodiscard]] std::size_t
    validate_input(const acoustics::ExhaustAcousticPressureBlock &input) const {
        if (input.rate != kAcousticRate) {
            throw std::invalid_argument{
                "presentation input must be exactly 192000/1 Hz"};
        }
        if (input.first_frame_index != stats_.input_frame_count) {
            throw std::invalid_argument{
                "presentation acoustic input is discontinuous"};
        }
        const auto frame_count = input.outlets[0].pressure_pa.size();
        if (frame_count == 0 ||
            frame_count > kMaximumPresentationInputFramesPerBlock ||
            input.outlets[1].pressure_pa.size() != frame_count) {
            throw std::invalid_argument{
                "presentation requires equal nonempty bounded outlet spans"};
        }
        if (frame_count > plan_.timeline.total_acoustic_frame_count -
                              stats_.input_frame_count) {
            throw std::invalid_argument{
                "presentation input exceeds its acoustic timeline"};
        }
        for (std::size_t outlet = 0; outlet < kOutletCount; ++outlet) {
            if (input.outlets[outlet].route_id != plan_.outlets[outlet].route_id) {
                throw std::invalid_argument{
                    "presentation acoustic outlet order or identity changed"};
            }
            if (std::ranges::any_of(input.outlets[outlet].pressure_pa,
                                    [](double value) {
                                        return !std::isfinite(value);
                                    })) {
                throw std::domain_error{
                    "presentation acoustic input contained non-finite pressure"};
            }
        }
        return frame_count;
    }

    void stage_published_block(
        const acoustics::ExhaustAcousticPressureBlock &input,
        std::size_t first_published_input_frame, std::size_t audible_frames) {
        for (auto &outlet : scratch_->outlets) {
            outlet.resize(audible_frames);
        }
        scratch_->raw.resize(audible_frames);
        scratch_->pcm24.resize(audible_frames);
        scratch_->mastered.resize(audible_frames);

        for (std::size_t outlet = 0; outlet < kOutletCount; ++outlet) {
            for (std::size_t frame = 0; frame < audible_frames; ++frame) {
                scratch_->outlets[outlet][frame] = calibrated_pressure_float32(
                    input.outlets[outlet]
                        .pressure_pa[first_published_input_frame + frame],
                    plan_.pa_per_full_scale);
            }
        }

        master_block(scratch_->outlets[0], scratch_->outlets[1],
                     stats_.published_frame_count, plan_.master.mastering,
                     scratch_->mastered);
        for (std::size_t frame = 0; frame < audible_frames; ++frame) {
            scratch_->raw[frame] = scratch_->mastered[frame].raw;
            scratch_->pcm24[frame] = scratch_->mastered[frame].pcm24;
        }
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
            if (!pending_sink_error_.has_value()) {
                pending_sink_error_ = *status;
            }
            return false;
        }
        observed.hash.update(bytes);
        observed.byte_count += static_cast<std::uint64_t>(bytes.size());
        return true;
    }

    void require_encoding_success(const WavEncodingStatus &status,
                                  std::string_view operation) const {
        if (pending_sink_error_.has_value()) {
            throw PresentationSinkFailure{operation, *pending_sink_error_};
        }
        if (status.has_value()) {
            throw std::runtime_error{std::string(operation) + ": " + status->message};
        }
    }

    void write_published_block() {
        require_encoding_success(
            encoders_[index(AudioIndex::outlet_front_pressure)]
                .write_float32_interleaved(
                    scratch_->outlets[0],
                    consumers_[index(AudioIndex::outlet_front_pressure)]),
            "front-outlet WAVE payload emission failed");
        require_encoding_success(
            encoders_[index(AudioIndex::outlet_rear_pressure)]
                .write_float32_interleaved(
                    scratch_->outlets[1],
                    consumers_[index(AudioIndex::outlet_rear_pressure)]),
            "rear-outlet WAVE payload emission failed");
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
        const auto expected_published_frames = published_frame_count(plan_);
        if (stats_.input_frame_count !=
                plan_.timeline.total_acoustic_frame_count ||
            stats_.pre_audible_frame_count !=
                plan_.timeline.pre_audible_frame_count ||
            stats_.published_frame_count != expected_published_frames) {
            throw std::logic_error{
                "presentation produced an incomplete acoustic frame interval"};
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
    std::array<PendingArtifact, kPresentationAudioArtifactCount> audio_artifacts_;
    RenderControl control_;
    std::array<WavEncoder, kFloatWaveCount> encoders_;
    artifacts::AuditionWaveEncoder audition_;
    std::unique_ptr<RenderScratch> scratch_;
    std::array<ArtifactObservation, kPresentationAudioArtifactCount> observations_;
    std::array<artifacts::WavChunkConsumer, kPresentationAudioArtifactCount> consumers_;
    std::optional<execution::LinuxExecutionFactsObservation> execution_observation_;
    PresentationRenderStats stats_;
    std::optional<std::array<contract::ArtifactRecord,
                             kPresentationAudioArtifactCount>>
        sealed_artifacts_;
    std::optional<contract::ExecutionFacts> sealed_execution_;
    std::optional<RenderSinkError> pending_sink_error_;
    PresentationRenderSessionState state_ = PresentationRenderSessionState::active;
    bool transaction_begun_ = false;
    bool commit_attempted_ = false;
};

PresentationSinkFailure::PresentationSinkFailure(std::string_view operation,
                                                 RenderSinkError error)
    : std::runtime_error{sink_failure_message(operation, error)},
      sink_error_(std::move(error)) {}

const RenderSinkError &PresentationSinkFailure::sink_error() const noexcept {
    return sink_error_;
}

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

void PresentationRenderSession::process(
    const acoustics::ExhaustAcousticPressureBlock &input) {
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
