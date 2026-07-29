#include "render/render_request.hpp"

namespace engine_sim_offline::render_detail {

contract::RenderRequestRecord
make_render_request_record(const RenderSpecification &specification,
                           const contract::RenderScenario &scenario) {
    return {
        {specification.engine, specification.presentation, specification.randomness,
         scenario},
        specification.provenance,
        specification.source_matrix,
    };
}

} // namespace engine_sim_offline::render_detail
