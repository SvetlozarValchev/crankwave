#include "authored_engine_fixture_support.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"

#include <algorithm>
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

using namespace engine_sim_offline;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] test::AuthoredEngineFixture
make_request(const test::AuthoredEngineFixture &canonical) {
    auto request =
        test::make_prescribed_fixture(canonical, std::vector<double>(200U, 2400.0));
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

[[nodiscard]] contract::RandomPlan
random_plan(const test::AuthoredEngineFixture &request) {
    return test::compile_fixture_random_plan(request);
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
    const auto baseline = require_plan(
        simulation::compile_low_order_capture_plan(request.engine, request.scenario));
    const auto expected_horizon = contract::resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);

    expect(expected_horizon.has_value() &&
               baseline.engine_profile_id == request.engine.profile_id.value &&
               baseline.scenario_id == request.scenario.scenario_id &&
               baseline.capture_horizon_frames == *expected_horizon &&
               baseline.capture_buffer.declared_block_capacity_frames == 200U &&
               baseline.capture_buffer.declared_event_capacity_records == 3800U &&
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
    const auto reordered = require_plan(
        simulation::compile_low_order_capture_plan(request.engine, request.scenario));
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
    rpm.post_step_rpm.assign(170000U, 2400.0);
    rpm.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(rpm.post_step_rpm);
    auto core_runtime = simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, reordered_core, random_plan(request));
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

    const auto result =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
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

    const auto result =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
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
    const auto plan = require_plan(
        simulation::compile_low_order_capture_plan(request.engine, request.scenario));
    expect(plan.capture_buffer.declared_block_capacity_frames == 37U &&
               plan.capture_buffer.declared_event_capacity_records == 37U * 19U &&
               plan.capture_buffer.maximum_events_per_frame == 19U,
           "capture plan retained the former 200/3800 transport ceiling");

    const auto canonical_profile_id = request.engine.profile_id.value;
    request.scenario.engine_profile_id = canonical_profile_id + "-foreign";
    const auto transplanted =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
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
    const auto insufficient =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
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
