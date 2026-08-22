#pragma once

#include "crankwave/session.hpp"

#include <span>
#include <string_view>

namespace crankwave::session_detail {

// Internal responsive-cooking boundary. Unlike create_engine_session(), this
// deliberately publishes only the named source-route dry buses and permits the
// presentation implementation to omit configured-transfer, selected, and master
// processing. The builder rejects every request that is not an exact dry-bus
// projection of the compiled scenario.
[[nodiscard]] EngineSessionCreateResult create_dry_projected_engine_session(
    const compile::CompiledScenario &scenario,
    std::span<const std::string_view> selected_dry_bus_ids);

} // namespace crankwave::session_detail
