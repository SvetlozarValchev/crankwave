#pragma once

#include "crankwave/contract/common.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::determinism {

// Content identity of one already-loaded ELF provider. Loader paths, mapping
// addresses, device/inode numbers, and timestamps are deliberately excluded.
struct GnuBuildId {
    std::array<std::uint8_t, 64> bytes{};
    std::uint8_t size = 0;

    [[nodiscard]] bool is_zero() const noexcept;

    friend bool operator==(const GnuBuildId &, const GnuBuildId &) = default;
};

struct DynamicProviderIdentity {
    std::string soname;
    GnuBuildId gnu_build_id;
    contract::Sha256Digest file_sha256;
    std::uint64_t file_size = 0;

    friend bool operator==(const DynamicProviderIdentity &,
                           const DynamicProviderIdentity &) = default;
};

struct VersionedSymbolIdentity {
    std::string name;
    std::string version;
    std::uint64_t provider_offset = 0;

    friend bool operator==(const VersionedSymbolIdentity &,
                           const VersionedSymbolIdentity &) = default;
};

struct LibStdCppHeaderIdentity {
    std::uint32_t release = 0;
    std::uint32_t header_date = 0;
    std::uint32_t gxx_abi_version = 0;
    bool use_cxx11_abi = false;

    friend bool operator==(const LibStdCppHeaderIdentity &,
                           const LibStdCppHeaderIdentity &) = default;
};

struct LoadedRuntimeIdentity {
    std::string standard_library_id;
    std::string standard_library_identity;
    LibStdCppHeaderIdentity standard_library_headers;
    DynamicProviderIdentity standard_library_provider;
    VersionedSymbolIdentity standard_library_anchor;

    std::string math_library_id;
    std::string math_library_identity;
    DynamicProviderIdentity math_library_provider;
    std::array<VersionedSymbolIdentity, 7> math_symbols;

    std::string compiler_runtime_id;
    std::string compiler_runtime_identity;
    DynamicProviderIdentity compiler_runtime_provider;
    VersionedSymbolIdentity compiler_runtime_anchor;

    friend bool operator==(const LoadedRuntimeIdentity &,
                           const LoadedRuntimeIdentity &) = default;
};

enum class LoadedRuntimeErrorCode {
    unsupported_platform,
    unsupported_headers,
    provider_missing,
    provider_metadata_invalid,
    symbol_missing,
    symbol_interposed,
    symbol_outside_provider,
    mapping_unavailable,
    provider_replaced,
    provider_file_invalid,
    provider_file_changed,
    provider_file_too_large,
    provider_hash_failed,
};

struct LoadedRuntimeError {
    LoadedRuntimeErrorCode code = LoadedRuntimeErrorCode::unsupported_platform;
    std::string component;
    std::string symbol;
    std::string message;

    friend bool operator==(const LoadedRuntimeError &,
                           const LoadedRuntimeError &) = default;
};

using LoadedRuntimeIdentityResult =
    std::variant<LoadedRuntimeIdentity, LoadedRuntimeError>;

// Observes only providers already present in the process. It never loads a missing
// library and has no caller-supplied identity overrides.
[[nodiscard]] LoadedRuntimeIdentityResult loaded_runtime_identity();

namespace detail {

enum class LoadedProviderClass {
    libstdcxx,
    glibc_libm,
    libgcc_s,
};

struct VersionedSymbolRequest {
    std::string_view name;
    std::string_view version;
};

struct LoadedProviderCandidate {
    DynamicProviderIdentity provider;
    std::vector<VersionedSymbolIdentity> symbols;
};

using LoadedProviderCandidateResult =
    std::variant<LoadedProviderCandidate, LoadedRuntimeError>;
using LoadedProviderValidationResult =
    std::variant<DynamicProviderIdentity, LoadedRuntimeError>;
using RuntimeVersionResult = std::variant<std::string, LoadedRuntimeError>;

// Linux observation owns loader, mapping, build-ID, and stable-file checks. The
// candidate contains content identity only; absolute paths remain in errors at most.
[[nodiscard]] LoadedProviderCandidateResult observe_linux_loaded_provider(
    LoadedProviderClass provider_class,
    std::span<const VersionedSymbolRequest> requested_symbols);

// Pure admission seam for malformed evidence and stable symbol ordering tests.
[[nodiscard]] LoadedProviderValidationResult validate_loaded_provider_candidate(
    LoadedProviderClass provider_class,
    std::span<const VersionedSymbolRequest> requested_symbols,
    const LoadedProviderCandidate &candidate);

[[nodiscard]] RuntimeVersionResult observe_linux_glibc_version();

[[nodiscard]] std::string
canonical_provider_token(const DynamicProviderIdentity &provider);
[[nodiscard]] std::string
canonical_symbol_token(std::span<const VersionedSymbolIdentity> symbols);

} // namespace detail
} // namespace crankwave::determinism
