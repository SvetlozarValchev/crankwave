#include "compile/scenario_resolver.hpp"

#include "contract_test_support.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile::detail;
namespace contract = engine_sim_offline::contract;
using engine_sim_offline::contract::test::expect;

[[nodiscard]] bool has_diagnostic(const authoring::DiagnosticReport &report,
                                  authoring::DiagnosticCode code,
                                  std::string_view path) {
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.code == code && diagnostic.json_pointer == path) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] authoring::Quantity quantity(double value, std::string unit) {
    return {value, std::move(unit), std::nullopt};
}

[[nodiscard]] authoring::RationalRate rate(std::uint64_t numerator) {
    return {numerator, 1U, "Hz"};
}

[[nodiscard]] authoring::ScenarioDocument held_speed_scenario(std::string engine_id,
                                                              std::string fuel_id) {
    authoring::ScenarioDocument scenario;
    scenario.id.value = "resolver.held-speed";
    scenario.engine.value = std::move(engine_id);
    scenario.fuel.value = std::move(fuel_id);
    scenario.ambient = {
        quantity(101325.0, "Pa"),
        quantity(293.15, "K"),
        0.5,
    };
    scenario.initial_thermal_state = {
        quantity(350.0, "K"),
        quantity(360.0, "K"),
        quantity(360.0, "K"),
        quantity(370.0, "K"),
    };
    scenario.crankcase = {
        quantity(101325.0, "Pa"),
        quantity(293.15, "K"),
    };
    scenario.initial_state = {
        quantity(3000.0, "rpm"), quantity(0.0, "rad"), true, true, false, true, false,
    };
    scenario.preparation = authoring::FixedHorizonPreparation{
        quantity(2.0, "s"),
        32U,
    };

    authoring::ScalarTrajectory throttle;
    throttle.interpolation = authoring::TrajectoryInterpolation::right_continuous_hold;
    throttle.points = {{quantity(0.0, "s"), 0.85}};
    scenario.mode =
        authoring::HeldSpeedMode{quantity(3000.0, "rpm"), std::move(throttle)};
    scenario.rates = {
        rate(10000U), rate(10000U), rate(192000U), rate(192000U), rate(192000U),
    };
    scenario.quality = {"resolver-production", 256U, 4096U, 1024U};
    scenario.total_duration = quantity(3.0, "s");
    scenario.audible_start = quantity(2.0, "s");
    scenario.audible_duration = quantity(1.0, "s");
    scenario.public_seed = 42U;
    scenario.output.buses = {
        authoring::AudioBusRef{"raw"},
        authoring::AudioBusRef{"audition"},
    };
    return scenario;
}

void test_held_speed_resolution_on_the_integer_clock() {
    contract::test::InputBuilder builder;
    auto engine = contract::test::make_engine(builder);
    auto presentation = contract::test::make_presentation(builder, engine);
    auto randomness = contract::test::make_randomness_policy(builder);
    constexpr std::string_view compiler_schema =
        "engine-sim-offline.compiler-resolution-provenance";
    builder.provenance.schema_id = compiler_schema;
    engine.provenance_schema_id = compiler_schema;
    presentation.provenance_schema_id = compiler_schema;

    const auto &profile =
        std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
    const compile::ResolvedFuelDescriptor fuel{
        profile.core.fuel.fuel_id.value,
        profile.core.fuel.fuel_id,
        profile.core.fuel.energy_density_j_per_kg,
        profile.core.fuel.molecular_mass_kg_per_mol,
        profile.core.fuel.molecular_afr,
    };
    const std::array<compile::ResolvedAudioBusDescriptor, 2U> buses{{
        {
            "raw",
            "master.engine.raw",
            contract::OutputBusKind::master_engine_raw,
            {contract::RouteId{1U}},
            1.0,
            contract::AudioSampleEncoding::float32le,
            true,
            false,
        },
        {
            "audition",
            "master.engine.audition",
            contract::OutputBusKind::master_engine_audition,
            {contract::RouteId{1U}},
            1.0,
            contract::AudioSampleEncoding::pcm_s24le,
            true,
            false,
        },
    }};
    const auto document = held_speed_scenario(engine.engine_id.value, fuel.authored_id);
    const compile::ScenarioResolverContext context{
        engine,
        presentation,
        randomness,
        builder.provenance,
        std::span<const compile::ResolvedFuelDescriptor>{&fuel, 1U},
        buses,
        {},
        contract::DistributionIntent::local_evaluation,
        {},
    };

    auto result = compile::resolve_scenario_document(document, context);
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        const auto message =
            report->diagnostics.empty()
                ? std::string{"scenario resolver returned an empty diagnostic"}
                : report->diagnostics.front().json_pointer + ": " +
                      report->diagnostics.front().message;
        throw std::runtime_error{message};
    }
    const auto &resolved = std::get<compile::ResolvedScenarioContracts>(result);
    const auto &held = std::get<contract::HeldSpeed>(resolved.scenario.mode);
    expect(held.engine_speed_rpm.value == 3000.0 && held.throttle_01.value == 0.85,
           "held-speed controls changed during resolution");
    expect(resolved.request_input.total_physics_frames == 30000U &&
               resolved.request_input.audible_delivery_frames == 192000U,
           "deterministic request frame material changed");
    expect(resolved.source_matrix.required_source_routes.size() == 1U &&
               resolved.source_matrix.required_output_buses.size() == 2U &&
               resolved.source_matrix.required_artifacts.size() == 5U,
           "declared bus did not produce the generic source matrix");
    expect(!resolved.random_plan.component_seeds.empty(),
           "scenario random plan was not compiled");
    expect(resolved.request_input.selected_audio_buses.size() == 2U &&
               resolved.request_input.selected_audio_buses.front().gain_linear == 1.0,
           "selected bus mix material was not retained");

    auto repeated = compile::resolve_scenario_document(document, context);
    expect(std::holds_alternative<compile::ResolvedScenarioContracts>(repeated) &&
               std::get<compile::ResolvedScenarioContracts>(repeated) == resolved,
           "identical authored scenario did not resolve deterministically");

    auto with_events = document;
    authoring::OperatingStatePatch retain_limiter_policy;
    retain_limiter_policy.limiter_enabled = false;
    with_events.events = {
        {authoring::ScenarioEventId{"z-event"}, quantity(0.5, "s"),
         retain_limiter_policy},
        {authoring::ScenarioEventId{"a-event"}, quantity(0.75, "s"),
         retain_limiter_policy},
    };
    auto event_result = compile::resolve_scenario_document(with_events, context);
    const auto *event_contracts =
        std::get_if<compile::ResolvedScenarioContracts>(&event_result);
    expect(event_contracts != nullptr &&
               event_contracts->stable_id_assignments.size() == 2U &&
               event_contracts->stable_id_assignments[0].authored_id == "a-event" &&
               event_contracts->stable_id_assignments[0].runtime_id == 1U &&
               event_contracts->stable_id_assignments[1].authored_id == "z-event" &&
               event_contracts->stable_id_assignments[1].runtime_id == 2U,
           "scenario event runtime IDs were not canonical and dense");

    auto unsupported = document;
    unsupported.mode = authoring::FreeEngineMode{};
    auto unsupported_result = compile::resolve_scenario_document(unsupported, context);
    const auto *unsupported_report =
        std::get_if<authoring::DiagnosticReport>(&unsupported_result);
    expect(unsupported_report != nullptr &&
               has_diagnostic(*unsupported_report,
                              authoring::DiagnosticCode::unsupported_capability,
                              "/mode/type"),
           "free-engine mode was not rejected as an explicit capability");
}

} // namespace

int main() {
    try {
        test_held_speed_resolution_on_the_integer_clock();
        std::cout << "scenario resolver tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "scenario resolver test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
