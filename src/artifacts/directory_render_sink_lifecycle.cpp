#include "directory_render_sink_impl.hpp"

#include "directory_render_sink_support.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::artifacts {

DirectoryRenderSink::Implementation::Implementation(
    std::filesystem::path publication_root, std::string publication_name,
    std::string manifest_relative_path, RenderManifestEncoder manifest_encoder)
    : publication_root_(std::move(publication_root)),
      publication_name_(std::move(publication_name)),
      manifest_relative_path_(std::move(manifest_relative_path)),
      manifest_digest_relative_path_(manifest_relative_path_ + ".sha256"),
      manifest_encoder_(std::move(manifest_encoder)) {}

DirectoryRenderSink::Implementation::~Implementation() {
    abort();
}

RenderSinkStatus DirectoryRenderSink::Implementation::begin_transaction(
    const contract::OutputContract &output_contract) {
    if (state_ != DirectoryRenderSinkState::idle) {
        return detail::protocol_error("sink-begin-invalid-state",
                                      "begin_transaction is allowed only while idle");
    }
    if (!manifest_encoder_) {
        return detail::protocol_error(
            "manifest-encoder-missing",
            "directory publication requires an explicit complete manifest encoder");
    }
    if (!detail::valid_path_component(publication_name_)) {
        return detail::protocol_error(
            "publication-name-invalid",
            "publication name must be one conservative portable path component");
    }
    if (!detail::valid_relative_path(manifest_relative_path_) ||
        !detail::valid_relative_path(manifest_digest_relative_path_) ||
        detail::portable_paths_conflict(manifest_relative_path_,
                                        manifest_digest_relative_path_)) {
        return detail::protocol_error(
            "manifest-path-invalid",
            "manifest and manifest-digest paths must be distinct conservative "
            "portable relative paths without file/directory conflicts");
    }
    if (!contract::is_valid_semantic_id(output_contract.source_matrix_id) ||
        output_contract.source_matrix_sha256.is_zero() ||
        output_contract.distribution == contract::DistributionIntent::unspecified ||
        output_contract.required_artifacts.empty()) {
        return detail::protocol_error(
            "output-contract-invalid",
            "output contract lacks a valid matrix identity, distribution, or "
            "required artifact set");
    }

    std::unordered_set<std::string> required_roles;
    for (const auto &requirement : output_contract.required_artifacts) {
        if (!contract::is_valid_semantic_id(requirement.role) ||
            !required_roles.insert(requirement.role).second ||
            !detail::valid_artifact_media(requirement.kind, requirement.audio)) {
            return detail::protocol_error(
                "output-contract-artifacts-invalid",
                "required artifact identities and media must be valid and unique");
        }
    }

    auto prepared_output_contract = output_contract;
    std::unordered_map<std::string, contract::ArtifactRequirement>
        prepared_requirements;
    prepared_requirements.reserve(output_contract.required_artifacts.size());
    for (const auto &requirement : output_contract.required_artifacts) {
        prepared_requirements.emplace(requirement.role, requirement);
    }
    std::unordered_set<std::string> prepared_paths;
    prepared_paths.reserve(output_contract.required_artifacts.size() + 2);
    prepared_paths.insert(manifest_relative_path_);
    prepared_paths.insert(manifest_digest_relative_path_);

#if defined(__linux__)
    root_fd_.reset(::open(publication_root_.c_str(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root_fd_.valid()) {
        return detail::publication_error(
            "publication-root-open-failed",
            detail::errno_message(
                "publication root is unavailable, not a directory, or a symlink",
                errno));
    }

    struct stat destination_status{};
    if (::fstatat(root_fd_.get(), publication_name_.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        root_fd_.reset();
        return detail::publication_error(
            "publication-destination-exists",
            "publication destination already exists and will not be overwritten");
    }
    if (errno != ENOENT) {
        const auto error_number = errno;
        root_fd_.reset();
        return detail::publication_error(
            "publication-destination-check-failed",
            detail::errno_message("could not inspect publication destination",
                                  error_number));
    }

    bool created = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        staging_name_ = detail::random_stage_name();
        if (::mkdirat(root_fd_.get(), staging_name_.c_str(), 0700) == 0) {
            created = true;
            break;
        }
        if (errno != EEXIST) {
            const auto error_number = errno;
            root_fd_.reset();
            staging_name_.clear();
            return detail::publication_error(
                "staging-directory-create-failed",
                detail::errno_message("could not create private staging directory",
                                      error_number));
        }
    }
    if (!created) {
        root_fd_.reset();
        staging_name_.clear();
        return detail::publication_error(
            "staging-name-exhausted",
            "could not allocate a unique private staging directory");
    }

    stage_fd_.reset(::openat(root_fd_.get(), staging_name_.c_str(),
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat stage_status{};
    if (!stage_fd_.valid() || ::fstat(stage_fd_.get(), &stage_status) == -1 ||
        !S_ISDIR(stage_status.st_mode)) {
        const auto error_number = errno;
        // Only remove an empty entry here: without a successfully opened descriptor
        // there is no inode identity that would authorize recursive cleanup.
        static_cast<void>(
            ::unlinkat(root_fd_.get(), staging_name_.c_str(), AT_REMOVEDIR));
        root_fd_.reset();
        stage_fd_.reset();
        staging_name_.clear();
        return detail::publication_error(
            "staging-directory-open-failed",
            detail::errno_message("could not open private staging directory",
                                  error_number));
    }
    stage_device_ = static_cast<std::uintmax_t>(stage_status.st_dev);
    stage_inode_ = static_cast<std::uintmax_t>(stage_status.st_ino);
    state_ = DirectoryRenderSinkState::begun;
    try {
        output_contract_.emplace(std::move(prepared_output_contract));
        required_artifacts_ = std::move(prepared_requirements);
        reserved_paths_ = std::move(prepared_paths);
    } catch (...) {
        abort();
        throw;
    }
    return std::nullopt;
#else
    static_cast<void>(output_contract);
    static_cast<void>(prepared_output_contract);
    static_cast<void>(prepared_requirements);
    static_cast<void>(prepared_paths);
    return detail::publication_error(
        "atomic-noreplace-unavailable",
        "this platform has no implemented atomic directory publish primitive");
#endif
}

RenderSinkStatus
DirectoryRenderSink::Implementation::declare_artifact(const PendingArtifact &artifact) {
    if (const auto state_error = require_healthy_begun("declare_artifact")) {
        return state_error;
    }
    if (!contract::is_valid_semantic_id(artifact.role) ||
        !detail::valid_relative_path(artifact.relative_path) ||
        !detail::valid_artifact_media(artifact.kind, artifact.audio)) {
        return poison(detail::protocol_error(
            "artifact-declaration-invalid",
            "artifact role, relative path, kind, and media must be valid"));
    }
    if (artifacts_.contains(artifact.role)) {
        return poison(detail::protocol_error(
            "artifact-role-duplicate",
            "an artifact role may be declared exactly once per transaction"));
    }

    const auto required = required_artifacts_.find(artifact.role);
    if (required == required_artifacts_.end()) {
        if (!artifact.diagnostic) {
            return poison(detail::protocol_error(
                "undeclared-artifact-not-diagnostic",
                "an artifact outside the output contract must be diagnostic"));
        }
    } else if (artifact.kind != required->second.kind ||
               artifact.audio != required->second.audio ||
               artifact.diagnostic != required->second.diagnostic) {
        return poison(detail::protocol_error(
            "artifact-requirement-mismatch",
            "artifact declaration does not exactly match its output requirement"));
    }

    if (std::ranges::any_of(reserved_paths_, [&](const auto &existing) {
            return detail::portable_paths_conflict(existing, artifact.relative_path);
        })) {
        return poison(detail::protocol_error(
            "artifact-path-duplicate",
            "artifact and metadata paths must have portable case-stable components "
            "and no file/directory prefix conflicts"));
    }
    reserved_paths_.insert(artifact.relative_path);

#if defined(__linux__)
    auto created = detail::create_file_beneath(stage_fd_.get(), artifact.relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&created)) {
        reserved_paths_.erase(artifact.relative_path);
        return poison(std::move(*error));
    }
    ArtifactState state;
    state.pending = artifact;
    state.file = std::move(std::get<detail::FileDescriptor>(created));
    artifacts_.emplace(artifact.role, std::move(state));
    return std::nullopt;
#else
    static_cast<void>(artifact);
    reserved_paths_.erase(artifact.relative_path);
    return poison(detail::publication_error(
        "atomic-noreplace-unavailable",
        "this platform has no implemented transactional directory sink"));
#endif
}

RenderSinkStatus
DirectoryRenderSink::Implementation::write_artifact_chunk(const ArtifactChunk &chunk) {
    if (const auto state_error = require_healthy_begun("write_artifact_chunk")) {
        return state_error;
    }
    const auto found = artifacts_.find(std::string(chunk.role));
    if (found == artifacts_.end()) {
        return poison(detail::protocol_error(
            "artifact-role-undeclared",
            "artifact chunks require a previously declared role"));
    }
    auto &artifact = found->second;
    if (artifact.sealed) {
        return poison(detail::protocol_error(
            "artifact-already-sealed",
            "a sealed artifact cannot accept additional chunks"));
    }
    if (chunk.byte_offset != artifact.byte_count) {
        return poison(detail::protocol_error(
            "artifact-offset-noncontiguous",
            "artifact chunk offsets must begin at zero and remain contiguous"));
    }
    if (chunk.bytes.size() >
        std::numeric_limits<std::uint64_t>::max() - artifact.byte_count) {
        return poison(
            detail::protocol_error("artifact-byte-count-overflow",
                                   "artifact byte count exceeds the uint64 contract"));
    }
#if defined(__linux__)
    if (!detail::write_all_at(artifact.file.get(), artifact.byte_count, chunk.bytes)) {
        return poison(detail::publication_error(
            "artifact-write-failed",
            detail::errno_message("could not write a complete artifact chunk", errno)));
    }
    artifact.hash.update(chunk.bytes);
    artifact.byte_count += static_cast<std::uint64_t>(chunk.bytes.size());
    return std::nullopt;
#else
    static_cast<void>(chunk);
    return poison(detail::publication_error(
        "atomic-noreplace-unavailable",
        "this platform has no implemented transactional directory sink"));
#endif
}

RenderSinkStatus DirectoryRenderSink::Implementation::seal_artifact(
    const contract::ArtifactRecord &record) {
    if (const auto state_error = require_healthy_begun("seal_artifact")) {
        return state_error;
    }
    const auto found = artifacts_.find(record.role);
    if (found == artifacts_.end()) {
        return poison(detail::protocol_error(
            "artifact-role-undeclared",
            "sealing requires a previously declared artifact role"));
    }
    auto &artifact = found->second;
    if (artifact.sealed) {
        return poison(detail::protocol_error("artifact-already-sealed",
                                             "an artifact may be sealed exactly once"));
    }
    if (record.role != artifact.pending.role || record.kind != artifact.pending.kind ||
        record.relative_path != artifact.pending.relative_path ||
        record.audio != artifact.pending.audio ||
        record.diagnostic != artifact.pending.diagnostic) {
        return poison(detail::protocol_error(
            "artifact-record-mismatch",
            "sealed artifact identity, path, and media must match its declaration"));
    }
    if (record.byte_count == 0 || record.byte_count != artifact.byte_count) {
        return poison(detail::protocol_error(
            "artifact-byte-count-mismatch",
            "sealed artifact byte count must be nonzero and match written bytes"));
    }
    const auto actual_digest = artifact.hash.finish();
    if (record.payload_sha256.is_zero() || record.payload_sha256 != actual_digest) {
        return poison(detail::protocol_error(
            "artifact-digest-mismatch",
            "sealed artifact SHA-256 must match the exact staged payload"));
    }

#if defined(__linux__)
    struct stat status{};
    if (::fstat(artifact.file.get(), &status) == -1 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != artifact.byte_count) {
        return poison(detail::publication_error(
            "artifact-file-verification-failed",
            "staged artifact is not one private regular file of the declared size"));
    }
    if (::fsync(artifact.file.get()) == -1) {
        return poison(detail::publication_error(
            "artifact-sync-failed",
            detail::errno_message("could not synchronize staged artifact", errno)));
    }
    artifact.identity = StagedFileIdentity{
        artifact.pending.relative_path,
        artifact.byte_count,
        record.payload_sha256,
        false,
        static_cast<std::uintmax_t>(status.st_dev),
        static_cast<std::uintmax_t>(status.st_ino),
    };
    artifact.file.reset();
#endif
    artifact.record = record;
    artifact.sealed = true;
    return std::nullopt;
}

} // namespace engine_sim_offline::artifacts
