#include "authored_engine_fixture_support.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/low_order_capture_buffer.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] test::AuthoredEngineFixture
make_request(const test::AuthoredEngineFixture &canonical) {
    auto request =
        test::make_prescribed_fixture(canonical, std::vector<double>(400U, 2400.0));
    request.scenario.total_duration_s.value = 17.0;
    request.scenario.audible_start_s.value = 2.0;
    request.scenario.audible_duration_s.value = 15.0;
    request.scenario.preparation = contract::FixedSettling{
        {1.0, "authored-fixture.warm-up"},
        {1.0, "authored-fixture.settling"},
    };
    return request;
}

[[nodiscard]] simulation::LowOrderCapturePlan
require_plan(simulation::LowOrderCapturePlanCompileResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message = "low-order capture plan was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<simulation::LowOrderCapturePlan>(std::move(result));
}

[[nodiscard]] simulation::SharedMechanismKinematicsPlan
require_mechanism_plan(simulation::MechanismKinematicsPlanCompileResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message = "mechanism kinematics plan was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<simulation::SharedMechanismKinematicsPlan>(std::move(result));
}

[[nodiscard]] contract::RandomPlan
random_plan(const test::AuthoredEngineFixture &request) {
    return test::compile_fixture_random_plan(request);
}

[[nodiscard]] simulation::LowOrderExecutionExtent
finite_extent(const contract::RenderScenario &scenario) {
    const auto frame_count = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    expect(frame_count.has_value(),
           "test scenario did not resolve to an integral physics horizon");
    return simulation::LowOrderExecutionExtent::finite_scenario(*frame_count);
}

[[nodiscard]] std::vector<contract::GasVolumeId>
expected_physical_inventory(const contract::EngineSpec &engine) {
    std::vector<contract::GasVolumeId> result;
    for (const auto &volume : engine.gas_volumes) {
        if (volume.kind.value != contract::GasVolumeKind::atmosphere) {
            result.push_back(volume.id);
        }
    }
    std::ranges::sort(result);
    return result;
}

[[nodiscard]] std::size_t insert_test_intake_route(test::AuthoredEngineFixture &request,
                                                   std::size_t insertion_index) {
    const auto &core = test::low_order_core(request.engine);
    expect(!core.gas_path.intakes.empty(), "fixture has no intake gas lane");
    expect(insertion_index <= request.engine.routes.size(),
           "intake test route insertion index is invalid");

    auto route = request.engine.routes.front();
    const auto maximum_route_id =
        std::ranges::max_element(request.engine.routes, {}, &contract::RouteSpec::id);
    expect(maximum_route_id != request.engine.routes.end(),
           "fixture has no source route identity");
    route.id = contract::RouteId{maximum_route_id->id.value + 1U};
    route.semantic_id.value = "intake.test";
    route.kind.value = contract::SourceRouteKind::intake_inlet;
    route.source_volume_id = core.gas_path.intakes.front().topology.plenum_volume_id;
    route.default_parent_route_id.reset();
    route.emitter_anchor_id.reset();
    request.engine.routes.insert(request.engine.routes.begin() +
                                     static_cast<std::ptrdiff_t>(insertion_index),
                                 std::move(route));
    return insertion_index;
}

void verify_chamber_mapping(const simulation::LowOrderCapturePlan &plan,
                            const contract::EngineSpec &engine,
                            const contract::LowOrderEngineCoreV1 &core) {
    expect(plan.cylinder_chambers.size() == engine.cylinders.size(),
           "capture plan did not map every engine cylinder");
    for (std::size_t index = 0; index < plan.cylinder_chambers.size(); ++index) {
        const auto &binding = plan.cylinder_chambers[index];
        expect(index == 0U ||
                   plan.cylinder_chambers[index - 1U].cylinder_id < binding.cylinder_id,
               "cylinder chamber bindings are not self-identifying ascending IDs");
        const auto core_cylinder = std::ranges::find(
            core.mechanism.cylinders, binding.cylinder_id,
            [](const auto &cylinder) { return cylinder.topology.cylinder_id; });
        expect(core_cylinder != core.mechanism.cylinders.end(),
               "fixture core lost an engine cylinder");
        const auto engine_cylinder = std::ranges::find(
            engine.cylinders, binding.cylinder_id, &contract::CylinderSpec::id);
        expect(engine_cylinder != engine.cylinders.end() &&
                   binding.chamber_volume_id ==
                       core_cylinder->topology.chamber_volume_id &&
                   binding.physical_volume_index <
                       plan.physical_gas_volume_ids.size() &&
                   plan.physical_gas_volume_ids[binding.physical_volume_index] ==
                       binding.chamber_volume_id,
               "cylinder chamber index does not address its physical volume");
    }
}

void test_physical_inventory_is_stable_and_excludes_atmosphere(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_request(canonical);
    const auto &baseline_core = test::low_order_core(request.engine);
    const auto expected = expected_physical_inventory(request.engine);
    const auto baseline = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    const auto expected_horizon = contract::resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);

    expect(expected_horizon.has_value() &&
               baseline.engine_profile_id == request.engine.profile_id.value &&
               baseline.scenario_id == request.scenario.scenario_id &&
               baseline.execution_extent.finite_physics_frame_count() ==
                   expected_horizon &&
               baseline.capture_buffer.declared_block_capacity_frames == 400U &&
               baseline.capture_buffer.declared_event_capacity_records == 7600U &&
               baseline.capture_buffer.maximum_events_per_frame == 19U,
           "capture ownership, horizon, or bounded storage capacities changed");
    expect(!expected.empty() && baseline.physical_gas_volume_ids == expected &&
               std::ranges::none_of(
                   baseline.physical_gas_volume_ids,
                   [&](contract::GasVolumeId id) {
                       const auto volume =
                           std::ranges::find(request.engine.gas_volumes, id,
                                             &contract::GasVolumeSpec::id);
                       return volume == request.engine.gas_volumes.end() ||
                              volume->kind.value == contract::GasVolumeKind::atmosphere;
                   }),
           "physical inventory retained atmosphere, omitted a volume, or changed "
           "ascending ID order");
    verify_chamber_mapping(baseline, request.engine, baseline_core);

    std::ranges::reverse(request.engine.gas_volumes);
    std::ranges::rotate(request.engine.cylinders,
                        request.engine.cylinders.begin() + 2U);
    auto &reordered_core = test::low_order_core(request.engine);
    std::ranges::rotate(reordered_core.mechanism.cylinders,
                        reordered_core.mechanism.cylinders.begin() + 2U);
    const auto reordered = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    expect(reordered.physical_gas_volume_ids == baseline.physical_gas_volume_ids &&
               reordered.cylinder_chambers == baseline.cylinder_chambers,
           "physical inventory or chamber mapping depends on EngineSpec vector "
           "order");
    expect(reordered.capture_buffer.gas_volumes.front().id ==
                   request.engine.gas_volumes.front().id &&
               reordered.capture_buffer.gas_volumes.back().id ==
                   request.engine.gas_volumes.back().id,
           "public capture layout stopped retaining EngineSpec order");
    expect(reordered.capture_buffer.cylinders.front() ==
                   request.engine.cylinders.front().id &&
               reordered.capture_buffer.cylinders.back() ==
                   request.engine.cylinders.back().id,
           "public cylinder capture layout stopped retaining EngineSpec order");
    verify_chamber_mapping(reordered, request.engine, reordered_core);

    auto &rpm = std::get<contract::FixedRateRpmTrajectory>(
        std::get<contract::PrescribedKinematicSweep>(request.scenario.mode)
            .trajectory.rpm);
    rpm.post_step_rpm.assign(340000U, 2400.0);
    rpm.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(rpm.post_step_rpm);
    const auto mechanism_plan = require_mechanism_plan(
        simulation::compile_mechanism_kinematics_plan(request.engine, reordered_core));
    auto core_runtime = simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, reordered_core, random_plan(request),
        mechanism_plan, finite_extent(request.scenario));
    if (const auto *report = std::get_if<contract::ValidationReport>(&core_runtime)) {
        std::string message =
            "matching EngineSpec/core cylinder permutation is not executable";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
}

void test_nonphysical_chamber_topology_is_rejected(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_request(canonical);
    auto &core = test::low_order_core(request.engine);
    const auto atmosphere =
        std::ranges::find_if(request.engine.gas_volumes, [](const auto &volume) {
            return volume.kind.value == contract::GasVolumeKind::atmosphere;
        });
    expect(atmosphere != request.engine.gas_volumes.end(),
           "fixture has no atmosphere volume");
    core.mechanism.cylinders.front().topology.chamber_volume_id = atmosphere->id;

    const auto result = simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario));
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.code ==
                                  contract::ContractIssueCode::inconsistent_semantics &&
                              issue.path == "engine.cylinders[0]";
                   }),
           "nonphysical cylinder-chamber topology was admitted");
}

void test_cylinder_chamber_requires_concrete_engine_role(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_request(canonical);
    const auto &core = test::low_order_core(request.engine);
    const auto chamber_id = core.mechanism.cylinders.front().topology.chamber_volume_id;
    const auto chamber = std::ranges::find(request.engine.gas_volumes, chamber_id,
                                           &contract::GasVolumeSpec::id);
    expect(chamber != request.engine.gas_volumes.end(),
           "fixture has no first-cylinder chamber");
    chamber->kind.value = contract::GasVolumeKind::exhaust_collector;

    const auto result = simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario));
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.code ==
                                  contract::ContractIssueCode::inconsistent_semantics &&
                              issue.path == "engine.cylinders[0]";
                   }),
           "cylinder chamber was admitted with a non-cylinder EngineSpec role");
}

void test_intake_route_binds_plenum_and_both_boundary_edges(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_request(canonical);
    const auto &core = test::low_order_core(request.engine);
    expect(!core.gas_path.intakes.empty(), "fixture has no intake gas lane");
    const auto &intake = core.gas_path.intakes.front();
    const auto intake_route_index = insert_test_intake_route(request, 1U);

    const auto plan = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    expect(plan.capture_buffer.routes.size() == request.engine.routes.size() &&
               plan.capture_buffer.route_bindings.size() ==
                   request.engine.routes.size() &&
               plan.capture_buffer.gas_exhaust_route_count ==
                   core.gas_path.exhaust_routes.size(),
           "intake route changed the physical exhaust-lane count");
    const auto &binding = plan.capture_buffer.route_bindings[intake_route_index];
    expect(binding.kind == contract::SourceRouteKind::intake_inlet &&
               !binding.gas_route_index.has_value() &&
               binding.secondary_boundary_edge_index.has_value() &&
               binding.effective_area_m2 == 0.0,
           "intake route did not retain its separate plenum-boundary topology");
    expect(
        plan.capture_buffer.gas_volumes[binding.source_volume_index].id ==
                intake.topology.plenum_volume_id &&
            plan.capture_buffer.flow_edges[binding.primary_boundary_edge_index].id ==
                intake.topology.main_throttle_edge_id &&
            plan.capture_buffer.flow_edges[*binding.secondary_boundary_edge_index].id ==
                intake.topology.idle_bypass_edge_id,
        "intake route did not bind plenum, main-throttle, and idle-bypass "
        "identities exactly");
}

void test_intake_route_append_uses_plenum_state_and_both_boundary_flows(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_request(canonical);
    const auto intake_route_index = insert_test_intake_route(request, 1U);
    auto plan = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    const auto &capture_plan = plan.capture_buffer;

    expect(capture_plan.rate == contract::RationalRateHz{20000U, 1U},
           "intake capture test stopped exercising the canonical 20 kHz clock");
    expect(capture_plan.routes.size() == 3U && intake_route_index == 1U &&
               capture_plan.routes[0].kind ==
                   contract::SourceRouteKind::exhaust_outlet &&
               capture_plan.routes[1].kind == contract::SourceRouteKind::intake_inlet &&
               capture_plan.routes[2].kind == contract::SourceRouteKind::exhaust_outlet,
           "intake route is not interleaved between both exhaust routes");

    simulation::LegacyMechanismStep mechanics;
    mechanics.rate = capture_plan.rate;
    mechanics.sample_index = 0U;
    mechanics.step_end_index = 1U;
    mechanics.timestamp_tick = 1U;
    mechanics.cylinders.reserve(capture_plan.cylinders.size());
    for (const auto cylinder_id : capture_plan.cylinders) {
        simulation::MechanismCylinderSample cylinder;
        cylinder.cylinder_id = cylinder_id;
        mechanics.cylinders.push_back(cylinder);
    }

    simulation::LegacyLowOrderGasStep gas;
    gas.rate = capture_plan.rate;
    gas.sample_index = 0U;
    gas.step_end_index = 1U;
    gas.timestamp_tick = 1U;
    gas.cylinders.reserve(capture_plan.cylinders.size());
    for (const auto cylinder_id : capture_plan.cylinders) {
        simulation::LegacyCylinderGasStepState cylinder;
        cylinder.cylinder_id = cylinder_id;
        gas.cylinders.push_back(cylinder);
    }

    gas.gas_volumes.reserve(capture_plan.gas_volumes.size());
    for (std::size_t index = 0; index < capture_plan.gas_volumes.size(); ++index) {
        const auto &identity = capture_plan.gas_volumes[index];
        simulation::LegacyGasVolumeStepState volume;
        volume.gas_volume_id = identity.id;
        volume.kind = identity.kind;
        volume.physically_resolved =
            identity.kind != contract::GasVolumeKind::atmosphere;
        if (volume.physically_resolved) {
            volume.cell = simulation::legacy_initialize_gas_cell(
                90000.0 + static_cast<double>(index), 0.004, 290.0);
        }
        gas.gas_volumes.push_back(volume);
    }

    gas.flow_edges.reserve(capture_plan.flow_edges.size());
    for (const auto &identity : capture_plan.flow_edges) {
        simulation::LegacyFlowEdgeStepState edge;
        edge.flow_edge_id = identity.id;
        edge.endpoint_0_volume_id = identity.endpoint_0_volume_id;
        edge.endpoint_1_volume_id = identity.endpoint_1_volume_id;
        gas.flow_edges.push_back(edge);
    }

    const auto &intake_binding = capture_plan.route_bindings[intake_route_index];
    expect(intake_binding.secondary_boundary_edge_index.has_value(),
           "compiled intake route lost its idle-bypass edge");
    constexpr double kPlenumPressurePa = 123456.0;
    constexpr double kPlenumTemperatureK = 333.25;
    constexpr double kMainAmountMol = 0.00031;
    constexpr double kIdleAmountMol = 0.00007;
    gas.gas_volumes[intake_binding.source_volume_index].cell =
        simulation::legacy_initialize_gas_cell(kPlenumPressurePa, 0.0065,
                                               kPlenumTemperatureK);
    gas.flow_edges[intake_binding.primary_boundary_edge_index].signed_amount_mol =
        kMainAmountMol;
    gas.flow_edges[*intake_binding.secondary_boundary_edge_index].signed_amount_mol =
        kIdleAmountMol;

    gas.exhaust_routes.resize(capture_plan.gas_exhaust_route_count);
    for (std::size_t route_index = 0; route_index < capture_plan.route_bindings.size();
         ++route_index) {
        const auto &binding = capture_plan.route_bindings[route_index];
        if (binding.kind != contract::SourceRouteKind::exhaust_outlet) {
            continue;
        }
        expect(binding.gas_route_index.has_value(),
               "compiled exhaust route lost its gas-lane index");
        auto &route = gas.exhaust_routes[*binding.gas_route_index];
        route.route_id = capture_plan.routes[route_index].id;
        route.collector_volume_id =
            capture_plan.gas_volumes[binding.source_volume_index].id;
        route.collector_outlet_edge_id =
            capture_plan.flow_edges[binding.primary_boundary_edge_index].id;
        route.collector_cross_section_area_m2 = binding.effective_area_m2;
        gas.flow_edges[binding.primary_boundary_edge_index].signed_amount_mol =
            0.001 * static_cast<double>(*binding.gas_route_index + 1U);
    }

    simulation::detail::LowOrderCaptureBuffer buffer{capture_plan};
    buffer.begin_block(0U);
    const auto failure = buffer.append(mechanics, gas, contract::TorqueTelemetry{});
    expect(!failure.has_value(),
           failure.has_value()
               ? "interleaved intake capture append failed: " + failure->detail_code +
                     "; " + failure->state_summary
               : "interleaved intake capture append failed");

    const auto block = buffer.view();
    expect(block.frame_count() == 1U && block.layout().routes().size() == 3U &&
               block.source_routes().size() == 3U,
           "interleaved append did not publish one complete route frame");
    for (std::size_t route_index = 0; route_index < 3U; ++route_index) {
        expect(block.layout().routes()[route_index].id ==
                       request.engine.routes[route_index].id &&
                   block.layout().routes()[route_index].kind ==
                       request.engine.routes[route_index].kind.value &&
                   block.gas_source_route_sample(0U, route_index) != nullptr,
               "append did not preserve the interleaved public route order");
    }

    const auto *intake = block.gas_source_route_sample(0U, intake_route_index);
    expect(intake != nullptr, "append did not publish the intake gas sample");
    const auto &plenum = gas.gas_volumes[intake_binding.source_volume_index].cell;
    const double step_s = static_cast<double>(capture_plan.rate.denominator) /
                          static_cast<double>(capture_plan.rate.numerator);
    const double expected_flow_kg_s = -(
        ((kMainAmountMol + kIdleAmountMol) * simulation::kLegacyAirMolarMassKgPerMol) /
        step_s);
    expect(intake->pressure_pa_abs == simulation::legacy_gas_pressure_pa(plenum) &&
               intake->temperature_k == simulation::legacy_gas_temperature_k(plenum) &&
               intake->signed_mass_flow_kg_s == expected_flow_kg_s &&
               intake->effective_area_m2 == 0.0 &&
               !std::signbit(intake->effective_area_m2),
           "intake append did not use plenum thermodynamics, negate the summed "
           "main+idle 20 kHz flow, or retain canonical +0.0 exterior area");

    for (const std::size_t route_index : {0U, 2U}) {
        const auto &binding = capture_plan.route_bindings[route_index];
        const auto *sample = block.gas_source_route_sample(0U, route_index);
        expect(sample != nullptr &&
                   sample->pressure_pa_abs ==
                       simulation::legacy_gas_pressure_pa(
                           gas.gas_volumes[binding.source_volume_index].cell),
               "interleaved intake append displaced an exhaust route sample");
    }
}

void test_capacity_derivation_is_shape_driven(
    const test::AuthoredEngineFixture &canonical) {
    expect(simulation::maximum_low_order_events_per_frame(6U) ==
                   std::optional<std::uint32_t>{19U} &&
               simulation::maximum_low_order_events_per_frame(8U) ==
                   std::optional<std::uint32_t>{25U} &&
               simulation::maximum_low_order_events_per_frame(85U) ==
                   std::optional<std::uint32_t>{256U} &&
               !simulation::maximum_low_order_events_per_frame(86U).has_value(),
           "low-order event capacity is not 3*cylinders+1 within uint8 ordinal "
           "capacity");

    auto request = make_request(canonical);
    request.scenario.quality.value.capture_block_capacity_frames = 37U;
    request.scenario.quality.value.event_journal_capacity_records = 37U * 19U;
    const auto plan = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    expect(plan.capture_buffer.declared_block_capacity_frames == 37U &&
               plan.capture_buffer.declared_event_capacity_records == 37U * 19U &&
               plan.capture_buffer.maximum_events_per_frame == 19U,
           "capture plan retained the canonical 400/7600 transport ceiling");

    const auto canonical_profile_id = request.engine.profile_id.value;
    request.scenario.engine_profile_id = canonical_profile_id + "-foreign";
    const auto transplanted = simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario));
    const auto *transplant_report =
        std::get_if<contract::ValidationReport>(&transplanted);
    expect(transplant_report != nullptr && !transplant_report->ok() &&
               std::ranges::any_of(transplant_report->issues,
                                   [](const auto &issue) {
                                       return issue.path ==
                                              "scenario.engine_profile_id";
                                   }),
           "capture plan admitted a scenario from a different engine profile");

    request.scenario.engine_profile_id = canonical_profile_id;
    --request.scenario.quality.value.event_journal_capacity_records;
    const auto insufficient = simulation::compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario));
    const auto *report = std::get_if<contract::ValidationReport>(&insufficient);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(report->issues,
                                   [](const auto &issue) {
                                       return issue.path ==
                                              "scenario.quality.value."
                                              "event_journal_capacity_records";
                                   }),
           "capture plan admitted a journal smaller than the full-block worst case");
}

void run_tests(const test::AuthoredEngineFixture &canonical) {
    test_capacity_derivation_is_shape_driven(canonical);
    test_physical_inventory_is_stable_and_excludes_atmosphere(canonical);
    test_nonphysical_chamber_topology_is_rejected(canonical);
    test_cylinder_chamber_requires_concrete_engine_role(canonical);
    test_intake_route_binds_plenum_and_both_boundary_edges(canonical);
    test_intake_route_append_uses_plenum_state_and_both_boundary_flows(canonical);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const auto canonical = test::load_canonical_authored_engine_fixture(
            std::filesystem::canonical(argv[1]));
        run_tests(canonical);
    } catch (const std::exception &error) {
        std::cerr << "low_order_capture_plan_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
