#pragma once

#include "engine_sim_offline/render.hpp"

namespace engine_sim_offline::render_detail {

[[nodiscard]] contract::RenderRequestRecord
make_render_request_record(const RenderSpecification &specification,
                           const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::render_detail
