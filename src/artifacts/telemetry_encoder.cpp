#include "engine_sim_offline/artifacts/telemetry_encoder.hpp"

#include "telemetry_encoder_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <ranges>
#include <utility>

namespace engine_sim_offline::artifacts {
namespace {

TelemetryEncodingError error(TelemetryEncodingErrorCode code, std::string path,
                             std::string message) {
    return {code, std::move(path), std::move(message)};
}

bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

std::uint8_t phase_code(contract::SamplePhase phase) noexcept {
    switch (phase) {
    case contract::SamplePhase::pre_step:
        return 1;
    case contract::SamplePhase::post_step:
        return 2;
    case contract::SamplePhase::unspecified:
        return 0;
    }
    return 0;
}

bool known_phase(contract::SamplePhase phase) noexcept {
    return phase == contract::SamplePhase::pre_step ||
           phase == contract::SamplePhase::post_step;
}

template <class T> bool same_span(std::span<const T> lhs, std::span<const T> rhs) {
    return std::ranges::equal(lhs, rhs);
}

bool same_layout(const contract::CaptureLayoutView &layout,
                 contract::EngineId engine_id,
                 std::span<const contract::CylinderId> cylinders,
                 std::span<const contract::PortIdentity> ports,
                 std::span<const contract::GasVolumeIdentity> gas_volumes,
                 std::span<const contract::FlowEdgeIdentity> flow_edges,
                 std::span<const contract::RouteIdentity> routes) {
    return layout.engine_id() == engine_id &&
           same_span(layout.cylinders(), cylinders) &&
           same_span(layout.ports(), ports) &&
           same_span(layout.gas_volumes(), gas_volumes) &&
           same_span(layout.flow_edges(), flow_edges) &&
           same_span(layout.routes(), routes);
}

bool topology_sizes_fit(const contract::CaptureLayoutView &layout) {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    if (layout.cylinders().size() > maximum || layout.ports().size() > maximum ||
        layout.gas_volumes().size() > maximum || layout.flow_edges().size() > maximum ||
        layout.routes().size() > maximum) {
        return false;
    }
    return std::ranges::all_of(layout.routes(), [](const auto &route) {
        return !route.emitter_anchor_id.has_value() ||
               route.emitter_anchor_id->size() <=
                   std::numeric_limits<std::uint32_t>::max();
    });
}

} // namespace

namespace detail {

TelemetryByteEmitter::TelemetryByteEmitter(std::size_t maximum_chunk_bytes,
                                           std::uint64_t initial_offset,
                                           const TelemetryChunkConsumer &consumer)
    : buffer_(maximum_chunk_bytes), offset_(initial_offset), consumer_(consumer) {}

bool TelemetryByteEmitter::append_byte(std::byte value) {
    buffer_[used_++] = value;
    return used_ != buffer_.size() || flush();
}

bool TelemetryByteEmitter::append(std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto available = buffer_.size() - used_;
        const auto count = std::min(available, bytes.size());
        std::copy_n(bytes.begin(), count,
                    buffer_.begin() + static_cast<std::ptrdiff_t>(used_));
        used_ += count;
        bytes = bytes.subspan(count);
        if (used_ == buffer_.size() && !flush()) {
            return false;
        }
    }
    return true;
}

bool TelemetryByteEmitter::append_u8(std::uint8_t value) {
    return append_byte(static_cast<std::byte>(value));
}

bool TelemetryByteEmitter::append_u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        if (!append_byte(static_cast<std::byte>((value >> shift) & 0xffU))) {
            return false;
        }
    }
    return true;
}

bool TelemetryByteEmitter::append_u64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        if (!append_byte(static_cast<std::byte>((value >> shift) & 0xffU))) {
            return false;
        }
    }
    return true;
}

bool TelemetryByteEmitter::append_f64(double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    return append_u64(std::bit_cast<std::uint64_t>(value));
}

bool TelemetryByteEmitter::append_bool(bool value) {
    return append_u8(value ? 1U : 0U);
}

bool TelemetryByteEmitter::append_fourcc(const std::array<char, 4> &value) {
    for (const auto character : value) {
        if (!append_byte(
                static_cast<std::byte>(static_cast<unsigned char>(character)))) {
            return false;
        }
    }
    return true;
}

bool TelemetryByteEmitter::append_string(std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max() ||
        !append_u32(static_cast<std::uint32_t>(value.size()))) {
        return false;
    }
    return append(std::as_bytes(std::span<const char>{value.data(), value.size()}));
}

bool TelemetryByteEmitter::finish() {
    return flush();
}

std::uint64_t TelemetryByteEmitter::offset() const noexcept {
    return offset_;
}

bool TelemetryByteEmitter::offset_overflowed() const noexcept {
    return offset_overflowed_;
}

bool TelemetryByteEmitter::flush() {
    if (used_ == 0) {
        return true;
    }
    if (used_ > std::numeric_limits<std::uint64_t>::max() - offset_) {
        offset_overflowed_ = true;
        return false;
    }
    bool accepted = false;
    try {
        accepted =
            consumer_(offset_, std::span<const std::byte>{buffer_.data(), used_});
    } catch (...) {
        accepted = false;
    }
    if (!accepted) {
        return false;
    }
    offset_ += used_;
    used_ = 0;
    return true;
}

} // namespace detail

TelemetryEncoder::TelemetryEncoder(TelemetryStreamDescriptor descriptor,
                                   contract::EngineId engine_id,
                                   std::vector<contract::CylinderId> cylinders,
                                   std::vector<contract::PortIdentity> ports,
                                   std::vector<contract::GasVolumeIdentity> gas_volumes,
                                   std::vector<contract::FlowEdgeIdentity> flow_edges,
                                   std::vector<contract::RouteIdentity> routes) noexcept
    : descriptor_(std::move(descriptor)), engine_id_(engine_id),
      cylinders_(std::move(cylinders)), ports_(std::move(ports)),
      gas_volumes_(std::move(gas_volumes)), flow_edges_(std::move(flow_edges)),
      routes_(std::move(routes)) {}

TelemetryEncodingStatus TelemetryEncoder::fail(TelemetryEncodingError value) noexcept {
    state_ = State::failed;
    return value;
}

TelemetryEncodingStatus
TelemetryEncoder::begin(const TelemetryChunkConsumer &consumer) {
    if (state_ != State::ready) {
        return fail(error(TelemetryEncodingErrorCode::invalid_state, "state",
                          "telemetry header may be emitted exactly once"));
    }

    detail::TelemetryByteEmitter emitter{descriptor_.maximum_chunk_bytes,
                                         bytes_emitted_, consumer};
    constexpr std::array<std::byte, 8> magic{
        std::byte{'E'}, std::byte{'S'}, std::byte{'O'},  std::byte{'T'},
        std::byte{'L'}, std::byte{'M'}, std::byte{'\r'}, std::byte{'\n'},
    };
    bool emitted =
        emitter.append(magic) && emitter.append_u32(kCaptureTelemetrySchemaVersion) &&
        emitter.append_u32(descriptor_.includes_reference_parity ? 1U : 0U) &&
        emitter.append_u64(descriptor_.first_clock.rate.numerator) &&
        emitter.append_u64(descriptor_.first_clock.rate.denominator) &&
        emitter.append_u8(phase_code(descriptor_.first_clock.phase));
    for (std::uint8_t index = 0; emitted && index < 7; ++index) {
        emitted = emitter.append_u8(0);
    }
    emitted = emitted &&
              emitter.append_u64(descriptor_.first_clock.first_sample_index) &&
              emitter.append_u64(descriptor_.first_clock.first_timestamp_tick) &&
              emitter.append_u64(descriptor_.frame_count) &&
              detail::emit_layout(emitter, engine_id_, cylinders_, ports_, gas_volumes_,
                                  flow_edges_, routes_) &&
              emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        if (emitter.offset_overflowed()) {
            return fail(error(TelemetryEncodingErrorCode::size_overflow, "byte_offset",
                              "telemetry byte offset overflowed uint64"));
        }
        return fail(error(TelemetryEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected telemetry header bytes"));
    }

    state_ = State::begun;
    return std::nullopt;
}

TelemetryEncodingStatus
TelemetryEncoder::write_block(const contract::CaptureBlockView &block,
                              const TelemetryChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(TelemetryEncodingErrorCode::invalid_state, "state",
                          "capture rows require an emitted telemetry header"));
    }

    const auto report = contract::validate(block);
    if (!report.ok()) {
        const auto &issue = report.issues.front();
        return fail(error(TelemetryEncodingErrorCode::invalid_capture_block, issue.path,
                          issue.message));
    }
    if (!same_layout(block.layout(), engine_id_, cylinders_, ports_, gas_volumes_,
                     flow_edges_, routes_)) {
        return fail(error(TelemetryEncodingErrorCode::inconsistent_layout,
                          "block.layout",
                          "capture topology differs from the stream header"));
    }

    std::uint64_t expected_sample_index = 0;
    std::uint64_t expected_timestamp_tick = 0;
    const auto origin_valid = checked_add(descriptor_.first_clock.first_sample_index,
                                          frames_written_, expected_sample_index) &&
                              checked_add(descriptor_.first_clock.first_timestamp_tick,
                                          frames_written_, expected_timestamp_tick);
    if (!origin_valid || block.clock().rate != descriptor_.first_clock.rate ||
        block.clock().phase != descriptor_.first_clock.phase ||
        block.clock().first_sample_index != expected_sample_index ||
        block.clock().first_timestamp_tick != expected_timestamp_tick) {
        return fail(error(
            TelemetryEncodingErrorCode::discontinuous_clock, "block.clock",
            "capture block does not begin at the next stream sample and timestamp"));
    }

    std::uint64_t next_frame_count = 0;
    if (block.frame_count() == 0 ||
        !checked_add(frames_written_, block.frame_count(), next_frame_count) ||
        next_frame_count > descriptor_.frame_count) {
        return fail(
            error(TelemetryEncodingErrorCode::frame_count_mismatch, "block.frame_count",
                  "capture block is empty or exceeds the declared stream horizon"));
    }
    if (block.reference_parity().has_value() != descriptor_.includes_reference_parity) {
        return fail(
            error(TelemetryEncodingErrorCode::reference_parity_mismatch,
                  "block.reference_parity",
                  "reference-parity presence differs from the telemetry schema flag"));
    }
    if (auto finite_error = detail::validate_all_serialized_values_finite(block);
        finite_error.has_value()) {
        return fail(std::move(*finite_error));
    }

    detail::TelemetryByteEmitter emitter{descriptor_.maximum_chunk_bytes,
                                         bytes_emitted_, consumer};
    bool emitted = true;
    for (std::uint32_t frame = 0; frame < block.frame_count(); ++frame) {
        emitted =
            detail::emit_frame(emitter, block, frame, expected_sample_index + frame,
                               expected_timestamp_tick + frame);
        if (!emitted) {
            break;
        }
    }
    emitted = emitted && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        if (emitter.offset_overflowed()) {
            return fail(error(TelemetryEncodingErrorCode::size_overflow, "byte_offset",
                              "telemetry byte offset overflowed uint64"));
        }
        return fail(error(TelemetryEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected telemetry row bytes"));
    }

    frames_written_ = next_frame_count;
    return std::nullopt;
}

TelemetryEncodingStatus
TelemetryEncoder::finish(const TelemetryChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(TelemetryEncodingErrorCode::invalid_state, "state",
                          "only a begun telemetry stream can be finished"));
    }
    if (frames_written_ != descriptor_.frame_count) {
        return fail(
            error(TelemetryEncodingErrorCode::frame_count_mismatch, "frames_written",
                  "telemetry stream did not receive exactly its declared frame count"));
    }

    detail::TelemetryByteEmitter emitter{descriptor_.maximum_chunk_bytes,
                                         bytes_emitted_, consumer};
    const auto emitted = emitter.append_fourcc({'E', 'N', 'D', '1'}) &&
                         emitter.append_u64(frames_written_) && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        if (emitter.offset_overflowed()) {
            return fail(error(TelemetryEncodingErrorCode::size_overflow, "byte_offset",
                              "telemetry byte offset overflowed uint64"));
        }
        return fail(error(TelemetryEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected telemetry footer bytes"));
    }
    state_ = State::finished;
    return std::nullopt;
}

const TelemetryStreamDescriptor &TelemetryEncoder::descriptor() const noexcept {
    return descriptor_;
}

std::uint64_t TelemetryEncoder::frames_written() const noexcept {
    return frames_written_;
}

std::uint64_t TelemetryEncoder::bytes_emitted() const noexcept {
    return bytes_emitted_;
}

std::size_t TelemetryEncoder::maximum_chunk_bytes() const noexcept {
    return descriptor_.maximum_chunk_bytes;
}

bool TelemetryEncoder::failed() const noexcept {
    return state_ == State::failed;
}

bool TelemetryEncoder::finished() const noexcept {
    return state_ == State::finished;
}

TelemetryEncoderResult make_telemetry_encoder(const contract::CaptureLayoutView &layout,
                                              TelemetryStreamDescriptor descriptor) {
    const auto layout_report = contract::validate(layout);
    if (!layout_report.ok()) {
        const auto &issue = layout_report.issues.front();
        return error(TelemetryEncodingErrorCode::invalid_layout, issue.path,
                     issue.message);
    }
    if (!contract::validate(descriptor.first_clock.rate).ok() ||
        !known_phase(descriptor.first_clock.phase) || descriptor.frame_count == 0 ||
        descriptor.maximum_chunk_bytes == 0 ||
        descriptor.maximum_chunk_bytes > kMaximumTelemetryEncoderChunkBytes) {
        return error(TelemetryEncodingErrorCode::invalid_descriptor, "descriptor",
                     "telemetry stream descriptor is structurally invalid");
    }

    const auto phase_offset =
        descriptor.first_clock.phase == contract::SamplePhase::post_step ? 1U : 0U;
    std::uint64_t expected_timestamp_origin = 0;
    std::uint64_t final_sample_index = 0;
    std::uint64_t final_timestamp_tick = 0;
    if (!checked_add(descriptor.first_clock.first_sample_index, phase_offset,
                     expected_timestamp_origin) ||
        descriptor.first_clock.first_timestamp_tick != expected_timestamp_origin ||
        !checked_add(descriptor.first_clock.first_sample_index,
                     descriptor.frame_count - 1, final_sample_index) ||
        !checked_add(descriptor.first_clock.first_timestamp_tick,
                     descriptor.frame_count - 1, final_timestamp_tick)) {
        return error(
            TelemetryEncodingErrorCode::invalid_descriptor, "descriptor.first_clock",
            "telemetry clock origin or horizon overflows or violates sample phase");
    }
    if (!topology_sizes_fit(layout)) {
        return error(TelemetryEncodingErrorCode::size_overflow, "layout",
                     "telemetry topology count or string length exceeds uint32");
    }

    return TelemetryEncoder{
        std::move(descriptor),
        layout.engine_id(),
        {layout.cylinders().begin(), layout.cylinders().end()},
        {layout.ports().begin(), layout.ports().end()},
        {layout.gas_volumes().begin(), layout.gas_volumes().end()},
        {layout.flow_edges().begin(), layout.flow_edges().end()},
        {layout.routes().begin(), layout.routes().end()},
    };
}

} // namespace engine_sim_offline::artifacts
