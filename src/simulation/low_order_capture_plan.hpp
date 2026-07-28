#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/legacy_low_order_capture_buffer.hpp"

#include <cstddef>
#include <cstdint>
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

// Profile-neutral capture topology compiled from one explicit low-order core.
// The capture buffer retains EngineSpec order for public capture compatibility.
// The physical inventory has its own exact ascending GasVolumeId order so cycle
// accounting never depends on authored vector order.
struct LowOrderCapturePlan {
    detail::LegacyCaptureBufferPlan capture_buffer;
    std::uint64_t capture_horizon_frames = 0;
    std::vector<contract::GasVolumeId> physical_gas_volume_ids;
    std::vector<LowOrderCylinderChamberCaptureBinding> cylinder_chambers;
};

using LowOrderCapturePlanCompileResult =
    std::variant<LowOrderCapturePlan, contract::ValidationReport>;

[[nodiscard]] LowOrderCapturePlanCompileResult
compile_low_order_capture_plan(const contract::EngineSpec &engine,
                               const contract::LowOrderEngineCoreV1 &core,
                               const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::simulation
