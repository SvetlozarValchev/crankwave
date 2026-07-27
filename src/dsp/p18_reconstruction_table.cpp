#include "dsp/p18_reconstruction_table.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine_sim_offline::dsp {
namespace {

constexpr double kP18ReconstructionPi = 3.141592653589793238462643383279502884;
constexpr double kP18KaiserBeta = 12.0;
constexpr double kP18ReconstructionBandwidth = 0.95;

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

double bessel_i0(double value) {
    require_finite(value, "P1.8 reconstruction Bessel input was non-finite");
    const double quarter_square = 0.25 * value * value;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k <= 40; ++k) {
        term = term * (quarter_square / static_cast<double>(k * k));
        sum = sum + term;
    }
    require_finite(sum, "P1.8 reconstruction Bessel result was non-finite");
    return sum;
}

double sinc(double value) {
    require_finite(value, "P1.8 reconstruction sinc input was non-finite");
    if (std::abs(value) < 1e-15) {
        return 1.0;
    }
    const double radians = kP18ReconstructionPi * value;
    const double result = std::sin(radians) / radians;
    require_finite(result, "P1.8 reconstruction sinc result was non-finite");
    return result;
}

} // namespace

P18ReconstructionTable::P18ReconstructionTable() : coefficients_(coefficient_count) {
    const double inverse_i0_beta = 1.0 / bessel_i0(kP18KaiserBeta);
    require_finite(inverse_i0_beta,
                   "P1.8 reconstruction inverse Bessel result was non-finite");

    std::vector<double> window(tap_count);
    for (std::size_t tap = 0; tap < tap_count; ++tap) {
        const double normalized =
            (static_cast<double>(tap) - static_cast<double>(half_width)) /
            static_cast<double>(half_width);
        const double radicand = std::max(0.0, 1.0 - normalized * normalized);
        window[tap] = bessel_i0(kP18KaiserBeta * std::sqrt(radicand)) * inverse_i0_beta;
        require_finite(window[tap],
                       "P1.8 reconstruction window coefficient was non-finite");
    }
    window.front() = 0.0;
    window.back() = 0.0;

    for (std::size_t phase = 0; phase < phase_interval_count; ++phase) {
        const double fraction =
            static_cast<double>(phase) / static_cast<double>(phase_interval_count);
        double sum = 0.0;
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            const double offset =
                static_cast<double>(tap) - static_cast<double>(half_width);
            const double distance = offset - fraction;
            const double value = kP18ReconstructionBandwidth *
                                 sinc(kP18ReconstructionBandwidth * distance) *
                                 window[tap];
            require_finite(value,
                           "P1.8 reconstruction table coefficient was non-finite");
            coefficients_[phase * tap_count + tap] = value;
            sum = sum + value;
        }
        require_finite(sum, "P1.8 reconstruction table row sum was non-finite");
        if (sum == 0.0) {
            throw std::domain_error{"P1.8 reconstruction table row sum was zero"};
        }
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            auto &value = coefficients_[phase * tap_count + tap];
            value = value / sum;
            require_finite(value,
                           "P1.8 normalized reconstruction coefficient was non-finite");
        }
    }

    coefficients_[phase_interval_count * tap_count] = 0.0;
    for (std::size_t tap = 1; tap < tap_count; ++tap) {
        coefficients_[phase_interval_count * tap_count + tap] = coefficients_[tap - 1];
    }
}

std::span<const double, P18ReconstructionTable::tap_count>
P18ReconstructionTable::phase_row(std::size_t phase) const {
    if (coefficients_.size() != coefficient_count) {
        throw std::logic_error{
            "P1.8 reconstruction table has no constructed coefficient storage"};
    }
    if (phase >= row_count) {
        throw std::out_of_range{"P1.8 reconstruction phase index is out of range"};
    }
    return std::span<const double, tap_count>{coefficients_.data() + phase * tap_count,
                                              tap_count};
}

double P18ReconstructionTable::coefficient(std::size_t phase, std::size_t tap) const {
    if (tap >= tap_count) {
        throw std::out_of_range{"P1.8 reconstruction tap index is out of range"};
    }
    return phase_row(phase)[tap];
}

} // namespace engine_sim_offline::dsp
