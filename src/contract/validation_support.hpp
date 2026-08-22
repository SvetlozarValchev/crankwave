#pragma once

#include "crankwave/contract/common.hpp"
#include "crankwave/contract/provenance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace crankwave::contract::detail {

inline void append_prefixed(ValidationReport &destination, ValidationReport source,
                            std::string_view prefix) {
    for (auto &issue : source.issues) {
        if (!prefix.empty()) {
            issue.path = issue.path.empty() ? std::string(prefix)
                                            : std::string(prefix) + "." + issue.path;
        }
        destination.issues.push_back(std::move(issue));
    }
}

inline void require(ValidationReport &report, bool condition, ContractIssueCode code,
                    std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

inline bool finite(double value) noexcept {
    return std::isfinite(value);
}

inline bool finite_nonnegative(double value) noexcept {
    return finite(value) && value >= 0.0;
}

inline bool finite_positive(double value) noexcept {
    return finite(value) && value > 0.0;
}

inline bool unit_interval(double value) noexcept {
    return finite(value) && value >= 0.0 && value <= 1.0;
}

inline bool nearly_equal(double lhs, double rhs, double absolute_tolerance = 1e-12,
                         double relative_ulps = 16.0) noexcept {
    if (!finite(lhs) || !finite(rhs)) {
        return false;
    }
    const auto scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    const auto tolerance =
        std::max(absolute_tolerance,
                 relative_ulps * std::numeric_limits<double>::epsilon() * scale);
    return std::abs(lhs - rhs) <= tolerance;
}

template <class Id> bool all_unique_valid_ids(std::span<const Id> ids) {
    std::unordered_set<std::uint32_t> seen;
    for (const auto id : ids) {
        if (!id.valid() || !seen.insert(id.value).second) {
            return false;
        }
    }
    return true;
}

inline bool has_claim(const ProvenanceLedger &ledger, std::string_view id) {
    return std::ranges::any_of(ledger.claims,
                               [id](const auto &claim) { return claim.id == id; });
}

inline bool has_resolution(const ProvenanceLedger &ledger, std::string_view id) {
    return std::ranges::any_of(ledger.resolutions, [id](const auto &resolution) {
        return resolution.id == id;
    });
}

inline const ResolutionRecord *find_resolution(const ProvenanceLedger &ledger,
                                               std::string_view id) {
    const auto found =
        std::ranges::find_if(ledger.resolutions, [id](const auto &resolution) {
            return resolution.id == id;
        });
    return found == ledger.resolutions.end() ? nullptr : &*found;
}

template <class T>
void validate_authored_value(ValidationReport &report, const AuthoredValue<T> &value,
                             const ProvenanceLedger &ledger, std::string path) {
    require(report, is_valid_semantic_id(value.claim_id),
            ContractIssueCode::invalid_value, path + ".claim_id",
            "claim ID must be a canonical semantic ID");
    require(report, has_claim(ledger, value.claim_id),
            ContractIssueCode::dangling_reference, path + ".claim_id",
            "claim ID is not present in the provenance ledger");
}

template <class T>
void validate_resolved_value(ValidationReport &report, const ResolvedValue<T> &value,
                             const ProvenanceLedger &ledger, std::string path) {
    require(report, is_valid_semantic_id(value.resolution_id),
            ContractIssueCode::invalid_value, path + ".resolution_id",
            "resolution ID must be a canonical semantic ID");
    const auto *resolution = find_resolution(ledger, value.resolution_id);
    require(report, resolution != nullptr, ContractIssueCode::dangling_reference,
            path + ".resolution_id",
            "resolution ID is not present in the provenance ledger");
    if (resolution != nullptr) {
        require(report, resolution->parameter_path == path,
                ContractIssueCode::inconsistent_semantics, path + ".resolution_id",
                "resolution ID belongs to a different resolved leaf");
    }
}

template <class Range, class Projection>
void require_unique_semantic_ids(ValidationReport &report, const Range &values,
                                 Projection projection, std::string path) {
    std::unordered_set<std::string> seen;
    for (const auto &value : values) {
        const auto &id = projection(value);
        if (!seen.insert(id).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "semantic IDs must be unique within their identity domain");
            return;
        }
    }
}

template <class Range, class Projection>
void require_unique_numeric_ids(ValidationReport &report, const Range &values,
                                Projection projection, std::string path) {
    std::unordered_set<std::uint32_t> seen;
    for (const auto &value : values) {
        const auto id = projection(value);
        if (!id.valid() || !seen.insert(id.value).second) {
            report.add(id.valid() ? ContractIssueCode::duplicate_identity
                                  : ContractIssueCode::invalid_value,
                       path, "stable numeric IDs must be nonzero and unique");
            return;
        }
    }
}

} // namespace crankwave::contract::detail
