#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/low_order_capture_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct LowOrderCylinderChamberCaptureBinding {
    contract::CylinderId cylinder_id;
    contract::GasVolumeId chamber_volume_id;
    std::size_t physical_volume_index = 0;

    friend bool operator==(const LowOrderCylinderChamberCaptureBinding &,
                           const LowOrderCylinderChamberCaptureBinding &) = default;
};

// Profile-neutral capture topology compiled from the engine's admitted low-order
// profile.
// The capture buffer retains EngineSpec order for public capture compatibility.
// The physical inventory has its own exact ascending GasVolumeId order so cycle
// accounting never depends on authored vector order.
struct LowOrderCapturePlan {
    detail::LowOrderCaptureBufferPlan capture_buffer;
    std::uint64_t capture_horizon_frames = 0;
    std::vector<contract::GasVolumeId> physical_gas_volume_ids;
    std::vector<LowOrderCylinderChamberCaptureBinding> cylinder_chambers;
};

using LowOrderCapturePlanCompileResult =
    std::variant<LowOrderCapturePlan, contract::ValidationReport>;

// Each cylinder can publish at most one spark crossing, one ignition result, and
// one extinction, plus one engine-wide limiter transition. The returned count is
// absent when contiguous uint8 ordinals cannot identify every event.
[[nodiscard]] std::optional<std::uint32_t>
maximum_low_order_events_per_frame(std::size_t cylinder_count) noexcept;

[[nodiscard]] LowOrderCapturePlanCompileResult
compile_low_order_capture_plan(const contract::EngineSpec &engine,
                               const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::simulation
