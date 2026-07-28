#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace engine_sim_offline::dsp {

// Fixed transform shape for the configured-IR route. This is not
// a general FFT API: its fixed topology and traversal order are part of the
// reference method identity.
struct FixedFftLimits {
    static constexpr std::size_t transform_bit_count = 16;
    static constexpr std::size_t transform_length = 1U << transform_bit_count;
    static constexpr std::size_t forward_root_count = transform_length / 2;
};

// Immutable radix-2 DIT plan for exactly 65,536 binary64 complex values.
// Multiple immutable kernels and independent route histories may share one
// plan. A transform validates its caller-owned work buffer before mutation and
// rejects a non-finite result after completing the recorded topology.
class FixedFftPlan {
  public:
    FixedFftPlan();

    FixedFftPlan(const FixedFftPlan &) = delete;
    FixedFftPlan &operator=(const FixedFftPlan &) = delete;
    FixedFftPlan(FixedFftPlan &&) = delete;
    FixedFftPlan &operator=(FixedFftPlan &&) = delete;

    void forward(std::span<std::complex<double>> values) const;
    void inverse(std::span<std::complex<double>> values) const;

    [[nodiscard]] std::size_t reversed_index(std::size_t index) const;
    [[nodiscard]] std::complex<double> forward_root(std::size_t index) const;

  private:
    void transform(std::span<std::complex<double>> values,
                   bool inverse_transform) const;

    const std::vector<std::size_t> bit_reversed_indices_;
    const std::vector<std::complex<double>> forward_roots_;
};

// Immutable zero-padded and forward-transformed form of the exact 30,071-tap
// accepted static IR. The original time-domain coefficients are deliberately not
// retained: route instances share this spectrum and own only their histories
// and work storage.
class FixedConvolutionKernel {
  public:
    static constexpr std::size_t coefficient_count = 30071;

    explicit FixedConvolutionKernel(std::span<const double> coefficients);
    FixedConvolutionKernel(std::span<const double> coefficients,
                           std::shared_ptr<const FixedFftPlan> shared_plan);

    FixedConvolutionKernel(const FixedConvolutionKernel &) = delete;
    FixedConvolutionKernel &operator=(const FixedConvolutionKernel &) = delete;
    FixedConvolutionKernel(FixedConvolutionKernel &&) = delete;
    FixedConvolutionKernel &operator=(FixedConvolutionKernel &&) = delete;

    [[nodiscard]] const std::shared_ptr<const FixedFftPlan> &plan() const noexcept;
    [[nodiscard]] std::span<const std::complex<double>,
                            FixedFftLimits::transform_length>
    spectrum() const noexcept;

  private:
    static std::shared_ptr<const FixedFftPlan>
    validated_plan(std::span<const double> coefficients,
                   std::shared_ptr<const FixedFftPlan> shared_plan);
    static std::vector<std::complex<double>>
    build_spectrum(std::span<const double> coefficients, const FixedFftPlan &plan);

    const std::shared_ptr<const FixedFftPlan> plan_;
    const std::vector<std::complex<double>> spectrum_;
};

} // namespace engine_sim_offline::dsp
