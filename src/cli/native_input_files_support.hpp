#pragma once

#include "native_input_files.hpp"

#include <optional>
#include <system_error>

namespace engine_sim_offline::cli::detail {

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
    void reset(int descriptor = -1) noexcept;

  private:
    int descriptor_ = -1;
};

struct ReadFile {
    std::filesystem::path canonical_path;
    std::vector<std::byte> bytes;
};

using ReadFileResult = std::variant<ReadFile, NativeInputError>;

struct OpenedAssetRoot {
    FileDescriptor descriptor;
    std::filesystem::path canonical_path;
};

using OpenedAssetRootResult =
    std::variant<OpenedAssetRoot, NativeInputError>;

[[nodiscard]] NativeInputError input_error(
    NativeInputErrorKind kind, NativeInputErrorCode code,
    NativeInputSubject subject, std::filesystem::path path, std::string message,
    std::string asset_id = {},
    std::optional<authoring::DiagnosticReport> diagnostics = std::nullopt);

[[nodiscard]] bool not_found(const std::error_code &error) noexcept;

[[nodiscard]] ReadFileResult read_exact_regular_file(
    const std::filesystem::path &path, NativeInputSubject subject,
    std::string_view asset_id, std::uintmax_t maximum_bytes);

[[nodiscard]] ReadFileResult read_exact_descriptor(
    int descriptor, std::filesystem::path display_path,
    NativeInputSubject subject, std::string_view asset_id,
    std::uintmax_t maximum_bytes);

[[nodiscard]] std::variant<std::filesystem::path, NativeInputError>
descriptor_path(int descriptor, NativeInputSubject subject,
                const std::filesystem::path &fallback_path,
                std::string_view asset_id = {});

[[nodiscard]] OpenedAssetRootResult
open_asset_root(const std::filesystem::path &asset_root);

[[nodiscard]] ReadFileResult read_confined_asset(
    const OpenedAssetRoot &asset_root,
    const std::filesystem::path &engine_document, std::string_view uri,
    NativeInputSubject subject, std::string_view asset_id,
    std::uintmax_t maximum_bytes);

[[nodiscard]] NativeEngineInputResult load_engine_impl(
    const std::filesystem::path &engine_path,
    const std::filesystem::path &asset_root, const NativeInputLimits &limits);

// Reads and parses the engine document and enforces its aggregate declared-asset
// limits without resolving any asset locator. Resolution adapters then append exact
// payload bytes either from authored URIs or from the built-in content catalog.
[[nodiscard]] NativeEngineInputResult load_engine_document_impl(
    const std::filesystem::path &engine_path,
    const NativeInputLimits &limits);

[[nodiscard]] std::optional<NativeInputError> append_confined_engine_asset(
    NativeEngineInput &input, const std::filesystem::path &locator_document,
    const OpenedAssetRoot &asset_root, compile::AssetKind kind,
    NativeInputSubject subject, std::string_view id, std::string_view uri,
    const NativeInputLimits &limits, std::uintmax_t &total_bytes);

[[nodiscard]] NativeScenarioInputResult load_scenario_impl(
    const std::filesystem::path &scenario_path,
    const NativeInputLimits &limits);

[[nodiscard]] NativeOutputError output_error(
    NativeOutputErrorKind kind, NativeOutputErrorCode code,
    std::filesystem::path path, std::string message);

[[nodiscard]] bool valid_portable_component(
    std::string_view component) noexcept;

} // namespace engine_sim_offline::cli::detail
