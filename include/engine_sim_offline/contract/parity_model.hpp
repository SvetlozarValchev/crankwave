#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::contract {

struct EngineSpec;

enum class LegacyRestrictionCalibration : std::uint8_t {
    unspecified,
    carb_at_1p5_inhg,
    cfm_at_28_inh2o,
};

template <template <class> class Field> struct LegacyRestrictionT {
    Field<LegacyRestrictionCalibration> calibration;
    Field<double> source_rating;
    Field<double> resolved_k;

    friend bool operator==(const LegacyRestrictionT &,
                           const LegacyRestrictionT &) = default;
};

using AuthoredLegacyRestriction = LegacyRestrictionT<AuthoredValue>;
using LegacyRestriction = LegacyRestrictionT<ResolvedValue>;

struct AuthoredLegacyCylinderTopology {
    AuthoredValue<std::string> cylinder_id;
    AuthoredValue<std::string> crankshaft_id;
    AuthoredValue<std::string> intake_id;
    AuthoredValue<std::string> intake_port_id;
    AuthoredValue<std::string> exhaust_port_id;
    AuthoredValue<std::string> intake_runner_volume_id;
    AuthoredValue<std::string> chamber_volume_id;
    AuthoredValue<std::string> exhaust_primary_volume_id;
    AuthoredValue<std::string> plenum_to_runner_edge_id;
    AuthoredValue<std::string> intake_valve_edge_id;
    AuthoredValue<std::string> exhaust_valve_edge_id;
    AuthoredValue<std::string> primary_to_collector_edge_id;
    AuthoredValue<std::string> blowby_edge_id;
    AuthoredValue<std::string> exhaust_route_id;

    friend bool operator==(const AuthoredLegacyCylinderTopology &,
                           const AuthoredLegacyCylinderTopology &) = default;
};

struct LegacyCylinderTopology {
    CylinderId cylinder_id;
    CrankshaftId crankshaft_id;
    IntakeId intake_id;
    PortId intake_port_id;
    PortId exhaust_port_id;
    GasVolumeId intake_runner_volume_id;
    GasVolumeId chamber_volume_id;
    GasVolumeId exhaust_primary_volume_id;
    FlowEdgeId plenum_to_runner_edge_id;
    FlowEdgeId intake_valve_edge_id;
    FlowEdgeId exhaust_valve_edge_id;
    FlowEdgeId primary_to_collector_edge_id;
    FlowEdgeId blowby_edge_id;
    RouteId exhaust_route_id;

    friend bool operator==(const LegacyCylinderTopology &,
                           const LegacyCylinderTopology &) = default;
};

template <template <class> class Field> struct LegacyCylinderParametersT {
    Field<double> bore_m;
    Field<double> stroke_m;
    Field<double> crank_radius_m;
    Field<double> connecting_rod_length_m;
    Field<double> deck_height_m;
    Field<double> piston_compression_height_m;
    Field<double> piston_displacement_term_m3;
    Field<double> piston_mass_kg;
    Field<double> connecting_rod_mass_kg;
    Field<double> connecting_rod_inertia_kg_m2;
    Field<double> journal_angle_rad;
    Field<double> ignition_wire_angle_rad;
    Field<double> header_primary_length_m;
    LegacyRestrictionT<Field> piston_blowby;

    friend bool operator==(const LegacyCylinderParametersT &,
                           const LegacyCylinderParametersT &) = default;
};

using AuthoredLegacyCylinderParameters = LegacyCylinderParametersT<AuthoredValue>;

struct LegacyCylinderParameters {
    ResolvedValue<double> bore_m;
    ResolvedValue<double> connecting_rod_length_m;
    ResolvedValue<double> connecting_rod_center_of_mass_from_crank_pin_m;
    ResolvedValue<double> deck_height_m;
    ResolvedValue<double> piston_compression_height_m;
    ResolvedValue<double> piston_wrist_pin_position_m;
    ResolvedValue<double> piston_displacement_term_m3;
    ResolvedValue<double> piston_mass_kg;
    ResolvedValue<double> connecting_rod_mass_kg;
    ResolvedValue<double> connecting_rod_inertia_kg_m2;
    ResolvedValue<double> ignition_wire_angle_rad;
    ResolvedValue<double> header_primary_length_m;
    LegacyRestriction piston_blowby;

    friend bool operator==(const LegacyCylinderParameters &,
                           const LegacyCylinderParameters &) = default;
};

struct LegacyDirectJournalKinematics {
    ResolvedValue<double> stroke_m;
    ResolvedValue<double> crank_radius_m;
    // Cylinder-axis-relative crank journal phase. This retains the exact direct
    // mechanism value and provenance path used before attachments were typed.
    ResolvedValue<double> journal_angle_rad;

    friend bool operator==(const LegacyDirectJournalKinematics &,
                           const LegacyDirectJournalKinematics &) = default;
};

struct LegacyMasterRodJournalKinematics {
    CylinderId master_cylinder_id;
    ResolvedValue<double> throw_radius_m;
    // Authored local phase about the master rod; zero points toward its wrist pin
    // and positive phase rotates counter-clockwise.
    ResolvedValue<double> master_local_phase_rad;

    friend bool operator==(const LegacyMasterRodJournalKinematics &,
                           const LegacyMasterRodJournalKinematics &) = default;
};

using LegacyCylinderKinematics =
    std::variant<std::monostate, LegacyDirectJournalKinematics,
                 LegacyMasterRodJournalKinematics>;

struct AuthoredLegacyCylinderAssembly {
    AuthoredLegacyCylinderTopology topology;
    AuthoredLegacyCylinderParameters parameters;

    friend bool operator==(const AuthoredLegacyCylinderAssembly &,
                           const AuthoredLegacyCylinderAssembly &) = default;
};

struct LegacyCylinderAssembly {
    LegacyCylinderTopology topology;
    LegacyCylinderParameters parameters;
    LegacyCylinderKinematics kinematics;

    friend bool operator==(const LegacyCylinderAssembly &,
                           const LegacyCylinderAssembly &) = default;
};

struct AuthoredLegacyCrankAssembly {
    AuthoredValue<std::string> crankshaft_id;
    AuthoredValue<double> crank_tdc_reference_rad;
    AuthoredValue<double> crankshaft_mass_kg;
    AuthoredValue<double> flywheel_mass_kg;
    AuthoredValue<double> authored_crank_inertia_kg_m2;
    AuthoredValue<double> running_friction_torque_magnitude_nm;

    friend bool operator==(const AuthoredLegacyCrankAssembly &,
                           const AuthoredLegacyCrankAssembly &) = default;
};

struct LegacyCrankAssembly {
    CrankshaftId crankshaft_id;
    ResolvedValue<double> crank_tdc_reference_rad;
    ResolvedValue<double> crankshaft_mass_kg;
    ResolvedValue<double> flywheel_mass_kg;
    ResolvedValue<double> authored_crank_inertia_kg_m2;
    ResolvedValue<double> running_friction_torque_magnitude_nm;

    friend bool operator==(const LegacyCrankAssembly &,
                           const LegacyCrankAssembly &) = default;
};

struct AuthoredLegacyMechanismProfile {
    AuthoredValue<std::string> output_crankshaft_id;
    std::vector<AuthoredLegacyCrankAssembly> cranks;
    std::vector<AuthoredLegacyCylinderAssembly> cylinders;

    friend bool operator==(const AuthoredLegacyMechanismProfile &,
                           const AuthoredLegacyMechanismProfile &) = default;
};

struct LegacyMechanismProfile {
    CrankshaftId output_crankshaft_id;
    std::vector<LegacyCrankAssembly> cranks;
    std::vector<LegacyCylinderAssembly> cylinders;

    friend bool operator==(const LegacyMechanismProfile &,
                           const LegacyMechanismProfile &) = default;
};

[[nodiscard]] const LegacyCrankAssembly *
find_crank(const LegacyMechanismProfile &mechanism, CrankshaftId id) noexcept;
[[nodiscard]] const LegacyCrankAssembly *
find_output_crank(const LegacyMechanismProfile &mechanism) noexcept;

struct AuthoredLegacyIntakeTopology {
    AuthoredValue<std::string> intake_id;
    AuthoredValue<std::string> plenum_volume_id;
    AuthoredValue<std::string> main_throttle_edge_id;
    AuthoredValue<std::string> idle_bypass_edge_id;

    friend bool operator==(const AuthoredLegacyIntakeTopology &,
                           const AuthoredLegacyIntakeTopology &) = default;
};

struct LegacyIntakeTopology {
    IntakeId intake_id;
    GasVolumeId plenum_volume_id;
    FlowEdgeId main_throttle_edge_id;
    FlowEdgeId idle_bypass_edge_id;

    friend bool operator==(const LegacyIntakeTopology &,
                           const LegacyIntakeTopology &) = default;
};

template <template <class> class Field> struct LegacyIntakeParametersT {
    Field<double> plenum_volume_m3;
    Field<double> plenum_cross_section_area_m2;
    Field<double> runner_length_m;
    Field<double> velocity_decay;
    Field<double> idle_throttle_plate_position_01;
    Field<double> main_mixture_lambda;
    LegacyRestrictionT<Field> main_throttle;
    LegacyRestrictionT<Field> idle_bypass;
    LegacyRestrictionT<Field> plenum_to_runner;

    friend bool operator==(const LegacyIntakeParametersT &,
                           const LegacyIntakeParametersT &) = default;
};

using AuthoredLegacyIntakeParameters = LegacyIntakeParametersT<AuthoredValue>;
using LegacyIntakeParameters = LegacyIntakeParametersT<ResolvedValue>;

struct AuthoredLegacyIntakeProfile {
    AuthoredLegacyIntakeTopology topology;
    AuthoredLegacyIntakeParameters parameters;

    friend bool operator==(const AuthoredLegacyIntakeProfile &,
                           const AuthoredLegacyIntakeProfile &) = default;
};

struct LegacyIntakeProfile {
    LegacyIntakeTopology topology;
    LegacyIntakeParameters parameters;

    friend bool operator==(const LegacyIntakeProfile &,
                           const LegacyIntakeProfile &) = default;
};

template <template <class> class Field> struct LegacyValveFlowPointT {
    Field<std::string> sample_id;
    Field<double> lift_m;
    Field<double> source_cfm_at_28_inh2o;
    Field<double> resolved_k;

    friend bool operator==(const LegacyValveFlowPointT &,
                           const LegacyValveFlowPointT &) = default;
};

using AuthoredLegacyValveFlowPoint = LegacyValveFlowPointT<AuthoredValue>;
using LegacyValveFlowPoint = LegacyValveFlowPointT<ResolvedValue>;

template <class Bank, template <class> class Field, class FlowPoint>
struct LegacyBankHeadProfileT {
    Bank bank_id;
    Field<double> chamber_volume_m3;
    Field<double> intake_runner_base_volume_m3;
    Field<double> intake_runner_cross_section_area_m2;
    Field<double> exhaust_runner_base_volume_m3;
    Field<double> exhaust_runner_cross_section_area_m2;
    Field<double> intake_flow_triangle_radius_m;
    Field<double> exhaust_flow_triangle_radius_m;
    std::vector<FlowPoint> intake_flow;
    std::vector<FlowPoint> exhaust_flow;

    friend bool operator==(const LegacyBankHeadProfileT &,
                           const LegacyBankHeadProfileT &) = default;
};

using AuthoredLegacyBankHeadProfile =
    LegacyBankHeadProfileT<AuthoredValue<std::string>, AuthoredValue,
                           AuthoredLegacyValveFlowPoint>;
using LegacyBankHeadProfile =
    LegacyBankHeadProfileT<BankId, ResolvedValue, LegacyValveFlowPoint>;

struct AuthoredLegacyExhaustRouteTopology {
    AuthoredValue<std::string> route_id;
    AuthoredValue<std::string> collector_volume_id;
    AuthoredValue<std::string> collector_outlet_edge_id;

    friend bool operator==(const AuthoredLegacyExhaustRouteTopology &,
                           const AuthoredLegacyExhaustRouteTopology &) = default;
};

struct LegacyExhaustRouteTopology {
    RouteId route_id;
    GasVolumeId collector_volume_id;
    FlowEdgeId collector_outlet_edge_id;

    friend bool operator==(const LegacyExhaustRouteTopology &,
                           const LegacyExhaustRouteTopology &) = default;
};

template <template <class> class Field> struct LegacyExhaustRouteParametersT {
    Field<double> collector_volume_m3;
    Field<double> collector_cross_section_area_m2;
    Field<double> exhaust_system_length_m;
    Field<double> primary_tube_length_m;
    Field<double> velocity_decay;
    Field<double> audio_volume_linear;
    LegacyRestrictionT<Field> primary_to_collector;
    LegacyRestrictionT<Field> collector_outlet;

    friend bool operator==(const LegacyExhaustRouteParametersT &,
                           const LegacyExhaustRouteParametersT &) = default;
};

using AuthoredLegacyExhaustRouteParameters =
    LegacyExhaustRouteParametersT<AuthoredValue>;
using LegacyExhaustRouteParameters = LegacyExhaustRouteParametersT<ResolvedValue>;

struct AuthoredLegacyExhaustRouteProfile {
    AuthoredLegacyExhaustRouteTopology topology;
    AuthoredLegacyExhaustRouteParameters parameters;

    friend bool operator==(const AuthoredLegacyExhaustRouteProfile &,
                           const AuthoredLegacyExhaustRouteProfile &) = default;
};

struct LegacyExhaustRouteProfile {
    LegacyExhaustRouteTopology topology;
    LegacyExhaustRouteParameters parameters;

    friend bool operator==(const LegacyExhaustRouteProfile &,
                           const LegacyExhaustRouteProfile &) = default;
};

template <class Intake, class Head, class ExhaustRoute> struct LegacyGasPathProfileT {
    std::vector<Intake> intakes;
    std::vector<Head> heads;
    std::vector<ExhaustRoute> exhaust_routes;

    friend bool operator==(const LegacyGasPathProfileT &,
                           const LegacyGasPathProfileT &) = default;
};

using AuthoredLegacyGasPathProfile =
    LegacyGasPathProfileT<AuthoredLegacyIntakeProfile, AuthoredLegacyBankHeadProfile,
                          AuthoredLegacyExhaustRouteProfile>;
using LegacyGasPathProfile =
    LegacyGasPathProfileT<LegacyIntakeProfile, LegacyBankHeadProfile,
                          LegacyExhaustRouteProfile>;

template <template <class> class Field> struct LegacyCamShapeT {
    Field<double> maximum_lift_m;
    Field<double> duration_at_reference_lift_rad;
    Field<double> exponent;
    Field<std::uint32_t> construction_steps;
    Field<double> advance_rad;
    Field<double> base_radius_m;

    friend bool operator==(const LegacyCamShapeT &, const LegacyCamShapeT &) = default;
};

using AuthoredLegacyCamShape = LegacyCamShapeT<AuthoredValue>;
using LegacyHarmonicCamShape = LegacyCamShapeT<ResolvedValue>;

struct LegacySampledCamPoint {
    ResolvedValue<std::string> sample_id;
    ResolvedValue<double> angle_rad;
    ResolvedValue<double> lift_m;

    friend bool operator==(const LegacySampledCamPoint &,
                           const LegacySampledCamPoint &) = default;
};

struct LegacySampledCamShape {
    ResolvedValue<double> triangle_radius_rad;
    std::vector<LegacySampledCamPoint> samples;
    ResolvedValue<double> advance_rad;
    ResolvedValue<double> base_radius_m;

    friend bool operator==(const LegacySampledCamShape &,
                           const LegacySampledCamShape &) = default;
};

using LegacyCamShape = std::variant<LegacyHarmonicCamShape, LegacySampledCamShape>;

struct AuthoredLegacyCamLobe {
    AuthoredValue<std::string> cylinder_id;
    AuthoredValue<std::string> port_id;
    AuthoredValue<double> crank_center_rad;

    friend bool operator==(const AuthoredLegacyCamLobe &,
                           const AuthoredLegacyCamLobe &) = default;
};

struct LegacyCamLobe {
    CylinderId cylinder_id;
    PortId port_id;
    std::uint32_t profile_index = 0;
    ResolvedValue<double> crank_center_rad;

    friend bool operator==(const LegacyCamLobe &, const LegacyCamLobe &) = default;
};

template <class Shape, class Lobe> struct LegacyCamshaftProfileT {
    Shape shape;
    std::vector<Lobe> lobes;

    friend bool operator==(const LegacyCamshaftProfileT &,
                           const LegacyCamshaftProfileT &) = default;
};

using AuthoredLegacyCamshaftProfile =
    LegacyCamshaftProfileT<AuthoredLegacyCamShape, AuthoredLegacyCamLobe>;

// A resolved role spans every physical camshaft serving that valve role. Profiles
// are ordered by first use in engine-cylinder order; each lobe selects the profile
// owned by its bank-local physical camshaft.
struct LegacyCamshaftProfile {
    std::vector<LegacyCamShape> profiles;
    std::vector<LegacyCamLobe> lobes;

    friend bool operator==(const LegacyCamshaftProfile &,
                           const LegacyCamshaftProfile &) = default;
};

template <class Camshaft> struct LegacyValvetrainProfileT {
    Camshaft intake;
    Camshaft exhaust;

    friend bool operator==(const LegacyValvetrainProfileT &,
                           const LegacyValvetrainProfileT &) = default;
};

using AuthoredLegacyValvetrainProfile =
    LegacyValvetrainProfileT<AuthoredLegacyCamshaftProfile>;

struct LegacyVtecActivationProfile {
    ResolvedValue<double> minimum_engine_speed_rad_s;
    ResolvedValue<double> minimum_mean_manifold_pressure_pa_abs;
    ResolvedValue<double> minimum_throttle_linkage_opening_01;

    friend bool operator==(const LegacyVtecActivationProfile &,
                           const LegacyVtecActivationProfile &) = default;
};

// Pristine owns VTEC selection at the cylinder head.  The executable contract
// normalizes that ownership to the bank served by the head so each cylinder can
// select its bank's coherent intake/exhaust pair while all selectors observe the
// same engine-global operating state.
struct LegacyVtecBankSelector {
    BankId bank_id;
    LegacyVtecActivationProfile activation;

    friend bool operator==(const LegacyVtecBankSelector &,
                           const LegacyVtecBankSelector &) = default;
};

struct LegacyVtecAlternateCamProfile {
    LegacyCamshaftProfile intake;
    LegacyCamshaftProfile exhaust;
    std::vector<LegacyVtecBankSelector> selectors;

    friend bool operator==(const LegacyVtecAlternateCamProfile &,
                           const LegacyVtecAlternateCamProfile &) = default;
};

struct LegacyValvetrainProfile {
    LegacyCamshaftProfile intake;
    LegacyCamshaftProfile exhaust;
    std::optional<LegacyVtecAlternateCamProfile> alternate;

    friend bool operator==(const LegacyValvetrainProfile &,
                           const LegacyValvetrainProfile &) = default;
};

template <template <class> class Field> struct LegacyTimingPointT {
    Field<std::string> sample_id;
    Field<double> angular_speed_rad_s;
    Field<double> timing_advance_rad;

    friend bool operator==(const LegacyTimingPointT &,
                           const LegacyTimingPointT &) = default;
};

using AuthoredLegacyTimingPoint = LegacyTimingPointT<AuthoredValue>;
using LegacyTimingPoint = LegacyTimingPointT<ResolvedValue>;

struct AuthoredLegacyIgnitionProfile {
    AuthoredValue<std::vector<std::string>> firing_order;
    AuthoredValue<double> timing_curve_triangle_radius_rad_s;
    std::vector<AuthoredLegacyTimingPoint> timing_curve;
    AuthoredValue<double> limiter_speed_rpm;
    AuthoredValue<double> limiter_hold_s;
    AuthoredValue<double> declared_redline_rpm;

    friend bool operator==(const AuthoredLegacyIgnitionProfile &,
                           const AuthoredLegacyIgnitionProfile &) = default;
};

struct LegacyIgnitionProfile {
    ResolvedValue<std::vector<CylinderId>> firing_order;
    ResolvedValue<double> timing_curve_triangle_radius_rad_s;
    std::vector<LegacyTimingPoint> timing_curve;
    ResolvedValue<double> limiter_speed_rpm;
    ResolvedValue<double> limiter_hold_s;
    ResolvedValue<double> declared_redline_rpm;

    friend bool operator==(const LegacyIgnitionProfile &,
                           const LegacyIgnitionProfile &) = default;
};

template <template <class> class Field> struct LegacyFlameSpeedPointT {
    Field<std::string> sample_id;
    Field<double> turbulence;
    Field<double> flame_speed_ratio;

    friend bool operator==(const LegacyFlameSpeedPointT &,
                           const LegacyFlameSpeedPointT &) = default;
};

using AuthoredLegacyFlameSpeedPoint = LegacyFlameSpeedPointT<AuthoredValue>;
using LegacyFlameSpeedPoint = LegacyFlameSpeedPointT<ResolvedValue>;

template <template <class> class Field, class FlamePoint> struct LegacyFuelProfileT {
    Field<std::string> fuel_id;
    Field<double> molecular_mass_kg_per_mol;
    Field<double> energy_density_j_per_kg;
    Field<double> molecular_afr;
    Field<double> maximum_burning_efficiency_01;
    Field<double> burning_efficiency_randomness_01;
    Field<double> low_efficiency_attenuation_01;
    Field<double> maximum_turbulence_effect;
    Field<double> maximum_dilution_effect;
    Field<double> lbv_multiplier;
    Field<double> turbulence_to_flame_speed_ratio_triangle_radius;
    std::vector<FlamePoint> turbulence_to_flame_speed_ratio;

    friend bool operator==(const LegacyFuelProfileT &,
                           const LegacyFuelProfileT &) = default;
};

using AuthoredLegacyFuelProfile =
    LegacyFuelProfileT<AuthoredValue, AuthoredLegacyFlameSpeedPoint>;
using LegacyFuelProfile = LegacyFuelProfileT<ResolvedValue, LegacyFlameSpeedPoint>;

template <template <class> class Field> struct LegacyExcitationPressureGainsT {
    Field<double> gauge_static;
    Field<double> dynamic_forward;
    Field<double> dynamic_reverse;

    friend bool operator==(const LegacyExcitationPressureGainsT &,
                           const LegacyExcitationPressureGainsT &) = default;
};

using AuthoredLegacyExcitationPressureGains =
    LegacyExcitationPressureGainsT<AuthoredValue>;
using LegacyExcitationPressureGains = LegacyExcitationPressureGainsT<ResolvedValue>;

struct AuthoredLegacyExcitationCylinderPath {
    AuthoredValue<std::string> cylinder_id;
    AuthoredValue<std::string> route_id;
    AuthoredValue<double> header_primary_length_m;
    AuthoredValue<double> sound_attenuation_linear;

    friend bool operator==(const AuthoredLegacyExcitationCylinderPath &,
                           const AuthoredLegacyExcitationCylinderPath &) = default;
};

struct LegacyExcitationCylinderPath {
    CylinderId cylinder_id;
    RouteId route_id;
    ResolvedValue<double> header_primary_length_m;
    ResolvedValue<double> sound_attenuation_linear;

    friend bool operator==(const LegacyExcitationCylinderPath &,
                           const LegacyExcitationCylinderPath &) = default;
};

struct AuthoredLegacyExcitationRoute {
    AuthoredValue<std::string> route_id;
    AuthoredValue<double> exhaust_system_length_m;
    AuthoredValue<double> audio_volume_linear;

    friend bool operator==(const AuthoredLegacyExcitationRoute &,
                           const AuthoredLegacyExcitationRoute &) = default;
};

struct LegacyExcitationRoute {
    RouteId route_id;
    ResolvedValue<double> exhaust_system_length_m;
    ResolvedValue<double> audio_volume_linear;

    friend bool operator==(const LegacyExcitationRoute &,
                           const LegacyExcitationRoute &) = default;
};

template <template <class> class Field, class CylinderRef, class CylinderPath,
          class Route>
struct LegacyReferenceExcitationProfileT {
    Field<double> reference_atmosphere_pa_abs;
    Field<double> legacy_propagation_speed_m_s;
    Field<double> excitation_scale;
    Field<double> filtered_speed_threshold_rpm;
    Field<std::uint32_t> filtered_speed_exponent;
    LegacyExcitationPressureGainsT<Field> pressure_gains;
    Field<double> cylinder_count_divisor;
    Field<double> inverse_length_exponent;
    Field<std::vector<CylinderRef>> cylinder_accumulation_order;
    std::vector<CylinderPath> cylinder_paths;
    std::vector<Route> routes;

    friend bool operator==(const LegacyReferenceExcitationProfileT &,
                           const LegacyReferenceExcitationProfileT &) = default;
};

using AuthoredLegacyReferenceExcitationProfile =
    LegacyReferenceExcitationProfileT<AuthoredValue, std::string,
                                      AuthoredLegacyExcitationCylinderPath,
                                      AuthoredLegacyExcitationRoute>;
using LegacyReferenceExcitationProfile = LegacyReferenceExcitationProfileT<
    ResolvedValue, CylinderId, LegacyExcitationCylinderPath, LegacyExcitationRoute>;

template <template <class> class Field> struct DirectThrottleControllerV1T {
    Field<double> gamma;

    friend bool operator==(const DirectThrottleControllerV1T &,
                           const DirectThrottleControllerV1T &) = default;
};

template <template <class> class Field> struct GovernorThrottleControllerV1T {
    Field<double> minimum_engine_speed_rad_s;
    Field<double> maximum_engine_speed_rad_s;
    Field<double> minimum_velocity_per_s;
    Field<double> maximum_velocity_per_s;
    Field<double> k_s;
    Field<double> k_d_per_s;
    Field<double> gamma;

    friend bool operator==(const GovernorThrottleControllerV1T &,
                           const GovernorThrottleControllerV1T &) = default;
};

using AuthoredDirectThrottleControllerV1 = DirectThrottleControllerV1T<AuthoredValue>;
using DirectThrottleControllerV1 = DirectThrottleControllerV1T<ResolvedValue>;
using AuthoredGovernorThrottleControllerV1 =
    GovernorThrottleControllerV1T<AuthoredValue>;
using GovernorThrottleControllerV1 = GovernorThrottleControllerV1T<ResolvedValue>;
using AuthoredThrottleControllerV1 = std::variant<AuthoredDirectThrottleControllerV1,
                                                  AuthoredGovernorThrottleControllerV1>;
using ThrottleControllerV1 =
    std::variant<DirectThrottleControllerV1, GovernorThrottleControllerV1>;

struct AuthoredLowOrderEngineCoreV1 {
    AuthoredLegacyMechanismProfile mechanism;
    AuthoredThrottleControllerV1 throttle_controller;
    AuthoredLegacyGasPathProfile gas_path;
    AuthoredLegacyValvetrainProfile valvetrain;
    AuthoredLegacyIgnitionProfile ignition;
    AuthoredLegacyFuelProfile fuel;
    AuthoredLegacyReferenceExcitationProfile excitation;

    friend bool operator==(const AuthoredLowOrderEngineCoreV1 &,
                           const AuthoredLowOrderEngineCoreV1 &) = default;
};

struct LowOrderEngineCoreV1 {
    LegacyMechanismProfile mechanism;
    ThrottleControllerV1 throttle_controller;
    LegacyGasPathProfile gas_path;
    LegacyValvetrainProfile valvetrain;
    LegacyIgnitionProfile ignition;
    LegacyFuelProfile fuel;
    LegacyReferenceExcitationProfile excitation;

    friend bool operator==(const LowOrderEngineCoreV1 &,
                           const LowOrderEngineCoreV1 &) = default;
};

struct AuthoredChenFlynnCycleMeanAggregateLossV1 {
    AuthoredValue<double> constant_fmep_bar;
    AuthoredValue<double> peak_pressure_coefficient;
    AuthoredValue<double> mean_piston_speed_coefficient_bar_s_per_m;
    AuthoredValue<double> mean_piston_speed_squared_coefficient_bar_s2_per_m2;
    AuthoredValue<double> required_oil_temperature_k;
    AuthoredValue<TorqueTermMask> included_terms;

    friend bool operator==(const AuthoredChenFlynnCycleMeanAggregateLossV1 &,
                           const AuthoredChenFlynnCycleMeanAggregateLossV1 &) = default;
};

struct ChenFlynnCycleMeanAggregateLossV1 {
    ResolvedValue<double> constant_fmep_bar;
    ResolvedValue<double> peak_pressure_coefficient;
    ResolvedValue<double> mean_piston_speed_coefficient_bar_s_per_m;
    ResolvedValue<double> mean_piston_speed_squared_coefficient_bar_s2_per_m2;
    ResolvedValue<double> required_oil_temperature_k;
    ResolvedValue<TorqueTermMask> included_terms;

    friend bool operator==(const ChenFlynnCycleMeanAggregateLossV1 &,
                           const ChenFlynnCycleMeanAggregateLossV1 &) = default;
};

struct AuthoredAccessoryConfigurationIdentityV1 {
    AuthoredValue<std::string> configuration_id;
    AuthoredValue<Sha256Digest> content_sha256;

    friend bool operator==(const AuthoredAccessoryConfigurationIdentityV1 &,
                           const AuthoredAccessoryConfigurationIdentityV1 &) = default;
};

struct AccessoryConfigurationIdentityV1 {
    ResolvedValue<std::string> configuration_id;
    ResolvedValue<Sha256Digest> content_sha256;

    friend bool operator==(const AccessoryConfigurationIdentityV1 &,
                           const AccessoryConfigurationIdentityV1 &) = default;
};

enum class StarterCapabilityType : std::uint8_t {
    unspecified,
    mechanically_disengaged,
    cranking,
};

template <template <class> class Field> struct StarterCapabilityV1T {
    Field<StarterCapabilityType> type;
    Field<double> maximum_torque_nm;
    Field<double> target_speed_rad_s;
    Field<TorqueTermMask> included_terms;

    friend bool operator==(const StarterCapabilityV1T &,
                           const StarterCapabilityV1T &) = default;
};

using AuthoredStarterCapabilityV1 = StarterCapabilityV1T<AuthoredValue>;
using StarterCapabilityV1 = StarterCapabilityV1T<ResolvedValue>;

struct AuthoredLowOrderOperatingPointV1Profile {
    AuthoredLowOrderEngineCoreV1 core;
    AuthoredChenFlynnCycleMeanAggregateLossV1 aggregate_loss;
    AuthoredAccessoryConfigurationIdentityV1 accessory_configuration;
    AuthoredStarterCapabilityV1 starter;
    AuthoredValue<MethodSelection> cycle_quadrature;

    friend bool operator==(const AuthoredLowOrderOperatingPointV1Profile &,
                           const AuthoredLowOrderOperatingPointV1Profile &) = default;
};

struct LowOrderOperatingPointV1Profile {
    LowOrderEngineCoreV1 core;
    ChenFlynnCycleMeanAggregateLossV1 aggregate_loss;
    AccessoryConfigurationIdentityV1 accessory_configuration;
    StarterCapabilityV1 starter;
    ResolvedValue<MethodIdentity> cycle_quadrature;

    friend bool operator==(const LowOrderOperatingPointV1Profile &,
                           const LowOrderOperatingPointV1Profile &) = default;
};

using AuthoredExecutablePhysicsProfile =
    std::variant<AuthoredLowOrderOperatingPointV1Profile>;
using ExecutablePhysicsProfile = std::variant<LowOrderOperatingPointV1Profile>;

[[nodiscard]] ValidationReport validate(const AuthoredExecutablePhysicsProfile &profile,
                                        const ProvenanceLedger &provenance);
[[nodiscard]] ValidationReport validate(const ExecutablePhysicsProfile &profile,
                                        const EngineSpec &engine,
                                        const ProvenanceLedger &provenance);

} // namespace engine_sim_offline::contract
