#include "vehicleengine_cli_support.hpp"

#include "artifacts/secure_filesystem_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::cli {
namespace {

[[nodiscard]] VehicleEngineCliError failure(VehicleEngineCliErrorKind kind,
                                        std::string message) {
    return {kind, std::move(message)};
}

[[nodiscard]] VehicleEngineCliError cancelled() {
    return failure(VehicleEngineCliErrorKind::cancelled,
                   "VEHICLEENGINE operation was cancelled");
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

[[nodiscard]] VehicleEngineCliError
container_failure(const artifacts::VehicleEngineContainerError &error) {
    return failure(
        VehicleEngineCliErrorKind::data_error,
        contextual_message("invalid VEHICLEENGINE container", error.path, error.message));
}

[[nodiscard]] VehicleEngineCliError
package_failure(const artifacts::VehicleEnginePackageError &error) {
    return failure(
        VehicleEngineCliErrorKind::data_error,
        contextual_message("invalid VEHICLEENGINE package", error.path, error.message));
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

using ReadFileResult = std::variant<std::vector<std::byte>, VehicleEngineCliError>;

[[nodiscard]] ReadFileResult read_regular_file(const std::filesystem::path &path,
                                               const std::uint64_t maximum_byte_count,
                                               const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(path, status_error);
    if (status_error || !std::filesystem::exists(status)) {
        return failure(VehicleEngineCliErrorKind::no_input,
                       "input file does not exist or cannot be inspected: " +
                           path.string());
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        return failure(VehicleEngineCliErrorKind::data_error,
                       "input must be a regular non-symlink file: " + path.string());
    }
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        return failure(VehicleEngineCliErrorKind::unavailable,
                       "input file size cannot be read: " + path.string());
    }
    if (file_size > maximum_byte_count ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size >
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return failure(VehicleEngineCliErrorKind::data_error,
                       "input file exceeds its byte limit: " + path.string());
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return failure(VehicleEngineCliErrorKind::no_input,
                       "input file cannot be opened: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    constexpr std::size_t chunk_byte_count = 1024U * 1024U;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return cancelled();
        }
        const auto count = std::min(chunk_byte_count, bytes.size() - offset);
        stream.read(reinterpret_cast<char *>(bytes.data() + offset),
                    static_cast<std::streamsize>(count));
        if (stream.gcount() != static_cast<std::streamsize>(count)) {
            return failure(VehicleEngineCliErrorKind::unavailable,
                           "input file changed or could not be read completely: " +
                               path.string());
        }
        offset += count;
    }
    if (stream.peek() != std::char_traits<char>::eof()) {
        return failure(VehicleEngineCliErrorKind::unavailable,
                       "input file changed while it was being read: " + path.string());
    }
    return stop_token.stop_requested() ? ReadFileResult{cancelled()}
                                       : ReadFileResult{std::move(bytes)};
}

[[nodiscard]] std::variant<std::vector<OwnedEntry>, VehicleEngineCliError>
read_package_tree(const std::filesystem::path &root, const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    std::error_code status_error;
    const auto root_status = std::filesystem::symlink_status(root, status_error);
    if (status_error || !std::filesystem::exists(root_status)) {
        return failure(VehicleEngineCliErrorKind::no_input,
                       "package directory does not exist or cannot be inspected: " +
                           root.string());
    }
    if (std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return failure(VehicleEngineCliErrorKind::data_error,
                       "package root must be a non-symlink directory: " +
                           root.string());
    }

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator{
        root, std::filesystem::directory_options::none, iterator_error};
    if (iterator_error) {
        return failure(VehicleEngineCliErrorKind::unavailable,
                       "package directory cannot be traversed: " + root.string());
    }

    std::vector<OwnedEntry> entries;
    std::uint64_t total_payload_bytes = 0;
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (stop_token.stop_requested()) {
            return cancelled();
        }
        const auto path = iterator->path();
        std::error_code entry_status_error;
        const auto status = std::filesystem::symlink_status(path, entry_status_error);
        if (entry_status_error) {
            return failure(VehicleEngineCliErrorKind::unavailable,
                           "package entry cannot be inspected: " + path.string());
        }
        const auto relative = path.lexically_relative(root).generic_string();
        if (!artifacts::is_portable_vehicleengine_path(relative)) {
            return failure(VehicleEngineCliErrorKind::data_error,
                           "package entry has a nonportable relative path: " +
                               relative);
        }
        if (std::filesystem::is_symlink(status)) {
            return failure(VehicleEngineCliErrorKind::data_error,
                           "package tree contains a symlink: " + relative);
        }
        if (std::filesystem::is_directory(status)) {
            iterator.increment(iterator_error);
            if (iterator_error) {
                return failure(VehicleEngineCliErrorKind::unavailable,
                               "package directory traversal failed");
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            return failure(VehicleEngineCliErrorKind::data_error,
                           "package tree contains a nonregular entry: " + relative);
        }
        if (entries.size() >= artifacts::kVehicleEngineMaximumEntryCountV1) {
            return failure(VehicleEngineCliErrorKind::data_error,
                           "package tree exceeds the VEHICLEENGINE entry limit");
        }
        auto read = read_regular_file(
            path, artifacts::kVehicleEngineMaximumEntryByteCountV1, stop_token);
        if (const auto *error = std::get_if<VehicleEngineCliError>(&read)) {
            return *error;
        }
        auto payload = std::get<std::vector<std::byte>>(std::move(read));
        if (payload.size() >
            artifacts::kVehicleEngineMaximumContainerByteCountV1 - total_payload_bytes) {
            return failure(VehicleEngineCliErrorKind::data_error,
                           "package tree exceeds the VEHICLEENGINE container limit");
        }
        total_payload_bytes += payload.size();
        entries.push_back({relative, std::move(payload)});

        iterator.increment(iterator_error);
        if (iterator_error) {
            return failure(VehicleEngineCliErrorKind::unavailable,
                           "package directory traversal failed");
        }
    }
    return stop_token.stop_requested()
               ? std::variant<std::vector<OwnedEntry>, VehicleEngineCliError>{cancelled()}
               : std::variant<std::vector<OwnedEntry>, VehicleEngineCliError>{
                     std::move(entries)};
}

[[nodiscard]] std::vector<artifacts::VehicleEnginePackEntry>
entry_views(const std::vector<OwnedEntry> &entries) {
    std::vector<artifacts::VehicleEnginePackEntry> views;
    views.reserve(entries.size());
    for (const auto &entry : entries) {
        views.push_back({entry.path, entry.bytes});
    }
    return views;
}

[[nodiscard]] std::variant<std::monostate, VehicleEngineCliError>
write_new_file(const std::filesystem::path &output,
               const std::span<const std::byte> bytes,
               const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    if (output.extension() != ".vehicleengine") {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "output file must use the .vehicleengine extension");
    }
    std::error_code output_status_error;
    const auto output_status =
        std::filesystem::symlink_status(output, output_status_error);
    if (!output_status_error && std::filesystem::exists(output_status)) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "output file already exists: " + output.string());
    }
    if (output_status_error &&
        output_status_error != std::errc::no_such_file_or_directory) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "output path cannot be inspected: " + output.string());
    }
    const auto parent =
        output.has_parent_path() ? output.parent_path() : std::filesystem::path{"."};
    std::error_code parent_status_error;
    const auto parent_status =
        std::filesystem::symlink_status(parent, parent_status_error);
    if (parent_status_error || std::filesystem::is_symlink(parent_status) ||
        !std::filesystem::is_directory(parent_status)) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "output parent must be an existing non-symlink directory");
    }

#if defined(__linux__)
    using artifacts::detail::FileDescriptor;
    FileDescriptor parent_descriptor{
        ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (!parent_descriptor.valid()) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       artifacts::detail::errno_message(
                           "output parent cannot be securely opened", errno));
    }

    std::string temporary_name;
    FileDescriptor temporary_descriptor;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary_name = artifacts::detail::random_stage_name();
        temporary_descriptor.reset(
            ::openat(parent_descriptor.get(), temporary_name.c_str(),
                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0666));
        if (temporary_descriptor.valid()) {
            break;
        }
        if (errno != EEXIST) {
            return failure(
                VehicleEngineCliErrorKind::cant_create,
                artifacts::detail::errno_message(
                    "private VEHICLEENGINE temporary file cannot be created", errno));
        }
    }
    if (!temporary_descriptor.valid()) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "private VEHICLEENGINE temporary name allocation was exhausted");
    }

    struct TemporaryCleanup {
        int parent = -1;
        std::string name;
        bool active = true;

        ~TemporaryCleanup() {
            if (active) {
                static_cast<void>(::unlinkat(parent, name.c_str(), 0));
            }
        }
    } cleanup{parent_descriptor.get(), temporary_name};

    constexpr std::size_t chunk_byte_count = 1024U * 1024U;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return cancelled();
        }
        const auto requested = std::min(chunk_byte_count, bytes.size() - offset);
        const auto count =
            ::write(temporary_descriptor.get(), bytes.data() + offset, requested);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        return failure(VehicleEngineCliErrorKind::cant_create,
                       artifacts::detail::errno_message(
                           "VEHICLEENGINE bytes could not be written completely", errno));
    }
    while (::fsync(temporary_descriptor.get()) == -1) {
        if (errno == EINTR) {
            if (stop_token.stop_requested()) {
                return cancelled();
            }
            continue;
        }
        return failure(
            VehicleEngineCliErrorKind::cant_create,
            artifacts::detail::errno_message(
                "VEHICLEENGINE temporary file could not be synchronized", errno));
    }
    if (stop_token.stop_requested()) {
        return cancelled();
    }

    const auto output_name = output.filename().string();
    if (::linkat(parent_descriptor.get(), temporary_name.c_str(),
                 parent_descriptor.get(), output_name.c_str(), 0) == -1) {
        const auto error_number = errno;
        return failure(VehicleEngineCliErrorKind::cant_create,
                       artifacts::detail::errno_message(
                           "VEHICLEENGINE output could not be published without overwrite",
                           error_number));
    }

    if (::unlinkat(parent_descriptor.get(), temporary_name.c_str(), 0) == -1) {
        const auto cleanup_error = errno;
        static_cast<void>(::unlinkat(parent_descriptor.get(), output_name.c_str(), 0));
        return failure(VehicleEngineCliErrorKind::cant_create,
                       artifacts::detail::errno_message(
                           "published VEHICLEENGINE temporary link could not be removed",
                           cleanup_error));
    }
    cleanup.active = false;
    temporary_descriptor.reset();

    while (::fsync(parent_descriptor.get()) == -1) {
        if (errno == EINTR) {
            continue;
        }
        const auto sync_error = errno;
        static_cast<void>(::unlinkat(parent_descriptor.get(), output_name.c_str(), 0));
        return failure(
            VehicleEngineCliErrorKind::cant_create,
            artifacts::detail::errno_message(
                "VEHICLEENGINE output directory could not be synchronized", sync_error));
    }
    return std::monostate{};
#else
    static_cast<void>(bytes);
    return failure(
        VehicleEngineCliErrorKind::unavailable,
        "secure no-replace VEHICLEENGINE publication is unavailable on this platform");
#endif
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

PackVehicleEngineFileResult
pack_vehicleengine_package_directory(const std::filesystem::path &package_directory,
                                 const std::filesystem::path &new_output_file,
                                 const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    std::error_code canonical_error;
    const auto canonical_root =
        std::filesystem::canonical(package_directory, canonical_error);
    if (canonical_error) {
        return failure(VehicleEngineCliErrorKind::no_input,
                       "package directory cannot be resolved");
    }
    const auto output_parent = new_output_file.has_parent_path()
                                   ? new_output_file.parent_path()
                                   : std::filesystem::path{"."};
    const auto canonical_output_parent =
        std::filesystem::canonical(output_parent, canonical_error);
    if (canonical_error) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "output parent directory cannot be resolved");
    }
    const auto canonical_output =
        (canonical_output_parent / new_output_file.filename()).lexically_normal();
    if (path_is_within(canonical_output, canonical_root)) {
        return failure(VehicleEngineCliErrorKind::cant_create,
                       "VEHICLEENGINE output must be outside the package directory");
    }

    auto read = read_package_tree(package_directory, stop_token);
    if (const auto *error = std::get_if<VehicleEngineCliError>(&read)) {
        return *error;
    }
    auto entries = std::get<std::vector<OwnedEntry>>(std::move(read));
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    const auto views = entry_views(entries);
    const auto tree_validation = artifacts::validate_vehicleengine_package_tree(views);
    if (const auto *error =
            std::get_if<artifacts::VehicleEnginePackageError>(&tree_validation)) {
        return package_failure(*error);
    }
    if (stop_token.stop_requested()) {
        return cancelled();
    }

    auto packed = artifacts::pack_vehicleengine_v1(views);
    if (const auto *error = std::get_if<artifacts::VehicleEngineContainerError>(&packed)) {
        return container_failure(*error);
    }
    auto container = std::get<std::vector<std::byte>>(std::move(packed));
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    const auto container_sha256 = contract::sha256(container);
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    const auto write = write_new_file(new_output_file, container, stop_token);
    if (const auto *error = std::get_if<VehicleEngineCliError>(&write)) {
        return *error;
    }
    return PackedVehicleEngineFile{
        new_output_file,
        container.size(),
        static_cast<std::uint32_t>(views.size()),
        container_sha256,
    };
}

LoadVehicleEngineFileResult inspect_vehicleengine_file(const std::filesystem::path &input_file,
                                               const bool verify_payloads,
                                               const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    auto read = read_regular_file(
        input_file, artifacts::kVehicleEngineMaximumContainerByteCountV1, stop_token);
    if (const auto *error = std::get_if<VehicleEngineCliError>(&read)) {
        return *error;
    }
    auto container = std::get<std::vector<std::byte>>(std::move(read));
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    auto inspected = verify_payloads ? artifacts::verify_vehicleengine(container)
                                     : artifacts::inspect_vehicleengine(container);
    if (const auto *error =
            std::get_if<artifacts::VehicleEngineContainerError>(&inspected)) {
        return container_failure(*error);
    }
    auto index = std::get<artifacts::VehicleEngineContainerIndex>(std::move(inspected));
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    LoadedVehicleEngineFile result{
        std::move(index), contract::sha256(container), verify_payloads, {}};
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    if (!verify_payloads) {
        return result;
    }

    std::vector<artifacts::VehicleEnginePackEntry> views;
    views.reserve(result.index.entries.size());
    for (const auto &entry : result.index.entries) {
        views.push_back(
            {entry.path, artifacts::vehicleengine_entry_payload(container, entry)});
    }
    auto tree_validation = artifacts::validate_vehicleengine_package_tree(views);
    if (const auto *error =
            std::get_if<artifacts::VehicleEnginePackageError>(&tree_validation)) {
        return package_failure(*error);
    }
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    result.package =
        std::get<artifacts::VehicleEnginePackageDescriptor>(std::move(tree_validation));
    return result;
}

} // namespace engine_sim_offline::cli
