#pragma once

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/contract/result.hpp"

#include <vector>

namespace engine_sim_offline::render_detail {

[[nodiscard]] std::vector<contract::AssetPayloadIdentity>
asset_payload_identities(
    const compile::detail::ResolvedEnginePackage &engine_package);

[[nodiscard]] contract::RenderRequestRecord make_render_request_record(
    const compile::detail::ResolvedEnginePackage &engine_package,
    const compile::detail::ResolvedScenarioContracts &scenario);

} // namespace engine_sim_offline::render_detail
