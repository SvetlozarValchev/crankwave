#include "engine_sim_offline/contract/source_matrix.hpp"
#include "presentation/presentation_render_session.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;
using namespace engine_sim_offline::reference;

constexpr std::uint64_t kInputFramesPerBlock = 200;
constexpr std::uint64_t kTotalBlockCount = 850;
constexpr std::uint64_t kPreAudibleBlockCount = 100;
constexpr std::uint64_t kRequiredAuditFrameCount =
    kInputFramesPerBlock * kTotalBlockCount;

static_assert(kInputFramesPerBlock == kExcitationFramesPerMethodBlock);
static_assert(kRequiredAuditFrameCount == kP18ReferenceAuditRecordCount);
static_assert(kP18ReferenceRouteCount == kExhaustExcitationRouteCount);
static_assert(kP18ReferenceAudioArtifactCount == kPresentationAudioArtifactCount);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] RenderSinkStatus protocol_error(std::string message) {
    return RenderSinkError{
        RenderSinkErrorKind::protocol_violation,
        "reference_comparator_sink_protocol",
        std::move(message),
    };
}

// A hashing session still streams every byte through a RenderSink. This sink retains
// only declarations, sealed records, and per-role offsets; it cannot publish files or
// a manifest. The session owns the payload hashes compared below.
class ComparatorSink final : public RenderSink {
  public:
    std::size_t begin_calls = 0;
    std::size_t write_calls = 0;
    std::size_t commit_calls = 0;
    std::size_t abort_calls = 0;
    std::vector<PendingArtifact> declarations;
    std::vector<contract::ArtifactRecord> seals;
    std::optional<contract::OutputContract> output_contract;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &contract) override {
        ++begin_calls;
        if (begin_calls != 1 || output_contract.has_value()) {
            return protocol_error("transaction began more than once");
        }
        output_contract = contract;
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override {
        if (!output_contract.has_value() || artifact.role.empty() ||
            !next_offsets_.emplace(artifact.role, 0).second) {
            return protocol_error("artifact declaration was invalid or duplicated");
        }
        declarations.push_back(artifact);
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override {
        ++write_calls;
        const auto found = next_offsets_.find(std::string{chunk.role});
        if (found == next_offsets_.end() || chunk.bytes.empty() ||
            chunk.byte_offset != found->second) {
            return protocol_error("artifact bytes were not contiguous and declared");
        }
        found->second += static_cast<std::uint64_t>(chunk.bytes.size());
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override {
        const auto found = next_offsets_.find(record.role);
        if (found == next_offsets_.end() || found->second != record.byte_count ||
            !sealed_roles_.insert(record.role).second) {
            return protocol_error("sealed artifact differed from streamed bytes");
        }
        seals.push_back(record);
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus commit(const contract::RenderManifest &) override {
        ++commit_calls;
        return std::nullopt;
    }

    void abort() noexcept override {
        ++abort_calls;
    }

  private:
    std::unordered_map<std::string, std::uint64_t> next_offsets_;
    std::unordered_set<std::string> sealed_roles_;
};

[[nodiscard]] const P18ExpectedRoute &
expected_route(const P18ReferenceCatalogV1 &catalog, P18ReferenceRoute route) {
    const auto found =
        std::ranges::find(catalog.expected_routes, route, &P18ExpectedRoute::route);
    if (found == catalog.expected_routes.end()) {
        throw std::logic_error{"BMW comparator route catalog is incomplete"};
    }
    return *found;
}

[[nodiscard]] const P18ExpectedAudioComparator &
expected_audio(const P18ReferenceCatalogV1 &catalog,
               P18ReferenceAudioArtifact artifact) {
    const auto *expected = catalog.find_expected_audio(artifact);
    if (expected == nullptr) {
        throw std::logic_error{"BMW comparator audio catalog is incomplete"};
    }
    return *expected;
}

[[nodiscard]] const contract::ArtifactRequirement &
required_artifact(const contract::OutputContract &output, std::string_view role) {
    const auto found = std::ranges::find(output.required_artifacts, role,
                                         &contract::ArtifactRequirement::role);
    if (found == output.required_artifacts.end()) {
        throw std::logic_error{"BMW source matrix is missing a catalog artifact"};
    }
    return *found;
}

[[nodiscard]] PendingArtifact make_artifact(const contract::OutputContract &output,
                                            const P18ReferenceCatalogV1 &catalog,
                                            P18ReferenceAudioArtifact artifact) {
    const auto &expected = expected_audio(catalog, artifact);
    const auto &required = required_artifact(output, expected.expected_role);
    if (required.kind != contract::ArtifactKind::audio || !required.audio.has_value() ||
        required.diagnostic != expected.expected_diagnostic) {
        throw std::logic_error{
            "BMW source-matrix artifact differs from catalog policy"};
    }
    return {
        required.role,
        required.kind,
        std::string{expected.expected_relative_path},
        required.audio,
        required.diagnostic,
    };
}

[[nodiscard]] double expected_binary64(P18ExpectedBinary64 value) noexcept {
    return std::bit_cast<double>(value.expected_ieee754_bits);
}

void expect_verified_seeds(const P18LoadedReferenceFixture &fixture,
                           const P18ReferenceCatalogV1 &catalog) {
    for (const auto &expected : catalog.expected_executed_seeds) {
        const auto route = static_cast<std::size_t>(expected.route);
        const auto &observed =
            expected.component == P18ReferenceExecutedRandomComponent::air_noise
                ? fixture.component_seeds.air_noise[route]
                : fixture.component_seeds.jitter[route];
        expect(observed.initial_state == expected.expected_initial_state &&
                   observed.stream == expected.expected_stream,
               "verified BMW conditioning seed differs from the catalog");
    }
    expect(fixture.derived_identities.configured_ir_kernel_f64le.payload_sha256 ==
                   catalog.expected_kernel.expected_coefficient_f64le_sha256 &&
               fixture.derived_identities.configured_ir_kernel_spectrum_f64le
                       .payload_sha256 ==
                   catalog.expected_kernel.expected_spectrum_f64le_sha256,
           "verified BMW IR kernel differs from the catalog");
}

[[nodiscard]] RouteConditioningSeeds
make_route_seeds(const P18ReferenceRouteSeeds &seeds) noexcept {
    return {
        {seeds.jitter.initial_state, seeds.jitter.stream},
        {seeds.air_noise.initial_state, seeds.air_noise.stream},
    };
}

[[nodiscard]] PresentationRouteRenderPlan
make_route_plan(const contract::OutputContract &output,
                const P18ReferenceCatalogV1 &catalog, P18ReferenceRoute route,
                const P18ReferenceRouteSeeds &seeds,
                const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel,
                P18ReferenceAudioArtifact dry, P18ReferenceAudioArtifact configured_ir,
                P18ReferenceAudioArtifact selected) {
    const auto &identity = expected_route(catalog, route);
    return {
        identity.expected_route_id,
        std::string{identity.expected_semantic_id},
        make_route_seeds(seeds),
        kernel,
        expected_binary64(
            catalog.expected_presentation.expected_scalars.expected_wet_mix_01),
        {
            make_artifact(output, catalog, dry),
            make_artifact(output, catalog, configured_ir),
            make_artifact(output, catalog, selected),
        },
    };
}

[[nodiscard]] std::uint64_t
resolve_fade_frames(P18ExpectedBinary64 duration,
                    const contract::RationalRateHz &sample_rate) {
    const auto resolved =
        contract::resolve_frame_index(expected_binary64(duration), sample_rate);
    if (!resolved.has_value()) {
        throw std::logic_error{"BMW audition fade did not resolve to whole frames"};
    }
    return *resolved;
}

[[nodiscard]] PresentationRenderPlan make_plan(const P18LoadedReferenceFixture &fixture,
                                               const P18ReferenceCatalogV1 &catalog) {
    const auto output = contract::resolve_output_contract(
        contract::bmw_m52b28_reference_source_matrix_v1());
    const auto fixture_seeds = fixture.component_seeds.route_seeds();
    const auto raw_master =
        make_artifact(output, catalog, P18ReferenceAudioArtifact::master_raw);
    const auto audition_master =
        make_artifact(output, catalog, P18ReferenceAudioArtifact::master_audition);
    if (!audition_master.audio.has_value()) {
        throw std::logic_error{"BMW audition artifact has no audio contract"};
    }

    const auto &scalars = catalog.expected_presentation.expected_scalars;
    const auto fade_in =
        resolve_fade_frames(scalars.expected_audition_fade_in_duration_s,
                            audition_master.audio->sample_rate);
    const auto fade_out =
        resolve_fade_frames(scalars.expected_audition_fade_out_duration_s,
                            audition_master.audio->sample_rate);
    const auto monitoring_gain =
        expected_binary64(scalars.expected_audition_monitoring_gain_linear);
    const auto monitoring_gain_float = static_cast<float>(monitoring_gain);
    if (static_cast<double>(monitoring_gain_float) != monitoring_gain) {
        throw std::logic_error{
            "BMW audition monitoring gain is not exactly representable as Float32"};
    }

    const auto &route_0 = expected_route(catalog, P18ReferenceRoute::exhaust_0);
    const auto &route_1 = expected_route(catalog, P18ReferenceRoute::exhaust_1);
    return {
        output,
        {kTotalBlockCount, kPreAudibleBlockCount,
         PresentationTailPolicy::truncate_at_timeline_end},
        {{
            make_route_plan(output, catalog, P18ReferenceRoute::exhaust_0,
                            fixture_seeds[0], fixture.configured_ir_kernel,
                            P18ReferenceAudioArtifact::exhaust_0_dry,
                            P18ReferenceAudioArtifact::exhaust_0_configured_ir,
                            P18ReferenceAudioArtifact::exhaust_0_selected),
            make_route_plan(output, catalog, P18ReferenceRoute::exhaust_1,
                            fixture_seeds[1], fixture.configured_ir_kernel,
                            P18ReferenceAudioArtifact::exhaust_1_dry,
                            P18ReferenceAudioArtifact::exhaust_1_configured_ir,
                            P18ReferenceAudioArtifact::exhaust_1_selected),
        }},
        expected_binary64(scalars.expected_publication_calibration_gain_linear),
        {
            {route_0.expected_route_id, route_1.expected_route_id},
            MasteringSettings{audition_master.audio->frame_count, fade_in, fade_out,
                              monitoring_gain_float},
            {
                "1500-6500 RPM over 15 s; 85% effort; coherent sum of two linear "
                "wet/dry exhaust buses; fixed x128 monitoring gain; no limiter or "
                "compressor",
                "BMW M52B28 fifth-gear-equivalent dyno sweep",
                "Lavf60.16.100",
            },
            raw_master,
            audition_master,
        },
    };
}

void replay_reference_audit(const P18DecodedReferenceAudit &audit,
                            const P18ReferenceCatalogV1 &catalog,
                            PresentationRenderSession &session) {
    expect(audit.frames.size() == kRequiredAuditFrameCount,
           "BMW audit does not contain exactly 850x200 frames");
    const std::array route_ids{
        expected_route(catalog, P18ReferenceRoute::exhaust_0).expected_route_id,
        expected_route(catalog, P18ReferenceRoute::exhaust_1).expected_route_id,
    };
    std::array<ExhaustExcitationFrame, kInputFramesPerBlock> block_frames{};
    for (std::uint64_t block = 0; block < kTotalBlockCount; ++block) {
        const auto first_frame = block * kInputFramesPerBlock;
        for (std::size_t frame = 0; frame < block_frames.size(); ++frame) {
            block_frames[frame].route_values_engine_sim_source_unit =
                audit.frames[static_cast<std::size_t>(first_frame) + frame]
                    .pre_dsp_buses;
        }
        session.process(ExhaustExcitationBlockView::borrow_for_callback(
            first_frame, kExcitationRateHz, route_ids, block_frames));
    }
}

void expect_exact_evidence(const SealedPresentationEvidence &evidence,
                           const P18ReferenceCatalogV1 &catalog,
                           const ComparatorSink &sink) {
    const auto &expected = catalog.expected_render;
    expect(evidence.stats() ==
               PresentationRenderStats{
                   expected.expected_input_frame_count,
                   expected.expected_processed_block_count,
                   expected.expected_warmup_block_count,
                   expected.expected_published_block_count,
                   expected.expected_processed_source_frame_count,
                   expected.expected_warmup_source_frame_count,
                   expected.expected_published_source_frame_count,
               },
           "BMW presentation statistics differ from the catalog");

    const auto &records = evidence.artifacts();
    expect(records.size() == kP18ReferenceAudioArtifactCount &&
               sink.seals.size() == records.size(),
           "BMW session did not seal exactly eight artifacts");
    std::unordered_set<std::string_view> observed_roles;
    for (const auto &expected_audio_record : catalog.expected_audio) {
        const auto actual =
            std::ranges::find(records, expected_audio_record.expected_role,
                              &contract::ArtifactRecord::role);
        expect(actual != records.end() &&
                   actual->relative_path ==
                       expected_audio_record.expected_relative_path &&
                   actual->byte_count == expected_audio_record.expected_byte_count &&
                   actual->payload_sha256 == expected_audio_record.expected_sha256 &&
                   actual->diagnostic == expected_audio_record.expected_diagnostic &&
                   observed_roles.insert(actual->role).second,
               "BMW sealed artifact differs from its byte-count/SHA-256 comparator");
    }
    expect(observed_roles.size() == records.size(),
           "BMW evidence contained an unrecognized or duplicate artifact role");
    for (std::size_t index = 0; index < records.size(); ++index) {
        expect(sink.seals[index] == records[index],
               "sink seal differs from retained BMW session evidence");
    }
}

void run(const std::filesystem::path &fixture_root) {
    const auto &catalog = p18_reference_catalog_v1();
    auto fixture = load_p18_reference_fixture(fixture_root);
    expect_verified_seeds(fixture, catalog);

    ComparatorSink sink;
    {
        PresentationRenderSession session{sink, make_plan(fixture, catalog)};
        replay_reference_audit(fixture.audit, catalog, session);
        const auto evidence = session.finish();
        expect(session.state() == PresentationRenderSessionState::sealed,
               "BMW presentation session did not finish sealed");
        expect_exact_evidence(evidence, catalog, sink);
        expect(sink.begin_calls == 1 &&
                   sink.declarations.size() == kPresentationAudioArtifactCount &&
                   sink.write_calls != 0 &&
                   sink.seals.size() == kPresentationAudioArtifactCount &&
                   sink.commit_calls == 0 && sink.abort_calls == 0,
               "BMW comparator published or closed the live transaction early");
    }
    expect(sink.commit_calls == 0 && sink.abort_calls == 1,
           "sealed BMW comparator did not abort exactly once without publication");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "expected the BMW P1.8 fixture-root argument");
        run(std::filesystem::path{argv[1]});
    } catch (const std::exception &error) {
        std::cerr << "BMW full presentation comparator failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
