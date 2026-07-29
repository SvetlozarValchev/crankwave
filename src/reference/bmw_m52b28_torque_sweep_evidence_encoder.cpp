#include "reference/bmw_m52b28_torque_sweep_evidence.hpp"

#include "identity/canonical_json_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <string>
#include <utility>

namespace engine_sim_offline::reference {
namespace {

using identity::detail::CanonicalJsonWriter;

[[nodiscard]] BmwM52b28TorqueSweepEvidenceError error(std::string detail_code,
                                                      std::string message) {
    return {std::move(detail_code), std::move(message),
            profiles::kBmwM52b28FullThrottleTorqueSweepPointCount};
}

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::string text;
    for (const auto &issue : report.issues) {
        text += "\n  ";
        text += issue.path;
        text += ": ";
        text += issue.message;
    }
    return text;
}

[[nodiscard]] bool key_f64(CanonicalJsonWriter &writer, std::string_view key,
                           double value) {
    return writer.key(key) && writer.binary64_bits_value(value);
}

[[nodiscard]] bool key_u64(CanonicalJsonWriter &writer, std::string_view key,
                           std::uint64_t value) {
    return writer.key(key) && writer.uint64_hex_value(value);
}

[[nodiscard]] bool key_sha256(CanonicalJsonWriter &writer, std::string_view key,
                              const contract::Sha256Digest &value) {
    return writer.key(key) && writer.sha256_value(value);
}

[[nodiscard]] bool key_string(CanonicalJsonWriter &writer, std::string_view key,
                              std::string_view value) {
    return writer.key(key) && writer.string_value(value);
}

[[nodiscard]] bool write_conditions(CanonicalJsonWriter &writer,
                                    const BmwM52b28TorqueSweepConditions &value) {
    return writer.begin_object() &&
           key_f64(writer, "ambient_pressure_pa_abs", value.ambient_pressure_pa_abs) &&
           key_f64(writer, "ambient_temperature_k", value.ambient_temperature_k) &&
           key_f64(writer, "relative_humidity_01", value.relative_humidity_01) &&
           key_f64(writer, "gas_temperature_k", value.gas_temperature_k) &&
           key_f64(writer, "wall_temperature_k", value.wall_temperature_k) &&
           key_f64(writer, "coolant_temperature_k", value.coolant_temperature_k) &&
           key_f64(writer, "oil_temperature_k", value.oil_temperature_k) &&
           key_f64(writer, "crankcase_pressure_pa_abs",
                   value.crankcase_pressure_pa_abs) &&
           key_f64(writer, "crankcase_temperature_k", value.crankcase_temperature_k) &&
           key_f64(writer, "total_displacement_m3", value.total_displacement_m3) &&
           key_string(writer, "fuel_id", value.fuel_id) &&
           key_f64(writer, "lower_heating_value_j_per_kg",
                   value.lower_heating_value_j_per_kg) &&
           key_f64(writer, "stoichiometric_air_fuel_mass_ratio",
                   value.stoichiometric_air_fuel_mass_ratio) &&
           key_string(writer, "accessory_configuration_id",
                      value.accessory_configuration_id) &&
           key_sha256(writer, "accessory_configuration_sha256",
                      value.accessory_configuration_sha256) &&
           key_f64(writer, "initial_theta_rad", value.initial_theta_rad) &&
           key_f64(writer, "throttle_01", value.throttle_01) &&
           key_u64(writer, "public_seed", value.public_seed) &&
           key_u64(writer, "physics_rate_numerator", value.physics_rate_numerator) &&
           key_u64(writer, "physics_rate_denominator",
                   value.physics_rate_denominator) &&
           key_string(writer, "convergence_method_id", value.convergence_method_id) &&
           key_u64(writer, "convergence_method_version",
                   value.convergence_method_version) &&
           key_sha256(writer, "convergence_method_configuration_sha256",
                      value.convergence_method_configuration_sha256) &&
           key_u64(writer, "comparison_cycle_count", value.comparison_cycle_count) &&
           key_u64(writer, "cutoff_frame", value.cutoff_frame) &&
           key_u64(writer, "tail_frame_count", value.tail_frame_count) &&
           writer.end_object();
}

[[nodiscard]] bool write_point(CanonicalJsonWriter &writer,
                               const BmwM52b28TorqueSweepPointEvidence &value) {
    return writer.begin_object() &&
           key_string(writer, "scenario_id", value.scenario_id) &&
           key_sha256(writer, "simulation_request_v2_sha256",
                      value.simulation_request_identity_v2_sha256) &&
           key_sha256(writer, "provenance_bundle_sha256",
                      value.provenance_bundle_sha256) &&
           key_f64(writer, "engine_speed_rpm", value.engine_speed_rpm) &&
           key_f64(writer, "throttle_01", value.throttle_01) &&
           key_f64(writer, "indicated_gas_torque_nm",
                   value.indicated_gas_cycle_mean_torque_nm) &&
           key_f64(writer, "aggregate_loss_torque_nm",
                   value.aggregate_loss_cycle_mean_torque_nm) &&
           key_f64(writer, "starter_torque_nm", value.starter_cycle_mean_torque_nm) &&
           key_f64(writer, "net_shaft_torque_nm",
                   value.net_shaft_cycle_mean_torque_nm) &&
           key_f64(writer, "net_bmep_pa", value.net_bmep_pa) &&
           key_f64(writer, "mean_power_w", value.mean_power_w) &&
           key_f64(writer, "torque_residual_nm", value.torque_residual_nm) &&
           key_f64(writer, "torque_tolerance_nm", value.torque_tolerance_nm) &&
           key_f64(writer, "pressure_residual_pa", value.pressure_residual_pa) &&
           key_f64(writer, "pressure_tolerance_pa", value.pressure_tolerance_pa) &&
           key_u64(writer, "block_a_first_cycle", value.block_a_first_cycle) &&
           key_u64(writer, "block_a_last_cycle", value.block_a_last_cycle) &&
           key_u64(writer, "block_b_first_cycle", value.block_b_first_cycle) &&
           key_u64(writer, "block_b_last_cycle", value.block_b_last_cycle) &&
           key_string(writer, "applicability_label", value.applicability_label) &&
           key_u64(writer, "elapsed_ns", value.elapsed_ns) && writer.end_object();
}

[[nodiscard]] bool write_points(
    CanonicalJsonWriter &writer,
    const std::array<BmwM52b28TorqueSweepPointEvidence,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount> &points) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &point : points) {
        if (!write_point(writer, point)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_comparisons(CanonicalJsonWriter &writer,
                                     const BmwM52b28TorqueSweepComparisons &value) {
    return writer.begin_object() &&
           key_f64(writer, "torque_at_3950_nm", value.torque_at_3950_nm) &&
           key_f64(writer, "torque_at_3950_to_280_ratio",
                   value.torque_at_3950_to_280_ratio) &&
           key_f64(writer, "power_at_5300_w", value.power_at_5300_w) &&
           key_f64(writer, "power_at_5300_to_142000_ratio",
                   value.power_at_5300_to_142000_ratio) &&
           key_f64(writer, "sampled_maximum_torque_nm",
                   value.sampled_maximum_torque_nm) &&
           key_f64(writer, "sampled_maximum_torque_rpm",
                   value.sampled_maximum_torque_rpm) &&
           key_f64(writer, "sampled_maximum_torque_to_280_ratio",
                   value.sampled_maximum_torque_to_280_ratio) &&
           key_f64(writer, "sampled_maximum_power_w", value.sampled_maximum_power_w) &&
           key_f64(writer, "sampled_maximum_power_rpm",
                   value.sampled_maximum_power_rpm) &&
           key_f64(writer, "sampled_maximum_power_to_142000_ratio",
                   value.sampled_maximum_power_to_142000_ratio) &&
           key_f64(writer, "gross_error_lower_ratio", value.gross_error_lower_ratio) &&
           key_f64(writer, "gross_error_upper_ratio", value.gross_error_upper_ratio) &&
           writer.end_object();
}

[[nodiscard]] bool write_warnings(CanonicalJsonWriter &writer,
                                  const std::vector<std::string> &warnings) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &warning : warnings) {
        if (!writer.string_value(warning)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_evidence(CanonicalJsonWriter &writer,
                                  const BmwM52b28TorqueSweepEvidence &evidence) {
    return writer.begin_object() &&
           key_string(writer, "wire_schema", kBmwM52b28TorqueSweepEvidenceWireSchema) &&
           writer.key("schema_version") && writer.uint32_value(1U) &&
           key_string(writer, "source_commit", evidence.source.full_git_head) &&
           key_sha256(writer, "model_record_sha256", evidence.model_record_sha256) &&
           key_string(writer, "engine_profile_id", evidence.engine_profile_id) &&
           writer.key("conditions") && write_conditions(writer, evidence.conditions) &&
           writer.key("points") && write_points(writer, evidence.points) &&
           writer.key("comparisons") &&
           write_comparisons(writer, evidence.comparisons) && writer.key("warnings") &&
           write_warnings(writer, evidence.warnings) && writer.key("execution") &&
           writer.begin_object() && writer.key("point_count") &&
           writer.uint32_value(profiles::kBmwM52b28FullThrottleTorqueSweepPointCount) &&
           key_u64(writer, "total_elapsed_ns", evidence.total_elapsed_ns) &&
           writer.end_object() && writer.end_object();
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::array<char, 16> kHexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result;
    result.resize(64U);
    for (std::size_t index = 0U; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kHexDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kHexDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> bytes_for(std::string_view value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const char character : value) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return result;
}

} // namespace

BmwM52b28TorqueSweepEvidenceEncodingResult
encode_bmw_m52b28_torque_sweep_evidence(const BmwM52b28TorqueSweepEvidence &evidence) {
    try {
        const auto report = validate_bmw_m52b28_torque_sweep_evidence(evidence);
        if (!report.ok()) {
            return error("bmw-torque-sweep-evidence-encoding-rejected",
                         "invalid evidence cannot be encoded" +
                             validation_text(report));
        }

        CanonicalJsonWriter writer;
        std::vector<std::byte> bytes;
        if (!write_evidence(writer, evidence) || !writer.finish(bytes)) {
            return error("bmw-torque-sweep-evidence-encoding-failed",
                         std::string{writer.error_message()});
        }
        if (bytes.empty() || bytes.back() != std::byte{'\n'}) {
            return error("bmw-torque-sweep-evidence-encoding-failed",
                         "canonical writer did not terminate its document");
        }
        // The shared writer uses final LF for manifests/request identities. The
        // frozen sweep grammar deliberately does not, so remove exactly that byte.
        bytes.pop_back();
        const auto sha256 = contract::sha256(bytes);
        const std::string sidecar = digest_hex(sha256) + "  " +
                                    std::string{kBmwM52b28TorqueSweepEvidenceFilename} +
                                    "\n";
        return BmwM52b28TorqueSweepEvidenceEncoding{
            std::move(bytes),
            sha256,
            bytes_for(sidecar),
        };
    } catch (const std::bad_alloc &) {
        return error("bmw-torque-sweep-evidence-encoding-allocation-failed",
                     "evidence encoding ran out of memory");
    } catch (const std::exception &exception) {
        return error("bmw-torque-sweep-evidence-encoding-threw",
                     "evidence encoding threw: " + std::string{exception.what()});
    } catch (...) {
        return error("bmw-torque-sweep-evidence-encoding-threw",
                     "evidence encoding threw a non-standard exception");
    }
}

} // namespace engine_sim_offline::reference
