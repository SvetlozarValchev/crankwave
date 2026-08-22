#include "crankwave/artifacts/telemetry_encoder.hpp"
#include "crankwave/artifacts/wav_encoder.hpp"
#include "telemetry_encoder_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::artifacts;
using namespace crankwave::contract;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint16_t read_u16(const std::vector<std::byte> &bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1]))
               << 8U;
}

std::uint32_t read_u32(const std::vector<std::byte> &bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (8U * index);
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::byte> &bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (8U * index);
    }
    return value;
}

bool has_fourcc(const std::vector<std::byte> &bytes, std::size_t offset,
                const std::array<char, 4> &value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (bytes[offset + index] !=
            static_cast<std::byte>(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

struct ByteCollector {
    explicit ByteCollector(std::size_t declared_maximum)
        : declared_maximum(declared_maximum) {}

    bool consume(std::uint64_t offset, std::span<const std::byte> chunk) {
        if (offset != bytes.size() || chunk.empty() ||
            chunk.size() > declared_maximum) {
            valid = false;
            return false;
        }
        maximum_seen = std::max(maximum_seen, chunk.size());
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return true;
    }

    std::size_t declared_maximum;
    std::size_t maximum_seen = 0;
    bool valid = true;
    std::vector<std::byte> bytes;
};

WavEncoder require_wav(WavEncoderResult result) {
    if (const auto *failure = std::get_if<WavEncodingError>(&result)) {
        throw std::runtime_error("WAVE encoder rejected valid contract: " +
                                 failure->path + ": " + failure->message);
    }
    return std::get<WavEncoder>(std::move(result));
}

TelemetryEncoder require_telemetry(TelemetryEncoderResult result) {
    if (const auto *failure = std::get_if<TelemetryEncodingError>(&result)) {
        throw std::runtime_error("telemetry encoder rejected valid contract: " +
                                 failure->path + ": " + failure->message);
    }
    return std::get<TelemetryEncoder>(std::move(result));
}

template <class Error>
void expect_error(const std::optional<Error> &status,
                  decltype(Error::code) expected_code, const char *message) {
    expect(status.has_value() && status->code == expected_code, message);
}

void test_float32_wave_identity_and_bounds() {
    const AudioContract audio{{192000, 1}, 2, "mono", "float32le"};
    auto encoder = require_wav(make_wav_encoder(audio, {7}));
    ByteCollector output{7};
    const WavChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };

    expect(!encoder.begin(consume).has_value(), "Float32 WAVE header failed");
    const std::array samples{1.25F, -0.0F};
    expect(!encoder.write_float32_interleaved(samples, consume).has_value(),
           "finite non-normalized Float32 samples were rejected");
    expect(!encoder.finish(consume).has_value(), "Float32 WAVE finish failed");

    expect(output.valid && output.maximum_seen <= 7 && output.bytes.size() == 66 &&
               encoder.expected_byte_count() == 66 && encoder.finished(),
           "Float32 WAVE chunking or exact byte count is wrong");
    expect(
        has_fourcc(output.bytes, 0, {'R', 'I', 'F', 'F'}) &&
            read_u32(output.bytes, 4) == 58 &&
            has_fourcc(output.bytes, 8, {'W', 'A', 'V', 'E'}) &&
            has_fourcc(output.bytes, 12, {'f', 'm', 't', ' '}) &&
            read_u32(output.bytes, 16) == 18 && read_u16(output.bytes, 20) == 3 &&
            read_u16(output.bytes, 22) == 1 && read_u32(output.bytes, 24) == 192000 &&
            read_u32(output.bytes, 28) == 768000 && read_u16(output.bytes, 32) == 4 &&
            read_u16(output.bytes, 34) == 32 && read_u16(output.bytes, 36) == 0 &&
            has_fourcc(output.bytes, 38, {'f', 'a', 'c', 't'}) &&
            read_u32(output.bytes, 42) == 4 && read_u32(output.bytes, 46) == 2 &&
            has_fourcc(output.bytes, 50, {'d', 'a', 't', 'a'}) &&
            read_u32(output.bytes, 54) == 8,
        "Float32 WAVE header differs from the frozen 58-byte contract");
    expect(read_u32(output.bytes, 58) == std::bit_cast<std::uint32_t>(1.25F) &&
               read_u32(output.bytes, 62) == std::bit_cast<std::uint32_t>(-0.0F),
           "Float32 WAVE did not preserve exact sample bit patterns");

    const AudioContract bmw_audio{
        {192000, 1},
        2880000,
        "mono",
        "float32le",
    };
    const auto bmw = require_wav(make_wav_encoder(bmw_audio));
    expect(bmw.expected_byte_count() == 11520058,
           "BMW baseline Float32 stem size is not exactly 11,520,058 bytes");
}

void test_pcm24_wave_identity_and_fail_closed_input() {
    const AudioContract audio{{48000, 1}, 1, "mono", "pcm_s24le"};
    auto encoder = require_wav(make_wav_encoder(audio, {5}));
    ByteCollector output{5};
    const WavChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(), "PCM24 WAVE header failed");
    const std::array<std::int32_t, 1> samples{-8388608};
    expect(!encoder.write_pcm_s24_interleaved(samples, consume).has_value(),
           "valid minimum PCM24 code was rejected");
    expect(!encoder.finish(consume).has_value(), "PCM24 WAVE finish failed");

    expect(output.valid && output.bytes.size() == 48 &&
               encoder.expected_byte_count() == 48,
           "odd PCM24 data did not produce one exact RIFF pad byte");
    expect(read_u32(output.bytes, 4) == 40 && read_u32(output.bytes, 16) == 16 &&
               read_u16(output.bytes, 20) == 1 && read_u16(output.bytes, 34) == 24 &&
               has_fourcc(output.bytes, 36, {'d', 'a', 't', 'a'}) &&
               read_u32(output.bytes, 40) == 3 && output.bytes[44] == std::byte{0x00} &&
               output.bytes[45] == std::byte{0x00} &&
               output.bytes[46] == std::byte{0x80} &&
               output.bytes[47] == std::byte{0x00},
           "PCM24 WAVE bytes or RIFF padding are wrong");

    auto out_of_range = require_wav(make_wav_encoder(audio));
    ByteCollector rejected_bytes{16384};
    const WavChunkConsumer reject_consume = [&](auto offset, auto bytes) {
        return rejected_bytes.consume(offset, bytes);
    };
    expect(!out_of_range.begin(reject_consume).has_value(),
           "PCM24 rejection setup failed");
    const std::array<std::int32_t, 1> invalid{8388608};
    const auto before = rejected_bytes.bytes.size();
    expect_error(out_of_range.write_pcm_s24_interleaved(invalid, reject_consume),
                 WavEncodingErrorCode::sample_out_of_range,
                 "out-of-range PCM24 code did not fail explicitly");
    expect(rejected_bytes.bytes.size() == before && out_of_range.failed(),
           "PCM24 range failure emitted bytes or remained reusable");

    auto wrong_method = require_wav(make_wav_encoder(audio));
    ByteCollector wrong_method_bytes{16384};
    const WavChunkConsumer wrong_consume = [&](auto offset, auto bytes) {
        return wrong_method_bytes.consume(offset, bytes);
    };
    expect(!wrong_method.begin(wrong_consume).has_value(), "wrong-method setup failed");
    const std::array<float, 1> float_input{0.0F};
    expect_error(wrong_method.write_float32_interleaved(float_input, wrong_consume),
                 WavEncodingErrorCode::unsupported_encoding,
                 "PCM24 contract accepted a Float32 write method");
}

void test_wave_adversarial_contracts_and_callbacks() {
    const AudioContract float_audio{{48000, 1}, 1, "mono", "float32le"};
    auto non_finite = require_wav(make_wav_encoder(float_audio));
    ByteCollector bytes{16384};
    const WavChunkConsumer consume = [&](auto offset, auto chunk) {
        return bytes.consume(offset, chunk);
    };
    expect(!non_finite.begin(consume).has_value(), "NaN test header failed");
    const auto header_bytes = bytes.bytes.size();
    const std::array bad{std::numeric_limits<float>::quiet_NaN()};
    expect_error(non_finite.write_float32_interleaved(bad, consume),
                 WavEncodingErrorCode::non_finite_sample,
                 "non-finite Float32 sample was serialized");
    expect(bytes.bytes.size() == header_bytes,
           "non-finite Float32 failure emitted partial sample bytes");

    auto incomplete = require_wav(make_wav_encoder(float_audio));
    ByteCollector incomplete_bytes{16384};
    const WavChunkConsumer incomplete_consume = [&](auto offset, auto chunk) {
        return incomplete_bytes.consume(offset, chunk);
    };
    expect(!incomplete.begin(incomplete_consume).has_value(),
           "incomplete WAVE setup failed");
    expect_error(incomplete.finish(incomplete_consume),
                 WavEncodingErrorCode::frame_count_mismatch,
                 "incomplete WAVE stream was accepted");

    auto rejected = require_wav(make_wav_encoder(float_audio, {1}));
    std::size_t calls = 0;
    const WavChunkConsumer reject = [&](auto, auto) { return ++calls < 3; };
    expect_error(rejected.begin(reject), WavEncodingErrorCode::callback_rejected,
                 "consumer-rejected WAVE header was accepted");
    expect(rejected.failed(), "consumer rejection was not terminal");

    auto unsupported = float_audio;
    unsupported.sample_encoding_id = "pcm_s16le";
    expect(std::holds_alternative<WavEncodingError>(make_wav_encoder(unsupported)),
           "undeclared PCM16 serializer was silently admitted");
    unsupported = float_audio;
    unsupported.channel_layout_id = "stereo";
    const auto stereo = make_wav_encoder(unsupported);
    expect(std::holds_alternative<WavEncodingError>(stereo) &&
               std::get<WavEncodingError>(stereo).code ==
                   WavEncodingErrorCode::unsupported_channel_layout,
           "unresolvable channel-layout semantics were guessed");
    unsupported = float_audio;
    unsupported.sample_rate = {48000, 1001};
    expect(std::holds_alternative<WavEncodingError>(make_wav_encoder(unsupported)),
           "non-integral WAVE sample rate was silently rounded");
    unsupported = float_audio;
    unsupported.frame_count = std::numeric_limits<std::uint32_t>::max();
    expect(std::holds_alternative<WavEncodingError>(make_wav_encoder(unsupported)),
           "classic RIFF overflow was not rejected before publication");
}

struct CaptureFixture {
    static constexpr std::size_t kFrameCount = 4;

    std::array<CylinderId, 1> cylinders{CylinderId{1}};
    std::array<PortIdentity, 1> ports{
        PortIdentity{PortId{1}, CylinderId{1}, PortKind::exhaust},
    };
    std::array<GasVolumeIdentity, 2> volumes{
        GasVolumeIdentity{GasVolumeId{1}, GasVolumeKind::cylinder},
        GasVolumeIdentity{GasVolumeId{2}, GasVolumeKind::exhaust_primary},
    };
    std::array<FlowEdgeIdentity, 1> edges{
        FlowEdgeIdentity{FlowEdgeId{1}, GasVolumeId{1}, GasVolumeId{2}},
    };
    std::array<RouteIdentity, 2> routes{
        RouteIdentity{RouteId{1}, SourceRouteKind::exhaust_outlet, GasVolumeId{2},
                      std::nullopt, std::nullopt},
        RouteIdentity{RouteId{2}, SourceRouteKind::mechanical_engine, std::nullopt,
                      RouteId{1}, std::string{"engine.block"}},
    };

    std::array<EngineCaptureSample, kFrameCount> engine{};
    std::array<CylinderCaptureSample, kFrameCount> cylinder_samples{};
    std::array<PortCaptureSample, kFrameCount> port_samples{};
    std::array<GasVolumeCaptureSample, kFrameCount * 2> volume_samples{};
    std::array<FlowEdgeCaptureSample, kFrameCount> edge_samples{};
    std::array<SourceRouteCaptureSample, kFrameCount * 2> route_samples{};
    std::array<double, kFrameCount> filtered_rpm{};
    std::array<ReferenceParityCylinderSample, kFrameCount> parity_cylinders{};
    std::array<std::vector<EngineEvent>, kFrameCount> events_by_frame;

    CaptureFixture() {
        const auto mechanism = capture_validity_mask(CaptureValidity::mechanism);
        const auto thermodynamic =
            capture_validity_mask(CaptureValidity::thermodynamic_state);
        const auto composition = capture_validity_mask(CaptureValidity::composition);
        const auto gas_exchange = capture_validity_mask(CaptureValidity::gas_exchange);
        const auto torque = capture_validity_mask(CaptureValidity::torque);

        for (std::size_t frame = 0; frame < kFrameCount; ++frame) {
            auto &engine_sample = engine[frame];
            engine_sample.step_end_index = frame + 1;
            engine_sample.validity = mechanism | torque;
            engine_sample.theta_rad = 0.1 * static_cast<double>(frame + 1);
            engine_sample.theta_cycle_rad = engine_sample.theta_rad;
            engine_sample.angular_speed_rad_s = 100.0 + frame;
            engine_sample.angular_acceleration_rad_s2 =
                frame == 0 ? -0.0 : 0.25 * static_cast<double>(frame);
            engine_sample.engine_speed_rpm = 950.0 + frame;
            engine_sample.requested_throttle_01 = 0.5;
            engine_sample.resolved_engine_throttle_01 = 0.6;
            engine_sample.intake_plate_position_01 = 0.6;
            engine_sample.main_flow_multiplier_01 = 0.5;
            engine_sample.ignition_enabled = true;
            engine_sample.fuel_enabled = true;
            engine_sample.dyno_enabled = true;
            engine_sample.limiter_cut_active = frame == 1;
            engine_sample.torque.instantaneous_indicated_gas = {
                20.0 + frame,
                Availability::available,
                Completeness::complete,
                QuantityUnavailableReason::none,
                torque_term_mask(TorqueTerm::indicated_gas),
                0,
            };

            auto &cylinder = cylinder_samples[frame];
            cylinder.validity = mechanism | thermodynamic | composition | torque;
            cylinder.chamber_volume_m3 = 0.0005;
            cylinder.chamber_dvolume_dtheta_m3_per_rad = 0.00001;
            cylinder.piston_velocity_m_s = 2.0;
            cylinder.pressure_pa_abs = 101325.0 + frame;
            cylinder.temperature_k = 450.0;
            cylinder.amount_mol = 0.01;
            cylinder.composition = {0.05, 0.74, 0.21};
            cylinder.combustion_heat_release_j = 2.0;
            cylinder.flame_radius_m = 0.001;
            cylinder.flame_axial_travel_m = 0.002;
            cylinder.indicated_gas_torque = {
                20.0 + frame,
                Availability::available,
                Completeness::complete,
                QuantityUnavailableReason::none,
                torque_term_mask(TorqueTerm::indicated_gas),
                0,
            };

            auto &port = port_samples[frame];
            port.validity = gas_exchange;
            port.pressure_pa_abs = 101325.0 + frame;
            port.temperature_k = 500.0;
            port.signed_mass_flow_kg_s = -0.01;
            port.effective_flow_area_m2 = 0.0001;
            port.effective_molar_flow_conductance_m2_sqrt_mol_per_kg =
                0.00002748668227937587;
            port.valve_lift_m = 0.001;

            for (std::size_t volume = 0; volume < 2; ++volume) {
                auto &sample = volume_samples[frame * 2 + volume];
                sample.validity = thermodynamic | composition;
                sample.volume_m3 = 0.001 + volume * 0.0001;
                sample.pressure_pa_abs = 101325.0 + frame * 2.0 + volume;
                sample.temperature_k = 400.0 + volume;
                sample.amount_mol = 0.02;
                sample.thermal_energy_j = 100.0;
                sample.momentum_x_kg_m_s = 0.1 * frame;
                sample.momentum_y_kg_m_s = -0.1 * frame;
                sample.composition = {0.05, 0.74, 0.21};
            }

            edge_samples[frame] = {gas_exchange, -0.01};
            route_samples[frame * 2] = GasSourceRouteCaptureSample{
                gas_exchange, 101325.0 + frame, 500.0, 0.01, 0.001,
            };
            route_samples[frame * 2 + 1] = MechanicalSourceRouteCaptureSample{
                mechanism,
                {1.0 + frame, 2.0, 3.0},
                {4.0, 5.0, 6.0 + frame},
            };
            filtered_rpm[frame] = 900.0 + frame;
            parity_cylinders[frame] = {
                101325.0 + frame,
                10.0 + frame,
                5.0 + frame,
            };
        }

        events_by_frame[0] = {
            EngineEvent{0, 0, SparkCrossing{CylinderId{1}, 0.0, 0.1, 0.1, 0.2, 0.1}},
        };
        events_by_frame[1] = {
            EngineEvent{1, 0, LimiterStateChanged{false, true, true, 0.5}},
        };
        events_by_frame[2] = {
            EngineEvent{2, 0, IgnitionAccepted{CylinderId{1}, 0.8, 1.2}},
            EngineEvent{
                2, 1, IgnitionRejected{CylinderId{1}, IgnitionRejection::active_flame}},
        };
        events_by_frame[3] = {
            EngineEvent{
                3, 0,
                FlameExtinguished{CylinderId{1}, 2,
                                  FlameExtinctionReason::no_geometric_progress}},
        };
    }

    CaptureLayoutView layout() const {
        return CaptureLayoutView::borrow_for_callback(EngineId{1}, cylinders, ports,
                                                      volumes, edges, routes);
    }
};

TelemetryEncodingStatus
write_fixture_block(TelemetryEncoder &encoder, const CaptureFixture &fixture,
                    std::uint32_t first_frame, std::uint32_t frame_count,
                    bool with_parity, const TelemetryChunkConsumer &consumer,
                    std::optional<CaptureClock> clock_override = std::nullopt) {
    const auto layout = fixture.layout();
    auto engine = std::span{fixture.engine}.subspan(first_frame, frame_count);
    auto cylinders =
        std::span{fixture.cylinder_samples}.subspan(first_frame, frame_count);
    auto ports = std::span{fixture.port_samples}.subspan(first_frame, frame_count);
    auto volumes =
        std::span{fixture.volume_samples}.subspan(first_frame * 2, frame_count * 2);
    auto edges = std::span{fixture.edge_samples}.subspan(first_frame, frame_count);
    auto routes =
        std::span{fixture.route_samples}.subspan(first_frame * 2, frame_count * 2);

    std::vector<std::uint32_t> offsets(frame_count + 1, 0);
    std::vector<EngineEvent> events;
    if (!with_parity) {
        for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
            for (auto event : fixture.events_by_frame[first_frame + frame]) {
                event.frame_offset = frame;
                events.push_back(std::move(event));
            }
            offsets[frame + 1] = static_cast<std::uint32_t>(events.size());
        }
    }
    const auto journal = EventJournalView::borrow_for_callback(offsets, events);

    std::optional<ReferenceParityBlockView> parity;
    auto filtered = std::span{fixture.filtered_rpm}.subspan(first_frame, frame_count);
    auto parity_cylinders =
        std::span{fixture.parity_cylinders}.subspan(first_frame, frame_count);
    if (with_parity) {
        parity =
            ReferenceParityBlockView::borrow_for_callback(filtered, parity_cylinders);
    }

    const CaptureClock expected_clock{
        {10000, 1},
        first_frame,
        first_frame + (with_parity ? 1U : 0U),
        with_parity ? SamplePhase::post_step : SamplePhase::pre_step,
    };
    const auto clock = clock_override.value_or(expected_clock);
    const auto block = CaptureBlockView::borrow_for_callback(
        layout, clock, frame_count, 4, 16, engine, cylinders, ports, volumes, edges,
        routes, journal, parity);
    return encoder.write_block(block, consumer);
}

std::vector<std::byte> encode_fixture(const CaptureFixture &fixture,
                                      std::span<const std::uint32_t> parts,
                                      std::size_t maximum_chunk_bytes,
                                      bool with_parity = false) {
    const TelemetryStreamDescriptor descriptor{
        CaptureClock{
            {10000, 1},
            0,
            with_parity ? 1U : 0U,
            with_parity ? SamplePhase::post_step : SamplePhase::pre_step,
        },
        CaptureFixture::kFrameCount,
        with_parity,
        maximum_chunk_bytes,
    };
    auto encoder =
        require_telemetry(make_telemetry_encoder(fixture.layout(), descriptor));
    ByteCollector output{maximum_chunk_bytes};
    const TelemetryChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "telemetry header serialization failed");

    std::uint32_t first = 0;
    for (const auto part : parts) {
        expect(!write_fixture_block(encoder, fixture, first, part, with_parity, consume)
                    .has_value(),
               "valid CaptureBlock telemetry serialization failed");
        first += part;
    }
    expect(first == CaptureFixture::kFrameCount,
           "test partition did not cover fixture");
    expect(!encoder.finish(consume).has_value(),
           "telemetry footer serialization failed");
    expect(output.valid && output.maximum_seen <= maximum_chunk_bytes &&
               encoder.frames_written() == CaptureFixture::kFrameCount &&
               encoder.bytes_emitted() == output.bytes.size() && encoder.finished(),
           "telemetry chunk bounds or terminal accounting is wrong");
    return output.bytes;
}

void test_telemetry_complete_partition_invariant_stream() {
    const CaptureFixture fixture;
    const std::array one_block{std::uint32_t{4}};
    const std::array split_blocks{
        std::uint32_t{1},
        std::uint32_t{1},
        std::uint32_t{2},
    };
    const auto contiguous =
        encode_fixture(fixture, one_block, kMaximumTelemetryEncoderChunkBytes);
    const auto partitioned = encode_fixture(fixture, split_blocks, 7);
    expect(contiguous == partitioned,
           "telemetry bytes changed with valid CaptureBlock/chunk partitioning");

    // These identities freeze the complete v1 wire order, enum/variant tags, and
    // field set. An intentional byte change requires a schema-version bump before
    // these pins may be updated.
    const Sha256Digest expected_generic{{
        0x61, 0x85, 0x80, 0x66, 0x09, 0x46, 0xb5, 0x5d, 0x48, 0x67, 0x84,
        0x4f, 0xef, 0x3b, 0x80, 0x52, 0x8b, 0x5d, 0xb0, 0xae, 0x67, 0x53,
        0xee, 0x57, 0x81, 0xff, 0xba, 0x51, 0x06, 0x24, 0x4f, 0xaf,
    }};
    expect(contiguous.size() == 3563 && sha256(contiguous) == expected_generic,
           "canonical generic telemetry v1 bytes changed without a schema bump");

    expect(contiguous.size() > 100 && has_fourcc(contiguous, 0, {'E', 'S', 'O', 'T'}) &&
               read_u32(contiguous, 8) == kCaptureTelemetrySchemaVersion &&
               read_u32(contiguous, 12) == 0 && read_u64(contiguous, 16) == 10000 &&
               read_u64(contiguous, 24) == 1 &&
               has_fourcc(contiguous, contiguous.size() - 12, {'E', 'N', 'D', '1'}) &&
               read_u64(contiguous, contiguous.size() - 8) ==
                   CaptureFixture::kFrameCount,
           "telemetry v1 header/footer identity is wrong");

    auto torque_changed = fixture;
    torque_changed.engine[0].torque.instantaneous_indicated_gas.value_nm += 1.0;
    expect(encode_fixture(torque_changed, one_block, 4096) != contiguous,
           "torque value was omitted from telemetry");
    auto gas_route_changed = fixture;
    std::get<GasSourceRouteCaptureSample>(gas_route_changed.route_samples[0])
        .pressure_pa_abs += 1.0;
    expect(encode_fixture(gas_route_changed, one_block, 4096) != contiguous,
           "gas source-route payload was omitted from telemetry");
    auto mechanical_changed = fixture;
    std::get<MechanicalSourceRouteCaptureSample>(mechanical_changed.route_samples[1])
        .force_xyz_n[0] += 1.0;
    expect(encode_fixture(mechanical_changed, one_block, 4096) != contiguous,
           "mechanical source-route payload was omitted from telemetry");
    auto event_changed = fixture;
    std::get<SparkCrossing>(event_changed.events_by_frame[0][0].payload)
        .timing_advance_rad += 0.01;
    expect(encode_fixture(event_changed, one_block, 4096) != contiguous,
           "event payload was omitted from telemetry");

    auto parity_fixture = fixture;
    for (auto &events : parity_fixture.events_by_frame) {
        events.clear();
    }
    const auto parity = encode_fixture(parity_fixture, one_block, 11, true);
    const Sha256Digest expected_parity{{
        0x05, 0x63, 0x40, 0xf4, 0xb7, 0xc8, 0xfd, 0x8c, 0x0f, 0xa0, 0x27,
        0x89, 0xd4, 0x5f, 0x25, 0x00, 0xa4, 0x52, 0xfe, 0x15, 0x83, 0x19,
        0xc3, 0xd1, 0xae, 0xf4, 0xc3, 0x6c, 0x99, 0xa1, 0x69, 0x10,
    }};
    expect(parity.size() == 3595 && sha256(parity) == expected_parity,
           "canonical parity telemetry v1 bytes changed without a schema bump");
    expect(read_u32(parity, 12) == 1 && parity != contiguous,
           "reference-parity schema flag or payload was omitted");
    parity_fixture.filtered_rpm[0] += 1.0;
    expect(encode_fixture(parity_fixture, one_block, 4096, true) != parity,
           "reference-parity value was omitted from telemetry");
}

TelemetryEncoder make_encoder_after_layout_owner_destruction() {
    CaptureFixture local;
    const TelemetryStreamDescriptor descriptor{
        CaptureClock{{10000, 1}, 0, 0, SamplePhase::pre_step},
        CaptureFixture::kFrameCount,
        false,
        4096,
    };
    return require_telemetry(make_telemetry_encoder(local.layout(), descriptor));
}

void test_telemetry_borrowing_and_adversarial_failures() {
    const CaptureFixture fixture;
    auto encoder = make_encoder_after_layout_owner_destruction();
    ByteCollector output{4096};
    const TelemetryChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "owned-layout telemetry header failed after source destruction");
    expect(!write_fixture_block(encoder, fixture, 0, 4, false, consume).has_value() &&
               !encoder.finish(consume).has_value(),
           "telemetry encoder retained its factory's borrowed layout view");

    const TelemetryStreamDescriptor descriptor{
        CaptureClock{{10000, 1}, 0, 0, SamplePhase::pre_step},
        CaptureFixture::kFrameCount,
        false,
        4096,
    };
    auto discontinuous =
        require_telemetry(make_telemetry_encoder(fixture.layout(), descriptor));
    ByteCollector discontinuous_bytes{4096};
    const TelemetryChunkConsumer discontinuous_consume = [&](auto offset, auto bytes) {
        return discontinuous_bytes.consume(offset, bytes);
    };
    expect(!discontinuous.begin(discontinuous_consume).has_value(),
           "discontinuity setup failed");
    expect(
        !write_fixture_block(discontinuous, fixture, 0, 1, false, discontinuous_consume)
             .has_value(),
        "first telemetry block failed");
    const CaptureClock skipped{{10000, 1}, 2, 2, SamplePhase::pre_step};
    const auto before = discontinuous_bytes.bytes.size();
    expect_error(write_fixture_block(discontinuous, fixture, 1, 1, false,
                                     discontinuous_consume, skipped),
                 TelemetryEncodingErrorCode::discontinuous_clock,
                 "gap in global telemetry sample indices was accepted");
    expect(discontinuous_bytes.bytes.size() == before && discontinuous.failed(),
           "clock discontinuity emitted bytes or remained reusable");

    auto layout_changed_fixture = fixture;
    layout_changed_fixture.routes[1].emitter_anchor_id = "engine.other";
    auto layout_changed =
        require_telemetry(make_telemetry_encoder(fixture.layout(), descriptor));
    ByteCollector layout_bytes{4096};
    const TelemetryChunkConsumer layout_consume = [&](auto offset, auto bytes) {
        return layout_bytes.consume(offset, bytes);
    };
    expect(!layout_changed.begin(layout_consume).has_value(),
           "layout mismatch setup failed");
    expect_error(write_fixture_block(layout_changed, layout_changed_fixture, 0, 4,
                                     false, layout_consume),
                 TelemetryEncodingErrorCode::inconsistent_layout,
                 "mid-stream telemetry topology change was accepted");

    const TelemetryStreamDescriptor parity_flag_descriptor{
        CaptureClock{{10000, 1}, 0, 1, SamplePhase::post_step},
        CaptureFixture::kFrameCount,
        false,
        4096,
    };
    auto wrong_parity = require_telemetry(
        make_telemetry_encoder(fixture.layout(), parity_flag_descriptor));
    ByteCollector parity_bytes{4096};
    const TelemetryChunkConsumer parity_consume = [&](auto offset, auto bytes) {
        return parity_bytes.consume(offset, bytes);
    };
    expect(!wrong_parity.begin(parity_consume).has_value(),
           "parity mismatch setup failed");
    expect_error(write_fixture_block(wrong_parity, fixture, 0, 4, true, parity_consume),
                 TelemetryEncodingErrorCode::reference_parity_mismatch,
                 "undeclared reference-parity payload did not fail closed");

    auto incomplete =
        require_telemetry(make_telemetry_encoder(fixture.layout(), descriptor));
    ByteCollector incomplete_bytes{4096};
    const TelemetryChunkConsumer incomplete_consume = [&](auto offset, auto bytes) {
        return incomplete_bytes.consume(offset, bytes);
    };
    expect(!incomplete.begin(incomplete_consume).has_value(),
           "incomplete telemetry setup failed");
    expect(!write_fixture_block(incomplete, fixture, 0, 1, false, incomplete_consume)
                .has_value(),
           "incomplete telemetry first block failed");
    expect_error(incomplete.finish(incomplete_consume),
                 TelemetryEncodingErrorCode::frame_count_mismatch,
                 "incomplete telemetry stream was accepted");

    auto rejected = require_telemetry(
        make_telemetry_encoder(fixture.layout(), TelemetryStreamDescriptor{
                                                     descriptor.first_clock,
                                                     descriptor.frame_count,
                                                     false,
                                                     1,
                                                 }));
    std::size_t calls = 0;
    const TelemetryChunkConsumer reject = [&](auto, auto) { return ++calls < 3; };
    expect_error(rejected.begin(reject), TelemetryEncodingErrorCode::callback_rejected,
                 "telemetry consumer rejection was accepted");

    std::size_t overflow_callbacks = 0;
    const TelemetryChunkConsumer overflow_consume = [&](auto, auto) {
        ++overflow_callbacks;
        return true;
    };
    artifacts::detail::TelemetryByteEmitter overflow{
        4, std::numeric_limits<std::uint64_t>::max() - 1, overflow_consume};
    expect(!overflow.append_u32(0x01020304) && overflow.offset_overflowed() &&
               overflow_callbacks == 0,
           "telemetry byte offset wrapped or invoked a discontinuous callback");
}

} // namespace

int main() {
    try {
        test_float32_wave_identity_and_bounds();
        test_pcm24_wave_identity_and_fail_closed_input();
        test_wave_adversarial_contracts_and_callbacks();
        test_telemetry_complete_partition_invariant_stream();
        test_telemetry_borrowing_and_adversarial_failures();
    } catch (const std::exception &error) {
        std::cerr << "artifact serialization test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
