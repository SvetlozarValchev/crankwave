#pragma once

#include "excitation/captured_source_excitation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::excitation::detail {

struct CapturedExcitationDelayState {
    std::vector<double> history;
    std::size_t next_write = 0;
    std::uint64_t accepted_input_count = 0;

    [[nodiscard]] double process(double input) noexcept;
};

struct CapturedExcitationCylinderPlan {
    contract::CylinderId cylinder_id;
    std::size_t capture_cylinder_index = 0;
    std::size_t route_index = 0;
    double sound_attenuation_linear = 0.0;
    CapturedExcitationDelayState delay;
};

struct CapturedExcitationRoutePlan {
    contract::RouteId route_id;
    double exhaust_system_length_m = 0.0;
    double audio_volume_linear = 0.0;
    CapturedExcitationDelayState downstream_delay;
};

class CapturedSourceExcitationState final {
  public:
    contract::EngineId engine_id;
    std::string model_id;
    std::string profile_id;
    contract::RationalRateHz sample_rate;
    std::uint32_t block_capacity_frames = 0;
    std::vector<contract::CylinderId> cylinder_ids;
    std::vector<double> piston_crown_areas_m2;
    double crankcase_pressure_pa_abs = 0.0;
    std::vector<contract::RouteIdentity> route_layout;
    std::vector<contract::RouteId> route_ids;
    std::vector<contract::RouteId> intake_route_ids;
    std::vector<std::size_t> intake_capture_route_indices;
    std::vector<CapturedExcitationCylinderPlan> cylinders;
    // A second, equally bounded delay bank makes whole-block arithmetic
    // transactional without allocating during processing.
    std::vector<CapturedExcitationDelayState> prospective_delays;
    std::vector<std::size_t> accumulation_order;
    std::vector<CapturedExcitationRoutePlan> routes;
    // A transactional route-delay bank mirrors the per-cylinder delay bank.
    std::vector<CapturedExcitationDelayState> prospective_route_delays;

    double reference_atmosphere_pa_abs = 0.0;
    double excitation_scale = 0.0;
    double filtered_speed_threshold_rpm = 0.0;
    double gauge_static_gain = 0.0;
    double dynamic_forward_gain = 0.0;
    double dynamic_reverse_gain = 0.0;
    double cylinder_count_divisor = 0.0;

    std::vector<double> pre_delay;
    std::vector<double> post_delay;
    std::vector<double> collector_bus_values;
    std::vector<double> route_bus_values;
    std::vector<double> intake_pressure_pa_abs;
    std::vector<double> axial_pressure_force_n;

    std::uint64_t next_frame_index = 0;
    std::uint64_t published_block_count = 0;
    bool consumer_callback_active = false;
    std::optional<contract::FailureContext> terminal_fault;
};

} // namespace engine_sim_offline::excitation::detail
