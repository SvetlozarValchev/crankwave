#include "presentation/presentation_render_session.hpp"

#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;

constexpr std::array<contract::RouteId, 3> kSyntheticRouteIds{
    contract::RouteId{1},
    contract::RouteId{2},
    contract::RouteId{3},
};

constexpr std::array<presentation::RouteConditioningSeeds, 3> kSyntheticSeeds{
    presentation::RouteConditioningSeeds{
        {UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
        {UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
    },
    presentation::RouteConditioningSeeds{
        {UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
        {UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
    },
    presentation::RouteConditioningSeeds{
        {UINT64_C(0x88f17c7c2d60e87b), UINT64_C(0x6fd4c7602345b719)},
        {UINT64_C(0xa54ff53a5f1d36f1), UINT64_C(0x3c6ef372fe94f82b)},
    },
};
constexpr presentation::RouteConditioningCalibration kSyntheticConditioning{
    0.5, 10000.0, std::bit_cast<double>(UINT64_C(0x3f847ae140000000)), 1.0, 2000.0,
};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.bytes.size() * 2);
    for (const auto byte : digest.bytes) {
        result.push_back(kHex[byte >> 4U]);
        result.push_back(kHex[byte & 0x0fU]);
    }
    return result;
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

template <class Function>
void expect_sink_failure(Function &&function, const RenderSinkError &expected,
                         const char *message) {
    try {
        std::forward<Function>(function)();
    } catch (const PresentationSinkFailure &failure) {
        expect(failure.sink_error() == expected, message);
        return;
    } catch (...) {
        throw std::runtime_error{message};
    }
    throw std::runtime_error{message};
}

[[nodiscard]] RenderSinkStatus rejected(std::string detail_code) {
    return RenderSinkError{
        RenderSinkErrorKind::publication_failure,
        std::move(detail_code),
        "injected recording-sink failure",
    };
}

class RecordingSink final : public RenderSink {
  public:
    struct Write {
        std::string role;
        std::uint64_t byte_offset = 0;
        std::size_t byte_count = 0;
    };

    bool reject_begin = false;
    bool reject_first_declaration = false;
    bool reject_first_write = false;
    std::optional<RenderSinkError> next_write_error;

    std::size_t begin_calls = 0;
    std::size_t commit_calls = 0;
    std::size_t abort_calls = 0;
    std::vector<PendingArtifact> declarations;
    std::vector<Write> writes;
    std::vector<contract::ArtifactRecord> seals;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &) override {
        ++begin_calls;
        if (reject_begin) {
            return rejected("injected_begin_failure");
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override {
        declarations.push_back(artifact);
        if (reject_first_declaration && declarations.size() == 1) {
            return rejected("injected_first_declaration_failure");
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override {
        writes.push_back(
            {std::string{chunk.role}, chunk.byte_offset, chunk.bytes.size()});
        if (reject_first_write && writes.size() == 1) {
            return rejected("injected_first_write_failure");
        }
        if (next_write_error.has_value()) {
            auto error = std::move(next_write_error);
            next_write_error.reset();
            return error;
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override {
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
};

[[nodiscard]] PresentationRenderPlan make_plan_for_routes(
    std::vector<std::shared_ptr<const dsp::FixedConvolutionKernel>> route_irs,
    std::uint64_t total_block_count, std::uint64_t pre_audible_block_count,
    std::vector<double> wet_mix_01) {
    if (route_irs.empty() || route_irs.size() > kSyntheticRouteIds.size() ||
        route_irs.size() != wet_mix_01.size()) {
        throw std::invalid_argument{"invalid synthetic route plan"};
    }
    contract::OutputContract output_contract;
    output_contract.source_matrix_id = "test.synthetic.presentation-session-boundary";
    output_contract.distribution = contract::DistributionIntent::local_evaluation;

    const auto published_block_count = total_block_count - pre_audible_block_count;
    const auto audible_frame_count =
        published_block_count * presentation::kSourceFramesPerMethodBlock;

    const contract::AudioContract float_audio{
        {192000, 1},
        audible_frame_count,
        "mono",
        "float32le",
    };
    const contract::AudioContract audition_audio{
        {192000, 1},
        audible_frame_count,
        "mono",
        "pcm_s24le",
    };

    std::vector<PendingArtifact> audio_artifacts(
        presentation_audio_artifact_count(route_irs.size()));
    for (std::size_t index = 0; index < audio_artifacts.size(); ++index) {
        const auto role = "test.synthetic.audio." + std::to_string(index);
        const auto path = "audio/synthetic-" + std::to_string(index) + ".wav";
        const auto &audio =
            index + 1 == audio_artifacts.size() ? audition_audio : float_audio;
        audio_artifacts[index] = {
            role, contract::ArtifactKind::audio, path, audio, false,
        };
        output_contract.required_artifacts.push_back({
            role,
            contract::ArtifactKind::audio,
            audio,
            false,
        });
    }
    std::vector<PresentationRouteRenderPlan> routes;
    routes.reserve(route_irs.size());
    std::vector<contract::RouteId> audition_route_ids;
    audition_route_ids.reserve(route_irs.size());
    for (std::size_t route = 0; route < route_irs.size(); ++route) {
        const auto base = route * kPresentationArtifactsPerRoute;
        const auto semantic_id = "test.synthetic.exhaust." + std::to_string(route);
        output_contract.required_source_routes.push_back({
            semantic_id,
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {
                audio_artifacts[base].role,
                audio_artifacts[base + 1].role,
                audio_artifacts[base + 2].role,
            },
        });
        routes.push_back({
            kSyntheticRouteIds[route],
            semantic_id,
            kSyntheticSeeds[route],
            std::move(route_irs[route]),
            wet_mix_01[route],
            {
                audio_artifacts[base],
                audio_artifacts[base + 1],
                audio_artifacts[base + 2],
            },
        });
        audition_route_ids.push_back(kSyntheticRouteIds[route]);
    }
    const auto raw_master = route_irs.size() * kPresentationArtifactsPerRoute;
    const auto audition_master = raw_master + 1;
    output_contract.required_output_buses = {
        {
            "test.synthetic.master.raw",
            contract::OutputBusKind::master_engine_raw,
            {audio_artifacts[raw_master].role},
        },
        {
            "test.synthetic.master.audition",
            contract::OutputBusKind::master_engine_audition,
            {audio_artifacts[audition_master].role},
        },
    };

    const auto fade_frame_count =
        std::min<std::uint64_t>(3'840, audible_frame_count / 2);
    return {
        std::move(output_contract),
        {total_block_count, pre_audible_block_count,
         PresentationTailPolicy::truncate_at_timeline_end},
        implemented_presentation_method_identities(),
        kSyntheticConditioning,
        std::move(routes),
        dsp::kSourcePublicationCalibration,
        {
            std::move(audition_route_ids),
            MasteringSettings{audible_frame_count, fade_frame_count, fade_frame_count,
                              128.0F},
            {"Synthetic presentation session", "Synthetic render", "test-suite"},
            audio_artifacts[raw_master],
            audio_artifacts[audition_master],
        },
    };
}

[[nodiscard]] PresentationRenderPlan
make_plan(const std::shared_ptr<const dsp::FixedConvolutionKernel> &route_0_ir,
          const std::shared_ptr<const dsp::FixedConvolutionKernel> &route_1_ir,
          std::uint64_t total_block_count = 850,
          std::uint64_t pre_audible_block_count = 100,
          std::array<double, 2> wet_mix_01 = {1.0, 1.0}) {
    std::vector<double> wet_mixes(wet_mix_01.begin(), wet_mix_01.end());
    return make_plan_for_routes({route_0_ir, route_1_ir}, total_block_count,
                                pre_audible_block_count, std::move(wet_mixes));
}

[[nodiscard]] std::shared_ptr<const dsp::FixedConvolutionKernel>
make_synthetic_kernel(std::uint32_t initial_state = UINT32_C(0x6a09e667)) {
    std::vector<double> coefficients(dsp::FixedConvolutionKernel::coefficient_count);
    std::uint32_t state = initial_state;
    for (double &coefficient : coefficients) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const std::int64_t centered =
            static_cast<std::int64_t>(state) - INT64_C(2147483648);
        coefficient = static_cast<double>(centered) * 0x1p-47;
    }
    auto fft_plan = std::make_shared<const dsp::FixedFftPlan>();
    return std::make_shared<const dsp::FixedConvolutionKernel>(coefficients,
                                                               std::move(fft_plan));
}

struct SyntheticExcitationBlock {
    explicit SyntheticExcitationBlock(std::size_t route_count = 2)
        : route_ids(kSyntheticRouteIds.begin(),
                    kSyntheticRouteIds.begin() +
                        static_cast<std::ptrdiff_t>(route_count)),
          values(kExcitationFramesPerMethodBlock * route_count) {}

    std::vector<contract::RouteId> route_ids;
    std::vector<double> values;
};

void fill_block(SyntheticExcitationBlock &block, std::uint64_t block_ordinal) {
    for (std::size_t frame = 0; frame < kExcitationFramesPerMethodBlock; ++frame) {
        const auto global = block_ordinal * kExcitationFramesPerMethodBlock + frame;
        for (std::size_t route = 0; route < block.route_ids.size(); ++route) {
            const auto code =
                static_cast<std::int64_t>((global + 1) * (route + 3) % 29) - 14;
            block.values[frame * block.route_ids.size() + route] =
                static_cast<double>(code) * 0.125;
        }
    }
}

[[nodiscard]] presentation::ExhaustExcitationBlockView
make_block(SyntheticExcitationBlock &block, std::uint64_t first_frame_index = 0,
           contract::RationalRateHz sample_rate = presentation::kExcitationRateHz) {
    return presentation::ExhaustExcitationBlockView::borrow_for_callback(
        first_frame_index, sample_rate, block.route_ids,
        kExcitationFramesPerMethodBlock, block.values);
}

void test_failed_begin_does_not_abort_or_write(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_begin = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_begin_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan(kernel, kernel)}; },
        expected,
        "presentation session construction accepted a failed transaction begin");
    expect(sink.begin_calls == 1 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "failed transaction begin wrote output or issued an abort");
}

void test_invalid_first_block_aborts_once(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel,
    bool use_wrong_rate) {
    RecordingSink sink;
    SyntheticExcitationBlock frames;

    {
        PresentationRenderSession session{sink, make_plan(kernel, kernel)};
        const auto header_write_count = sink.writes.size();
        expect(header_write_count != 0,
               "valid presentation session construction emitted no WAVE headers");

        if (use_wrong_rate) {
            expect_throw<std::invalid_argument>(
                [&] {
                    session.process(
                        make_block(frames, 0, contract::RationalRateHz{9999, 1}));
                },
                "presentation session accepted the wrong first-block sample rate");
        } else {
            std::swap(frames.route_ids[0], frames.route_ids[1]);
            expect_throw<std::invalid_argument>(
                [&] { session.process(make_block(frames)); },
                "presentation session accepted swapped first-block routes");
        }

        expect(session.state() == PresentationRenderSessionState::aborted &&
                   sink.writes.size() == header_write_count && sink.seals.empty() &&
                   sink.commit_calls == 0 && sink.abort_calls == 1,
               "invalid first block did not abort exactly once before publication");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "invalid first-block destruction aborted twice or committed");
}

void test_rejected_first_declaration_aborts_constructor_once(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_first_declaration = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_first_declaration_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan(kernel, kernel)}; },
        expected, "presentation session construction accepted a rejected declaration");
    expect(sink.begin_calls == 1 && sink.declarations.size() == 1 &&
               sink.writes.empty() && sink.seals.empty() && sink.commit_calls == 0 &&
               sink.abort_calls == 1,
           "rejected declaration leaked or multiply aborted its transaction");
}

void test_pre_requested_stop_leaves_sink_idle(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    std::stop_source stop;
    stop.request_stop();

    expect_throw<std::runtime_error>(
        [&] {
            PresentationRenderSession session{sink, make_plan(kernel, kernel),
                                              RenderControl{stop.get_token()}};
        },
        "presentation session construction accepted a pre-requested stop");
    expect(sink.begin_calls == 0 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "pre-requested stop touched the idle sink");
}

void test_destructor_aborts_successful_construction_once(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    {
        PresentationRenderSession session{sink, make_plan(kernel, kernel)};
        expect(sink.begin_calls == 1 &&
                   sink.declarations.size() == presentation_audio_artifact_count(2) &&
                   !sink.writes.empty() && sink.seals.empty() &&
                   sink.commit_calls == 0 && sink.abort_calls == 0,
               "active presentation session had an invalid initial sink lifecycle");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "active presentation session destructor did not abort exactly once");
}

void test_rejected_first_header_write_aborts_constructor_once(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_first_write = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_first_write_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan(kernel, kernel)}; },
        expected,
        "presentation session construction accepted a rejected first WAVE header");
    expect(sink.begin_calls == 1 &&
               sink.declarations.size() == presentation_audio_artifact_count(2) &&
               sink.writes.size() == 1 && sink.seals.empty() &&
               sink.commit_calls == 0 && sink.abort_calls == 1,
           "rejected first WAVE header leaked or multiply aborted its transaction");
}

void test_rejected_payload_write_preserves_sink_error(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    {
        PresentationRenderSession session{sink, make_plan(kernel, kernel, 1, 0)};
        const auto writes_before_payload = sink.writes.size();
        const RenderSinkError expected{
            RenderSinkErrorKind::protocol_violation,
            "injected_payload_protocol_failure",
            "injected payload write protocol failure",
        };
        sink.next_write_error = expected;

        SyntheticExcitationBlock frames;
        fill_block(frames, 0);
        expect_sink_failure(
            [&] { session.process(make_block(frames)); }, expected,
            "payload sink rejection was flattened into a WAVE callback error");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   sink.writes.size() == writes_before_payload + 1 &&
                   sink.seals.empty() && sink.commit_calls == 0 &&
                   sink.abort_calls == 1,
               "payload sink rejection did not abort exactly once");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "payload sink failure destruction retried transaction cleanup");
}

void test_manifest_evidence_mismatch_aborts_before_commit(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    const auto plan = make_plan(kernel, kernel, 1, 0);
    {
        PresentationRenderSession session{sink, plan};
        SyntheticExcitationBlock frames;
        fill_block(frames, 0);
        session.process(make_block(frames));
        const auto evidence = session.finish();

        contract::RenderManifest mismatched;
        mismatched.content.output_contract = plan.output_contract;
        mismatched.content.artifacts.assign(evidence.artifacts().begin(),
                                            evidence.artifacts().end());
        ++mismatched.content.artifacts.front().byte_count;
        mismatched.execution = evidence.execution().facts();
        expect_throw<std::logic_error>(
            [&] {
                session.commit(evidence, mismatched, contract::ProvenanceLedger{},
                               contract::SourceMatrixContract{});
            },
            "session accepted a manifest differing from its sealed artifacts");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   sink.commit_calls == 0 && sink.abort_calls == 1,
               "mismatched manifest reached the terminal sink commit");
    }
    expect(sink.commit_calls == 0 && sink.abort_calls == 1,
           "manifest-mismatch destruction retried transaction cleanup");
}

void test_complete_evidence_rejects_incomplete_manifest_before_commit(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    const auto plan = make_plan(kernel, kernel, 1, 0);
    {
        PresentationRenderSession session{sink, plan};
        SyntheticExcitationBlock frames;
        fill_block(frames, 0);
        session.process(make_block(frames));
        const auto evidence = session.finish();

        contract::RenderManifest incomplete;
        incomplete.content.output_contract = plan.output_contract;
        incomplete.content.artifacts.assign(evidence.artifacts().begin(),
                                            evidence.artifacts().end());
        incomplete.execution = evidence.execution().facts();
        expect_throw<std::logic_error>(
            [&] {
                session.commit(evidence, incomplete, contract::ProvenanceLedger{},
                               contract::SourceMatrixContract{});
            },
            "session committed a structurally incomplete manifest");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   sink.commit_calls == 0 && sink.abort_calls == 1,
               "invalid completed manifest reached the terminal sink commit");
    }
    expect(sink.commit_calls == 0 && sink.abort_calls == 1,
           "invalid-manifest destruction retried transaction cleanup");
}

void test_variable_timeline_and_route_settings(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    const auto alternate_kernel = make_synthetic_kernel(UINT32_C(0xbb67ae85));
    RecordingSink sink;
    {
        PresentationRenderSession session{
            sink, make_plan(kernel, alternate_kernel, 3, 1, {0.0, 1.0})};
        SyntheticExcitationBlock frames;
        for (std::uint64_t block = 0; block < 3; ++block) {
            fill_block(frames, block);
            session.process(
                make_block(frames, block * kExcitationFramesPerMethodBlock));
        }

        const auto evidence = session.finish();
        constexpr std::array<std::string_view, 8> expected_sha256{
            "8b5dd66b5b6d7942d4ff11d0d2b7d640bea0d9fc284607463bc8e776c08d86f0",
            "1fb47102925ae4e92190bf24e6db21613d7dac42ad64d9ca6e0a89d21d3aa1af",
            "8b5dd66b5b6d7942d4ff11d0d2b7d640bea0d9fc284607463bc8e776c08d86f0",
            "cb42469d3b3c1cc3d47af3e99320986a7e140824ea7d1a42f039eb1e252fca16",
            "faeea41b9a09810e67cf9f52db38518fe5ed23bbe0fb96e0df7d225eb5ff76e9",
            "faeea41b9a09810e67cf9f52db38518fe5ed23bbe0fb96e0df7d225eb5ff76e9",
            "e49960a51ef6dcb60282f7eef128812e26dcdc351f0c1cd334dd17be3e933e31",
            "5ecf2631c5340687ae822660635296e66e2a1dd02b9545f12c2b4ac1397998a4",
        };
        for (std::size_t index = 0; index < evidence.artifacts().size(); ++index) {
            const auto &record = evidence.artifacts()[index];
            const auto expected_byte_count = index + 1 == evidence.artifacts().size()
                                                 ? UINT64_C(23206)
                                                 : UINT64_C(30778);
            expect(record.byte_count == expected_byte_count &&
                       digest_hex(record.payload_sha256) == expected_sha256[index],
                   "generic presentation integration golden changed");
        }
        expect(session.state() == PresentationRenderSessionState::sealed,
               "variable presentation timeline did not seal");
        expect(evidence.stats() ==
                   PresentationRenderStats{600, 3, 1, 2, 11'520, 3'840, 7'680},
               "variable presentation timeline produced incorrect exact counts");
        expect(sink.seals.size() == presentation_audio_artifact_count(2),
               "variable presentation timeline did not seal all artifacts");
        for (const auto &record : evidence.artifacts()) {
            expect(record.audio.has_value() && record.audio->frame_count == 7'680,
                   "variable presentation artifact has the wrong frame horizon");
        }
        expect(evidence.artifacts()[0].payload_sha256 ==
                   evidence.artifacts()[2].payload_sha256,
               "zero-wet route did not select its dry signal exactly");
        expect(evidence.artifacts()[4].payload_sha256 ==
                   evidence.artifacts()[5].payload_sha256,
               "fully wet route did not select its configured-IR signal exactly");
        expect(evidence.artifacts()[1].payload_sha256 !=
                   evidence.artifacts()[4].payload_sha256,
               "distinct per-route IR kernels were silently collapsed");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "sealed variable presentation did not close its uncommitted transaction");

    RecordingSink zero_preparation_sink;
    {
        PresentationRenderSession session{zero_preparation_sink,
                                          make_plan(kernel, kernel, 1, 0)};
        SyntheticExcitationBlock frames;
        fill_block(frames, 0);
        session.process(make_block(frames));
        const auto evidence = session.finish();
        expect(evidence.stats() ==
                   PresentationRenderStats{200, 1, 0, 1, 3'840, 0, 3'840},
               "zero-preparation presentation timeline changed its exact counts");
    }
    expect(zero_preparation_sink.abort_calls == 1,
           "zero-preparation sealed session leaked its transaction");
}

void test_dynamic_route_count_publication(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    for (const std::size_t route_count : {std::size_t{1}, std::size_t{3}}) {
        std::vector<std::shared_ptr<const dsp::FixedConvolutionKernel>> kernels(
            route_count, kernel);
        std::vector<double> wet_mix(route_count, 0.5);
        auto plan = make_plan_for_routes(std::move(kernels), 1, 0, std::move(wet_mix));
        expect(plan.routes.size() == route_count &&
                   plan.audition.selected_route_ids.size() == route_count &&
                   plan.output_contract.required_source_routes.size() == route_count &&
                   plan.output_contract.required_output_buses.size() == 2 &&
                   plan.output_contract.required_artifacts.size() ==
                       presentation_audio_artifact_count(route_count),
               "dynamic presentation plan has the wrong R-dependent topology");

        RecordingSink sink;
        {
            PresentationRenderSession session{sink, std::move(plan)};
            SyntheticExcitationBlock frames{route_count};
            fill_block(frames, 0);
            session.process(make_block(frames));
            const auto evidence = session.finish();
            expect(evidence.artifacts().size() ==
                           presentation_audio_artifact_count(route_count) &&
                       sink.declarations.size() ==
                           presentation_audio_artifact_count(route_count) &&
                       sink.seals.size() ==
                           presentation_audio_artifact_count(route_count) &&
                       evidence.stats() ==
                           PresentationRenderStats{200, 1, 0, 1, 3'840, 0, 3'840},
                   "dynamic presentation session did not publish 3*R+2 artifacts");
            for (std::size_t route = 0; route < route_count; ++route) {
                const auto base = route * kPresentationArtifactsPerRoute;
                expect(evidence.artifacts()[base].role ==
                               "test.synthetic.audio." + std::to_string(base) &&
                           evidence.artifacts()[base + 1].role ==
                               "test.synthetic.audio." + std::to_string(base + 1) &&
                           evidence.artifacts()[base + 2].role ==
                               "test.synthetic.audio." + std::to_string(base + 2),
                       "dynamic route artifacts lost deterministic route order");
            }
        }
        expect(sink.abort_calls == 1 && sink.commit_calls == 0,
               "sealed dynamic-route session leaked its transaction");
    }
}

void test_invalid_or_incomplete_timeline_fails_closed(
    const std::shared_ptr<const dsp::FixedConvolutionKernel> &kernel) {
    RecordingSink invalid_method_sink;
    auto invalid_method_plan = make_plan(kernel, kernel);
    invalid_method_plan.methods.conditioning.version += 1;
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{invalid_method_sink,
                                              std::move(invalid_method_plan)};
        },
        "presentation accepted a method outside the implemented identity set");
    expect(invalid_method_sink.begin_calls == 0 && invalid_method_sink.abort_calls == 0,
           "invalid presentation method touched the sink");

    RecordingSink invalid_conditioning_sink;
    auto invalid_conditioning_plan = make_plan(kernel, kernel);
    invalid_conditioning_plan.conditioning.air_noise_mix_01 = 1.1;
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{invalid_conditioning_sink,
                                              std::move(invalid_conditioning_plan)};
        },
        "presentation accepted conditioning outside the executable domain");
    expect(invalid_conditioning_sink.begin_calls == 0 &&
               invalid_conditioning_sink.abort_calls == 0,
           "invalid presentation conditioning touched the sink");

    RecordingSink negative_zero_wet_sink;
    auto negative_zero_wet_plan = make_plan(kernel, kernel, 850, 100, {-0.0, 1.0});
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{negative_zero_wet_sink,
                                              std::move(negative_zero_wet_plan)};
        },
        "presentation accepted negative-zero wet mix");
    expect(negative_zero_wet_sink.begin_calls == 0 &&
               negative_zero_wet_sink.abort_calls == 0,
           "negative-zero presentation wet mix touched the sink");

    RecordingSink invalid_sink;
    auto invalid_plan = make_plan(kernel, kernel);
    invalid_plan.timeline.pre_audible_block_count =
        invalid_plan.timeline.total_block_count;
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{invalid_sink, std::move(invalid_plan)};
        },
        "presentation accepted a timeline without an audible interval");
    expect(invalid_sink.begin_calls == 0 && invalid_sink.abort_calls == 0,
           "invalid presentation timeline touched the sink");

    RecordingSink unspecified_tail_sink;
    auto unspecified_tail_plan = make_plan(kernel, kernel);
    unspecified_tail_plan.timeline.tail_policy = PresentationTailPolicy::unspecified;
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{unspecified_tail_sink,
                                              std::move(unspecified_tail_plan)};
        },
        "presentation accepted an unspecified convolution-tail policy");
    expect(unspecified_tail_sink.begin_calls == 0 &&
               unspecified_tail_sink.abort_calls == 0,
           "unspecified presentation tail policy touched the sink");

    RecordingSink misbound_role_sink;
    auto misbound_role_plan = make_plan(kernel, kernel);
    std::swap(misbound_role_plan.routes[0].artifacts.dry,
              misbound_role_plan.routes[1].artifacts.dry);
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{misbound_role_sink,
                                              std::move(misbound_role_plan)};
        },
        "presentation accepted an artifact role under the wrong route");
    expect(misbound_role_sink.begin_calls == 0 && misbound_role_sink.abort_calls == 0,
           "misbound presentation artifact role touched the sink");

    RecordingSink incomplete_sink;
    PresentationRenderSession incomplete{incomplete_sink,
                                         make_plan(kernel, kernel, 2, 1)};
    expect_throw<std::logic_error>([&] { static_cast<void>(incomplete.finish()); },
                                   "presentation finalized an incomplete timeline");
    expect(incomplete.state() == PresentationRenderSessionState::aborted &&
               incomplete_sink.abort_calls == 1 && incomplete_sink.seals.empty(),
           "incomplete presentation did not abort before artifact sealing");
}

void run_tests() {
    const auto kernel = make_synthetic_kernel();
    test_failed_begin_does_not_abort_or_write(kernel);
    test_rejected_first_declaration_aborts_constructor_once(kernel);
    test_pre_requested_stop_leaves_sink_idle(kernel);
    test_invalid_first_block_aborts_once(kernel, false);
    test_invalid_first_block_aborts_once(kernel, true);
    test_destructor_aborts_successful_construction_once(kernel);
    test_rejected_first_header_write_aborts_constructor_once(kernel);
    test_rejected_payload_write_preserves_sink_error(kernel);
    test_variable_timeline_and_route_settings(kernel);
    test_dynamic_route_count_publication(kernel);
    test_invalid_or_incomplete_timeline_fails_closed(kernel);
    test_manifest_evidence_mismatch_aborts_before_commit(kernel);
    test_complete_evidence_rejects_incomplete_manifest_before_commit(kernel);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "presentation-session boundary test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
