#pragma once

#include "engine_sim_offline/render.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::artifacts {

// Manifest serialization is deliberately an explicit dependency. The directory sink
// owns transactional publication, not the manifest wire format. A production encoder
// must serialize the complete RenderManifest deterministically; returning a summary,
// digest-only record, or host-ABI byte dump violates this interface contract.
struct ManifestEncoding {
    std::vector<std::byte> bytes;
};

using ManifestEncodingResult = std::variant<ManifestEncoding, RenderSinkError>;
using RenderManifestEncoder =
    std::function<ManifestEncodingResult(const contract::RenderManifest &manifest)>;

enum class DirectoryRenderSinkState {
    idle,
    begun,
    committed,
    aborted,
};

// Publishes one render as publication_root/publication_name. The publication name is
// one portable path component. Artifacts and the encoded manifest are first written
// beneath a private sibling staging directory. On Linux, commit uses renameat2 with
// RENAME_NOREPLACE so the complete directory becomes visible atomically and an
// existing destination is never replaced. Platforms without an implemented atomic
// no-replace primitive fail closed at begin_transaction.
//
// The caller owns manifest encoding and supplies its normalized relative path.
// DirectoryRenderSink adds "<manifest_relative_path>.sha256", containing the lowercase
// SHA-256 of the exact encoded bytes. Both metadata paths are reserved and cannot be
// used by artifacts.
class DirectoryRenderSink final : public RenderSink {
  public:
    DirectoryRenderSink(std::filesystem::path publication_root,
                        std::string publication_name,
                        std::string manifest_relative_path,
                        RenderManifestEncoder manifest_encoder);
    ~DirectoryRenderSink() override;

    DirectoryRenderSink(const DirectoryRenderSink &) = delete;
    DirectoryRenderSink &operator=(const DirectoryRenderSink &) = delete;
    DirectoryRenderSink(DirectoryRenderSink &&) = delete;
    DirectoryRenderSink &operator=(DirectoryRenderSink &&) = delete;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &output_contract) override;
    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override;
    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override;
    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override;
    [[nodiscard]] RenderSinkStatus
    commit(const contract::RenderManifest &manifest) override;
    void abort() noexcept override;

    [[nodiscard]] DirectoryRenderSinkState state() const noexcept;
    [[nodiscard]] std::filesystem::path publication_path() const;
    [[nodiscard]] std::optional<std::filesystem::path> staging_path() const;
    [[nodiscard]] std::optional<contract::Sha256Digest>
    manifest_payload_sha256() const noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::artifacts
