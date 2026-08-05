#pragma once

#include "dsp/source_conditioning_primitives.hpp"

namespace engine_sim_offline::presentation {

// Stateful mono audition master. The input is the uncalibrated Float32 serial
// sum in engine-sim source units. Publication remains a separate, untouched path.
class MasterDynamics final {
  public:
    static constexpr double kSampleRateHz = dsp::kConditionedSourceRateHz;
    static constexpr float kInitialPeakSourceUnits = 30'000.0F;
    static constexpr float kInitialGainLinear = 1.0F;
    static constexpr float kTargetPeakSourceUnits = 22'000.0F;
    static constexpr float kMinimumGainLinear = 0.00001F;
    static constexpr float kMaximumGainLinear = 1.3F;
    static constexpr float kPcm16FullScale = 32'767.0F;

    explicit MasterDynamics(double volume_linear);

    // Returns one normalized Float32 sample in [-1, 1]. State remains
    // continuous across caller block boundaries.
    [[nodiscard]] float process(float source_unit_mix);

    [[nodiscard]] float volume_linear() const noexcept;
    [[nodiscard]] float peak_source_units() const noexcept;
    [[nodiscard]] float gain_linear() const noexcept;
    [[nodiscard]] float peak_retention_per_frame() const noexcept;
    [[nodiscard]] float gain_retention_per_frame() const noexcept;

  private:
    float volume_linear_ = 0.0F;
    float peak_retention_per_frame_ = 0.0F;
    float gain_retention_per_frame_ = 0.0F;
    float peak_source_units_ = kInitialPeakSourceUnits;
    float gain_linear_ = kInitialGainLinear;
};

} // namespace engine_sim_offline::presentation
