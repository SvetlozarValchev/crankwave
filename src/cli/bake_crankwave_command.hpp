#pragma once

#include "crankwave/authoring/diagnostic.hpp"
#include "crankwave/contract/common.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

namespace crankwave::cli {

enum class BakeCrankwaveErrorKind : std::uint8_t {
    data_error,
    no_input,
    unavailable,
    software,
    cant_create,
    temporary_failure,
    cancelled,
};

struct BakeCrankwaveError {
    BakeCrankwaveErrorKind kind = BakeCrankwaveErrorKind::software;
    std::string code;
    std::string stage;
    std::filesystem::path path;
    std::string message;
    std::optional<authoring::DiagnosticReport> diagnostics;
    std::optional<contract::ValidationReport> validation;

    friend bool operator==(const BakeCrankwaveError &,
                           const BakeCrankwaveError &) = default;
};

struct BakeCrankwaveRequest {
    std::filesystem::path engine_path;
    std::filesystem::path output_file;
    std::optional<std::filesystem::path> asset_root;
    std::string release_identity;
};

struct BakedCrankwaveFile {
    std::filesystem::path output_path;
    std::string engine_id;
    std::string profile_id;
    std::uint64_t container_byte_count = 0U;
    std::size_t entry_count = 0U;
    std::size_t held_cell_count = 0U;
    std::size_t directional_capture_count = 0U;
    std::size_t lifecycle_capture_count = 0U;
    contract::Sha256Digest container_sha256;
    contract::Sha256Digest cache_identity_sha256;
    bool verified = false;
};

using BakeCrankwaveResult = std::variant<BakedCrankwaveFile, BakeCrankwaveError>;

[[nodiscard]] BakeCrankwaveResult
bake_crankwave_native(const BakeCrankwaveRequest &request,
                      std::stop_token stop_token = {});

[[nodiscard]] std::string_view
bake_crankwave_error_kind_label(BakeCrankwaveErrorKind kind) noexcept;

} // namespace crankwave::cli
