#pragma once

#include <array>

namespace crankwave::dsp {

// Deterministic primitives used by source conditioning and publication.
inline constexpr double kSourceConditioningPi = 3.14159265359;
inline constexpr double kConditionedSourceRateHz = 192000.0;
inline constexpr double kConditionedSourceTimeStepS = 1.0 / kConditionedSourceRateHz;
inline constexpr double kDcRemovalTimeConstantS = 1.0 / (20.0 * kSourceConditioningPi);
inline constexpr double kSourcePublicationCalibration = 0x1.0p-26;

struct FourthOrderLowPassCoefficients {
    double a0 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double a4 = 0.0;
    double numerator_scale = 0.0;

    friend bool operator==(const FourthOrderLowPassCoefficients &,
                           const FourthOrderLowPassCoefficients &) = default;
};

// One instance owns one continuous binary64, zero-state signal history.
class FourthOrderLowPass {
  public:
    FourthOrderLowPass(double cutoff_hz, double sample_rate_hz);

    [[nodiscard]] double process(double input);
    [[nodiscard]] const FourthOrderLowPassCoefficients &coefficients() const noexcept;

  private:
    FourthOrderLowPassCoefficients coefficients_;
    std::array<double, 4> prior_inputs_{};
    std::array<double, 4> prior_outputs_{};
};

// First-order low-frequency state subtraction used after source jitter.
// One instance owns one route's state, initially zero.
class DcRemoval {
  public:
    DcRemoval(double time_step_s, double time_constant_s);

    [[nodiscard]] double process(double input);
    [[nodiscard]] double alpha() const noexcept;

  private:
    double alpha_ = 0.0;
    double state_ = 0.0;
};

// Backward first difference. The previous input starts at
// zero and remains continuous across caller and method block boundaries.
class BackwardDerivative {
  public:
    explicit BackwardDerivative(double time_step_s);

    [[nodiscard]] double process(double input);

  private:
    double time_step_s_ = 0.0;
    double previous_ = 0.0;
};

// Clean up subnormals immediately after the conditioning mixture. Normal values
// and signed zero retain their bit pattern.
[[nodiscard]] double cleanup_conditioned_sample(double sample);

// Publish by rounding binary64 to Float32 first, multiplying that Float32 value
// (promoted back to binary64) by the explicit positive calibration gain, and
// rounding to Float32 again. This helper deliberately does not serialize a WAVE
// payload.
[[nodiscard]] float publish_calibrated_float32(double sample,
                                               double calibration_gain_linear);

} // namespace crankwave::dsp
