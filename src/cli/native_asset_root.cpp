#include "native_input_files_support.hpp"

#include <cerrno>
#include <string>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::cli::detail {
namespace {

#if !defined(__linux__)
NativeInputError unavailable(NativeInputSubject subject,
                             const std::filesystem::path &path,
                             std::string_view asset_id = {}) {
    return input_error(
        NativeInputErrorKind::unavailable,
        NativeInputErrorCode::platform_unavailable, subject, path,
        "atomic asset-root traversal is unavailable on this platform",
        std::string(asset_id));
}
#endif

#if defined(__linux__)

bool path_has_embedded_nul(const std::filesystem::path &path) {
    return path.native().find('\0') != std::string::npos;
}

NativeInputError root_open_failure(int error_number,
                                   const std::filesystem::path &path) {
    struct stat status {};
    if ((error_number == ELOOP || error_number == ENOTDIR) &&
        ::fstatat(AT_FDCWD, path.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(status.st_mode)) {
            return input_error(
                NativeInputErrorKind::no_input,
                NativeInputErrorCode::symbolic_link_not_allowed,
                NativeInputSubject::asset_root, path,
                "asset root must not be a symbolic link");
        }
        if (!S_ISDIR(status.st_mode)) {
            return input_error(
                NativeInputErrorKind::no_input,
                NativeInputErrorCode::asset_root_not_directory,
                NativeInputSubject::asset_root, path,
                "asset root is not a directory");
        }
    }
    if (error_number == ENOENT || error_number == ENOTDIR) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::path_not_found,
                           NativeInputSubject::asset_root, path,
                           "asset root is unavailable");
    }
    return input_error(NativeInputErrorKind::no_input,
                       NativeInputErrorCode::file_open_failed,
                       NativeInputSubject::asset_root, path,
                       "asset root could not be opened");
}

NativeInputError component_open_failure(
    int parent_descriptor, std::string_view component, int error_number,
    NativeInputSubject subject, const std::filesystem::path &display_path,
    std::string_view asset_id) {
    struct stat status {};
    const std::string name(component);
    if (::fstatat(parent_descriptor, name.c_str(), &status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(status.st_mode)) {
            return input_error(
                NativeInputErrorKind::no_input,
                NativeInputErrorCode::symbolic_link_not_allowed, subject,
                display_path,
                "asset path must not traverse a symbolic link",
                std::string(asset_id));
        }
        if (!S_ISDIR(status.st_mode) && error_number == ENOTDIR) {
            return input_error(
                NativeInputErrorKind::no_input,
                NativeInputErrorCode::not_regular_file, subject, display_path,
                "an asset path parent is not a directory",
                std::string(asset_id));
        }
    }
    if (error_number == ENOENT || error_number == ENOTDIR) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::path_not_found, subject,
                           display_path, "asset file is unavailable",
                           std::string(asset_id));
    }
    return input_error(NativeInputErrorKind::no_input,
                       NativeInputErrorCode::file_open_failed, subject,
                       display_path, "asset file could not be opened",
                       std::string(asset_id));
}

bool path_is_within(const std::filesystem::path &root,
                    const std::filesystem::path &candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end() && candidate_part != candidate.end();
         ++root_part, ++candidate_part) {
        if (*root_part != *candidate_part) {
            return false;
        }
    }
    return root_part == root.end();
}

bool valid_local_asset_uri(std::string_view uri) {
    if (uri.empty() || uri.front() == '/' || uri.front() == '\\') {
        return false;
    }
    for (const unsigned char byte : uri) {
        if (byte == 0 || byte < 0x20U || byte == 0x7fU || byte == '\\' ||
            byte == ':' || byte == '?' || byte == '#') {
            return false;
        }
    }
    std::size_t begin = 0;
    while (begin <= uri.size()) {
        const auto separator = uri.find('/', begin);
        const auto size = separator == std::string_view::npos
                              ? uri.size() - begin
                              : separator - begin;
        if (size == 0U) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1U;
    }
    const std::filesystem::path path{std::string(uri)};
    return path.is_relative() && !path.has_root_name() &&
           !path.has_root_directory();
}

#endif

} // namespace

OpenedAssetRootResult
open_asset_root(const std::filesystem::path &asset_root) {
#if !defined(__linux__)
    return unavailable(NativeInputSubject::asset_root, asset_root);
#else
    if (asset_root.empty() || path_has_embedded_nul(asset_root)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::empty_path, NativeInputSubject::asset_root,
            asset_root,
            "asset root must be nonempty and contain no NUL byte");
    }
    FileDescriptor descriptor(
        ::open(asset_root.c_str(),
               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW |
                   O_NONBLOCK));
    if (!descriptor.valid()) {
        return root_open_failure(errno, asset_root);
    }
    struct stat status {};
    if (::fstat(descriptor.get(), &status) == -1 ||
        !S_ISDIR(status.st_mode)) {
        return input_error(
            NativeInputErrorKind::no_input,
            NativeInputErrorCode::asset_root_not_directory,
            NativeInputSubject::asset_root, asset_root,
            "opened asset root is not a directory");
    }
    auto resolved = descriptor_path(
        descriptor.get(), NativeInputSubject::asset_root, asset_root);
    if (auto *error = std::get_if<NativeInputError>(&resolved)) {
        return std::move(*error);
    }
    return OpenedAssetRoot{
        std::move(descriptor),
        std::get<std::filesystem::path>(std::move(resolved))};
#endif
}

ReadFileResult read_confined_asset(
    const OpenedAssetRoot &asset_root,
    const std::filesystem::path &engine_document, std::string_view uri,
    NativeInputSubject subject, std::string_view asset_id,
    std::uintmax_t maximum_bytes) {
#if !defined(__linux__)
    static_cast<void>(engine_document);
    static_cast<void>(uri);
    static_cast<void>(maximum_bytes);
    return unavailable(subject, asset_root.canonical_path, asset_id);
#else
    if (!asset_root.descriptor.valid()) {
        return input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure, subject,
            asset_root.canonical_path,
            "asset-root descriptor is not valid", std::string(asset_id));
    }
    if (!valid_local_asset_uri(uri)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::invalid_local_asset_uri, subject, {},
            "asset URI must be a nonempty local relative path",
            std::string(asset_id));
    }

    const auto candidate =
        (engine_document.parent_path() /
         std::filesystem::path{std::string(uri)})
            .lexically_normal();
    if (!path_is_within(asset_root.canonical_path, candidate)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::asset_outside_root, subject, candidate,
            "asset path escapes the opened asset-root authority",
            std::string(asset_id));
    }

    std::vector<std::string> components;
    for (const auto &component :
         candidate.lexically_relative(asset_root.canonical_path)) {
        const auto value = component.string();
        if (value.empty() || value == "." || value == "..") {
            return input_error(
                NativeInputErrorKind::data_error,
                NativeInputErrorCode::asset_outside_root, subject, candidate,
                "asset path cannot be represented beneath its root",
                std::string(asset_id));
        }
        components.push_back(value);
    }
    if (components.empty()) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::not_regular_file, subject,
                           candidate, "asset path names its directory root",
                           std::string(asset_id));
    }

    FileDescriptor current(
        ::fcntl(asset_root.descriptor.get(), F_DUPFD_CLOEXEC, 0));
    if (!current.valid()) {
        return input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure, subject, candidate,
            "asset-root descriptor could not be duplicated",
            std::string(asset_id));
    }

    auto display_path = asset_root.canonical_path;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto &component = components[index];
        display_path /= component;
        const bool final = index + 1U == components.size();
        const int flags =
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
            (final ? 0 : O_DIRECTORY);
        FileDescriptor child(
            ::openat(current.get(), component.c_str(), flags));
        if (!child.valid()) {
            return component_open_failure(
                current.get(), component, errno, subject, display_path,
                asset_id);
        }
        current = std::move(child);
    }
    return read_exact_descriptor(current.get(), std::move(display_path),
                                 subject, asset_id, maximum_bytes);
#endif
}

} // namespace engine_sim_offline::cli::detail
