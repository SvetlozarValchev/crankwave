#pragma once

#include "dsp/p18_fixed_fft.hpp"

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

// One continuous causal P1.8 convolution history. Independent route instances
// share an immutable spectral kernel but never share mutable work or history.
// There is intentionally no direct-FIR fallback or tail-flush operation.
class P18CausalOverlapSaveConvolver {
  public:
    static constexpr std::size_t coefficient_count =
        dsp::P18FixedConvolutionKernel::coefficient_count;
    static constexpr std::size_t history_count = coefficient_count - 1;
    static constexpr std::size_t maximum_block_frame_count = 9600;
    static constexpr std::size_t transform_length =
        dsp::P18FixedFftLimits::transform_length;

    static_assert(history_count + maximum_block_frame_count <= transform_length);
    static_assert(maximum_block_frame_count <= history_count);

    explicit P18CausalOverlapSaveConvolver(
        std::shared_ptr<const dsp::P18FixedConvolutionKernel> kernel);

    P18CausalOverlapSaveConvolver(const P18CausalOverlapSaveConvolver &) = delete;
    P18CausalOverlapSaveConvolver &
    operator=(const P18CausalOverlapSaveConvolver &) = delete;
    P18CausalOverlapSaveConvolver(P18CausalOverlapSaveConvolver &&) = delete;
    P18CausalOverlapSaveConvolver &operator=(P18CausalOverlapSaveConvolver &&) = delete;

    // Processes one nonempty block of at most 9,600 frames. Input and output
    // lengths must match; overlapping spans are supported. All structural and
    // input checks occur before mutation. Arithmetic is staged so an exception
    // leaves both caller output and continuous history unchanged.
    void process(std::span<const double> input, std::span<double> output);

    [[nodiscard]] const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &
    kernel() const noexcept;
    [[nodiscard]] std::span<const double, history_count> history() const noexcept;

  private:
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> kernel_;
    std::vector<double> history_;
    std::vector<double> next_history_scratch_;
    std::vector<double> output_scratch_;
    std::vector<std::complex<double>> transform_scratch_;
};

} // namespace engine_sim_offline::presentation
