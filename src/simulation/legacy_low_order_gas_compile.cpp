#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"

#include "simulation/legacy_flow_calibration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

constexpr double kLegacyBoundaryWorkVolumeM3 = 1000.0;
struct AdmittedRoute {
    std::size_t public_route_index = 0;
    std::size_t collector_volume_index = 0;
    std::size_t collector_outlet_edge_index = 0;
    double collector_volume_m3 = 0.0;
    double collector_cross_section_area_m2 = 0.0;
    double exhaust_system_length_m = 0.0;
    double primary_tube_length_m = 0.0;
    double velocity_decay = 0.0;
    double primary_to_collector_k = 0.0;
    double collector_outlet_k = 0.0;
};

struct AdmittedCylinder {
    std::size_t intake_runner_volume_index = 0;
    std::size_t chamber_volume_index = 0;
    std::size_t exhaust_primary_volume_index = 0;
    std::size_t plenum_to_runner_edge_index = 0;
    std::size_t intake_valve_edge_index = 0;
    std::size_t exhaust_valve_edge_index = 0;
    std::size_t primary_to_collector_edge_index = 0;
    std::size_t blowby_edge_index = 0;
    std::size_t route_lane_index = 0;
    double bore_m = 0.0;
    double piston_area_m2 = 0.0;
    double fixed_geometry_volume_m3 = 0.0;
    double initial_chamber_volume_m3 = 0.0;
    double intake_runner_volume_m3 = 0.0;
    double exhaust_primary_volume_m3 = 0.0;
    double intake_runner_cross_section_area_m2 = 0.0;
    double exhaust_runner_cross_section_area_m2 = 0.0;
    std::uint64_t pcg32_initial_state = 0;
    std::uint64_t pcg32_stream = 0;
};

struct AdmittedCylinderMechanismGeometry {
    double bore_m = 0.0;
    double piston_area_m2 = 0.0;
    double fixed_geometry_volume_m3 = 0.0;
    double initial_chamber_volume_m3 = 0.0;
};

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

void append_prefixed(ValidationReport &destination, const ValidationReport &source,
                     const std::string &prefix) {
    for (const auto &source_issue : source.issues) {
        auto issue = source_issue;
        issue.path = issue.path.empty() ? prefix : prefix + "." + issue.path;
        destination.issues.push_back(std::move(issue));
    }
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool finite_nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool unit_interval(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool
exact_legacy_method(const contract::ResolvedValue<contract::MethodIdentity> &method) {
    return method.value == contract::legacy_low_order_v1_method_identity();
}

template <class Range, class Id>
[[nodiscard]] std::optional<std::size_t> find_id_index(const Range &range,
                                                       Id id) noexcept {
    const auto found = std::find_if(range.begin(), range.end(),
                                    [&](const auto &item) { return item.id == id; });
    if (found == range.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - range.begin());
}

template <class Range>
void admit_public_identities(const Range &range, ValidationReport &report,
                             const std::string &path) {
    std::unordered_set<std::uint32_t> ids;
    for (std::size_t index = 0; index < range.size(); ++index) {
        const auto id = range[index].id;
        require(report, id.valid(), ContractIssueCode::invalid_value,
                path + "[" + std::to_string(index) + "].id",
                "runtime identity must be nonzero");
        if (id.valid()) {
            require(report, ids.insert(id.value).second,
                    ContractIssueCode::duplicate_identity,
                    path + "[" + std::to_string(index) + "].id",
                    "runtime identities must be unique");
        }
    }
}

[[nodiscard]] bool admit_restriction(const contract::LegacyRestriction &restriction,
                                     ValidationReport &report,
                                     const std::string &path) {
    const auto calibration = restriction.calibration.value;
    const double source_rating = restriction.source_rating.value;
    const double resolved_k = restriction.resolved_k.value;
    const bool direct_values_valid = known_legacy_flow_calibration(calibration) &&
                                     finite_nonnegative(source_rating) &&
                                     finite_nonnegative(resolved_k);
    require(report, known_legacy_flow_calibration(calibration),
            ContractIssueCode::unsupported_value, path + ".calibration.value",
            "restriction calibration is not supported by legacy_low_order_v1");
    require(report, finite_nonnegative(source_rating) && finite_nonnegative(resolved_k),
            ContractIssueCode::invalid_value, path,
            "restriction rating and resolved K must be finite and nonnegative");
    if (!direct_values_valid) {
        return false;
    }
    const bool exact = same_binary64(
        resolved_k,
        legacy_flow_bench_restriction_coefficient(calibration, source_rating));
    require(report, exact, ContractIssueCode::inconsistent_semantics,
            path + ".resolved_k.value",
            "restriction K must exactly match the source calibration operation");
    return exact;
}

void admit_valve_flow_k(const std::vector<contract::LegacyValveFlowPoint> &points,
                        ValidationReport &report, const std::string &path) {
    std::unordered_set<std::string> sample_ids;
    require(report, points.size() >= 2U, ContractIssueCode::inconsistent_shape, path,
            "valve-flow table requires at least two samples");
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto &point = points[index];
        const std::string point_path = path + "[" + std::to_string(index) + "]";
        const bool direct_values_valid =
            contract::is_valid_semantic_id(point.sample_id.value) &&
            finite_nonnegative(point.lift_m.value) &&
            finite_nonnegative(point.source_cfm_at_28_inh2o.value) &&
            finite_nonnegative(point.resolved_k.value) &&
            (index == 0U || point.lift_m.value > points[index - 1U].lift_m.value);
        require(report, direct_values_valid, ContractIssueCode::invalid_value,
                point_path,
                "valve-flow samples require canonical identity, finite "
                "nonnegative data, and strictly increasing lift");
        require(report, sample_ids.insert(point.sample_id.value).second,
                ContractIssueCode::duplicate_identity, point_path + ".sample_id.value",
                "valve-flow sample identities must be unique");
        if (direct_values_valid) {
            const double expected_k = legacy_flow_bench_restriction_coefficient(
                contract::LegacyRestrictionCalibration::cfm_at_28_inh2o,
                point.source_cfm_at_28_inh2o.value);
            require(report, same_binary64(point.resolved_k.value, expected_k),
                    ContractIssueCode::inconsistent_semantics,
                    point_path + ".resolved_k.value",
                    "valve-flow K must exactly match its source CFM calibration");
        }
    }
}

[[nodiscard]] std::optional<std::size_t>
find_profile_route_index(const contract::LegacyGasPathProfile &gas_path,
                         contract::RouteId route_id) noexcept {
    const auto found = std::find_if(
        gas_path.exhaust_routes.begin(), gas_path.exhaust_routes.end(),
        [&](const auto &route) { return route.topology.route_id == route_id; });
    if (found == gas_path.exhaust_routes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - gas_path.exhaust_routes.begin());
}

[[nodiscard]] std::optional<std::size_t>
find_profile_head_index(const contract::LegacyGasPathProfile &gas_path,
                        contract::BankId bank_id) noexcept {
    const auto found = std::find_if(
        gas_path.heads.begin(), gas_path.heads.end(),
        [&](const auto &head) { return head.bank_id == bank_id; });
    if (found == gas_path.heads.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - gas_path.heads.begin());
}

[[nodiscard]] std::optional<std::size_t>
find_combustion_seed_index(const contract::RandomPlan &random_plan,
                           contract::CylinderId cylinder_id) noexcept {
    const auto found = std::find_if(
        random_plan.component_seeds.begin(), random_plan.component_seeds.end(),
        [&](const auto &seed) {
            return seed.kind == contract::RandomComponentKind::combustion &&
                   seed.cylinder_id == cylinder_id;
        });
    if (found == random_plan.component_seeds.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - random_plan.component_seeds.begin());
}

[[nodiscard]] bool all_bound(const std::vector<bool> &bound) noexcept {
    return std::all_of(bound.begin(), bound.end(), [](bool value) { return value; });
}

} // namespace

detail::LowOrderEngineCoreV1RuntimeFactory::GasCompileResult
detail::LowOrderEngineCoreV1RuntimeFactory::compile_gas(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core,
    const contract::RenderScenario &scenario, const contract::RandomPlan &random_plan,
    const ScenarioControlSchedule &schedule,
    SharedMechanismKinematicsPlan mechanism_plan) {
    ValidationReport report;
    const auto *direct_plan = direct_mechanism_kinematics_plan(mechanism_plan);
    const auto *radial_plan =
        one_level_master_rod_mechanism_kinematics_plan(mechanism_plan);
    const bool plan_matches_source =
        mechanism_kinematics_plan_matches_source(mechanism_plan, engine, core);
    require(report, direct_plan != nullptr || radial_plan != nullptr,
            ContractIssueCode::unsupported_value, "mechanism_plan",
            "legacy gas requires one compiled direct or one-level master-rod "
            "mechanism plan");
    require(report, plan_matches_source, ContractIssueCode::inconsistent_semantics,
            "mechanism_plan",
            "compiled mechanism plan does not exactly match its resolved "
            "engine source");
    if (radial_plan != nullptr) {
        require(report,
                std::holds_alternative<contract::PrescribedKinematicSweep>(
                    scenario.mode),
                ContractIssueCode::unsupported_value, "scenario.mode",
                "one-level master-rod gas is admitted only for prescribed "
                "kinematic sweeps");
        require(report,
                schedule.execution_extent().finite_physics_frame_count().has_value(),
                ContractIssueCode::unsupported_value, "schedule.execution_extent",
                "one-level master-rod gas requires a finite prescribed horizon");
    }
    if (!report.ok()) {
        return report;
    }

    require(report, engine.id.valid(), ContractIssueCode::invalid_value, "engine.id",
            "gas session requires a valid engine identity");
    require(report, scenario.engine_profile_id == engine.profile_id.value,
            ContractIssueCode::inconsistent_semantics, "scenario.engine_profile_id",
            "gas session requires matching engine and scenario profiles");
    require(report, engine.cycle.value == contract::EngineCycle::four_stroke,
            ContractIssueCode::unsupported_value, "engine.cycle.value",
            "legacy_low_order_v1 gas requires a four-stroke engine");
    require(report, engine.ignition.value == contract::IgnitionKind::spark_ignition,
            ContractIssueCode::unsupported_value, "engine.ignition.value",
            "legacy_low_order_v1 gas requires spark ignition");

    const std::array<
        std::pair<const contract::ResolvedValue<contract::MethodIdentity> *,
                  const char *>,
        6U>
        consumed_methods{{
            {&engine.methods.mechanism, "engine.methods.mechanism"},
            {&engine.methods.valvetrain, "engine.methods.valvetrain"},
            {&engine.methods.gas_exchange, "engine.methods.gas_exchange"},
            {&engine.methods.ignition, "engine.methods.ignition"},
            {&engine.methods.combustion, "engine.methods.combustion"},
            {&engine.methods.heat_transfer, "engine.methods.heat_transfer"},
        }};
    for (const auto &[method, path] : consumed_methods) {
        require(report, exact_legacy_method(*method),
                ContractIssueCode::unsupported_value, path,
                "gas session requires the exact legacy_low_order_v1 version 1 "
                "configuration");
    }

    require(report, scenario.rates.physics == contract::RationalRateHz{10000U, 1U},
            ContractIssueCode::unsupported_value, "scenario.rates.physics",
            "legacy_low_order_v1 gas requires exactly 10000 Hz physics");
    require(report, schedule.rate() == scenario.rates.physics,
            ContractIssueCode::inconsistent_semantics, "schedule.rate",
            "gas session requires the admitted scenario physics rate");
    require(report, schedule.first_step_index() == 0U,
            ContractIssueCode::inconsistent_semantics, "schedule.first_step_index",
            "gas session requires a schedule beginning at physics step zero");
    require(report, schedule.execution_extent().valid(),
            ContractIssueCode::inconsistent_shape, "schedule.execution_extent",
            "gas session requires a valid finite or open-ended execution extent");
    const bool supports_open_ended_execution =
        std::holds_alternative<contract::FreeEngine>(scenario.mode) ||
        std::holds_alternative<contract::HeldDyno>(scenario.mode) ||
        std::holds_alternative<contract::FreeVehicle>(scenario.mode);
    require(report,
            !schedule.execution_extent().is_open_ended() ||
                supports_open_ended_execution,
            ContractIssueCode::unsupported_value, "schedule.execution_extent",
            "open-ended gas execution is admitted only for FreeEngine, HeldDyno, or "
            "FreeVehicle");
    require(report,
            random_plan.generator == contract::pcg32_generator_method_identity(),
            ContractIssueCode::unsupported_value, "random_plan.generator",
            "gas session requires the implemented PCG32 generator");
    require(report,
            random_plan.derivation ==
                contract::component_seed_derivation_method_identity(),
            ContractIssueCode::unsupported_value, "random_plan.derivation",
            "gas session requires the implemented component-seed derivation");
    require(report, random_plan.public_seed == scenario.public_seed.value,
            ContractIssueCode::inconsistent_semantics, "random_plan.public_seed",
            "gas session random plan must belong to the scenario public seed");

    auto valvetrain_result = compile_legacy_selectable_valvetrain(engine, core);
    if (const auto *nested = std::get_if<ValidationReport>(&valvetrain_result)) {
        append_prefixed(report, *nested, "valvetrain");
    }

    admit_public_identities(engine.banks, report, "engine.banks");
    admit_public_identities(engine.cylinders, report, "engine.cylinders");
    admit_public_identities(engine.ports, report, "engine.ports");
    admit_public_identities(engine.gas_volumes, report, "engine.gas_volumes");
    admit_public_identities(engine.flow_edges, report, "engine.flow_edges");
    admit_public_identities(engine.routes, report, "engine.routes");

    const auto &mechanism = core.mechanism;
    const auto &gas_path = core.gas_path;
    require(report,
            !engine.cylinders.empty() &&
                mechanism.cylinders.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "engine and mechanism cylinder orders must be equal and nonempty");
    require(report, std::isfinite(mechanism.crank.crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank.crank_tdc_reference_rad.value",
            "crank TDC reference must be finite");
    require(report,
            std::isfinite(schedule.initial_theta_rad()) &&
                same_binary64(schedule.initial_theta_rad(),
                              mechanism.crank.crank_tdc_reference_rad.value),
            ContractIssueCode::unsupported_value, "schedule.initial_theta_rad",
            "fresh gas state requires the admitted initial angle to equal the "
            "crank TDC reference");

    require(
        report,
        finite_positive(scenario.ambient.pressure_pa_abs.value) &&
            finite_positive(scenario.ambient.temperature_k.value) &&
            finite_positive(scenario.initial_thermal_state.gas_temperature_k.value) &&
            finite_positive(scenario.initial_thermal_state.wall_temperature_k.value) &&
            finite_positive(scenario.crankcase.pressure_pa_abs.value) &&
            finite_positive(scenario.crankcase.temperature_k.value),
        ContractIssueCode::invalid_value, "scenario",
        "ambient, initial-gas, wall, and crankcase pressure/temperature inputs "
        "must be finite and positive");

    const auto &intake = gas_path.intake;
    require(report,
            finite_positive(intake.plenum_volume_m3.value) &&
                finite_positive(intake.plenum_cross_section_area_m2.value) &&
                finite_positive(intake.runner_length_m.value) &&
                finite_nonnegative(intake.velocity_decay.value),
            ContractIssueCode::invalid_value, "engine.physics_profile.gas_path.intake",
            "intake volume, areas, runner length, and velocity decay are outside "
            "the admitted domain");
    require(report,
            intake.idle_throttle_plate_position_01.value >= 0.0 &&
                intake.idle_throttle_plate_position_01.value <= 1.0,
            ContractIssueCode::invalid_value, "engine.physics_profile.gas_path.intake",
            "idle plate position must be finite in [0,1]");
    static_cast<void>(
        admit_restriction(intake.main_throttle, report,
                          "engine.physics_profile.gas_path.intake.main_throttle"));
    static_cast<void>(
        admit_restriction(intake.idle_bypass, report,
                          "engine.physics_profile.gas_path.intake.idle_bypass"));
    static_cast<void>(
        admit_restriction(intake.plenum_to_runner, report,
                          "engine.physics_profile.gas_path.intake.plenum_to_runner"));
    static_cast<void>(
        admit_restriction(gas_path.piston_blowby, report,
                          "engine.physics_profile.gas_path.piston_blowby"));

    require(report,
            gas_path.heads.size() == engine.banks.size() &&
                !gas_path.heads.empty(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.heads",
            "bank-local head profiles must exactly cover the nonempty engine bank "
            "order");
    for (std::size_t index = 0; index < gas_path.heads.size(); ++index) {
        const auto &head = gas_path.heads[index];
        const auto path = "engine.physics_profile.gas_path.heads[" +
                          std::to_string(index) + "]";
        const bool ordered_binding = index < engine.banks.size() &&
                                     head.bank_id.valid() &&
                                     head.bank_id == engine.banks[index].id;
        require(report, ordered_binding, ContractIssueCode::inconsistent_semantics,
                path + ".bank_id",
                "head profile identity/order must match the engine bank order");
        require(report,
                finite_positive(head.chamber_volume_m3.value) &&
                    finite_nonnegative(head.intake_runner_base_volume_m3.value) &&
                    finite_positive(
                        head.intake_runner_cross_section_area_m2.value) &&
                    finite_nonnegative(head.exhaust_runner_base_volume_m3.value) &&
                    finite_positive(
                        head.exhaust_runner_cross_section_area_m2.value) &&
                    finite_positive(head.intake_flow_triangle_radius_m.value) &&
                    finite_positive(head.exhaust_flow_triangle_radius_m.value),
                ContractIssueCode::invalid_value, path,
                "bank-local cylinder-head gas geometry is outside the admitted "
                "domain");
        admit_valve_flow_k(head.intake_flow, report, path + ".intake_flow");
        admit_valve_flow_k(head.exhaust_flow, report, path + ".exhaust_flow");
    }

    const auto &fuel = core.fuel;
    require(report,
            contract::is_valid_semantic_id(fuel.fuel_id.value) &&
                finite_positive(fuel.molecular_mass_kg_per_mol.value) &&
                finite_positive(fuel.energy_density_j_per_kg.value) &&
                finite_positive(fuel.molecular_afr.value) &&
                unit_interval(fuel.maximum_burning_efficiency_01.value) &&
                unit_interval(fuel.burning_efficiency_randomness_01.value) &&
                unit_interval(fuel.low_efficiency_attenuation_01.value) &&
                finite_positive(fuel.maximum_turbulence_effect.value) &&
                finite_positive(fuel.maximum_dilution_effect.value) &&
                finite_positive(fuel.lbv_multiplier.value) &&
                finite_positive(
                    fuel.turbulence_to_flame_speed_ratio_triangle_radius.value) &&
                fuel.turbulence_to_flame_speed_ratio.size() >= 2U,
            ContractIssueCode::invalid_value, "engine.physics_profile.fuel",
            "spark-gasoline fuel data are outside the admitted finite physical "
            "domain");
    require(report, scenario.fuel.fuel_id.value == fuel.fuel_id.value,
            ContractIssueCode::inconsistent_semantics, "scenario.fuel.fuel_id.value",
            "scenario and executable physics profile must select the same fuel");
    require(report,
            same_binary64(scenario.fuel.lower_heating_value_j_per_kg.value,
                          fuel.energy_density_j_per_kg.value),
            ContractIssueCode::inconsistent_semantics,
            "scenario.fuel.lower_heating_value_j_per_kg.value",
            "scenario fuel energy must exactly match the executable fuel profile");
    std::unordered_set<std::string> flame_sample_ids;
    for (std::size_t index = 0; index < fuel.turbulence_to_flame_speed_ratio.size();
         ++index) {
        const auto &point = fuel.turbulence_to_flame_speed_ratio[index];
        const std::string path =
            "engine.physics_profile.fuel.turbulence_to_flame_speed_ratio[" +
            std::to_string(index) + "]";
        const bool point_valid =
            contract::is_valid_semantic_id(point.sample_id.value) &&
            finite_nonnegative(point.turbulence.value) &&
            finite_nonnegative(point.flame_speed_ratio.value) &&
            (index == 0U ||
             point.turbulence.value >
                 fuel.turbulence_to_flame_speed_ratio[index - 1U].turbulence.value);
        require(report, point_valid, ContractIssueCode::invalid_value, path,
                "flame-speed table requires canonical identities, finite "
                "nonnegative values, and strictly increasing turbulence");
        require(report, flame_sample_ids.insert(point.sample_id.value).second,
                ContractIssueCode::duplicate_identity, path + ".sample_id.value",
                "flame-speed sample identities must be unique");
    }

    std::size_t maximum_event_count = 0;
    const bool event_count_representable =
        engine.cylinders.size() <= (std::numeric_limits<std::size_t>::max() - 1U) / 3U;
    require(report, event_count_representable, ContractIssueCode::unsupported_value,
            "engine.cylinders", "cylinder count overflows the composed event bound");
    if (event_count_representable) {
        maximum_event_count = 3U * engine.cylinders.size() + 1U;
        require(report,
                maximum_event_count <=
                    static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()) +
                        1U,
                ContractIssueCode::unsupported_value, "engine.cylinders",
                "composed event ordinals support at most 256 records per step");
    }

    std::vector<bool> bound_ports(engine.ports.size(), false);
    std::vector<bool> bound_volumes(engine.gas_volumes.size(), false);
    std::vector<bool> bound_edges(engine.flow_edges.size(), false);

    std::optional<std::size_t> atmosphere_volume_index;
    for (std::size_t index = 0; index < engine.gas_volumes.size(); ++index) {
        if (engine.gas_volumes[index].kind.value ==
            contract::GasVolumeKind::atmosphere) {
            require(report, !atmosphere_volume_index.has_value(),
                    ContractIssueCode::inconsistent_shape, "engine.gas_volumes",
                    "legacy_low_order_v1 requires one shared logical atmosphere "
                    "identity");
            if (!atmosphere_volume_index.has_value()) {
                atmosphere_volume_index = index;
            }
        }
    }
    require(report, atmosphere_volume_index.has_value(),
            ContractIssueCode::missing_value, "engine.gas_volumes",
            "legacy gas topology requires one atmosphere identity");
    if (atmosphere_volume_index.has_value()) {
        bound_volumes[*atmosphere_volume_index] = true;
    }

    const auto bind_volume =
        [&](contract::GasVolumeId id, contract::GasVolumeKind expected_kind,
            const std::string &path) -> std::optional<std::size_t> {
        const auto index = find_id_index(engine.gas_volumes, id);
        const bool exists_and_matches =
            index.has_value() && engine.gas_volumes[*index].kind.value == expected_kind;
        require(report, exists_and_matches, ContractIssueCode::inconsistent_semantics,
                path, "gas volume identity must exist with its exact specialized role");
        if (!exists_and_matches) {
            return std::nullopt;
        }
        const bool unique = !bound_volumes[*index];
        require(report, unique, ContractIssueCode::duplicate_identity, path,
                "specialized gas volume may be bound only once");
        if (!unique) {
            return std::nullopt;
        }
        bound_volumes[*index] = true;
        return index;
    };

    const auto bind_edge = [&](contract::FlowEdgeId id,
                               contract::GasVolumeId expected_endpoint_0,
                               contract::GasVolumeId expected_endpoint_1,
                               const std::string &path) -> std::optional<std::size_t> {
        const auto index = find_id_index(engine.flow_edges, id);
        const bool endpoints_match =
            index.has_value() &&
            engine.flow_edges[*index].endpoint_0_volume_id == expected_endpoint_0 &&
            engine.flow_edges[*index].endpoint_1_volume_id == expected_endpoint_1;
        require(report, endpoints_match, ContractIssueCode::inconsistent_semantics,
                path,
                "flow edge must exist in the exact declared endpoint-0 to "
                "endpoint-1 orientation");
        if (!endpoints_match) {
            return std::nullopt;
        }
        const bool unique = !bound_edges[*index];
        require(report, unique, ContractIssueCode::duplicate_identity, path,
                "specialized flow edge may be bound only once");
        if (!unique) {
            return std::nullopt;
        }
        bound_edges[*index] = true;
        return index;
    };

    const auto bind_port = [&](contract::PortId id, contract::CylinderId cylinder_id,
                               contract::PortKind expected_kind,
                               const std::string &path) -> std::optional<std::size_t> {
        const auto index = find_id_index(engine.ports, id);
        const bool binding_matches = index.has_value() &&
                                     engine.ports[*index].cylinder_id == cylinder_id &&
                                     engine.ports[*index].kind.value == expected_kind;
        require(report, binding_matches, ContractIssueCode::inconsistent_semantics,
                path, "port identity must belong to this cylinder with its exact role");
        if (!binding_matches) {
            return std::nullopt;
        }
        const bool unique = !bound_ports[*index];
        require(report, unique, ContractIssueCode::duplicate_identity, path,
                "specialized port may be bound only once");
        if (!unique) {
            return std::nullopt;
        }
        bound_ports[*index] = true;
        return index;
    };

    const auto &intake_topology = gas_path.intake_topology;
    const auto plenum_volume_index = bind_volume(
        intake_topology.plenum_volume_id, contract::GasVolumeKind::intake_plenum,
        "engine.physics_profile.gas_path.intake_topology."
        "plenum_volume_id");
    std::optional<std::size_t> main_throttle_edge_index;
    std::optional<std::size_t> idle_bypass_edge_index;
    if (atmosphere_volume_index.has_value() && plenum_volume_index.has_value()) {
        const auto atmosphere_id = engine.gas_volumes[*atmosphere_volume_index].id;
        const auto plenum_id = engine.gas_volumes[*plenum_volume_index].id;
        main_throttle_edge_index =
            bind_edge(intake_topology.main_throttle_edge_id, atmosphere_id, plenum_id,
                      "engine.physics_profile.gas_path.intake_topology."
                      "main_throttle_edge_id");
        idle_bypass_edge_index =
            bind_edge(intake_topology.idle_bypass_edge_id, atmosphere_id, plenum_id,
                      "engine.physics_profile.gas_path.intake_topology."
                      "idle_bypass_edge_id");
    }

    std::vector<std::size_t> public_exhaust_route_indices(engine.routes.size(), 0U);
    std::size_t public_exhaust_route_count = 0;
    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        if (engine.routes[index].kind.value ==
            contract::SourceRouteKind::exhaust_outlet) {
            public_exhaust_route_indices[index] = public_exhaust_route_count;
            ++public_exhaust_route_count;
        }
    }
    require(report,
            !gas_path.exhaust_routes.empty() &&
                gas_path.exhaust_routes.size() == public_exhaust_route_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.exhaust_routes",
            "gas profile must cover every exhaust-outlet route exactly once");

    std::vector<AdmittedRoute> admitted_routes(gas_path.exhaust_routes.size());
    std::vector<bool> route_used_by_cylinder(gas_path.exhaust_routes.size(), false);
    std::unordered_set<std::uint32_t> gas_route_ids;
    for (std::size_t route_index = 0; route_index < gas_path.exhaust_routes.size();
         ++route_index) {
        const auto &route = gas_path.exhaust_routes[route_index];
        const auto &topology = route.topology;
        const auto &parameters = route.parameters;
        const std::string path = "engine.physics_profile.gas_path.exhaust_routes[" +
                                 std::to_string(route_index) + "]";
        require(report,
                topology.route_id.valid() &&
                    gas_route_ids.insert(topology.route_id.value).second,
                ContractIssueCode::duplicate_identity, path + ".topology.route_id",
                "gas-path exhaust route identities must be valid and unique");

        const auto public_engine_route_index =
            find_id_index(engine.routes, topology.route_id);
        const bool public_route_matches =
            public_engine_route_index.has_value() &&
            engine.routes[*public_engine_route_index].kind.value ==
                contract::SourceRouteKind::exhaust_outlet &&
            engine.routes[*public_engine_route_index].source_volume_id ==
                std::optional<contract::GasVolumeId>{topology.collector_volume_id};
        require(report, public_route_matches, ContractIssueCode::inconsistent_semantics,
                path + ".topology",
                "exhaust route must source its declared collector volume");

        const auto collector_volume_index = bind_volume(
            topology.collector_volume_id, contract::GasVolumeKind::exhaust_collector,
            path + ".topology.collector_volume_id");
        std::optional<std::size_t> collector_outlet_edge_index;
        if (atmosphere_volume_index.has_value() && collector_volume_index.has_value()) {
            collector_outlet_edge_index =
                bind_edge(topology.collector_outlet_edge_id,
                          engine.gas_volumes[*atmosphere_volume_index].id,
                          engine.gas_volumes[*collector_volume_index].id,
                          path + ".topology.collector_outlet_edge_id");
        }

        const bool numeric_values_valid =
            finite_positive(parameters.collector_volume_m3.value) &&
            finite_positive(parameters.collector_cross_section_area_m2.value) &&
            finite_positive(parameters.exhaust_system_length_m.value) &&
            finite_nonnegative(parameters.primary_tube_length_m.value) &&
            finite_nonnegative(parameters.velocity_decay.value) &&
            finite_nonnegative(parameters.audio_volume_linear.value);
        require(report, numeric_values_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "exhaust route gas geometry and coefficients are outside the "
                "admitted domain");
        if (numeric_values_valid) {
            const double length_derived_from_volume =
                parameters.collector_volume_m3.value /
                parameters.collector_cross_section_area_m2.value;
            const double volume_derived_from_length =
                parameters.exhaust_system_length_m.value *
                parameters.collector_cross_section_area_m2.value;
            require(report,
                    same_binary64(parameters.exhaust_system_length_m.value,
                                  length_derived_from_volume) ||
                        same_binary64(parameters.collector_volume_m3.value,
                                      volume_derived_from_length),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".parameters.exhaust_system_length_m.value",
                    "collector geometry must exactly match either the "
                    "volume-divided-by-area or length-times-area derivation");
        }
        static_cast<void>(admit_restriction(parameters.primary_to_collector, report,
                                            path + ".parameters.primary_to_collector"));
        static_cast<void>(admit_restriction(parameters.collector_outlet, report,
                                            path + ".parameters.collector_outlet"));

        if (public_route_matches && collector_volume_index.has_value() &&
            collector_outlet_edge_index.has_value() && numeric_values_valid) {
            admitted_routes[route_index] = {
                public_exhaust_route_indices[*public_engine_route_index],
                *collector_volume_index,
                *collector_outlet_edge_index,
                parameters.collector_volume_m3.value,
                parameters.collector_cross_section_area_m2.value,
                parameters.exhaust_system_length_m.value,
                parameters.primary_tube_length_m.value,
                parameters.velocity_decay.value,
                parameters.primary_to_collector.resolved_k.value,
                parameters.collector_outlet.resolved_k.value,
            };
        }
    }

    std::vector<AdmittedCylinder> admitted_cylinders(engine.cylinders.size());
    std::unordered_set<std::uint32_t> mechanism_cylinder_ids;
    const std::size_t mechanism_plan_cylinder_count =
        direct_plan != nullptr ? direct_plan->cylinders.size()
                               : radial_plan->cylinders.size();
    for (std::size_t cylinder_index = 0; cylinder_index < engine.cylinders.size();
         ++cylinder_index) {
        if (cylinder_index >= mechanism.cylinders.size() ||
            cylinder_index >= mechanism_plan_cylinder_count) {
            break;
        }
        const auto &engine_cylinder = engine.cylinders[cylinder_index];
        const auto head_profile_index =
            find_profile_head_index(gas_path, engine_cylinder.bank_id);
        const auto *head = head_profile_index.has_value()
                               ? &gas_path.heads[*head_profile_index]
                               : nullptr;
        const auto &assembly = mechanism.cylinders[cylinder_index];
        const auto &topology = assembly.topology;
        const auto &parameters = assembly.parameters;
        const std::string path = "engine.physics_profile.mechanism.cylinders[" +
                                 std::to_string(cylinder_index) + "]";

        const bool cylinder_order_matches =
            topology.cylinder_id.valid() &&
            topology.cylinder_id == engine_cylinder.id &&
            mechanism_cylinder_ids.insert(topology.cylinder_id.value).second;
        require(report, cylinder_order_matches,
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "engine, mechanism, and admitted mechanics cylinder identity/order "
                "must match exactly");
        require(report, head != nullptr, ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(cylinder_index) + "].bank_id",
                "gas cylinder requires one bank-local head profile");

        static_cast<void>(bind_port(topology.intake_port_id, topology.cylinder_id,
                                    contract::PortKind::intake,
                                    path + ".topology.intake_port_id"));
        static_cast<void>(bind_port(topology.exhaust_port_id, topology.cylinder_id,
                                    contract::PortKind::exhaust,
                                    path + ".topology.exhaust_port_id"));

        const auto runner_volume_index = bind_volume(
            topology.intake_runner_volume_id, contract::GasVolumeKind::intake_runner,
            path + ".topology.intake_runner_volume_id");
        const auto chamber_volume_index =
            bind_volume(topology.chamber_volume_id, contract::GasVolumeKind::cylinder,
                        path + ".topology.chamber_volume_id");
        const auto primary_volume_index =
            bind_volume(topology.exhaust_primary_volume_id,
                        contract::GasVolumeKind::exhaust_primary,
                        path + ".topology.exhaust_primary_volume_id");

        const auto route_lane_index =
            find_profile_route_index(gas_path, topology.exhaust_route_id);
        require(report, route_lane_index.has_value(),
                ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "cylinder exhaust route must exist in the gas-path route order");
        if (route_lane_index.has_value()) {
            route_used_by_cylinder[*route_lane_index] = true;
        }

        std::optional<std::size_t> plenum_runner_edge_index;
        std::optional<std::size_t> intake_valve_edge_index;
        std::optional<std::size_t> exhaust_valve_edge_index;
        std::optional<std::size_t> primary_collector_edge_index;
        std::optional<std::size_t> blowby_edge_index;
        if (plenum_volume_index.has_value() && runner_volume_index.has_value()) {
            plenum_runner_edge_index =
                bind_edge(topology.plenum_to_runner_edge_id,
                          engine.gas_volumes[*plenum_volume_index].id,
                          engine.gas_volumes[*runner_volume_index].id,
                          path + ".topology.plenum_to_runner_edge_id");
        }
        if (runner_volume_index.has_value() && chamber_volume_index.has_value()) {
            intake_valve_edge_index =
                bind_edge(topology.intake_valve_edge_id,
                          engine.gas_volumes[*runner_volume_index].id,
                          engine.gas_volumes[*chamber_volume_index].id,
                          path + ".topology.intake_valve_edge_id");
        }
        if (chamber_volume_index.has_value() && primary_volume_index.has_value()) {
            exhaust_valve_edge_index =
                bind_edge(topology.exhaust_valve_edge_id,
                          engine.gas_volumes[*chamber_volume_index].id,
                          engine.gas_volumes[*primary_volume_index].id,
                          path + ".topology.exhaust_valve_edge_id");
        }
        if (primary_volume_index.has_value() && route_lane_index.has_value()) {
            primary_collector_edge_index =
                bind_edge(topology.primary_to_collector_edge_id,
                          engine.gas_volumes[*primary_volume_index].id,
                          engine
                              .gas_volumes[admitted_routes[*route_lane_index]
                                               .collector_volume_index]
                              .id,
                          path + ".topology.primary_to_collector_edge_id");
        }
        if (chamber_volume_index.has_value() && atmosphere_volume_index.has_value()) {
            blowby_edge_index = bind_edge(
                topology.blowby_edge_id, engine.gas_volumes[*chamber_volume_index].id,
                engine.gas_volumes[*atmosphere_volume_index].id,
                path + ".topology.blowby_edge_id");
        }

        std::optional<AdmittedCylinderMechanismGeometry> mechanism_geometry;
        if (direct_plan != nullptr) {
            const auto *direct =
                std::get_if<contract::LegacyDirectJournalKinematics>(
                    &assembly.kinematics);
            const auto &planned = direct_plan->cylinders[cylinder_index];
            const auto &model = planned.crank;

            const bool numeric_values_valid =
                direct != nullptr && finite_positive(parameters.bore_m.value) &&
                finite_positive(direct->crank_radius_m.value) &&
                finite_positive(parameters.connecting_rod_length_m.value) &&
                direct->crank_radius_m.value <
                    parameters.connecting_rod_length_m.value &&
                finite_positive(parameters.deck_height_m.value) &&
                finite_positive(parameters.piston_compression_height_m.value) &&
                head != nullptr &&
                finite_positive(head->chamber_volume_m3.value) &&
                std::isfinite(parameters.piston_displacement_term_m3.value) &&
                std::isfinite(direct->journal_angle_rad.value) &&
                std::isfinite(parameters.ignition_wire_angle_rad.value) &&
                finite_nonnegative(parameters.header_primary_length_m.value);
            require(report, numeric_values_valid, ContractIssueCode::invalid_value,
                    path + ".parameters",
                    "cylinder gas geometry inputs are outside the admitted domain");

            const double piston_area_m2 = model.piston_area_m2;
            const double fixed_geometry_volume_m3 =
                planned.fixed_geometry_volume_m3;
            const bool direct_model_valid =
                numeric_values_valid &&
                std::isfinite(model.geometric_tdc_rad) &&
                finite_positive(model.piston_area_m2) &&
                finite_positive(model.clearance_volume_m3) &&
                finite_positive(planned.fixed_geometry_volume_m3);
            require(report, direct_model_valid, ContractIssueCode::invalid_value,
                    path + ".mechanism_plan",
                    "compiled direct mechanism plan contains invalid derived cylinder "
                    "geometry");

            const auto initial_sample = evaluate_centered_slider_crank(
                model, schedule.initial_theta_rad(), 0.0);
            require(report,
                    direct_model_valid && initial_sample.valid &&
                        finite_positive(initial_sample.chamber_volume_m3),
                    ContractIssueCode::invalid_value,
                    path + ".initial_chamber_volume",
                    "fresh analytic chamber state must have a finite positive volume");
            if (direct_model_valid && initial_sample.valid &&
                finite_positive(initial_sample.chamber_volume_m3)) {
                mechanism_geometry = {
                    parameters.bore_m.value,
                    piston_area_m2,
                    fixed_geometry_volume_m3,
                    initial_sample.chamber_volume_m3,
                };
            }
        } else {
            const auto &planned = radial_plan->cylinders[cylinder_index];
            const bool radial_model_valid =
                finite_positive(planned.bore_m) &&
                finite_positive(planned.piston_area_m2) &&
                finite_positive(planned.fixed_geometry_volume_m3) &&
                std::isfinite(planned.ignition_wire_angle_rad) &&
                finite_nonnegative(parameters.header_primary_length_m.value);
            require(report, radial_model_valid, ContractIssueCode::invalid_value,
                    path + ".mechanism_plan",
                    "compiled one-level master-rod plan contains invalid gas "
                    "geometry");

            const auto initial_sample = evaluate_one_level_master_rod_plan(
                *radial_plan, cylinder_index, 0.0, 0.0);
            require(report,
                    radial_model_valid && initial_sample.valid &&
                        finite_positive(initial_sample.chamber_volume_m3),
                    ContractIssueCode::invalid_value,
                    path + ".initial_chamber_volume",
                    "fresh analytic chamber state must have a finite positive volume");
            if (radial_model_valid && initial_sample.valid &&
                finite_positive(initial_sample.chamber_volume_m3)) {
                mechanism_geometry = {
                    planned.bore_m,
                    planned.piston_area_m2,
                    planned.fixed_geometry_volume_m3,
                    initial_sample.chamber_volume_m3,
                };
            }
        }

        double runner_volume_m3 = 0.0;
        double primary_volume_m3 = 0.0;
        if (head != nullptr &&
            finite_nonnegative(head->intake_runner_base_volume_m3.value) &&
            finite_positive(head->intake_runner_cross_section_area_m2.value) &&
            finite_positive(intake.runner_length_m.value)) {
            runner_volume_m3 = head->intake_runner_base_volume_m3.value +
                               head->intake_runner_cross_section_area_m2.value *
                                   intake.runner_length_m.value;
        }
        if (head != nullptr && route_lane_index.has_value() &&
            finite_nonnegative(head->exhaust_runner_base_volume_m3.value) &&
            finite_positive(head->exhaust_runner_cross_section_area_m2.value) &&
            finite_nonnegative(parameters.header_primary_length_m.value)) {
            primary_volume_m3 =
                head->exhaust_runner_base_volume_m3.value +
                head->exhaust_runner_cross_section_area_m2.value *
                    (admitted_routes[*route_lane_index].primary_tube_length_m +
                     parameters.header_primary_length_m.value);
        }
        require(report,
                finite_positive(runner_volume_m3) && finite_positive(primary_volume_m3),
                ContractIssueCode::invalid_value, path + ".gas_geometry",
                "derived intake-runner and exhaust-primary volumes must be finite "
                "and positive");

        const auto stream_index =
            find_combustion_seed_index(random_plan, topology.cylinder_id);
        require(report, stream_index.has_value(), ContractIssueCode::missing_value,
                "random_plan.component_seeds",
                "every cylinder requires one combustion random stream");

        const bool all_bindings_valid =
            cylinder_order_matches && runner_volume_index.has_value() &&
            chamber_volume_index.has_value() && primary_volume_index.has_value() &&
            plenum_runner_edge_index.has_value() &&
            intake_valve_edge_index.has_value() &&
            exhaust_valve_edge_index.has_value() &&
            primary_collector_edge_index.has_value() && blowby_edge_index.has_value() &&
            route_lane_index.has_value() && stream_index.has_value() &&
            mechanism_geometry.has_value() && head != nullptr &&
            finite_positive(runner_volume_m3) &&
            finite_positive(primary_volume_m3);
        if (all_bindings_valid) {
            const auto &stream = random_plan.component_seeds[*stream_index];
            admitted_cylinders[cylinder_index] = {
                *runner_volume_index,
                *chamber_volume_index,
                *primary_volume_index,
                *plenum_runner_edge_index,
                *intake_valve_edge_index,
                *exhaust_valve_edge_index,
                *primary_collector_edge_index,
                *blowby_edge_index,
                *route_lane_index,
                mechanism_geometry->bore_m,
                mechanism_geometry->piston_area_m2,
                mechanism_geometry->fixed_geometry_volume_m3,
                mechanism_geometry->initial_chamber_volume_m3,
                runner_volume_m3,
                primary_volume_m3,
                head->intake_runner_cross_section_area_m2.value,
                head->exhaust_runner_cross_section_area_m2.value,
                stream.initial_state,
                stream.stream,
            };
        }
    }

    require(report, all_bound(bound_ports), ContractIssueCode::inconsistent_shape,
            "engine.ports",
            "every engine port must be covered exactly once by the gas topology");
    require(report, all_bound(bound_volumes), ContractIssueCode::inconsistent_shape,
            "engine.gas_volumes",
            "every engine gas volume must be covered exactly once by the gas "
            "topology");
    require(report, all_bound(bound_edges), ContractIssueCode::inconsistent_shape,
            "engine.flow_edges",
            "every engine flow edge must be covered exactly once by the gas "
            "topology");
    require(report,
            std::all_of(route_used_by_cylinder.begin(), route_used_by_cylinder.end(),
                        [](bool used) { return used; }),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.exhaust_routes",
            "every exhaust route must be used by at least one cylinder");

    std::size_t combustion_seed_count = 0;
    std::unordered_set<std::uint32_t> stream_cylinder_ids;
    std::unordered_set<std::uint64_t> stream_selectors;
    for (std::size_t index = 0; index < random_plan.component_seeds.size(); ++index) {
        const auto &stream = random_plan.component_seeds[index];
        if (stream.kind != contract::RandomComponentKind::combustion) {
            continue;
        }
        ++combustion_seed_count;
        const std::string path =
            "random_plan.component_seeds[" + std::to_string(index) + "]";
        require(report,
                stream.cylinder_id.has_value() && stream.cylinder_id->valid() &&
                    !stream.route_id.has_value(),
                ContractIssueCode::invalid_value, path,
                "combustion random-stream ownership must name one cylinder only");
        if (!stream.cylinder_id.has_value() || !stream.cylinder_id->valid()) {
            continue;
        }
        const bool known_cylinder =
            find_id_index(engine.cylinders, *stream.cylinder_id).has_value();
        require(report, known_cylinder, ContractIssueCode::dangling_reference,
                path + ".cylinder_id",
                "combustion stream owner must reference an engine cylinder");
        if (known_cylinder) {
            require(report,
                    stream_cylinder_ids.insert(stream.cylinder_id->value).second,
                    ContractIssueCode::duplicate_identity, path + ".cylinder_id",
                    "combustion stream owners must uniquely cover engine cylinders");
        }
        require(report, stream.stream <= kMaximumLegacyPcg32Stream,
                ContractIssueCode::invalid_value, path + ".stream",
                "PCG32 stream selector must fit the source 63-bit domain");
        require(report, stream_selectors.insert(stream.stream).second,
                ContractIssueCode::duplicate_identity, path + ".stream",
                "combustion PCG32 stream selectors must be unique");
    }
    require(report,
            combustion_seed_count == engine.cylinders.size() &&
                stream_cylinder_ids.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape, "random_plan.component_seeds",
            "fresh gas state requires exactly one combustion stream per cylinder");

    if (!report.ok()) {
        return report;
    }

    auto valvetrain =
        std::get<LegacySelectableValvetrain>(std::move(valvetrain_result));

    LegacyLowOrderGasSession session;
    session.rate_ = schedule.rate();
    session.mechanism_plan_ = std::move(mechanism_plan);
    session.first_sample_index_ = schedule.first_step_index();
    session.expected_sample_count_ =
        schedule.execution_extent().finite_physics_frame_count();
    session.maximum_event_count_ = maximum_event_count;
    session.step_s_ = 1.0 / 10000.0;
    session.gas_step_s_ = session.step_s_ / static_cast<double>(kLegacyGasSubstepCount);
    session.ambient_pressure_pa_ = scenario.ambient.pressure_pa_abs.value;
    session.ambient_temperature_k_ = scenario.ambient.temperature_k.value;
    session.wall_temperature_k_ =
        scenario.initial_thermal_state.wall_temperature_k.value;
    session.crankcase_pressure_pa_ = scenario.crankcase.pressure_pa_abs.value;
    session.crankcase_temperature_k_ = scenario.crankcase.temperature_k.value;
    session.blowby_k_ = gas_path.piston_blowby.resolved_k.value;
    session.inert_mixture_ = {0.0, 1.0, 0.0};
    session.valvetrain_.emplace(std::move(valvetrain));
    session.model_id_ = engine.methods.gas_exchange.value.id;
    session.profile_id_ = engine.profile_id.value;
    session.scenario_id_ = scenario.scenario_id;
    session.engine_id_ = engine.id;

    session.step_.rate = session.rate_;
    session.step_.gas_volumes.resize(engine.gas_volumes.size());
    for (std::size_t index = 0; index < engine.gas_volumes.size(); ++index) {
        auto &output = session.step_.gas_volumes[index];
        output.gas_volume_id = engine.gas_volumes[index].id;
        output.kind = engine.gas_volumes[index].kind.value;
    }
    session.step_.flow_edges.resize(engine.flow_edges.size());
    for (std::size_t index = 0; index < engine.flow_edges.size(); ++index) {
        session.step_.flow_edges[index] = {
            engine.flow_edges[index].id,
            engine.flow_edges[index].endpoint_0_volume_id,
            engine.flow_edges[index].endpoint_1_volume_id,
            0.0,
        };
    }
    session.step_.cylinders.resize(engine.cylinders.size());
    session.step_.exhaust_routes.resize(public_exhaust_route_count);
    session.step_.events.reserve(maximum_event_count);
    session.expected_spark_cylinders_.reserve(engine.cylinders.size());

    const double initial_gas_temperature_k =
        scenario.initial_thermal_state.gas_temperature_k.value;
    const auto initialize_finite_volume = [&](std::size_t volume_index,
                                              double volume_m3,
                                              LegacyGasCellGeometry geometry) {
        auto &output = session.step_.gas_volumes[volume_index];
        output.physically_resolved = true;
        output.cell = legacy_initialize_gas_cell(session.ambient_pressure_pa_,
                                                 volume_m3, initial_gas_temperature_k,
                                                 session.inert_mixture_);
        output.geometry = geometry;
    };

    session.intake_.plenum_volume_index = *plenum_volume_index;
    session.intake_.main_throttle_edge_index = *main_throttle_edge_index;
    session.intake_.idle_bypass_edge_index = *idle_bypass_edge_index;
    session.intake_.plenum_cross_section_area_m2 =
        intake.plenum_cross_section_area_m2.value;
    session.intake_.main_throttle_k = intake.main_throttle.resolved_k.value;
    session.intake_.idle_bypass_k = intake.idle_bypass.resolved_k.value;
    session.intake_.plenum_to_runner_k = intake.plenum_to_runner.resolved_k.value;
    session.intake_.velocity_decay = intake.velocity_decay.value;
    session.intake_.atmosphere_work_cell = legacy_initialize_gas_cell(
        session.ambient_pressure_pa_, kLegacyBoundaryWorkVolumeM3,
        session.ambient_temperature_k_, session.inert_mixture_);

    const double plenum_width_m = std::sqrt(intake.plenum_cross_section_area_m2.value);
    initialize_finite_volume(
        *plenum_volume_index, intake.plenum_volume_m3.value,
        {
            plenum_width_m,
            intake.plenum_volume_m3.value / intake.plenum_cross_section_area_m2.value,
            1.0,
            0.0,
        });

    session.routes_.resize(admitted_routes.size());
    for (std::size_t route_index = 0; route_index < admitted_routes.size();
         ++route_index) {
        const auto &admitted = admitted_routes[route_index];
        const auto &profile_route = gas_path.exhaust_routes[route_index];
        auto &lane = session.routes_[route_index];
        lane.public_route_index = admitted.public_route_index;
        lane.collector_volume_index = admitted.collector_volume_index;
        lane.collector_outlet_edge_index = admitted.collector_outlet_edge_index;
        lane.primary_to_collector_k = admitted.primary_to_collector_k;
        lane.collector_outlet_k = admitted.collector_outlet_k;
        lane.velocity_decay = admitted.velocity_decay;
        lane.atmosphere_work_cell = legacy_initialize_gas_cell(
            session.ambient_pressure_pa_, kLegacyBoundaryWorkVolumeM3,
            session.ambient_temperature_k_, session.inert_mixture_);

        initialize_finite_volume(
            admitted.collector_volume_index, admitted.collector_volume_m3,
            {
                admitted.exhaust_system_length_m,
                std::sqrt(admitted.collector_cross_section_area_m2),
                1.0,
                0.0,
            });
        session.step_.exhaust_routes[admitted.public_route_index] = {
            profile_route.topology.route_id,
            profile_route.topology.collector_volume_id,
            profile_route.topology.collector_outlet_edge_id,
            admitted.collector_cross_section_area_m2,
        };
    }

    session.cylinders_.resize(admitted_cylinders.size());
    for (std::size_t cylinder_index = 0; cylinder_index < admitted_cylinders.size();
         ++cylinder_index) {
        const auto &admitted = admitted_cylinders[cylinder_index];
        const auto &assembly = mechanism.cylinders[cylinder_index];
        const auto &topology = assembly.topology;
        auto &lane = session.cylinders_[cylinder_index];
        lane.public_cylinder_index = cylinder_index;
        lane.intake_runner_volume_index = admitted.intake_runner_volume_index;
        lane.chamber_volume_index = admitted.chamber_volume_index;
        lane.exhaust_primary_volume_index = admitted.exhaust_primary_volume_index;
        lane.plenum_to_runner_edge_index = admitted.plenum_to_runner_edge_index;
        lane.intake_valve_edge_index = admitted.intake_valve_edge_index;
        lane.exhaust_valve_edge_index = admitted.exhaust_valve_edge_index;
        lane.primary_to_collector_edge_index = admitted.primary_to_collector_edge_index;
        lane.blowby_edge_index = admitted.blowby_edge_index;
        lane.route_lane_index = admitted.route_lane_index;
        lane.bore_m = admitted.bore_m;
        lane.piston_area_m2 = admitted.piston_area_m2;
        lane.intake_runner_cross_section_area_m2 =
            admitted.intake_runner_cross_section_area_m2;
        lane.exhaust_primary_cross_section_area_m2 =
            admitted.exhaust_runner_cross_section_area_m2;
        const bool seeded =
            lane.random.seed(admitted.pcg32_initial_state, admitted.pcg32_stream);
        if (!seeded) {
            report.add(ContractIssueCode::invalid_value, "random_plan.component_seeds",
                       "admitted PCG32 stream unexpectedly failed fresh seeding");
            return report;
        }

        const double runner_area_m2 =
            admitted.intake_runner_cross_section_area_m2;
        initialize_finite_volume(admitted.intake_runner_volume_index,
                                 admitted.intake_runner_volume_m3,
                                 {
                                     admitted.intake_runner_volume_m3 / runner_area_m2,
                                     std::sqrt(runner_area_m2),
                                     1.0,
                                     0.0,
                                 });
        initialize_finite_volume(
            admitted.chamber_volume_index, admitted.initial_chamber_volume_m3,
            {
                std::sqrt(admitted.piston_area_m2),
                admitted.fixed_geometry_volume_m3 / admitted.piston_area_m2,
                1.0,
                0.0,
            });
        const double primary_area_m2 =
            admitted.exhaust_runner_cross_section_area_m2;
        initialize_finite_volume(
            admitted.exhaust_primary_volume_index, admitted.exhaust_primary_volume_m3,
            {
                admitted.exhaust_primary_volume_m3 / primary_area_m2,
                std::sqrt(primary_area_m2),
                1.0,
                0.0,
            });

        auto &output = session.step_.cylinders[cylinder_index];
        output.cylinder_id = topology.cylinder_id;
        output.intake_port_id = topology.intake_port_id;
        output.exhaust_port_id = topology.exhaust_port_id;
        output.intake_runner_volume_id = topology.intake_runner_volume_id;
        output.chamber_volume_id = topology.chamber_volume_id;
        output.exhaust_primary_volume_id = topology.exhaust_primary_volume_id;
        output.exhaust_route_id = topology.exhaust_route_id;
        output.valves.cylinder_id = topology.cylinder_id;
        output.valves.intake_port_id = topology.intake_port_id;
        output.valves.exhaust_port_id = topology.exhaust_port_id;
        output.flame = {};
        output.flame.global_mixture = session.inert_mixture_;
    }

    session.fuel_.molecular_mass_kg_per_mol = fuel.molecular_mass_kg_per_mol.value;
    session.fuel_.energy_density_j_per_kg = fuel.energy_density_j_per_kg.value;
    session.fuel_.molecular_afr = fuel.molecular_afr.value;
    session.fuel_.maximum_burning_efficiency_01 =
        fuel.maximum_burning_efficiency_01.value;
    session.fuel_.burning_efficiency_randomness_01 =
        fuel.burning_efficiency_randomness_01.value;
    session.fuel_.low_efficiency_attenuation_01 =
        fuel.low_efficiency_attenuation_01.value;
    session.fuel_.maximum_turbulence_effect = fuel.maximum_turbulence_effect.value;
    session.fuel_.maximum_dilution_effect = fuel.maximum_dilution_effect.value;
    session.fuel_.lbv_multiplier = fuel.lbv_multiplier.value;
    session.fuel_.turbulence_to_flame_speed_ratio_triangle_radius =
        fuel.turbulence_to_flame_speed_ratio_triangle_radius.value;
    session.fuel_.turbulence_to_flame_speed_ratio.reserve(
        fuel.turbulence_to_flame_speed_ratio.size());
    for (const auto &point : fuel.turbulence_to_flame_speed_ratio) {
        session.fuel_.turbulence_to_flame_speed_ratio.push_back(
            {point.turbulence.value, point.flame_speed_ratio.value});
    }

    return session;
}

} // namespace engine_sim_offline::simulation
