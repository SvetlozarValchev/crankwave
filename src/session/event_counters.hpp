#pragma once

#include "engine_sim_offline/session.hpp"

#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>

namespace engine_sim_offline::session {

// Internal reduction seam shared by EngineSession and its exhaustive focused test.
// Capture admission has already validated event geometry and ordering; this function
// freezes the v1 payload/reason inventory and fails rather than dropping evidence it
// cannot represent.
[[nodiscard]] inline bool
reduce_event_counters(const std::span<const contract::EngineEvent> events,
                      EngineEventCounters &destination) noexcept {
    EngineEventCounters counters;
    counters.total_event_record_count = static_cast<std::uint64_t>(events.size());

    for (const auto &event : events) {
        if (event.payload.valueless_by_exception()) {
            return false;
        }
        const auto categorized = std::visit(
            [&counters](const auto &payload) noexcept {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, contract::SparkCrossing>) {
                    ++counters.spark_crossing_count;
                    return true;
                } else if constexpr (std::is_same_v<Payload,
                                                    contract::LimiterStateChanged>) {
                    if (payload.old_active == payload.new_active) {
                        return false;
                    }
                    ++counters.limiter_transition_count;
                    if (payload.new_active) {
                        ++counters.limiter_activation_count;
                    } else {
                        ++counters.limiter_release_count;
                    }
                    if (payload.overspeed_refreshed) {
                        ++counters.limiter_transition_overspeed_refreshed_count;
                    }
                    return true;
                } else if constexpr (std::is_same_v<Payload,
                                                    contract::IgnitionAccepted>) {
                    ++counters.ignition_accepted_count;
                    return true;
                } else if constexpr (std::is_same_v<Payload,
                                                    contract::IgnitionRejected>) {
                    switch (payload.reason) {
                    case contract::IgnitionRejection::active_flame:
                        ++counters.ignition_rejected_active_flame_count;
                        return true;
                    case contract::IgnitionRejection::no_fuel:
                        ++counters.ignition_rejected_no_fuel_count;
                        return true;
                    case contract::IgnitionRejection::mixture_low:
                        ++counters.ignition_rejected_mixture_low_count;
                        return true;
                    case contract::IgnitionRejection::mixture_high:
                        ++counters.ignition_rejected_mixture_high_count;
                        return true;
                    case contract::IgnitionRejection::unspecified:
                        return false;
                    }
                    return false;
                } else {
                    static_assert(std::is_same_v<Payload, contract::FlameExtinguished>);
                    switch (payload.reason) {
                    case contract::FlameExtinctionReason::intake_transfer:
                        ++counters.flame_extinguished_intake_transfer_count;
                        return true;
                    case contract::FlameExtinctionReason::no_geometric_progress:
                        ++counters.flame_extinguished_no_geometric_progress_count;
                        return true;
                    case contract::FlameExtinctionReason::unspecified:
                        return false;
                    }
                    return false;
                }
            },
            event.payload);
        if (!categorized) {
            return false;
        }
    }

    const auto partitioned_event_count =
        counters.spark_crossing_count + counters.limiter_transition_count +
        counters.ignition_accepted_count +
        counters.ignition_rejected_active_flame_count +
        counters.ignition_rejected_no_fuel_count +
        counters.ignition_rejected_mixture_low_count +
        counters.ignition_rejected_mixture_high_count +
        counters.flame_extinguished_intake_transfer_count +
        counters.flame_extinguished_no_geometric_progress_count;
    if (partitioned_event_count != counters.total_event_record_count ||
        counters.limiter_activation_count + counters.limiter_release_count !=
            counters.limiter_transition_count ||
        counters.limiter_transition_overspeed_refreshed_count >
            counters.limiter_transition_count) {
        return false;
    }

    destination = counters;
    return true;
}

} // namespace engine_sim_offline::session
