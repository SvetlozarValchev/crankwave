#pragma once

#include "engine_sim_offline/contract/render_manifest.hpp"
#include "execution/linux_execution_facts.hpp"
#include "reference/p18_reference_manifest_content.hpp"
#include "reference/p18_reference_provenance.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {

// One semantically validated, canonically encoded P1.8 manifest. This value owns the
// exact provenance ledger used for complete-manifest validation and retains the wire
// bytes in memory; it does not publish either the document or its sidecar.
class P18CompletedReferenceManifest final {
  public:
    P18CompletedReferenceManifest(const P18CompletedReferenceManifest &) = default;
    P18CompletedReferenceManifest(P18CompletedReferenceManifest &&) noexcept = default;
    P18CompletedReferenceManifest &
    operator=(const P18CompletedReferenceManifest &) = delete;
    P18CompletedReferenceManifest &
    operator=(P18CompletedReferenceManifest &&) noexcept = delete;

    [[nodiscard]] const contract::RenderManifest &manifest() const noexcept;
    [[nodiscard]] const P18ReferenceProvenance &provenance() const noexcept;
    [[nodiscard]] std::span<const std::byte> canonical_bytes() const noexcept;

  private:
    P18CompletedReferenceManifest(P18ReferenceProvenance provenance,
                                  contract::RenderManifest manifest,
                                  std::vector<std::byte> canonical_bytes)
        : provenance_(std::move(provenance)), manifest_(std::move(manifest)),
          canonical_bytes_(std::move(canonical_bytes)) {}

    P18ReferenceProvenance provenance_;
    contract::RenderManifest manifest_;
    std::vector<std::byte> canonical_bytes_;

    friend P18CompletedReferenceManifest
    complete_p18_reference_manifest(const P18ReferenceManifestContent &content,
                                    const execution::ObservedExecutionFacts &execution);
};

// Completes the already validated deterministic content with facts sealed by the
// zero-argument single-job Linux observer. It revalidates the complete typed manifest
// against the retained provenance and exact BMW source matrix before invoking the
// canonical v2 encoder. Pure observer seams and raw ExecutionFacts cannot establish
// the required private-construction type. Throws on semantic or wire failure.
[[nodiscard]] P18CompletedReferenceManifest
complete_p18_reference_manifest(const P18ReferenceManifestContent &content,
                                const execution::ObservedExecutionFacts &execution);

} // namespace engine_sim_offline::reference
