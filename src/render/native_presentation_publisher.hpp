#pragma once

#include "artifacts/audition_wav_encoder.hpp"
#include "crankwave/publication.hpp"
#include "crankwave/session.hpp"
#include "execution/linux_execution_facts.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crankwave::render_detail {

inline constexpr std::size_t kNativePresentationArtifactsPerRoute = 3;
inline constexpr std::size_t kNativePresentationMasterArtifactCount = 2;
inline constexpr std::size_t kNativePresentationDiagnosticArtifactCount = 1;

[[nodiscard]] constexpr bool
native_presentation_artifact_count_representable(std::size_t route_count) noexcept {
    return route_count <= (std::numeric_limits<std::size_t>::max() -
                           kNativePresentationMasterArtifactCount -
                           kNativePresentationDiagnosticArtifactCount) /
                              kNativePresentationArtifactsPerRoute;
}

[[nodiscard]] constexpr std::size_t
native_presentation_audio_artifact_count(std::size_t route_count) noexcept {
    return native_presentation_artifact_count_representable(route_count)
               ? route_count * kNativePresentationArtifactsPerRoute +
                     kNativePresentationMasterArtifactCount
               : 0U;
}

[[nodiscard]] constexpr std::size_t
native_presentation_artifact_count(std::size_t route_count) noexcept {
    const auto audio_count = native_presentation_audio_artifact_count(route_count);
    return audio_count == 0U ? 0U
                             : audio_count + kNativePresentationDiagnosticArtifactCount;
}

enum class NativePresentationTailPolicy : std::uint8_t {
    unspecified,
    truncate_at_timeline_end,
};

struct NativePresentationTimeline {
    std::uint64_t total_block_count = 0;
    std::uint64_t pre_audible_block_count = 0;
    NativePresentationTailPolicy tail_policy =
        NativePresentationTailPolicy::unspecified;

    friend bool operator==(const NativePresentationTimeline &,
                           const NativePresentationTimeline &) = default;
};

struct NativePresentationRouteArtifacts {
    PendingArtifact dry;
    PendingArtifact configured_transfer;
    PendingArtifact selected;
};

// Publication binding for one already-produced active exhaust-source route. This owns
// no source conditioning, random seed, convolution kernel, or wet-selection setting.
struct NativePresentationRoutePublicationPlan {
    contract::RouteId route_id;
    std::string route_semantic_id;
    NativePresentationRouteArtifacts artifacts;
};

// Clip-envelope authority only. Stateful master dynamics and listening volume
// belong to the audio plan and are already reflected in audition_master().
struct NativePresentationFadeSettings {
    std::uint64_t audible_frame_count = 0;
    std::uint64_t fade_in_frame_count = 0;
    std::uint64_t fade_out_frame_count = 0;

    friend bool operator==(const NativePresentationFadeSettings &,
                           const NativePresentationFadeSettings &) = default;
};

struct NativePresentationAuditionPublicationPlan {
    // Exact order expected from PresentationAudioBlockView::audition_route_ids().
    std::vector<contract::RouteId> selected_route_ids;
    NativePresentationFadeSettings fade;
    artifacts::AuditionWaveMetadata metadata;
    PendingArtifact raw_master_artifact;
    PendingArtifact audition_master_artifact;
};

// Native delivery contract only. It binds an already-rendered stream to
// artifacts, timeline gating, clip fades, encoding, evidence, and a RenderSink
// transaction.
struct NativePresentationPublicationPlan {
    contract::OutputContract output_contract;
    NativePresentationTimeline timeline;
    contract::Sha256Digest simulation_request_identity_v7_sha256;
    presentation::PresentationMethodIdentities methods;
    PendingArtifact telemetry_artifact;
    std::vector<NativePresentationRoutePublicationPlan> routes;
    NativePresentationAuditionPublicationPlan audition;
};

struct NativePresentationPublicationStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t pre_audible_block_count = 0;
    std::uint64_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t pre_audible_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;
    std::uint64_t audition_saturated_sample_count = 0;

    friend bool operator==(const NativePresentationPublicationStats &,
                           const NativePresentationPublicationStats &) = default;
};

// Sealed native publication evidence retained for render-layer manifest
// completion. It is evidence, not independent authority to publish.
class SealedNativePresentationEvidence final {
  public:
    SealedNativePresentationEvidence(const SealedNativePresentationEvidence &) = delete;
    SealedNativePresentationEvidence &
    operator=(const SealedNativePresentationEvidence &) = delete;
    SealedNativePresentationEvidence(SealedNativePresentationEvidence &&) noexcept =
        default;
    SealedNativePresentationEvidence &
    operator=(SealedNativePresentationEvidence &&) = delete;

    [[nodiscard]] const NativePresentationPublicationStats &stats() const noexcept;
    [[nodiscard]] std::span<const contract::ArtifactRecord> artifacts() const noexcept;
    [[nodiscard]] const execution::ObservedExecutionFacts &execution() const noexcept;

  private:
    SealedNativePresentationEvidence(NativePresentationPublicationStats stats,
                                     std::vector<contract::ArtifactRecord> artifacts,
                                     execution::ObservedExecutionFacts execution)
        : stats_(std::move(stats)), artifacts_(std::move(artifacts)),
          execution_(std::move(execution)) {}

    NativePresentationPublicationStats stats_;
    std::vector<contract::ArtifactRecord> artifacts_;
    execution::ObservedExecutionFacts execution_;

    friend class NativePresentationPublisher;
};

// Retains the exact RenderSink failure across encoder callback boundaries.
class NativePresentationSinkFailure final : public std::runtime_error {
  public:
    NativePresentationSinkFailure(std::string_view operation, RenderSinkError error);

    [[nodiscard]] const RenderSinkError &sink_error() const noexcept;

  private:
    RenderSinkError sink_error_;
};

// Distinguishes a control-requested stop from a publication or sink failure.
// bake() may translate only this type into the retryable cancellation outcome;
// a concurrent real publication failure must retain its original classification.
class NativePresentationCancellation final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

enum class NativePresentationPublisherState : std::uint8_t {
    active,
    sealed,
    committed,
    aborted,
};

// One bounded native publication transaction. Each process() call accepts one
// borrowed, complete public EngineSession quantum. Bus bindings are resolved and
// validated once from the session descriptor during construction. Pre-audible
// quanta are discarded; audible quanta are synchronously serialized before
// process() returns, so no borrowed sample storage escapes the call.
class NativePresentationPublisher final {
  public:
    NativePresentationPublisher(RenderSink &sink,
                                const EngineSessionDescriptor &session,
                                NativePresentationPublicationPlan plan,
                                RenderControl control = {});
    ~NativePresentationPublisher();

    NativePresentationPublisher(const NativePresentationPublisher &) = delete;
    NativePresentationPublisher &
    operator=(const NativePresentationPublisher &) = delete;
    NativePresentationPublisher(NativePresentationPublisher &&) = delete;
    NativePresentationPublisher &operator=(NativePresentationPublisher &&) = delete;

    void process(const EngineSessionBlockView &block);

    [[nodiscard]] SealedNativePresentationEvidence finish();

    void commit(const SealedNativePresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix);

    [[nodiscard]] NativePresentationPublisherState state() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace crankwave::render_detail
