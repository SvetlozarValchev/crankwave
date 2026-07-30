#include "native_input_files_support.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <string>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::cli::detail {

FileDescriptor::FileDescriptor(int descriptor) noexcept
    : descriptor_(descriptor) {}

FileDescriptor::~FileDescriptor() {
    reset();
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)) {}

FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept {
    if (this != &other) {
        reset(std::exchange(other.descriptor_, -1));
    }
    return *this;
}

int FileDescriptor::get() const noexcept {
    return descriptor_;
}

bool FileDescriptor::valid() const noexcept {
    return descriptor_ >= 0;
}

void FileDescriptor::reset(int descriptor) noexcept {
#if defined(__linux__)
    if (descriptor_ >= 0) {
        static_cast<void>(::close(descriptor_));
    }
#endif
    descriptor_ = descriptor;
}

NativeInputError input_error(
    NativeInputErrorKind kind, NativeInputErrorCode code,
    NativeInputSubject subject, std::filesystem::path path, std::string message,
    std::string asset_id,
    std::optional<authoring::DiagnosticReport> diagnostics) {
    return {kind,       code,          subject, std::move(path),
            std::move(asset_id), std::move(message), std::move(diagnostics)};
}

bool not_found(const std::error_code &error) noexcept {
    return error == std::errc::no_such_file_or_directory ||
           error == std::errc::not_a_directory;
}

namespace {

NativeInputError platform_unavailable(NativeInputSubject subject,
                                      const std::filesystem::path &path,
                                      std::string_view asset_id) {
    return input_error(
        NativeInputErrorKind::unavailable,
        NativeInputErrorCode::platform_unavailable, subject, path,
        "atomic native input reads are unavailable on this platform",
        std::string(asset_id));
}

#if defined(__linux__)

bool path_has_embedded_nul(const std::filesystem::path &path) {
    return path.native().find('\0') != std::string::npos;
}

NativeInputError open_failure(int error_number, NativeInputSubject subject,
                              const std::filesystem::path &path,
                              std::string_view asset_id) {
    struct stat status {};
    if ((error_number == ELOOP || error_number == ENOTDIR) &&
        ::fstatat(AT_FDCWD, path.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISLNK(status.st_mode)) {
        return input_error(
            NativeInputErrorKind::no_input,
            NativeInputErrorCode::symbolic_link_not_allowed, subject, path,
            "input path must not resolve through a symbolic-link file",
            std::string(asset_id));
    }
    if (error_number == ENOENT || error_number == ENOTDIR) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::path_not_found, subject, path,
                           "input file does not exist", std::string(asset_id));
    }
    return input_error(NativeInputErrorKind::no_input,
                       NativeInputErrorCode::file_open_failed, subject, path,
                       "input file could not be opened", std::string(asset_id));
}

bool same_file_state(const struct stat &before,
                     const struct stat &after) noexcept {
    return before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
           before.st_mode == after.st_mode && before.st_size == after.st_size &&
           before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
           before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
           before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
           before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}

#endif

} // namespace

std::variant<std::filesystem::path, NativeInputError>
descriptor_path(int descriptor, NativeInputSubject subject,
                const std::filesystem::path &fallback_path,
                std::string_view asset_id) {
#if !defined(__linux__)
    static_cast<void>(descriptor);
    return platform_unavailable(subject, fallback_path, asset_id);
#else
    const auto proc_path =
        std::string("/proc/self/fd/") + std::to_string(descriptor);
    std::vector<char> buffer(256U);
    constexpr std::size_t kMaximumDescriptorPathBytes = 1024U * 1024U;
    for (;;) {
        const auto length =
            ::readlink(proc_path.c_str(), buffer.data(), buffer.size());
        if (length < 0) {
            return platform_unavailable(subject, fallback_path, asset_id);
        }
        const auto size = static_cast<std::size_t>(length);
        if (size < buffer.size()) {
            std::string path(buffer.data(), size);
            if (path.ends_with(" (deleted)")) {
                return input_error(
                    NativeInputErrorKind::software,
                    NativeInputErrorCode::file_changed_during_read, subject,
                    fallback_path,
                    "input path was removed while its descriptor was open",
                    std::string(asset_id));
            }
            std::filesystem::path resolved{std::move(path)};
            if (!resolved.is_absolute()) {
                return platform_unavailable(subject, fallback_path, asset_id);
            }
            return resolved.lexically_normal();
        }
        if (buffer.size() == kMaximumDescriptorPathBytes) {
            return input_error(
                NativeInputErrorKind::data_error,
                NativeInputErrorCode::size_overflow, subject, fallback_path,
                "resolved input path exceeds the native adapter limit",
                std::string(asset_id));
        }
        buffer.resize(
            std::min(buffer.size() * 2U, kMaximumDescriptorPathBytes));
    }
#endif
}

ReadFileResult read_exact_descriptor(int descriptor,
                                     std::filesystem::path display_path,
                                     NativeInputSubject subject,
                                     std::string_view asset_id,
                                     std::uintmax_t maximum_bytes) {
#if !defined(__linux__)
    static_cast<void>(descriptor);
    static_cast<void>(maximum_bytes);
    return platform_unavailable(subject, display_path, asset_id);
#else
    struct stat before {};
    if (::fstat(descriptor, &before) == -1) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::file_size_unavailable, subject,
                           std::move(display_path),
                           "input file metadata is unavailable",
                           std::string(asset_id));
    }
    if (!S_ISREG(before.st_mode)) {
        return input_error(NativeInputErrorKind::no_input,
                           NativeInputErrorCode::not_regular_file, subject,
                           std::move(display_path),
                           "input path is not a regular file",
                           std::string(asset_id));
    }
    if (before.st_size < 0) {
        return input_error(NativeInputErrorKind::data_error,
                           NativeInputErrorCode::size_overflow, subject,
                           std::move(display_path),
                           "input file reports a negative byte size",
                           std::string(asset_id));
    }
    const auto byte_count = static_cast<std::uintmax_t>(before.st_size);
    if (byte_count > maximum_bytes) {
        return input_error(NativeInputErrorKind::data_error,
                           NativeInputErrorCode::file_too_large, subject,
                           std::move(display_path),
                           "input file exceeds its byte limit",
                           std::string(asset_id));
    }
    if (byte_count > std::numeric_limits<std::size_t>::max()) {
        return input_error(NativeInputErrorKind::data_error,
                           NativeInputErrorCode::size_overflow, subject,
                           std::move(display_path),
                           "input file size is not representable by this process",
                           std::string(asset_id));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto request =
            std::min(remaining,
                     static_cast<std::size_t>(
                         std::numeric_limits<ssize_t>::max()));
        const auto read_count =
            ::read(descriptor, bytes.data() + offset, request);
        if (read_count < 0 && errno == EINTR) {
            continue;
        }
        if (read_count < 0) {
            return input_error(
                NativeInputErrorKind::no_input,
                NativeInputErrorCode::file_read_failed, subject,
                std::move(display_path),
                "input file became unreadable during its exact read",
                std::string(asset_id));
        }
        if (read_count == 0) {
            return input_error(
                NativeInputErrorKind::software,
                NativeInputErrorCode::file_changed_during_read, subject,
                std::move(display_path),
                "input file changed during its exact read",
                std::string(asset_id));
        }
        offset += static_cast<std::size_t>(read_count);
    }

    std::byte extra {};
    ssize_t extra_count = -1;
    do {
        extra_count = ::read(descriptor, &extra, 1U);
    } while (extra_count < 0 && errno == EINTR);
    if (extra_count < 0) {
        return input_error(
            NativeInputErrorKind::no_input,
            NativeInputErrorCode::file_read_failed, subject,
            std::move(display_path),
            "input file became unreadable during its exact read",
            std::string(asset_id));
    }
    if (extra_count > 0) {
        return input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::file_changed_during_read, subject,
            std::move(display_path),
            "input file changed during its exact read",
            std::string(asset_id));
    }

    struct stat after {};
    if (::fstat(descriptor, &after) == -1 ||
        !same_file_state(before, after)) {
        return input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::file_changed_during_read, subject,
            std::move(display_path),
            "input file metadata changed during its exact read",
            std::string(asset_id));
    }
    return ReadFile{std::move(display_path), std::move(bytes)};
#endif
}

ReadFileResult read_exact_regular_file(const std::filesystem::path &path,
                                       NativeInputSubject subject,
                                       std::string_view asset_id,
                                       std::uintmax_t maximum_bytes) {
#if !defined(__linux__)
    static_cast<void>(maximum_bytes);
    return platform_unavailable(subject, path, asset_id);
#else
    if (path.empty() || path_has_embedded_nul(path)) {
        return input_error(NativeInputErrorKind::data_error,
                           NativeInputErrorCode::empty_path, subject, path,
                           "input path must be nonempty and contain no NUL byte",
                           std::string(asset_id));
    }
    FileDescriptor descriptor(
        ::open(path.c_str(),
               O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!descriptor.valid()) {
        return open_failure(errno, subject, path, asset_id);
    }
    auto resolved = descriptor_path(descriptor.get(), subject, path, asset_id);
    if (auto *error = std::get_if<NativeInputError>(&resolved)) {
        return std::move(*error);
    }
    return read_exact_descriptor(
        descriptor.get(),
        std::get<std::filesystem::path>(std::move(resolved)), subject,
        asset_id, maximum_bytes);
#endif
}

} // namespace engine_sim_offline::cli::detail
