#include "p18_reference_artifact_set_impl.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::reference {

using p18_artifact_set_detail::protocol_error;
using p18_artifact_set_detail::publication_error;

#if defined(__linux__)

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::verify_file(const FileIdentity &identity) {
    auto opened =
        artifacts::detail::open_file_beneath(stage_fd_.get(), identity.relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&opened)) {
        return std::move(*error);
    }
    auto file = std::move(std::get<artifacts::detail::FileDescriptor>(opened));
    struct stat before{};
    if (::fstat(file.get(), &before) == -1 || !S_ISREG(before.st_mode) ||
        before.st_nlink != 1 || before.st_size < 0 ||
        static_cast<std::uint64_t>(before.st_size) != identity.byte_count ||
        static_cast<std::uintmax_t>(before.st_dev) != identity.device ||
        static_cast<std::uintmax_t>(before.st_ino) != identity.inode) {
        return publication_error(
            "p18-staged-file-identity-mismatch",
            "a sealed P1.8 file was replaced or changed before publication");
    }

    artifacts::detail::Sha256Stream hash;
    std::array<std::byte, 64U * 1024U> buffer{};
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
        return publication_error(
            "p18-staged-file-read-failed",
            artifacts::detail::errno_message(
                "could not re-read a sealed P1.8 file before publication",
                count == 0 ? EIO : errno));
    }

    struct stat after{};
    if (::fstat(file.get(), &after) == -1 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        return publication_error(
            "p18-staged-file-state-changed",
            "a sealed P1.8 file changed while it was being verified");
    }
    if (hash.finish() != identity.payload_sha256) {
        return publication_error(
            "p18-staged-file-digest-mismatch",
            "a sealed P1.8 file payload changed before atomic publication");
    }
    verified_files_.push_back(std::move(file));
    return std::nullopt;
}

RenderSinkStatus P18ReferenceArtifactSet::Implementation::verify_exact_inventory() {
    std::unordered_set<std::string> expected_files;
    std::unordered_set<std::string> expected_directories;
    const auto expect_file = [&](std::string_view path) {
        expected_files.emplace(path);
        const auto components = artifacts::detail::path_components(path);
        std::string prefix;
        for (std::size_t index = 0; index + 1 < components.size(); ++index) {
            if (!prefix.empty()) {
                prefix.push_back('/');
            }
            prefix.append(components[index]);
            expected_directories.insert(prefix);
        }
    };
    for (const auto &audio : audio_) {
        if (audio.identity.has_value()) {
            expect_file(audio.identity->relative_path);
        }
    }
    for (const auto &report : reports_) {
        expect_file(report.relative_path);
    }

    const auto expected_count = expected_files.size() + expected_directories.size();
    auto inventory =
        artifacts::detail::inventory_directory_tree(stage_fd_.get(), expected_count);
    if (auto *error = std::get_if<RenderSinkError>(&inventory)) {
        return std::move(*error);
    }
    auto &entries =
        std::get<std::vector<artifacts::detail::DirectoryTreeEntry>>(inventory);
    if (entries.size() != expected_count) {
        return publication_error(
            "p18-staging-inventory-mismatch",
            "P1.8 staging does not contain exactly the sealed audio and reports");
    }
    for (const auto &entry : entries) {
        auto &expected = entry.directory ? expected_directories : expected_files;
        if (expected.erase(entry.relative_path) != 1) {
            return publication_error(
                "p18-staging-inventory-mismatch",
                "P1.8 staging contains an undeclared or incorrectly typed entry");
        }
    }
    if (!expected_files.empty() || !expected_directories.empty()) {
        return publication_error(
            "p18-staging-inventory-mismatch",
            "P1.8 staging is missing a sealed file or parent directory");
    }
    return std::nullopt;
}

RenderSinkStatus P18ReferenceArtifactSet::Implementation::verify_staging_identity() {
    struct stat held{};
    struct stat named{};
    if (::fstat(stage_fd_.get(), &held) == -1 ||
        ::fstatat(root_fd_.get(), staging_name_.c_str(), &named, AT_SYMLINK_NOFOLLOW) ==
            -1 ||
        !S_ISDIR(held.st_mode) || !S_ISDIR(named.st_mode) ||
        static_cast<std::uintmax_t>(held.st_dev) != stage_device_ ||
        static_cast<std::uintmax_t>(held.st_ino) != stage_inode_ ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
        return publication_error(
            "p18-staging-directory-identity-mismatch",
            "the named P1.8 staging directory was replaced or moved");
    }
    return std::nullopt;
}

#endif

RenderSinkStatus P18ReferenceArtifactSet::Implementation::publish() {
    if (auto error = require_writable("publish")) {
        return error;
    }
    if (!all_audio_sealed()) {
        return terminal_failure(protocol_error(
            "p18-required-audio-incomplete",
            "all eight P1.8 audio files must be complete and sealed before publish"));
    }

#if defined(__linux__)
    if (!artifacts::detail::sync_directory_tree(stage_fd_.get())) {
        return terminal_failure(publication_error(
            "p18-staging-tree-sync-failed",
            artifacts::detail::errno_message(
                "could not synchronize the complete P1.8 staging tree", errno)));
    }
    if (::fsync(root_fd_.get()) == -1) {
        return terminal_failure(publication_error(
            "p18-publication-root-sync-failed",
            artifacts::detail::errno_message(
                "could not synchronize the P1.8 publication root", errno)));
    }
    for (const auto &audio : audio_) {
        if (!audio.identity.has_value()) {
            return terminal_failure(protocol_error(
                "p18-sealed-audio-identity-missing",
                "a sealed P1.8 audio file lacks its filesystem identity"));
        }
        if (auto error = verify_file(*audio.identity)) {
            return terminal_failure(std::move(*error));
        }
    }
    for (const auto &report : reports_) {
        if (auto error = verify_file(report)) {
            return terminal_failure(std::move(*error));
        }
    }
    if (auto error = verify_exact_inventory()) {
        return terminal_failure(std::move(*error));
    }
    if (auto error = verify_staging_identity()) {
        return terminal_failure(std::move(*error));
    }
    if (artifacts::detail::rename_noreplace(root_fd_.get(), staging_name_.c_str(),
                                            publication_name_.c_str()) == -1) {
        const auto error_number = errno;
        return terminal_failure(publication_error(
            error_number == EEXIST ? "p18-publication-destination-exists"
                                   : "p18-atomic-publication-failed",
            artifacts::detail::errno_message(
                "could not atomically publish P1.8 output without replacement",
                error_number)));
    }

    const auto root_sync_result = ::fsync(root_fd_.get());
    const auto root_sync_error = root_sync_result == -1 ? errno : 0;
    verified_files_.clear();
    stage_fd_.reset();
    owns_stage_ = false;
    begun_ = false;
    staging_name_.clear();
    state_ = P18ReferenceArtifactSetState::published;
    poisoned_ = false;
    root_fd_.reset();
    if (root_sync_result == -1) {
        auto error = publication_error(
            "p18-published-root-sync-failed",
            artifacts::detail::errno_message(
                "P1.8 output was atomically published, but publication-root "
                "durability could not be confirmed",
                root_sync_error));
        last_error_ = error;
        return error;
    }
    return std::nullopt;
#else
    return terminal_failure(publication_error(
        "p18-atomic-noreplace-unavailable",
        "P1.8 reference publication is currently supported only on Linux"));
#endif
}

} // namespace engine_sim_offline::reference
