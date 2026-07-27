#pragma once

#include <array>

namespace engine_sim_offline::dsp {

// These primitives reproduce narrowly frozen P1.8 presentation behavior. Their
// names intentionally prevent the behavioral-reference arithmetic from being
// mistaken for a generally preferred production DSP implementation.
inline constexpr double kP18FilterPi = 3.14159265359;
inline constexpr double kP18SourceRateHz = 192000.0;
inline constexpr double kP18SourceTimeStepS = 1.0 / kP18SourceRateHz;
inline constexpr double kP18DcTimeConstantS = 1.0 / (20.0 * kP18FilterPi);
inline constexpr double kP18SourceCalibration = 0x1.0p-26;

struct P18FourthOrderLowPassCoefficients {
    double a0 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double a4 = 0.0;
    double numerator_scale = 0.0;

    friend bool operator==(const P18FourthOrderLowPassCoefficients &,
                           const P18FourthOrderLowPassCoefficients &) = default;
};

// Exact binary64 statement order and zero-state recurrence specified by the
// P1.8 renderer record. One instance owns one continuous signal history.
class P18FourthOrderLowPass {
  public:
    P18FourthOrderLowPass(double cutoff_hz, double sample_rate_hz);

    [[nodiscard]] double process(double input);
    [[nodiscard]] const P18FourthOrderLowPassCoefficients &
    coefficients() const noexcept;

  private:
    P18FourthOrderLowPassCoefficients coefficients_;
    std::array<double, 4> prior_inputs_{};
    std::array<double, 4> prior_outputs_{};
};

// Exact first-order low-frequency state subtraction used after P1.8 jitter.
// One instance owns one route's state, initially zero.
class P18DcRemoval {
  public:
    P18DcRemoval(double time_step_s, double time_constant_s);

    [[nodiscard]] double process(double input);
    [[nodiscard]] double alpha() const noexcept;

  private:
    double alpha_ = 0.0;
    double state_ = 0.0;
};

// Exact backward first difference used by P1.8. The previous input starts at
// zero and remains continuous across caller and method block boundaries.
class P18BackwardDerivative {
  public:
    explicit P18BackwardDerivative(double time_step_s);

    [[nodiscard]] double process(double input);

  private:
    double time_step_s_ = 0.0;
    double previous_ = 0.0;
};

// P1.8 owns a single explicit subnormal cleanup immediately after its
// conditioning mixture. Normal values and signed zero retain their bit pattern.
[[nodiscard]] double p18_cleanup_conditioned_sample(double sample);

// P1.8 publishes by rounding binary64 to Float32 first, multiplying that
// Float32 value (promoted back to binary64) by 2^-26, and rounding to Float32
// again. This helper deliberately does not serialize a WAVE payload.
[[nodiscard]] float p18_publish_calibrated_float32(double sample);

} // namespace engine_sim_offline::dsp
