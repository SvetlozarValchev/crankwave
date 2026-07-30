#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/legacy_flow_calibration.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::profiles::detail {
namespace {

using Source = BmwResolutionSource;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kLegacyRpmScale = 0.104719755;

std::string numbered_id(std::string_view prefix, std::uint32_t number) {
    return std::string(prefix) + std::to_string(number);
}

contract::LegacyRestriction
make_restriction(BmwProvenanceBuilder &builder,
                 contract::LegacyRestrictionCalibration calibration,
                 double source_rating, const std::string &base_path) {
    const std::string calibration_path = base_path + ".calibration";
    const std::string source_rating_path = base_path + ".source_rating";
    const std::string resolved_k_path = base_path + ".resolved_k";
    return {
        builder.resolved(calibration, calibration_path, Source::legacy_asset),
        builder.resolved(source_rating, source_rating_path, Source::legacy_asset),
        builder.derived(
            simulation::legacy_flow_bench_restriction_coefficient(calibration,
                                                                  source_rating),
            resolved_k_path,
                        derived_method("legacy-flow-constant-v1"),
                        {
                            calibration_path,
                            source_rating_path,
                        }),
    };
}

contract::LegacyValveFlowPoint
make_valve_flow_point(BmwProvenanceBuilder &builder, std::string_view table_name,
                      std::uint32_t lift_index, double source_cfm,
                      double millimetre_source) {
    const std::string sample_id = numbered_id("lift-", lift_index);
    const std::string base_path =
        builder.profile_path("gas_path.head." + std::string(table_name) + "." +
                             sample_id);
    const std::string source_cfm_path = base_path + ".source_cfm_at_28_inh2o";
    return {
        builder.resolved(sample_id, base_path + ".sample_id", Source::legacy_asset),
        builder.resolved(static_cast<double>(lift_index) * millimetre_source,
                         base_path + ".lift_m", Source::legacy_asset),
        builder.resolved(source_cfm, source_cfm_path, Source::legacy_asset),
        builder.derived(
            simulation::legacy_flow_bench_restriction_coefficient(
                contract::LegacyRestrictionCalibration::cfm_at_28_inh2o,
                source_cfm),
            base_path + ".resolved_k", derived_method("legacy-flow-constant-v1"),
            {
                source_cfm_path,
            }),
    };
}

contract::LegacyCamShape make_cam_shape(BmwProvenanceBuilder &builder,
                                        std::string_view cam_name,
                                        double millimetre_source, double inch_source,
                                        double degree_source) {
    const std::string base_path =
        builder.profile_path("valvetrain." + std::string(cam_name) + ".shape");
    return {
        builder.resolved(9.0 * millimetre_source, base_path + ".maximum_lift_m",
                         Source::legacy_asset),
        builder.resolved(210.0 * degree_source,
                         base_path + ".duration_at_reference_lift_rad",
                         Source::legacy_asset),
        builder.resolved(0.8, base_path + ".exponent", Source::legacy_asset),
        builder.resolved<std::uint32_t>(100, base_path + ".construction_steps",
                                        Source::legacy_asset),
        builder.resolved(0.0, base_path + ".advance_rad", Source::legacy_asset),
        builder.resolved(0.6 * inch_source, base_path + ".base_radius_m",
                         Source::legacy_asset),
    };
}

contract::LegacyTimingPoint make_timing_point(BmwProvenanceBuilder &builder,
                                              std::uint32_t rpm_point,
                                              double advance_rad) {
    const std::string sample_id = numbered_id("rpm-", rpm_point);
    const std::string base_path =
        builder.profile_path("ignition.timing_curve." + sample_id);
    return {
        builder.resolved(sample_id, base_path + ".sample_id", Source::legacy_asset),
        builder.resolved(static_cast<double>(rpm_point) * kLegacyRpmScale,
                         base_path + ".angular_speed_rad_s", Source::legacy_asset),
        builder.resolved(advance_rad, base_path + ".timing_advance_rad",
                         Source::legacy_asset),
    };
}

contract::LegacyFlameSpeedPoint
make_flame_speed_point(BmwProvenanceBuilder &builder, std::uint32_t turbulence) {
    const std::string sample_id = numbered_id("turbulence-", turbulence);
    const std::string base_path =
        builder.profile_path("fuel.turbulence_to_flame_speed_ratio." + sample_id);
    const double turbulence_value = static_cast<double>(turbulence);
    const double ratio = turbulence == 0 ? 3.0 : 1.5 * turbulence_value;
    return {
        builder.resolved(sample_id, base_path + ".sample_id", Source::legacy_asset),
        builder.resolved(turbulence_value, base_path + ".turbulence",
                         Source::legacy_asset),
        builder.resolved(ratio, base_path + ".flame_speed_ratio", Source::legacy_asset),
    };
}

} // namespace

contract::EngineSpec
build_bmw_m52b28_low_order_engine(BmwProvenanceBuilder &builder) {
    const bool operating_profile =
        builder.profile_kind() == BmwProfileKind::low_order_operating_point_v1;
    const auto profile_path = [&builder](std::string_view suffix) {
        return builder.profile_path(suffix);
    };
    const double centimetre_source = 1.0 / 100.0;
    const double millimetre_source = 1.0 / 1000.0;
    const double gram_source = 1.0 / 1000.0;
    const double inch_source = centimetre_source * 2.54;
    const double foot_source = inch_source * 12.0;
    const double lbf_source = 4.44822;
    const double lb_ft_source = lbf_source * foot_source;
    const double cc_source = centimetre_source * centimetre_source * centimetre_source;
    const double litre_source = cc_source * 1000.0;
    const double degree_source = kLegacyPi / 180.0;

    const double bore_m = 84.0 * millimetre_source;
    const double stroke_m = 84.0 * millimetre_source;
    const double crank_radius_m = 84.0 * millimetre_source / 2.0;
    const double connecting_rod_length_m = 135.0 * millimetre_source;
    const double deck_height_m = (210.0 + 1.0) * millimetre_source;
    const double piston_compression_height_m = 31.82 * millimetre_source;
    const double head_chamber_volume_m3 = 34.0 * cc_source;
    const double piston_displacement_term_m3 = 0.0;
    const auto cylinder_geometry = simulation::derive_legacy_cylinder_geometry(
        bore_m, crank_radius_m, connecting_rod_length_m, deck_height_m,
        piston_compression_height_m, head_chamber_volume_m3,
        piston_displacement_term_m3);
    const double compression_ratio = cylinder_geometry.compression_ratio;

    const double rot120 = 120.0 * degree_source;
    const double rot360 = 360.0 * degree_source;
    const double intake_base = rot360 + 110.0 * degree_source;
    const double exhaust_base = rot360 - 105.0 * degree_source;
    constexpr std::array<std::uint32_t, 6> center_multipliers{
        0, 4, 2, 5, 1, 3,
    };
    const std::array<double, 6> journal_angles{
        0.0,
        120.0 * degree_source,
        240.0 * degree_source,
        240.0 * degree_source,
        120.0 * degree_source,
        0.0 * degree_source,
    };
    std::array<double, 6> ignition_wire_angles{};
    std::array<double, 6> intake_centers{};
    std::array<double, 6> exhaust_centers{};
    for (std::size_t index = 0; index < center_multipliers.size(); ++index) {
        const double rank = static_cast<double>(center_multipliers[index]);
        ignition_wire_angles[index] = (rank / 6.0) * ((2.0 * 360.0) * degree_source);
        intake_centers[index] = intake_base + rank * rot120;
        exhaust_centers[index] = exhaust_base + rank * rot120;
    }

    contract::EngineSpec engine;
    engine.schema_version = 1;
    engine.id = contract::EngineId{1};
    engine.engine_id = builder.resolved(std::string{"bmw-m52b28"}, "engine.engine_id",
                                        Source::legacy_asset);
    engine.profile_id =
        builder.resolved(std::string{builder.engine_profile_id()},
                         "engine.profile_id", Source::profile_contract);
    engine.display_name = builder.resolved(std::string{"BMW M52B28"},
                                           "engine.display_name", Source::legacy_asset);
    engine.cycle = builder.resolved(contract::EngineCycle::four_stroke, "engine.cycle",
                                    Source::legacy_asset);
    engine.ignition = builder.resolved(contract::IgnitionKind::spark_ignition,
                                       "engine.ignition", Source::legacy_asset);
    engine.cylinder_layout =
        builder.resolved(contract::CylinderLayoutKind::inline_engine,
                         "engine.cylinder_layout", Source::legacy_asset);

    engine.banks.push_back({
        contract::BankId{1},
        builder.resolved(std::string{"bank.inline-1"},
                         "engine.banks.bank.inline-1.semantic_id", Source::profile_contract),
    });

    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const std::string semantic_id = numbered_id("cylinder-", number);
        const std::string engine_path = "engine.cylinders." + semantic_id;
        const std::string profile_cylinder_path =
            profile_path("mechanism.cylinders." + semantic_id);
        engine.cylinders.push_back({
            contract::CylinderId{number},
            builder.resolved(semantic_id, engine_path + ".semantic_id",
                             Source::profile_contract),
            contract::BankId{1},
            builder.resolved(bore_m, engine_path + ".bore_m", Source::legacy_asset),
            builder.resolved(stroke_m, engine_path + ".stroke_m", Source::legacy_asset),
            builder.resolved(connecting_rod_length_m,
                             engine_path + ".connecting_rod_length_m",
                             Source::legacy_asset),
            builder.derived(compression_ratio, engine_path + ".compression_ratio",
                            derived_method("legacy-slider-crank-compression-ratio-v1"),
                            {
                                profile_cylinder_path + ".bore_m",
                                profile_cylinder_path + ".crank_radius_m",
                                profile_cylinder_path + ".connecting_rod_length_m",
                                profile_cylinder_path + ".deck_height_m",
                                profile_cylinder_path + ".piston_compression_height_m",
                                profile_cylinder_path + ".head_chamber_volume_m3",
                                profile_cylinder_path + ".piston_displacement_term_m3",
                            }),
            builder.resolved(ignition_wire_angles[index],
                             engine_path + ".firing_tdc_offset_rad",
                             Source::legacy_asset),
            builder.resolved(journal_angles[index], engine_path + ".journal_phase_rad",
                             Source::legacy_asset),
        });
    }

    double total_displacement_m3 = 0.0;
    for (const auto &cylinder : engine.cylinders) {
        total_displacement_m3 += std::numbers::pi * cylinder.bore_m.value *
                                 cylinder.bore_m.value * cylinder.stroke_m.value / 4.0;
    }
    engine.total_displacement_m3 =
        builder.derived(total_displacement_m3, "engine.total_displacement_m3",
                        derived_method("geometric-cylinder-displacement-std-pi-v1"),
                        {
                            "engine.cylinders.cylinder-1.bore_m",
                            "engine.cylinders.cylinder-1.stroke_m",
                            "engine.cylinders.cylinder-2.bore_m",
                            "engine.cylinders.cylinder-2.stroke_m",
                            "engine.cylinders.cylinder-3.bore_m",
                            "engine.cylinders.cylinder-3.stroke_m",
                            "engine.cylinders.cylinder-4.bore_m",
                            "engine.cylinders.cylinder-4.stroke_m",
                            "engine.cylinders.cylinder-5.bore_m",
                            "engine.cylinders.cylinder-5.stroke_m",
                            "engine.cylinders.cylinder-6.bore_m",
                            "engine.cylinders.cylinder-6.stroke_m",
                        });

    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const contract::CylinderId cylinder_id{number};
        const std::string intake_id = numbered_id("intake-port-", number);
        const std::string exhaust_id = numbered_id("exhaust-port-", number);
        engine.ports.push_back({
            contract::PortId{2 * number - 1},
            builder.resolved(intake_id, "engine.ports." + intake_id + ".semantic_id",
                             Source::profile_contract),
            cylinder_id,
            builder.resolved(contract::PortKind::intake,
                             "engine.ports." + intake_id + ".kind", Source::profile_contract),
        });
        engine.ports.push_back({
            contract::PortId{2 * number},
            builder.resolved(exhaust_id, "engine.ports." + exhaust_id + ".semantic_id",
                             Source::profile_contract),
            cylinder_id,
            builder.resolved(contract::PortKind::exhaust,
                             "engine.ports." + exhaust_id + ".kind", Source::profile_contract),
        });
    }

    const auto add_volume = [&](std::uint32_t id, std::string semantic_id,
                                contract::GasVolumeKind kind) {
        const std::string base_path = "engine.gas_volumes." + semantic_id;
        engine.gas_volumes.push_back({
            contract::GasVolumeId{id},
            builder.resolved(semantic_id, base_path + ".semantic_id", Source::profile_contract),
            builder.resolved(kind, base_path + ".kind", Source::profile_contract),
        });
    };
    add_volume(1, "atmosphere", contract::GasVolumeKind::atmosphere);
    add_volume(2, "intake.plenum", contract::GasVolumeKind::intake_plenum);
    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        add_volume(3 + 3 * index, numbered_id("intake.runner.", number),
                   contract::GasVolumeKind::intake_runner);
        add_volume(4 + 3 * index, numbered_id("cylinder.", number),
                   contract::GasVolumeKind::cylinder);
        add_volume(5 + 3 * index, numbered_id("exhaust.primary.", number),
                   contract::GasVolumeKind::exhaust_primary);
    }
    add_volume(21, "exhaust.collector.0", contract::GasVolumeKind::exhaust_collector);
    add_volume(22, "exhaust.collector.1", contract::GasVolumeKind::exhaust_collector);

    const auto add_edge = [&](std::uint32_t id, std::string semantic_id,
                              std::uint32_t endpoint_0, std::uint32_t endpoint_1) {
        engine.flow_edges.push_back({
            contract::FlowEdgeId{id},
            builder.resolved(semantic_id,
                             "engine.flow_edges." + semantic_id + ".semantic_id",
                             Source::profile_contract),
            contract::GasVolumeId{endpoint_0},
            contract::GasVolumeId{endpoint_1},
        });
    };
    add_edge(1, "flow.main-throttle", 1, 2);
    add_edge(2, "flow.idle-bypass", 1, 2);
    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const std::uint32_t edge_base = 3 + 5 * index;
        const std::uint32_t runner_volume = 3 + 3 * index;
        const std::uint32_t cylinder_volume = 4 + 3 * index;
        const std::uint32_t primary_volume = 5 + 3 * index;
        const std::uint32_t collector_volume = number % 2 == 0 ? 21 : 22;
        add_edge(edge_base, numbered_id("flow.plenum-to-runner.", number), 2,
                 runner_volume);
        add_edge(edge_base + 1, numbered_id("flow.intake-valve.", number),
                 runner_volume, cylinder_volume);
        add_edge(edge_base + 2, numbered_id("flow.exhaust-valve.", number),
                 cylinder_volume, primary_volume);
        add_edge(edge_base + 3, numbered_id("flow.primary-to-collector.", number),
                 primary_volume, collector_volume);
        add_edge(edge_base + 4, numbered_id("flow.blowby.", number), cylinder_volume,
                 1);
    }
    add_edge(33, "flow.collector-outlet.0", 1, 21);
    add_edge(34, "flow.collector-outlet.1", 1, 22);

    engine.routes = {
        {
            contract::RouteId{1},
            builder.resolved(std::string{"exhaust.reference.0"},
                             "engine.routes.exhaust.reference.0."
                             "semantic_id",
                             Source::profile_contract),
            builder.resolved(contract::SourceRouteKind::exhaust_outlet,
                             "engine.routes.exhaust.reference.0.kind",
                             Source::profile_contract),
            contract::GasVolumeId{21},
            std::nullopt,
            std::nullopt,
        },
        {
            contract::RouteId{2},
            builder.resolved(std::string{"exhaust.reference.1"},
                             "engine.routes.exhaust.reference.1."
                             "semantic_id",
                             Source::profile_contract),
            builder.resolved(contract::SourceRouteKind::exhaust_outlet,
                             "engine.routes.exhaust.reference.1.kind",
                             Source::profile_contract),
            contract::GasVolumeId{22},
            std::nullopt,
            std::nullopt,
        },
    };

    const auto core_method_source =
        operating_profile ? Source::implemented_method
                          : Source::profile_contract;
    engine.methods = {
        builder.resolved(legacy_low_order_method(), "engine.methods.mechanism",
                         core_method_source),
        builder.resolved(legacy_low_order_method(), "engine.methods.valvetrain",
                         core_method_source),
        builder.resolved(legacy_low_order_method(), "engine.methods.gas_exchange",
                         core_method_source),
        builder.resolved(legacy_low_order_method(), "engine.methods.ignition",
                         core_method_source),
        builder.resolved(legacy_low_order_method(), "engine.methods.combustion",
                         core_method_source),
        builder.resolved(legacy_low_order_method(), "engine.methods.heat_transfer",
                         core_method_source),
        builder.resolved(
            operating_profile
                ? simulation::chen_flynn_cycle_mean_aggregate_loss_method_identity()
                : legacy_low_order_method(),
            "engine.methods.losses",
            operating_profile ? Source::implemented_method
                              : Source::profile_contract),
        builder.resolved(legacy_low_order_method(), "engine.methods.excitation",
                         core_method_source),
    };

    contract::LowOrderEngineCoreV1 core;
    contract::LegacyFixedCrankLossV1 fixed_crank_loss;
    core.mechanism.crank = {
        builder.resolved(120.0 * degree_source,
                         profile_path("mechanism.crank.crank_tdc_reference_rad"),
                         Source::legacy_asset),
        builder.resolved(5.0, profile_path("mechanism.crank.crankshaft_mass_kg"),
                         Source::legacy_asset),
        builder.resolved(5.9, profile_path("mechanism.crank.flywheel_mass_kg"),
                         Source::legacy_asset),
        builder.resolved(0.22986844776863666 * 0.9,
                         profile_path("mechanism.crank.authored_crank_inertia_kg_m2"),
                         Source::legacy_asset),
    };
    if (!operating_profile) {
        fixed_crank_loss.fixed_crank_friction_magnitude_nm = builder.resolved(
            10.0 * lb_ft_source,
            profile_path("mechanism.crank.fixed_crank_friction_magnitude_nm"),
            Source::legacy_asset);
    }

    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const std::string semantic_id = numbered_id("cylinder-", number);
        const std::string cylinder_path =
            profile_path("mechanism.cylinders." + semantic_id);
        const std::uint32_t topology_base = 3 + 5 * index;
        const contract::RouteId exhaust_route_id{
            number % 2 == 0 ? 1U : 2U,
        };
        core.mechanism.cylinders.push_back({
            {
                contract::CylinderId{number},
                contract::PortId{2 * number - 1},
                contract::PortId{2 * number},
                contract::GasVolumeId{3 + 3 * index},
                contract::GasVolumeId{4 + 3 * index},
                contract::GasVolumeId{5 + 3 * index},
                contract::FlowEdgeId{topology_base},
                contract::FlowEdgeId{topology_base + 1},
                contract::FlowEdgeId{topology_base + 2},
                contract::FlowEdgeId{topology_base + 3},
                contract::FlowEdgeId{topology_base + 4},
                exhaust_route_id,
            },
            {
                builder.resolved(bore_m, cylinder_path + ".bore_m",
                                 Source::legacy_asset),
                builder.resolved(stroke_m, cylinder_path + ".stroke_m",
                                 Source::legacy_asset),
                builder.resolved(crank_radius_m, cylinder_path + ".crank_radius_m",
                                 Source::legacy_asset),
                builder.resolved(connecting_rod_length_m,
                                 cylinder_path + ".connecting_rod_length_m",
                                 Source::legacy_asset),
                builder.resolved(deck_height_m, cylinder_path + ".deck_height_m",
                                 Source::legacy_asset),
                builder.resolved(piston_compression_height_m,
                                 cylinder_path + ".piston_compression_height_m",
                                 Source::legacy_asset),
                builder.resolved(head_chamber_volume_m3,
                                 cylinder_path + ".head_chamber_volume_m3",
                                 Source::legacy_asset),
                builder.resolved(piston_displacement_term_m3,
                                 cylinder_path + ".piston_displacement_term_m3",
                                 Source::legacy_asset),
                builder.resolved(280.0 * gram_source, cylinder_path + ".piston_mass_kg",
                                 Source::legacy_asset),
                builder.resolved(300.0 * gram_source,
                                 cylinder_path + ".connecting_rod_mass_kg",
                                 Source::legacy_asset),
                builder.resolved(0.0015884918028487504,
                                 cylinder_path + ".connecting_rod_inertia_kg_m2",
                                 Source::legacy_asset),
                builder.resolved(journal_angles[index],
                                 cylinder_path + ".journal_angle_rad",
                                 Source::legacy_asset),
                builder.resolved(ignition_wire_angles[index],
                                 cylinder_path + ".ignition_wire_angle_rad",
                                 Source::legacy_asset),
                builder.resolved(0.0, cylinder_path + ".header_primary_length_m",
                                 Source::reference_fixture),
            },
        });
    }

    core.gas_path.intake_topology = {
        contract::GasVolumeId{2},
        contract::FlowEdgeId{1},
        contract::FlowEdgeId{2},
    };
    core.gas_path.intake = {
        builder.resolved(2.0 * litre_source,
                         profile_path("gas_path.intake.plenum_volume_m3"),
                         Source::legacy_asset),
        builder.resolved(100.0 * (centimetre_source * centimetre_source),
                         profile_path("gas_path.intake.plenum_cross_section_area_m2"),
                         Source::legacy_asset),
        builder.resolved(6.0 * inch_source,
                         profile_path("gas_path.intake.runner_length_m"),
                         Source::legacy_asset),
        builder.resolved(0.1, profile_path("gas_path.intake.velocity_decay"),
                         Source::legacy_asset),
        builder.resolved(2.0, profile_path("gas_path.intake.throttle_gamma"),
                         Source::legacy_asset),
        builder.resolved(
            0.994, profile_path("gas_path.intake.idle_throttle_plate_position_01"),
            Source::legacy_asset),
        make_restriction(builder,
                         contract::LegacyRestrictionCalibration::carb_at_1p5_inhg,
                         500.0, profile_path("gas_path.intake.main_throttle")),
        make_restriction(builder,
                         contract::LegacyRestrictionCalibration::carb_at_1p5_inhg, 0.1,
                         profile_path("gas_path.intake.idle_bypass")),
        make_restriction(builder,
                         contract::LegacyRestrictionCalibration::carb_at_1p5_inhg,
                         500.0, profile_path("gas_path.intake.plenum_to_runner")),
    };

    const double intake_runner_area_m2 =
        2.0 * 12.4087 * (centimetre_source * centimetre_source);
    const double exhaust_primary_radius_m = 0.85 * inch_source;
    const double exhaust_primary_area_m2 =
        kLegacyPi * exhaust_primary_radius_m * exhaust_primary_radius_m;
    core.gas_path.head.intake_runner_base_volume_m3 = builder.resolved(
        100.0 * cc_source, profile_path("gas_path.head.intake_runner_base_volume_m3"),
        Source::legacy_asset);
    core.gas_path.head.intake_runner_cross_section_area_m2 = builder.resolved(
        intake_runner_area_m2,
        profile_path("gas_path.head.intake_runner_cross_section_area_m2"),
        Source::legacy_asset);
    core.gas_path.head.exhaust_runner_base_volume_m3 = builder.resolved(
        300.0 * cc_source, profile_path("gas_path.head.exhaust_runner_base_volume_m3"),
        Source::legacy_asset);
    core.gas_path.head.exhaust_runner_cross_section_area_m2 = builder.resolved(
        exhaust_primary_area_m2,
        profile_path("gas_path.head.exhaust_runner_cross_section_area_m2"),
        Source::legacy_asset);
    core.gas_path.head.flow_table_triangle_radius_m =
        builder.resolved(1.0 * millimetre_source,
                         profile_path("gas_path.head.flow_table_triangle_radius_m"),
                         Source::legacy_asset);

    constexpr std::array<double, 13> intake_flow_cfm{
        0.0,   35.0,  60.0,  90.0,  125.0, 150.0, 175.0,
        200.0, 215.0, 230.0, 235.0, 235.0, 238.0,
    };
    constexpr std::array<double, 13> exhaust_flow_cfm{
        0.0,   35.0,  55.0,  85.0,  105.0, 120.0, 140.0,
        150.0, 155.0, 160.0, 165.0, 165.0, 165.0,
    };
    for (std::uint32_t index = 0; index < intake_flow_cfm.size(); ++index) {
        core.gas_path.head.intake_flow.push_back(make_valve_flow_point(
            builder, "intake_flow", index, intake_flow_cfm[index], millimetre_source));
        core.gas_path.head.exhaust_flow.push_back(
            make_valve_flow_point(builder, "exhaust_flow", index,
                                  exhaust_flow_cfm[index], millimetre_source));
    }

    const double collector_radius_m = 2.0 * inch_source;
    const double collector_cross_section_area_m2 =
        kLegacyPi * collector_radius_m * collector_radius_m;
    const double collector_volume_m3 = 50.0 * litre_source;
    const double exhaust_system_length_m =
        collector_volume_m3 / collector_cross_section_area_m2;
    const auto add_exhaust_route = [&](contract::RouteId route_id,
                                       contract::GasVolumeId collector_volume_id,
                                       contract::FlowEdgeId outlet_edge_id,
                                       std::string route_semantic_id,
                                       double audio_volume) {
        const std::string route_path =
            profile_path("gas_path.exhaust_routes." + route_semantic_id);
        const std::string collector_volume_path = route_path + ".collector_volume_m3";
        const std::string collector_area_path =
            route_path + ".collector_cross_section_area_m2";
        core.gas_path.exhaust_routes.push_back({
            {
                route_id,
                collector_volume_id,
                outlet_edge_id,
            },
            {
                builder.resolved(collector_volume_m3, collector_volume_path,
                                 Source::legacy_asset),
                builder.resolved(collector_cross_section_area_m2, collector_area_path,
                                 Source::legacy_asset),
                builder.derived(exhaust_system_length_m,
                                route_path + ".exhaust_system_length_m",
                                derived_method("collector-volume-over-area-v1"),
                                {
                                    collector_volume_path,
                                    collector_area_path,
                                }),
                builder.resolved(20.0 * inch_source,
                                 route_path + ".primary_tube_length_m",
                                 Source::legacy_asset),
                builder.resolved(1.0, route_path + ".velocity_decay",
                                 Source::legacy_asset),
                builder.resolved(audio_volume, route_path + ".audio_volume_linear",
                                 Source::legacy_asset),
                make_restriction(
                    builder, contract::LegacyRestrictionCalibration::carb_at_1p5_inhg,
                    200.0, route_path + ".primary_to_collector"),
                make_restriction(
                    builder, contract::LegacyRestrictionCalibration::carb_at_1p5_inhg,
                    1000.0, route_path + ".collector_outlet"),
            },
        });
    };
    add_exhaust_route(contract::RouteId{1}, contract::GasVolumeId{21},
                      contract::FlowEdgeId{33}, "exhaust.reference.0", 0.5);
    add_exhaust_route(contract::RouteId{2}, contract::GasVolumeId{22},
                      contract::FlowEdgeId{34}, "exhaust.reference.1", 1.0);
    core.gas_path.piston_blowby = make_restriction(
        builder, contract::LegacyRestrictionCalibration::cfm_at_28_inh2o, 0.1,
        profile_path("gas_path.piston_blowby"));

    core.valvetrain.intake.shape = make_cam_shape(
        builder, "intake", millimetre_source, inch_source, degree_source);
    core.valvetrain.exhaust.shape = make_cam_shape(
        builder, "exhaust", millimetre_source, inch_source, degree_source);
    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const std::string cylinder_id = numbered_id("cylinder-", number);
        core.valvetrain.intake.lobes.push_back({
            contract::CylinderId{number},
            contract::PortId{2 * number - 1},
            builder.resolved(intake_centers[index],
                             profile_path("valvetrain.intake.lobes." + cylinder_id +
                                          ".crank_center_rad"),
                             Source::legacy_asset),
        });
        core.valvetrain.exhaust.lobes.push_back({
            contract::CylinderId{number},
            contract::PortId{2 * number},
            builder.resolved(exhaust_centers[index],
                             profile_path("valvetrain.exhaust.lobes." + cylinder_id +
                                          ".crank_center_rad"),
                             Source::legacy_asset),
        });
    }

    core.ignition.firing_order = builder.resolved(
        std::vector<contract::CylinderId>{
            contract::CylinderId{1},
            contract::CylinderId{5},
            contract::CylinderId{3},
            contract::CylinderId{6},
            contract::CylinderId{2},
            contract::CylinderId{4},
        },
        profile_path("ignition.firing_order"), Source::legacy_asset);
    core.ignition.timing_curve_triangle_radius_rad_s =
        builder.resolved(1000.0 * kLegacyRpmScale,
                         profile_path("ignition.timing_curve_triangle_radius_rad_s"),
                         Source::legacy_asset);
    for (std::uint32_t rpm = 0; rpm <= 7000; rpm += 1000) {
        const double advance = (rpm < 2000 ? 10.0 : 30.0) * degree_source;
        core.ignition.timing_curve.push_back(
            make_timing_point(builder, rpm, advance));
    }
    core.ignition.limiter_speed_rpm = builder.resolved(
        8000.0, profile_path("ignition.limiter_speed_rpm"), Source::legacy_asset);
    core.ignition.limiter_hold_s = builder.resolved(
        0.5, profile_path("ignition.limiter_hold_s"), Source::legacy_asset);
    core.ignition.declared_redline_rpm = builder.resolved(
        7000.0, profile_path("ignition.declared_redline_rpm"), Source::legacy_asset);

    core.fuel.fuel_id =
        builder.resolved(std::string{"gasoline-legacy-engine-sim-v1"},
                         profile_path("fuel.fuel_id"), Source::legacy_asset);
    core.fuel.molecular_mass_kg_per_mol = builder.resolved(
        100.0 * gram_source, profile_path("fuel.molecular_mass_kg_per_mol"),
        Source::legacy_asset);
    core.fuel.energy_density_j_per_kg = builder.resolved(
        (48.1 * 1000.0) / gram_source, profile_path("fuel.energy_density_j_per_kg"),
        Source::legacy_asset);
    core.fuel.molecular_afr = builder.resolved(
        25.0 / 2.0, profile_path("fuel.molecular_afr"), Source::legacy_asset);
    core.fuel.maximum_burning_efficiency_01 = builder.resolved(
        0.8, profile_path("fuel.maximum_burning_efficiency_01"), Source::legacy_asset);
    core.fuel.burning_efficiency_randomness_01 =
        builder.resolved(0.5, profile_path("fuel.burning_efficiency_randomness_01"),
                         Source::legacy_asset);
    core.fuel.low_efficiency_attenuation_01 = builder.resolved(
        0.6, profile_path("fuel.low_efficiency_attenuation_01"), Source::legacy_asset);
    core.fuel.maximum_turbulence_effect = builder.resolved(
        4.0, profile_path("fuel.maximum_turbulence_effect"), Source::legacy_asset);
    core.fuel.maximum_dilution_effect = builder.resolved(
        10.0, profile_path("fuel.maximum_dilution_effect"), Source::legacy_asset);
    core.fuel.lbv_multiplier = builder.resolved(
        1.0, profile_path("fuel.lbv_multiplier"), Source::legacy_asset);
    core.fuel.turbulence_to_flame_speed_ratio_triangle_radius = builder.resolved(
        5.0,
        profile_path("fuel."
                     "turbulence_to_flame_speed_ratio_triangle_radius"),
        Source::legacy_asset);
    for (std::uint32_t turbulence = 0; turbulence <= 45; turbulence += 5) {
        core.fuel.turbulence_to_flame_speed_ratio.push_back(
            make_flame_speed_point(builder, turbulence));
    }

    const contract::TorqueTermMask included_terms =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const contract::TorqueTermMask omitted_terms =
        contract::known_torque_term_mask() & ~included_terms;
    if (!operating_profile) {
        fixed_crank_loss.included_terms = builder.resolved(
            included_terms, profile_path("losses.included_terms"),
            Source::profile_contract);
        fixed_crank_loss.omitted_terms = builder.resolved(
            omitted_terms, profile_path("losses.omitted_terms"),
            Source::profile_contract);
    }

    core.excitation.reference_atmosphere_pa_abs = builder.resolved(
        101325.0, profile_path("reference_excitation.reference_atmosphere_pa_abs"),
        Source::reference_fixture);
    core.excitation.legacy_propagation_speed_m_s = builder.resolved(
        343.0, profile_path("reference_excitation.legacy_propagation_speed_m_s"),
        Source::reference_fixture);
    core.excitation.excitation_scale =
        builder.resolved(1600.0, profile_path("reference_excitation.excitation_scale"),
                         Source::reference_fixture);
    core.excitation.filtered_speed_threshold_rpm = builder.resolved(
        40.0, profile_path("reference_excitation.filtered_speed_threshold_rpm"),
        Source::reference_fixture);
    core.excitation.filtered_speed_exponent = builder.resolved<std::uint32_t>(
        3, profile_path("reference_excitation.filtered_speed_exponent"),
        Source::reference_fixture);
    core.excitation.pressure_gains = {
        builder.resolved(
            1.0, profile_path("reference_excitation.pressure_gains.gauge_static"),
            Source::reference_fixture),
        builder.resolved(
            0.1, profile_path("reference_excitation.pressure_gains.dynamic_forward"),
            Source::reference_fixture),
        builder.resolved(
            0.1, profile_path("reference_excitation.pressure_gains.dynamic_reverse"),
            Source::reference_fixture),
    };
    core.excitation.cylinder_count_divisor = builder.resolved(
        6.0, profile_path("reference_excitation.cylinder_count_divisor"),
        Source::reference_fixture);
    core.excitation.inverse_length_exponent = builder.resolved(
        2.0, profile_path("reference_excitation.inverse_length_exponent"),
        Source::reference_fixture);
    core.excitation.delay_rate = builder.resolved(
        contract::RationalRateHz{10000, 1},
        profile_path("reference_excitation.delay_rate"), Source::reference_fixture);
    core.excitation.cylinder_accumulation_order = builder.resolved(
        std::vector<contract::CylinderId>{
            contract::CylinderId{1},
            contract::CylinderId{2},
            contract::CylinderId{3},
            contract::CylinderId{4},
            contract::CylinderId{5},
            contract::CylinderId{6},
        },
        profile_path("reference_excitation.cylinder_accumulation_order"),
        Source::reference_fixture);

    core.excitation.routes = {
        {
            contract::RouteId{1},
            builder.resolved(
                exhaust_system_length_m,
                profile_path("reference_excitation.routes."
                             "exhaust.reference.0.exhaust_system_length_m"),
                Source::reference_fixture),
            builder.resolved(0.5,
                             profile_path("reference_excitation.routes."
                                          "exhaust.reference.0.audio_volume_linear"),
                             Source::reference_fixture),
        },
        {
            contract::RouteId{2},
            builder.resolved(
                exhaust_system_length_m,
                profile_path("reference_excitation.routes."
                             "exhaust.reference.1.exhaust_system_length_m"),
                Source::reference_fixture),
            builder.resolved(1.0,
                             profile_path("reference_excitation.routes."
                                          "exhaust.reference.1.audio_volume_linear"),
                             Source::reference_fixture),
        },
    };

    const double propagation_speed_m_s = 343.0;
    const contract::RationalRateHz delay_rate{10000, 1};
    for (std::uint32_t index = 0; index < 6; ++index) {
        const std::uint32_t number = index + 1;
        const contract::RouteId route_id{
            number % 2 == 0 ? 1U : 2U,
        };
        const std::string cylinder_id = numbered_id("cylinder-", number);
        const std::string route_id_text =
            number % 2 == 0 ? "exhaust.reference.0" : "exhaust.reference.1";
        const std::string cylinder_path =
            profile_path("reference_excitation.cylinder_paths." + cylinder_id);
        const std::string excitation_route_path =
            profile_path("reference_excitation.routes." + route_id_text);
        const double header_primary_length_m = 0.0;
        const double delay_seconds =
            (header_primary_length_m + exhaust_system_length_m) / propagation_speed_m_s;
        const double delay_rate_hz = static_cast<double>(delay_rate.numerator) /
                                     static_cast<double>(delay_rate.denominator);
        const double requested_delay_samples = delay_seconds * delay_rate_hz;
        const std::uint32_t resolved_delay_samples =
            static_cast<std::uint32_t>(std::round(requested_delay_samples));
        core.excitation.cylinder_paths.push_back({
            contract::CylinderId{number},
            route_id,
            builder.resolved(header_primary_length_m,
                             cylinder_path + ".header_primary_length_m",
                             Source::reference_fixture),
            builder.resolved(1.0, cylinder_path + ".sound_attenuation_linear",
                             Source::reference_fixture),
            builder.derived<std::uint32_t>(
                resolved_delay_samples, cylinder_path + ".resolved_delay_samples",
                derived_method("legacy-propagation-delay-round-v1"),
                {
                    cylinder_path + ".header_primary_length_m",
                    excitation_route_path + ".exhaust_system_length_m",
                    profile_path("reference_excitation."
                                 "legacy_propagation_speed_m_s"),
                    profile_path("reference_excitation.delay_rate"),
                }),
        });
    }

    if (operating_profile) {
        contract::LowOrderOperatingPointV1Profile profile;
        profile.core = std::move(core);
        profile.aggregate_loss = {
            builder.resolved(
                0.4, profile_path("aggregate_loss.constant_fmep_bar"),
                Source::operating_literature),
            builder.resolved(
                0.005, profile_path("aggregate_loss.peak_pressure_coefficient"),
                Source::operating_literature),
            builder.resolved(
                0.09,
                profile_path(
                    "aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m"),
                Source::operating_literature),
            builder.resolved(
                0.0009,
                profile_path("aggregate_loss."
                             "mean_piston_speed_squared_coefficient_bar_s2_per_m2"),
                Source::operating_literature),
            builder.resolved(
                363.15,
                profile_path("aggregate_loss.required_oil_temperature_k"),
                Source::declared_default),
            builder.resolved(
                contract::friction_pump_and_accessory_torque_term_mask(),
                profile_path("aggregate_loss.included_terms"),
                Source::profile_contract),
        };
        profile.accessory_configuration = {
            builder.resolved(
                std::string{"bmw-m52b28-warm-stock-accessories-v1"},
                profile_path("accessory_configuration.configuration_id"),
                Source::accessory_configuration),
            builder.resolved(
                bmw_m52b28_operating_accessory_descriptor_sha256(),
                profile_path("accessory_configuration.content_sha256"),
                Source::accessory_configuration),
        };
        profile.starter = {
            builder.resolved(
                true, profile_path("starter.mechanically_disengaged"),
                Source::profile_contract),
            builder.resolved(
                contract::torque_term_mask(contract::TorqueTerm::starter),
                profile_path("starter.included_terms"),
                Source::profile_contract),
        };
        profile.cycle_quadrature = builder.resolved(
            simulation::
                four_stroke_piecewise_linear_cycle_quadrature_method_identity(),
            profile_path("cycle_quadrature"), Source::implemented_method);
        engine.physics_profile = std::move(profile);
        engine.torque_capability = builder.resolved(
            contract::TorqueCapability{
                {
                    contract::Availability::available,
                    contract::Completeness::complete,
                    contract::known_torque_term_mask(),
                    0,
                },
                {
                    contract::Availability::available,
                    contract::Completeness::complete,
                    contract::known_torque_term_mask(),
                    0,
                },
                true,
            },
            "engine.torque_capability", Source::profile_contract);
    } else {
        engine.physics_profile = contract::LegacyLowOrderV1Profile{
            std::move(core),
            std::move(fixed_crank_loss),
        };
        engine.torque_capability = builder.resolved(
            contract::TorqueCapability{
                {
                    contract::Availability::available,
                    contract::Completeness::incomplete,
                    included_terms,
                    omitted_terms,
                },
                {
                    contract::Availability::unavailable,
                    contract::Completeness::incomplete,
                    0,
                    0,
                },
                false,
            },
            "engine.torque_capability", Source::profile_contract);
    }
    engine.provenance_schema_id = std::string{builder.provenance_schema_id()};
    return engine;
}

} // namespace engine_sim_offline::profiles::detail
