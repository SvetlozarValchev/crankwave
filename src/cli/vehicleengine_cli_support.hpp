#pragma once

#include "engine_sim_offline/artifacts/vehicleengine_container.hpp"
#include "engine_sim_offline/artifacts/vehicleengine_package.hpp"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <variant>

namespace engine_sim_offline::cli {

enum class VehicleEngineCliErrorKind : std::uint8_t {
    data_error,
    no_input,
    cant_create,
    unavailable,
    cancelled,
};

struct VehicleEngineCliError {
    VehicleEngineCliErrorKind kind = VehicleEngineCliErrorKind::data_error;
    std::string message;
};

struct PackedVehicleEngineFile {
    std::filesystem::path output_path;
    std::uint64_t container_byte_count = 0;
    std::uint32_t entry_count = 0;
    contract::Sha256Digest container_sha256;
};

struct LoadedVehicleEngineFile {
    artifacts::VehicleEngineContainerIndex index;
    contract::Sha256Digest container_sha256;
    bool fully_verified = false;
    std::variant<std::monostate, artifacts::VehicleEnginePackageDescriptor> package;
};

using PackVehicleEngineFileResult = std::variant<PackedVehicleEngineFile, VehicleEngineCliError>;
using LoadVehicleEngineFileResult = std::variant<LoadedVehicleEngineFile, VehicleEngineCliError>;

[[nodiscard]] PackVehicleEngineFileResult
pack_vehicleengine_package_directory(const std::filesystem::path &package_directory,
                                 const std::filesystem::path &new_output_file,
                                 std::stop_token stop_token = {});

[[nodiscard]] LoadVehicleEngineFileResult
inspect_vehicleengine_file(const std::filesystem::path &input_file, bool verify_payloads,
                       std::stop_token stop_token = {});

[[nodiscard]] std::string sha256_lower_hex(const contract::Sha256Digest &digest);

} // namespace engine_sim_offline::cli
