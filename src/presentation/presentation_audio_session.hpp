#pragma once

#include "dsp/fixed_fft.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/exhaust_source_stage.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr contract::RationalRateHz kPresentationAudioRateHz{192000, 1};
inline constexpr std::size_t kPresentationAudioStemsPerRoute = 3;

// The three route-local signals retained by the presentation audio core. The
// order is normative: storage and publication adapters may use the underlying
// value as the route-local stem offset.
enum class PresentationAudioStemRole : std::uint8_t {
    dry = 0,
    configured_ir = 1,
    selected = 2,
};

// All state required to render one route, without any artifact, filesystem, or
// delivery-format policy.
struct PresentationAudioRoutePlan {
    contract::RouteId route_id;
    RouteConditioningSeeds conditioning_seeds;
    std::shared_ptr<const dsp::FixedConvolutionKernel> configured_ir;
    double wet_mix_01 = 0.0;
};

// Processing-only presentation contract. `routes` owns source/stem order while
// `audition_route_ids` owns the exact serial Float32 reduction order. The latter
// must be a permutation of every configured route.
struct PresentationAudioPlan {
    RouteConditioningCalibration conditioning;
    std::vector<PresentationAudioRoutePlan> routes;
    double publication_calibration_gain_linear = 0.0;
    std::vector<contract::RouteId> audition_route_ids;
    float audition_monitoring_gain_linear = 0.0F;
};

// Non-owning view of one exact presentation method quantum. Every referenced
// sample belongs to the producing PresentationAudioSession and remains valid
// only until the next call to that session's process() begins or the session is
// destroyed.
//
// Route stems are route-major, then role-major. Both masters are Float32:
// raw_master() is the ordered selected-route sum and audition_master() is that
// raw sum multiplied by the configured monitoring gain. Clip fades, integer
// quantization, WAVE encoding, and publication are deliberately downstream.
class PresentationAudioBlockView final {
  public:
    [[nodiscard]] std::uint64_t first_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t first_source_frame_index() const noexcept;
    [[nodiscard]] std::size_t input_frame_count() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept;

    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId>
    audition_route_ids() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;

    [[nodiscard]] std::span<const float>
    route_stem(std::size_t route_index, PresentationAudioStemRole role) const;
    [[nodiscard]] std::span<const float> raw_master() const noexcept;
    [[nodiscard]] std::span<const float> audition_master() const noexcept;

  private:
    using StemBlock = std::span<const float>;

    PresentationAudioBlockView(SourceBlockExtent extent,
                               std::span<const contract::RouteId> route_ids,
                               std::span<const contract::RouteId> audition_route_ids,
                               std::span<const StemBlock> stems,
                               std::span<const float> raw_master,
                               std::span<const float> audition_master) noexcept;

    SourceBlockExtent extent_;
    std::span<const contract::RouteId> route_ids_;
    std::span<const contract::RouteId> audition_route_ids_;
    std::span<const StemBlock> stems_;
    std::span<const float> raw_master_;
    std::span<const float> audition_master_;

    friend class PresentationAudioSession;
};

// Stateful, processing-only presentation renderer. Construction owns and
// preallocates every route processor and scratch block. A successful process()
// performs no dynamic allocation and consumes exactly one 200-frame excitation
// block, returning a borrowed 3,840-frame Float32 result.
//
// Structural input rejection occurs before DSP mutation. A failure after the
// source stage advances is terminal because reconstruction, conditioning, and
// convolution histories can no longer be rolled back as a unit.
class PresentationAudioSession final {
  public:
    explicit PresentationAudioSession(PresentationAudioPlan plan);
    ~PresentationAudioSession();

    PresentationAudioSession(const PresentationAudioSession &) = delete;
    PresentationAudioSession &operator=(const PresentationAudioSession &) = delete;
    PresentationAudioSession(PresentationAudioSession &&) = delete;
    PresentationAudioSession &operator=(PresentationAudioSession &&) = delete;

    [[nodiscard]] PresentationAudioBlockView process(ExhaustExcitationBlockView input);

    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId>
    audition_route_ids() const noexcept;
    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept;
    [[nodiscard]] bool terminal_failed() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::presentation
