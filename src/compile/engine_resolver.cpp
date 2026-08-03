#include "compile/engine_resolver_internal.hpp"

#include "compile/diagnostics.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace engine_sim_offline::compile::detail {
namespace {

[[nodiscard]] authoring::DiagnosticReport
contract_failure(const contract::ValidationReport &validation,
                 std::string_view contract_name) {
    authoring::DiagnosticReport report;
    for (const auto &issue : validation.issues) {
        authoring::Diagnostic diagnostic_value;
        diagnostic_value.code = authoring::DiagnosticCode::internal_failure;
        diagnostic_value.message =
            std::string{contract_name} + " failed resolved validation at '" +
            issue.path + "': " + issue.message;
        report.diagnostics.push_back(std::move(diagnostic_value));
    }
    return report;
}

} // namespace

EngineResolutionResult resolve_engine_package(
    const authoring::EnginePackageDocument &document,
    std::span<const AssetPayloadView> assets) noexcept {
    using namespace engine_resolution;
    try {
        std::optional<ModelContext> context;
        auto admission = admit_engine_document(document, assets, context);
        if (!admission.ok() || !context.has_value()) {
            return admission;
        }

        ResolutionProvenanceBuilder provenance_builder{"engine"};
        ResolutionEmitter declaration_pass{provenance_builder};
        static_cast<void>(assemble_contracts(*context, declaration_pass));

        auto provenance_result = std::move(provenance_builder).finish();
        if (const auto *report =
                std::get_if<authoring::DiagnosticReport>(&provenance_result)) {
            return *report;
        }
        auto provenance =
            std::get<contract::ProvenanceLedger>(std::move(provenance_result));
        attach_asset_evidence(provenance, context->assets.values);
        const auto provenance_validation = contract::validate(provenance);
        if (!provenance_validation.ok()) {
            return contract_failure(provenance_validation,
                                    "engine provenance");
        }

        ResolutionEmitter final_pass{provenance};
        auto contracts = assemble_contracts(*context, final_pass);
        contracts.engine.provenance_schema_id = provenance.schema_id;
        contracts.presentation.provenance_schema_id = provenance.schema_id;

        const auto engine_validation =
            contract::validate(contracts.engine, provenance);
        if (!engine_validation.ok()) {
            return contract_failure(engine_validation, "resolved engine");
        }
        const auto randomness_validation =
            contract::validate(contracts.randomness, provenance);
        if (!randomness_validation.ok()) {
            return contract_failure(randomness_validation,
                                    "resolved randomness policy");
        }

        contract::PresentationValidationContext presentation_context;
        presentation_context.engine_profile_id =
            contracts.engine.profile_id.value;
        presentation_context.routes.reserve(contracts.engine.routes.size());
        for (const auto &route : contracts.engine.routes) {
            presentation_context.routes.push_back({
                route.id,
                route.semantic_id.value,
                route.kind.value,
            });
        }
        presentation_context.rates = {
            {20000U, 1U},
            {20000U, 1U},
            {192000U, 1U},
            {192000U, 1U},
            {192000U, 1U},
        };
        const double fade_horizon =
            contracts.presentation.audition.fade_in_duration_s.value +
            contracts.presentation.audition.fade_out_duration_s.value;
        presentation_context.total_duration_s =
            std::max(1.0, fade_horizon + 1.0);
        presentation_context.audible_start_s = 0.0;
        presentation_context.audible_duration_s =
            presentation_context.total_duration_s;
        const auto presentation_validation = contract::validate(
            contracts.presentation, presentation_context, provenance);
        if (!presentation_validation.ok()) {
            return contract_failure(presentation_validation,
                                    "resolved presentation");
        }

        return ResolvedEnginePackage{
            std::move(contracts.engine),
            std::move(contracts.presentation),
            std::move(contracts.randomness),
            std::move(contracts.rig),
            std::move(context->ids.assignments),
            std::move(provenance),
            std::move(context->assets.values),
            std::move(contracts.fuels),
            std::move(contracts.audio_buses),
        };
    } catch (const std::bad_alloc &) {
        return resource_failure("resolving an engine package");
    } catch (const std::exception &exception) {
        return diagnostic(
            authoring::DiagnosticCode::internal_failure, "",
            "unexpected engine-resolution failure: " +
                std::string{exception.what()});
    } catch (...) {
        return internal_failure("resolving an engine package");
    }
}

} // namespace engine_sim_offline::compile::detail
