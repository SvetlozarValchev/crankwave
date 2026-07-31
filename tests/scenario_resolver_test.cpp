#include "compile/scenario_resolver.hpp"

#include "contract_test_support.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/free_vehicle_method_registry.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
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

void test_free_engine_method_identity_is_bound_to_nonnegative_semantics() {
    constexpr std::string_view kExpectedId =
        "nonnegative-speed-free-engine-centered-slider-crank-v1";
    const auto descriptor = engine_sim_offline::simulation::
        nonnegative_speed_free_engine_centered_slider_crank_method_descriptor();
    const auto descriptor_digest = contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
    const auto &identity = engine_sim_offline::simulation::
        nonnegative_speed_free_engine_centered_slider_crank_method_identity();

    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           "free-engine method descriptor is not canonical LF text");
    expect(
        identity.id == kExpectedId &&
            identity.id == engine_sim_offline::simulation::
                               kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodId &&
            identity.version ==
                engine_sim_offline::simulation::
                    kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodVersion &&
            identity.configuration_sha256 == descriptor_digest &&
            contract::validate(identity).ok() &&
            &identity ==
                &engine_sim_offline::simulation::
                    nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
        "free-engine method identity is invalid, unstable, or detached from its "
        "canonical descriptor");
    expect(descriptor.find("positive-speed-arithmetic=") != std::string_view::npos &&
               descriptor.find("cold-bootstrap=") != std::string_view::npos &&
               descriptor.find("stall-commit=") != std::string_view::npos &&
               descriptor.find("rest-constraint=") != std::string_view::npos &&
               descriptor.find("rest-motion=") != std::string_view::npos &&
               descriptor.find("warm-acquisition=dynamic-physics-continues-without-"
                               "reset-from-release-through-an-equal-or-later-audible-"
                               "start-boundary") != std::string_view::npos &&
               descriptor.find("reverse=unsupported-and-never-published") !=
                   std::string_view::npos,
           "free-engine method descriptor lost nonnegative-speed execution "
           "semantics");
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
    scenario.quality = {"resolver-production", 4096U, 7U, 13U};
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

    compile::ResolvedRigDescriptor rig;
    rig.runtime_id = 10U;
    rig.semantic_id = builder.resolved(std::string{"fixture-rig"}, "rig.semantic_id");
    rig.vehicle.emplace();
    rig.vehicle->runtime_id = 11U;
    rig.vehicle->semantic_id =
        builder.resolved(std::string{"fixture-vehicle"}, "rig.vehicle.semantic_id");
    rig.vehicle->mass_kg = builder.resolved(1500.0, "rig.vehicle.mass_kg");
    rig.vehicle->drag_coefficient =
        builder.resolved(0.30, "rig.vehicle.drag_coefficient");
    rig.vehicle->frontal_area_m2 =
        builder.resolved(2.10, "rig.vehicle.frontal_area_m2");
    rig.vehicle->differential_ratio =
        builder.resolved(3.25, "rig.vehicle.differential_ratio");
    rig.vehicle->tire_radius_m = builder.resolved(0.33, "rig.vehicle.tire_radius_m");
    rig.vehicle->rolling_resistance_force_n =
        builder.resolved(180.0, "rig.vehicle.rolling_resistance_force_n");
    rig.vehicle->maximum_service_brake_force_n =
        builder.resolved(12000.0, "rig.vehicle.maximum_service_brake_force_n");
    rig.transmission.emplace();
    rig.transmission->runtime_id = 12U;
    rig.transmission->semantic_id = builder.resolved(
        std::string{"fixture-transmission"}, "rig.transmission.semantic_id");
    rig.transmission->maximum_clutch_torque_nm =
        builder.resolved(450.0, "rig.transmission.maximum_clutch_torque_nm");
    rig.transmission->gears = {
        {
            13U,
            builder.resolved(1U, "rig.transmission.gears.gear-1.authored_ordinal"),
            builder.resolved(std::string{"gear-1"},
                             "rig.transmission.gears.gear-1.semantic_id"),
            builder.resolved(3.50, "rig.transmission.gears.gear-1.ratio"),
        },
        {
            14U,
            builder.resolved(2U, "rig.transmission.gears.gear-2.authored_ordinal"),
            builder.resolved(std::string{"gear-2"},
                             "rig.transmission.gears.gear-2.semantic_id"),
            builder.resolved(2.10, "rig.transmission.gears.gear-2.ratio"),
        },
    };

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
        &rig,
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
    expect(resolved.request_input.session_capacities ==
               engine_sim_offline::compile::CompiledSessionCapacities{
                   4096U,
                   7U,
                   13U,
               },
           "authored session capacities were not retained as delivery/control/"
           "telemetry bounds");
    const auto expected_internal_event_capacity =
        static_cast<std::uint32_t>((3U * engine.cylinders.size() + 1U) * 200U);
    expect(resolved.scenario.quality.value.capture_block_capacity_frames == 200U &&
               resolved.scenario.quality.value.event_journal_capacity_records ==
                   expected_internal_event_capacity,
           "public session capacities leaked into private capture transport");
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

    auto free_engine_document = document;
    free_engine_document.id.value = "resolver.free-engine";
    free_engine_document.initial_state.dyno_enabled = false;
    free_engine_document.initial_state.limiter_enabled = true;
    authoring::FreeEngineMode authored_free_engine;
    authored_free_engine.attached_inertia = quantity(0.25, "kg*m2");
    authored_free_engine.throttle_01.interpolation =
        authoring::TrajectoryInterpolation::right_continuous_hold;
    authored_free_engine.throttle_01.points = {
        {quantity(0.0, "s"), 0.15},
        {quantity(2.0, "s"), 0.85},
    };
    authored_free_engine.external_resisting_torque.emplace();
    authored_free_engine.external_resisting_torque->value_dimension =
        authoring::QuantityDimension::torque;
    authored_free_engine.external_resisting_torque->interpolation =
        authoring::TrajectoryInterpolation::right_continuous_hold;
    authored_free_engine.external_resisting_torque->points = {
        {quantity(0.0, "s"), quantity(10.0, "N*m")},
        {quantity(2.5, "s"), quantity(5.0, "N*m")},
    };
    free_engine_document.mode = authored_free_engine;

    auto free_engine_result =
        compile::resolve_scenario_document(free_engine_document, context);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&free_engine_result)) {
        const auto message =
            report->diagnostics.empty()
                ? std::string{"free-engine resolver returned an empty diagnostic"}
                : report->diagnostics.front().json_pointer + ": " +
                      report->diagnostics.front().message;
        throw std::runtime_error{message};
    }
    const auto &free_engine_contracts =
        std::get<compile::ResolvedScenarioContracts>(free_engine_result);
    const auto &free_engine =
        std::get<contract::FreeEngine>(free_engine_contracts.scenario.mode);
    const auto &mechanism =
        std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile)
            .core.mechanism;
    const auto inertia_calculation = engine_sim_offline::simulation::
        calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto &derived_inertia =
        std::get<engine_sim_offline::simulation::CenteredSliderCrankCycleMeanInertia>(
            inertia_calculation);
    expect(free_engine.initial_engine_speed_rpm.value == 3000.0 &&
               free_engine.engine_baseline_inertia_kg_m2.value ==
                   derived_inertia.engine_equivalent_inertia_kg_m2 &&
               free_engine.attached_inertia_kg_m2.value == 0.25 &&
               free_engine.total_equivalent_inertia_kg_m2.value ==
                   derived_inertia.engine_equivalent_inertia_kg_m2 + 0.25 &&
               free_engine.throttle_01.points.size() == 2U &&
               free_engine.throttle_01.points.back().value == 0.85 &&
               free_engine.external_resisting_torque_nm.points.size() == 2U &&
               free_engine.external_resisting_torque_nm.points.front().value == 10.0,
           "free-engine controls changed during SI resolution");
    expect(
        free_engine.crank_dynamics_method.value ==
            engine_sim_offline::simulation::
                nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
        "free-engine resolver selected the wrong crank-dynamics method");
    expect(!free_engine.initial_engine_speed_rpm.resolution_id.empty() &&
               !free_engine.engine_baseline_inertia_kg_m2.resolution_id.empty() &&
               !free_engine.attached_inertia_kg_m2.resolution_id.empty() &&
               !free_engine.total_equivalent_inertia_kg_m2.resolution_id.empty() &&
               !free_engine.throttle_01.resolution_id.empty() &&
               !free_engine.external_resisting_torque_nm.resolution_id.empty() &&
               !free_engine.crank_dynamics_method.resolution_id.empty(),
           "free-engine provenance was not bound to every resolved input");

    auto repeated_free_engine =
        compile::resolve_scenario_document(free_engine_document, context);
    expect(std::holds_alternative<compile::ResolvedScenarioContracts>(
               repeated_free_engine) &&
               std::get<compile::ResolvedScenarioContracts>(repeated_free_engine) ==
                   free_engine_contracts,
           "identical authored free-engine scenario did not resolve deterministically");

    auto free_vehicle_document = document;
    free_vehicle_document.id.value = "resolver.free-vehicle";
    free_vehicle_document.initial_state.dyno_enabled = false;
    free_vehicle_document.initial_state.limiter_enabled = true;
    authoring::FreeVehicleMode authored_free_vehicle;
    authored_free_vehicle.rig.value = "fixture-rig";
    authored_free_vehicle.initial_gear = authoring::GearRef{"gear-1"};
    authored_free_vehicle.initial_vehicle_speed = quantity(10.0, "m/s");
    authored_free_vehicle.initial_clutch_engagement_01 = 0.25;
    authored_free_vehicle.initial_service_brake_application_01 = 0.0;
    authored_free_vehicle.throttle_01.interpolation =
        authoring::TrajectoryInterpolation::right_continuous_hold;
    authored_free_vehicle.throttle_01.points = {
        {quantity(0.0, "s"), 0.2},
        {quantity(2.0, "s"), 0.8},
    };
    free_vehicle_document.mode = authored_free_vehicle;
    free_vehicle_document.events = {
        {authoring::ScenarioEventId{"select-second"}, quantity(2.25, "s"),
         authoring::SelectGearEvent{authoring::GearRef{"gear-2"}}},
        {authoring::ScenarioEventId{"engage-clutch"}, quantity(2.5, "s"),
         authoring::SetClutchEngagementEvent{0.8}},
        {authoring::ScenarioEventId{"apply-brake"}, quantity(2.75, "s"),
         authoring::SetServiceBrakeApplicationEvent{0.5}},
    };

    auto free_vehicle_result =
        compile::resolve_scenario_document(free_vehicle_document, context);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&free_vehicle_result)) {
        const auto message =
            report->diagnostics.empty()
                ? std::string{"free-vehicle resolver returned an empty diagnostic"}
                : report->diagnostics.front().json_pointer + ": " +
                      report->diagnostics.front().message;
        throw std::runtime_error{message};
    }
    const auto &free_vehicle_contracts =
        std::get<compile::ResolvedScenarioContracts>(free_vehicle_result);
    const auto &free_vehicle =
        std::get<contract::FreeVehicle>(free_vehicle_contracts.scenario.mode);
    expect(free_vehicle.rig.id.value == rig.runtime_id &&
               free_vehicle.rig.semantic_id == rig.semantic_id &&
               free_vehicle.rig.vehicle.mass_kg == rig.vehicle->mass_kg &&
               free_vehicle.rig.vehicle.maximum_service_brake_force_n ==
                   rig.vehicle->maximum_service_brake_force_n &&
               free_vehicle.rig.transmission.maximum_clutch_torque_nm ==
                   rig.transmission->maximum_clutch_torque_nm &&
               free_vehicle.rig.transmission.gears[1].ratio ==
                   rig.transmission->gears[1].ratio,
           "free-vehicle resolver did not retain engine-owned rig values and "
           "provenance IDs verbatim");
    expect(
        free_vehicle.initial_vehicle_speed_m_s.value == 10.0 &&
            free_vehicle.selected_gear.value.size() == 2U &&
            free_vehicle.selected_gear.value.front().gear_id == contract::GearId{13U} &&
            free_vehicle.selected_gear.value.back().gear_id == contract::GearId{14U} &&
            free_vehicle.clutch_engagement_01.value.size() == 2U &&
            free_vehicle.clutch_engagement_01.value.back().value == 0.8 &&
            free_vehicle.service_brake_application_01.value.size() == 2U &&
            free_vehicle.service_brake_application_01.value.back().value == 0.5,
        "free-vehicle initial state or right-continuous control lanes changed "
        "during resolution");
    expect(free_vehicle.drivetrain_dynamics_method.value ==
                   engine_sim_offline::simulation::
                       bounded_forward_vehicle_drivetrain_method_identity() &&
               contract::validate(free_vehicle_contracts.scenario,
                                  free_vehicle_contracts.combined_provenance)
                   .ok(),
           "free-vehicle resolver did not bind the exact coupled method or a valid "
           "self-contained contract");
    auto repeated_free_vehicle =
        compile::resolve_scenario_document(free_vehicle_document, context);
    expect(std::holds_alternative<compile::ResolvedScenarioContracts>(
               repeated_free_vehicle) &&
               std::get<compile::ResolvedScenarioContracts>(repeated_free_vehicle) ==
                   free_vehicle_contracts,
           "identical authored free-vehicle scenario did not resolve "
           "deterministically");

    auto rig_without_brake = rig;
    rig_without_brake.vehicle->maximum_service_brake_force_n.reset();
    const compile::ScenarioResolverContext no_brake_context{
        engine,
        &rig_without_brake,
        presentation,
        randomness,
        builder.provenance,
        std::span<const compile::ResolvedFuelDescriptor>{&fuel, 1U},
        buses,
        {},
        contract::DistributionIntent::local_evaluation,
        {},
    };
    const auto no_brake_result =
        compile::resolve_scenario_document(free_vehicle_document, no_brake_context);
    const auto *no_brake_report =
        std::get_if<authoring::DiagnosticReport>(&no_brake_result);
    expect(no_brake_report != nullptr &&
               has_diagnostic(*no_brake_report,
                              authoring::DiagnosticCode::unsupported_capability,
                              "/mode/initial_service_brake_application_01"),
           "nonzero service-brake lane was accepted without a brake actuator");

    auto negative_zero_controls = free_vehicle_document;
    auto &negative_zero_mode =
        std::get<authoring::FreeVehicleMode>(negative_zero_controls.mode);
    negative_zero_mode.initial_clutch_engagement_01 = -0.0;
    negative_zero_mode.initial_service_brake_application_01 = -0.0;
    const auto negative_zero_controls_result =
        compile::resolve_scenario_document(negative_zero_controls, context);
    const auto *negative_zero_controls_report =
        std::get_if<authoring::DiagnosticReport>(&negative_zero_controls_result);
    expect(negative_zero_controls_report != nullptr &&
               has_diagnostic(*negative_zero_controls_report,
                              authoring::DiagnosticCode::invalid_value,
                              "/mode/clutch_engagement_01/0") &&
               has_diagnostic(*negative_zero_controls_report,
                              authoring::DiagnosticCode::invalid_value,
                              "/mode/service_brake_application_01/0"),
           "negative-zero clutch or service-brake control reached execution");

    auto negative_resistance = free_engine_document;
    std::get<authoring::FreeEngineMode>(negative_resistance.mode)
        .external_resisting_torque->points.front()
        .value = quantity(-1.0, "N*m");
    const auto negative_resistance_result =
        compile::resolve_scenario_document(negative_resistance, context);
    const auto *negative_resistance_report =
        std::get_if<authoring::DiagnosticReport>(&negative_resistance_result);
    expect(negative_resistance_report != nullptr &&
               has_diagnostic(*negative_resistance_report,
                              authoring::DiagnosticCode::out_of_range,
                              "/mode/external_resisting_torque/points/0/value"),
           "negative external resisting torque was accepted");

    auto linear_resistance = free_engine_document;
    std::get<authoring::FreeEngineMode>(linear_resistance.mode)
        .external_resisting_torque->interpolation =
        authoring::TrajectoryInterpolation::linear;
    const auto linear_resistance_result =
        compile::resolve_scenario_document(linear_resistance, context);
    const auto *linear_resistance_report =
        std::get_if<authoring::DiagnosticReport>(&linear_resistance_result);
    expect(linear_resistance_report != nullptr &&
               has_diagnostic(*linear_resistance_report,
                              authoring::DiagnosticCode::unsupported_capability,
                              "/mode/external_resisting_torque/interpolation"),
           "unsupported linear external resisting torque was accepted");

    auto defaulted_free_engine = free_engine_document;
    auto &defaulted_authoring =
        std::get<authoring::FreeEngineMode>(defaulted_free_engine.mode);
    defaulted_authoring.attached_inertia.reset();
    defaulted_authoring.external_resisting_torque.reset();
    const auto defaulted_result =
        compile::resolve_scenario_document(defaulted_free_engine, context);
    const auto *defaulted_contracts =
        std::get_if<compile::ResolvedScenarioContracts>(&defaulted_result);
    expect(defaulted_contracts != nullptr,
           "defaulted free-engine controls did not resolve");
    const auto &defaulted_mode =
        std::get<contract::FreeEngine>(defaulted_contracts->scenario.mode);
    expect(defaulted_mode.attached_inertia_kg_m2.value == 0.0 &&
               !std::signbit(defaulted_mode.attached_inertia_kg_m2.value) &&
               defaulted_mode.total_equivalent_inertia_kg_m2.value ==
                   defaulted_mode.engine_baseline_inertia_kg_m2.value &&
               defaulted_mode.external_resisting_torque_nm.points.size() == 1U &&
               defaulted_mode.external_resisting_torque_nm.points.front().time_s ==
                   0.0 &&
               defaulted_mode.external_resisting_torque_nm.points.front().value == 0.0,
           "omitted free-engine controls did not canonicalize to positive zero");

    auto undersized_process = document;
    undersized_process.quality.process_block_capacity_frames = 3839U;
    const auto undersized_process_result =
        compile::resolve_scenario_document(undersized_process, context);
    const auto *undersized_process_report =
        std::get_if<authoring::DiagnosticReport>(&undersized_process_result);
    expect(undersized_process_report != nullptr &&
               has_diagnostic(*undersized_process_report,
                              authoring::DiagnosticCode::unsupported_capability,
                              "/quality/process_block_capacity_frames"),
           "session capacity below the exact delivery method quantum was accepted");
}

} // namespace

int main() {
    try {
        test_free_engine_method_identity_is_bound_to_nonnegative_semantics();
        test_held_speed_resolution_on_the_integer_clock();
        std::cout << "scenario resolver tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "scenario resolver test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
