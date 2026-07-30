#pragma once

#include "engine_sim_offline/contract/presentation.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::presentation {

inline constexpr std::string_view kCausalReconstructionMethodId =
    "causal-kaiser-sinc-257tap-4096phase-10000-to-192000-binary64-v1";
inline constexpr std::uint32_t kCausalReconstructionMethodVersion = 1;

inline constexpr std::string_view kRouteConditioningMethodId =
    "route-jitter-dc-derivative-air-noise-binary64-v1";
inline constexpr std::uint32_t kRouteConditioningMethodVersion = 1;

#if defined(__wasm32__)
inline constexpr std::string_view kStaticIrConversionMethodId =
    "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-"
    "wasm32-binary128-v1";
#else
inline constexpr std::string_view kStaticIrConversionMethodId =
    "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-v1";
#endif
inline constexpr std::uint32_t kStaticIrConversionMethodVersion = 1;

inline constexpr std::string_view kFixedOverlapSaveConvolutionMethodId =
    "fixed-causal-overlap-save-radix2-dit-fft-65536-binary64-v1";
inline constexpr std::uint32_t kFixedOverlapSaveConvolutionMethodVersion = 1;

inline constexpr std::string_view kRouteStemPublicationMethodId =
    "n-route-wet-selection-float32-wave-publication-v1";
inline constexpr std::uint32_t kRouteStemPublicationMethodVersion = 1;

#if defined(__wasm32__)
inline constexpr std::string_view kOrderedRouteAuditionMethodId =
    "ordered-n-route-serial-float32-quarter-sine-pcm24-wave-master-"
    "wasm32-binary128-v1";
#else
inline constexpr std::string_view kOrderedRouteAuditionMethodId =
    "ordered-n-route-serial-float32-quarter-sine-pcm24-wave-master-v1";
#endif
inline constexpr std::uint32_t kOrderedRouteAuditionMethodVersion = 1;

struct PresentationMethodIdentities {
    contract::MethodIdentity reconstruction;
    contract::MethodIdentity conditioning;
    contract::MethodIdentity impulse_response_conversion;
    contract::MethodIdentity convolution;
    contract::MethodIdentity publication;
    contract::MethodIdentity audition_mix;

    friend bool operator==(const PresentationMethodIdentities &,
                           const PresentationMethodIdentities &) = default;
};

[[nodiscard]] std::string_view causal_reconstruction_method_descriptor() noexcept;
[[nodiscard]] std::string_view route_conditioning_method_descriptor() noexcept;
[[nodiscard]] std::string_view static_ir_conversion_method_descriptor() noexcept;
[[nodiscard]] std::string_view
fixed_overlap_save_convolution_method_descriptor() noexcept;
[[nodiscard]] std::string_view route_stem_publication_method_descriptor() noexcept;
[[nodiscard]] std::string_view ordered_route_audition_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &causal_reconstruction_method_identity();
[[nodiscard]] const contract::MethodIdentity &route_conditioning_method_identity();
[[nodiscard]] const contract::MethodIdentity &static_ir_conversion_method_identity();
[[nodiscard]] const contract::MethodIdentity &
fixed_overlap_save_convolution_method_identity();
[[nodiscard]] const contract::MethodIdentity &route_stem_publication_method_identity();
[[nodiscard]] const contract::MethodIdentity &ordered_route_audition_method_identity();

[[nodiscard]] const PresentationMethodIdentities &
implemented_presentation_method_identities();

[[nodiscard]] bool exactly_matches_implemented_presentation_methods(
    const contract::PresentationMethods &methods);

[[nodiscard]] contract::ValidationReport
admit_implemented_presentation_methods(const contract::PresentationMethods &methods);

} // namespace engine_sim_offline::presentation
