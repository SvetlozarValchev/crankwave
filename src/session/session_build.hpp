#pragma once

#include "engine_sim_offline/session.hpp"
#include "excitation/captured_source_excitation.hpp"
#include "presentation/presentation_audio_session.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::session_detail {

struct BuiltSessionComponents {
    compile::CompiledScenario compiled_scenario;
    EngineSessionExecutionKind execution_kind =
        EngineSessionExecutionKind::finite_scenario;
    contract::Sha256Digest simulation_request_identity;
    contract::RandomPlan random_plan;
    presentation::AdmittedPresentationCalibration calibration;
    simulation::LowOrderCaptureSession simulation;
    excitation::CapturedSourceExcitationSession excitation;
    std::unique_ptr<presentation::PresentationAudioSession> presentation;
    // Empty is the complete public session. A nonempty list is the exact internal
    // responsive projection and owns request order for the session lifetime.
    std::vector<std::string> projected_dry_bus_ids;
};

using SessionBuildResult = std::variant<BuiltSessionComponents, EngineSessionError>;

[[nodiscard]] SessionBuildResult
build_session_components(const compile::CompiledScenario &scenario,
                         EngineSessionExecutionKind execution_kind,
                         std::span<const std::string_view> projected_dry_bus_ids = {});

} // namespace engine_sim_offline::session_detail
