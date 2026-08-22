#include "presentation/overlap_save_convolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace crankwave::presentation {
namespace {

[[nodiscard]] std::shared_ptr<const dsp::FixedConvolutionKernel>
require_kernel(std::shared_ptr<const dsp::FixedConvolutionKernel> kernel) {
    if (!kernel) {
        throw std::invalid_argument{"convolver requires an immutable kernel"};
    }
    return kernel;
}

} // namespace

CausalOverlapSaveConvolver::CausalOverlapSaveConvolver(
    std::shared_ptr<const dsp::FixedConvolutionKernel> kernel)
    : kernel_(require_kernel(std::move(kernel))), history_(history_count, 0.0),
      next_history_scratch_(history_count, 0.0),
      output_scratch_(maximum_block_frame_count, 0.0),
      transform_scratch_(transform_length, std::complex<double>{0.0, 0.0}) {}

void CausalOverlapSaveConvolver::process(std::span<const double> input,
                                         std::span<double> output) {
    if (input.empty()) {
        throw std::invalid_argument{"convolution block must not be empty"};
    }
    if (input.size() > maximum_block_frame_count) {
        throw std::length_error{"convolution block exceeds 9600 frames"};
    }
    if (output.size() != input.size()) {
        throw std::invalid_argument{"convolution input and output lengths must match"};
    }
    for (const double sample : input) {
        if (!std::isfinite(sample)) {
            throw std::domain_error{"convolution input was non-finite"};
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
            throw std::domain_error{"convolution output was non-finite"};
        }
        output_scratch_[index] = sample;
    }

    std::copy_n(output_scratch_.begin(), input.size(), output.begin());
    history_.swap(next_history_scratch_);
}

const std::shared_ptr<const dsp::FixedConvolutionKernel> &
CausalOverlapSaveConvolver::kernel() const noexcept {
    return kernel_;
}

std::span<const double, CausalOverlapSaveConvolver::history_count>
CausalOverlapSaveConvolver::history() const noexcept {
    return std::span<const double, history_count>{history_.data(), history_count};
}

CausalPartitionedConvolver::CausalPartitionedConvolver(
    std::shared_ptr<const dsp::PartitionedConvolutionKernel> kernel)
    : kernel_(std::move(kernel)),
      input_spectra_((kernel_ ? kernel_->partition_count() : 0U) * transform_length,
                     std::complex<double>{0.0, 0.0}),
      input_scratch_(transform_length, std::complex<double>{0.0, 0.0}),
      output_spectrum_scratch_(transform_length,
                               std::complex<double>{0.0, 0.0}),
      overlap_(block_frame_count - 1U, 0.0),
      next_overlap_scratch_(block_frame_count - 1U, 0.0),
      output_scratch_(block_frame_count, 0.0) {
    if (!kernel_) {
        throw std::invalid_argument{
            "partitioned convolver requires an immutable kernel"};
    }
}

void CausalPartitionedConvolver::process(std::span<const double> input,
                                         std::span<double> output) {
    if (input.size() != block_frame_count) {
        throw std::invalid_argument{
            "partitioned convolution requires exactly 3840 input frames"};
    }
    if (output.size() != input.size()) {
        throw std::invalid_argument{
            "partitioned convolution input and output lengths must match"};
    }
    for (const double sample : input) {
        if (!std::isfinite(sample)) {
            throw std::domain_error{
                "partitioned convolution input was non-finite"};
        }
    }

    std::fill(input_scratch_.begin(), input_scratch_.end(),
              std::complex<double>{0.0, 0.0});
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        input_scratch_[frame] = std::complex<double>{input[frame], 0.0};
    }
    kernel_->plan()->forward(input_scratch_);

    std::fill(output_spectrum_scratch_.begin(),
              output_spectrum_scratch_.end(),
              std::complex<double>{0.0, 0.0});
    const std::size_t partition_count = kernel_->partition_count();
    const std::size_t available_partition_count = static_cast<std::size_t>(
        std::min<std::uint64_t>(processed_block_count_ + 1U, partition_count));
    const std::size_t current_slot =
        static_cast<std::size_t>(processed_block_count_ % partition_count);
    for (std::size_t partition = 0; partition < available_partition_count;
         ++partition) {
        const auto kernel_spectrum = kernel_->partition_spectrum(partition);
        std::span<const std::complex<double>> input_spectrum;
        if (partition == 0U) {
            input_spectrum = input_scratch_;
        } else {
            const std::size_t slot =
                (current_slot + partition_count - partition) % partition_count;
            input_spectrum = {
                input_spectra_.data() + slot * transform_length,
                transform_length,
            };
        }
        for (std::size_t bin = 0; bin < transform_length; ++bin) {
            output_spectrum_scratch_[bin] +=
                input_spectrum[bin] * kernel_spectrum[bin];
        }
    }
    kernel_->plan()->inverse(output_spectrum_scratch_);

    for (std::size_t frame = 0; frame < block_frame_count; ++frame) {
        const double prior_overlap = frame < overlap_.size() ? overlap_[frame] : 0.0;
        const double sample = output_spectrum_scratch_[frame].real() + prior_overlap;
        if (!std::isfinite(sample)) {
            throw std::domain_error{
                "partitioned convolution output was non-finite"};
        }
        output_scratch_[frame] = sample;
    }
    for (std::size_t frame = 0; frame < next_overlap_scratch_.size(); ++frame) {
        const double sample =
            output_spectrum_scratch_[block_frame_count + frame].real();
        if (!std::isfinite(sample)) {
            throw std::domain_error{
                "partitioned convolution overlap was non-finite"};
        }
        next_overlap_scratch_[frame] = sample;
    }
    if (processed_block_count_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{
            "partitioned convolution block horizon overflowed"};
    }

    std::copy_n(input_scratch_.begin(), transform_length,
                input_spectra_.begin() +
                    static_cast<std::ptrdiff_t>(current_slot * transform_length));
    std::copy_n(output_scratch_.begin(), output.size(), output.begin());
    overlap_.swap(next_overlap_scratch_);
    ++processed_block_count_;
}

CausalConfiguredIrConvolver::CausalConfiguredIrConvolver(
    dsp::RuntimeConvolutionKernel kernel)
    : implementation_(std::visit(
          [](auto value) -> Implementation {
              using Kernel = typename decltype(value)::element_type;
              if constexpr (std::is_same_v<Kernel,
                                           const dsp::FixedConvolutionKernel>) {
                  return std::make_unique<CausalOverlapSaveConvolver>(
                      std::move(value));
              } else {
                  return std::make_unique<CausalPartitionedConvolver>(
                      std::move(value));
              }
          },
          std::move(kernel).storage())) {}

void CausalConfiguredIrConvolver::process(std::span<const double> input,
                                          std::span<double> output) {
    std::visit([&](auto &implementation) { implementation->process(input, output); },
               implementation_);
}

} // namespace crankwave::presentation
