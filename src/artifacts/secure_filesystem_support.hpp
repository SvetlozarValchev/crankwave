#pragma once

#include "engine_sim_offline/render.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if defined(__linux__)

namespace engine_sim_offline::artifacts::detail {

class FileDescriptor {
  public:
    FileDescriptor() = default;
    explicit FileDescriptor(int descriptor) noexcept;
    ~FileDescriptor();

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept;
    FileDescriptor &operator=(FileDescriptor &&other) noexcept;

    [[nodiscard]] int get() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;
    void reset(int descriptor = -1) noexcept;

  private:
    int descriptor_ = -1;
};

[[nodiscard]] std::string errno_message(std::string_view operation, int error_number);
[[nodiscard]] bool remove_tree_entry_at(int parent_fd, const char *name) noexcept;
[[nodiscard]] bool remove_tree_entry_by_identity(int parent_fd, std::uintmax_t device,
                                                 std::uintmax_t inode) noexcept;
[[nodiscard]] bool sync_directory_tree(int directory_fd);
[[nodiscard]] std::string random_stage_name();

struct DirectoryTreeEntry {
    std::string relative_path;
    bool directory = false;
};

[[nodiscard]] std::variant<std::vector<DirectoryTreeEntry>, RenderSinkError>
inventory_directory_tree(int directory_fd, std::size_t maximum_entries);

[[nodiscard]] std::variant<FileDescriptor, RenderSinkError>
create_file_beneath(int stage_fd, std::string_view relative_path);
[[nodiscard]] std::variant<FileDescriptor, RenderSinkError>
open_file_beneath(int stage_fd, std::string_view relative_path);
[[nodiscard]] bool write_all_at(int descriptor, std::uint64_t offset,
                                std::span<const std::byte> bytes);
[[nodiscard]] int rename_noreplace(int parent_fd, const char *source,
                                   const char *destination);

} // namespace engine_sim_offline::artifacts::detail

#endif
