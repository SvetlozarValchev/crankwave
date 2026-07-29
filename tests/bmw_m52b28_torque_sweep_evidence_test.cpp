#include "engine_sim_offline/request_identity.hpp"
#include "reference/bmw_m52b28_torque_sweep_evidence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] contract::Sha256Digest digest(std::uint8_t marker) {
    contract::Sha256Digest value;
    value.bytes.back() = marker;
    return value;
}

[[nodiscard]] reference::BmwM52b28TorqueSweepEvidence fixture() {
    auto request_result =
        profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    const auto *requests =
        std::get_if<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(
            &request_result);
    expect(requests != nullptr,
           "canonical BMW torque-sweep request factory was rejected");
    const auto &first_request = requests->front();
    const auto *profile = std::get_if<contract::LowOrderOperatingPointV1Profile>(
        &first_request.engine.physics_profile);
    const auto *first_held =
        std::get_if<contract::HeldSpeed>(&first_request.scenario.mode);
    const auto *preparation = std::get_if<contract::FixedHorizonCycleSampling>(
        &first_request.scenario.preparation);
    const auto fixed_preparation_horizon_frame =
        preparation == nullptr ? std::optional<std::uint64_t>{}
                               : contract::resolve_frame_index(
                                     preparation->fixed_preparation_horizon_s.value,
                                     first_request.scenario.rates.physics);
    const auto total_frame =
        contract::resolve_frame_index(first_request.scenario.total_duration_s.value,
                                      first_request.scenario.rates.physics);
    expect(profile != nullptr && first_held != nullptr && preparation != nullptr &&
               fixed_preparation_horizon_frame.has_value() && total_frame.has_value() &&
               *total_frame >= *fixed_preparation_horizon_frame,
           "canonical BMW torque-sweep request lost its frozen conditions");

    std::optional<contract::Sha256Digest> model_record_sha256;
    for (const auto &source : first_request.provenance.evidence) {
        if (source.id != "operating-point-model-record") {
            continue;
        }
        expect(!model_record_sha256.has_value() && source.content_sha256.has_value(),
               "canonical BMW request has ambiguous model-record evidence");
        model_record_sha256 = source.content_sha256;
    }
    expect(model_record_sha256.has_value(),
           "canonical BMW request has no model-record evidence");

    reference::BmwM52b28TorqueSweepEvidence evidence;
    evidence.source = {
        determinism::RendererSourceState::clean,
        std::string(40U, 'a'),
        digest(1U),
        "Clang",
        "20.0.0",
        "x86_64-unknown-linux-gnu",
    };
    evidence.model_record_sha256 = *model_record_sha256;
    evidence.engine_profile_id = first_request.engine.profile_id.value;
    evidence.conditions = {
        first_request.scenario.ambient.pressure_pa_abs.value,
        first_request.scenario.ambient.temperature_k.value,
        first_request.scenario.ambient.relative_humidity_01.value,
        first_request.scenario.initial_thermal_state.gas_temperature_k.value,
        first_request.scenario.initial_thermal_state.wall_temperature_k.value,
        first_request.scenario.initial_thermal_state.coolant_temperature_k.value,
        first_request.scenario.initial_thermal_state.oil_temperature_k.value,
        first_request.scenario.crankcase.pressure_pa_abs.value,
        first_request.scenario.crankcase.temperature_k.value,
        first_request.engine.total_displacement_m3.value,
        first_request.scenario.fuel.fuel_id.value,
        first_request.scenario.fuel.lower_heating_value_j_per_kg.value,
        first_request.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
        profile->accessory_configuration.configuration_id.value,
        profile->accessory_configuration.content_sha256.value,
        first_held->initial_theta_rad.value,
        first_held->throttle_01.value,
        first_request.scenario.public_seed.value,
        first_request.scenario.rates.physics.numerator,
        first_request.scenario.rates.physics.denominator,
        preparation->method.value.id,
        preparation->method.value.version,
        preparation->method.value.configuration_sha256,
        preparation->trailing_complete_cycle_count.value,
        *fixed_preparation_horizon_frame,
        *total_frame - *fixed_preparation_horizon_frame,
    };

    constexpr std::array<double, 9> kTorques{
        200.0, 240.0, 270.0, 290.0, 300.0, 300.0, 280.0, 260.0, 200.0,
    };
    constexpr std::array<double, 9> kPowers{
        30000.0,  60000.0,  85000.0,  110000.0, 130000.0,
        150000.0, 160000.0, 160000.0, 150000.0,
    };
    std::uint64_t elapsed_sum = 0U;
    for (std::size_t index = 0U; index < evidence.points.size(); ++index) {
        const auto &request = (*requests)[index];
        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        expect(held != nullptr,
               "canonical BMW torque-sweep point lost held-speed mode");
        const auto identity_result = identity::encode_simulation_request_identity_v3(
            request.engine, request.scenario, request.provenance.bundle);
        const auto *identity_encoding =
            std::get_if<identity::SimulationRequestIdentityEncoding>(&identity_result);
        expect(identity_encoding != nullptr,
               "canonical BMW torque-sweep request identity did not encode");
        const auto first_cycle = static_cast<std::uint64_t>(index * 64U);
        const auto elapsed = static_cast<std::uint64_t>(100U + index);
        elapsed_sum += elapsed;
        evidence.points[index] = {
            request.scenario.scenario_id,
            identity_encoding->sha256,
            request.provenance.bundle.sha256,
            held->engine_speed_rpm.value,
            held->throttle_01.value,
            kTorques[index] + 60.0,
            -60.0,
            0.0,
            kTorques[index],
            1000000.0,
            kPowers[index],
            first_cycle,
            first_cycle + 31U,
            std::string{
                contract::kGenericChenFlynnLowOrderModelPredictionApplicability},
            elapsed,
        };
    }
    evidence.comparisons = {
        kTorques[4U],
        kTorques[4U] / reference::kBmwM52b28NominalTorqueNm,
        kPowers[6U],
        kPowers[6U] / reference::kBmwM52b28NominalPowerW,
        kTorques[4U],
        profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm[4U],
        kTorques[4U] / reference::kBmwM52b28NominalTorqueNm,
        kPowers[6U],
        profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm[6U],
        kPowers[6U] / reference::kBmwM52b28NominalPowerW,
        reference::kBmwM52b28GrossErrorWarningLowerRatio,
        reference::kBmwM52b28GrossErrorWarningUpperRatio,
    };
    evidence.total_elapsed_ns = elapsed_sum + 50U;
    return evidence;
}

[[nodiscard]] std::string text(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &value) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string result(64U, '0');
    for (std::size_t index = 0U; index < value.bytes.size(); ++index) {
        result[index * 2U] = kDigits[value.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[value.bytes[index] & 0x0fU];
    }
    return result;
}

void run_tests() {
    const auto evidence = fixture();
    expect(reference::validate_bmw_m52b28_torque_sweep_evidence(evidence).ok(),
           "valid canonical evidence fixture was rejected");

    const auto encoded_result =
        reference::encode_bmw_m52b28_torque_sweep_evidence(evidence);
    const auto *encoded =
        std::get_if<reference::BmwM52b28TorqueSweepEvidenceEncoding>(&encoded_result);
    expect(encoded != nullptr, "valid evidence did not encode");
    expect(!encoded->bytes.empty() && encoded->bytes.back() != std::byte{'\n'},
           "canonical JSON unexpectedly has a final newline");
    expect(contract::sha256(encoded->bytes) == encoded->sha256,
           "retained SHA-256 does not cover exact canonical JSON bytes");
    const auto encoded_sha256 = digest_hex(encoded->sha256);
    constexpr std::string_view kExpectedEncodedSha256 =
        "0baca7c1ad439e3f13938f2dc2b4f7253c775b55c5e962d978e55186afa23479";
    if (encoded_sha256 != kExpectedEncodedSha256) {
        std::cerr << "BMW torque-sweep evidence SHA-256: " << encoded_sha256 << '\n';
    }
    expect(encoded_sha256 == kExpectedEncodedSha256,
           "canonical fixture bytes drifted from the pinned full-document SHA-256");

    const auto json = text(encoded->bytes);
    expect(
        json.starts_with("{\"wire_schema\":\"engine-sim-offline.bmw-m52b28-torque-"
                         "sweep-evidence.v2\",\"schema_version\":2,\"source_commit\":"),
        "root wire fields are not in frozen order");
    expect(json.find("\"schema_version\":\"0x") == std::string::npos,
           "schema_version was not encoded as an unsigned decimal integer");
    expect(json.find("\"point_count\":9") != std::string::npos,
           "point_count was not encoded as an unsigned decimal integer");
    expect(json.find("\"sampling_method_version\":\"0x0000000000000001\"") !=
               std::string::npos,
           "non-exempt integer was not encoded as fixed-width quoted hex");

    const auto expected_sidecar =
        digest_hex(encoded->sha256) + "  " +
        std::string{reference::kBmwM52b28TorqueSweepEvidenceFilename} + "\n";
    expect(text(encoded->sha256_sidecar_bytes) == expected_sidecar,
           "SHA-256 sidecar bytes do not match the frozen sha256sum grammar");

    const auto repeated_result =
        reference::encode_bmw_m52b28_torque_sweep_evidence(evidence);
    const auto *repeated =
        std::get_if<reference::BmwM52b28TorqueSweepEvidenceEncoding>(&repeated_result);
    expect(repeated != nullptr && *repeated == *encoded,
           "same evidence did not produce byte-identical encoding");

    const auto expect_binding_rejected = [](const auto &candidate,
                                            std::string_view message) {
        expect(!reference::validate_bmw_m52b28_torque_sweep_evidence(candidate).ok(),
               message);
    };
    auto wrong_model_record = evidence;
    wrong_model_record.model_record_sha256 = digest(2U);
    expect_binding_rejected(wrong_model_record,
                            "fabricated model-record digest was accepted");

    auto wrong_engine_profile = evidence;
    wrong_engine_profile.engine_profile_id = "bmw-m52b28-low-order-operating-point-v2";
    expect_binding_rejected(wrong_engine_profile,
                            "fabricated engine profile ID was accepted");

    auto wrong_conditions = evidence;
    wrong_conditions.conditions.total_displacement_m3 *= 1.01;
    expect_binding_rejected(wrong_conditions,
                            "fabricated canonical conditions were accepted");

    auto wrong_request_identity = evidence;
    wrong_request_identity.points.front().simulation_request_v3_sha256 = digest(20U);
    expect_binding_rejected(wrong_request_identity,
                            "fabricated request identity was accepted");

    auto wrong_provenance = evidence;
    wrong_provenance.points.front().provenance_bundle_sha256 = digest(21U);
    expect_binding_rejected(wrong_provenance,
                            "fabricated provenance identity was accepted");

    auto wrong_scenario = evidence;
    wrong_scenario.points.front().scenario_id =
        "bmw-m52b28-held-1500rpm-full-throttle-torque-sweep-v3";
    expect_binding_rejected(wrong_scenario,
                            "fabricated scenario identity was accepted");

    auto wrong_operating_point = evidence;
    wrong_operating_point.points.front().engine_speed_rpm = 1501.0;
    wrong_operating_point.points.front().throttle_01 = 0.99;
    expect_binding_rejected(wrong_operating_point,
                            "fabricated RPM/throttle binding was accepted");
    expect(
        std::holds_alternative<reference::BmwM52b28TorqueSweepEvidenceError>(
            reference::encode_bmw_m52b28_torque_sweep_evidence(wrong_operating_point)),
        "encoder accepted fabricated canonical-labelled evidence");

    auto warnings = evidence;
    warnings.points[4U].indicated_gas_cycle_mean_torque_nm = 560.0;
    warnings.points[4U].net_shaft_cycle_mean_torque_nm = 500.0;
    warnings.points[5U].indicated_gas_cycle_mean_torque_nm = 560.0;
    warnings.points[5U].net_shaft_cycle_mean_torque_nm = 500.0;
    warnings.points[6U].mean_power_w = 300000.0;
    warnings.points[7U].mean_power_w = 300000.0;
    warnings.comparisons = {
        500.0,
        500.0 / reference::kBmwM52b28NominalTorqueNm,
        300000.0,
        300000.0 / reference::kBmwM52b28NominalPowerW,
        500.0,
        3950.0,
        500.0 / reference::kBmwM52b28NominalTorqueNm,
        300000.0,
        5300.0,
        300000.0 / reference::kBmwM52b28NominalPowerW,
        reference::kBmwM52b28GrossErrorWarningLowerRatio,
        reference::kBmwM52b28GrossErrorWarningUpperRatio,
    };
    warnings.warnings = {
        "sampled-maximum-torque-outside-warning-ratio",
        "sampled-maximum-power-outside-warning-ratio",
    };
    expect(reference::validate_bmw_m52b28_torque_sweep_evidence(warnings).ok(),
           "exact stable warning strings in torque-then-power order were rejected");
    const auto warnings_encoded_result =
        reference::encode_bmw_m52b28_torque_sweep_evidence(warnings);
    const auto *warnings_encoded =
        std::get_if<reference::BmwM52b28TorqueSweepEvidenceEncoding>(
            &warnings_encoded_result);
    expect(warnings_encoded != nullptr,
           "valid warning-bearing evidence did not encode");
    expect(
        text(warnings_encoded->bytes)
                .find(
                    "\"warnings\":[\"sampled-maximum-torque-outside-warning-ratio\","
                    "\"sampled-maximum-power-outside-warning-ratio\"],\"execution\"") !=
            std::string::npos,
        "warning strings or their frozen torque-then-power wire order drifted");

    auto inclusive_endpoints = evidence;
    inclusive_endpoints.points[4U].indicated_gas_cycle_mean_torque_nm = 480.0;
    inclusive_endpoints.points[4U].net_shaft_cycle_mean_torque_nm = 420.0;
    inclusive_endpoints.points[5U].indicated_gas_cycle_mean_torque_nm = 480.0;
    inclusive_endpoints.points[5U].net_shaft_cycle_mean_torque_nm = 420.0;
    inclusive_endpoints.points[6U].mean_power_w = 213000.0;
    inclusive_endpoints.points[7U].mean_power_w = 213000.0;
    inclusive_endpoints.comparisons = {
        420.0,
        1.5,
        213000.0,
        1.5,
        420.0,
        3950.0,
        1.5,
        213000.0,
        5300.0,
        1.5,
        reference::kBmwM52b28GrossErrorWarningLowerRatio,
        reference::kBmwM52b28GrossErrorWarningUpperRatio,
    };
    expect(
        reference::validate_bmw_m52b28_torque_sweep_evidence(inclusive_endpoints).ok(),
        "inclusive 1.5 warning endpoints incorrectly required warnings");

    auto wrong_tie = evidence;
    wrong_tie.comparisons.sampled_maximum_torque_rpm = 4500.0;
    expect(!reference::validate_bmw_m52b28_torque_sweep_evidence(wrong_tie).ok(),
           "higher-RPM winner for an exact torque tie was accepted");

    auto invalid_number = evidence;
    invalid_number.points.front().mean_power_w =
        std::numeric_limits<double>::quiet_NaN();
    expect(std::holds_alternative<reference::BmwM52b28TorqueSweepEvidenceError>(
               reference::encode_bmw_m52b28_torque_sweep_evidence(invalid_number)),
           "non-finite point evidence was encoded");
}

} // namespace

int main() {
    run_tests();
    return EXIT_SUCCESS;
}
