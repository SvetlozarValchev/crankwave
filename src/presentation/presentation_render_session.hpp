#pragma once

#include "artifacts/audition_wav_encoder.hpp"
#include "engine_sim_offline/render.hpp"
#include "execution/linux_execution_facts.hpp"
#include "presentation/mastering.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::acoustics {
struct ExhaustAcousticPressureBlock;
}

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kPresentationExhaustOutletCount = 2;
inline constexpr std::size_t kPresentationAudioArtifactCount = 4;
inline constexpr std::size_t kMaximumPresentationInputFramesPerBlock = 3'840;

enum class PresentationTailPolicy : std::uint8_t {
    unspecified,
    truncate_at_timeline_end,
};

// The timeline is expressed directly on the acoustic output clock. Input block
// partitioning has no bearing on the preparation crop or published frame horizon.
struct PresentationTimeline {
    std::uint64_t total_acoustic_frame_count = 0;
    std::uint64_t pre_audible_frame_count = 0;
    PresentationTailPolicy tail_policy = PresentationTailPolicy::unspecified;

    friend bool operator==(const PresentationTimeline &,
                           const PresentationTimeline &) = default;
};

struct PresentationOutletRenderPlan {
    contract::RouteId route_id;
    std::string route_semantic_id;
    PendingArtifact pressure_stem_artifact;
};

struct PresentationMasterRenderPlan {
    MasteringSettings mastering;
    artifacts::AuditionWaveMetadata audition_metadata;
    PendingArtifact raw_master_artifact;
    PendingArtifact audition_master_artifact;
};

// One physical presentation path: calibrated outlet pressures, their coherent raw
// sum, and one common-gain listening derivative. pa_per_full_scale is the sole
// conversion from the acoustic session's Pa domain into the WAVE full-scale domain.
struct PresentationRenderPlan {
    contract::OutputContract output_contract;
    PresentationTimeline timeline;
    std::array<PresentationOutletRenderPlan, kPresentationExhaustOutletCount> outlets;
    double pa_per_full_scale = 0.0;
    PresentationMasterRenderPlan master;
};

struct PresentationRenderStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t pre_audible_frame_count = 0;
    std::uint64_t published_frame_count = 0;

    friend bool operator==(const PresentationRenderStats &,
                           const PresentationRenderStats &) = default;
};

// Evidence that the session streamed and sealed all four role-bound artifacts and
// then finished its live execution observation. Publication authority remains with
// the admitted-job boundary that binds this evidence to a complete manifest.
class SealedPresentationEvidence final {
  public:
    SealedPresentationEvidence(const SealedPresentationEvidence &) = delete;
    SealedPresentationEvidence &operator=(const SealedPresentationEvidence &) = delete;
    SealedPresentationEvidence(SealedPresentationEvidence &&) noexcept = default;
    SealedPresentationEvidence &operator=(SealedPresentationEvidence &&) = delete;

    [[nodiscard]] const PresentationRenderStats &stats() const noexcept;
    [[nodiscard]] const std::array<contract::ArtifactRecord,
                                   kPresentationAudioArtifactCount> &
    artifacts() const noexcept;
    [[nodiscard]] const execution::ObservedExecutionFacts &execution() const noexcept;

  private:
    SealedPresentationEvidence(
        PresentationRenderStats stats,
        std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> artifacts,
        execution::ObservedExecutionFacts execution)
        : stats_(std::move(stats)), artifacts_(std::move(artifacts)),
          execution_(std::move(execution)) {}

    PresentationRenderStats stats_;
    std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> artifacts_;
    execution::ObservedExecutionFacts execution_;

    friend class PresentationRenderSession;
};

// Preserves exact transactional endpoint rejection through WAVE callback boundaries.
class PresentationSinkFailure final : public std::runtime_error {
  public:
    PresentationSinkFailure(std::string_view operation, RenderSinkError error);

    [[nodiscard]] const RenderSinkError &sink_error() const noexcept;

  private:
    RenderSinkError sink_error_;
};

enum class PresentationRenderSessionState : std::uint8_t {
    active,
    sealed,
    committed,
    aborted,
};

// One bounded serial transaction. Every input block is raw radiated pressure from the
// acoustic network at 192 kHz. The session performs no reconstruction, source
// conditioning, route-specific gain, convolution, wet selection, or fallback.
//
// Cancellation is observed before transaction begin, between complete input blocks,
// and once before finalization. Any failure after a successful sink begin aborts
// exactly once. A commit attempt is terminal because RenderSink owns cleanup on both
// commit success and commit failure.
class PresentationRenderSession final {
  public:
    PresentationRenderSession(RenderSink &sink, PresentationRenderPlan plan,
                              RenderControl control = {});
    ~PresentationRenderSession();

    PresentationRenderSession(const PresentationRenderSession &) = delete;
    PresentationRenderSession &operator=(const PresentationRenderSession &) = delete;
    PresentationRenderSession(PresentationRenderSession &&) = delete;
    PresentationRenderSession &operator=(PresentationRenderSession &&) = delete;

    void process(const acoustics::ExhaustAcousticPressureBlock &input);

    [[nodiscard]] SealedPresentationEvidence finish();

    void commit(const SealedPresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix);

    [[nodiscard]] PresentationRenderSessionState state() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::presentation
