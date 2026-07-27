#pragma once

#include "determinism/loaded_runtime_identity.hpp"

#include <elf.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::determinism::detail::linux_detail {

struct BuildIdAccumulator {
    std::optional<GnuBuildId> value;
    std::size_t count = 0;
};

[[nodiscard]] bool parse_note_block(std::span<const std::byte> bytes,
                                    BuildIdAccumulator &build_id);

struct MappingRecord {
    dev_t device = 0;
    ino_t inode = 0;
    std::string path;
};

using MappingResult = std::variant<MappingRecord, LoadedRuntimeError>;
using ProviderFileResult = std::variant<DynamicProviderIdentity, LoadedRuntimeError>;

[[nodiscard]] MappingResult mapping_for_address(std::uintptr_t address,
                                                std::string_view provider_component,
                                                std::string_view symbol);

[[nodiscard]] ProviderFileResult identify_provider_file(
    const MappingRecord &mapping, std::span<const Elf64_Phdr> program_headers,
    std::uintptr_t load_bias, std::string_view soname,
    const GnuBuildId &memory_build_id, std::string_view provider_component);

} // namespace engine_sim_offline::determinism::detail::linux_detail
