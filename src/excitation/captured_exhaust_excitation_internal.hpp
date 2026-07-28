#pragma once

#include "excitation/captured_exhaust_excitation.hpp"

#include <array>
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
};

class CapturedExhaustExcitationState final {
  public:
    contract::EngineId engine_id;
    std::string model_id;
    std::string profile_id;
    std::array<contract::CylinderId, kCapturedExcitationCylinderCount> cylinder_ids{};
    std::array<contract::RouteIdentity, kCapturedExcitationRouteCount> route_layout{};
    std::array<contract::RouteId, kCapturedExcitationRouteCount> route_ids{};
    std::array<CapturedExcitationCylinderPlan, kCapturedExcitationCylinderCount>
        cylinders{};
    // A second, equally bounded delay bank makes whole-block arithmetic
    // transactional without allocating during processing.
    std::array<CapturedExcitationDelayState, kCapturedExcitationCylinderCount>
        prospective_delays{};
    std::array<std::size_t, kCapturedExcitationCylinderCount> accumulation_order{};
    std::array<CapturedExcitationRoutePlan, kCapturedExcitationRouteCount> routes{};

    double reference_atmosphere_pa_abs = 0.0;
    double excitation_scale = 0.0;
    double filtered_speed_threshold_rpm = 0.0;
    double gauge_static_gain = 0.0;
    double dynamic_forward_gain = 0.0;
    double dynamic_reverse_gain = 0.0;
    double cylinder_count_divisor = 0.0;

    std::array<double,
               kCapturedExcitationFramesPerBlock * kCapturedExcitationCylinderCount>
        pre_delay{};
    std::array<double,
               kCapturedExcitationFramesPerBlock * kCapturedExcitationCylinderCount>
        post_delay{};
    std::array<presentation::ExhaustExcitationFrame, kCapturedExcitationFramesPerBlock>
        route_bus_frames{};

    std::uint64_t next_frame_index = 0;
    std::uint64_t published_block_count = 0;
    bool consumer_callback_active = false;
    std::optional<contract::FailureContext> terminal_fault;
};

} // namespace engine_sim_offline::excitation::detail
