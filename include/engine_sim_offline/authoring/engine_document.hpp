#pragma once

#include "engine_sim_offline/authoring/quantity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::authoring {

struct EngineTag;
struct CurveTag;
struct CrankshaftTag;
struct JournalTag;
struct ConnectingRodTag;
struct PistonTag;
struct BankTag;
struct CylinderTag;
struct IntakeTag;
struct HeadTag;
struct PortTag;
struct ExhaustTag;
struct CamshaftTag;
struct CamLobeTag;
struct ValvetrainTag;
struct FuelTag;
struct IgnitionWireTag;
struct ThrottleControllerTag;
struct SourceRouteTag;
struct AudioAssetTag;
struct AudioBusTag;
struct RigTag;
struct VehicleTag;
struct TransmissionTag;
struct GearTag;
struct AccessoryConfigurationTag;

using EngineId = StableId<EngineTag>;
using EngineRef = StableRef<EngineTag>;
using CurveId = StableId<CurveTag>;
using CurveRef = StableRef<CurveTag>;
using CrankshaftId = StableId<CrankshaftTag>;
using CrankshaftRef = StableRef<CrankshaftTag>;
using JournalId = StableId<JournalTag>;
using JournalRef = StableRef<JournalTag>;
using ConnectingRodId = StableId<ConnectingRodTag>;
using ConnectingRodRef = StableRef<ConnectingRodTag>;
using PistonId = StableId<PistonTag>;
using PistonRef = StableRef<PistonTag>;
using BankId = StableId<BankTag>;
using BankRef = StableRef<BankTag>;
using CylinderId = StableId<CylinderTag>;
using CylinderRef = StableRef<CylinderTag>;
using IntakeId = StableId<IntakeTag>;
using IntakeRef = StableRef<IntakeTag>;
using HeadId = StableId<HeadTag>;
using HeadRef = StableRef<HeadTag>;
using PortId = StableId<PortTag>;
using PortRef = StableRef<PortTag>;
using ExhaustId = StableId<ExhaustTag>;
using ExhaustRef = StableRef<ExhaustTag>;
using CamshaftId = StableId<CamshaftTag>;
using CamshaftRef = StableRef<CamshaftTag>;
using CamLobeId = StableId<CamLobeTag>;
using CamLobeRef = StableRef<CamLobeTag>;
using ValvetrainId = StableId<ValvetrainTag>;
using ValvetrainRef = StableRef<ValvetrainTag>;
using FuelId = StableId<FuelTag>;
using FuelRef = StableRef<FuelTag>;
using IgnitionWireId = StableId<IgnitionWireTag>;
using IgnitionWireRef = StableRef<IgnitionWireTag>;
using ThrottleControllerId = StableId<ThrottleControllerTag>;
using ThrottleControllerRef = StableRef<ThrottleControllerTag>;
using SourceRouteId = StableId<SourceRouteTag>;
using SourceRouteRef = StableRef<SourceRouteTag>;
using AudioAssetId = StableId<AudioAssetTag>;
using AudioAssetRef = StableRef<AudioAssetTag>;
using AudioBusId = StableId<AudioBusTag>;
using AudioBusRef = StableRef<AudioBusTag>;
using RigId = StableId<RigTag>;
using RigRef = StableRef<RigTag>;
using VehicleId = StableId<VehicleTag>;
using VehicleRef = StableRef<VehicleTag>;
using TransmissionId = StableId<TransmissionTag>;
using TransmissionRef = StableRef<TransmissionTag>;
using GearId = StableId<GearTag>;
using GearRef = StableRef<GearTag>;
using AccessoryConfigurationId = StableId<AccessoryConfigurationTag>;
using AccessoryConfigurationRef = StableRef<AccessoryConfigurationTag>;

enum class EngineCycle : std::uint8_t {
    four_stroke,
};

enum class CylinderLayout : std::uint8_t {
    inline_engine,
    v_engine,
    opposed,
    custom,
};

enum class PortKind : std::uint8_t {
    intake,
    exhaust,
};

enum class CurveEvaluation : std::uint8_t {
    linear,
    right_continuous_hold,
    triangle_weighted_samples,
};

enum class CurveBoundaryBehavior : std::uint8_t {
    clamp,
    zero,
    reject,
};

struct CurveSample {
    Quantity input;
    Quantity output;

    friend bool operator==(const CurveSample &, const CurveSample &) = default;
};

struct CurveDefinition {
    CurveId id;
    QuantityDimension input_dimension = QuantityDimension::dimensionless;
    QuantityDimension output_dimension = QuantityDimension::dimensionless;
    CurveEvaluation evaluation = CurveEvaluation::linear;
    std::optional<Quantity> triangle_filter_radius;
    CurveBoundaryBehavior below_domain = CurveBoundaryBehavior::clamp;
    CurveBoundaryBehavior above_domain = CurveBoundaryBehavior::clamp;
    std::vector<CurveSample> samples;

    friend bool operator==(const CurveDefinition &, const CurveDefinition &) = default;
};

struct FlowBenchRestriction {
    Quantity rated_flow;
    Quantity pressure_drop;

    friend bool operator==(const FlowBenchRestriction &,
                           const FlowBenchRestriction &) = default;
};

struct OrificeRestriction {
    Quantity effective_area;
    double discharge_coefficient_01 = 0.0;

    friend bool operator==(const OrificeRestriction &,
                           const OrificeRestriction &) = default;
};

struct CurveRestriction {
    CurveRef pressure_drop_to_flow;

    friend bool operator==(const CurveRestriction &,
                           const CurveRestriction &) = default;
};

using FlowRestriction =
    std::variant<FlowBenchRestriction, OrificeRestriction, CurveRestriction>;

struct EngineIdentity {
    EngineId id;
    std::string display_name;
    std::optional<std::string> description;

    friend bool operator==(const EngineIdentity &, const EngineIdentity &) = default;
};

struct EngineLimits {
    Quantity redline;

    friend bool operator==(const EngineLimits &, const EngineLimits &) = default;
};

struct CrankshaftDefinition {
    CrankshaftId id;
    Quantity throw_radius;
    Quantity mass;
    Quantity flywheel_mass;
    Quantity moment_of_inertia;
    std::optional<Quantity> friction_torque;
    Quantity tdc_reference_angle;

    friend bool operator==(const CrankshaftDefinition &,
                           const CrankshaftDefinition &) = default;
};

struct CrankshaftJournalAttachment {
    CrankshaftRef crankshaft;

    friend bool operator==(const CrankshaftJournalAttachment &,
                           const CrankshaftJournalAttachment &) = default;
};

using JournalAttachment = std::variant<CrankshaftJournalAttachment>;

struct JournalDefinition {
    JournalId id;
    JournalAttachment attachment;
    Quantity phase;

    friend bool operator==(const JournalDefinition &,
                           const JournalDefinition &) = default;
};

struct ConnectingRodDefinition {
    ConnectingRodId id;
    Quantity length;
    Quantity mass;
    Quantity moment_of_inertia;
    std::optional<Quantity> center_of_mass_from_crank_pin;

    friend bool operator==(const ConnectingRodDefinition &,
                           const ConnectingRodDefinition &) = default;
};

struct PistonDefinition {
    PistonId id;
    Quantity mass;
    Quantity compression_height;
    std::optional<Quantity> wrist_pin_position;
    Quantity displacement_volume;
    std::optional<FlowRestriction> blowby;

    friend bool operator==(const PistonDefinition &,
                           const PistonDefinition &) = default;
};

struct BankDefinition {
    BankId id;
    Quantity angle;
    Quantity bore;
    Quantity deck_height;
    HeadRef head;

    friend bool operator==(const BankDefinition &, const BankDefinition &) = default;
};

struct IntakeDefinition {
    IntakeId id;
    Quantity plenum_volume;
    Quantity plenum_cross_section_area;
    Quantity runner_length;
    FlowRestriction main_restriction;
    FlowRestriction idle_bypass_restriction;
    FlowRestriction runner_restriction;
    double idle_throttle_position_01 = 0.0;
    double runner_velocity_decay_01 = 0.0;

    friend bool operator==(const IntakeDefinition &,
                           const IntakeDefinition &) = default;
};

struct ExhaustDefinition {
    ExhaustId id;
    Quantity collector_cross_section_area;
    std::optional<Quantity> collector_length;
    std::optional<Quantity> collector_volume;
    Quantity primary_tube_length;
    Quantity primary_cross_section_area;
    FlowRestriction outlet_restriction;
    FlowRestriction primary_restriction;
    double velocity_decay_01 = 0.0;

    friend bool operator==(const ExhaustDefinition &,
                           const ExhaustDefinition &) = default;
};

struct PortDefinition {
    PortId id;
    HeadRef head;
    PortKind kind = PortKind::intake;
    Quantity runner_volume;
    Quantity runner_cross_section_area;
    CurveRef flow_curve;

    friend bool operator==(const PortDefinition &, const PortDefinition &) = default;
};

struct SampledCamLobe {
    CurveRef lift_curve;

    friend bool operator==(const SampledCamLobe &, const SampledCamLobe &) = default;
};

// A compiler expands this convenience into the same sampled curve representation used
// by SampledCamLobe. It is not a runtime node or scripting surface.
struct HarmonicCamLobe {
    Quantity duration_at_reference_lift;
    Quantity reference_lift;
    Quantity maximum_lift;
    double gamma = 0.0;
    std::uint32_t sample_count = 0;

    friend bool operator==(const HarmonicCamLobe &, const HarmonicCamLobe &) = default;
};

using CamLobeShape = std::variant<SampledCamLobe, HarmonicCamLobe>;

struct CamLobeDefinition {
    CamLobeId id;
    CylinderRef cylinder;
    PortKind port_kind = PortKind::intake;
    Quantity centerline;
    CamLobeShape shape;

    friend bool operator==(const CamLobeDefinition &,
                           const CamLobeDefinition &) = default;
};

struct CamshaftDefinition {
    CamshaftId id;
    Quantity advance;
    Quantity base_radius;
    std::vector<CamLobeRef> lobes;

    friend bool operator==(const CamshaftDefinition &,
                           const CamshaftDefinition &) = default;
};

struct StandardValvetrain {
    CamshaftRef intake_camshaft;
    CamshaftRef exhaust_camshaft;

    friend bool operator==(const StandardValvetrain &,
                           const StandardValvetrain &) = default;
};

struct VtecActivation {
    Quantity minimum_engine_speed;
    Quantity minimum_manifold_pressure_abs;
    double minimum_throttle_linkage_opening_01 = 0.0;

    friend bool operator==(const VtecActivation &, const VtecActivation &) = default;
};

struct VtecValvetrain {
    CamshaftRef base_intake_camshaft;
    CamshaftRef base_exhaust_camshaft;
    CamshaftRef alternate_intake_camshaft;
    CamshaftRef alternate_exhaust_camshaft;
    VtecActivation activation;

    friend bool operator==(const VtecValvetrain &, const VtecValvetrain &) = default;
};

using ValvetrainKind = std::variant<StandardValvetrain, VtecValvetrain>;

struct ValvetrainDefinition {
    ValvetrainId id;
    ValvetrainKind kind;

    friend bool operator==(const ValvetrainDefinition &,
                           const ValvetrainDefinition &) = default;
};

struct HeadDefinition {
    HeadId id;
    Quantity chamber_volume;
    ValvetrainRef valvetrain;
    std::vector<PortRef> ports;

    friend bool operator==(const HeadDefinition &, const HeadDefinition &) = default;
};

struct IgnitionWireDefinition {
    IgnitionWireId id;

    friend bool operator==(const IgnitionWireDefinition &,
                           const IgnitionWireDefinition &) = default;
};

struct FiringEventDefinition {
    IgnitionWireRef wire;
    Quantity crank_angle;

    friend bool operator==(const FiringEventDefinition &,
                           const FiringEventDefinition &) = default;
};

struct RevLimiterDefinition {
    Quantity activation_speed;
    Quantity cut_duration;

    friend bool operator==(const RevLimiterDefinition &,
                           const RevLimiterDefinition &) = default;
};

struct IgnitionDefinition {
    CurveRef timing_curve;
    std::vector<IgnitionWireDefinition> wires;
    std::vector<FiringEventDefinition> firing_order;
    RevLimiterDefinition limiter;

    friend bool operator==(const IgnitionDefinition &,
                           const IgnitionDefinition &) = default;
};

struct CombustionDefinition {
    double maximum_efficiency_01 = 0.0;
    double cycle_variation_01 = 0.0;
    double low_efficiency_attenuation_01 = 0.0;
    double maximum_turbulence_effect = 0.0;
    double maximum_dilution_effect = 0.0;

    friend bool operator==(const CombustionDefinition &,
                           const CombustionDefinition &) = default;
};

struct FuelDefinition {
    FuelId id;
    std::string display_name;
    Quantity molecular_mass;
    std::optional<Quantity> density;
    Quantity lower_heating_value;
    double stoichiometric_air_fuel_molar_ratio = 0.0;
    CurveRef turbulence_to_flame_speed;
    CombustionDefinition combustion;

    friend bool operator==(const FuelDefinition &, const FuelDefinition &) = default;
};

struct DirectThrottleController {
    double gamma = 1.0;

    friend bool operator==(const DirectThrottleController &,
                           const DirectThrottleController &) = default;
};

struct GovernorThrottleController {
    Quantity minimum_engine_speed;
    Quantity maximum_engine_speed;
    double minimum_velocity = 0.0;
    double maximum_velocity = 0.0;
    double k_s = 0.0;
    double k_d = 0.0;
    double gamma = 1.0;

    friend bool operator==(const GovernorThrottleController &,
                           const GovernorThrottleController &) = default;
};

using ThrottleControllerKind =
    std::variant<DirectThrottleController, GovernorThrottleController>;

struct ThrottleControllerDefinition {
    ThrottleControllerId id;
    ThrottleControllerKind kind;

    friend bool operator==(const ThrottleControllerDefinition &,
                           const ThrottleControllerDefinition &) = default;
};

struct MechanicallyDisengagedStarter {
    friend bool operator==(const MechanicallyDisengagedStarter &,
                           const MechanicallyDisengagedStarter &) = default;
};

struct CrankingStarter {
    Quantity torque;
    Quantity target_speed;

    friend bool operator==(const CrankingStarter &, const CrankingStarter &) = default;
};

using StarterDefinition = std::variant<MechanicallyDisengagedStarter, CrankingStarter>;

struct CylinderDefinition {
    CylinderId id;
    BankRef bank;
    JournalRef journal;
    ConnectingRodRef connecting_rod;
    PistonRef piston;
    IntakeRef intake;
    ExhaustRef exhaust;
    IgnitionWireRef ignition_wire;
    PortRef intake_port;
    PortRef exhaust_port;
    Quantity exhaust_header_primary_length;

    friend bool operator==(const CylinderDefinition &,
                           const CylinderDefinition &) = default;
};

struct ExhaustRouteSource {
    ExhaustRef exhaust;

    friend bool operator==(const ExhaustRouteSource &,
                           const ExhaustRouteSource &) = default;
};

struct IntakeRouteSource {
    IntakeRef intake;

    friend bool operator==(const IntakeRouteSource &,
                           const IntakeRouteSource &) = default;
};

struct MechanicalRouteSource {
    std::string component;

    friend bool operator==(const MechanicalRouteSource &,
                           const MechanicalRouteSource &) = default;
};

using SourceRouteBinding =
    std::variant<ExhaustRouteSource, IntakeRouteSource, MechanicalRouteSource>;

struct SourceRouteDefinition {
    SourceRouteId id;
    SourceRouteBinding source;

    friend bool operator==(const SourceRouteDefinition &,
                           const SourceRouteDefinition &) = default;
};

enum class AudioAssetKind : std::uint8_t {
    impulse_response,
    audio_sample,
};

struct AudioAssetDefinition {
    AudioAssetId id;
    AudioAssetKind kind = AudioAssetKind::impulse_response;
    std::string uri;
    std::optional<std::string> sha256;

    friend bool operator==(const AudioAssetDefinition &,
                           const AudioAssetDefinition &) = default;
};

struct CylinderRoutePresentation {
    CylinderRef cylinder;
    SourceRouteRef route;
    double gain_linear = 1.0;

    friend bool operator==(const CylinderRoutePresentation &,
                           const CylinderRoutePresentation &) = default;
};

struct RoutePresentation {
    SourceRouteRef route;
    double source_gain_linear = 1.0;
    std::optional<AudioAssetRef> impulse_response;
    double impulse_response_gain_linear = 1.0;
    double wet_mix_01 = 1.0;

    friend bool operator==(const RoutePresentation &,
                           const RoutePresentation &) = default;
};

struct PresentationConditioning {
    double jitter_scale = 0.0;
    Quantity jitter_modulation_cutoff_frequency;
    double derivative_mix_01 = 0.0;
    double air_noise_mix_01 = 0.0;
    Quantity air_noise_cutoff_frequency;

    friend bool operator==(const PresentationConditioning &,
                           const PresentationConditioning &) = default;
};

struct AudioBusDefinition {
    AudioBusId id;
    std::vector<SourceRouteRef> routes;
    double gain_linear = 1.0;
    bool publish = true;

    friend bool operator==(const AudioBusDefinition &,
                           const AudioBusDefinition &) = default;
};

struct AuditionMixDefinition {
    std::vector<AudioBusRef> buses;
    double monitoring_gain_linear = 1.0;
    Quantity fade_in;
    Quantity fade_out;

    friend bool operator==(const AuditionMixDefinition &,
                           const AuditionMixDefinition &) = default;
};

struct PresentationDefinition {
    std::vector<AudioAssetDefinition> assets;
    std::vector<CylinderRoutePresentation> cylinder_routes;
    std::vector<RoutePresentation> routes;
    PresentationConditioning conditioning;
    std::vector<AudioBusDefinition> buses;
    AuditionMixDefinition audition;
    double publication_gain_linear = 1.0;

    friend bool operator==(const PresentationDefinition &,
                           const PresentationDefinition &) = default;
};

struct VehicleDefinition {
    VehicleId id;
    Quantity mass;
    double drag_coefficient = 0.0;
    Quantity frontal_area;
    double differential_ratio = 0.0;
    Quantity tire_radius;
    Quantity rolling_resistance_force;
    // Missing means that this rig does not provide a service-brake actuator.
    std::optional<Quantity> maximum_service_brake_force;

    friend bool operator==(const VehicleDefinition &,
                           const VehicleDefinition &) = default;
};

struct GearDefinition {
    GearId id;
    double ratio = 0.0;

    friend bool operator==(const GearDefinition &, const GearDefinition &) = default;
};

struct TransmissionDefinition {
    TransmissionId id;
    Quantity maximum_clutch_torque;
    std::vector<GearDefinition> gears;

    friend bool operator==(const TransmissionDefinition &,
                           const TransmissionDefinition &) = default;
};

struct DynoDefaultsDefinition {
    Quantity minimum_engine_speed;
    Quantity maximum_engine_speed;
    Quantity hold_step;

    friend bool operator==(const DynoDefaultsDefinition &,
                           const DynoDefaultsDefinition &) = default;
};

struct RigDefinition {
    RigId id;
    std::optional<VehicleDefinition> vehicle;
    std::optional<TransmissionDefinition> transmission;
    std::optional<DynoDefaultsDefinition> dyno_defaults;

    friend bool operator==(const RigDefinition &, const RigDefinition &) = default;
};

struct AccessoryConfigurationDefinition {
    AccessoryConfigurationId id;
    std::string uri;
    std::optional<std::string> sha256;

    friend bool operator==(const AccessoryConfigurationDefinition &,
                           const AccessoryConfigurationDefinition &) = default;
};

struct ChenFlynnLossDefinition {
    Quantity constant_fmep;
    double peak_pressure_coefficient = 0.0;
    Quantity mean_piston_speed_coefficient;
    Quantity mean_piston_speed_squared_coefficient;
    Quantity required_oil_temperature;
    AccessoryConfigurationRef accessory_configuration_id;

    friend bool operator==(const ChenFlynnLossDefinition &,
                           const ChenFlynnLossDefinition &) = default;
};

using EngineLossDefinition = std::variant<ChenFlynnLossDefinition>;

struct EngineDefinition {
    EngineIdentity identity;
    EngineCycle cycle = EngineCycle::four_stroke;
    CylinderLayout layout = CylinderLayout::custom;
    EngineLimits limits;
    std::vector<CurveDefinition> curves;
    std::vector<CrankshaftDefinition> crankshafts;
    std::vector<JournalDefinition> journals;
    std::vector<ConnectingRodDefinition> connecting_rods;
    std::vector<PistonDefinition> pistons;
    std::vector<BankDefinition> banks;
    std::vector<IntakeDefinition> intakes;
    std::vector<ExhaustDefinition> exhausts;
    std::vector<PortDefinition> ports;
    std::vector<CamLobeDefinition> cam_lobes;
    std::vector<CamshaftDefinition> camshafts;
    std::vector<ValvetrainDefinition> valvetrains;
    std::vector<HeadDefinition> heads;
    std::vector<FuelDefinition> fuels;
    FuelRef default_fuel;
    std::vector<AccessoryConfigurationDefinition> accessory_configurations;
    EngineLossDefinition losses;
    IgnitionDefinition ignition;
    std::optional<std::vector<ThrottleControllerDefinition>> throttle_controllers;
    std::optional<ThrottleControllerRef> throttle_controller;
    StarterDefinition starter;
    std::vector<CylinderDefinition> cylinders;
    std::vector<SourceRouteDefinition> source_routes;

    friend bool operator==(const EngineDefinition &,
                           const EngineDefinition &) = default;
};

struct EnginePackageDocument {
    std::string schema = "engine-sim-offline/engine";
    EngineDefinition engine;
    PresentationDefinition presentation;
    std::optional<RigDefinition> rig;

    friend bool operator==(const EnginePackageDocument &,
                           const EnginePackageDocument &) = default;
};

} // namespace engine_sim_offline::authoring
