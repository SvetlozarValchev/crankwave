#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/low_order_capture_plan.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
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

[[nodiscard]] profiles::BmwM52b28ParityRequest make_request() {
    return profiles::detail::build_bmw_m52b28_parity_request_unvalidated(
        std::vector<double>(200U, 2400.0));
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

void test_physical_inventory_is_stable_and_excludes_atmosphere() {
    auto request = make_request();
    const auto &core =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile)
            .core;
    const auto expected = expected_physical_inventory(request.engine);
    const auto baseline = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, core, request.scenario));
    const auto expected_horizon = contract::resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);

    expect(expected_horizon.has_value() &&
               baseline.capture_horizon_frames == *expected_horizon &&
               baseline.capture_buffer.declared_block_capacity_frames == 200U &&
               baseline.capture_buffer.declared_event_capacity_records == 3800U,
           "capture horizon or bounded storage capacities changed");
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
    verify_chamber_mapping(baseline, request.engine, core);

    std::ranges::reverse(request.engine.gas_volumes);
    std::ranges::rotate(request.engine.cylinders,
                        request.engine.cylinders.begin() + 2U);
    const auto reordered = require_plan(simulation::compile_low_order_capture_plan(
        request.engine, core, request.scenario));
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
    verify_chamber_mapping(reordered, request.engine, core);
}

void test_nonphysical_chamber_topology_is_rejected() {
    auto request = make_request();
    auto core =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile)
            .core;
    const auto atmosphere =
        std::ranges::find_if(request.engine.gas_volumes, [](const auto &volume) {
            return volume.kind.value == contract::GasVolumeKind::atmosphere;
        });
    expect(atmosphere != request.engine.gas_volumes.end(),
           "fixture has no atmosphere volume");
    core.mechanism.cylinders.front().topology.chamber_volume_id = atmosphere->id;

    const auto result = simulation::compile_low_order_capture_plan(request.engine, core,
                                                                   request.scenario);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.code ==
                                  contract::ContractIssueCode::inconsistent_shape &&
                              issue.path == "engine.gas_volumes";
                   }),
           "nonphysical cylinder-chamber topology was admitted");
}

void test_physical_inventory_requires_exact_topology_roles() {
    auto request = make_request();
    const auto &core =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile)
            .core;
    const auto collector =
        std::ranges::find_if(request.engine.gas_volumes, [](const auto &volume) {
            return volume.kind.value == contract::GasVolumeKind::exhaust_collector;
        });
    expect(collector != request.engine.gas_volumes.end(),
           "fixture has no exhaust collector");
    collector->kind.value = contract::GasVolumeKind::cylinder;

    const auto result = simulation::compile_low_order_capture_plan(request.engine, core,
                                                                   request.scenario);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.code ==
                                  contract::ContractIssueCode::inconsistent_shape &&
                              issue.path == "engine.gas_volumes";
                   }),
           "EngineSpec physical role inventory diverged from topology without "
           "rejection");
}

void run_tests() {
    test_physical_inventory_is_stable_and_excludes_atmosphere();
    test_nonphysical_chamber_topology_is_rejected();
    test_physical_inventory_requires_exact_topology_roles();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "low_order_capture_plan_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
