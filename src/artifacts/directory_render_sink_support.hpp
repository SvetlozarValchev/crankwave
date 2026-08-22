#pragma once

#include "crankwave/artifacts/directory_render_sink.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crankwave::artifacts::detail {

inline RenderSinkError protocol_error(std::string detail_code, std::string message) {
    return {
        RenderSinkErrorKind::protocol_violation,
        std::move(detail_code),
        std::move(message),
    };
}

inline RenderSinkError publication_error(std::string detail_code, std::string message) {
    return {
        RenderSinkErrorKind::publication_failure,
        std::move(detail_code),
        std::move(message),
    };
}

inline bool valid_artifact_kind(contract::ArtifactKind kind) noexcept {
    switch (kind) {
    case contract::ArtifactKind::audio:
    case contract::ArtifactKind::telemetry:
    case contract::ArtifactKind::routing_report:
    case contract::ArtifactKind::validation_report:
        return true;
    case contract::ArtifactKind::unspecified:
        return false;
    }
    return false;
}

inline bool valid_audio_contract(const contract::AudioContract &audio) {
    return contract::validate(audio.sample_rate).ok() && audio.frame_count > 0 &&
           contract::is_valid_semantic_id(audio.channel_layout_id) &&
           contract::is_valid_semantic_id(audio.sample_encoding_id);
}

inline bool valid_artifact_media(contract::ArtifactKind kind,
                                 const std::optional<contract::AudioContract> &audio) {
    if (!valid_artifact_kind(kind) ||
        (kind == contract::ArtifactKind::audio) != audio.has_value()) {
        return false;
    }
    return !audio.has_value() || valid_audio_contract(*audio);
}

inline bool valid_path_component(std::string_view component) noexcept {
    if (component.empty() || component.size() > 255 || component == "." ||
        component == "..") {
        return false;
    }
    const auto ascii_alphanumeric = [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    if (!ascii_alphanumeric(static_cast<unsigned char>(component.front())) ||
        component.back() == '.') {
        return false;
    }
    for (std::size_t index = 0; index < component.size(); ++index) {
        const auto byte = static_cast<unsigned char>(component[index]);
        if (ascii_alphanumeric(byte) || byte == '-' || byte == '_' || byte == '.') {
            continue;
        }
        if (byte == '%' && index + 2 < component.size() &&
            component[index + 1] == '2' && component[index + 2] == 'f') {
            index += 2;
            continue;
        }
        return false;
    }

    auto stem = component.substr(0, component.find('.'));
    std::string folded_stem;
    folded_stem.reserve(stem.size());
    for (const char character : stem) {
        folded_stem.push_back(character >= 'a' && character <= 'z'
                                  ? static_cast<char>(character - 'a' + 'A')
                                  : character);
    }
    if (folded_stem == "CON" || folded_stem == "PRN" || folded_stem == "AUX" ||
        folded_stem == "NUL") {
        return false;
    }
    if (folded_stem.size() == 4 &&
        (folded_stem.starts_with("COM") || folded_stem.starts_with("LPT")) &&
        folded_stem.back() >= '1' && folded_stem.back() <= '9') {
        return false;
    }
    return true;
}

inline std::vector<std::string_view> path_components(std::string_view path) {
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        if (end == std::string_view::npos) {
            result.push_back(path.substr(begin));
            break;
        }
        result.push_back(path.substr(begin, end - begin));
        begin = end + 1;
    }
    return result;
}

inline std::string portable_path_key(std::string_view path);

inline bool portable_paths_conflict(std::string_view lhs, std::string_view rhs) {
    const auto lhs_components = path_components(lhs);
    const auto rhs_components = path_components(rhs);
    const auto shared = std::min(lhs_components.size(), rhs_components.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const auto lhs_folded = portable_path_key(lhs_components[index]);
        const auto rhs_folded = portable_path_key(rhs_components[index]);
        if (lhs_folded != rhs_folded) {
            return false;
        }
        if (lhs_components[index] != rhs_components[index]) {
            return true;
        }
    }
    return lhs_components.size() == rhs_components.size() ||
           shared == lhs_components.size() || shared == rhs_components.size();
}

inline bool valid_relative_path(std::string_view path) noexcept {
    // Keep enough headroom for the caller-selected publication root on legacy
    // path-length-limited filesystems.
    if (path.empty() || path.size() > 240 || path.front() == '/') {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (!valid_path_component(component)) {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

inline std::string portable_path_key(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    for (const char character : path) {
        if (character >= 'A' && character <= 'Z') {
            result.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            result.push_back(character);
        }
    }
    return result;
}

inline std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.resize(digest.bytes.size() * 2);
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

} // namespace crankwave::artifacts::detail
