#pragma once

#include "crankwave/contract/capture.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::artifacts {

inline constexpr std::string_view kCaptureTelemetrySchemaId =
    "crankwave.capture-telemetry.le.v1";
inline constexpr std::uint32_t kCaptureTelemetrySchemaVersion = 1;
inline constexpr std::size_t kMaximumTelemetryEncoderChunkBytes = 64U * 1024U;

enum class TelemetryEncodingErrorCode : std::uint8_t {
    invalid_descriptor,
    invalid_layout,
    invalid_capture_block,
    inconsistent_layout,
    discontinuous_clock,
    reference_parity_mismatch,
    frame_count_mismatch,
    size_overflow,
    invalid_state,
    non_finite_value,
    callback_rejected,
};

struct TelemetryEncodingError {
    TelemetryEncodingErrorCode code = TelemetryEncodingErrorCode::invalid_descriptor;
    std::string path;
    std::string message;

    friend bool operator==(const TelemetryEncodingError &,
                           const TelemetryEncodingError &) = default;
};

using TelemetryEncodingStatus = std::optional<TelemetryEncodingError>;

// The view is callback-scoped and is never retained. Offsets are contiguous from zero.
using TelemetryChunkConsumer =
    std::function<bool(std::uint64_t byte_offset, std::span<const std::byte> bytes)>;

struct TelemetryStreamDescriptor {
    // The rate, origin, and phase of the first serialized frame.
    contract::CaptureClock first_clock;
    std::uint64_t frame_count = 0;
    bool includes_reference_parity = false;
    std::size_t maximum_chunk_bytes = 16U * 1024U;

    friend bool operator==(const TelemetryStreamDescriptor &,
                           const TelemetryStreamDescriptor &) = default;
};

/**
 * Canonical little-endian CaptureBlock telemetry, schema v1.
 *
 * Header:
 *   magic[8] = "ESOTLM\r\n", schema_version:u32, flags:u32,
 *   rate_numerator:u64, rate_denominator:u64, phase:u8, reserved[7],
 *   first_sample_index:u64, first_timestamp_tick:u64, frame_count:u64,
 *   engine_id:u32, then five u32 topology counts and the complete ordered topology.
 *
 * Every frame is normalized out of its transport block and begins with:
 *   marker="FRM1", sample_index:u64, timestamp_tick:u64
 * followed by every EngineCaptureSample field, every frame-major cylinder, port,
 * gas-volume, flow-edge, and route field, optional reference-parity fields, then
 * event_count:u32 and all events for that frame in ordinal order. Event frame_offset
 * is deliberately represented by the containing global frame; unlike an event's
 * payload and ordinal, its block-local offset is not stable across valid partitions.
 *
 * The stream ends with marker="END1" and the exact u64 frame count. Integers,
 * binary64/Float32 bit patterns, booleans, enum codes, variant tags, optional flags,
 * and length-prefixed UTF-8 strings have explicit little-endian encodings in v1.
 * No CaptureBlock field is omitted: torque availability/completeness/reasons/masks,
 * both source-route variants, every event variant, and optional reference_parity are
 * lossless. Transport block boundaries and capacities are not capture observables and
 * are omitted so valid repartitioning produces identical bytes.
 *
 * The encoder copies the topology at construction. write_block() validates and
 * consumes a borrowed CaptureBlockView synchronously and retains no borrowed view.
 */
class TelemetryEncoder {
  public:
    [[nodiscard]] TelemetryEncodingStatus begin(const TelemetryChunkConsumer &consumer);
    [[nodiscard]] TelemetryEncodingStatus
    write_block(const contract::CaptureBlockView &block,
                const TelemetryChunkConsumer &consumer);
    [[nodiscard]] TelemetryEncodingStatus
    finish(const TelemetryChunkConsumer &consumer);

    [[nodiscard]] const TelemetryStreamDescriptor &descriptor() const noexcept;
    [[nodiscard]] std::uint64_t frames_written() const noexcept;
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept;
    [[nodiscard]] std::size_t maximum_chunk_bytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

  private:
    enum class State : std::uint8_t {
        ready,
        begun,
        finished,
        failed,
    };

    TelemetryEncoder(TelemetryStreamDescriptor descriptor, contract::EngineId engine_id,
                     std::vector<contract::CylinderId> cylinders,
                     std::vector<contract::PortIdentity> ports,
                     std::vector<contract::GasVolumeIdentity> gas_volumes,
                     std::vector<contract::FlowEdgeIdentity> flow_edges,
                     std::vector<contract::RouteIdentity> routes) noexcept;

    [[nodiscard]] TelemetryEncodingStatus fail(TelemetryEncodingError error) noexcept;

    TelemetryStreamDescriptor descriptor_;
    contract::EngineId engine_id_;
    std::vector<contract::CylinderId> cylinders_;
    std::vector<contract::PortIdentity> ports_;
    std::vector<contract::GasVolumeIdentity> gas_volumes_;
    std::vector<contract::FlowEdgeIdentity> flow_edges_;
    std::vector<contract::RouteIdentity> routes_;
    std::uint64_t frames_written_ = 0;
    std::uint64_t bytes_emitted_ = 0;
    State state_ = State::ready;

    friend std::variant<TelemetryEncoder, TelemetryEncodingError>
    make_telemetry_encoder(const contract::CaptureLayoutView &,
                           TelemetryStreamDescriptor);
};

using TelemetryEncoderResult = std::variant<TelemetryEncoder, TelemetryEncodingError>;

[[nodiscard]] TelemetryEncoderResult
make_telemetry_encoder(const contract::CaptureLayoutView &layout,
                       TelemetryStreamDescriptor descriptor);

} // namespace crankwave::artifacts
