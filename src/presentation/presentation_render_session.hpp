#pragma once

#include "artifacts/audition_wav_encoder.hpp"
#include "dsp/fixed_fft.hpp"
#include "engine_sim_offline/render.hpp"
#include "execution/linux_execution_facts.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/exhaust_source_stage.hpp"
#include "presentation/mastering.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kPresentationAudioArtifactCount = 8;

enum class PresentationTailPolicy : std::uint8_t {
    unspecified,
    truncate_at_timeline_end,
};

struct PresentationTimeline {
    std::uint64_t total_block_count = 0;
    std::uint64_t pre_audible_block_count = 0;
    PresentationTailPolicy tail_policy = PresentationTailPolicy::unspecified;

    friend bool operator==(const PresentationTimeline &,
                           const PresentationTimeline &) = default;
};

struct PresentationRouteArtifacts {
    PendingArtifact dry;
    PendingArtifact configured_ir;
    PendingArtifact selected;
};

struct PresentationRouteRenderPlan {
    contract::RouteId route_id;
    std::string route_semantic_id;
    RouteConditioningSeeds conditioning_seeds;
    std::shared_ptr<const dsp::FixedConvolutionKernel> configured_ir;
    double wet_mix_01 = 0.0;
    PresentationRouteArtifacts artifacts;
};

struct PresentationAuditionRenderPlan {
    std::array<contract::RouteId, kExhaustExcitationRouteCount> selected_route_ids;
    MasteringSettings mastering;
    artifacts::AuditionWaveMetadata metadata;
    PendingArtifact raw_master_artifact;
    PendingArtifact audition_master_artifact;
};

// The renderer's fixed signal topology is expressed through named, route-owned
// artifacts rather than a caller-defined positional array. The output contract must
// bind every supplied role to the same route or output bus before a sink is touched.
struct PresentationRenderPlan {
    contract::OutputContract output_contract;
    PresentationTimeline timeline;
    PresentationMethodIdentities methods;
    RouteConditioningCalibration conditioning;
    std::array<PresentationRouteRenderPlan, kExhaustExcitationRouteCount> routes;
    double publication_calibration_gain_linear = 0.0;
    PresentationAuditionRenderPlan audition;
};

struct PresentationRenderStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t pre_audible_block_count = 0;
    std::uint64_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t pre_audible_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;

    friend bool operator==(const PresentationRenderStats &,
                           const PresentationRenderStats &) = default;
};

// Evidence that the session actually streamed and sealed all eight role-bound
// artifacts and then finished its live execution observation. This is deliberately
// not publication authority: callers can inspect or move it, but only a future
// admitted-job boundary may bind it to a manifest and commit it.
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

enum class PresentationRenderSessionState : std::uint8_t {
    active,
    sealed,
    aborted,
};

// One bounded, serial, fixture-free presentation transaction. The caller pushes
// complete ExhaustExcitationBlockView values; this type has no fixture path, decoder,
// audit, or comparator dependency. It owns all stateful DSP, WAVE encoders, bounded
// scratch, output hashing, sink lifecycle, and the live execution observation.
//
// Cancellation is observed before transaction begin, between complete input blocks,
// and once before finalization. Any failure after a successful sink begin aborts
// exactly once. This low-level session deliberately cannot publish: public render()
// remains fail-closed until one compiler can derive both the executable plan and its
// manifest basis from the same admitted request.
class PresentationRenderSession final {
  public:
    PresentationRenderSession(RenderSink &sink, PresentationRenderPlan plan,
                              RenderControl control = {});
    ~PresentationRenderSession();

    PresentationRenderSession(const PresentationRenderSession &) = delete;
    PresentationRenderSession &operator=(const PresentationRenderSession &) = delete;
    PresentationRenderSession(PresentationRenderSession &&) = delete;
    PresentationRenderSession &operator=(PresentationRenderSession &&) = delete;

    void process(ExhaustExcitationBlockView input);

    [[nodiscard]] SealedPresentationEvidence finish();

    [[nodiscard]] PresentationRenderSessionState state() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::presentation
