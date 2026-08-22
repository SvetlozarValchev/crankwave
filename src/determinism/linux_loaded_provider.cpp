#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "determinism/linux_loaded_provider_internal.hpp"
#include "determinism/loaded_runtime_identity.hpp"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::determinism::detail {
namespace {

constexpr std::uint64_t kMaximumNoteBytes = UINT64_C(1024) * 1024U;
constexpr std::size_t kMaximumProgramHeaders = 128;
constexpr std::size_t kMaximumDynamicEntries = 4096;
constexpr std::size_t kMaximumSonameBytes = 256;

template <typename T> using Result = std::variant<T, LoadedRuntimeError>;

[[nodiscard]] std::string_view component(LoadedProviderClass provider_class) {
    switch (provider_class) {
    case LoadedProviderClass::libstdcxx:
        return "standard-library";
    case LoadedProviderClass::glibc_libm:
        return "math-library";
    case LoadedProviderClass::libgcc_s:
        return "compiler-runtime";
    }
    return "runtime-provider";
}

[[nodiscard]] std::string_view expected_soname(LoadedProviderClass provider_class) {
    switch (provider_class) {
    case LoadedProviderClass::libstdcxx:
        return "libstdc++.so.6";
    case LoadedProviderClass::glibc_libm:
        return "libm.so.6";
    case LoadedProviderClass::libgcc_s:
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

class DynamicHandle {
  public:
    explicit DynamicHandle(void *value = nullptr) noexcept : value_(value) {}
    ~DynamicHandle() {
        if (value_ != nullptr) {
            ::dlclose(value_);
        }
    }
    DynamicHandle(const DynamicHandle &) = delete;
    DynamicHandle &operator=(const DynamicHandle &) = delete;

    [[nodiscard]] void *get() const noexcept {
        return value_;
    }

  private:
    void *value_;
};

[[nodiscard]] bool checked_add(std::uintptr_t left, std::uint64_t right,
                               std::uintptr_t &result) noexcept {
    if (right > std::numeric_limits<std::uintptr_t>::max() - left) {
        return false;
    }
    result = left + static_cast<std::uintptr_t>(right);
    return true;
}

struct ObjectSnapshot {
    std::uintptr_t base = 0;
    std::uintptr_t phdr_address = 0;
    std::vector<Elf64_Phdr> headers;
    std::string soname;
    GnuBuildId memory_build_id;
};

[[nodiscard]] bool range_in_load(const ObjectSnapshot &snapshot,
                                 std::uint64_t virtual_address, std::uint64_t size,
                                 std::uint32_t flags) {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    if (!checked_add(snapshot.base, virtual_address, begin) ||
        !checked_add(begin, size, end)) {
        return false;
    }
    for (const auto &header : snapshot.headers) {
        std::uintptr_t load_begin = 0;
        std::uintptr_t load_end = 0;
        if (header.p_type == PT_LOAD && (header.p_flags & flags) == flags &&
            checked_add(snapshot.base, header.p_vaddr, load_begin) &&
            checked_add(load_begin, header.p_memsz, load_end) && begin >= load_begin &&
            end >= begin && end <= load_end) {
            return true;
        }
    }
    return false;
}

struct IterateContext {
    std::uintptr_t expected_base = 0;
    std::size_t matches = 0;
    ObjectSnapshot snapshot{};
};

int copy_matching_object(dl_phdr_info *information, std::size_t, void *opaque) {
    auto &context = *static_cast<IterateContext *>(opaque);
    if (static_cast<std::uintptr_t>(information->dlpi_addr) != context.expected_base) {
        return 0;
    }
    ++context.matches;
    if (information->dlpi_phnum == 0 ||
        information->dlpi_phnum > kMaximumProgramHeaders) {
        return 0;
    }
    context.snapshot.base = context.expected_base;
    context.snapshot.phdr_address =
        reinterpret_cast<std::uintptr_t>(information->dlpi_phdr);
    context.snapshot.headers.assign(information->dlpi_phdr,
                                    information->dlpi_phdr + information->dlpi_phnum);
    return 0;
}

[[nodiscard]] Result<ObjectSnapshot>
snapshot_object(void *handle, std::string_view provider_component) {
    link_map *mapping = nullptr;
    if (::dlinfo(handle, RTLD_DI_LINKMAP, &mapping) != 0 || mapping == nullptr ||
        mapping->l_addr == 0 || mapping->l_ld == nullptr) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "provider has no usable link-map metadata");
    }
    IterateContext context{};
    context.expected_base = static_cast<std::uintptr_t>(mapping->l_addr);
    ::dl_iterate_phdr(copy_matching_object, &context);
    if (context.matches != 1 || context.snapshot.headers.empty() ||
        !range_in_load(context.snapshot, 0, sizeof(Elf64_Ehdr), PF_R)) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component,
                     "provider has no unique bounded readable ELF image");
    }

    Elf64_Ehdr elf_header{};
    std::memcpy(&elf_header, reinterpret_cast<const void *>(context.snapshot.base),
                sizeof(elf_header));
    if (std::memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0 ||
        elf_header.e_ident[EI_CLASS] != ELFCLASS64 ||
        elf_header.e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_header.e_ident[EI_VERSION] != EV_CURRENT || elf_header.e_type != ET_DYN ||
        elf_header.e_machine != EM_X86_64 || elf_header.e_version != EV_CURRENT ||
        elf_header.e_ehsize != sizeof(Elf64_Ehdr) ||
        elf_header.e_phentsize != sizeof(Elf64_Phdr) ||
        elf_header.e_phnum != context.snapshot.headers.size()) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component,
                     "provider is not supported ELF64 little-endian x86_64");
    }
    std::uintptr_t expected_phdr = 0;
    const auto program_header_bytes =
        static_cast<std::uint64_t>(elf_header.e_phnum) * sizeof(Elf64_Phdr);
    if (!checked_add(context.snapshot.base, elf_header.e_phoff, expected_phdr) ||
        expected_phdr != context.snapshot.phdr_address ||
        !range_in_load(context.snapshot, elf_header.e_phoff, program_header_bytes,
                       PF_R)) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "ELF and loader program headers disagree");
    }

    const Elf64_Phdr *dynamic_header = nullptr;
    linux_detail::BuildIdAccumulator build_id;
    bool has_executable_load = false;
    for (const auto &header : context.snapshot.headers) {
        if (header.p_type == PT_LOAD) {
            if (header.p_filesz > header.p_memsz ||
                ((header.p_flags & PF_W) != 0 && (header.p_flags & PF_X) != 0) ||
                ((header.p_flags & PF_W) == 0 && (header.p_flags & PF_R) == 0)) {
                return error(
                    LoadedRuntimeErrorCode::provider_metadata_invalid,
                    provider_component,
                    "provider has an invalid or mutable executable load segment");
            }
            has_executable_load |= (header.p_flags & PF_X) != 0;
        } else if (header.p_type == PT_DYNAMIC) {
            if (dynamic_header != nullptr || header.p_memsz < sizeof(Elf64_Dyn) ||
                header.p_memsz / sizeof(Elf64_Dyn) > kMaximumDynamicEntries ||
                !range_in_load(context.snapshot, header.p_vaddr, header.p_memsz,
                               PF_R)) {
                return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                             provider_component, "provider dynamic segment is invalid");
            }
            dynamic_header = &header;
        } else if (header.p_type == PT_NOTE) {
            std::uintptr_t address = 0;
            if (header.p_filesz > kMaximumNoteBytes ||
                !range_in_load(context.snapshot, header.p_vaddr, header.p_filesz,
                               PF_R) ||
                !checked_add(context.snapshot.base, header.p_vaddr, address) ||
                !linux_detail::parse_note_block(
                    {reinterpret_cast<const std::byte *>(address),
                     static_cast<std::size_t>(header.p_filesz)},
                    build_id)) {
                return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                             provider_component, "provider notes are invalid");
            }
        }
    }
    if (!has_executable_load || dynamic_header == nullptr || build_id.count != 1 ||
        !build_id.value.has_value()) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component,
                     "provider lacks executable, dynamic, or unique build-ID evidence");
    }

    std::uintptr_t dynamic_address = 0;
    if (!checked_add(context.snapshot.base, dynamic_header->p_vaddr, dynamic_address) ||
        dynamic_address != reinterpret_cast<std::uintptr_t>(mapping->l_ld)) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "link-map and PT_DYNAMIC disagree");
    }
    const auto *dynamic = reinterpret_cast<const Elf64_Dyn *>(dynamic_address);
    std::optional<std::uint64_t> string_table;
    std::optional<std::uint64_t> string_table_size;
    std::optional<std::uint64_t> soname_offset;
    bool has_text_relocations = false;
    bool terminated = false;
    const auto dynamic_count =
        static_cast<std::size_t>(dynamic_header->p_memsz / sizeof(Elf64_Dyn));
    for (std::size_t index = 0; index < dynamic_count; ++index) {
        const auto &entry = dynamic[index];
        if (entry.d_tag == DT_NULL) {
            terminated = true;
            break;
        }
        auto set_unique = [&](auto &slot, std::uint64_t value) {
            if (slot.has_value()) {
                return false;
            }
            slot = value;
            return true;
        };
        if ((entry.d_tag == DT_STRTAB && !set_unique(string_table, entry.d_un.d_ptr)) ||
            (entry.d_tag == DT_STRSZ &&
             !set_unique(string_table_size, entry.d_un.d_val)) ||
            (entry.d_tag == DT_SONAME &&
             !set_unique(soname_offset, entry.d_un.d_val))) {
            terminated = false;
            break;
        }
        if (entry.d_tag == DT_TEXTREL ||
            (entry.d_tag == DT_FLAGS && (entry.d_un.d_val & DF_TEXTREL) != 0)) {
            has_text_relocations = true;
        }
    }
    if (!terminated || !string_table.has_value() || !string_table_size.has_value() ||
        !soname_offset.has_value() || *string_table < context.snapshot.base ||
        *string_table_size == 0 || *string_table_size > UINT64_C(16) * 1024U * 1024U ||
        *soname_offset >= *string_table_size || has_text_relocations ||
        !range_in_load(context.snapshot, *string_table - context.snapshot.base,
                       *string_table_size, PF_R)) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "provider has no bounded DT_SONAME");
    }
    const auto available = static_cast<std::size_t>(std::min<std::uint64_t>(
        *string_table_size - *soname_offset, kMaximumSonameBytes));
    const auto *soname = reinterpret_cast<const char *>(
        static_cast<std::uintptr_t>(*string_table + *soname_offset));
    const auto *terminator =
        static_cast<const char *>(std::memchr(soname, '\0', available));
    if (terminator == nullptr || terminator == soname) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "provider DT_SONAME is empty or unbounded");
    }
    context.snapshot.soname.assign(soname, terminator);
    context.snapshot.memory_build_id = *build_id.value;
    return context.snapshot;
}

[[nodiscard]] Result<void *> lookup(void *handle, std::string_view name,
                                    std::string_view version,
                                    std::string_view provider_component) {
    const std::string symbol_name(name);
    const std::string symbol_version(version);
    const auto checked = [](auto operation) -> std::pair<void *, const char *> {
        ::dlerror();
        void *address = operation();
        return {address, ::dlerror()};
    };
    const auto [plain, plain_error] =
        checked([&] { return ::dlsym(RTLD_DEFAULT, symbol_name.c_str()); });
    const auto [versioned, versioned_error] = checked([&] {
        return ::dlvsym(RTLD_DEFAULT, symbol_name.c_str(), symbol_version.c_str());
    });
    const auto [provider, provider_error] = checked(
        [&] { return ::dlvsym(handle, symbol_name.c_str(), symbol_version.c_str()); });
    if (plain_error != nullptr || versioned_error != nullptr ||
        provider_error != nullptr || plain == nullptr || versioned == nullptr ||
        provider == nullptr) {
        return error(LoadedRuntimeErrorCode::symbol_missing, provider_component,
                     "required versioned runtime symbol is unavailable", symbol_name);
    }
    if (plain != versioned || plain != provider) {
        return error(LoadedRuntimeErrorCode::symbol_interposed, provider_component,
                     "plain, versioned, and provider lookups disagree", symbol_name);
    }
    return plain;
}

[[nodiscard]] bool in_executable_segment(const ObjectSnapshot &snapshot,
                                         std::uintptr_t address) {
    for (const auto &header : snapshot.headers) {
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0 &&
            checked_add(snapshot.base, header.p_vaddr, begin) &&
            checked_add(begin, header.p_memsz, end) && address >= begin &&
            address < end) {
            return true;
        }
    }
    return false;
}

} // namespace

LoadedProviderCandidateResult observe_linux_loaded_provider(
    LoadedProviderClass provider_class,
    std::span<const VersionedSymbolRequest> requested_symbols) {
#if !defined(__linux__) || !defined(__x86_64__)
    (void)provider_class;
    (void)requested_symbols;
    return error(LoadedRuntimeErrorCode::unsupported_platform, "runtime-provider",
                 "loaded providers require Linux x86_64");
#else
    const auto provider_component = component(provider_class);
    const std::string soname(expected_soname(provider_class));
    ::dlerror();
    DynamicHandle handle(::dlopen(soname.c_str(), RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL));
    const char *open_error = ::dlerror();
    if (handle.get() == nullptr || open_error != nullptr) {
        return error(LoadedRuntimeErrorCode::provider_missing, provider_component,
                     "provider was not already dynamically loaded: " + soname);
    }
    auto snapshot_result = snapshot_object(handle.get(), provider_component);
    if (const auto *failure = std::get_if<LoadedRuntimeError>(&snapshot_result)) {
        return *failure;
    }
    const auto &snapshot = std::get<ObjectSnapshot>(snapshot_result);
    if (snapshot.soname != soname) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid,
                     provider_component, "provider DT_SONAME does not match " + soname);
    }

    LoadedProviderCandidate candidate;
    candidate.symbols.reserve(requested_symbols.size());
    std::optional<linux_detail::MappingRecord> provider_mapping;
    for (const auto &request : requested_symbols) {
        auto address_result =
            lookup(handle.get(), request.name, request.version, provider_component);
        if (const auto *failure = std::get_if<LoadedRuntimeError>(&address_result)) {
            return *failure;
        }
        const auto address =
            reinterpret_cast<std::uintptr_t>(std::get<void *>(address_result));
        Dl_info information{};
        if (!in_executable_segment(snapshot, address) ||
            ::dladdr(reinterpret_cast<const void *>(address), &information) == 0 ||
            reinterpret_cast<std::uintptr_t>(information.dli_fbase) != snapshot.base) {
            return error(LoadedRuntimeErrorCode::symbol_outside_provider,
                         provider_component,
                         "symbol is outside the provider PF_X PT_LOAD",
                         std::string(request.name));
        }
        auto mapping_result = linux_detail::mapping_for_address(
            address, provider_component, request.name);
        if (const auto *failure = std::get_if<LoadedRuntimeError>(&mapping_result)) {
            return *failure;
        }
        const auto &mapping = std::get<linux_detail::MappingRecord>(mapping_result);
        if (!provider_mapping.has_value()) {
            provider_mapping = mapping;
        } else if (provider_mapping->device != mapping.device ||
                   provider_mapping->inode != mapping.inode ||
                   provider_mapping->path != mapping.path) {
            return error(LoadedRuntimeErrorCode::symbol_interposed, provider_component,
                         "selected symbols do not share one mapped provider file",
                         std::string(request.name));
        }
        candidate.symbols.push_back(
            {std::string(request.name), std::string(request.version),
             static_cast<std::uint64_t>(address - snapshot.base)});
    }
    if (!provider_mapping.has_value()) {
        return error(LoadedRuntimeErrorCode::symbol_missing, provider_component,
                     "provider has no requested anchor symbols");
    }
    auto file_result = linux_detail::identify_provider_file(
        *provider_mapping, snapshot.headers, snapshot.base, snapshot.soname,
        snapshot.memory_build_id, provider_component);
    if (const auto *failure = std::get_if<LoadedRuntimeError>(&file_result)) {
        return *failure;
    }
    candidate.provider = std::get<DynamicProviderIdentity>(file_result);
    return candidate;
#endif
}

RuntimeVersionResult observe_linux_glibc_version() {
#if !defined(__linux__) || !defined(__x86_64__)
    return error(LoadedRuntimeErrorCode::unsupported_platform, "math-library",
                 "glibc version observation requires Linux x86_64");
#else
    constexpr std::string_view kSoname = "libc.so.6";
    constexpr std::string_view kName = "gnu_get_libc_version";
    constexpr std::string_view kVersion = "GLIBC_2.2.5";
    ::dlerror();
    DynamicHandle handle(::dlopen(kSoname.data(), RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL));
    const char *open_error = ::dlerror();
    if (handle.get() == nullptr || open_error != nullptr) {
        return error(LoadedRuntimeErrorCode::provider_missing, "math-library",
                     "glibc was not already dynamically loaded");
    }
    auto snapshot_result = snapshot_object(handle.get(), "math-library");
    if (const auto *failure = std::get_if<LoadedRuntimeError>(&snapshot_result)) {
        return *failure;
    }
    const auto &snapshot = std::get<ObjectSnapshot>(snapshot_result);
    auto address_result = lookup(handle.get(), kName, kVersion, "math-library");
    if (snapshot.soname != kSoname ||
        std::holds_alternative<LoadedRuntimeError>(address_result)) {
        if (const auto *failure = std::get_if<LoadedRuntimeError>(&address_result)) {
            return *failure;
        }
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid, "math-library",
                     "glibc provider metadata is unsupported");
    }
    void *address = std::get<void *>(address_result);
    if (!in_executable_segment(snapshot, reinterpret_cast<std::uintptr_t>(address))) {
        return error(LoadedRuntimeErrorCode::symbol_outside_provider, "math-library",
                     "glibc version function is outside libc PF_X", std::string(kName));
    }
    using VersionFunction = const char *(*)();
    static_assert(sizeof(VersionFunction) == sizeof(address));
    VersionFunction function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    const char *version = function();
    const std::string observed = version == nullptr ? "" : version;
    if (observed.empty() || observed.size() > 32 || observed.front() == '.' ||
        observed.back() == '.' || observed.find("..") != std::string::npos ||
        observed.find_first_not_of("0123456789.") != std::string::npos) {
        return error(LoadedRuntimeErrorCode::provider_metadata_invalid, "math-library",
                     "glibc runtime version is malformed");
    }
    return observed;
#endif
}

} // namespace crankwave::determinism::detail
