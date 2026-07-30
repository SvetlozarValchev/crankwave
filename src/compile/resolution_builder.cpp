#include "compile/resolution_builder.hpp"

#include "compile/diagnostics.hpp"

#include <algorithm>
#include <new>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::compile::detail {
namespace {

constexpr std::string_view kProvenanceSchema =
    "engine-sim-offline.compiler-resolution-provenance";
constexpr std::string_view kProvenanceDigestGrammar =
    "engine-sim-offline.compiler-resolution-provenance-digest";

[[nodiscard]] std::string scoped_id(std::string_view scope, std::string_view suffix) {
    return "compiler." + std::string{scope} + "." + std::string{suffix};
}

} // namespace

ResolutionProvenanceBuilder::ResolutionProvenanceBuilder(
    std::string scope, contract::ProvenanceLedger base)
    : scope_(std::move(scope)), base_(std::move(base)) {}

void ResolutionProvenanceBuilder::add_authored(std::string resolved_parameter_path) {
    resolutions_.push_back({
        std::move(resolved_parameter_path),
        contract::ResolutionMode::authored,
        std::nullopt,
        {},
    });
}

void ResolutionProvenanceBuilder::add_declared_default(
    std::string resolved_parameter_path) {
    resolutions_.push_back({
        std::move(resolved_parameter_path),
        contract::ResolutionMode::declared_default,
        std::nullopt,
        {},
    });
}

void ResolutionProvenanceBuilder::add_derived(
    std::string resolved_parameter_path, contract::MethodIdentity method,
    std::span<const std::string_view> dependency_parameter_paths) {
    std::vector<std::string> dependencies;
    dependencies.reserve(dependency_parameter_paths.size());
    for (const auto dependency : dependency_parameter_paths) {
        dependencies.emplace_back(dependency);
    }
    std::ranges::sort(dependencies);
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    resolutions_.push_back({
        std::move(resolved_parameter_path),
        contract::ResolutionMode::derived,
        std::move(method),
        std::move(dependencies),
    });
}

ProvenanceBuildResult ResolutionProvenanceBuilder::finish() && noexcept {
    try {
        if (!contract::is_valid_semantic_id(scope_)) {
            return diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "compiler provenance scope is not a canonical semantic ID");
        }
        const bool has_base = !base_.schema_id.empty();
        if (has_base) {
            const auto base_validation = contract::validate(base_);
            if (!base_validation.ok() || base_.schema_id != kProvenanceSchema) {
                return diagnostic(
                    authoring::DiagnosticCode::internal_failure, "",
                    "compiler provenance base is invalid or belongs to another "
                    "schema");
            }
        }
        std::ranges::sort(
            resolutions_, {},
            &ResolutionProvenanceBuilder::PendingResolution::parameter_path);
        for (std::size_t index = 0; index < resolutions_.size(); ++index) {
            const auto &resolution = resolutions_[index];
            if (!contract::is_valid_semantic_id(resolution.parameter_path)) {
                return diagnostic(
                    authoring::DiagnosticCode::internal_failure, "",
                    "compiler produced a noncanonical resolved parameter path '" +
                        resolution.parameter_path + "'");
            }
            if (index != 0U &&
                resolutions_[index - 1U].parameter_path == resolution.parameter_path) {
                return diagnostic(
                    authoring::DiagnosticCode::internal_failure, "",
                    "compiler produced duplicate resolution for parameter path '" +
                        resolution.parameter_path + "'");
            }
            const bool duplicates_base =
                std::ranges::any_of(base_.resolutions, [&](const auto &existing) {
                    return existing.parameter_path == resolution.parameter_path;
                });
            if (duplicates_base) {
                return diagnostic(
                    authoring::DiagnosticCode::internal_failure, "",
                    "compiler produced duplicate cross-scope resolution for "
                    "parameter path '" +
                        resolution.parameter_path + "'");
            }
        }

        contract::ProvenanceLedger ledger = std::move(base_);
        ledger.schema_id = kProvenanceSchema;
        ledger.bundle.id = scoped_id(scope_, has_base ? "combined-provenance"
                                                      : "generated-provenance");
        const auto authored_claim = scoped_id(scope_, "authored-product-data");
        const auto declared_default_claim = scoped_id(scope_, "declared-default");
        const auto derived_claim = scoped_id(scope_, "derived");

        const bool has_authored =
            std::ranges::any_of(resolutions_, [](const auto &resolution) {
                return resolution.mode == contract::ResolutionMode::authored;
            });
        const bool has_derived =
            std::ranges::any_of(resolutions_, [](const auto &resolution) {
                return resolution.mode == contract::ResolutionMode::derived;
            });
        const bool has_declared_default =
            std::ranges::any_of(resolutions_, [](const auto &resolution) {
                return resolution.mode == contract::ResolutionMode::declared_default;
            });
        if (has_authored) {
            ledger.claims.push_back({
                authored_claim,
                contract::ProvenanceOrigin::authored_product_data,
                {},
                std::nullopt,
            });
        }
        if (has_declared_default) {
            ledger.claims.push_back({
                declared_default_claim,
                contract::ProvenanceOrigin::scenario,
                {},
                std::nullopt,
            });
        }
        if (has_derived) {
            ledger.claims.push_back({
                derived_claim,
                contract::ProvenanceOrigin::derived,
                {},
                std::nullopt,
            });
        }

        ledger.resolutions.reserve(ledger.resolutions.size() + resolutions_.size());
        for (std::size_t index = 0; index < resolutions_.size(); ++index) {
            auto &pending = resolutions_[index];
            ledger.resolutions.push_back({
                scoped_id(scope_, "resolution." + std::to_string(index + 1U)),
                std::move(pending.parameter_path),
                pending.mode,
                pending.mode == contract::ResolutionMode::authored ? authored_claim
                : pending.mode == contract::ResolutionMode::declared_default
                    ? declared_default_claim
                    : derived_claim,
                std::move(pending.method),
                std::move(pending.dependency_parameter_paths),
            });
        }
        ledger.bundle.sha256 = contract::canonical_provenance_ledger_digest(
            ledger, std::string{kProvenanceDigestGrammar} + "." + scope_);
        const auto validation = contract::validate(ledger);
        if (!validation.ok()) {
            const auto &issue = validation.issues.front();
            return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                              "compiler-generated provenance failed validation at '" +
                                  issue.path + "': " + issue.message);
        }
        return ledger;
    } catch (const std::bad_alloc &) {
        return resource_failure("building compiler provenance");
    } catch (...) {
        return internal_failure("building compiler provenance");
    }
}

} // namespace engine_sim_offline::compile::detail
