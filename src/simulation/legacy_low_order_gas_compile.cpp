#include "simulation/legacy_low_order_gas.hpp"

#include "simulation/kinematic_scenario_schedule.hpp"

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

constexpr contract::Sha256Digest kLegacyLowOrderV1ConfigurationSha256{{
    0x43, 0x54, 0x41, 0x89, 0x0e, 0x0a, 0x5f, 0x8d, 0x01, 0xe8, 0x19,
    0x95, 0xf6, 0x4f, 0x33, 0xd4, 0xc5, 0x54, 0x14, 0x4f, 0x5b, 0x14,
    0x36, 0x89, 0x5e, 0x68, 0x16, 0xf6, 0xdb, 0x85, 0xe3, 0x4c,
}};

constexpr contract::Sha256Digest kFixedRateRpmV1ConfigurationSha256{{
    0xc6, 0x4a, 0xb8, 0xb9, 0xc2, 0xf8, 0xc7, 0x8a, 0x15, 0x12, 0x22,
    0xd8, 0x89, 0x86, 0x52, 0x69, 0xbe, 0x19, 0xcc, 0x52, 0x1e, 0x68,
    0x52, 0xc4, 0x6d, 0xdf, 0x34, 0x50, 0x86, 0x9e, 0x75, 0xe4,
}};

constexpr double kLegacyCalibrationPressurePa = 101325.0;
constexpr double kLegacyCalibrationTemperatureK = 298.15;
constexpr double kLegacyBoundaryWorkVolumeM3 = 1000.0;
constexpr double kLegacyIntakePlateMultiplier = 0.994;
constexpr double kLegacyDeprecatedThrottleGamma = 2.0;

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
    std::uint64_t pcg32_initial_state = 0;
    std::uint64_t pcg32_stream = 0;
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

[[nodiscard]] bool exact_legacy_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method) noexcept {
    return method.value.id == "legacy_low_order_v1" && method.value.version == 1U &&
           method.value.configuration_sha256 == kLegacyLowOrderV1ConfigurationSha256;
}

[[nodiscard]] bool exact_fixed_rate_rpm_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method) noexcept {
    return method.value.id == "fixed-rate-post-step-rpm-binary64-v1" &&
           method.value.version == 1U &&
           method.value.configuration_sha256 == kFixedRateRpmV1ConfigurationSha256;
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

[[nodiscard]] bool known_restriction_calibration(
    contract::LegacyRestrictionCalibration calibration) noexcept {
    return calibration == contract::LegacyRestrictionCalibration::carb_at_1p5_inhg ||
           calibration == contract::LegacyRestrictionCalibration::cfm_at_28_inh2o;
}

[[nodiscard]] double restriction_pressure_drop_pa(
    contract::LegacyRestrictionCalibration calibration) noexcept {
    if (calibration == contract::LegacyRestrictionCalibration::cfm_at_28_inh2o) {
        return 28.0 * (3386.3886666666713 * 0.0734824);
    }
    return 1.5 * 3386.3886666666713;
}

[[nodiscard]] double
recompute_restriction_k(contract::LegacyRestrictionCalibration calibration,
                        double source_rating) noexcept {
    const double one_source_scfm_mol_s = 0.002641 * 453.59237 / 60.0;
    const double target_source_flow_mol_s = source_rating * one_source_scfm_mol_s;
    return legacy_restriction_coefficient(
        target_source_flow_mol_s, kLegacyCalibrationPressurePa,
        restriction_pressure_drop_pa(calibration), kLegacyCalibrationTemperatureK);
}

[[nodiscard]] bool admit_restriction(const contract::LegacyRestriction &restriction,
                                     ValidationReport &report,
                                     const std::string &path) {
    const auto calibration = restriction.calibration.value;
    const double source_rating = restriction.source_rating.value;
    const double resolved_k = restriction.resolved_k.value;
    const bool direct_values_valid = known_restriction_calibration(calibration) &&
                                     finite_nonnegative(source_rating) &&
                                     finite_nonnegative(resolved_k);
    require(report, known_restriction_calibration(calibration),
            ContractIssueCode::unsupported_value, path + ".calibration.value",
            "restriction calibration is not supported by legacy_low_order_v1");
    require(report, finite_nonnegative(source_rating) && finite_nonnegative(resolved_k),
            ContractIssueCode::invalid_value, path,
            "restriction rating and resolved K must be finite and nonnegative");
    if (!direct_values_valid) {
        return false;
    }
    const bool exact =
        same_binary64(resolved_k, recompute_restriction_k(calibration, source_rating));
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
            const double expected_k = recompute_restriction_k(
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
find_random_stream_index(const contract::LegacyLowOrderV1Profile &profile,
                         contract::CylinderId cylinder_id) noexcept {
    const auto found =
        std::find_if(profile.combustion_random_streams.begin(),
                     profile.combustion_random_streams.end(), [&](const auto &stream) {
                         return stream.cylinder_id == cylinder_id;
                     });
    if (found == profile.combustion_random_streams.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - profile.combustion_random_streams.begin());
}

[[nodiscard]] bool all_bound(const std::vector<bool> &bound) noexcept {
    return std::all_of(bound.begin(), bound.end(), [](bool value) { return value; });
}

} // namespace

LegacyGasCompileResult compile_legacy_low_order_gas_session(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    std::span<const CenteredSliderCrankCylinder> cylinder_models) {
    ValidationReport report;

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
        7U>
        consumed_methods{{
            {&engine.methods.mechanism, "engine.methods.mechanism"},
            {&engine.methods.valvetrain, "engine.methods.valvetrain"},
            {&engine.methods.gas_exchange, "engine.methods.gas_exchange"},
            {&engine.methods.ignition, "engine.methods.ignition"},
            {&engine.methods.combustion, "engine.methods.combustion"},
            {&engine.methods.heat_transfer, "engine.methods.heat_transfer"},
            {&engine.methods.losses, "engine.methods.losses"},
        }};
    for (const auto &[method, path] : consumed_methods) {
        require(report, exact_legacy_method(*method),
                ContractIssueCode::unsupported_value, path,
                "gas session requires the exact legacy_low_order_v1 version 1 "
                "configuration");
    }

    auto schedule_result = compile_kinematic_scenario_schedule(scenario);
    if (const auto *nested = std::get_if<ValidationReport>(&schedule_result)) {
        append_prefixed(report, *nested, "schedule");
    }
    require(report, scenario.rates.physics == contract::RationalRateHz{10000U, 1U},
            ContractIssueCode::unsupported_value, "scenario.rates.physics",
            "legacy_low_order_v1 gas requires exactly 10000 Hz physics");
    require(report, scenario.rates.capture == scenario.rates.physics,
            ContractIssueCode::unsupported_value, "scenario.rates.capture",
            "gas session publishes exactly one post-step sample per physics step");

    const auto *sweep = std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    require(report, sweep != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode",
            "legacy_low_order_v1 gas requires a prescribed kinematic sweep");
    if (sweep != nullptr) {
        require(report,
                exact_fixed_rate_rpm_method(sweep->trajectory.kinematic_resolution),
                ContractIssueCode::unsupported_value,
                "scenario.mode.trajectory.kinematic_resolution",
                "gas session requires fixed-rate post-step RPM resolution version "
                "1");
    }

    const auto *profile =
        std::get_if<contract::LegacyLowOrderV1Profile>(&engine.physics_profile);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile", "gas session requires LegacyLowOrderV1Profile");
    if (profile == nullptr || sweep == nullptr) {
        return report;
    }

    auto valvetrain_result = compile_legacy_fixed_valvetrain(engine);
    if (const auto *nested = std::get_if<ValidationReport>(&valvetrain_result)) {
        append_prefixed(report, *nested, "valvetrain");
    }

    admit_public_identities(engine.cylinders, report, "engine.cylinders");
    admit_public_identities(engine.ports, report, "engine.ports");
    admit_public_identities(engine.gas_volumes, report, "engine.gas_volumes");
    admit_public_identities(engine.flow_edges, report, "engine.flow_edges");
    admit_public_identities(engine.routes, report, "engine.routes");

    const auto &mechanism = profile->mechanism;
    const auto &gas_path = profile->gas_path;
    const auto &head = gas_path.head;
    require(report,
            !engine.cylinders.empty() &&
                mechanism.cylinders.size() == engine.cylinders.size() &&
                cylinder_models.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "engine, mechanism, and admitted mechanics-model cylinder orders must "
            "be equal and nonempty");
    require(report, std::isfinite(mechanism.crank.crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank.crank_tdc_reference_rad.value",
            "crank TDC reference must be finite");
    require(report,
            std::isfinite(sweep->trajectory.initial_theta_rad.value) &&
                same_binary64(sweep->trajectory.initial_theta_rad.value,
                              mechanism.crank.crank_tdc_reference_rad.value),
            ContractIssueCode::unsupported_value,
            "scenario.mode.trajectory.initial_theta_rad.value",
            "fresh gas state requires the prescribed initial angle to equal the "
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
            same_binary64(intake.idle_throttle_plate_position_01.value,
                          kLegacyIntakePlateMultiplier),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.gas_path.intake."
            "idle_throttle_plate_position_01.value",
            "the composed mechanics/gas path currently admits the exact v1 0.994 "
            "plate multiplier");
    require(report,
            same_binary64(intake.throttle_gamma.value, kLegacyDeprecatedThrottleGamma),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.gas_path.intake.throttle_gamma.value",
            "legacy_low_order_v1 preserves the source-deprecated throttle gamma "
            "datum at 2.0");
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
            finite_nonnegative(head.intake_runner_base_volume_m3.value) &&
                finite_positive(head.intake_runner_cross_section_area_m2.value) &&
                finite_nonnegative(head.exhaust_runner_base_volume_m3.value) &&
                finite_positive(head.exhaust_runner_cross_section_area_m2.value) &&
                finite_positive(head.flow_table_triangle_radius_m.value),
            ContractIssueCode::invalid_value, "engine.physics_profile.gas_path.head",
            "cylinder-head gas geometry is outside the admitted domain");
    admit_valve_flow_k(head.intake_flow, report,
                       "engine.physics_profile.gas_path.head.intake_flow");
    admit_valve_flow_k(head.exhaust_flow, report,
                       "engine.physics_profile.gas_path.head.exhaust_flow");

    const auto &fuel = profile->fuel;
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
    require(report, !fuel.compression_ignition_enabled.value,
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.fuel.compression_ignition_enabled.value",
            "legacy_low_order_v1 gas session does not admit compression ignition");
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

    const auto required_included_terms =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto required_omitted_terms =
        contract::known_torque_term_mask() & ~required_included_terms;
    require(report,
            profile->losses.included_terms.value == required_included_terms &&
                profile->losses.omitted_terms.value == required_omitted_terms,
            ContractIssueCode::unsupported_value, "engine.physics_profile.losses",
            "legacy_low_order_v1 gas reports only indicated gas and fixed crank "
            "friction");
    require(
        report,
        engine.torque_capability.value.instantaneous_net_shaft.availability ==
                contract::Availability::available &&
            engine.torque_capability.value.instantaneous_net_shaft.completeness ==
                contract::Completeness::incomplete &&
            engine.torque_capability.value.instantaneous_net_shaft.included_terms ==
                required_included_terms &&
            engine.torque_capability.value.instantaneous_net_shaft.omitted_terms ==
                required_omitted_terms &&
            engine.torque_capability.value.cycle_mean_net_shaft.availability ==
                contract::Availability::unavailable &&
            engine.torque_capability.value.cycle_mean_net_shaft.completeness ==
                contract::Completeness::incomplete &&
            engine.torque_capability.value.cycle_mean_net_shaft.included_terms == 0 &&
            engine.torque_capability.value.cycle_mean_net_shaft.omitted_terms == 0 &&
            !engine.torque_capability.value.equivalent_inertia_available,
        ContractIssueCode::inconsistent_semantics, "engine.torque_capability.value",
        "engine torque capability must exactly describe the available incomplete "
        "M3 instantaneous form and unavailable cycle-mean form");
    require(report,
            finite_nonnegative(mechanism.crank.fixed_crank_friction_magnitude_nm.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank."
            "fixed_crank_friction_magnitude_nm.value",
            "fixed crank-friction magnitude must be finite and nonnegative");

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
        const std::uint64_t required_block_event_capacity =
            static_cast<std::uint64_t>(
                scenario.quality.value.capture_block_capacity_frames) *
            static_cast<std::uint64_t>(maximum_event_count);
        require(report,
                scenario.quality.value.capture_block_capacity_frames > 0U &&
                    static_cast<std::uint64_t>(
                        scenario.quality.value.event_journal_capacity_records) >=
                        required_block_event_capacity,
                ContractIssueCode::invalid_value,
                "scenario.quality.value.event_journal_capacity_records",
                "event journal must hold the exact worst-case composed event bound "
                "for a full capture block");
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
            const double expected_length =
                parameters.collector_volume_m3.value /
                parameters.collector_cross_section_area_m2.value;
            require(report,
                    same_binary64(parameters.exhaust_system_length_m.value,
                                  expected_length),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".parameters.exhaust_system_length_m.value",
                    "collector length must exactly equal volume divided by area");
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
    for (std::size_t cylinder_index = 0; cylinder_index < engine.cylinders.size();
         ++cylinder_index) {
        if (cylinder_index >= mechanism.cylinders.size() ||
            cylinder_index >= cylinder_models.size()) {
            break;
        }
        const auto &engine_cylinder = engine.cylinders[cylinder_index];
        const auto &assembly = mechanism.cylinders[cylinder_index];
        const auto &topology = assembly.topology;
        const auto &parameters = assembly.parameters;
        const auto &model = cylinder_models[cylinder_index];
        const std::string path = "engine.physics_profile.mechanism.cylinders[" +
                                 std::to_string(cylinder_index) + "]";

        const bool cylinder_order_matches =
            topology.cylinder_id.valid() &&
            topology.cylinder_id == engine_cylinder.id &&
            model.cylinder_id == engine_cylinder.id &&
            mechanism_cylinder_ids.insert(topology.cylinder_id.value).second;
        require(report, cylinder_order_matches,
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "engine, mechanism, and admitted mechanics cylinder identity/order "
                "must match exactly");

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

        const bool numeric_values_valid =
            finite_positive(parameters.bore_m.value) &&
            finite_positive(parameters.crank_radius_m.value) &&
            finite_positive(parameters.connecting_rod_length_m.value) &&
            parameters.crank_radius_m.value <
                parameters.connecting_rod_length_m.value &&
            finite_positive(parameters.deck_height_m.value) &&
            finite_positive(parameters.piston_compression_height_m.value) &&
            finite_positive(parameters.head_chamber_volume_m3.value) &&
            std::isfinite(parameters.piston_displacement_term_m3.value) &&
            std::isfinite(parameters.journal_angle_rad.value) &&
            std::isfinite(parameters.ignition_wire_angle_rad.value) &&
            finite_nonnegative(parameters.header_primary_length_m.value);
        require(report, numeric_values_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "cylinder gas geometry inputs are outside the admitted domain");

        double piston_area_m2 = 0.0;
        double fixed_geometry_volume_m3 = 0.0;
        double clearance_volume_m3 = 0.0;
        double geometric_tdc_rad = 0.0;
        bool model_matches = false;
        if (numeric_values_valid &&
            std::isfinite(mechanism.crank.crank_tdc_reference_rad.value)) {
            piston_area_m2 =
                kLegacyPi * parameters.bore_m.value * parameters.bore_m.value / 4.0;
            const double tdc_mechanism_height_m =
                parameters.crank_radius_m.value * std::cos(0.0) +
                std::sqrt(parameters.connecting_rod_length_m.value *
                          parameters.connecting_rod_length_m.value);
            clearance_volume_m3 =
                parameters.head_chamber_volume_m3.value -
                parameters.piston_displacement_term_m3.value +
                piston_area_m2 *
                    (parameters.deck_height_m.value - tdc_mechanism_height_m -
                     parameters.piston_compression_height_m.value);
            fixed_geometry_volume_m3 =
                parameters.head_chamber_volume_m3.value +
                piston_area_m2 * (parameters.deck_height_m.value -
                                  parameters.piston_compression_height_m.value);
            geometric_tdc_rad =
                legacy_wrap_2pi(mechanism.crank.crank_tdc_reference_rad.value +
                                parameters.journal_angle_rad.value - kLegacyPi / 2.0);
            const bool derived_values_valid =
                finite_positive(piston_area_m2) &&
                finite_positive(clearance_volume_m3) &&
                finite_positive(fixed_geometry_volume_m3) &&
                std::isfinite(geometric_tdc_rad);
            require(report, derived_values_valid, ContractIssueCode::invalid_value,
                    path + ".parameters",
                    "derived piston area, clearance, and fixed planar geometry "
                    "must be finite and positive");
            model_matches =
                derived_values_valid && model.cylinder_id == topology.cylinder_id &&
                same_binary64(model.geometric_tdc_rad, geometric_tdc_rad) &&
                same_binary64(model.piston_area_m2, piston_area_m2) &&
                same_binary64(model.crank_radius_m, parameters.crank_radius_m.value) &&
                same_binary64(model.connecting_rod_length_m,
                              parameters.connecting_rod_length_m.value) &&
                same_binary64(model.clearance_volume_m3, clearance_volume_m3) &&
                same_binary64(model.ignition_wire_angle_rad,
                              parameters.ignition_wire_angle_rad.value);
            require(report, model_matches, ContractIssueCode::inconsistent_semantics,
                    path + ".mechanics_model",
                    "supplied admitted mechanics model must exactly match the gas "
                    "profile's cylinder geometry and phase");
        }

        const auto initial_sample = evaluate_centered_slider_crank(
            model, sweep->trajectory.initial_theta_rad.value, 0.0);
        require(report,
                model_matches && initial_sample.valid &&
                    finite_positive(initial_sample.chamber_volume_m3),
                ContractIssueCode::invalid_value, path + ".initial_chamber_volume",
                "fresh analytic chamber state must have a finite positive volume");

        double runner_volume_m3 = 0.0;
        double primary_volume_m3 = 0.0;
        if (finite_nonnegative(head.intake_runner_base_volume_m3.value) &&
            finite_positive(head.intake_runner_cross_section_area_m2.value) &&
            finite_positive(intake.runner_length_m.value)) {
            runner_volume_m3 = head.intake_runner_base_volume_m3.value +
                               head.intake_runner_cross_section_area_m2.value *
                                   intake.runner_length_m.value;
        }
        if (route_lane_index.has_value() &&
            finite_nonnegative(head.exhaust_runner_base_volume_m3.value) &&
            finite_positive(head.exhaust_runner_cross_section_area_m2.value) &&
            finite_nonnegative(parameters.header_primary_length_m.value)) {
            primary_volume_m3 =
                head.exhaust_runner_base_volume_m3.value +
                head.exhaust_runner_cross_section_area_m2.value *
                    (admitted_routes[*route_lane_index].primary_tube_length_m +
                     parameters.header_primary_length_m.value);
        }
        require(report,
                finite_positive(runner_volume_m3) && finite_positive(primary_volume_m3),
                ContractIssueCode::invalid_value, path + ".gas_geometry",
                "derived intake-runner and exhaust-primary volumes must be finite "
                "and positive");

        const auto stream_index =
            find_random_stream_index(*profile, topology.cylinder_id);
        require(report, stream_index.has_value(), ContractIssueCode::missing_value,
                "engine.physics_profile.combustion_random_streams",
                "every cylinder requires one combustion random stream");

        const bool all_bindings_valid =
            cylinder_order_matches && runner_volume_index.has_value() &&
            chamber_volume_index.has_value() && primary_volume_index.has_value() &&
            plenum_runner_edge_index.has_value() &&
            intake_valve_edge_index.has_value() &&
            exhaust_valve_edge_index.has_value() &&
            primary_collector_edge_index.has_value() && blowby_edge_index.has_value() &&
            route_lane_index.has_value() && stream_index.has_value() && model_matches &&
            initial_sample.valid && finite_positive(runner_volume_m3) &&
            finite_positive(primary_volume_m3);
        if (all_bindings_valid) {
            const auto &stream = profile->combustion_random_streams[*stream_index];
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
                parameters.bore_m.value,
                piston_area_m2,
                fixed_geometry_volume_m3,
                initial_sample.chamber_volume_m3,
                runner_volume_m3,
                primary_volume_m3,
                stream.pcg32_initial_state.value,
                stream.pcg32_stream.value,
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

    require(report,
            profile->combustion_random_streams.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.combustion_random_streams",
            "fresh gas state requires exactly one combustion stream per cylinder");
    std::unordered_set<std::uint32_t> stream_cylinder_ids;
    std::unordered_set<std::uint64_t> stream_selectors;
    for (std::size_t index = 0; index < profile->combustion_random_streams.size();
         ++index) {
        const auto &stream = profile->combustion_random_streams[index];
        const std::string path = "engine.physics_profile.combustion_random_streams[" +
                                 std::to_string(index) + "]";
        require(report,
                stream.cylinder_id.valid() &&
                    find_id_index(engine.cylinders, stream.cylinder_id).has_value() &&
                    stream_cylinder_ids.insert(stream.cylinder_id.value).second,
                ContractIssueCode::duplicate_identity, path + ".cylinder_id",
                "combustion stream owners must uniquely cover engine cylinders");
        require(report, stream.pcg32_stream.value <= kMaximumLegacyPcg32Stream,
                ContractIssueCode::invalid_value, path + ".pcg32_stream.value",
                "PCG32 stream selector must fit the source 63-bit domain");
        require(report, stream_selectors.insert(stream.pcg32_stream.value).second,
                ContractIssueCode::duplicate_identity, path + ".pcg32_stream.value",
                "combustion PCG32 stream selectors must be unique");
    }

    if (!report.ok()) {
        return report;
    }

    const auto &schedule = std::get<KinematicScenarioSchedule>(schedule_result);
    auto valvetrain = std::get<LegacyFixedValvetrain>(std::move(valvetrain_result));

    LegacyLowOrderGasSession session;
    session.rate_ = schedule.rate();
    session.first_sample_index_ = schedule.first_step_index();
    session.expected_sample_count_ = schedule.sample_count();
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
    session.crank_friction_magnitude_nm_ =
        mechanism.crank.fixed_crank_friction_magnitude_nm.value;
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
    session.intake_.runner_cross_section_area_m2 =
        head.intake_runner_cross_section_area_m2.value;
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
        lane.exhaust_primary_cross_section_area_m2 =
            head.exhaust_runner_cross_section_area_m2.value;
        const bool seeded =
            lane.random.seed(admitted.pcg32_initial_state, admitted.pcg32_stream);
        if (!seeded) {
            report.add(ContractIssueCode::invalid_value,
                       "engine.physics_profile.combustion_random_streams",
                       "admitted PCG32 stream unexpectedly failed fresh seeding");
            return report;
        }

        const double runner_area_m2 = head.intake_runner_cross_section_area_m2.value;
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
        const double primary_area_m2 = head.exhaust_runner_cross_section_area_m2.value;
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
