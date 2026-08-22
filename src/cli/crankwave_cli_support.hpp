#pragma once

#include "crankwave/artifacts/crankwave_container.hpp"
#include "crankwave/artifacts/crankwave_package.hpp"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <variant>

namespace crankwave::cli {

enum class CrankwaveCliErrorKind : std::uint8_t {
    data_error,
    no_input,
    cant_create,
    unavailable,
    cancelled,
};

struct CrankwaveCliError {
    CrankwaveCliErrorKind kind = CrankwaveCliErrorKind::data_error;
    std::string message;
};

struct PackedCrankwaveFile {
    std::filesystem::path output_path;
    std::uint64_t container_byte_count = 0;
    std::uint32_t entry_count = 0;
    contract::Sha256Digest container_sha256;
};

struct LoadedCrankwaveFile {
    artifacts::CrankwaveContainerIndex index;
    contract::Sha256Digest container_sha256;
    bool fully_verified = false;
    std::variant<std::monostate, artifacts::CrankwavePackageDescriptor> package;
};

using PackCrankwaveFileResult = std::variant<PackedCrankwaveFile, CrankwaveCliError>;
using LoadCrankwaveFileResult = std::variant<LoadedCrankwaveFile, CrankwaveCliError>;

[[nodiscard]] PackCrankwaveFileResult
pack_crankwave_package_directory(const std::filesystem::path &package_directory,
                                 const std::filesystem::path &new_output_file,
                                 std::stop_token stop_token = {});

[[nodiscard]] LoadCrankwaveFileResult
inspect_crankwave_file(const std::filesystem::path &input_file, bool verify_payloads,
                       std::stop_token stop_token = {});

[[nodiscard]] std::string sha256_lower_hex(const contract::Sha256Digest &digest);

} // namespace crankwave::cli
