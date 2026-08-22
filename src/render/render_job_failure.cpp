#include "render/render_job_failure.hpp"

#include <utility>

namespace crankwave::render_detail {

contract::RenderFailure
make_job_failure(contract::RenderRequestRecord request, contract::FailureKind kind,
                 std::string detail_code, std::string model_id,
                 std::string state_summary, std::uint64_t sample_index,
                 std::uint64_t step_end_index, double scenario_time_s) {
    contract::FailureContext context;
    context.kind = kind;
    context.detail_code = std::move(detail_code);
    context.model_id = std::move(model_id);
    context.profile_id = request.resolved_inputs.scenario.engine_profile_id;
    context.sample_index = sample_index;
    context.step_end_index = step_end_index;
    context.scenario_time_s = scenario_time_s;
    if (request.resolved_inputs.engine.id.valid()) {
        context.engine_id = request.resolved_inputs.engine.id;
    }
    context.state_summary = std::move(state_summary);
    context.attempted_recovery =
        "none; render failed closed and generated no fallback output";
    return {std::move(context), std::move(request), {}};
}

} // namespace crankwave::render_detail
