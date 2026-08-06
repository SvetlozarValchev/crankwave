#include "engine_sim_offline/artifacts/audio_atlas_directory_publisher.hpp"

#include "engine_sim_offline/artifacts/audio_atlas_manifest_encoder.hpp"

#include "secure_filesystem_support.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::artifacts {
namespace {

using ErrorCode = AudioAtlasDirectoryPublicationErrorCode;
using Result = AudioAtlasDirectoryPublicationResult;

[[nodiscard]] Result fail(const ErrorCode code, std::string detail_code,
                          std::string message) {
    return AudioAtlasDirectoryPublicationError{code, std::move(detail_code),
                                               std::move(message)};
}

[[nodiscard]] bool is_single_leaf(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= 255U && value != "." && value != ".." &&
           value.find('/') == std::string_view::npos &&
           value.find('\\') == std::string_view::npos &&
           value.find('\0') == std::string_view::npos;
}

[[nodiscard]] Result map_manifest_error(const RenderSinkError &error) {
    return fail(ErrorCode::manifest_rejected, error.detail_code, error.message);
}

[[nodiscard]] Result map_stage_error(const RenderSinkError &error,
                                     const ErrorCode code) {
    return fail(code, error.detail_code, error.message);
}

[[nodiscard]] Result verify_payloads(const AssembledAudioAtlas &atlas) {
    if (atlas.payloads.size() != atlas.manifest.artifacts.size()) {
        return fail(ErrorCode::payload_mismatch,
                    "audio-atlas-publication-payload-count-mismatch",
                    "assembled payload count does not match manifest artifact count");
    }

    std::unordered_set<std::string> relative_paths;
    relative_paths.reserve(atlas.manifest.artifacts.size());
    for (std::size_t index = 0U; index < atlas.payloads.size(); ++index) {
        const auto &artifact = atlas.manifest.artifacts[index];
        const auto &payload = atlas.payloads[index];
        if (payload.artifact_id != artifact.id) {
            return fail(ErrorCode::payload_mismatch,
                        "audio-atlas-publication-payload-order-mismatch",
                        "assembled payload order or artifact identity does not match "
                        "the manifest");
        }
        if (payload.bytes.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) ||
            static_cast<std::uint64_t>(payload.bytes.size()) !=
                artifact.byte_count) {
            return fail(ErrorCode::payload_mismatch,
                        "audio-atlas-publication-payload-size-mismatch",
                        "assembled payload byte count does not match the manifest");
        }
        if (contract::sha256(payload.bytes) != artifact.sha256) {
            return fail(ErrorCode::payload_mismatch,
                        "audio-atlas-publication-payload-digest-mismatch",
                        "assembled payload SHA-256 does not match the manifest");
        }
        if (artifact.path == "atlas.json" ||
            !relative_paths.insert(artifact.path).second) {
            return fail(ErrorCode::manifest_rejected,
                        "audio-atlas-publication-artifact-path-conflict",
                        "manifest artifact paths must be unique and must not reserve "
                        "atlas.json");
        }
    }
    return AudioAtlasDirectoryPublication{};
}

#if defined(__linux__)

class StagingCleanup final {
  public:
    StagingCleanup(const int parent_fd, const std::uintmax_t device,
                   const std::uintmax_t inode)
        : parent_fd_(parent_fd), device_(device), inode_(inode) {}

    StagingCleanup(const StagingCleanup &) = delete;
    StagingCleanup &operator=(const StagingCleanup &) = delete;

    ~StagingCleanup() {
        if (active_) {
            static_cast<void>(detail::remove_tree_entry_by_identity(
                parent_fd_, device_, inode_));
        }
    }

    void release() noexcept {
        active_ = false;
    }

  private:
    int parent_fd_ = -1;
    std::uintmax_t device_ = 0U;
    std::uintmax_t inode_ = 0U;
    bool active_ = true;
};

[[nodiscard]] Result verify_staging_tree(
    const int parent_fd, const int stage_fd, const std::string &staging_name,
    const std::uintmax_t stage_device, const std::uintmax_t stage_inode,
    const AssembledAudioAtlas &atlas) {
    std::unordered_set<std::string> expected_files{"atlas.json"};
    std::unordered_set<std::string> expected_directories;
    const auto expect_path = [&](const std::string_view path) {
        expected_files.emplace(path);
        std::size_t separator = path.find('/');
        while (separator != std::string_view::npos) {
            expected_directories.emplace(path.substr(0U, separator));
            separator = path.find('/', separator + 1U);
        }
    };
    for (const auto &artifact : atlas.manifest.artifacts) {
        expect_path(artifact.path);
    }

    const auto expected_count = expected_files.size() + expected_directories.size();
    auto inventory = detail::inventory_directory_tree(stage_fd, expected_count);
    if (const auto *error = std::get_if<RenderSinkError>(&inventory)) {
        return map_stage_error(*error, ErrorCode::publication_failure);
    }
    auto entries =
        std::get<std::vector<detail::DirectoryTreeEntry>>(std::move(inventory));
    if (entries.size() != expected_count) {
        return fail(ErrorCode::publication_failure,
                    "audio-atlas-publication-staging-inventory-mismatch",
                    "staging tree does not contain exactly the atlas manifest and "
                    "declared payloads");
    }
    for (const auto &entry : entries) {
        auto &expected = entry.directory ? expected_directories : expected_files;
        if (expected.erase(entry.relative_path) != 1U) {
            return fail(ErrorCode::publication_failure,
                        "audio-atlas-publication-staging-inventory-mismatch",
                        "staging tree contains an undeclared or incorrectly typed "
                        "entry");
        }
    }
    if (!expected_files.empty() || !expected_directories.empty()) {
        return fail(ErrorCode::publication_failure,
                    "audio-atlas-publication-staging-inventory-mismatch",
                    "staging tree is missing a declared atlas file or directory");
    }

    struct stat held {};
    struct stat named {};
    if (::fstat(stage_fd, &held) == -1 ||
        ::fstatat(parent_fd, staging_name.c_str(), &named, AT_SYMLINK_NOFOLLOW) ==
            -1 ||
        !S_ISDIR(held.st_mode) || !S_ISDIR(named.st_mode) ||
        static_cast<std::uintmax_t>(held.st_dev) != stage_device ||
        static_cast<std::uintmax_t>(held.st_ino) != stage_inode ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
        return fail(ErrorCode::publication_failure,
                    "audio-atlas-publication-staging-identity-mismatch",
                    "the named staging directory was replaced or moved before "
                    "publication");
    }
    return AudioAtlasDirectoryPublication{};
}

[[nodiscard]] Result write_staged_file(const int stage_fd,
                                       const std::string_view relative_path,
                                       const std::span<const std::byte> bytes) {
    auto created = detail::create_file_beneath(stage_fd, relative_path);
    if (const auto *error = std::get_if<RenderSinkError>(&created)) {
        return map_stage_error(*error, ErrorCode::write_failure);
    }
    auto file = std::move(std::get<detail::FileDescriptor>(created));
    if (!detail::write_all_at(file.get(), 0U, bytes)) {
        return fail(
            ErrorCode::write_failure, "audio-atlas-publication-file-write-failed",
            detail::errno_message("could not write a staged atlas file", errno));
    }
    if (::fsync(file.get()) == -1) {
        return fail(
            ErrorCode::synchronization_failure,
            "audio-atlas-publication-file-sync-failed",
            detail::errno_message("could not synchronize a staged atlas file", errno));
    }
    return AudioAtlasDirectoryPublication{};
}

[[nodiscard]] Result publish_linux(const std::filesystem::path &canonical_parent,
                                   const std::string &publication_leaf,
                                   const std::filesystem::path &publication_path,
                                   const std::span<const std::byte> encoded_manifest,
                                   const AssembledAudioAtlas &atlas) {
    detail::FileDescriptor parent_fd(::open(
        canonical_parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent_fd.valid()) {
        return fail(ErrorCode::parent_unavailable,
                    "audio-atlas-publication-parent-open-failed",
                    detail::errno_message(
                        "could not open the canonical publication parent", errno));
    }

    std::string staging_name;
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        staging_name = detail::random_stage_name();
        if (::mkdirat(parent_fd.get(), staging_name.c_str(), 0700) == 0) {
            break;
        }
        if (errno != EEXIST) {
            return fail(ErrorCode::staging_failure,
                        "audio-atlas-publication-staging-create-failed",
                        detail::errno_message(
                            "could not create the atlas staging directory", errno));
        }
        staging_name.clear();
    }
    if (staging_name.empty()) {
        return fail(ErrorCode::staging_failure,
                    "audio-atlas-publication-staging-name-exhausted",
                    "could not allocate a unique atlas staging directory");
    }

    detail::FileDescriptor stage_fd(
        ::openat(parent_fd.get(), staging_name.c_str(),
                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat stage_status {};
    if (!stage_fd.valid() || ::fstat(stage_fd.get(), &stage_status) == -1 ||
        !S_ISDIR(stage_status.st_mode)) {
        const auto error_number = errno;
        static_cast<void>(
            ::unlinkat(parent_fd.get(), staging_name.c_str(), AT_REMOVEDIR));
        return fail(
            ErrorCode::staging_failure, "audio-atlas-publication-staging-open-failed",
            detail::errno_message("could not open the atlas staging directory",
                                  error_number));
    }
    const auto stage_device =
        static_cast<std::uintmax_t>(stage_status.st_dev);
    const auto stage_inode = static_cast<std::uintmax_t>(stage_status.st_ino);
    StagingCleanup cleanup(parent_fd.get(), stage_device, stage_inode);

    if (auto result = write_staged_file(stage_fd.get(), "atlas.json", encoded_manifest);
        std::holds_alternative<AudioAtlasDirectoryPublicationError>(result)) {
        return result;
    }
    for (std::size_t index = 0U; index < atlas.payloads.size(); ++index) {
        if (auto result = write_staged_file(
                stage_fd.get(), atlas.manifest.artifacts[index].path,
                atlas.payloads[index].bytes);
            std::holds_alternative<AudioAtlasDirectoryPublicationError>(result)) {
            return result;
        }
    }

    if (!detail::sync_directory_tree(stage_fd.get())) {
        return fail(
            ErrorCode::synchronization_failure,
            "audio-atlas-publication-tree-sync-failed",
            detail::errno_message(
                "could not synchronize the staged atlas directory tree", errno));
    }
    if (auto verified = verify_staging_tree(
            parent_fd.get(), stage_fd.get(), staging_name, stage_device,
            stage_inode, atlas);
        std::holds_alternative<AudioAtlasDirectoryPublicationError>(verified)) {
        return verified;
    }

    if (detail::rename_noreplace(parent_fd.get(), staging_name.c_str(),
                                 publication_leaf.c_str()) == -1) {
        const auto error_number = errno;
        if (error_number == EEXIST) {
            return fail(ErrorCode::destination_exists,
                        "audio-atlas-publication-destination-exists",
                        "the requested atlas publication directory already exists");
        }
        return fail(ErrorCode::publication_failure,
                    "audio-atlas-publication-rename-failed",
                    detail::errno_message(
                        "could not atomically publish the staged atlas", error_number));
    }
    cleanup.release();

    if (::fsync(parent_fd.get()) == -1) {
        return fail(ErrorCode::synchronization_failure,
                    "audio-atlas-publication-parent-sync-failed-after-commit",
                    detail::errno_message(
                        "atlas was published but its parent directory could not be "
                        "synchronized",
                        errno));
    }
    return AudioAtlasDirectoryPublication{publication_path};
}

#endif

} // namespace

AudioAtlasDirectoryPublicationResult
publish_audio_atlas_directory(const std::filesystem::path &canonical_parent,
                              const std::string_view publication_leaf,
                              const AssembledAudioAtlas &atlas) noexcept {
    try {
        if (canonical_parent.empty() || !is_single_leaf(publication_leaf)) {
            return fail(ErrorCode::invalid_request,
                        "audio-atlas-publication-target-invalid",
                        "publication requires an existing canonical parent and one "
                        "portable destination leaf");
        }

        auto encoded = encode_audio_atlas_manifest(atlas.manifest);
        if (const auto *error = std::get_if<RenderSinkError>(&encoded)) {
            return map_manifest_error(*error);
        }
        if (auto verified = verify_payloads(atlas);
            std::holds_alternative<AudioAtlasDirectoryPublicationError>(verified)) {
            return verified;
        }

        const auto leaf = std::string{publication_leaf};
        const auto publication_path = canonical_parent / leaf;
        const auto &manifest_bytes = std::get<ManifestEncoding>(encoded).bytes;
#if defined(__linux__)
        return publish_linux(canonical_parent, leaf, publication_path, manifest_bytes,
                             atlas);
#else
        static_cast<void>(publication_path);
        static_cast<void>(manifest_bytes);
        return fail(ErrorCode::unsupported_platform,
                    "audio-atlas-publication-platform-unsupported",
                    "atomic no-replacement atlas publication is implemented on "
                    "Linux only");
#endif
    } catch (const std::bad_alloc &) {
        return fail(ErrorCode::resource_limit,
                    "audio-atlas-publication-allocation-failed",
                    "allocation failed while publishing the audio atlas");
    } catch (const std::exception &error) {
        return fail(
            ErrorCode::internal_failure, "audio-atlas-publication-internal-failure",
            std::string{"unexpected atlas publication failure: "} + error.what());
    } catch (...) {
        return fail(ErrorCode::internal_failure,
                    "audio-atlas-publication-internal-failure",
                    "unexpected non-standard atlas publication failure");
    }
}

} // namespace engine_sim_offline::artifacts
