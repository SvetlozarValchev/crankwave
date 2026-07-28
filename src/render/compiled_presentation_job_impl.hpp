#pragma once

#include "determinism/renderer_determinism_envelope.hpp"
#include "excitation/captured_exhaust_excitation.hpp"
#include "presentation/presentation_asset_compiler.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "presentation/presentation_render_session.hpp"
#include "render/compiled_presentation_job.hpp"
#include "simulation/legacy_low_order_simulation.hpp"

#include <utility>
#include <vector>

namespace engine_sim_offline::render_detail {

class CompiledPresentationJob::Implementation final {
  public:
    Implementation(contract::RenderRequestRecord request,
                   determinism::RendererDeterminismEnvelope determinism,
                   contract::RandomPlan random_plan,
                   presentation::AdmittedPresentationCalibration calibration,
                   std::vector<presentation::CompiledPresentationAsset> compiled_assets,
                   std::vector<presentation::CompiledPresentationConvolutionKernel>
                       compiled_kernels,
                   presentation::PresentationRenderPlan presentation_plan,
                   contract::RenderManifestContent manifest_basis,
                   simulation::LegacyLowOrderSimulationSession simulation,
                   excitation::CapturedExhaustExcitationSession excitation)
        : request(std::move(request)), determinism(std::move(determinism)),
          random_plan(std::move(random_plan)), calibration(std::move(calibration)),
          compiled_assets(std::move(compiled_assets)),
          compiled_kernels(std::move(compiled_kernels)),
          presentation_plan(std::move(presentation_plan)),
          manifest_basis(std::move(manifest_basis)), simulation(std::move(simulation)),
          excitation(std::move(excitation)) {}

    contract::RenderRequestRecord request;
    determinism::RendererDeterminismEnvelope determinism;
    contract::RandomPlan random_plan;
    presentation::AdmittedPresentationCalibration calibration;
    std::vector<presentation::CompiledPresentationAsset> compiled_assets;
    std::vector<presentation::CompiledPresentationConvolutionKernel> compiled_kernels;
    presentation::PresentationRenderPlan presentation_plan;
    contract::RenderManifestContent manifest_basis;
    simulation::LegacyLowOrderSimulationSession simulation;
    excitation::CapturedExhaustExcitationSession excitation;
};

} // namespace engine_sim_offline::render_detail
