#include "engine_sim_offline/contract/presentation.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

constexpr RationalRateHz kPhysicalPressureRate{192000, 1};

template <class T>
void validate_authored(ValidationReport &report, const AuthoredValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_authored_value(report, value, provenance, path);
}

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

void validate_selection(ValidationReport &report, const MethodSelection &selection,
                        const std::string &path) {
    detail::require(report, is_valid_semantic_id(selection.id),
                    ContractIssueCode::invalid_value, path + ".id",
                    "method selection ID must be canonical");
    detail::require(report, selection.version > 0, ContractIssueCode::invalid_value,
                    path + ".version", "method selection version must be positive");
}

template <class Function>
void for_each_authored_method(const AuthoredPresentationMethods &methods,
                              Function function) {
    function(methods.calibrated_pressure_publication,
             "presentation.methods.calibrated_pressure_publication");
    function(methods.coherent_two_outlet_audition,
             "presentation.methods.coherent_two_outlet_audition");
}

template <class Function>
void for_each_method(const PresentationMethods &methods, Function function) {
    function(methods.calibrated_pressure_publication,
             "presentation.methods.calibrated_pressure_publication");
    function(methods.coherent_two_outlet_audition,
             "presentation.methods.coherent_two_outlet_audition");
}

bool gain_representable_as_positive_float(double value) noexcept {
    if (!detail::finite_positive(value)) {
        return false;
    }
    const auto compiled = static_cast<float>(value);
    return std::isfinite(compiled) && compiled > 0.0F;
}

PresentationValidationContext
make_presentation_context(const EngineSpec &engine, const RenderScenario &scenario) {
    PresentationValidationContext context;
    context.engine_profile_id = engine.profile_id.value;
    context.routes.reserve(engine.routes.size());
    for (const auto &route : engine.routes) {
        context.routes.push_back({route.id, route.semantic_id.value, route.kind.value});
    }
    context.rates = scenario.rates;
    context.audible_duration_s = scenario.audible_duration_s.value;
    return context;
}

} // namespace

ValidationReport validate(const AuthoredPresentationCalibration &calibration) {
    using detail::finite_nonnegative;
    using detail::require;

    ValidationReport report = validate(calibration.provenance);
    require(report, calibration.schema_version == 1,
            ContractIssueCode::unsupported_value, "schema_version",
            "physical-pressure presentation schema version must be exactly 1");
    require(report, is_valid_semantic_id(calibration.calibration_id),
            ContractIssueCode::invalid_value, "calibration_id",
            "presentation calibration ID must be canonical");
    validate_authored(report, calibration.engine_profile_id, calibration.provenance,
                      "presentation.engine_profile_id");
    require(report, is_valid_semantic_id(calibration.engine_profile_id.value),
            ContractIssueCode::invalid_value, "presentation.engine_profile_id.value",
            "engine profile ID must be canonical");

    for_each_authored_method(
        calibration.methods, [&](const auto &method, const std::string &path) {
            validate_authored(report, method, calibration.provenance, path);
            validate_selection(report, method.value, path + ".value");
        });

    validate_authored(report, calibration.monitoring.gain_linear,
                      calibration.provenance, "presentation.monitoring.gain_linear");
    require(report,
            gain_representable_as_positive_float(
                calibration.monitoring.gain_linear.value),
            ContractIssueCode::invalid_value,
            "presentation.monitoring.gain_linear.value",
            "monitoring gain must compile to a finite positive Float32 value");

    const auto validate_fade = [&](const AuthoredValue<double> &value,
                                   const std::string &path) {
        validate_authored(report, value, calibration.provenance, path);
        require(report, finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "fade duration must be finite and nonnegative");
    };
    validate_fade(calibration.monitoring.fade_in_duration_s,
                  "presentation.monitoring.fade_in_duration_s");
    validate_fade(calibration.monitoring.fade_out_duration_s,
                  "presentation.monitoring.fade_out_duration_s");
    return report;
}

ValidationReport validate(const PresentationCalibration &calibration,
                          const PresentationValidationContext &context,
                          const ProvenanceLedger &provenance) {
    using detail::append_prefixed;
    using detail::finite_nonnegative;
    using detail::require;

    ValidationReport report = validate(provenance);
    require(report, calibration.schema_version == 1,
            ContractIssueCode::unsupported_value, "schema_version",
            "physical-pressure presentation schema version must be exactly 1");
    require(report, is_valid_semantic_id(calibration.calibration_id),
            ContractIssueCode::invalid_value, "calibration_id",
            "presentation calibration ID must be canonical");
    require(report, calibration.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "presentation calibration and provenance schema IDs must match");
    validate_resolved(report, calibration.engine_profile_id, provenance,
                      "presentation.engine_profile_id");
    require(report, calibration.engine_profile_id.value == context.engine_profile_id,
            ContractIssueCode::inconsistent_semantics,
            "presentation.engine_profile_id.value",
            "render context and presentation profile IDs must match");

    for_each_method(
        calibration.methods, [&](const auto &method, const std::string &path) {
            validate_resolved(report, method, provenance, path);
            append_prefixed(report, validate(method.value), path + ".value");
        });

    validate_resolved(report, calibration.monitoring.gain_linear, provenance,
                      "presentation.monitoring.gain_linear");
    require(report,
            gain_representable_as_positive_float(
                calibration.monitoring.gain_linear.value),
            ContractIssueCode::invalid_value,
            "presentation.monitoring.gain_linear.value",
            "monitoring gain must compile to a finite positive Float32 value");

    const auto validate_fade = [&](const ResolvedValue<double> &value,
                                   const std::string &path) {
        validate_resolved(report, value, provenance, path);
        require(report, finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "fade duration must be finite and nonnegative");
    };
    validate_fade(calibration.monitoring.fade_in_duration_s,
                  "presentation.monitoring.fade_in_duration_s");
    validate_fade(calibration.monitoring.fade_out_duration_s,
                  "presentation.monitoring.fade_out_duration_s");

    append_prefixed(report, validate(context.rates), "rates");
    require(report,
            context.rates.acoustic == kPhysicalPressureRate &&
                context.rates.delivery == kPhysicalPressureRate,
            ContractIssueCode::unsupported_value, "rates",
            "physical-pressure presentation requires 192000/1 Hz acoustic and "
            "delivery rates");

    require(report, context.routes.size() == 2,
            ContractIssueCode::inconsistent_shape, "routes",
            "coherent outlet presentation requires exactly two source routes");
    std::unordered_set<std::uint32_t> route_ids;
    std::unordered_set<std::string> route_semantic_ids;
    for (std::size_t index = 0; index < context.routes.size(); ++index) {
        const auto &route = context.routes[index];
        const auto path = "routes[" + std::to_string(index) + "]";
        require(report, route.route_id.valid(), ContractIssueCode::invalid_value,
                path + ".route_id", "outlet route ID must be nonzero");
        if (!route_ids.insert(route.route_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".route_id",
                       "outlet route IDs must be unique");
        }
        if (index > 0) {
            require(report,
                    context.routes[index - 1].route_id.value < route.route_id.value,
                    ContractIssueCode::inconsistent_semantics, path + ".route_id",
                    "outlet routes must be ordered by ascending stable route ID");
        }
        require(report, is_valid_semantic_id(route.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "outlet route semantic ID must be canonical");
        if (!route_semantic_ids.insert(route.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "outlet route semantic IDs must be unique");
        }
        require(report, route.kind == SourceRouteKind::exhaust_outlet,
                ContractIssueCode::unsupported_value, path + ".kind",
                "physical-pressure presentation accepts exhaust outlets only");
    }

    const auto audible_frames =
        resolve_frame_index(context.audible_duration_s, context.rates.delivery);
    const auto fade_in_frames = resolve_frame_index(
        calibration.monitoring.fade_in_duration_s.value, context.rates.delivery);
    const auto fade_out_frames = resolve_frame_index(
        calibration.monitoring.fade_out_duration_s.value, context.rates.delivery);
    require(report, audible_frames.has_value() && *audible_frames > 0,
            ContractIssueCode::invalid_value, "audible_duration_s",
            "audible duration must resolve to a positive delivery-frame count");
    require(report, fade_in_frames.has_value(), ContractIssueCode::invalid_value,
            "presentation.monitoring.fade_in_duration_s.value",
            "fade-in duration must resolve exactly to a delivery-frame index");
    require(report, fade_out_frames.has_value(), ContractIssueCode::invalid_value,
            "presentation.monitoring.fade_out_duration_s.value",
            "fade-out duration must resolve exactly to a delivery-frame index");
    if (audible_frames.has_value() && fade_in_frames.has_value() &&
        fade_out_frames.has_value()) {
        require(report,
                *fade_in_frames <= *audible_frames &&
                    *fade_out_frames <= *audible_frames - *fade_in_frames,
                ContractIssueCode::inconsistent_semantics,
                "presentation.monitoring",
                "audition fades must fit inside the audible interval");
    }
    return report;
}

ValidationReport validate(const PresentationCalibration &calibration,
                          const EngineSpec &engine, const RenderScenario &scenario,
                          const ProvenanceLedger &provenance) {
    auto report =
        validate(calibration, make_presentation_context(engine, scenario), provenance);
    detail::require(report, scenario.engine_profile_id == engine.profile_id.value,
                    ContractIssueCode::inconsistent_semantics,
                    "presentation.engine_profile_id.value",
                    "engine, scenario, and presentation profile IDs must match");
    return report;
}

} // namespace engine_sim_offline::contract
