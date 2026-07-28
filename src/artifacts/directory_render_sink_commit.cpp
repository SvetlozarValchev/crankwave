#include "directory_render_sink_impl.hpp"

#include "directory_render_sink_support.hpp"
#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::artifacts {

RenderSinkStatus
DirectoryRenderSink::Implementation::commit(const contract::RenderManifest &manifest) {
    if (state_ != DirectoryRenderSinkState::begun) {
        return detail::protocol_error(
            "sink-commit-invalid-state",
            "commit is allowed only after a successful begin");
    }
    if (poisoned_) {
        return terminal_failure(detail::protocol_error(
            "sink-transaction-poisoned",
            "a failed declaration, write, or seal requires transaction abort"));
    }

    for (const auto &[role, requirement] : required_artifacts_) {
        static_cast<void>(requirement);
        const auto artifact = artifacts_.find(role);
        if (artifact == artifacts_.end() || !artifact->second.sealed) {
            return terminal_failure(detail::protocol_error(
                "required-artifact-incomplete",
                "every output-contract artifact must be declared, written, and "
                "sealed before commit"));
        }
    }
    if (std::ranges::any_of(artifacts_,
                            [](const auto &entry) { return !entry.second.sealed; })) {
        return terminal_failure(detail::protocol_error(
            "declared-artifact-unsealed",
            "every declared artifact must be sealed before commit"));
    }
    if (manifest.content.output_contract != output_contract_) {
        return terminal_failure(detail::protocol_error(
            "manifest-output-contract-mismatch",
            "commit manifest must retain the exact begun output contract"));
    }
    if (manifest.content.artifacts.size() != artifacts_.size()) {
        return terminal_failure(detail::protocol_error(
            "manifest-artifact-set-mismatch",
            "commit manifest must contain every and only staged artifact record"));
    }

    std::unordered_set<std::string> manifested_roles;
    for (const auto &record : manifest.content.artifacts) {
        const auto artifact = artifacts_.find(record.role);
        if (!manifested_roles.insert(record.role).second ||
            artifact == artifacts_.end() ||
            artifact->second.record != std::optional{record}) {
            return terminal_failure(detail::protocol_error(
                "manifest-artifact-record-mismatch",
                "manifest artifact records must exactly match sealed payloads"));
        }
    }

    ManifestEncodingResult encoded;
    try {
        encoded = encode_simulation_manifest_v5(manifest);
    } catch (const std::exception &error) {
        return terminal_failure(detail::publication_error(
            "manifest-encoding-threw",
            std::string("manifest encoder threw: ") + error.what()));
    } catch (...) {
        return terminal_failure(detail::publication_error(
            "manifest-encoding-threw",
            "manifest encoder threw a non-standard exception"));
    }
    if (auto *error = std::get_if<RenderSinkError>(&encoded)) {
        return terminal_failure(std::move(*error));
    }
    auto &manifest_bytes = std::get<ManifestEncoding>(encoded).bytes;
    if (manifest_bytes.empty()) {
        return terminal_failure(detail::protocol_error(
            "manifest-encoding-empty",
            "the simulation-v5 manifest encoder returned an empty document"));
    }

#if defined(__linux__)
    const auto manifest_digest = contract::sha256(manifest_bytes);
    if (auto error = write_metadata_file(manifest_relative_path_, manifest_bytes)) {
        return terminal_failure(std::move(*error));
    }
    auto digest_text = detail::digest_hex(manifest_digest);
    digest_text.push_back('\n');
    const auto digest_bytes = std::as_bytes(std::span(digest_text));
    if (auto error =
            write_metadata_file(manifest_digest_relative_path_, digest_bytes)) {
        return terminal_failure(std::move(*error));
    }
    if (!detail::sync_directory_tree(stage_fd_.get())) {
        return terminal_failure(detail::publication_error(
            "staging-tree-sync-failed",
            detail::errno_message("could not synchronize the complete staging tree",
                                  errno)));
    }
    if (::fsync(root_fd_.get()) == -1) {
        return terminal_failure(detail::publication_error(
            "publication-root-sync-failed",
            detail::errno_message("could not synchronize publication root", errno)));
    }
    for (const auto &[role, artifact] : artifacts_) {
        static_cast<void>(role);
        if (!artifact.identity.has_value()) {
            return terminal_failure(detail::protocol_error(
                "sealed-artifact-identity-missing",
                "a sealed artifact lacks its staged filesystem identity"));
        }
        if (auto error = verify_staged_file(*artifact.identity)) {
            return terminal_failure(std::move(*error));
        }
    }
    for (const auto &metadata : metadata_files_) {
        if (auto error = verify_staged_file(metadata)) {
            return terminal_failure(std::move(*error));
        }
    }
    if (auto error = verify_exact_staging_inventory()) {
        return terminal_failure(std::move(*error));
    }
    if (auto error = verify_staging_identity()) {
        return terminal_failure(std::move(*error));
    }
    if (detail::rename_noreplace(root_fd_.get(), staging_name_.c_str(),
                                 publication_name_.c_str()) == -1) {
        const auto error_number = errno;
        const auto detail_code = error_number == EEXIST
                                     ? "publication-destination-exists"
                                     : "atomic-publication-failed";
        return terminal_failure(detail::publication_error(
            detail_code,
            detail::errno_message(
                "could not atomically publish without replacing a destination",
                error_number)));
    }

    manifest_digest_ = manifest_digest;
    stage_fd_.reset();
    staging_name_.clear();
    state_ = DirectoryRenderSinkState::committed;
    poisoned_ = false;
    verified_files_.clear();
    metadata_files_.clear();
    artifacts_.clear();
    required_artifacts_.clear();
    reserved_paths_.clear();
    output_contract_.reset();
    static_cast<void>(::fsync(root_fd_.get()));
    root_fd_.reset();
    return std::nullopt;
#else
    return terminal_failure(detail::publication_error(
        "atomic-noreplace-unavailable",
        "this platform has no implemented atomic directory publish primitive"));
#endif
}

void DirectoryRenderSink::Implementation::abort() noexcept {
    if (state_ == DirectoryRenderSinkState::committed ||
        state_ == DirectoryRenderSinkState::aborted) {
        return;
    }
    if (state_ == DirectoryRenderSinkState::idle) {
        state_ = DirectoryRenderSinkState::aborted;
        return;
    }

    close_artifact_files();
#if defined(__linux__)
    stage_fd_.reset();
    if (root_fd_.valid() && !staging_name_.empty()) {
        static_cast<void>(detail::remove_tree_entry_by_identity(
            root_fd_.get(), stage_device_, stage_inode_));
    }
    root_fd_.reset();
#endif
    clear_transaction();
    state_ = DirectoryRenderSinkState::aborted;
}

DirectoryRenderSinkState DirectoryRenderSink::Implementation::state() const noexcept {
    return state_;
}

std::filesystem::path DirectoryRenderSink::Implementation::publication_path() const {
    return publication_root_ / publication_name_;
}

std::optional<std::filesystem::path>
DirectoryRenderSink::Implementation::staging_path() const {
    if (state_ != DirectoryRenderSinkState::begun || staging_name_.empty()) {
        return std::nullopt;
    }
    return publication_root_ / staging_name_;
}

std::optional<contract::Sha256Digest>
DirectoryRenderSink::Implementation::manifest_payload_sha256() const noexcept {
    return manifest_digest_;
}

RenderSinkStatus
DirectoryRenderSink::Implementation::require_healthy_begun(std::string_view operation) {
    if (state_ != DirectoryRenderSinkState::begun) {
        return detail::protocol_error(
            "sink-operation-invalid-state",
            std::string(operation) + " is allowed only during a begun transaction");
    }
    if (poisoned_) {
        return detail::protocol_error(
            "sink-transaction-poisoned",
            "the transaction must be aborted after its first failed operation");
    }
    return std::nullopt;
}

RenderSinkStatus DirectoryRenderSink::Implementation::poison(RenderSinkError error) {
    poisoned_ = true;
    return error;
}

RenderSinkStatus
DirectoryRenderSink::Implementation::terminal_failure(RenderSinkError error) {
    close_artifact_files();
#if defined(__linux__)
    stage_fd_.reset();
    bool cleanup_succeeded = true;
    if (root_fd_.valid() && !staging_name_.empty()) {
        cleanup_succeeded = detail::remove_tree_entry_by_identity(
            root_fd_.get(), stage_device_, stage_inode_);
    }
    if (!cleanup_succeeded) {
        error = detail::publication_error(
            "staging-cleanup-failed",
            "commit failed and its private staging directory could not be fully "
            "removed");
    }
    root_fd_.reset();
#endif
    clear_transaction();
    state_ = DirectoryRenderSinkState::aborted;
    return error;
}

void DirectoryRenderSink::Implementation::close_artifact_files() noexcept {
#if defined(__linux__)
    verified_files_.clear();
    for (auto &[role, artifact] : artifacts_) {
        static_cast<void>(role);
        artifact.file.reset();
    }
#endif
}

void DirectoryRenderSink::Implementation::clear_transaction() noexcept {
    artifacts_.clear();
    required_artifacts_.clear();
    reserved_paths_.clear();
    output_contract_.reset();
#if defined(__linux__)
    metadata_files_.clear();
    stage_device_ = 0;
    stage_inode_ = 0;
#endif
    staging_name_.clear();
    poisoned_ = false;
}

#if defined(__linux__)

RenderSinkStatus DirectoryRenderSink::Implementation::verify_staged_file(
    const StagedFileIdentity &identity) {
    auto opened = detail::open_file_beneath(stage_fd_.get(), identity.relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&opened)) {
        return std::move(*error);
    }
    auto file = std::move(std::get<detail::FileDescriptor>(opened));
    const auto identity_code = identity.transaction_metadata
                                   ? "transaction-metadata-final-identity-mismatch"
                                   : "artifact-final-identity-mismatch";
    const auto read_code = identity.transaction_metadata
                               ? "transaction-metadata-final-read-failed"
                               : "artifact-final-read-failed";
    const auto state_code = identity.transaction_metadata
                                ? "transaction-metadata-final-state-changed"
                                : "artifact-final-state-changed";
    const auto digest_code = identity.transaction_metadata
                                 ? "transaction-metadata-final-digest-mismatch"
                                 : "artifact-final-digest-mismatch";

    struct stat before{};
    if (::fstat(file.get(), &before) == -1 || !S_ISREG(before.st_mode) ||
        before.st_nlink != 1 || before.st_size < 0 ||
        static_cast<std::uint64_t>(before.st_size) != identity.byte_count ||
        static_cast<std::uintmax_t>(before.st_dev) != identity.device ||
        static_cast<std::uintmax_t>(before.st_ino) != identity.inode) {
        return detail::publication_error(
            identity_code,
            "a sealed staged file was replaced or changed before commit");
    }

    contract::detail::Sha256Stream hash;
    std::array<std::byte, 64 * 1024> buffer{};
    std::uint64_t offset = 0;
    while (offset < identity.byte_count) {
        const auto request = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), identity.byte_count - offset));
        const auto count =
            ::pread(file.get(), buffer.data(), request, static_cast<off_t>(offset));
        if (count > 0) {
            hash.update(std::span(buffer).first(static_cast<std::size_t>(count)));
            offset += static_cast<std::uint64_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        return detail::publication_error(
            read_code, detail::errno_message(
                           "could not re-read a sealed staged file before commit",
                           count == 0 ? EIO : errno));
    }

    struct stat after{};
    if (::fstat(file.get(), &after) == -1 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        return detail::publication_error(
            state_code, "a sealed staged file changed while being verified");
    }
    if (hash.finish() != identity.payload_sha256) {
        return detail::publication_error(
            digest_code,
            "a sealed staged file payload changed before atomic publication");
    }
    verified_files_.push_back(std::move(file));
    return std::nullopt;
}

RenderSinkStatus DirectoryRenderSink::Implementation::verify_exact_staging_inventory() {
    std::unordered_set<std::string> expected_files;
    std::unordered_set<std::string> expected_directories;
    const auto expect_file = [&](std::string_view path) {
        expected_files.emplace(path);
        const auto components = detail::path_components(path);
        std::string prefix;
        for (std::size_t index = 0; index + 1 < components.size(); ++index) {
            if (!prefix.empty()) {
                prefix.push_back('/');
            }
            prefix.append(components[index]);
            expected_directories.insert(prefix);
        }
    };
    for (const auto &[role, artifact] : artifacts_) {
        static_cast<void>(role);
        if (artifact.identity.has_value()) {
            expect_file(artifact.identity->relative_path);
        }
    }
    for (const auto &metadata : metadata_files_) {
        expect_file(metadata.relative_path);
    }

    const auto expected_count = expected_files.size() + expected_directories.size();
    auto inventory = detail::inventory_directory_tree(stage_fd_.get(), expected_count);
    if (auto *error = std::get_if<RenderSinkError>(&inventory)) {
        return std::move(*error);
    }
    auto &entries = std::get<std::vector<detail::DirectoryTreeEntry>>(inventory);
    if (entries.size() != expected_count) {
        return detail::publication_error(
            "staging-inventory-mismatch",
            "staging tree does not contain exactly the declared artifacts and "
            "transaction metadata (expected " +
                std::to_string(expected_count) + " entries, found " +
                std::to_string(entries.size()) + ")");
    }
    for (const auto &entry : entries) {
        auto &expected = entry.directory ? expected_directories : expected_files;
        if (expected.erase(entry.relative_path) != 1) {
            return detail::publication_error(
                "staging-inventory-mismatch",
                "staging tree contains an undeclared or incorrectly typed entry");
        }
    }
    if (!expected_files.empty() || !expected_directories.empty()) {
        return detail::publication_error(
            "staging-inventory-mismatch",
            "staging tree is missing a declared artifact, metadata file, or "
            "required parent directory");
    }
    return std::nullopt;
}

RenderSinkStatus DirectoryRenderSink::Implementation::verify_staging_identity() {
    struct stat held{};
    struct stat named{};
    if (::fstat(stage_fd_.get(), &held) == -1 ||
        ::fstatat(root_fd_.get(), staging_name_.c_str(), &named, AT_SYMLINK_NOFOLLOW) ==
            -1 ||
        !S_ISDIR(held.st_mode) || !S_ISDIR(named.st_mode) ||
        static_cast<std::uintmax_t>(held.st_dev) != stage_device_ ||
        static_cast<std::uintmax_t>(held.st_ino) != stage_inode_ ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
        return detail::publication_error(
            "staging-directory-identity-mismatch",
            "the named staging directory was replaced or moved before publication");
    }
    return std::nullopt;
}

RenderSinkStatus DirectoryRenderSink::Implementation::write_metadata_file(
    std::string_view relative_path, std::span<const std::byte> bytes) {
    auto created = detail::create_file_beneath(stage_fd_.get(), relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&created)) {
        return std::move(*error);
    }
    auto file = std::move(std::get<detail::FileDescriptor>(created));
    if (!detail::write_all_at(file.get(), 0, bytes)) {
        return detail::publication_error(
            "transaction-metadata-write-failed",
            detail::errno_message("could not write transaction metadata", errno));
    }
    if (::fsync(file.get()) == -1) {
        return detail::publication_error(
            "transaction-metadata-sync-failed",
            detail::errno_message("could not synchronize transaction metadata", errno));
    }
    struct stat status{};
    if (::fstat(file.get(), &status) == -1 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != bytes.size()) {
        return detail::publication_error(
            "transaction-metadata-verification-failed",
            "staged transaction metadata is not one private regular file of the "
            "encoded size");
    }
    metadata_files_.push_back({
        std::string(relative_path),
        static_cast<std::uint64_t>(bytes.size()),
        contract::sha256(bytes),
        true,
        static_cast<std::uintmax_t>(status.st_dev),
        static_cast<std::uintmax_t>(status.st_ino),
    });
    return std::nullopt;
}

#endif

} // namespace engine_sim_offline::artifacts
