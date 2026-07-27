#include "reference/p18_reference_session_adapter.hpp"

#include "engine_sim_offline/contract/source_matrix.hpp"
#include "reference/p18_reference_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace engine_sim_offline::reference {
namespace {

constexpr std::size_t kAuditFramesPerBlock = 200;
constexpr std::size_t kAuditBlockCount = 850;
constexpr std::size_t kRequiredAuditFrameCount =
    kAuditFramesPerBlock * kAuditBlockCount;
constexpr contract::RationalRateHz kAuditSampleRate{10000, 1};
constexpr std::array<contract::RouteId, presentation::kP18ExhaustRouteCount>
    kAuditRouteIds{contract::RouteId{1}, contract::RouteId{2}};

[[noreturn]] void throw_policy_mismatch(std::size_t artifact_index,
                                        const char *detail) {
    throw std::logic_error{"P1.8 reference artifact policy mismatch at index " +
                           std::to_string(artifact_index) + ": " + detail};
}

} // namespace

P18PresentationSessionPlan make_p18_reference_presentation_session_plan() {
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();
    const auto &catalog = p18_reference_catalog_v1();
    const auto &requirements = source_matrix.required_artifacts;

    if (requirements.size() != kP18PresentationAudioArtifactCount) {
        throw std::logic_error{
            "P1.8 reference source matrix must require exactly eight artifacts"};
    }

    P18PresentationSessionPlan plan;
    plan.output_contract = contract::resolve_output_contract(source_matrix);
    for (std::size_t index = 0; index < plan.audio_artifacts.size(); ++index) {
        const auto &requirement = requirements[index];
        const auto &catalog_entry = catalog.expected_audio[index];
        if (catalog_entry.audio != static_cast<P18ReferenceAudioArtifact>(index)) {
            throw_policy_mismatch(index, "catalog order differs from renderer order");
        }
        if (requirement.role != catalog_entry.expected_role) {
            throw_policy_mismatch(index, "source-matrix and catalog roles differ");
        }
        if (requirement.diagnostic != catalog_entry.expected_diagnostic) {
            throw_policy_mismatch(index,
                                  "source-matrix and catalog diagnostics differ");
        }
        if (requirement.kind != contract::ArtifactKind::audio ||
            !requirement.audio.has_value()) {
            throw_policy_mismatch(index, "source-matrix entry is not contracted audio");
        }
        if (catalog_entry.expected_relative_path.empty()) {
            throw_policy_mismatch(index, "catalog path is empty");
        }

        plan.audio_artifacts[index] = {
            requirement.role,
            requirement.kind,
            std::string{catalog_entry.expected_relative_path},
            requirement.audio,
            requirement.diagnostic,
        };
    }
    return plan;
}

std::array<presentation::P18RouteConditioningSeeds, 2>
p18_reference_presentation_seeds(const P18DecodedReferenceSeeds &seeds) {
    const auto fixture_route_seeds = seeds.route_seeds();
    std::array<presentation::P18RouteConditioningSeeds, 2> result{};
    for (std::size_t route = 0; route < result.size(); ++route) {
        result[route] = {
            {
                fixture_route_seeds[route].jitter.initial_state,
                fixture_route_seeds[route].jitter.stream,
            },
            {
                fixture_route_seeds[route].air_noise.initial_state,
                fixture_route_seeds[route].air_noise.stream,
            },
        };
    }
    return result;
}

void replay_p18_reference_audit(const P18DecodedReferenceAudit &audit,
                                P18PresentationSession &session) {
    if (audit.frames.size() != kRequiredAuditFrameCount) {
        throw std::invalid_argument{
            "P1.8 reference replay requires exactly 170000 audit frames"};
    }

    std::array<presentation::ExhaustExcitationFrame, kAuditFramesPerBlock>
        block_frames{};
    for (std::size_t block = 0; block < kAuditBlockCount; ++block) {
        const std::size_t first_frame = block * kAuditFramesPerBlock;
        for (std::size_t frame = 0; frame < block_frames.size(); ++frame) {
            block_frames[frame].route_values_engine_sim_source_unit =
                audit.frames[first_frame + frame].pre_dsp_buses;
        }
        session.process(presentation::ExhaustExcitationBlockView::borrow_for_callback(
            static_cast<std::uint64_t>(first_frame), kAuditSampleRate, kAuditRouteIds,
            block_frames));
    }
}

} // namespace engine_sim_offline::reference
