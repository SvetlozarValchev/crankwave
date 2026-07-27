#include "reference/p18_reference_render_session.hpp"

#include "presentation/p18_mastering.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

constexpr std::array<presentation::P18RouteConditioningSeeds,
                     presentation::kP18ExhaustRouteCount>
    kSyntheticSeeds{
        presentation::P18RouteConditioningSeeds{
            {UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
            {UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
        },
        presentation::P18RouteConditioningSeeds{
            {UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
            {UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
        },
    };

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
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

[[nodiscard]] P18PresentationSessionPlan make_plan() {
    P18PresentationSessionPlan plan;
    plan.output_contract.source_matrix_id = "test.synthetic.p18-session-boundary";
    plan.output_contract.distribution = contract::DistributionIntent::local_evaluation;

    const contract::AudioContract float_audio{
        {192000, 1},
        presentation::kP18AudibleFrameCount,
        "mono",
        "float32le",
    };
    const contract::AudioContract audition_audio{
        {192000, 1},
        presentation::kP18AudibleFrameCount,
        "mono",
        "pcm_s24le",
    };

    for (std::size_t index = 0; index < plan.audio_artifacts.size(); ++index) {
        const auto role = "test.synthetic.audio." + std::to_string(index);
        const auto path = "audio/synthetic-" + std::to_string(index) + ".wav";
        const auto &audio =
            index + 1 == plan.audio_artifacts.size() ? audition_audio : float_audio;
        plan.audio_artifacts[index] = {
            role, contract::ArtifactKind::audio, path, audio, false,
        };
        plan.output_contract.required_artifacts.push_back({
            role,
            contract::ArtifactKind::audio,
            audio,
            false,
        });
    }
    return plan;
}

[[nodiscard]] std::shared_ptr<const dsp::P18FixedConvolutionKernel>
make_synthetic_kernel() {
    std::vector<double> coefficients(dsp::P18FixedConvolutionKernel::coefficient_count);
    std::uint32_t state = UINT32_C(0x6a09e667);
    for (double &coefficient : coefficients) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const std::int64_t centered =
            static_cast<std::int64_t>(state) - INT64_C(2147483648);
        coefficient = static_cast<double>(centered) * 0x1p-47;
    }
    auto fft_plan = std::make_shared<const dsp::P18FixedFftPlan>();
    return std::make_shared<const dsp::P18FixedConvolutionKernel>(coefficients,
                                                                  std::move(fft_plan));
}

[[nodiscard]] presentation::ExhaustExcitationBlockView make_block(
    std::array<presentation::ExhaustExcitationFrame,
               presentation::kP18PhysicsFramesPerMethodBlock> &frames,
    contract::RationalRateHz sample_rate = presentation::kP18ExcitationRateHz,
    std::array<contract::RouteId, presentation::kP18ExhaustRouteCount> route_ids =
        presentation::kP18ReferenceRouteIds) {
    return presentation::ExhaustExcitationBlockView::borrow_for_callback(
        0, sample_rate, route_ids, frames);
}

void test_failed_begin_does_not_abort_or_write(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_begin = true;

    expect_throw<std::runtime_error>(
        [&] {
            P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel};
        },
        "P1.8 session construction accepted a failed transaction begin");
    expect(sink.begin_calls == 1 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "failed transaction begin wrote output or issued an abort");
}

void test_invalid_first_block_aborts_once(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel,
    bool use_wrong_rate) {
    RecordingSink sink;
    std::array<presentation::ExhaustExcitationFrame,
               presentation::kP18PhysicsFramesPerMethodBlock>
        frames{};

    {
        P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel};
        const auto header_write_count = sink.writes.size();
        expect(header_write_count != 0,
               "valid P1.8 session construction emitted no WAVE headers");

        if (use_wrong_rate) {
            expect_throw<std::invalid_argument>(
                [&] {
                    session.process(
                        make_block(frames, contract::RationalRateHz{9999, 1}));
                },
                "P1.8 session accepted the wrong first-block sample rate");
        } else {
            auto routes = presentation::kP18ReferenceRouteIds;
            std::swap(routes[0], routes[1]);
            expect_throw<std::invalid_argument>(
                [&] {
                    session.process(
                        make_block(frames, presentation::kP18ExcitationRateHz, routes));
                },
                "P1.8 session accepted swapped first-block routes");
        }

        expect(session.state() == P18PresentationSessionState::aborted &&
                   sink.writes.size() == header_write_count && sink.seals.empty() &&
                   sink.commit_calls == 0 && sink.abort_calls == 1,
               "invalid first block did not abort exactly once before publication");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "invalid first-block destruction aborted twice or committed");
}

void test_rejected_first_declaration_aborts_constructor_once(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_first_declaration = true;

    expect_throw<std::runtime_error>(
        [&] {
            P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel};
        },
        "P1.8 session construction accepted a rejected declaration");
    expect(sink.begin_calls == 1 && sink.declarations.size() == 1 &&
               sink.writes.empty() && sink.seals.empty() && sink.commit_calls == 0 &&
               sink.abort_calls == 1,
           "rejected declaration leaked or multiply aborted its transaction");
}

void test_pre_requested_stop_leaves_sink_idle(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    std::stop_source stop;
    stop.request_stop();

    expect_throw<std::runtime_error>(
        [&] {
            P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel,
                                           RenderControl{stop.get_token()}};
        },
        "P1.8 session construction accepted a pre-requested stop");
    expect(sink.begin_calls == 0 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "pre-requested stop touched the idle sink");
}

void test_destructor_aborts_successful_construction_once(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    {
        P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel};
        expect(sink.begin_calls == 1 &&
                   sink.declarations.size() == kP18PresentationAudioArtifactCount &&
                   !sink.writes.empty() && sink.seals.empty() &&
                   sink.commit_calls == 0 && sink.abort_calls == 0,
               "active P1.8 session had an invalid initial sink lifecycle");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "active P1.8 session destructor did not abort exactly once");
}

void test_rejected_first_header_write_aborts_constructor_once(
    const std::shared_ptr<const dsp::P18FixedConvolutionKernel> &kernel) {
    RecordingSink sink;
    sink.reject_first_write = true;

    expect_throw<std::runtime_error>(
        [&] {
            P18PresentationSession session{sink, make_plan(), kSyntheticSeeds, kernel};
        },
        "P1.8 session construction accepted a rejected first WAVE header");
    expect(sink.begin_calls == 1 &&
               sink.declarations.size() == kP18PresentationAudioArtifactCount &&
               sink.writes.size() == 1 && sink.seals.empty() &&
               sink.commit_calls == 0 && sink.abort_calls == 1,
           "rejected first WAVE header leaked or multiply aborted its transaction");
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
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "P1.8 presentation-session boundary test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
