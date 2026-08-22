#pragma once

#include "crankwave/compile.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace crankwave::compile::detail {

struct StableIdSource {
    std::string_view authored_id;
    std::string_view json_pointer;
};

using StableIdAssignmentResult = CompileResult<std::vector<StableIdAssignment>>;

[[nodiscard]] StableIdAssignmentResult
assign_stable_runtime_ids(std::string_view object_namespace,
                          std::span<const StableIdSource> sources) noexcept;

} // namespace crankwave::compile::detail
