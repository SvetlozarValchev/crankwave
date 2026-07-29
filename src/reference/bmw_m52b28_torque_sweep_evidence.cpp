#include "reference/bmw_m52b28_torque_sweep_evidence.hpp"

#include "engine_sim_offline/request_identity.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace engine_sim_offline::reference {
namespace {

using SweepClock = std::chrono::steady_clock;

constexpr std::size_t kNoPoint = profiles::kBmwM52b28FullThrottleTorqueSweepPointCount;
constexpr std::uint32_t kComparisonCycleCount = 16U;
constexpr std::uint64_t kCutoffFrame = UINT64_C(64400);
constexpr std::uint64_t kTailFrameCount = UINT64_C(200);
constexpr std::string_view kModelRecordEvidenceId = "operating-point-model-record";
constexpr std::string_view kTorqueWarning =
    "sampled-maximum-torque-outside-warning-ratio";
constexpr std::string_view kPowerWarning =
    "sampled-maximum-power-outside-warning-ratio";

struct ExecutedPoint {
    BmwM52b28TorqueSweepPointEvidence evidence;
    BmwM52b28TorqueSweepConditions conditions;
    SweepClock::time_point interval_end;
};

[[nodiscard]] bool same_binary64(double lhs, double rhs) noexcept {
    return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
}

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool lowercase_hex(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::ostringstream text;
    for (const auto &issue : report.issues) {
        text << "\n  " << issue.path << ": " << issue.message;
    }
    return text.str();
}

[[nodiscard]] BmwM52b28TorqueSweepEvidenceError
error(std::string detail_code, std::string message,
      std::size_t point_index = kNoPoint) {
    return {std::move(detail_code), std::move(message), point_index};
}

void require(contract::ValidationReport &report, bool condition,
             contract::ContractIssueCode code, std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool warning_for_ratio(double ratio) noexcept {
    return ratio < kBmwM52b28GrossErrorWarningLowerRatio ||
           ratio > kBmwM52b28GrossErrorWarningUpperRatio;
}

[[nodiscard]] BmwM52b28TorqueSweepComparisons comparisons_for(
    const std::array<BmwM52b28TorqueSweepPointEvidence,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount> &points) {
    std::size_t maximum_torque_index = 0U;
    std::size_t maximum_power_index = 0U;
    for (std::size_t index = 1U; index < points.size(); ++index) {
        // Strict greater-than keeps the earlier/lower-RPM sample on an exact
        // binary64 tie because the input order is frozen ascending.
        if (points[index].net_shaft_cycle_mean_torque_nm >
            points[maximum_torque_index].net_shaft_cycle_mean_torque_nm) {
            maximum_torque_index = index;
        }
        if (points[index].mean_power_w > points[maximum_power_index].mean_power_w) {
            maximum_power_index = index;
        }
    }

    const auto &torque_landmark = points[4U];
    const auto &power_landmark = points[6U];
    const auto &maximum_torque = points[maximum_torque_index];
    const auto &maximum_power = points[maximum_power_index];
    return {
        torque_landmark.net_shaft_cycle_mean_torque_nm,
        torque_landmark.net_shaft_cycle_mean_torque_nm / kBmwM52b28NominalTorqueNm,
        power_landmark.mean_power_w,
        power_landmark.mean_power_w / kBmwM52b28NominalPowerW,
        maximum_torque.net_shaft_cycle_mean_torque_nm,
        maximum_torque.engine_speed_rpm,
        maximum_torque.net_shaft_cycle_mean_torque_nm / kBmwM52b28NominalTorqueNm,
        maximum_power.mean_power_w,
        maximum_power.engine_speed_rpm,
        maximum_power.mean_power_w / kBmwM52b28NominalPowerW,
        kBmwM52b28GrossErrorWarningLowerRatio,
        kBmwM52b28GrossErrorWarningUpperRatio,
    };
}

[[nodiscard]] std::vector<std::string>
warnings_for(const BmwM52b28TorqueSweepComparisons &comparisons) {
    std::vector<std::string> warnings;
    if (warning_for_ratio(comparisons.sampled_maximum_torque_to_280_ratio)) {
        warnings.emplace_back(kTorqueWarning);
    }
    if (warning_for_ratio(comparisons.sampled_maximum_power_to_142000_ratio)) {
        warnings.emplace_back(kPowerWarning);
    }
    return warnings;
}

[[nodiscard]] bool complete_available_term(const contract::TorqueValueNm &term,
                                           contract::TorqueTermMask included) {
    return finite(term.value_nm) &&
           term.availability == contract::Availability::available &&
           term.completeness == contract::Completeness::complete &&
           term.unavailable_reason == contract::QuantityUnavailableReason::none &&
           term.included_terms == included && term.omitted_terms == 0U;
}

[[nodiscard]] std::optional<contract::Sha256Digest>
model_record_digest(const contract::ProvenanceLedger &provenance) {
    std::optional<contract::Sha256Digest> result;
    for (const auto &source : provenance.evidence) {
        if (source.id != kModelRecordEvidenceId) {
            continue;
        }
        if (result.has_value() || !source.content_sha256.has_value()) {
            return std::nullopt;
        }
        result = source.content_sha256;
    }
    return result;
}

struct CanonicalPointBinding {
    std::string scenario_id;
    contract::Sha256Digest simulation_request_identity_v2_sha256;
    contract::Sha256Digest provenance_bundle_sha256;
    double engine_speed_rpm = 0.0;
    double throttle_01 = 0.0;
};

struct CanonicalEvidenceBindings {
    contract::Sha256Digest model_record_sha256;
    std::string engine_profile_id;
    BmwM52b28TorqueSweepConditions conditions;
    std::array<CanonicalPointBinding,
               profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
        points;
};

[[nodiscard]] bool
same_conditions_exact(const BmwM52b28TorqueSweepConditions &lhs,
                      const BmwM52b28TorqueSweepConditions &rhs) noexcept {
    return same_binary64(lhs.ambient_pressure_pa_abs, rhs.ambient_pressure_pa_abs) &&
           same_binary64(lhs.ambient_temperature_k, rhs.ambient_temperature_k) &&
           same_binary64(lhs.relative_humidity_01, rhs.relative_humidity_01) &&
           same_binary64(lhs.gas_temperature_k, rhs.gas_temperature_k) &&
           same_binary64(lhs.wall_temperature_k, rhs.wall_temperature_k) &&
           same_binary64(lhs.coolant_temperature_k, rhs.coolant_temperature_k) &&
           same_binary64(lhs.oil_temperature_k, rhs.oil_temperature_k) &&
           same_binary64(lhs.crankcase_pressure_pa_abs,
                         rhs.crankcase_pressure_pa_abs) &&
           same_binary64(lhs.crankcase_temperature_k, rhs.crankcase_temperature_k) &&
           same_binary64(lhs.total_displacement_m3, rhs.total_displacement_m3) &&
           lhs.fuel_id == rhs.fuel_id &&
           same_binary64(lhs.lower_heating_value_j_per_kg,
                         rhs.lower_heating_value_j_per_kg) &&
           same_binary64(lhs.stoichiometric_air_fuel_mass_ratio,
                         rhs.stoichiometric_air_fuel_mass_ratio) &&
           lhs.accessory_configuration_id == rhs.accessory_configuration_id &&
           lhs.accessory_configuration_sha256 == rhs.accessory_configuration_sha256 &&
           same_binary64(lhs.initial_theta_rad, rhs.initial_theta_rad) &&
           same_binary64(lhs.throttle_01, rhs.throttle_01) &&
           lhs.public_seed == rhs.public_seed &&
           lhs.physics_rate_numerator == rhs.physics_rate_numerator &&
           lhs.physics_rate_denominator == rhs.physics_rate_denominator &&
           lhs.convergence_method_id == rhs.convergence_method_id &&
           lhs.convergence_method_version == rhs.convergence_method_version &&
           lhs.convergence_method_configuration_sha256 ==
               rhs.convergence_method_configuration_sha256 &&
           lhs.comparison_cycle_count == rhs.comparison_cycle_count &&
           lhs.cutoff_frame == rhs.cutoff_frame &&
           lhs.tail_frame_count == rhs.tail_frame_count;
}

[[nodiscard]] std::optional<BmwM52b28TorqueSweepConditions> conditions_from_request(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request) {
    const auto *profile = std::get_if<contract::LowOrderOperatingPointV1Profile>(
        &request.engine.physics_profile);
    const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
    const auto *preparation =
        std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
    const auto cutoff_frame =
        preparation == nullptr ? std::optional<std::uint64_t>{}
                               : contract::resolve_frame_index(
                                     preparation->maximum_preparation_duration_s.value,
                                     request.scenario.rates.physics);
    const auto total_frame = contract::resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.physics);
    if (profile == nullptr || held == nullptr || preparation == nullptr ||
        !cutoff_frame.has_value() || !total_frame.has_value() ||
        *total_frame < *cutoff_frame) {
        return std::nullopt;
    }
    return BmwM52b28TorqueSweepConditions{
        request.scenario.ambient.pressure_pa_abs.value,
        request.scenario.ambient.temperature_k.value,
        request.scenario.ambient.relative_humidity_01.value,
        request.scenario.initial_thermal_state.gas_temperature_k.value,
        request.scenario.initial_thermal_state.wall_temperature_k.value,
        request.scenario.initial_thermal_state.coolant_temperature_k.value,
        request.scenario.initial_thermal_state.oil_temperature_k.value,
        request.scenario.crankcase.pressure_pa_abs.value,
        request.scenario.crankcase.temperature_k.value,
        request.engine.total_displacement_m3.value,
        request.scenario.fuel.fuel_id.value,
        request.scenario.fuel.lower_heating_value_j_per_kg.value,
        request.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
        profile->accessory_configuration.configuration_id.value,
        profile->accessory_configuration.content_sha256.value,
        held->initial_theta_rad.value,
        held->throttle_01.value,
        request.scenario.public_seed.value,
        request.scenario.rates.physics.numerator,
        request.scenario.rates.physics.denominator,
        preparation->method.value.id,
        preparation->method.value.version,
        preparation->method.value.configuration_sha256,
        preparation->comparison_cycle_count.value,
        *cutoff_frame,
        *total_frame - *cutoff_frame,
    };
}

using CanonicalEvidenceBindingsResult =
    std::variant<CanonicalEvidenceBindings, contract::ValidationReport>;

[[nodiscard]] CanonicalEvidenceBindingsResult canonical_evidence_bindings() {
    auto request_result =
        profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    if (auto *report = std::get_if<contract::ValidationReport>(&request_result)) {
        return std::move(*report);
    }
    const auto &requests =
        std::get<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(request_result);
    contract::ValidationReport report;
    const auto model_digest = model_record_digest(requests.front().provenance);
    const auto conditions = conditions_from_request(requests.front());
    if (!model_digest.has_value() || !conditions.has_value()) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "canonical_bmw_torque_sweep",
                   "canonical request factory did not expose complete evidence "
                   "bindings");
        return report;
    }

    CanonicalEvidenceBindings bindings{
        *model_digest,
        requests.front().engine.profile_id.value,
        *conditions,
        {},
    };
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const auto &request = requests[index];
        const auto identity_result = identity::encode_simulation_request_identity_v2(
            request.engine, request.scenario, request.provenance.bundle);
        const auto *identity_encoding =
            std::get_if<identity::SimulationRequestIdentityEncoding>(&identity_result);
        if (identity_encoding == nullptr) {
            const auto &identity_error =
                std::get<identity::SimulationRequestIdentityError>(identity_result);
            report.add(contract::ContractIssueCode::invalid_value,
                       "canonical_bmw_torque_sweep.points[" + std::to_string(index) +
                           "].simulation_request_v2_sha256",
                       "canonical request identity encoding failed: " +
                           identity_error.detail_code + ": " + identity_error.message);
            continue;
        }
        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        if (held == nullptr) {
            report.add(contract::ContractIssueCode::inconsistent_semantics,
                       "canonical_bmw_torque_sweep.points[" + std::to_string(index) +
                           "].mode",
                       "canonical request factory returned a non-held point");
            continue;
        }
        bindings.points[index] = {
            request.scenario.scenario_id,     identity_encoding->sha256,
            request.provenance.bundle.sha256, held->engine_speed_rpm.value,
            held->throttle_01.value,
        };
    }
    if (!report.ok()) {
        return report;
    }
    return bindings;
}

[[nodiscard]] std::variant<ExecutedPoint, BmwM52b28TorqueSweepEvidenceError>
run_point(const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
          const contract::Sha256Digest &request_identity, std::size_t point_index) {
    const auto interval_started = SweepClock::now();
    auto compilation = simulation::compile_low_order_capture_session(
        request.engine, request.scenario, request_identity);
    if (const auto *report = std::get_if<contract::ValidationReport>(&compilation)) {
        return error("bmw-torque-sweep-point-compile-rejected",
                     "held-speed capture compilation failed" + validation_text(*report),
                     point_index);
    }
    auto session = std::move(std::get<simulation::LowOrderCaptureSession>(compilation));

    while (true) {
        contract::ValidationReport block_validation;
        auto advanced =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                block_validation =
                    contract::validate(block, request.engine, request.scenario);
                return block_validation.ok();
            });
        if (!block_validation.ok()) {
            return error("bmw-torque-sweep-capture-block-invalid",
                         "request-bound capture block validation failed" +
                             validation_text(block_validation),
                         point_index);
        }
        if (const auto *failure = std::get_if<contract::FailureContext>(&advanced)) {
            return error(failure->detail_code, failure->state_summary, point_index);
        }
        if (std::holds_alternative<simulation::LowOrderCaptureBlockPublished>(
                advanced)) {
            continue;
        }

        const auto &completed =
            std::get<simulation::LowOrderCaptureCompleted>(advanced);
        if (completed.inertial_dyno.has_value() ||
            !completed.held_speed_operating_point.has_value()) {
            return error("bmw-torque-sweep-completion-result-missing",
                         "completed held-speed session did not return exactly one "
                         "held operating-point result",
                         point_index);
        }
        const auto &operating = *completed.held_speed_operating_point;
        const auto result_report = contract::validate(operating, request.scenario,
                                                      request.engine, request_identity);
        if (!result_report.ok()) {
            return error("bmw-torque-sweep-operating-point-invalid",
                         "request-bound held operating-point result is invalid" +
                             validation_text(result_report),
                         point_index);
        }

        const auto &block = operating.reported_block();
        const auto &torque = block.cycle_mean_torque;
        if (!complete_available_term(torque.indicated_gas,
                                     contract::indicated_gas_torque_term_mask()) ||
            !complete_available_term(
                torque.aggregate_loss,
                contract::friction_pump_and_accessory_torque_term_mask()) ||
            !complete_available_term(
                torque.starter,
                contract::torque_term_mask(contract::TorqueTerm::starter)) ||
            !complete_available_term(torque.net_shaft,
                                     contract::known_torque_term_mask())) {
            return error("bmw-torque-sweep-complete-torque-term-missing",
                         "reported operating point does not retain every complete "
                         "cycle-mean torque term",
                         point_index);
        }

        const auto interval_end = SweepClock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 interval_end - interval_started)
                                 .count();
        if (elapsed < 0) {
            return error("bmw-torque-sweep-clock-regressed",
                         "steady clock produced a negative point duration",
                         point_index);
        }
        const auto *preparation =
            std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        const auto total_frame = contract::resolve_frame_index(
            request.scenario.total_duration_s.value, request.scenario.rates.physics);
        if (preparation == nullptr || held == nullptr || !total_frame.has_value() ||
            *total_frame < kCutoffFrame) {
            return error("bmw-torque-sweep-condition-extraction-failed",
                         "validated request did not expose its frozen held timing",
                         point_index);
        }

        BmwM52b28TorqueSweepConditions conditions{
            operating.conditions.ambient.pressure_pa_abs,
            operating.conditions.ambient.temperature_k,
            operating.conditions.ambient.relative_humidity_01,
            operating.conditions.initial_thermal_state.gas_temperature_k,
            operating.conditions.initial_thermal_state.wall_temperature_k,
            operating.conditions.initial_thermal_state.coolant_temperature_k,
            operating.conditions.initial_thermal_state.oil_temperature_k,
            operating.conditions.crankcase.pressure_pa_abs,
            operating.conditions.crankcase.temperature_k,
            operating.conditions.total_displacement_m3,
            operating.conditions.fuel.fuel_id,
            operating.conditions.fuel.lower_heating_value_j_per_kg,
            operating.conditions.fuel.stoichiometric_air_fuel_mass_ratio,
            operating.conditions.accessory_configuration.configuration_id,
            operating.conditions.accessory_configuration.content_sha256,
            held->initial_theta_rad.value,
            held->throttle_01.value,
            request.scenario.public_seed.value,
            request.scenario.rates.physics.numerator,
            request.scenario.rates.physics.denominator,
            operating.convergence.method.id,
            operating.convergence.method.version,
            operating.convergence.method.configuration_sha256,
            operating.convergence.comparison_cycle_count,
            kCutoffFrame,
            *total_frame - kCutoffFrame,
        };
        BmwM52b28TorqueSweepPointEvidence point{
            request_identity,
            request.provenance.bundle.sha256,
            request.scenario.scenario_id,
            operating.conditions.engine_speed_rpm,
            operating.conditions.throttle_01,
            torque.indicated_gas.value_nm,
            torque.aggregate_loss.value_nm,
            torque.starter.value_nm,
            torque.net_shaft.value_nm,
            block.net_bmep_pa,
            block.mean_power_w,
            operating.convergence.torque_residual_nm,
            operating.convergence.torque_tolerance_nm,
            operating.convergence.pressure_residual_pa,
            operating.convergence.pressure_tolerance_pa,
            operating.convergence.block_a.cycles.first_completed_cycle_ordinal,
            operating.convergence.block_a.cycles.last_completed_cycle_ordinal,
            operating.convergence.block_b.cycles.first_completed_cycle_ordinal,
            operating.convergence.block_b.cycles.last_completed_cycle_ordinal,
            operating.applicability_label,
            static_cast<std::uint64_t>(elapsed),
        };
        return ExecutedPoint{
            std::move(point),
            std::move(conditions),
            interval_end,
        };
    }
}

} // namespace

contract::ValidationReport validate_bmw_m52b28_torque_sweep_evidence(
    const BmwM52b28TorqueSweepEvidence &evidence) {
    contract::ValidationReport report;
    auto binding_result = canonical_evidence_bindings();
    const auto *bindings = std::get_if<CanonicalEvidenceBindings>(&binding_result);
    if (bindings == nullptr) {
        report.append(std::move(std::get<contract::ValidationReport>(binding_result)));
        return report;
    }
    require(report,
            evidence.source.source_state == determinism::RendererSourceState::clean &&
                (evidence.source.full_git_head.size() == 40U ||
                 evidence.source.full_git_head.size() == 64U) &&
                lowercase_hex(evidence.source.full_git_head) &&
                !evidence.source.source_closure_sha256.is_zero() &&
                !evidence.source.compiler_id.empty() &&
                !evidence.source.compiler_version.empty() &&
                !evidence.source.target_triple.empty(),
            contract::ContractIssueCode::invalid_value, "source_commit",
            "evidence requires one complete clean build-owned source stamp");
    require(report, !evidence.model_record_sha256.is_zero(),
            contract::ContractIssueCode::invalid_value, "model_record_sha256",
            "evidence requires the operating-point model record identity");
    require(report, evidence.model_record_sha256 == bindings->model_record_sha256,
            contract::ContractIssueCode::inconsistent_semantics, "model_record_sha256",
            "evidence model record does not match the canonical request factory");
    require(report, contract::is_valid_semantic_id(evidence.engine_profile_id),
            contract::ContractIssueCode::invalid_value, "engine_profile_id",
            "evidence requires a canonical engine profile ID");
    require(report, evidence.engine_profile_id == bindings->engine_profile_id,
            contract::ContractIssueCode::inconsistent_semantics, "engine_profile_id",
            "evidence engine profile does not match the canonical request factory");

    const auto &conditions = evidence.conditions;
    const auto expected_method =
        contract::adjacent_cycle_block_mean_convergence_method_identity();
    require(report,
            finite(conditions.ambient_pressure_pa_abs) &&
                same_binary64(conditions.ambient_pressure_pa_abs, 101325.0) &&
                finite(conditions.ambient_temperature_k) &&
                same_binary64(conditions.ambient_temperature_k, 298.15) &&
                same_binary64(conditions.relative_humidity_01, 0.0) &&
                same_binary64(conditions.gas_temperature_k, 298.15) &&
                same_binary64(conditions.wall_temperature_k, 363.15) &&
                same_binary64(conditions.coolant_temperature_k, 363.15) &&
                same_binary64(conditions.oil_temperature_k, 363.15) &&
                same_binary64(conditions.crankcase_pressure_pa_abs, 101325.0) &&
                same_binary64(conditions.crankcase_temperature_k, 298.15) &&
                finite(conditions.total_displacement_m3) &&
                conditions.total_displacement_m3 > 0.0 &&
                contract::is_valid_semantic_id(conditions.fuel_id) &&
                finite(conditions.lower_heating_value_j_per_kg) &&
                conditions.lower_heating_value_j_per_kg > 0.0 &&
                finite(conditions.stoichiometric_air_fuel_mass_ratio) &&
                conditions.stoichiometric_air_fuel_mass_ratio > 0.0 &&
                contract::is_valid_semantic_id(conditions.accessory_configuration_id) &&
                !conditions.accessory_configuration_sha256.is_zero() &&
                finite(conditions.initial_theta_rad) &&
                same_binary64(conditions.throttle_01, 1.0) &&
                conditions.public_seed == UINT64_C(0xC0FFEE) &&
                conditions.physics_rate_numerator == UINT64_C(10000) &&
                conditions.physics_rate_denominator == UINT64_C(1) &&
                conditions.convergence_method_id == expected_method.id &&
                conditions.convergence_method_version == expected_method.version &&
                conditions.convergence_method_configuration_sha256 ==
                    expected_method.configuration_sha256 &&
                conditions.comparison_cycle_count == kComparisonCycleCount &&
                conditions.cutoff_frame == kCutoffFrame &&
                conditions.tail_frame_count == kTailFrameCount,
            contract::ContractIssueCode::inconsistent_semantics, "conditions",
            "common conditions do not match the frozen canonical BMW sweep");
    require(report, same_conditions_exact(conditions, bindings->conditions),
            contract::ContractIssueCode::inconsistent_semantics, "conditions",
            "common conditions do not exactly match the canonical request factory");

    std::uint64_t summed_point_elapsed_ns = 0U;
    bool elapsed_sum_representable = true;
    for (std::size_t index = 0U; index < evidence.points.size(); ++index) {
        const auto &point = evidence.points[index];
        const auto &binding = bindings->points[index];
        const auto path = "points[" + std::to_string(index) + "]";
        require(report,
                !point.simulation_request_identity_v2_sha256.is_zero() &&
                    !point.provenance_bundle_sha256.is_zero(),
                contract::ContractIssueCode::invalid_value, path,
                "sweep point requires nonzero request and provenance digests");
        for (std::size_t prior = 0U; prior < index; ++prior) {
            require(report,
                    point.simulation_request_identity_v2_sha256 !=
                        evidence.points[prior].simulation_request_identity_v2_sha256,
                    contract::ContractIssueCode::duplicate_identity,
                    path + ".simulation_request_v2_sha256",
                    "independent sweep points must have distinct request digests");
        }
        require(report,
                point.simulation_request_identity_v2_sha256 ==
                        binding.simulation_request_identity_v2_sha256 &&
                    point.provenance_bundle_sha256 ==
                        binding.provenance_bundle_sha256 &&
                    point.scenario_id == binding.scenario_id &&
                    same_binary64(point.engine_speed_rpm, binding.engine_speed_rpm) &&
                    same_binary64(point.throttle_01, binding.throttle_01),
                contract::ContractIssueCode::inconsistent_semantics, path,
                "point does not retain its exact canonical request identity, "
                "provenance, scenario, RPM, and throttle");
        require(report,
                finite(point.indicated_gas_cycle_mean_torque_nm) &&
                    finite(point.aggregate_loss_cycle_mean_torque_nm) &&
                    finite(point.starter_cycle_mean_torque_nm) &&
                    finite(point.net_shaft_cycle_mean_torque_nm) &&
                    finite(point.net_bmep_pa) && finite(point.mean_power_w) &&
                    finite(point.torque_residual_nm) &&
                    finite(point.torque_tolerance_nm) &&
                    finite(point.pressure_residual_pa) &&
                    finite(point.pressure_tolerance_pa),
                contract::ContractIssueCode::invalid_value, path,
                "all retained point quantities must be finite");
        require(report,
                point.aggregate_loss_cycle_mean_torque_nm < 0.0 &&
                    same_binary64(point.starter_cycle_mean_torque_nm, 0.0) &&
                    point.torque_residual_nm >= 0.0 &&
                    same_binary64(point.torque_tolerance_nm, 0.75) &&
                    point.torque_residual_nm <= point.torque_tolerance_nm &&
                    point.pressure_residual_pa >= 0.0 &&
                    same_binary64(point.pressure_tolerance_pa, 1500.0) &&
                    point.pressure_residual_pa <= point.pressure_tolerance_pa,
                contract::ContractIssueCode::inconsistent_semantics, path,
                "loss/starter semantics or frozen inclusive convergence "
                "tolerances are not satisfied");
        const bool a_representable =
            point.block_a_first_cycle <=
            std::numeric_limits<std::uint64_t>::max() - (kComparisonCycleCount - 1U);
        const bool b_representable =
            point.block_b_first_cycle <=
            std::numeric_limits<std::uint64_t>::max() - (kComparisonCycleCount - 1U);
        require(report,
                a_representable && b_representable &&
                    point.block_a_last_cycle ==
                        point.block_a_first_cycle + (kComparisonCycleCount - 1U) &&
                    point.block_b_last_cycle ==
                        point.block_b_first_cycle + (kComparisonCycleCount - 1U) &&
                    point.block_a_last_cycle <
                        std::numeric_limits<std::uint64_t>::max() &&
                    point.block_b_first_cycle == point.block_a_last_cycle + 1U,
                contract::ContractIssueCode::inconsistent_semantics, path,
                "point must retain two adjacent 16-complete-cycle ranges");
        require(report,
                point.applicability_label ==
                    contract::kGenericChenFlynnLowOrderModelPredictionApplicability,
                contract::ContractIssueCode::inconsistent_semantics,
                path + ".applicability_label",
                "point must retain the generic low-order prediction label");
        if (point.elapsed_ns >
            std::numeric_limits<std::uint64_t>::max() - summed_point_elapsed_ns) {
            elapsed_sum_representable = false;
        } else {
            summed_point_elapsed_ns += point.elapsed_ns;
        }
    }

    require(report,
            elapsed_sum_representable &&
                evidence.total_elapsed_ns >= summed_point_elapsed_ns,
            contract::ContractIssueCode::inconsistent_semantics,
            "execution.total_elapsed_ns",
            "sequential total duration must cover all nine point intervals");
    const auto expected_comparisons = comparisons_for(evidence.points);
    require(report, evidence.comparisons == expected_comparisons,
            contract::ContractIssueCode::inconsistent_semantics, "comparisons",
            "landmark ratios, sampled maxima, lower-RPM tie-break, or tripwire "
            "bounds were not derived exactly from the nine ascending points");
    require(report, evidence.warnings == warnings_for(expected_comparisons),
            contract::ContractIssueCode::inconsistent_semantics, "warnings",
            "warnings must be the exact stable torque-then-power tripwire set");
    return report;
}

BmwM52b28TorqueSweepEvidenceResult run_bmw_m52b28_full_throttle_torque_sweep(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequestSet &requests) {
    try {
        auto source_result = determinism::renderer_source_stamp();
        const auto *source =
            std::get_if<determinism::RendererSourceStamp>(&source_result);
        if (source == nullptr) {
            const auto &source_error =
                std::get<determinism::RendererSourceStampError>(source_result);
            return error("bmw-torque-sweep-source-stamp-unavailable",
                         "clean build-owned source stamp unavailable: " +
                             source_error.message);
        }
        const auto request_report =
            profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request_set(
                requests);
        if (!request_report.ok()) {
            return error("bmw-torque-sweep-request-set-invalid",
                         "canonical request set validation failed" +
                             validation_text(request_report));
        }
        const auto model_digest = model_record_digest(requests.front().provenance);
        if (!model_digest.has_value() || model_digest->is_zero()) {
            return error("bmw-torque-sweep-model-record-missing",
                         "canonical provenance does not contain exactly one model "
                         "record digest");
        }

        BmwM52b28TorqueSweepEvidence evidence;
        evidence.source = *source;
        evidence.model_record_sha256 = *model_digest;
        evidence.engine_profile_id = requests.front().scenario.engine_profile_id;
        std::optional<SweepClock::time_point> total_started;
        std::optional<SweepClock::time_point> total_ended;
        for (std::size_t index = 0U; index < requests.size(); ++index) {
            if (index != 0U &&
                model_record_digest(requests[index].provenance) != model_digest) {
                return error("bmw-torque-sweep-model-record-mismatch",
                             "point provenance does not retain the common model "
                             "record identity",
                             index);
            }
            if (index == 0U) {
                // The total interval deliberately includes point-zero request
                // identity encoding, matching the frozen evidence contract.
                total_started = SweepClock::now();
            }
            const auto identity_result =
                identity::encode_simulation_request_identity_v2(
                    requests[index].engine, requests[index].scenario,
                    requests[index].provenance.bundle);
            const auto *identity_encoding =
                std::get_if<identity::SimulationRequestIdentityEncoding>(
                    &identity_result);
            if (identity_encoding == nullptr) {
                const auto &identity_error =
                    std::get<identity::SimulationRequestIdentityError>(identity_result);
                return error(identity_error.detail_code, identity_error.message, index);
            }

            auto executed_result =
                run_point(requests[index], identity_encoding->sha256, index);
            if (auto *executed = std::get_if<ExecutedPoint>(&executed_result)) {
                if (index == 0U) {
                    evidence.conditions = executed->conditions;
                } else if (executed->conditions != evidence.conditions) {
                    return error("bmw-torque-sweep-common-condition-mismatch",
                                 "independent result does not retain the common "
                                 "frozen conditions",
                                 index);
                }
                evidence.points[index] = std::move(executed->evidence);
                total_ended = executed->interval_end;
            } else {
                return std::move(
                    std::get<BmwM52b28TorqueSweepEvidenceError>(executed_result));
            }
        }
        if (!total_started.has_value() || !total_ended.has_value()) {
            return error("bmw-torque-sweep-timing-missing",
                         "sequential runner did not retain its total interval");
        }
        const auto total_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       *total_ended - *total_started)
                                       .count();
        if (total_elapsed < 0) {
            return error("bmw-torque-sweep-clock-regressed",
                         "steady clock produced a negative total duration");
        }
        evidence.total_elapsed_ns = static_cast<std::uint64_t>(total_elapsed);
        evidence.comparisons = comparisons_for(evidence.points);
        evidence.warnings = warnings_for(evidence.comparisons);

        const auto evidence_report =
            validate_bmw_m52b28_torque_sweep_evidence(evidence);
        if (!evidence_report.ok()) {
            return error("bmw-torque-sweep-evidence-invalid",
                         "assembled evidence record failed validation" +
                             validation_text(evidence_report));
        }
        return evidence;
    } catch (const std::exception &exception) {
        return error("bmw-torque-sweep-runner-threw",
                     "torque-sweep runner threw: " + std::string{exception.what()});
    } catch (...) {
        return error("bmw-torque-sweep-runner-threw",
                     "torque-sweep runner threw a non-standard exception");
    }
}

} // namespace engine_sim_offline::reference
