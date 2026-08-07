#include "revengine_cli_support.hpp"

#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace engine_sim_offline::cli {
namespace {

[[nodiscard]] RevengineCliError failure(RevengineCliErrorKind kind,
                                        std::string message) {
    return {kind, std::move(message)};
}

[[nodiscard]] std::string contextual_message(const std::string_view prefix,
                                             const std::string_view path,
                                             const std::string_view message) {
    std::string result{prefix};
    if (!path.empty()) {
        result += " '";
        result += path;
        result += "'";
    }
    result += ": ";
    result += message;
    return result;
}

[[nodiscard]] RevengineCliError
container_failure(const artifacts::RevengineContainerError &error) {
    return failure(
        RevengineCliErrorKind::data_error,
        contextual_message("invalid REVENGINE container", error.path, error.message));
}

[[nodiscard]] RevengineCliError
package_failure(const artifacts::RevenginePackageError &error) {
    return failure(
        RevengineCliErrorKind::data_error,
        contextual_message("invalid REVENGINE package", error.path, error.message));
}

[[nodiscard]] bool path_is_within(const std::filesystem::path &candidate,
                                  const std::filesystem::path &root) {
    auto candidate_it = candidate.begin();
    for (auto root_it = root.begin(); root_it != root.end();
         ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *candidate_it != *root_it) {
            return false;
        }
    }
    return true;
}

struct OwnedEntry {
    std::string path;
    std::vector<std::byte> bytes;
};

using ReadFileResult = std::variant<std::vector<std::byte>, RevengineCliError>;

[[nodiscard]] ReadFileResult read_regular_file(const std::filesystem::path &path,
                                               const std::uint64_t maximum_byte_count) {
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(path, status_error);
    if (status_error || !std::filesystem::exists(status)) {
        return failure(RevengineCliErrorKind::no_input,
                       "input file does not exist or cannot be inspected: " +
                           path.string());
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        return failure(RevengineCliErrorKind::data_error,
                       "input must be a regular non-symlink file: " + path.string());
    }
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        return failure(RevengineCliErrorKind::unavailable,
                       "input file size cannot be read: " + path.string());
    }
    if (file_size > maximum_byte_count ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size >
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return failure(RevengineCliErrorKind::data_error,
                       "input file exceeds its byte limit: " + path.string());
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return failure(RevengineCliErrorKind::no_input,
                       "input file cannot be opened: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return failure(RevengineCliErrorKind::unavailable,
                           "input file changed or could not be read completely: " +
                               path.string());
        }
    }
    if (stream.peek() != std::char_traits<char>::eof()) {
        return failure(RevengineCliErrorKind::unavailable,
                       "input file changed while it was being read: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::variant<std::vector<OwnedEntry>, RevengineCliError>
read_package_tree(const std::filesystem::path &root) {
    std::error_code status_error;
    const auto root_status = std::filesystem::symlink_status(root, status_error);
    if (status_error || !std::filesystem::exists(root_status)) {
        return failure(RevengineCliErrorKind::no_input,
                       "package directory does not exist or cannot be inspected: " +
                           root.string());
    }
    if (std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return failure(RevengineCliErrorKind::data_error,
                       "package root must be a non-symlink directory: " +
                           root.string());
    }

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator{
        root, std::filesystem::directory_options::none, iterator_error};
    if (iterator_error) {
        return failure(RevengineCliErrorKind::unavailable,
                       "package directory cannot be traversed: " + root.string());
    }

    std::vector<OwnedEntry> entries;
    std::uint64_t total_payload_bytes = 0;
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto path = iterator->path();
        std::error_code entry_status_error;
        const auto status = std::filesystem::symlink_status(path, entry_status_error);
        if (entry_status_error) {
            return failure(RevengineCliErrorKind::unavailable,
                           "package entry cannot be inspected: " + path.string());
        }
        const auto relative = path.lexically_relative(root).generic_string();
        if (!artifacts::is_portable_revengine_path(relative)) {
            return failure(RevengineCliErrorKind::data_error,
                           "package entry has a nonportable relative path: " +
                               relative);
        }
        if (std::filesystem::is_symlink(status)) {
            return failure(RevengineCliErrorKind::data_error,
                           "package tree contains a symlink: " + relative);
        }
        if (std::filesystem::is_directory(status)) {
            iterator.increment(iterator_error);
            if (iterator_error) {
                return failure(RevengineCliErrorKind::unavailable,
                               "package directory traversal failed");
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            return failure(RevengineCliErrorKind::data_error,
                           "package tree contains a nonregular entry: " + relative);
        }
        if (entries.size() >= artifacts::kRevengineMaximumEntryCountV1) {
            return failure(RevengineCliErrorKind::data_error,
                           "package tree exceeds the REVENGINE entry limit");
        }
        auto read =
            read_regular_file(path, artifacts::kRevengineMaximumEntryByteCountV1);
        if (const auto *error = std::get_if<RevengineCliError>(&read)) {
            return *error;
        }
        auto payload = std::get<std::vector<std::byte>>(std::move(read));
        if (payload.size() >
            artifacts::kRevengineMaximumContainerByteCountV1 - total_payload_bytes) {
            return failure(RevengineCliErrorKind::data_error,
                           "package tree exceeds the REVENGINE container limit");
        }
        total_payload_bytes += payload.size();
        entries.push_back({relative, std::move(payload)});

        iterator.increment(iterator_error);
        if (iterator_error) {
            return failure(RevengineCliErrorKind::unavailable,
                           "package directory traversal failed");
        }
    }
    return entries;
}

[[nodiscard]] std::vector<artifacts::RevenginePackEntry>
entry_views(const std::vector<OwnedEntry> &entries) {
    std::vector<artifacts::RevenginePackEntry> views;
    views.reserve(entries.size());
    for (const auto &entry : entries) {
        views.push_back({entry.path, entry.bytes});
    }
    return views;
}

[[nodiscard]] std::variant<std::monostate, RevengineCliError>
write_new_file(const std::filesystem::path &output,
               const std::span<const std::byte> bytes) {
    if (output.extension() != ".revengine") {
        return failure(RevengineCliErrorKind::cant_create,
                       "output file must use the .revengine extension");
    }
    std::error_code output_status_error;
    const auto output_status =
        std::filesystem::symlink_status(output, output_status_error);
    if (!output_status_error && std::filesystem::exists(output_status)) {
        return failure(RevengineCliErrorKind::cant_create,
                       "output file already exists: " + output.string());
    }
    if (output_status_error &&
        output_status_error != std::errc::no_such_file_or_directory) {
        return failure(RevengineCliErrorKind::cant_create,
                       "output path cannot be inspected: " + output.string());
    }
    const auto parent =
        output.has_parent_path() ? output.parent_path() : std::filesystem::path{"."};
    std::error_code parent_status_error;
    const auto parent_status =
        std::filesystem::symlink_status(parent, parent_status_error);
    if (parent_status_error || std::filesystem::is_symlink(parent_status) ||
        !std::filesystem::is_directory(parent_status)) {
        return failure(RevengineCliErrorKind::cant_create,
                       "output parent must be an existing non-symlink directory");
    }

    const auto temporary = parent / ("." + output.filename().string() + ".tmp");
    std::error_code temporary_status_error;
    const auto temporary_status =
        std::filesystem::symlink_status(temporary, temporary_status_error);
    if (!temporary_status_error && std::filesystem::exists(temporary_status)) {
        return failure(RevengineCliErrorKind::cant_create,
                       "temporary output path already exists: " + temporary.string());
    }
    if (temporary_status_error &&
        temporary_status_error != std::errc::no_such_file_or_directory) {
        return failure(RevengineCliErrorKind::cant_create,
                       "temporary output path cannot be inspected");
    }

    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::out};
        if (!stream) {
            return failure(RevengineCliErrorKind::cant_create,
                           "temporary output file cannot be created");
        }
        stream.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            stream.close();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return failure(RevengineCliErrorKind::cant_create,
                           "REVENGINE bytes could not be written completely");
        }
    }

    std::error_code publish_error;
    std::filesystem::create_hard_link(temporary, output, publish_error);
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    if (publish_error) {
        return failure(RevengineCliErrorKind::cant_create,
                       "REVENGINE output could not be published without overwrite");
    }
    return std::monostate{};
}

} // namespace

std::string sha256_lower_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto value : digest.bytes) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

PackRevengineFileResult
pack_revengine_package_directory(const std::filesystem::path &package_directory,
                                 const std::filesystem::path &new_output_file) {
    std::error_code canonical_error;
    const auto canonical_root =
        std::filesystem::canonical(package_directory, canonical_error);
    if (canonical_error) {
        return failure(RevengineCliErrorKind::no_input,
                       "package directory cannot be resolved");
    }
    const auto output_parent = new_output_file.has_parent_path()
                                   ? new_output_file.parent_path()
                                   : std::filesystem::path{"."};
    const auto canonical_output_parent =
        std::filesystem::canonical(output_parent, canonical_error);
    if (canonical_error) {
        return failure(RevengineCliErrorKind::cant_create,
                       "output parent directory cannot be resolved");
    }
    const auto canonical_output =
        (canonical_output_parent / new_output_file.filename()).lexically_normal();
    if (path_is_within(canonical_output, canonical_root)) {
        return failure(RevengineCliErrorKind::cant_create,
                       "REVENGINE output must be outside the package directory");
    }

    auto read = read_package_tree(package_directory);
    if (const auto *error = std::get_if<RevengineCliError>(&read)) {
        return *error;
    }
    auto entries = std::get<std::vector<OwnedEntry>>(std::move(read));
    const auto views = entry_views(entries);
    const auto tree_validation = artifacts::validate_revengine_package_tree(views);
    if (const auto *error =
            std::get_if<artifacts::RevenginePackageError>(&tree_validation)) {
        return package_failure(*error);
    }

    auto packed = artifacts::pack_revengine_v1(views);
    if (const auto *error = std::get_if<artifacts::RevengineContainerError>(&packed)) {
        return container_failure(*error);
    }
    auto container = std::get<std::vector<std::byte>>(std::move(packed));
    const auto container_sha256 = contract::sha256(container);
    const auto write = write_new_file(new_output_file, container);
    if (const auto *error = std::get_if<RevengineCliError>(&write)) {
        return *error;
    }
    return PackedRevengineFile{
        new_output_file,
        container.size(),
        static_cast<std::uint32_t>(views.size()),
        container_sha256,
    };
}

LoadRevengineFileResult inspect_revengine_file(const std::filesystem::path &input_file,
                                               const bool verify_payloads) {
    auto read =
        read_regular_file(input_file, artifacts::kRevengineMaximumContainerByteCountV1);
    if (const auto *error = std::get_if<RevengineCliError>(&read)) {
        return *error;
    }
    auto container = std::get<std::vector<std::byte>>(std::move(read));
    auto inspected = verify_payloads ? artifacts::verify_revengine(container)
                                     : artifacts::inspect_revengine(container);
    if (const auto *error =
            std::get_if<artifacts::RevengineContainerError>(&inspected)) {
        return container_failure(*error);
    }
    auto index = std::get<artifacts::RevengineContainerIndex>(std::move(inspected));
    LoadedRevengineFile result{
        std::move(index), contract::sha256(container), verify_payloads, {}};
    if (!verify_payloads) {
        return result;
    }

    std::vector<artifacts::RevenginePackEntry> views;
    views.reserve(result.index.entries.size());
    for (const auto &entry : result.index.entries) {
        views.push_back(
            {entry.path, artifacts::revengine_entry_payload(container, entry)});
    }
    auto tree_validation = artifacts::validate_revengine_package_tree(views);
    if (const auto *error =
            std::get_if<artifacts::RevenginePackageError>(&tree_validation)) {
        return package_failure(*error);
    }
    result.package =
        std::get<artifacts::RevenginePackageDescriptor>(std::move(tree_validation));
    return result;
}

} // namespace engine_sim_offline::cli
