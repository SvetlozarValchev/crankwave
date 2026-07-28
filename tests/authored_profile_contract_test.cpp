#include "contract_test_support.hpp"

#include <limits>
#include <numbers>
#include <string>
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
    core.fuel.compression_ignition_enabled = authored(false);
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

    fixed_crank_loss.included_terms = authored(known_torque_term_mask());
    fixed_crank_loss.omitted_terms = authored<TorqueTermMask>(0);

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

ProvenanceLedger make_provenance() {
    ProvenanceLedger ledger;
    ledger.schema_id = "authored-profile-test-provenance-v1";
    ledger.bundle = {"authored-profile-test-provenance-v1", digest(41)};
    ledger.claims.push_back({
        "claim",
        ProvenanceOrigin::artistic,
        {},
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

template <class Mutation>
void expect_authored_mutation_rejected(const char *message, Mutation &&mutation) {
    auto profile = make_authored_profile();
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
        "compression ignition was enabled in a spark-ignition authored profile",
        [](AuthoredLegacyLowOrderV1Profile &profile) {
            profile.core.fuel.compression_ignition_enabled.value = true;
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
