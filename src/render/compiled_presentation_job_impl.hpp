#pragma once

#include "acoustics/exhaust_acoustic_session.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "presentation/presentation_render_session.hpp"
#include "render/compiled_presentation_job.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <cstdint>
#include <utility>

namespace engine_sim_offline::render_detail {

class CompiledPresentationJob::Implementation final {
  public:
    Implementation(contract::RenderRequestRecord request,
                   contract::Sha256Digest simulation_request_identity_v3_sha256,
                   determinism::RendererDeterminismEnvelope determinism,
                   presentation::PresentationRenderPlan presentation_plan,
                   contract::RenderManifestContent manifest_basis,
                   simulation::LowOrderCaptureSession simulation,
                   acoustics::ExhaustAcousticSession acoustics,
                   std::uint64_t expected_capture_frame_count,
                   std::uint64_t expected_source_interval_count,
                   std::uint64_t expected_acoustic_frame_count,
                   std::uint64_t expected_pre_audible_frame_count)
        : request(std::move(request)),
          simulation_request_identity_v3_sha256(simulation_request_identity_v3_sha256),
          determinism(std::move(determinism)),
          presentation_plan(std::move(presentation_plan)),
          manifest_basis(std::move(manifest_basis)), simulation(std::move(simulation)),
          acoustics(std::move(acoustics)),
          expected_capture_frame_count(expected_capture_frame_count),
          expected_source_interval_count(expected_source_interval_count),
          expected_acoustic_frame_count(expected_acoustic_frame_count),
          expected_pre_audible_frame_count(expected_pre_audible_frame_count) {}

    contract::RenderRequestRecord request;
    contract::Sha256Digest simulation_request_identity_v3_sha256;
    determinism::RendererDeterminismEnvelope determinism;
    presentation::PresentationRenderPlan presentation_plan;
    contract::RenderManifestContent manifest_basis;
    simulation::LowOrderCaptureSession simulation;
    acoustics::ExhaustAcousticSession acoustics;
    std::uint64_t expected_capture_frame_count = 0;
    std::uint64_t expected_source_interval_count = 0;
    std::uint64_t expected_acoustic_frame_count = 0;
    std::uint64_t expected_pre_audible_frame_count = 0;
};

} // namespace engine_sim_offline::render_detail
