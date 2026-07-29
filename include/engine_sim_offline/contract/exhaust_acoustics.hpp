#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::contract {

struct AuthoredEngineDefinition;
struct EngineSpec;

enum class AcousticDuctKind : std::uint8_t {
    unspecified,
    primary,
    downstream,
};

struct AuthoredExhaustAcousticMethods {
    AuthoredValue<MethodSelection> source_properties;
    AuthoredValue<MethodSelection> reconstruction;
    AuthoredValue<MethodSelection> waveguide;
    AuthoredValue<MethodSelection> junction;
    AuthoredValue<MethodSelection> outlet_reflection;
    AuthoredValue<MethodSelection> exterior_radiation;

    friend bool operator==(const AuthoredExhaustAcousticMethods &,
                           const AuthoredExhaustAcousticMethods &) = default;
};

struct ExhaustAcousticMethods {
    ResolvedValue<MethodIdentity> source_properties;
    ResolvedValue<MethodIdentity> reconstruction;
    ResolvedValue<MethodIdentity> waveguide;
    ResolvedValue<MethodIdentity> junction;
    ResolvedValue<MethodIdentity> outlet_reflection;
    ResolvedValue<MethodIdentity> exterior_radiation;

    friend bool operator==(const ExhaustAcousticMethods &,
                           const ExhaustAcousticMethods &) = default;
};

struct AuthoredAcousticDuctDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<AcousticDuctKind> kind;
    AuthoredValue<double> length_m;
    AuthoredValue<double> inner_diameter_m;
    AuthoredValue<double> reference_temperature_k;
    AuthoredValue<double> propagation_loss_np_per_m;

    friend bool operator==(const AuthoredAcousticDuctDefinition &,
                           const AuthoredAcousticDuctDefinition &) = default;
};

struct AcousticDuctSpec {
    AcousticDuctId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<AcousticDuctKind> kind;
    ResolvedValue<double> length_m;
    ResolvedValue<double> inner_diameter_m;
    ResolvedValue<double> reference_temperature_k;
    ResolvedValue<double> propagation_loss_np_per_m;

    friend bool operator==(const AcousticDuctSpec &,
                           const AcousticDuctSpec &) = default;
};

struct AuthoredExhaustPrimaryBinding {
    AuthoredValue<std::string> cylinder_id;
    AuthoredValue<std::string> exhaust_port_id;
    AuthoredValue<std::string> primary_duct_id;
    AuthoredValue<std::string> junction_id;

    friend bool operator==(const AuthoredExhaustPrimaryBinding &,
                           const AuthoredExhaustPrimaryBinding &) = default;
};

struct ExhaustPrimaryBinding {
    CylinderId cylinder_id;
    PortId exhaust_port_id;
    AcousticDuctId primary_duct_id;
    AcousticJunctionId junction_id;

    friend bool operator==(const ExhaustPrimaryBinding &,
                           const ExhaustPrimaryBinding &) = default;
};

struct AuthoredExhaustAcousticJunction {
    AuthoredValue<std::string> semantic_id;
    std::vector<AuthoredValue<std::string>> primary_duct_ids;
    AuthoredValue<std::string> downstream_duct_id;

    friend bool operator==(const AuthoredExhaustAcousticJunction &,
                           const AuthoredExhaustAcousticJunction &) = default;
};

struct ExhaustAcousticJunction {
    AcousticJunctionId id;
    ResolvedValue<std::string> semantic_id;
    std::vector<AcousticDuctId> primary_duct_ids;
    AcousticDuctId downstream_duct_id;

    friend bool operator==(const ExhaustAcousticJunction &,
                           const ExhaustAcousticJunction &) = default;
};

struct AuthoredExhaustAcousticOutlet {
    AuthoredValue<std::string> route_id;
    AuthoredValue<std::string> downstream_duct_id;
    AuthoredValue<double> observation_distance_m;

    friend bool operator==(const AuthoredExhaustAcousticOutlet &,
                           const AuthoredExhaustAcousticOutlet &) = default;
};

struct ExhaustAcousticOutlet {
    RouteId route_id;
    AcousticDuctId downstream_duct_id;
    ResolvedValue<double> observation_distance_m;

    friend bool operator==(const ExhaustAcousticOutlet &,
                           const ExhaustAcousticOutlet &) = default;
};

struct AuthoredExhaustAcousticAssembly {
    AuthoredValue<std::string> assembly_id;
    AuthoredExhaustAcousticMethods methods;
    AuthoredValue<RationalRateHz> source_interval_rate;
    AuthoredValue<RationalRateHz> acoustic_rate;
    AuthoredValue<double> universal_gas_constant_j_per_mol_k;
    AuthoredValue<double> source_molar_mass_kg_per_mol;
    AuthoredValue<double> source_heat_capacity_ratio;
    AuthoredValue<double> pa_per_full_scale;
    std::vector<AuthoredAcousticDuctDefinition> ducts;
    std::vector<AuthoredExhaustPrimaryBinding> primary_bindings;
    std::vector<AuthoredExhaustAcousticJunction> junctions;
    std::vector<AuthoredExhaustAcousticOutlet> outlets;

    friend bool operator==(const AuthoredExhaustAcousticAssembly &,
                           const AuthoredExhaustAcousticAssembly &) = default;
};

struct ExhaustAcousticAssembly {
    ResolvedValue<std::string> assembly_id;
    ExhaustAcousticMethods methods;
    ResolvedValue<RationalRateHz> source_interval_rate;
    ResolvedValue<RationalRateHz> acoustic_rate;
    ResolvedValue<double> universal_gas_constant_j_per_mol_k;
    ResolvedValue<double> source_molar_mass_kg_per_mol;
    ResolvedValue<double> source_heat_capacity_ratio;
    ResolvedValue<double> pa_per_full_scale;
    std::vector<AcousticDuctSpec> ducts;
    std::vector<ExhaustPrimaryBinding> primary_bindings;
    std::vector<ExhaustAcousticJunction> junctions;
    std::vector<ExhaustAcousticOutlet> outlets;

    friend bool operator==(const ExhaustAcousticAssembly &,
                           const ExhaustAcousticAssembly &) = default;
};

// The caller supplies the canonical profile-owned root so every provenance leaf is
// checked against its complete parameter path rather than a context-free suffix.
[[nodiscard]] ValidationReport validate(
    const AuthoredExhaustAcousticAssembly &assembly,
    const AuthoredEngineDefinition &engine, const ProvenanceLedger &provenance,
    std::string_view root_path);

[[nodiscard]] ValidationReport validate(const ExhaustAcousticAssembly &assembly,
                                        const EngineSpec &engine,
                                        const ProvenanceLedger &provenance,
                                        std::string_view root_path);

} // namespace engine_sim_offline::contract
