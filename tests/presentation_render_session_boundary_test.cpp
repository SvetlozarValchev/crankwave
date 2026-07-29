#include "presentation/presentation_render_session.hpp"

#include "acoustics/exhaust_acoustic_session.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
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

constexpr std::array<contract::RouteId, kPresentationExhaustOutletCount>
    kSyntheticRouteIds{
        contract::RouteId{101},
        contract::RouteId{202},
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

    struct Payload {
        std::string role;
        std::vector<std::byte> bytes;
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
    std::vector<Payload> payloads;

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
        payloads.push_back({artifact.role, {}});
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
        const auto payload = std::ranges::find(payloads, chunk.role, &Payload::role);
        if (payload == payloads.end() || chunk.byte_offset != payload->bytes.size()) {
            return RenderSinkError{
                RenderSinkErrorKind::protocol_violation,
                "recording_sink_noncontiguous_write",
                "recording sink received a noncontiguous artifact write",
            };
        }
        payload->bytes.insert(payload->bytes.end(), chunk.bytes.begin(),
                              chunk.bytes.end());
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

    [[nodiscard]] const std::vector<std::byte> &payload(std::string_view role) const {
        const auto found = std::ranges::find(payloads, role, &Payload::role);
        if (found == payloads.end()) {
            throw std::logic_error{"recording sink has no requested payload"};
        }
        return found->bytes;
    }
};

[[nodiscard]] PresentationRenderPlan make_plan(std::uint64_t total_frame_count = 8,
                                               std::uint64_t pre_audible_frame_count = 3,
                                               float audition_gain = 0.5F) {
    const auto audible_frame_count = total_frame_count - pre_audible_frame_count;
    const contract::AudioContract float_audio{
        {192000, 1}, audible_frame_count, "mono", "float32le"};
    const contract::AudioContract audition_audio{
        {192000, 1}, audible_frame_count, "mono", "pcm_s24le"};

    const std::array<PendingArtifact, kPresentationAudioArtifactCount> artifacts{
        PendingArtifact{"test.outlet.front.pressure", contract::ArtifactKind::audio,
                        "audio/front-pressure.wav", float_audio, false},
        PendingArtifact{"test.outlet.rear.pressure", contract::ArtifactKind::audio,
                        "audio/rear-pressure.wav", float_audio, false},
        PendingArtifact{"test.master.raw", contract::ArtifactKind::audio,
                        "audio/master-raw.wav", float_audio, false},
        PendingArtifact{"test.master.audition", contract::ArtifactKind::audio,
                        "audio/master-audition.wav", audition_audio, false},
    };

    contract::OutputContract output_contract;
    output_contract.source_matrix_id = "test.pressure-presentation";
    output_contract.distribution = contract::DistributionIntent::local_evaluation;
    output_contract.required_source_routes = {
        {
            "test.outlet.front",
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {artifacts[0].role},
        },
        {
            "test.outlet.rear",
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {artifacts[1].role},
        },
    };
    output_contract.required_output_buses = {
        {
            "test.master.raw",
            contract::OutputBusKind::master_engine_raw,
            {artifacts[2].role},
        },
        {
            "test.master.audition",
            contract::OutputBusKind::master_engine_audition,
            {artifacts[3].role},
        },
    };
    for (const auto &artifact : artifacts) {
        output_contract.required_artifacts.push_back({
            artifact.role, artifact.kind, artifact.audio, artifact.diagnostic});
    }

    const auto fade_frames = std::min<std::uint64_t>(2, audible_frame_count / 2);
    return {
        std::move(output_contract),
        {
            total_frame_count,
            pre_audible_frame_count,
            PresentationTailPolicy::truncate_at_timeline_end,
        },
        {{
            {kSyntheticRouteIds[0], "test.outlet.front", artifacts[0]},
            {kSyntheticRouteIds[1], "test.outlet.rear", artifacts[1]},
        }},
        10.0,
        {
            MasteringSettings{audible_frame_count, fade_frames, fade_frames,
                              audition_gain},
            {"Synthetic pressure presentation", "Synthetic render", "test-suite"},
            artifacts[2],
            artifacts[3],
        },
    };
}

[[nodiscard]] acoustics::ExhaustAcousticPressureBlock
make_block(std::uint64_t first_frame_index, std::size_t frame_count,
           contract::RationalRateHz rate = {192000, 1},
           std::array<contract::RouteId, kPresentationExhaustOutletCount> route_ids =
               kSyntheticRouteIds) {
    acoustics::ExhaustAcousticPressureBlock block{
        rate,
        first_frame_index,
        {
            acoustics::RadiatedExhaustOutletBlock{route_ids[0],
                                                  std::vector<double>(frame_count)},
            acoustics::RadiatedExhaustOutletBlock{route_ids[1],
                                                  std::vector<double>(frame_count)},
        },
    };
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto global = first_frame_index + frame;
        block.outlets[0].pressure_pa[frame] =
            static_cast<double>(global + 1U);
        block.outlets[1].pressure_pa[frame] =
            -0.25 * static_cast<double>(global + 1U);
    }
    return block;
}

[[nodiscard]] std::uint32_t read_u32le(std::span<const std::byte> bytes,
                                       std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw std::runtime_error{"truncated WAVE u32"};
    }
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::span<const std::byte>
wave_data(std::span<const std::byte> bytes) {
    if (bytes.size() < 12U || std::memcmp(bytes.data(), "RIFF", 4U) != 0 ||
        std::memcmp(bytes.data() + 8U, "WAVE", 4U) != 0) {
        throw std::runtime_error{"payload is not a RIFF/WAVE file"};
    }
    std::size_t offset = 12U;
    while (offset <= bytes.size() && bytes.size() - offset >= 8U) {
        const auto chunk_size = static_cast<std::size_t>(read_u32le(bytes, offset + 4U));
        const auto payload_offset = offset + 8U;
        if (chunk_size > bytes.size() - payload_offset) {
            throw std::runtime_error{"truncated WAVE chunk"};
        }
        if (std::memcmp(bytes.data() + offset, "data", 4U) == 0) {
            return bytes.subspan(payload_offset, chunk_size);
        }
        offset = payload_offset + chunk_size + (chunk_size & 1U);
    }
    throw std::runtime_error{"WAVE file has no data chunk"};
}

[[nodiscard]] std::vector<float>
decode_float32_wave(const std::vector<std::byte> &wave) {
    const auto data = wave_data(wave);
    if (data.size() % 4U != 0U) {
        throw std::runtime_error{"Float32 WAVE payload is not frame-aligned"};
    }
    std::vector<float> result;
    result.reserve(data.size() / 4U);
    for (std::size_t offset = 0; offset < data.size(); offset += 4U) {
        result.push_back(std::bit_cast<float>(read_u32le(data, offset)));
    }
    return result;
}

[[nodiscard]] std::vector<std::int32_t>
decode_pcm24_wave(const std::vector<std::byte> &wave) {
    const auto data = wave_data(wave);
    if (data.size() % 3U != 0U) {
        throw std::runtime_error{"PCM24 WAVE payload is not frame-aligned"};
    }
    std::vector<std::int32_t> result;
    result.reserve(data.size() / 3U);
    for (std::size_t offset = 0; offset < data.size(); offset += 3U) {
        std::uint32_t bits = std::to_integer<std::uint32_t>(data[offset]) |
                             (std::to_integer<std::uint32_t>(data[offset + 1U]) << 8U) |
                             (std::to_integer<std::uint32_t>(data[offset + 2U]) << 16U);
        if ((bits & UINT32_C(0x00800000)) != 0U) {
            bits |= UINT32_C(0xff000000);
        }
        result.push_back(static_cast<std::int32_t>(bits));
    }
    return result;
}

void test_failed_begin_does_not_abort_or_write() {
    RecordingSink sink;
    sink.reject_begin = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_begin_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan()}; }, expected,
        "presentation construction accepted a failed transaction begin");
    expect(sink.begin_calls == 1 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "failed transaction begin wrote output or issued an abort");
}

void test_rejected_first_declaration_aborts_constructor_once() {
    RecordingSink sink;
    sink.reject_first_declaration = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_first_declaration_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan()}; }, expected,
        "presentation construction accepted a rejected declaration");
    expect(sink.begin_calls == 1 && sink.declarations.size() == 1 &&
               sink.writes.empty() && sink.seals.empty() && sink.commit_calls == 0 &&
               sink.abort_calls == 1,
           "rejected declaration leaked or multiply aborted its transaction");
}

void test_pre_requested_stop_leaves_sink_idle() {
    RecordingSink sink;
    std::stop_source stop;
    stop.request_stop();

    expect_throw<std::runtime_error>(
        [&] {
            PresentationRenderSession session{sink, make_plan(),
                                              RenderControl{stop.get_token()}};
        },
        "presentation construction accepted a pre-requested stop");
    expect(sink.begin_calls == 0 && sink.declarations.empty() && sink.writes.empty() &&
               sink.seals.empty() && sink.commit_calls == 0 && sink.abort_calls == 0,
           "pre-requested stop touched the idle sink");
}

void test_destructor_aborts_successful_construction_once() {
    RecordingSink sink;
    {
        PresentationRenderSession session{sink, make_plan()};
        expect(sink.begin_calls == 1 &&
                   sink.declarations.size() == kPresentationAudioArtifactCount &&
                   !sink.writes.empty() && sink.seals.empty() &&
                   sink.commit_calls == 0 && sink.abort_calls == 0,
               "active presentation session had an invalid initial sink lifecycle");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "active presentation destructor did not abort exactly once");
}

void test_rejected_first_header_write_aborts_constructor_once() {
    RecordingSink sink;
    sink.reject_first_write = true;

    const RenderSinkError expected{
        RenderSinkErrorKind::publication_failure,
        "injected_first_write_failure",
        "injected recording-sink failure",
    };
    expect_sink_failure(
        [&] { PresentationRenderSession session{sink, make_plan()}; }, expected,
        "presentation construction accepted a rejected first WAVE header");
    expect(sink.begin_calls == 1 &&
               sink.declarations.size() == kPresentationAudioArtifactCount &&
               sink.writes.size() == 1 && sink.seals.empty() &&
               sink.commit_calls == 0 && sink.abort_calls == 1,
           "rejected first WAVE header leaked or multiply aborted its transaction");
}

void test_invalid_first_block_aborts_once() {
    RecordingSink wrong_rate_sink;
    {
        PresentationRenderSession session{wrong_rate_sink, make_plan()};
        const auto header_write_count = wrong_rate_sink.writes.size();
        expect_throw<std::invalid_argument>(
            [&] { session.process(make_block(0, 2, {191999, 1})); },
            "presentation accepted the wrong acoustic sample rate");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   wrong_rate_sink.writes.size() == header_write_count &&
                   wrong_rate_sink.abort_calls == 1,
               "wrong-rate input did not abort before payload publication");
    }
    expect(wrong_rate_sink.abort_calls == 1,
           "wrong-rate destruction aborted the transaction twice");

    RecordingSink swapped_routes_sink;
    {
        PresentationRenderSession session{swapped_routes_sink, make_plan()};
        auto routes = kSyntheticRouteIds;
        std::swap(routes[0], routes[1]);
        expect_throw<std::invalid_argument>(
            [&] { session.process(make_block(0, 2, {192000, 1}, routes)); },
            "presentation accepted swapped acoustic outlet routes");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   swapped_routes_sink.abort_calls == 1,
               "swapped outlet routes did not abort exactly once");
    }
}

void test_rejected_payload_write_preserves_sink_error() {
    RecordingSink sink;
    {
        PresentationRenderSession session{sink, make_plan(2, 0)};
        const auto writes_before_payload = sink.writes.size();
        const RenderSinkError expected{
            RenderSinkErrorKind::protocol_violation,
            "injected_payload_protocol_failure",
            "injected payload write protocol failure",
        };
        sink.next_write_error = expected;

        expect_sink_failure(
            [&] { session.process(make_block(0, 2)); }, expected,
            "payload sink rejection was flattened into a WAVE callback error");
        expect(session.state() == PresentationRenderSessionState::aborted &&
                   sink.writes.size() == writes_before_payload + 1 &&
                   sink.seals.empty() && sink.commit_calls == 0 &&
                   sink.abort_calls == 1,
               "payload sink rejection did not abort exactly once");
    }
    expect(sink.abort_calls == 1 && sink.commit_calls == 0,
           "payload failure destruction retried transaction cleanup");
}

struct RenderedOutput {
    PresentationRenderStats stats;
    std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> records;
    std::array<std::vector<std::byte>, kPresentationAudioArtifactCount> wave_bytes;
    std::size_t abort_calls = 0;
};

[[nodiscard]] RenderedOutput render_partitioned(std::span<const std::size_t> partitions) {
    RecordingSink sink;
    PresentationRenderStats stats;
    std::array<contract::ArtifactRecord, kPresentationAudioArtifactCount> records;
    {
        PresentationRenderSession session{sink, make_plan()};
        std::uint64_t first_frame = 0;
        for (const auto frame_count : partitions) {
            session.process(make_block(first_frame, frame_count));
            first_frame += frame_count;
        }
        const auto evidence = session.finish();
        stats = evidence.stats();
        records = evidence.artifacts();
        expect(session.state() == PresentationRenderSessionState::sealed,
               "completed pressure render did not seal");
    }

    std::array<std::vector<std::byte>, kPresentationAudioArtifactCount> wave_bytes;
    for (std::size_t index = 0; index < records.size(); ++index) {
        wave_bytes[index] = sink.payload(records[index].role);
    }
    return {stats, records, std::move(wave_bytes), sink.abort_calls};
}

void test_pressure_calibration_crop_mastering_and_partition_invariance() {
    constexpr std::array<std::size_t, 3> split{2, 4, 2};
    constexpr std::array<std::size_t, 1> joined{8};
    const auto partitioned = render_partitioned(split);
    const auto one_block = render_partitioned(joined);

    expect(partitioned.stats == PresentationRenderStats{8, 3, 3, 5},
           "frame-domain pressure timeline produced incorrect exact counts");
    expect(one_block.stats == PresentationRenderStats{8, 1, 3, 5},
           "one-block pressure timeline produced incorrect exact counts");
    expect(partitioned.records == one_block.records &&
               partitioned.wave_bytes == one_block.wave_bytes,
           "pressure presentation output depends on input block partitioning");
    expect(partitioned.abort_calls == 1 && one_block.abort_calls == 1,
           "sealed uncommitted pressure render leaked its transaction");

    const auto front = decode_float32_wave(partitioned.wave_bytes[0]);
    const auto rear = decode_float32_wave(partitioned.wave_bytes[1]);
    const auto raw = decode_float32_wave(partitioned.wave_bytes[2]);
    const auto audition = decode_pcm24_wave(partitioned.wave_bytes[3]);
    expect(front.size() == 5 && rear.size() == 5 && raw.size() == 5 &&
               audition.size() == 5,
           "pressure presentation published the wrong audible frame horizon");

    const MasteringSettings mastering{5, 2, 2, 0.5F};
    for (std::size_t frame = 0; frame < 5; ++frame) {
        const auto global = frame + 3U;
        const float expected_front =
            static_cast<float>(static_cast<double>(global + 1U) / 10.0);
        const float expected_rear =
            static_cast<float>((-0.25 * static_cast<double>(global + 1U)) / 10.0);
        const auto expected_mastered =
            master_frame(expected_front, expected_rear, frame, mastering);
        expect(std::bit_cast<std::uint32_t>(front[frame]) ==
                       std::bit_cast<std::uint32_t>(expected_front) &&
                   std::bit_cast<std::uint32_t>(rear[frame]) ==
                       std::bit_cast<std::uint32_t>(expected_rear),
               "outlet stem did not contain the sole Pa/full-scale conversion");
        expect(std::bit_cast<std::uint32_t>(raw[frame]) ==
                   std::bit_cast<std::uint32_t>(expected_mastered.raw),
               "raw master was not the coherent Float32 outlet sum");
        expect(audition[frame] == expected_mastered.pcm24,
               "audition master differed from one common gain plus deterministic "
               "fade");
    }
}

void test_invalid_or_incomplete_plan_and_schedule_fail_closed() {
    RecordingSink invalid_calibration_sink;
    auto invalid_calibration_plan = make_plan();
    invalid_calibration_plan.pa_per_full_scale = 0.0;
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{invalid_calibration_sink,
                                              std::move(invalid_calibration_plan)};
        },
        "presentation accepted zero Pa/full-scale calibration");
    expect(invalid_calibration_sink.begin_calls == 0 &&
               invalid_calibration_sink.abort_calls == 0,
           "invalid Pa calibration touched the sink");

    RecordingSink mismatched_mastering_sink;
    auto mismatched_mastering_plan = make_plan();
    mismatched_mastering_plan.master.mastering = MasteringSettings{6, 2, 2, 0.5F};
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{mismatched_mastering_sink,
                                              std::move(mismatched_mastering_plan)};
        },
        "presentation accepted mastering with a different frame horizon");
    expect(mismatched_mastering_sink.begin_calls == 0 &&
               mismatched_mastering_sink.abort_calls == 0,
           "mismatched mastering touched the sink");

    RecordingSink misbound_route_sink;
    auto misbound_route_plan = make_plan();
    std::swap(misbound_route_plan.outlets[0].pressure_stem_artifact,
              misbound_route_plan.outlets[1].pressure_stem_artifact);
    expect_throw<std::invalid_argument>(
        [&] {
            PresentationRenderSession session{misbound_route_sink,
                                              std::move(misbound_route_plan)};
        },
        "presentation accepted pressure stems under the wrong outlet owners");
    expect(misbound_route_sink.begin_calls == 0 &&
               misbound_route_sink.abort_calls == 0,
           "misbound pressure stem touched the sink");

    RecordingSink incomplete_sink;
    PresentationRenderSession incomplete{incomplete_sink, make_plan()};
    incomplete.process(make_block(0, 4));
    expect_throw<std::logic_error>([&] { static_cast<void>(incomplete.finish()); },
                                   "presentation finalized an incomplete timeline");
    expect(incomplete.state() == PresentationRenderSessionState::aborted &&
               incomplete_sink.abort_calls == 1 && incomplete_sink.seals.empty(),
           "incomplete presentation did not abort before artifact sealing");
}

void test_nonfinite_and_discontinuous_input_fail_before_payload() {
    RecordingSink nonfinite_sink;
    {
        PresentationRenderSession session{nonfinite_sink, make_plan()};
        const auto writes_before = nonfinite_sink.writes.size();
        auto block = make_block(0, 2);
        block.outlets[1].pressure_pa[1] =
            std::numeric_limits<double>::quiet_NaN();
        expect_throw<std::domain_error>([&] { session.process(block); },
                                        "presentation accepted non-finite pressure");
        expect(nonfinite_sink.writes.size() == writes_before &&
                   nonfinite_sink.abort_calls == 1,
               "non-finite pressure reached payload publication");
    }

    RecordingSink discontinuous_sink;
    {
        PresentationRenderSession session{discontinuous_sink, make_plan()};
        expect_throw<std::invalid_argument>(
            [&] { session.process(make_block(1, 2)); },
            "presentation accepted a discontinuous acoustic frame clock");
        expect(discontinuous_sink.abort_calls == 1,
               "discontinuous pressure block did not abort exactly once");
    }
}

void test_manifest_evidence_mismatch_aborts_before_commit() {
    RecordingSink sink;
    const auto plan = make_plan(2, 0);
    {
        PresentationRenderSession session{sink, plan};
        session.process(make_block(0, 2));
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

void run_tests() {
    test_failed_begin_does_not_abort_or_write();
    test_rejected_first_declaration_aborts_constructor_once();
    test_pre_requested_stop_leaves_sink_idle();
    test_destructor_aborts_successful_construction_once();
    test_rejected_first_header_write_aborts_constructor_once();
    test_invalid_first_block_aborts_once();
    test_rejected_payload_write_preserves_sink_error();
    test_pressure_calibration_crop_mastering_and_partition_invariance();
    test_invalid_or_incomplete_plan_and_schedule_fail_closed();
    test_nonfinite_and_discontinuous_input_fail_before_payload();
    test_manifest_evidence_mismatch_aborts_before_commit();
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
