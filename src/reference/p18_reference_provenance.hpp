#pragma once

#include "engine_sim_offline/contract/provenance.hpp"
#include "reference/p18_reference_fixture_loader.hpp"

#include <string_view>
#include <utility>

namespace engine_sim_offline::reference {

// A sealed provenance ledger for the frozen trace-driven reference route. The only
// production factory accepts verified observed lineage, not catalog expectation
// records, so expected comparator identities cannot be substituted at this boundary.
class P18ReferenceProvenance {
  public:
    P18ReferenceProvenance(const P18ReferenceProvenance &) = default;
    P18ReferenceProvenance(P18ReferenceProvenance &&) noexcept = default;
    P18ReferenceProvenance &operator=(const P18ReferenceProvenance &) = delete;
    P18ReferenceProvenance &operator=(P18ReferenceProvenance &&) noexcept = delete;

    [[nodiscard]] const contract::ProvenanceLedger &ledger() const noexcept;
    [[nodiscard]] std::string_view resolution_id(std::string_view parameter_path) const;

  private:
    explicit P18ReferenceProvenance(contract::ProvenanceLedger ledger)
        : ledger_(std::move(ledger)) {}

    contract::ProvenanceLedger ledger_;

    friend P18ReferenceProvenance
    make_p18_reference_provenance(const P18VerifiedReferenceLineage &lineage);
};

// Canonical SHA-256 of every ledger field except bundle.sha256 itself. This function
// is public only inside the private reference target so focused tests can recompute
// the self-digest from the sealed ledger.
[[nodiscard]] contract::Sha256Digest canonical_p18_reference_provenance_digest(
    const contract::ProvenanceLedger &ledger) noexcept;

[[nodiscard]] P18ReferenceProvenance
make_p18_reference_provenance(const P18VerifiedReferenceLineage &lineage);

} // namespace engine_sim_offline::reference
