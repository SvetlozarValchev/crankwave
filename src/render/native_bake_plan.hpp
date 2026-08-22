#pragma once

#include "compile/compiled_scenario_view.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "render/native_presentation_publisher.hpp"

#include <string>
#include <variant>

namespace crankwave::render_detail {

struct NativeBakePlan {
    contract::RenderRequestRecord request;
    NativePresentationPublicationPlan publication;
    contract::RenderManifestContent manifest_basis;
};

struct NativeBakePlanError {
    std::string detail_code;
    std::string message;
};

using NativeBakePlanResult = std::variant<NativeBakePlan, NativeBakePlanError>;

[[nodiscard]] NativeBakePlanResult derive_native_bake_plan(
    compile::detail::CompiledScenarioInputsView inputs,
    const contract::RandomPlan &random_plan,
    const presentation::AdmittedPresentationCalibration &calibration,
    const determinism::RendererDeterminismEnvelope &determinism);

} // namespace crankwave::render_detail
