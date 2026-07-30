#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::simulation::detail {

// Legacy engine-sim evolves the three species fractions independently and does not
// renormalize after transfers. Preserve that solver state, but project accumulated
// common-scale roundoff back onto the public capture contract's fraction simplex.
[[nodiscard]] contract::MixtureFractions
capture_mixture_for_contract(const LegacyGasMixture &mixture) noexcept;

struct LowOrderCapturePortBinding {
    std::size_t cylinder_index = 0;
    std::size_t duct_volume_index = 0;
    std::size_t valve_edge_index = 0;
    contract::PortKind kind = contract::PortKind::unspecified;
};

struct LowOrderCaptureCylinderBinding {
    std::size_t chamber_volume_index = 0;
    std::size_t exhaust_primary_volume_index = 0;
};

struct LowOrderCaptureRouteBinding {
    std::size_t gas_route_index = 0;
    std::size_t source_volume_index = 0;
    std::size_t outlet_edge_index = 0;
};

struct LowOrderCaptureBufferFault {
    contract::FailureKind kind = contract::FailureKind::contract_violation;
    std::string detail_code;
    std::string state_summary;
    std::optional<contract::CylinderId> cylinder_id;
    std::optional<contract::PortId> port_id;
    std::optional<contract::GasVolumeId> gas_volume_id;
    std::optional<contract::FlowEdgeId> flow_edge_id;
    std::optional<contract::RouteId> route_id;
};

struct LowOrderCaptureBufferPlan {
    contract::EngineId engine_id;
    contract::RationalRateHz rate;
    std::uint32_t declared_block_capacity_frames = 0;
    std::uint32_t declared_event_capacity_records = 0;
    std::uint32_t maximum_events_per_frame = 0;
    std::vector<contract::CylinderId> cylinders;
    std::vector<contract::PortIdentity> ports;
    std::vector<contract::GasVolumeIdentity> gas_volumes;
    std::vector<contract::FlowEdgeIdentity> flow_edges;
    std::vector<contract::RouteIdentity> routes;
    std::vector<LowOrderCaptureCylinderBinding> cylinder_bindings;
    std::vector<LowOrderCapturePortBinding> port_bindings;
    std::vector<LowOrderCaptureRouteBinding> route_bindings;
};

class LowOrderCaptureBuffer final {
  public:
    explicit LowOrderCaptureBuffer(LowOrderCaptureBufferPlan plan);

    LowOrderCaptureBuffer(const LowOrderCaptureBuffer &) = delete;
    LowOrderCaptureBuffer &operator=(const LowOrderCaptureBuffer &) = delete;
    LowOrderCaptureBuffer(LowOrderCaptureBuffer &&) noexcept = default;
    LowOrderCaptureBuffer &operator=(LowOrderCaptureBuffer &&) noexcept = default;

    void begin_block(std::uint64_t first_sample_index) noexcept;
    [[nodiscard]] std::optional<LowOrderCaptureBufferFault>
    append(const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas,
           const contract::TorqueTelemetry &torque);
    [[nodiscard]] contract::CaptureBlockView view() const noexcept;
    [[nodiscard]] std::uint32_t frame_count() const noexcept;
    [[nodiscard]] std::uint32_t block_capacity_frames() const noexcept;
    [[nodiscard]] std::uint64_t first_sample_index() const noexcept;

  private:
    LowOrderCaptureBufferPlan plan_;
    std::uint64_t first_sample_index_ = 0;
    std::uint32_t frame_count_ = 0;

    std::vector<contract::EngineCaptureSample> engine_;
    std::vector<contract::CylinderCaptureSample> cylinders_;
    std::vector<contract::PortCaptureSample> ports_;
    std::vector<contract::GasVolumeCaptureSample> gas_volumes_;
    std::vector<contract::FlowEdgeCaptureSample> flow_edges_;
    std::vector<contract::SourceRouteCaptureSample> routes_;
    std::vector<std::uint32_t> event_offsets_;
    std::vector<contract::EngineEvent> events_;
    std::vector<double> filtered_engine_speed_rpm_;
    std::vector<contract::ReferenceParityCylinderSample> parity_cylinders_;
};

} // namespace engine_sim_offline::simulation::detail
