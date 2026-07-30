#include "engine_sim_offline/compile.hpp"

#include "compile/resolution_builder.hpp"
#include "compile/si_conversion.hpp"
#include "compile/stable_id.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace detail = engine_sim_offline::compile::detail;
namespace contract = engine_sim_offline::contract;

static_assert(!std::is_default_constructible_v<compile::CompiledEngine>);
static_assert(!std::is_default_constructible_v<compile::CompiledScenario>);
static_assert(std::is_nothrow_copy_constructible_v<compile::CompiledEngine>);
static_assert(std::is_nothrow_copy_constructible_v<compile::CompiledScenario>);
static_assert(std::is_nothrow_move_constructible_v<compile::CompiledEngine>);
static_assert(std::is_nothrow_move_constructible_v<compile::CompiledScenario>);

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(const double actual, const double expected, const double tolerance,
                 const std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Value>
[[nodiscard]] Value require_value(compile::CompileResult<Value> result,
                                  const std::string_view message) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        std::string failure{message};
        if (!report->diagnostics.empty()) {
            failure += ": " + report->diagnostics.front().message;
        }
        throw std::runtime_error{failure};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] const authoring::Diagnostic &
require_diagnostic(const detail::SiQuantityResult &result) {
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    if (report == nullptr || report->diagnostics.empty()) {
        throw std::runtime_error{"expected SI conversion diagnostic was absent"};
    }
    return report->diagnostics.front();
}

[[nodiscard]] contract::MethodIdentity derived_method() {
    contract::MethodIdentity method;
    method.id = "compiler.test-derived";
    method.version = 1U;
    method.configuration_sha256.bytes.front() = 1U;
    return method;
}

[[nodiscard]] contract::ProvenanceLedger provenance_in_order(const bool reverse) {
    detail::ResolutionProvenanceBuilder builder{"test"};
    constexpr std::array<std::string_view, 2U> dependencies{
        "engine.bore",
        "engine.stroke",
    };
    if (reverse) {
        builder.add_derived("engine.displacement", derived_method(), dependencies);
        builder.add_authored("engine.stroke");
        builder.add_authored("engine.bore");
    } else {
        builder.add_authored("engine.bore");
        builder.add_authored("engine.stroke");
        builder.add_derived("engine.displacement", derived_method(), dependencies);
    }
    return require_value(std::move(builder).finish(),
                         "compiler provenance build failed");
}

void test_strict_si_quantity_and_rate_conversion() {
    const auto speed =
        require_value(detail::convert_quantity_to_si(
                          {7200.0, "rpm", std::nullopt},
                          authoring::QuantityDimension::angular_speed, "/speed"),
                      "RPM conversion failed");
    expect_near(speed.value, 240.0 * std::numbers::pi_v<double>, 1.0e-12,
                "RPM did not resolve to radians per second");

    const auto temperature =
        require_value(detail::convert_quantity_to_si(
                          {20.0, "degC", std::nullopt},
                          authoring::QuantityDimension::temperature, "/temperature"),
                      "temperature conversion failed");
    expect_near(temperature.value, 293.15, 1.0e-12,
                "Celsius did not resolve to kelvin");

    const auto flow =
        require_value(detail::convert_quantity_to_si(
                          {200.0, "cfm", std::string{"port_28_inh2o"}},
                          authoring::QuantityDimension::volume_flow_rate, "/flow"),
                      "flow conversion failed");
    expect_near(flow.value, 0.09438948864, 1.0e-14,
                "CFM did not resolve to cubic metres per second");
    expect(flow.flow_calibration == compile::FlowCalibrationStandard::port_28_inh2o,
           "flow calibration basis was discarded");

    const auto coefficient =
        require_value(detail::convert_quantity_to_si(
                          {0.02, "bar*s/m", std::nullopt},
                          authoring::QuantityDimension::pressure_per_speed, "/loss/k"),
                      "Chen-Flynn coefficient conversion failed");
    expect_near(coefficient.value, 2000.0, 1.0e-12,
                "pressure-per-speed coefficient changed");

    const auto rate =
        require_value(detail::convert_rate_to_si({96000U, 2U, "Hz"}, "/rate"),
                      "rate conversion failed");
    expect(rate == compile::SiRate{48000U, 1U},
           "exact rate was not canonically reduced");
}

void test_unsupported_unit_diagnostic() {
    const auto result = detail::convert_quantity_to_si(
        {1.0, "rpm", std::nullopt}, authoring::QuantityDimension::pressure,
        "/losses/constant_fmep");
    const auto &diagnostic = require_diagnostic(result);
    expect(diagnostic.code == authoring::DiagnosticCode::invalid_unit,
           "unsupported unit returned the wrong diagnostic code");
    expect(diagnostic.json_pointer == "/losses/constant_fmep/unit",
           "unsupported unit lost its authored JSON pointer");
}

void test_deterministic_dense_stable_ids() {
    constexpr std::array<detail::StableIdSource, 3U> first{
        detail::StableIdSource{"cylinder-c", "/cylinders/0/id"},
        detail::StableIdSource{"Cylinder-A", "/cylinders/1/id"},
        detail::StableIdSource{"cylinder-b", "/cylinders/2/id"},
    };
    constexpr std::array<detail::StableIdSource, 3U> second{
        detail::StableIdSource{"cylinder-b", "/cylinders/0/id"},
        detail::StableIdSource{"cylinder-c", "/cylinders/1/id"},
        detail::StableIdSource{"Cylinder-A", "/cylinders/2/id"},
    };
    const auto first_assignments =
        require_value(detail::assign_stable_runtime_ids("engine.cylinder", first),
                      "first stable-ID assignment failed");
    const auto second_assignments =
        require_value(detail::assign_stable_runtime_ids("engine.cylinder", second),
                      "second stable-ID assignment failed");
    expect(first_assignments == second_assignments,
           "JSON array order changed stable runtime IDs");
    expect(first_assignments.front().authored_id == "Cylinder-A" &&
               first_assignments.front().runtime_id == 1U &&
               first_assignments.back().authored_id == "cylinder-c" &&
               first_assignments.back().runtime_id == 3U,
           "stable IDs did not use canonical byte order and dense IDs");
}

void test_deterministic_generic_provenance() {
    const auto first = provenance_in_order(false);
    const auto second = provenance_in_order(true);
    expect(first == second, "resolution insertion order changed compiler provenance");
    expect(contract::validate(first).ok(),
           "compiler-generated provenance failed contract validation");
    expect(first.claims.size() == 2U &&
               first.claims.front().origin ==
                   contract::ProvenanceOrigin::authored_product_data &&
               first.claims.back().origin == contract::ProvenanceOrigin::derived,
           "generic authored/derived provenance origins changed");
    expect(!first.bundle.sha256.is_zero(),
           "compiler-generated provenance was not self-sealed");
}

void test_scoped_provenance_combines_without_rekeying_engine_values() {
    auto engine = provenance_in_order(false);
    const auto retained_engine_resolution_id = engine.resolutions.front().id;

    detail::ResolutionProvenanceBuilder scenario_builder{"scenario", engine};
    constexpr std::array<std::string_view, 1U> dependencies{"engine.bore"};
    scenario_builder.add_derived("scenario.projected-bore", derived_method(),
                                 dependencies);
    auto combined = require_value(std::move(scenario_builder).finish(),
                                  "combined provenance build failed");

    expect(contract::validate(combined).ok(),
           "combined scoped provenance failed validation");
    expect(combined.resolutions.size() == engine.resolutions.size() + 1U &&
               combined.resolutions.front().id ==
                   retained_engine_resolution_id &&
               combined.resolutions.back().id ==
                   "compiler.scenario.resolution.1",
           "scenario provenance rekeyed an immutable engine resolution");
}

} // namespace

int main() {
    try {
        test_strict_si_quantity_and_rate_conversion();
        test_unsupported_unit_diagnostic();
        test_deterministic_dense_stable_ids();
        test_deterministic_generic_provenance();
        test_scoped_provenance_combines_without_rekeying_engine_values();
        std::cout << "compiler foundation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "compiler foundation test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
