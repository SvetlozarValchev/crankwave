#include "contract_test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace crankwave::contract::test {
namespace {

template <class Mutation>
void expect_parity_mutation_rejected(const char *message, Mutation &&mutation) {
    InputBuilder builder;
    auto engine = make_engine(builder);
    std::forward<Mutation>(mutation)(engine, builder);
    expect(!validate(engine, builder.provenance).ok(), message);
}

void falsely_mark_authored(InputBuilder &builder, const std::string &resolution_id) {
    const auto resolution = std::ranges::find(builder.provenance.resolutions,
                                              resolution_id, &ResolutionRecord::id);
    expect(resolution != builder.provenance.resolutions.end(),
           "test fixture resolution was not found");
    resolution->mode = ResolutionMode::authored;
    resolution->claim_id = "claim";
    resolution->method.reset();
    resolution->dependency_parameter_paths.clear();
}

LowOrderOperatingPointV1Profile &operating_profile(EngineSpec &engine) {
    return std::get<LowOrderOperatingPointV1Profile>(engine.physics_profile);
}

const LowOrderOperatingPointV1Profile &operating_profile(const EngineSpec &engine) {
    return std::get<LowOrderOperatingPointV1Profile>(engine.physics_profile);
}

LegacyCamShape &only_cam_profile(LegacyCamshaftProfile &camshaft) {
    expect(camshaft.profiles.size() == 1U,
           "fixture camshaft did not contain exactly one profile");
    return camshaft.profiles.front();
}

const LegacyCamShape &only_cam_profile(const LegacyCamshaftProfile &camshaft) {
    expect(camshaft.profiles.size() == 1U,
           "fixture camshaft did not contain exactly one profile");
    return camshaft.profiles.front();
}

LegacySampledCamShape make_sampled_cam_shape(InputBuilder &builder,
                                             const LegacyHarmonicCamShape &harmonic,
                                             std::string_view role) {
    const auto base =
        std::string{"engine.physics.low-order-operating-point-v1.valvetrain."} +
        std::string{role} + ".profiles.profile-0.shape";
    const auto point = [&](std::string id, double angle_rad, double lift_m) {
        const auto path = base + ".samples." + id;
        return LegacySampledCamPoint{
            builder.resolved(id, path + ".sample_id"),
            builder.resolved(angle_rad, path + ".angle_rad"),
            builder.resolved(lift_m, path + ".lift_m"),
        };
    };
    return {
        builder.resolved(0.01, base + ".triangle_radius_rad"),
        {
            point("opening", -1.0, 0.0),
            point("peak", 0.0, 0.009),
            point("closing", 1.0, 0.0),
        },
        harmonic.advance_rad,
        harmonic.base_radius_m,
    };
}

LegacyCamshaftProfile make_vtec_camshaft(InputBuilder &builder,
                                         const LegacyCamshaftProfile &source,
                                         std::string_view role, double maximum_lift_m) {
    const auto base =
        std::string{"engine.physics.low-order-operating-point-v1.valvetrain."
                    "alternate."} +
        std::string{role};
    const auto profile_base = base + ".profiles.profile-0";
    const auto &shape = std::get<LegacyHarmonicCamShape>(only_cam_profile(source));
    LegacyCamshaftProfile result;
    result.profiles.push_back(LegacyHarmonicCamShape{
        builder.resolved(maximum_lift_m, profile_base + ".shape.maximum_lift_m"),
        builder.resolved(shape.duration_at_reference_lift_rad.value,
                         profile_base + ".shape.duration_at_reference_lift_rad"),
        builder.resolved(shape.exponent.value, profile_base + ".shape.exponent"),
        builder.resolved(shape.construction_steps.value,
                         profile_base + ".shape.construction_steps"),
        builder.resolved(shape.advance_rad.value, profile_base + ".shape.advance_rad"),
        builder.resolved(shape.base_radius_m.value,
                         profile_base + ".shape.base_radius_m"),
    });
    for (const auto &lobe : source.lobes) {
        result.lobes.push_back({
            lobe.cylinder_id,
            lobe.port_id,
            lobe.profile_index,
            builder.resolved(lobe.crank_center_rad.value,
                             base + ".lobes.cylinder-1.crank_center_rad"),
        });
    }
    return result;
}

LegacyVtecAlternateCamProfile
make_vtec_alternate(InputBuilder &builder, const LegacyValvetrainProfile &valvetrain) {
    constexpr std::string_view kActivationBase =
        "engine.physics.low-order-operating-point-v1.valvetrain.alternate.selectors."
        "selector-0.activation";
    return {
        make_vtec_camshaft(builder, valvetrain.intake, "intake", 0.0115),
        make_vtec_camshaft(builder, valvetrain.exhaust, "exhaust", 0.0105),
        {{
            BankId{1},
            {
                builder.resolved(607.3745796940267, std::string{kActivationBase} +
                                                        ".minimum_engine_speed_rad_s"),
                builder.resolved(84393.05666666667,
                                 std::string{kActivationBase} +
                                     ".minimum_mean_manifold_pressure_pa_abs"),
                builder.resolved(0.3, std::string{kActivationBase} +
                                          ".minimum_throttle_linkage_opening_01"),
            },
        }},
    };
}

} // namespace

void run_parity_model_contract_tests() {
    InputBuilder valid_builder;
    const auto valid_engine = make_engine(valid_builder);
    expect(validate(valid_engine, valid_builder.provenance).ok(),
           "valid low-order operating-point engine was rejected");
    const auto &valid_mechanism = operating_profile(valid_engine).core.mechanism;
    expect(find_crank(valid_mechanism, CrankshaftId{1}) ==
               &valid_mechanism.cranks.front(),
           "crankshaft lookup did not return the requested crank");
    expect(find_output_crank(valid_mechanism) == &valid_mechanism.cranks.front(),
           "output crankshaft lookup did not return the explicit output crank");
    expect(find_crank(valid_mechanism, CrankshaftId{}) == nullptr &&
               find_crank(valid_mechanism, CrankshaftId{2}) == nullptr,
           "crankshaft lookup accepted an invalid or absent ID");
    auto malformed_lookup_mechanism = valid_mechanism;
    malformed_lookup_mechanism.cranks.push_back(
        malformed_lookup_mechanism.cranks.front());
    expect(find_crank(malformed_lookup_mechanism, CrankshaftId{1}) == nullptr &&
               find_output_crank(malformed_lookup_mechanism) == nullptr,
           "crankshaft lookup accepted duplicate mechanism identities");

    expect_parity_mutation_rejected(
        "resolved engine accepted an empty crankshaft set",
        [](EngineSpec &engine, InputBuilder &) { engine.crankshafts.clear(); });
    expect_parity_mutation_rejected(
        "resolved engine accepted an invalid output crankshaft",
        [](EngineSpec &engine, InputBuilder &) {
            engine.output_crankshaft_id = CrankshaftId{};
        });
    expect_parity_mutation_rejected(
        "resolved profile accepted an empty crankshaft set",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.mechanism.cranks.clear();
        });
    expect_parity_mutation_rejected(
        "resolved profile accepted duplicate crankshaft identities",
        [](EngineSpec &engine, InputBuilder &) {
            auto &cranks = operating_profile(engine).core.mechanism.cranks;
            cranks.push_back(cranks.front());
        });
    expect_parity_mutation_rejected(
        "resolved profile output crankshaft drifted from EngineSpec",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.mechanism.output_crankshaft_id =
                CrankshaftId{2};
        });
    expect_parity_mutation_rejected(
        "resolved cylinder accepted an undeclared crankshaft",
        [](EngineSpec &engine, InputBuilder &) {
            engine.cylinders.front().crankshaft_id = CrankshaftId{2};
        });
    expect_parity_mutation_rejected(
        "mechanism cylinder crankshaft drifted from EngineSpec",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.mechanism.cylinders.front()
                .topology.crankshaft_id = CrankshaftId{2};
        });

    InputBuilder ordered_crank_builder;
    auto ordered_crank_engine = make_engine(ordered_crank_builder);
    ordered_crank_engine.crankshafts.push_back({
        CrankshaftId{2},
        ordered_crank_builder.resolved(
            std::string{"secondary-crank"},
            "engine.crankshafts.secondary-crank.semantic_id"),
    });
    const auto &source_crank =
        operating_profile(ordered_crank_engine).core.mechanism.cranks.front();
    constexpr std::string_view kSecondaryCrankBase =
        "engine.physics.low-order-operating-point-v1.mechanism.cranks."
        "secondary-crank";
    const auto secondary_crank_path = [=](std::string_view field) {
        return std::string{kSecondaryCrankBase} + "." + std::string{field};
    };
    operating_profile(ordered_crank_engine)
        .core.mechanism.cranks.push_back({
            CrankshaftId{2},
            ordered_crank_builder.resolved(
                source_crank.crank_tdc_reference_rad.value,
                secondary_crank_path("crank_tdc_reference_rad")),
            ordered_crank_builder.resolved(source_crank.crankshaft_mass_kg.value,
                                           secondary_crank_path("crankshaft_mass_kg")),
            ordered_crank_builder.resolved(source_crank.flywheel_mass_kg.value,
                                           secondary_crank_path("flywheel_mass_kg")),
            ordered_crank_builder.resolved(
                source_crank.authored_crank_inertia_kg_m2.value,
                secondary_crank_path("authored_crank_inertia_kg_m2")),
            ordered_crank_builder.resolved(
                source_crank.running_friction_torque_magnitude_nm.value,
                secondary_crank_path("running_friction_torque_magnitude_nm")),
        });
    expect(validate(ordered_crank_engine, ordered_crank_builder.provenance).ok(),
           "matching resolved multi-crank coverage was rejected");
    auto &ordered_mechanism_cranks =
        operating_profile(ordered_crank_engine).core.mechanism.cranks;
    std::ranges::swap(ordered_mechanism_cranks.front(),
                      ordered_mechanism_cranks.back());
    expect(!validate(ordered_crank_engine, ordered_crank_builder.provenance).ok(),
           "resolved profile accepted reordered crankshaft coverage");

    InputBuilder deterministic_builder;
    auto deterministic_engine = make_engine(deterministic_builder);
    operating_profile(deterministic_engine)
        .core.fuel.burning_efficiency_randomness_01.value = 0.0;
    expect(validate(deterministic_engine, deterministic_builder.provenance).ok(),
           "zero burning-efficiency variation invalidated the engine profile");

    expect_parity_mutation_rejected(
        "empty resolved cam profile pool was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.valvetrain.intake.profiles.clear();
        });
    expect_parity_mutation_rejected(
        "out-of-range resolved cam profile binding was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.valvetrain.intake.lobes.front()
                .profile_index = 1U;
        });
    expect_parity_mutation_rejected(
        "unreferenced resolved cam profile was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            auto &camshaft = operating_profile(engine).core.valvetrain.intake;
            camshaft.profiles.push_back(camshaft.profiles.front());
        });

    InputBuilder sampled_builder;
    auto sampled_engine = make_engine(sampled_builder);
    auto &sampled_intake = operating_profile(sampled_engine).core.valvetrain.intake;
    only_cam_profile(sampled_intake) = make_sampled_cam_shape(
        sampled_builder,
        std::get<LegacyHarmonicCamShape>(only_cam_profile(sampled_intake)), "intake");
    expect(validate(sampled_engine, sampled_builder.provenance).ok(),
           "valid sampled fixed cam shape was rejected");

    InputBuilder vtec_builder;
    auto vtec_engine = make_engine(vtec_builder);
    auto &vtec_valvetrain = operating_profile(vtec_engine).core.valvetrain;
    vtec_valvetrain.alternate = make_vtec_alternate(vtec_builder, vtec_valvetrain);
    expect(validate(vtec_engine, vtec_builder.provenance).ok(),
           "valid VTEC alternate cam pair and activation thresholds were rejected");

    expect_parity_mutation_rejected(
        "nonpositive VTEC manifold-pressure threshold was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &valvetrain = operating_profile(engine).core.valvetrain;
            valvetrain.alternate = make_vtec_alternate(builder, valvetrain);
            valvetrain.alternate->selectors.front()
                .activation.minimum_mean_manifold_pressure_pa_abs.value = 0.0;
        });
    expect_parity_mutation_rejected(
        "alternate VTEC cams without a bank selector were accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &valvetrain = operating_profile(engine).core.valvetrain;
            valvetrain.alternate = make_vtec_alternate(builder, valvetrain);
            valvetrain.alternate->selectors.clear();
        });
    expect_parity_mutation_rejected("duplicate VTEC bank selectors were accepted",
                                    [](EngineSpec &engine, InputBuilder &builder) {
                                        auto &valvetrain =
                                            operating_profile(engine).core.valvetrain;
                                        valvetrain.alternate =
                                            make_vtec_alternate(builder, valvetrain);
                                        valvetrain.alternate->selectors.push_back(
                                            valvetrain.alternate->selectors.front());
                                    });
    expect_parity_mutation_rejected(
        "VTEC selector for an unknown bank was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &valvetrain = operating_profile(engine).core.valvetrain;
            valvetrain.alternate = make_vtec_alternate(builder, valvetrain);
            valvetrain.alternate->selectors.front().bank_id = BankId{999U};
        });

    expect_parity_mutation_rejected(
        "nonpositive sampled cam radius was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            std::get<LegacySampledCamShape>(only_cam_profile(intake))
                .triangle_radius_rad.value = 0.0;
        });

    expect_parity_mutation_rejected(
        "undersized sampled cam table was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            std::get<LegacySampledCamShape>(only_cam_profile(intake)).samples.resize(1);
        });

    expect_parity_mutation_rejected(
        "noncanonical sampled cam point identity was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            std::get<LegacySampledCamShape>(only_cam_profile(intake))
                .samples.front()
                .sample_id.value = "Not Canonical";
        });

    expect_parity_mutation_rejected(
        "duplicate sampled cam point identity was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            auto &samples =
                std::get<LegacySampledCamShape>(only_cam_profile(intake)).samples;
            samples[1].sample_id.value = samples[0].sample_id.value;
        });

    expect_parity_mutation_rejected(
        "unordered sampled cam angles were accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            std::get<LegacySampledCamShape>(only_cam_profile(intake))
                .samples[1]
                .angle_rad.value = -1.0;
        });

    expect_parity_mutation_rejected(
        "negative sampled cam lift was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            only_cam_profile(intake) = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(only_cam_profile(intake)),
                "intake");
            std::get<LegacySampledCamShape>(only_cam_profile(intake))
                .samples[1]
                .lift_m.value = -0.001;
        });

    expect_parity_mutation_rejected("non-legacy subsystem method identity was accepted",
                                    [](EngineSpec &engine, InputBuilder &) {
                                        engine.methods.gas_exchange.value.id =
                                            "alternate_low_order_v1";
                                    });

    expect_parity_mutation_rejected(
        "operating profile accepted a false aggregate-loss term scope",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).aggregate_loss.included_terms.value =
                indicated_gas_torque_term_mask();
        });

    expect_parity_mutation_rejected(
        "operating profile accepted incomplete instantaneous torque",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.instantaneous_net_shaft = {
                Availability::available,
                Completeness::incomplete,
                indicated_gas_torque_term_mask(),
                known_torque_term_mask() & ~indicated_gas_torque_term_mask(),
            };
        });

    expect_parity_mutation_rejected(
        "operating profile accepted unavailable cycle-mean torque",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.cycle_mean_net_shaft = {
                Availability::unavailable,
                Completeness::incomplete,
                0,
                0,
            };
        });

    expect_parity_mutation_rejected(
        "operating profile accepted missing equivalent inertia",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.equivalent_inertia_available = false;
        });

    expect_parity_mutation_rejected("cylinder intake topology accepted an exhaust port",
                                    [](EngineSpec &engine, InputBuilder &) {
                                        operating_profile(engine)
                                            .core.mechanism.cylinders.front()
                                            .topology.intake_port_id = PortId{2};
                                    });

    expect_parity_mutation_rejected(
        "profile topology accepted an edge with incompatible endpoints",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.mechanism.cylinders.front()
                .topology.plenum_to_runner_edge_id = FlowEdgeId{1};
        });

    expect_parity_mutation_rejected(
        "resolved engine accepted an empty intake identity set",
        [](EngineSpec &engine, InputBuilder &) { engine.intakes.clear(); });

    expect_parity_mutation_rejected(
        "resolved intake profile identity drifted from the engine intake",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.gas_path.intakes.front().topology.intake_id =
                IntakeId{2};
        });

    expect_parity_mutation_rejected(
        "resolved cylinder accepted an undeclared intake binding",
        [](EngineSpec &engine, InputBuilder &) {
            engine.cylinders.front().intake_id = IntakeId{2};
        });

    expect_parity_mutation_rejected(
        "mechanism cylinder intake binding drifted from the resolved engine",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.mechanism.cylinders.front()
                .topology.intake_id = IntakeId{2};
        });

    expect_parity_mutation_rejected(
        "resolved profile accepted missing bank-head coverage",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.gas_path.heads.clear();
        });

    expect_parity_mutation_rejected(
        "resolved profile accepted a bank-head identity mismatch",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.gas_path.heads.front().bank_id = BankId{2};
        });

    expect_parity_mutation_rejected(
        "resolved engine accepted an unbound extra intake port",
        [](EngineSpec &engine, InputBuilder &builder) {
            engine.ports.push_back({
                PortId{3},
                builder.resolved(std::string{"extra-intake-port"},
                                 "engine.ports.extra-intake-port.semantic_id"),
                CylinderId{1},
                builder.resolved(PortKind::intake,
                                 "engine.ports.extra-intake-port.kind"),
            });
        });

    expect_parity_mutation_rejected(
        "physics profile geometry drifted from the resolved engine",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.mechanism.cylinders.front()
                .parameters.bore_m.value += 0.001;
        });

    expect_parity_mutation_rejected(
        "nonpositive resolved flame-speed table radius was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.value = 0.0;
        });

    expect_parity_mutation_rejected(
        "unresolved flame-speed table radius was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.resolution_id
                .clear();
        });

    expect_parity_mutation_rejected(
        "duplicate cylinder accumulation entry was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine)
                .core.excitation.cylinder_accumulation_order.value = {CylinderId{1},
                                                                      CylinderId{1}};
        });

    expect_parity_mutation_rejected(
        "incomplete excitation route set was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            operating_profile(engine).core.excitation.routes.clear();
        });

    expect_parity_mutation_rejected(
        "derived restriction coefficient was accepted as authored",
        [](EngineSpec &engine, InputBuilder &builder) {
            const auto &resolution_id =
                operating_profile(engine)
                    .core.gas_path.intakes.front()
                    .parameters.main_throttle.resolved_k.resolution_id;
            falsely_mark_authored(builder, resolution_id);
        });
}

} // namespace crankwave::contract::test
