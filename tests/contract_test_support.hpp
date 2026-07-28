#pragma once

#include "engine_sim_offline/contract.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract::test {

inline void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline Sha256Digest digest(std::uint8_t first_byte = 1) {
    Sha256Digest value;
    value.bytes[0] = first_byte;
    return value;
}

inline MethodIdentity method(std::string id, std::uint8_t digest_byte = 1) {
    return {std::move(id), 1, digest(digest_byte)};
}

inline std::string test_runtime_provider_identity(std::string_view soname) {
    return "elf64le-x86_64.soname." + std::string(soname) +
           ".bytes.4096.buildid."
           "0100000000000000000000000000000000000000.sha256."
           "0200000000000000000000000000000000000000000000000000000000000000";
}

inline std::string test_standard_library_identity() {
    return "release.13.headers.20240601.gxxabi.1019.cxx11abi.1+" +
           test_runtime_provider_identity("libstdc++.so.6") +
           "+symbols.__cxa_throw.CXXABI_1.3.00000000000bb340";
}

inline std::string test_math_library_identity() {
    return "glibc.2.39+" + test_runtime_provider_identity("libm.so.6") +
           "+symbols.ceil.GLIBC_2.2.5.0000000000000001"
           ".cos.GLIBC_2.2.5.0000000000000002"
           ".floor.GLIBC_2.2.5.0000000000000003"
           ".roundl.GLIBC_2.2.5.0000000000000004"
           ".sin.GLIBC_2.2.5.0000000000000005"
           ".sincos.GLIBC_2.2.5.0000000000000006"
           ".tan.GLIBC_2.2.5.0000000000000007";
}

inline std::string test_compiler_runtime_identity() {
    return test_runtime_provider_identity("libgcc_s.so.1") +
           "+symbols.__muldc3.GCC_4.0.0.0000000000000001";
}

struct InputBuilder {
    ProvenanceLedger provenance{
        "engine-sim-offline.provenance.v1",
        {"bmw-m52b28-inputs-v1", digest(11)},
        {
            EvidenceSource{
                "source",
                "docs/source",
                std::nullopt,
                digest(1),
                RightsDisposition::permitted,
            },
            EvidenceSource{
                "test-ir-source",
                "test-assets/test-ir.wav",
                std::nullopt,
                digest(30),
                RightsDisposition::permitted,
            },
            EvidenceSource{
                "test-presentation-record-source",
                "docs/test-presentation-record.md",
                std::nullopt,
                digest(29),
                RightsDisposition::permitted,
            },
        },
        {
            ProvenanceClaim{
                "claim",
                ProvenanceOrigin::literature,
                {EvidenceCitation{"source", "section-1"}},
                std::nullopt,
            },
            ProvenanceClaim{
                "derived-claim",
                ProvenanceOrigin::derived,
                {},
                std::nullopt,
            },
        },
        {},
    };
    std::uint32_t next_resolution = 1;

    [[nodiscard]] std::string add_resolution(std::string parameter_path) {
        const auto id = "resolution-" + std::to_string(next_resolution++);
        provenance.resolutions.push_back({
            id,
            std::move(parameter_path),
            ResolutionMode::authored,
            "claim",
            std::nullopt,
            {},
        });
        return id;
    }

    template <class T>
    [[nodiscard]] ResolvedValue<T> resolved(T value, std::string parameter_path) {
        return {
            std::move(value),
            add_resolution(std::move(parameter_path)),
        };
    }

    template <class T>
    [[nodiscard]] ResolvedValue<T>
    derived(T value, std::string parameter_path,
            std::vector<std::string> dependency_parameter_paths) {
        const auto id = "resolution-" + std::to_string(next_resolution++);
        provenance.resolutions.push_back({
            id,
            parameter_path,
            ResolutionMode::derived,
            "derived-claim",
            method("contract-test-derived-v1", 250),
            std::move(dependency_parameter_paths),
        });
        return {
            std::move(value),
            id,
        };
    }
};

inline LegacyRestriction make_restriction(InputBuilder &builder,
                                          LegacyRestrictionCalibration calibration,
                                          double source_rating, double resolved_k,
                                          const std::string &path) {
    auto resolved_calibration = builder.resolved(calibration, path + ".calibration");
    auto resolved_source_rating =
        builder.resolved(source_rating, path + ".source_rating");
    return {
        std::move(resolved_calibration),
        std::move(resolved_source_rating),
        builder.derived(resolved_k, path + ".resolved_k",
                        {path + ".calibration", path + ".source_rating"}),
    };
}

inline LegacyLowOrderV1Profile make_physics_profile(InputBuilder &builder) {
    constexpr std::string_view root = "engine.physics.legacy-low-order-v1";
    const auto path = [&](std::string_view suffix) {
        return std::string(root) + "." + std::string(suffix);
    };
    constexpr double kCarb500 = 0.015925315712742586;
    constexpr double kCarb200 = 0.006370126285097034;
    constexpr double kCarb1000 = 0.03185063142548517;
    constexpr double kCarbPointOne = 0.0000031850631425485175;
    constexpr double kCfmOne = 0.00002748668227937587;
    constexpr double kCfmPointOne = 0.0000027486682279375876;

    LegacyLowOrderV1Profile profile;
    profile.mechanism.crank = {
        builder.resolved(0.0, path("mechanism.crank.crank_tdc_reference_rad")),
        builder.resolved(5.0, path("mechanism.crank.crankshaft_mass_kg")),
        builder.resolved(5.9, path("mechanism.crank.flywheel_mass_kg")),
        builder.resolved(0.2, path("mechanism.crank.authored_crank_inertia_kg_m2")),
        builder.resolved(10.0,
                         path("mechanism.crank.fixed_crank_friction_magnitude_nm")),
    };
    LegacyCylinderAssembly cylinder;
    cylinder.topology = {
        CylinderId{1},  PortId{1},      PortId{2},     GasVolumeId{2},
        GasVolumeId{3}, GasVolumeId{4}, FlowEdgeId{3}, FlowEdgeId{4},
        FlowEdgeId{5},  FlowEdgeId{6},  FlowEdgeId{7}, RouteId{1},
    };
    const auto cylinder_path = path("mechanism.cylinders.cylinder-1");
    cylinder.parameters = {
        builder.resolved(0.084, cylinder_path + ".bore_m"),
        builder.resolved(0.084, cylinder_path + ".stroke_m"),
        builder.resolved(0.042, cylinder_path + ".crank_radius_m"),
        builder.resolved(0.135, cylinder_path + ".connecting_rod_length_m"),
        builder.resolved(0.211, cylinder_path + ".deck_height_m"),
        builder.resolved(0.03182, cylinder_path + ".piston_compression_height_m"),
        builder.resolved(0.000046, cylinder_path + ".head_chamber_volume_m3"),
        builder.resolved(0.0, cylinder_path + ".piston_displacement_term_m3"),
        builder.resolved(0.28, cylinder_path + ".piston_mass_kg"),
        builder.resolved(0.30, cylinder_path + ".connecting_rod_mass_kg"),
        builder.resolved(0.0015, cylinder_path + ".connecting_rod_inertia_kg_m2"),
        builder.resolved(0.0, cylinder_path + ".journal_angle_rad"),
        builder.resolved(0.0, cylinder_path + ".ignition_wire_angle_rad"),
        builder.resolved(0.0, cylinder_path + ".header_primary_length_m"),
    };
    profile.mechanism.cylinders.push_back(std::move(cylinder));

    profile.gas_path.intake_topology = {
        GasVolumeId{1},
        FlowEdgeId{1},
        FlowEdgeId{2},
    };
    profile.gas_path.intake = {
        builder.resolved(0.002, path("gas_path.intake.plenum_volume_m3")),
        builder.resolved(0.01, path("gas_path.intake.plenum_cross_section_area_m2")),
        builder.resolved(0.15, path("gas_path.intake.runner_length_m")),
        builder.resolved(1.0, path("gas_path.intake.velocity_decay")),
        builder.resolved(2.0, path("gas_path.intake.throttle_gamma")),
        builder.resolved(0.0, path("gas_path.intake.idle_throttle_plate_position_01")),
        make_restriction(builder, LegacyRestrictionCalibration::carb_at_1p5_inhg, 500.0,
                         kCarb500, path("gas_path.intake.main_throttle")),
        make_restriction(builder, LegacyRestrictionCalibration::carb_at_1p5_inhg, 0.1,
                         kCarbPointOne, path("gas_path.intake.idle_bypass")),
        make_restriction(builder, LegacyRestrictionCalibration::carb_at_1p5_inhg, 500.0,
                         kCarb500, path("gas_path.intake.plenum_to_runner")),
    };
    profile.gas_path.head.intake_runner_base_volume_m3 =
        builder.resolved(0.0001, path("gas_path.head.intake_runner_base_volume_m3"));
    profile.gas_path.head.intake_runner_cross_section_area_m2 = builder.resolved(
        0.002, path("gas_path.head.intake_runner_cross_section_area_m2"));
    profile.gas_path.head.exhaust_runner_base_volume_m3 =
        builder.resolved(0.0003, path("gas_path.head.exhaust_runner_base_volume_m3"));
    profile.gas_path.head.exhaust_runner_cross_section_area_m2 = builder.resolved(
        0.0014, path("gas_path.head.exhaust_runner_cross_section_area_m2"));
    profile.gas_path.head.flow_table_triangle_radius_m =
        builder.resolved(0.001, path("gas_path.head.flow_table_triangle_radius_m"));
    const auto make_flow_point = [&](std::string table, std::string id, double lift,
                                     double cfm, double k) {
        const auto base = path("gas_path.head." + table + "." + id);
        auto sample_id = builder.resolved(id, base + ".sample_id");
        auto resolved_lift = builder.resolved(lift, base + ".lift_m");
        auto resolved_source = builder.resolved(cfm, base + ".source_cfm_at_28_inh2o");
        return LegacyValveFlowPoint{
            std::move(sample_id),
            std::move(resolved_lift),
            std::move(resolved_source),
            builder.derived(k, base + ".resolved_k",
                            {base + ".source_cfm_at_28_inh2o"}),
        };
    };
    profile.gas_path.head.intake_flow = {
        make_flow_point("intake_flow", "lift-0", 0.0, 0.0, 0.0),
        make_flow_point("intake_flow", "lift-1", 0.001, 1.0, kCfmOne),
    };
    profile.gas_path.head.exhaust_flow = {
        make_flow_point("exhaust_flow", "lift-0", 0.0, 0.0, 0.0),
        make_flow_point("exhaust_flow", "lift-1", 0.001, 1.0, kCfmOne),
    };

    LegacyExhaustRouteProfile exhaust;
    exhaust.topology = {RouteId{1}, GasVolumeId{5}, FlowEdgeId{8}};
    const auto exhaust_path = path("gas_path.exhaust_routes.exhaust.outlet-1");
    exhaust.parameters = {
        builder.resolved(0.05, exhaust_path + ".collector_volume_m3"),
        builder.resolved(0.008, exhaust_path + ".collector_cross_section_area_m2"),
        builder.resolved(6.25, exhaust_path + ".exhaust_system_length_m"),
        builder.resolved(0.5, exhaust_path + ".primary_tube_length_m"),
        builder.resolved(1.0, exhaust_path + ".velocity_decay"),
        builder.resolved(1.0, exhaust_path + ".audio_volume_linear"),
        make_restriction(builder, LegacyRestrictionCalibration::carb_at_1p5_inhg, 200.0,
                         kCarb200, exhaust_path + ".primary_to_collector"),
        make_restriction(builder, LegacyRestrictionCalibration::carb_at_1p5_inhg,
                         1000.0, kCarb1000, exhaust_path + ".collector_outlet"),
    };
    profile.gas_path.exhaust_routes.push_back(std::move(exhaust));
    profile.gas_path.piston_blowby =
        make_restriction(builder, LegacyRestrictionCalibration::cfm_at_28_inh2o, 0.1,
                         kCfmPointOne, path("gas_path.piston_blowby"));

    const auto make_cam = [&](std::string name, PortId port_id) {
        const auto base = path("valvetrain." + name);
        LegacyCamshaftProfile camshaft;
        camshaft.shape = {
            builder.resolved(0.009, base + ".shape.maximum_lift_m"),
            builder.resolved(3.6, base + ".shape.duration_at_reference_lift_rad"),
            builder.resolved(0.8, base + ".shape.exponent"),
            builder.resolved<std::uint32_t>(100, base + ".shape.construction_steps"),
            builder.resolved(0.0, base + ".shape.advance_rad"),
            builder.resolved(0.015, base + ".shape.base_radius_m"),
        };
        camshaft.lobes.push_back({
            CylinderId{1},
            port_id,
            builder.resolved(0.0, base + ".lobes.cylinder-1.crank_center_rad"),
        });
        return camshaft;
    };
    profile.valvetrain.intake = make_cam("intake", PortId{1});
    profile.valvetrain.exhaust = make_cam("exhaust", PortId{2});

    profile.ignition.firing_order = builder.resolved(
        std::vector<CylinderId>{CylinderId{1}}, path("ignition.firing_order"));
    profile.ignition.timing_curve_triangle_radius_rad_s =
        builder.resolved(1000.0, path("ignition.timing_curve_triangle_radius_rad_s"));
    const auto make_timing = [&](std::string id, double angular_speed_rad_s) {
        const auto base = path("ignition.timing_curve." + id);
        return LegacyTimingPoint{
            builder.resolved(id, base + ".sample_id"),
            builder.resolved(angular_speed_rad_s, base + ".angular_speed_rad_s"),
            builder.resolved(0.1, base + ".timing_advance_rad"),
        };
    };
    profile.ignition.timing_curve = {
        make_timing("rpm-0", 0.0),
        make_timing("rpm-1000", 1000.0),
    };
    profile.ignition.limiter_speed_rpm =
        builder.resolved(8000.0, path("ignition.limiter_speed_rpm"));
    profile.ignition.limiter_hold_s =
        builder.resolved(0.5, path("ignition.limiter_hold_s"));
    profile.ignition.declared_redline_rpm =
        builder.resolved(7000.0, path("ignition.declared_redline_rpm"));

    profile.fuel.fuel_id =
        builder.resolved(std::string{"gasoline"}, path("fuel.fuel_id"));
    profile.fuel.molecular_mass_kg_per_mol =
        builder.resolved(0.1, path("fuel.molecular_mass_kg_per_mol"));
    profile.fuel.energy_density_j_per_kg =
        builder.resolved(48.1e6, path("fuel.energy_density_j_per_kg"));
    profile.fuel.molecular_afr = builder.resolved(12.5, path("fuel.molecular_afr"));
    profile.fuel.maximum_burning_efficiency_01 =
        builder.resolved(0.8, path("fuel.maximum_burning_efficiency_01"));
    profile.fuel.burning_efficiency_randomness_01 =
        builder.resolved(0.5, path("fuel.burning_efficiency_randomness_01"));
    profile.fuel.low_efficiency_attenuation_01 =
        builder.resolved(0.6, path("fuel.low_efficiency_attenuation_01"));
    profile.fuel.maximum_turbulence_effect =
        builder.resolved(4.0, path("fuel.maximum_turbulence_effect"));
    profile.fuel.maximum_dilution_effect =
        builder.resolved(10.0, path("fuel.maximum_dilution_effect"));
    profile.fuel.lbv_multiplier = builder.resolved(1.0, path("fuel.lbv_multiplier"));
    profile.fuel.compression_ignition_enabled =
        builder.resolved(false, path("fuel.compression_ignition_enabled"));
    profile.fuel.turbulence_to_flame_speed_ratio_triangle_radius = builder.resolved(
        5.0, path("fuel.turbulence_to_flame_speed_ratio_triangle_radius"));
    const auto make_flame_point = [&](std::string id, double turbulence, double ratio) {
        const auto base = path("fuel.turbulence_to_flame_speed_ratio." + id);
        return LegacyFlameSpeedPoint{
            builder.resolved(id, base + ".sample_id"),
            builder.resolved(turbulence, base + ".turbulence"),
            builder.resolved(ratio, base + ".flame_speed_ratio"),
        };
    };
    profile.fuel.turbulence_to_flame_speed_ratio = {
        make_flame_point("turbulence-0", 0.0, 1.0),
        make_flame_point("turbulence-1", 1.0, 2.0),
    };

    const auto combustion_stream_path = path("combustion_random_streams.cylinder-1");
    profile.combustion_random_streams.push_back({
        CylinderId{1},
        builder.resolved<std::uint64_t>(UINT64_C(0x6ba3d060370e05fa),
                                        combustion_stream_path +
                                            ".pcg32_initial_state"),
        builder.resolved<std::uint64_t>(UINT64_C(0x3e13b1e68ef2f790),
                                        combustion_stream_path + ".pcg32_stream"),
    });

    profile.losses.included_terms =
        builder.resolved(known_torque_term_mask(), path("losses.included_terms"));
    profile.losses.omitted_terms =
        builder.resolved<TorqueTermMask>(0, path("losses.omitted_terms"));

    profile.excitation.reference_atmosphere_pa_abs = builder.resolved(
        101325.0, path("reference_excitation.reference_atmosphere_pa_abs"));
    profile.excitation.legacy_propagation_speed_m_s = builder.resolved(
        343.0, path("reference_excitation.legacy_propagation_speed_m_s"));
    profile.excitation.excitation_scale =
        builder.resolved(1600.0, path("reference_excitation.excitation_scale"));
    profile.excitation.filtered_speed_threshold_rpm = builder.resolved(
        40.0, path("reference_excitation.filtered_speed_threshold_rpm"));
    profile.excitation.filtered_speed_exponent = builder.resolved<std::uint32_t>(
        3, path("reference_excitation.filtered_speed_exponent"));
    profile.excitation.pressure_gains = {
        builder.resolved(1.0, path("reference_excitation.pressure_gains.gauge_static")),
        builder.resolved(0.1,
                         path("reference_excitation.pressure_gains.dynamic_forward")),
        builder.resolved(0.1,
                         path("reference_excitation.pressure_gains.dynamic_reverse")),
    };
    profile.excitation.cylinder_count_divisor =
        builder.resolved(1.0, path("reference_excitation.cylinder_count_divisor"));
    profile.excitation.inverse_length_exponent =
        builder.resolved(2.0, path("reference_excitation.inverse_length_exponent"));
    profile.excitation.delay_rate = builder.resolved(
        RationalRateHz{10000, 1}, path("reference_excitation.delay_rate"));
    profile.excitation.cylinder_accumulation_order =
        builder.resolved(std::vector<CylinderId>{CylinderId{1}},
                         path("reference_excitation.cylinder_accumulation_order"));
    profile.excitation.cylinder_paths.push_back({
        CylinderId{1},
        RouteId{1},
        builder.resolved(0.0, path("reference_excitation.cylinder_paths.cylinder-1."
                                   "header_primary_length_m")),
        builder.resolved(1.0, path("reference_excitation.cylinder_paths.cylinder-1."
                                   "sound_attenuation_linear")),
        builder.derived<std::uint32_t>(
            182,
            path("reference_excitation.cylinder_paths.cylinder-1."
                 "resolved_delay_samples"),
            {
                path("reference_excitation.cylinder_paths.cylinder-1."
                     "header_primary_length_m"),
                path("reference_excitation.routes.exhaust.outlet-1."
                     "exhaust_system_length_m"),
                path("reference_excitation.legacy_propagation_speed_m_s"),
                path("reference_excitation.delay_rate"),
            }),
    });
    profile.excitation.routes.push_back({
        RouteId{1},
        builder.resolved(6.25, path("reference_excitation.routes.exhaust.outlet-1."
                                    "exhaust_system_length_m")),
        builder.resolved(1.0, path("reference_excitation.routes.exhaust.outlet-1."
                                   "audio_volume_linear")),
    });
    return profile;
}

inline EngineSpec make_engine(InputBuilder &builder) {
    EngineSpec spec;
    spec.schema_version = 1;
    spec.id = EngineId{1};
    spec.engine_id = builder.resolved(std::string{"bmw-m52b28"}, "engine.engine_id");
    spec.profile_id =
        builder.resolved(std::string{"bmw-m52b28-contract-test"}, "engine.profile_id");
    spec.display_name =
        builder.resolved(std::string{"BMW M52B28"}, "engine.display_name");
    spec.cycle = builder.resolved(EngineCycle::four_stroke, "engine.cycle");
    spec.ignition = builder.resolved(IgnitionKind::spark_ignition, "engine.ignition");
    spec.cylinder_layout =
        builder.resolved(CylinderLayoutKind::inline_engine, "engine.cylinder_layout");

    constexpr double bore_m = 0.084;
    constexpr double stroke_m = 0.084;
    const double displacement_m3 = std::acos(-1.0) * bore_m * bore_m * stroke_m / 4.0;
    const double piston_area_m2 = std::acos(-1.0) * bore_m * bore_m / 4.0;
    const double clearance_volume_m3 =
        0.000046 + piston_area_m2 * (0.211 - (0.5 * stroke_m + 0.135) - 0.03182);
    spec.total_displacement_m3 =
        builder.resolved(displacement_m3, "engine.total_displacement_m3");
    spec.banks.push_back({
        BankId{1},
        builder.resolved(std::string{"bank-1"}, "engine.banks.bank-1.semantic_id"),
    });
    spec.cylinders.push_back({
        CylinderId{1},
        builder.resolved(std::string{"cylinder-1"},
                         "engine.cylinders.cylinder-1.semantic_id"),
        BankId{1},
        builder.resolved(bore_m, "engine.cylinders.cylinder-1.bore_m"),
        builder.resolved(stroke_m, "engine.cylinders.cylinder-1.stroke_m"),
        builder.resolved(0.135, "engine.cylinders.cylinder-1.connecting_rod_length_m"),
        builder.resolved((clearance_volume_m3 + displacement_m3) / clearance_volume_m3,
                         "engine.cylinders.cylinder-1.compression_ratio"),
        builder.resolved(0.0, "engine.cylinders.cylinder-1.firing_tdc_offset_rad"),
        builder.resolved(0.0, "engine.cylinders.cylinder-1.journal_phase_rad"),
    });
    spec.ports = {
        {
            PortId{1},
            builder.resolved(std::string{"intake-port-1"},
                             "engine.ports.intake-port-1.semantic_id"),
            CylinderId{1},
            builder.resolved(PortKind::intake, "engine.ports.intake-port-1.kind"),
        },
        {
            PortId{2},
            builder.resolved(std::string{"exhaust-port-1"},
                             "engine.ports.exhaust-port-1.semantic_id"),
            CylinderId{1},
            builder.resolved(PortKind::exhaust, "engine.ports.exhaust-port-1.kind"),
        },
    };
    const auto add_volume = [&](std::uint32_t id, std::string semantic_id,
                                GasVolumeKind kind) {
        const auto path = "engine.gas_volumes." + semantic_id;
        spec.gas_volumes.push_back({
            GasVolumeId{id},
            builder.resolved(semantic_id, path + ".semantic_id"),
            builder.resolved(kind, path + ".kind"),
        });
    };
    add_volume(1, "intake-plenum", GasVolumeKind::intake_plenum);
    add_volume(2, "intake-runner-1", GasVolumeKind::intake_runner);
    add_volume(3, "cylinder-volume-1", GasVolumeKind::cylinder);
    add_volume(4, "exhaust-primary-1", GasVolumeKind::exhaust_primary);
    add_volume(5, "exhaust-collector-1", GasVolumeKind::exhaust_collector);
    add_volume(6, "atmosphere", GasVolumeKind::atmosphere);

    const auto add_edge = [&](std::uint32_t id, std::string semantic_id,
                              std::uint32_t endpoint_0, std::uint32_t endpoint_1) {
        spec.flow_edges.push_back({
            FlowEdgeId{id},
            builder.resolved(semantic_id,
                             "engine.flow_edges." + semantic_id + ".semantic_id"),
            GasVolumeId{endpoint_0},
            GasVolumeId{endpoint_1},
        });
    };
    add_edge(1, "main-throttle-edge", 6, 1);
    add_edge(2, "idle-bypass-edge", 6, 1);
    add_edge(3, "plenum-runner-edge-1", 1, 2);
    add_edge(4, "intake-valve-edge-1", 2, 3);
    add_edge(5, "exhaust-valve-edge-1", 3, 4);
    add_edge(6, "primary-collector-edge-1", 4, 5);
    add_edge(7, "blowby-edge-1", 3, 6);
    add_edge(8, "collector-outlet-edge-1", 6, 5);

    spec.routes.push_back({
        RouteId{1},
        builder.resolved(std::string{"exhaust.outlet-1"},
                         "engine.routes.exhaust.outlet-1.semantic_id"),
        builder.resolved(SourceRouteKind::exhaust_outlet,
                         "engine.routes.exhaust.outlet-1.kind"),
        GasVolumeId{5},
        std::nullopt,
        std::nullopt,
    });
    const auto resolve_method = [&](std::string id, std::uint8_t byte,
                                    std::string field) {
        return builder.resolved(method(std::move(id), byte), "engine.methods." + field);
    };
    spec.methods = {
        resolve_method("legacy_low_order_v1", 2, "mechanism"),
        resolve_method("legacy_low_order_v1", 3, "valvetrain"),
        resolve_method("legacy_low_order_v1", 4, "gas_exchange"),
        resolve_method("legacy_low_order_v1", 5, "ignition"),
        resolve_method("legacy_low_order_v1", 6, "combustion"),
        resolve_method("legacy_low_order_v1", 7, "heat_transfer"),
        resolve_method("legacy_low_order_v1", 8, "losses"),
        resolve_method("legacy_low_order_v1", 9, "excitation"),
    };
    spec.physics_profile = make_physics_profile(builder);
    spec.torque_capability = builder.resolved(
        TorqueCapability{
            true,
            true,
            true,
            known_torque_term_mask(),
            0,
        },
        "engine.torque_capability");
    spec.provenance_schema_id = builder.provenance.schema_id;
    return spec;
}

inline PresentationCalibration make_presentation(InputBuilder &builder,
                                                 const EngineSpec &engine) {
    PresentationCalibration presentation;
    presentation.schema_version = 1;
    presentation.calibration_id = "contract-test-presentation-v1";
    presentation.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id");
    const auto resolved_method = [&](std::string id, std::uint8_t byte,
                                     std::string path) {
        return builder.resolved(method(std::move(id), byte),
                                "presentation.methods." + path);
    };
    presentation.methods = {
        resolved_method("reconstruction-v1", 20, "reconstruction"),
        resolved_method("conditioning-v1", 21, "conditioning"),
        resolved_method("ir-conversion-v1", 22, "impulse_response_conversion"),
        resolved_method("convolution-v1", 23, "convolution"),
        resolved_method("publication-v1", 24, "publication"),
        resolved_method("audition-mix-v1", 25, "audition_mix"),
    };
    presentation.algorithm_record = {
        builder.resolved(std::string{"contract-test-presentation-record-v1"},
                         "presentation.algorithm_record.semantic_id"),
        builder.resolved(std::string{"test-presentation-record-source"},
                         "presentation.algorithm_record.evidence_source_id"),
        builder.resolved(digest(29), "presentation.algorithm_record.content_sha256"),
    };
    presentation.conditioning = {
        builder.resolved(0.5, "presentation.conditioning.jitter_scale"),
        builder.resolved(10000.0,
                         "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(0.01, "presentation.conditioning.derivative_mix_01"),
        builder.resolved(1.0, "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(2000.0, "presentation.conditioning.air_noise_cutoff_hz"),
    };
    presentation.assets.push_back({
        AudioAssetId{1},
        builder.resolved(std::string{"test-ir"},
                         "presentation.assets.test-ir.semantic_id"),
        builder.resolved(std::string{"test-ir-source"},
                         "presentation.assets.test-ir.evidence_source_id"),
        builder.resolved(digest(30), "presentation.assets.test-ir.content_sha256"),
        builder.resolved(
            AudioMediaContract{
                AudioSampleEncoding::pcm_s16le,
                AudioChannelLayout::mono,
                {48000, 1},
                128,
            },
            "presentation.assets.test-ir.media"),
    });
    presentation.routes.push_back({
        RouteId{1},
        AudioAssetId{1},
        builder.resolved(0.001, "presentation.routes.exhaust.outlet-1."
                                "impulse_response_gain_linear"),
        builder.resolved(1.0, "presentation.routes.exhaust.outlet-1.wet_mix_01"),
    });
    presentation.publication.calibration_gain_linear =
        builder.resolved(0x1.0p-26, "presentation.publication.calibration_gain_linear");
    presentation.audition = {
        builder.resolved(std::vector<RouteId>{RouteId{1}},
                         "presentation.audition.selected_routes"),
        builder.resolved(1.0, "presentation.audition.monitoring_gain_linear"),
        builder.resolved(0.02, "presentation.audition.fade_in_duration_s"),
        builder.resolved(0.02, "presentation.audition.fade_out_duration_s"),
    };
    presentation.provenance_schema_id = builder.provenance.schema_id;
    return presentation;
}

inline RenderScenario make_scenario(InputBuilder &builder, const EngineSpec &engine) {
    RenderScenario scenario;
    scenario.schema_version = 1;
    scenario.scenario_id = "held-speed-smoke";
    scenario.engine_profile_id = engine.profile_id.value;
    scenario.ambient = {
        builder.resolved(101325.0, "scenario.ambient.pressure_pa_abs"),
        builder.resolved(293.15, "scenario.ambient.temperature_k"),
        builder.resolved(0.5, "scenario.ambient.relative_humidity_01"),
    };
    scenario.fuel = {
        builder.resolved(std::string{"gasoline"}, "scenario.fuel.fuel_id"),
        builder.resolved(48.1e6, "scenario.fuel.lower_heating_value_j_per_kg"),
        builder.resolved(14.1, "scenario.fuel.stoichiometric_air_fuel_mass_ratio"),
    };
    scenario.initial_thermal_state = {
        builder.resolved(350.0, "scenario.initial_thermal_state.gas_temperature_k"),
        builder.resolved(360.0, "scenario.initial_thermal_state.wall_temperature_k"),
        builder.resolved(360.0, "scenario.initial_thermal_state.coolant_temperature_k"),
        builder.resolved(370.0, "scenario.initial_thermal_state.oil_temperature_k"),
    };
    scenario.crankcase = {
        builder.resolved(101325.0, "scenario.crankcase.pressure_pa_abs"),
        builder.resolved(293.15, "scenario.crankcase.temperature_k"),
    };
    scenario.preparation = FixedSettling{
        builder.resolved(1.0, "scenario.preparation.warm_up_duration_s"),
        builder.resolved(1.0, "scenario.preparation.settling_duration_s"),
    };
    scenario.operating_state = builder.resolved(
        std::vector<OperatingStatePoint>{
            {
                "fired",
                0.0,
                OperatingState{true, true, false, true, true},
            },
        },
        "scenario.operating_state");
    scenario.total_duration_s = builder.resolved(3.0, "scenario.total_duration_s");
    scenario.audible_start_s = builder.resolved(2.0, "scenario.audible_start_s");
    scenario.audible_duration_s = builder.resolved(1.0, "scenario.audible_duration_s");
    scenario.rates = {
        {10000, 1}, {10000, 1}, {192000, 1}, {192000, 1}, {192000, 1},
    };
    scenario.rates_resolution_id = builder.add_resolution("scenario.rates");
    scenario.quality = builder.resolved(RenderQuality{"production-v1", 1, 256, 4096},
                                        "scenario.quality");
    scenario.public_seed =
        builder.resolved<std::uint64_t>(12648430, "scenario.public_seed");
    scenario.mode = HeldSpeed{
        builder.resolved(3000.0, "scenario.mode.engine_speed_rpm"),
        builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        builder.resolved(0.85, "scenario.mode.throttle_01"),
    };
    scenario.mode_resolution_id = builder.add_resolution("scenario.mode.kind");
    scenario.provenance_schema_id = builder.provenance.schema_id;
    return scenario;
}

inline SourceMatrixContract make_source_matrix() {
    const AudioContract audio{
        {192000, 1},
        192000,
        "mono",
        "float32le",
    };
    return {
        "contract-test-source-matrix-v1",
        digest(40),
        DistributionIntent::local_evaluation,
        {
            {
                "exhaust.outlet-1",
                SourceRouteKind::exhaust_outlet,
                RouteDisposition::rendered,
                "",
                {"exhaust.outlet-1.selected"},
            },
        },
        {
            {
                "master.engine.raw",
                OutputBusKind::master_engine_raw,
                {"master.engine.raw"},
            },
        },
        {
            {
                "exhaust.outlet-1.selected",
                ArtifactKind::audio,
                audio,
                false,
            },
            {
                "master.engine.raw",
                ArtifactKind::audio,
                audio,
                false,
            },
        },
        {
            {
                "intake",
                OmissionKind::source_route,
                "Not part of this one-route contract fixture.",
            },
        },
    };
}

inline ResolvedRenderInputs &simulation_inputs(RenderManifestContent &content) {
    return content.inputs.resolved;
}

inline const ResolvedRenderInputs &
simulation_inputs(const RenderManifestContent &content) {
    return content.inputs.resolved;
}

inline RenderManifestContent make_manifest_content(InputBuilder &builder) {
    const auto engine = make_engine(builder);
    const auto presentation = make_presentation(builder, engine);
    const auto scenario = make_scenario(builder, engine);
    const auto source_matrix = make_source_matrix();

    RenderManifestContent content;
    content.schema_version = 2;
    content.inputs =
        SimulationManifestInputs{ResolvedRenderInputs{engine, presentation, scenario}};
    content.provenance = builder.provenance.bundle;
    content.determinism = {
        BuildIdentity{
            "0123456789abcdef0123456789abcdef01234567",
            digest(12),
            "Clang",
            "21.1.8",
            "x86_64-pc-linux-gnu",
            "libstdcxx",
            test_standard_library_identity(),
            "glibc-libm",
            test_math_library_identity(),
            "libgcc-s",
            test_compiler_runtime_identity(),
        },
        "x86-64-v1-binary64-x87-extended-strict-v1",
        "x86-64-v1",
        FloatingPointIdentity{
            "ieee754_binary64",
            "nearest_ties_to_even",
            false,
            false,
            false,
        },
        1,
        "serial-stable-order",
    };
    content.rates = scenario.rates;
    content.randomness = {
        method("pcg32-v1", 14),
        scenario.public_seed.value,
        method("domain-seed-v1", 15),
        {
            ComponentSeed{
                RandomComponentKind::combustion,
                CylinderId{1},
                std::nullopt,
                100,
                200,
            },
            ComponentSeed{
                RandomComponentKind::presentation_jitter,
                std::nullopt,
                RouteId{1},
                123,
                456,
            },
            ComponentSeed{
                RandomComponentKind::presentation_air_noise,
                std::nullopt,
                RouteId{1},
                789,
                321,
            },
        },
    };
    content.output_contract = resolve_output_contract(source_matrix);
    content.routes = {
        RouteRecord{
            RouteId{1},
            "exhaust.outlet-1",
            SourceRouteKind::exhaust_outlet,
            RouteDisposition::rendered,
            "",
            {"exhaust.outlet-1.selected"},
        },
    };
    content.output_buses = {
        OutputBusRecord{
            "master.engine.raw",
            OutputBusKind::master_engine_raw,
            {"master.engine.raw"},
        },
    };
    const AudioContract audio{
        {192000, 1},
        192000,
        "mono",
        "float32le",
    };
    content.artifacts = {
        ArtifactRecord{
            "exhaust.outlet-1.selected",
            ArtifactKind::audio,
            "audio/exhaust-outlet-1.wav",
            audio,
            768044,
            digest(17),
            false,
        },
        ArtifactRecord{
            "master.engine.raw",
            ArtifactKind::audio,
            "audio/master-engine-raw.wav",
            audio,
            768044,
            digest(18),
            false,
        },
    };
    return content;
}

void run_primitives_contract_tests();
void run_authored_profile_contract_tests();
void run_parity_model_contract_tests();
void run_capture_contract_tests();
void run_scenario_manifest_contract_tests();

} // namespace engine_sim_offline::contract::test
