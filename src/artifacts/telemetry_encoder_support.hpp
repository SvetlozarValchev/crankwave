#pragma once

#include "crankwave/artifacts/telemetry_encoder.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace crankwave::artifacts::detail {

class TelemetryByteEmitter {
  public:
    TelemetryByteEmitter(std::size_t maximum_chunk_bytes, std::uint64_t initial_offset,
                         const TelemetryChunkConsumer &consumer);

    [[nodiscard]] bool append_byte(std::byte value);
    [[nodiscard]] bool append(std::span<const std::byte> bytes);
    [[nodiscard]] bool append_u8(std::uint8_t value);
    [[nodiscard]] bool append_u32(std::uint32_t value);
    [[nodiscard]] bool append_u64(std::uint64_t value);
    [[nodiscard]] bool append_f64(double value);
    [[nodiscard]] bool append_bool(bool value);
    [[nodiscard]] bool append_fourcc(const std::array<char, 4> &value);
    [[nodiscard]] bool append_string(std::string_view value);
    [[nodiscard]] bool finish();
    [[nodiscard]] std::uint64_t offset() const noexcept;
    [[nodiscard]] bool offset_overflowed() const noexcept;

  private:
    [[nodiscard]] bool flush();

    std::vector<std::byte> buffer_;
    std::size_t used_ = 0;
    std::uint64_t offset_ = 0;
    bool offset_overflowed_ = false;
    const TelemetryChunkConsumer &consumer_;
};

[[nodiscard]] std::optional<TelemetryEncodingError>
validate_all_serialized_values_finite(const contract::CaptureBlockView &block);

[[nodiscard]] bool emit_layout(TelemetryByteEmitter &emitter,
                               contract::EngineId engine_id,
                               std::span<const contract::CylinderId> cylinders,
                               std::span<const contract::PortIdentity> ports,
                               std::span<const contract::GasVolumeIdentity> gas_volumes,
                               std::span<const contract::FlowEdgeIdentity> flow_edges,
                               std::span<const contract::RouteIdentity> routes);

[[nodiscard]] bool emit_frame(TelemetryByteEmitter &emitter,
                              const contract::CaptureBlockView &block,
                              std::uint32_t frame_offset,
                              std::uint64_t global_sample_index,
                              std::uint64_t timestamp_tick);

} // namespace crankwave::artifacts::detail
