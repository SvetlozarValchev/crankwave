#pragma once

#include "crankwave/atlas_assembly.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace crankwave::artifacts {

struct AudioAtlasDirectoryPublication {
    std::filesystem::path publication_path;

    friend bool operator==(const AudioAtlasDirectoryPublication &,
                           const AudioAtlasDirectoryPublication &) = default;
};

enum class AudioAtlasDirectoryPublicationErrorCode : std::uint8_t {
    invalid_request,
    manifest_rejected,
    payload_mismatch,
    parent_unavailable,
    staging_failure,
    write_failure,
    synchronization_failure,
    destination_exists,
    publication_failure,
    unsupported_platform,
    resource_limit,
    internal_failure,
};

struct AudioAtlasDirectoryPublicationError {
    AudioAtlasDirectoryPublicationErrorCode code =
        AudioAtlasDirectoryPublicationErrorCode::invalid_request;
    std::string detail_code;
    std::string message;

    friend bool operator==(const AudioAtlasDirectoryPublicationError &,
                           const AudioAtlasDirectoryPublicationError &) = default;
};

using AudioAtlasDirectoryPublicationResult =
    std::variant<AudioAtlasDirectoryPublication, AudioAtlasDirectoryPublicationError>;

// Publishes one already assembled atlas beneath an existing canonical parent.
// publication_leaf is one preflighted portable path component. A successful call
// creates exactly one new directory and never replaces an existing publication.
[[nodiscard]] AudioAtlasDirectoryPublicationResult
publish_audio_atlas_directory(const std::filesystem::path &canonical_parent,
                              std::string_view publication_leaf,
                              const AssembledAudioAtlas &atlas) noexcept;

} // namespace crankwave::artifacts
