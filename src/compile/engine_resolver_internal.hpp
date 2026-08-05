#pragma once

#include "compile/engine_resolver.hpp"
#include "compile/resolution_builder.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {

struct IdNamespace {
    std::string name;
    std::unordered_map<std::string, RuntimeObjectId> by_semantic_id;
};

struct RuntimeIds {
    IdNamespace crankshafts;
    IdNamespace banks;
    IdNamespace intakes;
    IdNamespace cylinders;
    IdNamespace ports;
    IdNamespace gas_volumes;
    IdNamespace flow_edges;
    IdNamespace routes;
    IdNamespace audio_assets;
    IdNamespace audio_buses;
    IdNamespace accessory_configurations;
    IdNamespace rigs;
    IdNamespace vehicles;
    IdNamespace transmissions;
    IdNamespace gears;
    std::vector<StableIdAssignment> assignments;
};

struct VerifiedAssets {
    std::vector<VerifiedEngineAsset> values;
    std::unordered_map<std::string, std::size_t> audio_by_id;
    std::unordered_map<std::string, std::size_t> accessory_by_id;
    std::unordered_map<std::string, contract::AudioMediaContract> audio_media_by_id;
};

struct ModelContext {
    explicit ModelContext(const authoring::EnginePackageDocument &source) noexcept
        : document(source) {}

    const authoring::EnginePackageDocument &document;
    RuntimeIds ids;
    VerifiedAssets assets;

    std::string profile_id;
    std::string calibration_id;

    const authoring::CrankshaftDefinition *output_crankshaft = nullptr;
    const authoring::FuelDefinition *fuel = nullptr;
    const authoring::ThrottleControllerDefinition *throttle_controller = nullptr;
    const authoring::AccessoryConfigurationDefinition *accessory_configuration =
        nullptr;

    std::unordered_map<std::string, const authoring::CurveDefinition *> curves;
    std::unordered_map<std::string, const authoring::CrankshaftDefinition *>
        crankshafts;
    std::unordered_map<std::string, const authoring::BankDefinition *> banks;
    std::unordered_map<std::string, const authoring::IntakeDefinition *> intakes;
    std::unordered_map<std::string, const authoring::JournalDefinition *> journals;
    std::unordered_map<std::string, const authoring::ConnectingRodDefinition *> rods;
    std::unordered_map<std::string, const authoring::PistonDefinition *> pistons;
    std::unordered_map<std::string, const authoring::HeadDefinition *> heads;
    std::unordered_map<std::string, const authoring::PortDefinition *> authored_ports;
    std::unordered_map<std::string, const authoring::CamLobeDefinition *> cam_lobes;
    std::unordered_map<std::string, const authoring::CamshaftDefinition *> camshafts;
    std::unordered_map<std::string, const authoring::ValvetrainDefinition *>
        valvetrains;
    std::unordered_map<std::string, const authoring::ExhaustDefinition *> exhausts;
    std::unordered_map<std::string, const authoring::SourceRouteDefinition *>
        source_routes;

    // A cylinder's crankshaft is derived through its journal attachment. Direct
    // journals name the crankshaft; master-rod journals inherit the crankshaft of
    // their direct-root master cylinder.
    std::unordered_map<std::string, const authoring::CrankshaftDefinition *>
        crankshaft_for_cylinder;

    std::unordered_map<std::string, std::string> route_for_exhaust;
    // Pristine ignition wires are fan-out connections: one distributor post may
    // drive more than one cylinder.  Preserve authored cylinder order inside each
    // wire so expansion into the executable cylinder firing order is deterministic.
    std::unordered_map<std::string, std::vector<std::string>> cylinders_for_wire;
    std::unordered_map<std::string, double> firing_angle_for_wire_rad;
    std::unordered_map<std::string, const authoring::CylinderRoutePresentation *>
        cylinder_presentations;
    std::unordered_map<std::string, const authoring::RoutePresentation *>
        route_presentations;
    std::unordered_map<std::string, const authoring::PortDefinition *>
        intake_port_for_head;
    std::unordered_map<std::string, const authoring::PortDefinition *>
        exhaust_port_for_head;
    std::unordered_map<std::string, const authoring::CamshaftDefinition *>
        intake_camshaft_for_cylinder;
    std::unordered_map<std::string, const authoring::CamshaftDefinition *>
        exhaust_camshaft_for_cylinder;
    std::unordered_map<std::string, const authoring::CamshaftDefinition *>
        alternate_intake_camshaft_for_cylinder;
    std::unordered_map<std::string, const authoring::CamshaftDefinition *>
        alternate_exhaust_camshaft_for_cylinder;
    // VTEC is owned by the head serving a bank. Standard-valvetrain banks have no
    // entry; separate VTEC banks may point at distinct definitions/thresholds.
    std::unordered_map<std::string, const authoring::VtecValvetrain *>
        vtec_valvetrain_for_bank;
};

class ResolutionEmitter {
  public:
    explicit ResolutionEmitter(ResolutionProvenanceBuilder &builder) noexcept;
    explicit ResolutionEmitter(const contract::ProvenanceLedger &ledger);

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value> authored(Value value,
                                                          std::string path) {
        return {std::move(value), authored_id(std::move(path))};
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value> declared_default(Value value,
                                                                  std::string path) {
        return {std::move(value), declared_default_id(std::move(path))};
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value>
    derived(Value value, std::string path, contract::MethodIdentity method,
            std::span<const std::string_view> dependencies) {
        return {
            std::move(value),
            derived_id(std::move(path), std::move(method), dependencies),
        };
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value>
    derived(Value value, std::string path, contract::MethodIdentity method,
            std::initializer_list<std::string_view> dependencies) {
        return derived(std::move(value), std::move(path), std::move(method),
                       std::span<const std::string_view>{dependencies.begin(),
                                                         dependencies.size()});
    }

  private:
    [[nodiscard]] std::string authored_id(std::string path);
    [[nodiscard]] std::string declared_default_id(std::string path);
    [[nodiscard]] std::string
    derived_id(std::string path, contract::MethodIdentity method,
               std::span<const std::string_view> dependencies);

    ResolutionProvenanceBuilder *builder_ = nullptr;
    std::unordered_map<std::string, std::string> resolution_ids_;
};

struct AssembledContracts {
    contract::EngineSpec engine;
    contract::PresentationCalibration presentation;
    contract::ResolvedRandomnessPolicy randomness;
    std::optional<ResolvedRigDescriptor> rig;
    std::vector<ResolvedFuelDescriptor> fuels;
    std::vector<ResolvedAudioBusDescriptor> audio_buses;
};

struct ResolvedRouteSource {
    const authoring::SourceRouteDefinition *route = nullptr;
    const authoring::ExhaustDefinition *exhaust = nullptr;
};

[[nodiscard]] authoring::DiagnosticReport
admit_engine_document(const authoring::EnginePackageDocument &document,
                      std::span<const AssetPayloadView> assets,
                      std::optional<ModelContext> &context);

void verify_engine_assets(const authoring::EnginePackageDocument &document,
                          std::span<const AssetPayloadView> assets,
                          authoring::DiagnosticReport &report, VerifiedAssets &output);

void admit_engine_presentation(const authoring::EnginePackageDocument &document,
                               ModelContext &context,
                               authoring::DiagnosticReport &report);

void assign_engine_runtime_ids(ModelContext &context,
                               authoring::DiagnosticReport &report);

void admit_engine_physical_model(ModelContext &context,
                                 authoring::DiagnosticReport &report);

void admit_engine_operating_systems(ModelContext &context,
                                    authoring::DiagnosticReport &report);

void admit_engine_rig(ModelContext &context, authoring::DiagnosticReport &report);

void attach_asset_evidence(contract::ProvenanceLedger &ledger,
                           const std::vector<VerifiedEngineAsset> &assets);

[[nodiscard]] AssembledContracts assemble_contracts(const ModelContext &context,
                                                    ResolutionEmitter &emitter);

[[nodiscard]] contract::EngineSpec assemble_engine(const ModelContext &context,
                                                   ResolutionEmitter &emitter);

[[nodiscard]] std::optional<ResolvedRigDescriptor>
assemble_rig(const ModelContext &context, ResolutionEmitter &emitter);

[[nodiscard]] std::string profile_path(std::string_view suffix);
[[nodiscard]] contract::CrankshaftId crankshaft_id(const ModelContext &context,
                                                   std::string_view semantic_id);
[[nodiscard]] contract::BankId bank_id(const ModelContext &context,
                                       std::string_view semantic_id);
[[nodiscard]] contract::IntakeId intake_id(const ModelContext &context,
                                           std::string_view semantic_id);
[[nodiscard]] contract::CylinderId cylinder_id(const ModelContext &context,
                                               std::string_view semantic_id);
[[nodiscard]] contract::PortId port_id(const ModelContext &context,
                                       std::string_view semantic_id);
[[nodiscard]] contract::GasVolumeId volume_id(const ModelContext &context,
                                              std::string_view semantic_id);
[[nodiscard]] contract::FlowEdgeId edge_id(const ModelContext &context,
                                           std::string_view semantic_id);
[[nodiscard]] contract::RouteId route_id(const ModelContext &context,
                                         std::string_view semantic_id);
[[nodiscard]] std::string port_semantic_id(std::string_view cylinder,
                                           authoring::PortKind kind);
[[nodiscard]] std::string volume_semantic_id(std::string_view cylinder,
                                             std::string_view role);
[[nodiscard]] std::string intake_plenum_semantic_id(std::string_view intake);
[[nodiscard]] std::string collector_semantic_id(std::string_view exhaust);
[[nodiscard]] std::string flow_semantic_id(std::string_view owner,
                                           std::string_view role);
[[nodiscard]] contract::LegacyRestriction
resolve_restriction(const authoring::FlowRestriction &source, std::string base_path,
                    ResolutionEmitter &emitter);
[[nodiscard]] double rpm_value(const authoring::Quantity &quantity);
[[nodiscard]] std::string sample_id(std::size_t index);
[[nodiscard]] std::vector<ResolvedRouteSource>
ordered_routes(const ModelContext &context);

[[nodiscard]] std::vector<ResolvedRouteSource>
ordered_exhaust_routes(const ModelContext &context);
[[nodiscard]] std::vector<const authoring::BankDefinition *>
ordered_banks(const ModelContext &context);
[[nodiscard]] std::vector<const authoring::IntakeDefinition *>
ordered_intakes(const ModelContext &context);
[[nodiscard]] const authoring::CrankshaftDefinition &
crankshaft_for_cylinder(const ModelContext &context,
                        std::string_view cylinder_semantic_id);
[[nodiscard]] const authoring::CamLobeDefinition &
cam_lobe_for_cylinder(const ModelContext &context,
                      const authoring::CamshaftDefinition &camshaft,
                      std::string_view cylinder, authoring::PortKind kind);
[[nodiscard]] contract::LegacyCamShape resolve_cam_shape(
    const ModelContext &context, const authoring::CamshaftDefinition &camshaft,
    authoring::PortKind kind, std::string role, ResolutionEmitter &emitter);
[[nodiscard]] contract::LegacyValveFlowPoint
resolve_valve_flow_point(const authoring::CurveSample &source, std::size_t index,
                         std::string base, ResolutionEmitter &emitter);
[[nodiscard]] contract::TorqueCapability operating_torque_capability();

void resolve_public_topology(const ModelContext &context, ResolutionEmitter &emitter,
                             contract::EngineSpec &engine);

void resolve_mechanism(const ModelContext &context, ResolutionEmitter &emitter,
                       contract::LowOrderEngineCoreV1 &core);
void resolve_throttle_controller(const ModelContext &context,
                                 ResolutionEmitter &emitter,
                                 contract::LowOrderEngineCoreV1 &core);
void resolve_gas_path(const ModelContext &context, ResolutionEmitter &emitter,
                      contract::LowOrderEngineCoreV1 &core);
void resolve_valvetrain(const ModelContext &context, ResolutionEmitter &emitter,
                        contract::LowOrderEngineCoreV1 &core);
void resolve_ignition_and_fuel(const ModelContext &context, ResolutionEmitter &emitter,
                               contract::LowOrderEngineCoreV1 &core);
void resolve_excitation(const ModelContext &context, ResolutionEmitter &emitter,
                        contract::LowOrderEngineCoreV1 &core);
void resolve_operating_accounting(const ModelContext &context,
                                  ResolutionEmitter &emitter,
                                  contract::LowOrderOperatingPointV1Profile &profile);

void assemble_presentation(const ModelContext &context, ResolutionEmitter &emitter,
                           contract::PresentationCalibration &presentation,
                           contract::ResolvedRandomnessPolicy &randomness,
                           std::vector<ResolvedAudioBusDescriptor> &audio_buses);

[[nodiscard]] contract::MethodIdentity legacy_low_order_method_identity();
[[nodiscard]] contract::MethodIdentity
derived_method_identity(std::string_view method_id);

[[nodiscard]] double legacy_si_value(const authoring::Quantity &quantity);

[[nodiscard]] std::string pointer_index(std::string_view collection, std::size_t index);

[[nodiscard]] authoring::DiagnosticReport unsupported(std::string_view path,
                                                      std::string message);

[[nodiscard]] authoring::DiagnosticReport invalid(std::string_view path,
                                                  std::string message);

} // namespace engine_sim_offline::compile::detail::engine_resolution
