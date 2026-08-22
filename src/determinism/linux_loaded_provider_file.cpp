#include "determinism/linux_loaded_provider_internal.hpp"

#include "contract/sha256_stream.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace crankwave::determinism::detail::linux_detail {
namespace {

constexpr std::uint64_t kMaximumProviderBytes = UINT64_C(256) * 1024U * 1024U;
constexpr std::uint64_t kMaximumNoteBytes = UINT64_C(1024) * 1024U;

[[nodiscard]] LoadedRuntimeError error(LoadedRuntimeErrorCode code,
                                       std::string_view component, std::string message,
                                       std::string symbol = {}) {
    return {code, std::string(component), std::move(symbol), std::move(message)};
}

class FileDescriptor {
  public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            ::close(value_);
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    [[nodiscard]] int get() const noexcept {
        return value_;
    }

  private:
    int value_;
};

[[nodiscard]] bool same_stat(const struct stat &left,
                             const struct stat &right) noexcept {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino &&
           left.st_mode == right.st_mode && left.st_nlink == right.st_nlink &&
           left.st_size == right.st_size &&
           left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
           left.st_mtim.tv_nsec == right.st_mtim.tv_nsec &&
           left.st_ctim.tv_sec == right.st_ctim.tv_sec &&
           left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}

[[nodiscard]] bool pread_exact(int descriptor, std::span<std::byte> destination,
                               std::uint64_t offset) {
    std::size_t completed = 0;
    while (completed < destination.size()) {
        const auto count = ::pread(descriptor, destination.data() + completed,
                                   destination.size() - completed,
                                   static_cast<off_t>(offset + completed));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

[[nodiscard]] bool checked_add(std::uintptr_t left, std::uint64_t right,
                               std::uintptr_t &result) noexcept {
    if (right > std::numeric_limits<std::uintptr_t>::max() - left) {
        return false;
    }
    result = left + static_cast<std::uintptr_t>(right);
    return true;
}

} // namespace

bool parse_note_block(std::span<const std::byte> bytes, BuildIdAccumulator &build_id) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < sizeof(Elf64_Nhdr)) {
            return false;
        }
        Elf64_Nhdr header{};
        std::memcpy(&header, bytes.data() + offset, sizeof(header));
        offset += sizeof(header);

        const auto aligned = [](std::uint32_t size) -> std::optional<std::size_t> {
            if (size > std::numeric_limits<std::uint32_t>::max() - 3U) {
                return std::nullopt;
            }
            return static_cast<std::size_t>((size + 3U) & ~UINT32_C(3));
        };
        const auto name_bytes = aligned(header.n_namesz);
        const auto description_bytes = aligned(header.n_descsz);
        if (!name_bytes.has_value() || !description_bytes.has_value() ||
            *name_bytes > bytes.size() - offset) {
            return false;
        }
        const auto *name = bytes.data() + offset;
        offset += *name_bytes;
        if (*description_bytes > bytes.size() - offset) {
            return false;
        }
        const auto *description = bytes.data() + offset;
        offset += *description_bytes;

        if (header.n_type != NT_GNU_BUILD_ID || header.n_namesz != 4 ||
            std::memcmp(name, "GNU\0", 4) != 0) {
            continue;
        }
        if (header.n_descsz == 0 || header.n_descsz > GnuBuildId{}.bytes.size()) {
            return false;
        }
        GnuBuildId observed;
        observed.size = static_cast<std::uint8_t>(header.n_descsz);
        std::memcpy(observed.bytes.data(), description, header.n_descsz);
        if (observed.is_zero()) {
            return false;
        }
        ++build_id.count;
        build_id.value = observed;
    }
    return true;
}

MappingResult mapping_for_address(std::uintptr_t address,
                                  std::string_view provider_component,
                                  std::string_view symbol) {
    std::FILE *maps = std::fopen("/proc/self/maps", "re");
    if (maps == nullptr) {
        return error(LoadedRuntimeErrorCode::mapping_unavailable, provider_component,
                     "cannot read /proc/self/maps", std::string(symbol));
    }
    std::array<char, 4096> line{};
    std::optional<MappingRecord> found;
    while (std::fgets(line.data(), static_cast<int>(line.size()), maps) != nullptr) {
        const auto length = std::strlen(line.data());
        if (length == 0 || (line[length - 1] != '\n' && !std::feof(maps))) {
            found.reset();
            break;
        }
        unsigned long long begin = 0;
        unsigned long long end = 0;
        unsigned long long file_offset = 0;
        unsigned int device_major = 0;
        unsigned int device_minor = 0;
        unsigned long long inode = 0;
        std::array<char, 5> permissions{};
        int consumed = 0;
        if (std::sscanf(line.data(), "%llx-%llx %4s %llx %x:%x %llu %n", &begin, &end,
                        permissions.data(), &file_offset, &device_major, &device_minor,
                        &inode, &consumed) < 7 ||
            address < begin || address >= end) {
            continue;
        }
        std::string path(line.data() + consumed);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
            path.pop_back();
        }
        const auto first_character = path.find_first_not_of(' ');
        if (first_character != std::string::npos) {
            path.erase(0, first_character);
        }
        if (permissions[0] != 'r' || permissions[2] != 'x' || inode == 0 ||
            path.empty() || path.front() != '/' || path.ends_with(" (deleted)") ||
            inode >
                static_cast<unsigned long long>(std::numeric_limits<ino_t>::max())) {
            found.reset();
            break;
        }
        found = MappingRecord{::makedev(device_major, device_minor),
                              static_cast<ino_t>(inode), std::move(path)};
        break;
    }
    const bool read_error = std::ferror(maps) != 0;
    std::fclose(maps);
    if (read_error || !found.has_value()) {
        return error(LoadedRuntimeErrorCode::mapping_unavailable, provider_component,
                     "symbol has no usable executable file mapping",
                     std::string(symbol));
    }
    return *found;
}

ProviderFileResult identify_provider_file(const MappingRecord &mapping,
                                          std::span<const Elf64_Phdr> program_headers,
                                          std::uintptr_t load_bias,
                                          std::string_view soname,
                                          const GnuBuildId &memory_build_id,
                                          std::string_view provider_component) {
    FileDescriptor file(::open(mapping.path.c_str(), O_RDONLY | O_CLOEXEC));
    struct stat descriptor_before{};
    struct stat path_before{};
    if (file.get() < 0 || ::fstat(file.get(), &descriptor_before) != 0 ||
        ::stat(mapping.path.c_str(), &path_before) != 0 ||
        !S_ISREG(descriptor_before.st_mode) || descriptor_before.st_nlink == 0 ||
        descriptor_before.st_dev != mapping.device ||
        descriptor_before.st_ino != mapping.inode ||
        !same_stat(descriptor_before, path_before)) {
        return error(LoadedRuntimeErrorCode::provider_replaced, provider_component,
                     "loaded mapping no longer names the same regular provider file: " +
                         mapping.path);
    }
    if (descriptor_before.st_size <= 0 ||
        static_cast<std::uint64_t>(descriptor_before.st_size) > kMaximumProviderBytes) {
        return error(LoadedRuntimeErrorCode::provider_file_too_large,
                     provider_component,
                     "provider file size is outside the bounded hashing policy: " +
                         mapping.path);
    }

    BuildIdAccumulator file_build_id;
    for (const auto &header : program_headers) {
        if (header.p_type != PT_NOTE) {
            continue;
        }
        if (header.p_filesz > kMaximumNoteBytes ||
            header.p_offset > static_cast<std::uint64_t>(descriptor_before.st_size) ||
            header.p_filesz > static_cast<std::uint64_t>(descriptor_before.st_size) -
                                  header.p_offset) {
            return error(
                LoadedRuntimeErrorCode::provider_file_invalid, provider_component,
                "provider note segment is outside the bound file: " + mapping.path);
        }
        std::vector<std::byte> note(static_cast<std::size_t>(header.p_filesz));
        if (!pread_exact(file.get(), note, header.p_offset) ||
            !parse_note_block(note, file_build_id)) {
            return error(LoadedRuntimeErrorCode::provider_file_invalid,
                         provider_component,
                         "provider file notes are malformed: " + mapping.path);
        }
    }
    if (file_build_id.count != 1 || !file_build_id.value.has_value() ||
        *file_build_id.value != memory_build_id) {
        return error(LoadedRuntimeErrorCode::provider_replaced, provider_component,
                     "provider file and loaded-memory build IDs disagree: " +
                         mapping.path);
    }

    // Bind the disk digest to the implementation and read-only coefficient bytes
    // that this process is actually executing. Writable loads are relocation/data
    // state and are intentionally excluded; W+X and text relocations were rejected
    // while validating the in-memory dynamic image.
    std::array<std::byte, 64 * 1024> comparison{};
    for (const auto &header : program_headers) {
        if (header.p_type != PT_LOAD || (header.p_flags & PF_W) != 0 ||
            header.p_filesz == 0) {
            continue;
        }
        if (header.p_offset > static_cast<std::uint64_t>(descriptor_before.st_size) ||
            header.p_filesz > static_cast<std::uint64_t>(descriptor_before.st_size) -
                                  header.p_offset) {
            return error(
                LoadedRuntimeErrorCode::provider_file_invalid, provider_component,
                "non-writable provider load is outside the file: " + mapping.path);
        }
        std::uintptr_t memory = 0;
        std::uintptr_t memory_end = 0;
        if (!checked_add(load_bias, header.p_vaddr, memory) ||
            !checked_add(memory, header.p_filesz, memory_end)) {
            return error(LoadedRuntimeErrorCode::provider_file_invalid,
                         provider_component,
                         "non-writable provider load address overflowed");
        }
        std::uint64_t compared = 0;
        while (compared < header.p_filesz) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(comparison.size(), header.p_filesz - compared));
            auto destination = std::span<std::byte>(comparison).first(count);
            if (!pread_exact(file.get(), destination, header.p_offset + compared) ||
                std::memcmp(destination.data(),
                            reinterpret_cast<const void *>(memory + compared),
                            count) != 0) {
                return error(LoadedRuntimeErrorCode::provider_replaced,
                             provider_component,
                             "loaded non-writable bytes disagree with provider file: " +
                                 mapping.path);
            }
            compared += count;
        }
    }

    contract::detail::Sha256Stream hash;
    std::array<std::byte, 64 * 1024> buffer{};
    std::uint64_t offset = 0;
    const auto file_size = static_cast<std::uint64_t>(descriptor_before.st_size);
    while (offset < file_size) {
        const auto requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), file_size - offset));
        auto destination = std::span<std::byte>(buffer).first(requested);
        if (!pread_exact(file.get(), destination, offset)) {
            return error(LoadedRuntimeErrorCode::provider_hash_failed,
                         provider_component,
                         "could not hash the complete provider file: " + mapping.path);
        }
        hash.update(destination);
        offset += requested;
    }
    std::byte extra{};
    ssize_t extra_count = -1;
    do {
        extra_count = ::pread(file.get(), &extra, 1, static_cast<off_t>(file_size));
    } while (extra_count < 0 && errno == EINTR);

    struct stat descriptor_after{};
    struct stat path_after{};
    if (extra_count != 0 || ::fstat(file.get(), &descriptor_after) != 0 ||
        ::stat(mapping.path.c_str(), &path_after) != 0 ||
        !same_stat(descriptor_before, descriptor_after) ||
        !same_stat(descriptor_before, path_after)) {
        return error(LoadedRuntimeErrorCode::provider_file_changed, provider_component,
                     "provider file changed while it was identified: " + mapping.path);
    }
    const auto digest = hash.finish();
    if (digest.is_zero()) {
        return error(LoadedRuntimeErrorCode::provider_hash_failed, provider_component,
                     "provider file produced an invalid all-zero SHA-256: " +
                         mapping.path);
    }
    return DynamicProviderIdentity{std::string(soname), memory_build_id, digest,
                                   file_size};
}

} // namespace crankwave::determinism::detail::linux_detail
