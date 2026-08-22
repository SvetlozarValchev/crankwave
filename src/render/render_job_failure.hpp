#pragma once

#include "crankwave/contract/result.hpp"

#include <cstdint>
#include <string>

namespace crankwave::render_detail {

[[nodiscard]] contract::RenderFailure
make_job_failure(contract::RenderRequestRecord request, contract::FailureKind kind,
                 std::string detail_code, std::string model_id,
                 std::string state_summary, std::uint64_t sample_index = 0,
                 std::uint64_t step_end_index = 0, double scenario_time_s = 0.0);

} // namespace crankwave::render_detail
