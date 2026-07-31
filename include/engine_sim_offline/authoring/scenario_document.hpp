#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/quantity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::authoring {

struct ScenarioTag;
struct ScenarioEventTag;
struct TelemetryChannelTag;

using ScenarioId = StableId<ScenarioTag>;
using ScenarioEventId = StableId<ScenarioEventTag>;
using TelemetryChannelRef = StableRef<TelemetryChannelTag>;

enum class TrajectoryInterpolation : std::uint8_t {
    right_continuous_hold,
    linear,
};

struct ScalarTrajectoryPoint {
    Quantity time;
    double value = 0.0;

    friend bool operator==(const ScalarTrajectoryPoint &,
                           const ScalarTrajectoryPoint &) = default;
};

struct ScalarTrajectory {
    TrajectoryInterpolation interpolation =
        TrajectoryInterpolation::right_continuous_hold;
    std::vector<ScalarTrajectoryPoint> points;

    friend bool operator==(const ScalarTrajectory &,
                           const ScalarTrajectory &) = default;
};

struct QuantityTrajectoryPoint {
    Quantity time;
    Quantity value;

    friend bool operator==(const QuantityTrajectoryPoint &,
                           const QuantityTrajectoryPoint &) = default;
};

struct QuantityTrajectory {
    QuantityDimension value_dimension = QuantityDimension::dimensionless;
    TrajectoryInterpolation interpolation =
        TrajectoryInterpolation::right_continuous_hold;
    std::vector<QuantityTrajectoryPoint> points;

    friend bool operator==(const QuantityTrajectory &,
                           const QuantityTrajectory &) = default;
};

struct AmbientConditions {
    Quantity pressure;
    Quantity temperature;
    double relative_humidity_01 = 0.0;

    friend bool operator==(const AmbientConditions &,
                           const AmbientConditions &) = default;
};

struct InitialThermalState {
    Quantity gas_temperature;
    Quantity wall_temperature;
    Quantity coolant_temperature;
    Quantity oil_temperature;

    friend bool operator==(const InitialThermalState &,
                           const InitialThermalState &) = default;
};

struct CrankcaseConditions {
    Quantity pressure;
    Quantity temperature;

    friend bool operator==(const CrankcaseConditions &,
                           const CrankcaseConditions &) = default;
};

struct InitialOperatingState {
    Quantity engine_speed;
    Quantity crank_angle;
    bool ignition_enabled = false;
    bool fuel_enabled = false;
    bool starter_enabled = false;
    bool dyno_enabled = false;
    bool limiter_enabled = false;

    friend bool operator==(const InitialOperatingState &,
                           const InitialOperatingState &) = default;
};

struct FixedSettlingPreparation {
    Quantity warm_up_duration;
    Quantity settling_duration;

    friend bool operator==(const FixedSettlingPreparation &,
                           const FixedSettlingPreparation &) = default;
};

struct FixedHorizonPreparation {
    Quantity preparation_duration;
    std::uint32_t trailing_complete_cycle_count = 0;

    friend bool operator==(const FixedHorizonPreparation &,
                           const FixedHorizonPreparation &) = default;
};

using Preparation = std::variant<FixedSettlingPreparation, FixedHorizonPreparation>;

struct FreeEngineMode {
    // Additional inertia coupled to the crank by the surrounding test rig. The
    // engine's own crank-referred inertia is derived from its mechanism model.
    std::optional<Quantity> attached_inertia;
    ScalarTrajectory throttle_01;
    std::optional<QuantityTrajectory> external_resisting_torque;

    friend bool operator==(const FreeEngineMode &, const FreeEngineMode &) = default;
};

struct FreeVehicleMode {
    RigRef rig;
    GearRef initial_gear;
    double initial_clutch_position_01 = 0.0;
    ScalarTrajectory throttle_01;

    friend bool operator==(const FreeVehicleMode &, const FreeVehicleMode &) = default;
};

struct HeldSpeedMode {
    Quantity target_engine_speed;
    ScalarTrajectory throttle_01;

    friend bool operator==(const HeldSpeedMode &, const HeldSpeedMode &) = default;
};

struct HeldDynoMode {
    QuantityTrajectory target_engine_speed;
    Quantity maximum_absorbing_torque;
    Quantity maximum_driving_torque;
    ScalarTrajectory throttle_01;

    friend bool operator==(const HeldDynoMode &, const HeldDynoMode &) = default;
};

struct LoadTargetHeldMode {
    Quantity target_engine_speed;
    Quantity target_net_bmep;
    Quantity target_tolerance;
    double throttle_lower_bound_01 = 0.0;
    double throttle_upper_bound_01 = 1.0;

    friend bool operator==(const LoadTargetHeldMode &,
                           const LoadTargetHeldMode &) = default;
};

struct ExternalSpeedMode {
    QuantityTrajectory engine_speed;
    ScalarTrajectory throttle_01;

    friend bool operator==(const ExternalSpeedMode &,
                           const ExternalSpeedMode &) = default;
};

struct BrakeTorquePoint {
    Quantity engine_speed;
    Quantity resisting_torque;

    friend bool operator==(const BrakeTorquePoint &,
                           const BrakeTorquePoint &) = default;
};

struct InertialDynoMode {
    Quantity equivalent_inertia;
    ScalarTrajectory throttle_01;
    std::vector<BrakeTorquePoint> brake_curve;
    Quantity target_engine_speed;

    friend bool operator==(const InertialDynoMode &,
                           const InertialDynoMode &) = default;
};

using ScenarioMode =
    std::variant<FreeEngineMode, FreeVehicleMode, HeldSpeedMode, HeldDynoMode,
                 LoadTargetHeldMode, ExternalSpeedMode, InertialDynoMode>;

struct OperatingStatePatch {
    std::optional<bool> ignition_enabled;
    std::optional<bool> fuel_enabled;
    std::optional<bool> starter_enabled;
    std::optional<bool> dyno_enabled;
    std::optional<bool> limiter_enabled;

    friend bool operator==(const OperatingStatePatch &,
                           const OperatingStatePatch &) = default;
};

struct SelectGearEvent {
    GearRef gear;

    friend bool operator==(const SelectGearEvent &, const SelectGearEvent &) = default;
};

struct SetClutchEvent {
    double clutch_position_01 = 0.0;

    friend bool operator==(const SetClutchEvent &, const SetClutchEvent &) = default;
};

struct SetRouteMonitoringEvent {
    SourceRouteRef route;
    std::optional<bool> muted;
    std::optional<double> gain_linear;
    std::optional<double> wet_mix_01;

    friend bool operator==(const SetRouteMonitoringEvent &,
                           const SetRouteMonitoringEvent &) = default;
};

struct SetMasterMonitoringEvent {
    double gain_linear = 1.0;

    friend bool operator==(const SetMasterMonitoringEvent &,
                           const SetMasterMonitoringEvent &) = default;
};

struct SetConditioningMonitoringEvent {
    std::optional<double> jitter_scale;
    std::optional<double> derivative_mix_01;
    std::optional<double> air_noise_mix_01;

    friend bool operator==(const SetConditioningMonitoringEvent &,
                           const SetConditioningMonitoringEvent &) = default;
};

enum class LifecycleAction : std::uint8_t {
    startup,
    shutdown,
    reset,
};

struct LifecycleEvent {
    LifecycleAction action = LifecycleAction::startup;

    friend bool operator==(const LifecycleEvent &, const LifecycleEvent &) = default;
};

using ScenarioEventPayload =
    std::variant<OperatingStatePatch, SelectGearEvent, SetClutchEvent,
                 SetRouteMonitoringEvent, SetMasterMonitoringEvent,
                 SetConditioningMonitoringEvent, LifecycleEvent>;

struct ScenarioEvent {
    ScenarioEventId id;
    Quantity time;
    ScenarioEventPayload payload;

    friend bool operator==(const ScenarioEvent &, const ScenarioEvent &) = default;
};

struct ScenarioRates {
    RationalRate physics;
    RationalRate capture;
    RationalRate source_processing;
    RationalRate acoustics;
    RationalRate delivery;

    friend bool operator==(const ScenarioRates &, const ScenarioRates &) = default;
};

struct ScenarioQuality {
    std::string id;
    // Maximum delivery-rate PCM frames accepted by one EngineSession process call.
    std::uint32_t process_block_capacity_frames = 0;
    // Maximum caller-authored ControlCommand records retained by one session.
    std::uint32_t event_queue_capacity = 0;
    // Maximum delivery-frame telemetry records returned by one process call.
    std::uint32_t telemetry_capacity_frames = 0;

    friend bool operator==(const ScenarioQuality &, const ScenarioQuality &) = default;
};

struct OutputSelection {
    std::vector<AudioBusRef> buses;
    std::vector<TelemetryChannelRef> telemetry_channels;

    friend bool operator==(const OutputSelection &, const OutputSelection &) = default;
};

struct ScenarioDocument {
    std::string schema = "engine-sim-offline/scenario";
    ScenarioId id;
    EngineRef engine;
    FuelRef fuel;
    AmbientConditions ambient;
    InitialThermalState initial_thermal_state;
    CrankcaseConditions crankcase;
    InitialOperatingState initial_state;
    Preparation preparation;
    ScenarioMode mode;
    std::vector<ScenarioEvent> events;
    ScenarioRates rates;
    ScenarioQuality quality;
    Quantity total_duration;
    Quantity audible_start;
    Quantity audible_duration;
    std::uint64_t public_seed = 0;
    OutputSelection output;

    friend bool operator==(const ScenarioDocument &,
                           const ScenarioDocument &) = default;
};

} // namespace engine_sim_offline::authoring
