#include "dsp/causal_reconstruction_table.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine_sim_offline::dsp {
namespace {

constexpr double kReconstructionPi = 3.141592653589793238462643383279502884;
constexpr double kKaiserBeta = 12.0;
constexpr double kReconstructionBandwidth = 0.95;

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

double bessel_i0(double value) {
    require_finite(value, "reconstruction Bessel input was non-finite");
    const double quarter_square = 0.25 * value * value;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k <= 40; ++k) {
        term = term * (quarter_square / static_cast<double>(k * k));
        sum = sum + term;
    }
    require_finite(sum, "reconstruction Bessel result was non-finite");
    return sum;
}

double sinc(double value) {
    require_finite(value, "reconstruction sinc input was non-finite");
    if (std::abs(value) < 1e-15) {
        return 1.0;
    }
    const double radians = kReconstructionPi * value;
    const double result = std::sin(radians) / radians;
    require_finite(result, "reconstruction sinc result was non-finite");
    return result;
}

} // namespace

CausalReconstructionTable::CausalReconstructionTable()
    : coefficients_(coefficient_count) {
    const double inverse_i0_beta = 1.0 / bessel_i0(kKaiserBeta);
    require_finite(inverse_i0_beta,
                   "reconstruction inverse Bessel result was non-finite");

    std::vector<double> window(tap_count);
    for (std::size_t tap = 0; tap < tap_count; ++tap) {
        const double normalized =
            (static_cast<double>(tap) - static_cast<double>(half_width)) /
            static_cast<double>(half_width);
        const double radicand = std::max(0.0, 1.0 - normalized * normalized);
        window[tap] = bessel_i0(kKaiserBeta * std::sqrt(radicand)) * inverse_i0_beta;
        require_finite(window[tap],
                       "reconstruction window coefficient was non-finite");
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
            const double value = kReconstructionBandwidth *
                                 sinc(kReconstructionBandwidth * distance) *
                                 window[tap];
            require_finite(value,
                           "reconstruction table coefficient was non-finite");
            coefficients_[phase * tap_count + tap] = value;
            sum = sum + value;
        }
        require_finite(sum, "reconstruction table row sum was non-finite");
        if (sum == 0.0) {
            throw std::domain_error{"reconstruction table row sum was zero"};
        }
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            auto &value = coefficients_[phase * tap_count + tap];
            value = value / sum;
            require_finite(value,
                           "normalized reconstruction coefficient was non-finite");
        }
    }

    coefficients_[phase_interval_count * tap_count] = 0.0;
    for (std::size_t tap = 1; tap < tap_count; ++tap) {
        coefficients_[phase_interval_count * tap_count + tap] = coefficients_[tap - 1];
    }
}

std::span<const double, CausalReconstructionTable::tap_count>
CausalReconstructionTable::phase_row(std::size_t phase) const {
    if (coefficients_.size() != coefficient_count) {
        throw std::logic_error{
            "reconstruction table has no constructed coefficient storage"};
    }
    if (phase >= row_count) {
        throw std::out_of_range{"reconstruction phase index is out of range"};
    }
    return std::span<const double, tap_count>{coefficients_.data() + phase * tap_count,
                                              tap_count};
}

double CausalReconstructionTable::coefficient(std::size_t phase,
                                              std::size_t tap) const {
    if (tap >= tap_count) {
        throw std::out_of_range{"reconstruction tap index is out of range"};
    }
    return phase_row(phase)[tap];
}

} // namespace engine_sim_offline::dsp
