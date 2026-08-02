#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/kinematic_scenario_schedule.hpp"
#include "simulation/legacy_combustion_primitives.hpp"
#include "simulation/legacy_fixed_valvetrain.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

namespace detail {
struct LowOrderEngineCoreV1RuntimeFactory;
}

inline constexpr std::uint32_t kLegacyGasSubstepCount = 8U;

struct LegacyGasVolumeStepState {
    contract::GasVolumeId gas_volume_id;
    contract::GasVolumeKind kind = contract::GasVolumeKind::unspecified;
    // The single authored atmosphere identity aliases several resettable boundary
    // work cells. It therefore has no unique physical state to publish.
    bool physically_resolved = false;
    LegacyGasCell cell;
    LegacyGasCellGeometry geometry;

    friend bool operator==(const LegacyGasVolumeStepState &,
                           const LegacyGasVolumeStepState &) = default;
};

struct LegacyFlowEdgeStepState {
    contract::FlowEdgeId flow_edge_id;
    contract::GasVolumeId endpoint_0_volume_id;
    contract::GasVolumeId endpoint_1_volume_id;
    // Signed endpoint 0 -> endpoint 1, accumulated over all eight gas substeps.
    double signed_amount_mol = 0.0;

    friend bool operator==(const LegacyFlowEdgeStepState &,
                           const LegacyFlowEdgeStepState &) = default;
};

struct LegacyCylinderGasStepState {
    contract::CylinderId cylinder_id;
    contract::PortId intake_port_id;
    contract::PortId exhaust_port_id;
    contract::GasVolumeId intake_runner_volume_id;
    contract::GasVolumeId chamber_volume_id;
    contract::GasVolumeId exhaust_primary_volume_id;
    contract::RouteId exhaust_route_id;
    LegacyCylinderValveSample valves;
    LegacyFlameState flame;
    double outer_step_combustion_heat_release_j = 0.0;
    double peak_temperature_k = 0.0;
    double cumulative_burned_fuel_mass_kg = 0.0;
    double latest_signed_intake_transfer_mol = 0.0;
    double latest_signed_exhaust_transfer_mol = 0.0;
    double indicated_gas_torque_nm = 0.0;

    friend bool operator==(const LegacyCylinderGasStepState &,
                           const LegacyCylinderGasStepState &) = default;
};

struct LegacyExhaustRouteGasStepState {
    contract::RouteId route_id;
    contract::GasVolumeId collector_volume_id;
    contract::FlowEdgeId collector_outlet_edge_id;
    double collector_cross_section_area_m2 = 0.0;

    friend bool operator==(const LegacyExhaustRouteGasStepState &,
                           const LegacyExhaustRouteGasStepState &) = default;
};

struct LegacyLowOrderGasStep {
    contract::RationalRateHz rate;
    std::uint64_t sample_index = 0;
    std::uint64_t step_end_index = 0;
    std::uint64_t timestamp_tick = 0;
    std::vector<LegacyGasVolumeStepState> gas_volumes;
    std::vector<LegacyFlowEdgeStepState> flow_edges;
    std::vector<LegacyCylinderGasStepState> cylinders;
    std::vector<LegacyExhaustRouteGasStepState> exhaust_routes;
    std::vector<ScheduledMechanismEvent> events;
    double indicated_gas_torque_nm = 0.0;
};

using LegacyGasAdvanceResult =
    std::variant<std::reference_wrapper<const LegacyLowOrderGasStep>,
                 contract::FailureContext>;

class LegacyLowOrderGasSession final {
  public:
    LegacyLowOrderGasSession(const LegacyLowOrderGasSession &) = delete;
    LegacyLowOrderGasSession &operator=(const LegacyLowOrderGasSession &) = delete;
    LegacyLowOrderGasSession(LegacyLowOrderGasSession &&) noexcept = default;
    LegacyLowOrderGasSession &operator=(LegacyLowOrderGasSession &&) noexcept = default;

    // Consumes exactly one already-admitted mechanics step. The returned reference
    // is session-owned and remains valid only until the next advance call. A fault
    // is terminal and stable.
    [[nodiscard]] LegacyGasAdvanceResult advance(const LegacyMechanismStep &mechanics);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] std::uint64_t produced_sample_count() const noexcept;

  private:
    struct IntakeLane {
        contract::IntakeId intake_id;
        std::size_t plenum_volume_index = 0;
        std::size_t main_throttle_edge_index = 0;
        std::size_t idle_bypass_edge_index = 0;
        double plenum_cross_section_area_m2 = 0.0;
        double main_mixture_lambda = 0.0;
        double idle_throttle_plate_position_01 = 0.0;
        double main_throttle_k = 0.0;
        double idle_bypass_k = 0.0;
        double plenum_to_runner_k = 0.0;
        double velocity_decay = 0.0;
        LegacyGasCell atmosphere_work_cell;
    };

    struct RouteLane {
        std::size_t public_route_index = 0;
        std::size_t collector_volume_index = 0;
        std::size_t collector_outlet_edge_index = 0;
        double primary_to_collector_k = 0.0;
        double collector_outlet_k = 0.0;
        double velocity_decay = 0.0;
        LegacyGasCell atmosphere_work_cell;
    };

    struct CylinderLane {
        std::size_t public_cylinder_index = 0;
        std::size_t intake_runner_volume_index = 0;
        std::size_t chamber_volume_index = 0;
        std::size_t exhaust_primary_volume_index = 0;
        std::size_t plenum_to_runner_edge_index = 0;
        std::size_t intake_valve_edge_index = 0;
        std::size_t exhaust_valve_edge_index = 0;
        std::size_t primary_to_collector_edge_index = 0;
        std::size_t blowby_edge_index = 0;
        std::size_t intake_lane_index = 0;
        std::size_t route_lane_index = 0;
        double blowby_k = 0.0;
        double bore_m = 0.0;
        double piston_area_m2 = 0.0;
        double intake_runner_cross_section_area_m2 = 0.0;
        double exhaust_primary_cross_section_area_m2 = 0.0;
        std::array<double, kLegacyCombustionHistorySampleCount>
            piston_speed_history_m_s{};
        std::array<double, kLegacyCombustionHistorySampleCount> pressure_history_pa{};
        LegacyPcg32 random;
    };

    struct FuelModel {
        double molecular_mass_kg_per_mol = 0.0;
        double energy_density_j_per_kg = 0.0;
        double molecular_afr = 0.0;
        double maximum_burning_efficiency_01 = 0.0;
        double burning_efficiency_randomness_01 = 0.0;
        double low_efficiency_attenuation_01 = 0.0;
        double maximum_turbulence_effect = 0.0;
        double maximum_dilution_effect = 0.0;
        double lbv_multiplier = 0.0;
        double turbulence_to_flame_speed_ratio_triangle_radius = 0.0;
        std::vector<LegacyTrianglePoint> turbulence_to_flame_speed_ratio;

        [[nodiscard]] LegacyGasolineFuelParameters view() const noexcept;
    };

    LegacyLowOrderGasSession() = default;

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          std::optional<contract::CylinderId> cylinder_id = std::nullopt,
          std::optional<contract::PortId> port_id = std::nullopt,
          std::optional<contract::GasVolumeId> gas_volume_id = std::nullopt,
          std::optional<contract::FlowEdgeId> flow_edge_id = std::nullopt,
          std::optional<contract::RouteId> route_id = std::nullopt) const;
    [[nodiscard]] bool append_event(const contract::EngineEventPayload &payload);
    [[nodiscard]] bool
    validate_cell(std::size_t gas_volume_index, std::string_view operation,
                  std::uint32_t gas_substep_index,
                  std::optional<contract::CylinderId> cylinder_id = std::nullopt,
                  std::optional<contract::FlowEdgeId> flow_edge_id = std::nullopt,
                  std::optional<contract::RouteId> route_id = std::nullopt);

    contract::RationalRateHz rate_;
    SharedMechanismKinematicsPlan mechanism_plan_;
    std::uint64_t first_sample_index_ = 0;
    std::optional<std::uint64_t> expected_sample_count_;
    std::uint64_t produced_sample_count_ = 0;
    std::size_t maximum_event_count_ = 0;
    double step_s_ = 0.0;
    double gas_step_s_ = 0.0;
    double ambient_pressure_pa_ = 0.0;
    double ambient_temperature_k_ = 0.0;
    double wall_temperature_k_ = 0.0;
    double crankcase_pressure_pa_ = 0.0;
    double crankcase_temperature_k_ = 0.0;
    double current_theta_unwrapped_rad_ = 0.0;
    bool previous_limiter_cut_active_ = false;
    LegacyGasMixture inert_mixture_{};
    std::optional<LegacySelectableValvetrain> valvetrain_;
    std::vector<IntakeLane> intakes_;
    std::vector<RouteLane> routes_;
    std::vector<CylinderLane> cylinders_;
    std::vector<contract::CylinderId> expected_spark_cylinders_;
    FuelModel fuel_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    LegacyLowOrderGasStep step_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend struct detail::LowOrderEngineCoreV1RuntimeFactory;
};

} // namespace engine_sim_offline::simulation
