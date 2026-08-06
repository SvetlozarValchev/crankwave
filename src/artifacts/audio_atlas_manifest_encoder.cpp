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
    if (!writer.character('{') || !writer.key("minimum_rpm") ||
        !writer.number_value(domain.minimum_rpm) || !writer.character(',') ||
        !writer.key("maximum_rpm") ||
        !writer.number_value(domain.maximum_rpm) || !writer.character(',') ||
        !writer.key("load_coordinate")) {
        return false;
    }
    switch (domain.load_coordinate) {
    case contract::AudioAtlasLoadCoordinate::
        measured_intake_manifold_pressure_pa_abs:
        if (!writer.string_value(
                "measured-intake-manifold-pressure-pa-abs")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas load coordinate");
    }
    if (!writer.character(',') || !writer.key("minimum_load_pa_abs") ||
        !writer.number_value(domain.minimum_load_pa_abs) ||
        !writer.character(',') || !writer.key("maximum_load_pa_abs") ||
        !writer.number_value(domain.maximum_load_pa_abs) ||
        !writer.character(',') || !writer.key("phase_cycle_revolutions") ||
        !writer.number_value(domain.phase_cycle_revolutions) ||
        !writer.character(',') || !writer.key("supported_state_masks") ||
        !write_array(writer, domain.supported_state_masks,
                     [](JsonEncoder &output, const std::uint32_t state) {
                         return output.uint32_value(state);
                     }) ||
        !writer.character(',') || !writer.key("out_of_domain_behavior")) {
        return false;
    }
    switch (domain.out_of_domain_behavior) {
    case contract::AudioAtlasOutOfDomainBehavior::unavailable:
        if (!writer.string_value("unavailable")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas out-of-domain behavior");
    }
    return writer.character('}');
}

[[nodiscard]] bool
write_bus_artifact_ref(JsonEncoder &writer,
                       const contract::AudioAtlasBusArtifactRef &reference) {
    return writer.character('{') && writer.key("bus_id") &&
           writer.string_value(reference.bus_id) && writer.character(',') &&
           writer.key("artifact_id") && writer.string_value(reference.artifact_id) &&
           writer.character('}');
}

[[nodiscard]] bool
write_residual_taper(JsonEncoder &writer,
                     const contract::AudioAtlasResidualTaper &taper) {
    if (!writer.character('{') || !writer.key("method")) {
        return false;
    }
    switch (taper.method) {
    case contract::AudioAtlasResidualTaperMethod::
        boundary_zero_smoothstep_v1:
        if (!writer.string_value("boundary-zero-smoothstep-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual taper method");
    }
    return writer.character(',') && writer.key("boundary_value") &&
           writer.number_value(taper.boundary_value) && writer.character(',') &&
           writer.key("fraction_per_edge") &&
           writer.number_value(taper.fraction_per_edge) && writer.character(',') &&
           writer.key("frames_per_edge") &&
           writer.uint32_value(taper.frames_per_edge) && writer.character('}');
}

[[nodiscard]] bool write_selector(
    JsonEncoder &writer, const contract::AudioAtlasSelectorContract &selector) {
    if (!writer.character('{') || !writer.key("method")) {
        return false;
    }
    switch (selector.method) {
    case contract::AudioAtlasSelectorMethod::splitmix64_shuffled_bags_v1:
        if (!writer.string_value("splitmix64-shuffled-bags-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual selector method");
    }
    if (!writer.character(',') || !writer.key("no_adjacent_repeat") ||
        !writer.raw(selector.no_adjacent_repeat ? "true" : "false") ||
        !writer.character(',') || !writer.key("change_phase")) {
        return false;
    }
    switch (selector.change_phase) {
    case contract::AudioAtlasSelectorChangePhase::phase_cycle_boundary:
        if (!writer.string_value("phase-cycle-boundary")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual selector change phase");
    }
    return writer.character('}');
}

[[nodiscard]] bool write_interpolation(
    JsonEncoder &writer,
    const contract::AudioAtlasInterpolationContract &interpolation) {
    if (!writer.character('{') || !writer.key("mean") ||
        !writer.character('{') || !writer.key("method")) {
        return false;
    }
    switch (interpolation.mean.method) {
    case contract::AudioAtlasMeanInterpolationMethod::
        common_delay_phase_warp_v1:
        if (!writer.string_value("common-delay-phase-warp-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported phase-mean interpolation method");
    }
    if (!writer.character(',') || !writer.key("energy_target")) {
        return false;
    }
    switch (interpolation.mean.energy_target) {
    case contract::AudioAtlasMeanEnergyTarget::linear_anchor_rms:
        if (!writer.string_value("linear-anchor-rms")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported phase-mean energy target");
    }
    if (!writer.character('}') || !writer.character(',') ||
        !writer.key("residual") || !writer.character('{') ||
        !writer.key("cross_cell_correlation")) {
        return false;
    }
    switch (interpolation.residual.cross_cell_correlation) {
    case contract::AudioAtlasResidualCorrelation::independent:
        if (!writer.string_value("independent")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual correlation contract");
    }
    if (!writer.character(',') || !writer.key("energy_target")) {
        return false;
    }
    switch (interpolation.residual.energy_target) {
    case contract::AudioAtlasResidualEnergyTarget::linear_anchor_power:
        if (!writer.string_value("linear-anchor-power")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual energy target");
    }
    if (!writer.character(',') || !writer.key("normalization")) {
        return false;
    }
    switch (interpolation.residual.normalization) {
    case contract::AudioAtlasResidualNormalization::
        sqrt_target_power_over_weighted_anchor_power:
        if (!writer.string_value(
                "sqrt-target-power-over-weighted-anchor-power")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported residual normalization");
    }
    return writer.character('}') && writer.character('}');
}

[[nodiscard]] bool write_load_lane(JsonEncoder &writer,
                                   const contract::AudioAtlasLoadLane &lane) {
    return writer.character('{') && writer.key("id") &&
           writer.string_value(lane.id) && writer.character(',') &&
           writer.key("requested_throttle_01") &&
           writer.number_value(lane.requested_throttle_01) &&
           writer.character(',') && writer.key("state_mask") &&
           writer.uint32_value(lane.state_mask) && writer.character('}');
}

[[nodiscard]] bool
write_phase_route(JsonEncoder &writer,
                  const contract::AudioAtlasPhaseRouteRef &route) {
    return writer.character('{') && writer.key("route_id") &&
           writer.string_value(route.route_id) && writer.character(',') &&
           writer.key("mean_artifact_id") &&
           writer.string_value(route.mean_artifact_id) && writer.character(',') &&
           writer.key("residual_artifact_id") &&
           writer.string_value(route.residual_artifact_id) &&
           writer.character(',') && writer.key("mean_rms") &&
           writer.number_value(route.mean_rms) && writer.character(',') &&
           writer.key("residual_power") &&
           writer.number_value(route.residual_power) && writer.character('}');
}

[[nodiscard]] bool write_phase_cell(JsonEncoder &writer,
                                    const contract::AudioAtlasPhaseCell &cell) {
    return writer.character('{') && writer.key("id") &&
           writer.string_value(cell.id) && writer.character(',') &&
           writer.key("load_lane_id") &&
           writer.string_value(cell.load_lane_id) && writer.character(',') &&
           writer.key("rpm") && writer.number_value(cell.rpm) &&
           writer.character(',') && writer.key("load_coordinate_pa_abs") &&
           writer.number_value(cell.load_coordinate_pa_abs) &&
           writer.character(',') && writer.key("requested_throttle_01") &&
           writer.number_value(cell.requested_throttle_01) &&
           writer.character(',') && writer.key("state_mask") &&
           writer.uint32_value(cell.state_mask) && writer.character(',') &&
           writer.key("shift_to_canonical_samples") &&
           writer.number_value(cell.shift_to_canonical_samples) &&
           writer.character(',') && writer.key("routes") &&
           write_array(writer, cell.routes, write_phase_route) &&
           writer.character('}');
}

[[nodiscard]] bool
write_phase_texture(JsonEncoder &writer,
                    const contract::AudioAtlasPhaseTexture &texture) {
    return writer.character('{') && writer.key("samples_per_cycle") &&
           writer.uint32_value(texture.samples_per_cycle) &&
           writer.character(',') && writer.key("residual_cycle_count") &&
           writer.uint32_value(texture.residual_cycle_count) &&
           writer.character(',') && writer.key("residual_taper") &&
           write_residual_taper(writer, texture.residual_taper) &&
           writer.character(',') && writer.key("selector") &&
           write_selector(writer, texture.selector) && writer.character(',') &&
           writer.key("interpolation") &&
           write_interpolation(writer, texture.interpolation) &&
           writer.character(',') && writer.key("rpm_anchors") &&
           write_array(writer, texture.rpm_anchors,
                       [](JsonEncoder &output, const double rpm) {
                           return output.number_value(rpm);
                       }) &&
           writer.character(',') && writer.key("load_lanes") &&
           write_array(writer, texture.load_lanes, write_load_lane) &&
           writer.character(',') && writer.key("reference_cell_id") &&
           writer.string_value(texture.reference_cell_id) &&
           writer.character(',') && writer.key("source_route_ids") &&
           write_array(writer, texture.source_route_ids,
                       [](JsonEncoder &output, const std::string &id) {
                           return output.string_value(id);
                       }) &&
           writer.character(',') && writer.key("cells") &&
           write_array(writer, texture.cells, write_phase_cell) &&
           writer.character('}');
}

[[nodiscard]] bool write_transient_envelope(
    JsonEncoder &writer,
    const contract::AudioAtlasTransientEnvelope &envelope) {
    return writer.character('{') && writer.key("attack_frames") &&
           writer.uint32_value(envelope.attack_frames) && writer.character(',') &&
           writer.key("hold_frames") &&
           writer.uint32_value(envelope.hold_frames) && writer.character(',') &&
           writer.key("release_frames") &&
           writer.uint32_value(envelope.release_frames) &&
           writer.character(',') && writer.key("maximum_gain_linear") &&
           writer.number_value(envelope.maximum_gain_linear) &&
           writer.character('}');
}

[[nodiscard]] bool write_transient_policy(
    JsonEncoder &writer, const contract::AudioAtlasTransientPolicy &policy) {
    if (!writer.character('{') || !writer.key("detection_method")) {
        return false;
    }
    switch (policy.detection_method) {
    case contract::AudioAtlasTransientDetectionMethod::
        causal_throttle_window_v1:
        if (!writer.string_value("causal-throttle-window-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported transient detection method");
    }
    return writer.character(',') && writer.key("throttle_window_frames") &&
           writer.uint32_value(policy.throttle_window_frames) &&
           writer.character(',') && writer.key("minimum_history_frames") &&
           writer.uint32_value(policy.minimum_history_frames) &&
           writer.character(',') && writer.key("throttle_delta_onset_01") &&
           writer.number_value(policy.throttle_delta_onset_01) &&
           writer.character(',') && writer.key("throttle_delta_full_01") &&
           writer.number_value(policy.throttle_delta_full_01) &&
           writer.character(',') && writer.key("throttle_delta_rearm_01") &&
           writer.number_value(policy.throttle_delta_rearm_01) &&
           writer.character(',') && writer.key("refractory_frames") &&
           writer.uint32_value(policy.refractory_frames) &&
           writer.character(',') && writer.key("opposite_return_frames") &&
           writer.uint32_value(policy.opposite_return_frames) &&
           writer.character(',') && writer.key("rising") &&
           write_transient_envelope(writer, policy.rising) &&
           writer.character(',') && writer.key("falling") &&
           write_transient_envelope(writer, policy.falling) &&
           writer.character('}');
}

[[nodiscard]] bool write_transient_route(
    JsonEncoder &writer, const contract::AudioAtlasTransientRouteRef &route) {
    return writer.character('{') && writer.key("route_id") &&
           writer.string_value(route.route_id) && writer.character(',') &&
           writer.key("artifact_id") && writer.string_value(route.artifact_id) &&
           writer.character('}');
}

[[nodiscard]] bool write_transient_cell(
    JsonEncoder &writer, const contract::AudioAtlasTransientCell &cell) {
    if (!writer.character('{') || !writer.key("id") ||
        !writer.string_value(cell.id) || !writer.character(',') ||
        !writer.key("rpm") || !writer.number_value(cell.rpm) ||
        !writer.character(',') || !writer.key("load_coordinate_pa_abs") ||
        !writer.number_value(cell.load_coordinate_pa_abs) ||
        !writer.character(',') || !writer.key("requested_throttle_01") ||
        !writer.number_value(cell.requested_throttle_01) ||
        !writer.character(',') || !writer.key("state_mask") ||
        !writer.uint32_value(cell.state_mask) || !writer.character(',') ||
        !writer.key("cycle_count") ||
        !writer.uint32_value(cell.cycle_count) || !writer.character(',') ||
        !writer.key("samples_per_cycle") ||
        !writer.uint32_value(cell.samples_per_cycle) ||
        !writer.character(',') || !writer.key("phase_origin_revolutions") ||
        !writer.number_value(cell.phase_origin_revolutions) ||
        !writer.character(',') ||
        !writer.key("source_cycle_origin_ordinal_mod_cycle_count") ||
        !writer.uint32_value(
            cell.source_cycle_origin_ordinal_mod_cycle_count) ||
        !writer.character(',') || !writer.key("seam_closure") ||
        !writer.character('{') || !writer.key("method")) {
        return false;
    }
    switch (cell.seam_closure_method) {
    case contract::AudioAtlasTransientCell::SeamClosureMethod::
        phase_aligned_boundary_smoothstep_v1:
        if (!writer.string_value("phase-aligned-boundary-smoothstep-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported transient seam-closure method");
    }
    return writer.character(',') && writer.key("frames_per_side") &&
           writer.uint32_value(cell.seam_closure_frames_per_side) &&
           writer.character('}') && writer.character(',') &&
           writer.key("routes") &&
           write_array(writer, cell.routes, write_transient_route) &&
           writer.character('}');
}

[[nodiscard]] bool write_transient_layer(
    JsonEncoder &writer, const contract::AudioAtlasTransientLayer &layer) {
    if (!writer.character('{') || !writer.key("id") ||
        !writer.string_value(layer.id) || !writer.character(',') ||
        !writer.key("direction")) {
        return false;
    }
    switch (layer.direction) {
    case contract::AudioAtlasTransientDirection::rising:
        if (!writer.string_value("rising")) {
            return false;
        }
        break;
    case contract::AudioAtlasTransientDirection::falling:
        if (!writer.string_value("falling")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported transient direction");
    }
    return writer.character(',') && writer.key("source_route_ids") &&
           write_array(writer, layer.source_route_ids,
                       [](JsonEncoder &output, const std::string &id) {
                           return output.string_value(id);
                       }) &&
           writer.character(',') && writer.key("cells") &&
           write_array(writer, layer.cells, write_transient_cell) &&
           writer.character('}');
}

[[nodiscard]] bool write_lifecycle_knot(
    JsonEncoder &writer, const contract::AudioAtlasLifecycleKnot &knot) {
    return writer.character('{') && writer.key("frame") &&
           writer.uint64_string_value(knot.frame) && writer.character(',') &&
           writer.key("rpm") && writer.number_value(knot.rpm) &&
           writer.character(',') && writer.key("load_coordinate_pa_abs") &&
           writer.number_value(knot.load_coordinate_pa_abs) &&
           writer.character(',') && writer.key("requested_throttle_01") &&
           writer.number_value(knot.requested_throttle_01) &&
           writer.character(',') && writer.key("state_mask") &&
           writer.uint32_value(knot.state_mask) && writer.character('}');
}

[[nodiscard]] bool write_lifecycle_performance(
    JsonEncoder &writer,
    const contract::AudioAtlasLifecyclePerformance &performance) {
    if (!writer.character('{') || !writer.key("id") ||
        !writer.string_value(performance.id) || !writer.character(',') ||
        !writer.key("event")) {
        return false;
    }
    switch (performance.event) {
    case contract::AudioAtlasLifecycleEvent::startup:
        if (!writer.string_value("startup")) {
            return false;
        }
        break;
    case contract::AudioAtlasLifecycleEvent::shutdown:
        if (!writer.string_value("shutdown")) {
            return false;
        }
        break;
    case contract::AudioAtlasLifecycleEvent::limiter:
        if (!writer.string_value("limiter")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported lifecycle event");
    }
    return writer.character(',') && writer.key("entry_state_mask") &&
           writer.uint32_value(performance.entry_state_mask) &&
           writer.character(',') && writer.key("exit_state_mask") &&
           writer.uint32_value(performance.exit_state_mask) &&
           writer.character(',') && writer.key("artifacts") &&
           write_array(writer, performance.artifacts, write_bus_artifact_ref) &&
           writer.character(',') && writer.key("timeline") &&
           write_array(writer, performance.timeline, write_lifecycle_knot) &&
           writer.character('}');
}

[[nodiscard]] bool write_transfer(
    JsonEncoder &writer, const contract::AudioAtlasRouteTransfer &transfer) {
    if (!writer.character('{') || !writer.key("kind")) {
        return false;
    }
    switch (transfer.kind) {
    case contract::AudioAtlasTransferKind::direct:
        return writer.string_value("direct") && writer.character('}');
    case contract::AudioAtlasTransferKind::fixed_spectrum_convolution:
        return writer.string_value("fixed-spectrum-convolution") &&
               writer.character(',') && writer.key("fft_size") &&
               writer.uint32_value(transfer.fft_size) && writer.character(',') &&
               writer.key("coefficient_count") &&
               writer.uint32_value(transfer.coefficient_count) &&
               writer.character(',') && writer.key("spectrum_artifact_id") &&
               writer.string_value(transfer.spectrum_artifact_id) &&
               writer.character('}');
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported presentation transfer");
    }
}

[[nodiscard]] bool write_presentation_route(
    JsonEncoder &writer, const contract::AudioAtlasPresentationRoute &route) {
    return writer.character('{') && writer.key("route_id") &&
           writer.string_value(route.route_id) && writer.character(',') &&
           writer.key("output_bus_id") &&
           writer.string_value(route.output_bus_id) && writer.character(',') &&
           writer.key("wet_mix_01") && writer.number_value(route.wet_mix_01) &&
           writer.character(',') && writer.key("transfer") &&
           write_transfer(writer, route.transfer) && writer.character('}');
}

[[nodiscard]] bool write_presentation(
    JsonEncoder &writer, const contract::AudioAtlasPresentation &presentation) {
    if (!writer.character('{') || !writer.key("method_identity") ||
        !write_content_identity(writer, presentation.method_identity) ||
        !writer.character(',') || !writer.key("build_identity") ||
        !write_content_identity(writer, presentation.build_identity) ||
        !writer.character(',') || !writer.key("batch_frames") ||
        !writer.uint32_value(presentation.batch_frames) ||
        !writer.character(',') ||
        !writer.key("captured_to_source_scale") ||
        !writer.number_value(presentation.captured_to_source_scale) ||
        !writer.character(',') || !writer.key("source_routes") ||
        !write_array(writer, presentation.source_routes,
                     write_presentation_route) ||
        !writer.character(',') || !writer.key("master") ||
        !writer.character('{') || !writer.key("method")) {
        return false;
    }
    switch (presentation.master.method) {
    case contract::AudioAtlasMasterMethod::canonical_adaptive_v1:
        if (!writer.string_value("canonical-adaptive-v1")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported master presentation method");
    }
    return writer.character(',') && writer.key("volume_linear") &&
           writer.number_value(presentation.master.volume_linear) &&
           writer.character('}') && writer.character('}');
}

[[nodiscard]] bool write_artifact(JsonEncoder &writer,
                                  const contract::AudioAtlasArtifact &artifact) {
    if (!writer.character('{') || !writer.key("id") ||
        !writer.string_value(artifact.id) || !writer.character(',') ||
        !writer.key("path") || !writer.string_value(artifact.path) ||
        !writer.character(',') || !writer.key("encoding")) {
        return false;
    }
    switch (artifact.encoding) {
    case contract::AudioAtlasArtifactEncoding::float32le:
        if (!writer.string_value("float32le")) {
            return false;
        }
        break;
    case contract::AudioAtlasArtifactEncoding::complex_float64le:
        if (!writer.string_value("complex-float64le")) {
            return false;
        }
        break;
    default:
        return writer.fail(EncoderError::unsupported_value,
                           "unsupported audio-atlas artifact encoding");
    }
    return writer.character(',') && writer.key("element_count") &&
           writer.uint64_string_value(artifact.element_count) &&
           writer.character(',') && writer.key("byte_count") &&
           writer.uint64_string_value(artifact.byte_count) &&
           writer.character(',') && writer.key("sha256") &&
           writer.sha256_value(artifact.sha256) && writer.character('}');
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
           writer.key("phase_texture") &&
           write_phase_texture(writer, manifest.phase_texture) &&
           writer.character(',') && writer.key("transient_policy") &&
           write_transient_policy(writer, manifest.transient_policy) &&
           writer.character(',') && writer.key("transient_layers") &&
           write_array(writer, manifest.transient_layers,
                       write_transient_layer) &&
           writer.character(',') && writer.key("lifecycle_performances") &&
           write_array(writer, manifest.lifecycle_performances,
                       write_lifecycle_performance) &&
           writer.character(',') && writer.key("presentation") &&
           write_presentation(writer, manifest.presentation) &&
           writer.character(',') && writer.key("artifacts") &&
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
