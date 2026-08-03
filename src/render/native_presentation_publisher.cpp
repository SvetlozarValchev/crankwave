#include "render/native_presentation_publisher.hpp"

#include "contract/sha256_stream.hpp"
#include "engine_sim_offline/artifacts/wav_encoder.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::render_detail {
namespace {

using artifacts::WavEncoder;
using artifacts::WavEncodingStatus;

constexpr std::size_t kSourceFramesPerBlock = kEngineSessionDeliveryFramesPerBlock;
constexpr std::size_t kMaximumWaveChunkBytes = 16U * 1024U;

[[nodiscard]] constexpr std::size_t stem_count(std::size_t route_count) noexcept {
    return route_count * kNativePresentationArtifactsPerRoute;
}

[[nodiscard]] constexpr std::size_t float_wave_count(std::size_t route_count) noexcept {
    return stem_count(route_count) + 1U;
}

[[nodiscard]] constexpr std::size_t raw_master_index(std::size_t route_count) noexcept {
    return stem_count(route_count);
}

[[nodiscard]] constexpr std::size_t
audition_master_index(std::size_t route_count) noexcept {
    return float_wave_count(route_count);
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
published_block_count(const NativePresentationPublicationPlan &plan) noexcept {
    return plan.timeline.total_block_count - plan.timeline.pre_audible_block_count;
}

[[nodiscard]] std::uint64_t
processed_input_frame_count(const NativePresentationPublicationPlan &plan,
                            std::size_t input_frames_per_block) {
    return checked_frame_product(plan.timeline.total_block_count, input_frames_per_block,
                                 "native presentation input horizon");
}

[[nodiscard]] std::uint64_t
processed_source_frame_count(const NativePresentationPublicationPlan &plan) {
    return checked_frame_product(plan.timeline.total_block_count, kSourceFramesPerBlock,
                                 "native presentation source horizon");
}

[[nodiscard]] std::uint64_t
pre_audible_source_frame_count(const NativePresentationPublicationPlan &plan) {
    return checked_frame_product(plan.timeline.pre_audible_block_count,
                                 kSourceFramesPerBlock,
                                 "native presentation pre-audible horizon");
}

[[nodiscard]] std::uint64_t
published_source_frame_count(const NativePresentationPublicationPlan &plan) {
    return checked_frame_product(published_block_count(plan), kSourceFramesPerBlock,
                                 "native presentation audible horizon");
}

[[nodiscard]] std::vector<PendingArtifact>
ordered_audio_artifacts(const NativePresentationPublicationPlan &plan) {
    std::vector<PendingArtifact> result;
    result.reserve(native_presentation_artifact_count(plan.routes.size()));
    for (const auto &route : plan.routes) {
        result.push_back(route.artifacts.dry);
        result.push_back(route.artifacts.configured_ir);
        result.push_back(route.artifacts.selected);
    }
    result.push_back(plan.audition.raw_master_artifact);
    result.push_back(plan.audition.audition_master_artifact);
    return result;
}

[[nodiscard]] std::string sink_failure_message(std::string_view operation,
                                               const RenderSinkError &error) {
    return std::string(operation) + ": " + error.detail_code + ": " + error.message;
}

void require_sink_success(std::string_view operation, const RenderSinkStatus &status) {
    if (status.has_value()) {
        throw NativePresentationSinkFailure{operation, *status};
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
    throw execution_error("could not begin native presentation execution observation",
                          std::get<execution::LinuxExecutionFactsError>(result));
}

[[nodiscard]] execution::ObservedExecutionFacts
require_execution_finish(execution::LinuxExecutionFactsObservation &&observation) {
    auto result = execution::finish_single_job_linux_execution(std::move(observation));
    if (auto *facts = std::get_if<execution::ObservedExecutionFacts>(&result)) {
        return std::move(*facts);
    }
    throw execution_error("could not finish native presentation execution observation",
                          std::get<execution::LinuxExecutionFactsError>(result));
}

[[nodiscard]] bool is_float_audio(const contract::AudioContract &audio,
                                  std::uint64_t expected_frame_count) noexcept {
    return audio.sample_rate == kEngineSessionDeliveryRateHz &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "float32le";
}

[[nodiscard]] bool is_audition_audio(const contract::AudioContract &audio,
                                     std::uint64_t expected_frame_count) noexcept {
    return audio.sample_rate == kEngineSessionDeliveryRateHz &&
           audio.frame_count == expected_frame_count &&
           audio.channel_layout_id == "mono" && audio.sample_encoding_id == "pcm_s24le";
}

[[nodiscard]] presentation::MasteringSettings
make_fade_settings(const NativePresentationPublicationPlan &plan) {
    return {
        plan.audition.fade.audible_frame_count,
        plan.audition.fade.fade_in_frame_count,
        plan.audition.fade.fade_out_frame_count,
        1.0F,
    };
}

void validate_plan(const NativePresentationPublicationPlan &plan) {
    if (plan.timeline.total_block_count == 0 ||
        plan.timeline.pre_audible_block_count >= plan.timeline.total_block_count) {
        throw std::invalid_argument{
            "native presentation timeline requires a positive audible block "
            "interval"};
    }
    if (plan.timeline.tail_policy !=
        NativePresentationTailPolicy::truncate_at_timeline_end) {
        throw std::invalid_argument{
            "native presentation timeline requires an explicit supported tail "
            "policy"};
    }
    if (plan.methods != presentation::implemented_presentation_method_identities()) {
        throw std::invalid_argument{
            "native presentation methods do not exactly match the executable "
            "implementation"};
    }

    static_cast<void>(processed_source_frame_count(plan));
    static_cast<void>(pre_audible_source_frame_count(plan));
    const auto audible_frames = published_source_frame_count(plan);
    if (plan.audition.fade.audible_frame_count != audible_frames) {
        throw std::invalid_argument{
            "native presentation mastering horizon differs from the timeline"};
    }
    static_cast<void>(make_fade_settings(plan));
    if (plan.routes.empty()) {
        throw std::invalid_argument{
            "native presentation requires at least one rendered route"};
    }
    if (!native_presentation_artifact_count_representable(plan.routes.size())) {
        throw std::invalid_argument{
            "native presentation route count cannot represent its artifact "
            "layout"};
    }

    for (std::size_t route = 0; route < plan.routes.size(); ++route) {
        const auto &configured = plan.routes[route];
        if (!configured.route_id.valid() || configured.route_semantic_id.empty()) {
            throw std::invalid_argument{
                "native presentation route requires valid route identities"};
        }
        for (std::size_t prior = 0; prior < route; ++prior) {
            if (configured.route_id == plan.routes[prior].route_id ||
                configured.route_semantic_id == plan.routes[prior].route_semantic_id) {
                throw std::invalid_argument{
                    "native presentation route identities must be distinct"};
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
                "native presentation route artifacts differ from their "
                "source-route owner"};
        }
    }

    if (plan.audition.selected_route_ids.size() != plan.routes.size()) {
        throw std::invalid_argument{
            "native presentation audition must select every rendered route"};
    }
    std::unordered_set<std::uint32_t> selected_route_ids;
    for (const auto selected : plan.audition.selected_route_ids) {
        if (!selected_route_ids.insert(selected.value).second) {
            throw std::invalid_argument{
                "native presentation audition routes must be distinct"};
        }
        if (std::ranges::none_of(plan.routes, [&](const auto &route) {
                return route.route_id == selected;
            })) {
            throw std::invalid_argument{
                "native presentation audition route is absent from the route "
                "plan"};
        }
    }

    if (plan.output_contract.required_source_routes.size() != plan.routes.size() ||
        plan.output_contract.required_output_buses.size() != 2 ||
        plan.output_contract.required_artifacts.size() !=
            native_presentation_artifact_count(plan.routes.size())) {
        throw std::invalid_argument{
            "native presentation requires three artifacts per rendered route "
            "and exactly two master buses and artifacts"};
    }

    const auto raw_bus = std::ranges::find_if(
        plan.output_contract.required_output_buses, [](const auto &bus) {
            return bus.kind == contract::OutputBusKind::master_engine_raw;
        });
    const auto audition_bus = std::ranges::find_if(
        plan.output_contract.required_output_buses, [](const auto &bus) {
            return bus.kind == contract::OutputBusKind::master_engine_audition;
        });
    if (raw_bus == plan.output_contract.required_output_buses.end() ||
        audition_bus == plan.output_contract.required_output_buses.end() ||
        raw_bus == audition_bus || raw_bus->artifact_roles.size() != 1 ||
        audition_bus->artifact_roles.size() != 1 ||
        raw_bus->artifact_roles.front() != plan.audition.raw_master_artifact.role ||
        audition_bus->artifact_roles.front() !=
            plan.audition.audition_master_artifact.role) {
        throw std::invalid_argument{
            "native presentation master artifacts differ from their "
            "output-bus owners"};
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
                "native presentation artifact plan differs from its output "
                "contract"};
        }
        const bool media_matches =
            artifact_index < float_wave_count(plan.routes.size())
                ? is_float_audio(*pending.audio, audible_frames)
                : is_audition_audio(*pending.audio, audible_frames);
        if (!media_matches) {
            throw std::invalid_argument{
                "native presentation artifact media differs from the accepted "
                "publisher"};
        }
    }
}

[[nodiscard]] NativePresentationPublicationPlan
validated_plan(NativePresentationPublicationPlan plan) {
    validate_plan(plan);
    return plan;
}

struct NativePresentationRouteBusBinding {
    std::array<std::size_t, kNativePresentationArtifactsPerRoute> stems{};
};

struct NativePresentationBusBinding {
    std::vector<NativePresentationRouteBusBinding> routes;
    std::size_t raw_master = 0;
    std::size_t audition_master = 0;
    std::size_t bus_count = 0;
    std::string raw_master_id;
    std::string audition_master_id;
};

[[nodiscard]] bool
same_optional_route(const std::optional<contract::RouteId> &actual,
                    const std::optional<contract::RouteId> &expected) noexcept {
    return actual == expected;
}

[[nodiscard]] bool is_expected_bus(const EngineAudioBusDescriptor &bus,
                                   EngineAudioBusKind kind,
                                   const std::optional<contract::RouteId> &route_id,
                                   std::string_view id) noexcept {
    return bus.id == id && bus.kind == kind &&
           same_optional_route(bus.route_id, route_id) && bus.channel_count == 1U &&
           bus.sample_rate == kEngineSessionDeliveryRateHz;
}

[[nodiscard]] std::size_t bind_unique_bus(const EngineSessionDescriptor &session,
                                          std::vector<bool> &claimed,
                                          EngineAudioBusKind kind,
                                          std::optional<contract::RouteId> route_id,
                                          std::string_view id) {
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < session.audio_buses.size(); ++index) {
        if (!is_expected_bus(session.audio_buses[index], kind, route_id, id)) {
            continue;
        }
        if (match.has_value()) {
            throw std::invalid_argument{
                "native presentation session descriptor contains a duplicate "
                "required audio bus"};
        }
        match = index;
    }
    if (!match.has_value() || claimed[*match]) {
        throw std::invalid_argument{
            "native presentation session descriptor lacks one unique required "
            "audio bus"};
    }
    claimed[*match] = true;
    return *match;
}

[[nodiscard]] const contract::OutputBusRequirement &
require_output_bus(const NativePresentationPublicationPlan &plan,
                   contract::OutputBusKind kind) {
    const auto found = std::ranges::find(plan.output_contract.required_output_buses,
                                         kind, &contract::OutputBusRequirement::kind);
    if (found == plan.output_contract.required_output_buses.end()) {
        throw std::invalid_argument{
            "native presentation plan lacks a required master output bus"};
    }
    return *found;
}

[[nodiscard]] NativePresentationBusBinding
bind_session_buses(const EngineSessionDescriptor &session,
                   const NativePresentationPublicationPlan &plan) {
    if (session.physics_rate != kEngineSessionPhysicsRateHz ||
        session.physics_frames_per_block != kEngineSessionPhysicsFramesPerBlock ||
        session.delivery_rate != kEngineSessionDeliveryRateHz ||
        session.delivery_frames_per_block != kEngineSessionDeliveryFramesPerBlock ||
        session.total_block_count != plan.timeline.total_block_count ||
        session.preparation_block_count != plan.timeline.pre_audible_block_count ||
        session.audio_buses.size() !=
            native_presentation_artifact_count(plan.routes.size())) {
        throw std::invalid_argument{
            "native presentation session descriptor differs from its publication "
            "timeline or fixed block contract"};
    }

    NativePresentationBusBinding result;
    result.routes.resize(plan.routes.size());
    result.bus_count = session.audio_buses.size();
    std::vector<bool> claimed(result.bus_count, false);

    constexpr std::array route_kinds{
        EngineAudioBusKind::exhaust_route_dry,
        EngineAudioBusKind::exhaust_route_configured_ir,
        EngineAudioBusKind::exhaust_route_selected,
    };
    for (std::size_t route = 0; route < plan.routes.size(); ++route) {
        const auto &configured = plan.routes[route];
        const std::array<std::string_view, kNativePresentationArtifactsPerRoute> ids{
            configured.artifacts.dry.role,
            configured.artifacts.configured_ir.role,
            configured.artifacts.selected.role,
        };
        for (std::size_t stem = 0; stem < route_kinds.size(); ++stem) {
            result.routes[route].stems[stem] = bind_unique_bus(
                session, claimed, route_kinds[stem], configured.route_id, ids[stem]);
        }
    }

    const auto &raw =
        require_output_bus(plan, contract::OutputBusKind::master_engine_raw);
    const auto &audition =
        require_output_bus(plan, contract::OutputBusKind::master_engine_audition);
    result.raw_master_id = raw.semantic_id;
    result.audition_master_id = audition.semantic_id;
    result.raw_master =
        bind_unique_bus(session, claimed, EngineAudioBusKind::engine_raw_master,
                        std::nullopt, result.raw_master_id);
    result.audition_master =
        bind_unique_bus(session, claimed, EngineAudioBusKind::engine_audition_master,
                        std::nullopt, result.audition_master_id);

    if (std::ranges::any_of(claimed, [](bool value) { return !value; })) {
        throw std::invalid_argument{
            "native presentation session descriptor contains an unbound audio "
            "bus"};
    }
    return result;
}

[[nodiscard]] WavEncoder
make_float_wave_encoder(std::span<const PendingArtifact> audio_artifacts,
                        std::size_t artifact_index) {
    auto result = artifacts::make_wav_encoder(*audio_artifacts[artifact_index].audio,
                                              {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct native Float32 WAVE encoder: " +
                               error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] std::vector<WavEncoder>
make_float_wave_encoders(std::span<const PendingArtifact> audio_artifacts,
                         std::size_t route_count) {
    std::vector<WavEncoder> result;
    result.reserve(float_wave_count(route_count));
    for (std::size_t artifact = 0; artifact < float_wave_count(route_count);
         ++artifact) {
        result.push_back(make_float_wave_encoder(audio_artifacts, artifact));
    }
    return result;
}

[[nodiscard]] artifacts::AuditionWaveEncoder
make_audition_wave_encoder(const NativePresentationPublicationPlan &plan,
                           std::span<const PendingArtifact> audio_artifacts) {
    auto result = artifacts::make_audition_wave_encoder(
        *audio_artifacts[audition_master_index(plan.routes.size())].audio,
        plan.audition.metadata, {kMaximumWaveChunkBytes});
    if (const auto *error = std::get_if<artifacts::WavEncodingError>(&result)) {
        throw std::logic_error{"cannot construct native audition WAVE encoder: " +
                               error->message};
    }
    return std::get<artifacts::AuditionWaveEncoder>(std::move(result));
}

struct PublicationScratch {
    std::array<std::int32_t, kSourceFramesPerBlock> pcm24{};
};

struct ArtifactObservation {
    contract::detail::Sha256Stream hash;
    std::uint64_t byte_count = 0;
};

struct FinishedPublisher {
    NativePresentationPublicationStats stats;
    std::vector<contract::ArtifactRecord> artifacts;
    execution::ObservedExecutionFacts execution;
};

[[nodiscard]] std::logic_error
manifest_validation_error(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return std::logic_error{
            "complete native publication manifest failed validation"};
    }
    const auto &first = report.issues.front();
    return std::logic_error{"complete native publication manifest is invalid at " +
                            first.path + ": " + first.message};
}

} // namespace

class NativePresentationPublisher::Implementation final {
  public:
    Implementation(RenderSink &sink, const EngineSessionDescriptor &session,
                   NativePresentationPublicationPlan plan, RenderControl control)
        : sink_(sink), plan_(validated_plan(std::move(plan))),
          input_frames_per_block_(session.physics_frames_per_block),
          buses_(bind_session_buses(session, plan_)),
          audio_artifacts_(ordered_audio_artifacts(plan_)),
          control_(std::move(control)),
          encoders_(make_float_wave_encoders(audio_artifacts_, plan_.routes.size())),
          audition_(make_audition_wave_encoder(plan_, audio_artifacts_)),
          fade_settings_(make_fade_settings(plan_)),
          observations_(audio_artifacts_.size()), consumers_(audio_artifacts_.size()) {
        try {
            static_cast<void>(
                processed_input_frame_count(plan_, input_frames_per_block_));
            begin();
        } catch (...) {
            // A throwing constructor does not run ~Implementation.
            abort_once();
            throw;
        }
    }

    ~Implementation() {
        abort_once();
    }

    void process(const EngineSessionBlockView &block) {
        if (state_ != NativePresentationPublisherState::active) {
            abort_once();
            throw std::logic_error{
                "native publication input requires an active publisher"};
        }
        if (stats_.processed_block_count >= plan_.timeline.total_block_count) {
            abort_once();
            throw std::invalid_argument{
                "native publication received more blocks than its timeline"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{
                "native publication cancelled between complete input blocks"};
        }

        try {
            validate_block(block);

            stats_.input_frame_count +=
                static_cast<std::uint64_t>(block.physics_frame_count());
            ++stats_.processed_block_count;
            stats_.processed_source_frame_count +=
                static_cast<std::uint64_t>(block.delivery_frame_count());

            const auto completed_block = stats_.processed_block_count - 1U;
            if (completed_block < plan_.timeline.pre_audible_block_count) {
                ++stats_.pre_audible_block_count;
                stats_.pre_audible_source_frame_count +=
                    static_cast<std::uint64_t>(block.delivery_frame_count());
            } else {
                write_published_block(block);
                audible_frame_ +=
                    static_cast<std::uint64_t>(block.delivery_frame_count());
                ++stats_.published_block_count;
                stats_.published_source_frame_count +=
                    static_cast<std::uint64_t>(block.delivery_frame_count());
            }
        } catch (...) {
            abort_once();
            throw;
        }
    }

    [[nodiscard]] FinishedPublisher finish() {
        if (state_ != NativePresentationPublisherState::active) {
            abort_once();
            throw std::logic_error{
                "native publication finalization requires an active publisher"};
        }
        if (control_.stop_token.stop_requested()) {
            abort_once();
            throw std::runtime_error{
                "native publication cancelled before finalization"};
        }

        try {
            require_complete_schedule();
            for (std::size_t artifact_index = 0; artifact_index < encoders_.size();
                 ++artifact_index) {
                require_encoding_success(
                    encoders_[artifact_index].finish(consumers_[artifact_index]),
                    "native WAVE finalization failed");
                if (encoders_[artifact_index].frames_written() !=
                        published_source_frame_count(plan_) ||
                    encoders_[artifact_index].bytes_emitted() !=
                        observations_[artifact_index].byte_count) {
                    throw std::logic_error{"native Float32 WAVE length changed"};
                }
            }

            const auto audition_index = audition_master_index(plan_.routes.size());
            require_encoding_success(audition_.finish(consumers_[audition_index]),
                                     "native audition WAVE finalization failed");
            if (audition_.frames_written() != published_source_frame_count(plan_) ||
                audition_.bytes_emitted() != observations_[audition_index].byte_count) {
                throw std::logic_error{"native audition WAVE length changed"};
            }

            std::vector<contract::ArtifactRecord> records;
            records.reserve(audio_artifacts_.size());
            for (std::size_t artifact_index = 0;
                 artifact_index < audio_artifacts_.size(); ++artifact_index) {
                const auto &pending = audio_artifacts_[artifact_index];
                auto &observed = observations_[artifact_index];
                records.push_back({
                    pending.role,
                    pending.kind,
                    pending.relative_path,
                    pending.audio,
                    observed.byte_count,
                    observed.hash.finish(),
                    pending.diagnostic,
                });
                require_sink_success("could not seal native presentation artifact",
                                     sink_.seal_artifact(records.back()));
            }

            if (!execution_observation_.has_value()) {
                throw std::logic_error{
                    "native presentation lost its execution observation"};
            }
            auto execution =
                require_execution_finish(std::move(*execution_observation_));
            execution_observation_.reset();

            sealed_artifacts_ = records;
            sealed_execution_ = execution.facts();
            state_ = NativePresentationPublisherState::sealed;
            return {
                stats_,
                std::move(records),
                std::move(execution),
            };
        } catch (...) {
            abort_once();
            throw;
        }
    }

    void commit(const SealedNativePresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix) {
        if (state_ != NativePresentationPublisherState::sealed ||
            !sealed_artifacts_.has_value() || !sealed_execution_.has_value()) {
            abort_once();
            throw std::logic_error{
                "native presentation commit requires this publisher's sealed "
                "evidence"};
        }

        try {
            if (!std::ranges::equal(evidence.artifacts(), *sealed_artifacts_) ||
                evidence.execution().facts() != *sealed_execution_ ||
                manifest.execution !=
                    std::optional<contract::ExecutionFacts>{*sealed_execution_} ||
                manifest.content.output_contract != plan_.output_contract ||
                manifest.content.artifacts.size() != sealed_artifacts_->size() ||
                !std::equal(manifest.content.artifacts.begin(),
                            manifest.content.artifacts.end(),
                            sealed_artifacts_->begin())) {
                throw std::logic_error{
                    "commit manifest differs from this native publisher's "
                    "sealed artifact or execution evidence"};
            }
            const auto report = contract::validate(manifest, provenance, source_matrix);
            if (!report.ok()) {
                throw manifest_validation_error(report);
            }
        } catch (...) {
            abort_once();
            throw;
        }

        // RenderSink::commit is the terminal attempt. The sink owns cleanup
        // after this call on both success and failure.
        commit_attempted_ = true;
        try {
            require_sink_success("could not commit native presentation",
                                 sink_.commit(manifest));
            state_ = NativePresentationPublisherState::committed;
        } catch (...) {
            if (state_ != NativePresentationPublisherState::committed) {
                state_ = NativePresentationPublisherState::aborted;
            }
            throw;
        }
    }

    [[nodiscard]] NativePresentationPublisherState state() const noexcept {
        return state_;
    }

  private:
    void begin() {
        if (control_.stop_token.stop_requested()) {
            throw std::runtime_error{
                "native presentation cancelled before transaction begin"};
        }
        require_sink_success("could not begin native presentation transaction",
                             sink_.begin_transaction(plan_.output_contract));
        transaction_begun_ = true;

        for (const auto &pending : audio_artifacts_) {
            require_sink_success("could not declare native presentation artifact",
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

        // Declarations are complete; every native WAVE byte and all
        // publication work follow this observation boundary.
        execution_observation_.emplace(require_execution_begin());
        for (std::size_t artifact_index = 0; artifact_index < encoders_.size();
             ++artifact_index) {
            require_encoding_success(
                encoders_[artifact_index].begin(consumers_[artifact_index]),
                "native WAVE header emission failed");
        }
        require_encoding_success(
            audition_.begin(consumers_[audition_master_index(plan_.routes.size())]),
            "native audition WAVE prefix emission failed");
    }

    void validate_runtime_bus(const EngineAudioBusBlockView &bus,
                              EngineAudioBusKind kind,
                              std::optional<contract::RouteId> route_id,
                              std::string_view id) const {
        if (!is_expected_bus(bus.descriptor, kind, route_id, id) ||
            bus.samples.size() != kSourceFramesPerBlock) {
            throw std::invalid_argument{
                "native publication block differs from its prebound session "
                "audio-bus contract"};
        }
    }

    void validate_block(const EngineSessionBlockView &block) const {
        const auto expected_block = stats_.processed_block_count;
        const auto expected_input_frame = checked_frame_product(
            expected_block, input_frames_per_block_, "native input continuity");
        const auto expected_source_frame = checked_frame_product(
            expected_block, kSourceFramesPerBlock, "native source continuity");
        const auto expected_phase =
            expected_block < plan_.timeline.pre_audible_block_count
                ? EngineSessionBlockPhase::preparation
                : EngineSessionBlockPhase::audible;
        if (block.block_ordinal() != expected_block ||
            block.phase() != expected_phase ||
            block.first_physics_frame() != expected_input_frame ||
            block.first_delivery_frame() != expected_source_frame ||
            block.physics_frame_count() != input_frames_per_block_ ||
            block.delivery_frame_count() != kSourceFramesPerBlock ||
            block.audio_buses().size() != buses_.bus_count) {
            throw std::invalid_argument{
                "native publication requires one exact contiguous public session "
                "quantum"};
        }

        const auto audio_buses = block.audio_buses();
        constexpr std::array route_kinds{
            EngineAudioBusKind::exhaust_route_dry,
            EngineAudioBusKind::exhaust_route_configured_ir,
            EngineAudioBusKind::exhaust_route_selected,
        };
        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            const auto &configured = plan_.routes[route];
            const std::array<std::string_view, kNativePresentationArtifactsPerRoute>
                ids{
                    configured.artifacts.dry.role,
                    configured.artifacts.configured_ir.role,
                    configured.artifacts.selected.role,
                };
            for (std::size_t stem = 0; stem < route_kinds.size(); ++stem) {
                validate_runtime_bus(audio_buses[buses_.routes[route].stems[stem]],
                                     route_kinds[stem], configured.route_id, ids[stem]);
            }
        }
        validate_runtime_bus(audio_buses[buses_.raw_master],
                             EngineAudioBusKind::engine_raw_master, std::nullopt,
                             buses_.raw_master_id);
        validate_runtime_bus(audio_buses[buses_.audition_master],
                             EngineAudioBusKind::engine_audition_master, std::nullopt,
                             buses_.audition_master_id);
    }

    void prepare_published_block(const EngineSessionBlockView &block) {
        const auto audio_buses = block.audio_buses();
        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            for (const auto stem_bus : buses_.routes[route].stems) {
                for (const float sample : audio_buses[stem_bus].samples) {
                    if (!std::isfinite(sample)) {
                        throw std::domain_error{
                            "native presentation stem was non-finite"};
                    }
                }
            }
        }
        for (const float sample : audio_buses[buses_.raw_master].samples) {
            if (!std::isfinite(sample)) {
                throw std::domain_error{
                    "native presentation raw master was non-finite"};
            }
        }

        const auto monitor = audio_buses[buses_.audition_master].samples;
        for (std::size_t frame = 0; frame < monitor.size(); ++frame) {
            if (!std::isfinite(monitor[frame])) {
                throw std::domain_error{
                    "native presentation audition monitor was non-finite"};
            }
            const double fade = presentation::audition_fade_gain(audible_frame_ + frame,
                                                                 fade_settings_);
            const float faded =
                static_cast<float>(static_cast<double>(monitor[frame]) * fade);
            if (!std::isfinite(faded)) {
                throw std::domain_error{
                    "native presentation audition fade produced non-finite "
                    "output"};
            }
            const auto quantized = presentation::quantize_pcm24(faded);
            scratch_.pcm24[frame] = quantized.pcm24;
            stats_.audition_saturated_sample_count +=
                quantized.saturated ? UINT64_C(1) : UINT64_C(0);
        }
    }

    void write_published_block(const EngineSessionBlockView &block) {
        prepare_published_block(block);
        const auto audio_buses = block.audio_buses();

        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            const auto base = route * kNativePresentationArtifactsPerRoute;
            for (std::size_t role = 0; role < kNativePresentationArtifactsPerRoute;
                 ++role) {
                require_encoding_success(
                    encoders_[base + role].write_float32_interleaved(
                        audio_buses[buses_.routes[route].stems[role]].samples,
                        consumers_[base + role]),
                    "native stem WAVE payload emission failed");
            }
        }

        const auto raw_index = raw_master_index(plan_.routes.size());
        require_encoding_success(
            encoders_[raw_index].write_float32_interleaved(
                audio_buses[buses_.raw_master].samples, consumers_[raw_index]),
            "native raw-master WAVE payload emission failed");

        const auto audition_index = audition_master_index(plan_.routes.size());
        require_encoding_success(
            audition_.write_pcm24(scratch_.pcm24, consumers_[audition_index]),
            "native audition WAVE payload emission failed");
    }

    [[nodiscard]] bool consume_artifact_bytes(std::size_t artifact_index,
                                              std::uint64_t byte_offset,
                                              std::span<const std::byte> bytes) {
        if (artifact_index >= observations_.size() ||
            state_ != NativePresentationPublisherState::active) {
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
            throw NativePresentationSinkFailure{operation, *pending_sink_error_};
        }
        if (status.has_value()) {
            throw std::runtime_error{std::string(operation) + ": " + status->message};
        }
    }

    void require_complete_schedule() const {
        const auto expected_input_frames =
            processed_input_frame_count(plan_, input_frames_per_block_);
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
            audible_frame_ != expected_published_frames) {
            throw std::logic_error{
                "native presentation produced an incomplete frame interval"};
        }
    }

    void abort_once() noexcept {
        if (!transaction_begun_ || commit_attempted_ ||
            state_ == NativePresentationPublisherState::committed ||
            state_ == NativePresentationPublisherState::aborted) {
            return;
        }
        sink_.abort();
        state_ = NativePresentationPublisherState::aborted;
    }

    RenderSink &sink_;
    NativePresentationPublicationPlan plan_;
    std::size_t input_frames_per_block_ = 0U;
    NativePresentationBusBinding buses_;
    std::vector<PendingArtifact> audio_artifacts_;
    RenderControl control_;
    std::vector<WavEncoder> encoders_;
    artifacts::AuditionWaveEncoder audition_;
    presentation::MasteringSettings fade_settings_;
    PublicationScratch scratch_;
    std::vector<ArtifactObservation> observations_;
    std::vector<artifacts::WavChunkConsumer> consumers_;
    std::optional<execution::LinuxExecutionFactsObservation> execution_observation_;
    NativePresentationPublicationStats stats_;
    std::optional<std::vector<contract::ArtifactRecord>> sealed_artifacts_;
    std::optional<contract::ExecutionFacts> sealed_execution_;
    std::optional<RenderSinkError> pending_sink_error_;
    std::uint64_t audible_frame_ = 0;
    NativePresentationPublisherState state_ = NativePresentationPublisherState::active;
    bool transaction_begun_ = false;
    bool commit_attempted_ = false;
};

NativePresentationSinkFailure::NativePresentationSinkFailure(std::string_view operation,
                                                             RenderSinkError error)
    : std::runtime_error{sink_failure_message(operation, error)},
      sink_error_(std::move(error)) {}

const RenderSinkError &NativePresentationSinkFailure::sink_error() const noexcept {
    return sink_error_;
}

const NativePresentationPublicationStats &
SealedNativePresentationEvidence::stats() const noexcept {
    return stats_;
}

std::span<const contract::ArtifactRecord>
SealedNativePresentationEvidence::artifacts() const noexcept {
    return artifacts_;
}

const execution::ObservedExecutionFacts &
SealedNativePresentationEvidence::execution() const noexcept {
    return execution_;
}

NativePresentationPublisher::NativePresentationPublisher(
    RenderSink &sink, const EngineSessionDescriptor &session,
    NativePresentationPublicationPlan plan, RenderControl control)
    : implementation_(std::make_unique<Implementation>(sink, session, std::move(plan),
                                                       std::move(control))) {}

NativePresentationPublisher::~NativePresentationPublisher() = default;

void NativePresentationPublisher::process(const EngineSessionBlockView &block) {
    implementation_->process(block);
}

SealedNativePresentationEvidence NativePresentationPublisher::finish() {
    auto finished = implementation_->finish();
    return {
        std::move(finished.stats),
        std::move(finished.artifacts),
        std::move(finished.execution),
    };
}

void NativePresentationPublisher::commit(
    const SealedNativePresentationEvidence &evidence,
    const contract::RenderManifest &manifest,
    const contract::ProvenanceLedger &provenance,
    const contract::SourceMatrixContract &source_matrix) {
    implementation_->commit(evidence, manifest, provenance, source_matrix);
}

NativePresentationPublisherState NativePresentationPublisher::state() const noexcept {
    return implementation_->state();
}

} // namespace engine_sim_offline::render_detail
