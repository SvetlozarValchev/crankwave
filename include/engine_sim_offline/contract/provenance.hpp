#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::contract {

enum class ProvenanceOrigin : std::uint8_t {
    unspecified,
    literature,
    bmw_official,
    measurement,
    inferred,
    reference_fixture,
    legacy_asset_unverified,
    derived,
    scenario,
    calibrated,
    calibrated_m3,
    artistic,
};

enum class ResolutionMode : std::uint8_t {
    unspecified,
    authored,
    declared_default,
    derived,
    inferred,
    calibrated,
};

enum class RightsDisposition : std::uint8_t {
    unspecified,
    permitted,
    local_evaluation_only,
    noassertion,
    prohibited,
};

struct ProvenanceBundleRef {
    std::string id;
    Sha256Digest sha256;

    friend bool operator==(const ProvenanceBundleRef &,
                           const ProvenanceBundleRef &) = default;
};

struct EvidenceSource {
    std::string id;
    std::string locator;
    std::optional<std::string> revision;
    std::optional<Sha256Digest> content_sha256;
    RightsDisposition rights = RightsDisposition::unspecified;

    friend bool operator==(const EvidenceSource &, const EvidenceSource &) = default;
};

struct EvidenceCitation {
    std::string evidence_id;
    std::string claim_locator;

    friend bool operator==(const EvidenceCitation &,
                           const EvidenceCitation &) = default;
};

struct UncertaintyStatement {
    std::optional<double> standard_uncertainty;
    std::string method;

    friend bool operator==(const UncertaintyStatement &,
                           const UncertaintyStatement &) = default;
};

struct ProvenanceClaim {
    std::string id;
    ProvenanceOrigin origin = ProvenanceOrigin::unspecified;
    std::vector<EvidenceCitation> citations;
    std::optional<UncertaintyStatement> uncertainty;

    friend bool operator==(const ProvenanceClaim &, const ProvenanceClaim &) = default;
};

struct ResolutionRecord {
    std::string id;
    std::string parameter_path;
    ResolutionMode mode = ResolutionMode::unspecified;
    std::string claim_id;
    std::optional<MethodIdentity> method;
    std::vector<std::string> dependency_parameter_paths;

    friend bool operator==(const ResolutionRecord &,
                           const ResolutionRecord &) = default;
};

struct ProvenanceLedger {
    std::string schema_id;
    ProvenanceBundleRef bundle;
    std::vector<EvidenceSource> evidence;
    std::vector<ProvenanceClaim> claims;
    std::vector<ResolutionRecord> resolutions;

    friend bool operator==(const ProvenanceLedger &,
                           const ProvenanceLedger &) = default;
};

template <class T> struct AuthoredValue {
    T value{};
    std::string claim_id;

    friend bool operator==(const AuthoredValue &, const AuthoredValue &) = default;
};

template <class T> struct ResolvedValue {
    T value{};
    std::string resolution_id;

    friend bool operator==(const ResolvedValue &, const ResolvedValue &) = default;
};

[[nodiscard]] ValidationReport validate(const ProvenanceLedger &ledger);

// Canonical SHA-256 of every ledger field except bundle.sha256 itself. The caller's
// grammar ID is the first length-prefixed field and therefore separates independently
// versioned ledger domains without duplicating the byte grammar.
[[nodiscard]] Sha256Digest
canonical_provenance_ledger_digest(const ProvenanceLedger &ledger,
                                   std::string_view grammar_id) noexcept;

} // namespace engine_sim_offline::contract
