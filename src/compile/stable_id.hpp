#pragma once

#include "engine_sim_offline/compile.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace engine_sim_offline::compile::detail {

struct StableIdSource {
    std::string_view authored_id;
    std::string_view json_pointer;
};

using StableIdAssignmentResult = CompileResult<std::vector<StableIdAssignment>>;

[[nodiscard]] StableIdAssignmentResult
assign_stable_runtime_ids(std::string_view object_namespace,
                          std::span<const StableIdSource> sources) noexcept;

} // namespace engine_sim_offline::compile::detail
