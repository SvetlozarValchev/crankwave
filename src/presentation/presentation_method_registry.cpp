#include "presentation/presentation_method_registry.hpp"

#include <span>

namespace engine_sim_offline::presentation {
namespace {

[[nodiscard]] consteval bool
canonical_lf_descriptor(std::string_view descriptor) noexcept {
    if (descriptor.empty() || descriptor.back() != '\n') {
        return false;
    }
    for (const char character : descriptor) {
        if (character == '\r' || character == '\0') {
            return false;
        }
    }
    return true;
}

static_assert(canonical_lf_descriptor(kStaticIrConversionMethodDescriptor));
static_assert(canonical_lf_descriptor(kFixedOverlapSaveConvolutionMethodDescriptor));
static_assert(kStaticIrConversionMethodId != kFixedOverlapSaveConvolutionMethodId);

[[nodiscard]] contract::Sha256Digest
descriptor_digest(std::string_view descriptor) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

} // namespace

const contract::MethodIdentity &static_ir_conversion_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{kStaticIrConversionMethodId},
        kStaticIrConversionMethodVersion,
        descriptor_digest(kStaticIrConversionMethodDescriptor),
    };
    return identity;
}

const contract::MethodIdentity &fixed_overlap_save_convolution_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{kFixedOverlapSaveConvolutionMethodId},
        kFixedOverlapSaveConvolutionMethodVersion,
        descriptor_digest(kFixedOverlapSaveConvolutionMethodDescriptor),
    };
    return identity;
}

} // namespace engine_sim_offline::presentation
