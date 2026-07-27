#pragma once

#include "p18_reference_artifact_set.hpp"

#include "../artifacts/directory_render_sink_support.hpp"
#include "../artifacts/secure_filesystem_support.hpp"
#include "../artifacts/sha256_stream.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {
namespace p18_artifact_set_detail {

[[nodiscard]] inline const auto &audio_artifacts() noexcept {
    return p18_reference_catalog_v1().expected_audio;
}

[[nodiscard]] inline std::optional<std::size_t>
artifact_index(P18ReferenceAudioArtifact artifact) noexcept {
    const auto &catalog = p18_reference_catalog_v1();
    if (catalog.find_expected_audio(artifact) == nullptr) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(artifact);
}

inline RenderSinkError protocol_error(std::string detail_code, std::string message) {
    return artifacts::detail::protocol_error(std::move(detail_code),
                                             std::move(message));
}

inline RenderSinkError publication_error(std::string detail_code, std::string message) {
    return artifacts::detail::publication_error(std::move(detail_code),
                                                std::move(message));
}

} // namespace p18_artifact_set_detail

class P18ReferenceArtifactSet::Implementation {
  public:
    Implementation(std::filesystem::path publication_root,
                   std::string publication_name);
    ~Implementation();

    [[nodiscard]] RenderSinkStatus begin();
    [[nodiscard]] bool write(P18ReferenceAudioArtifact artifact,
                             std::uint64_t byte_offset,
                             std::span<const std::byte> bytes);
    [[nodiscard]] RenderSinkStatus seal(P18ReferenceAudioArtifact artifact);
    [[nodiscard]] RenderSinkStatus write_text_report(std::string relative_path,
                                                     std::string_view contents);
    [[nodiscard]] RenderSinkStatus publish();
    void abort() noexcept;

    [[nodiscard]] P18ReferenceArtifactSetState state() const noexcept {
        return state_;
    }

    [[nodiscard]] std::filesystem::path publication_path() const {
        return publication_root_ / publication_name_;
    }

    [[nodiscard]] std::optional<std::filesystem::path> staging_path() const {
        if (state_ != P18ReferenceArtifactSetState::open || !owns_stage_ ||
            staging_name_.empty()) {
            return std::nullopt;
        }
        return publication_root_ / staging_name_;
    }

    [[nodiscard]] std::optional<P18ReferenceArtifactRecord>
    record(P18ReferenceAudioArtifact artifact) const noexcept {
        const auto index = p18_artifact_set_detail::artifact_index(artifact);
        return index.has_value() ? audio_[*index].record : std::nullopt;
    }

    [[nodiscard]] std::optional<RenderSinkError> last_error() const {
        return last_error_;
    }

  private:
    struct FileIdentity {
        std::string relative_path;
        std::uint64_t byte_count = 0;
        contract::Sha256Digest payload_sha256;
#if defined(__linux__)
        std::uintmax_t device = 0;
        std::uintmax_t inode = 0;
#endif
    };

    struct AudioState {
#if defined(__linux__)
        artifacts::detail::FileDescriptor file;
#endif
        artifacts::detail::Sha256Stream hash;
        std::uint64_t byte_count = 0;
        bool sealed = false;
        std::optional<FileIdentity> identity;
        std::optional<P18ReferenceArtifactRecord> record;
    };

    [[nodiscard]] bool all_audio_sealed() const noexcept {
        return std::ranges::all_of(
            audio_, [](const AudioState &audio) { return audio.sealed; });
    }

    [[nodiscard]] RenderSinkStatus require_writable(std::string_view operation);
    [[nodiscard]] RenderSinkStatus poison(RenderSinkError error);
    [[nodiscard]] RenderSinkStatus terminal_failure(RenderSinkError error);

#if defined(__linux__)
    [[nodiscard]] RenderSinkStatus verify_file(const FileIdentity &identity);
    [[nodiscard]] RenderSinkStatus verify_exact_inventory();
    [[nodiscard]] RenderSinkStatus verify_staging_identity();
#endif

    std::filesystem::path publication_root_;
    std::string publication_name_;
    P18ReferenceArtifactSetState state_ = P18ReferenceArtifactSetState::open;
    bool begun_ = false;
    bool poisoned_ = false;
    bool owns_stage_ = false;
    std::optional<RenderSinkError> last_error_;
    std::string staging_name_;
    std::array<AudioState, kP18ReferenceAudioArtifactCount> audio_{};
    std::vector<FileIdentity> reports_;
    std::unordered_set<std::string> reserved_paths_;
#if defined(__linux__)
    artifacts::detail::FileDescriptor root_fd_;
    artifacts::detail::FileDescriptor stage_fd_;
    std::uintmax_t stage_device_ = 0;
    std::uintmax_t stage_inode_ = 0;
    std::vector<artifacts::detail::FileDescriptor> verified_files_;
#endif
};

} // namespace engine_sim_offline::reference
