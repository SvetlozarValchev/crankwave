#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/legacy_gas_primitives.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

template <class Range, class Predicate>
[[nodiscard]] bool same_sequence(const Range &left, const Range &right,
                                 Predicate same_element) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_element(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
same_capture_topology(const detail::LowOrderCaptureBufferPlan &left,
                      const detail::LowOrderCaptureBufferPlan &right) {
    const bool same_cylinder_bindings = same_sequence(
        left.cylinder_bindings, right.cylinder_bindings,
        [](const auto &a, const auto &b) {
            return a.chamber_volume_index == b.chamber_volume_index &&
                   a.exhaust_primary_volume_index == b.exhaust_primary_volume_index;
        });
    const bool same_port_bindings = same_sequence(
        left.port_bindings, right.port_bindings, [](const auto &a, const auto &b) {
            return a.cylinder_index == b.cylinder_index &&
                   a.duct_volume_index == b.duct_volume_index &&
                   a.valve_edge_index == b.valve_edge_index && a.kind == b.kind;
        });
    const bool same_route_bindings = same_sequence(
        left.route_bindings, right.route_bindings, [](const auto &a, const auto &b) {
            return a.gas_route_index == b.gas_route_index &&
                   a.source_volume_index == b.source_volume_index &&
                   a.outlet_edge_index == b.outlet_edge_index;
        });
    return left.cylinders == right.cylinders && left.ports == right.ports &&
           left.gas_volumes == right.gas_volumes &&
           left.flow_edges == right.flow_edges && left.routes == right.routes &&
           same_cylinder_bindings && same_port_bindings && same_route_bindings;
}

[[nodiscard]] std::optional<std::size_t>
find_capture_volume_index(const LowOrderCapturePlan &capture_plan,
                          contract::GasVolumeId id) {
    const auto found = std::ranges::find(capture_plan.capture_buffer.gas_volumes, id,
                                         &contract::GasVolumeIdentity::id);
    if (found == capture_plan.capture_buffer.gas_volumes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found -
                                    capture_plan.capture_buffer.gas_volumes.begin());
}

[[nodiscard]] const contract::CylinderSpec *
find_cylinder(const contract::EngineSpec &engine, contract::CylinderId cylinder_id) {
    const auto found =
        std::ranges::find(engine.cylinders, cylinder_id, &contract::CylinderSpec::id);
    return found == engine.cylinders.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::LegacyCylinderAssembly *
find_core_cylinder(const contract::LowOrderEngineCoreV1 &core,
                   contract::CylinderId cylinder_id) {
    const auto found =
        std::ranges::find_if(core.mechanism.cylinders, [&](const auto &assembly) {
            return assembly.topology.cylinder_id == cylinder_id;
        });
    return found == core.mechanism.cylinders.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::LegacyExhaustRouteProfile *
find_core_route(const contract::LowOrderEngineCoreV1 &core,
                contract::RouteId route_id) {
    const auto found =
        std::ranges::find_if(core.gas_path.exhaust_routes, [&](const auto &route) {
            return route.topology.route_id == route_id;
        });
    return found == core.gas_path.exhaust_routes.end() ? nullptr : &*found;
}

} // namespace

LowOrderOperatingPointV1CompileResult compile_low_order_operating_point_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256) {
    ValidationReport report;
    report.append(contract::validate_for_engine(scenario, engine));

    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *held = std::get_if<contract::HeldSpeed>(&scenario.mode);
    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "operating runtime requires low_order_operating_point_v1");
    require(report, held != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode", "operating runtime requires held-speed mode");
    require(report, preparation != nullptr, ContractIssueCode::unsupported_value,
            "scenario.preparation",
            "operating runtime requires fixed-horizon cycle sampling");
    require(report, !simulation_request_identity_v3_sha256.is_zero(),
            ContractIssueCode::missing_value, "simulation_request_identity_v3_sha256",
            "operating runtime requires the canonical nonzero request identity");
    if (profile == nullptr || held == nullptr || preparation == nullptr) {
        return report;
    }
    const auto *direct =
        std::get_if<contract::DirectThrottleControllerV1>(
            &profile->core.throttle_controller);
    require(report, direct != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile.throttle_controller",
            "held operating-point accounting currently requires direct throttle");
    if (direct == nullptr) {
        return report;
    }

    const double expected_mass_afr = legacy_pseudo_gas_stoichiometric_mass_afr(
        profile->core.fuel.molecular_afr.value,
        profile->core.fuel.molecular_mass_kg_per_mol.value);
    require(report,
            std::bit_cast<std::uint64_t>(
                scenario.fuel.stoichiometric_air_fuel_mass_ratio.value) ==
                std::bit_cast<std::uint64_t>(expected_mass_afr),
            ContractIssueCode::inconsistent_semantics,
            "scenario.fuel.stoichiometric_air_fuel_mass_ratio.value",
            "operating runtime requires scenario stoichiometric mass AFR to exactly "
            "match the admitted pseudo-gas molecular-to-mass conversion");

    report.append(admit_implemented_cycle_accounting_methods(engine, *profile));
    require(report,
            preparation->method.value ==
                contract::fixed_horizon_cycle_sampling_method_identity(),
            ContractIssueCode::unsupported_value, "scenario.preparation.method.value",
            "operating runtime requires the exact implemented fixed-horizon "
            "sampling method");
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "operating runtime requires identical physics and capture clocks");

    auto canonical_plan_result =
        compile_low_order_capture_plan(engine, scenario, capture_plan.execution_extent);
    const auto *canonical_plan =
        std::get_if<LowOrderCapturePlan>(&canonical_plan_result);
    if (const auto *nested = std::get_if<ValidationReport>(&canonical_plan_result)) {
        report.append(*nested);
    }
    if (canonical_plan != nullptr) {
        require(
            report, capture_plan.engine_profile_id == canonical_plan->engine_profile_id,
            ContractIssueCode::inconsistent_semantics, "capture_plan.engine_profile_id",
            "capture plan belongs to a different engine profile");
        require(report, capture_plan.scenario_id == canonical_plan->scenario_id,
                ContractIssueCode::inconsistent_semantics, "capture_plan.scenario_id",
                "capture plan belongs to a different scenario");
        require(report,
                capture_plan.capture_buffer.engine_id ==
                    canonical_plan->capture_buffer.engine_id,
                ContractIssueCode::inconsistent_semantics,
                "capture_plan.capture_buffer.engine_id",
                "capture plan belongs to a different engine");
        require(report,
                capture_plan.capture_buffer.rate == canonical_plan->capture_buffer.rate,
                ContractIssueCode::inconsistent_semantics,
                "capture_plan.capture_buffer.rate",
                "capture-plan rate differs from the canonical scenario rate");
        require(
            report, capture_plan.execution_extent == canonical_plan->execution_extent,
            ContractIssueCode::inconsistent_semantics, "capture_plan.execution_extent",
            "capture-plan execution extent differs from the canonical "
            "scenario extent");
        require(
            report,
            capture_plan.capture_buffer.declared_block_capacity_frames ==
                    canonical_plan->capture_buffer.declared_block_capacity_frames &&
                capture_plan.capture_buffer.declared_event_capacity_records ==
                    canonical_plan->capture_buffer.declared_event_capacity_records &&
                capture_plan.capture_buffer.maximum_events_per_frame ==
                    canonical_plan->capture_buffer.maximum_events_per_frame,
            ContractIssueCode::inconsistent_semantics,
            "capture_plan.capture_buffer.capacity",
            "capture-plan transport capacities differ from the canonical scenario");
        require(report,
                capture_plan.physical_gas_volume_ids ==
                    canonical_plan->physical_gas_volume_ids,
                ContractIssueCode::inconsistent_semantics,
                "capture_plan.physical_gas_volume_ids",
                "capture-plan pressure inventory differs from the canonical engine");
        require(
            report, capture_plan.cylinder_chambers == canonical_plan->cylinder_chambers,
            ContractIssueCode::inconsistent_semantics, "capture_plan.cylinder_chambers",
            "capture-plan chamber bindings differ from the canonical engine");
        require(report,
                same_capture_topology(capture_plan.capture_buffer,
                                      canonical_plan->capture_buffer),
                ContractIssueCode::inconsistent_semantics,
                "capture_plan.capture_buffer.topology",
                "capture-plan public topology differs from the canonical engine");
    }

    const auto fixed_horizon_frame = contract::resolve_frame_index(
        preparation->fixed_preparation_horizon_s.value, scenario.rates.physics);
    const auto capture_horizon =
        capture_plan.execution_extent.finite_physics_frame_count();
    require(report, fixed_horizon_frame.has_value() && *fixed_horizon_frame > 0U,
            ContractIssueCode::inconsistent_semantics,
            "scenario.preparation.fixed_preparation_horizon_s.value",
            "operating preparation horizon must resolve to a positive integral "
            "physics frame");
    require(report,
            fixed_horizon_frame.has_value() && capture_horizon.has_value() &&
                *fixed_horizon_frame <= *capture_horizon,
            ContractIssueCode::inconsistent_semantics, "capture_plan.execution_extent",
            "capture horizon must include the complete fixed-horizon transaction");
    if (!report.ok() || canonical_plan == nullptr || !fixed_horizon_frame.has_value()) {
        return report;
    }

    std::vector<OperatingCylinderAccountingPlan> cylinders;
    cylinders.reserve(capture_plan.cylinder_chambers.size());
    double stroke_m = 0.0;
    for (std::size_t index = 0; index < capture_plan.cylinder_chambers.size();
         ++index) {
        const auto &binding = capture_plan.cylinder_chambers[index];
        const auto *cylinder = find_cylinder(engine, binding.cylinder_id);
        require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                "capture_plan.cylinder_chambers[" + std::to_string(index) + "]",
                "operating chamber binding references an unknown cylinder");
        if (cylinder == nullptr) {
            continue;
        }
        const double displacement_m3 = std::numbers::pi_v<double> *
                                       cylinder->bore_m.value * cylinder->bore_m.value *
                                       cylinder->stroke_m.value / 4.0;
        if (index == 0U) {
            stroke_m = cylinder->stroke_m.value;
        }
        require(report,
                std::bit_cast<std::uint64_t>(cylinder->stroke_m.value) ==
                    std::bit_cast<std::uint64_t>(stroke_m),
                ContractIssueCode::inconsistent_semantics, "engine.cylinders",
                "operating runtime requires bit-identical cylinder strokes");
        cylinders.push_back({
            binding.cylinder_id,
            binding.chamber_volume_id,
            displacement_m3,
        });
    }

    std::vector<std::size_t> physical_gas_step_indices;
    std::vector<OperatingGasVolumePressureSample> pressure_samples;
    physical_gas_step_indices.reserve(capture_plan.physical_gas_volume_ids.size());
    pressure_samples.reserve(capture_plan.physical_gas_volume_ids.size());
    for (std::size_t index = 0; index < capture_plan.physical_gas_volume_ids.size();
         ++index) {
        const auto id = capture_plan.physical_gas_volume_ids[index];
        const auto gas_index = find_capture_volume_index(capture_plan, id);
        require(report, gas_index.has_value(), ContractIssueCode::dangling_reference,
                "capture_plan.physical_gas_volume_ids[" + std::to_string(index) + "]",
                "physical sampling volume is absent from capture topology");
        if (gas_index.has_value()) {
            physical_gas_step_indices.push_back(*gas_index);
            pressure_samples.push_back({id, 0.0});
        }
    }

    LowOrderOperatingPointV1Runtime::TransactionShape transaction_shape;
    transaction_shape.gas_volumes = capture_plan.capture_buffer.gas_volumes;
    transaction_shape.flow_edges = capture_plan.capture_buffer.flow_edges;
    transaction_shape.cylinders.reserve(engine.cylinders.size());
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto *assembly =
            find_core_cylinder(profile->core, engine.cylinders[index].id);
        require(report, assembly != nullptr, ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "]",
                "operating transaction shape cannot bind the core cylinder");
        if (assembly == nullptr) {
            continue;
        }
        transaction_shape.cylinders.push_back({
            assembly->topology.cylinder_id,
            assembly->topology.exhaust_route_id,
            assembly->topology.intake_port_id,
            assembly->topology.exhaust_port_id,
            assembly->topology.intake_runner_volume_id,
            assembly->topology.chamber_volume_id,
            assembly->topology.exhaust_primary_volume_id,
        });
    }
    transaction_shape.exhaust_routes.reserve(engine.routes.size());
    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        const auto *route = find_core_route(profile->core, engine.routes[index].id);
        require(report, route != nullptr, ContractIssueCode::dangling_reference,
                "engine.routes[" + std::to_string(index) + "]",
                "operating transaction shape cannot bind the core route");
        if (route == nullptr) {
            continue;
        }
        transaction_shape.exhaust_routes.push_back({
            route->topology.route_id,
            route->topology.collector_volume_id,
            route->topology.collector_outlet_edge_id,
        });
    }

    auto accountant_result = compile_operating_cycle_accountant({
        {
            profile->core.mechanism.crank.crank_tdc_reference_rad.value,
            engine.total_displacement_m3.value,
        },
        {
            profile->aggregate_loss.constant_fmep_bar.value,
            profile->aggregate_loss.peak_pressure_coefficient.value,
            profile->aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value,
            profile->aggregate_loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2
                .value,
        },
        held->engine_speed_rpm.value,
        stroke_m,
        true,
        contract::indicated_gas_torque_term_mask(),
        profile->aggregate_loss.included_terms.value,
        profile->starter.included_terms.value,
        std::move(cylinders),
        capture_plan.physical_gas_volume_ids,
    });
    if (const auto *error =
            std::get_if<OperatingCycleAccountingError>(&accountant_result)) {
        report.add(ContractIssueCode::unsupported_value, "engine.physics_profile",
                   "operating accountant rejected the admitted profile; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    auto sampling_result = compile_fixed_horizon_cycle_sampler({
        preparation->method.value,
        preparation->trailing_complete_cycle_count.value,
        preparation->fixed_preparation_horizon_s.value,
        capture_plan.physical_gas_volume_ids,
    });
    if (const auto *error =
            std::get_if<FixedHorizonCycleSamplingError>(&sampling_result)) {
        report.add(ContractIssueCode::unsupported_value, "scenario.preparation",
                   "fixed-horizon sampler rejected the admitted preparation; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    contract::HeldSpeedOperatingPointConditions conditions{
        engine.profile_id.value,
        held->engine_speed_rpm.value,
        held->initial_theta_rad.value,
        profile->core.mechanism.crank.crank_tdc_reference_rad.value,
        held->throttle_01.value,
        scenario.rates.physics,
        {
            scenario.ambient.pressure_pa_abs.value,
            scenario.ambient.temperature_k.value,
            scenario.ambient.relative_humidity_01.value,
        },
        {
            scenario.fuel.fuel_id.value,
            scenario.fuel.lower_heating_value_j_per_kg.value,
            scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
        },
        {
            scenario.initial_thermal_state.gas_temperature_k.value,
            scenario.initial_thermal_state.wall_temperature_k.value,
            scenario.initial_thermal_state.coolant_temperature_k.value,
            scenario.initial_thermal_state.oil_temperature_k.value,
        },
        {
            scenario.crankcase.pressure_pa_abs.value,
            scenario.crankcase.temperature_k.value,
        },
        engine.total_displacement_m3.value,
        {
            profile->accessory_configuration.configuration_id.value,
            profile->accessory_configuration.content_sha256.value,
        },
        true,
        profile->starter.included_terms.value,
    };

    return LowOrderOperatingPointV1Runtime{
        std::get<OperatingCycleAccountant>(std::move(accountant_result)),
        std::get<FixedHorizonCycleSampler>(std::move(sampling_result)),
        std::move(physical_gas_step_indices),
        std::move(pressure_samples),
        std::move(transaction_shape),
        *fixed_horizon_frame,
        simulation_request_identity_v3_sha256,
        std::move(conditions),
        direct->gamma.value,
        profile->core.gas_path.intake.idle_throttle_plate_position_01.value,
        "low-order-operating-point-v1",
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
