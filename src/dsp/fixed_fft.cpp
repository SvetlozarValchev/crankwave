#include "dsp/fixed_fft.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::dsp {
namespace {

using Limits = FixedFftLimits;
using Complex = std::complex<double>;

constexpr double kFftPi = 3.141592653589793238462643383279502884;

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
        const double angle = -2.0 * kFftPi * static_cast<double>(index) /
                             static_cast<double>(Limits::transform_length);
        roots[index] = Complex{std::cos(angle), std::sin(angle)};
        if (!std::isfinite(roots[index].real()) ||
            !std::isfinite(roots[index].imag())) {
            throw std::domain_error{"FFT root construction was non-finite"};
        }
    }
    return roots;
}

void require_exact_transform_shape(std::span<const Complex> values) {
    if (values.size() != Limits::transform_length) {
        throw std::invalid_argument{"FFT requires exactly 65536 complex values"};
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

FixedFftPlan::FixedFftPlan()
    : bit_reversed_indices_(make_bit_reversal_table()),
      forward_roots_(make_forward_roots()) {}

void FixedFftPlan::forward(std::span<Complex> values) const {
    transform(values, false);
}

void FixedFftPlan::inverse(std::span<Complex> values) const {
    transform(values, true);
}

std::size_t FixedFftPlan::reversed_index(std::size_t index) const {
    if (index >= bit_reversed_indices_.size()) {
        throw std::out_of_range{"FFT bit-reversal index is out of range"};
    }
    return bit_reversed_indices_[index];
}

Complex FixedFftPlan::forward_root(std::size_t index) const {
    if (index >= forward_roots_.size()) {
        throw std::out_of_range{"FFT root index is out of range"};
    }
    return forward_roots_[index];
}

void FixedFftPlan::transform(std::span<Complex> values,
                             bool inverse_transform) const {
    require_exact_transform_shape(values);
    require_finite_transform(values, "FFT input was non-finite");

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

    require_finite_transform(values, "FFT output was non-finite");
}

std::shared_ptr<const FixedFftPlan> FixedConvolutionKernel::validated_plan(
    std::span<const double> coefficients,
    std::shared_ptr<const FixedFftPlan> shared_plan) {
    if (coefficients.size() != coefficient_count) {
        throw std::invalid_argument{
            "convolution kernel requires exactly 30071 coefficients"};
    }
    for (const double coefficient : coefficients) {
        if (!std::isfinite(coefficient)) {
            throw std::domain_error{
                "convolution kernel coefficient was non-finite"};
        }
    }
    if (shared_plan) {
        return shared_plan;
    }
    return std::make_shared<const FixedFftPlan>();
}

std::vector<Complex>
FixedConvolutionKernel::build_spectrum(std::span<const double> coefficients,
                                       const FixedFftPlan &plan) {
    std::vector<Complex> spectrum(Limits::transform_length, Complex{0.0, 0.0});
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        spectrum[index] = Complex{coefficients[index], 0.0};
    }
    plan.forward(spectrum);
    return spectrum;
}

FixedConvolutionKernel::FixedConvolutionKernel(
    std::span<const double> coefficients)
    : FixedConvolutionKernel(coefficients, nullptr) {}

FixedConvolutionKernel::FixedConvolutionKernel(
    std::span<const double> coefficients,
    std::shared_ptr<const FixedFftPlan> shared_plan)
    : plan_(validated_plan(coefficients, std::move(shared_plan))),
      spectrum_(build_spectrum(coefficients, *plan_)) {}

const std::shared_ptr<const FixedFftPlan> &
FixedConvolutionKernel::plan() const noexcept {
    return plan_;
}

std::span<const Complex, Limits::transform_length>
FixedConvolutionKernel::spectrum() const noexcept {
    return std::span<const Complex, Limits::transform_length>{spectrum_.data(),
                                                              Limits::transform_length};
}

namespace {

[[nodiscard]] std::vector<std::size_t> make_partitioned_bit_reversal_table() {
    using PartitionLimits = PartitionedConvolutionLimits;
    std::vector<std::size_t> table(PartitionLimits::transform_length);
    for (std::size_t index = 0; index < table.size(); ++index) {
        std::size_t remaining = index;
        std::size_t reversed = 0;
        for (std::size_t bit = 0; bit < PartitionLimits::transform_bit_count; ++bit) {
            reversed = (reversed << 1U) | (remaining & 1U);
            remaining >>= 1U;
        }
        table[index] = reversed;
    }
    return table;
}

[[nodiscard]] std::vector<Complex> make_partitioned_forward_roots() {
    using PartitionLimits = PartitionedConvolutionLimits;
    std::vector<Complex> roots(PartitionLimits::forward_root_count);
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const double angle =
            -2.0 * kFftPi * static_cast<double>(index) /
            static_cast<double>(PartitionLimits::transform_length);
        roots[index] = Complex{std::cos(angle), std::sin(angle)};
        if (!std::isfinite(roots[index].real()) ||
            !std::isfinite(roots[index].imag())) {
            throw std::domain_error{
                "partitioned FFT root construction was non-finite"};
        }
    }
    return roots;
}

} // namespace

PartitionedFftPlan::PartitionedFftPlan()
    : bit_reversed_indices_(make_partitioned_bit_reversal_table()),
      forward_roots_(make_partitioned_forward_roots()) {}

void PartitionedFftPlan::forward(std::span<Complex> values) const {
    transform(values, false);
}

void PartitionedFftPlan::inverse(std::span<Complex> values) const {
    transform(values, true);
}

void PartitionedFftPlan::transform(std::span<Complex> values,
                                   bool inverse_transform) const {
    using PartitionLimits = PartitionedConvolutionLimits;
    if (values.size() != PartitionLimits::transform_length) {
        throw std::invalid_argument{
            "partitioned FFT requires exactly 8192 complex values"};
    }
    require_finite_transform(values, "partitioned FFT input was non-finite");

    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t reversed = bit_reversed_indices_[index];
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }
    for (std::size_t width = 2; width <= PartitionLimits::transform_length;
         width <<= 1U) {
        const std::size_t half_width = width / 2;
        const std::size_t root_step = PartitionLimits::transform_length / width;
        for (std::size_t base = 0; base < PartitionLimits::transform_length;
             base += width) {
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
            1.0 / static_cast<double>(PartitionLimits::transform_length);
        for (auto &value : values) {
            value *= inverse_length;
        }
    }
    require_finite_transform(values, "partitioned FFT output was non-finite");
}

PartitionedConvolutionKernel::PartitionedConvolutionKernel(
    std::span<const double> coefficients)
    : plan_(std::make_shared<const PartitionedFftPlan>()),
      coefficient_count_(coefficients.size()),
      partition_count_((coefficients.size() +
                        PartitionedConvolutionLimits::partition_frame_count - 1) /
                       PartitionedConvolutionLimits::partition_frame_count),
      spectra_([&] {
          using PartitionLimits = PartitionedConvolutionLimits;
          if (coefficients.size() <= FixedConvolutionKernel::coefficient_count ||
              coefficients.size() > PartitionLimits::maximum_coefficient_count) {
              throw std::invalid_argument{
                  "partitioned convolution coefficient count is outside its "
                  "extended envelope"};
          }
          for (const double coefficient : coefficients) {
              if (!std::isfinite(coefficient)) {
                  throw std::domain_error{
                      "partitioned convolution coefficient was non-finite"};
              }
          }
          std::vector<Complex> spectra(
              partition_count_ * PartitionLimits::transform_length,
              Complex{0.0, 0.0});
          for (std::size_t partition = 0; partition < partition_count_; ++partition) {
              auto spectrum = std::span<Complex>{
                  spectra.data() + partition * PartitionLimits::transform_length,
                  PartitionLimits::transform_length};
              const std::size_t first =
                  partition * PartitionLimits::partition_frame_count;
              const std::size_t count = std::min(
                  PartitionLimits::partition_frame_count,
                  coefficients.size() - first);
              for (std::size_t index = 0; index < count; ++index) {
                  spectrum[index] = Complex{coefficients[first + index], 0.0};
              }
              plan_->forward(spectrum);
          }
          return spectra;
      }()) {}

const std::shared_ptr<const PartitionedFftPlan> &
PartitionedConvolutionKernel::plan() const noexcept {
    return plan_;
}

std::size_t PartitionedConvolutionKernel::coefficient_count() const noexcept {
    return coefficient_count_;
}

std::size_t PartitionedConvolutionKernel::partition_count() const noexcept {
    return partition_count_;
}

std::span<const Complex> PartitionedConvolutionKernel::partition_spectrum(
    std::size_t partition) const {
    if (partition >= partition_count_) {
        throw std::out_of_range{
            "partitioned convolution spectrum index is out of range"};
    }
    return {spectra_.data() +
                partition * PartitionedConvolutionLimits::transform_length,
            PartitionedConvolutionLimits::transform_length};
}

std::span<const Complex>
PartitionedConvolutionKernel::spectra() const noexcept {
    return spectra_;
}

} // namespace engine_sim_offline::dsp
