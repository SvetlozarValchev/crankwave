#include "presentation/presentation_method_registry.hpp"

#include <span>
#include <string>
#include <utility>

namespace engine_sim_offline::presentation {
namespace {

static_assert(kCalibratedPressurePublicationMethodId !=
              kCoherentTwoOutletAuditionMethodId);

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
                   "method identity is not the exact implemented physical-pressure "
                   "presentation authority");
    }
}

} // namespace

const PresentationMethodIdentities &implemented_presentation_method_identities() {
    static const PresentationMethodIdentities identities{
        make_identity(kCalibratedPressurePublicationMethodId,
                      kCalibratedPressurePublicationMethodVersion,
                      calibrated_pressure_publication_method_descriptor()),
        make_identity(kCoherentTwoOutletAuditionMethodId,
                      kCoherentTwoOutletAuditionMethodVersion,
                      coherent_two_outlet_audition_method_descriptor()),
    };
    return identities;
}

const contract::MethodIdentity &
calibrated_pressure_publication_method_identity() {
    return implemented_presentation_method_identities()
        .calibrated_pressure_publication;
}

const contract::MethodIdentity &coherent_two_outlet_audition_method_identity() {
    return implemented_presentation_method_identities()
        .coherent_two_outlet_audition;
}

bool exactly_matches_implemented_presentation_methods(
    const contract::PresentationMethods &methods) {
    const auto &implemented = implemented_presentation_method_identities();
    return methods.calibrated_pressure_publication.value ==
               implemented.calibrated_pressure_publication &&
           methods.coherent_two_outlet_audition.value ==
               implemented.coherent_two_outlet_audition;
}

contract::ValidationReport
admit_implemented_presentation_methods(const contract::PresentationMethods &methods) {
    contract::ValidationReport report;
    const auto &implemented = implemented_presentation_method_identities();
    require_exact(report, methods.calibrated_pressure_publication.value,
                  implemented.calibrated_pressure_publication,
                  "presentation.methods.calibrated_pressure_publication.value");
    require_exact(report, methods.coherent_two_outlet_audition.value,
                  implemented.coherent_two_outlet_audition,
                  "presentation.methods.coherent_two_outlet_audition.value");
    return report;
}

} // namespace engine_sim_offline::presentation
