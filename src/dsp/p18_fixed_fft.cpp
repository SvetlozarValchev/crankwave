#include "dsp/p18_fixed_fft.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::dsp {
namespace {

using Limits = P18FixedFftLimits;
using Complex = std::complex<double>;

constexpr double kP18FftPi = 3.141592653589793238462643383279502884;

[[nodiscard]] std::vector<std::size_t> make_bit_reversal_table() {
    std::vector<std::size_t> table(Limits::transform_length);
    for (std::size_t index = 0; index < Limits::transform_length; ++index) {
        std::size_t remaining = index;
        std::size_t reversed = 0;
        for (std::size_t bit = 0; bit < Limits::transform_bit_count; ++bit) {
            reversed = (reversed << 1U) | (remaining & 1U);
            remaining >>= 1U;
        }
        table[index] = reversed;
    }
    return table;
}

[[nodiscard]] std::vector<Complex> make_forward_roots() {
    std::vector<Complex> roots(Limits::forward_root_count);
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const double angle = -2.0 * kP18FftPi * static_cast<double>(index) /
                             static_cast<double>(Limits::transform_length);
        roots[index] = Complex{std::cos(angle), std::sin(angle)};
        if (!std::isfinite(roots[index].real()) ||
            !std::isfinite(roots[index].imag())) {
            throw std::domain_error{"P1.8 FFT root construction was non-finite"};
        }
    }
    return roots;
}

void require_exact_transform_shape(std::span<const Complex> values) {
    if (values.size() != Limits::transform_length) {
        throw std::invalid_argument{"P1.8 FFT requires exactly 65536 complex values"};
    }
}

void require_finite_transform(std::span<const Complex> values, const char *message) {
    for (const auto value : values) {
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            throw std::domain_error{message};
        }
    }
}

} // namespace

P18FixedFftPlan::P18FixedFftPlan()
    : bit_reversed_indices_(make_bit_reversal_table()),
      forward_roots_(make_forward_roots()) {}

void P18FixedFftPlan::forward(std::span<Complex> values) const {
    transform(values, false);
}

void P18FixedFftPlan::inverse(std::span<Complex> values) const {
    transform(values, true);
}

std::size_t P18FixedFftPlan::reversed_index(std::size_t index) const {
    if (index >= bit_reversed_indices_.size()) {
        throw std::out_of_range{"P1.8 FFT bit-reversal index is out of range"};
    }
    return bit_reversed_indices_[index];
}

Complex P18FixedFftPlan::forward_root(std::size_t index) const {
    if (index >= forward_roots_.size()) {
        throw std::out_of_range{"P1.8 FFT root index is out of range"};
    }
    return forward_roots_[index];
}

void P18FixedFftPlan::transform(std::span<Complex> values,
                                bool inverse_transform) const {
    require_exact_transform_shape(values);
    require_finite_transform(values, "P1.8 FFT input was non-finite");

    for (std::size_t index = 0; index < Limits::transform_length; ++index) {
        const std::size_t reversed = bit_reversed_indices_[index];
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }

    for (std::size_t width = 2; width <= Limits::transform_length; width <<= 1U) {
        const std::size_t half_width = width / 2;
        const std::size_t root_step = Limits::transform_length / width;
        for (std::size_t base = 0; base < Limits::transform_length; base += width) {
            for (std::size_t offset = 0; offset < half_width; ++offset) {
                const Complex forward_root = forward_roots_[offset * root_step];
                const Complex root =
                    inverse_transform ? std::conj(forward_root) : forward_root;
                const Complex odd = values[base + offset + half_width] * root;
                const Complex even = values[base + offset];
                values[base + offset] = even + odd;
                values[base + offset + half_width] = even - odd;
            }
        }
    }

    if (inverse_transform) {
        const double inverse_length =
            1.0 / static_cast<double>(Limits::transform_length);
        for (std::size_t index = 0; index < Limits::transform_length; ++index) {
            values[index] *= inverse_length;
        }
    }

    require_finite_transform(values, "P1.8 FFT output was non-finite");
}

std::shared_ptr<const P18FixedFftPlan> P18FixedConvolutionKernel::validated_plan(
    std::span<const double> coefficients,
    std::shared_ptr<const P18FixedFftPlan> shared_plan) {
    if (coefficients.size() != coefficient_count) {
        throw std::invalid_argument{
            "P1.8 convolution kernel requires exactly 30071 coefficients"};
    }
    for (const double coefficient : coefficients) {
        if (!std::isfinite(coefficient)) {
            throw std::domain_error{
                "P1.8 convolution kernel coefficient was non-finite"};
        }
    }
    if (shared_plan) {
        return shared_plan;
    }
    return std::make_shared<const P18FixedFftPlan>();
}

std::vector<Complex>
P18FixedConvolutionKernel::build_spectrum(std::span<const double> coefficients,
                                          const P18FixedFftPlan &plan) {
    std::vector<Complex> spectrum(Limits::transform_length, Complex{0.0, 0.0});
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        spectrum[index] = Complex{coefficients[index], 0.0};
    }
    plan.forward(spectrum);
    return spectrum;
}

P18FixedConvolutionKernel::P18FixedConvolutionKernel(
    std::span<const double> coefficients)
    : P18FixedConvolutionKernel(coefficients, nullptr) {}

P18FixedConvolutionKernel::P18FixedConvolutionKernel(
    std::span<const double> coefficients,
    std::shared_ptr<const P18FixedFftPlan> shared_plan)
    : plan_(validated_plan(coefficients, std::move(shared_plan))),
      spectrum_(build_spectrum(coefficients, *plan_)) {}

const std::shared_ptr<const P18FixedFftPlan> &
P18FixedConvolutionKernel::plan() const noexcept {
    return plan_;
}

std::span<const Complex, Limits::transform_length>
P18FixedConvolutionKernel::spectrum() const noexcept {
    return std::span<const Complex, Limits::transform_length>{spectrum_.data(),
                                                              Limits::transform_length};
}

} // namespace engine_sim_offline::dsp
