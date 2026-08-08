#include "engine_sim_offline/responsive/native_publication.hpp"

#include "engine_sim_offline/artifacts/revengine_container.hpp"
#include "engine_sim_offline/artifacts/revengine_package.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::responsive {
namespace {

using Error = NativeResponsivePackageError;
using ErrorCode = NativeResponsivePackageErrorCode;

[[nodiscard]] Error error(ErrorCode code, std::string detail_code, std::string path,
                          std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] Error publication_error(std::string detail_code, std::string path,
                                      std::string message) {
    return error(ErrorCode::publication_failure, std::move(detail_code),
                 std::move(path), std::move(message));
}

[[nodiscard]] Error errno_error(std::string detail_code, std::string path,
                                std::string_view operation, const int error_number) {
    return publication_error(std::move(detail_code), std::move(path),
                             std::string{operation} + ": " +
                                 std::strerror(error_number));
}

[[nodiscard]] Error cancelled_error() {
    return error(ErrorCode::cancelled, "native-responsive-publication-cancelled", "",
                 "native responsive package publication was cancelled");
}

[[nodiscard]] bool valid_output_component(const std::string_view value) noexcept {
    return value.find('/') == value.npos &&
           artifacts::is_portable_revengine_path(value);
}

struct VerifiedPackage {
    std::uint64_t carrier_byte_count = 0;
};

using VerificationResult = std::variant<VerifiedPackage, Error>;

[[nodiscard]] VerificationResult
verify_package(const NativeResponsivePackageV2 &package,
               const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (package.members.empty() || package.revengine_v1.empty() ||
        package.members.size() > artifacts::kRevengineMaximumEntryCountV1 ||
        package.revengine_v1.size() >
            artifacts::kRevengineMaximumContainerByteCountV1) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-publication-package-empty", "",
                     "publication requires a bounded built package and carrier");
    }
    std::uint64_t payload_bytes = 0U;
    std::string_view previous;
    std::vector<artifacts::RevenginePackEntry> tree;
    tree.reserve(package.members.size());
    for (std::size_t index = 0; index < package.members.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &member = package.members[index];
        if (!artifacts::is_portable_revengine_path(member.path) ||
            (index != 0U && !(previous < member.path)) ||
            member.bytes.size() > artifacts::kRevengineMaximumEntryByteCountV1 ||
            member.bytes.size() >
                kNativeResponsiveMaximumPackagePayloadBytes - payload_bytes) {
            return error(
                ErrorCode::invalid_member,
                "native-responsive-publication-member-invalid", member.path,
                "package members are not a sorted, unique, bounded portable tree");
        }
        previous = member.path;
        payload_bytes += member.bytes.size();
        tree.push_back({member.path, member.bytes});
    }
    auto tree_result = artifacts::validate_revengine_package_tree(tree);
    if (const auto *failure =
            std::get_if<artifacts::RevenginePackageError>(&tree_result)) {
        return error(ErrorCode::invalid_member,
                     "native-responsive-publication-tree-invalid", failure->path,
                     failure->message);
    }
    if (contract::sha256(package.revengine_v1) != package.revengine_sha256) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-publication-carrier-digest-mismatch", "",
                     "carrier bytes do not match the package carrier digest");
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    auto verified_result = artifacts::verify_revengine(package.revengine_v1);
    if (const auto *failure =
            std::get_if<artifacts::RevengineContainerError>(&verified_result)) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-publication-carrier-invalid", failure->path,
                     failure->message);
    }
    const auto &index = std::get<artifacts::RevengineContainerIndex>(verified_result);
    if (index.entries.size() != package.members.size()) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-publication-carrier-tree-mismatch", "",
                     "carrier and package tree contain different entry counts");
    }
    for (std::size_t ordinal = 0; ordinal < index.entries.size(); ++ordinal) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &indexed = index.entries[ordinal];
        const auto &member = package.members[ordinal];
        if (indexed.path != member.path ||
            indexed.payload_byte_count != member.bytes.size() ||
            indexed.payload_sha256 != contract::sha256(member.bytes)) {
            return error(
                ErrorCode::invalid_argument,
                "native-responsive-publication-carrier-tree-mismatch", member.path,
                "carrier entry does not bind the supplied package member bytes");
        }
    }
    return VerifiedPackage{package.revengine_v1.size()};
}

#if defined(__linux__)

class FileDescriptor final {
  public:
    FileDescriptor() = default;
    explicit FileDescriptor(const int value) noexcept : value_(value) {}
    ~FileDescriptor() {
        reset();
    }

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept
        : value_(std::exchange(other.value_, -1)) {}
    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.value_, -1));
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return value_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return value_ >= 0;
    }
    [[nodiscard]] int release() noexcept {
        return std::exchange(value_, -1);
    }
    void reset(const int value = -1) noexcept {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
        }
        value_ = value;
    }

  private:
    int value_ = -1;
};

[[nodiscard]] std::vector<std::string>
split_portable_path(const std::string_view path) {
    std::vector<std::string> components;
    std::size_t begin = 0U;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        if (end == path.npos) {
            components.emplace_back(path.substr(begin));
            break;
        }
        components.emplace_back(path.substr(begin, end - begin));
        begin = end + 1U;
    }
    return components;
}

[[nodiscard]] std::string random_private_name(const std::string_view purpose) {
    static std::atomic<std::uint64_t> fallback_counter{0U};
    std::array<std::uint8_t, 16U> random{};
    std::size_t received = 0U;
    while (received < random.size()) {
        const auto count =
            ::getrandom(random.data() + received, random.size() - received, 0);
        if (count > 0) {
            received += static_cast<std::size_t>(count);
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (received != random.size()) {
        const auto process = static_cast<std::uint64_t>(::getpid());
        const auto count = fallback_counter.fetch_add(1U, std::memory_order_relaxed);
        for (std::size_t index = 0; index < random.size(); ++index) {
            const auto source = index < 8U ? process : count;
            random[index] = static_cast<std::uint8_t>(
                source >> static_cast<unsigned>((index % 8U) * 8U));
        }
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result = ".engine-sim-offline-" + std::string{purpose} + "-";
    result.reserve(result.size() + random.size() * 2U);
    for (const auto value : random) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

[[nodiscard]] bool remove_tree_at(const int parent_fd, const char *name) noexcept {
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
    const auto raw = directory.release();
    DIR *stream = ::fdopendir(raw);
    if (stream == nullptr) {
        static_cast<void>(::close(raw));
        return false;
    }
    bool success = true;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!remove_tree_at(::dirfd(stream), entry->d_name)) {
            success = false;
        }
        errno = 0;
    }
    if (errno != 0 || ::closedir(stream) == -1) {
        success = false;
    }
    return success &&
           (::unlinkat(parent_fd, name, AT_REMOVEDIR) == 0 || errno == ENOENT);
}

[[nodiscard]] bool remove_entry_by_identity(const int parent_fd,
                                            const std::uintmax_t device,
                                            const std::uintmax_t inode) noexcept {
    FileDescriptor directory(
        ::openat(parent_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        return false;
    }
    const auto raw = directory.release();
    DIR *stream = ::fdopendir(raw);
    if (stream == nullptr) {
        static_cast<void>(::close(raw));
        return false;
    }
    std::string match;
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
        if (static_cast<std::uintmax_t>(status.st_dev) == device &&
            static_cast<std::uintmax_t>(status.st_ino) == inode) {
            match = entry->d_name;
            break;
        }
        errno = 0;
    }
    if (errno != 0 || ::closedir(stream) == -1) {
        success = false;
    }
    return success && !match.empty() && remove_tree_at(parent_fd, match.c_str());
}

class PrivateEntryGuard final {
  public:
    PrivateEntryGuard(const int parent_fd, const std::uintmax_t device,
                      const std::uintmax_t inode) noexcept
        : parent_fd_(parent_fd), device_(device), inode_(inode) {}
    ~PrivateEntryGuard() {
        if (active_) {
            static_cast<void>(remove_entry_by_identity(parent_fd_, device_, inode_));
        }
    }
    PrivateEntryGuard(const PrivateEntryGuard &) = delete;
    PrivateEntryGuard &operator=(const PrivateEntryGuard &) = delete;
    void release() noexcept {
        active_ = false;
    }

  private:
    int parent_fd_ = -1;
    std::uintmax_t device_ = 0U;
    std::uintmax_t inode_ = 0U;
    bool active_ = true;
};

[[nodiscard]] std::variant<FileDescriptor, Error>
open_parent_beneath(const int stage_fd, const std::string_view path, const bool create,
                    const std::stop_token stop_token) {
    const auto components = split_portable_path(path);
    FileDescriptor current(::dup(stage_fd));
    if (!current.valid()) {
        return errno_error("native-responsive-stage-open-failed", std::string{path},
                           "could not duplicate staging directory", errno);
    }
    for (std::size_t index = 0; index + 1U < components.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (create && ::mkdirat(current.get(), components[index].c_str(), 0700) == -1 &&
            errno != EEXIST) {
            return errno_error("native-responsive-stage-parent-create-failed",
                               std::string{path},
                               "could not create staged package parent", errno);
        }
        FileDescriptor child(::openat(current.get(), components[index].c_str(),
                                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!child.valid()) {
            return errno_error("native-responsive-stage-parent-open-failed",
                               std::string{path},
                               "could not open staged package parent", errno);
        }
        current = std::move(child);
    }
    return current;
}

[[nodiscard]] std::optional<Error> write_all(const int descriptor,
                                             const std::span<const std::byte> bytes,
                                             const std::string_view path,
                                             const std::stop_token stop_token) {
    constexpr std::size_t kChunkBytes = 1024U * 1024U;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto request = std::min(kChunkBytes, bytes.size() - offset);
        const auto count = ::write(descriptor, bytes.data() + offset, request);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            return errno_error("native-responsive-stage-write-failed",
                               std::string{path},
                               "could not write staged package bytes", errno);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
write_member(const int stage_fd, const PortableResponsivePackageMember &member,
             const std::stop_token stop_token) {
    auto parent_result = open_parent_beneath(stage_fd, member.path, true, stop_token);
    if (const auto *failure = std::get_if<Error>(&parent_result)) {
        return *failure;
    }
    auto parent = std::get<FileDescriptor>(std::move(parent_result));
    const auto components = split_portable_path(member.path);
    FileDescriptor file(::openat(parent.get(), components.back().c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0600));
    if (!file.valid()) {
        return errno_error("native-responsive-stage-file-create-failed", member.path,
                           "could not exclusively create staged package member", errno);
    }
    if (const auto failure =
            write_all(file.get(), member.bytes, member.path, stop_token)) {
        return *failure;
    }
    if (::fsync(file.get()) == -1) {
        return errno_error("native-responsive-stage-file-sync-failed", member.path,
                           "could not synchronize staged package member", errno);
    }
    return std::nullopt;
}

struct InventoryEntry {
    std::string path;
    bool directory = false;

    friend bool operator==(const InventoryEntry &, const InventoryEntry &) = default;
};

[[nodiscard]] std::optional<Error>
inventory_recursive(const int directory_fd, const std::string_view prefix,
                    std::vector<InventoryEntry> &entries,
                    const std::size_t maximum_entries,
                    const std::stop_token stop_token) {
    FileDescriptor directory(
        ::openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        return errno_error("native-responsive-stage-inventory-open-failed",
                           std::string{prefix},
                           "could not duplicate staged package directory", errno);
    }
    const auto raw = directory.release();
    DIR *stream = ::fdopendir(raw);
    if (stream == nullptr) {
        const auto error_number = errno;
        static_cast<void>(::close(raw));
        return errno_error(
            "native-responsive-stage-inventory-open-failed", std::string{prefix},
            "could not enumerate staged package directory", error_number);
    }
    std::optional<Error> failure;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (stop_token.stop_requested()) {
            failure = cancelled_error();
            break;
        }
        if (entries.size() >= maximum_entries) {
            failure = error(
                ErrorCode::resource_limit,
                "native-responsive-stage-inventory-limit-exceeded", std::string{prefix},
                "staged package contains more filesystem entries than admitted");
            break;
        }
        const auto path = prefix.empty() ? std::string{entry->d_name}
                                         : std::string{prefix} + "/" + entry->d_name;
        struct stat status{};
        if (::fstatat(::dirfd(stream), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ==
            -1) {
            failure = errno_error("native-responsive-stage-inventory-stat-failed", path,
                                  "could not inspect staged package entry", errno);
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            entries.push_back({path, true});
            FileDescriptor child(
                ::openat(::dirfd(stream), entry->d_name,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (!child.valid()) {
                failure =
                    errno_error("native-responsive-stage-inventory-open-failed", path,
                                "could not open staged package child directory", errno);
                break;
            }
            failure = inventory_recursive(child.get(), path, entries, maximum_entries,
                                          stop_token);
            if (failure.has_value()) {
                break;
            }
        } else if (S_ISREG(status.st_mode)) {
            entries.push_back({path, false});
        } else {
            failure = publication_error(
                "native-responsive-stage-entry-unsupported", path,
                "staged package contains a symbolic link or unsupported file type");
            break;
        }
        errno = 0;
    }
    const auto enumeration_error = errno;
    if (::closedir(stream) == -1 && !failure.has_value()) {
        failure = errno_error("native-responsive-stage-inventory-close-failed",
                              std::string{prefix},
                              "could not close staged package directory", errno);
    } else if (enumeration_error != 0 && !failure.has_value()) {
        failure = errno_error(
            "native-responsive-stage-inventory-read-failed", std::string{prefix},
            "could not enumerate staged package directory", enumeration_error);
    }
    return failure;
}

[[nodiscard]] std::vector<InventoryEntry>
expected_inventory(const std::vector<PortableResponsivePackageMember> &members) {
    std::set<std::string> directories;
    std::vector<InventoryEntry> result;
    result.reserve(members.size() * 2U);
    for (const auto &member : members) {
        std::size_t slash = member.path.find('/');
        while (slash != member.path.npos) {
            directories.insert(member.path.substr(0U, slash));
            slash = member.path.find('/', slash + 1U);
        }
        result.push_back({member.path, false});
    }
    for (const auto &directory : directories) {
        result.push_back({directory, true});
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        if (left.path != right.path) {
            return left.path < right.path;
        }
        return left.directory < right.directory;
    });
    return result;
}

[[nodiscard]] std::optional<Error>
compare_staged_file(const int stage_fd, const PortableResponsivePackageMember &member,
                    const std::stop_token stop_token) {
    auto parent_result = open_parent_beneath(stage_fd, member.path, false, stop_token);
    if (const auto *failure = std::get_if<Error>(&parent_result)) {
        return *failure;
    }
    auto parent = std::get<FileDescriptor>(std::move(parent_result));
    const auto components = split_portable_path(member.path);
    FileDescriptor file(::openat(parent.get(), components.back().c_str(),
                                 O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!file.valid()) {
        return errno_error("native-responsive-stage-file-verify-open-failed",
                           member.path, "could not reopen staged package member",
                           errno);
    }
    struct stat status{};
    if (::fstat(file.get(), &status) == -1 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != member.bytes.size()) {
        return publication_error("native-responsive-stage-file-extent-mismatch",
                                 member.path,
                                 "staged package member has a different type or size");
    }
    constexpr std::size_t kChunkBytes = 1024U * 1024U;
    std::vector<std::byte> buffer(kChunkBytes);
    std::size_t offset = 0U;
    while (offset < member.bytes.size()) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto request = std::min(buffer.size(), member.bytes.size() - offset);
        const auto count =
            ::pread(file.get(), buffer.data(), request, static_cast<off_t>(offset));
        if (count > 0) {
            const auto received = static_cast<std::size_t>(count);
            if (!std::equal(buffer.begin(), buffer.begin() + received,
                            member.bytes.begin() + offset)) {
                return publication_error(
                    "native-responsive-stage-file-content-mismatch", member.path,
                    "staged package member bytes changed before publication");
            }
            offset += received;
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            return errno_error("native-responsive-stage-file-verify-read-failed",
                               member.path, "could not read staged package member",
                               errno);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
sync_directory_tree(const int directory_fd, const std::string_view path,
                    const std::stop_token stop_token) {
    FileDescriptor directory(
        ::openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory.valid()) {
        return errno_error("native-responsive-stage-directory-sync-open-failed",
                           std::string{path},
                           "could not duplicate staged package directory", errno);
    }
    const auto raw = directory.release();
    DIR *stream = ::fdopendir(raw);
    if (stream == nullptr) {
        const auto error_number = errno;
        static_cast<void>(::close(raw));
        return errno_error(
            "native-responsive-stage-directory-sync-open-failed", std::string{path},
            "could not enumerate staged package directory", error_number);
    }
    std::optional<Error> failure;
    errno = 0;
    while (auto *entry = ::readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (stop_token.stop_requested()) {
            failure = cancelled_error();
            break;
        }
        struct stat status{};
        if (::fstatat(::dirfd(stream), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ==
            -1) {
            failure = errno_error("native-responsive-stage-directory-sync-stat-failed",
                                  std::string{path},
                                  "could not inspect staged package tree", errno);
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            FileDescriptor child(
                ::openat(::dirfd(stream), entry->d_name,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (!child.valid()) {
                failure =
                    errno_error("native-responsive-stage-directory-sync-open-failed",
                                std::string{path},
                                "could not open staged package child directory", errno);
                break;
            }
            const auto child_path = path.empty()
                                        ? std::string{entry->d_name}
                                        : std::string{path} + "/" + entry->d_name;
            failure = sync_directory_tree(child.get(), child_path, stop_token);
            if (failure.has_value()) {
                break;
            }
        } else if (!S_ISREG(status.st_mode)) {
            failure = publication_error(
                "native-responsive-stage-entry-unsupported", std::string{path},
                "staged package contains an unsupported filesystem entry");
            break;
        }
        errno = 0;
    }
    const auto enumeration_error = errno;
    if (!failure.has_value() && enumeration_error != 0) {
        failure = errno_error(
            "native-responsive-stage-directory-sync-read-failed", std::string{path},
            "could not enumerate staged package directory", enumeration_error);
    }
    if (!failure.has_value() && ::fsync(::dirfd(stream)) == -1) {
        failure = errno_error("native-responsive-stage-directory-sync-failed",
                              std::string{path},
                              "could not synchronize staged package directory", errno);
    }
    if (::closedir(stream) == -1 && !failure.has_value()) {
        failure = errno_error("native-responsive-stage-directory-close-failed",
                              std::string{path},
                              "could not close staged package directory", errno);
    }
    return failure;
}

[[nodiscard]] int rename_noreplace(const int parent_fd, const char *source,
                                   const char *destination) noexcept {
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

[[nodiscard]] std::variant<std::pair<FileDescriptor, std::string>, Error>
create_private_directory(const int root_fd) {
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        auto name = random_private_name("responsive-stage");
        if (::mkdirat(root_fd, name.c_str(), 0700) == -1) {
            if (errno == EEXIST) {
                continue;
            }
            return errno_error("native-responsive-stage-create-failed", name,
                               "could not create private package stage", errno);
        }
        FileDescriptor directory(::openat(
            root_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!directory.valid()) {
            const auto error_number = errno;
            static_cast<void>(::unlinkat(root_fd, name.c_str(), AT_REMOVEDIR));
            return errno_error("native-responsive-stage-open-failed", name,
                               "could not open private package stage", error_number);
        }
        return std::pair<FileDescriptor, std::string>{std::move(directory),
                                                      std::move(name)};
    }
    return publication_error("native-responsive-stage-name-exhausted", "",
                             "could not allocate a unique private package stage");
}

[[nodiscard]] std::variant<std::pair<FileDescriptor, std::string>, Error>
create_private_file(const int root_fd) {
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        auto name = random_private_name("responsive-carrier");
        FileDescriptor file(::openat(root_fd, name.c_str(),
                                     O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                     0600));
        if (!file.valid()) {
            if (errno == EEXIST) {
                continue;
            }
            return errno_error("native-responsive-carrier-stage-create-failed", name,
                               "could not create private carrier stage", errno);
        }
        return std::pair<FileDescriptor, std::string>{std::move(file), std::move(name)};
    }
    return publication_error("native-responsive-carrier-stage-name-exhausted", "",
                             "could not allocate a unique private carrier stage");
}

#endif

} // namespace

NativeResponsiveDirectoryPublicationResult
publish_native_responsive_package_atomic(const NativeResponsivePackageV2 &package,
                                         const std::filesystem::path &publication_root,
                                         std::string publication_name,
                                         const std::stop_token stop_token) {
    if (!valid_output_component(publication_name)) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-publication-name-invalid", publication_name,
                     "publication name must be one portable filesystem component");
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
#if !defined(__linux__)
    static_cast<void>(publication_root);
    return error(
        ErrorCode::unsupported_platform,
        "native-responsive-atomic-publication-unsupported", "",
        "atomic no-replace directory publication is implemented only on Linux");
#else
    FileDescriptor root(::open(publication_root.c_str(),
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root.valid()) {
        return errno_error(
            "native-responsive-publication-root-open-failed", publication_root.string(),
            "publication root is unavailable or is a symbolic link", errno);
    }
    struct stat destination_status{};
    if (::fstatat(root.get(), publication_name.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        return error(ErrorCode::output_conflict,
                     "native-responsive-publication-destination-exists",
                     (publication_root / publication_name).string(),
                     "publication destination already exists");
    }
    if (errno != ENOENT) {
        return errno_error("native-responsive-publication-destination-stat-failed",
                           (publication_root / publication_name).string(),
                           "could not inspect publication destination", errno);
    }
    auto package_verification = verify_package(package, stop_token);
    if (const auto *failure = std::get_if<Error>(&package_verification)) {
        return *failure;
    }
    auto stage_result = create_private_directory(root.get());
    if (const auto *failure = std::get_if<Error>(&stage_result)) {
        return *failure;
    }
    auto [stage, stage_name] =
        std::get<std::pair<FileDescriptor, std::string>>(std::move(stage_result));
    struct stat stage_status{};
    if (::fstat(stage.get(), &stage_status) == -1) {
        const auto failure =
            errno_error("native-responsive-stage-stat-failed", stage_name,
                        "could not identify private package stage", errno);
        stage.reset();
        // Without an inode identity, authorize only removal of the empty directory
        // just created. Never recursively remove by an unbound name.
        static_cast<void>(::unlinkat(root.get(), stage_name.c_str(), AT_REMOVEDIR));
        return failure;
    }
    PrivateEntryGuard cleanup(root.get(),
                              static_cast<std::uintmax_t>(stage_status.st_dev),
                              static_cast<std::uintmax_t>(stage_status.st_ino));

    for (const auto &member : package.members) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (const auto failure = write_member(stage.get(), member, stop_token)) {
            return *failure;
        }
    }
    if (const auto failure = sync_directory_tree(stage.get(), {}, stop_token)) {
        return *failure;
    }
    if (::fsync(root.get()) == -1) {
        return errno_error("native-responsive-publication-root-sync-failed",
                           publication_root.string(),
                           "could not synchronize publication root", errno);
    }
    for (const auto &member : package.members) {
        if (const auto failure = compare_staged_file(stage.get(), member, stop_token)) {
            return *failure;
        }
    }
    auto expected = expected_inventory(package.members);
    std::vector<InventoryEntry> actual;
    actual.reserve(expected.size());
    if (const auto failure = inventory_recursive(stage.get(), {}, actual,
                                                 expected.size() + 1U, stop_token)) {
        return *failure;
    }
    std::sort(actual.begin(), actual.end(), [](const auto &left, const auto &right) {
        if (left.path != right.path) {
            return left.path < right.path;
        }
        return left.directory < right.directory;
    });
    if (actual != expected) {
        std::size_t mismatch = 0U;
        while (mismatch < actual.size() && mismatch < expected.size() &&
               actual[mismatch] == expected[mismatch]) {
            ++mismatch;
        }
        const auto describe = [](const std::vector<InventoryEntry> &inventory,
                                 const std::size_t index) {
            return index >= inventory.size()
                       ? std::string{"<end>"}
                       : inventory[index].path +
                             (inventory[index].directory ? " (directory)" : " (file)");
        };
        return publication_error(
            "native-responsive-stage-inventory-mismatch", stage_name,
            "staged package inventory differs from built members at " +
                std::to_string(mismatch) + ": actual " + describe(actual, mismatch) +
                ", expected " + describe(expected, mismatch));
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    struct stat current_stage_status{};
    if (::fstat(stage.get(), &current_stage_status) == -1 ||
        current_stage_status.st_dev != stage_status.st_dev ||
        current_stage_status.st_ino != stage_status.st_ino) {
        return publication_error(
            "native-responsive-stage-identity-mismatch", stage_name,
            "private package stage identity changed before publication");
    }
    if (rename_noreplace(root.get(), stage_name.c_str(), publication_name.c_str()) ==
        -1) {
        const auto error_number = errno;
        if (error_number == EEXIST) {
            return error(ErrorCode::output_conflict,
                         "native-responsive-publication-destination-exists",
                         (publication_root / publication_name).string(),
                         "publication destination appeared before atomic commit");
        }
        return errno_error("native-responsive-atomic-directory-publish-failed",
                           (publication_root / publication_name).string(),
                           "could not atomically publish package without replacement",
                           error_number);
    }
    cleanup.release();
    stage.reset();
    static_cast<void>(::fsync(root.get()));
    return NativeResponsiveDirectoryPublication{publication_root / publication_name,
                                                package.members.size()};
#endif
}

NativeResponsiveCarrierPublicationResult
publish_native_revengine_atomic(const NativeResponsivePackageV2 &package,
                                const std::filesystem::path &output_file,
                                const std::stop_token stop_token) {
    const auto file_name = output_file.filename().string();
    if (!valid_output_component(file_name)) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-carrier-name-invalid", output_file.string(),
                     "carrier output name must be one portable filesystem component");
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
#if !defined(__linux__)
    return error(ErrorCode::unsupported_platform,
                 "native-responsive-atomic-publication-unsupported", "",
                 "atomic no-replace carrier publication is implemented only on Linux");
#else
    auto parent_path = output_file.parent_path();
    if (parent_path.empty()) {
        parent_path = ".";
    }
    FileDescriptor parent(
        ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent.valid()) {
        return errno_error(
            "native-responsive-carrier-parent-open-failed", parent_path.string(),
            "carrier parent is unavailable or is a symbolic link", errno);
    }
    struct stat destination_status{};
    if (::fstatat(parent.get(), file_name.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        return error(ErrorCode::output_conflict,
                     "native-responsive-carrier-destination-exists",
                     output_file.string(), "carrier destination already exists");
    }
    if (errno != ENOENT) {
        return errno_error("native-responsive-carrier-destination-stat-failed",
                           output_file.string(),
                           "could not inspect carrier destination", errno);
    }
    auto package_verification = verify_package(package, stop_token);
    if (const auto *failure = std::get_if<Error>(&package_verification)) {
        return *failure;
    }
    auto stage_result = create_private_file(parent.get());
    if (const auto *failure = std::get_if<Error>(&stage_result)) {
        return *failure;
    }
    auto [stage, stage_name] =
        std::get<std::pair<FileDescriptor, std::string>>(std::move(stage_result));
    struct stat stage_status{};
    if (::fstat(stage.get(), &stage_status) == -1) {
        const auto failure =
            errno_error("native-responsive-carrier-stage-stat-failed", stage_name,
                        "could not identify private carrier stage", errno);
        stage.reset();
        static_cast<void>(::unlinkat(parent.get(), stage_name.c_str(), 0));
        return failure;
    }
    PrivateEntryGuard cleanup(parent.get(),
                              static_cast<std::uintmax_t>(stage_status.st_dev),
                              static_cast<std::uintmax_t>(stage_status.st_ino));
    if (const auto failure = write_all(stage.get(), package.revengine_v1,
                                       output_file.string(), stop_token)) {
        return *failure;
    }
    if (::fsync(stage.get()) == -1) {
        return errno_error("native-responsive-carrier-stage-sync-failed",
                           output_file.string(), "could not synchronize staged carrier",
                           errno);
    }
    if (::fsync(parent.get()) == -1) {
        return errno_error("native-responsive-carrier-parent-sync-failed",
                           parent_path.string(), "could not synchronize carrier parent",
                           errno);
    }
    struct stat current_status{};
    if (::fstat(stage.get(), &current_status) == -1 ||
        current_status.st_dev != stage_status.st_dev ||
        current_status.st_ino != stage_status.st_ino || current_status.st_size < 0 ||
        static_cast<std::uint64_t>(current_status.st_size) !=
            package.revengine_v1.size()) {
        return publication_error("native-responsive-carrier-stage-identity-mismatch",
                                 output_file.string(),
                                 "private carrier stage identity or extent changed");
    }
    constexpr std::size_t kChunkBytes = 1024U * 1024U;
    std::vector<std::byte> buffer(kChunkBytes);
    std::size_t offset = 0U;
    while (offset < package.revengine_v1.size()) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto request =
            std::min(buffer.size(), package.revengine_v1.size() - offset);
        const auto count =
            ::pread(stage.get(), buffer.data(), request, static_cast<off_t>(offset));
        if (count > 0) {
            const auto received = static_cast<std::size_t>(count);
            if (!std::equal(buffer.begin(), buffer.begin() + received,
                            package.revengine_v1.begin() + offset)) {
                return publication_error(
                    "native-responsive-carrier-stage-content-mismatch",
                    output_file.string(),
                    "staged carrier bytes changed before publication");
            }
            offset += received;
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            return errno_error("native-responsive-carrier-stage-read-failed",
                               output_file.string(),
                               "could not verify staged carrier bytes", errno);
        }
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (rename_noreplace(parent.get(), stage_name.c_str(), file_name.c_str()) == -1) {
        const auto error_number = errno;
        if (error_number == EEXIST) {
            return error(ErrorCode::output_conflict,
                         "native-responsive-carrier-destination-exists",
                         output_file.string(),
                         "carrier destination appeared before atomic commit");
        }
        return errno_error(
            "native-responsive-atomic-carrier-publish-failed", output_file.string(),
            "could not atomically publish carrier without replacement", error_number);
    }
    cleanup.release();
    stage.reset();
    static_cast<void>(::fsync(parent.get()));
    return NativeResponsiveCarrierPublication{
        output_file, static_cast<std::uint64_t>(package.revengine_v1.size()),
        package.revengine_sha256};
#endif
}

} // namespace engine_sim_offline::responsive
