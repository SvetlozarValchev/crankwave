#include "secure_filesystem_support.hpp"

#if defined(__linux__)

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace engine_sim_offline::artifacts::detail {
namespace {

RenderSinkError publication_error(std::string detail_code, std::string message) {
    return {
        RenderSinkErrorKind::publication_failure,
        std::move(detail_code),
        std::move(message),
    };
}

std::vector<std::string> split_relative_path(std::string_view path) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        if (end == std::string_view::npos) {
            result.emplace_back(path.substr(begin));
            break;
        }
        result.emplace_back(path.substr(begin, end - begin));
        begin = end + 1;
    }
    return result;
}

std::variant<FileDescriptor, RenderSinkError>
open_parent_beneath(int stage_fd, const std::vector<std::string> &components,
                    bool create) {
    FileDescriptor current(::dup(stage_fd));
    if (!current.valid()) {
        return publication_error(
            "staging-directory-open-failed",
            errno_message("could not duplicate staging directory descriptor", errno));
    }

    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        const auto &component = components[index];
        if (create && ::mkdirat(current.get(), component.c_str(), 0700) == -1 &&
            errno != EEXIST) {
            return publication_error(
                "artifact-parent-create-failed",
                errno_message("could not create an artifact parent directory", errno));
        }
        FileDescriptor child(::openat(current.get(), component.c_str(),
                                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!child.valid()) {
            return publication_error(
                "artifact-parent-open-failed",
                errno_message("artifact parent is unavailable or is a symbolic link",
                              errno));
        }
        current = std::move(child);
    }
    return current;
}

} // namespace

FileDescriptor::FileDescriptor(int descriptor) noexcept : descriptor_(descriptor) {}

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

int FileDescriptor::release() noexcept {
    return std::exchange(descriptor_, -1);
}

void FileDescriptor::reset(int descriptor) noexcept {
    if (descriptor_ >= 0) {
        // Linux releases the descriptor even when close reports EINTR. Retrying can
        // close an unrelated descriptor that another thread acquired in the interim.
        static_cast<void>(::close(descriptor_));
    }
    descriptor_ = descriptor;
}

std::string errno_message(std::string_view operation, int error_number) {
    return std::string(operation) + ": " + std::strerror(error_number);
}

bool remove_tree_entry_at(int parent_fd, const char *name) noexcept {
    struct stat status{};
    if (::fstatat(parent_fd, name, &status, AT_SYMLINK_NOFOLLOW) == -1) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(status.st_mode)) {
        return ::unlinkat(parent_fd, name, 0) == 0 || errno == ENOENT;
    }

    FileDescriptor directory(
        ::openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!directory.valid()) {
        return false;
    }
    const auto raw_directory = directory.release();
    DIR *stream = ::fdopendir(raw_directory);
    if (stream == nullptr) {
        static_cast<void>(::close(raw_directory));
        return false;
    }

    bool removed_children = true;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!remove_tree_entry_at(::dirfd(stream), entry->d_name)) {
            removed_children = false;
        }
        errno = 0;
    }
    if (errno != 0) {
        removed_children = false;
    }
    if (::closedir(stream) == -1) {
        removed_children = false;
    }
    if (!removed_children) {
        return false;
    }
    return ::unlinkat(parent_fd, name, AT_REMOVEDIR) == 0 || errno == ENOENT;
}

bool remove_tree_entry_by_identity(int parent_fd, std::uintmax_t device,
                                   std::uintmax_t inode) noexcept {
    FileDescriptor directory(
        ::openat(parent_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        return false;
    }
    const auto raw_directory = directory.release();
    DIR *stream = ::fdopendir(raw_directory);
    if (stream == nullptr) {
        static_cast<void>(::close(raw_directory));
        return false;
    }

    bool success = true;
    std::array<char, NAME_MAX + 1> matching_name{};
    bool found = false;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        struct stat status{};
        if (::fstatat(::dirfd(stream), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ==
            -1) {
            success = false;
            break;
        }
        if (static_cast<std::uintmax_t>(status.st_dev) == device &&
            static_cast<std::uintmax_t>(status.st_ino) == inode) {
            const auto length = std::strlen(entry->d_name);
            if (length > NAME_MAX) {
                success = false;
                break;
            }
            std::memcpy(matching_name.data(), entry->d_name, length + 1);
            found = true;
            break;
        }
        errno = 0;
    }
    if (errno != 0) {
        success = false;
    }
    if (::closedir(stream) == -1) {
        success = false;
    }
    if (!success || !found) {
        return false;
    }
    return remove_tree_entry_at(parent_fd, matching_name.data());
}

bool sync_directory_tree(int directory_fd) {
    FileDescriptor directory(
        ::openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        return false;
    }
    const auto raw_directory = directory.release();
    DIR *stream = ::fdopendir(raw_directory);
    if (stream == nullptr) {
        static_cast<void>(::close(raw_directory));
        return false;
    }

    bool success = true;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        struct stat status{};
        if (::fstatat(::dirfd(stream), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ==
            -1) {
            success = false;
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            FileDescriptor child(
                ::openat(::dirfd(stream), entry->d_name,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (!child.valid() || !sync_directory_tree(child.get())) {
                if (errno == 0) {
                    errno = EIO;
                }
                success = false;
                break;
            }
        } else if (!S_ISREG(status.st_mode)) {
            errno = EINVAL;
            success = false;
            break;
        }
        errno = 0;
    }
    if (errno != 0) {
        success = false;
    }
    if (success && ::fsync(::dirfd(stream)) == -1) {
        success = false;
    }
    if (::closedir(stream) == -1) {
        success = false;
    }
    return success;
}

namespace {

bool inventory_tree_recursive(int directory_fd, std::string_view prefix,
                              std::size_t maximum_entries,
                              std::vector<DirectoryTreeEntry> &entries,
                              RenderSinkError &error) {
    FileDescriptor directory(
        ::openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        error = publication_error(
            "staging-inventory-open-failed",
            errno_message("could not duplicate a staging directory descriptor", errno));
        return false;
    }
    const auto raw_directory = directory.release();
    DIR *stream = ::fdopendir(raw_directory);
    if (stream == nullptr) {
        const auto error_number = errno;
        static_cast<void>(::close(raw_directory));
        error = publication_error(
            "staging-inventory-open-failed",
            errno_message("could not inspect a staging directory", error_number));
        return false;
    }

    bool success = true;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        const auto relative_path = prefix.empty()
                                       ? std::string(entry->d_name)
                                       : std::string(prefix) + "/" + entry->d_name;
        struct stat status{};
        if (::fstatat(::dirfd(stream), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ==
            -1) {
            error = publication_error(
                "staging-inventory-stat-failed",
                errno_message("could not inspect a staged tree entry", errno));
            success = false;
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            if (entries.size() == maximum_entries) {
                error = publication_error(
                    "staging-inventory-limit-exceeded",
                    "staging contains more entries than the declared transaction "
                    "can publish");
                success = false;
                break;
            }
            entries.push_back({relative_path, true});
            FileDescriptor child(
                ::openat(::dirfd(stream), entry->d_name,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (!child.valid() ||
                !inventory_tree_recursive(child.get(), relative_path, maximum_entries,
                                          entries, error)) {
                if (!child.valid()) {
                    error = publication_error(
                        "staging-inventory-open-failed",
                        errno_message("could not open a staged directory", errno));
                }
                success = false;
                break;
            }
        } else if (S_ISREG(status.st_mode)) {
            if (entries.size() == maximum_entries) {
                error = publication_error(
                    "staging-inventory-limit-exceeded",
                    "staging contains more entries than the declared transaction "
                    "can publish");
                success = false;
                break;
            }
            entries.push_back({relative_path, false});
        } else {
            error = publication_error(
                "staging-inventory-unsupported-entry",
                "staging contains a symbolic link, device, FIFO, socket, or other "
                "unsupported entry");
            success = false;
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        error = publication_error(
            "staging-inventory-read-failed",
            errno_message("could not enumerate a staging directory", errno));
        success = false;
    }
    if (::closedir(stream) == -1 && success) {
        error = publication_error(
            "staging-inventory-close-failed",
            errno_message("could not close a staging directory", errno));
        success = false;
    }
    return success;
}

} // namespace

std::variant<std::vector<DirectoryTreeEntry>, RenderSinkError>
inventory_directory_tree(int directory_fd, std::size_t maximum_entries) {
    std::vector<DirectoryTreeEntry> entries;
    entries.reserve(maximum_entries);
    RenderSinkError error;
    if (!inventory_tree_recursive(directory_fd, {}, maximum_entries, entries, error)) {
        return error;
    }
    return entries;
}

std::string random_stage_name() {
    static std::atomic<std::uint64_t> fallback_counter{0};
    std::array<std::uint8_t, 16> random_bytes{};
    std::size_t obtained = 0;
    while (obtained < random_bytes.size()) {
        const auto count = ::getrandom(random_bytes.data() + obtained,
                                       random_bytes.size() - obtained, 0);
        if (count > 0) {
            obtained += static_cast<std::size_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (obtained != random_bytes.size()) {
        const auto counter = fallback_counter.fetch_add(1, std::memory_order_relaxed);
        const auto process = static_cast<std::uint64_t>(::getpid());
        for (std::size_t index = 0; index < random_bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>((index % 8) * 8);
            const auto source = index < 8 ? process : counter;
            random_bytes[index] = static_cast<std::uint8_t>(source >> shift);
        }
    }

    constexpr std::string_view digits = "0123456789abcdef";
    std::string name = ".engine-sim-offline-stage-";
    name.reserve(name.size() + random_bytes.size() * 2);
    for (const auto byte : random_bytes) {
        name.push_back(digits[byte >> 4U]);
        name.push_back(digits[byte & 0x0fU]);
    }
    return name;
}

std::variant<FileDescriptor, RenderSinkError>
create_file_beneath(int stage_fd, std::string_view relative_path) {
    const auto components = split_relative_path(relative_path);
    auto parent = open_parent_beneath(stage_fd, components, true);
    if (auto *error = std::get_if<RenderSinkError>(&parent)) {
        return std::move(*error);
    }
    auto directory = std::move(std::get<FileDescriptor>(parent));
    FileDescriptor file(::openat(directory.get(), components.back().c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0600));
    if (!file.valid()) {
        return publication_error(
            "artifact-file-create-failed",
            errno_message("could not exclusively create a staged file", errno));
    }
    return file;
}

std::variant<FileDescriptor, RenderSinkError>
open_file_beneath(int stage_fd, std::string_view relative_path) {
    const auto components = split_relative_path(relative_path);
    auto parent = open_parent_beneath(stage_fd, components, false);
    if (auto *error = std::get_if<RenderSinkError>(&parent)) {
        return std::move(*error);
    }
    auto directory = std::move(std::get<FileDescriptor>(parent));
    FileDescriptor file(::openat(directory.get(), components.back().c_str(),
                                 O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!file.valid()) {
        return publication_error(
            "artifact-file-reopen-failed",
            errno_message("could not reopen a staged artifact without following "
                          "symbolic links",
                          errno));
    }
    return file;
}

bool write_all_at(int descriptor, std::uint64_t offset,
                  std::span<const std::byte> bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto remaining = bytes.size() - written;
        const auto request = std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto position = offset + written;
        if (position > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            errno = EFBIG;
            return false;
        }
        const auto count = ::pwrite(descriptor, bytes.data() + written, request,
                                    static_cast<off_t>(position));
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

int rename_noreplace(int parent_fd, const char *source, const char *destination) {
#if defined(SYS_renameat2)
    constexpr unsigned kRenameNoReplace = 1U;
    return static_cast<int>(::syscall(SYS_renameat2, parent_fd, source, parent_fd,
                                      destination, kRenameNoReplace));
#else
    static_cast<void>(parent_fd);
    static_cast<void>(source);
    static_cast<void>(destination);
    errno = ENOSYS;
    return -1;
#endif
}

} // namespace engine_sim_offline::artifacts::detail

#endif
