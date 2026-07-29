#pragma once

#include "determinism/renderer_source_stamp.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_full_throttle_torque_sweep_request.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::reference {

inline constexpr std::string_view kBmwM52b28TorqueSweepEvidenceWireSchema =
    "engine-sim-offline.bmw-m52b28-torque-sweep-evidence.v1";
inline constexpr std::string_view kBmwM52b28TorqueSweepEvidenceCanonicalGrammar =
    "engine-sim-offline.bmw-m52b28-torque-sweep-evidence-canonical-json.v1";
inline constexpr std::string_view kBmwM52b28TorqueSweepEvidenceFilename =
    "bmw-m52b28-m4-torque-sweep-v1.json";
inline constexpr std::string_view kBmwM52b28TorqueSweepEvidenceSha256Filename =
    "bmw-m52b28-m4-torque-sweep-v1.json.sha256";
inline constexpr double kBmwM52b28NominalTorqueNm = 280.0;
inline constexpr double kBmwM52b28NominalPowerW = 142000.0;
inline constexpr double kBmwM52b28GrossErrorWarningLowerRatio = 0.5;
inline constexpr double kBmwM52b28GrossErrorWarningUpperRatio = 1.5;

struct BmwM52b28TorqueSweepConditions {
    double ambient_pressure_pa_abs = 0.0;
    double ambient_temperature_k = 0.0;
    double relative_humidity_01 = 0.0;
    double gas_temperature_k = 0.0;
    double wall_temperature_k = 0.0;
    double coolant_temperature_k = 0.0;
    double oil_temperature_k = 0.0;
    double crankcase_pressure_pa_abs = 0.0;
    double crankcase_temperature_k = 0.0;
    double total_displacement_m3 = 0.0;
    std::string fuel_id;
    double lower_heating_value_j_per_kg = 0.0;
    double stoichiometric_air_fuel_mass_ratio = 0.0;
    std::string accessory_configuration_id;
    contract::Sha256Digest accessory_configuration_sha256;
    double initial_theta_rad = 0.0;
    double throttle_01 = 0.0;
    std::uint64_t public_seed = 0U;
    std::uint64_t physics_rate_numerator = 0U;
    std::uint64_t physics_rate_denominator = 0U;
    std::string convergence_method_id;
    std::uint32_t convergence_method_version = 0U;
    contract::Sha256Digest convergence_method_configuration_sha256;
    std::uint32_t comparison_cycle_count = 0U;
    std::uint64_t cutoff_frame = 0U;
    std::uint64_t tail_frame_count = 0U;

    friend bool operator==(const BmwM52b28TorqueSweepConditions &,
                           const BmwM52b28TorqueSweepConditions &) = default;
};

struct BmwM52b28TorqueSweepPointEvidence {
    contract::Sha256Digest simulation_request_identity_v2_sha256;
    contract::Sha256Digest provenance_bundle_sha256;
    std::string scenario_id;
    double engine_speed_rpm = 0.0;
    double throttle_01 = 0.0;
    double indicated_gas_cycle_mean_torque_nm = 0.0;
    double aggregate_loss_cycle_mean_torque_nm = 0.0;
    double starter_cycle_mean_torque_nm = 0.0;
    double net_shaft_cycle_mean_torque_nm = 0.0;
    double net_bmep_pa = 0.0;
    double mean_power_w = 0.0;
    double torque_residual_nm = 0.0;
    double torque_tolerance_nm = 0.0;
    double pressure_residual_pa = 0.0;
    double pressure_tolerance_pa = 0.0;
    std::uint64_t block_a_first_cycle = 0U;
    std::uint64_t block_a_last_cycle = 0U;
    std::uint64_t block_b_first_cycle = 0U;
    std::uint64_t block_b_last_cycle = 0U;
    std::string applicability_label;
    std::uint64_t elapsed_ns = 0U;

    friend bool operator==(const BmwM52b28TorqueSweepPointEvidence &,
                           const BmwM52b28TorqueSweepPointEvidence &) = default;
};

struct BmwM52b28TorqueSweepComparisons {
    double torque_at_3950_nm = 0.0;
    double torque_at_3950_to_280_ratio = 0.0;
    double power_at_5300_w = 0.0;
    double power_at_5300_to_142000_ratio = 0.0;
    double sampled_maximum_torque_nm = 0.0;
    double sampled_maximum_torque_rpm = 0.0;
    double sampled_maximum_torque_to_280_ratio = 0.0;
    double sampled_maximum_power_w = 0.0;
    double sampled_maximum_power_rpm = 0.0;
    double sampled_maximum_power_to_142000_ratio = 0.0;
    double gross_error_lower_ratio = kBmwM52b28GrossErrorWarningLowerRatio;
    double gross_error_upper_ratio = kBmwM52b28GrossErrorWarningUpperRatio;

    friend bool operator==(const BmwM52b28TorqueSweepComparisons &,
                           const BmwM52b28TorqueSweepComparisons &) = default;
};

struct BmwM52b28TorqueSweepEvidence {
    determinism::RendererSourceStamp source;
    contract::Sha256Digest model_record_sha256;
    std::string engine_profile_id;
    BmwM52b28TorqueSweepConditions conditions;
    std::array<BmwM52b28TorqueSweepPointEvidence,
               profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
        points;
    BmwM52b28TorqueSweepComparisons comparisons;
    std::vector<std::string> warnings;
    std::uint64_t total_elapsed_ns = 0U;

    friend bool operator==(const BmwM52b28TorqueSweepEvidence &,
                           const BmwM52b28TorqueSweepEvidence &) = default;
};

struct BmwM52b28TorqueSweepEvidenceError {
    std::string detail_code;
    std::string message;
    std::size_t point_index = profiles::kBmwM52b28FullThrottleTorqueSweepPointCount;

    friend bool operator==(const BmwM52b28TorqueSweepEvidenceError &,
                           const BmwM52b28TorqueSweepEvidenceError &) = default;
};

struct BmwM52b28TorqueSweepEvidenceEncoding {
    // bytes is the exact no-final-LF canonical JSON payload. sha256_sidecar_bytes is
    // the exact sha256sum-compatible sidecar for the frozen JSON basename.
    std::vector<std::byte> bytes;
    contract::Sha256Digest sha256;
    std::vector<std::byte> sha256_sidecar_bytes;

    friend bool operator==(const BmwM52b28TorqueSweepEvidenceEncoding &,
                           const BmwM52b28TorqueSweepEvidenceEncoding &) = default;
};

using BmwM52b28TorqueSweepEvidenceResult =
    std::variant<BmwM52b28TorqueSweepEvidence, BmwM52b28TorqueSweepEvidenceError>;
using BmwM52b28TorqueSweepEvidenceEncodingResult =
    std::variant<BmwM52b28TorqueSweepEvidenceEncoding,
                 BmwM52b28TorqueSweepEvidenceError>;

[[nodiscard]] BmwM52b28TorqueSweepEvidenceResult
run_bmw_m52b28_full_throttle_torque_sweep(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequestSet &requests);

[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_torque_sweep_evidence(const BmwM52b28TorqueSweepEvidence &evidence);

[[nodiscard]] BmwM52b28TorqueSweepEvidenceEncodingResult
encode_bmw_m52b28_torque_sweep_evidence(const BmwM52b28TorqueSweepEvidence &evidence);

} // namespace engine_sim_offline::reference
