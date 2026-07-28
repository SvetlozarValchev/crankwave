#pragma once

#include "dsp/fixed_fft.hpp"

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

// One continuous causal convolution history. Independent route instances
// share an immutable spectral kernel but never share mutable work or history.
// There is intentionally no direct-FIR fallback or tail-flush operation.
class CausalOverlapSaveConvolver {
  public:
    static constexpr std::size_t coefficient_count =
        dsp::FixedConvolutionKernel::coefficient_count;
    static constexpr std::size_t history_count = coefficient_count - 1;
    static constexpr std::size_t maximum_block_frame_count = 9600;
    static constexpr std::size_t transform_length =
        dsp::FixedFftLimits::transform_length;

    static_assert(history_count + maximum_block_frame_count <= transform_length);
    static_assert(maximum_block_frame_count <= history_count);

    explicit CausalOverlapSaveConvolver(
        std::shared_ptr<const dsp::FixedConvolutionKernel> kernel);

    CausalOverlapSaveConvolver(const CausalOverlapSaveConvolver &) = delete;
    CausalOverlapSaveConvolver &operator=(const CausalOverlapSaveConvolver &) = delete;
    CausalOverlapSaveConvolver(CausalOverlapSaveConvolver &&) = delete;
    CausalOverlapSaveConvolver &operator=(CausalOverlapSaveConvolver &&) = delete;

    // Processes one nonempty block of at most 9,600 frames. Input and output
    // lengths must match; overlapping spans are supported. All structural and
    // input checks occur before mutation. Arithmetic is staged so an exception
    // leaves both caller output and continuous history unchanged.
    void process(std::span<const double> input, std::span<double> output);

    [[nodiscard]] const std::shared_ptr<const dsp::FixedConvolutionKernel> &
    kernel() const noexcept;
    [[nodiscard]] std::span<const double, history_count> history() const noexcept;

  private:
    const std::shared_ptr<const dsp::FixedConvolutionKernel> kernel_;
    std::vector<double> history_;
    std::vector<double> next_history_scratch_;
    std::vector<double> output_scratch_;
    std::vector<std::complex<double>> transform_scratch_;
};

} // namespace engine_sim_offline::presentation
