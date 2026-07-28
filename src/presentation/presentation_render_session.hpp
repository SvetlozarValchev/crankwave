#pragma once

#include "dsp/fixed_fft.hpp"
#include "engine_sim_offline/render.hpp"
#include "execution/linux_execution_facts.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/exhaust_source_stage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kPresentationAudioArtifactCount = 8;

// The array order is the renderer's fixed signal topology:
// route 0 dry/configured/selected, route 1 dry/configured/selected, raw master,
// audition master. Roles, paths, media, and diagnostic flags remain policy supplied
// rather than being inferred by the renderer.
struct PresentationRenderPlan {
    contract::OutputContract output_contract;
    std::array<PendingArtifact, kPresentationAudioArtifactCount> audio_artifacts;
};

struct PresentationRenderStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t warmup_block_count = 0;
    std::uint64_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t warmup_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;

    friend bool operator==(const PresentationRenderStats &,
                           const PresentationRenderStats &) = default;
};

// Authority that the session actually streamed and sealed all eight role-bound
// artifacts and then finished its live execution observation. Callers can inspect or
// move it, but only PresentationRenderSession can construct it.
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
    committed,
    aborted,
};

// One bounded, serial, fixture-free presentation transaction. The caller pushes
// complete ExhaustExcitationBlockView values; this type has no fixture path, decoder,
// audit, or comparator dependency. It owns all stateful DSP, WAVE encoders, bounded
// scratch, output hashing, sink lifecycle, and the live execution observation.
//
// Cancellation is observed before transaction begin, between complete input blocks,
// and once before finalization. Any failure after a successful sink begin aborts
// exactly once. A commit attempt is terminal because RenderSink owns commit-failure
// cleanup.
class PresentationRenderSession final {
  public:
    PresentationRenderSession(
        RenderSink &sink, PresentationRenderPlan plan,
        std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> route_seeds,
        std::shared_ptr<const dsp::FixedConvolutionKernel> configured_ir,
        RenderControl control = {});
    ~PresentationRenderSession();

    PresentationRenderSession(const PresentationRenderSession &) = delete;
    PresentationRenderSession &operator=(const PresentationRenderSession &) = delete;
    PresentationRenderSession(PresentationRenderSession &&) = delete;
    PresentationRenderSession &operator=(PresentationRenderSession &&) = delete;

    void process(ExhaustExcitationBlockView input);

    [[nodiscard]] SealedPresentationEvidence finish();

    // Revalidates the complete manifest at the publication boundary and requires its
    // artifact and execution observations to be exactly this session's sealed
    // evidence before making the sink's one terminal commit attempt.
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
