#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::contract {

enum class EngineCycle : std::uint8_t {
    unspecified,
    four_stroke,
};

enum class IgnitionKind : std::uint8_t {
    unspecified,
    spark_ignition,
};

enum class CylinderLayoutKind : std::uint8_t {
    unspecified,
    inline_engine,
    vee_engine,
    flat_engine,
    other,
};

enum class PortKind : std::uint8_t {
    unspecified,
    intake,
    exhaust,
};

enum class GasVolumeKind : std::uint8_t {
    unspecified,
    atmosphere,
    intake_plenum,
    intake_runner,
    cylinder,
    exhaust_primary,
    exhaust_collector,
};

enum class SourceRouteKind : std::uint8_t {
    unspecified,
    exhaust_outlet,
    intake_inlet,
    mechanical_engine,
    mechanical_starter,
};

struct AuthoredCylinderDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<std::string> bank_id;
    std::optional<AuthoredValue<double>> bore_m;
    std::optional<AuthoredValue<double>> stroke_m;
    std::optional<AuthoredValue<double>> connecting_rod_length_m;
    std::optional<AuthoredValue<double>> compression_ratio;
    std::optional<AuthoredValue<double>> firing_tdc_offset_rad;
    std::optional<AuthoredValue<double>> journal_phase_rad;

    friend bool operator==(const AuthoredCylinderDefinition &,
                           const AuthoredCylinderDefinition &) = default;
};

struct AuthoredPortDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<std::string> cylinder_id;
    AuthoredValue<PortKind> kind;

    friend bool operator==(const AuthoredPortDefinition &,
                           const AuthoredPortDefinition &) = default;
};

struct AuthoredGasVolumeDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<GasVolumeKind> kind;

    friend bool operator==(const AuthoredGasVolumeDefinition &,
                           const AuthoredGasVolumeDefinition &) = default;
};

struct AuthoredFlowEdgeDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<std::string> endpoint_0_volume_id;
    AuthoredValue<std::string> endpoint_1_volume_id;

    friend bool operator==(const AuthoredFlowEdgeDefinition &,
                           const AuthoredFlowEdgeDefinition &) = default;
};

struct AuthoredRouteDefinition {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<SourceRouteKind> kind;
    std::optional<AuthoredValue<std::string>> source_volume_id;
    std::optional<AuthoredValue<std::string>> default_parent_route_id;
    std::optional<AuthoredValue<std::string>> emitter_anchor_id;

    friend bool operator==(const AuthoredRouteDefinition &,
                           const AuthoredRouteDefinition &) = default;
};

struct AuthoredModelMethods {
    AuthoredValue<MethodSelection> mechanism;
    AuthoredValue<MethodSelection> valvetrain;
    AuthoredValue<MethodSelection> gas_exchange;
    AuthoredValue<MethodSelection> ignition;
    AuthoredValue<MethodSelection> combustion;
    AuthoredValue<MethodSelection> heat_transfer;
    AuthoredValue<MethodSelection> losses;
    AuthoredValue<MethodSelection> excitation;

    friend bool operator==(const AuthoredModelMethods &,
                           const AuthoredModelMethods &) = default;
};

struct AuthoredEngineDefinition {
    std::uint32_t schema_version = 0;
    std::string definition_id;
    AuthoredValue<std::string> engine_id;
    AuthoredValue<std::string> profile_id;
    AuthoredValue<std::string> display_name;
    AuthoredValue<EngineCycle> cycle;
    AuthoredValue<IgnitionKind> ignition;
    AuthoredValue<CylinderLayoutKind> cylinder_layout;
    std::vector<AuthoredValue<std::string>> banks;
    std::vector<AuthoredCylinderDefinition> cylinders;
    std::vector<AuthoredPortDefinition> ports;
    std::vector<AuthoredGasVolumeDefinition> gas_volumes;
    std::vector<AuthoredFlowEdgeDefinition> flow_edges;
    std::vector<AuthoredRouteDefinition> routes;
    AuthoredModelMethods methods;
    AuthoredExecutablePhysicsProfile physics_profile;
    ProvenanceLedger provenance;

    friend bool operator==(const AuthoredEngineDefinition &,
                           const AuthoredEngineDefinition &) = default;
};

struct BankSpec {
    BankId id;
    ResolvedValue<std::string> semantic_id;
    // Inline bank angle is constrained to zero; layouts with geometric bank
    // separation resolve each authored bank axis explicitly.
    std::optional<ResolvedValue<double>> angle_rad;

    friend bool operator==(const BankSpec &, const BankSpec &) = default;
};

struct MasterRodAttachmentSpec {
    CylinderId master_cylinder_id;
    ResolvedValue<double> throw_radius_m;

    friend bool operator==(const MasterRodAttachmentSpec &,
                           const MasterRodAttachmentSpec &) = default;
};

struct CylinderSpec {
    CylinderId id;
    ResolvedValue<std::string> semantic_id;
    BankId bank_id;
    ResolvedValue<double> bore_m;
    ResolvedValue<double> stroke_m;
    ResolvedValue<double> connecting_rod_length_m;
    ResolvedValue<double> compression_ratio;
    ResolvedValue<double> firing_tdc_offset_rad;
    ResolvedValue<double> journal_phase_rad;
    // Absent for a journal attached directly to the crankshaft. When present,
    // journal_phase_rad is the slave pin's authored local phase on this master
    // cylinder's connecting rod.
    std::optional<MasterRodAttachmentSpec> master_rod_attachment = std::nullopt;
    // Present only when two or more cylinders share one stateless pristine
    // ignition wire. Distinct one-cylinder wires are execution-equivalent after
    // their firing angles resolve and therefore do not survive as runtime objects.
    std::optional<ResolvedValue<std::string>> shared_ignition_wire_semantic_id;

    friend bool operator==(const CylinderSpec &, const CylinderSpec &) = default;
};

struct PortSpec {
    PortId id;
    ResolvedValue<std::string> semantic_id;
    CylinderId cylinder_id;
    ResolvedValue<PortKind> kind;

    friend bool operator==(const PortSpec &, const PortSpec &) = default;
};

struct GasVolumeSpec {
    GasVolumeId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<GasVolumeKind> kind;

    friend bool operator==(const GasVolumeSpec &, const GasVolumeSpec &) = default;
};

struct FlowEdgeSpec {
    FlowEdgeId id;
    ResolvedValue<std::string> semantic_id;
    GasVolumeId endpoint_0_volume_id;
    GasVolumeId endpoint_1_volume_id;

    friend bool operator==(const FlowEdgeSpec &, const FlowEdgeSpec &) = default;
};

struct RouteSpec {
    RouteId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<SourceRouteKind> kind;
    std::optional<GasVolumeId> source_volume_id;
    std::optional<RouteId> default_parent_route_id;
    std::optional<ResolvedValue<std::string>> emitter_anchor_id;

    friend bool operator==(const RouteSpec &, const RouteSpec &) = default;
};

struct EngineSpec {
    std::uint32_t schema_version = 0;
    EngineId id;
    ResolvedValue<std::string> engine_id;
    ResolvedValue<std::string> profile_id;
    ResolvedValue<std::string> display_name;
    ResolvedValue<EngineCycle> cycle;
    ResolvedValue<IgnitionKind> ignition;
    ResolvedValue<CylinderLayoutKind> cylinder_layout;
    ResolvedValue<double> total_displacement_m3;
    std::vector<BankSpec> banks;
    std::vector<CylinderSpec> cylinders;
    std::vector<PortSpec> ports;
    std::vector<GasVolumeSpec> gas_volumes;
    std::vector<FlowEdgeSpec> flow_edges;
    std::vector<RouteSpec> routes;
    struct ModelMethods {
        ResolvedValue<MethodIdentity> mechanism;
        ResolvedValue<MethodIdentity> valvetrain;
        ResolvedValue<MethodIdentity> gas_exchange;
        ResolvedValue<MethodIdentity> ignition;
        ResolvedValue<MethodIdentity> combustion;
        ResolvedValue<MethodIdentity> heat_transfer;
        ResolvedValue<MethodIdentity> losses;
        ResolvedValue<MethodIdentity> excitation;

        friend bool operator==(const ModelMethods &, const ModelMethods &) = default;
    } methods;
    ExecutablePhysicsProfile physics_profile;
    ResolvedValue<TorqueCapability> torque_capability;
    std::string provenance_schema_id;

    friend bool operator==(const EngineSpec &, const EngineSpec &) = default;
};

[[nodiscard]] ValidationReport validate(const AuthoredEngineDefinition &definition);
[[nodiscard]] ValidationReport validate(const EngineSpec &spec,
                                        const ProvenanceLedger &provenance);

} // namespace engine_sim_offline::contract
