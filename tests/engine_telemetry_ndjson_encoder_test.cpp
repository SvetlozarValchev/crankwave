#include "crankwave/artifacts/engine_telemetry_ndjson_encoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::artifacts;

// These exact-arity structured bindings deliberately make a public telemetry field
// addition a compile failure until schema v1 is consciously revised or versioned.
void freeze_v1_public_field_inventory() {
    EngineTelemetryFrame frame;
    [[maybe_unused]] auto &[frame_step, frame_map, frame_engine, frame_dyno,
                            frame_vehicle] = frame;

    contract::EngineCaptureSample engine;
    [[maybe_unused]] auto &[engine_step, engine_validity, engine_theta,
                            engine_theta_cycle, engine_speed_rad_s, engine_acceleration,
                            engine_rpm, engine_requested_throttle,
                            engine_resolved_throttle, engine_intake_plate,
                            engine_flow_multiplier, engine_ignition, engine_fuel,
                            engine_starter, engine_dyno, engine_limiter,
                            engine_limiter_cut, engine_resisting_torque,
                            engine_torque] = engine;

    contract::TorqueTelemetry torque;
    [[maybe_unused]] auto &[torque_indicated, torque_pumping, torque_friction,
                            torque_starter, torque_net, torque_cycle_net,
                            torque_actuator, torque_dyno, torque_work, torque_bmep,
                            torque_power, torque_cycle_power] = torque;

    contract::TorqueValueNm torque_value;
    [[maybe_unused]] auto &[torque_number, torque_availability, torque_completeness,
                            torque_reason, torque_included, torque_omitted] =
        torque_value;

    contract::QuantityValue quantity;
    [[maybe_unused]] auto &[quantity_number, quantity_availability,
                            quantity_completeness, quantity_reason] = quantity;

    EngineHeldDynoTelemetry held_dyno;
    [[maybe_unused]] auto &[dyno_target, dyno_absorbing, dyno_driving, dyno_required,
                            dyno_applied, dyno_disposition] = held_dyno;

    EngineFreeVehicleTelemetry free_vehicle;
    [[maybe_unused]] auto &[vehicle_speed, vehicle_distance, vehicle_gear,
                            vehicle_clutch, vehicle_brake, vehicle_clutch_disposition,
                            vehicle_clutch_capacity, vehicle_clutch_torque,
                            vehicle_clutch_slip, vehicle_road_disposition,
                            vehicle_road_requested, vehicle_road_applied] =
        free_vehicle;

    EngineCycleBoundaryEvidence boundary;
    [[maybe_unused]] auto &[boundary_ordinal, boundary_left, boundary_right,
                            boundary_fraction, boundary_theta, boundary_time,
                            boundary_delivery] = boundary;

    EngineCycleControlEvidence control;
    [[maybe_unused]] auto &[control_mean, control_minimum, control_maximum,
                            control_changes] = control;

    EngineCycleNetShaftEvidence cycle_torque;
    [[maybe_unused]] auto &[cycle_work, cycle_mean_torque, cycle_availability,
                            cycle_completeness, cycle_reason, cycle_included,
                            cycle_omitted] = cycle_torque;

    EngineCompletedCycleEvidence cycle_evidence;
    [[maybe_unused]] auto &[cycle_ordinal, cycle_start, cycle_end, cycle_duration,
                            cycle_rpm, cycle_requested, cycle_resolved, cycle_intake,
                            cycle_net, cycle_start_state, cycle_end_state,
                            cycle_transitions] = cycle_evidence;

    EngineEventCounters events;
    [[maybe_unused]] auto &[events_total, events_spark, events_limiter,
                            events_limiter_activation, events_limiter_release,
                            events_limiter_refresh, events_ignition_accepted,
                            events_rejected_active, events_rejected_no_fuel,
                            events_rejected_low, events_rejected_high,
                            events_extinguished_intake,
                            events_extinguished_no_progress] = events;
}

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EngineTelemetryNdjsonEncoder
require_encoder(EngineTelemetryNdjsonEncoderResult result) {
    if (const auto *failure =
            std::get_if<EngineTelemetryNdjsonEncodingError>(&result)) {
        throw std::runtime_error("valid telemetry descriptor was rejected: " +
                                 failure->path + ": " + failure->message);
    }
    return std::get<EngineTelemetryNdjsonEncoder>(std::move(result));
}

struct Collector {
    explicit Collector(std::size_t maximum_chunk) : maximum_chunk(maximum_chunk) {}

    bool consume(std::uint64_t offset, std::span<const std::byte> chunk) {
        if (offset != bytes.size() || chunk.empty() || chunk.size() > maximum_chunk) {
            valid = false;
            return false;
        }
        largest_chunk = std::max(largest_chunk, chunk.size());
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return true;
    }

    std::size_t maximum_chunk;
    std::size_t largest_chunk = 0;
    bool valid = true;
    std::vector<std::byte> bytes;
};

EngineTelemetryNdjsonStreamDescriptor descriptor() {
    EngineTelemetryNdjsonStreamDescriptor result;
    for (std::size_t index = 0;
         index < result.simulation_request_identity_v7_sha256.bytes.size(); ++index) {
        result.simulation_request_identity_v7_sha256.bytes[index] =
            static_cast<std::uint8_t>(index + 1U);
    }
    result.engine_id = "diagnostic-engine";
    result.scenario_id = "fixed-probe-v1";
    result.motion_mode = EngineMotionMode::free_vehicle;
    result.physics_rate = {20000U, 1U};
    result.delivery_rate = {192000U, 1U};
    result.physics_frames_per_block = 400U;
    result.delivery_frames_per_block = 3840U;
    result.preparation_block_count = 1U;
    result.total_block_count = 2U;
    result.maximum_chunk_bytes = 17U;
    return result;
}

EngineTelemetryFrame endpoint(std::uint64_t physics_end) {
    EngineTelemetryFrame result;
    result.physics_step_end = physics_end;
    result.mean_intake_manifold_pressure_pa_abs = 101325.25;
    result.engine.step_end_index = physics_end;
    result.engine.validity =
        contract::capture_validity_mask(contract::CaptureValidity::mechanism) |
        contract::capture_validity_mask(contract::CaptureValidity::torque);
    result.engine.theta_rad = -0.0;
    result.engine.theta_cycle_rad = 1.25;
    result.engine.angular_speed_rad_s = 125.5;
    result.engine.angular_acceleration_rad_s2 = -3.25;
    result.engine.engine_speed_rpm = 1198.75;
    result.engine.requested_throttle_01 = 0.25;
    result.engine.resolved_engine_throttle_01 = 0.2;
    result.engine.intake_plate_position_01 = 0.1875;
    result.engine.main_flow_multiplier_01 = 0.75;
    result.engine.ignition_enabled = true;
    result.engine.fuel_enabled = true;
    result.engine.limiter_enabled = true;
    result.engine.requested_external_resisting_torque_nm = 12.5;
    result.engine.torque.instantaneous_net_shaft.value_nm = 88.125;
    result.engine.torque.instantaneous_net_shaft.availability =
        contract::Availability::available;
    result.engine.torque.instantaneous_net_shaft.completeness =
        contract::Completeness::complete;
    result.engine.torque.instantaneous_net_shaft.unavailable_reason =
        contract::QuantityUnavailableReason::none;
    result.engine.torque.instantaneous_net_shaft.included_terms =
        contract::known_torque_term_mask();
    return result;
}

EngineTelemetryFrame held_dyno_endpoint(std::uint64_t physics_end) {
    auto frame = endpoint(physics_end);
    frame.held_dyno = EngineHeldDynoTelemetry{
        1200.0, 300.0, 40.0,
        -10.0,  -9.5,  EngineHeldDynoDisposition::absorbing_torque_limited,
    };
    frame.engine.torque.actuator.value_nm = -9.5;
    frame.engine.torque.actuator.availability = contract::Availability::available;
    frame.engine.torque.actuator.completeness = contract::Completeness::complete;
    frame.engine.torque.actuator.unavailable_reason =
        contract::QuantityUnavailableReason::none;
    frame.engine.torque.dyno_reaction.value_nm = 9.5;
    frame.engine.torque.dyno_reaction.availability = contract::Availability::available;
    frame.engine.torque.dyno_reaction.completeness = contract::Completeness::complete;
    frame.engine.torque.dyno_reaction.unavailable_reason =
        contract::QuantityUnavailableReason::none;
    return frame;
}

EngineCompletedCycleEvidence cycle() {
    EngineCompletedCycleEvidence result;
    result.completed_cycle_ordinal = 0U;
    result.start_boundary = {
        7, 399U, 400U, 0.25, 100.0, 0.02, 3840.25,
    };
    result.end_boundary = {
        8, 798U, 799U, 0.75, 112.56637061435917, 0.04, 7679.75,
    };
    result.duration_s = 0.02;
    result.mean_engine_speed_rpm = 6000.0;
    result.requested_throttle = {0.25, 0.2, 0.3, 2U};
    result.resolved_engine_throttle = {0.2, 0.15, 0.25, 1U};
    result.intake_plate_position = {0.1875, 0.125, 0.25, 3U};
    result.instantaneous_net_shaft.angular_work_j = 125.0;
    result.instantaneous_net_shaft.cycle_mean_torque_nm = 9.947183943243459;
    result.instantaneous_net_shaft.availability = contract::Availability::available;
    result.instantaneous_net_shaft.completeness = contract::Completeness::incomplete;
    result.instantaneous_net_shaft.unavailable_reason =
        contract::QuantityUnavailableReason::none;
    result.instantaneous_net_shaft.included_terms =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas);
    result.instantaneous_net_shaft.omitted_terms =
        contract::torque_term_mask(contract::TorqueTerm::accessory);
    result.start_state_flags =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled);
    result.end_state_flags =
        result.start_state_flags |
        engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
    result.state_transition_flags =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
    return result;
}

EngineEventCounters comprehensive_events() {
    EngineEventCounters result;
    result.total_event_record_count = 9U;
    result.spark_crossing_count = 1U;
    result.limiter_transition_count = 1U;
    result.limiter_activation_count = 1U;
    result.limiter_transition_overspeed_refreshed_count = 1U;
    result.ignition_accepted_count = 1U;
    result.ignition_rejected_active_flame_count = 1U;
    result.ignition_rejected_no_fuel_count = 1U;
    result.ignition_rejected_mixture_low_count = 1U;
    result.ignition_rejected_mixture_high_count = 1U;
    result.flame_extinguished_intake_transfer_count = 1U;
    result.flame_extinguished_no_geometric_progress_count = 1U;
    return result;
}

std::vector<std::byte> encode_complete_stream() {
    auto encoder = require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(), "header encoding failed");

    auto preparation_endpoint = endpoint(400U);
    const std::array preparation_telemetry{preparation_endpoint};
    const std::array<EngineCompletedCycleEvidence, 0> no_cycles{};
    const EngineTelemetryNdjsonBlockInput preparation{
        0U,
        EngineSessionBlockPhase::preparation,
        0U,
        400U,
        0U,
        3840U,
        preparation_telemetry,
        no_cycles,
        comprehensive_events(),
    };
    expect(!encoder.write_block(preparation, consume).has_value(),
           "preparation block encoding failed");

    auto audible_endpoint = endpoint(800U);
    audible_endpoint.free_vehicle = EngineFreeVehicleTelemetry{
        12.25, 55.5,
        2U,    0.75,
        0.125, EngineClutchDisposition::tracking,
        350.0, -82.5,
        4.25,  EngineRoadLoadDisposition::moving,
        125.0, 120.0,
    };
    const std::array audible_telemetry{audible_endpoint};
    const std::array cycles{cycle()};
    EngineEventCounters release;
    release.total_event_record_count = 1U;
    release.limiter_transition_count = 1U;
    release.limiter_release_count = 1U;
    const EngineTelemetryNdjsonBlockInput audible{
        1U,
        EngineSessionBlockPhase::audible,
        400U,
        400U,
        3840U,
        3840U,
        audible_telemetry,
        cycles,
        release,
    };
    expect(!encoder.write_block(audible, consume).has_value(),
           "audible block encoding failed");
    expect(!encoder.finish(consume).has_value(), "footer encoding failed");
    expect(output.valid && output.largest_chunk == 17U && encoder.finished() &&
               encoder.blocks_written() == 2U && encoder.cycles_written() == 1U &&
               encoder.event_totals().total_event_record_count == 10U &&
               encoder.bytes_emitted() == output.bytes.size(),
           "stream counters or chunk bounds changed");
    return output.bytes;
}

std::vector<std::byte> encode_held_dyno_stream() {
    auto held_descriptor = descriptor();
    held_descriptor.motion_mode = EngineMotionMode::held_dyno;
    held_descriptor.preparation_block_count = 0U;
    held_descriptor.total_block_count = 1U;
    auto encoder = require_encoder(
        make_engine_telemetry_ndjson_encoder(std::move(held_descriptor)));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(), "held-dyno header encoding failed");

    auto frame = held_dyno_endpoint(400U);
    const std::array telemetry{frame};
    const std::array<EngineCompletedCycleEvidence, 0> cycles{};
    const EngineTelemetryNdjsonBlockInput block{
        0U, EngineSessionBlockPhase::audible, 0U, 400U, 0U, 3840U, telemetry, cycles,
        {},
    };
    expect(!encoder.write_block(block, consume).has_value(),
           "held-dyno block encoding failed");
    expect(!encoder.finish(consume).has_value(), "held-dyno footer encoding failed");
    return output.bytes;
}

std::string text(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

void test_canonical_complete_stream() {
    const auto first = encode_complete_stream();
    const auto second = encode_complete_stream();
    const contract::Sha256Digest expected_golden_sha256{{
        0x6aU, 0x80U, 0x2cU, 0x30U, 0xf1U, 0xfaU, 0x27U, 0xb5U, 0xe3U, 0xfbU, 0xdbU,
        0x38U, 0xa6U, 0x8cU, 0x07U, 0x71U, 0x22U, 0x7cU, 0xbcU, 0x23U, 0x42U, 0x33U,
        0xc9U, 0x6bU, 0xb1U, 0x2cU, 0xb2U, 0xfeU, 0x38U, 0xf4U, 0xd6U, 0xa0U,
    }};
    const auto actual_golden_sha256 = contract::sha256(first);
    if (actual_golden_sha256 != expected_golden_sha256) {
        constexpr std::string_view digits = "0123456789abcdef";
        std::cerr << "engine telemetry NDJSON SHA-256: ";
        for (const auto byte : actual_golden_sha256.bytes) {
            std::cerr << digits[byte >> 4U] << digits[byte & UINT8_C(0x0f)];
        }
        std::cerr << '\n';
    }
    expect(first == second && actual_golden_sha256 == expected_golden_sha256 &&
               contract::sha256(second) == expected_golden_sha256,
           "identical inputs did not produce identical bytes and SHA-256");
    const auto encoded = text(first);
    expect(
        encoded.starts_with(
            "{\"record_type\":\"header\",\"schema\":\"crankwave."
            "engine-telemetry.ndjson.v1\",\"schema_version\":1,") &&
            encoded.find("\"motion_mode\":\"free_vehicle\"") != std::string::npos &&
            encoded.find("\"audition_frame_range\":null,") != std::string::npos &&
            encoded.find(
                "\"audition_frame_range\":{\"begin\":\"0\",\"end\":\"3840\"}") !=
                std::string::npos &&
            encoded.find("\"endpoint_scenario_time_s\":0,") != std::string::npos &&
            encoded.find("\"theta_rad\":0,") != std::string::npos &&
            encoded.find("\"included_terms_mask\":\"0x0000000000000001\"") !=
                std::string::npos &&
            encoded.find("\"unavailable_reason\":\"model_not_admitted\"") !=
                std::string::npos &&
            encoded.find("\"held_dyno\":null") != std::string::npos &&
            encoded.find("\"free_vehicle\":{") != std::string::npos &&
            encoded.find("\"selected_forward_gear_ordinal\":2") != std::string::npos &&
            encoded.find("\"final_clutch_slip_rad_s\":4.25") != std::string::npos &&
            encoded.find(
                "\n{\"record_type\":\"cycle\",\"emitting_block_ordinal\":\"1\"") !=
                std::string::npos &&
            encoded.ends_with("\"final_audition_frame_end\":\"3840\"}\n"),
        "canonical key order, numeric normalization, sidecars, or alignment changed");
    expect(static_cast<std::size_t>(std::count(encoded.begin(), encoded.end(), '\n')) ==
               5U,
           "record ordering or final-newline contract changed");
}

void test_held_dyno_sidecar_golden() {
    const auto first = encode_held_dyno_stream();
    const auto second = encode_held_dyno_stream();
    // Filled from the canonical bytes below; this is intentionally a separate
    // mode-consistent golden from the FreeVehicle carrier above.
    const contract::Sha256Digest expected_golden_sha256{{
        0x7fU, 0x4dU, 0x34U, 0x3aU, 0xacU, 0xa3U, 0x9dU, 0xb1U, 0x17U, 0xf5U, 0xddU,
        0x76U, 0xd7U, 0xf7U, 0x8fU, 0x02U, 0xe0U, 0xa9U, 0x61U, 0xa2U, 0xc8U, 0xc4U,
        0xdfU, 0x75U, 0xbbU, 0x16U, 0x3bU, 0xf9U, 0x76U, 0xadU, 0x74U, 0xc3U,
    }};
    const auto actual_golden_sha256 = contract::sha256(first);
    if (actual_golden_sha256 != expected_golden_sha256) {
        constexpr std::string_view digits = "0123456789abcdef";
        std::cerr << "held-dyno telemetry NDJSON SHA-256: ";
        for (const auto byte : actual_golden_sha256.bytes) {
            std::cerr << digits[byte >> 4U] << digits[byte & UINT8_C(0x0f)];
        }
        std::cerr << '\n';
    }
    expect(first == second && actual_golden_sha256 == expected_golden_sha256 &&
               contract::sha256(second) == expected_golden_sha256,
           "held-dyno sidecar bytes or SHA-256 changed");
    const auto encoded = text(first);
    expect(encoded.find("\"motion_mode\":\"held_dyno\"") != std::string::npos &&
               encoded.find("\"held_dyno\":{") != std::string::npos &&
               encoded.find("\"disposition\":\"absorbing_torque_limited\"") !=
                   std::string::npos &&
               encoded.find("\"free_vehicle\":null") != std::string::npos,
           "held-dyno optional sidecar did not round-trip losslessly");
}

void test_cycle_control_roundoff_tolerance() {
    auto one_block = descriptor();
    one_block.motion_mode = EngineMotionMode::held_speed;
    one_block.preparation_block_count = 0U;
    one_block.total_block_count = 1U;
    auto encoder = require_encoder(make_engine_telemetry_ndjson_encoder(one_block));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(), "roundoff test header failed");

    const auto frame = endpoint(400U);
    const std::array telemetry{frame};
    auto rounded_cycle = cycle();
    rounded_cycle.start_boundary.left_physics_frame = 0U;
    rounded_cycle.start_boundary.right_physics_frame = 1U;
    rounded_cycle.end_boundary.left_physics_frame = 398U;
    rounded_cycle.end_boundary.right_physics_frame = 399U;
    rounded_cycle.requested_throttle.time_weighted_mean_01 =
        rounded_cycle.requested_throttle.maximum_01 + 5.0e-13;
    const std::array cycles{rounded_cycle};
    const EngineTelemetryNdjsonBlockInput block{
        0U, EngineSessionBlockPhase::audible, 0U, 400U, 0U, 3840U, telemetry, cycles,
        {},
    };
    expect(!encoder.write_block(block, consume).has_value() &&
               !encoder.finish(consume).has_value(),
           "sub-picounit cycle-control accumulation roundoff was rejected");
    expect(text(output.bytes).find("\"time_weighted_mean_01\":0.3000000000005") !=
               std::string::npos,
           "accepted cycle-control roundoff was altered instead of serialized");

    auto rejecting =
        require_encoder(make_engine_telemetry_ndjson_encoder(std::move(one_block)));
    Collector rejected_output{17U};
    const EngineTelemetryNdjsonChunkConsumer reject_consume =
        [&](auto offset, auto bytes) { return rejected_output.consume(offset, bytes); };
    expect(!rejecting.begin(reject_consume).has_value(),
           "excess-roundoff test header failed");
    auto invalid_cycle = rounded_cycle;
    invalid_cycle.requested_throttle.time_weighted_mean_01 =
        invalid_cycle.requested_throttle.maximum_01 + 2.0e-12;
    const std::array invalid_cycles{invalid_cycle};
    const EngineTelemetryNdjsonBlockInput invalid_block{
        0U,        EngineSessionBlockPhase::audible,
        0U,        400U,
        0U,        3840U,
        telemetry, invalid_cycles,
        {},
    };
    const auto before = rejected_output.bytes.size();
    const auto rejected = rejecting.write_block(invalid_block, reject_consume);
    expect(rejected.has_value() &&
               rejected->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::invalid_value &&
               rejected_output.bytes.size() == before && rejecting.failed(),
           "materially inconsistent cycle-control evidence was accepted");
}

template <class Mutator>
void expect_malformed_endpoint_quantity_rejected(Mutator mutate,
                                                 std::string_view expected_path) {
    auto encoder = require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "malformed-quantity test header failed");
    auto frame = endpoint(400U);
    mutate(frame.engine.torque);
    const std::array telemetry{frame};
    const std::array<EngineCompletedCycleEvidence, 0> cycles{};
    const EngineTelemetryNdjsonBlockInput block{
        0U,        EngineSessionBlockPhase::preparation,
        0U,        400U,
        0U,        3840U,
        telemetry, cycles,
        {},
    };
    const auto before = output.bytes.size();
    const auto status = encoder.write_block(block, consume);
    expect(status.has_value() &&
               status->code == EngineTelemetryNdjsonEncodingErrorCode::invalid_value &&
               status->path == expected_path && output.bytes.size() == before &&
               encoder.failed(),
           "malformed torque/quantity semantics reached the NDJSON stream");
}

void test_malformed_quantity_semantics() {
    expect_malformed_endpoint_quantity_rejected(
        [](contract::TorqueTelemetry &torque) { torque.cycle_work_j.value = -0.0; },
        "block.telemetry.engine.torque.cycle_work_j.value");

    expect_malformed_endpoint_quantity_rejected(
        [](contract::TorqueTelemetry &torque) {
            torque.instantaneous_net_shaft.omitted_terms =
                contract::torque_term_mask(contract::TorqueTerm::indicated_gas);
        },
        "block.telemetry.engine.torque.instantaneous_net_shaft.included_terms");

    expect_malformed_endpoint_quantity_rejected(
        [](contract::TorqueTelemetry &torque) {
            auto &indicated = torque.instantaneous_indicated_gas;
            indicated.value_nm = 10.0;
            indicated.availability = contract::Availability::available;
            indicated.completeness = contract::Completeness::complete;
            indicated.unavailable_reason = contract::QuantityUnavailableReason::none;
        },
        "block.telemetry.engine.torque.instantaneous_indicated_gas.included_terms");

    auto one_block = descriptor();
    one_block.motion_mode = EngineMotionMode::held_speed;
    one_block.preparation_block_count = 0U;
    one_block.total_block_count = 1U;
    auto encoder =
        require_encoder(make_engine_telemetry_ndjson_encoder(std::move(one_block)));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "malformed cycle-quantity header failed");
    const auto frame = endpoint(400U);
    const std::array telemetry{frame};
    auto malformed_cycle = cycle();
    malformed_cycle.start_boundary.left_physics_frame = 0U;
    malformed_cycle.start_boundary.right_physics_frame = 1U;
    malformed_cycle.end_boundary.left_physics_frame = 398U;
    malformed_cycle.end_boundary.right_physics_frame = 399U;
    malformed_cycle.instantaneous_net_shaft.completeness =
        contract::Completeness::complete;
    const std::array cycles{malformed_cycle};
    const EngineTelemetryNdjsonBlockInput block{
        0U, EngineSessionBlockPhase::audible, 0U, 400U, 0U, 3840U, telemetry, cycles,
        {},
    };
    const auto before = output.bytes.size();
    const auto status = encoder.write_block(block, consume);
    expect(
        status.has_value() &&
            status->code == EngineTelemetryNdjsonEncodingErrorCode::invalid_value &&
            status->path.find("instantaneous_net_shaft.angular_work_j.completeness") !=
                std::string::npos &&
            output.bytes.size() == before && encoder.failed(),
        "malformed completed-cycle torque semantics were published");
}

template <class Mutator>
void expect_endpoint_invariant_rejected(
    Mutator mutate, std::string_view expected_path,
    EngineMotionMode motion_mode = EngineMotionMode::free_vehicle) {
    auto one_block = descriptor();
    one_block.motion_mode = motion_mode;
    one_block.preparation_block_count = 0U;
    one_block.total_block_count = 1U;
    auto encoder =
        require_encoder(make_engine_telemetry_ndjson_encoder(std::move(one_block)));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(),
           "endpoint-invariant test header failed");
    auto frame = motion_mode == EngineMotionMode::held_dyno ? held_dyno_endpoint(400U)
                                                            : endpoint(400U);
    mutate(frame);
    const std::array telemetry{frame};
    const std::array<EngineCompletedCycleEvidence, 0> cycles{};
    const EngineTelemetryNdjsonBlockInput block{
        0U, EngineSessionBlockPhase::audible, 0U, 400U, 0U, 3840U, telemetry, cycles,
        {},
    };
    const auto before = output.bytes.size();
    const auto status = encoder.write_block(block, consume);
    expect(status.has_value() &&
               status->code == EngineTelemetryNdjsonEncodingErrorCode::invalid_value &&
               status->path == expected_path && output.bytes.size() == before &&
               encoder.failed(),
           "session endpoint invariant was not enforced by standalone encoding");
}

void test_session_endpoint_invariants() {
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) { frame.engine.theta_cycle_rad = -0.25; },
        "block.telemetry.engine.theta_cycle_rad");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.theta_cycle_rad = 4.0 * std::numbers::pi;
        },
        "block.telemetry.engine.theta_cycle_rad");

    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) { frame.engine.requested_throttle_01 = -0.01; },
        "block.telemetry.engine.requested_throttle_01");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.resolved_engine_throttle_01 = 1.01;
        },
        "block.telemetry.engine.resolved_engine_throttle_01");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.intake_plate_position_01 = -0.01;
        },
        "block.telemetry.engine.intake_plate_position_01");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.main_flow_multiplier_01 = 1.01;
        },
        "block.telemetry.engine.main_flow_multiplier_01");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.requested_external_resisting_torque_nm = -0.01;
        },
        "block.telemetry.engine.requested_external_resisting_torque_nm");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.mean_intake_manifold_pressure_pa_abs = 0.0;
        },
        "block.telemetry.mean_intake_manifold_pressure_pa_abs");

    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.engine.validity &=
                ~contract::capture_validity_mask(contract::CaptureValidity::torque);
        },
        "block.telemetry.engine.torque");
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) { frame.engine.torque = {}; },
        "block.telemetry.engine.torque");
}

void test_held_dyno_sidecar_invariants() {
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) { frame.engine.torque.actuator = {}; },
        "block.telemetry.engine.torque.actuator.availability",
        EngineMotionMode::held_dyno);
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) { frame.engine.torque.dyno_reaction = {}; },
        "block.telemetry.engine.torque.dyno_reaction.availability",
        EngineMotionMode::held_dyno);
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.held_dyno->applied_actuator_torque_nm = -9.25;
        },
        "block.telemetry.held_dyno.applied_actuator_torque_nm",
        EngineMotionMode::held_dyno);
    expect_endpoint_invariant_rejected(
        [](EngineTelemetryFrame &frame) {
            frame.held_dyno->applied_actuator_torque_nm = -0.0;
            frame.engine.torque.actuator.value_nm = -0.0;
            frame.engine.torque.dyno_reaction.value_nm = -0.0;
        },
        "block.telemetry.engine.torque.dyno_reaction.value_nm",
        EngineMotionMode::held_dyno);
}

void test_fail_closed_validation() {
    auto encoder = require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector output{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto bytes) {
        return output.consume(offset, bytes);
    };
    expect(!encoder.begin(consume).has_value(), "failure test header failed");
    const auto before = output.bytes.size();
    auto invalid_endpoint = endpoint(400U);
    invalid_endpoint.engine.engine_speed_rpm = std::numeric_limits<double>::quiet_NaN();
    const std::array telemetry{invalid_endpoint};
    const std::array<EngineCompletedCycleEvidence, 0> cycles{};
    const EngineTelemetryNdjsonBlockInput invalid{
        0U,        EngineSessionBlockPhase::preparation,
        0U,        400U,
        0U,        3840U,
        telemetry, cycles,
        {},
    };
    const auto status = encoder.write_block(invalid, consume);
    expect(status.has_value() &&
               status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::non_finite_value &&
               status->path == "block.telemetry.engine.engine_speed_rpm" &&
               output.bytes.size() == before && encoder.failed(),
           "non-finite endpoint did not fail before publishing its block");

    auto bad_events_encoder =
        require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector event_output{17U};
    const EngineTelemetryNdjsonChunkConsumer event_consume =
        [&](auto offset, auto bytes) { return event_output.consume(offset, bytes); };
    expect(!bad_events_encoder.begin(event_consume).has_value(),
           "event failure header failed");
    auto valid_endpoint = endpoint(400U);
    const std::array valid_telemetry{valid_endpoint};
    EngineEventCounters malformed;
    malformed.total_event_record_count = 1U;
    const EngineTelemetryNdjsonBlockInput invalid_events{
        0U,
        EngineSessionBlockPhase::preparation,
        0U,
        400U,
        0U,
        3840U,
        valid_telemetry,
        cycles,
        malformed,
    };
    const auto event_status =
        bad_events_encoder.write_block(invalid_events, event_consume);
    expect(event_status.has_value() &&
               event_status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::event_count_mismatch,
           "event partition mismatch was accepted");

    auto sidecar_encoder =
        require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector sidecar_output{17U};
    const EngineTelemetryNdjsonChunkConsumer sidecar_consume =
        [&](auto offset, auto bytes) { return sidecar_output.consume(offset, bytes); };
    expect(!sidecar_encoder.begin(sidecar_consume).has_value(),
           "sidecar failure header failed");
    auto impossible_frame = endpoint(400U);
    impossible_frame.held_dyno = EngineHeldDynoTelemetry{};
    const std::array impossible_telemetry{impossible_frame};
    const EngineTelemetryNdjsonBlockInput impossible_sidecar{
        0U,
        EngineSessionBlockPhase::preparation,
        0U,
        400U,
        0U,
        3840U,
        impossible_telemetry,
        cycles,
        {},
    };
    const auto sidecar_before = sidecar_output.bytes.size();
    const auto sidecar_status =
        sidecar_encoder.write_block(impossible_sidecar, sidecar_consume);
    expect(sidecar_status.has_value() &&
               sidecar_status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::invalid_value &&
               sidecar_status->path == "block.telemetry.mode_sidecars" &&
               sidecar_output.bytes.size() == sidecar_before &&
               sidecar_encoder.failed(),
           "motion-inconsistent telemetry sidecar was published");
}

void test_sequence_and_sink_failures() {
    auto early = require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector bytes{17U};
    const EngineTelemetryNdjsonChunkConsumer consume = [&](auto offset, auto chunk) {
        return bytes.consume(offset, chunk);
    };
    expect(!early.begin(consume).has_value(), "early-finish header failed");
    const auto early_status = early.finish(consume);
    expect(early_status.has_value() &&
               early_status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::unexpected_block &&
               early.failed(),
           "incomplete stream finalized successfully");

    auto out_of_order =
        require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    Collector ordering_bytes{17U};
    const EngineTelemetryNdjsonChunkConsumer ordering_consume =
        [&](auto offset, auto chunk) { return ordering_bytes.consume(offset, chunk); };
    expect(!out_of_order.begin(ordering_consume).has_value(),
           "ordering test header failed");
    const auto frame = endpoint(800U);
    const std::array telemetry{frame};
    const std::array<EngineCompletedCycleEvidence, 0> cycles{};
    const EngineTelemetryNdjsonBlockInput second_first{
        1U,        EngineSessionBlockPhase::audible,
        400U,      400U,
        3840U,     3840U,
        telemetry, cycles,
        {},
    };
    const auto ordering_status =
        out_of_order.write_block(second_first, ordering_consume);
    expect(ordering_status.has_value() &&
               ordering_status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::unexpected_block &&
               out_of_order.failed(),
           "out-of-order block was accepted");

    auto rejected = require_encoder(make_engine_telemetry_ndjson_encoder(descriptor()));
    std::size_t calls = 0;
    const EngineTelemetryNdjsonChunkConsumer rejecting =
        [&](std::uint64_t, std::span<const std::byte>) {
            ++calls;
            return calls < 2U;
        };
    const auto rejected_status = rejected.begin(rejecting);
    expect(rejected_status.has_value() &&
               rejected_status->code ==
                   EngineTelemetryNdjsonEncodingErrorCode::callback_rejected &&
               rejected.failed() && rejected.bytes_emitted() == 17U,
           "sink rejection did not terminally preserve the accepted byte horizon");
}

} // namespace

int main() {
    try {
        freeze_v1_public_field_inventory();
        test_canonical_complete_stream();
        test_held_dyno_sidecar_golden();
        test_cycle_control_roundoff_tolerance();
        test_malformed_quantity_semantics();
        test_session_endpoint_invariants();
        test_held_dyno_sidecar_invariants();
        test_fail_closed_validation();
        test_sequence_and_sink_failures();
    } catch (const std::exception &error) {
        std::cerr << "engine telemetry NDJSON encoder test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
