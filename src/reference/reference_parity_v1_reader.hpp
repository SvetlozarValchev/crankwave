#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>
#include <vector>

namespace crankwave::reference {

inline constexpr std::size_t kReferenceParityV1HeaderBytes = 608;
inline constexpr std::size_t kReferenceParityV1RecordBytes = 224;
inline constexpr std::size_t kReferenceParityV1CylinderDescriptorBytes = 80;
inline constexpr std::size_t kReferenceParityV1CylinderCount = 6;
inline constexpr std::size_t kReferenceParityV1RouteCount = 2;
inline constexpr std::uint32_t kReferenceParityV1SampleRateHz = 10000;
inline constexpr std::uint64_t kReferenceParityV1RecordCount = 170000;
inline constexpr std::uint64_t kReferenceParityV1AudibleStartRecord = 20000;
inline constexpr std::uint64_t kReferenceParityV1IntervalEndExclusive = 170000;
inline constexpr std::size_t kReferenceParityV1ByteCount =
    kReferenceParityV1HeaderBytes +
    static_cast<std::size_t>(kReferenceParityV1RecordCount) *
        kReferenceParityV1RecordBytes;
inline constexpr std::uint64_t kReferenceParityV1NoRecordIndex =
    std::numeric_limits<std::uint64_t>::max();

enum class ReferenceParityV1DecodeErrorCode : std::uint8_t {
    truncated_header,
    invalid_magic,
    unsupported_version,
    invalid_header_size,
    unsupported_cylinder_count,
    unsupported_route_count,
    invalid_record_size,
    unsupported_sample_rate,
    invalid_record_count,
    invalid_audible_start,
    invalid_interval_end,
    invalid_reference_parameter,
    invalid_cylinder_descriptor,
    truncated_payload,
    trailing_bytes,
    invalid_sample_index,
    invalid_step_end,
    invalid_fixture_time,
    invalid_step_duration,
    invalid_engine_speed,
    invalid_filtered_engine_speed,
    filtered_engine_speed_mismatch,
    invalid_crank_angle,
    invalid_control_timeline,
    nonzero_reserved_record,
    invalid_cylinder_pressure,
    content_digest_mismatch,
};

struct ReferenceParityV1DecodeError {
    ReferenceParityV1DecodeErrorCode code =
        ReferenceParityV1DecodeErrorCode::truncated_header;
    std::size_t byte_offset = 0;
    std::uint64_t record_index = kReferenceParityV1NoRecordIndex;

    friend bool operator==(const ReferenceParityV1DecodeError &,
                           const ReferenceParityV1DecodeError &) = default;
};

struct ReferenceParityV1Metadata {
    std::uint32_t version = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint64_t record_count = 0;
    std::uint64_t audible_start_record = 0;
    std::uint64_t interval_end_record_exclusive = 0;
    double reference_atmosphere_pa_abs = 0.0;
    double legacy_propagation_speed_m_s = 0.0;
    double excitation_scale = 0.0;
    double filtered_speed_threshold_rpm = 0.0;
    double gauge_static_gain = 0.0;
    double dynamic_forward_gain = 0.0;
    double dynamic_reverse_gain = 0.0;
    double cylinder_count_divisor = 0.0;
    double inverse_length_exponent = 0.0;

    friend bool operator==(const ReferenceParityV1Metadata &,
                           const ReferenceParityV1Metadata &) = default;
};

struct ReferenceParityV1CylinderDescriptor {
    std::uint32_t runtime_index = 0;
    std::uint32_t stable_id = 0;
    std::uint32_t firing_rank = 0;
    std::uint32_t route_index = 0;
    std::uint32_t delay_samples = 0;
    double firing_angle_rad = 0.0;
    double header_primary_length_m = 0.0;
    double exhaust_system_length_m = 0.0;
    double total_audio_length_m = 0.0;
    double gas_primary_tube_length_m = 0.0;
    double sound_attenuation_linear = 0.0;
    double route_audio_volume_linear = 0.0;

    friend bool operator==(const ReferenceParityV1CylinderDescriptor &,
                           const ReferenceParityV1CylinderDescriptor &) = default;
};

struct ReferenceParityV1Controls {
    double requested_throttle_01 = 0.0;
    double resolved_intake_throttle_01 = 0.0;
    bool ignition_enabled = false;
    bool fuel_enabled = false;
    bool starter_enabled = false;
    bool dyno_enabled = false;

    friend bool operator==(const ReferenceParityV1Controls &,
                           const ReferenceParityV1Controls &) = default;
};

struct ReferenceParityV1CylinderPressure {
    double static_pressure_pa_abs = 0.0;
    double dynamic_pressure_forward_pa = 0.0;
    double dynamic_pressure_reverse_pa = 0.0;

    friend bool operator==(const ReferenceParityV1CylinderPressure &,
                           const ReferenceParityV1CylinderPressure &) = default;
};

struct ReferenceParityV1Frame {
    std::uint64_t sample_index = 0;
    std::uint64_t step_end = 0;
    double fixture_time_s = 0.0;
    double dt_s = 0.0;
    double engine_speed_rpm = 0.0;
    double filtered_engine_speed_rpm = 0.0;
    double crank_angle_rad = 0.0;
    ReferenceParityV1Controls controls;
    std::array<ReferenceParityV1CylinderPressure, kReferenceParityV1CylinderCount>
        cylinders{};

    friend bool operator==(const ReferenceParityV1Frame &,
                           const ReferenceParityV1Frame &) = default;
};

struct DecodedReferenceParityV1 {
    ReferenceParityV1Metadata metadata;
    std::array<ReferenceParityV1CylinderDescriptor, kReferenceParityV1CylinderCount>
        cylinders{};
    std::vector<ReferenceParityV1Frame> frames;

    friend bool operator==(const DecodedReferenceParityV1 &,
                           const DecodedReferenceParityV1 &) = default;
};

using ReferenceParityV1DecodeResult =
    std::variant<DecodedReferenceParityV1, ReferenceParityV1DecodeError>;

// Strict, path-free decoder for the frozen ESOPAR01 v1 comparator evidence.
// The returned value owns every retained field. This reader is reference tooling:
// captured pressures, crank angle, filtered RPM, and controls are evidence and must
// never be supplied to the simulator as a prebuilt physical or excitation result.
[[nodiscard]] ReferenceParityV1DecodeResult
decode_reference_parity_v1(std::span<const std::byte> bytes);

} // namespace crankwave::reference
