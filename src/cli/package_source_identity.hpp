#pragma once

#include "native_input_files.hpp"

#include "engine_sim_offline/contract/provenance.hpp"

#include <string_view>

namespace engine_sim_offline::cli {

// Hashes the exact authored source closure without retaining paths, mtimes, or
// platform metadata. Records are typed, length-prefixed, and sorted by semantic
// identity before the domain-separated outer hash is calculated.
[[nodiscard]] contract::ProvenanceBundleRef package_source_inputs_identity(
    const NativeEngineInput &engine,
    const NativePackageBakeInput &package,
    std::string_view package_id);

} // namespace engine_sim_offline::cli
