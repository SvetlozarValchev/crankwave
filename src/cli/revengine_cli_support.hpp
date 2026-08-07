#pragma once

#include "engine_sim_offline/artifacts/revengine_container.hpp"
#include "engine_sim_offline/artifacts/revengine_package.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace engine_sim_offline::cli {

enum class RevengineCliErrorKind : std::uint8_t {
    data_error,
    no_input,
    cant_create,
    unavailable,
};

struct RevengineCliError {
    RevengineCliErrorKind kind = RevengineCliErrorKind::data_error;
    std::string message;
};

struct PackedRevengineFile {
    std::filesystem::path output_path;
    std::uint64_t container_byte_count = 0;
    std::uint32_t entry_count = 0;
    contract::Sha256Digest container_sha256;
};

struct LoadedRevengineFile {
    artifacts::RevengineContainerIndex index;
    contract::Sha256Digest container_sha256;
    bool fully_verified = false;
    std::variant<std::monostate, artifacts::RevenginePackageDescriptor> package;
};

using PackRevengineFileResult = std::variant<PackedRevengineFile, RevengineCliError>;
using LoadRevengineFileResult = std::variant<LoadedRevengineFile, RevengineCliError>;

[[nodiscard]] PackRevengineFileResult
pack_revengine_package_directory(const std::filesystem::path &package_directory,
                                 const std::filesystem::path &new_output_file);

[[nodiscard]] LoadRevengineFileResult
inspect_revengine_file(const std::filesystem::path &input_file, bool verify_payloads);

[[nodiscard]] std::string sha256_lower_hex(const contract::Sha256Digest &digest);

} // namespace engine_sim_offline::cli
