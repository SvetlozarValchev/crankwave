#pragma once
#include "bake_crankwave_command.hpp"
#include <optional>

namespace crankwave::cli {
// No result means the document belongs to the existing engine model path.
[[nodiscard]] std::optional<BakeCrankwaveResult>
try_bake_mechanical_diesel(const BakeCrankwaveRequest &request,
                           std::stop_token stop_token);
} // namespace crankwave::cli
