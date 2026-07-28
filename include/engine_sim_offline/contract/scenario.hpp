#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstdint>
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

inline constexpr std::string_view kAdjacentCycleBlockMeanConvergenceMethodId =
    "adjacent-nonoverlapping-cycle-block-mean-v1";
inline constexpr std::uint32_t kAdjacentCycleBlockMeanConvergenceMethodVersion = 1;
inline constexpr Sha256Digest
    kAdjacentCycleBlockMeanConvergenceMethodConfigurationSha256{{
        0xb1, 0xa1, 0xad, 0x37, 0x08, 0x8c, 0xeb, 0x2a, 0x88, 0xdb, 0xb5,
        0xdb, 0x4b, 0xb3, 0x85, 0x06, 0x7f, 0xef, 0xa6, 0xb2, 0x44, 0xb0,
        0x91, 0xde, 0xee, 0xbe, 0x5b, 0xf3, 0x8a, 0xca, 0xc4, 0x06,
    }};

[[nodiscard]] const MethodIdentity &
adjacent_cycle_block_mean_convergence_method_identity();

struct ConvergenceSettling {
    ResolvedValue<MethodIdentity> method;
    ResolvedValue<double> minimum_warm_up_duration_s;
    ResolvedValue<double> minimum_settling_duration_s;
    ResolvedValue<double> maximum_preparation_duration_s;
    ResolvedValue<std::uint32_t> comparison_cycle_count;
    ResolvedValue<double> cycle_mean_torque_tolerance_nm;
    ResolvedValue<double> pressure_tolerance_pa;

    friend bool operator==(const ConvergenceSettling &,
                           const ConvergenceSettling &) = default;
};

using PreparationPolicy = std::variant<FixedSettling, ConvergenceSettling>;

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

    friend bool operator==(const InertialDyno &, const InertialDyno &) = default;
};

using ScenarioMode = std::variant<HeldSpeed, PrescribedKinematicSweep,
                                  LoadTargetHeldCapture, InertialDyno>;

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
