#include "engine_sim_offline/render.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline {
namespace {

using contract::ContractIssueCode;
using contract::FailureContext;
using contract::FailureKind;
using contract::RenderFailure;
using contract::RenderResult;
using contract::ValidationReport;

void append_prefixed(ValidationReport &destination, ValidationReport source,
                     std::string_view prefix) {
    for (auto &issue : source.issues) {
        issue.path = issue.path.empty() ? std::string(prefix)
                                        : std::string(prefix) + "." + issue.path;
        destination.issues.push_back(std::move(issue));
    }
}

ValidationReport validate_structure(const RenderSpecification &specification,
                                    const contract::RenderScenario &scenario) {
    ValidationReport report;
    append_prefixed(report,
                    contract::validate_render_admission(
                        specification.engine, specification.presentation, scenario,
                        specification.provenance, specification.source_matrix),
                    "specification");

    std::unordered_set<std::uint32_t> payload_ids;
    for (std::size_t index = 0; index < specification.asset_payloads.size(); ++index) {
        const auto &payload = specification.asset_payloads[index];
        const auto path = "specification.asset_payloads[" + std::to_string(index) + "]";
        if (!payload.id.valid()) {
            report.add(ContractIssueCode::invalid_value, path + ".id",
                       "asset payload ID must be nonzero");
        }
        if (!payload_ids.insert(payload.id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "asset payload IDs must be unique");
        }
        const auto asset = std::ranges::find(specification.presentation.assets,
                                             payload.id, &contract::AudioAssetSpec::id);
        if (asset == specification.presentation.assets.end()) {
            report.add(ContractIssueCode::dangling_reference, path + ".id",
                       "asset payload is absent from the presentation calibration");
            continue;
        }
        if (payload.bytes.empty()) {
            report.add(ContractIssueCode::missing_value, path + ".bytes",
                       "asset payload bytes must be present");
        } else if (contract::sha256(payload.bytes) != asset->content_sha256.value) {
            report.add(ContractIssueCode::inconsistent_semantics, path + ".bytes",
                       "asset payload SHA-256 must match the presentation "
                       "calibration");
        }
    }
    for (const auto &asset : specification.presentation.assets) {
        if (!payload_ids.contains(asset.id.value)) {
            report.add(ContractIssueCode::missing_value, "specification.asset_payloads",
                       "every presentation asset requires one content-addressed "
                       "payload");
        }
    }
    return report;
}

ValidationReport validate_rights(const RenderSpecification &specification) {
    auto report = contract::validate_evidence_rights(
        specification.provenance, specification.source_matrix.distribution);
    for (auto &issue : report.issues) {
        issue.path = "specification.provenance." + issue.path;
    }
    return report;
}

std::vector<contract::AssetPayloadIdentity>
asset_payload_identities(const RenderSpecification &specification) {
    std::vector<contract::AssetPayloadIdentity> identities;
    identities.reserve(specification.asset_payloads.size());
    for (const auto &payload : specification.asset_payloads) {
        identities.push_back({
            payload.id,
            static_cast<std::uint64_t>(payload.bytes.size()),
            contract::sha256(payload.bytes),
        });
    }
    std::ranges::sort(identities, [](const auto &lhs, const auto &rhs) {
        if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
        }
        if (lhs.byte_count != rhs.byte_count) {
            return lhs.byte_count < rhs.byte_count;
        }
        return lhs.payload_sha256.bytes < rhs.payload_sha256.bytes;
    });
    return identities;
}

contract::RenderRequestRecord
make_request_record(const RenderSpecification &specification,
                    const contract::RenderScenario &scenario) {
    return {
        {specification.engine, specification.presentation, scenario},
        specification.provenance,
        specification.source_matrix,
        asset_payload_identities(specification),
    };
}

void require(ValidationReport &report, bool condition, std::string path,
             std::string message) {
    if (!condition) {
        report.add(ContractIssueCode::inconsistent_semantics, std::move(path),
                   std::move(message));
    }
}

std::string failure_profile_id(const contract::RenderScenario &scenario) {
    if (contract::is_valid_semantic_id(scenario.engine_profile_id)) {
        return scenario.engine_profile_id;
    }
    return "unresolved";
}

RenderResult reject(FailureKind kind, std::string detail_code, std::string model_id,
                    std::string state_summary, const RenderSpecification &specification,
                    const contract::RenderScenario &scenario,
                    ValidationReport validation = {}) {
    FailureContext context;
    context.kind = kind;
    context.detail_code = std::move(detail_code);
    context.model_id = std::move(model_id);
    context.profile_id = failure_profile_id(scenario);
    if (specification.engine.id.valid()) {
        context.engine_id = specification.engine.id;
    }
    context.state_summary = std::move(state_summary);
    context.attempted_recovery =
        "none; render failed closed and generated no fallback output";
    return RenderFailure{
        std::move(context),
        make_request_record(specification, scenario),
        std::move(validation),
    };
}

} // namespace

contract::RenderResult render(const RenderSpecification &specification,
                              const contract::RenderScenario &scenario,
                              RenderSink &sink, RenderControl control) {
    // The sink is intentionally unused until all admission checks pass and a
    // concrete execution route exists. Keeping the name documents that this is the
    // public render boundary rather than a validation-only overload.
    (void)sink;

    auto structural = validate_structure(specification, scenario);
    if (!structural.ok()) {
        return reject(FailureKind::invalid_specification, "render-preflight-invalid",
                      "render-preflight-v1",
                      "resolved render specification failed structural preflight",
                      specification, scenario, std::move(structural));
    }

    auto rights = validate_rights(specification);
    if (!rights.ok()) {
        return reject(FailureKind::evidence_rights_failure,
                      "render-evidence-not-admitted", "render-preflight-v1",
                      "selected output policy is incompatible with evidence rights",
                      specification, scenario, std::move(rights));
    }

    if (control.stop_token.stop_requested()) {
        return reject(FailureKind::cancelled, "render-cancelled-before-execution",
                      "render-session-v1",
                      "cancellation was observed at the pre-execution block boundary",
                      specification, scenario);
    }

    return reject(
        FailureKind::incomplete_source_route, "render-pipeline-not-admitted",
        "render-session-v1",
        "preflight passed but no complete capture-to-artifact execution route is "
        "admitted in this build",
        specification, scenario);
}

contract::ValidationReport validate(const contract::RenderResult &result,
                                    const RenderSpecification &specification,
                                    const contract::RenderScenario &scenario) {
    ValidationReport report;
    append_prefixed(report,
                    contract::validate(result, scenario, specification.provenance,
                                       specification.source_matrix),
                    "contract");

    const auto structural = validate_structure(specification, scenario);
    const auto rights = validate_rights(specification);
    std::visit(
        [&](const auto &outcome) {
            using Outcome = std::decay_t<decltype(outcome)>;
            if constexpr (std::is_same_v<Outcome, contract::RenderFailure> ||
                          std::is_same_v<Outcome, contract::UnreachableTarget>) {
                constexpr auto outcome_path =
                    std::is_same_v<Outcome, contract::RenderFailure> ? "failure"
                                                                     : "unreachable";
                require(report,
                        outcome.request.resolved_inputs.engine == specification.engine,
                        std::string(outcome_path) + ".request.resolved_inputs.engine",
                        std::string(outcome_path) +
                            " result must retain the exact requested engine");
                require(report,
                        outcome.request.resolved_inputs.presentation ==
                            specification.presentation,
                        std::string(outcome_path) +
                            ".request.resolved_inputs.presentation",
                        std::string(outcome_path) +
                            " result must retain the exact requested presentation");
                require(report,
                        outcome.request.asset_payloads ==
                            asset_payload_identities(specification),
                        std::string(outcome_path) + ".request.asset_payloads",
                        std::string(outcome_path) +
                            " result must retain the exact requested asset identities");

                if constexpr (std::is_same_v<Outcome, contract::RenderFailure>) {
                    if (outcome.context.kind == FailureKind::invalid_specification) {
                        require(report, !structural.ok(), "failure.context.kind",
                                "invalid-specification failure requires structural "
                                "preflight rejection");
                        require(report, outcome.validation.issues == structural.issues,
                                "failure.validation",
                                "invalid-specification failure must retain the exact "
                                "preflight diagnostics");
                    } else if (outcome.context.kind ==
                               FailureKind::evidence_rights_failure) {
                        require(
                            report, structural.ok(), "failure.context.kind",
                            "evidence-rights failure requires successful structural "
                            "preflight");
                        require(
                            report, !rights.ok(), "failure.context.kind",
                            "evidence-rights failure requires inadmissible evidence");
                        require(
                            report, outcome.validation.issues == rights.issues,
                            "failure.validation",
                            "evidence-rights failure must retain exact diagnostics");
                    } else {
                        require(report, structural.ok(), "failure.context.kind",
                                "runtime failure requires successful structural "
                                "preflight");
                        require(report, rights.ok(), "failure.context.kind",
                                "runtime failure requires admitted evidence rights");
                        require(report, outcome.validation.ok(), "failure.validation",
                                "runtime failure cannot carry preflight diagnostics");
                    }
                } else {
                    require(report, structural.ok(), "unreachable",
                            "unreachable result requires successful structural "
                            "preflight");
                    require(report, rights.ok(), "unreachable",
                            "unreachable result requires admitted evidence rights");
                }
            } else if constexpr (std::is_same_v<Outcome, contract::RenderSuccess>) {
                require(report, structural.ok(), "success",
                        "render success requires successful structural preflight");
                require(report, rights.ok(), "success",
                        "render success requires admitted evidence rights");
                const auto &simulation_inputs = outcome.manifest.content.inputs;
                require(report,
                        simulation_inputs.resolved.engine == specification.engine,
                        "success.manifest.content.inputs.simulation.engine",
                        "success manifest must retain the exact requested engine");
                require(
                    report,
                    simulation_inputs.resolved.presentation ==
                        specification.presentation,
                    "success.manifest.content.inputs.simulation.presentation",
                    "success manifest must retain the exact requested presentation");
            }
        },
        result);
    return report;
}

} // namespace engine_sim_offline
