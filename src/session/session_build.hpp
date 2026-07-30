#pragma once

#include "engine_sim_offline/session.hpp"
#include "excitation/captured_exhaust_excitation.hpp"
#include "presentation/presentation_audio_session.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <memory>
#include <variant>

namespace engine_sim_offline::session_detail {

struct BuiltSessionComponents {
    compile::CompiledScenario compiled_scenario;
    EngineSessionExecutionKind execution_kind =
        EngineSessionExecutionKind::finite_scenario;
    contract::Sha256Digest simulation_request_identity;
    contract::RandomPlan random_plan;
    presentation::AdmittedPresentationCalibration calibration;
    simulation::LowOrderCaptureSession simulation;
    excitation::CapturedExhaustExcitationSession excitation;
    std::unique_ptr<presentation::PresentationAudioSession> presentation;
};

using SessionBuildResult =
    std::variant<BuiltSessionComponents, EngineSessionError>;

[[nodiscard]] SessionBuildResult
build_session_components(const compile::CompiledScenario &scenario,
                         EngineSessionExecutionKind execution_kind);

} // namespace engine_sim_offline::session_detail
