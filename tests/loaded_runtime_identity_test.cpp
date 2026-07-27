#include "determinism/loaded_runtime_identity.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::determinism;

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kAddressSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kAddressSanitized = true;
#else
constexpr bool kAddressSanitized = false;
#endif
#else
constexpr bool kAddressSanitized = false;
#endif

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
constexpr bool kSupportedLinuxX86_64 = true;
#else
constexpr bool kSupportedLinuxX86_64 = false;
#endif

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string(message)};
    }
}

DynamicProviderIdentity provider(std::string soname) {
    DynamicProviderIdentity result;
    result.soname = std::move(soname);
    result.gnu_build_id.size = 20;
    result.gnu_build_id.bytes[0] = 1;
    result.file_sha256.bytes[0] = 2;
    result.file_size = 4096;
    return result;
}

detail::LoadedProviderCandidate standard_candidate() {
    return {
        provider("libstdc++.so.6"),
        {VersionedSymbolIdentity{"__cxa_throw", "CXXABI_1.3", 0xbb340}},
    };
}

void test_pure_provider_validation() {
    constexpr std::array requests{
        detail::VersionedSymbolRequest{"__cxa_throw", "CXXABI_1.3"},
    };
    auto candidate = standard_candidate();
    expect(std::holds_alternative<DynamicProviderIdentity>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, candidate)),
           "complete provider candidate was rejected");

    auto wrong_soname = candidate;
    wrong_soname.provider.soname = "libreplacement.so";
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, wrong_soname)),
           "wrong SONAME was admitted");

    auto zero_build_id = candidate;
    zero_build_id.provider.gnu_build_id.bytes = {};
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, zero_build_id)),
           "zero GNU build ID was admitted");

    auto noncanonical_build_id = candidate;
    noncanonical_build_id.provider.gnu_build_id.bytes.back() = 1;
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests,
                   noncanonical_build_id)),
           "nonzero build-ID padding was admitted");

    auto zero_hash = candidate;
    zero_hash.provider.file_sha256 = {};
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, zero_hash)),
           "zero provider SHA-256 was admitted");

    auto zero_offset = candidate;
    zero_offset.symbols.front().provider_offset = 0;
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, zero_offset)),
           "zero provider-relative symbol offset was admitted");

    auto wrong_version = candidate;
    wrong_version.symbols.front().version = "CXXABI_1.2";
    expect(std::holds_alternative<LoadedRuntimeError>(
               detail::validate_loaded_provider_candidate(
                   detail::LoadedProviderClass::libstdcxx, requests, wrong_version)),
           "wrong symbol version was admitted");
}

void test_canonical_tokens_exclude_process_location() {
    const auto candidate = standard_candidate();
    const auto provider_token = detail::canonical_provider_token(candidate.provider);
    const auto symbol_token = detail::canonical_symbol_token(candidate.symbols);
    expect(provider_token ==
               "elf64le-x86_64.soname.libstdc++.so.6.bytes.4096.buildid."
               "0100000000000000000000000000000000000000.sha256."
               "0200000000000000000000000000000000000000000000000000000000000000",
           "provider token format changed");
    expect(symbol_token == "symbols.__cxa_throw.CXXABI_1.3.00000000000bb340",
           "symbol token is not fixed-width and provider-relative");
    expect(provider_token.find('/') == std::string::npos &&
               symbol_token.find("0x") == std::string::npos,
           "canonical runtime token leaked a path or absolute-address spelling");
}

void expect_provider(const DynamicProviderIdentity &provider_identity,
                     std::string_view expected_soname) {
    expect(provider_identity.soname == expected_soname,
           "live provider has the wrong SONAME");
    expect(!provider_identity.gnu_build_id.is_zero(),
           "live provider has no GNU build ID");
    expect(!provider_identity.file_sha256.is_zero(),
           "live provider has no whole-file SHA-256");
    expect(provider_identity.file_size > 0 &&
               provider_identity.file_size <= UINT64_C(256) * 1024U * 1024U,
           "live provider escaped the file-size bound");
}

void test_live_loaded_runtime_is_complete_and_stable() {
    const auto first_result = loaded_runtime_identity();
    const auto *first = std::get_if<LoadedRuntimeIdentity>(&first_result);
    if (first == nullptr) {
        const auto &failure = std::get<LoadedRuntimeError>(first_result);
        if (!kSupportedLinuxX86_64) {
            expect(failure.code == LoadedRuntimeErrorCode::unsupported_platform,
                   "unsupported host returned the wrong runtime error");
            return;
        }
        if (kAddressSanitized) {
            expect(failure.code == LoadedRuntimeErrorCode::symbol_interposed ||
                       failure.code == LoadedRuntimeErrorCode::symbol_outside_provider,
                   "sanitized runtime failed for a reason other than interposition");
            return;
        }
        throw std::runtime_error{"live runtime admission failed for " +
                                 failure.component + "/" + failure.symbol + ": " +
                                 failure.message};
    }
    const auto second_result = loaded_runtime_identity();
    const auto *second = std::get_if<LoadedRuntimeIdentity>(&second_result);
    expect(second != nullptr && *second == *first,
           "repeated live runtime observation was not stable");

    expect(first->standard_library_id == "libstdcxx" &&
               first->math_library_id == "glibc-libm" &&
               first->compiler_runtime_id == "libgcc-s",
           "live runtime component IDs changed");
    expect(first->standard_library_headers.release > 0 &&
               first->standard_library_headers.header_date >= 19700101 &&
               first->standard_library_headers.gxx_abi_version > 0,
           "libstdc++ header macros were not retained");
    expect_provider(first->standard_library_provider, "libstdc++.so.6");
    expect_provider(first->math_library_provider, "libm.so.6");
    expect_provider(first->compiler_runtime_provider, "libgcc_s.so.1");

    expect(first->standard_library_anchor.name == "__cxa_throw" &&
               first->standard_library_anchor.version == "CXXABI_1.3" &&
               first->standard_library_anchor.provider_offset != 0,
           "libstdc++ anchor identity is incomplete");
    expect(first->compiler_runtime_anchor.name == "__muldc3" &&
               first->compiler_runtime_anchor.version == "GCC_4.0.0" &&
               first->compiler_runtime_anchor.provider_offset != 0,
           "libgcc_s anchor identity is incomplete");

    constexpr std::array expected_math_names{
        std::string_view{"ceil"},   std::string_view{"cos"}, std::string_view{"floor"},
        std::string_view{"roundl"}, std::string_view{"sin"}, std::string_view{"sincos"},
        std::string_view{"tan"},
    };
    for (std::size_t index = 0; index < first->math_symbols.size(); ++index) {
        expect(first->math_symbols[index].name == expected_math_names[index] &&
                   first->math_symbols[index].version == "GLIBC_2.2.5" &&
                   first->math_symbols[index].provider_offset != 0,
               "libm symbol set is incomplete or not sorted");
    }

    expect(first->standard_library_version.find(detail::canonical_provider_token(
               first->standard_library_provider)) != std::string::npos &&
               first->math_library_version.find(detail::canonical_symbol_token(
                   first->math_symbols)) != std::string::npos &&
               first->compiler_runtime_version.find(detail::canonical_provider_token(
                   first->compiler_runtime_provider)) != std::string::npos,
           "component versions are not derived from typed provider evidence");
    expect(first->standard_library_version.find('/') == std::string::npos &&
               first->math_library_version.find('/') == std::string::npos &&
               first->compiler_runtime_version.find('/') == std::string::npos,
           "component versions leaked a diagnostic provider path");
}

void test_preloaded_math_interposition_is_rejected() {
    const auto result = loaded_runtime_identity();
    const auto *failure = std::get_if<LoadedRuntimeError>(&result);
    expect(failure != nullptr,
           "preloaded math interposer produced an admissible runtime");
    expect(failure->code == LoadedRuntimeErrorCode::symbol_interposed &&
               failure->component == "math-library" && failure->symbol == "tan",
           "preloaded tan interposer returned the wrong typed rejection");
}

#if defined(ENGINE_SIM_OFFLINE_TEST_STATIC_RUNTIME)
void test_static_standard_runtime_is_rejected() {
    const auto result = loaded_runtime_identity();
    const auto *failure = std::get_if<LoadedRuntimeError>(&result);
    expect(failure != nullptr,
           "static standard runtime produced an admissible identity");
    expect(failure->code == LoadedRuntimeErrorCode::provider_missing &&
               failure->component == "standard-library",
           "static standard runtime returned the wrong typed rejection");
}
#endif

} // namespace

int main() {
#if defined(ENGINE_SIM_OFFLINE_TEST_STATIC_RUNTIME)
    test_static_standard_runtime_is_rejected();
    return 0;
#endif
    if (std::getenv("ENGINE_SIM_OFFLINE_TEST_EXPECT_TAN_INTERPOSITION") != nullptr) {
        test_preloaded_math_interposition_is_rejected();
        return 0;
    }
    test_pure_provider_validation();
    test_canonical_tokens_exclude_process_location();
    test_live_loaded_runtime_is_complete_and_stable();
}
