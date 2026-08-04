#include "engine_sim_offline/artifacts/audio_package_directory_publisher.hpp"

#include "directory_render_sink_support.hpp"
#include "secure_filesystem_support.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::artifacts {
namespace {

[[nodiscard]] RenderSinkError failure(std::string detail_code,
                                      std::string message) {
    return detail::publication_error(std::move(detail_code),
                                     std::move(message));
}

[[nodiscard]] std::optional<RenderSinkError> validate_request(
    const std::string_view publication_name,
    const std::span<const AudioPackagePublicationFileView> payload_files,
    const std::span<const std::byte> package_json) {
    if (!detail::valid_path_component(publication_name)) {
        return detail::protocol_error(
            "audio-package-publication-name-invalid",
            "publication name must be one conservative portable component");
    }
    if (package_json.empty()) {
        return detail::protocol_error(
            "audio-package-manifest-empty",
            "package.json must be a nonempty already encoded manifest");
    }
    if (payload_files.empty() || payload_files.size() > 4096U) {
        return detail::protocol_error(
            "audio-package-payload-count-invalid",
            "audio package must contain between one and 4096 payload files");
    }

    std::vector<std::string_view> paths;
    paths.reserve(payload_files.size() + 1U);
    paths.push_back(kAudioPackageManifestRelativePath);
    for (const auto &file : payload_files) {
        if (!detail::valid_relative_path(file.relative_path) ||
            file.bytes.empty()) {
            return detail::protocol_error(
                "audio-package-payload-invalid",
                "payload paths must be conservative relative paths with nonempty bytes");
        }
        if (std::ranges::any_of(paths, [&](const auto existing) {
                return detail::portable_paths_conflict(existing,
                                                       file.relative_path);
            })) {
            return detail::protocol_error(
                "audio-package-payload-path-conflict",
                "package payload paths must be unique and may not conflict with package.json");
        }
        paths.push_back(file.relative_path);
    }
    return std::nullopt;
}

#if defined(__linux__)

class StagingTransaction final {
  public:
    StagingTransaction() = default;
    StagingTransaction(const StagingTransaction &) = delete;
    StagingTransaction &operator=(const StagingTransaction &) = delete;
    StagingTransaction(StagingTransaction &&other) noexcept
        : root(std::move(other.root)), stage(std::move(other.stage)),
          name(std::move(other.name)), device(other.device), inode(other.inode),
          committed(other.committed) {
        other.device = 0U;
        other.inode = 0U;
        other.committed = true;
    }
    StagingTransaction &operator=(StagingTransaction &&) = delete;

    ~StagingTransaction() {
        stage.reset();
        if (!committed && root.valid() && !name.empty() && device != 0U &&
            inode != 0U) {
            static_cast<void>(detail::remove_tree_entry_by_identity(
                root.get(), device, inode));
        }
    }

    detail::FileDescriptor root;
    detail::FileDescriptor stage;
    std::string name;
    std::uintmax_t device = 0U;
    std::uintmax_t inode = 0U;
    bool committed = false;
};

[[nodiscard]] std::variant<StagingTransaction, RenderSinkError>
begin_transaction(const std::filesystem::path &publication_root,
                  const std::string_view publication_name) {
    StagingTransaction transaction;
    transaction.root.reset(::open(publication_root.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                      O_NOFOLLOW));
    if (!transaction.root.valid()) {
        return failure(
            "audio-package-publication-root-open-failed",
            detail::errno_message(
                "publication root is unavailable, not a directory, or a symlink",
                errno));
    }

    const std::string destination{publication_name};
    struct stat destination_status {};
    if (::fstatat(transaction.root.get(), destination.c_str(),
                  &destination_status, AT_SYMLINK_NOFOLLOW) == 0) {
        return failure("audio-package-publication-destination-exists",
                       "publication destination already exists and will not be overwritten");
    }
    if (errno != ENOENT) {
        return failure(
            "audio-package-publication-destination-check-failed",
            detail::errno_message("could not inspect publication destination",
                                  errno));
    }

    bool created = false;
    for (unsigned attempt = 0; attempt < 64U; ++attempt) {
        transaction.name = detail::random_stage_name();
        if (::mkdirat(transaction.root.get(), transaction.name.c_str(), 0700) ==
            0) {
            created = true;
            break;
        }
        if (errno != EEXIST) {
            return failure(
                "audio-package-staging-create-failed",
                detail::errno_message("could not create private staging directory",
                                      errno));
        }
    }
    if (!created) {
        return failure("audio-package-staging-name-exhausted",
                       "could not allocate a unique private staging directory");
    }

    transaction.stage.reset(::openat(
        transaction.root.get(), transaction.name.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat stage_status {};
    if (!transaction.stage.valid() ||
        ::fstat(transaction.stage.get(), &stage_status) == -1 ||
        !S_ISDIR(stage_status.st_mode)) {
        const auto error_number = errno;
        static_cast<void>(::unlinkat(transaction.root.get(),
                                     transaction.name.c_str(), AT_REMOVEDIR));
        transaction.name.clear();
        return failure(
            "audio-package-staging-open-failed",
            detail::errno_message("could not open private staging directory",
                                  error_number));
    }
    transaction.device = static_cast<std::uintmax_t>(stage_status.st_dev);
    transaction.inode = static_cast<std::uintmax_t>(stage_status.st_ino);
    return transaction;
}

[[nodiscard]] std::optional<RenderSinkError>
write_staged_file(const int stage_fd, const std::string_view relative_path,
                  const std::span<const std::byte> bytes) {
    auto created = detail::create_file_beneath(stage_fd, relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&created)) {
        return std::move(*error);
    }
    auto file = std::get<detail::FileDescriptor>(std::move(created));
    if (!detail::write_all_at(file.get(), 0U, bytes)) {
        return failure(
            "audio-package-file-write-failed",
            detail::errno_message("could not write a complete package file",
                                  errno));
    }
    if (::fsync(file.get()) == -1) {
        return failure(
            "audio-package-file-sync-failed",
            detail::errno_message("could not synchronize a package file", errno));
    }
    return std::nullopt;
}

#endif

} // namespace

AudioPackageDirectoryPublicationResult publish_audio_package_directory(
    const std::filesystem::path &publication_root,
    const std::string_view publication_name,
    const std::span<const AudioPackagePublicationFileView> payload_files,
    const std::span<const std::byte> package_json) noexcept {
    try {
        if (auto error =
                validate_request(publication_name, payload_files, package_json)) {
            return std::move(*error);
        }
#if !defined(__linux__)
        static_cast<void>(publication_root);
        return failure(
            "atomic-noreplace-unavailable",
            "this platform has no implemented atomic directory publish primitive");
#else
        auto begun = begin_transaction(publication_root, publication_name);
        if (auto *error = std::get_if<RenderSinkError>(&begun)) {
            return std::move(*error);
        }
        auto transaction =
            std::get<StagingTransaction>(std::move(begun));
        for (const auto &file : payload_files) {
            if (auto error = write_staged_file(transaction.stage.get(),
                                               file.relative_path, file.bytes)) {
                return std::move(*error);
            }
        }
        if (auto error = write_staged_file(transaction.stage.get(),
                                           kAudioPackageManifestRelativePath,
                                           package_json)) {
            return std::move(*error);
        }
        if (!detail::sync_directory_tree(transaction.stage.get())) {
            return failure(
                "audio-package-staging-sync-failed",
                detail::errno_message(
                    "could not synchronize the complete package staging tree",
                    errno));
        }

        struct stat current_stage {};
        if (::fstat(transaction.stage.get(), &current_stage) == -1 ||
            !S_ISDIR(current_stage.st_mode) ||
            static_cast<std::uintmax_t>(current_stage.st_dev) !=
                transaction.device ||
            static_cast<std::uintmax_t>(current_stage.st_ino) !=
                transaction.inode) {
            return failure(
                "audio-package-staging-identity-mismatch",
                "private staging directory identity changed before publication");
        }
        const std::string destination{publication_name};
        if (detail::rename_noreplace(transaction.root.get(),
                                     transaction.name.c_str(),
                                     destination.c_str()) == -1) {
            const auto error_number = errno;
            return failure(
                error_number == EEXIST
                    ? "audio-package-publication-destination-exists"
                    : "audio-package-atomic-publication-failed",
                detail::errno_message(
                    "could not atomically publish without replacing a destination",
                    error_number));
        }
        transaction.stage.reset();
        transaction.committed = true;
        transaction.name.clear();
        static_cast<void>(::fsync(transaction.root.get()));
        return AudioPackageDirectoryPublication{
            publication_root / destination, contract::sha256(package_json)};
#endif
    } catch (const std::bad_alloc &) {
        return failure("audio-package-publication-allocation-failed",
                       "allocation failed while publishing the audio package");
    } catch (...) {
        return failure("audio-package-publication-unexpected-failure",
                       "an unexpected failure interrupted audio package publication");
    }
}

} // namespace engine_sim_offline::artifacts
