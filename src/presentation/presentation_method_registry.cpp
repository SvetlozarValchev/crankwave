#include "presentation/presentation_method_registry.hpp"

#include <span>
#include <string>
#include <utility>

namespace engine_sim_offline::presentation {
namespace {

static_assert(kCausalReconstructionMethodId != kRouteConditioningMethodId);
static_assert(kCausalReconstructionMethodId != kStaticIrConversionMethodId);
static_assert(kCausalReconstructionMethodId != kFixedOverlapSaveConvolutionMethodId);
static_assert(kCausalReconstructionMethodId != kRouteStemPublicationMethodId);
static_assert(kCausalReconstructionMethodId != kOrderedRouteAuditionMethodId);
static_assert(kRouteConditioningMethodId != kStaticIrConversionMethodId);
static_assert(kRouteConditioningMethodId != kFixedOverlapSaveConvolutionMethodId);
static_assert(kRouteConditioningMethodId != kRouteStemPublicationMethodId);
static_assert(kRouteConditioningMethodId != kOrderedRouteAuditionMethodId);
static_assert(kStaticIrConversionMethodId != kFixedOverlapSaveConvolutionMethodId);
static_assert(kStaticIrConversionMethodId != kRouteStemPublicationMethodId);
static_assert(kStaticIrConversionMethodId != kOrderedRouteAuditionMethodId);
static_assert(kFixedOverlapSaveConvolutionMethodId != kRouteStemPublicationMethodId);
static_assert(kFixedOverlapSaveConvolutionMethodId != kOrderedRouteAuditionMethodId);
static_assert(kRouteStemPublicationMethodId != kOrderedRouteAuditionMethodId);

[[nodiscard]] contract::Sha256Digest
descriptor_digest(std::string_view descriptor) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] contract::MethodIdentity
make_identity(std::string_view id, std::uint32_t version, std::string_view descriptor) {
    return {
        std::string{id},
        version,
        descriptor_digest(descriptor),
    };
}

void require_exact(contract::ValidationReport &report,
                   const contract::MethodIdentity &actual,
                   const contract::MethodIdentity &implemented, std::string path) {
    if (actual != implemented) {
        report.add(contract::ContractIssueCode::unsupported_value, std::move(path),
                   "method identity is not the exact implemented presentation "
                   "authority");
    }
}

} // namespace

const PresentationMethodIdentities &implemented_presentation_method_identities() {
    static const PresentationMethodIdentities identities{
        make_identity(kCausalReconstructionMethodId, kCausalReconstructionMethodVersion,
                      causal_reconstruction_method_descriptor()),
        make_identity(kRouteConditioningMethodId, kRouteConditioningMethodVersion,
                      route_conditioning_method_descriptor()),
        make_identity(kStaticIrConversionMethodId, kStaticIrConversionMethodVersion,
                      static_ir_conversion_method_descriptor()),
        make_identity(kFixedOverlapSaveConvolutionMethodId,
                      kFixedOverlapSaveConvolutionMethodVersion,
                      fixed_overlap_save_convolution_method_descriptor()),
        make_identity(kRouteStemPublicationMethodId, kRouteStemPublicationMethodVersion,
                      route_stem_publication_method_descriptor()),
        make_identity(kOrderedRouteAuditionMethodId, kOrderedRouteAuditionMethodVersion,
                      ordered_route_audition_method_descriptor()),
    };
    return identities;
}

const contract::MethodIdentity &causal_reconstruction_method_identity() {
    return implemented_presentation_method_identities().reconstruction;
}

const contract::MethodIdentity &route_conditioning_method_identity() {
    return implemented_presentation_method_identities().conditioning;
}

const contract::MethodIdentity &static_ir_conversion_method_identity() {
    return implemented_presentation_method_identities().impulse_response_conversion;
}

const contract::MethodIdentity &fixed_overlap_save_convolution_method_identity() {
    return implemented_presentation_method_identities().convolution;
}

const contract::MethodIdentity &route_stem_publication_method_identity() {
    return implemented_presentation_method_identities().publication;
}

const contract::MethodIdentity &ordered_route_audition_method_identity() {
    return implemented_presentation_method_identities().audition_mix;
}

bool exactly_matches_implemented_presentation_methods(
    const contract::PresentationMethods &methods) {
    const auto &implemented = implemented_presentation_method_identities();
    return methods.reconstruction.value == implemented.reconstruction &&
           methods.conditioning.value == implemented.conditioning &&
           methods.impulse_response_conversion.value ==
               implemented.impulse_response_conversion &&
           methods.convolution.value == implemented.convolution &&
           methods.publication.value == implemented.publication &&
           methods.audition_mix.value == implemented.audition_mix;
}

contract::ValidationReport
admit_implemented_presentation_methods(const contract::PresentationMethods &methods) {
    contract::ValidationReport report;
    const auto &implemented = implemented_presentation_method_identities();
    require_exact(report, methods.reconstruction.value, implemented.reconstruction,
                  "presentation.methods.reconstruction.value");
    require_exact(report, methods.conditioning.value, implemented.conditioning,
                  "presentation.methods.conditioning.value");
    require_exact(report, methods.impulse_response_conversion.value,
                  implemented.impulse_response_conversion,
                  "presentation.methods.impulse_response_conversion.value");
    require_exact(report, methods.convolution.value, implemented.convolution,
                  "presentation.methods.convolution.value");
    require_exact(report, methods.publication.value, implemented.publication,
                  "presentation.methods.publication.value");
    require_exact(report, methods.audition_mix.value, implemented.audition_mix,
                  "presentation.methods.audition_mix.value");
    return report;
}

} // namespace engine_sim_offline::presentation
