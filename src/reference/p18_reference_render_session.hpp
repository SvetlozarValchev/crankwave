#pragma once

#include "dsp/p18_fixed_fft.hpp"
#include "reference/p18_reference_artifact_set.hpp"
#include "reference/p18_reference_audit_reader.hpp"
#include "reference/p18_reference_seed_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace engine_sim_offline::reference {

using P18ReferenceAudioConsumers =
    std::array<artifacts::WavChunkConsumer, kP18ReferenceAudioArtifactCount>;

struct P18ReferenceRenderStats {
    std::uint64_t input_frame_count = 0;
    std::uint64_t processed_block_count = 0;
    std::uint64_t warmup_block_count = 0;
    std::uint64_t published_block_count = 0;
    std::uint64_t processed_source_frame_count = 0;
    std::uint64_t warmup_source_frame_count = 0;
    std::uint64_t published_source_frame_count = 0;

    contract::Sha256Digest raw_float32_payload_sha256;
    contract::Sha256Digest monitoring_float32_payload_sha256;
    contract::Sha256Digest faded_float32_payload_sha256;
    contract::Sha256Digest s32le_payload_sha256;
    contract::Sha256Digest pcm24le_payload_sha256;
    std::uint64_t saturation_count = 0;
    float faded_absolute_peak = 0.0F;

    friend bool operator==(const P18ReferenceRenderStats &,
                           const P18ReferenceRenderStats &) = default;
};

// Runs the fixed, path-free P1.8 reference presentation session. Consumers are
// ordered by P18ReferenceAudioArtifact and receive complete WAVE streams. The
// operation throws on malformed input, arithmetic failure, rejected output, or an
// incomplete stream; there is no partial-success result and no tail flush.
[[nodiscard]] P18ReferenceRenderStats render_p18_reference_audio(
    const P18DecodedReferenceAudit &audit,
    std::array<P18ReferenceRouteSeeds, kP18ReferenceSeedRouteCount> route_seeds,
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir,
    const P18ReferenceAudioConsumers &consumers);

} // namespace engine_sim_offline::reference
