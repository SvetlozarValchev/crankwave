#pragma once

#include "engine_sim_offline/render.hpp"

#include <vector>

namespace engine_sim_offline::render_detail {

[[nodiscard]] std::vector<contract::AssetPayloadIdentity>
asset_payload_identities(const RenderSpecification &specification);

[[nodiscard]] contract::RenderRequestRecord
make_render_request_record(const RenderSpecification &specification,
                           const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::render_detail
