#include "p18_reference_artifact_set_impl.hpp"

#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace engine_sim_offline::reference {

std::span<const P18ReferenceAudioArtifactDescription, kP18ReferenceAudioArtifactCount>
p18_reference_audio_artifacts() noexcept {
    return p18_artifact_set_detail::kAudioArtifacts;
}

P18ReferenceArtifactSet::P18ReferenceArtifactSet(std::filesystem::path publication_root,
                                                 std::string publication_name)
    : implementation_(std::make_unique<Implementation>(std::move(publication_root),
                                                       std::move(publication_name))) {}

P18ReferenceArtifactSet::~P18ReferenceArtifactSet() = default;

P18ReferenceArtifactSet::CreateResult
P18ReferenceArtifactSet::create(std::filesystem::path publication_root,
                                std::string publication_name) {
    auto result = std::unique_ptr<P18ReferenceArtifactSet>(new P18ReferenceArtifactSet(
        std::move(publication_root), std::move(publication_name)));
    if (auto error = result->implementation_->begin()) {
        auto retained = std::move(*error);
        result->implementation_->abort();
        return retained;
    }
    return result;
}

artifacts::WavChunkConsumer
P18ReferenceArtifactSet::consumer(P18ReferenceAudioArtifact artifact) {
    return
        [this, artifact](std::uint64_t byte_offset, std::span<const std::byte> bytes) {
            return implementation_->write(artifact, byte_offset, bytes);
        };
}

RenderSinkStatus P18ReferenceArtifactSet::seal(P18ReferenceAudioArtifact artifact) {
    return implementation_->seal(artifact);
}

RenderSinkStatus P18ReferenceArtifactSet::write_text_report(std::string relative_path,
                                                            std::string_view contents) {
    return implementation_->write_text_report(std::move(relative_path), contents);
}

RenderSinkStatus P18ReferenceArtifactSet::publish() {
    return implementation_->publish();
}

void P18ReferenceArtifactSet::abort() noexcept {
    implementation_->abort();
}

P18ReferenceArtifactSetState P18ReferenceArtifactSet::state() const noexcept {
    return implementation_->state();
}

std::filesystem::path P18ReferenceArtifactSet::publication_path() const {
    return implementation_->publication_path();
}

std::optional<std::filesystem::path> P18ReferenceArtifactSet::staging_path() const {
    return implementation_->staging_path();
}

std::optional<P18ReferenceArtifactRecord>
P18ReferenceArtifactSet::record(P18ReferenceAudioArtifact artifact) const noexcept {
    return implementation_->record(artifact);
}

std::optional<RenderSinkError> P18ReferenceArtifactSet::last_error() const {
    return implementation_->last_error();
}

} // namespace engine_sim_offline::reference
