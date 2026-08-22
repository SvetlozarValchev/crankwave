#pragma once

#include "dsp/fixed_fft.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace crankwave::presentation {

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

// Additive uniform-partitioned path for IRs outside the v1 fixed-kernel shape.
// It consumes the exact 3,840-frame presentation quantum and preserves the
// complete causal tail between calls.
class CausalPartitionedConvolver {
  public:
    static constexpr std::size_t block_frame_count =
        dsp::PartitionedConvolutionLimits::partition_frame_count;
    static constexpr std::size_t transform_length =
        dsp::PartitionedConvolutionLimits::transform_length;

    explicit CausalPartitionedConvolver(
        std::shared_ptr<const dsp::PartitionedConvolutionKernel> kernel);

    CausalPartitionedConvolver(const CausalPartitionedConvolver &) = delete;
    CausalPartitionedConvolver &
    operator=(const CausalPartitionedConvolver &) = delete;
    CausalPartitionedConvolver(CausalPartitionedConvolver &&) = delete;
    CausalPartitionedConvolver &operator=(CausalPartitionedConvolver &&) = delete;

    void process(std::span<const double> input, std::span<double> output);

  private:
    const std::shared_ptr<const dsp::PartitionedConvolutionKernel> kernel_;
    std::vector<std::complex<double>> input_spectra_;
    std::vector<std::complex<double>> input_scratch_;
    std::vector<std::complex<double>> output_spectrum_scratch_;
    std::vector<double> overlap_;
    std::vector<double> next_overlap_scratch_;
    std::vector<double> output_scratch_;
    std::uint64_t processed_block_count_ = 0;
};

// Runtime discriminator used by the presentation session. Legacy routes still
// construct CausalOverlapSaveConvolver directly; extended routes cannot alter
// that class's arithmetic or state shape.
class CausalConfiguredIrConvolver {
  public:
    explicit CausalConfiguredIrConvolver(dsp::RuntimeConvolutionKernel kernel);

    CausalConfiguredIrConvolver(const CausalConfiguredIrConvolver &) = delete;
    CausalConfiguredIrConvolver &
    operator=(const CausalConfiguredIrConvolver &) = delete;
    CausalConfiguredIrConvolver(CausalConfiguredIrConvolver &&) = delete;
    CausalConfiguredIrConvolver &operator=(CausalConfiguredIrConvolver &&) = delete;

    void process(std::span<const double> input, std::span<double> output);

  private:
    using Implementation =
        std::variant<std::unique_ptr<CausalOverlapSaveConvolver>,
                     std::unique_ptr<CausalPartitionedConvolver>>;
    Implementation implementation_;
};

} // namespace crankwave::presentation
