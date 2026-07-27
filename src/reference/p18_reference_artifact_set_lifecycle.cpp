#include "p18_reference_artifact_set_impl.hpp"

#include <cerrno>
#include <string>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::reference {

using p18_artifact_set_detail::protocol_error;
using p18_artifact_set_detail::publication_error;

P18ReferenceArtifactSet::Implementation::Implementation(
    std::filesystem::path publication_root, std::string publication_name)
    : publication_root_(std::move(publication_root)),
      publication_name_(std::move(publication_name)) {}

P18ReferenceArtifactSet::Implementation::~Implementation() {
    abort();
}

RenderSinkStatus P18ReferenceArtifactSet::Implementation::begin() {
    if (!artifacts::detail::valid_path_component(publication_name_)) {
        return protocol_error(
            "p18-publication-name-invalid",
            "P1.8 publication name must be one conservative portable path component");
    }

    const auto &catalog = p18_reference_catalog_v1();
    for (const auto &description : catalog.expected_audio) {
        if (catalog.find_expected_audio(description.audio) != &description) {
            return protocol_error(
                "p18-audio-catalog-invalid",
                "P1.8 audio catalog is not exhaustive canonical enum order");
        }
    }

#if defined(__linux__)
    root_fd_.reset(::open(publication_root_.c_str(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root_fd_.valid()) {
        return publication_error(
            "p18-publication-root-open-failed",
            artifacts::detail::errno_message(
                "P1.8 publication root is unavailable, not a directory, or a symlink",
                errno));
    }

    struct stat destination_status{};
    if (::fstatat(root_fd_.get(), publication_name_.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        return publication_error(
            "p18-publication-destination-exists",
            "P1.8 publication destination already exists and will not be overwritten");
    }
    if (errno != ENOENT) {
        return publication_error(
            "p18-publication-destination-check-failed",
            artifacts::detail::errno_message(
                "could not inspect P1.8 publication destination", errno));
    }

    bool created = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        staging_name_ = artifacts::detail::random_stage_name();
        if (::mkdirat(root_fd_.get(), staging_name_.c_str(), 0700) == 0) {
            created = true;
            break;
        }
        if (errno != EEXIST) {
            return publication_error(
                "p18-staging-directory-create-failed",
                artifacts::detail::errno_message(
                    "could not create private P1.8 staging directory", errno));
        }
    }
    if (!created) {
        return publication_error(
            "p18-staging-name-exhausted",
            "could not allocate a unique private P1.8 staging directory");
    }

    stage_fd_.reset(::openat(root_fd_.get(), staging_name_.c_str(),
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat stage_status{};
    if (!stage_fd_.valid() || ::fstat(stage_fd_.get(), &stage_status) == -1 ||
        !S_ISDIR(stage_status.st_mode)) {
        const auto error_number = errno;
        static_cast<void>(
            ::unlinkat(root_fd_.get(), staging_name_.c_str(), AT_REMOVEDIR));
        stage_fd_.reset();
        staging_name_.clear();
        return publication_error(
            "p18-staging-directory-open-failed",
            artifacts::detail::errno_message(
                "could not open private P1.8 staging directory", error_number));
    }
    stage_device_ = static_cast<std::uintmax_t>(stage_status.st_dev);
    stage_inode_ = static_cast<std::uintmax_t>(stage_status.st_ino);
    owns_stage_ = true;

    const auto &audio_artifacts = catalog.expected_audio;
    for (std::size_t index = 0; index < audio_artifacts.size(); ++index) {
        const auto &description = audio_artifacts[index];
        reserved_paths_.emplace(description.expected_relative_path);
        auto created_file = artifacts::detail::create_file_beneath(
            stage_fd_.get(), description.expected_relative_path);
        if (auto *error = std::get_if<RenderSinkError>(&created_file)) {
            return std::move(*error);
        }
        audio_[index].file =
            std::move(std::get<artifacts::detail::FileDescriptor>(created_file));
    }
    begun_ = true;
    return std::nullopt;
#else
    return publication_error(
        "p18-atomic-noreplace-unavailable",
        "P1.8 reference publication is currently supported only on Linux");
#endif
}

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::require_writable(std::string_view operation) {
    if (state_ != P18ReferenceArtifactSetState::open || !begun_) {
        return protocol_error(
            "p18-artifact-set-invalid-state",
            std::string(operation) +
                " is allowed only during an open P1.8 artifact transaction");
    }
    if (poisoned_) {
        if (last_error_.has_value()) {
            return last_error_;
        }
        return protocol_error(
            "p18-artifact-set-poisoned",
            "the P1.8 artifact transaction must be aborted after a failed operation");
    }
    return std::nullopt;
}

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::poison(RenderSinkError error) {
    poisoned_ = true;
    if (!last_error_.has_value()) {
        last_error_ = error;
    }
    return error;
}

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::terminal_failure(RenderSinkError error) {
#if defined(__linux__)
    verified_files_.clear();
    for (auto &audio : audio_) {
        audio.file.reset();
    }
    stage_fd_.reset();
    bool cleanup_succeeded = true;
    if (owns_stage_ && root_fd_.valid()) {
        cleanup_succeeded = artifacts::detail::remove_tree_entry_by_identity(
            root_fd_.get(), stage_device_, stage_inode_);
    }
    if (!cleanup_succeeded) {
        error = publication_error(
            "p18-staging-cleanup-failed",
            "P1.8 publication failed (" + error.detail_code +
                ") and its private staging directory could not be fully removed");
    }
    root_fd_.reset();
#endif
    last_error_ = error;
    owns_stage_ = false;
    begun_ = false;
    poisoned_ = false;
    staging_name_.clear();
    state_ = P18ReferenceArtifactSetState::aborted;
    return error;
}

void P18ReferenceArtifactSet::Implementation::abort() noexcept {
    if (state_ == P18ReferenceArtifactSetState::published ||
        state_ == P18ReferenceArtifactSetState::aborted) {
        return;
    }
#if defined(__linux__)
    verified_files_.clear();
    for (auto &audio : audio_) {
        audio.file.reset();
    }
    stage_fd_.reset();
    if (owns_stage_ && root_fd_.valid()) {
        static_cast<void>(artifacts::detail::remove_tree_entry_by_identity(
            root_fd_.get(), stage_device_, stage_inode_));
    }
    root_fd_.reset();
#endif
    owns_stage_ = false;
    begun_ = false;
    staging_name_.clear();
    state_ = P18ReferenceArtifactSetState::aborted;
}

} // namespace engine_sim_offline::reference
