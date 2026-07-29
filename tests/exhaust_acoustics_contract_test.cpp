#include "contract_test_support.hpp"

#include <string>
#include <utility>

namespace engine_sim_offline::contract::test {
namespace {

constexpr std::string_view kRoot =
    "engine.physics.low-order-operating-point.exhaust_acoustics";

[[nodiscard]] std::string path(std::string_view suffix) {
    return std::string{kRoot} + "." + std::string{suffix};
}

[[nodiscard]] AuthoredValue<std::string> authored_string(std::string value) {
    return {std::move(value), "claim"};
}

template <class T> [[nodiscard]] AuthoredValue<T> authored(T value) {
    return {std::move(value), "claim"};
}

[[nodiscard]] AuthoredEngineDefinition make_authored_engine() {
    AuthoredEngineDefinition engine;
    AuthoredCylinderDefinition cylinder;
    cylinder.semantic_id = authored_string("cylinder-1");
    cylinder.bank_id = authored_string("bank-1");
    engine.cylinders.push_back(std::move(cylinder));
    engine.ports = {
        {
            authored_string("intake-port-1"),
            authored_string("cylinder-1"),
            authored(PortKind::intake),
        },
        {
            authored_string("exhaust-port-1"),
            authored_string("cylinder-1"),
            authored(PortKind::exhaust),
        },
    };
    engine.routes.push_back({
        authored_string("exhaust.outlet-1"),
        authored(SourceRouteKind::exhaust_outlet),
        std::nullopt,
        std::nullopt,
        std::nullopt,
    });
    return engine;
}

[[nodiscard]] AuthoredExhaustAcousticAssembly make_authored_assembly() {
    const auto method = [](std::string id) {
        return authored(MethodSelection{std::move(id), 1});
    };
    AuthoredExhaustAcousticAssembly assembly;
    assembly.assembly_id = authored_string("declared-test-cell-twin-open-pipe");
    assembly.methods = {
        method("ideal-pseudo-gas-source-properties"),
        method("causal-bandlimited-rational-resampling"),
        method("uniform-cylindrical-digital-waveguide"),
        method("ideal-compact-pressure-junction"),
        method("causal-unflanged-pipe-reflection"),
        method("compact-monopole-free-field-radiation"),
    };
    assembly.source_interval_rate = authored(RationalRateHz{80000, 1});
    assembly.acoustic_rate = authored(RationalRateHz{192000, 1});
    assembly.universal_gas_constant_j_per_mol_k = authored(8.31446261815324);
    assembly.source_molar_mass_kg_per_mol = authored(0.02897);
    assembly.source_heat_capacity_ratio = authored(1.4);
    assembly.pa_per_full_scale = authored(256.0);
    assembly.ducts = {
        {
            authored_string("primary-1"),
            authored(AcousticDuctKind::primary),
            authored(0.3),
            authored(0.042),
            authored(800.0),
            authored(0.1),
        },
        {
            authored_string("downstream-1"),
            authored(AcousticDuctKind::downstream),
            authored(1.5),
            authored(0.046),
            authored(600.0),
            authored(0.1),
        },
    };
    assembly.primary_bindings.push_back({
        authored_string("cylinder-1"),
        authored_string("exhaust-port-1"),
        authored_string("primary-1"),
        authored_string("junction-1"),
    });
    assembly.junctions.push_back({
        authored_string("junction-1"),
        {authored_string("primary-1")},
        authored_string("downstream-1"),
    });
    assembly.outlets.push_back({
        authored_string("exhaust.outlet-1"),
        authored_string("downstream-1"),
        authored(1.0),
    });
    return assembly;
}

[[nodiscard]] ExhaustAcousticAssembly make_resolved_assembly(InputBuilder &builder) {
    const auto resolved_method = [&](std::string id, std::uint8_t byte,
                                     std::string_view field) {
        return builder.resolved(method(std::move(id), byte),
                                path("methods." + std::string(field)));
    };
    ExhaustAcousticAssembly assembly;
    assembly.assembly_id = builder.resolved(
        std::string{"declared-test-cell-twin-open-pipe"}, path("assembly_id"));
    assembly.methods = {
        resolved_method("ideal-pseudo-gas-source-properties", 41,
                        "source_properties"),
        resolved_method("causal-bandlimited-rational-resampling", 42,
                        "reconstruction"),
        resolved_method("uniform-cylindrical-digital-waveguide", 43, "waveguide"),
        resolved_method("ideal-compact-pressure-junction", 44, "junction"),
        resolved_method("causal-unflanged-pipe-reflection", 45,
                        "outlet_reflection"),
        resolved_method("compact-monopole-free-field-radiation", 46,
                        "exterior_radiation"),
    };
    assembly.source_interval_rate =
        builder.resolved(RationalRateHz{80000, 1}, path("source_interval_rate"));
    assembly.acoustic_rate =
        builder.resolved(RationalRateHz{192000, 1}, path("acoustic_rate"));
    assembly.universal_gas_constant_j_per_mol_k = builder.resolved(
        8.31446261815324, path("universal_gas_constant_j_per_mol_k"));
    assembly.source_molar_mass_kg_per_mol =
        builder.resolved(0.02897, path("source_molar_mass_kg_per_mol"));
    assembly.source_heat_capacity_ratio =
        builder.resolved(1.4, path("source_heat_capacity_ratio"));
    assembly.pa_per_full_scale =
        builder.resolved(256.0, path("pa_per_full_scale"));
    assembly.ducts = {
        {
            AcousticDuctId{1},
            builder.resolved(std::string{"primary-1"},
                             path("ducts.primary-1.semantic_id")),
            builder.resolved(AcousticDuctKind::primary,
                             path("ducts.primary-1.kind")),
            builder.resolved(0.3, path("ducts.primary-1.length_m")),
            builder.resolved(0.042, path("ducts.primary-1.inner_diameter_m")),
            builder.resolved(800.0,
                             path("ducts.primary-1.reference_temperature_k")),
            builder.resolved(0.1,
                             path("ducts.primary-1.propagation_loss_np_per_m")),
        },
        {
            AcousticDuctId{2},
            builder.resolved(std::string{"downstream-1"},
                             path("ducts.downstream-1.semantic_id")),
            builder.resolved(AcousticDuctKind::downstream,
                             path("ducts.downstream-1.kind")),
            builder.resolved(1.5, path("ducts.downstream-1.length_m")),
            builder.resolved(0.046,
                             path("ducts.downstream-1.inner_diameter_m")),
            builder.resolved(600.0,
                             path("ducts.downstream-1.reference_temperature_k")),
            builder.resolved(0.1,
                             path("ducts.downstream-1.propagation_loss_np_per_m")),
        },
    };
    assembly.primary_bindings.push_back(
        {CylinderId{1}, PortId{2}, AcousticDuctId{1}, AcousticJunctionId{1}});
    assembly.junctions.push_back({
        AcousticJunctionId{1},
        builder.resolved(std::string{"junction-1"},
                         path("junctions.junction-1.semantic_id")),
        {AcousticDuctId{1}},
        AcousticDuctId{2},
    });
    assembly.outlets.push_back({
        RouteId{1},
        AcousticDuctId{2},
        builder.resolved(1.0,
                         path("outlets.exhaust.outlet-1.observation_distance_m")),
    });
    return assembly;
}

} // namespace

void run_exhaust_acoustics_contract_tests() {
    InputBuilder authored_builder;
    const auto authored_engine = make_authored_engine();
    auto authored_assembly = make_authored_assembly();
    expect(validate(authored_assembly, authored_engine, authored_builder.provenance,
                    kRoot)
               .ok(),
           "valid authored exhaust acoustic assembly was rejected");
    authored_assembly.ducts.front().length_m.value = 0.0;
    expect(!validate(authored_assembly, authored_engine, authored_builder.provenance,
                     kRoot)
                .ok(),
           "authored exhaust acoustic assembly accepted zero duct length");

    InputBuilder valid_builder;
    const auto valid_engine = make_engine(valid_builder);
    const auto valid_assembly = make_resolved_assembly(valid_builder);
    expect(validate(valid_assembly, valid_engine, valid_builder.provenance, kRoot).ok(),
           "valid resolved exhaust acoustic assembly was rejected");

    InputBuilder wrong_rate_builder;
    const auto wrong_rate_engine = make_engine(wrong_rate_builder);
    auto wrong_rate = make_resolved_assembly(wrong_rate_builder);
    wrong_rate.source_interval_rate.value = {40000, 1};
    expect(!validate(wrong_rate, wrong_rate_engine, wrong_rate_builder.provenance,
                     kRoot)
                .ok(),
           "exhaust acoustic assembly accepted the wrong source rate");

    InputBuilder cross_binding_builder;
    const auto cross_binding_engine = make_engine(cross_binding_builder);
    auto cross_binding = make_resolved_assembly(cross_binding_builder);
    cross_binding.primary_bindings.front().exhaust_port_id = PortId{1};
    expect(!validate(cross_binding, cross_binding_engine,
                     cross_binding_builder.provenance, kRoot)
                .ok(),
           "exhaust acoustic assembly accepted an intake-port source binding");

    InputBuilder missing_primary_builder;
    const auto missing_primary_engine = make_engine(missing_primary_builder);
    auto missing_primary = make_resolved_assembly(missing_primary_builder);
    missing_primary.junctions.front().primary_duct_ids.clear();
    expect(!validate(missing_primary, missing_primary_engine,
                     missing_primary_builder.provenance, kRoot)
                .ok(),
           "exhaust acoustic assembly accepted a junction without a primary");

    InputBuilder bad_outlet_builder;
    const auto bad_outlet_engine = make_engine(bad_outlet_builder);
    auto bad_outlet = make_resolved_assembly(bad_outlet_builder);
    bad_outlet.outlets.front().observation_distance_m.value = -1.0;
    expect(!validate(bad_outlet, bad_outlet_engine, bad_outlet_builder.provenance,
                     kRoot)
                .ok(),
           "exhaust acoustic assembly accepted a negative observation distance");
}

} // namespace engine_sim_offline::contract::test
