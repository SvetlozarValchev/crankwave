#pragma once

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"

#include "contract/sha256_stream.hpp"
#include "secure_filesystem_support.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine_sim_offline::artifacts {

class DirectoryRenderSink::Implementation {
  public:
    Implementation(std::filesystem::path publication_root, std::string publication_name,
                   std::string manifest_relative_path,
                   RenderManifestEncoder manifest_encoder);
    ~Implementation();

    RenderSinkStatus begin_transaction(const contract::OutputContract &output_contract);
    RenderSinkStatus declare_artifact(const PendingArtifact &artifact);
    RenderSinkStatus write_artifact_chunk(const ArtifactChunk &chunk);
    RenderSinkStatus seal_artifact(const contract::ArtifactRecord &record);
    RenderSinkStatus commit(const contract::RenderManifest &manifest);
    void abort() noexcept;

    [[nodiscard]] DirectoryRenderSinkState state() const noexcept;
    [[nodiscard]] std::filesystem::path publication_path() const;
    [[nodiscard]] std::optional<std::filesystem::path> staging_path() const;
    [[nodiscard]] std::optional<contract::Sha256Digest>
    manifest_payload_sha256() const noexcept;

  private:
    struct StagedFileIdentity {
        std::string relative_path;
        std::uint64_t byte_count = 0;
        contract::Sha256Digest payload_sha256;
        bool transaction_metadata = false;
#if defined(__linux__)
        std::uintmax_t device = 0;
        std::uintmax_t inode = 0;
#endif
    };

    struct ArtifactState {
        PendingArtifact pending;
#if defined(__linux__)
        detail::FileDescriptor file;
#endif
        contract::detail::Sha256Stream hash;
        std::uint64_t byte_count = 0;
        std::optional<contract::ArtifactRecord> record;
        std::optional<StagedFileIdentity> identity;
        bool sealed = false;
    };

    RenderSinkStatus require_healthy_begun(std::string_view operation);
    RenderSinkStatus poison(RenderSinkError error);
    RenderSinkStatus terminal_failure(RenderSinkError error);
    void close_artifact_files() noexcept;
    void clear_transaction() noexcept;

#if defined(__linux__)
    RenderSinkStatus verify_staged_file(const StagedFileIdentity &identity);
    RenderSinkStatus verify_exact_staging_inventory();
    RenderSinkStatus verify_staging_identity();
    RenderSinkStatus write_metadata_file(std::string_view relative_path,
                                         std::span<const std::byte> bytes);
#endif

    std::filesystem::path publication_root_;
    std::string publication_name_;
    std::string manifest_relative_path_;
    std::string manifest_digest_relative_path_;
    RenderManifestEncoder manifest_encoder_;
    DirectoryRenderSinkState state_ = DirectoryRenderSinkState::idle;
    bool poisoned_ = false;
    std::string staging_name_;
    std::optional<contract::OutputContract> output_contract_;
    std::unordered_map<std::string, contract::ArtifactRequirement> required_artifacts_;
    std::unordered_map<std::string, ArtifactState> artifacts_;
    std::unordered_set<std::string> reserved_paths_;
    std::optional<contract::Sha256Digest> manifest_digest_;
#if defined(__linux__)
    detail::FileDescriptor root_fd_;
    detail::FileDescriptor stage_fd_;
    std::uintmax_t stage_device_ = 0;
    std::uintmax_t stage_inode_ = 0;
    std::vector<StagedFileIdentity> metadata_files_;
    std::vector<detail::FileDescriptor> verified_files_;
#endif
};

} // namespace engine_sim_offline::artifacts
