#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::contract {

struct EngineSpec;

struct AmbientConditions {
    ResolvedValue<double> pressure_pa_abs;
    ResolvedValue<double> temperature_k;
    ResolvedValue<double> relative_humidity_01;

    friend bool operator==(const AmbientConditions &,
                           const AmbientConditions &) = default;
};

struct FuelDefinition {
    ResolvedValue<std::string> fuel_id;
    ResolvedValue<double> lower_heating_value_j_per_kg;
    ResolvedValue<double> stoichiometric_air_fuel_mass_ratio;

    friend bool operator==(const FuelDefinition &, const FuelDefinition &) = default;
};

struct InitialThermalState {
    ResolvedValue<double> gas_temperature_k;
    ResolvedValue<double> wall_temperature_k;
    ResolvedValue<double> coolant_temperature_k;
    ResolvedValue<double> oil_temperature_k;

    friend bool operator==(const InitialThermalState &,
                           const InitialThermalState &) = default;
};

struct CrankcaseConditions {
    ResolvedValue<double> pressure_pa_abs;
    ResolvedValue<double> temperature_k;

    friend bool operator==(const CrankcaseConditions &,
                           const CrankcaseConditions &) = default;
};

struct OperatingState {
    bool ignition_enabled = false;
    bool fuel_enabled = false;
    bool starter_enabled = false;
    bool dyno_enabled = false;
    bool limiter_enabled = false;

    friend bool operator==(const OperatingState &, const OperatingState &) = default;
};

struct OperatingStatePoint {
    std::string event_id;
    double time_s = 0.0;
    OperatingState state;

    friend bool operator==(const OperatingStatePoint &,
                           const OperatingStatePoint &) = default;
};

enum class TrajectoryInterpolation : std::uint8_t {
    right_continuous_hold,
    linear,
};

struct ScalarTrajectoryPoint {
    double time_s = 0.0;
    double value = 0.0;

    friend bool operator==(const ScalarTrajectoryPoint &,
                           const ScalarTrajectoryPoint &) = default;
};

struct ScalarTrajectory {
    TrajectoryInterpolation interpolation =
        TrajectoryInterpolation::right_continuous_hold;
    std::vector<ScalarTrajectoryPoint> points;
    std::string resolution_id;

    friend bool operator==(const ScalarTrajectory &,
                           const ScalarTrajectory &) = default;
};

enum class RpmSampleSemantics : std::uint8_t {
    post_step_rpm,
};

struct FixedRateRpmTrajectory {
    RationalRateHz rate;
    std::uint64_t first_step_index = 0;
    RpmSampleSemantics semantics = RpmSampleSemantics::post_step_rpm;
    std::vector<double> post_step_rpm;
    Sha256Digest samples_f64le_sha256;
    std::string resolution_id;

    friend bool operator==(const FixedRateRpmTrajectory &,
                           const FixedRateRpmTrajectory &) = default;
};

struct RpmTrajectory {
    std::variant<ScalarTrajectory, FixedRateRpmTrajectory> rpm;
    ResolvedValue<double> initial_theta_rad;
    ResolvedValue<MethodIdentity> kinematic_resolution;

    friend bool operator==(const RpmTrajectory &, const RpmTrajectory &) = default;
};

struct FixedSettling {
    ResolvedValue<double> warm_up_duration_s;
    ResolvedValue<double> settling_duration_s;

    friend bool operator==(const FixedSettling &, const FixedSettling &) = default;
};

inline constexpr std::string_view kFixedHorizonCycleSamplingMethodId =
    "fixed-horizon-trailing-complete-cycle-sample-v1";
inline constexpr std::uint32_t kFixedHorizonCycleSamplingMethodVersion = 1;
inline constexpr Sha256Digest kFixedHorizonCycleSamplingMethodConfigurationSha256{{
    0x9e, 0xfb, 0xb1, 0x5d, 0x0a, 0xd2, 0x7d, 0x3f, 0x97, 0xd7, 0x5d,
    0x13, 0x5b, 0x64, 0x2c, 0x9a, 0x7f, 0xee, 0xc6, 0x61, 0x0c, 0x50,
    0xe7, 0xec, 0x82, 0x52, 0x3e, 0xc6, 0x28, 0x08, 0xda, 0x63,
}};

[[nodiscard]] const MethodIdentity &fixed_horizon_cycle_sampling_method_identity();

struct FixedHorizonCycleSampling {
    ResolvedValue<MethodIdentity> method;
    ResolvedValue<double> fixed_preparation_horizon_s;
    ResolvedValue<std::uint32_t> trailing_complete_cycle_count;

    friend bool operator==(const FixedHorizonCycleSampling &,
                           const FixedHorizonCycleSampling &) = default;
};

using PreparationPolicy = std::variant<FixedSettling, FixedHorizonCycleSampling>;

struct HeldSpeed {
    ResolvedValue<double> engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    ResolvedValue<double> throttle_01;

    friend bool operator==(const HeldSpeed &, const HeldSpeed &) = default;
};

struct PrescribedKinematicSweep {
    RpmTrajectory trajectory;
    ScalarTrajectory throttle_01;

    friend bool operator==(const PrescribedKinematicSweep &,
                           const PrescribedKinematicSweep &) = default;
};

struct HeldDyno {
    ResolvedValue<double> initial_engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    FixedRateRpmTrajectory target_engine_speed_rpm;
    ScalarTrajectory throttle_01;
    ResolvedValue<double> maximum_absorbing_torque_nm;
    ResolvedValue<double> maximum_driving_torque_nm;
    ResolvedValue<MethodIdentity> constraint_method;

    friend bool operator==(const HeldDyno &, const HeldDyno &) = default;
};

struct LoadTargetHeldCapture {
    ResolvedValue<double> engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    ResolvedValue<double> target_net_bmep_pa;
    ResolvedValue<double> target_tolerance_pa;
    ResolvedValue<double> throttle_lower_bound_01;
    ResolvedValue<double> throttle_upper_bound_01;
    ResolvedValue<MethodIdentity> search_method;

    friend bool operator==(const LoadTargetHeldCapture &,
                           const LoadTargetHeldCapture &) = default;
};

struct BrakeTorquePoint {
    double angular_speed_rad_s = 0.0;
    double resisting_torque_nm = 0.0;

    friend bool operator==(const BrakeTorquePoint &,
                           const BrakeTorquePoint &) = default;
};

struct InertialDyno {
    ResolvedValue<double> initial_engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    ResolvedValue<double> equivalent_inertia_kg_m2;
    ScalarTrajectory throttle_01;
    std::vector<BrakeTorquePoint> brake_curve;
    std::string brake_curve_resolution_id;
    ResolvedValue<MethodIdentity> crank_dynamics_method;
    // A pull remains a fixed-horizon render. This target identifies the requested
    // upward crossing for result evidence; reaching it does not end capture early.
    ResolvedValue<double> target_engine_speed_rpm;
    // Evaluation/interpolation of the authored passive curve is executable method
    // identity, distinct from provenance for the curve values themselves.
    ResolvedValue<MethodIdentity> brake_torque_method;

    friend bool operator==(const InertialDyno &, const InertialDyno &) = default;
};

struct FreeEngine {
    ResolvedValue<double> initial_engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    // Cycle-mean crank-referred inertia derived from the engine mechanism.
    ResolvedValue<double> engine_baseline_inertia_kg_m2;
    // Additional inertia coupled by a test rig or other external attachment.
    ResolvedValue<double> attached_inertia_kg_m2;
    // Exact cycle-mean engine-plus-attachment reference used for validation and
    // provenance. Runtime mechanics evaluates engine M(theta) and adds the same
    // constant attached inertia at each left boundary.
    ResolvedValue<double> total_equivalent_inertia_kg_m2;
    ScalarTrajectory throttle_01;
    ScalarTrajectory external_resisting_torque_nm;
    ResolvedValue<MethodIdentity> crank_dynamics_method;

    friend bool operator==(const FreeEngine &, const FreeEngine &) = default;
};

struct ForwardGearSpec {
    GearId id;
    // One-based authored order. Neutral is not a gear entry.
    ResolvedValue<std::uint32_t> authored_ordinal;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<double> ratio;

    friend bool operator==(const ForwardGearSpec &, const ForwardGearSpec &) = default;
};

struct ForwardTransmissionSpec {
    TransmissionId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<double> maximum_clutch_torque_nm;
    // Authored forward-gear order is executable product data.
    std::vector<ForwardGearSpec> gears;

    friend bool operator==(const ForwardTransmissionSpec &,
                           const ForwardTransmissionSpec &) = default;
};

struct ForwardVehicleSpec {
    VehicleId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<double> mass_kg;
    ResolvedValue<double> drag_coefficient;
    ResolvedValue<double> frontal_area_m2;
    ResolvedValue<double> differential_ratio;
    ResolvedValue<double> tire_radius_m;
    ResolvedValue<double> rolling_resistance_force_n;
    // Missing means that this rig has no service-brake actuator.
    std::optional<ResolvedValue<double>> maximum_service_brake_force_n;

    friend bool operator==(const ForwardVehicleSpec &,
                           const ForwardVehicleSpec &) = default;
};

struct FreeVehicleRig {
    RigId id;
    ResolvedValue<std::string> semantic_id;
    ForwardVehicleSpec vehicle;
    ForwardTransmissionSpec transmission;

    friend bool operator==(const FreeVehicleRig &, const FreeVehicleRig &) = default;
};

struct GearSelectionPoint {
    std::string event_id;
    double time_s = 0.0;
    // Null is neutral; a value names one entry in rig.transmission.gears.
    std::optional<GearId> gear_id;

    friend bool operator==(const GearSelectionPoint &,
                           const GearSelectionPoint &) = default;
};

struct ScalarControlPoint {
    std::string event_id;
    double time_s = 0.0;
    double value = 0.0;

    friend bool operator==(const ScalarControlPoint &,
                           const ScalarControlPoint &) = default;
};

struct FreeVehicle {
    ResolvedValue<double> initial_engine_speed_rpm;
    ResolvedValue<double> initial_theta_rad;
    ResolvedValue<double> engine_baseline_inertia_kg_m2;
    ResolvedValue<double> initial_vehicle_speed_m_s;
    // The selected engine-owned rig is copied into the immutable scenario. Its
    // ResolvedValue leaves retain their original engine-provenance resolution IDs.
    FreeVehicleRig rig;
    ScalarTrajectory throttle_01;
    // All three lanes are right-continuous, start at time zero, and apply at the
    // left boundary of the corresponding physics step.
    ResolvedValue<std::vector<GearSelectionPoint>> selected_gear;
    ResolvedValue<std::vector<ScalarControlPoint>> clutch_engagement_01;
    ResolvedValue<std::vector<ScalarControlPoint>> service_brake_application_01;
    ResolvedValue<MethodIdentity> crank_dynamics_method;
    ResolvedValue<MethodIdentity> road_load_method;
    ResolvedValue<MethodIdentity> clutch_coupling_method;
    // Identifies the exact bounded coupled solve, including row order and
    // iteration count; the primitive method identities above identify its rows.
    ResolvedValue<MethodIdentity> drivetrain_dynamics_method;

    friend bool operator==(const FreeVehicle &, const FreeVehicle &) = default;
};

using ScenarioMode =
    std::variant<HeldSpeed, PrescribedKinematicSweep, HeldDyno, LoadTargetHeldCapture,
                 InertialDyno, FreeEngine, FreeVehicle>;

struct RenderQuality {
    std::string profile_id;
    std::uint32_t version = 0;
    std::uint32_t capture_block_capacity_frames = 0;
    std::uint32_t event_journal_capacity_records = 0;

    friend bool operator==(const RenderQuality &, const RenderQuality &) = default;
};

struct RenderScenario {
    std::uint32_t schema_version = 0;
    std::string scenario_id;
    std::string engine_profile_id;
    AmbientConditions ambient;
    FuelDefinition fuel;
    InitialThermalState initial_thermal_state;
    CrankcaseConditions crankcase;
    PreparationPolicy preparation;
    ResolvedValue<std::vector<OperatingStatePoint>> operating_state;
    ResolvedValue<double> total_duration_s;
    ResolvedValue<double> audible_start_s;
    ResolvedValue<double> audible_duration_s;
    RenderRates rates;
    std::string rates_resolution_id;
    ResolvedValue<RenderQuality> quality;
    ResolvedValue<std::uint64_t> public_seed;
    ScenarioMode mode;
    std::string mode_resolution_id;
    std::string provenance_schema_id;

    friend bool operator==(const RenderScenario &, const RenderScenario &) = default;
};

// Hashes the raw IEEE-754 binary64 bit pattern of each sample in little-endian
// order, with no header or sample-count prefix.
[[nodiscard]] Sha256Digest
canonical_binary64_le_sha256(std::span<const double> samples) noexcept;
[[nodiscard]] ValidationReport validate(const RenderScenario &scenario,
                                        const ProvenanceLedger &provenance);
// Resolves every fixed-rate horizon and authored control boundary to an integer
// frame grid. This check is provenance-independent and is also reused by the
// deterministic scheduler.
[[nodiscard]] ValidationReport validate_clock_grid(const RenderScenario &scenario);
[[nodiscard]] ValidationReport validate_for_engine(const RenderScenario &scenario,
                                                   const EngineSpec &spec);

} // namespace engine_sim_offline::contract
