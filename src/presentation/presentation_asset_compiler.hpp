#pragma once

#include "dsp/fixed_fft.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "presentation/pcm16_ir_decoder.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::presentation {

namespace detail {
struct CompiledPresentationAssetFactory;
struct CompiledPresentationConvolutionKernelFactory;
} // namespace detail

// Borrowed only for one compile call. Keeping this view in the presentation layer
// avoids making the DSP/asset compiler depend upward on the public render API that
// owns payload storage.
struct PresentationAssetPayloadView {
    contract::AudioAssetId id;
    std::span<const std::byte> bytes;
};

// A data chunk is already frame-bounded by the decoder. This separate whole-container
// limit prevents an otherwise valid RIFF file from carrying gigabytes of irrelevant
// ancillary chunks through request hashing and decoding.
inline constexpr std::size_t kMaximumConfiguredIrContainerByteCount = 1024U * 1024U;

// Canonical little-endian identities. Coefficients are serialized as one f64 per
// index. Spectrum bins are serialized in index order as real-f64 then imaginary-f64.
struct PresentationDerivedPayloadIdentity {
    std::uint64_t byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const PresentationDerivedPayloadIdentity &,
                           const PresentationDerivedPayloadIdentity &) = default;
};

class CompiledPresentationAsset final {
  public:
    CompiledPresentationAsset(const CompiledPresentationAsset &) = delete;
    CompiledPresentationAsset &operator=(const CompiledPresentationAsset &) = delete;
    CompiledPresentationAsset(CompiledPresentationAsset &&) noexcept = default;
    CompiledPresentationAsset &operator=(CompiledPresentationAsset &&) = delete;

    [[nodiscard]] const contract::AssetPayloadIdentity &
    raw_payload_identity() const noexcept;
    [[nodiscard]] const contract::AudioMediaContract &source_media() const noexcept;
    [[nodiscard]] const contract::MethodIdentity &conversion_method() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    configured_gain() const noexcept;
    [[nodiscard]] std::size_t meaningful_support_frame_count() const noexcept;
    [[nodiscard]] std::span<const double> coefficients() const noexcept;
    [[nodiscard]] const PresentationDerivedPayloadIdentity &
    coefficient_f64le_identity() const noexcept;

  private:
    CompiledPresentationAsset(
        contract::AssetPayloadIdentity raw_payload_identity,
        contract::AudioMediaContract source_media,
        contract::MethodIdentity conversion_method,
        contract::ResolvedValue<double> configured_gain,
        std::size_t meaningful_support_frame_count, std::vector<double> coefficients,
        PresentationDerivedPayloadIdentity coefficient_f64le_identity);

    contract::AssetPayloadIdentity raw_payload_identity_;
    contract::AudioMediaContract source_media_;
    contract::MethodIdentity conversion_method_;
    contract::ResolvedValue<double> configured_gain_;
    std::size_t meaningful_support_frame_count_ = 0;
    std::vector<double> coefficients_;
    PresentationDerivedPayloadIdentity coefficient_f64le_identity_;

    friend struct detail::CompiledPresentationAssetFactory;
};

enum class PresentationAssetCompileErrorCode : std::uint8_t {
    invalid_asset_id,
    payload_id_mismatch,
    missing_payload,
    payload_container_too_large,
    payload_sha256_mismatch,
    unsupported_media_contract,
    media_frame_count_mismatch,
    unsupported_conversion_method,
    conversion_method_unavailable,
    invalid_impulse_response_gain,
    pcm16_wave_decode_failed,
    empty_meaningful_support,
    conversion_failed,
};

struct PresentationAssetCompileError {
    PresentationAssetCompileErrorCode code =
        PresentationAssetCompileErrorCode::invalid_asset_id;
    std::variant<std::monostate, Pcm16IrDecodeError> detail;

    friend bool operator==(const PresentationAssetCompileError &,
                           const PresentationAssetCompileError &) = default;
};

using PresentationAssetCompileResult =
    std::variant<CompiledPresentationAsset, PresentationAssetCompileError>;

// Compiles one content-addressed configured-IR asset for one route. The caller must
// already have validated the complete PresentationCalibration and its provenance;
// the enclosing opaque job retains that original specification. All local payload
// and method mismatches are returned as typed errors. Resource exhaustion remains
// exceptional.
[[nodiscard]] PresentationAssetCompileResult compile_presentation_asset(
    const contract::AudioAssetSpec &asset, PresentationAssetPayloadView payload,
    const contract::MethodIdentity &impulse_response_conversion_method,
    const contract::ResolvedValue<double> &impulse_response_gain_linear);

struct PresentationConvolutionKernelKey {
    contract::AssetPayloadIdentity raw_payload_identity;
    contract::MethodIdentity conversion_method;
    std::uint64_t configured_gain_binary64_bits = 0;
    PresentationDerivedPayloadIdentity coefficient_f64le_identity;
    contract::MethodIdentity convolution_method;

    friend bool operator==(const PresentationConvolutionKernelKey &,
                           const PresentationConvolutionKernelKey &) = default;
};

class CompiledPresentationConvolutionKernel final {
  public:
    CompiledPresentationConvolutionKernel(
        const CompiledPresentationConvolutionKernel &) = delete;
    CompiledPresentationConvolutionKernel &
    operator=(const CompiledPresentationConvolutionKernel &) = delete;
    CompiledPresentationConvolutionKernel(
        CompiledPresentationConvolutionKernel &&) noexcept = default;
    CompiledPresentationConvolutionKernel &
    operator=(CompiledPresentationConvolutionKernel &&) = delete;

    [[nodiscard]] const PresentationConvolutionKernelKey &key() const noexcept;
    [[nodiscard]] const std::shared_ptr<const dsp::FixedConvolutionKernel> &
    kernel() const noexcept;
    [[nodiscard]] const PresentationDerivedPayloadIdentity &
    spectrum_complex_f64le_identity() const noexcept;

  private:
    CompiledPresentationConvolutionKernel(
        PresentationConvolutionKernelKey key,
        std::shared_ptr<const dsp::FixedConvolutionKernel> kernel,
        PresentationDerivedPayloadIdentity spectrum_complex_f64le_identity);

    PresentationConvolutionKernelKey key_;
    std::shared_ptr<const dsp::FixedConvolutionKernel> kernel_;
    PresentationDerivedPayloadIdentity spectrum_complex_f64le_identity_;

    friend struct detail::CompiledPresentationConvolutionKernelFactory;
};

enum class PresentationConvolutionKernelCompileErrorCode : std::uint8_t {
    unsupported_convolution_method,
    unsupported_coefficient_shape,
    coefficient_identity_mismatch,
    kernel_construction_failed,
};

struct PresentationConvolutionKernelCompileError {
    PresentationConvolutionKernelCompileErrorCode code =
        PresentationConvolutionKernelCompileErrorCode::unsupported_convolution_method;

    friend bool operator==(const PresentationConvolutionKernelCompileError &,
                           const PresentationConvolutionKernelCompileError &) = default;
};

using PresentationConvolutionKernelCompileResult =
    std::variant<CompiledPresentationConvolutionKernel,
                 PresentationConvolutionKernelCompileError>;

// Compiles the independently identified FFT-domain kernel. Keeping this operation
// separate prevents the IR conversion identity from silently claiming FFT behavior.
[[nodiscard]] PresentationConvolutionKernelCompileResult
compile_presentation_convolution_kernel(
    const CompiledPresentationAsset &asset,
    const contract::MethodIdentity &convolution_method);

} // namespace engine_sim_offline::presentation
