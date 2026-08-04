#include "engine_sim_offline/artifacts/audio_atlas_manifest_encoder.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

enum class EncoderError : std::uint8_t {
    none,
    invalid_utf8,
    non_finite_number,
    unsupported_value,
};

[[nodiscard]] bool is_continuation(const unsigned char value) noexcept {
    return value >= 0x80U && value <= 0xbfU;
}

[[nodiscard]] bool is_valid_unicode_scalar_utf8(const std::string_view value) noexcept {
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first <= 0x7fU) {
            ++offset;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (value.size() - offset < 2U ||
                !is_continuation(static_cast<unsigned char>(value[offset + 1U]))) {
                return false;
            }
            offset += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (value.size() - offset < 3U) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            if (!is_continuation(third) ||
                (first == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
                (first == 0xedU && (second < 0x80U || second > 0x9fU)) ||
                ((first != 0xe0U && first != 0xedU) && !is_continuation(second))) {
                return false;
            }
            offset += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (value.size() - offset < 4U) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            const auto fourth = static_cast<unsigned char>(value[offset + 3U]);
            if (!is_continuation(third) || !is_continuation(fourth) ||
                (first == 0xf0U && (second < 0x90U || second > 0xbfU)) ||
                (first == 0xf4U && (second < 0x80U || second > 0x8fU)) ||
                ((first != 0xf0U && first != 0xf4U) && !is_continuation(second))) {
                return false;
            }
            offset += 4U;
            continue;
        }
        return false;
    }
    return true;
}

class JsonEncoder final {
  public:
    [[nodiscard]] bool raw(const std::string_view value) {
        if (error_ != EncoderError::none) {
            return false;
        }
        for (const char character : value) {
            bytes_.push_back(
                static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        return true;
    }

    [[nodiscard]] bool character(const char value) {
        if (error_ != EncoderError::none) {
            return false;
        }
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        return true;
    }

    [[nodiscard]] bool string_value(const std::string_view value) {
        if (!is_valid_unicode_scalar_utf8(value)) {
            return fail(EncoderError::invalid_utf8,
                        "audio-atlas string is not valid Unicode scalar UTF-8");
        }
        if (!character('"')) {
            return false;
        }
        for (const char source_character : value) {
            const auto byte = static_cast<unsigned char>(source_character);
            switch (byte) {
            case '"':
                if (!raw("\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!raw("\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!raw("\\b")) {
                    return false;
                }
                break;
            case '\t':
                if (!raw("\\t")) {
                    return false;
                }
                break;
            case '\n':
                if (!raw("\\n")) {
                    return false;
                }
                break;
            case '\f':
                if (!raw("\\f")) {
                    return false;
                }
                break;
            case '\r':
                if (!raw("\\r")) {
                    return false;
                }
                break;
            default:
                if (byte < 0x20U) {
                    const std::array<char, 6U> escape{'\\',
                                                      'u',
                                                      '0',
                                                      '0',
                                                      kHexDigits[byte >> 4U],
                                                      kHexDigits[byte & 0x0fU]};
                    if (!raw(std::string_view{escape.data(), escape.size()})) {
                        return false;
                    }
                } else if (!character(source_character)) {
                    return false;
                }
                break;
            }
        }
        return character('"');
    }

    [[nodiscard]] bool key(const std::string_view value) {
        return string_value(value) && character(':');
    }

    [[nodiscard]] bool uint32_value(const std::uint32_t value) {
        std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1U> encoded{};
        const auto conversion =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (conversion.ec != std::errc{}) {
            return fail(EncoderError::unsupported_value,
                        "audio-atlas uint32 value could not be encoded");
        }
        return raw(std::string_view{
            encoded.data(), static_cast<std::size_t>(conversion.ptr - encoded.data())});
    }

    [[nodiscard]] bool uint64_string_value(const std::uint64_t value) {
        std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1U> encoded{};
        const auto conversion =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (conversion.ec != std::errc{}) {
            return fail(EncoderError::unsupported_value,
                        "audio-atlas uint64 value could not be encoded");
        }
        return character('"') &&
               raw(std::string_view{
                   encoded.data(),
                   static_cast<std::size_t>(conversion.ptr - encoded.data())}) &&
               character('"');
    }

    [[nodiscard]] bool number_value(double value) {
        if (!std::isfinite(value)) {
            return fail(EncoderError::non_finite_number,
                        "audio-atlas number must be finite");
        }
        if (value == 0.0) {
            value = 0.0;
        }
        std::array<char, 32U> encoded{};
        const auto conversion =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value,
                          std::chars_format::general);
        if (conversion.ec != std::errc{}) {
            return fail(EncoderError::unsupported_value,
                        "audio-atlas binary64 value could not be encoded");
        }
        return raw(std::string_view{
            encoded.data(), static_cast<std::size_t>(conversion.ptr - encoded.data())});
    }

    [[nodiscard]] bool sha256_value(const contract::Sha256Digest &value) {
        std::array<char, 64U> encoded{};
        for (std::size_t index = 0U; index < value.bytes.size(); ++index) {
            const auto byte = value.bytes[index];
            encoded[index * 2U] = kHexDigits[(byte >> 4U) & 0x0fU];
            encoded[index * 2U + 1U] = kHexDigits[byte & 0x0fU];
        }
        return character('"') &&
               raw(std::string_view{encoded.data(), encoded.size()}) && character('"');
    }

    [[nodiscard]] bool fail(const EncoderError error, std::string message) {
        if (error_ == EncoderError::none) {
            error_ = error;
            error_message_ = std::move(message);
        }
        return false;
    }

    [[nodiscard]] EncoderError error() const noexcept {
        return error_;
    }

    [[nodiscard]] std::string_view error_message() const noexcept {
        return error_message_;
    }

    [[nodiscard]] std::vector<std::byte> finish() {
        if (error_ != EncoderError::none) {
            return {};
        }
        static_cast<void>(character('\n'));
        return std::move(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
    EncoderError error_ = EncoderError::none;
    std::string error_message_;
};

template <class Range, class WriteElement>
[[nodiscard]] bool write_array(JsonEncoder &writer, const Range &values,
                               WriteElement write_element) {
    if (!writer.character('[')) {
        return false;
    }
    bool first = true;
    for (const auto &value : values) {
        if ((!first && !writer.character(',')) || !write_element(writer, value)) {
            return false;
        }
        first = false;
    }
    return writer.character(']');
}

[[nodiscard]] bool
write_content_identity(JsonEncoder &writer,
                       const contract::AudioAtlasContentIdentity &identity) {
    return writer.character('{') && writer.key("id") &&
           writer.string_value(identity.id) && writer.character(',') &&
           writer.key("sha256") && writer.sha256_value(identity.sha256) &&
           writer.character('}');
}

[[nodiscard]] bool write_source_inputs(JsonEncoder &writer,
                                       const contract::ProvenanceBundleRef &identity) {
    return writer.character('{') && writer.key("id") &&
           writer.string_value(identity.id) && writer.character(',') &&
           writer.key("sha256") && writer.sha256_value(identity.sha256) &&
           writer.character('}');
}

[[nodiscard]] bool write_audio_format(JsonEncoder &writer,
                                      const contract::AudioAtlasAudioFormat &audio) {
    if (!writer.character('{') || !writer.key("sample_rate_hz") ||
        !writer.uint32_value(audio.sample_rate_hz) || !writer.character(',') ||
        !writer.key("encoding")) {
        return false;
    }
    switch (audio.encoding) {
    case contract::AudioAtlasSampleEncoding::float32le:
        if (!writer.string_value("float32le")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas sample encoding");
    }
    if (!writer.character(',') || !writer.key("channel_layout")) {
        return false;
    }
    switch (audio.channel_layout) {
    case contract::AudioAtlasChannelLayout::mono:
        if (!writer.string_value("mono")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas channel layout");
    }
    return writer.character(',') && writer.key("buses") &&
           write_array(writer, audio.buses,
                       [](JsonEncoder &output, const contract::AudioAtlasBus &bus) {
                           return output.character('{') && output.key("id") &&
                                  output.string_value(bus.id) && output.character('}');
                       }) &&
           writer.character('}');
}

[[nodiscard]] bool write_domain(JsonEncoder &writer,
                                const contract::AudioAtlasDomain &domain) {
    return writer.character('{') && writer.key("minimum_rpm") &&
           writer.number_value(domain.minimum_rpm) && writer.character(',') &&
           writer.key("maximum_rpm") && writer.number_value(domain.maximum_rpm) &&
           writer.character(',') && writer.key("minimum_load_coordinate") &&
           writer.number_value(domain.minimum_load_coordinate) &&
           writer.character(',') && writer.key("maximum_load_coordinate") &&
           writer.number_value(domain.maximum_load_coordinate) && writer.character('}');
}

[[nodiscard]] bool write_frame_range(JsonEncoder &writer,
                                     const contract::AudioAtlasFrameRange &range) {
    return writer.character('{') && writer.key("begin") &&
           writer.uint64_string_value(range.begin) && writer.character(',') &&
           writer.key("end") && writer.uint64_string_value(range.end) &&
           writer.character('}');
}

[[nodiscard]] bool write_rpm_range(JsonEncoder &writer,
                                   const contract::AudioAtlasRpmRange &range) {
    return writer.character('{') && writer.key("minimum") &&
           writer.number_value(range.minimum) && writer.character(',') &&
           writer.key("maximum") && writer.number_value(range.maximum) &&
           writer.character('}');
}

[[nodiscard]] bool
write_normalized_slope_range(JsonEncoder &writer,
                             const contract::AudioAtlasNormalizedRpmSlopeRange &range) {
    return writer.character('{') && writer.key("minimum_per_second") &&
           writer.number_value(range.minimum_per_second) && writer.character(',') &&
           writer.key("maximum_per_second") &&
           writer.number_value(range.maximum_per_second) && writer.character('}');
}

[[nodiscard]] bool
write_bus_artifact_ref(JsonEncoder &writer,
                       const contract::AudioAtlasBusArtifactRef &reference) {
    return writer.character('{') && writer.key("bus_id") &&
           writer.string_value(reference.bus_id) && writer.character(',') &&
           writer.key("artifact_id") && writer.string_value(reference.artifact_id) &&
           writer.character('}');
}

[[nodiscard]] bool write_timeline_knot(JsonEncoder &writer,
                                       const contract::AudioAtlasTimelineKnot &knot) {
    return writer.character('{') && writer.key("frame") &&
           writer.uint64_string_value(knot.frame) && writer.character(',') &&
           writer.key("rpm") && writer.number_value(knot.rpm) &&
           writer.character(',') && writer.key("rpm_slope_rpm_per_second") &&
           writer.number_value(knot.rpm_slope_rpm_per_second) &&
           writer.character(',') && writer.key("requested_throttle_01") &&
           writer.number_value(knot.requested_throttle_01) && writer.character(',') &&
           writer.key("signed_load_coordinate") &&
           writer.number_value(knot.signed_load_coordinate) && writer.character(',') &&
           writer.key("manifold_pressure_pa_abs") &&
           writer.number_value(knot.manifold_pressure_pa_abs) &&
           writer.character(',') && writer.key("unwrapped_crank_revolutions") &&
           writer.number_value(knot.unwrapped_crank_revolutions) &&
           writer.character(',') && writer.key("state_mask") &&
           writer.uint32_value(knot.state_mask) && writer.character(',') &&
           writer.key("transition_mask") && writer.uint32_value(knot.transition_mask) &&
           writer.character('}');
}

[[nodiscard]] bool
write_fractional_frame(JsonEncoder &writer,
                       const contract::AudioAtlasFractionalFrame &position) {
    return writer.character('{') && writer.key("left_frame") &&
           writer.uint64_string_value(position.left_frame) && writer.character(',') &&
           writer.key("right_frame") &&
           writer.uint64_string_value(position.right_frame) && writer.character(',') &&
           writer.key("fraction_from_left_01") &&
           writer.number_value(position.fraction_from_left_01) && writer.character('}');
}

[[nodiscard]] bool
write_crank_boundary(JsonEncoder &writer,
                     const contract::AudioAtlasCrankBoundary &boundary) {
    return writer.character('{') && writer.key("completed_cycle_ordinal") &&
           writer.uint64_string_value(boundary.completed_cycle_ordinal) &&
           writer.character(',') && writer.key("position") &&
           write_fractional_frame(writer, boundary.position) && writer.character('}');
}

[[nodiscard]] bool write_handoff(JsonEncoder &writer,
                                 const contract::AudioAtlasHandoffEnvelope &handoff) {
    return writer.character('{') && writer.key("transition_frames") &&
           writer.uint32_value(handoff.transition_frames) && writer.character(',') &&
           writer.key("maximum_rpm_error") &&
           writer.number_value(handoff.maximum_rpm_error) && writer.character(',') &&
           writer.key("maximum_normalized_rpm_slope_error_per_second") &&
           writer.number_value(handoff.maximum_normalized_rpm_slope_error_per_second) &&
           writer.character(',') && writer.key("maximum_load_error") &&
           writer.number_value(handoff.maximum_load_error) && writer.character(',') &&
           writer.key("maximum_crank_phase_error_revolutions") &&
           writer.number_value(handoff.maximum_crank_phase_error_revolutions) &&
           writer.character('}');
}

[[nodiscard]] bool
write_moving_segment(JsonEncoder &writer,
                     const contract::AudioAtlasMovingSegment &segment) {
    if (!writer.character('{') || !writer.key("id") ||
        !writer.string_value(segment.id) || !writer.character(',') ||
        !writer.key("direction")) {
        return false;
    }
    switch (segment.direction) {
    case contract::AudioAtlasMovingDirection::rising:
        if (!writer.string_value("rising")) {
            return false;
        }
        break;
    case contract::AudioAtlasMovingDirection::falling:
        if (!writer.string_value("falling")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas moving direction");
    }
    return writer.character(',') && writer.key("load_coordinate") &&
           writer.number_value(segment.load_coordinate) && writer.character(',') &&
           writer.key("state_mask") && writer.uint32_value(segment.state_mask) &&
           writer.character(',') && writer.key("normalized_rpm_slope") &&
           write_normalized_slope_range(writer, segment.normalized_rpm_slope) &&
           writer.character(',') && writer.key("captured_frames") &&
           write_frame_range(writer, segment.captured_frames) &&
           writer.character(',') && writer.key("usable_frames") &&
           write_frame_range(writer, segment.usable_frames) && writer.character(',') &&
           writer.key("captured_rpm") &&
           write_rpm_range(writer, segment.captured_rpm) && writer.character(',') &&
           writer.key("usable_rpm") && write_rpm_range(writer, segment.usable_rpm) &&
           writer.character(',') && writer.key("source_scenario") &&
           write_content_identity(writer, segment.source_scenario) &&
           writer.character(',') && writer.key("capture_configuration") &&
           write_content_identity(writer, segment.capture_configuration) &&
           writer.character(',') && writer.key("artifacts") &&
           write_array(writer, segment.artifacts, write_bus_artifact_ref) &&
           writer.character(',') && writer.key("timeline") && writer.character('{') &&
           writer.key("knots") &&
           write_array(writer, segment.timeline.knots, write_timeline_knot) &&
           writer.character('}') && writer.character(',') &&
           writer.key("crank_boundaries") &&
           write_array(writer, segment.crank_boundaries, write_crank_boundary) &&
           writer.character(',') && writer.key("handoff") &&
           write_handoff(writer, segment.handoff) && writer.character('}');
}

[[nodiscard]] bool write_artifact(JsonEncoder &writer,
                                  const contract::AudioAtlasArtifact &artifact) {
    return writer.character('{') && writer.key("id") &&
           writer.string_value(artifact.id) && writer.character(',') &&
           writer.key("relative_path") && writer.string_value(artifact.relative_path) &&
           writer.character(',') && writer.key("frame_count") &&
           writer.uint64_string_value(artifact.frame_count) && writer.character(',') &&
           writer.key("byte_count") &&
           writer.uint64_string_value(artifact.byte_count) && writer.character(',') &&
           writer.key("payload_sha256") &&
           writer.sha256_value(artifact.payload_sha256) && writer.character('}');
}

[[nodiscard]] bool write_provenance(JsonEncoder &writer,
                                    const contract::AudioAtlasProvenance &provenance) {
    return writer.character('{') && writer.key("engine") &&
           write_content_identity(writer, provenance.engine) && writer.character(',') &&
           writer.key("bake_document") &&
           write_content_identity(writer, provenance.bake_document) &&
           writer.character(',') && writer.key("renderer_build") &&
           write_content_identity(writer, provenance.renderer_build) &&
           writer.character(',') && writer.key("source_inputs") &&
           write_source_inputs(writer, provenance.source_inputs) &&
           writer.character('}');
}

[[nodiscard]] bool write_manifest(JsonEncoder &writer,
                                  const contract::AudioAtlasManifest &manifest) {
    return writer.character('{') && writer.key("schema") &&
           writer.string_value(manifest.schema) && writer.character(',') &&
           writer.key("id") && writer.string_value(manifest.id) &&
           writer.character(',') && writer.key("engine") &&
           writer.string_value(manifest.engine) && writer.character(',') &&
           writer.key("public_seed") &&
           writer.uint64_string_value(manifest.public_seed) && writer.character(',') &&
           writer.key("audio") && write_audio_format(writer, manifest.audio) &&
           writer.character(',') && writer.key("domain") &&
           write_domain(writer, manifest.domain) && writer.character(',') &&
           writer.key("moving_segments") &&
           write_array(writer, manifest.moving_segments, write_moving_segment) &&
           writer.character(',') && writer.key("stationary_tiles") &&
           writer.raw("[]") && writer.character(',') &&
           writer.key("transient_performances") && writer.raw("[]") &&
           writer.character(',') && writer.key("lifecycle_performances") &&
           writer.raw("[]") && writer.character(',') && writer.key("artifacts") &&
           write_array(writer, manifest.artifacts, write_artifact) &&
           writer.character(',') && writer.key("provenance") &&
           write_provenance(writer, manifest.provenance) && writer.character('}');
}

[[nodiscard]] RenderSinkError writer_error(const JsonEncoder &writer) {
    std::string detail_code = "audio-atlas-manifest-wire-unrepresentable";
    if (writer.error() == EncoderError::invalid_utf8) {
        detail_code = "audio-atlas-manifest-wire-invalid-utf8";
    } else if (writer.error() == EncoderError::non_finite_number) {
        detail_code = "audio-atlas-manifest-wire-nonfinite";
    }
    return {RenderSinkErrorKind::protocol_violation, std::move(detail_code),
            std::string{writer.error_message()}};
}

[[nodiscard]] RenderSinkError allocation_failure() {
    return {RenderSinkErrorKind::publication_failure,
            "audio-atlas-manifest-encoding-allocation-failed",
            "audio-atlas manifest encoding ran out of memory"};
}

[[nodiscard]] RenderSinkError exception_failure(const std::exception *exception) {
    auto message = std::string{"audio-atlas manifest encoding threw"};
    if (exception != nullptr) {
        message += ": ";
        message += exception->what();
    } else {
        message += " a non-standard exception";
    }
    return {RenderSinkErrorKind::publication_failure,
            "audio-atlas-manifest-encoding-threw", std::move(message)};
}

} // namespace

ManifestEncodingResult
encode_audio_atlas_manifest(const contract::AudioAtlasManifest &manifest) {
    try {
        const auto validation = contract::validate(manifest);
        if (!validation.ok()) {
            const auto &issue = validation.issues.front();
            return RenderSinkError{
                RenderSinkErrorKind::protocol_violation,
                "audio-atlas-manifest-invalid",
                issue.path + ": " + issue.message,
            };
        }

        JsonEncoder writer;
        if (!write_manifest(writer, manifest)) {
            return writer_error(writer);
        }
        return ManifestEncoding{writer.finish()};
    } catch (const std::bad_alloc &) {
        return allocation_failure();
    } catch (const std::exception &error) {
        return exception_failure(&error);
    } catch (...) {
        return exception_failure(nullptr);
    }
}

} // namespace engine_sim_offline::artifacts
