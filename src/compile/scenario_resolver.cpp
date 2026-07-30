#include "compile/scenario_resolver_internal.hpp"

#include "compile/diagnostics.hpp"
#include "compile/si_conversion.hpp"
#include "compile/stable_id.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::compile::detail {
namespace {

constexpr std::string_view kFixedRateRpmMethodDescriptor =
    "method_id: fixed-rate-post-step-rpm-binary64-v1\n"
    "version: 1\n"
    "input: authored piecewise engine-speed trajectory and exact physics rate\n"
    "clock: sample index i represents the speed after physics step i\n"
    "sampling: evaluate at integer boundary i+1 without accumulated time\n"
    "encoding: owned IEEE-754 binary64 RPM vector in host-independent order\n"
    "digest: SHA-256 over each binary64 bit pattern serialized little-endian\n";

} // namespace

const contract::MethodIdentity &fixed_rate_post_step_rpm_method_identity() {
    static const contract::MethodIdentity method{
        "fixed-rate-post-step-rpm-binary64-v1",
        1U,
        contract::sha256(
            std::as_bytes(std::span<const char>{kFixedRateRpmMethodDescriptor.data(),
                                                kFixedRateRpmMethodDescriptor.size()})),
    };
    return method;
}

namespace scenario_resolution {

ScenarioResolver::ScenarioResolver(const authoring::ScenarioDocument &document,
                                   const ScenarioResolverContext &context)
    : document_(document), context_(context),
      provenance_{"scenario", context.engine_provenance} {}

ScenarioResolutionResult ScenarioResolver::resolve() {
    validate_context();
    if (!report_.ok()) {
        return std::move(report_);
    }

    compile_common_fields();
    compile_preparation();
    compile_operating_state();
    compile_mode();
    compile_output_selection();
    std::vector<std::string> event_paths;
    event_paths.reserve(document_.events.size());
    std::vector<StableIdSource> event_ids;
    event_ids.reserve(document_.events.size());
    for (std::size_t index = 0; index < document_.events.size(); ++index) {
        event_paths.push_back("/events/" + std::to_string(index) + "/id");
        event_ids.push_back({
            document_.events[index].id.value,
            event_paths.back(),
        });
    }
    auto assignment_result = assign_stable_runtime_ids("scenario.event", event_ids);
    if (auto *assignment_report =
            std::get_if<authoring::DiagnosticReport>(&assignment_result)) {
        append(report_, std::move(*assignment_report));
    } else {
        stable_id_assignments_ =
            std::get<std::vector<StableIdAssignment>>(std::move(assignment_result));
    }
    if (!report_.ok()) {
        return std::move(report_);
    }

    register_provenance();
    auto provenance_result = std::move(provenance_).finish();
    if (auto *report = std::get_if<authoring::DiagnosticReport>(&provenance_result)) {
        return std::move(*report);
    }
    combined_provenance_ =
        std::get<contract::ProvenanceLedger>(std::move(provenance_result));
    bind_resolution_ids();

    source_matrix_ = build_source_matrix();
    if (!report_.ok()) {
        return std::move(report_);
    }

    append_contract_report(report_,
                           contract::validate(scenario_, combined_provenance_));
    append_contract_report(report_,
                           contract::validate_for_engine(scenario_, context_.engine));
    append_contract_report(report_,
                           contract::validate(context_.presentation, context_.engine,
                                              scenario_, combined_provenance_));
    append_contract_report(
        report_, contract::validate(context_.randomness, combined_provenance_));

    const auto matrix_validation = contract::validate(source_matrix_);
    if (!matrix_validation.ok()) {
        append_contract_report(report_, matrix_validation,
                               authoring::DiagnosticCode::internal_failure, "/output");
    }
    append_contract_report(report_,
                           contract::validate_evidence_rights(
                               combined_provenance_, source_matrix_.distribution),
                           std::nullopt, "/output");
    if (!report_.ok()) {
        return std::move(report_);
    }

    auto random_result = contract::compile_random_plan(
        context_.randomness, context_.engine, context_.presentation, scenario_);
    if (auto *validation = std::get_if<contract::ValidationReport>(&random_result)) {
        append_contract_report(report_, std::move(*validation),
                               authoring::DiagnosticCode::unsupported_capability,
                               "/public_seed");
        return std::move(report_);
    }

    ResolvedScenarioContracts result;
    result.scenario = std::move(scenario_);
    result.source_matrix = std::move(source_matrix_);
    result.random_plan = std::get<contract::RandomPlan>(std::move(random_result));
    result.combined_provenance = std::move(combined_provenance_);
    result.request_input = std::move(request_input_);
    result.stable_id_assignments = std::move(stable_id_assignments_);
    return result;
}

void ScenarioResolver::add(authoring::DiagnosticCode code, std::string_view path,
                           std::string message) {
    append(report_, diagnostic(code, path, std::move(message)));
}

double ScenarioResolver::quantity(const authoring::Quantity &value,
                                  authoring::QuantityDimension dimension,
                                  std::string_view path) {
    auto result = convert_quantity_to_si(value, dimension, path);
    if (auto *diagnostics = std::get_if<authoring::DiagnosticReport>(&result)) {
        append(report_, std::move(*diagnostics));
        return 0.0;
    }
    return std::get<SiQuantity>(result).value;
}

double ScenarioResolver::engine_speed_rpm(const authoring::Quantity &value,
                                          std::string_view path) {
    const auto angular_speed =
        quantity(value, authoring::QuantityDimension::angular_speed, path);
    return value.unit == "rpm" ? value.value
                               : radians_per_second_to_rpm(angular_speed);
}

contract::RationalRateHz ScenarioResolver::rate(const authoring::RationalRate &value,
                                                std::string_view path) {
    auto result = convert_rate_to_si(value, path);
    if (auto *diagnostics = std::get_if<authoring::DiagnosticReport>(&result)) {
        append(report_, std::move(*diagnostics));
        return {};
    }
    const auto converted = std::get<SiRate>(result);
    return {converted.numerator_hz, converted.denominator};
}

std::optional<std::uint64_t> ScenarioResolver::physics_frame(double time_s,
                                                             std::string_view path) {
    const auto frame = contract::resolve_frame_index(time_s, scenario_.rates.physics);
    if (!frame.has_value()) {
        add(authoring::DiagnosticCode::inconsistent_value, path,
            "time must resolve to an exact integer physics-frame boundary");
    }
    return frame;
}

void ScenarioResolver::validate_context() {
    if (document_.schema != "engine-sim-offline/scenario") {
        add(authoring::DiagnosticCode::unsupported_schema, "/schema",
            "scenario resolver accepts the current scenario schema only");
    }
    const auto engine_validation =
        contract::validate(context_.engine, context_.engine_provenance);
    if (!engine_validation.ok()) {
        append_contract_report(report_, engine_validation,
                               authoring::DiagnosticCode::internal_failure, "");
    }
    const auto randomness_validation =
        contract::validate(context_.randomness, context_.engine_provenance);
    if (!randomness_validation.ok()) {
        append_contract_report(report_, randomness_validation,
                               authoring::DiagnosticCode::internal_failure, "");
    }
    if (document_.engine.value != context_.engine.engine_id.value) {
        add(authoring::DiagnosticCode::dangling_reference, "/engine",
            "scenario engine reference does not name the supplied compiled "
            "engine");
    }
    if (!contract::is_valid_semantic_id(document_.id.value)) {
        add(authoring::DiagnosticCode::invalid_value, "/id",
            "current executable scenario IDs use the canonical lowercase "
            "semantic-ID grammar");
    }
    if (context_.limits.maximum_fixed_rate_trajectory_frames == 0U) {
        add(authoring::DiagnosticCode::internal_failure, "",
            "scenario resolver trajectory limit must be positive");
    }

    std::unordered_set<std::string> fuel_ids;
    for (const auto &fuel : context_.fuels) {
        if (!fuel_ids.insert(fuel.authored_id).second) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "compiled engine contains duplicate scenario fuel descriptors");
        }
        if (fuel.authored_id == document_.fuel.value &&
            selected_fuel_.descriptor == nullptr) {
            selected_fuel_.descriptor = &fuel;
        }
    }
    if (selected_fuel_.descriptor == nullptr) {
        add(authoring::DiagnosticCode::dangling_reference, "/fuel",
            "scenario fuel reference does not resolve in the compiled engine");
        return;
    }
    if (selected_fuel_.descriptor->fuel_id.value !=
        selected_fuel_.descriptor->authored_id) {
        add(authoring::DiagnosticCode::internal_failure, "",
            "resolved scenario fuel identity disagrees with its authored ID");
    }
    const auto *heating = find_resolution(
        context_.engine_provenance,
        selected_fuel_.descriptor->lower_heating_value_j_per_kg.resolution_id);
    const auto *molecular_mass = find_resolution(
        context_.engine_provenance,
        selected_fuel_.descriptor->molecular_mass_kg_per_mol.resolution_id);
    const auto *molecular_afr = find_resolution(
        context_.engine_provenance,
        selected_fuel_.descriptor->molecular_air_fuel_ratio.resolution_id);
    const auto *identity = find_resolution(
        context_.engine_provenance, selected_fuel_.descriptor->fuel_id.resolution_id);
    if (heating == nullptr || molecular_mass == nullptr || molecular_afr == nullptr ||
        identity == nullptr) {
        add(authoring::DiagnosticCode::internal_failure, "",
            "resolved scenario fuel does not belong to engine provenance");
    } else {
        selected_fuel_.heating_value_source_path = heating->parameter_path;
        selected_fuel_.molecular_mass_source_path = molecular_mass->parameter_path;
        selected_fuel_.molecular_afr_source_path = molecular_afr->parameter_path;
    }
}

} // namespace scenario_resolution

ScenarioResolutionResult
resolve_scenario_document(const authoring::ScenarioDocument &document,
                          const ScenarioResolverContext &context) noexcept {
    try {
        return scenario_resolution::ScenarioResolver{document, context}.resolve();
    } catch (const std::bad_alloc &) {
        return resource_failure("resolving authored scenario contracts");
    } catch (const std::exception &) {
        return internal_failure("resolving authored scenario contracts");
    } catch (...) {
        return internal_failure("resolving authored scenario contracts");
    }
}

} // namespace engine_sim_offline::compile::detail
