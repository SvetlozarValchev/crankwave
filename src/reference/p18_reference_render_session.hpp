#pragma once

#include "dsp/p18_fixed_fft.hpp"
#include "engine_sim_offline/render.hpp"
#include "execution/linux_execution_facts.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/p18_source_stage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace engine_sim_offline::reference {

inline constexpr std::size_t kP18PresentationAudioArtifactCount = 8;

// The array order is the renderer's fixed signal topology:
// route 0 dry/configured/selected, route 1 dry/configured/selected, raw master,
// audition master. Roles, paths, media, and diagnostic flags remain policy supplied
// rather than being inferred by the renderer.
struct P18PresentationSessionPlan {
    contract::OutputContract output_contract;
    std::array<PendingArtifact, kP18PresentationAudioArtifactCount> audio_artifacts;
};

struct P18PresentationRenderStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t warmup_block_count = 0;
    std::uint64_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t warmup_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;

    friend bool operator==(const P18PresentationRenderStats &,
                           const P18PresentationRenderStats &) = default;
};

// Authority that the session actually streamed and sealed all eight role-bound
// artifacts and then finished its live execution observation. Callers can inspect or
// move it, but only P18PresentationSession can construct it.
class P18SealedPresentationEvidence final {
  public:
    P18SealedPresentationEvidence(const P18SealedPresentationEvidence &) = delete;
    P18SealedPresentationEvidence &
    operator=(const P18SealedPresentationEvidence &) = delete;
    P18SealedPresentationEvidence(P18SealedPresentationEvidence &&) noexcept = default;
    P18SealedPresentationEvidence &operator=(P18SealedPresentationEvidence &&) = delete;

    [[nodiscard]] const P18PresentationRenderStats &stats() const noexcept;
    [[nodiscard]] const std::array<contract::ArtifactRecord,
                                   kP18PresentationAudioArtifactCount> &
    artifacts() const noexcept;
    [[nodiscard]] const execution::ObservedExecutionFacts &execution() const noexcept;

  private:
    P18SealedPresentationEvidence(
        P18PresentationRenderStats stats,
        std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount>
            artifacts,
        execution::ObservedExecutionFacts execution)
        : stats_(std::move(stats)), artifacts_(std::move(artifacts)),
          execution_(std::move(execution)) {}

    P18PresentationRenderStats stats_;
    std::array<contract::ArtifactRecord, kP18PresentationAudioArtifactCount> artifacts_;
    execution::ObservedExecutionFacts execution_;

    friend class P18PresentationSession;
};

enum class P18PresentationSessionState : std::uint8_t {
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
class P18PresentationSession final {
  public:
    P18PresentationSession(
        RenderSink &sink, P18PresentationSessionPlan plan,
        std::array<presentation::P18RouteConditioningSeeds,
                   presentation::kP18ExhaustRouteCount>
            route_seeds,
        std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir,
        RenderControl control = {});
    ~P18PresentationSession();

    P18PresentationSession(const P18PresentationSession &) = delete;
    P18PresentationSession &operator=(const P18PresentationSession &) = delete;
    P18PresentationSession(P18PresentationSession &&) = delete;
    P18PresentationSession &operator=(P18PresentationSession &&) = delete;

    void process(presentation::ExhaustExcitationBlockView input);

    [[nodiscard]] P18SealedPresentationEvidence finish();

    // Revalidates the complete manifest at the publication boundary and requires its
    // artifact and execution observations to be exactly this session's sealed
    // evidence before making the sink's one terminal commit attempt.
    void commit(const P18SealedPresentationEvidence &evidence,
                const contract::RenderManifest &manifest,
                const contract::ProvenanceLedger &provenance,
                const contract::SourceMatrixContract &source_matrix);

    [[nodiscard]] P18PresentationSessionState state() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::reference
