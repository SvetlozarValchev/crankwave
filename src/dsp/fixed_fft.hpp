#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <variant>
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

// Versioned long-IR convolution shape. Presentation produces exactly 3,840
// frames per 20 ms method block, so one causal uniform partition has the same
// extent. An 8,192-point transform is large enough for the complete 7,679-frame
// linear convolution of two partitions without circular aliasing.
struct PartitionedConvolutionLimits {
    static constexpr std::size_t partition_frame_count = 3840;
    static constexpr std::size_t transform_bit_count = 13;
    static constexpr std::size_t transform_length = 1U << transform_bit_count;
    static constexpr std::size_t forward_root_count = transform_length / 2;
    static constexpr std::size_t maximum_coefficient_count = 570654;
    static constexpr std::size_t maximum_partition_count =
        (maximum_coefficient_count + partition_frame_count - 1) /
        partition_frame_count;
};

class PartitionedFftPlan {
  public:
    PartitionedFftPlan();

    PartitionedFftPlan(const PartitionedFftPlan &) = delete;
    PartitionedFftPlan &operator=(const PartitionedFftPlan &) = delete;
    PartitionedFftPlan(PartitionedFftPlan &&) = delete;
    PartitionedFftPlan &operator=(PartitionedFftPlan &&) = delete;

    void forward(std::span<std::complex<double>> values) const;
    void inverse(std::span<std::complex<double>> values) const;

  private:
    void transform(std::span<std::complex<double>> values,
                   bool inverse_transform) const;

    const std::vector<std::size_t> bit_reversed_indices_;
    const std::vector<std::complex<double>> forward_roots_;
};

// Immutable frequency-domain uniform partitions. Coefficients remain ordered
// and complete; the last partition alone is right-zero-padded. The old fixed
// kernel remains a separate type so its construction and bytes cannot change.
class PartitionedConvolutionKernel {
  public:
    explicit PartitionedConvolutionKernel(std::span<const double> coefficients);

    PartitionedConvolutionKernel(const PartitionedConvolutionKernel &) = delete;
    PartitionedConvolutionKernel &
    operator=(const PartitionedConvolutionKernel &) = delete;
    PartitionedConvolutionKernel(PartitionedConvolutionKernel &&) = delete;
    PartitionedConvolutionKernel &operator=(PartitionedConvolutionKernel &&) = delete;

    [[nodiscard]] const std::shared_ptr<const PartitionedFftPlan> &
    plan() const noexcept;
    [[nodiscard]] std::size_t coefficient_count() const noexcept;
    [[nodiscard]] std::size_t partition_count() const noexcept;
    [[nodiscard]] std::span<const std::complex<double>>
    partition_spectrum(std::size_t partition) const;
    [[nodiscard]] std::span<const std::complex<double>> spectra() const noexcept;

  private:
    const std::shared_ptr<const PartitionedFftPlan> plan_;
    const std::size_t coefficient_count_;
    const std::size_t partition_count_;
    const std::vector<std::complex<double>> spectra_;
};

class RuntimeConvolutionKernel {
  public:
    using Storage =
        std::variant<std::shared_ptr<const FixedConvolutionKernel>,
                     std::shared_ptr<const PartitionedConvolutionKernel>>;

    RuntimeConvolutionKernel() = default;
    RuntimeConvolutionKernel(std::nullptr_t) noexcept {}
    RuntimeConvolutionKernel(
        std::shared_ptr<const FixedConvolutionKernel> kernel) noexcept
        : storage_(std::move(kernel)) {}
    RuntimeConvolutionKernel(
        std::shared_ptr<const PartitionedConvolutionKernel> kernel) noexcept
        : storage_(std::move(kernel)) {}

    [[nodiscard]] const Storage &storage() const noexcept { return storage_; }
    [[nodiscard]] bool valid() const noexcept {
        return std::visit([](const auto &value) { return value != nullptr; },
                          storage_);
    }

    // Compatibility conversion for code that intentionally exercises the
    // legacy convolver directly. Extended kernels convert to an empty pointer.
    operator std::shared_ptr<const FixedConvolutionKernel>() const noexcept {
        const auto *fixed =
            std::get_if<std::shared_ptr<const FixedConvolutionKernel>>(&storage_);
        return fixed == nullptr ? nullptr : *fixed;
    }

  private:
    Storage storage_ = std::shared_ptr<const FixedConvolutionKernel>{};
};

} // namespace engine_sim_offline::dsp
