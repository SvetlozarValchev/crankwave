#include "compile/compiled_model_builder.hpp"

#include "compile/compiled_model_storage.hpp"
#include "compile/diagnostics.hpp"
#include "presentation/presentation_calibration_compiler.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail {
namespace {

[[nodiscard]] bool known(const AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::audio:
    case AssetKind::accessory_configuration:
        return true;
    }
    return false;
}

[[nodiscard]] authoring::DiagnosticReport
validate_provenance(const contract::ProvenanceLedger &provenance) {
    const auto validation = contract::validate(provenance);
    if (validation.ok()) {
        return {};
    }
    const auto &issue = validation.issues.front();
    return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                      "compiler provenance is invalid at '" + issue.path +
                          "': " + issue.message);
}

[[nodiscard]] authoring::DiagnosticReport
validate_assignments(const std::vector<StableIdAssignment> &assignments) {
    std::string_view current_namespace;
    std::string_view previous_id;
    RuntimeObjectId expected_runtime_id = 0;
    for (const auto &assignment : assignments) {
        if (expected_runtime_id == 0U ||
            assignment.object_namespace != current_namespace) {
            if (!contract::is_valid_semantic_id(assignment.object_namespace)) {
                return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                                  "compiled runtime-ID namespace is not canonical");
            }
            current_namespace = assignment.object_namespace;
            previous_id = {};
            expected_runtime_id = 1U;
        } else {
            ++expected_runtime_id;
        }
        if (assignment.runtime_id != expected_runtime_id ||
            assignment.authored_id.empty() ||
            (!previous_id.empty() && assignment.authored_id <= previous_id)) {
            return diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "compiled runtime-ID assignments are not canonical and dense");
        }
        previous_id = assignment.authored_id;
    }
    return {};
}

[[nodiscard]] authoring::DiagnosticReport
validate_assets(const std::vector<VerifiedEngineAsset> &assets) {
    for (std::size_t index = 0; index < assets.size(); ++index) {
        const auto &asset = assets[index];
        if (!known(asset.kind) || asset.asset_id.empty()) {
            return diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "resolved asset kind and stable identity must be valid");
        }
        if (asset.bytes.empty()) {
            return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                              "resolved asset payload contains no bytes");
        }
        if (contract::sha256(asset.bytes) != asset.content_sha256) {
            return diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "resolved asset content digest disagrees with its owned bytes");
        }
        if (index != 0U &&
            std::tie(assets[index - 1U].kind, assets[index - 1U].asset_id) >=
                std::tie(asset.kind, asset.asset_id)) {
            return diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "resolved assets are not in canonical unique kind-and-ID order");
        }
    }
    return {};
}

[[nodiscard]] authoring::DiagnosticCode
diagnostic_code(const contract::ContractIssueCode code) noexcept {
    using enum authoring::DiagnosticCode;
    switch (code) {
    case contract::ContractIssueCode::missing_value:
        return missing_value;
    case contract::ContractIssueCode::invalid_value:
        return invalid_value;
    case contract::ContractIssueCode::duplicate_identity:
        return duplicate_id;
    case contract::ContractIssueCode::dangling_reference:
        return dangling_reference;
    case contract::ContractIssueCode::inconsistent_shape:
    case contract::ContractIssueCode::inconsistent_semantics:
        return inconsistent_value;
    case contract::ContractIssueCode::unsupported_value:
        return unsupported_capability;
    }
    return internal_failure;
}

[[nodiscard]] std::string json_pointer(std::string_view path) {
    if (path.starts_with("scenario.")) {
        path.remove_prefix(std::string_view{"scenario."}.size());
    }

    std::string result;
    result.reserve(path.size() + 1U);
    result.push_back('/');
    for (const char byte : path) {
        if (byte == '.' || byte == '[') {
            result.push_back('/');
        } else if (byte != ']') {
            result.push_back(byte);
        }
    }
    constexpr std::string_view resolved_value = "/value";
    std::size_t offset = 0;
    while ((offset = result.find(resolved_value, offset)) != std::string::npos) {
        const auto after = offset + resolved_value.size();
        if (after == result.size() || result[after] == '/') {
            result.erase(offset, resolved_value.size());
        } else {
            offset = after;
        }
    }
    return result == "/" ? std::string{} : result;
}

[[nodiscard]] authoring::DiagnosticReport presentation_admission_report(
    const presentation::PresentationCalibrationCompileError &error) {
    authoring::DiagnosticReport report;
    if (error.validation.issues.empty()) {
        return internal_failure(
            "converting an empty presentation-admission failure");
    }
    report.diagnostics.reserve(error.validation.issues.size());
    for (const auto &issue : error.validation.issues) {
        authoring::Diagnostic value;
        value.code = diagnostic_code(issue.code);
        value.json_pointer = json_pointer(issue.path);
        value.message = issue.message;
        report.diagnostics.push_back(std::move(value));
    }
    return report;
}

} // namespace

EngineCompileResult
CompiledEngineBuilder::build(ResolvedEnginePackage resolved) noexcept {
    try {
        const auto &engine_id = resolved.engine.engine_id.value;
        if (engine_id.empty()) {
            return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                              "compiled engine identity is empty");
        }
        std::ranges::sort(
            resolved.stable_id_assignments,
            [](const auto &left, const auto &right) {
                return std::tie(left.object_namespace, left.authored_id) <
                       std::tie(right.object_namespace, right.authored_id);
            });
        if (auto report =
                validate_assignments(resolved.stable_id_assignments);
            !report.ok()) {
            return report;
        }
        if (auto report = validate_provenance(resolved.provenance); !report.ok()) {
            return report;
        }
        if (auto report = validate_assets(resolved.assets); !report.ok()) {
            return report;
        }

        auto storage = std::make_shared<CompiledEngineStorage>();
        storage->id = engine_id;
        storage->resolved = std::move(resolved);
        return CompiledEngine{std::move(storage)};
    } catch (const std::bad_alloc &) {
        return resource_failure("building compiled-engine ownership");
    } catch (...) {
        return internal_failure("building compiled-engine ownership");
    }
}

ScenarioCompileResult
CompiledScenarioBuilder::compile(
    const CompiledEngine &engine,
    const authoring::ScenarioDocument &document) noexcept {
    try {
        if (!engine.storage_) {
            return diagnostic(authoring::DiagnosticCode::invalid_value, "",
                              "compiled engine handle is empty");
        }
        const auto &resolved_engine = engine.storage_->resolved;
        const ScenarioResolverContext context{
            resolved_engine.engine,
            resolved_engine.rig ? &*resolved_engine.rig : nullptr,
            resolved_engine.presentation,
            resolved_engine.randomness,
            resolved_engine.provenance,
            resolved_engine.fuels,
            resolved_engine.audio_buses,
            {},
            contract::DistributionIntent::local_evaluation,
            {},
        };
        auto result = resolve_scenario_document(document, context);
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
            return std::move(*report);
        }
        auto resolved =
            std::get<ResolvedScenarioContracts>(std::move(result));
        auto presentation_result =
            presentation::compile_presentation_calibration(
                resolved_engine.presentation, resolved_engine.engine,
                resolved.scenario, resolved.combined_provenance);
        if (const auto *error =
                std::get_if<presentation::PresentationCalibrationCompileError>(
                    &presentation_result)) {
            return presentation_admission_report(*error);
        }
        return build(engine, std::move(resolved));
    } catch (const std::bad_alloc &) {
        return resource_failure("admitting a compiled scenario");
    } catch (...) {
        return internal_failure("admitting a compiled scenario");
    }
}

ScenarioCompileResult
CompiledScenarioBuilder::build(
    const CompiledEngine &engine, ResolvedScenarioContracts resolved) noexcept {
    try {
        if (!engine.storage_) {
            return diagnostic(authoring::DiagnosticCode::invalid_value, "",
                              "compiled engine handle is empty");
        }
        const auto &scenario_id = resolved.scenario.scenario_id;
        if (scenario_id.empty()) {
            return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                              "compiled scenario identity is empty");
        }
        std::ranges::sort(
            resolved.stable_id_assignments,
            [](const auto &left, const auto &right) {
                return std::tie(left.object_namespace, left.authored_id) <
                       std::tie(right.object_namespace, right.authored_id);
            });
        if (auto report =
                validate_assignments(resolved.stable_id_assignments);
            !report.ok()) {
            return report;
        }
        if (auto report = validate_provenance(resolved.combined_provenance);
            !report.ok()) {
            return report;
        }

        auto storage = std::make_shared<CompiledScenarioStorage>();
        storage->id = scenario_id;
        storage->engine = engine.storage_;
        storage->resolved = std::move(resolved);
        return CompiledScenario{std::move(storage)};
    } catch (const std::bad_alloc &) {
        return resource_failure("building compiled-scenario ownership");
    } catch (...) {
        return internal_failure("building compiled-scenario ownership");
    }
}

} // namespace engine_sim_offline::compile::detail
