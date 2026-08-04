#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/publication.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kAudioPackageManifestRelativePath =
    "package.json";

struct AudioPackagePublicationFileView {
    std::string_view relative_path;
    std::span<const std::byte> bytes;
};

struct AudioPackageDirectoryPublication {
    std::filesystem::path publication_path;
    contract::Sha256Digest manifest_sha256;
};

using AudioPackageDirectoryPublicationResult =
    std::variant<AudioPackageDirectoryPublication, RenderSinkError>;

// Publishes one already assembled package as a new directory. Payload files are
// written before package.json; the private staging tree then becomes visible in
// one no-replace rename. An existing destination is never overwritten.
[[nodiscard]] AudioPackageDirectoryPublicationResult
publish_audio_package_directory(
    const std::filesystem::path &publication_root,
    std::string_view publication_name,
    std::span<const AudioPackagePublicationFileView> payload_files,
    std::span<const std::byte> package_json) noexcept;

} // namespace engine_sim_offline::artifacts
