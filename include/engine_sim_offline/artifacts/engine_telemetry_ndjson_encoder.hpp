#pragma once

#include "engine_sim_offline/contract/source_matrix.hpp"
#include "engine_sim_offline/session.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kEngineTelemetryNdjsonArtifactRoleV1 =
    "diagnostics.engine-telemetry.v1";
inline constexpr contract::ArtifactKind kEngineTelemetryNdjsonArtifactKindV1 =
    contract::ArtifactKind::telemetry;
inline constexpr std::string_view kEngineTelemetryNdjsonRelativePathV1 =
    "telemetry/engine-telemetry.v1.ndjson";
inline constexpr std::string_view kEngineTelemetryNdjsonSchemaIdV1 =
    "engine-sim-offline.engine-telemetry.ndjson.v1";
inline constexpr std::uint32_t kEngineTelemetryNdjsonSchemaVersionV1 = 1U;
inline constexpr std::size_t kMaximumEngineTelemetryNdjsonChunkBytes = 64U * 1024U;

enum class EngineTelemetryNdjsonEncodingErrorCode : std::uint8_t {
    invalid_descriptor,
    invalid_state,
    unexpected_block,
    discontinuous_range,
    endpoint_count_mismatch,
    cycle_order_mismatch,
    event_count_mismatch,
    integer_overflow,
    invalid_enum,
    invalid_value,
    non_finite_value,
    callback_rejected,
    size_overflow,
    allocation_failure,
    encoding_failure,
};

struct EngineTelemetryNdjsonEncodingError {
    EngineTelemetryNdjsonEncodingErrorCode code =
        EngineTelemetryNdjsonEncodingErrorCode::invalid_descriptor;
    std::string path;
    std::string message;

    friend bool operator==(const EngineTelemetryNdjsonEncodingError &,
                           const EngineTelemetryNdjsonEncodingError &) = default;
};

using EngineTelemetryNdjsonEncodingStatus =
    std::optional<EngineTelemetryNdjsonEncodingError>;

// The view is callback-scoped and is never retained. Offsets are contiguous from
// zero. Throwing or returning false terminally fails the encoder.
using EngineTelemetryNdjsonChunkConsumer =
    std::function<bool(std::uint64_t byte_offset, std::span<const std::byte> bytes)>;

struct EngineTelemetryNdjsonStreamDescriptor {
    contract::Sha256Digest simulation_request_identity_v7_sha256;
    std::string engine_id;
    std::string scenario_id;
    EngineSessionExecutionKind execution_kind =
        EngineSessionExecutionKind::finite_scenario;
    EngineMotionMode motion_mode = EngineMotionMode::held_speed;
    contract::RationalRateHz physics_rate = kEngineSessionPhysicsRateHz;
    contract::RationalRateHz delivery_rate = kEngineSessionDeliveryRateHz;
    std::uint32_t physics_frames_per_block = kEngineSessionPhysicsFramesPerBlock;
    std::uint32_t delivery_frames_per_block = kEngineSessionDeliveryFramesPerBlock;
    std::uint64_t preparation_block_count = 0;
    std::uint64_t total_block_count = 0;
    std::size_t maximum_chunk_bytes = 16U * 1024U;

    friend bool operator==(const EngineTelemetryNdjsonStreamDescriptor &,
                           const EngineTelemetryNdjsonStreamDescriptor &) = default;
};

// Borrowed input adapter used by focused encoder tests and non-session hosts. The
// encoder consumes every span synchronously and retains no borrowed storage.
struct EngineTelemetryNdjsonBlockInput {
    std::uint64_t block_ordinal = 0;
    EngineSessionBlockPhase phase = EngineSessionBlockPhase::preparation;
    std::uint64_t first_physics_frame = 0;
    std::uint32_t physics_frame_count = 0;
    std::uint64_t first_delivery_frame = 0;
    std::uint32_t delivery_frame_count = 0;
    std::span<const EngineTelemetryFrame> telemetry;
    std::span<const EngineCompletedCycleEvidence> cycle_evidence;
    EngineEventCounters event_counters;
};

[[nodiscard]] EngineTelemetryNdjsonBlockInput
borrow_engine_telemetry_ndjson_block(const EngineSessionBlockView &block) noexcept;

/**
 * Canonical streaming engine diagnostic telemetry, NDJSON schema v1.
 *
 * Record order is one header, then one block record immediately followed by each
 * cycle completed by that block, then one footer. Every record has fixed key order
 * and exactly one trailing LF. Binary64 values use normalized-zero,
 * shortest-round-trip decimal JSON numbers. All 64-bit integers are decimal strings;
 * masks are fixed-width lowercase hexadecimal strings.
 *
 * begin(), write_block(), and finish() validate before publishing their next logical
 * input. Any error is terminal. A surrounding atomic artifact transaction is
 * responsible for discarding bytes already accepted before a later failure.
 */
class EngineTelemetryNdjsonEncoder final {
  public:
    EngineTelemetryNdjsonEncoder(const EngineTelemetryNdjsonEncoder &) = delete;
    EngineTelemetryNdjsonEncoder &
    operator=(const EngineTelemetryNdjsonEncoder &) = delete;
    EngineTelemetryNdjsonEncoder(EngineTelemetryNdjsonEncoder &&) noexcept = default;
    EngineTelemetryNdjsonEncoder &
    operator=(EngineTelemetryNdjsonEncoder &&) noexcept = default;

    [[nodiscard]] EngineTelemetryNdjsonEncodingStatus
    begin(const EngineTelemetryNdjsonChunkConsumer &consumer);
    [[nodiscard]] EngineTelemetryNdjsonEncodingStatus
    write_block(const EngineSessionBlockView &block,
                const EngineTelemetryNdjsonChunkConsumer &consumer);
    [[nodiscard]] EngineTelemetryNdjsonEncodingStatus
    write_block(const EngineTelemetryNdjsonBlockInput &block,
                const EngineTelemetryNdjsonChunkConsumer &consumer);
    [[nodiscard]] EngineTelemetryNdjsonEncodingStatus
    finish(const EngineTelemetryNdjsonChunkConsumer &consumer);

    [[nodiscard]] const EngineTelemetryNdjsonStreamDescriptor &
    descriptor() const noexcept;
    [[nodiscard]] std::uint64_t blocks_written() const noexcept;
    [[nodiscard]] std::uint64_t cycles_written() const noexcept;
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept;
    [[nodiscard]] const EngineEventCounters &event_totals() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

  private:
    enum class State : std::uint8_t { ready, begun, finished, failed };

    explicit EngineTelemetryNdjsonEncoder(
        EngineTelemetryNdjsonStreamDescriptor descriptor) noexcept;

    [[nodiscard]] EngineTelemetryNdjsonEncodingStatus
    fail(EngineTelemetryNdjsonEncodingError error) noexcept;

    EngineTelemetryNdjsonStreamDescriptor descriptor_;
    std::uint64_t blocks_written_ = 0;
    std::uint64_t cycles_written_ = 0;
    std::uint64_t bytes_emitted_ = 0;
    EngineEventCounters event_totals_;
    State state_ = State::ready;

    friend std::variant<EngineTelemetryNdjsonEncoder,
                        EngineTelemetryNdjsonEncodingError>
    make_engine_telemetry_ndjson_encoder(
        EngineTelemetryNdjsonStreamDescriptor descriptor);
};

using EngineTelemetryNdjsonEncoderResult =
    std::variant<EngineTelemetryNdjsonEncoder, EngineTelemetryNdjsonEncodingError>;

[[nodiscard]] EngineTelemetryNdjsonEncoderResult
make_engine_telemetry_ndjson_encoder(EngineTelemetryNdjsonStreamDescriptor descriptor);

[[nodiscard]] EngineTelemetryNdjsonEncoderResult make_engine_telemetry_ndjson_encoder(
    const EngineSessionDescriptor &session,
    const contract::Sha256Digest &simulation_request_identity_v7_sha256,
    std::size_t maximum_chunk_bytes = 16U * 1024U);

} // namespace engine_sim_offline::artifacts
