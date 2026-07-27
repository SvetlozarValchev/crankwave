#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace engine_sim_offline::dsp {

// Frozen transform shape for the narrow P1.8 configured-IR route. This is not
// a general FFT API: its fixed topology and traversal order are part of the
// reference method identity.
struct P18FixedFftLimits {
    static constexpr std::size_t transform_bit_count = 16;
    static constexpr std::size_t transform_length = 1U << transform_bit_count;
    static constexpr std::size_t forward_root_count = transform_length / 2;
};

// Immutable radix-2 DIT plan for exactly 65,536 binary64 complex values.
// Multiple immutable kernels and independent route histories may share one
// plan. A transform validates its caller-owned work buffer before mutation and
// rejects a non-finite result after completing the recorded topology.
class P18FixedFftPlan {
  public:
    P18FixedFftPlan();

    P18FixedFftPlan(const P18FixedFftPlan &) = delete;
    P18FixedFftPlan &operator=(const P18FixedFftPlan &) = delete;
    P18FixedFftPlan(P18FixedFftPlan &&) = delete;
    P18FixedFftPlan &operator=(P18FixedFftPlan &&) = delete;

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
// P1.8 static IR. The original time-domain coefficients are deliberately not
// retained: route instances share this spectrum and own only their histories
// and work storage.
class P18FixedConvolutionKernel {
  public:
    static constexpr std::size_t coefficient_count = 30071;

    explicit P18FixedConvolutionKernel(std::span<const double> coefficients);
    P18FixedConvolutionKernel(std::span<const double> coefficients,
                              std::shared_ptr<const P18FixedFftPlan> shared_plan);

    P18FixedConvolutionKernel(const P18FixedConvolutionKernel &) = delete;
    P18FixedConvolutionKernel &operator=(const P18FixedConvolutionKernel &) = delete;
    P18FixedConvolutionKernel(P18FixedConvolutionKernel &&) = delete;
    P18FixedConvolutionKernel &operator=(P18FixedConvolutionKernel &&) = delete;

    [[nodiscard]] const std::shared_ptr<const P18FixedFftPlan> &plan() const noexcept;
    [[nodiscard]] std::span<const std::complex<double>,
                            P18FixedFftLimits::transform_length>
    spectrum() const noexcept;

  private:
    static std::shared_ptr<const P18FixedFftPlan>
    validated_plan(std::span<const double> coefficients,
                   std::shared_ptr<const P18FixedFftPlan> shared_plan);
    static std::vector<std::complex<double>>
    build_spectrum(std::span<const double> coefficients, const P18FixedFftPlan &plan);

    const std::shared_ptr<const P18FixedFftPlan> plan_;
    const std::vector<std::complex<double>> spectrum_;
};

} // namespace engine_sim_offline::dsp
