#include "presentation/p18_overlap_save_convolver.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::presentation {
namespace {

[[nodiscard]] std::shared_ptr<const dsp::P18FixedConvolutionKernel>
require_kernel(std::shared_ptr<const dsp::P18FixedConvolutionKernel> kernel) {
    if (!kernel) {
        throw std::invalid_argument{"P1.8 convolver requires an immutable kernel"};
    }
    return kernel;
}

} // namespace

P18CausalOverlapSaveConvolver::P18CausalOverlapSaveConvolver(
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> kernel)
    : kernel_(require_kernel(std::move(kernel))), history_(history_count, 0.0),
      next_history_scratch_(history_count, 0.0),
      output_scratch_(maximum_block_frame_count, 0.0),
      transform_scratch_(transform_length, std::complex<double>{0.0, 0.0}) {}

void P18CausalOverlapSaveConvolver::process(std::span<const double> input,
                                            std::span<double> output) {
    if (input.empty()) {
        throw std::invalid_argument{"P1.8 convolution block must not be empty"};
    }
    if (input.size() > maximum_block_frame_count) {
        throw std::length_error{"P1.8 convolution block exceeds 9600 frames"};
    }
    if (output.size() != input.size()) {
        throw std::invalid_argument{
            "P1.8 convolution input and output lengths must match"};
    }
    for (const double sample : input) {
        if (!std::isfinite(sample)) {
            throw std::domain_error{"P1.8 convolution input was non-finite"};
        }
    }

    std::fill(transform_scratch_.begin(), transform_scratch_.end(),
              std::complex<double>{0.0, 0.0});
    for (std::size_t index = 0; index < history_count; ++index) {
        transform_scratch_[index] = std::complex<double>{history_[index], 0.0};
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
        transform_scratch_[history_count + index] =
            std::complex<double>{input[index], 0.0};
    }

    const std::size_t retained_history_count = history_count - input.size();
    std::copy(history_.begin() + static_cast<std::ptrdiff_t>(input.size()),
              history_.end(), next_history_scratch_.begin());
    std::copy(input.begin(), input.end(),
              next_history_scratch_.begin() +
                  static_cast<std::ptrdiff_t>(retained_history_count));

    const auto &plan = *kernel_->plan();
    plan.forward(transform_scratch_);

    const auto kernel_spectrum = kernel_->spectrum();
    for (std::size_t index = 0; index < transform_length; ++index) {
        transform_scratch_[index] *= kernel_spectrum[index];
    }

    plan.inverse(transform_scratch_);
    for (std::size_t index = 0; index < input.size(); ++index) {
        const double sample = transform_scratch_[history_count + index].real();
        if (!std::isfinite(sample)) {
            throw std::domain_error{"P1.8 convolution output was non-finite"};
        }
        output_scratch_[index] = sample;
    }

    std::copy_n(output_scratch_.begin(), input.size(), output.begin());
    history_.swap(next_history_scratch_);
}

const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &
P18CausalOverlapSaveConvolver::kernel() const noexcept {
    return kernel_;
}

std::span<const double, P18CausalOverlapSaveConvolver::history_count>
P18CausalOverlapSaveConvolver::history() const noexcept {
    return std::span<const double, history_count>{history_.data(), history_count};
}

} // namespace engine_sim_offline::presentation
