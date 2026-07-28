#include "engine_sim_offline/contract/parity_model.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

constexpr double kLegacyPi = 3.14159265359;
constexpr double kGasConstant = 8.31446261815324;
constexpr double kOneSourceScfm = 0.002641 * 453.59237 / 60.0;
constexpr std::string_view kLegacyProfileRoot = "engine.physics.legacy-low-order-v1";

template <class T>
void validate_authored(ValidationReport &report, const AuthoredValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_authored_value(report, value, provenance, path);
}

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

std::string profile_path(std::string_view suffix) {
    return std::string(kLegacyProfileRoot) + "." + std::string(suffix);
}

template <class T>
void validate_derived_resolution(
    ValidationReport &report, const ResolvedValue<T> &value,
    const ProvenanceLedger &provenance, const std::string &path,
    std::initializer_list<std::string> direct_dependencies) {
    const auto *resolution = detail::find_resolution(provenance, value.resolution_id);
    if (resolution == nullptr) {
        return;
    }

    detail::require(report, resolution->mode == ResolutionMode::derived,
                    ContractIssueCode::inconsistent_semantics, path + ".resolution_id",
                    "this value must use a derived resolution");
    detail::require(report, resolution->method.has_value(),
                    ContractIssueCode::missing_value, path + ".resolution_id",
                    "a derived value must name its derivation method");

    std::unordered_set<std::string> actual_dependencies;
    for (const auto &dependency : resolution->dependency_parameter_paths) {
        actual_dependencies.insert(dependency);
    }
    const std::unordered_set<std::string> expected_dependencies{
        direct_dependencies.begin(), direct_dependencies.end()};
    detail::require(
        report,
        actual_dependencies.size() == resolution->dependency_parameter_paths.size() &&
            actual_dependencies == expected_dependencies,
        ContractIssueCode::inconsistent_semantics, path + ".resolution_id",
        "derived resolution must name exactly its direct parameter dependencies");
}

template <class Range, class Id, class Projection>
const typename Range::value_type *find_by_id(const Range &range, Id id,
                                             Projection projection) {
    const auto iterator = std::ranges::find(range, id, projection);
    return iterator == range.end() ? nullptr : &*iterator;
}

template <class Parameters, class Function>
void visit_cylinder_parameters(const Parameters &parameters, const std::string &base,
                               Function function) {
    function(parameters.bore_m, base + ".bore_m");
    function(parameters.stroke_m, base + ".stroke_m");
    function(parameters.crank_radius_m, base + ".crank_radius_m");
    function(parameters.connecting_rod_length_m, base + ".connecting_rod_length_m");
    function(parameters.deck_height_m, base + ".deck_height_m");
    function(parameters.piston_compression_height_m,
             base + ".piston_compression_height_m");
    function(parameters.head_chamber_volume_m3, base + ".head_chamber_volume_m3");
    function(parameters.piston_displacement_term_m3,
             base + ".piston_displacement_term_m3");
    function(parameters.piston_mass_kg, base + ".piston_mass_kg");
    function(parameters.connecting_rod_mass_kg, base + ".connecting_rod_mass_kg");
    function(parameters.connecting_rod_inertia_kg_m2,
             base + ".connecting_rod_inertia_kg_m2");
    function(parameters.journal_angle_rad, base + ".journal_angle_rad");
    function(parameters.ignition_wire_angle_rad, base + ".ignition_wire_angle_rad");
    function(parameters.header_primary_length_m, base + ".header_primary_length_m");
}

template <class Crank, class Function>
void visit_crank(const Crank &crank, const std::string &base, Function function) {
    function(crank.crank_tdc_reference_rad, base + ".crank_tdc_reference_rad");
    function(crank.crankshaft_mass_kg, base + ".crankshaft_mass_kg");
    function(crank.flywheel_mass_kg, base + ".flywheel_mass_kg");
    function(crank.authored_crank_inertia_kg_m2,
             base + ".authored_crank_inertia_kg_m2");
    function(crank.fixed_crank_friction_magnitude_nm,
             base + ".fixed_crank_friction_magnitude_nm");
}

template <class Restriction, class Function>
void visit_restriction(const Restriction &restriction, const std::string &base,
                       Function function) {
    function(restriction.calibration, base + ".calibration");
    function(restriction.source_rating, base + ".source_rating");
    function(restriction.resolved_k, base + ".resolved_k");
}

template <class Intake, class Function>
void visit_intake(const Intake &intake, const std::string &base, Function function) {
    function(intake.plenum_volume_m3, base + ".plenum_volume_m3");
    function(intake.plenum_cross_section_area_m2,
             base + ".plenum_cross_section_area_m2");
    function(intake.runner_length_m, base + ".runner_length_m");
    function(intake.velocity_decay, base + ".velocity_decay");
    function(intake.throttle_gamma, base + ".throttle_gamma");
    function(intake.idle_throttle_plate_position_01,
             base + ".idle_throttle_plate_position_01");
    visit_restriction(intake.main_throttle, base + ".main_throttle", function);
    visit_restriction(intake.idle_bypass, base + ".idle_bypass", function);
    visit_restriction(intake.plenum_to_runner, base + ".plenum_to_runner", function);
}

template <class Point, class Function>
void visit_valve_point(const Point &point, const std::string &base, Function function) {
    function(point.sample_id, base + ".sample_id");
    function(point.lift_m, base + ".lift_m");
    function(point.source_cfm_at_28_inh2o, base + ".source_cfm_at_28_inh2o");
    function(point.resolved_k, base + ".resolved_k");
}

template <class Head, class Function>
void visit_head(const Head &head, const std::string &base, Function function) {
    function(head.intake_runner_base_volume_m3, base + ".intake_runner_base_volume_m3");
    function(head.intake_runner_cross_section_area_m2,
             base + ".intake_runner_cross_section_area_m2");
    function(head.exhaust_runner_base_volume_m3,
             base + ".exhaust_runner_base_volume_m3");
    function(head.exhaust_runner_cross_section_area_m2,
             base + ".exhaust_runner_cross_section_area_m2");
    function(head.flow_table_triangle_radius_m, base + ".flow_table_triangle_radius_m");
    for (const auto &point : head.intake_flow) {
        const auto point_base = base + ".intake_flow." + point.sample_id.value;
        visit_valve_point(point, point_base, function);
    }
    for (const auto &point : head.exhaust_flow) {
        const auto point_base = base + ".exhaust_flow." + point.sample_id.value;
        visit_valve_point(point, point_base, function);
    }
}

template <class Parameters, class Function>
void visit_exhaust_parameters(const Parameters &parameters, const std::string &base,
                              Function function) {
    function(parameters.collector_volume_m3, base + ".collector_volume_m3");
    function(parameters.collector_cross_section_area_m2,
             base + ".collector_cross_section_area_m2");
    function(parameters.exhaust_system_length_m, base + ".exhaust_system_length_m");
    function(parameters.primary_tube_length_m, base + ".primary_tube_length_m");
    function(parameters.velocity_decay, base + ".velocity_decay");
    function(parameters.audio_volume_linear, base + ".audio_volume_linear");
    visit_restriction(parameters.primary_to_collector, base + ".primary_to_collector",
                      function);
    visit_restriction(parameters.collector_outlet, base + ".collector_outlet",
                      function);
}

template <class Shape, class Function>
void visit_cam_shape(const Shape &shape, const std::string &base, Function function) {
    function(shape.maximum_lift_m, base + ".maximum_lift_m");
    function(shape.duration_at_reference_lift_rad,
             base + ".duration_at_reference_lift_rad");
    function(shape.exponent, base + ".exponent");
    function(shape.construction_steps, base + ".construction_steps");
    function(shape.advance_rad, base + ".advance_rad");
    function(shape.base_radius_m, base + ".base_radius_m");
}

template <class TimingPoint, class Function>
void visit_timing_point(const TimingPoint &point, const std::string &base,
                        Function function) {
    function(point.sample_id, base + ".sample_id");
    function(point.angular_speed_rad_s, base + ".angular_speed_rad_s");
    function(point.timing_advance_rad, base + ".timing_advance_rad");
}

template <class Fuel, class Function>
void visit_fuel(const Fuel &fuel, const std::string &base, Function function) {
    function(fuel.fuel_id, base + ".fuel_id");
    function(fuel.molecular_mass_kg_per_mol, base + ".molecular_mass_kg_per_mol");
    function(fuel.energy_density_j_per_kg, base + ".energy_density_j_per_kg");
    function(fuel.molecular_afr, base + ".molecular_afr");
    function(fuel.maximum_burning_efficiency_01,
             base + ".maximum_burning_efficiency_01");
    function(fuel.burning_efficiency_randomness_01,
             base + ".burning_efficiency_randomness_01");
    function(fuel.low_efficiency_attenuation_01,
             base + ".low_efficiency_attenuation_01");
    function(fuel.maximum_turbulence_effect, base + ".maximum_turbulence_effect");
    function(fuel.maximum_dilution_effect, base + ".maximum_dilution_effect");
    function(fuel.lbv_multiplier, base + ".lbv_multiplier");
    function(fuel.compression_ignition_enabled, base + ".compression_ignition_enabled");
    function(fuel.turbulence_to_flame_speed_ratio_triangle_radius,
             base + ".turbulence_to_flame_speed_ratio_triangle_radius");
    for (const auto &point : fuel.turbulence_to_flame_speed_ratio) {
        const auto point_base =
            base + ".turbulence_to_flame_speed_ratio." + point.sample_id.value;
        function(point.sample_id, point_base + ".sample_id");
        function(point.turbulence, point_base + ".turbulence");
        function(point.flame_speed_ratio, point_base + ".flame_speed_ratio");
    }
}

template <class Gains, class Function>
void visit_pressure_gains(const Gains &gains, const std::string &base,
                          Function function) {
    function(gains.gauge_static, base + ".gauge_static");
    function(gains.dynamic_forward, base + ".dynamic_forward");
    function(gains.dynamic_reverse, base + ".dynamic_reverse");
}

template <class Profile, class Function>
void visit_profile_fields(const Profile &profile, Function function,
                          const auto &cylinder_name, const auto &route_name) {
    constexpr std::string_view root = kLegacyProfileRoot;
    visit_crank(profile.mechanism.crank, std::string(root) + ".mechanism.crank",
                function);
    for (const auto &cylinder : profile.mechanism.cylinders) {
        visit_cylinder_parameters(cylinder.parameters,
                                  std::string(root) + ".mechanism.cylinders." +
                                      cylinder_name(cylinder),
                                  function);
    }

    visit_intake(profile.gas_path.intake, std::string(root) + ".gas_path.intake",
                 function);
    visit_head(profile.gas_path.head, std::string(root) + ".gas_path.head", function);
    for (const auto &route : profile.gas_path.exhaust_routes) {
        visit_exhaust_parameters(route.parameters,
                                 std::string(root) + ".gas_path.exhaust_routes." +
                                     route_name(route),
                                 function);
    }
    visit_restriction(profile.gas_path.piston_blowby,
                      std::string(root) + ".gas_path.piston_blowby", function);

    const auto visit_camshaft = [&](const auto &camshaft, const std::string &base) {
        visit_cam_shape(camshaft.shape, base + ".shape", function);
        for (const auto &lobe : camshaft.lobes) {
            function(lobe.crank_center_rad,
                     base + ".lobes." + cylinder_name(lobe) + ".crank_center_rad");
        }
    };
    visit_camshaft(profile.valvetrain.intake, std::string(root) + ".valvetrain.intake");
    visit_camshaft(profile.valvetrain.exhaust,
                   std::string(root) + ".valvetrain.exhaust");

    function(profile.ignition.firing_order,
             std::string(root) + ".ignition.firing_order");
    function(profile.ignition.timing_curve_triangle_radius_rad_s,
             std::string(root) + ".ignition.timing_curve_triangle_radius_rad_s");
    for (const auto &point : profile.ignition.timing_curve) {
        const auto point_base =
            std::string(root) + ".ignition.timing_curve." + point.sample_id.value;
        visit_timing_point(point, point_base, function);
    }
    function(profile.ignition.limiter_speed_rpm,
             std::string(root) + ".ignition.limiter_speed_rpm");
    function(profile.ignition.limiter_hold_s,
             std::string(root) + ".ignition.limiter_hold_s");
    function(profile.ignition.declared_redline_rpm,
             std::string(root) + ".ignition.declared_redline_rpm");

    visit_fuel(profile.fuel, std::string(root) + ".fuel", function);
    for (const auto &stream : profile.combustion_random_streams) {
        const auto stream_base =
            std::string(root) + ".combustion_random_streams." + cylinder_name(stream);
        function(stream.pcg32_initial_state, stream_base + ".pcg32_initial_state");
        function(stream.pcg32_stream, stream_base + ".pcg32_stream");
    }
    function(profile.losses.included_terms,
             std::string(root) + ".losses.included_terms");
    function(profile.losses.omitted_terms, std::string(root) + ".losses.omitted_terms");

    const auto excitation_base = std::string(root) + ".reference_excitation";
    function(profile.excitation.reference_atmosphere_pa_abs,
             excitation_base + ".reference_atmosphere_pa_abs");
    function(profile.excitation.legacy_propagation_speed_m_s,
             excitation_base + ".legacy_propagation_speed_m_s");
    function(profile.excitation.excitation_scale,
             excitation_base + ".excitation_scale");
    function(profile.excitation.filtered_speed_threshold_rpm,
             excitation_base + ".filtered_speed_threshold_rpm");
    function(profile.excitation.filtered_speed_exponent,
             excitation_base + ".filtered_speed_exponent");
    visit_pressure_gains(profile.excitation.pressure_gains,
                         excitation_base + ".pressure_gains", function);
    function(profile.excitation.cylinder_count_divisor,
             excitation_base + ".cylinder_count_divisor");
    function(profile.excitation.inverse_length_exponent,
             excitation_base + ".inverse_length_exponent");
    function(profile.excitation.delay_rate, excitation_base + ".delay_rate");
    function(profile.excitation.cylinder_accumulation_order,
             excitation_base + ".cylinder_accumulation_order");
    for (const auto &path : profile.excitation.cylinder_paths) {
        const auto path_base =
            excitation_base + ".cylinder_paths." + cylinder_name(path);
        function(path.header_primary_length_m, path_base + ".header_primary_length_m");
        function(path.sound_attenuation_linear,
                 path_base + ".sound_attenuation_linear");
        function(path.resolved_delay_samples, path_base + ".resolved_delay_samples");
    }
    for (const auto &route : profile.excitation.routes) {
        const auto route_base = excitation_base + ".routes." + route_name(route);
        function(route.exhaust_system_length_m,
                 route_base + ".exhaust_system_length_m");
        function(route.audio_volume_linear, route_base + ".audio_volume_linear");
    }
}

template <class Id, class Range, class Projection>
bool contains_id(const Range &range, Id id, Projection projection) {
    return std::ranges::find(range, id, projection) != range.end();
}

template <class Id, class Range, class Projection>
std::string semantic_id_for(const Range &range, Id id, Projection id_projection,
                            auto semantic_projection) {
    const auto iterator = std::ranges::find(range, id, id_projection);
    if (iterator == range.end()) {
        return "unknown-" + std::to_string(id.value);
    }
    return std::invoke(semantic_projection, *iterator);
}

std::string cylinder_name(const EngineSpec &engine, CylinderId id) {
    return semantic_id_for(
        engine.cylinders, id, &CylinderSpec::id,
        [](const CylinderSpec &cylinder) { return cylinder.semantic_id.value; });
}

std::string route_name(const EngineSpec &engine, RouteId id) {
    return semantic_id_for(
        engine.routes, id, &RouteSpec::id,
        [](const RouteSpec &route) { return route.semantic_id.value; });
}

bool known(LegacyRestrictionCalibration value) noexcept {
    return value == LegacyRestrictionCalibration::carb_at_1p5_inhg ||
           value == LegacyRestrictionCalibration::cfm_at_28_inh2o;
}

double restriction_pressure_drop_pa(LegacyRestrictionCalibration calibration) {
    if (calibration == LegacyRestrictionCalibration::cfm_at_28_inh2o) {
        return 28.0 * (3386.3886666666713 * 0.0734824);
    }
    return 1.5 * 3386.3886666666713;
}

template <class Restriction>
double recompute_restriction_k(const Restriction &restriction) {
    constexpr double gamma = 1.4;
    constexpr double pressure_pa = 101325.0;
    constexpr double temperature_k = 298.15;
    const auto pressure_target =
        pressure_pa - restriction_pressure_drop_pa(restriction.calibration.value);
    const auto ratio = pressure_target / pressure_pa;
    const auto critical = std::pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
    double flow = 0.0;
    if (ratio <= critical) {
        flow = std::sqrt(gamma);
        flow *= std::pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
    } else {
        flow = (2.0 * gamma) / (gamma - 1.0);
        flow *= 1.0 - std::pow(ratio, (gamma - 1.0) / gamma);
        flow = std::sqrt(flow);
        flow *= std::pow(ratio, 1.0 / gamma);
    }
    flow *= pressure_pa / std::sqrt(kGasConstant * temperature_k);
    return restriction.source_rating.value * kOneSourceScfm / flow;
}

void validate_restriction_domain(ValidationReport &report,
                                 const AuthoredLegacyRestriction &restriction,
                                 const std::string &path) {
    detail::require(report, known(restriction.calibration.value),
                    ContractIssueCode::unsupported_value, path + ".calibration.value",
                    "restriction calibration is not recognized");
    detail::require(report,
                    detail::finite_nonnegative(restriction.source_rating.value) &&
                        detail::finite_nonnegative(restriction.resolved_k.value),
                    ContractIssueCode::invalid_value, path,
                    "restriction rating and K must be finite and nonnegative");
    if (known(restriction.calibration.value) &&
        detail::finite_nonnegative(restriction.source_rating.value) &&
        detail::finite_nonnegative(restriction.resolved_k.value)) {
        detail::require(
            report,
            std::bit_cast<std::uint64_t>(restriction.resolved_k.value) ==
                std::bit_cast<std::uint64_t>(recompute_restriction_k(restriction)),
            ContractIssueCode::inconsistent_semantics, path + ".resolved_k.value",
            "authored restriction K must exactly match its source rating and "
            "calibration rule");
    }
}

void validate_restriction_domain(ValidationReport &report,
                                 const LegacyRestriction &restriction,
                                 const std::string &path,
                                 const ProvenanceLedger *provenance = nullptr) {
    detail::require(report, known(restriction.calibration.value),
                    ContractIssueCode::unsupported_value, path + ".calibration.value",
                    "restriction calibration is not recognized");
    detail::require(report,
                    detail::finite_nonnegative(restriction.source_rating.value) &&
                        detail::finite_nonnegative(restriction.resolved_k.value),
                    ContractIssueCode::invalid_value, path,
                    "restriction rating and K must be finite and nonnegative");
    if (known(restriction.calibration.value) &&
        detail::finite_nonnegative(restriction.source_rating.value)) {
        detail::require(
            report,
            std::bit_cast<std::uint64_t>(restriction.resolved_k.value) ==
                std::bit_cast<std::uint64_t>(recompute_restriction_k(restriction)),
            ContractIssueCode::inconsistent_semantics, path + ".resolved_k.value",
            "resolved restriction K must exactly match its source rating and "
            "calibration rule");
    }
    if (provenance != nullptr) {
        const auto canonical_path = profile_path(path);
        validate_derived_resolution(report, restriction.resolved_k, *provenance,
                                    canonical_path + ".resolved_k",
                                    {
                                        canonical_path + ".calibration",
                                        canonical_path + ".source_rating",
                                    });
    }
}

const GasVolumeSpec *find_volume(const EngineSpec &engine, GasVolumeId id) {
    return find_by_id(engine.gas_volumes, id, &GasVolumeSpec::id);
}

const FlowEdgeSpec *find_edge(const EngineSpec &engine, FlowEdgeId id) {
    return find_by_id(engine.flow_edges, id, &FlowEdgeSpec::id);
}

const RouteSpec *find_route(const EngineSpec &engine, RouteId id) {
    return find_by_id(engine.routes, id, &RouteSpec::id);
}

bool volume_has_kind(const EngineSpec &engine, GasVolumeId id, GasVolumeKind kind) {
    const auto *volume = find_volume(engine, id);
    return volume != nullptr && volume->kind.value == kind;
}

bool edge_connects(const EngineSpec &engine, FlowEdgeId id, GasVolumeId endpoint_0,
                   GasVolumeId endpoint_1) {
    const auto *edge = find_edge(engine, id);
    return edge != nullptr && edge->endpoint_0_volume_id == endpoint_0 &&
           edge->endpoint_1_volume_id == endpoint_1;
}

bool edge_connects_to_kind(const EngineSpec &engine, FlowEdgeId id,
                           GasVolumeKind endpoint_0_kind, GasVolumeId endpoint_1) {
    const auto *edge = find_edge(engine, id);
    return edge != nullptr &&
           volume_has_kind(engine, edge->endpoint_0_volume_id, endpoint_0_kind) &&
           edge->endpoint_1_volume_id == endpoint_1;
}

template <class Range> bool unique_valid(const Range &ids) {
    using Id = typename Range::value_type;
    return detail::all_unique_valid_ids(std::span<const Id>{ids.data(), ids.size()});
}

template <class Range, class Projection>
bool unique_valid_projected(const Range &range, Projection projection) {
    std::unordered_set<std::uint32_t> ids;
    for (const auto &item : range) {
        const auto id = std::invoke(projection, item);
        if (!id.valid() || !ids.insert(id.value).second) {
            return false;
        }
    }
    return true;
}

template <class Point>
void validate_sample_ids(ValidationReport &report, const std::vector<Point> &points,
                         const std::string &path) {
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto &id = points[index].sample_id.value;
        detail::require(report, is_valid_semantic_id(id),
                        ContractIssueCode::invalid_value,
                        path + "[" + std::to_string(index) + "].sample_id.value",
                        "table sample ID must be canonical");
        if (!ids.insert(id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + "[" + std::to_string(index) + "].sample_id.value",
                       "table sample IDs must be unique");
        }
    }
}

void validate_authored_domains(ValidationReport &report,
                               const AuthoredLegacyLowOrderV1Profile &profile) {
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::finite_positive;
    using detail::require;

    const auto &crank = profile.mechanism.crank;
    require(report,
            finite(crank.crank_tdc_reference_rad.value) &&
                finite_positive(crank.crankshaft_mass_kg.value) &&
                finite_positive(crank.flywheel_mass_kg.value) &&
                finite_positive(crank.authored_crank_inertia_kg_m2.value) &&
                finite_nonnegative(crank.fixed_crank_friction_magnitude_nm.value),
            ContractIssueCode::invalid_value, "mechanism.crank",
            "crank assembly values are outside their physical domain");
    require(report, !profile.mechanism.cylinders.empty(),
            ContractIssueCode::missing_value, "mechanism.cylinders",
            "legacy mechanism requires at least one cylinder");

    for (std::size_t index = 0; index < profile.mechanism.cylinders.size(); ++index) {
        const auto &cylinder = profile.mechanism.cylinders[index];
        const auto &parameters = cylinder.parameters;
        const auto path = "mechanism.cylinders[" + std::to_string(index) + "]";
        require(report,
                finite_positive(parameters.bore_m.value) &&
                    finite_positive(parameters.stroke_m.value) &&
                    finite_positive(parameters.crank_radius_m.value) &&
                    finite_positive(parameters.connecting_rod_length_m.value) &&
                    parameters.connecting_rod_length_m.value >
                        parameters.crank_radius_m.value &&
                    finite_positive(parameters.deck_height_m.value) &&
                    finite_positive(parameters.piston_compression_height_m.value) &&
                    finite_positive(parameters.head_chamber_volume_m3.value) &&
                    finite(parameters.piston_displacement_term_m3.value) &&
                    finite_positive(parameters.piston_mass_kg.value) &&
                    finite_positive(parameters.connecting_rod_mass_kg.value) &&
                    finite_positive(parameters.connecting_rod_inertia_kg_m2.value) &&
                    finite(parameters.journal_angle_rad.value) &&
                    finite(parameters.ignition_wire_angle_rad.value) &&
                    finite_nonnegative(parameters.header_primary_length_m.value),
                ContractIssueCode::invalid_value, path + ".parameters",
                "legacy cylinder parameters are outside their physical domain");
        require(report,
                detail::nearly_equal(2.0 * parameters.crank_radius_m.value,
                                     parameters.stroke_m.value),
                ContractIssueCode::inconsistent_semantics,
                path + ".parameters.crank_radius_m.value",
                "twice crank radius must equal stroke");

        if (finite_positive(parameters.bore_m.value) &&
            finite_positive(parameters.crank_radius_m.value) &&
            finite_positive(parameters.connecting_rod_length_m.value) &&
            finite_positive(parameters.deck_height_m.value) &&
            finite_positive(parameters.piston_compression_height_m.value) &&
            finite_positive(parameters.head_chamber_volume_m3.value) &&
            finite(parameters.piston_displacement_term_m3.value)) {
            const auto piston_area_m2 =
                kLegacyPi * parameters.bore_m.value * parameters.bore_m.value / 4.0;
            const auto tdc_mechanism_height_m =
                parameters.crank_radius_m.value +
                parameters.connecting_rod_length_m.value;
            const auto clearance_volume_m3 =
                parameters.head_chamber_volume_m3.value -
                parameters.piston_displacement_term_m3.value +
                piston_area_m2 *
                    (parameters.deck_height_m.value - tdc_mechanism_height_m -
                     parameters.piston_compression_height_m.value);
            const auto swept_volume_m3 =
                piston_area_m2 * (2.0 * parameters.crank_radius_m.value);
            const auto fixed_geometry_volume_m3 =
                parameters.head_chamber_volume_m3.value -
                parameters.piston_displacement_term_m3.value +
                piston_area_m2 * (parameters.deck_height_m.value -
                                  parameters.piston_compression_height_m.value);
            require(report,
                    finite_positive(piston_area_m2) &&
                        finite_positive(clearance_volume_m3) &&
                        finite_positive(swept_volume_m3) &&
                        finite_positive(fixed_geometry_volume_m3),
                    ContractIssueCode::inconsistent_semantics, path + ".parameters",
                    "derived piston area, clearance, swept volume, and fixed "
                    "cylinder geometry must be positive");
        }
    }

    const auto &intake = profile.gas_path.intake;
    require(report,
            finite_positive(intake.plenum_volume_m3.value) &&
                finite_positive(intake.plenum_cross_section_area_m2.value) &&
                finite_positive(intake.runner_length_m.value) &&
                finite_nonnegative(intake.velocity_decay.value) &&
                finite_positive(intake.throttle_gamma.value) &&
                detail::unit_interval(intake.idle_throttle_plate_position_01.value),
            ContractIssueCode::invalid_value, "gas_path.intake",
            "legacy intake parameters are outside their domain");
    validate_restriction_domain(report, intake.main_throttle,
                                "gas_path.intake.main_throttle");
    validate_restriction_domain(report, intake.idle_bypass,
                                "gas_path.intake.idle_bypass");
    validate_restriction_domain(report, intake.plenum_to_runner,
                                "gas_path.intake.plenum_to_runner");
    validate_restriction_domain(report, profile.gas_path.piston_blowby,
                                "gas_path.piston_blowby");

    const auto &head = profile.gas_path.head;
    require(report,
            finite_nonnegative(head.intake_runner_base_volume_m3.value) &&
                finite_positive(head.intake_runner_cross_section_area_m2.value) &&
                finite_nonnegative(head.exhaust_runner_base_volume_m3.value) &&
                finite_positive(head.exhaust_runner_cross_section_area_m2.value) &&
                finite_positive(head.flow_table_triangle_radius_m.value) &&
                head.intake_flow.size() >= 2 && head.exhaust_flow.size() >= 2,
            ContractIssueCode::invalid_value, "gas_path.head",
            "cylinder-head geometry and flow tables are invalid");
    validate_sample_ids(report, head.intake_flow, "gas_path.head.intake_flow");
    validate_sample_ids(report, head.exhaust_flow, "gas_path.head.exhaust_flow");
    const auto validate_flow = [&](const auto &points, const std::string &path) {
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto &point = points[index];
            const auto point_path = path + "." + point.sample_id.value;
            require(report,
                    finite_nonnegative(point.lift_m.value) &&
                        finite_nonnegative(point.source_cfm_at_28_inh2o.value) &&
                        finite_nonnegative(point.resolved_k.value),
                    ContractIssueCode::invalid_value, point_path,
                    "valve-flow sample must be finite and nonnegative");
            if (index != 0) {
                require(report, point.lift_m.value > points[index - 1].lift_m.value,
                        ContractIssueCode::inconsistent_semantics,
                        point_path + ".lift_m.value",
                        "valve-flow lifts must be strictly increasing");
            }
            validate_restriction_domain(
                report,
                AuthoredLegacyRestriction{
                    AuthoredValue<LegacyRestrictionCalibration>{
                        LegacyRestrictionCalibration::cfm_at_28_inh2o,
                        point.source_cfm_at_28_inh2o.claim_id,
                    },
                    point.source_cfm_at_28_inh2o,
                    point.resolved_k,
                },
                point_path);
        }
    };
    validate_flow(head.intake_flow, "gas_path.head.intake_flow");
    validate_flow(head.exhaust_flow, "gas_path.head.exhaust_flow");

    require(report, !profile.gas_path.exhaust_routes.empty(),
            ContractIssueCode::missing_value, "gas_path.exhaust_routes",
            "legacy gas path requires at least one exhaust route");
    for (std::size_t index = 0; index < profile.gas_path.exhaust_routes.size();
         ++index) {
        const auto &parameters = profile.gas_path.exhaust_routes[index].parameters;
        const auto path =
            "gas_path.exhaust_routes[" + std::to_string(index) + "].parameters";
        require(report,
                finite_positive(parameters.collector_volume_m3.value) &&
                    finite_positive(parameters.collector_cross_section_area_m2.value) &&
                    finite_positive(parameters.exhaust_system_length_m.value) &&
                    finite_nonnegative(parameters.primary_tube_length_m.value) &&
                    finite_nonnegative(parameters.velocity_decay.value) &&
                    finite_nonnegative(parameters.audio_volume_linear.value),
                ContractIssueCode::invalid_value, path,
                "exhaust-route parameters are outside their domain");
        if (finite_positive(parameters.collector_volume_m3.value) &&
            finite_positive(parameters.collector_cross_section_area_m2.value)) {
            const auto derived_length_m =
                parameters.collector_volume_m3.value /
                parameters.collector_cross_section_area_m2.value;
            require(report,
                    detail::nearly_equal(derived_length_m,
                                         parameters.exhaust_system_length_m.value),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".exhaust_system_length_m.value",
                    "exhaust-system length must equal collector volume divided "
                    "by collector cross-section area");
        }
        validate_restriction_domain(report, parameters.primary_to_collector,
                                    path + ".primary_to_collector");
        validate_restriction_domain(report, parameters.collector_outlet,
                                    path + ".collector_outlet");
    }

    const auto validate_camshaft = [&](const AuthoredLegacyCamshaftProfile &camshaft,
                                       const std::string &path) {
        const auto &shape = camshaft.shape;
        require(report,
                finite_positive(shape.maximum_lift_m.value) &&
                    finite_positive(shape.duration_at_reference_lift_rad.value) &&
                    finite_positive(shape.exponent.value) &&
                    shape.construction_steps.value >= 5 &&
                    finite(shape.advance_rad.value) &&
                    finite_positive(shape.base_radius_m.value),
                ContractIssueCode::invalid_value, path + ".shape",
                "cam shape is outside its domain");
        require(report, !camshaft.lobes.empty(), ContractIssueCode::missing_value,
                path + ".lobes", "camshaft requires at least one lobe");
        for (std::size_t index = 0; index < camshaft.lobes.size(); ++index) {
            require(report, finite(camshaft.lobes[index].crank_center_rad.value),
                    ContractIssueCode::invalid_value,
                    path + ".lobes[" + std::to_string(index) +
                        "].crank_center_rad.value",
                    "cam lobe center must be finite");
        }
    };
    validate_camshaft(profile.valvetrain.intake, "valvetrain.intake");
    validate_camshaft(profile.valvetrain.exhaust, "valvetrain.exhaust");

    require(
        report,
        finite_positive(profile.ignition.timing_curve_triangle_radius_rad_s.value) &&
            profile.ignition.timing_curve.size() >= 2 &&
            finite_positive(profile.ignition.limiter_speed_rpm.value) &&
            finite_positive(profile.ignition.limiter_hold_s.value) &&
            finite_positive(profile.ignition.declared_redline_rpm.value),
        ContractIssueCode::invalid_value, "ignition",
        "ignition curve, limiter, or redline is invalid");
    validate_sample_ids(report, profile.ignition.timing_curve, "ignition.timing_curve");
    for (std::size_t index = 0; index < profile.ignition.timing_curve.size(); ++index) {
        const auto &point = profile.ignition.timing_curve[index];
        require(report,
                finite_nonnegative(point.angular_speed_rad_s.value) &&
                    finite(point.timing_advance_rad.value),
                ContractIssueCode::invalid_value,
                "ignition.timing_curve." + point.sample_id.value,
                "ignition timing sample is invalid");
        if (index != 0) {
            require(
                report,
                point.angular_speed_rad_s.value >
                    profile.ignition.timing_curve[index - 1].angular_speed_rad_s.value,
                ContractIssueCode::inconsistent_semantics,
                "ignition.timing_curve." + point.sample_id.value,
                "ignition speed samples must be strictly increasing");
        }
    }

    const auto &fuel = profile.fuel;
    require(report, is_valid_semantic_id(fuel.fuel_id.value),
            ContractIssueCode::invalid_value, "fuel.fuel_id.value",
            "fuel ID must be canonical");
    require(report,
            finite_positive(fuel.molecular_mass_kg_per_mol.value) &&
                finite_positive(fuel.energy_density_j_per_kg.value) &&
                finite_positive(fuel.molecular_afr.value) &&
                detail::unit_interval(fuel.maximum_burning_efficiency_01.value) &&
                detail::unit_interval(fuel.burning_efficiency_randomness_01.value) &&
                detail::unit_interval(fuel.low_efficiency_attenuation_01.value) &&
                finite_nonnegative(fuel.maximum_turbulence_effect.value) &&
                finite_nonnegative(fuel.maximum_dilution_effect.value) &&
                finite_nonnegative(fuel.lbv_multiplier.value) &&
                finite_positive(
                    fuel.turbulence_to_flame_speed_ratio_triangle_radius.value) &&
                !fuel.turbulence_to_flame_speed_ratio.empty(),
            ContractIssueCode::invalid_value, "fuel",
            "legacy fuel parameters are outside their domain");
    require(report, !fuel.compression_ignition_enabled.value,
            ContractIssueCode::inconsistent_semantics,
            "fuel.compression_ignition_enabled.value",
            "the admitted spark-ignition profile cannot enable compression "
            "ignition");
    validate_sample_ids(report, fuel.turbulence_to_flame_speed_ratio,
                        "fuel.turbulence_to_flame_speed_ratio");
    for (std::size_t index = 0; index < fuel.turbulence_to_flame_speed_ratio.size();
         ++index) {
        const auto &point = fuel.turbulence_to_flame_speed_ratio[index];
        require(report,
                finite_nonnegative(point.turbulence.value) &&
                    finite_nonnegative(point.flame_speed_ratio.value),
                ContractIssueCode::invalid_value,
                "fuel.turbulence_to_flame_speed_ratio." + point.sample_id.value,
                "flame-speed curve sample is invalid");
        if (index != 0) {
            require(
                report,
                point.turbulence.value >
                    fuel.turbulence_to_flame_speed_ratio[index - 1].turbulence.value,
                ContractIssueCode::inconsistent_semantics,
                "fuel.turbulence_to_flame_speed_ratio." + point.sample_id.value,
                "flame-speed curve inputs must be strictly increasing");
        }
    }

    std::unordered_set<std::string> mechanism_cylinder_ids;
    for (const auto &cylinder : profile.mechanism.cylinders) {
        mechanism_cylinder_ids.insert(cylinder.topology.cylinder_id.value);
    }
    std::unordered_set<std::string> random_stream_cylinder_ids;
    for (const auto &stream : profile.combustion_random_streams) {
        const auto &cylinder_id = stream.cylinder_id.value;
        const auto stream_path =
            profile_path("combustion_random_streams." + cylinder_id);
        require(report, is_valid_semantic_id(cylinder_id),
                ContractIssueCode::invalid_value, stream_path + ".cylinder_id.value",
                "combustion random-stream owner must be canonical");
        require(report, mechanism_cylinder_ids.contains(cylinder_id),
                ContractIssueCode::dangling_reference,
                stream_path + ".cylinder_id.value",
                "combustion random stream references an unknown cylinder");
        if (!random_stream_cylinder_ids.insert(cylinder_id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       stream_path + ".cylinder_id.value",
                       "a cylinder may own only one combustion random stream");
        }
        require(
            report,
            stream.pcg32_stream.value <=
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            ContractIssueCode::invalid_value, stream_path + ".pcg32_stream.value",
            "PCG32 stream must fit the 63-bit sequence domain");
    }
    if (fuel.burning_efficiency_randomness_01.value > 0.0) {
        require(report,
                profile.combustion_random_streams.size() ==
                        profile.mechanism.cylinders.size() &&
                    mechanism_cylinder_ids.size() ==
                        profile.mechanism.cylinders.size() &&
                    random_stream_cylinder_ids == mechanism_cylinder_ids,
                ContractIssueCode::inconsistent_shape,
                profile_path("combustion_random_streams"),
                "nonzero burning randomness requires exactly one combustion "
                "random stream per cylinder");
    }

    const auto classified =
        profile.losses.included_terms.value | profile.losses.omitted_terms.value;
    require(report,
            (profile.losses.included_terms.value &
             profile.losses.omitted_terms.value) == 0 &&
                (classified & ~known_torque_term_mask()) == 0 &&
                classified == known_torque_term_mask(),
            ContractIssueCode::inconsistent_semantics, "losses",
            "legacy loss profile must classify every known torque term once");

    const auto &excitation = profile.excitation;
    require(report,
            finite_positive(excitation.reference_atmosphere_pa_abs.value) &&
                finite_positive(excitation.legacy_propagation_speed_m_s.value) &&
                finite_nonnegative(excitation.excitation_scale.value) &&
                finite_nonnegative(excitation.filtered_speed_threshold_rpm.value) &&
                excitation.filtered_speed_exponent.value > 0 &&
                finite(excitation.pressure_gains.gauge_static.value) &&
                finite(excitation.pressure_gains.dynamic_forward.value) &&
                finite(excitation.pressure_gains.dynamic_reverse.value) &&
                finite_positive(excitation.cylinder_count_divisor.value) &&
                finite(excitation.inverse_length_exponent.value) &&
                detail::nearly_equal(
                    excitation.cylinder_count_divisor.value,
                    static_cast<double>(profile.mechanism.cylinders.size())),
            ContractIssueCode::invalid_value, "reference_excitation",
            "reference excitation values are outside their domain");
    detail::append_prefixed(report, validate(excitation.delay_rate.value),
                            "reference_excitation.delay_rate.value");

    const auto find_gas_route =
        [&](std::string_view id) -> const AuthoredLegacyExhaustRouteProfile * {
        const auto iterator =
            std::ranges::find_if(profile.gas_path.exhaust_routes,
                                 [&](const AuthoredLegacyExhaustRouteProfile &route) {
                                     return route.topology.route_id.value == id;
                                 });
        return iterator == profile.gas_path.exhaust_routes.end() ? nullptr : &*iterator;
    };
    const auto find_excitation_route =
        [&](std::string_view id) -> const AuthoredLegacyExcitationRoute * {
        const auto iterator = std::ranges::find_if(
            excitation.routes, [&](const AuthoredLegacyExcitationRoute &route) {
                return route.route_id.value == id;
            });
        return iterator == excitation.routes.end() ? nullptr : &*iterator;
    };
    const auto find_mechanism_cylinder =
        [&](std::string_view id) -> const AuthoredLegacyCylinderAssembly * {
        const auto iterator =
            std::ranges::find_if(profile.mechanism.cylinders,
                                 [&](const AuthoredLegacyCylinderAssembly &cylinder) {
                                     return cylinder.topology.cylinder_id.value == id;
                                 });
        return iterator == profile.mechanism.cylinders.end() ? nullptr : &*iterator;
    };

    for (std::size_t index = 0; index < excitation.routes.size(); ++index) {
        const auto &route = excitation.routes[index];
        const auto path = "reference_excitation.routes[" + std::to_string(index) + "]";
        require(report,
                finite_positive(route.exhaust_system_length_m.value) &&
                    finite_nonnegative(route.audio_volume_linear.value),
                ContractIssueCode::invalid_value, path,
                "excitation route is outside its domain");
        const auto *gas_route = find_gas_route(route.route_id.value);
        if (gas_route != nullptr) {
            require(
                report,
                detail::nearly_equal(
                    route.exhaust_system_length_m.value,
                    gas_route->parameters.exhaust_system_length_m.value) &&
                    detail::nearly_equal(
                        route.audio_volume_linear.value,
                        gas_route->parameters.audio_volume_linear.value),
                ContractIssueCode::inconsistent_semantics, path,
                "excitation route length and gain must agree with its gas-path route");
        }
    }

    for (std::size_t index = 0; index < excitation.cylinder_paths.size(); ++index) {
        const auto &path = excitation.cylinder_paths[index];
        const auto local_path =
            "reference_excitation.cylinder_paths[" + std::to_string(index) + "]";
        require(report,
                finite_nonnegative(path.header_primary_length_m.value) &&
                    finite_nonnegative(path.sound_attenuation_linear.value),
                ContractIssueCode::invalid_value, local_path,
                "excitation cylinder path is outside its domain");
        const auto *cylinder = find_mechanism_cylinder(path.cylinder_id.value);
        if (cylinder != nullptr) {
            require(report,
                    detail::nearly_equal(
                        path.header_primary_length_m.value,
                        cylinder->parameters.header_primary_length_m.value),
                    ContractIssueCode::inconsistent_semantics, local_path,
                    "excitation header length must agree with its mechanism cylinder");
        }

        const auto *route = find_excitation_route(path.route_id.value);
        if (route != nullptr &&
            finite_nonnegative(path.header_primary_length_m.value) &&
            finite_positive(route->exhaust_system_length_m.value) &&
            finite_positive(excitation.legacy_propagation_speed_m_s.value) &&
            excitation.delay_rate.value.numerator != 0 &&
            excitation.delay_rate.value.denominator != 0) {
            const auto total_length_m = path.header_primary_length_m.value +
                                        route->exhaust_system_length_m.value;
            const auto delay_seconds =
                total_length_m / excitation.legacy_propagation_speed_m_s.value;
            const auto delay_rate_hz =
                static_cast<double>(excitation.delay_rate.value.numerator) /
                static_cast<double>(excitation.delay_rate.value.denominator);
            const auto requested_samples = delay_seconds * delay_rate_hz;
            const auto rounded_samples = std::round(requested_samples);
            const auto delay_in_range =
                finite(total_length_m) && total_length_m >= 0.0 &&
                finite(delay_seconds) && delay_seconds >= 0.0 &&
                finite(delay_rate_hz) && delay_rate_hz > 0.0 &&
                finite(requested_samples) && requested_samples >= 0.0 &&
                finite(rounded_samples) && rounded_samples >= 0.0 &&
                rounded_samples <=
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max());
            require(report, delay_in_range, ContractIssueCode::invalid_value,
                    local_path + ".resolved_delay_samples.value",
                    "derived propagation delay must fit uint32 sample count");
            if (delay_in_range) {
                require(report,
                        path.resolved_delay_samples.value ==
                            static_cast<std::uint32_t>(rounded_samples),
                        ContractIssueCode::inconsistent_semantics,
                        local_path + ".resolved_delay_samples.value",
                        "authored propagation delay must equal round-ties-away "
                        "of path time at delay rate");
            }
        }
    }
}

void validate_domains(ValidationReport &report, const LegacyLowOrderV1Profile &profile,
                      const EngineSpec &engine, const ProvenanceLedger &provenance) {
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::finite_positive;
    using detail::require;

    const auto &crank = profile.mechanism.crank;
    require(report,
            finite(crank.crank_tdc_reference_rad.value) &&
                finite_positive(crank.crankshaft_mass_kg.value) &&
                finite_positive(crank.flywheel_mass_kg.value) &&
                finite_positive(crank.authored_crank_inertia_kg_m2.value) &&
                finite_nonnegative(crank.fixed_crank_friction_magnitude_nm.value),
            ContractIssueCode::invalid_value, "mechanism.crank",
            "crank assembly values are outside their physical domain");

    require(report,
            profile.mechanism.cylinders.size() == engine.cylinders.size() &&
                unique_valid_projected(profile.mechanism.cylinders,
                                       [](const LegacyCylinderAssembly &cylinder) {
                                           return cylinder.topology.cylinder_id;
                                       }),
            ContractIssueCode::inconsistent_shape, "mechanism.cylinders",
            "mechanism must bind every engine cylinder exactly once");

    const auto &intake_topology = profile.gas_path.intake_topology;
    const auto *plenum = find_volume(engine, intake_topology.plenum_volume_id);
    require(report,
            plenum != nullptr && plenum->kind.value == GasVolumeKind::intake_plenum,
            ContractIssueCode::inconsistent_semantics,
            "gas_path.intake_topology.plenum_volume_id",
            "legacy intake plenum must reference an intake-plenum volume");
    require(report,
            edge_connects_to_kind(engine, intake_topology.main_throttle_edge_id,
                                  GasVolumeKind::atmosphere,
                                  intake_topology.plenum_volume_id) &&
                edge_connects_to_kind(engine, intake_topology.idle_bypass_edge_id,
                                      GasVolumeKind::atmosphere,
                                      intake_topology.plenum_volume_id),
            ContractIssueCode::inconsistent_semantics, "gas_path.intake_topology",
            "main and idle intake edges must be oriented atmosphere-to-plenum");
    require(report,
            intake_topology.main_throttle_edge_id !=
                intake_topology.idle_bypass_edge_id,
            ContractIssueCode::duplicate_identity, "gas_path.intake_topology",
            "main throttle and idle bypass must be distinct flow edges");

    const auto find_gas_route = [&](RouteId id) -> const LegacyExhaustRouteProfile * {
        return find_by_id(profile.gas_path.exhaust_routes, id,
                          [](const LegacyExhaustRouteProfile &route) {
                              return route.topology.route_id;
                          });
    };
    std::unordered_set<std::uint32_t> intake_port_ids;
    std::unordered_set<std::uint32_t> exhaust_port_ids;
    std::unordered_set<std::uint32_t> runner_volume_ids;
    std::unordered_set<std::uint32_t> chamber_volume_ids;
    std::unordered_set<std::uint32_t> primary_volume_ids;
    std::unordered_set<std::uint32_t> plenum_runner_edge_ids;
    std::unordered_set<std::uint32_t> intake_valve_edge_ids;
    std::unordered_set<std::uint32_t> exhaust_valve_edge_ids;
    std::unordered_set<std::uint32_t> primary_collector_edge_ids;
    std::unordered_set<std::uint32_t> blowby_edge_ids;
    std::unordered_set<std::uint32_t> topology_edge_ids;
    std::unordered_set<std::uint32_t> atmosphere_volume_ids;
    std::unordered_set<std::uint32_t> used_exhaust_route_ids;
    double profile_displacement_m3 = 0.0;

    const auto require_unique_resource = [&](auto &ids, const auto id,
                                             const std::string &path,
                                             std::string_view role) {
        if (id.valid() && !ids.insert(id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       std::string(role) + " must be owned by exactly one cylinder");
        }
    };
    const auto collect_valid_id = [](auto &ids, const auto id) {
        if (id.valid()) {
            ids.insert(id.value);
        }
    };
    collect_valid_id(topology_edge_ids, intake_topology.main_throttle_edge_id);
    collect_valid_id(topology_edge_ids, intake_topology.idle_bypass_edge_id);
    for (const auto edge_id :
         {intake_topology.main_throttle_edge_id, intake_topology.idle_bypass_edge_id}) {
        const auto *edge = find_edge(engine, edge_id);
        if (edge != nullptr && volume_has_kind(engine, edge->endpoint_0_volume_id,
                                               GasVolumeKind::atmosphere)) {
            collect_valid_id(atmosphere_volume_ids, edge->endpoint_0_volume_id);
        }
    }
    for (const auto &cylinder : profile.mechanism.cylinders) {
        const auto &topology = cylinder.topology;
        const auto &parameters = cylinder.parameters;
        const auto path =
            "mechanism.cylinders." + cylinder_name(engine, topology.cylinder_id);
        const auto *engine_cylinder =
            find_by_id(engine.cylinders, topology.cylinder_id, &CylinderSpec::id);
        const auto *intake_port =
            find_by_id(engine.ports, topology.intake_port_id, &PortSpec::id);
        const auto *exhaust_port =
            find_by_id(engine.ports, topology.exhaust_port_id, &PortSpec::id);
        const auto *gas_route = find_gas_route(topology.exhaust_route_id);
        const auto *engine_route = find_route(engine, topology.exhaust_route_id);

        require(report, engine_cylinder != nullptr,
                ContractIssueCode::dangling_reference, path + ".topology.cylinder_id",
                "legacy cylinder references an unknown engine cylinder");
        require(report,
                intake_port != nullptr &&
                    intake_port->cylinder_id == topology.cylinder_id &&
                    intake_port->kind.value == PortKind::intake,
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.intake_port_id",
                "intake port must belong to this cylinder and have intake kind");
        require(report,
                exhaust_port != nullptr &&
                    exhaust_port->cylinder_id == topology.cylinder_id &&
                    exhaust_port->kind.value == PortKind::exhaust,
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.exhaust_port_id",
                "exhaust port must belong to this cylinder and have exhaust kind");
        require(report,
                volume_has_kind(engine, topology.intake_runner_volume_id,
                                GasVolumeKind::intake_runner) &&
                    volume_has_kind(engine, topology.chamber_volume_id,
                                    GasVolumeKind::cylinder) &&
                    volume_has_kind(engine, topology.exhaust_primary_volume_id,
                                    GasVolumeKind::exhaust_primary),
                ContractIssueCode::inconsistent_semantics, path + ".topology",
                "cylinder volumes must have runner, chamber, and primary roles");
        require(report,
                edge_connects(engine, topology.plenum_to_runner_edge_id,
                              intake_topology.plenum_volume_id,
                              topology.intake_runner_volume_id) &&
                    edge_connects(engine, topology.intake_valve_edge_id,
                                  topology.intake_runner_volume_id,
                                  topology.chamber_volume_id) &&
                    edge_connects(engine, topology.exhaust_valve_edge_id,
                                  topology.chamber_volume_id,
                                  topology.exhaust_primary_volume_id),
                ContractIssueCode::inconsistent_semantics, path + ".topology",
                "cylinder intake/exhaust edges have incorrect endpoint orientation");
        const auto *blowby = find_edge(engine, topology.blowby_edge_id);
        require(report,
                blowby != nullptr &&
                    blowby->endpoint_0_volume_id == topology.chamber_volume_id &&
                    volume_has_kind(engine, blowby->endpoint_1_volume_id,
                                    GasVolumeKind::atmosphere),
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.blowby_edge_id",
                "blowby edge must be oriented chamber-to-environment");
        require(report, gas_route != nullptr, ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "cylinder exhaust route is absent from the legacy gas path");
        if (gas_route != nullptr) {
            require(report,
                    edge_connects(engine, topology.primary_to_collector_edge_id,
                                  topology.exhaust_primary_volume_id,
                                  gas_route->topology.collector_volume_id),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".topology.primary_to_collector_edge_id",
                    "primary-to-collector edge has incorrect endpoint orientation");
        }
        require(
            report,
            engine_route != nullptr &&
                engine_route->kind.value == SourceRouteKind::exhaust_outlet &&
                gas_route != nullptr &&
                engine_route->source_volume_id ==
                    std::optional<GasVolumeId>{gas_route->topology.collector_volume_id},
            ContractIssueCode::inconsistent_semantics,
            path + ".topology.exhaust_route_id",
            "cylinder exhaust route must source its matching collector");

        require_unique_resource(intake_port_ids, topology.intake_port_id,
                                path + ".topology.intake_port_id", "intake port");
        require_unique_resource(exhaust_port_ids, topology.exhaust_port_id,
                                path + ".topology.exhaust_port_id", "exhaust port");
        require_unique_resource(runner_volume_ids, topology.intake_runner_volume_id,
                                path + ".topology.intake_runner_volume_id",
                                "intake runner");
        require_unique_resource(chamber_volume_ids, topology.chamber_volume_id,
                                path + ".topology.chamber_volume_id", "chamber");
        require_unique_resource(primary_volume_ids, topology.exhaust_primary_volume_id,
                                path + ".topology.exhaust_primary_volume_id",
                                "exhaust primary");
        require_unique_resource(
            plenum_runner_edge_ids, topology.plenum_to_runner_edge_id,
            path + ".topology.plenum_to_runner_edge_id", "plenum-to-runner edge");
        require_unique_resource(intake_valve_edge_ids, topology.intake_valve_edge_id,
                                path + ".topology.intake_valve_edge_id",
                                "intake-valve edge");
        require_unique_resource(exhaust_valve_edge_ids, topology.exhaust_valve_edge_id,
                                path + ".topology.exhaust_valve_edge_id",
                                "exhaust-valve edge");
        require_unique_resource(primary_collector_edge_ids,
                                topology.primary_to_collector_edge_id,
                                path + ".topology.primary_to_collector_edge_id",
                                "primary-to-collector edge");
        require_unique_resource(blowby_edge_ids, topology.blowby_edge_id,
                                path + ".topology.blowby_edge_id", "blowby edge");
        collect_valid_id(topology_edge_ids, topology.plenum_to_runner_edge_id);
        collect_valid_id(topology_edge_ids, topology.intake_valve_edge_id);
        collect_valid_id(topology_edge_ids, topology.exhaust_valve_edge_id);
        collect_valid_id(topology_edge_ids, topology.primary_to_collector_edge_id);
        collect_valid_id(topology_edge_ids, topology.blowby_edge_id);
        if (blowby != nullptr && volume_has_kind(engine, blowby->endpoint_1_volume_id,
                                                 GasVolumeKind::atmosphere)) {
            collect_valid_id(atmosphere_volume_ids, blowby->endpoint_1_volume_id);
        }
        if (topology.exhaust_route_id.valid()) {
            used_exhaust_route_ids.insert(topology.exhaust_route_id.value);
        }

        require(report,
                finite_positive(parameters.bore_m.value) &&
                    finite_positive(parameters.stroke_m.value) &&
                    finite_positive(parameters.crank_radius_m.value) &&
                    finite_positive(parameters.connecting_rod_length_m.value) &&
                    parameters.connecting_rod_length_m.value >
                        parameters.crank_radius_m.value &&
                    finite_positive(parameters.deck_height_m.value) &&
                    finite_positive(parameters.piston_compression_height_m.value) &&
                    finite_positive(parameters.head_chamber_volume_m3.value) &&
                    finite(parameters.piston_displacement_term_m3.value) &&
                    finite_positive(parameters.piston_mass_kg.value) &&
                    finite_positive(parameters.connecting_rod_mass_kg.value) &&
                    finite_positive(parameters.connecting_rod_inertia_kg_m2.value) &&
                    finite(parameters.journal_angle_rad.value) &&
                    finite(parameters.ignition_wire_angle_rad.value) &&
                    finite_nonnegative(parameters.header_primary_length_m.value),
                ContractIssueCode::invalid_value, path + ".parameters",
                "legacy cylinder parameters are outside their physical domain");
        require(report,
                detail::nearly_equal(2.0 * parameters.crank_radius_m.value,
                                     parameters.stroke_m.value),
                ContractIssueCode::inconsistent_semantics,
                path + ".parameters.crank_radius_m.value",
                "twice crank radius must equal stroke");

        if (engine_cylinder != nullptr) {
            require(
                report,
                detail::nearly_equal(parameters.bore_m.value,
                                     engine_cylinder->bore_m.value) &&
                    detail::nearly_equal(parameters.stroke_m.value,
                                         engine_cylinder->stroke_m.value) &&
                    detail::nearly_equal(
                        parameters.connecting_rod_length_m.value,
                        engine_cylinder->connecting_rod_length_m.value) &&
                    detail::nearly_equal(parameters.journal_angle_rad.value,
                                         engine_cylinder->journal_phase_rad.value) &&
                    detail::nearly_equal(parameters.ignition_wire_angle_rad.value,
                                         engine_cylinder->firing_tdc_offset_rad.value),
                ContractIssueCode::inconsistent_semantics, path + ".parameters",
                "legacy cylinder geometry and phases must agree with EngineSpec");
        }

        if (finite_positive(parameters.bore_m.value) &&
            finite_positive(parameters.crank_radius_m.value) &&
            finite_positive(parameters.connecting_rod_length_m.value) &&
            finite_positive(parameters.deck_height_m.value) &&
            finite_positive(parameters.piston_compression_height_m.value) &&
            finite_positive(parameters.head_chamber_volume_m3.value) &&
            finite(parameters.piston_displacement_term_m3.value)) {
            const auto piston_area_m2 =
                kLegacyPi * parameters.bore_m.value * parameters.bore_m.value / 4.0;
            const auto tdc_mechanism_height_m =
                parameters.crank_radius_m.value * std::cos(0.0) +
                std::sqrt(parameters.connecting_rod_length_m.value *
                          parameters.connecting_rod_length_m.value);
            const auto clearance_volume_m3 =
                parameters.head_chamber_volume_m3.value -
                parameters.piston_displacement_term_m3.value +
                piston_area_m2 *
                    (parameters.deck_height_m.value - tdc_mechanism_height_m -
                     parameters.piston_compression_height_m.value);
            const auto swept_volume_m3 =
                piston_area_m2 * (2.0 * parameters.crank_radius_m.value);
            const auto fixed_geometry_volume_m3 =
                parameters.head_chamber_volume_m3.value -
                parameters.piston_displacement_term_m3.value +
                piston_area_m2 * (parameters.deck_height_m.value -
                                  parameters.piston_compression_height_m.value);
            require(report,
                    finite_positive(piston_area_m2) &&
                        finite_positive(clearance_volume_m3) &&
                        finite_positive(swept_volume_m3) &&
                        finite_positive(fixed_geometry_volume_m3),
                    ContractIssueCode::inconsistent_semantics, path + ".parameters",
                    "derived piston area, clearance, swept volume, and fixed "
                    "cylinder geometry must be positive");
            if (finite_positive(clearance_volume_m3) &&
                finite_positive(swept_volume_m3)) {
                const auto compression_ratio =
                    (clearance_volume_m3 + swept_volume_m3) / clearance_volume_m3;
                if (engine_cylinder != nullptr) {
                    require(
                        report,
                        detail::nearly_equal(compression_ratio,
                                             engine_cylinder->compression_ratio.value),
                        ContractIssueCode::inconsistent_semantics,
                        path + ".parameters.head_chamber_volume_m3.value",
                        "derived compression ratio must agree with EngineSpec");
                }
                profile_displacement_m3 += swept_volume_m3;
            }
        }
    }
    require(report,
            detail::nearly_equal(profile_displacement_m3,
                                 engine.total_displacement_m3.value),
            ContractIssueCode::inconsistent_semantics, "mechanism.cylinders",
            "legacy mechanism displacement must agree with EngineSpec");

    const auto &intake = profile.gas_path.intake;
    require(report,
            finite_positive(intake.plenum_volume_m3.value) &&
                finite_positive(intake.plenum_cross_section_area_m2.value) &&
                finite_positive(intake.runner_length_m.value) &&
                finite_nonnegative(intake.velocity_decay.value) &&
                finite_positive(intake.throttle_gamma.value) &&
                detail::unit_interval(intake.idle_throttle_plate_position_01.value),
            ContractIssueCode::invalid_value, "gas_path.intake",
            "legacy intake parameters are outside their domain");
    validate_restriction_domain(report, intake.main_throttle,
                                "gas_path.intake.main_throttle", &provenance);
    validate_restriction_domain(report, intake.idle_bypass,
                                "gas_path.intake.idle_bypass", &provenance);
    validate_restriction_domain(report, intake.plenum_to_runner,
                                "gas_path.intake.plenum_to_runner", &provenance);
    validate_restriction_domain(report, profile.gas_path.piston_blowby,
                                "gas_path.piston_blowby", &provenance);

    const auto &head = profile.gas_path.head;
    require(report,
            finite_nonnegative(head.intake_runner_base_volume_m3.value) &&
                finite_positive(head.intake_runner_cross_section_area_m2.value) &&
                finite_nonnegative(head.exhaust_runner_base_volume_m3.value) &&
                finite_positive(head.exhaust_runner_cross_section_area_m2.value) &&
                finite_positive(head.flow_table_triangle_radius_m.value) &&
                head.intake_flow.size() >= 2 && head.exhaust_flow.size() >= 2,
            ContractIssueCode::invalid_value, "gas_path.head",
            "cylinder-head geometry and flow tables are invalid");
    validate_sample_ids(report, head.intake_flow, "gas_path.head.intake_flow");
    validate_sample_ids(report, head.exhaust_flow, "gas_path.head.exhaust_flow");
    const auto validate_flow = [&](const auto &points, const std::string &path) {
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto &point = points[index];
            require(report,
                    finite_nonnegative(point.lift_m.value) &&
                        finite_nonnegative(point.source_cfm_at_28_inh2o.value) &&
                        finite_nonnegative(point.resolved_k.value),
                    ContractIssueCode::invalid_value,
                    path + "." + point.sample_id.value,
                    "valve-flow sample must be finite and nonnegative");
            if (index != 0) {
                require(report, point.lift_m.value > points[index - 1].lift_m.value,
                        ContractIssueCode::inconsistent_semantics,
                        path + "." + point.sample_id.value + ".lift_m.value",
                        "valve-flow lifts must be strictly increasing");
            }
            LegacyRestriction restriction{
                ResolvedValue<LegacyRestrictionCalibration>{
                    LegacyRestrictionCalibration::cfm_at_28_inh2o, {}},
                point.source_cfm_at_28_inh2o,
                point.resolved_k,
            };
            validate_restriction_domain(report, restriction,
                                        path + "." + point.sample_id.value);
            const auto point_path = profile_path(path + "." + point.sample_id.value);
            validate_derived_resolution(report, point.resolved_k, provenance,
                                        point_path + ".resolved_k",
                                        {point_path + ".source_cfm_at_28_inh2o"});
        }
    };
    validate_flow(head.intake_flow, "gas_path.head.intake_flow");
    validate_flow(head.exhaust_flow, "gas_path.head.exhaust_flow");

    require(report,
            !profile.gas_path.exhaust_routes.empty() &&
                unique_valid_projected(profile.gas_path.exhaust_routes,
                                       [](const LegacyExhaustRouteProfile &route) {
                                           return route.topology.route_id;
                                       }),
            ContractIssueCode::inconsistent_shape, "gas_path.exhaust_routes",
            "legacy gas path requires uniquely identified exhaust routes");
    std::unordered_set<std::uint32_t> engine_exhaust_route_ids;
    for (const auto &route : engine.routes) {
        if (route.kind.value == SourceRouteKind::exhaust_outlet) {
            engine_exhaust_route_ids.insert(route.id.value);
        }
    }
    std::unordered_set<std::uint32_t> gas_path_route_ids;
    std::unordered_set<std::uint32_t> collector_volume_ids;
    std::unordered_set<std::uint32_t> collector_outlet_edge_ids;
    for (const auto &route : profile.gas_path.exhaust_routes) {
        const auto path =
            "gas_path.exhaust_routes." + route_name(engine, route.topology.route_id);
        const auto *engine_route = find_route(engine, route.topology.route_id);
        const auto *collector = find_volume(engine, route.topology.collector_volume_id);
        const auto *collector_outlet =
            find_edge(engine, route.topology.collector_outlet_edge_id);
        require(report,
                engine_route != nullptr &&
                    engine_route->kind.value == SourceRouteKind::exhaust_outlet &&
                    collector != nullptr &&
                    collector->kind.value == GasVolumeKind::exhaust_collector &&
                    engine_route->source_volume_id ==
                        std::optional<GasVolumeId>{route.topology.collector_volume_id},
                ContractIssueCode::inconsistent_semantics, path + ".topology",
                "legacy exhaust route must source its declared collector");
        require(report,
                collector_outlet != nullptr &&
                    volume_has_kind(engine, collector_outlet->endpoint_0_volume_id,
                                    GasVolumeKind::atmosphere) &&
                    collector_outlet->endpoint_1_volume_id ==
                        route.topology.collector_volume_id,
                ContractIssueCode::inconsistent_semantics,
                path + ".topology.collector_outlet_edge_id",
                "collector outlet must be oriented atmosphere-to-collector");
        if (route.topology.route_id.valid()) {
            gas_path_route_ids.insert(route.topology.route_id.value);
        }
        require_unique_resource(collector_volume_ids,
                                route.topology.collector_volume_id,
                                path + ".topology.collector_volume_id", "collector");
        require_unique_resource(
            collector_outlet_edge_ids, route.topology.collector_outlet_edge_id,
            path + ".topology.collector_outlet_edge_id", "collector-outlet edge");
        collect_valid_id(topology_edge_ids, route.topology.collector_outlet_edge_id);
        if (collector_outlet != nullptr &&
            volume_has_kind(engine, collector_outlet->endpoint_0_volume_id,
                            GasVolumeKind::atmosphere)) {
            collect_valid_id(atmosphere_volume_ids,
                             collector_outlet->endpoint_0_volume_id);
        }

        const auto &parameters = route.parameters;
        require(report,
                finite_positive(parameters.collector_volume_m3.value) &&
                    finite_positive(parameters.collector_cross_section_area_m2.value) &&
                    finite_positive(parameters.exhaust_system_length_m.value) &&
                    finite_nonnegative(parameters.primary_tube_length_m.value) &&
                    finite_nonnegative(parameters.velocity_decay.value) &&
                    finite_nonnegative(parameters.audio_volume_linear.value),
                ContractIssueCode::invalid_value, path + ".parameters",
                "exhaust-route parameters are outside their domain");
        if (finite_positive(parameters.collector_volume_m3.value) &&
            finite_positive(parameters.collector_cross_section_area_m2.value)) {
            const auto derived_length_m =
                parameters.collector_volume_m3.value /
                parameters.collector_cross_section_area_m2.value;
            require(report,
                    detail::nearly_equal(derived_length_m,
                                         parameters.exhaust_system_length_m.value),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".exhaust_system_length_m.value",
                    "exhaust-system length must equal collector volume divided "
                    "by collector cross-section area");
        }
        validate_restriction_domain(report, parameters.primary_to_collector,
                                    path + ".primary_to_collector", &provenance);
        validate_restriction_domain(report, parameters.collector_outlet,
                                    path + ".collector_outlet", &provenance);
    }
    const auto role_ids = [](const auto &range, const auto expected_kind) {
        std::unordered_set<std::uint32_t> ids;
        for (const auto &item : range) {
            if (item.kind.value == expected_kind && item.id.valid()) {
                ids.insert(item.id.value);
            }
        }
        return ids;
    };
    const auto require_exact_role_coverage =
        [&](const auto &range, const auto expected_kind,
            const std::unordered_set<std::uint32_t> &profile_ids,
            const std::string &path, std::string_view role) {
            require(report, role_ids(range, expected_kind) == profile_ids,
                    ContractIssueCode::inconsistent_shape, path,
                    std::string(role) +
                        " must exactly match the legacy topology's bound set");
        };
    require_exact_role_coverage(engine.ports, PortKind::intake, intake_port_ids,
                                "mechanism.cylinders", "resolved intake ports");
    require_exact_role_coverage(engine.ports, PortKind::exhaust, exhaust_port_ids,
                                "mechanism.cylinders", "resolved exhaust ports");
    require_exact_role_coverage(
        engine.gas_volumes, GasVolumeKind::intake_plenum,
        std::unordered_set<std::uint32_t>{intake_topology.plenum_volume_id.value},
        "gas_path.intake_topology.plenum_volume_id", "resolved intake plenums");
    require_exact_role_coverage(engine.gas_volumes, GasVolumeKind::intake_runner,
                                runner_volume_ids, "mechanism.cylinders",
                                "resolved intake runners");
    require_exact_role_coverage(engine.gas_volumes, GasVolumeKind::cylinder,
                                chamber_volume_ids, "mechanism.cylinders",
                                "resolved chamber volumes");
    require_exact_role_coverage(engine.gas_volumes, GasVolumeKind::exhaust_primary,
                                primary_volume_ids, "mechanism.cylinders",
                                "resolved exhaust primaries");
    require_exact_role_coverage(engine.gas_volumes, GasVolumeKind::exhaust_collector,
                                collector_volume_ids, "gas_path.exhaust_routes",
                                "resolved exhaust collectors");
    require_exact_role_coverage(engine.gas_volumes, GasVolumeKind::atmosphere,
                                atmosphere_volume_ids, "gas_path",
                                "resolved boundary atmosphere volumes");

    std::unordered_set<std::uint32_t> engine_flow_edge_ids;
    for (const auto &edge : engine.flow_edges) {
        collect_valid_id(engine_flow_edge_ids, edge.id);
    }
    const auto topology_edge_binding_count = std::size_t{2} +
                                             5 * profile.mechanism.cylinders.size() +
                                             profile.gas_path.exhaust_routes.size();
    require(report,
            engine_flow_edge_ids == topology_edge_ids &&
                topology_edge_ids.size() == topology_edge_binding_count,
            ContractIssueCode::inconsistent_shape, "gas_path",
            "resolved flow edges must be covered exactly once by the legacy "
            "topology");

    require(report, gas_path_route_ids == engine_exhaust_route_ids,
            ContractIssueCode::inconsistent_shape, "gas_path.exhaust_routes",
            "legacy gas path must cover every engine exhaust-outlet route exactly "
            "once");
    require(report, used_exhaust_route_ids == gas_path_route_ids,
            ContractIssueCode::inconsistent_shape, "mechanism.cylinders",
            "every legacy exhaust route must be used by at least one cylinder");

    const auto validate_camshaft = [&](const LegacyCamshaftProfile &camshaft,
                                       PortKind expected_kind,
                                       const std::string &path) {
        const auto &shape = camshaft.shape;
        require(report,
                finite_positive(shape.maximum_lift_m.value) &&
                    finite_positive(shape.duration_at_reference_lift_rad.value) &&
                    finite_positive(shape.exponent.value) &&
                    shape.construction_steps.value >= 5 &&
                    finite(shape.advance_rad.value) &&
                    finite_positive(shape.base_radius_m.value),
                ContractIssueCode::invalid_value, path + ".shape",
                "cam shape is outside its domain");
        require(report,
                camshaft.lobes.size() == engine.cylinders.size() &&
                    unique_valid_projected(
                        camshaft.lobes,
                        [](const LegacyCamLobe &lobe) { return lobe.cylinder_id; }),
                ContractIssueCode::inconsistent_shape, path + ".lobes",
                "camshaft must have one lobe per cylinder");
        for (const auto &lobe : camshaft.lobes) {
            const auto port =
                std::ranges::find(engine.ports, lobe.port_id, &PortSpec::id);
            const auto *mechanism_cylinder =
                find_by_id(profile.mechanism.cylinders, lobe.cylinder_id,
                           [](const LegacyCylinderAssembly &cylinder) {
                               return cylinder.topology.cylinder_id;
                           });
            const auto expected_port_id =
                mechanism_cylinder == nullptr ? PortId{}
                : expected_kind == PortKind::intake
                    ? mechanism_cylinder->topology.intake_port_id
                    : mechanism_cylinder->topology.exhaust_port_id;
            require(report,
                    port != engine.ports.end() &&
                        port->cylinder_id == lobe.cylinder_id &&
                        port->kind.value == expected_kind &&
                        lobe.port_id == expected_port_id &&
                        finite(lobe.crank_center_rad.value),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".lobes." + cylinder_name(engine, lobe.cylinder_id),
                    "cam lobe must bind the matching cylinder port");
        }
    };
    validate_camshaft(profile.valvetrain.intake, PortKind::intake, "valvetrain.intake");
    validate_camshaft(profile.valvetrain.exhaust, PortKind::exhaust,
                      "valvetrain.exhaust");

    require(report,
            profile.ignition.firing_order.value.size() == engine.cylinders.size() &&
                unique_valid(profile.ignition.firing_order.value),
            ContractIssueCode::inconsistent_shape, "ignition.firing_order",
            "firing order must contain every cylinder exactly once");
    for (const auto cylinder : profile.ignition.firing_order.value) {
        require(report, contains_id(engine.cylinders, cylinder, &CylinderSpec::id),
                ContractIssueCode::dangling_reference, "ignition.firing_order",
                "firing order references an unknown cylinder");
    }
    require(
        report,
        finite_positive(profile.ignition.timing_curve_triangle_radius_rad_s.value) &&
            profile.ignition.timing_curve.size() >= 2 &&
            finite_positive(profile.ignition.limiter_speed_rpm.value) &&
            finite_positive(profile.ignition.limiter_hold_s.value) &&
            finite_positive(profile.ignition.declared_redline_rpm.value),
        ContractIssueCode::invalid_value, "ignition",
        "ignition curve, limiter, or redline is invalid");
    validate_sample_ids(report, profile.ignition.timing_curve, "ignition.timing_curve");
    for (std::size_t index = 0; index < profile.ignition.timing_curve.size(); ++index) {
        const auto &point = profile.ignition.timing_curve[index];
        require(report,
                finite_nonnegative(point.angular_speed_rad_s.value) &&
                    finite(point.timing_advance_rad.value),
                ContractIssueCode::invalid_value,
                "ignition.timing_curve." + point.sample_id.value,
                "ignition timing sample is invalid");
        if (index != 0) {
            require(
                report,
                point.angular_speed_rad_s.value >
                    profile.ignition.timing_curve[index - 1].angular_speed_rad_s.value,
                ContractIssueCode::inconsistent_semantics,
                "ignition.timing_curve." + point.sample_id.value,
                "ignition speed samples must be strictly increasing");
        }
    }

    const auto &fuel = profile.fuel;
    require(report, is_valid_semantic_id(fuel.fuel_id.value),
            ContractIssueCode::invalid_value, "fuel.fuel_id.value",
            "fuel ID must be canonical");
    require(report,
            finite_positive(fuel.molecular_mass_kg_per_mol.value) &&
                finite_positive(fuel.energy_density_j_per_kg.value) &&
                finite_positive(fuel.molecular_afr.value) &&
                detail::unit_interval(fuel.maximum_burning_efficiency_01.value) &&
                detail::unit_interval(fuel.burning_efficiency_randomness_01.value) &&
                detail::unit_interval(fuel.low_efficiency_attenuation_01.value) &&
                finite_nonnegative(fuel.maximum_turbulence_effect.value) &&
                finite_nonnegative(fuel.maximum_dilution_effect.value) &&
                finite_nonnegative(fuel.lbv_multiplier.value) &&
                finite_positive(
                    fuel.turbulence_to_flame_speed_ratio_triangle_radius.value) &&
                !fuel.turbulence_to_flame_speed_ratio.empty(),
            ContractIssueCode::invalid_value, "fuel",
            "legacy fuel parameters are outside their domain");
    require(report, !fuel.compression_ignition_enabled.value,
            ContractIssueCode::inconsistent_semantics,
            "fuel.compression_ignition_enabled.value",
            "the admitted spark-ignition profile cannot enable compression "
            "ignition");
    validate_sample_ids(report, fuel.turbulence_to_flame_speed_ratio,
                        "fuel.turbulence_to_flame_speed_ratio");
    for (std::size_t index = 0; index < fuel.turbulence_to_flame_speed_ratio.size();
         ++index) {
        const auto &point = fuel.turbulence_to_flame_speed_ratio[index];
        require(report,
                finite_nonnegative(point.turbulence.value) &&
                    finite_nonnegative(point.flame_speed_ratio.value),
                ContractIssueCode::invalid_value,
                "fuel.turbulence_to_flame_speed_ratio." + point.sample_id.value,
                "flame-speed curve sample is invalid");
        if (index != 0) {
            require(
                report,
                point.turbulence.value >
                    fuel.turbulence_to_flame_speed_ratio[index - 1].turbulence.value,
                ContractIssueCode::inconsistent_semantics,
                "fuel.turbulence_to_flame_speed_ratio." + point.sample_id.value,
                "flame-speed curve inputs must be strictly increasing");
        }
    }

    std::unordered_set<std::uint32_t> engine_cylinder_ids;
    for (const auto &cylinder : engine.cylinders) {
        if (cylinder.id.valid()) {
            engine_cylinder_ids.insert(cylinder.id.value);
        }
    }
    std::unordered_set<std::uint32_t> random_stream_cylinder_ids;
    for (const auto &stream : profile.combustion_random_streams) {
        const auto stream_path = profile_path(
            "combustion_random_streams." + cylinder_name(engine, stream.cylinder_id));
        require(report, stream.cylinder_id.valid(), ContractIssueCode::invalid_value,
                stream_path + ".cylinder_id",
                "combustion random-stream owner must be nonzero");
        require(report,
                contains_id(engine.cylinders, stream.cylinder_id, &CylinderSpec::id),
                ContractIssueCode::dangling_reference, stream_path + ".cylinder_id",
                "combustion random stream references an unknown cylinder");
        if (stream.cylinder_id.valid() &&
            !random_stream_cylinder_ids.insert(stream.cylinder_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       stream_path + ".cylinder_id",
                       "a cylinder may own only one combustion random stream");
        }
        require(
            report,
            stream.pcg32_stream.value <=
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            ContractIssueCode::invalid_value, stream_path + ".pcg32_stream.value",
            "PCG32 stream must fit the 63-bit sequence domain");
    }
    if (fuel.burning_efficiency_randomness_01.value > 0.0) {
        require(report,
                profile.combustion_random_streams.size() == engine.cylinders.size() &&
                    random_stream_cylinder_ids == engine_cylinder_ids,
                ContractIssueCode::inconsistent_shape,
                profile_path("combustion_random_streams"),
                "nonzero burning randomness requires exactly one combustion "
                "random stream per cylinder");
    }

    const auto classified =
        profile.losses.included_terms.value | profile.losses.omitted_terms.value;
    require(report,
            (profile.losses.included_terms.value &
             profile.losses.omitted_terms.value) == 0 &&
                (classified & ~known_torque_term_mask()) == 0 &&
                classified == known_torque_term_mask(),
            ContractIssueCode::inconsistent_semantics, "losses",
            "legacy loss profile must classify every known torque term once");
    require(report,
            profile.losses.included_terms.value ==
                    engine.torque_capability.value.included_terms &&
                profile.losses.omitted_terms.value ==
                    engine.torque_capability.value.omitted_terms,
            ContractIssueCode::inconsistent_semantics, "losses",
            "physics loss classification and engine torque capability must agree");

    const auto &excitation = profile.excitation;
    require(report,
            finite_positive(excitation.reference_atmosphere_pa_abs.value) &&
                finite_positive(excitation.legacy_propagation_speed_m_s.value) &&
                finite_nonnegative(excitation.excitation_scale.value) &&
                finite_nonnegative(excitation.filtered_speed_threshold_rpm.value) &&
                excitation.filtered_speed_exponent.value > 0 &&
                finite(excitation.pressure_gains.gauge_static.value) &&
                finite(excitation.pressure_gains.dynamic_forward.value) &&
                finite(excitation.pressure_gains.dynamic_reverse.value) &&
                finite_positive(excitation.cylinder_count_divisor.value) &&
                finite(excitation.inverse_length_exponent.value) &&
                detail::nearly_equal(excitation.cylinder_count_divisor.value,
                                     static_cast<double>(engine.cylinders.size())),
            ContractIssueCode::invalid_value, "reference_excitation",
            "reference excitation values are outside their domain");
    detail::append_prefixed(report, validate(excitation.delay_rate.value),
                            "reference_excitation.delay_rate.value");

    std::unordered_set<std::uint32_t> accumulation_cylinder_ids;
    for (const auto cylinder_id : excitation.cylinder_accumulation_order.value) {
        if (cylinder_id.valid()) {
            accumulation_cylinder_ids.insert(cylinder_id.value);
        }
    }
    require(report,
            excitation.cylinder_accumulation_order.value.size() ==
                    engine.cylinders.size() &&
                unique_valid(excitation.cylinder_accumulation_order.value) &&
                accumulation_cylinder_ids == engine_cylinder_ids,
            ContractIssueCode::inconsistent_shape,
            "reference_excitation.cylinder_accumulation_order",
            "excitation accumulation order must contain every cylinder once");

    require(report,
            excitation.routes.size() == profile.gas_path.exhaust_routes.size() &&
                unique_valid_projected(
                    excitation.routes,
                    [](const LegacyExcitationRoute &route) { return route.route_id; }),
            ContractIssueCode::inconsistent_shape, "reference_excitation.routes",
            "excitation requires one record per exhaust route");
    std::unordered_set<std::uint32_t> excitation_route_ids;
    for (const auto &route : excitation.routes) {
        const auto route_semantic = route_name(engine, route.route_id);
        const auto path = "reference_excitation.routes." + route_semantic;
        const auto *gas_route = find_gas_route(route.route_id);
        require(report,
                contains_id(engine.routes, route.route_id, &RouteSpec::id) &&
                    finite_positive(route.exhaust_system_length_m.value) &&
                    finite_nonnegative(route.audio_volume_linear.value),
                ContractIssueCode::invalid_value, path, "excitation route is invalid");
        require(
            report,
            gas_route != nullptr &&
                detail::nearly_equal(
                    route.exhaust_system_length_m.value,
                    gas_route->parameters.exhaust_system_length_m.value) &&
                detail::nearly_equal(route.audio_volume_linear.value,
                                     gas_route->parameters.audio_volume_linear.value),
            ContractIssueCode::inconsistent_semantics, path,
            "excitation route length and gain must agree with its gas-path route");
        if (route.route_id.valid()) {
            excitation_route_ids.insert(route.route_id.value);
        }
    }
    require(report, excitation_route_ids == gas_path_route_ids,
            ContractIssueCode::inconsistent_shape, "reference_excitation.routes",
            "excitation route IDs must exactly cover the gas-path exhaust routes");

    require(report,
            excitation.cylinder_paths.size() == engine.cylinders.size() &&
                unique_valid_projected(excitation.cylinder_paths,
                                       [](const LegacyExcitationCylinderPath &path) {
                                           return path.cylinder_id;
                                       }),
            ContractIssueCode::inconsistent_shape,
            "reference_excitation.cylinder_paths",
            "excitation requires one path per cylinder");
    std::unordered_set<std::uint32_t> excitation_path_cylinder_ids;
    for (const auto &path : excitation.cylinder_paths) {
        const auto cylinder_semantic = cylinder_name(engine, path.cylinder_id);
        const auto route_semantic = route_name(engine, path.route_id);
        const auto local_path =
            "reference_excitation.cylinder_paths." + cylinder_semantic;
        const auto *mechanism_cylinder =
            find_by_id(profile.mechanism.cylinders, path.cylinder_id,
                       [](const LegacyCylinderAssembly &cylinder) {
                           return cylinder.topology.cylinder_id;
                       });
        const auto *excitation_route = find_by_id(excitation.routes, path.route_id,
                                                  &LegacyExcitationRoute::route_id);
        require(report,
                contains_id(engine.cylinders, path.cylinder_id, &CylinderSpec::id) &&
                    excitation_route != nullptr &&
                    finite_nonnegative(path.header_primary_length_m.value) &&
                    finite_nonnegative(path.sound_attenuation_linear.value),
                ContractIssueCode::invalid_value, local_path,
                "excitation cylinder path is invalid");
        require(report,
                mechanism_cylinder != nullptr &&
                    path.route_id == mechanism_cylinder->topology.exhaust_route_id &&
                    detail::nearly_equal(
                        path.header_primary_length_m.value,
                        mechanism_cylinder->parameters.header_primary_length_m.value),
                ContractIssueCode::inconsistent_semantics, local_path,
                "excitation path route and header length must agree with its "
                "mechanism cylinder");
        if (path.cylinder_id.valid()) {
            excitation_path_cylinder_ids.insert(path.cylinder_id.value);
        }

        const auto canonical_path = profile_path(local_path);
        const auto canonical_route_path =
            profile_path("reference_excitation.routes." + route_semantic);
        validate_derived_resolution(
            report, path.resolved_delay_samples, provenance,
            canonical_path + ".resolved_delay_samples",
            {
                canonical_path + ".header_primary_length_m",
                canonical_route_path + ".exhaust_system_length_m",
                profile_path("reference_excitation.legacy_propagation_speed_m_s"),
                profile_path("reference_excitation.delay_rate"),
            });

        if (excitation_route != nullptr &&
            finite_nonnegative(path.header_primary_length_m.value) &&
            finite_positive(excitation_route->exhaust_system_length_m.value) &&
            finite_positive(excitation.legacy_propagation_speed_m_s.value) &&
            excitation.delay_rate.value.numerator != 0 &&
            excitation.delay_rate.value.denominator != 0) {
            const auto total_length_m = path.header_primary_length_m.value +
                                        excitation_route->exhaust_system_length_m.value;
            const auto delay_seconds =
                total_length_m / excitation.legacy_propagation_speed_m_s.value;
            const auto delay_rate_hz =
                static_cast<double>(excitation.delay_rate.value.numerator) /
                static_cast<double>(excitation.delay_rate.value.denominator);
            const auto requested_samples = delay_seconds * delay_rate_hz;
            const auto rounded_samples = std::round(requested_samples);
            const auto delay_in_range =
                finite(total_length_m) && total_length_m >= 0.0 &&
                finite(delay_seconds) && delay_seconds >= 0.0 &&
                finite(delay_rate_hz) && delay_rate_hz > 0.0 &&
                finite(requested_samples) && requested_samples >= 0.0 &&
                finite(rounded_samples) && rounded_samples >= 0.0 &&
                rounded_samples <=
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max());
            require(report, delay_in_range, ContractIssueCode::invalid_value,
                    canonical_path + ".resolved_delay_samples.value",
                    "derived propagation delay must fit uint32 sample count");
            if (delay_in_range) {
                require(report,
                        path.resolved_delay_samples.value ==
                            static_cast<std::uint32_t>(rounded_samples),
                        ContractIssueCode::inconsistent_semantics,
                        canonical_path + ".resolved_delay_samples.value",
                        "resolved propagation delay must equal round-ties-away "
                        "of path time at delay rate");
            }
        }
    }
    require(report, excitation_path_cylinder_ids == engine_cylinder_ids,
            ContractIssueCode::inconsistent_shape,
            "reference_excitation.cylinder_paths",
            "excitation cylinder paths must exactly cover the engine cylinders");
}

} // namespace

ValidationReport validate(const AuthoredExecutablePhysicsProfile &profile,
                          const ProvenanceLedger &provenance) {
    ValidationReport report;
    std::visit(
        [&](const AuthoredLegacyLowOrderV1Profile &legacy) {
            const auto validate_topology_field = [&](const auto &value,
                                                     const std::string &path) {
                validate_authored(report, value, provenance, path);
                if constexpr (std::is_same_v<std::decay_t<decltype(value.value)>,
                                             std::string>) {
                    detail::require(report, is_valid_semantic_id(value.value),
                                    ContractIssueCode::invalid_value, path + ".value",
                                    "authored topology reference must be canonical");
                }
            };
            constexpr std::string_view root = kLegacyProfileRoot;
            for (std::size_t index = 0; index < legacy.mechanism.cylinders.size();
                 ++index) {
                const auto &topology = legacy.mechanism.cylinders[index].topology;
                const auto base = std::string(root) + ".mechanism.cylinders[" +
                                  std::to_string(index) + "].topology";
                validate_topology_field(topology.cylinder_id, base + ".cylinder_id");
                validate_topology_field(topology.intake_port_id,
                                        base + ".intake_port_id");
                validate_topology_field(topology.exhaust_port_id,
                                        base + ".exhaust_port_id");
                validate_topology_field(topology.intake_runner_volume_id,
                                        base + ".intake_runner_volume_id");
                validate_topology_field(topology.chamber_volume_id,
                                        base + ".chamber_volume_id");
                validate_topology_field(topology.exhaust_primary_volume_id,
                                        base + ".exhaust_primary_volume_id");
                validate_topology_field(topology.plenum_to_runner_edge_id,
                                        base + ".plenum_to_runner_edge_id");
                validate_topology_field(topology.intake_valve_edge_id,
                                        base + ".intake_valve_edge_id");
                validate_topology_field(topology.exhaust_valve_edge_id,
                                        base + ".exhaust_valve_edge_id");
                validate_topology_field(topology.primary_to_collector_edge_id,
                                        base + ".primary_to_collector_edge_id");
                validate_topology_field(topology.blowby_edge_id,
                                        base + ".blowby_edge_id");
                validate_topology_field(topology.exhaust_route_id,
                                        base + ".exhaust_route_id");
            }
            validate_topology_field(legacy.gas_path.intake_topology.plenum_volume_id,
                                    std::string(root) +
                                        ".gas_path.intake_topology.plenum_volume_id");
            validate_topology_field(
                legacy.gas_path.intake_topology.main_throttle_edge_id,
                std::string(root) + ".gas_path.intake_topology.main_throttle_edge_id");
            validate_topology_field(
                legacy.gas_path.intake_topology.idle_bypass_edge_id,
                std::string(root) + ".gas_path.intake_topology.idle_bypass_edge_id");
            for (std::size_t index = 0; index < legacy.gas_path.exhaust_routes.size();
                 ++index) {
                const auto &topology = legacy.gas_path.exhaust_routes[index].topology;
                const auto base = std::string(root) + ".gas_path.exhaust_routes[" +
                                  std::to_string(index) + "].topology";
                validate_topology_field(topology.route_id, base + ".route_id");
                validate_topology_field(topology.collector_volume_id,
                                        base + ".collector_volume_id");
                validate_topology_field(topology.collector_outlet_edge_id,
                                        base + ".collector_outlet_edge_id");
            }
            const auto validate_lobes = [&](const auto &camshaft,
                                            std::string_view name) {
                for (std::size_t index = 0; index < camshaft.lobes.size(); ++index) {
                    const auto &lobe = camshaft.lobes[index];
                    const auto base = std::string(root) + ".valvetrain." +
                                      std::string(name) + ".lobes[" +
                                      std::to_string(index) + "]";
                    validate_topology_field(lobe.cylinder_id, base + ".cylinder_id");
                    validate_topology_field(lobe.port_id, base + ".port_id");
                }
            };
            validate_lobes(legacy.valvetrain.intake, "intake");
            validate_lobes(legacy.valvetrain.exhaust, "exhaust");
            for (const auto &stream : legacy.combustion_random_streams) {
                const auto base = std::string(root) + ".combustion_random_streams." +
                                  stream.cylinder_id.value;
                validate_topology_field(stream.cylinder_id, base + ".cylinder_id");
            }
            for (std::size_t index = 0; index < legacy.excitation.cylinder_paths.size();
                 ++index) {
                const auto &path = legacy.excitation.cylinder_paths[index];
                const auto base = std::string(root) +
                                  ".reference_excitation.cylinder_paths[" +
                                  std::to_string(index) + "]";
                validate_topology_field(path.cylinder_id, base + ".cylinder_id");
                validate_topology_field(path.route_id, base + ".route_id");
            }
            for (std::size_t index = 0; index < legacy.excitation.routes.size();
                 ++index) {
                validate_topology_field(legacy.excitation.routes[index].route_id,
                                        std::string(root) +
                                            ".reference_excitation.routes[" +
                                            std::to_string(index) + "].route_id");
            }
            const auto cylinder_name = [](const auto &item) {
                if constexpr (requires { item.topology.cylinder_id.value; }) {
                    return item.topology.cylinder_id.value;
                } else {
                    return item.cylinder_id.value;
                }
            };
            const auto route_name = [](const auto &item) {
                if constexpr (requires { item.topology.route_id.value; }) {
                    return item.topology.route_id.value;
                } else {
                    return item.route_id.value;
                }
            };
            visit_profile_fields(
                legacy,
                [&](const auto &value, const std::string &path) {
                    validate_authored(report, value, provenance, path);
                },
                cylinder_name, route_name);
            validate_authored_domains(report, legacy);
        },
        profile);
    return report;
}

ValidationReport validate(const ExecutablePhysicsProfile &profile,
                          const EngineSpec &engine,
                          const ProvenanceLedger &provenance) {
    ValidationReport report;
    std::visit(
        [&](const LegacyLowOrderV1Profile &legacy) {
            const auto require_legacy_method =
                [&](const ResolvedValue<MethodIdentity> &method,
                    std::string_view path) {
                    detail::require(
                        report,
                        method.value.id == "legacy_low_order_v1" &&
                            method.value.version == 1,
                        ContractIssueCode::inconsistent_semantics,
                        std::string(path) + ".value",
                        "LegacyLowOrderV1Profile requires legacy_low_order_v1 "
                        "method identity version 1");
                };
            require_legacy_method(engine.methods.mechanism, "engine.methods.mechanism");
            require_legacy_method(engine.methods.valvetrain,
                                  "engine.methods.valvetrain");
            require_legacy_method(engine.methods.gas_exchange,
                                  "engine.methods.gas_exchange");
            require_legacy_method(engine.methods.ignition, "engine.methods.ignition");
            require_legacy_method(engine.methods.combustion,
                                  "engine.methods.combustion");
            require_legacy_method(engine.methods.heat_transfer,
                                  "engine.methods.heat_transfer");
            require_legacy_method(engine.methods.losses, "engine.methods.losses");
            require_legacy_method(engine.methods.excitation,
                                  "engine.methods.excitation");

            const auto cylinder_namer = [&](const auto &item) {
                if constexpr (requires { item.topology.cylinder_id; }) {
                    return cylinder_name(engine, item.topology.cylinder_id);
                } else {
                    return cylinder_name(engine, item.cylinder_id);
                }
            };
            const auto route_namer = [&](const auto &item) {
                if constexpr (requires { item.topology.route_id; }) {
                    return route_name(engine, item.topology.route_id);
                } else {
                    return route_name(engine, item.route_id);
                }
            };
            visit_profile_fields(
                legacy,
                [&](const auto &value, const std::string &path) {
                    validate_resolved(report, value, provenance, path);
                },
                cylinder_namer, route_namer);
            validate_domains(report, legacy, engine, provenance);
        },
        profile);
    return report;
}

} // namespace engine_sim_offline::contract
