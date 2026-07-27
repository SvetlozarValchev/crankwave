#include "dsp/p18_primitives.hpp"

#include <cmath>
#include <stdexcept>

namespace engine_sim_offline::dsp {
namespace {

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

} // namespace

P18FourthOrderLowPass::P18FourthOrderLowPass(double cutoff_hz, double sample_rate_hz) {
    if (!std::isfinite(cutoff_hz) || !std::isfinite(sample_rate_hz) ||
        cutoff_hz <= 0.0 || sample_rate_hz <= 0.0 ||
        cutoff_hz >= sample_rate_hz / 2.0) {
        throw std::invalid_argument{
            "P1.8 low-pass requires a finite cutoff strictly inside Nyquist"};
    }

    const double f = std::tan(kP18FilterPi * cutoff_hz / sample_rate_hz);
    const double f2 = f * f;
    const double f3 = f2 * f;
    const double f4 = f2 * f2;
    const double m = -2.0 * std::cos(5.0 * kP18FilterPi / 8.0);
    const double n = -2.0 * std::cos(7.0 * kP18FilterPi / 8.0);

    coefficients_.a0 = 1.0 + (m + n) * f + (2.0 + n * m) * f2 + (m + n) * f3 + f4;
    coefficients_.a1 =
        (-4.0 - 2.0 * (n + m) * f + 2.0 * (m + n) * f3 + 4.0 * f4) / coefficients_.a0;
    coefficients_.a2 = (6.0 - 2.0 * (2.0 + m * n) * f2 + 6.0 * f4) / coefficients_.a0;
    coefficients_.a3 =
        (-4.0 + 2.0 * (m + n) * f - 2.0 * (m + n) * f3 + 4.0 * f4) / coefficients_.a0;
    coefficients_.a4 =
        (1.0 - (n + m) * f + (2.0 + m * n) * f2 - (m + n) * f3 + f4) / coefficients_.a0;
    coefficients_.numerator_scale = f4 / coefficients_.a0;

    require_finite(coefficients_.a0,
                   "P1.8 low-pass coefficient construction was non-finite");
    require_finite(coefficients_.a1,
                   "P1.8 low-pass coefficient construction was non-finite");
    require_finite(coefficients_.a2,
                   "P1.8 low-pass coefficient construction was non-finite");
    require_finite(coefficients_.a3,
                   "P1.8 low-pass coefficient construction was non-finite");
    require_finite(coefficients_.a4,
                   "P1.8 low-pass coefficient construction was non-finite");
    require_finite(coefficients_.numerator_scale,
                   "P1.8 low-pass coefficient construction was non-finite");
}

double P18FourthOrderLowPass::process(double input) {
    require_finite(input, "P1.8 low-pass input was non-finite");

    const double numerator = coefficients_.numerator_scale *
                             (input + 4.0 * prior_inputs_[0] + 6.0 * prior_inputs_[1] +
                              4.0 * prior_inputs_[2] + prior_inputs_[3]);
    const double feedback =
        -coefficients_.a1 * prior_outputs_[0] - coefficients_.a2 * prior_outputs_[1] -
        coefficients_.a3 * prior_outputs_[2] - coefficients_.a4 * prior_outputs_[3];
    const double output = numerator + feedback;
    require_finite(output, "P1.8 low-pass output was non-finite");

    prior_inputs_[3] = prior_inputs_[2];
    prior_inputs_[2] = prior_inputs_[1];
    prior_inputs_[1] = prior_inputs_[0];
    prior_inputs_[0] = input;
    prior_outputs_[3] = prior_outputs_[2];
    prior_outputs_[2] = prior_outputs_[1];
    prior_outputs_[1] = prior_outputs_[0];
    prior_outputs_[0] = output;
    return output;
}

const P18FourthOrderLowPassCoefficients &
P18FourthOrderLowPass::coefficients() const noexcept {
    return coefficients_;
}

P18DcRemoval::P18DcRemoval(double time_step_s, double time_constant_s) {
    if (!std::isfinite(time_step_s) || !std::isfinite(time_constant_s) ||
        time_step_s <= 0.0 || time_constant_s <= 0.0) {
        throw std::invalid_argument{
            "P1.8 DC removal requires positive finite time parameters"};
    }
    alpha_ = time_step_s / (time_constant_s + time_step_s);
    require_finite(alpha_, "P1.8 DC-removal coefficient was non-finite");
}

double P18DcRemoval::process(double input) {
    require_finite(input, "P1.8 DC-removal input was non-finite");
    state_ = alpha_ * input + (1.0 - alpha_) * state_;
    const double output = input - state_;
    require_finite(state_, "P1.8 DC-removal state was non-finite");
    require_finite(output, "P1.8 DC-removal output was non-finite");
    return output;
}

double P18DcRemoval::alpha() const noexcept {
    return alpha_;
}

P18BackwardDerivative::P18BackwardDerivative(double time_step_s) {
    if (!std::isfinite(time_step_s) || time_step_s <= 0.0) {
        throw std::invalid_argument{
            "P1.8 backward derivative requires a positive finite time step"};
    }
    time_step_s_ = time_step_s;
}

double P18BackwardDerivative::process(double input) {
    require_finite(input, "P1.8 backward-derivative input was non-finite");
    const double output = (input - previous_) / time_step_s_;
    previous_ = input;
    require_finite(output, "P1.8 backward-derivative output was non-finite");
    return output;
}

double p18_cleanup_conditioned_sample(double sample) {
    require_finite(sample, "P1.8 conditioned sample was non-finite");
    if (std::fpclassify(sample) == FP_SUBNORMAL) {
        return 0.0;
    }
    return sample;
}

float p18_publish_calibrated_float32(double sample) {
    require_finite(sample, "P1.8 publication input was non-finite");
    const float published = static_cast<float>(sample);
    if (!std::isfinite(published)) {
        throw std::domain_error{
            "P1.8 publication overflowed during binary64-to-Float32 conversion"};
    }
    const double calibrated = static_cast<double>(published) * kP18SourceCalibration;
    const float output = static_cast<float>(calibrated);
    if (!std::isfinite(output)) {
        throw std::domain_error{"P1.8 calibrated publication was non-finite"};
    }
    return output;
}

} // namespace engine_sim_offline::dsp
