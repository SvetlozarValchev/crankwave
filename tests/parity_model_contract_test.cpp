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

LegacyLowOrderV1Profile &legacy_profile(EngineSpec &engine) {
    return std::get<LegacyLowOrderV1Profile>(engine.physics_profile);
}

} // namespace

void run_parity_model_contract_tests() {
    InputBuilder valid_builder;
    const auto valid_engine = make_engine(valid_builder);
    expect(validate(valid_engine, valid_builder.provenance).ok(),
           "valid legacy_low_order_v1 resolved engine was rejected");

    InputBuilder deterministic_builder;
    auto deterministic_engine = make_engine(deterministic_builder);
    legacy_profile(deterministic_engine)
        .core.fuel.burning_efficiency_randomness_01.value = 0.0;
    expect(validate(deterministic_engine, deterministic_builder.provenance).ok(),
           "zero burning-efficiency variation invalidated the engine profile");

    expect_parity_mutation_rejected("non-legacy subsystem method identity was accepted",
                                    [](EngineSpec &engine, InputBuilder &) {
                                        engine.methods.gas_exchange.value.id =
                                            "alternate_low_order_v1";
                                    });

    expect_parity_mutation_rejected(
        "legacy profile accepted a disjoint but false torque partition",
        [](EngineSpec &engine, InputBuilder &) {
            auto &loss = legacy_profile(engine).fixed_crank_loss;
            loss.included_terms.value = indicated_gas_torque_term_mask();
            loss.omitted_terms.value =
                known_torque_term_mask() & ~indicated_gas_torque_term_mask();
        });

    expect_parity_mutation_rejected(
        "legacy profile falsely advertised complete instantaneous torque",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.instantaneous_net_shaft = {
                Availability::available,
                Completeness::complete,
                known_torque_term_mask(),
                0,
            };
        });

    expect_parity_mutation_rejected(
        "legacy profile falsely claimed cycle-mean net torque",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.cycle_mean_net_shaft = {
                Availability::available,
                Completeness::complete,
                known_torque_term_mask(),
                0,
            };
        });

    expect_parity_mutation_rejected(
        "legacy profile falsely claimed equivalent inertia",
        [](EngineSpec &engine, InputBuilder &) {
            engine.torque_capability.value.equivalent_inertia_available = true;
        });

    expect_parity_mutation_rejected("cylinder intake topology accepted an exhaust port",
                                    [](EngineSpec &engine, InputBuilder &) {
                                        legacy_profile(engine)
                                            .core.mechanism.cylinders.front()
                                            .topology.intake_port_id = PortId{2};
                                    });

    expect_parity_mutation_rejected(
        "profile topology accepted an edge with incompatible endpoints",
        [](EngineSpec &engine, InputBuilder &) {
            legacy_profile(engine)
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
            legacy_profile(engine)
                .core.mechanism.cylinders.front()
                .parameters.bore_m.value += 0.001;
        });

    expect_parity_mutation_rejected(
        "stale propagation delay survived a path-length calculation",
        [](EngineSpec &engine, InputBuilder &) {
            ++legacy_profile(engine)
                  .core.excitation.cylinder_paths.front()
                  .resolved_delay_samples.value;
        });

    expect_parity_mutation_rejected(
        "nonpositive resolved flame-speed table radius was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            legacy_profile(engine)
                .core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.value = 0.0;
        });

    expect_parity_mutation_rejected(
        "unresolved flame-speed table radius was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            legacy_profile(engine)
                .core.fuel.turbulence_to_flame_speed_ratio_triangle_radius.resolution_id
                .clear();
        });

    expect_parity_mutation_rejected(
        "duplicate cylinder accumulation entry was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            legacy_profile(engine).core.excitation.cylinder_accumulation_order.value = {
                CylinderId{1}, CylinderId{1}};
        });

    expect_parity_mutation_rejected(
        "incomplete excitation route set was accepted",
        [](EngineSpec &engine, InputBuilder &) {
            legacy_profile(engine).core.excitation.routes.clear();
        });

    expect_parity_mutation_rejected(
        "derived restriction coefficient was accepted as authored",
        [](EngineSpec &engine, InputBuilder &builder) {
            const auto &resolution_id =
                legacy_profile(engine)
                    .core.gas_path.intake.main_throttle.resolved_k.resolution_id;
            falsely_mark_authored(builder, resolution_id);
        });

    expect_parity_mutation_rejected(
        "derived propagation delay was accepted as authored",
        [](EngineSpec &engine, InputBuilder &builder) {
            const auto &resolution_id = legacy_profile(engine)
                                            .core.excitation.cylinder_paths.front()
                                            .resolved_delay_samples.resolution_id;
            falsely_mark_authored(builder, resolution_id);
        });
}

} // namespace engine_sim_offline::contract::test
