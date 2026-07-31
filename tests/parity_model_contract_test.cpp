#include "contract_test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace engine_sim_offline::contract::test {
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

LegacySampledCamShape make_sampled_cam_shape(InputBuilder &builder,
                                             const LegacyHarmonicCamShape &harmonic,
                                             std::string_view role) {
    const auto base =
        std::string{"engine.physics.low-order-operating-point-v1.valvetrain."} +
        std::string{role} + ".shape";
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

} // namespace

void run_parity_model_contract_tests() {
    InputBuilder valid_builder;
    const auto valid_engine = make_engine(valid_builder);
    expect(validate(valid_engine, valid_builder.provenance).ok(),
           "valid low-order operating-point engine was rejected");

    InputBuilder deterministic_builder;
    auto deterministic_engine = make_engine(deterministic_builder);
    operating_profile(deterministic_engine)
        .core.fuel.burning_efficiency_randomness_01.value = 0.0;
    expect(validate(deterministic_engine, deterministic_builder.provenance).ok(),
           "zero burning-efficiency variation invalidated the engine profile");

    InputBuilder sampled_builder;
    auto sampled_engine = make_engine(sampled_builder);
    auto &sampled_intake = operating_profile(sampled_engine).core.valvetrain.intake;
    sampled_intake.shape = make_sampled_cam_shape(
        sampled_builder, std::get<LegacyHarmonicCamShape>(sampled_intake.shape),
        "intake");
    expect(validate(sampled_engine, sampled_builder.provenance).ok(),
           "valid sampled fixed cam shape was rejected");

    expect_parity_mutation_rejected(
        "nonpositive sampled cam radius was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            std::get<LegacySampledCamShape>(intake.shape).triangle_radius_rad.value =
                0.0;
        });

    expect_parity_mutation_rejected(
        "undersized sampled cam table was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            std::get<LegacySampledCamShape>(intake.shape).samples.resize(1);
        });

    expect_parity_mutation_rejected(
        "noncanonical sampled cam point identity was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            std::get<LegacySampledCamShape>(intake.shape)
                .samples.front()
                .sample_id.value = "Not Canonical";
        });

    expect_parity_mutation_rejected(
        "duplicate sampled cam point identity was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            auto &samples = std::get<LegacySampledCamShape>(intake.shape).samples;
            samples[1].sample_id.value = samples[0].sample_id.value;
        });

    expect_parity_mutation_rejected(
        "unordered sampled cam angles were accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            std::get<LegacySampledCamShape>(intake.shape).samples[1].angle_rad.value =
                -1.0;
        });

    expect_parity_mutation_rejected(
        "negative sampled cam lift was accepted",
        [](EngineSpec &engine, InputBuilder &builder) {
            auto &intake = operating_profile(engine).core.valvetrain.intake;
            intake.shape = make_sampled_cam_shape(
                builder, std::get<LegacyHarmonicCamShape>(intake.shape), "intake");
            std::get<LegacySampledCamShape>(intake.shape).samples[1].lift_m.value =
                -0.001;
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
        "stale propagation delay survived a path-length calculation",
        [](EngineSpec &engine, InputBuilder &) {
            ++operating_profile(engine)
                  .core.excitation.cylinder_paths.front()
                  .resolved_delay_samples.value;
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
                    .core.gas_path.intake.main_throttle.resolved_k.resolution_id;
            falsely_mark_authored(builder, resolution_id);
        });

    expect_parity_mutation_rejected(
        "derived propagation delay was accepted as authored",
        [](EngineSpec &engine, InputBuilder &builder) {
            const auto &resolution_id = operating_profile(engine)
                                            .core.excitation.cylinder_paths.front()
                                            .resolved_delay_samples.resolution_id;
            falsely_mark_authored(builder, resolution_id);
        });
}

} // namespace engine_sim_offline::contract::test
