#include "manifest_encoder_impl.hpp"

namespace engine_sim_offline::artifacts::detail {
namespace {

bool write_audio_sample_encoding(CanonicalJsonWriter &writer,
                                 contract::AudioSampleEncoding encoding) {
    switch (encoding) {
    case contract::AudioSampleEncoding::pcm_s16le:
        return writer.string_value("pcm_s16le");
    case contract::AudioSampleEncoding::float32le:
        return writer.string_value("float32le");
    case contract::AudioSampleEncoding::pcm_s24le:
        return writer.string_value("pcm_s24le");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "unsupported audio sample encoding");
}

bool write_audio_channel_layout(CanonicalJsonWriter &writer,
                                contract::AudioChannelLayout layout) {
    switch (layout) {
    case contract::AudioChannelLayout::mono:
        return writer.string_value("mono");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "unsupported audio channel layout");
}

bool write_audio_media_contract(CanonicalJsonWriter &writer,
                                const contract::AudioMediaContract &media) {
    return writer.begin_object() && writer.key("encoding") &&
           write_audio_sample_encoding(writer, media.encoding) &&
           writer.key("channel_layout") &&
           write_audio_channel_layout(writer, media.channel_layout) &&
           writer.key("sample_rate") &&
           write_rational_rate(writer, media.sample_rate) &&
           writer.key("frame_count") && writer.uint64_hex_value(media.frame_count) &&
           writer.end_object();
}

bool write_presentation_methods(CanonicalJsonWriter &writer,
                                const contract::PresentationMethods &methods) {
    const auto write_method = [](CanonicalJsonWriter &output,
                                 const contract::MethodIdentity &method) {
        return write_method_identity(output, method);
    };
    return writer.begin_object() && writer.key("reconstruction") &&
           write_resolved(writer, methods.reconstruction, write_method) &&
           writer.key("conditioning") &&
           write_resolved(writer, methods.conditioning, write_method) &&
           writer.key("impulse_response_conversion") &&
           write_resolved(writer, methods.impulse_response_conversion, write_method) &&
           writer.key("convolution") &&
           write_resolved(writer, methods.convolution, write_method) &&
           writer.key("publication") &&
           write_resolved(writer, methods.publication, write_method) &&
           writer.key("audition_mix") &&
           write_resolved(writer, methods.audition_mix, write_method) &&
           writer.end_object();
}

bool write_conditioning(CanonicalJsonWriter &writer,
                        const contract::PresentationConditioning &conditioning) {
    const auto write_f64 = [](CanonicalJsonWriter &output, double value) {
        return output.binary64_bits_value(value);
    };
    return writer.begin_object() && writer.key("jitter_scale") &&
           write_resolved(writer, conditioning.jitter_scale, write_f64) &&
           writer.key("jitter_modulation_cutoff_hz") &&
           write_resolved(writer, conditioning.jitter_modulation_cutoff_hz,
                          write_f64) &&
           writer.key("derivative_mix_01") &&
           write_resolved(writer, conditioning.derivative_mix_01, write_f64) &&
           writer.key("air_noise_mix_01") &&
           write_resolved(writer, conditioning.air_noise_mix_01, write_f64) &&
           writer.key("air_noise_cutoff_hz") &&
           write_resolved(writer, conditioning.air_noise_cutoff_hz, write_f64) &&
           writer.end_object();
}

bool write_audio_asset(CanonicalJsonWriter &writer,
                       const contract::AudioAssetSpec &asset) {
    const auto write_string = [](CanonicalJsonWriter &output,
                                 const std::string &value) {
        return output.string_value(value);
    };
    const auto write_sha256 = [](CanonicalJsonWriter &output,
                                 const contract::Sha256Digest &value) {
        return output.sha256_value(value);
    };
    const auto write_media = [](CanonicalJsonWriter &output,
                                const contract::AudioMediaContract &value) {
        return write_audio_media_contract(output, value);
    };
    return writer.begin_object() && writer.key("id") &&
           writer.uint32_value(asset.id.value) && writer.key("semantic_id") &&
           write_resolved(writer, asset.semantic_id, write_string) &&
           writer.key("evidence_source_id") &&
           write_resolved(writer, asset.evidence_source_id, write_string) &&
           writer.key("content_sha256") &&
           write_resolved(writer, asset.content_sha256, write_sha256) &&
           writer.key("media") && write_resolved(writer, asset.media, write_media) &&
           writer.end_object();
}

bool write_audio_assets(CanonicalJsonWriter &writer,
                        const std::vector<contract::AudioAssetSpec> &assets) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &asset : assets) {
        if (!write_audio_asset(writer, asset)) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_route_presentation(CanonicalJsonWriter &writer,
                              const contract::RoutePresentation &route) {
    const auto write_f64 = [](CanonicalJsonWriter &output, double value) {
        return output.binary64_bits_value(value);
    };
    return writer.begin_object() && writer.key("route_id") &&
           writer.uint32_value(route.route_id.value) &&
           writer.key("source_gain_linear") &&
           write_resolved(writer, route.source_gain_linear, write_f64) &&
           writer.key("impulse_response_asset_id") &&
           (route.impulse_response_asset_id.has_value()
                ? writer.uint32_value(route.impulse_response_asset_id->value)
                : writer.null_value()) &&
           writer.key("impulse_response_gain_linear") &&
           write_resolved(writer, route.impulse_response_gain_linear, write_f64) &&
           writer.key("wet_mix_01") &&
           write_resolved(writer, route.wet_mix_01, write_f64) && writer.end_object();
}

bool write_route_presentations(CanonicalJsonWriter &writer,
                               const std::vector<contract::RoutePresentation> &routes) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &route : routes) {
        if (!write_route_presentation(writer, route)) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_stem_publication(CanonicalJsonWriter &writer,
                            const contract::StemPublication &publication) {
    const auto write_f64 = [](CanonicalJsonWriter &output, double value) {
        return output.binary64_bits_value(value);
    };
    return writer.begin_object() && writer.key("calibration_gain_linear") &&
           write_resolved(writer, publication.calibration_gain_linear, write_f64) &&
           writer.end_object();
}

bool write_route_id_array(CanonicalJsonWriter &writer,
                          const std::vector<contract::RouteId> &routes) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto route : routes) {
        if (!writer.uint32_value(route.value)) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_audition_mix(CanonicalJsonWriter &writer,
                        const contract::AuditionMix &audition) {
    const auto write_routes = [](CanonicalJsonWriter &output,
                                 const std::vector<contract::RouteId> &routes) {
        return write_route_id_array(output, routes);
    };
    const auto write_f64 = [](CanonicalJsonWriter &output, double value) {
        return output.binary64_bits_value(value);
    };
    return writer.begin_object() && writer.key("selected_routes") &&
           write_resolved(writer, audition.selected_routes, write_routes) &&
           writer.key("volume_linear") &&
           write_resolved(writer, audition.volume_linear, write_f64) &&
           writer.key("fade_in_duration_s") &&
           write_resolved(writer, audition.fade_in_duration_s, write_f64) &&
           writer.key("fade_out_duration_s") &&
           write_resolved(writer, audition.fade_out_duration_s, write_f64) &&
           writer.end_object();
}

} // namespace

bool write_presentation_calibration(
    CanonicalJsonWriter &writer,
    const contract::PresentationCalibration &presentation) {
    if (presentation.schema_version != 2U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "presentation calibration schema version is not v2");
    }
    const auto write_string = [](CanonicalJsonWriter &output,
                                 const std::string &value) {
        return output.string_value(value);
    };
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(presentation.schema_version) &&
           writer.key("calibration_id") &&
           writer.string_value(presentation.calibration_id) &&
           writer.key("engine_profile_id") &&
           write_resolved(writer, presentation.engine_profile_id, write_string) &&
           writer.key("methods") &&
           write_presentation_methods(writer, presentation.methods) &&
           writer.key("conditioning") &&
           write_conditioning(writer, presentation.conditioning) &&
           writer.key("assets") && write_audio_assets(writer, presentation.assets) &&
           writer.key("routes") &&
           write_route_presentations(writer, presentation.routes) &&
           writer.key("publication") &&
           write_stem_publication(writer, presentation.publication) &&
           writer.key("audition") &&
           write_audition_mix(writer, presentation.audition) &&
           writer.key("provenance_schema_id") &&
           writer.string_value(presentation.provenance_schema_id) &&
           writer.end_object();
}

} // namespace engine_sim_offline::artifacts::detail
