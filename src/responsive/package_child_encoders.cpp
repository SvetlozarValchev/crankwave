#include "engine_sim_offline/responsive/package_children.hpp"

#include "engine_sim_offline/artifacts/revengine_container.hpp"
#include "engine_sim_offline/authoring/json.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {
namespace {

using Error = NativeResponsivePackageError;
using ErrorCode = NativeResponsivePackageErrorCode;

constexpr std::size_t kMaximumJsonBytes = 16U * 1024U * 1024U;
constexpr std::uint64_t kLifecyclePhysicsFramesPerBlock = 200U;
constexpr std::string_view kSharedStarterAudioRelativePath =
    "audio/recorded-starter.cropped.192000hz.mono.f32le";
constexpr std::string_view kSharedStarterSourceSha256 =
    "8edcfa21f846098472dd3f57236f23367a7667f4458f7452b565370062635a81";
constexpr std::string_view kSharedStarterManifestSha256 =
    "1fb698a9c304ecee323361b059dbfc615ab82c06357e01b815faa8f3a008365e";
constexpr std::string_view kSharedStarterPayloadSha256 =
    "1949863ca58aef11146d4a842609ef217f6b7df4ba6db38f478eb918cef2964a";
constexpr std::size_t kSharedStarterManifestByteCount = 3'120U;
constexpr std::size_t kSharedStarterPayloadByteCount = 1'037'272U;

[[nodiscard]] Error error(const ErrorCode code, std::string detail_code,
                          std::string path, std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] Error cancelled_error() {
    return error(ErrorCode::cancelled, "responsive-child-encoding-cancelled", {},
                 "responsive child package encoding was cancelled");
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto value : digest.bytes) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

class JsonWriter final {
  public:
    [[nodiscard]] bool begin_object() {
        if (!before_value()) {
            return false;
        }
        bytes_.push_back('{');
        stack_.push_back({Kind::object, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_object() {
        if (stack_.empty() || stack_.back().kind != Kind::object ||
            stack_.back().awaiting_value) {
            return fail("invalid object close");
        }
        bytes_.push_back('}');
        stack_.pop_back();
        return size_ok();
    }

    [[nodiscard]] bool begin_array() {
        if (!before_value()) {
            return false;
        }
        bytes_.push_back('[');
        stack_.push_back({Kind::array, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_array() {
        if (stack_.empty() || stack_.back().kind != Kind::array) {
            return fail("invalid array close");
        }
        bytes_.push_back(']');
        stack_.pop_back();
        return size_ok();
    }

    [[nodiscard]] bool key(const std::string_view value) {
        if (stack_.empty() || stack_.back().kind != Kind::object ||
            stack_.back().awaiting_value) {
            return fail("object key is out of sequence");
        }
        auto &context = stack_.back();
        if (context.count != 0U) {
            bytes_.push_back(',');
        }
        if (!append_string(value)) {
            return false;
        }
        bytes_.push_back(':');
        context.awaiting_value = true;
        return size_ok();
    }

    [[nodiscard]] bool string_value(const std::string_view value) {
        return before_value() && append_string(value);
    }

    [[nodiscard]] bool number_value(const double value) {
        if (!std::isfinite(value) || !before_value()) {
            return fail("non-finite JSON number");
        }
        std::array<char, 64U> encoded{};
        const auto normalized = value == 0.0 ? 0.0 : value;
        const auto [end, conversion_error] = std::to_chars(
            encoded.data(), encoded.data() + encoded.size(), normalized,
            std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (conversion_error != std::errc{}) {
            return fail("JSON number conversion failed");
        }
        bytes_.append(encoded.data(), static_cast<std::size_t>(end - encoded.data()));
        return size_ok();
    }

    [[nodiscard]] bool uint_value(const std::uint64_t value) {
        if (!before_value()) {
            return false;
        }
        return append_uint(value);
    }

    [[nodiscard]] bool uint_string_value(const std::uint64_t value) {
        if (!before_value()) {
            return false;
        }
        bytes_.push_back('"');
        if (!append_uint(value)) {
            return false;
        }
        bytes_.push_back('"');
        return size_ok();
    }

    [[nodiscard]] bool bool_value(const bool value) {
        if (!before_value()) {
            return false;
        }
        bytes_.append(value ? "true" : "false");
        return size_ok();
    }

    [[nodiscard]] std::variant<std::vector<std::byte>, Error>
    finish(const std::string_view path) {
        if (!failed_ && !root_written_) {
            static_cast<void>(fail("JSON root is absent"));
        }
        if (!failed_ && !stack_.empty()) {
            static_cast<void>(fail("JSON containers remain open"));
        }
        if (failed_) {
            return error(ErrorCode::invalid_argument,
                         "responsive-child-json-encoding-failed", std::string{path},
                         failure_);
        }
        bytes_.push_back('\n');
        std::vector<std::byte> output(bytes_.size());
        std::transform(
            bytes_.begin(), bytes_.end(), output.begin(), [](const char value) {
                return static_cast<std::byte>(static_cast<unsigned char>(value));
            });
        return output;
    }

  private:
    enum class Kind : std::uint8_t { object, array };
    struct Context {
        Kind kind = Kind::object;
        std::size_t count = 0U;
        bool awaiting_value = false;
    };

    [[nodiscard]] bool before_value() {
        if (failed_) {
            return false;
        }
        if (stack_.empty()) {
            if (root_written_) {
                return fail("multiple JSON roots");
            }
            root_written_ = true;
            return true;
        }
        auto &context = stack_.back();
        if (context.kind == Kind::object) {
            if (!context.awaiting_value) {
                return fail("object value has no key");
            }
            context.awaiting_value = false;
            ++context.count;
            return true;
        }
        if (context.count != 0U) {
            bytes_.push_back(',');
        }
        ++context.count;
        return size_ok();
    }

    [[nodiscard]] bool append_uint(const std::uint64_t value) {
        std::array<char, 32U> encoded{};
        const auto [end, conversion_error] =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (conversion_error != std::errc{}) {
            return fail("JSON integer conversion failed");
        }
        bytes_.append(encoded.data(), static_cast<std::size_t>(end - encoded.data()));
        return size_ok();
    }

    [[nodiscard]] bool append_string(const std::string_view value) {
        constexpr std::string_view digits = "0123456789abcdef";
        bytes_.push_back('"');
        for (const unsigned char character : value) {
            switch (character) {
            case '"':
                bytes_.append("\\\"");
                break;
            case '\\':
                bytes_.append("\\\\");
                break;
            case '\b':
                bytes_.append("\\b");
                break;
            case '\f':
                bytes_.append("\\f");
                break;
            case '\n':
                bytes_.append("\\n");
                break;
            case '\r':
                bytes_.append("\\r");
                break;
            case '\t':
                bytes_.append("\\t");
                break;
            default:
                if (character < 0x20U) {
                    bytes_.append("\\u00");
                    bytes_.push_back(digits[character >> 4U]);
                    bytes_.push_back(digits[character & 0x0fU]);
                } else {
                    bytes_.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        bytes_.push_back('"');
        return size_ok();
    }

    [[nodiscard]] bool size_ok() {
        return bytes_.size() <= kMaximumJsonBytes ||
               fail("JSON document exceeds the responsive child limit");
    }

    [[nodiscard]] bool fail(std::string message) {
        if (!failed_) {
            failed_ = true;
            failure_ = std::move(message);
        }
        return false;
    }

    std::string bytes_;
    std::vector<Context> stack_;
    bool root_written_ = false;
    bool failed_ = false;
    std::string failure_;
};

[[nodiscard]] bool field(JsonWriter &writer, const std::string_view key,
                         const std::string_view value) {
    return writer.key(key) && writer.string_value(value);
}

[[nodiscard]] bool field(JsonWriter &writer, const std::string_view key,
                         const double value) {
    return writer.key(key) && writer.number_value(value);
}

[[nodiscard]] bool uint_field(JsonWriter &writer, const std::string_view key,
                              const std::uint64_t value) {
    return writer.key(key) && writer.uint_value(value);
}

[[nodiscard]] bool uint_string_field(JsonWriter &writer, const std::string_view key,
                                     const std::uint64_t value) {
    return writer.key(key) && writer.uint_string_value(value);
}

[[nodiscard]] bool bool_field(JsonWriter &writer, const std::string_view key,
                              const bool value) {
    return writer.key(key) && writer.bool_value(value);
}

[[nodiscard]] bool digest_field(JsonWriter &writer, const std::string_view key,
                                const contract::Sha256Digest &value) {
    return field(writer, key, digest_hex(value));
}

[[nodiscard]] std::vector<std::byte>
float32_le_bytes(const std::span<const float> samples) {
    std::vector<std::byte> result;
    result.reserve(samples.size() * sizeof(float));
    for (const float sample : samples) {
        const auto bits = std::bit_cast<std::uint32_t>(sample);
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            result.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
        }
    }
    return result;
}

[[nodiscard]] bool finite(const std::initializer_list<double> values) {
    return std::ranges::all_of(values,
                               [](const double value) { return std::isfinite(value); });
}

[[nodiscard]] bool write_quantity(JsonWriter &writer,
                                  const authoring::Quantity &quantity) {
    bool ok = writer.begin_object() && field(writer, "value", quantity.value) &&
              field(writer, "unit", quantity.unit);
    if (quantity.standard.has_value()) {
        ok = ok && field(writer, "standard", *quantity.standard);
    }
    return ok && writer.end_object();
}

[[nodiscard]] std::string_view
interpolation_name(const authoring::TrajectoryInterpolation interpolation) {
    switch (interpolation) {
    case authoring::TrajectoryInterpolation::right_continuous_hold:
        return "right_continuous_hold";
    case authoring::TrajectoryInterpolation::linear:
        return "linear";
    }
    return {};
}

[[nodiscard]] bool
write_scalar_trajectory(JsonWriter &writer,
                        const authoring::ScalarTrajectory &trajectory) {
    bool ok =
        writer.begin_object() &&
        field(writer, "interpolation", interpolation_name(trajectory.interpolation)) &&
        writer.key("points") && writer.begin_array();
    for (const auto &point : trajectory.points) {
        ok = ok && writer.begin_object() && writer.key("time") &&
             write_quantity(writer, point.time) &&
             field(writer, "value", point.value) && writer.end_object();
    }
    return ok && writer.end_array() && writer.end_object();
}

[[nodiscard]] bool
write_quantity_trajectory(JsonWriter &writer,
                          const authoring::QuantityTrajectory &trajectory) {
    if (trajectory.value_dimension != authoring::QuantityDimension::torque) {
        return false;
    }
    bool ok =
        writer.begin_object() && field(writer, "value_dimension", "torque") &&
        field(writer, "interpolation", interpolation_name(trajectory.interpolation)) &&
        writer.key("points") && writer.begin_array();
    for (const auto &point : trajectory.points) {
        ok = ok && writer.begin_object() && writer.key("time") &&
             write_quantity(writer, point.time) && writer.key("value") &&
             write_quantity(writer, point.value) && writer.end_object();
    }
    return ok && writer.end_array() && writer.end_object();
}

[[nodiscard]] bool write_preparation(JsonWriter &writer,
                                     const authoring::Preparation &preparation) {
    if (const auto *fixed =
            std::get_if<authoring::FixedSettlingPreparation>(&preparation)) {
        return writer.begin_object() && field(writer, "type", "fixed_settling") &&
               writer.key("warm_up_duration") &&
               write_quantity(writer, fixed->warm_up_duration) &&
               writer.key("settling_duration") &&
               write_quantity(writer, fixed->settling_duration) && writer.end_object();
    }
    const auto &fixed = std::get<authoring::FixedHorizonPreparation>(preparation);
    return writer.begin_object() && field(writer, "type", "fixed_horizon") &&
           writer.key("preparation_duration") &&
           write_quantity(writer, fixed.preparation_duration) &&
           uint_field(writer, "trailing_complete_cycle_count",
                      fixed.trailing_complete_cycle_count) &&
           writer.end_object();
}

[[nodiscard]] bool write_free_engine_mode(JsonWriter &writer,
                                          const authoring::FreeEngineMode &mode) {
    bool ok = writer.begin_object() && field(writer, "type", "free_engine");
    if (mode.attached_inertia.has_value()) {
        ok = ok && writer.key("attached_inertia") &&
             write_quantity(writer, *mode.attached_inertia);
    }
    ok = ok && writer.key("throttle_01") &&
         write_scalar_trajectory(writer, mode.throttle_01);
    if (mode.external_resisting_torque.has_value()) {
        ok = ok && writer.key("external_resisting_torque") &&
             write_quantity_trajectory(writer, *mode.external_resisting_torque);
    }
    return ok && writer.end_object();
}

[[nodiscard]] bool
write_operating_state_patch(JsonWriter &writer,
                            const authoring::OperatingStatePatch &patch) {
    bool ok = writer.begin_object() && field(writer, "type", "operating_state_patch");
    const auto append = [&](const std::string_view name,
                            const std::optional<bool> &value) {
        if (value.has_value()) {
            ok = ok && bool_field(writer, name, *value);
        }
    };
    append("ignition_enabled", patch.ignition_enabled);
    append("fuel_enabled", patch.fuel_enabled);
    append("starter_enabled", patch.starter_enabled);
    append("dyno_enabled", patch.dyno_enabled);
    append("limiter_enabled", patch.limiter_enabled);
    return ok && writer.end_object();
}

[[nodiscard]] bool write_rate(JsonWriter &writer, const authoring::RationalRate &rate) {
    return writer.begin_object() &&
           uint_string_field(writer, "numerator", rate.numerator) &&
           uint_string_field(writer, "denominator", rate.denominator) &&
           field(writer, "unit", rate.unit) && writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_scenario(const authoring::ScenarioDocument &scenario,
                const std::string_view path) {
    const auto *mode = std::get_if<authoring::FreeEngineMode>(&scenario.mode);
    if (mode == nullptr || scenario.output.telemetry_channels.size() != 0U) {
        return error(ErrorCode::invalid_argument,
                     "responsive-lifecycle-scenario-shape-unsupported",
                     std::string{path},
                     "published lifecycle scenarios require free_engine mode and no "
                     "authored telemetry channels");
    }
    JsonWriter writer;
    bool ok =
        writer.begin_object() && field(writer, "schema", scenario.schema) &&
        field(writer, "id", scenario.id.value) &&
        field(writer, "engine", scenario.engine.value) &&
        field(writer, "fuel", scenario.fuel.value) && writer.key("ambient") &&
        writer.begin_object() && writer.key("pressure") &&
        write_quantity(writer, scenario.ambient.pressure) &&
        writer.key("temperature") &&
        write_quantity(writer, scenario.ambient.temperature) &&
        field(writer, "relative_humidity_01", scenario.ambient.relative_humidity_01) &&
        writer.end_object() && writer.key("initial_thermal_state") &&
        writer.begin_object() && writer.key("gas_temperature") &&
        write_quantity(writer, scenario.initial_thermal_state.gas_temperature) &&
        writer.key("wall_temperature") &&
        write_quantity(writer, scenario.initial_thermal_state.wall_temperature) &&
        writer.key("coolant_temperature") &&
        write_quantity(writer, scenario.initial_thermal_state.coolant_temperature) &&
        writer.key("oil_temperature") &&
        write_quantity(writer, scenario.initial_thermal_state.oil_temperature) &&
        writer.end_object() && writer.key("crankcase") && writer.begin_object() &&
        writer.key("pressure") && write_quantity(writer, scenario.crankcase.pressure) &&
        writer.key("temperature") &&
        write_quantity(writer, scenario.crankcase.temperature) && writer.end_object() &&
        writer.key("initial_state") && writer.begin_object() &&
        writer.key("engine_speed") &&
        write_quantity(writer, scenario.initial_state.engine_speed) &&
        writer.key("crank_angle") &&
        write_quantity(writer, scenario.initial_state.crank_angle) &&
        bool_field(writer, "ignition_enabled",
                   scenario.initial_state.ignition_enabled) &&
        bool_field(writer, "fuel_enabled", scenario.initial_state.fuel_enabled) &&
        bool_field(writer, "starter_enabled", scenario.initial_state.starter_enabled) &&
        bool_field(writer, "dyno_enabled", scenario.initial_state.dyno_enabled) &&
        bool_field(writer, "limiter_enabled", scenario.initial_state.limiter_enabled) &&
        writer.end_object() && writer.key("preparation") &&
        write_preparation(writer, scenario.preparation) && writer.key("mode") &&
        write_free_engine_mode(writer, *mode) && writer.key("events") &&
        writer.begin_array();
    for (const auto &event : scenario.events) {
        const auto *patch = std::get_if<authoring::OperatingStatePatch>(&event.payload);
        if (patch == nullptr) {
            return error(ErrorCode::invalid_argument,
                         "responsive-lifecycle-event-shape-unsupported",
                         std::string{path},
                         "published lifecycle scenarios require operating-state "
                         "patch events");
        }
        ok = ok && writer.begin_object() && field(writer, "id", event.id.value) &&
             writer.key("time") && write_quantity(writer, event.time) &&
             writer.key("payload") && write_operating_state_patch(writer, *patch) &&
             writer.end_object();
    }
    ok = ok && writer.end_array() && writer.key("rates") && writer.begin_object() &&
         writer.key("physics") && write_rate(writer, scenario.rates.physics) &&
         writer.key("capture") && write_rate(writer, scenario.rates.capture) &&
         writer.key("source_processing") &&
         write_rate(writer, scenario.rates.source_processing) &&
         writer.key("acoustics") && write_rate(writer, scenario.rates.acoustics) &&
         writer.key("delivery") && write_rate(writer, scenario.rates.delivery) &&
         writer.end_object() && writer.key("quality") && writer.begin_object() &&
         field(writer, "id", scenario.quality.id) &&
         uint_field(writer, "process_block_capacity_frames",
                    scenario.quality.process_block_capacity_frames) &&
         uint_field(writer, "event_queue_capacity",
                    scenario.quality.event_queue_capacity) &&
         uint_field(writer, "telemetry_capacity_frames",
                    scenario.quality.telemetry_capacity_frames) &&
         writer.end_object() && writer.key("total_duration") &&
         write_quantity(writer, scenario.total_duration) &&
         writer.key("audible_start") &&
         write_quantity(writer, scenario.audible_start) &&
         writer.key("audible_duration") &&
         write_quantity(writer, scenario.audible_duration) &&
         uint_string_field(writer, "public_seed", scenario.public_seed) &&
         writer.key("output") && writer.begin_object() && writer.key("buses") &&
         writer.begin_array();
    for (const auto &bus : scenario.output.buses) {
        ok = ok && writer.string_value(bus.value);
    }
    ok = ok && writer.end_array() && writer.key("telemetry_channels") &&
         writer.begin_array() && writer.end_array() && writer.end_object() &&
         writer.end_object();
    static_cast<void>(ok);
    auto finished = writer.finish(path);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&finished);
    if (bytes == nullptr) {
        return std::get<Error>(std::move(finished));
    }
    authoring::AuthoringParseLimits limits;
    limits.json.maximum_input_bytes = kMaximumJsonBytes;
    const std::string_view encoded{reinterpret_cast<const char *>(bytes->data()),
                                   bytes->size()};
    auto parsed = authoring::parse_scenario_document(encoded, limits);
    const auto *round_trip = std::get_if<authoring::ScenarioDocument>(&parsed);
    if (round_trip == nullptr || *round_trip != scenario) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-lifecycle-scenario-roundtrip-failed",
                     std::string{path},
                     "encoded lifecycle scenario did not round-trip through the "
                     "strict authoring boundary");
    }
    return std::move(*bytes);
}

[[nodiscard]] bool write_cycle(JsonWriter &writer, const LifecycleCaptureCycle &cycle) {
    return writer.begin_object() &&
           uint_field(writer, "start_frame", cycle.start_frame) &&
           uint_field(writer, "end_frame", cycle.end_frame) &&
           field(writer, "mean_rpm", cycle.mean_rpm) && writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_capture_evidence(const LifecycleCaptureEvidence &capture,
                        const std::string_view role, const std::string_view path) {
    JsonWriter writer;
    bool ok = writer.begin_object() &&
              field(writer, "schema", kLifecycleCaptureEvidenceSchema) &&
              field(writer, "role", role) &&
              uint_field(writer, "physics_rate_hz", capture.physics_rate_hz) &&
              uint_field(writer, "delivery_rate_hz", capture.delivery_rate_hz) &&
              field(writer, "scenario_id", capture.scenario_id) &&
              writer.key("points") && writer.begin_array();
    for (const auto &point : capture.points) {
        ok = ok && writer.begin_object() &&
             uint_field(writer, "source_frame", point.source_frame) &&
             uint_field(writer, "physics_step_end", point.physics_step_end) &&
             field(writer, "simulation_seconds", point.simulation_seconds) &&
             field(writer, "rpm", point.rpm) &&
             bool_field(writer, "ignition_enabled", point.ignition_enabled) &&
             bool_field(writer, "fuel_enabled", point.fuel_enabled) &&
             bool_field(writer, "starter_enabled", point.starter_enabled) &&
             field(writer, "indicated_gas_torque_nm", point.indicated_gas_torque_nm) &&
             uint_field(writer, "indicated_gas_availability",
                        static_cast<std::uint32_t>(point.indicated_gas_availability)) &&
             writer.end_object();
    }
    ok = ok && writer.end_array() && writer.key("completed_cycles") &&
         writer.begin_array();
    for (const auto &cycle : capture.completed_cycles) {
        ok = ok && write_cycle(writer, cycle);
    }
    ok = ok && writer.end_array() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish(path);
}

[[nodiscard]] bool write_lane(JsonWriter &writer,
                              const LifecycleStartupAdmissionLane &lane) {
    return writer.begin_object() && field(writer, "id", lane.id) &&
           field(writer, "throttle_01", lane.throttle_01) &&
           field(writer, "floor_running_gain_linear", lane.floor_running_gain_linear) &&
           writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_admission_evidence(const LifecycleStartupAdmissionFloorEvidence &evidence) {
    JsonWriter writer;
    bool ok =
        writer.begin_object() && field(writer, "schema", evidence.schema) &&
        field(writer, "candidate_status", evidence.candidate_status) &&
        field(writer, "atlas_manifest", evidence.atlas_manifest) &&
        digest_field(writer, "atlas_manifest_sha256", evidence.atlas_manifest_sha256) &&
        field(writer, "atlas_load_coordinate", evidence.atlas_load_coordinate) &&
        field(writer, "running_floor_rpm", evidence.running_floor_rpm) &&
        field(writer, "held_anchor_floor_rpm", evidence.held_anchor_floor_rpm) &&
        writer.key("release") && writer.begin_object() &&
        field(writer, "method", evidence.release.method) &&
        field(writer, "seconds", evidence.release.seconds) &&
        uint_field(writer, "first_positive_combustion_frame",
                   evidence.release.first_positive_combustion_frame) &&
        uint_field(writer, "running_floor_frame",
                   evidence.release.running_floor_frame) &&
        writer.key("release_cycle") &&
        write_cycle(writer, evidence.release.release_cycle) && writer.end_object() &&
        writer.key("lanes") && writer.begin_array();
    for (const auto &lane : evidence.lanes) {
        ok = ok && write_lane(writer, lane);
    }
    ok = ok && writer.end_array() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish("lifecycle/evidence/startup-admission.json");
}

struct CaptureBinding {
    const LifecycleCaptureEvidence *capture = nullptr;
    LifecyclePublicationNames names;
    std::string audio_path;
    contract::Sha256Digest audio_sha256;
    std::string scenario_path;
    contract::Sha256Digest scenario_sha256;
    std::string evidence_path;
    contract::Sha256Digest evidence_sha256;
};

[[nodiscard]] bool write_artifact(JsonWriter &writer, const CaptureBinding &binding) {
    return writer.begin_object() && field(writer, "path", binding.audio_path) &&
           digest_field(writer, "sha256", binding.audio_sha256) &&
           uint_field(writer, "frame_count", binding.capture->pcm.size()) &&
           writer.end_object();
}

[[nodiscard]] bool write_checkpoint(JsonWriter &writer,
                                    const LifecycleCheckpoint &checkpoint) {
    return writer.begin_object() && field(writer, "kind", checkpoint.kind) &&
           uint_field(writer, "frame", checkpoint.frame) &&
           field(writer, "rpm", checkpoint.rpm) &&
           field(writer, "precision", checkpoint.precision) &&
           field(writer, "method", checkpoint.method) && writer.end_object();
}

[[nodiscard]] bool
write_checkpoints(JsonWriter &writer,
                  const std::vector<LifecycleCheckpoint> &checkpoints) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &checkpoint : checkpoints) {
        if (!write_checkpoint(writer, checkpoint)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_seam(JsonWriter &writer, const LifecycleSeam &seam) {
    return writer.begin_object() &&
           uint_field(writer, "source_frame", seam.source_frame) &&
           uint_field(writer, "crossfade_frames", seam.crossfade_frames) &&
           field(writer, "source_rpm", seam.source_rpm) &&
           uint_field(writer, "target_source_frame", seam.target_source_frame) &&
           field(writer, "target_rpm", seam.target_rpm) &&
           field(writer, "correlation", seam.correlation) &&
           field(writer, "target", seam.target) &&
           field(writer, "target_reference", seam.target_reference) &&
           writer.end_object();
}

[[nodiscard]] bool write_starter(JsonWriter &writer,
                                 const LifecycleStarterPresentation &starter,
                                 const CaptureBinding &binding) {
    return writer.begin_object() && writer.key("artifact") &&
           write_artifact(writer, binding) &&
           field(writer, "mean_crank_rpm", starter.mean_crank_rpm) &&
           field(writer, "reference_rpm", starter.reference_rpm) &&
           uint_field(writer, "loop_start_frame", starter.loop_start_frame) &&
           uint_field(writer, "loop_end_frame", starter.loop_end_frame) &&
           uint_field(writer, "crossfade_frames", starter.crossfade_frames) &&
           uint_field(writer, "attack_fade_frames", starter.attack_fade_frames) &&
           uint_field(writer, "release_fade_frames", starter.release_fade_frames) &&
           writer.end_object();
}

[[nodiscard]] bool write_startup(JsonWriter &writer,
                                 const LifecycleStartupPresentation &startup,
                                 const CaptureBinding &binding) {
    return writer.begin_object() && writer.key("artifact") &&
           write_artifact(writer, binding) && writer.key("checkpoints") &&
           write_checkpoints(writer, startup.checkpoints) && writer.key("entry") &&
           write_seam(writer, startup.entry) && writer.key("exit") &&
           write_seam(writer, startup.exit) && writer.end_object();
}

[[nodiscard]] bool write_shutdown(JsonWriter &writer,
                                  const LifecycleShutdownPresentation &shutdown,
                                  const CaptureBinding &binding) {
    return writer.begin_object() && writer.key("artifact") &&
           write_artifact(writer, binding) && writer.key("checkpoints") &&
           write_checkpoints(writer, shutdown.checkpoints) && writer.key("entry") &&
           write_seam(writer, shutdown.entry) &&
           uint_field(writer, "silence_frame", shutdown.silence_frame) &&
           uint_field(writer, "exit_fade_frames", shutdown.exit_fade_frames) &&
           uint_field(writer, "quiet_tail_frames", shutdown.quiet_tail_frames) &&
           field(writer, "quiet_peak_threshold", shutdown.quiet_peak_threshold) &&
           field(writer, "quiet_rms_threshold", shutdown.quiet_rms_threshold) &&
           writer.end_object();
}

[[nodiscard]] bool
write_startup_admission(JsonWriter &writer,
                        const LifecycleStartupAdmissionPresentation &admission) {
    bool ok = writer.begin_object() && field(writer, "schema", admission.schema) &&
              field(writer, "running_bed_load_coordinate",
                    admission.running_bed_load_coordinate) &&
              field(writer, "admission_lane_coordinate",
                    admission.admission_lane_coordinate) &&
              field(writer, "blend", admission.blend) &&
              field(writer, "pre_floor_progress", admission.pre_floor_progress) &&
              field(writer, "completion_progress", admission.completion_progress) &&
              field(writer, "completion_crank_travel_revolutions",
                    admission.completion_crank_travel_revolutions) &&
              bool_field(writer, "monotone_ownership", admission.monotone_ownership) &&
              writer.key("lanes") && writer.begin_array();
    for (const auto &lane : admission.lanes) {
        ok = ok && write_lane(writer, lane);
    }
    ok = ok && writer.end_array() && writer.key("coast_stability") &&
         writer.begin_object() &&
         field(writer, "lane_id", admission.coast_stability.lane_id) &&
         bool_field(writer, "requires_starter_released",
                    admission.coast_stability.requires_starter_released) &&
         field(writer, "post_peak_crank_travel_revolutions",
               admission.coast_stability.post_peak_crank_travel_revolutions) &&
         field(writer, "clock_law", admission.coast_stability.clock_law) &&
         writer.end_object() && writer.key("evidence") && writer.begin_object() &&
         field(writer, "method", admission.evidence.method) &&
         field(writer, "path", admission.evidence.path) &&
         digest_field(writer, "sha256", admission.evidence.sha256) &&
         digest_field(writer, "corrected_held_manifest_sha256",
                      admission.evidence.corrected_held_manifest_sha256) &&
         writer.end_object() && writer.end_object();
    return ok;
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_lifecycle_runtime(const LifecycleCookedPackage &lifecycle,
                         const ResponsivePackageProvenanceV1 &provenance,
                         const LifecycleStartupAdmissionPresentation &admission,
                         const std::span<const CaptureBinding> bindings) {
    JsonWriter writer;
    bool ok = writer.begin_object() && field(writer, "schema", lifecycle.schema) &&
              field(writer, "id", lifecycle.id) &&
              field(writer, "engine", lifecycle.engine_id) && writer.key("audio") &&
              writer.begin_object() &&
              uint_field(writer, "sample_rate_hz", lifecycle.sample_rate_hz) &&
              field(writer, "encoding", lifecycle.encoding) &&
              field(writer, "channel_layout", lifecycle.channel_layout) &&
              field(writer, "bus_id", lifecycle.bus_id) && writer.end_object() &&
              writer.key("starter") &&
              write_starter(writer, lifecycle.starter, bindings[0U]) &&
              writer.key("startup") &&
              write_startup(writer, lifecycle.startup, bindings[1U]) &&
              writer.key("shutdown") &&
              write_shutdown(writer, lifecycle.shutdown, bindings[2U]);
    if (lifecycle.shutdown_elevated.has_value()) {
        ok = ok && writer.key("shutdown_elevated") &&
             write_shutdown(writer, *lifecycle.shutdown_elevated, bindings[3U]);
    }
    ok = ok && writer.key("startup_admission") &&
         write_startup_admission(writer, admission) && writer.key("provenance") &&
         writer.begin_object() && writer.key("engine") && writer.begin_object() &&
         field(writer, "id", provenance.engine_id) &&
         digest_field(writer, "sha256", provenance.compiled_engine_sha256) &&
         writer.end_object() && writer.key("renderer_build") && writer.begin_object() &&
         field(writer, "id", provenance.renderer_build_id) &&
         digest_field(writer, "sha256", provenance.renderer_source_sha256) &&
         writer.end_object() &&
         field(writer, "representation", lifecycle.representation) &&
         uint_field(writer, "physics_rate_hz", kLifecyclePhysicsRateHz) &&
         uint_field(writer, "delivery_rate_hz", lifecycle.sample_rate_hz) &&
         field(writer, "fidelity_alignment", lifecycle.fidelity_alignment) &&
         field(writer, "checkpoint_alignment", lifecycle.checkpoint_alignment) &&
         field(writer, "seam_reference_scope", lifecycle.seam_reference_scope) &&
         writer.key("scenarios") && writer.begin_array();
    for (const auto &binding : bindings) {
        ok = ok && writer.begin_object() &&
             field(writer, "role", binding.names.provenance_role) &&
             field(writer, "canonical_source_id",
                   binding.capture->canonical_source_id) &&
             field(writer, "derived_id", binding.capture->scenario_id) &&
             field(writer, "path", binding.scenario_path) &&
             digest_field(writer, "sha256", binding.scenario_sha256) &&
             field(writer, "evidence_path", binding.evidence_path) &&
             digest_field(writer, "evidence_sha256", binding.evidence_sha256) &&
             writer.end_object();
    }
    ok = ok && writer.end_array() && writer.end_object() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish(kResponsiveLifecyclePackagePathV1);
}

[[nodiscard]] bool valid_rate(const authoring::RationalRate &rate,
                              const std::uint64_t expected) {
    return rate.denominator != 0U &&
           rate.denominator <= std::numeric_limits<std::uint64_t>::max() / expected &&
           rate.unit == "Hz" && rate.numerator == expected * rate.denominator;
}

[[nodiscard]] std::optional<Error>
validate_scenario(const LifecycleCaptureEvidence &capture,
                  const std::string_view path) {
    const auto &scenario = capture.scenario;
    const auto *mode = std::get_if<authoring::FreeEngineMode>(&scenario.mode);
    if (scenario.schema != "engine-sim-offline/scenario" ||
        scenario.id.value != capture.scenario_id ||
        scenario.engine.value != capture.engine_id || scenario.fuel.value.empty() ||
        mode == nullptr || mode->attached_inertia.has_value() ||
        mode->throttle_01.points.empty() || scenario.output.buses.size() != 2U ||
        scenario.output.buses[0U].value != "master-engine-raw" ||
        scenario.output.buses[1U].value != "master-engine-audition" ||
        !scenario.output.telemetry_channels.empty() ||
        !valid_rate(scenario.rates.physics, kLifecyclePhysicsRateHz) ||
        !valid_rate(scenario.rates.capture, kLifecyclePhysicsRateHz) ||
        !valid_rate(scenario.rates.source_processing, kLifecycleDeliveryRateHz) ||
        !valid_rate(scenario.rates.acoustics, kLifecycleDeliveryRateHz) ||
        !valid_rate(scenario.rates.delivery, kLifecycleDeliveryRateHz) ||
        scenario.quality.process_block_capacity_frames != kLifecycleFramesPerBlock ||
        scenario.quality.event_queue_capacity != 64U ||
        scenario.quality.telemetry_capacity_frames != 1U ||
        scenario.quality.id != "listening" || scenario.total_duration.unit != "s" ||
        scenario.audible_start.unit != "s" || scenario.audible_duration.unit != "s" ||
        !finite({scenario.total_duration.value, scenario.audible_start.value,
                 scenario.audible_duration.value}) ||
        scenario.total_duration.value <= 0.0 || scenario.audible_start.value < 0.0 ||
        scenario.audible_duration.value <= 0.0 ||
        scenario.total_duration.value >
            static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 50.0 ||
        scenario.audible_start.value >
            static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 50.0 ||
        std::abs(scenario.audible_start.value + scenario.audible_duration.value -
                 scenario.total_duration.value) > 1.0e-12) {
        return error(ErrorCode::invalid_argument,
                     "responsive-lifecycle-scenario-invalid", std::string{path},
                     "lifecycle scenario differs from the frozen finite native "
                     "publication shape");
    }
    const auto expected_total_blocks =
        std::llround(scenario.total_duration.value * 50.0);
    const auto expected_preparation_blocks =
        std::llround(scenario.audible_start.value * 50.0);
    if (expected_total_blocks < 0 || expected_preparation_blocks < 0 ||
        static_cast<std::uint64_t>(expected_total_blocks) !=
            capture.total_block_count ||
        static_cast<std::uint64_t>(expected_preparation_blocks) !=
            capture.preparation_block_count ||
        std::abs(scenario.total_duration.value * 50.0 -
                 static_cast<double>(expected_total_blocks)) > 1.0e-9 ||
        std::abs(scenario.audible_start.value * 50.0 -
                 static_cast<double>(expected_preparation_blocks)) > 1.0e-9) {
        return error(ErrorCode::invalid_argument,
                     "responsive-lifecycle-scenario-horizon-mismatch",
                     std::string{path},
                     "lifecycle scenario durations do not bind the capture block "
                     "horizons");
    }
    for (const auto &event : scenario.events) {
        const auto *patch = std::get_if<authoring::OperatingStatePatch>(&event.payload);
        if (event.id.value.empty() || event.time.unit != "s" ||
            !std::isfinite(event.time.value) || event.time.value < 0.0 ||
            patch == nullptr ||
            (!patch->ignition_enabled && !patch->fuel_enabled &&
             !patch->starter_enabled && !patch->dyno_enabled &&
             !patch->limiter_enabled)) {
            return error(ErrorCode::invalid_argument,
                         "responsive-lifecycle-scenario-event-invalid",
                         std::string{path},
                         "lifecycle scenario contains an invalid event");
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error> validate_capture(
    const LifecycleCaptureEvidence &capture, const LifecycleScenarioRole expected_role,
    const LifecycleCookedPackage &lifecycle,
    const ResponsivePackageProvenanceV1 &provenance, const std::stop_token stop_token) {
    const auto path =
        "lifecycle/captures/" +
        std::string{lifecycle_publication_names(expected_role).capture_role};
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (capture.preparation_block_count > capture.total_block_count ||
        capture.total_block_count == 0U ||
        capture.total_block_count >
            std::numeric_limits<std::uint64_t>::max() / kLifecycleFramesPerBlock ||
        capture.total_block_count > std::numeric_limits<std::uint64_t>::max() /
                                        kLifecyclePhysicsFramesPerBlock) {
        return error(ErrorCode::invalid_member,
                     "responsive-lifecycle-capture-horizon-invalid", path,
                     "lifecycle capture block horizons overflow or are reversed");
    }
    const auto audible_blocks =
        capture.total_block_count - capture.preparation_block_count;
    const auto expected_frames = audible_blocks * kLifecycleFramesPerBlock;
    if (capture.role != expected_role || capture.engine_id != lifecycle.engine_id ||
        capture.bus_id != lifecycle.bus_id ||
        !contract::is_valid_semantic_id(capture.canonical_source_id) ||
        !contract::is_valid_semantic_id(capture.scenario_id) ||
        capture.physics_rate_hz != kLifecyclePhysicsRateHz ||
        capture.delivery_rate_hz != kLifecycleDeliveryRateHz ||
        expected_frames != capture.audible_frame_count ||
        capture.pcm.size() != capture.audible_frame_count || capture.pcm.size() < 2U ||
        capture.points.size() != audible_blocks ||
        capture.audible_first_delivery_frame !=
            capture.preparation_block_count * kLifecycleFramesPerBlock ||
        capture.final_delivery_frame !=
            capture.total_block_count * kLifecycleFramesPerBlock ||
        capture.final_physics_frame !=
            capture.total_block_count * kLifecyclePhysicsFramesPerBlock ||
        capture.scenario_spec_sha256.is_zero() ||
        !contract::validate(capture.engine_provenance).ok() ||
        !contract::validate(capture.scenario_provenance).ok() ||
        capture.engine_provenance.bundle.sha256 != provenance.compiled_engine_sha256) {
        return error(ErrorCode::invalid_member, "responsive-lifecycle-capture-invalid",
                     path,
                     "lifecycle capture identity, topology, horizons or compiler "
                     "provenance is invalid");
    }
    for (std::size_t index = 0U; index < capture.pcm.size(); ++index) {
        if ((index & 4095U) == 0U && stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (!std::isfinite(capture.pcm[index])) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-pcm-nonfinite", path + "/audio",
                         "lifecycle PCM contains a non-finite sample");
        }
    }
    for (std::size_t index = 0U; index < capture.points.size(); ++index) {
        const auto &point = capture.points[index];
        const auto expected_source_frame =
            (static_cast<std::uint64_t>(index) + 1U) * kLifecycleFramesPerBlock - 1U;
        const auto expected_physics_step =
            (capture.preparation_block_count + index + 1U) *
            kLifecyclePhysicsFramesPerBlock;
        const auto availability =
            static_cast<std::uint32_t>(point.indicated_gas_availability);
        if (point.source_frame != expected_source_frame ||
            point.physics_step_end != expected_physics_step ||
            point.simulation_seconds !=
                static_cast<double>(point.physics_step_end) /
                    static_cast<double>(kLifecyclePhysicsRateHz) ||
            !finite(
                {point.simulation_seconds, point.rpm, point.indicated_gas_torque_nm}) ||
            availability > 1U) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-endpoint-invalid",
                         path + "/points/" + std::to_string(index),
                         "lifecycle endpoint cadence or data is invalid");
        }
    }
    std::uint64_t prior_end = 0U;
    for (std::size_t index = 0U; index < capture.completed_cycles.size(); ++index) {
        const auto &cycle = capture.completed_cycles[index];
        if (cycle.start_frame >= cycle.end_frame ||
            cycle.end_frame > capture.pcm.size() || !std::isfinite(cycle.mean_rpm) ||
            cycle.mean_rpm < 0.0 || (index != 0U && cycle.end_frame <= prior_end)) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-cycle-invalid",
                         path + "/completed_cycles/" + std::to_string(index),
                         "lifecycle completed-cycle evidence is invalid");
        }
        prior_end = cycle.end_frame;
    }
    return validate_scenario(capture, path + "/scenario");
}

[[nodiscard]] bool valid_seam(const LifecycleSeam &seam,
                              const std::uint64_t source_frames,
                              const std::uint64_t target_frames) {
    return seam.crossfade_frames > 0U && seam.source_frame < source_frames &&
           seam.crossfade_frames <= source_frames - seam.source_frame &&
           seam.target_source_frame < target_frames &&
           seam.crossfade_frames <= target_frames - seam.target_source_frame &&
           finite({seam.source_rpm, seam.target_rpm, seam.correlation}) &&
           seam.source_rpm >= 0.0 && seam.target_rpm >= 0.0 &&
           seam.correlation >= -1.0 && seam.correlation <= 1.0 &&
           !seam.target.empty() && !seam.target_reference.empty();
}

[[nodiscard]] bool
valid_checkpoints(const std::vector<LifecycleCheckpoint> &checkpoints,
                  const std::span<const std::string_view> kinds,
                  const std::uint64_t frame_count) {
    if (checkpoints.size() != kinds.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < checkpoints.size(); ++index) {
        const auto &point = checkpoints[index];
        if (point.kind != kinds[index] || point.frame >= frame_count ||
            !std::isfinite(point.rpm) || point.rpm < 0.0 || point.precision.empty() ||
            point.method.empty() ||
            (index != 0U && point.frame < checkpoints[index - 1U].frame)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
valid_shutdown(const LifecycleShutdownPresentation &shutdown,
               const std::span<const std::string_view> expected_kinds,
               const std::uint64_t frame_count) {
    return valid_checkpoints(shutdown.checkpoints, expected_kinds, frame_count) &&
           valid_seam(shutdown.entry, frame_count, frame_count) &&
           shutdown.entry.source_frame == shutdown.checkpoints.front().frame &&
           shutdown.entry.source_frame + shutdown.entry.crossfade_frames <=
               shutdown.checkpoints[1U].frame &&
           shutdown.checkpoints[1U].frame <= shutdown.checkpoints[2U].frame &&
           shutdown.checkpoints[2U].frame <= shutdown.silence_frame &&
           shutdown.silence_frame < frame_count &&
           shutdown.quiet_tail_frames == frame_count - shutdown.silence_frame &&
           shutdown.quiet_tail_frames >= kLifecycleQuietTailFrames &&
           shutdown.exit_fade_frames > 0U &&
           finite({shutdown.quiet_peak_threshold, shutdown.quiet_rms_threshold}) &&
           shutdown.quiet_peak_threshold >= 0.0 && shutdown.quiet_rms_threshold >= 0.0;
}

[[nodiscard]] std::optional<Error>
validate_lifecycle(const LifecycleCookedPackage &lifecycle,
                   const ResponsivePackageProvenanceV1 &provenance,
                   const contract::Sha256Digest &held_manifest_sha256,
                   const std::stop_token stop_token) {
    const auto expected_capture_count =
        lifecycle.shutdown_elevated.has_value() ? 4U : 3U;
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (lifecycle.schema != kResponsiveLifecycleSchema ||
        !contract::is_valid_semantic_id(lifecycle.engine_id) ||
        lifecycle.id != lifecycle.engine_id + "-lifecycle-preview" ||
        lifecycle.engine_id != provenance.engine_id ||
        lifecycle.sample_rate_hz != kLifecycleDeliveryRateHz ||
        lifecycle.encoding != "float32le" || lifecycle.channel_layout != "mono" ||
        lifecycle.bus_id != "master.engine.audition" ||
        provenance.compiled_engine_sha256.is_zero() ||
        provenance.renderer_source_sha256.is_zero() ||
        provenance.renderer_build_id != "engine-sim-offline-renderer-build" ||
        held_manifest_sha256.is_zero() ||
        lifecycle.captures.size() != expected_capture_count ||
        lifecycle.shutdown_elevated.has_value() != (lifecycle.captures.size() == 4U) ||
        lifecycle.representation.empty() || lifecycle.fidelity_alignment.empty() ||
        lifecycle.checkpoint_alignment.empty() ||
        lifecycle.seam_reference_scope.empty()) {
        return error(ErrorCode::invalid_argument,
                     "responsive-lifecycle-package-invalid",
                     std::string{kResponsiveLifecyclePackagePathV1},
                     "lifecycle aggregate identity, format or canonical role set is "
                     "invalid");
    }
    const auto &admission = lifecycle.startup_admission_seed;
    if (admission.candidate_status.empty() ||
        !finite({admission.running_floor_rpm, admission.held_anchor_floor_rpm,
                 admission.release.seconds,
                 admission.release.release_cycle.mean_rpm}) ||
        admission.running_floor_rpm <= 0.0 || admission.held_anchor_floor_rpm <= 0.0 ||
        admission.release.physics_tick == 0U || admission.release.seconds <= 0.0 ||
        admission.release.seconds !=
            static_cast<double>(admission.release.physics_tick) /
                static_cast<double>(kLifecyclePhysicsRateHz) ||
        admission.release.release_cycle.start_frame >=
            admission.release.release_cycle.end_frame ||
        admission.lanes.size() < 2U) {
        return error(ErrorCode::invalid_member,
                     "responsive-lifecycle-admission-seed-invalid",
                     "lifecycle/startup_admission",
                     "startup admission seed or dynamic release is invalid");
    }
    std::set<std::string_view> admission_lane_ids;
    for (std::size_t index = 0U; index < admission.lanes.size(); ++index) {
        const auto &lane = admission.lanes[index];
        if (!contract::is_valid_semantic_id(lane.id) ||
            !admission_lane_ids.insert(lane.id).second ||
            !finite({lane.throttle_01, lane.floor_running_gain_linear}) ||
            lane.throttle_01 < 0.0 || lane.throttle_01 > 1.0 ||
            lane.floor_running_gain_linear < 0.0 ||
            lane.floor_running_gain_linear > 1.0 ||
            (index != 0U &&
             lane.throttle_01 <= admission.lanes[index - 1U].throttle_01)) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-admission-lane-invalid",
                         "lifecycle/startup_admission/lanes/" + std::to_string(index),
                         "startup admission lanes must be unique, finite and "
                         "strictly ordered");
        }
    }
    if (admission.lanes.front().throttle_01 != 0.0 ||
        admission.lanes.back().throttle_01 != 1.0 ||
        !admission_lane_ids.contains("coast")) {
        return error(ErrorCode::topology_mismatch,
                     "responsive-lifecycle-admission-domain-invalid",
                     "lifecycle/startup_admission/lanes",
                     "startup admission lanes must span closed throttle through WOT "
                     "and include coast");
    }
    constexpr std::array<LifecycleScenarioRole, 4U> roles{
        LifecycleScenarioRole::starter, LifecycleScenarioRole::startup,
        LifecycleScenarioRole::shutdown, LifecycleScenarioRole::shutdown_elevated};
    for (std::size_t index = 0U; index < lifecycle.captures.size(); ++index) {
        if (const auto failure =
                validate_capture(lifecycle.captures[index], roles[index], lifecycle,
                                 provenance, stop_token)) {
            return failure;
        }
    }
    const auto starter_frames = lifecycle.captures[0U].pcm.size();
    if (!finite({lifecycle.starter.mean_crank_rpm, lifecycle.starter.reference_rpm}) ||
        lifecycle.starter.mean_crank_rpm <= 0.0 ||
        lifecycle.starter.reference_rpm <= 0.0 ||
        lifecycle.starter.loop_start_frame >= lifecycle.starter.loop_end_frame ||
        lifecycle.starter.loop_end_frame > starter_frames ||
        lifecycle.starter.crossfade_frames == 0U ||
        static_cast<std::uint64_t>(lifecycle.starter.crossfade_frames) * 2U >=
            lifecycle.starter.loop_end_frame - lifecycle.starter.loop_start_frame ||
        lifecycle.starter.attack_fade_frames == 0U ||
        lifecycle.starter.release_fade_frames == 0U) {
        return error(ErrorCode::invalid_member, "responsive-lifecycle-starter-invalid",
                     "lifecycle/starter",
                     "lifecycle starter loop or presentation values are invalid");
    }
    constexpr std::array<std::string_view, 4U> startup_kinds{
        "ignition-on", "first-combustion", "starter-release", "running-floor"};
    const auto startup_frames = lifecycle.captures[1U].pcm.size();
    if (!valid_checkpoints(lifecycle.startup.checkpoints, startup_kinds,
                           startup_frames) ||
        !valid_seam(lifecycle.startup.entry, startup_frames, starter_frames) ||
        !valid_seam(lifecycle.startup.exit, startup_frames, startup_frames) ||
        lifecycle.startup.entry.source_frame +
                lifecycle.startup.entry.crossfade_frames >
            lifecycle.startup.checkpoints[0U].frame ||
        lifecycle.startup.checkpoints.back().frame !=
            lifecycle.startup.exit.source_frame) {
        return error(ErrorCode::invalid_member, "responsive-lifecycle-startup-invalid",
                     "lifecycle/startup",
                     "lifecycle startup checkpoints or seams are invalid");
    }
    constexpr std::array<std::string_view, 3U> shutdown_kinds{
        "settled-idle", "ignition-off", "engine-stopped"};
    if (!valid_shutdown(lifecycle.shutdown, shutdown_kinds,
                        lifecycle.captures[2U].pcm.size())) {
        return error(ErrorCode::invalid_member, "responsive-lifecycle-shutdown-invalid",
                     "lifecycle/shutdown",
                     "lifecycle shutdown checkpoints, seam or quiet tail is invalid");
    }
    if (lifecycle.shutdown_elevated.has_value()) {
        constexpr std::array<std::string_view, 3U> elevated_kinds{
            "settled-running", "ignition-off", "engine-stopped"};
        if (!valid_shutdown(*lifecycle.shutdown_elevated, elevated_kinds,
                            lifecycle.captures[3U].pcm.size())) {
            return error(
                ErrorCode::invalid_member,
                "responsive-lifecycle-elevated-shutdown-invalid",
                "lifecycle/shutdown_elevated",
                "elevated shutdown checkpoints, seam or quiet tail is invalid");
        }
    }
    return std::nullopt;
}

[[nodiscard]] Error lifecycle_binding_error(const LifecycleError &failure,
                                            const std::string_view path) {
    const auto code = failure.code == LifecycleErrorCode::cancelled
                          ? ErrorCode::cancelled
                          : ErrorCode::invalid_member;
    return error(code, "responsive-lifecycle-admission-binding-failed",
                 std::string{path} + (failure.path.empty() ? "" : "/" + failure.path),
                 failure.detail_code + ": " + failure.message);
}

[[nodiscard]] std::string_view bytes_view(const std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool exact_keys(const authoring::JsonValue value,
                              const std::initializer_list<std::string_view> keys) {
    if (value.kind() != authoring::JsonKind::object || value.size() != keys.size()) {
        return false;
    }
    return std::ranges::all_of(keys,
                               [&](const auto key) { return value.find(key).valid(); });
}

[[nodiscard]] bool json_string(const authoring::JsonValue value,
                               const std::string_view expected) {
    const auto parsed = value.string();
    return parsed.has_value() && *parsed == expected;
}

[[nodiscard]] std::optional<std::uint64_t> json_uint(const authoring::JsonValue value) {
    const auto number = value.number();
    if (!number.has_value() || *number < 0.0 ||
        *number > static_cast<double>((UINT64_C(1) << 53U) - 1U) ||
        std::floor(*number) != *number) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*number);
}

[[nodiscard]] std::optional<double> json_finite(const authoring::JsonValue value) {
    const auto number = value.number();
    if (!number.has_value() || !std::isfinite(*number)) {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] bool json_bool(const authoring::JsonValue value, const bool expected) {
    const auto parsed = value.boolean();
    return parsed.has_value() && *parsed == expected;
}

[[nodiscard]] bool lowercase_hex(const std::string_view value,
                                 const std::size_t count) {
    return value.size() == count &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool nearly_equal(const double left, const double right) {
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 1.0e-12 * scale;
}

[[nodiscard]] std::optional<Error>
validate_shared_starter_manifest(const authoring::JsonValue root,
                                 const std::span<const std::byte> audio,
                                 contract::Sha256Digest &payload_sha256) {
    if (!exact_keys(root, {"schema", "id", "purpose", "rights", "audio", "markers",
                           "mix", "provenance"}) ||
        !json_string(root.find("schema"),
                     "engine-sim-offline/shared-recorded-starter") ||
        !json_string(root.find("id"), "shared-recorded-starter-licensed") ||
        !json_string(root.find("purpose"), "shared-source-a-and-b-lifecycle-layer")) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-root-invalid",
                     std::string{kResponsiveSharedRecordedStarterPackagePathV1},
                     "shared starter root schema, identity or purpose is invalid");
    }
    const auto rights = root.find("rights");
    if (!exact_keys(rights, {"status", "basis", "licensee", "audition_only",
                             "modification_authorized", "redistribution_authorized",
                             "attested_by", "attestation_date", "notice"}) ||
        !json_string(rights.find("status"), "licensed") ||
        !json_string(rights.find("basis"), "commissioned-original-recording") ||
        !json_string(rights.find("licensee"), "SvetlozarValchev") ||
        !json_bool(rights.find("audition_only"), false) ||
        !json_bool(rights.find("modification_authorized"), true) ||
        !json_bool(rights.find("redistribution_authorized"), true) ||
        !json_string(rights.find("attested_by"), "SvetlozarValchev") ||
        !json_string(rights.find("attestation_date"), "2026-08-06") ||
        !json_string(rights.find("notice"),
                     "Commissioned original recording licensed to SvetlozarValchev; "
                     "modification and redistribution are authorized.")) {
        return error(ErrorCode::invalid_identity,
                     "responsive-shared-starter-rights-invalid",
                     "shared-recorded-starter/runtime.json/rights",
                     "shared starter rights attestation does not match the licensed "
                     "installed asset");
    }
    const auto audio_object = root.find("audio");
    if (!exact_keys(audio_object,
                    {"relative_path", "sample_rate_hz", "encoding", "channel_layout",
                     "frame_count", "byte_count", "payload_sha256", "duration_seconds",
                     "peak", "rms"}) ||
        !json_string(audio_object.find("relative_path"),
                     kSharedStarterAudioRelativePath) ||
        json_uint(audio_object.find("sample_rate_hz")) != 192'000U ||
        !json_string(audio_object.find("encoding"), "float32le") ||
        !json_string(audio_object.find("channel_layout"), "mono")) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-audio-format-invalid",
                     "shared-recorded-starter/runtime.json/audio",
                     "shared starter audio format or path is invalid");
    }
    const auto frame_count = json_uint(audio_object.find("frame_count"));
    const auto byte_count = json_uint(audio_object.find("byte_count"));
    const auto payload_hex = audio_object.find("payload_sha256").string();
    const auto duration = json_finite(audio_object.find("duration_seconds"));
    const auto expected_peak = json_finite(audio_object.find("peak"));
    const auto expected_rms = json_finite(audio_object.find("rms"));
    payload_sha256 = contract::sha256(audio);
    if (!frame_count.has_value() || !byte_count.has_value() ||
        !payload_hex.has_value() || !lowercase_hex(*payload_hex, 64U) ||
        !duration.has_value() || !expected_peak.has_value() ||
        !expected_rms.has_value() || *frame_count < 2U ||
        *byte_count != *frame_count * sizeof(float) || audio.size() != *byte_count ||
        *payload_hex != digest_hex(payload_sha256) ||
        !nearly_equal(*duration, static_cast<double>(*frame_count) / 192'000.0)) {
        return error(ErrorCode::invalid_member,
                     "responsive-shared-starter-audio-binding-invalid",
                     "shared-recorded-starter/audio",
                     "shared starter PCM size, duration or digest is invalid");
    }
    double peak = 0.0;
    double energy = 0.0;
    for (std::size_t offset = 0U; offset < audio.size(); offset += sizeof(float)) {
        std::uint32_t bits = 0U;
        for (std::uint32_t byte = 0U; byte < sizeof(float); ++byte) {
            bits |= static_cast<std::uint32_t>(
                        std::to_integer<std::uint8_t>(audio[offset + byte]))
                    << (byte * 8U);
        }
        const auto sample = std::bit_cast<float>(bits);
        if (!std::isfinite(sample)) {
            return error(ErrorCode::invalid_member,
                         "responsive-shared-starter-pcm-nonfinite",
                         "shared-recorded-starter/audio",
                         "shared starter PCM contains a non-finite sample");
        }
        const auto value = static_cast<double>(sample);
        peak = std::max(peak, std::abs(value));
        energy += value * value;
    }
    const auto rms = std::sqrt(energy / static_cast<double>(*frame_count));
    if (!nearly_equal(peak, *expected_peak) || !nearly_equal(rms, *expected_rms)) {
        return error(ErrorCode::invalid_member,
                     "responsive-shared-starter-audio-statistics-invalid",
                     "shared-recorded-starter/runtime.json/audio",
                     "shared starter declared peak or RMS differs from its PCM");
    }
    const auto markers = root.find("markers");
    const auto repeat_in = json_uint(markers.find("repeat_in_frame"));
    const auto repeat_out = json_uint(markers.find("repeat_out_frame"));
    const auto cut = json_uint(markers.find("cut_frame"));
    const auto crossfade = json_uint(markers.find("seam_crossfade_frames"));
    if (!exact_keys(markers, {"repeat_in_frame", "repeat_out_frame", "cut_frame",
                              "seam_crossfade_frames"}) ||
        !repeat_in.has_value() || !repeat_out.has_value() || !cut.has_value() ||
        !crossfade.has_value() || *repeat_in >= *repeat_out || *repeat_out > *cut ||
        *cut != *frame_count || *crossfade == 0U ||
        *crossfade > (*repeat_out - *repeat_in) / 2U) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-markers-invalid",
                     "shared-recorded-starter/runtime.json/markers",
                     "shared starter repeat/cut markers are invalid");
    }
    const auto mix = root.find("mix");
    if (!exact_keys(mix,
                    {"default_enabled", "source_gain", "speed_up_start_rpm",
                     "speed_up_end_rpm", "base_playback_rate", "catch_playback_rate",
                     "speed_up_curve", "rpm_smoothing_milliseconds",
                     "attack_milliseconds", "pre_catch_engine_gain", "catch_rpm",
                     "ignition_duck_lead_milliseconds", "catch_starter_gain",
                     "engine_catch_gain", "handoff_milliseconds",
                     "catch_offset_milliseconds"})) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-mix-shape-invalid",
                     "shared-recorded-starter/runtime.json/mix",
                     "shared starter mix contract has unexpected fields");
    }
    const std::array<authoring::JsonValue, 14U> finite_mix{
        mix.find("source_gain"),
        mix.find("speed_up_start_rpm"),
        mix.find("speed_up_end_rpm"),
        mix.find("base_playback_rate"),
        mix.find("catch_playback_rate"),
        mix.find("speed_up_curve"),
        mix.find("rpm_smoothing_milliseconds"),
        mix.find("attack_milliseconds"),
        mix.find("pre_catch_engine_gain"),
        mix.find("catch_rpm"),
        mix.find("ignition_duck_lead_milliseconds"),
        mix.find("catch_starter_gain"),
        mix.find("engine_catch_gain"),
        mix.find("handoff_milliseconds")};
    if (!json_bool(mix.find("default_enabled"), true) ||
        std::ranges::any_of(finite_mix,
                            [](const auto value) {
                                const auto parsed = json_finite(value);
                                return !parsed.has_value() || *parsed < 0.0;
                            }) ||
        !json_finite(mix.find("catch_offset_milliseconds")).has_value() ||
        *json_finite(mix.find("speed_up_end_rpm")) <=
            *json_finite(mix.find("speed_up_start_rpm")) ||
        *json_finite(mix.find("source_gain")) > 1.0 ||
        *json_finite(mix.find("catch_starter_gain")) > 1.0) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-mix-invalid",
                     "shared-recorded-starter/runtime.json/mix",
                     "shared starter mix contains invalid controls");
    }
    const auto provenance = root.find("provenance");
    const auto source = provenance.find("source");
    const auto selection = provenance.find("selection");
    const auto canonicalization = provenance.find("canonicalization");
    const auto accepted_commit =
        provenance.find("accepted_engine_audio_lab_layer_commit").string();
    if (!exact_keys(provenance, {"accepted_engine_audio_lab_layer_commit", "source",
                                 "selection", "canonicalization"}) ||
        !accepted_commit.has_value() || !lowercase_hex(*accepted_commit, 40U) ||
        !exact_keys(source, {"origin", "asset_file", "sha256", "byte_count", "codec",
                             "sample_rate_hz", "channels", "channel_relationship"}) ||
        !json_string(source.find("origin"), "commissioned-original-recording") ||
        !json_string(source.find("asset_file"), "recorded-starter-source.mp3") ||
        !json_string(source.find("sha256"), kSharedStarterSourceSha256) ||
        !json_uint(source.find("byte_count")).has_value() ||
        !json_string(source.find("codec"), "mp3") ||
        json_uint(source.find("sample_rate_hz")) != 44'100U ||
        json_uint(source.find("channels")) != 2U ||
        !json_string(source.find("channel_relationship"),
                     "dual mono; FFmpeg decode is bit-identical and Chromium decode "
                     "differs by at most one PCM16 LSB") ||
        !exact_keys(selection,
                    {"description", "accepted_date", "repeat_bed_start_frame",
                     "repeat_bed_end_frame", "seam_crossfade_frames_at_44100hz"}) ||
        !json_string(selection.find("description"),
                     "user-auditioned accepted starter and handoff settings") ||
        !json_string(selection.find("accepted_date"), "2026-07-20") ||
        !json_uint(selection.find("repeat_bed_start_frame")).has_value() ||
        !json_uint(selection.find("repeat_bed_end_frame")).has_value() ||
        !json_uint(selection.find("seam_crossfade_frames_at_44100hz")).has_value() ||
        !exact_keys(canonicalization,
                    {"method", "source_channel", "source_crop_begin_frame_inclusive",
                     "source_crop_end_frame_exclusive", "source_crop_frames",
                     "output_sample_rate_hz", "output_frames", "marker_mapping",
                     "ffmpeg_version", "filter_graph"}) ||
        !json_string(
            canonicalization.find("method"),
            "decode-left-dual-mono-then-exact-source-frame-crop-then-soxr-resample") ||
        json_uint(canonicalization.find("source_channel")) != 0U ||
        json_uint(canonicalization.find("output_sample_rate_hz")) != 192'000U ||
        json_uint(canonicalization.find("output_frames")) != *frame_count ||
        !json_string(canonicalization.find("marker_mapping"),
                     "round(relative_source_frame * 192000 / 44100)")) {
        return error(ErrorCode::invalid_identity,
                     "responsive-shared-starter-provenance-invalid",
                     "shared-recorded-starter/runtime.json/provenance",
                     "shared starter provenance does not bind the accepted licensed "
                     "source and canonicalization");
    }
    const auto crop_begin =
        json_uint(canonicalization.find("source_crop_begin_frame_inclusive"));
    const auto crop_end =
        json_uint(canonicalization.find("source_crop_end_frame_exclusive"));
    const auto crop_frames = json_uint(canonicalization.find("source_crop_frames"));
    const auto selection_begin = json_uint(selection.find("repeat_bed_start_frame"));
    const auto selection_end = json_uint(selection.find("repeat_bed_end_frame"));
    const auto selection_crossfade =
        json_uint(selection.find("seam_crossfade_frames_at_44100hz"));
    const auto ffmpeg = canonicalization.find("ffmpeg_version").string();
    const auto graph = canonicalization.find("filter_graph").string();
    if (!crop_begin || !crop_end || !crop_frames || *crop_begin >= *crop_end ||
        *crop_frames != *crop_end - *crop_begin || !selection_begin || !selection_end ||
        !selection_crossfade || *selection_begin >= *selection_end ||
        *selection_crossfade == 0U ||
        *selection_crossfade > (*selection_end - *selection_begin) / 2U || !ffmpeg ||
        ffmpeg->empty() || !graph || graph->empty()) {
        return error(ErrorCode::invalid_identity,
                     "responsive-shared-starter-provenance-range-invalid",
                     "shared-recorded-starter/runtime.json/provenance",
                     "shared starter selection or canonicalization ranges are invalid");
    }
    return std::nullopt;
}

[[nodiscard]] contract::Sha256Digest
shared_starter_tree_identity(const std::span<const std::byte> runtime,
                             const contract::Sha256Digest &runtime_sha256,
                             const std::span<const std::byte> audio,
                             const contract::Sha256Digest &audio_sha256) {
    // Exact JSON.stringify(entries) grammar from fingerprintRegularTree: sorted
    // paths, ordered path/byte_count/sha256 keys and no trailing newline.
    std::string canonical;
    canonical.reserve(320U);
    canonical.append("[{\"path\":\"");
    canonical.append(kSharedStarterAudioRelativePath);
    canonical.append("\",\"byte_count\":");
    canonical.append(std::to_string(audio.size()));
    canonical.append(",\"sha256\":\"");
    canonical.append(digest_hex(audio_sha256));
    canonical.append("\"},{\"path\":\"runtime.json\",\"byte_count\":");
    canonical.append(std::to_string(runtime.size()));
    canonical.append(",\"sha256\":\"");
    canonical.append(digest_hex(runtime_sha256));
    canonical.append("\"}]");
    return contract::sha256(std::as_bytes(std::span{canonical}));
}

} // namespace

ResponsiveOptionalChildEncodeResultV1
encode_responsive_lifecycle_child_v1(const LifecycleCookedPackage &lifecycle,
                                     const ResponsivePackageProvenanceV1 &provenance,
                                     const contract::Sha256Digest &held_manifest_sha256,
                                     const std::stop_token stop_token) {
    if (const auto failure = validate_lifecycle(lifecycle, provenance,
                                                held_manifest_sha256, stop_token)) {
        return *failure;
    }
    auto admission_evidence_result = bind_lifecycle_startup_admission_floor_evidence(
        lifecycle.startup_admission_seed,
        {"../../held/package.json", held_manifest_sha256,
         "measured-intake-manifold-pressure-pa-abs"});
    if (const auto *failure = std::get_if<LifecycleError>(&admission_evidence_result)) {
        return lifecycle_binding_error(*failure,
                                       "lifecycle/evidence/startup-admission.json");
    }
    const auto &admission_evidence =
        std::get<LifecycleStartupAdmissionFloorEvidence>(admission_evidence_result);
    auto admission_bytes_result = encode_admission_evidence(admission_evidence);
    if (const auto *failure = std::get_if<Error>(&admission_bytes_result)) {
        return *failure;
    }
    auto admission_bytes =
        std::get<std::vector<std::byte>>(std::move(admission_bytes_result));
    const auto admission_sha256 = contract::sha256(admission_bytes);
    auto admission_result = bind_lifecycle_startup_admission_presentation(
        admission_evidence, {"evidence/startup-admission.json", admission_sha256});
    if (const auto *failure = std::get_if<LifecycleError>(&admission_result)) {
        return lifecycle_binding_error(*failure, "lifecycle/startup_admission");
    }
    const auto &admission =
        std::get<LifecycleStartupAdmissionPresentation>(admission_result);

    ResponsiveOptionalChildPackageV1 child;
    child.role = ResponsiveOptionalChildRole::lifecycle;
    child.runtime_path = std::string{kResponsiveLifecyclePackagePathV1};
    child.engine_id = provenance.engine_id;
    child.compiled_engine_sha256 = provenance.compiled_engine_sha256;
    child.renderer_build_id = provenance.renderer_build_id;
    child.renderer_source_sha256 = provenance.renderer_source_sha256;
    child.members.reserve(lifecycle.captures.size() * 3U + 2U);

    std::vector<CaptureBinding> bindings;
    bindings.reserve(lifecycle.captures.size());
    for (const auto &capture : lifecycle.captures) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto names = lifecycle_publication_names(capture.role);
        const auto stem = std::string{names.capture_role};
        CaptureBinding binding;
        binding.capture = &capture;
        binding.names = names;
        binding.audio_path = "audio/" + stem + ".master-engine-audition.f32le";
        binding.scenario_path = "source/scenarios/" + stem + "-10khz.json";
        binding.evidence_path = "evidence/" + stem + ".json";

        auto audio_bytes = float32_le_bytes(capture.pcm);
        binding.audio_sha256 = contract::sha256(audio_bytes);
        child.members.push_back(
            {"lifecycle/" + binding.audio_path, std::move(audio_bytes)});

        auto scenario_result =
            encode_scenario(capture.scenario, "lifecycle/" + binding.scenario_path);
        if (const auto *failure = std::get_if<Error>(&scenario_result)) {
            return *failure;
        }
        auto scenario_bytes =
            std::get<std::vector<std::byte>>(std::move(scenario_result));
        binding.scenario_sha256 = contract::sha256(scenario_bytes);
        child.members.push_back(
            {"lifecycle/" + binding.scenario_path, std::move(scenario_bytes)});

        auto evidence_result = encode_capture_evidence(
            capture, names.capture_role, "lifecycle/" + binding.evidence_path);
        if (const auto *failure = std::get_if<Error>(&evidence_result)) {
            return *failure;
        }
        auto evidence_bytes =
            std::get<std::vector<std::byte>>(std::move(evidence_result));
        binding.evidence_sha256 = contract::sha256(evidence_bytes);
        child.members.push_back(
            {"lifecycle/" + binding.evidence_path, std::move(evidence_bytes)});
        bindings.push_back(std::move(binding));
    }
    child.members.push_back(
        {"lifecycle/evidence/startup-admission.json", std::move(admission_bytes)});
    auto runtime_result =
        encode_lifecycle_runtime(lifecycle, provenance, admission, bindings);
    if (const auto *failure = std::get_if<Error>(&runtime_result)) {
        return *failure;
    }
    child.members.push_back(
        {std::string{kResponsiveLifecyclePackagePathV1},
         std::get<std::vector<std::byte>>(std::move(runtime_result))});
    std::ranges::sort(child.members, {}, &PortableResponsivePackageMember::path);
    return child;
}

ResponsiveSharedRecordedStarterEncodeResultV1
encode_responsive_shared_recorded_starter_v1(
    const ResponsiveSharedRecordedStarterBytesV1 &input,
    const ResponsivePackageProvenanceV1 &package_provenance,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (input.runtime_json.empty() || input.audio_payload.empty() ||
        input.runtime_json.size() > kMaximumJsonBytes ||
        input.audio_payload.size() >
            kNativeResponsiveMaximumPackagePayloadBytes - input.runtime_json.size() ||
        !contract::is_valid_semantic_id(package_provenance.engine_id) ||
        package_provenance.compiled_engine_sha256.is_zero() ||
        package_provenance.renderer_build_id != "engine-sim-offline-renderer-build" ||
        package_provenance.renderer_source_sha256.is_zero()) {
        return error(ErrorCode::invalid_argument,
                     "responsive-shared-starter-input-invalid",
                     std::string{kResponsiveSharedRecordedStarterPackagePathV1},
                     "shared starter bytes or package provenance are invalid");
    }
    const auto runtime_sha256 = contract::sha256(input.runtime_json);
    const auto admitted_audio_sha256 = contract::sha256(input.audio_payload);
    if (input.runtime_json.size() != kSharedStarterManifestByteCount ||
        input.audio_payload.size() != kSharedStarterPayloadByteCount ||
        digest_hex(runtime_sha256) != kSharedStarterManifestSha256 ||
        digest_hex(admitted_audio_sha256) != kSharedStarterPayloadSha256) {
        return error(ErrorCode::invalid_identity,
                     "responsive-shared-starter-release-identity-mismatch",
                     std::string{kResponsiveSharedRecordedStarterPackagePathV1},
                     "shared starter bytes differ from the exact package admitted "
                     "by the playback runtime");
    }
    authoring::JsonParseLimits limits;
    limits.maximum_input_bytes = kMaximumJsonBytes;
    limits.maximum_nodes = 262'144U;
    auto parsed = authoring::parse_json(bytes_view(input.runtime_json), limits);
    const auto *document = std::get_if<authoring::JsonDocument>(&parsed);
    if (document == nullptr) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-shared-starter-json-malformed",
                     std::string{kResponsiveSharedRecordedStarterPackagePathV1},
                     "shared starter runtime is not bounded valid JSON");
    }
    contract::Sha256Digest audio_sha256;
    if (const auto failure = validate_shared_starter_manifest(
            document->root(), input.audio_payload, audio_sha256)) {
        return *failure;
    }
    ResponsiveOptionalChildPackageV1 child;
    child.role = ResponsiveOptionalChildRole::shared_recorded_starter;
    child.runtime_path = std::string{kResponsiveSharedRecordedStarterPackagePathV1};
    child.engine_id = package_provenance.engine_id;
    child.compiled_engine_sha256 = package_provenance.compiled_engine_sha256;
    child.renderer_build_id = package_provenance.renderer_build_id;
    child.renderer_source_sha256 = package_provenance.renderer_source_sha256;
    child.members.push_back(
        {"shared-recorded-starter/" + std::string{kSharedStarterAudioRelativePath},
         std::vector<std::byte>{input.audio_payload.begin(),
                                input.audio_payload.end()}});
    child.members.push_back(
        {std::string{kResponsiveSharedRecordedStarterPackagePathV1},
         std::vector<std::byte>{input.runtime_json.begin(), input.runtime_json.end()}});
    return EncodedResponsiveSharedRecordedStarterV1{
        std::move(child),
        {shared_starter_tree_identity(input.runtime_json, runtime_sha256,
                                      input.audio_payload, audio_sha256),
         2U}};
}

} // namespace engine_sim_offline::responsive
