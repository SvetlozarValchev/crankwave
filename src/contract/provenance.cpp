#include "engine_sim_offline/contract/provenance.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine_sim_offline::contract {
namespace {

bool known(ProvenanceOrigin origin) noexcept {
    switch (origin) {
    case ProvenanceOrigin::literature:
    case ProvenanceOrigin::bmw_official:
    case ProvenanceOrigin::measurement:
    case ProvenanceOrigin::inferred:
    case ProvenanceOrigin::reference_fixture:
    case ProvenanceOrigin::legacy_asset_unverified:
    case ProvenanceOrigin::derived:
    case ProvenanceOrigin::scenario:
    case ProvenanceOrigin::calibrated:
    case ProvenanceOrigin::calibrated_m3:
    case ProvenanceOrigin::artistic:
    case ProvenanceOrigin::authored_product_data:
        return true;
    case ProvenanceOrigin::unspecified:
        return false;
    }
    return false;
}

bool known(ResolutionMode mode) noexcept {
    switch (mode) {
    case ResolutionMode::authored:
    case ResolutionMode::declared_default:
    case ResolutionMode::derived:
    case ResolutionMode::inferred:
    case ResolutionMode::calibrated:
        return true;
    case ResolutionMode::unspecified:
        return false;
    }
    return false;
}

bool known(RightsDisposition rights) noexcept {
    switch (rights) {
    case RightsDisposition::permitted:
    case RightsDisposition::local_evaluation_only:
    case RightsDisposition::noassertion:
    case RightsDisposition::prohibited:
        return true;
    case RightsDisposition::unspecified:
        return false;
    }
    return false;
}

bool origin_requires_immutable_evidence(ProvenanceOrigin origin) noexcept {
    return origin == ProvenanceOrigin::measurement ||
           origin == ProvenanceOrigin::reference_fixture ||
           origin == ProvenanceOrigin::legacy_asset_unverified;
}

bool origin_requires_citation(ProvenanceOrigin origin) noexcept {
    return origin == ProvenanceOrigin::literature ||
           origin == ProvenanceOrigin::bmw_official ||
           origin == ProvenanceOrigin::measurement ||
           origin == ProvenanceOrigin::inferred ||
           origin == ProvenanceOrigin::reference_fixture ||
           origin == ProvenanceOrigin::legacy_asset_unverified ||
           origin == ProvenanceOrigin::calibrated ||
           origin == ProvenanceOrigin::calibrated_m3;
}

} // namespace

ValidationReport validate(const ProvenanceLedger &ledger) {
    using detail::append_prefixed;
    using detail::finite_nonnegative;
    using detail::require;

    ValidationReport report;
    require(report, is_valid_semantic_id(ledger.schema_id),
            ContractIssueCode::invalid_value, "schema_id",
            "provenance schema ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(ledger.bundle.id),
            ContractIssueCode::invalid_value, "bundle.id",
            "provenance bundle ID must be a canonical semantic ID");
    require(report, !ledger.bundle.sha256.is_zero(), ContractIssueCode::invalid_value,
            "bundle.sha256", "provenance bundle content digest must be nonzero");

    std::unordered_map<std::string, const EvidenceSource *> evidence_by_id;
    for (std::size_t index = 0; index < ledger.evidence.size(); ++index) {
        const auto &evidence = ledger.evidence[index];
        const auto path = "evidence[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(evidence.id),
                ContractIssueCode::invalid_value, path + ".id",
                "evidence ID must be a canonical semantic ID");
        if (!evidence_by_id.emplace(evidence.id, &evidence).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "evidence IDs must be unique");
        }
        require(report, !evidence.locator.empty(), ContractIssueCode::missing_value,
                path + ".locator", "evidence locator must be present");
        require(report, known(evidence.rights), ContractIssueCode::unsupported_value,
                path + ".rights",
                "evidence rights disposition must be recognized and explicit");
        if (evidence.content_sha256.has_value() && evidence.content_sha256->is_zero()) {
            report.add(ContractIssueCode::invalid_value, path + ".content_sha256",
                       "present evidence digest must be nonzero");
        }
    }

    std::unordered_map<std::string, const ProvenanceClaim *> claim_by_id;
    for (std::size_t index = 0; index < ledger.claims.size(); ++index) {
        const auto &claim = ledger.claims[index];
        const auto path = "claims[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(claim.id),
                ContractIssueCode::invalid_value, path + ".id",
                "claim ID must be a canonical semantic ID");
        if (!claim_by_id.emplace(claim.id, &claim).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "claim IDs must be unique");
        }
        require(report, known(claim.origin), ContractIssueCode::unsupported_value,
                path + ".origin", "provenance origin must be recognized and explicit");
        if (origin_requires_citation(claim.origin)) {
            require(report, !claim.citations.empty(), ContractIssueCode::missing_value,
                    path + ".citations", "this provenance origin requires evidence");
        }
        for (std::size_t citation_index = 0; citation_index < claim.citations.size();
             ++citation_index) {
            const auto &citation = claim.citations[citation_index];
            const auto citation_path =
                path + ".citations[" + std::to_string(citation_index) + "]";
            const auto evidence_it = evidence_by_id.find(citation.evidence_id);
            require(report, evidence_it != evidence_by_id.end(),
                    ContractIssueCode::dangling_reference,
                    citation_path + ".evidence_id",
                    "citation references unknown evidence");
            require(report, !citation.claim_locator.empty(),
                    ContractIssueCode::missing_value, citation_path + ".claim_locator",
                    "citation must locate the supported claim");
            if (evidence_it != evidence_by_id.end() &&
                origin_requires_immutable_evidence(claim.origin)) {
                require(report,
                        evidence_it->second->content_sha256.has_value() &&
                            !evidence_it->second->content_sha256->is_zero(),
                        ContractIssueCode::missing_value,
                        citation_path + ".evidence_id",
                        "this origin requires content-addressed evidence");
            }
        }
        if (claim.uncertainty.has_value()) {
            const auto &uncertainty = *claim.uncertainty;
            if (uncertainty.standard_uncertainty.has_value()) {
                require(report, finite_nonnegative(*uncertainty.standard_uncertainty),
                        ContractIssueCode::invalid_value,
                        path + ".uncertainty.standard_uncertainty",
                        "standard uncertainty must be finite and nonnegative");
            }
            require(report, !uncertainty.method.empty(),
                    ContractIssueCode::missing_value, path + ".uncertainty.method",
                    "an uncertainty statement must name its method");
        }
    }

    std::unordered_map<std::string, std::size_t> resolution_by_id;
    std::unordered_map<std::string, std::size_t> resolution_by_path;
    for (std::size_t index = 0; index < ledger.resolutions.size(); ++index) {
        const auto &resolution = ledger.resolutions[index];
        const auto path = "resolutions[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(resolution.id),
                ContractIssueCode::invalid_value, path + ".id",
                "resolution ID must be a canonical semantic ID");
        if (!resolution_by_id.emplace(resolution.id, index).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "resolution IDs must be unique");
        }
        require(report, is_valid_semantic_id(resolution.parameter_path),
                ContractIssueCode::invalid_value, path + ".parameter_path",
                "resolved parameter path must be canonical");
        require(report, known(resolution.mode), ContractIssueCode::unsupported_value,
                path + ".mode", "resolution mode must be recognized and explicit");
        if (!resolution_by_path.emplace(resolution.parameter_path, index).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".parameter_path",
                       "each resolved leaf must have exactly one resolution");
        }

        const auto claim_it = claim_by_id.find(resolution.claim_id);
        require(report, claim_it != claim_by_id.end(),
                ContractIssueCode::dangling_reference, path + ".claim_id",
                "resolution references an unknown provenance claim");
        if (resolution.method.has_value()) {
            append_prefixed(report, validate(*resolution.method), path + ".method");
        }

        const bool needs_method = resolution.mode == ResolutionMode::derived ||
                                  resolution.mode == ResolutionMode::inferred ||
                                  resolution.mode == ResolutionMode::calibrated;
        require(report, !needs_method || resolution.method.has_value(),
                ContractIssueCode::missing_value, path + ".method",
                "derived, inferred, and calibrated resolutions require a method");
        require(report,
                resolution.mode != ResolutionMode::derived ||
                    !resolution.dependency_parameter_paths.empty(),
                ContractIssueCode::missing_value, path + ".dependency_parameter_paths",
                "derived resolutions require explicit dependencies");
        if (claim_it != claim_by_id.end()) {
            const auto origin = claim_it->second->origin;
            require(report,
                    resolution.mode != ResolutionMode::derived ||
                        origin == ProvenanceOrigin::derived,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "a derived resolution must reference a derived claim");
            require(report,
                    resolution.mode != ResolutionMode::inferred ||
                        origin == ProvenanceOrigin::inferred,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "an inferred resolution must reference an inferred claim");
            require(report,
                    resolution.mode != ResolutionMode::calibrated ||
                        origin == ProvenanceOrigin::calibrated ||
                        origin == ProvenanceOrigin::calibrated_m3,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "a calibrated resolution must reference a calibrated claim");
            require(report,
                    origin != ProvenanceOrigin::derived ||
                        resolution.mode == ResolutionMode::derived,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "a derived claim requires a derived resolution");
            require(report,
                    origin != ProvenanceOrigin::inferred ||
                        resolution.mode == ResolutionMode::inferred,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "an inferred claim requires an inferred resolution");
            require(report,
                    (origin != ProvenanceOrigin::calibrated &&
                     origin != ProvenanceOrigin::calibrated_m3) ||
                        resolution.mode == ResolutionMode::calibrated,
                    ContractIssueCode::inconsistent_semantics, path + ".mode",
                    "a calibrated claim requires a calibrated resolution");
        }
    }

    std::vector<std::vector<std::size_t>> dependencies(ledger.resolutions.size());
    for (std::size_t index = 0; index < ledger.resolutions.size(); ++index) {
        for (const auto &dependency :
             ledger.resolutions[index].dependency_parameter_paths) {
            const auto dependency_it = resolution_by_path.find(dependency);
            if (dependency_it == resolution_by_path.end()) {
                report.add(ContractIssueCode::dangling_reference,
                           "resolutions[" + std::to_string(index) +
                               "].dependency_parameter_paths",
                           "dependency does not identify a resolved parameter");
            } else {
                dependencies[index].push_back(dependency_it->second);
            }
        }
    }

    enum class Visit : std::uint8_t {
        unvisited,
        active,
        complete,
    };
    std::vector<Visit> visits(ledger.resolutions.size(), Visit::unvisited);
    bool cycle_found = false;
    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        if (visits[index] == Visit::active) {
            cycle_found = true;
            return;
        }
        if (visits[index] == Visit::complete) {
            return;
        }
        visits[index] = Visit::active;
        for (const auto dependency : dependencies[index]) {
            visit(dependency);
        }
        visits[index] = Visit::complete;
    };
    for (std::size_t index = 0; index < visits.size(); ++index) {
        visit(index);
    }
    if (cycle_found) {
        report.add(ContractIssueCode::inconsistent_semantics, "resolutions",
                   "resolution dependency graph must be acyclic");
    }

    return report;
}

} // namespace engine_sim_offline::contract
