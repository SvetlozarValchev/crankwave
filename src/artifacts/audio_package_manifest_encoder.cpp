#include "engine_sim_offline/artifacts/audio_package_manifest_encoder.hpp"

#include <array>
#include <charconv>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

inline constexpr std::size_t kMaximumAudioPackageManifestBytes = 16U * 1024U * 1024U;
inline constexpr char kHexDigits[] = "0123456789abcdef";

class RuntimeJsonWriter final {
  public:
    [[nodiscard]] bool begin_object() {
        if (!before_value() || !append_byte('{')) {
            return false;
        }
        containers_.push_back({ContainerKind::object, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_object() {
        if (!ready() || containers_.empty() ||
            containers_.back().kind != ContainerKind::object ||
            containers_.back().awaiting_member_value) {
            return fail("object ended in an invalid writer state");
        }
        if (!append_byte('}')) {
            return false;
        }
        containers_.pop_back();
        return true;
    }

    [[nodiscard]] bool begin_array() {
        if (!before_value() || !append_byte('[')) {
            return false;
        }
        containers_.push_back({ContainerKind::array, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_array() {
        if (!ready() || containers_.empty() ||
            containers_.back().kind != ContainerKind::array) {
            return fail("array ended in an invalid writer state");
        }
        if (!append_byte(']')) {
            return false;
        }
        containers_.pop_back();
        return true;
    }

    [[nodiscard]] bool key(std::string_view value) {
        if (!ready() || containers_.empty() ||
            containers_.back().kind != ContainerKind::object ||
            containers_.back().awaiting_member_value) {
            return fail("object key written in an invalid writer state");
        }
        auto &object = containers_.back();
        if ((object.value_count != 0U && !append_byte(',')) || !append_quoted(value) ||
            !append_byte(':')) {
            return false;
        }
        object.awaiting_member_value = true;
        return true;
    }

    [[nodiscard]] bool string_value(std::string_view value) {
        return before_value() && append_quoted(value);
    }

    [[nodiscard]] bool uint32_value(std::uint32_t value) {
        return unsigned_value(value);
    }

    [[nodiscard]] bool uint64_value(std::uint64_t value) {
        return unsigned_value(value);
    }

    [[nodiscard]] bool uint64_string_value(std::uint64_t value) {
        std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1U> encoded{};
        const auto [end, error] =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (error != std::errc{}) {
            return fail("uint64 conversion failed");
        }
        return before_value() && append_byte('"') &&
               append(std::string_view{
                   encoded.data(), static_cast<std::size_t>(end - encoded.data())}) &&
               append_byte('"');
    }

    [[nodiscard]] bool double_value(double value) {
        std::array<char, 64> encoded{};
        const auto [end, error] =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value,
                          std::chars_format::general);
        if (error != std::errc{}) {
            return fail("binary64 shortest-round-trip conversion failed");
        }
        return before_value() &&
               append(std::string_view{encoded.data(),
                                       static_cast<std::size_t>(end - encoded.data())});
    }

    [[nodiscard]] bool null_value() {
        return before_value() && append("null");
    }

    [[nodiscard]] bool sha256_value(const contract::Sha256Digest &digest) {
        std::array<char, 64> encoded{};
        for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
            encoded[index * 2U] = kHexDigits[digest.bytes[index] >> 4U];
            encoded[index * 2U + 1U] = kHexDigits[digest.bytes[index] & 0x0fU];
        }
        return string_value(std::string_view{encoded.data(), encoded.size()});
    }

    [[nodiscard]] bool finish(std::vector<std::byte> &output) {
        if (!ready() || !root_written_ || !containers_.empty()) {
            return fail("JSON document is incomplete");
        }
        if (!append_byte('\n')) {
            return false;
        }
        output = std::move(bytes_);
        finished_ = true;
        return true;
    }

    [[nodiscard]] std::string_view error_message() const noexcept {
        return error_message_;
    }

  private:
    enum class ContainerKind : std::uint8_t { object, array };
    struct Container {
        ContainerKind kind;
        std::size_t value_count;
        bool awaiting_member_value;
    };

    template <class Unsigned> [[nodiscard]] bool unsigned_value(Unsigned value) {
        std::array<char, std::numeric_limits<Unsigned>::digits10 + 1U> encoded{};
        const auto [end, error] =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (error != std::errc{}) {
            return fail("unsigned integer conversion failed");
        }
        return before_value() &&
               append(std::string_view{encoded.data(),
                                       static_cast<std::size_t>(end - encoded.data())});
    }

    [[nodiscard]] bool before_value() {
        if (!ready()) {
            return false;
        }
        if (containers_.empty()) {
            if (root_written_) {
                return fail("JSON document may contain only one root value");
            }
            root_written_ = true;
            return true;
        }
        auto &container = containers_.back();
        if (container.kind == ContainerKind::object) {
            if (!container.awaiting_member_value) {
                return fail("object value requires a preceding key");
            }
            container.awaiting_member_value = false;
            ++container.value_count;
            return true;
        }
        if (container.value_count != 0U && !append_byte(',')) {
            return false;
        }
        ++container.value_count;
        return true;
    }

    [[nodiscard]] bool append_quoted(std::string_view value) {
        if (!append_byte('"')) {
            return false;
        }
        for (const unsigned char byte : value) {
            switch (byte) {
            case '"':
                if (!append("\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!append("\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!append("\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!append("\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!append("\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!append("\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!append("\\t")) {
                    return false;
                }
                break;
            default:
                if (byte <= 0x1fU) {
                    return fail("control byte is not encodable in package JSON");
                }
                if (!append_byte(static_cast<char>(byte))) {
                    return false;
                }
                break;
            }
        }
        return append_byte('"');
    }

    [[nodiscard]] bool append(std::string_view value) {
        if (!ready()) {
            return false;
        }
        if (value.size() > kMaximumAudioPackageManifestBytes - bytes_.size()) {
            return fail("audio package manifest exceeds the 16 MiB limit");
        }
        for (const char character : value) {
            bytes_.push_back(
                static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        return true;
    }

    [[nodiscard]] bool append_byte(char value) {
        if (!ready()) {
            return false;
        }
        if (bytes_.size() == kMaximumAudioPackageManifestBytes) {
            return fail("audio package manifest exceeds the 16 MiB limit");
        }
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        return true;
    }

    [[nodiscard]] bool fail(std::string message) {
        if (error_message_.empty()) {
            error_message_ = std::move(message);
        }
        return false;
    }

    [[nodiscard]] bool ready() const noexcept {
        return error_message_.empty() && !finished_;
    }

    std::vector<std::byte> bytes_;
    std::vector<Container> containers_;
    std::string error_message_;
    bool root_written_ = false;
    bool finished_ = false;
};

[[nodiscard]] bool
write_content_identity(RuntimeJsonWriter &writer,
                       const contract::AudioPackageContentIdentity &identity) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(identity.id) && writer.key("sha256") &&
           writer.sha256_value(identity.sha256) && writer.end_object();
}

[[nodiscard]] std::string_view
encoding_id(contract::AudioSampleEncoding encoding) noexcept {
    switch (encoding) {
    case contract::AudioSampleEncoding::float32le:
        return "float32le";
    case contract::AudioSampleEncoding::pcm_s16le:
    case contract::AudioSampleEncoding::pcm_s24le:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view
container_id(contract::AudioPackageAudioContainer container) noexcept {
    return container == contract::AudioPackageAudioContainer::wav ? "wav"
                                                                  : std::string_view{};
}

[[nodiscard]] std::string_view
channel_layout_id(contract::AudioChannelLayout layout) noexcept {
    return layout == contract::AudioChannelLayout::mono ? "mono" : std::string_view{};
}

[[nodiscard]] std::string_view
direction_id(contract::AudioPackageRunningDirection direction) noexcept {
    switch (direction) {
    case contract::AudioPackageRunningDirection::rising:
        return "rising";
    case contract::AudioPackageRunningDirection::falling:
        return "falling";
    }
    return {};
}

[[nodiscard]] std::string_view
bus_kind_id(contract::AudioPackageBusKind kind) noexcept {
    switch (kind) {
    case contract::AudioPackageBusKind::master_engine_audition:
        return "master_engine_audition";
    case contract::AudioPackageBusKind::source_route:
        return "source_route";
    }
    return {};
}

[[nodiscard]] std::string_view
bus_disposition_id(contract::AudioPackageBusDisposition disposition) noexcept {
    switch (disposition) {
    case contract::AudioPackageBusDisposition::monitor_mix:
        return "monitor_mix";
    case contract::AudioPackageBusDisposition::positional_emitter:
        return "positional_emitter";
    }
    return {};
}

[[nodiscard]] std::string_view
source_route_kind_id(contract::SourceRouteKind kind) noexcept {
    switch (kind) {
    case contract::SourceRouteKind::exhaust_outlet:
        return "exhaust_outlet";
    case contract::SourceRouteKind::intake_inlet:
        return "intake_inlet";
    case contract::SourceRouteKind::mechanical_engine:
        return "mechanical_engine";
    case contract::SourceRouteKind::mechanical_starter:
        return "mechanical_starter";
    case contract::SourceRouteKind::unspecified:
        break;
    }
    return {};
}

[[nodiscard]] bool write_source_route(
    RuntimeJsonWriter &writer,
    const std::optional<contract::AudioPackageSourceRouteDescriptor> &route) {
    if (!route.has_value()) {
        return writer.null_value();
    }
    return writer.begin_object() && writer.key("kind") &&
           writer.string_value(source_route_kind_id(route->kind)) &&
           writer.key("semantic_id") && writer.string_value(route->semantic_id) &&
           writer.key("emitter_anchor_id") &&
           writer.string_value(route->emitter_anchor_id) && writer.end_object();
}

[[nodiscard]] bool
write_boundary(RuntimeJsonWriter &writer,
               const contract::AudioPackageSourceBoundary &boundary) {
    return writer.begin_object() && writer.key("left_frame") &&
           writer.uint64_value(boundary.left_frame) && writer.key("right_frame") &&
           writer.uint64_value(boundary.right_frame) &&
           writer.key("fraction_from_left_01") &&
           writer.double_value(boundary.fraction_from_left_01) && writer.end_object();
}

[[nodiscard]] bool write_unit(RuntimeJsonWriter &writer,
                              const contract::AudioPackageCycleUnit &unit) {
    return writer.begin_object() && writer.key("completed_cycle_ordinal") &&
           writer.uint64_value(unit.completed_cycle_ordinal) && writer.key("start") &&
           write_boundary(writer, unit.start) && writer.key("end") &&
           write_boundary(writer, unit.end) && writer.key("canonical_rpm") &&
           writer.double_value(unit.canonical_rpm) && writer.key("measured_rpm") &&
           writer.double_value(unit.measured_rpm) &&
           writer.key("average_signed_load") &&
           writer.double_value(unit.average_signed_load) &&
           writer.key("average_net_torque_nm") &&
           (unit.average_net_torque_nm.has_value()
                ? writer.double_value(*unit.average_net_torque_nm)
                : writer.null_value()) &&
           writer.key("average_requested_throttle_01") &&
           writer.double_value(unit.average_requested_throttle_01) &&
           writer.key("average_resolved_throttle_01") &&
           writer.double_value(unit.average_resolved_throttle_01) &&
           writer.key("state_mask") && writer.uint32_value(unit.state_mask) &&
           writer.key("transition_mask") && writer.uint32_value(unit.transition_mask) &&
           writer.end_object();
}

[[nodiscard]] std::string_view
completeness_id(const contract::Completeness completeness) noexcept {
    switch (completeness) {
    case contract::Completeness::complete:
        return "complete";
    case contract::Completeness::incomplete:
        return "incomplete";
    }
    return {};
}

[[nodiscard]] bool
write_units(RuntimeJsonWriter &writer,
            const std::vector<contract::AudioPackageCycleUnit> &units) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &unit : units) {
        if (!write_unit(writer, unit)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_lane_artifacts(
    RuntimeJsonWriter &writer,
    const std::vector<contract::AudioPackageLaneArtifactRef> &references) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &reference : references) {
        if (!(writer.begin_object() && writer.key("bus_id") &&
              writer.string_value(reference.bus_id) && writer.key("artifact_id") &&
              writer.string_value(reference.artifact_id) && writer.end_object())) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_plane(RuntimeJsonWriter &writer,
                               const contract::AudioPackageRunningPlane &plane) {
    return writer.begin_object() && writer.key("id") && writer.string_value(plane.id) &&
           writer.key("load_coordinate") &&
           writer.double_value(plane.load_coordinate) && writer.key("direction") &&
           writer.string_value(direction_id(plane.direction)) &&
           writer.key("source_scenario") &&
           write_content_identity(writer, plane.source_scenario) &&
           writer.key("artifacts") && write_lane_artifacts(writer, plane.artifacts) &&
           writer.key("units") && write_units(writer, plane.units) &&
           writer.end_object();
}

[[nodiscard]] bool write_manifest(RuntimeJsonWriter &writer,
                                  const contract::AudioPackageManifest &manifest) {
    if (!(writer.begin_object() && writer.key("schema") &&
          writer.string_value(manifest.schema) && writer.key("identity") &&
          writer.begin_object() && writer.key("package_id") &&
          writer.string_value(manifest.identity.package_id) && writer.key("engine") &&
          write_content_identity(writer, manifest.identity.engine) &&
          writer.key("bake_plan") &&
          write_content_identity(writer, manifest.identity.bake_plan) &&
          writer.end_object() && writer.key("provenance") && writer.begin_object() &&
          writer.key("renderer_build") &&
          write_content_identity(writer, manifest.provenance.renderer_build) &&
          writer.key("source_inputs") && writer.begin_object() && writer.key("id") &&
          writer.string_value(manifest.provenance.source_inputs.id) &&
          writer.key("sha256") &&
          writer.sha256_value(manifest.provenance.source_inputs.sha256) &&
          writer.end_object() && writer.end_object() && writer.key("audio") &&
          writer.begin_object() && writer.key("sample_rate") && writer.begin_object() &&
          writer.key("numerator") &&
          writer.uint64_value(manifest.audio.sample_rate.numerator) &&
          writer.key("denominator") &&
          writer.uint64_value(manifest.audio.sample_rate.denominator) &&
          writer.end_object() && writer.key("container") &&
          writer.string_value(container_id(manifest.audio.container)) &&
          writer.key("encoding") &&
          writer.string_value(encoding_id(manifest.audio.encoding)) &&
          writer.key("channel_layout") &&
          writer.string_value(channel_layout_id(manifest.audio.channel_layout)) &&
          writer.end_object() && writer.key("buses") && writer.begin_array())) {
        return false;
    }
    for (const auto &bus : manifest.buses) {
        if (!(writer.begin_object() && writer.key("id") &&
              writer.string_value(bus.id) && writer.key("kind") &&
              writer.string_value(bus_kind_id(bus.kind)) && writer.key("disposition") &&
              writer.string_value(bus_disposition_id(bus.disposition)) &&
              writer.key("source_route") &&
              write_source_route(writer, bus.source_route) && writer.end_object())) {
            return false;
        }
    }
    if (!(writer.end_array() && writer.key("running") && writer.begin_object() &&
          writer.key("cycle_revolutions") &&
          writer.uint32_value(manifest.running.cycle_revolutions) &&
          writer.key("selector_seed") &&
          writer.uint64_string_value(manifest.running.selector_seed) &&
          writer.key("cycle_signal_alignment_frames") &&
          writer.double_value(manifest.running.cycle_signal_alignment_frames) &&
          writer.key("load_calibration") && writer.begin_object() &&
          writer.key("signal") &&
          writer.string_value(
              "cycle-mean-integrated-instantaneous-net-shaft") &&
          writer.key("completeness") &&
          writer.string_value(
              completeness_id(manifest.running.load_calibration.completeness)) &&
          writer.key("included_terms") &&
          writer.uint64_string_value(
              manifest.running.load_calibration.included_terms) &&
          writer.key("omitted_terms") &&
          writer.uint64_string_value(
              manifest.running.load_calibration.omitted_terms) &&
          writer.end_object() &&
          writer.key("rpm_grid") && writer.begin_object() &&
          writer.key("minimum_rpm") &&
          writer.double_value(manifest.running.rpm_grid.minimum_rpm) &&
          writer.key("playback_minimum_rpm") &&
          writer.double_value(manifest.running.rpm_grid.playback_minimum_rpm) &&
          writer.key("playback_maximum_rpm") &&
          writer.double_value(manifest.running.rpm_grid.playback_maximum_rpm) &&
          writer.key("maximum_rpm") &&
          writer.double_value(manifest.running.rpm_grid.maximum_rpm) &&
          writer.key("spacing_rpm") &&
          writer.double_value(manifest.running.rpm_grid.spacing_rpm) &&
          writer.key("padding_rows_per_side") &&
          writer.uint32_value(manifest.running.rpm_grid.padding_rows_per_side) &&
          writer.key("neighbor_radius_rows") &&
          writer.uint32_value(manifest.running.rpm_grid.neighbor_radius_rows) &&
          writer.key("edge_guard_frames") &&
          writer.uint32_value(manifest.running.rpm_grid.edge_guard_frames) &&
          writer.key("maximum_assignment_error_rpm") &&
          writer.double_value(manifest.running.rpm_grid.maximum_assignment_error_rpm) &&
          writer.end_object() && writer.key("planes") && writer.begin_array())) {
        return false;
    }
    for (const auto &plane : manifest.running.planes) {
        if (!write_plane(writer, plane)) {
            return false;
        }
    }
    if (!(writer.end_array() && writer.key("idle") && writer.begin_object() &&
          writer.key("source_scenario") &&
          write_content_identity(writer, manifest.running.idle.source_scenario) &&
          writer.key("artifacts") &&
          write_lane_artifacts(writer, manifest.running.idle.artifacts) &&
          writer.key("units") && write_units(writer, manifest.running.idle.units) &&
          writer.end_object() && writer.end_object() && writer.key("events") &&
          writer.begin_array() && writer.end_array() && writer.key("artifacts") &&
          writer.begin_array())) {
        return false;
    }
    for (const auto &artifact : manifest.artifacts) {
        if (!(writer.begin_object() && writer.key("id") &&
              writer.string_value(artifact.id) && writer.key("relative_path") &&
              writer.string_value(artifact.relative_path) &&
              writer.key("frame_count") && writer.uint64_value(artifact.frame_count) &&
              writer.key("byte_count") && writer.uint64_value(artifact.byte_count) &&
              writer.key("payload_sha256") &&
              writer.sha256_value(artifact.payload_sha256) && writer.end_object())) {
            return false;
        }
    }
    return writer.end_array() && writer.end_object();
}

[[nodiscard]] RenderSinkError
invalid_manifest_error(const contract::ValidationReport &report) {
    std::string message = "audio package manifest failed contract validation";
    if (!report.issues.empty()) {
        message += ": ";
        message += report.issues.front().path;
        message += ": ";
        message += report.issues.front().message;
    }
    return {
        RenderSinkErrorKind::protocol_violation,
        "audio-package-manifest-invalid",
        std::move(message),
    };
}

} // namespace

ManifestEncodingResult
encode_audio_package_manifest(const contract::AudioPackageManifest &manifest) {
    try {
        const auto report = contract::validate(manifest);
        if (!report.ok()) {
            return invalid_manifest_error(report);
        }

        RuntimeJsonWriter writer;
        std::vector<std::byte> bytes;
        if (!write_manifest(writer, manifest) || !writer.finish(bytes)) {
            return RenderSinkError{
                RenderSinkErrorKind::publication_failure,
                "audio-package-manifest-encoding-failed",
                "deterministic audio package JSON encoding failed: " +
                    std::string{writer.error_message()},
            };
        }
        return ManifestEncoding{std::move(bytes)};
    } catch (const std::bad_alloc &) {
        return RenderSinkError{
            RenderSinkErrorKind::publication_failure,
            "audio-package-manifest-encoding-allocation-failed",
            "deterministic audio package JSON encoding ran out of memory",
        };
    } catch (const std::exception &exception) {
        return RenderSinkError{
            RenderSinkErrorKind::publication_failure,
            "audio-package-manifest-encoding-threw",
            "deterministic audio package JSON encoding threw: " +
                std::string{exception.what()},
        };
    } catch (...) {
        return RenderSinkError{
            RenderSinkErrorKind::publication_failure,
            "audio-package-manifest-encoding-threw",
            "deterministic audio package JSON encoding threw a non-standard "
            "exception",
        };
    }
}

} // namespace engine_sim_offline::artifacts
