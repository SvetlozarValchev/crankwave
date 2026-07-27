#include "reference/p18_reference_manifest_completion.hpp"

#include "engine_sim_offline/artifacts/reference_manifest_encoder.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace engine_sim_offline::reference {
namespace {

[[nodiscard]] std::logic_error
validation_error(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return std::logic_error{"completed P1.8 reference manifest failed validation"};
    }
    const auto &first = report.issues.front();
    return std::logic_error{"completed P1.8 reference manifest is invalid at " +
                            first.path + ": " + first.message};
}

} // namespace

const contract::RenderManifest &
P18CompletedReferenceManifest::manifest() const noexcept {
    return manifest_;
}

const P18ReferenceProvenance &
P18CompletedReferenceManifest::provenance() const noexcept {
    return provenance_;
}

std::span<const std::byte>
P18CompletedReferenceManifest::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

P18CompletedReferenceManifest
complete_p18_reference_manifest(const P18ReferenceManifestContent &content,
                                const execution::ObservedExecutionFacts &execution) {
    auto provenance = content.provenance();
    contract::RenderManifest manifest{
        content.content(),
        execution.facts(),
    };
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();
    const auto report =
        contract::validate(manifest, provenance.ledger(), source_matrix);
    if (!report.ok()) {
        throw validation_error(report);
    }

    auto encoded = artifacts::encode_reference_manifest_v2(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&encoded)) {
        throw std::logic_error{
            "could not canonically encode completed P1.8 manifest: " +
            error->detail_code + ": " + error->message};
    }
    auto canonical_bytes =
        std::move(std::get<artifacts::ManifestEncoding>(encoded).bytes);
    if (canonical_bytes.empty()) {
        throw std::logic_error{
            "canonical P1.8 manifest encoder returned an empty document"};
    }

    return P18CompletedReferenceManifest{
        std::move(provenance),
        std::move(manifest),
        std::move(canonical_bytes),
    };
}

} // namespace engine_sim_offline::reference
