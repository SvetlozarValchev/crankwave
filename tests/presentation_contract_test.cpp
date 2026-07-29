#include "contract_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;

[[nodiscard]] bool has_issue(const ValidationReport &report, ContractIssueCode code,
                             std::string_view path_fragment) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.code == code &&
               issue.path.find(path_fragment) != std::string::npos;
    });
}

void test_authored_contract() {
    InputBuilder builder;
    AuthoredPresentationCalibration calibration;
    calibration.schema_version = 1;
    calibration.calibration_id = "physical-pressure-listening-v1";
    calibration.engine_profile_id = {"bmw-m52b28", "claim"};
    calibration.methods = {
        {MethodSelection{"calibrated-pressure-publication-v1", 1}, "claim"},
        {MethodSelection{"coherent-two-outlet-audition-v1", 1}, "claim"},
    };
    calibration.monitoring = {
        {0.5, "claim"},
        {0.02, "claim"},
        {0.02, "claim"},
    };
    calibration.provenance = builder.provenance;
    expect(validate(calibration).ok(),
           "valid authored physical-pressure presentation was rejected");

    auto obsolete_schema = calibration;
    obsolete_schema.schema_version = 2;
    expect(has_issue(validate(obsolete_schema), ContractIssueCode::unsupported_value,
                     "schema_version"),
           "obsolete presentation schema was accepted");

    auto zero_gain = calibration;
    zero_gain.monitoring.gain_linear.value = 0.0;
    expect(has_issue(validate(zero_gain), ContractIssueCode::invalid_value,
                     "monitoring.gain_linear.value"),
           "zero monitoring gain was accepted");

    auto unrepresentable_gain = calibration;
    unrepresentable_gain.monitoring.gain_linear.value =
        std::numeric_limits<double>::max();
    expect(has_issue(validate(unrepresentable_gain), ContractIssueCode::invalid_value,
                     "monitoring.gain_linear.value"),
           "monitoring gain that overflows Float32 was accepted");
}

void test_resolved_contract() {
    InputBuilder builder;
    const auto content = make_manifest_content(builder);
    const auto &inputs = simulation_inputs(content);
    PresentationValidationContext context;
    context.engine_profile_id = inputs.engine.profile_id.value;
    context.routes = {
        {RouteId{1}, "exhaust.outlet.front", SourceRouteKind::exhaust_outlet},
        {RouteId{2}, "exhaust.outlet.rear", SourceRouteKind::exhaust_outlet},
    };
    context.rates = inputs.scenario.rates;
    context.audible_duration_s = inputs.scenario.audible_duration_s.value;
    expect(validate(inputs.presentation, context, builder.provenance).ok(),
           "valid resolved physical-pressure presentation was rejected");

    auto obsolete_schema = inputs.presentation;
    obsolete_schema.schema_version = 2;
    expect(has_issue(validate(obsolete_schema, context, builder.provenance),
                     ContractIssueCode::unsupported_value, "schema_version"),
           "obsolete resolved presentation schema was accepted");

    auto bad_method = inputs.presentation;
    bad_method.methods.calibrated_pressure_publication.value.configuration_sha256 = {};
    expect(has_issue(validate(bad_method, context, builder.provenance),
                     ContractIssueCode::invalid_value,
                     "calibrated_pressure_publication.value.configuration_sha256"),
           "unidentified pressure-publication method was accepted");

    auto nonfinite_gain = inputs.presentation;
    nonfinite_gain.monitoring.gain_linear.value =
        std::numeric_limits<double>::infinity();
    expect(has_issue(validate(nonfinite_gain, context, builder.provenance),
                     ContractIssueCode::invalid_value,
                     "monitoring.gain_linear.value"),
           "nonfinite monitoring gain was accepted");

    auto overlapping_fades = inputs.presentation;
    overlapping_fades.monitoring.fade_in_duration_s.value = 0.75;
    overlapping_fades.monitoring.fade_out_duration_s.value = 0.75;
    expect(has_issue(validate(overlapping_fades, context, builder.provenance),
                     ContractIssueCode::inconsistent_semantics, "monitoring"),
           "overlapping audition fades were accepted");

    auto one_route = context;
    one_route.routes.pop_back();
    expect(has_issue(validate(inputs.presentation, one_route, builder.provenance),
                     ContractIssueCode::inconsistent_shape, "routes"),
           "one-outlet presentation context was accepted");

    auto reversed_routes = context;
    std::ranges::reverse(reversed_routes.routes);
    expect(has_issue(validate(inputs.presentation, reversed_routes,
                              builder.provenance),
                     ContractIssueCode::inconsistent_semantics, "route_id"),
           "noncanonical outlet reduction order was accepted");

    auto non_exhaust = context;
    non_exhaust.routes.front().kind = SourceRouteKind::intake_inlet;
    expect(has_issue(validate(inputs.presentation, non_exhaust, builder.provenance),
                     ContractIssueCode::unsupported_value, "routes[0].kind"),
           "non-exhaust route was accepted by pressure presentation");

    auto resampled = context;
    resampled.rates.delivery = {48000, 1};
    expect(has_issue(validate(inputs.presentation, resampled, builder.provenance),
                     ContractIssueCode::unsupported_value, "rates"),
           "hidden delivery resampling was accepted");
}

} // namespace

int main() {
    try {
        test_authored_contract();
        test_resolved_contract();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
