#include "contract_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract::test {
namespace {

template <class T> AuthoredValue<T> authored(T value) {
    return {std::move(value), "claim"};
}

AuthoredLegacyRestriction make_restriction(LegacyRestrictionCalibration calibration,
                                           double source_rating, double resolved_k) {
    return {
        authored(calibration),
        authored(source_rating),
        authored(resolved_k),
    };
}

AuthoredLegacyLowOrderV1Profile make_authored_profile() {
    constexpr double kCarb500 = 0.015925315712742586;
    constexpr double kCarb200 = 0.006370126285097034;
    constexpr double kCarb1000 = 0.03185063142548517;
    constexpr double kCarbPointOne = 0.0000031850631425485175;
    constexpr double kCfmOne = 0.00002748668227937587;
    constexpr double kCfmPointOne = 0.0000027486682279375876;

    AuthoredLegacyLowOrderV1Profile profile;
    auto &core = profile.core;
    auto &fixed_crank_loss = profile.fixed_crank_loss;
    core.mechanism.crank = {
        authored(0.0), authored(5.0), authored(5.9), authored(0.2),
    };
    fixed_crank_loss.fixed_crank_friction_magnitude_nm = authored(10.0);
    core.mechanism.cylinders.push_back({
        {
            authored(std::string{"cylinder-1"}),
            authored(std::string{"intake-port-1"}),
            authored(std::string{"exhaust-port-1"}),
            authored(std::string{"intake-runner-1"}),
            authored(std::string{"cylinder-volume-1"}),
            authored(std::string{"exhaust-primary-1"}),
            authored(std::string{"plenum-runner-edge-1"}),
            authored(std::string{"intake-valve-edge-1"}),
            authored(std::string{"exhaust-valve-edge-1"}),
            authored(std::string{"primary-collector-edge-1"}),
            authored(std::string{"blowby-edge-1"}),
            authored(std::string{"exhaust.outlet-1"}),
        },
        {
            authored(0.084),
            authored(0.084),
            authored(0.042),
            authored(0.135),
            authored(0.211),
            authored(0.03182),
            authored(0.000046),
            authored(0.0),
            authored(0.28),
            authored(0.30),
            authored(0.0015),
            authored(0.0),
            authored(0.0),
            authored(0.0),
        },
    });

    core.gas_path.intake_topology = {
        authored(std::string{"intake-plenum"}),
        authored(std::string{"main-throttle-edge"}),
        authored(std::string{"idle-bypass-edge"}),
    };
    core.gas_path.intake = {
        authored(0.002),
        authored(0.01),
        authored(0.15),
        authored(1.0),
        authored(2.0),
        authored(0.0),
        make_restriction(LegacyRestrictionCalibration::carb_at_1p5_inhg, 500.0,
                         kCarb500),
        make_restriction(LegacyRestrictionCalibration::carb_at_1p5_inhg, 0.1,
                         kCarbPointOne),
        make_restriction(LegacyRestrictionCalibration::carb_at_1p5_inhg, 500.0,
                         kCarb500),
    };
    core.gas_path.head.intake_runner_base_volume_m3 = authored(0.0001);
    core.gas_path.head.intake_runner_cross_section_area_m2 = authored(0.002);
    core.gas_path.head.exhaust_runner_base_volume_m3 = authored(0.0003);
    core.gas_path.head.exhaust_runner_cross_section_area_m2 = authored(0.0014);
    core.gas_path.head.flow_table_triangle_radius_m = authored(0.001);
    const auto flow_point = [](std::string id, double lift, double cfm, double k) {
        return AuthoredLegacyValveFlowPoint{
            authored(std::move(id)),
            authored(lift),
            authored(cfm),
            authored(k),
        };
    };
    core.gas_path.head.intake_flow = {
        flow_point("lift-0", 0.0, 0.0, 0.0),
        flow_point("lift-1", 0.001, 1.0, kCfmOne),
    };
    core.gas_path.head.exhaust_flow = {
        flow_point("lift-0", 0.0, 0.0, 0.0),
        flow_point("lift-1", 0.001, 1.0, kCfmOne),
    };
    core.gas_path.exhaust_routes.push_back({
        {
            authored(std::string{"exhaust.outlet-1"}),
            authored(std::string{"exhaust-collector-1"}),
            authored(std::string{"collector-outlet-edge-1"}),
        },
        {
            authored(0.05),
            authored(0.008),
            authored(6.25),
            authored(0.5),
            authored(1.0),
            authored(1.0),
            make_restriction(LegacyRestrictionCalibration::carb_at_1p5_inhg, 200.0,
                             kCarb200),
            make_restriction(LegacyRestrictionCalibration::carb_at_1p5_inhg, 1000.0,
                             kCarb1000),
        },
    });
    core.gas_path.piston_blowby = make_restriction(
        LegacyRestrictionCalibration::cfm_at_28_inh2o, 0.1, kCfmPointOne);

    const auto camshaft = [](std::string port_id) {
        AuthoredLegacyCamshaftProfile cam;
        cam.shape = {
            authored(0.009), authored(3.6),
            authored(0.8),   authored<std::uint32_t>(100),
            authored(0.0),   authored(0.015),
        };
        cam.lobes.push_back({
            authored(std::string{"cylinder-1"}),
            authored(std::move(port_id)),
            authored(0.0),
        });
        return cam;
    };
    core.valvetrain.intake = camshaft("intake-port-1");
    core.valvetrain.exhaust = camshaft("exhaust-port-1");

    core.ignition.firing_order = authored(std::vector<std::string>{"cylinder-1"});
    core.ignition.timing_curve_triangle_radius_rad_s = authored(1000.0);
    core.ignition.timing_curve = {
        {
            authored(std::string{"rpm-0"}),
            authored(0.0),
            authored(0.1),
        },
        {
            authored(std::string{"rpm-1000"}),
            authored(1000.0),
            authored(0.1),
        },
    };
    core.ignition.limiter_speed_rpm = authored(8000.0);
    core.ignition.limiter_hold_s = authored(0.5);
    core.ignition.declared_redline_rpm = authored(7000.0);

    core.fuel.fuel_id = authored(std::string{"gasoline"});
    core.fuel.molecular_mass_kg_per_mol = authored(0.1);
    core.fuel.energy_density_j_per_kg = authored(48.1e6);
    core.fuel.molecular_afr = authored(12.5);
    core.fuel.maximum_burning_efficiency_01 = authored(0.8);
    core.fuel.burning_efficiency_randomness_01 = authored(0.5);
    core.fuel.low_efficiency_attenuation_01 = authored(0.6);
    core.fuel.maximum_turbulence_effect = authored(4.0);
    core.fuel.maximum_dilution_effect = authored(10.0);
    core.fuel.lbv_multiplier = authored(1.0);
    core.fuel.turbulence_to_flame_speed_ratio_triangle_radius = authored(5.0);
    core.fuel.turbulence_to_flame_speed_ratio = {
        {
            authored(std::string{"turbulence-0"}),
            authored(0.0),
            authored(1.0),
        },
        {
            authored(std::string{"turbulence-1"}),
            authored(1.0),
            authored(2.0),
        },
    };

    core.combustion_random_streams.push_back({
        authored(std::string{"cylinder-1"}),
        authored<std::uint64_t>(UINT64_C(0x6ba3d060370e05fa)),
        authored<std::uint64_t>(UINT64_C(0x3e13b1e68ef2f790)),
    });

    const TorqueTermMask included_torque_terms =
        torque_term_mask(TorqueTerm::indicated_gas) |
        torque_term_mask(TorqueTerm::crank_friction);
    fixed_crank_loss.included_terms = authored(included_torque_terms);
    fixed_crank_loss.omitted_terms =
        authored(known_torque_term_mask() & ~included_torque_terms);

    core.excitation.reference_atmosphere_pa_abs = authored(101325.0);
    core.excitation.legacy_propagation_speed_m_s = authored(343.0);
    core.excitation.excitation_scale = authored(1600.0);
    core.excitation.filtered_speed_threshold_rpm = authored(40.0);
    core.excitation.filtered_speed_exponent = authored<std::uint32_t>(3);
    core.excitation.pressure_gains = {
        authored(1.0),
        authored(0.1),
        authored(0.1),
    };
    core.excitation.cylinder_count_divisor = authored(1.0);
    core.excitation.inverse_length_exponent = authored(2.0);
    core.excitation.delay_rate = authored(RationalRateHz{10000, 1});
    core.excitation.cylinder_accumulation_order =
        authored(std::vector<std::string>{"cylinder-1"});
    core.excitation.cylinder_paths.push_back({
        authored(std::string{"cylinder-1"}),
        authored(std::string{"exhaust.outlet-1"}),
        authored(0.0),
        authored(1.0),
        authored<std::uint32_t>(182),
    });
    core.excitation.routes.push_back({
        authored(std::string{"exhaust.outlet-1"}),
        authored(6.25),
        authored(1.0),
    });
    return profile;
}

AuthoredLowOrderOperatingPointV1Profile make_authored_operating_profile() {
    auto legacy = make_authored_profile();
    AuthoredLowOrderOperatingPointV1Profile profile;
    profile.core = std::move(legacy.core);
    profile.aggregate_loss = {
        authored(0.4),
        authored(0.005),
        authored(0.09),
        authored(0.0009),
        authored(363.15),
        authored(friction_pump_and_accessory_torque_term_mask()),
    };
    profile.accessory_configuration = {
        authored(std::string{"warm-stock-accessories-v1"}),
        authored(digest(77)),
    };
    profile.starter = {
        authored(true),
        authored(torque_term_mask(TorqueTerm::starter)),
    };
    profile.cycle_quadrature = authored(
        MethodSelection{"four-stroke-piecewise-linear-cycle-quadrature-v1", 1});
    const auto acoustic_method = [](std::string id) {
        return authored(MethodSelection{std::move(id), 1});
    };
    profile.exhaust_acoustics.assembly_id =
        authored(std::string{"declared-test-cell-open-pipe"});
    profile.exhaust_acoustics.methods = {
        acoustic_method("ideal-pseudo-gas-source-properties"),
        acoustic_method("causal-bandlimited-rational-resampling"),
        acoustic_method("uniform-cylindrical-digital-waveguide"),
        acoustic_method("ideal-compact-pressure-junction"),
        acoustic_method("causal-unflanged-pipe-reflection"),
        acoustic_method("compact-monopole-free-field-radiation"),
    };
    profile.exhaust_acoustics.source_interval_rate = authored(RationalRateHz{80000, 1});
    profile.exhaust_acoustics.acoustic_rate = authored(RationalRateHz{192000, 1});
    profile.exhaust_acoustics.universal_gas_constant_j_per_mol_k =
        authored(8.31446261815324);
    profile.exhaust_acoustics.source_molar_mass_kg_per_mol = authored(0.02897);
    profile.exhaust_acoustics.source_heat_capacity_ratio = authored(1.4);
    profile.exhaust_acoustics.pa_per_full_scale = authored(256.0);
    profile.exhaust_acoustics.ducts = {
        {
            authored(std::string{"primary-1"}),
            authored(AcousticDuctKind::primary),
            authored(0.3),
            authored(0.042),
            authored(800.0),
            authored(0.1),
        },
        {
            authored(std::string{"downstream-1"}),
            authored(AcousticDuctKind::downstream),
            authored(1.5),
            authored(0.046),
            authored(600.0),
            authored(0.1),
        },
    };
    profile.exhaust_acoustics.primary_bindings.push_back({
        authored(std::string{"cylinder-1"}),
        authored(std::string{"exhaust-port-1"}),
        authored(std::string{"primary-1"}),
        authored(std::string{"junction-1"}),
    });
    profile.exhaust_acoustics.junctions.push_back({
        authored(std::string{"junction-1"}),
        {authored(std::string{"primary-1"})},
        authored(std::string{"downstream-1"}),
    });
    profile.exhaust_acoustics.outlets.push_back({
        authored(std::string{"exhaust.outlet-1"}),
        authored(std::string{"downstream-1"}),
        authored(1.0),
    });
    return profile;
}

ProvenanceLedger make_provenance() {
    ProvenanceLedger ledger;
    ledger.schema_id = "authored-profile-test-provenance-v1";
    ledger.bundle = {"authored-profile-test-provenance-v1", digest(41)};
    ledger.evidence.push_back({
        "accessory-descriptor",
        "test/accessory-descriptor-v1",
        std::nullopt,
        digest(77),
        RightsDisposition::permitted,
    });
    ledger.claims.push_back({
        "claim",
        ProvenanceOrigin::artistic,
        {EvidenceCitation{"accessory-descriptor", "complete descriptor"}},
        std::nullopt,
    });
    return ledger;
}

AuthoredEngineDefinition make_authored_engine() {
    AuthoredEngineDefinition definition;
    definition.schema_version = 1;
    definition.definition_id = "authored-engine-test-v1";
    definition.engine_id = authored(std::string{"test-engine"});
    definition.profile_id = authored(std::string{"test-engine-profile"});
    definition.display_name = authored(std::string{"Test engine"});
    definition.cycle = authored(EngineCycle::four_stroke);
    definition.ignition = authored(IgnitionKind::spark_ignition);
    definition.cylinder_layout = authored(CylinderLayoutKind::inline_engine);
    definition.banks = {authored(std::string{"bank-1"})};

    constexpr double bore_m = 0.084;
    constexpr double stroke_m = 0.084;
    constexpr double crank_radius_m = 0.042;
    constexpr double rod_length_m = 0.135;
    constexpr double deck_height_m = 0.211;
    constexpr double piston_compression_height_m = 0.03182;
    constexpr double head_chamber_volume_m3 = 0.000046;
    const auto piston_area_m2 = std::numbers::pi * bore_m * bore_m / 4.0;
    const auto clearance_volume_m3 =
        head_chamber_volume_m3 +
        piston_area_m2 * (deck_height_m - (crank_radius_m + rod_length_m) -
                          piston_compression_height_m);
    const auto swept_volume_m3 = piston_area_m2 * stroke_m;
    const auto compression_ratio =
        (clearance_volume_m3 + swept_volume_m3) / clearance_volume_m3;
    definition.cylinders.push_back({
        authored(std::string{"cylinder-1"}),
        authored(std::string{"bank-1"}),
        authored(bore_m),
        authored(stroke_m),
        authored(rod_length_m),
        authored(compression_ratio),
        authored(0.0),
        authored(0.0),
    });
    definition.ports = {
        {
            authored(std::string{"intake-port-1"}),
            authored(std::string{"cylinder-1"}),
            authored(PortKind::intake),
        },
        {
            authored(std::string{"exhaust-port-1"}),
            authored(std::string{"cylinder-1"}),
            authored(PortKind::exhaust),
        },
    };
    const auto volume = [](std::string id, GasVolumeKind kind) {
        return AuthoredGasVolumeDefinition{
            authored(std::move(id)),
            authored(kind),
        };
    };
    definition.gas_volumes = {
        volume("intake-plenum", GasVolumeKind::intake_plenum),
        volume("intake-runner-1", GasVolumeKind::intake_runner),
        volume("cylinder-volume-1", GasVolumeKind::cylinder),
        volume("exhaust-primary-1", GasVolumeKind::exhaust_primary),
        volume("exhaust-collector-1", GasVolumeKind::exhaust_collector),
        volume("atmosphere", GasVolumeKind::atmosphere),
    };
    const auto edge = [](std::string id, std::string endpoint_0,
                         std::string endpoint_1) {
        return AuthoredFlowEdgeDefinition{
            authored(std::move(id)),
            authored(std::move(endpoint_0)),
            authored(std::move(endpoint_1)),
        };
    };
    definition.flow_edges = {
        edge("main-throttle-edge", "atmosphere", "intake-plenum"),
        edge("idle-bypass-edge", "atmosphere", "intake-plenum"),
        edge("plenum-runner-edge-1", "intake-plenum", "intake-runner-1"),
        edge("intake-valve-edge-1", "intake-runner-1", "cylinder-volume-1"),
        edge("exhaust-valve-edge-1", "cylinder-volume-1", "exhaust-primary-1"),
        edge("primary-collector-edge-1", "exhaust-primary-1", "exhaust-collector-1"),
        edge("blowby-edge-1", "cylinder-volume-1", "atmosphere"),
        edge("collector-outlet-edge-1", "atmosphere", "exhaust-collector-1"),
    };
    definition.routes.push_back({
        authored(std::string{"exhaust.outlet-1"}),
        authored(SourceRouteKind::exhaust_outlet),
        authored(std::string{"exhaust-collector-1"}),
        std::nullopt,
        std::nullopt,
    });
    const auto legacy_method = authored(MethodSelection{"legacy_low_order_v1", 1});
    definition.methods = {
        legacy_method, legacy_method, legacy_method, legacy_method,
        legacy_method, legacy_method, legacy_method, legacy_method,
    };
    definition.physics_profile = make_authored_profile();
    definition.provenance = make_provenance();
    return definition;
}

EngineSpec make_resolved_operating_engine(InputBuilder &builder) {
    constexpr std::string_view old_root =
        "engine.physics.legacy-low-order-v1";
    constexpr std::string_view new_root =
        "engine.physics.low-order-operating-point-v1";
    const auto reroot = [&](std::string &path) {
        if (path.starts_with(old_root)) {
            path.replace(0, old_root.size(), new_root);
        }
    };

    auto engine = make_engine(builder);
    for (auto &resolution : builder.provenance.resolutions) {
        reroot(resolution.parameter_path);
        for (auto &dependency : resolution.dependency_parameter_paths) {
            reroot(dependency);
        }
    }
    builder.provenance.evidence.push_back({
        "accessory-descriptor",
        "test/accessory-descriptor-v1",
        std::nullopt,
        digest(77),
        RightsDisposition::permitted,
    });
    builder.provenance.claims.front().citations.push_back(
        {"accessory-descriptor", "complete descriptor"});

    auto legacy = std::get<LegacyLowOrderV1Profile>(
        std::move(engine.physics_profile));
    LowOrderOperatingPointV1Profile profile;
    profile.core = std::move(legacy.core);
    const auto path = [](std::string_view suffix) {
        return std::string{"engine.physics.low-order-operating-point-v1."} +
               std::string{suffix};
    };
    profile.aggregate_loss = {
        builder.resolved(0.4, path("aggregate_loss.constant_fmep_bar")),
        builder.resolved(0.005,
                         path("aggregate_loss.peak_pressure_coefficient")),
        builder.resolved(
            0.09,
            path("aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m")),
        builder.resolved(
            0.0009,
            path("aggregate_loss."
                 "mean_piston_speed_squared_coefficient_bar_s2_per_m2")),
        builder.resolved(363.15,
                         path("aggregate_loss.required_oil_temperature_k")),
        builder.resolved(friction_pump_and_accessory_torque_term_mask(),
                         path("aggregate_loss.included_terms")),
    };
    profile.accessory_configuration = {
        builder.resolved(std::string{"warm-stock-accessories-v1"},
                         path("accessory_configuration.configuration_id")),
        builder.resolved(digest(77),
                         path("accessory_configuration.content_sha256")),
    };
    profile.starter = {
        builder.resolved(true, path("starter.mechanically_disengaged")),
        builder.resolved(torque_term_mask(TorqueTerm::starter),
                         path("starter.included_terms")),
    };
    profile.cycle_quadrature = builder.resolved(
        method("four-stroke-piecewise-linear-cycle-quadrature-v1", 82),
        path("cycle_quadrature"));
    auto &acoustics = profile.exhaust_acoustics;
    const auto acoustic_path = [&](std::string_view suffix) {
        return path("exhaust_acoustics." + std::string{suffix});
    };
    const auto acoustic_method = [&](std::string id, std::uint8_t byte,
                                     std::string_view role) {
        return builder.resolved(method(std::move(id), byte),
                                acoustic_path("methods." + std::string{role}));
    };
    acoustics.assembly_id = builder.resolved(
        std::string{"declared-test-cell-open-pipe"}, acoustic_path("assembly_id"));
    acoustics.methods = {
        acoustic_method("ideal-pseudo-gas-source-properties", 91, "source_properties"),
        acoustic_method("causal-bandlimited-rational-resampling", 92, "reconstruction"),
        acoustic_method("uniform-cylindrical-digital-waveguide", 93, "waveguide"),
        acoustic_method("ideal-compact-pressure-junction", 94, "junction"),
        acoustic_method("causal-unflanged-pipe-reflection", 95, "outlet_reflection"),
        acoustic_method("compact-monopole-free-field-radiation", 96,
                        "exterior_radiation"),
    };
    acoustics.source_interval_rate = builder.resolved(
        RationalRateHz{80000, 1}, acoustic_path("source_interval_rate"));
    acoustics.acoustic_rate =
        builder.resolved(RationalRateHz{192000, 1}, acoustic_path("acoustic_rate"));
    acoustics.universal_gas_constant_j_per_mol_k = builder.resolved(
        8.31446261815324, acoustic_path("universal_gas_constant_j_per_mol_k"));
    acoustics.source_molar_mass_kg_per_mol =
        builder.resolved(0.02897, acoustic_path("source_molar_mass_kg_per_mol"));
    acoustics.source_heat_capacity_ratio =
        builder.resolved(1.4, acoustic_path("source_heat_capacity_ratio"));
    acoustics.pa_per_full_scale =
        builder.resolved(256.0, acoustic_path("pa_per_full_scale"));
    acoustics.ducts = {
        {
            AcousticDuctId{1},
            builder.resolved(std::string{"primary-1"},
                             acoustic_path("ducts.primary-1.semantic_id")),
            builder.resolved(AcousticDuctKind::primary,
                             acoustic_path("ducts.primary-1.kind")),
            builder.resolved(0.3, acoustic_path("ducts.primary-1.length_m")),
            builder.resolved(0.042, acoustic_path("ducts.primary-1.inner_diameter_m")),
            builder.resolved(800.0,
                             acoustic_path("ducts.primary-1.reference_temperature_k")),
            builder.resolved(
                0.1, acoustic_path("ducts.primary-1.propagation_loss_np_per_m")),
        },
        {
            AcousticDuctId{2},
            builder.resolved(std::string{"downstream-1"},
                             acoustic_path("ducts.downstream-1.semantic_id")),
            builder.resolved(AcousticDuctKind::downstream,
                             acoustic_path("ducts.downstream-1.kind")),
            builder.resolved(1.5, acoustic_path("ducts.downstream-1.length_m")),
            builder.resolved(0.046,
                             acoustic_path("ducts.downstream-1.inner_diameter_m")),
            builder.resolved(
                600.0, acoustic_path("ducts.downstream-1.reference_temperature_k")),
            builder.resolved(
                0.1, acoustic_path("ducts.downstream-1.propagation_loss_np_per_m")),
        },
    };
    acoustics.primary_bindings.push_back(
        {CylinderId{1}, PortId{2}, AcousticDuctId{1}, AcousticJunctionId{1}});
    acoustics.junctions.push_back({
        AcousticJunctionId{1},
        builder.resolved(std::string{"junction-1"},
                         acoustic_path("junctions.junction-1.semantic_id")),
        {AcousticDuctId{1}},
        AcousticDuctId{2},
    });
    acoustics.outlets.push_back({
        RouteId{1},
        AcousticDuctId{2},
        builder.resolved(
            1.0, acoustic_path("outlets.exhaust.outlet-1.observation_distance_m")),
    });
    engine.physics_profile = std::move(profile);
    engine.methods.losses.value =
        method("chen-flynn-cycle-mean-aggregate-loss-v1", 81);
    engine.torque_capability.value = {
        {
            Availability::available,
            Completeness::complete,
            known_torque_term_mask(),
            0,
        },
        {
            Availability::available,
            Completeness::complete,
            known_torque_term_mask(),
            0,
        },
        true,
    };
    return engine;
}

template <class Mutation>
void expect_authored_mutation_rejected(const char *message, Mutation &&mutation) {
    auto profile = make_authored_profile();
    std::forward<Mutation>(mutation)(profile);
    const auto report = validate(AuthoredExecutablePhysicsProfile{std::move(profile)},
                                 make_provenance());
    expect(!report.ok(), message);
}

template <class Mutation>
void expect_authored_operating_mutation_rejected(const char *message,
                                                 Mutation &&mutation) {
    auto profile = make_authored_operating_profile();
    std::forward<Mutation>(mutation)(profile);
    const auto report = validate(AuthoredExecutablePhysicsProfile{std::move(profile)},
                                 make_provenance());
    expect(!report.ok(), message);
}

} // namespace

void run_authored_profile_contract_tests() {
    const auto valid_report = validate(
        AuthoredExecutablePhysicsProfile{make_authored_profile()}, make_provenance());
    expect(valid_report.ok(), "valid authored executable profile was rejected");

    expect(validate(AuthoredExecutablePhysicsProfile{
                        make_authored_operating_profile()},
                    make_provenance())
               .ok(),
           "valid authored operating-point profile was rejected");
    expect_authored_operating_mutation_rejected(
        "negative-zero operating loss coefficient was accepted",
        [](AuthoredLowOrderOperatingPointV1Profile &profile) {
            profile.aggregate_loss.constant_fmep_bar.value = -0.0;
        });
    expect_authored_operating_mutation_rejected(
        "incorrect operating aggregate-loss term scope was accepted",
        [](AuthoredLowOrderOperatingPointV1Profile &profile) {
            profile.aggregate_loss.included_terms.value ^=
                torque_term_mask(TorqueTerm::accessory);
        });
    expect_authored_operating_mutation_rejected(
        "engaged operating starter declaration was accepted",
        [](AuthoredLowOrderOperatingPointV1Profile &profile) {
            profile.starter.mechanically_disengaged.value = false;
        });
    expect_authored_operating_mutation_rejected(
        "wrong operating cycle quadrature was accepted",
        [](AuthoredLowOrderOperatingPointV1Profile &profile) {
            profile.cycle_quadrature.value.id = "other-quadrature-v1";
        });
    {
        auto profile = make_authored_operating_profile();
        auto provenance = make_provenance();
        provenance.evidence.front().content_sha256 = digest(78);
        expect(!validate(AuthoredExecutablePhysicsProfile{std::move(profile)},
                         provenance)
                    .ok(),
               "accessory descriptor digest without matching evidence was accepted");
    }

    auto deterministic_profile = make_authored_profile();
    deterministic_profile.core.fuel.burning_efficiency_randomness_01.value = 0.0;
    expect(validate(AuthoredExecutablePhysicsProfile{deterministic_profile},
                    make_provenance())
               .ok(),
           "zero burning-efficiency variation invalidated the executed RNG stream");
    deterministic_profile.core.combustion_random_streams.clear();
    expect(!validate(AuthoredExecutablePhysicsProfile{std::move(deterministic_profile)},
                     make_provenance())
                .ok(),
           "zero burning-efficiency variation hid an RNG stream consumed by "
           "combustion");

    expect_authored_mutation_rejected(
        "negative authored piston mass was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.mechanism.cylinders.front()
                .parameters.piston_mass_kg.value = -0.1;
        });
    expect_authored_mutation_rejected(
        "NaN authored crankshaft mass was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.mechanism.crank.crankshaft_mass_kg.value =
                std::numeric_limits<double>::quiet_NaN();
        });
    expect_authored_mutation_rejected(
        "negative authored fixed crank loss was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.fixed_crank_loss.fixed_crank_friction_magnitude_nm.value = -0.1;
        });
    expect_authored_mutation_rejected(
        "unknown authored restriction calibration was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.gas_path.intake.main_throttle.calibration.value =
                LegacyRestrictionCalibration::unspecified;
        });
    expect_authored_mutation_rejected(
        "stale authored restriction coefficient was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.gas_path.intake.main_throttle.resolved_k.value += 0.001;
        });
    expect_authored_mutation_rejected(
        "unsorted authored valve-flow table was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.gas_path.head.intake_flow.back().lift_m.value = 0.0;
        });
    expect_authored_mutation_rejected(
        "out-of-range authored fuel efficiency was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.fuel.maximum_burning_efficiency_01.value = 1.1;
        });
    expect_authored_mutation_rejected(
        "nonpositive authored flame-speed table radius was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.value =
                0.0;
        });
    expect_authored_mutation_rejected(
        "unclaimed authored flame-speed table radius was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.claim_id
                .clear();
        });
    expect_authored_mutation_rejected(
        "implemented authored combustion accepted no random streams",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.clear();
        });
    expect_authored_mutation_rejected(
        "duplicate authored combustion random-stream owner was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.push_back(
                profile.core.combustion_random_streams.front());
        });
    expect_authored_mutation_rejected(
        "dangling authored combustion random-stream owner was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.front().cylinder_id.value =
                "cylinder-2";
        });
    expect_authored_mutation_rejected(
        "out-of-range authored PCG32 stream was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.front().pcg32_stream.value =
                std::numeric_limits<std::uint64_t>::max();
        });
    expect_authored_mutation_rejected(
        "unclaimed authored PCG32 initial state was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.front()
                .pcg32_initial_state.claim_id.clear();
        });
    expect_authored_mutation_rejected(
        "unclaimed authored PCG32 stream was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.combustion_random_streams.front()
                .pcg32_stream.claim_id.clear();
        });
    expect_authored_mutation_rejected(
        "overlapping authored loss classifications were accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.fixed_crank_loss.omitted_terms.value =
                torque_term_mask(TorqueTerm::indicated_gas);
        });
    expect_authored_mutation_rejected(
        "disjoint but false authored loss partition was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.fixed_crank_loss.included_terms.value =
                indicated_gas_torque_term_mask();
            profile.fixed_crank_loss.omitted_terms.value =
                known_torque_term_mask() & ~indicated_gas_torque_term_mask();
        });
    expect_authored_mutation_rejected(
        "invalid authored excitation propagation speed was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.excitation.legacy_propagation_speed_m_s.value = -1.0;
        });
    expect_authored_mutation_rejected(
        "authored excitation route drifted from its gas-path route",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.excitation.routes.front().audio_volume_linear.value += 0.1;
        });
    expect_authored_mutation_rejected(
        "stale authored propagation delay was accepted",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            ++profile.core.excitation.cylinder_paths.front()
                  .resolved_delay_samples.value;
        });

    expect(validate(make_authored_engine()).ok(),
           "valid authored engine and executable profile were rejected");
    auto operating_engine = make_authored_engine();
    operating_engine.methods.losses.value = {
        "chen-flynn-cycle-mean-aggregate-loss-v1",
        1,
    };
    operating_engine.physics_profile = make_authored_operating_profile();
    expect(validate(operating_engine).ok(),
           "valid authored operating-point engine was rejected");
    operating_engine.methods.losses.value = {"legacy_low_order_v1", 1};
    expect(!validate(operating_engine).ok(),
           "operating-point profile accepted the legacy loss method");

    InputBuilder resolved_builder;
    auto resolved_operating_engine =
        make_resolved_operating_engine(resolved_builder);
    expect(validate(resolved_operating_engine, resolved_builder.provenance).ok(),
           "valid resolved operating-point engine was rejected");

    auto wrong_resolved_capability = resolved_operating_engine;
    wrong_resolved_capability.torque_capability.value.instantaneous_net_shaft = {
        Availability::available,
        Completeness::incomplete,
        indicated_gas_torque_term_mask(),
        known_torque_term_mask() & ~indicated_gas_torque_term_mask(),
    };
    expect(!validate(wrong_resolved_capability, resolved_builder.provenance).ok(),
           "operating profile accepted incomplete instantaneous net torque");

    auto wrong_resolved_total = resolved_operating_engine;
    wrong_resolved_total.total_displacement_m3.value =
        std::nextafter(wrong_resolved_total.total_displacement_m3.value,
                       std::numeric_limits<double>::infinity());
    expect(!validate(wrong_resolved_total, resolved_builder.provenance).ok(),
           "operating profile accepted a nonidentical stable displacement sum");

    auto wrong_resolved_quadrature = resolved_operating_engine;
    std::get<LowOrderOperatingPointV1Profile>(
        wrong_resolved_quadrature.physics_profile)
        .cycle_quadrature.value.id = "other-quadrature-v1";
    expect(!validate(wrong_resolved_quadrature, resolved_builder.provenance).ok(),
           "resolved operating profile accepted the wrong cycle quadrature");

    auto shallow_relabelled = resolved_operating_engine;
    const auto &core_field =
        std::get<LowOrderOperatingPointV1Profile>(
            shallow_relabelled.physics_profile)
            .core.mechanism.crank.crankshaft_mass_kg;
    auto shallow_provenance = resolved_builder.provenance;
    const auto shallow_resolution = std::ranges::find(
        shallow_provenance.resolutions, core_field.resolution_id,
        &ResolutionRecord::id);
    expect(shallow_resolution != shallow_provenance.resolutions.end(),
           "operating core test resolution disappeared");
    shallow_resolution->parameter_path =
        "engine.physics.legacy-low-order-v1.mechanism.crank.crankshaft_mass_kg";
    expect(!validate(shallow_relabelled, shallow_provenance).ok(),
           "M3 resolution was shallow-relabelled as an operating profile");

    auto mismatched_accessory_evidence = resolved_builder.provenance;
    std::ranges::find(mismatched_accessory_evidence.evidence,
                      std::string{"accessory-descriptor"},
                      &EvidenceSource::id)
        ->content_sha256 = digest(78);
    expect(!validate(resolved_operating_engine, mismatched_accessory_evidence)
                .ok(),
           "resolved accessory digest without matching evidence was accepted");

    auto contradictory_engine = make_authored_engine();
    contradictory_engine.cylinders.front().bore_m->value += 0.001;
    expect(!validate(contradictory_engine).ok(),
           "authored bore was allowed to contradict the executable profile");
    contradictory_engine = make_authored_engine();
    contradictory_engine.cylinders.front().compression_ratio->value += 0.1;
    expect(!validate(contradictory_engine).ok(),
           "authored compression ratio was allowed to contradict the executable "
           "profile");
    contradictory_engine = make_authored_engine();
    contradictory_engine.cylinders.front().journal_phase_rad->value =
        std::numeric_limits<double>::quiet_NaN();
    expect(!validate(contradictory_engine).ok(),
           "nonfinite authored journal phase was accepted");
}

} // namespace engine_sim_offline::contract::test
