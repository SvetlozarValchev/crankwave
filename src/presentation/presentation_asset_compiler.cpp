#include "presentation/presentation_asset_compiler.hpp"

#include "dsp/static_ir_conversion.hpp"
#include "numeric/target_extended_precision.hpp"

#include <bit>
#include <cmath>
#include <complex>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::presentation {
namespace {

[[nodiscard]] bool
exact_supported_conversion_method(const contract::MethodIdentity &method) {
    return method == static_ir_conversion_method_identity();
}

[[nodiscard]] bool
exact_supported_convolution_method(const contract::MethodIdentity &method) {
    return method == fixed_overlap_save_convolution_method_identity();
}

[[nodiscard]] constexpr bool conversion_environment_available() noexcept {
    return numeric::target_extended_precision_format_is_admitted();
}

[[nodiscard]] bool supported_media(const contract::AudioMediaContract &media) noexcept {
    return media.encoding == contract::AudioSampleEncoding::pcm_s16le &&
           media.channel_layout == contract::AudioChannelLayout::mono &&
           media.sample_rate ==
               contract::RationalRateHz{kConfiguredIrSampleRateHz, 1} &&
           media.frame_count > 0 && media.frame_count <= kMaximumConfiguredIrFrameCount;
}

void append_f64le(std::vector<std::byte> &destination, double value) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        destination.push_back(
            std::byte{static_cast<unsigned char>((bits >> shift) & 0xffU)});
    }
}

[[nodiscard]] PresentationDerivedPayloadIdentity
coefficient_identity(std::span<const double> coefficients) {
    std::vector<std::byte> bytes;
    bytes.reserve(coefficients.size() * sizeof(double));
    for (const double coefficient : coefficients) {
        append_f64le(bytes, coefficient);
    }
    return {static_cast<std::uint64_t>(bytes.size()), contract::sha256(bytes)};
}

[[nodiscard]] PresentationDerivedPayloadIdentity
spectrum_identity(std::span<const std::complex<double>> spectrum) {
    std::vector<std::byte> bytes;
    bytes.reserve(spectrum.size() * 2 * sizeof(double));
    for (const std::complex<double> bin : spectrum) {
        append_f64le(bytes, bin.real());
        append_f64le(bytes, bin.imag());
    }
    return {static_cast<std::uint64_t>(bytes.size()), contract::sha256(bytes)};
}

[[nodiscard]] PresentationAssetCompileError
error(PresentationAssetCompileErrorCode code) noexcept {
    return {code, std::monostate{}};
}

} // namespace

namespace detail {

struct CompiledPresentationAssetFactory {
    [[nodiscard]] static CompiledPresentationAsset
    make(contract::AssetPayloadIdentity raw_payload_identity,
         contract::AudioMediaContract source_media,
         contract::MethodIdentity conversion_method,
         contract::ResolvedValue<double> configured_gain,
         std::size_t meaningful_support_frame_count, std::vector<double> coefficients,
         PresentationDerivedPayloadIdentity coefficient_f64le_identity) {
        return CompiledPresentationAsset{
            std::move(raw_payload_identity), std::move(source_media),
            std::move(conversion_method),    std::move(configured_gain),
            meaningful_support_frame_count,  std::move(coefficients),
            coefficient_f64le_identity,
        };
    }
};

struct CompiledPresentationConvolutionKernelFactory {
    [[nodiscard]] static CompiledPresentationConvolutionKernel
    make(PresentationConvolutionKernelKey key,
         std::shared_ptr<const dsp::FixedConvolutionKernel> kernel,
         PresentationDerivedPayloadIdentity spectrum_complex_f64le_identity) {
        return CompiledPresentationConvolutionKernel{std::move(key), std::move(kernel),
                                                     spectrum_complex_f64le_identity};
    }
};

} // namespace detail

CompiledPresentationAsset::CompiledPresentationAsset(
    contract::AssetPayloadIdentity raw_payload_identity,
    contract::AudioMediaContract source_media,
    contract::MethodIdentity conversion_method,
    contract::ResolvedValue<double> configured_gain,
    std::size_t meaningful_support_frame_count, std::vector<double> coefficients,
    PresentationDerivedPayloadIdentity coefficient_f64le_identity)
    : raw_payload_identity_(std::move(raw_payload_identity)),
      source_media_(std::move(source_media)),
      conversion_method_(std::move(conversion_method)),
      configured_gain_(std::move(configured_gain)),
      meaningful_support_frame_count_(meaningful_support_frame_count),
      coefficients_(std::move(coefficients)),
      coefficient_f64le_identity_(coefficient_f64le_identity) {}

const contract::AssetPayloadIdentity &
CompiledPresentationAsset::raw_payload_identity() const noexcept {
    return raw_payload_identity_;
}

const contract::AudioMediaContract &
CompiledPresentationAsset::source_media() const noexcept {
    return source_media_;
}

const contract::MethodIdentity &
CompiledPresentationAsset::conversion_method() const noexcept {
    return conversion_method_;
}

const contract::ResolvedValue<double> &
CompiledPresentationAsset::configured_gain() const noexcept {
    return configured_gain_;
}

std::size_t CompiledPresentationAsset::meaningful_support_frame_count() const noexcept {
    return meaningful_support_frame_count_;
}

std::span<const double> CompiledPresentationAsset::coefficients() const noexcept {
    return coefficients_;
}

const PresentationDerivedPayloadIdentity &
CompiledPresentationAsset::coefficient_f64le_identity() const noexcept {
    return coefficient_f64le_identity_;
}

CompiledPresentationConvolutionKernel::CompiledPresentationConvolutionKernel(
    PresentationConvolutionKernelKey key,
    std::shared_ptr<const dsp::FixedConvolutionKernel> kernel,
    PresentationDerivedPayloadIdentity spectrum_complex_f64le_identity)
    : key_(std::move(key)), kernel_(std::move(kernel)),
      spectrum_complex_f64le_identity_(spectrum_complex_f64le_identity) {}

const PresentationConvolutionKernelKey &
CompiledPresentationConvolutionKernel::key() const noexcept {
    return key_;
}

const std::shared_ptr<const dsp::FixedConvolutionKernel> &
CompiledPresentationConvolutionKernel::kernel() const noexcept {
    return kernel_;
}

const PresentationDerivedPayloadIdentity &
CompiledPresentationConvolutionKernel::spectrum_complex_f64le_identity()
    const noexcept {
    return spectrum_complex_f64le_identity_;
}

PresentationAssetCompileResult compile_presentation_asset(
    const contract::AudioAssetSpec &asset, PresentationAssetPayloadView payload,
    const contract::MethodIdentity &impulse_response_conversion_method,
    const contract::ResolvedValue<double> &impulse_response_gain_linear) {
    if (!asset.id.valid()) {
        return error(PresentationAssetCompileErrorCode::invalid_asset_id);
    }
    if (payload.id != asset.id) {
        return error(PresentationAssetCompileErrorCode::payload_id_mismatch);
    }
    if (payload.bytes.empty()) {
        return error(PresentationAssetCompileErrorCode::missing_payload);
    }
    if (payload.bytes.size() > kMaximumConfiguredIrContainerByteCount) {
        return error(PresentationAssetCompileErrorCode::payload_container_too_large);
    }
    if (!supported_media(asset.media.value)) {
        return error(PresentationAssetCompileErrorCode::unsupported_media_contract);
    }
    if (!exact_supported_conversion_method(impulse_response_conversion_method)) {
        return error(PresentationAssetCompileErrorCode::unsupported_conversion_method);
    }
    if (!conversion_environment_available()) {
        return error(PresentationAssetCompileErrorCode::conversion_method_unavailable);
    }
    if (!std::isfinite(impulse_response_gain_linear.value) ||
        impulse_response_gain_linear.value < 0.0 ||
        std::signbit(impulse_response_gain_linear.value)) {
        return error(PresentationAssetCompileErrorCode::invalid_impulse_response_gain);
    }

    const contract::Sha256Digest payload_sha256 = contract::sha256(payload.bytes);
    if (asset.content_sha256.value.is_zero() ||
        payload_sha256 != asset.content_sha256.value) {
        return error(PresentationAssetCompileErrorCode::payload_sha256_mismatch);
    }

    auto decoded_result = decode_pcm16_ir_wave(payload.bytes);
    auto *decoded = std::get_if<DecodedPcm16Ir>(&decoded_result);
    if (decoded == nullptr) {
        return PresentationAssetCompileError{
            PresentationAssetCompileErrorCode::pcm16_wave_decode_failed,
            std::get<Pcm16IrDecodeError>(decoded_result),
        };
    }
    if (decoded->samples.size() != asset.media.value.frame_count) {
        return error(PresentationAssetCompileErrorCode::media_frame_count_mismatch);
    }
    if (decoded->meaningful_support_frames == 0) {
        return error(PresentationAssetCompileErrorCode::empty_meaningful_support);
    }

    std::vector<double> coefficients;
    try {
        coefficients =
            dsp::convert_static_ir(decoded->samples, decoded->meaningful_support_frames,
                                   impulse_response_gain_linear.value);
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::runtime_error &) {
        return error(PresentationAssetCompileErrorCode::conversion_method_unavailable);
    } catch (const std::logic_error &) {
        return error(PresentationAssetCompileErrorCode::conversion_failed);
    }
    if (coefficients.size() !=
        dsp::static_ir_target_count(decoded->meaningful_support_frames)) {
        return error(PresentationAssetCompileErrorCode::conversion_failed);
    }

    const auto coefficients_f64le = coefficient_identity(coefficients);
    return detail::CompiledPresentationAssetFactory::make(
        {payload.id, static_cast<std::uint64_t>(payload.bytes.size()), payload_sha256},
        asset.media.value, impulse_response_conversion_method,
        impulse_response_gain_linear, decoded->meaningful_support_frames,
        std::move(coefficients), coefficients_f64le);
}

PresentationConvolutionKernelCompileResult compile_presentation_convolution_kernel(
    const CompiledPresentationAsset &asset,
    const contract::MethodIdentity &convolution_method) {
    const auto kernel_error = [](PresentationConvolutionKernelCompileErrorCode code) {
        return PresentationConvolutionKernelCompileError{code};
    };
    if (!exact_supported_convolution_method(convolution_method)) {
        return kernel_error(PresentationConvolutionKernelCompileErrorCode::
                                unsupported_convolution_method);
    }
    if (asset.coefficients().empty() ||
        asset.coefficients().size() > dsp::FixedConvolutionKernel::coefficient_count) {
        return kernel_error(PresentationConvolutionKernelCompileErrorCode::
                                unsupported_coefficient_shape);
    }
    const auto observed_coefficient_identity =
        coefficient_identity(asset.coefficients());
    if (observed_coefficient_identity != asset.coefficient_f64le_identity()) {
        return kernel_error(PresentationConvolutionKernelCompileErrorCode::
                                coefficient_identity_mismatch);
    }

    std::shared_ptr<const dsp::FixedConvolutionKernel> kernel;
    try {
        if (asset.coefficients().size() ==
            dsp::FixedConvolutionKernel::coefficient_count) {
            kernel = std::make_shared<const dsp::FixedConvolutionKernel>(
                asset.coefficients());
        } else {
            std::vector<double> padded_coefficients(asset.coefficients().begin(),
                                                    asset.coefficients().end());
            padded_coefficients.resize(dsp::FixedConvolutionKernel::coefficient_count,
                                       0.0);
            kernel = std::make_shared<const dsp::FixedConvolutionKernel>(
                padded_coefficients);
        }
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::logic_error &) {
        return kernel_error(
            PresentationConvolutionKernelCompileErrorCode::kernel_construction_failed);
    }
    const auto spectrum_complex_f64le = spectrum_identity(kernel->spectrum());
    PresentationConvolutionKernelKey key{
        asset.raw_payload_identity(),
        asset.conversion_method(),
        std::bit_cast<std::uint64_t>(asset.configured_gain().value),
        asset.coefficient_f64le_identity(),
        convolution_method,
    };
    return detail::CompiledPresentationConvolutionKernelFactory::make(
        std::move(key), std::move(kernel), spectrum_complex_f64le);
}

} // namespace engine_sim_offline::presentation
