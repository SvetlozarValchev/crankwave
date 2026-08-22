#include "determinism/loaded_runtime_identity.hpp"

#if __has_include(<bits/c++config.h>)
#include <bits/c++config.h>
#define CRANKWAVE_HAS_LIBSTDCXX_HEADERS 1
#else
#define CRANKWAVE_HAS_LIBSTDCXX_HEADERS 0
#endif

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

namespace crankwave::determinism {
namespace {

constexpr std::uint64_t kMaximumProviderBytes = UINT64_C(256) * 1024U * 1024U;

[[nodiscard]] std::string_view component(detail::LoadedProviderClass provider_class) {
    switch (provider_class) {
    case detail::LoadedProviderClass::libstdcxx:
        return "standard-library";
    case detail::LoadedProviderClass::glibc_libm:
        return "math-library";
    case detail::LoadedProviderClass::libgcc_s:
        return "compiler-runtime";
    }
    return "runtime-provider";
}

[[nodiscard]] std::string_view soname(detail::LoadedProviderClass provider_class) {
    switch (provider_class) {
    case detail::LoadedProviderClass::libstdcxx:
        return "libstdc++.so.6";
    case detail::LoadedProviderClass::glibc_libm:
        return "libm.so.6";
    case detail::LoadedProviderClass::libgcc_s:
        return "libgcc_s.so.1";
    }
    return {};
}

[[nodiscard]] LoadedRuntimeError error(LoadedRuntimeErrorCode code,
                                       std::string_view provider_component,
                                       std::string message, std::string symbol = {}) {
    return {code, std::string(provider_component), std::move(symbol),
            std::move(message)};
}

[[nodiscard]] std::string lower_hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4U];
        result[index * 2 + 1] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    return lower_hex(digest.bytes);
}

[[nodiscard]] std::string offset_hex(std::uint64_t offset) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[result.size() - index - 1] = digits[offset & 0x0fU];
        offset >>= 4U;
    }
    return result;
}

template <typename T>
[[nodiscard]] const LoadedRuntimeError *result_error(const T &result) {
    return std::get_if<LoadedRuntimeError>(&result);
}

} // namespace

bool GnuBuildId::is_zero() const noexcept {
    if (size == 0 || size > bytes.size()) {
        return true;
    }
    return std::none_of(bytes.begin(), bytes.begin() + size,
                        [](std::uint8_t byte) { return byte != 0; });
}

namespace detail {

LoadedProviderValidationResult validate_loaded_provider_candidate(
    LoadedProviderClass provider_class,
    std::span<const VersionedSymbolRequest> requested_symbols,
    const LoadedProviderCandidate &candidate) {
    const auto provider_component = component(provider_class);
    if (candidate.provider.soname != soname(provider_class)) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "provider candidate has the wrong DT_SONAME");
    }
    const auto &build_id = candidate.provider.gnu_build_id;
    const bool nonzero_padding =
        build_id.size <= build_id.bytes.size() &&
        std::any_of(build_id.bytes.begin() + build_id.size, build_id.bytes.end(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (build_id.is_zero() || nonzero_padding ||
        candidate.provider.file_sha256.is_zero() || candidate.provider.file_size == 0 ||
        candidate.provider.file_size > kMaximumProviderBytes) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component,
                     "provider candidate has incomplete content identity");
    }
    if (requested_symbols.empty() ||
        candidate.symbols.size() != requested_symbols.size()) {
        return error(LoadedRuntimeErrorCode::symbol_missing, provider_component,
                     "provider candidate has the wrong symbol set");
    }
    for (std::size_t index = 0; index < requested_symbols.size(); ++index) {
        const auto &request = requested_symbols[index];
        const auto &symbol = candidate.symbols[index];
        if (request.name.empty() || request.version.empty() ||
            symbol.name != request.name || symbol.version != request.version ||
            symbol.provider_offset == 0 ||
            (index != 0 && requested_symbols[index - 1].name >= request.name)) {
            return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                         provider_component,
                         "provider candidate symbols are not the exact sorted request",
                         symbol.name);
        }
    }
    return candidate.provider;
}

std::string canonical_provider_token(const DynamicProviderIdentity &provider) {
    if (provider.gnu_build_id.size == 0 ||
        provider.gnu_build_id.size > provider.gnu_build_id.bytes.size()) {
        return {};
    }
    return "elf64le-x86_64.soname." + provider.soname + ".bytes." +
           std::to_string(provider.file_size) + ".buildid." +
           lower_hex(std::span(provider.gnu_build_id.bytes)
                         .first(provider.gnu_build_id.size)) +
           ".sha256." + digest_hex(provider.file_sha256);
}

std::string canonical_symbol_token(std::span<const VersionedSymbolIdentity> symbols) {
    std::string result = "symbols";
    for (const auto &symbol : symbols) {
        result += "." + symbol.name + "." + symbol.version + "." +
                  offset_hex(symbol.provider_offset);
    }
    return result;
}

} // namespace detail

LoadedRuntimeIdentityResult loaded_runtime_identity() {
#if !defined(__linux__) || !defined(__x86_64__) || defined(__ILP32__)
    return error(LoadedRuntimeErrorCode::unsupported_platform, "runtime-provider",
                 "runtime identity requires Linux x86_64");
#elif CRANKWAVE_HAS_LIBSTDCXX_HEADERS == 0 || !defined(_GLIBCXX_RELEASE) ||   \
    !defined(__GLIBCXX__) || !defined(_GLIBCXX_USE_CXX11_ABI) ||                       \
    !defined(__GXX_ABI_VERSION)
    return error(LoadedRuntimeErrorCode::unsupported_headers, "standard-library",
                 "runtime identity requires complete libstdc++ header macros");
#else
    if constexpr (std::endian::native != std::endian::little || sizeof(void *) != 8) {
        return error(LoadedRuntimeErrorCode::unsupported_platform, "runtime-provider",
                     "runtime identity requires a 64-bit little-endian process");
    }
    if (_GLIBCXX_RELEASE <= 0 || __GLIBCXX__ < 19700101 || __GXX_ABI_VERSION <= 0 ||
        !(_GLIBCXX_USE_CXX11_ABI == 0 || _GLIBCXX_USE_CXX11_ABI == 1)) {
        return error(LoadedRuntimeErrorCode::unsupported_headers, "standard-library",
                     "libstdc++ header identity macros are malformed");
    }

    static constexpr std::array kStandardSymbols{
        detail::VersionedSymbolRequest{"__cxa_throw", "CXXABI_1.3"},
    };
    static constexpr std::array kMathSymbols{
        detail::VersionedSymbolRequest{"ceil", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"cos", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"floor", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"roundl", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"sin", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"sincos", "GLIBC_2.2.5"},
        detail::VersionedSymbolRequest{"tan", "GLIBC_2.2.5"},
    };
    static constexpr std::array kCompilerRuntimeSymbols{
        detail::VersionedSymbolRequest{"__muldc3", "GCC_4.0.0"},
    };

    auto standard_candidate_result = detail::observe_linux_loaded_provider(
        detail::LoadedProviderClass::libstdcxx, kStandardSymbols);
    if (const auto *failure = result_error(standard_candidate_result)) {
        return *failure;
    }
    auto math_candidate_result = detail::observe_linux_loaded_provider(
        detail::LoadedProviderClass::glibc_libm, kMathSymbols);
    if (const auto *failure = result_error(math_candidate_result)) {
        return *failure;
    }
    auto compiler_candidate_result = detail::observe_linux_loaded_provider(
        detail::LoadedProviderClass::libgcc_s, kCompilerRuntimeSymbols);
    if (const auto *failure = result_error(compiler_candidate_result)) {
        return *failure;
    }
    auto glibc_version_result = detail::observe_linux_glibc_version();
    if (const auto *failure = result_error(glibc_version_result)) {
        return *failure;
    }

    const auto &standard_candidate =
        std::get<detail::LoadedProviderCandidate>(standard_candidate_result);
    const auto &math_candidate =
        std::get<detail::LoadedProviderCandidate>(math_candidate_result);
    const auto &compiler_candidate =
        std::get<detail::LoadedProviderCandidate>(compiler_candidate_result);
    for (const auto &[provider_class, requests, candidate] : std::array{
             std::tuple{
                 detail::LoadedProviderClass::libstdcxx,
                 std::span<const detail::VersionedSymbolRequest>(kStandardSymbols),
                 &standard_candidate},
             std::tuple{detail::LoadedProviderClass::glibc_libm,
                        std::span<const detail::VersionedSymbolRequest>(kMathSymbols),
                        &math_candidate},
             std::tuple{detail::LoadedProviderClass::libgcc_s,
                        std::span<const detail::VersionedSymbolRequest>(
                            kCompilerRuntimeSymbols),
                        &compiler_candidate},
         }) {
        auto validation = detail::validate_loaded_provider_candidate(
            provider_class, requests, *candidate);
        if (const auto *failure = result_error(validation)) {
            return *failure;
        }
    }

    LoadedRuntimeIdentity identity;
    identity.standard_library_id = "libstdcxx";
    identity.standard_library_headers = {
        static_cast<std::uint32_t>(_GLIBCXX_RELEASE),
        static_cast<std::uint32_t>(__GLIBCXX__),
        static_cast<std::uint32_t>(__GXX_ABI_VERSION),
        _GLIBCXX_USE_CXX11_ABI == 1,
    };
    identity.standard_library_provider = standard_candidate.provider;
    identity.standard_library_anchor = standard_candidate.symbols.front();
    identity.standard_library_identity =
        "release." + std::to_string(identity.standard_library_headers.release) +
        ".headers." + std::to_string(identity.standard_library_headers.header_date) +
        ".gxxabi." + std::to_string(identity.standard_library_headers.gxx_abi_version) +
        ".cxx11abi." +
        std::to_string(identity.standard_library_headers.use_cxx11_abi ? 1 : 0) + "+" +
        detail::canonical_provider_token(identity.standard_library_provider) + "+" +
        detail::canonical_symbol_token(std::span(&identity.standard_library_anchor, 1));

    identity.math_library_id = "glibc-libm";
    identity.math_library_provider = math_candidate.provider;
    std::copy(math_candidate.symbols.begin(), math_candidate.symbols.end(),
              identity.math_symbols.begin());
    identity.math_library_identity =
        "glibc." + std::get<std::string>(glibc_version_result) + "+" +
        detail::canonical_provider_token(identity.math_library_provider) + "+" +
        detail::canonical_symbol_token(identity.math_symbols);

    identity.compiler_runtime_id = "libgcc-s";
    identity.compiler_runtime_provider = compiler_candidate.provider;
    identity.compiler_runtime_anchor = compiler_candidate.symbols.front();
    identity.compiler_runtime_identity =
        detail::canonical_provider_token(identity.compiler_runtime_provider) + "+" +
        detail::canonical_symbol_token(std::span(&identity.compiler_runtime_anchor, 1));
    return identity;
#endif
}

} // namespace crankwave::determinism
