#include "engine_sim_offline/artifacts/directory_render_sink.hpp"

#include "directory_render_sink_impl.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace engine_sim_offline::artifacts {

DirectoryRenderSink::DirectoryRenderSink(std::filesystem::path publication_root,
                                         std::string publication_name,
                                         std::string manifest_relative_path,
                                         RenderManifestEncoder manifest_encoder)
    : implementation_(std::make_unique<Implementation>(
          std::move(publication_root), std::move(publication_name),
          std::move(manifest_relative_path), std::move(manifest_encoder))) {}

DirectoryRenderSink::~DirectoryRenderSink() = default;

RenderSinkStatus DirectoryRenderSink::begin_transaction(
    const contract::OutputContract &output_contract) {
    return implementation_->begin_transaction(output_contract);
}

RenderSinkStatus
DirectoryRenderSink::declare_artifact(const PendingArtifact &artifact) {
    return implementation_->declare_artifact(artifact);
}

RenderSinkStatus DirectoryRenderSink::write_artifact_chunk(const ArtifactChunk &chunk) {
    return implementation_->write_artifact_chunk(chunk);
}

RenderSinkStatus
DirectoryRenderSink::seal_artifact(const contract::ArtifactRecord &record) {
    return implementation_->seal_artifact(record);
}

RenderSinkStatus DirectoryRenderSink::commit(const contract::RenderManifest &manifest) {
    return implementation_->commit(manifest);
}

void DirectoryRenderSink::abort() noexcept {
    implementation_->abort();
}

DirectoryRenderSinkState DirectoryRenderSink::state() const noexcept {
    return implementation_->state();
}

std::filesystem::path DirectoryRenderSink::publication_path() const {
    return implementation_->publication_path();
}

std::optional<std::filesystem::path> DirectoryRenderSink::staging_path() const {
    return implementation_->staging_path();
}

std::optional<contract::Sha256Digest>
DirectoryRenderSink::manifest_payload_sha256() const noexcept {
    return implementation_->manifest_payload_sha256();
}

} // namespace engine_sim_offline::artifacts
