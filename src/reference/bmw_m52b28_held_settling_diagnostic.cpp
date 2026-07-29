#include "reference/bmw_m52b28_held_settling_diagnostic.hpp"

#include "engine_sim_offline/profiles/bmw_m52b28_full_throttle_torque_sweep_request.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <locale>
#include <numbers>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::reference {
namespace {

constexpr std::size_t kPointCount =
    profiles::kBmwM52b28FullThrottleTorqueSweepPointCount;
constexpr std::uint32_t kCyclesPerBlock = 16U;
constexpr std::size_t kCycleCount = 2U * kCyclesPerBlock;
constexpr double kTorqueFloorNm = 0.75;
constexpr double kPressureFloorPa = 1500.0;
constexpr double kAudibleDurationS = 0.02;
constexpr double kFourStrokeCycleRadians =
    4.0 * std::numbers::pi_v<double>;

struct Horizon {
    double cutoff_s = 0.0;
    double total_s = 0.0;
    std::uint64_t cutoff_frame = 0U;
    std::uint64_t total_frame = 0U;
    std::string scenario_id;
};

struct PointPlan {
    std::size_t point_index = 0U;
    double rpm = 0.0;
    std::array<Horizon, 3U> horizons;
    std::array<profiles::BmwM52b28FullThrottleTorqueSweepRequest, 3U> requests;
    std::array<contract::Sha256Digest, 3U> request_identities;
};

struct PhysicalVolume {
    contract::GasVolumeId id;
    std::string semantic_id;
};

struct CyclePressure {
    contract::GasVolumeId id;
    double pressure_pa_abs = 0.0;
};

struct DiagnosticCycle {
    std::uint64_t ordinal = 0U;
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    std::vector<CyclePressure> pressures;
};

struct DiagnosticWindow {
    Horizon horizon;
    contract::Sha256Digest request_identity;
    bool old_v1_gate_passed = false;
    std::uint64_t block_a_first_ordinal = 0U;
    std::uint64_t block_a_last_ordinal = 0U;
    std::uint64_t block_b_first_ordinal = 0U;
    std::uint64_t block_b_last_ordinal = 0U;
    std::vector<PhysicalVolume> volumes;
    std::vector<DiagnosticCycle> cycles;
};

struct ScalarSummary {
    double mean = 0.0;
    double population_rms_dispersion = 0.0;
};

struct VolumeSummary {
    PhysicalVolume volume;
    ScalarSummary pressure;
};

struct WindowSummary {
    double total_indicated_gas_work_j = 0.0;
    double total_positive_aggregate_loss_work_j = 0.0;
    double total_starter_work_j = 0.0;
    double total_brake_work_j = 0.0;
    std::array<double, kCycleCount> cycle_brake_torque_nm{};
    ScalarSummary torque;
    std::vector<VolumeSummary> pressures;
};

struct ScalarComparison {
    double delta = 0.0;
    double envelope = 0.0;
    bool inside = false;
};

struct VolumeComparison {
    PhysicalVolume volume;
    ScalarComparison pressure;
};

struct WindowComparison {
    ScalarComparison torque;
    std::vector<VolumeComparison> pressures;
    bool inside = false;
};

struct PointResult {
    std::size_t point_index = 0U;
    double rpm = 0.0;
    DiagnosticWindow early;
    DiagnosticWindow later;
    WindowSummary early_summary;
    WindowSummary later_summary;
    WindowComparison early_to_later;
    std::optional<DiagnosticWindow> extension;
    std::optional<WindowSummary> extension_summary;
    std::optional<WindowComparison> later_to_extension;
};

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error{std::move(message)};
}

[[nodiscard]] std::uint64_t bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        result += "\n  ";
        result += issue.path;
        result += ": ";
        result += issue.message;
    }
    return result;
}

[[nodiscard]] std::string exact_binary64(double value) {
    std::array<char, 64U> decimal{};
    const auto converted =
        std::to_chars(decimal.data(), decimal.data() + decimal.size(), value,
                      std::chars_format::general,
                      std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{}) {
        fail("binary64 diagnostic decimal conversion failed");
    }
    return std::string{decimal.data(), converted.ptr} +
           " [binary64=" + std::to_string(bits(value)) + "]";
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string result(64U, '0');
    for (std::size_t index = 0U; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kHex[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kHex[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::string integer_rpm_text(double rpm) {
    const auto integer = static_cast<std::uint64_t>(rpm);
    if (static_cast<double>(integer) != rpm) {
        fail("canonical diagnostic RPM is not an exact nonnegative integer");
    }
    std::array<char, 32U> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), integer);
    if (converted.ec != std::errc{}) {
        fail("diagnostic RPM formatting failed");
    }
    return std::string{buffer.data(), converted.ptr};
}

[[nodiscard]] std::string later_scenario_id(double rpm,
                                            std::uint64_t cutoff_frame) {
    return "bmw-m52b28-held-" + integer_rpm_text(rpm) +
           "rpm-full-throttle-fixed-sample-diagnostic-" +
           std::to_string(cutoff_frame) + "f-v1";
}

[[nodiscard]] std::vector<PhysicalVolume>
physical_volumes(const contract::EngineSpec &engine) {
    std::vector<PhysicalVolume> result;
    result.reserve(engine.gas_volumes.size());
    for (const auto &volume : engine.gas_volumes) {
        if (volume.kind.value != contract::GasVolumeKind::atmosphere) {
            result.push_back({volume.id, volume.semantic_id.value});
        }
    }
    std::ranges::sort(result, {}, &PhysicalVolume::id);
    if (result.empty()) {
        fail("diagnostic engine has no physical gas volumes");
    }
    for (std::size_t index = 1U; index < result.size(); ++index) {
        if (!(result[index - 1U].id < result[index].id)) {
            fail("diagnostic physical gas-volume identities are not unique");
        }
    }
    return result;
}

void mutate_horizon(
    profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
    const Horizon &horizon) {
    auto *preparation =
        std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
    if (preparation == nullptr) {
        fail("canonical torque-sweep request lost convergence preparation");
    }
    request.scenario.scenario_id = horizon.scenario_id;
    preparation->maximum_preparation_duration_s.value = horizon.cutoff_s;
    request.scenario.audible_start_s.value = horizon.cutoff_s;
    request.scenario.total_duration_s.value = horizon.total_s;
}

[[nodiscard]] contract::Sha256Digest request_identity(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request) {
    auto encoded = identity::encode_simulation_request_identity_v2(
        request.engine, request.scenario, request.provenance.bundle);
    if (const auto *error =
            std::get_if<identity::SimulationRequestIdentityError>(&encoded)) {
        fail("diagnostic request-v2 identity failed: " + error->detail_code +
             ": " + error->message);
    }
    return std::get<identity::SimulationRequestIdentityEncoding>(std::move(encoded))
        .sha256;
}

void validate_request(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &canonical,
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
    const Horizon &horizon, double rpm) {
    auto expected = canonical;
    mutate_horizon(expected, horizon);
    if (request != expected) {
        fail("diagnostic request changed a field outside the four frozen horizon "
             "fields");
    }

    contract::ValidationReport report;
    report.append(contract::validate(request.provenance));
    report.append(contract::validate(request.engine, request.provenance));
    report.append(contract::validate(request.scenario, request.provenance));
    report.append(contract::validate_for_engine(request.scenario, request.engine));
    report.append(contract::validate_clock_grid(request.scenario));
    if (!report.ok()) {
        fail("diagnostic request failed validation" + validation_text(report));
    }

    const auto *preparation =
        std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
    const auto *held =
        std::get_if<contract::HeldSpeed>(&request.scenario.mode);
    if (preparation == nullptr || held == nullptr ||
        bits(held->engine_speed_rpm.value) != bits(rpm) ||
        preparation->comparison_cycle_count.value != kCyclesPerBlock ||
        bits(preparation->maximum_preparation_duration_s.value) !=
            bits(horizon.cutoff_s) ||
        bits(preparation->cycle_mean_torque_tolerance_nm.value) !=
            bits(kTorqueFloorNm) ||
        bits(preparation->pressure_tolerance_pa.value) != bits(kPressureFloorPa) ||
        request.scenario.scenario_id != horizon.scenario_id ||
        bits(request.scenario.audible_start_s.value) != bits(horizon.cutoff_s) ||
        bits(request.scenario.audible_duration_s.value) !=
            bits(kAudibleDurationS) ||
        bits(request.scenario.total_duration_s.value) != bits(horizon.total_s) ||
        contract::resolve_frame_index(horizon.cutoff_s,
                                      request.scenario.rates.physics) !=
            horizon.cutoff_frame ||
        contract::resolve_frame_index(horizon.total_s,
                                      request.scenario.rates.physics) !=
            horizon.total_frame) {
        fail("diagnostic request disagrees with its frozen RPM, cutoff, or sample "
             "policy");
    }
}

template <class PressureRange>
[[nodiscard]] std::vector<CyclePressure>
copy_pressures(const PressureRange &source,
               const std::vector<PhysicalVolume> &expected) {
    if (source.size() != expected.size()) {
        fail("diagnostic cycle pressure inventory has the wrong size");
    }
    std::vector<CyclePressure> result;
    result.reserve(source.size());
    for (std::size_t index = 0U; index < source.size(); ++index) {
        if (source[index].gas_volume_id != expected[index].id ||
            !std::isfinite(source[index].pressure_pa_abs) ||
            !(source[index].pressure_pa_abs > 0.0)) {
            fail("diagnostic cycle pressure inventory is malformed or reordered");
        }
        result.push_back(
            {source[index].gas_volume_id, source[index].pressure_pa_abs});
    }
    return result;
}

void validate_window_cycles(const DiagnosticWindow &window) {
    if (window.cycles.size() != kCycleCount ||
        window.block_a_first_ordinal == 0U ||
        window.block_a_last_ordinal - window.block_a_first_ordinal + 1U !=
            kCyclesPerBlock ||
        window.block_b_first_ordinal != window.block_a_last_ordinal + 1U ||
        window.block_b_last_ordinal - window.block_b_first_ordinal + 1U !=
            kCyclesPerBlock) {
        fail("diagnostic evidence is not one adjacent two-by-16-cycle window");
    }
    for (std::size_t index = 0U; index < window.cycles.size(); ++index) {
        const auto &cycle = window.cycles[index];
        if (cycle.ordinal != window.block_a_first_ordinal + index ||
            cycle.pressures.size() != window.volumes.size() ||
            !std::isfinite(cycle.indicated_gas_work_j) ||
            !std::isfinite(cycle.positive_aggregate_loss_work_j) ||
            !std::isfinite(cycle.starter_work_j) ||
            !std::isfinite(cycle.brake_work_j)) {
            fail("diagnostic completed-cycle evidence is malformed or unordered");
        }
    }
}

[[nodiscard]] DiagnosticWindow from_terminal_error(
    const Horizon &horizon, const contract::EngineSpec &engine,
    const contract::Sha256Digest &identity,
    const simulation::AdjacentCycleBlockConvergenceError &error) {
    if (error.code !=
            simulation::AdjacentCycleBlockConvergenceErrorCode::nonconverged ||
        error.retained_cycle_count != kCycleCount ||
        error.required_cycle_count != kCycleCount || !error.evidence.has_value()) {
        fail("diagnostic terminal failure omitted the required typed 32-cycle "
             "evidence");
    }
    const auto &evidence = *error.evidence;
    if (evidence.cycles_per_block != kCyclesPerBlock || evidence.settled ||
        bits(evidence.fixed_cutoff_time_s) != bits(horizon.cutoff_s) ||
        bits(evidence.cycle_mean_torque_tolerance_nm) != bits(kTorqueFloorNm) ||
        bits(evidence.pressure_tolerance_pa) != bits(kPressureFloorPa)) {
        fail("diagnostic terminal evidence disagrees with its frozen policy");
    }

    DiagnosticWindow result{
        horizon,
        identity,
        false,
        evidence.block_a.range.first_cycle_ordinal,
        evidence.block_a.range.last_cycle_ordinal,
        evidence.block_b.range.first_cycle_ordinal,
        evidence.block_b.range.last_cycle_ordinal,
        physical_volumes(engine),
        {},
    };
    const auto append = [&](const simulation::AdjacentCycleBlockMean &block) {
        if (block.completed_cycles.size() != kCyclesPerBlock) {
            fail("diagnostic terminal block has the wrong cycle count");
        }
        for (const auto &cycle : block.completed_cycles) {
            result.cycles.push_back({
                cycle.completed_cycle_ordinal,
                cycle.indicated_gas_work_j,
                cycle.positive_aggregate_loss_work_j,
                cycle.starter_work_j,
                cycle.brake_work_j,
                copy_pressures(cycle.end_boundary_pressures, result.volumes),
            });
        }
    };
    result.cycles.reserve(kCycleCount);
    append(evidence.block_a);
    append(evidence.block_b);
    validate_window_cycles(result);
    return result;
}

[[nodiscard]] DiagnosticWindow from_success(
    const Horizon &horizon, const contract::EngineSpec &engine,
    const contract::RenderScenario &scenario,
    const contract::Sha256Digest &identity,
    const contract::HeldSpeedOperatingPointResult &operating_point) {
    const auto result_report =
        contract::validate(operating_point, scenario, engine, identity);
    if (!result_report.ok()) {
        fail("diagnostic successful result failed request-bound validation" +
             validation_text(result_report));
    }
    const auto &evidence = operating_point.convergence;
    if (evidence.comparison_cycle_count != kCyclesPerBlock ||
        bits(evidence.fixed_cutoff_time_s) != bits(horizon.cutoff_s) ||
        bits(evidence.torque_tolerance_nm) != bits(kTorqueFloorNm) ||
        bits(evidence.pressure_tolerance_pa) != bits(kPressureFloorPa) ||
        evidence.last_eligible_completed_cycle_ordinal_at_fixed_cutoff !=
            evidence.block_b.cycles.last_completed_cycle_ordinal) {
        fail("diagnostic successful evidence disagrees with its frozen policy");
    }

    DiagnosticWindow result{
        horizon,
        identity,
        true,
        evidence.block_a.cycles.first_completed_cycle_ordinal,
        evidence.block_a.cycles.last_completed_cycle_ordinal,
        evidence.block_b.cycles.first_completed_cycle_ordinal,
        evidence.block_b.cycles.last_completed_cycle_ordinal,
        physical_volumes(engine),
        {},
    };
    const auto append = [&](const contract::HeldSpeedCycleBlockEvidence &block) {
        if (block.completed_cycles.size() != kCyclesPerBlock) {
            fail("diagnostic successful block has the wrong cycle count");
        }
        for (const auto &cycle : block.completed_cycles) {
            result.cycles.push_back({
                cycle.completed_cycle_ordinal,
                cycle.indicated_gas_work_j,
                cycle.aggregate_loss_work_j,
                cycle.starter_work_j,
                cycle.brake_work_j,
                copy_pressures(cycle.end_boundary_pressures, result.volumes),
            });
        }
    };
    result.cycles.reserve(kCycleCount);
    append(evidence.block_a);
    append(evidence.block_b);
    validate_window_cycles(result);
    return result;
}

[[nodiscard]] DiagnosticWindow execute(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
    const Horizon &horizon, const contract::Sha256Digest &identity) {
    auto compiled = simulation::compile_low_order_capture_session(
        request.engine, request.scenario, identity);
    if (const auto *report = std::get_if<contract::ValidationReport>(&compiled)) {
        fail("diagnostic capture-session compilation failed" +
             validation_text(*report));
    }
    auto session =
        std::move(std::get<simulation::LowOrderCaptureSession>(compiled));
    const auto capacity =
        request.scenario.quality.value.capture_block_capacity_frames;
    if (capacity == 0U) {
        fail("diagnostic capture block capacity is zero");
    }
    const std::uint64_t expected_block_count =
        (horizon.total_frame + capacity - 1U) / capacity;
    for (std::uint64_t call = 0U; call <= expected_block_count; ++call) {
        contract::ValidationReport block_report;
        auto advanced =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                block_report =
                    contract::validate(block, request.engine, request.scenario);
                return block_report.ok();
            });
        if (!block_report.ok()) {
            fail("diagnostic emitted an invalid capture block" +
                 validation_text(block_report));
        }
        if (const auto *failure =
                std::get_if<contract::FailureContext>(&advanced)) {
            const auto failure_report = contract::validate(*failure);
            if (!failure_report.ok()) {
                fail("diagnostic runtime failure context is invalid" +
                     validation_text(failure_report));
            }
            if (failure->kind != contract::FailureKind::preparation_not_converged ||
                failure->detail_code !=
                    contract::kPreparationNotConvergedDetailCode) {
                fail("diagnostic terminated for a reason other than the old v1 "
                     "fixed-cutoff gate: " +
                     failure->detail_code + ": " + failure->state_summary);
            }
            const auto &terminal =
                session.held_speed_convergence_finalization_error();
            if (!terminal.has_value()) {
                fail("diagnostic old-gate failure omitted retained typed evidence");
            }
            return from_terminal_error(horizon, request.engine, identity, *terminal);
        }
        if (std::holds_alternative<simulation::LowOrderCaptureBlockPublished>(
                advanced)) {
            continue;
        }
        const auto &completed =
            std::get<simulation::LowOrderCaptureCompleted>(advanced);
        if (completed.inertial_dyno.has_value() ||
            !completed.held_speed_operating_point.has_value() ||
            session.held_speed_convergence_finalization_error().has_value()) {
            fail("diagnostic completed without exactly one clean held result");
        }
        return from_success(horizon, request.engine, request.scenario, identity,
                            *completed.held_speed_operating_point);
    }
    fail("diagnostic session exceeded its compiled block-count bound");
}

[[nodiscard]] ScalarSummary
two_pass_population_summary(const std::array<double, kCycleCount> &samples) {
    double sum = 0.0;
    for (const double sample : samples) {
        if (!std::isfinite(sample)) {
            fail("diagnostic scalar sample is nonfinite");
        }
        sum += sample;
        if (!std::isfinite(sum)) {
            fail("diagnostic chronological scalar sum overflowed");
        }
    }
    const double mean = sum / static_cast<double>(samples.size());
    double squared_deviation_sum = 0.0;
    for (const double sample : samples) {
        const double deviation = sample - mean;
        squared_deviation_sum += deviation * deviation;
        if (!std::isfinite(squared_deviation_sum)) {
            fail("diagnostic RMS reduction overflowed");
        }
    }
    const double rms =
        std::sqrt(squared_deviation_sum / static_cast<double>(samples.size()));
    if (!std::isfinite(mean) || !std::isfinite(rms)) {
        fail("diagnostic two-pass summary is nonfinite");
    }
    return {mean, rms};
}

[[nodiscard]] WindowSummary summarize(const DiagnosticWindow &window) {
    WindowSummary result;
    result.pressures.reserve(window.volumes.size());
    for (std::size_t cycle_index = 0U; cycle_index < window.cycles.size();
         ++cycle_index) {
        const auto &cycle = window.cycles[cycle_index];
        result.total_indicated_gas_work_j += cycle.indicated_gas_work_j;
        result.total_positive_aggregate_loss_work_j +=
            cycle.positive_aggregate_loss_work_j;
        result.total_starter_work_j += cycle.starter_work_j;
        result.total_brake_work_j += cycle.brake_work_j;
        result.cycle_brake_torque_nm[cycle_index] =
            cycle.brake_work_j / kFourStrokeCycleRadians;
    }
    if (!std::isfinite(result.total_indicated_gas_work_j) ||
        !std::isfinite(result.total_positive_aggregate_loss_work_j) ||
        !std::isfinite(result.total_starter_work_j) ||
        !std::isfinite(result.total_brake_work_j)) {
        fail("diagnostic chronological work reduction overflowed");
    }
    result.torque =
        two_pass_population_summary(result.cycle_brake_torque_nm);

    for (std::size_t volume_index = 0U; volume_index < window.volumes.size();
         ++volume_index) {
        std::array<double, kCycleCount> samples{};
        for (std::size_t cycle_index = 0U; cycle_index < window.cycles.size();
             ++cycle_index) {
            samples[cycle_index] =
                window.cycles[cycle_index].pressures[volume_index].pressure_pa_abs;
        }
        result.pressures.push_back(
            {window.volumes[volume_index], two_pass_population_summary(samples)});
    }
    return result;
}

[[nodiscard]] ScalarComparison compare_scalar(const ScalarSummary &earlier,
                                               const ScalarSummary &later,
                                               double absolute_floor) {
    const double delta = std::abs(later.mean - earlier.mean);
    const double envelope =
        absolute_floor + std::max(earlier.population_rms_dispersion,
                                  later.population_rms_dispersion);
    if (!std::isfinite(delta) || !std::isfinite(envelope)) {
        fail("diagnostic separated-window comparison is nonfinite");
    }
    return {delta, envelope, delta <= envelope};
}

[[nodiscard]] WindowComparison compare_windows(const WindowSummary &earlier,
                                               const WindowSummary &later) {
    if (earlier.pressures.size() != later.pressures.size()) {
        fail("diagnostic comparison pressure inventories differ");
    }
    WindowComparison result;
    result.torque = compare_scalar(earlier.torque, later.torque, kTorqueFloorNm);
    result.inside = result.torque.inside;
    result.pressures.reserve(earlier.pressures.size());
    for (std::size_t index = 0U; index < earlier.pressures.size(); ++index) {
        if (earlier.pressures[index].volume.id != later.pressures[index].volume.id ||
            earlier.pressures[index].volume.semantic_id !=
                later.pressures[index].volume.semantic_id) {
            fail("diagnostic comparison pressure identity order differs");
        }
        const auto comparison =
            compare_scalar(earlier.pressures[index].pressure,
                           later.pressures[index].pressure, kPressureFloorPa);
        result.pressures.push_back({earlier.pressures[index].volume, comparison});
        result.inside = result.inside && comparison.inside;
    }
    return result;
}

[[nodiscard]] PointResult execute_point(const PointPlan &plan) {
    PointResult result;
    result.point_index = plan.point_index;
    result.rpm = plan.rpm;
    result.early =
        execute(plan.requests[0], plan.horizons[0], plan.request_identities[0]);
    result.later =
        execute(plan.requests[1], plan.horizons[1], plan.request_identities[1]);
    result.early_summary = summarize(result.early);
    result.later_summary = summarize(result.later);
    result.early_to_later =
        compare_windows(result.early_summary, result.later_summary);
    if (!result.early_to_later.inside) {
        result.extension.emplace(
            execute(plan.requests[2], plan.horizons[2],
                    plan.request_identities[2]));
        result.extension_summary.emplace(summarize(*result.extension));
        result.later_to_extension.emplace(compare_windows(
            result.later_summary, *result.extension_summary));
    }
    return result;
}

[[nodiscard]] std::array<PointPlan, kPointCount> build_plans() {
    auto request_set_result =
        profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&request_set_result)) {
        fail("canonical BMW torque-sweep request construction failed" +
             validation_text(*report));
    }
    auto canonical_set =
        std::get<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(
            std::move(request_set_result));
    const auto set_report =
        profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request_set(
            canonical_set);
    if (!set_report.ok()) {
        fail("canonical BMW torque-sweep request set changed" +
             validation_text(set_report));
    }

    std::array<PointPlan, kPointCount> plans;
    std::vector<contract::Sha256Digest> all_identities;
    all_identities.reserve(kPointCount * 3U);
    for (std::size_t point_index = 0U; point_index < plans.size(); ++point_index) {
        const auto rpm =
            profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm[point_index];
        auto &plan = plans[point_index];
        plan.point_index = point_index;
        plan.rpm = rpm;
        plan.horizons = {{
            {6.44, 6.46, UINT64_C(64400), UINT64_C(64600),
             canonical_set[point_index].scenario.scenario_id},
            {12.88, 12.90, UINT64_C(128800), UINT64_C(129000),
             later_scenario_id(rpm, UINT64_C(128800))},
            {25.76, 25.78, UINT64_C(257600), UINT64_C(257800),
             later_scenario_id(rpm, UINT64_C(257600))},
        }};
        plan.requests = {canonical_set[point_index], canonical_set[point_index],
                         canonical_set[point_index]};
        mutate_horizon(plan.requests[1], plan.horizons[1]);
        mutate_horizon(plan.requests[2], plan.horizons[2]);
        for (std::size_t horizon_index = 0U; horizon_index < 3U;
             ++horizon_index) {
            validate_request(canonical_set[point_index],
                             plan.requests[horizon_index],
                             plan.horizons[horizon_index], rpm);
            plan.request_identities[horizon_index] =
                request_identity(plan.requests[horizon_index]);
            if (std::ranges::find(all_identities,
                                  plan.request_identities[horizon_index]) !=
                all_identities.end()) {
                fail("two cross-RPM diagnostic requests share a request-v2 "
                     "identity");
            }
            all_identities.push_back(plan.request_identities[horizon_index]);
        }
    }
    return plans;
}

void print_window(std::ostringstream &report, std::string_view label,
                  const DiagnosticWindow &window,
                  const WindowSummary &summary) {
    report << "window=" << label << '\n'
           << "scenario_id=" << window.horizon.scenario_id << '\n'
           << "simulation_request_v2_sha256="
           << digest_hex(window.request_identity) << '\n'
           << "cutoff_s=" << exact_binary64(window.horizon.cutoff_s) << '\n'
           << "total_horizon_s=" << exact_binary64(window.horizon.total_s) << '\n'
           << "old_v1_gate_status="
           << (window.old_v1_gate_passed ? "passed" : "failed") << '\n'
           << "block_a_cycle_range=" << window.block_a_first_ordinal << ".."
           << window.block_a_last_ordinal << '\n'
           << "block_b_cycle_range=" << window.block_b_first_ordinal << ".."
           << window.block_b_last_ordinal << '\n'
           << "total_indicated_gas_work_j="
           << exact_binary64(summary.total_indicated_gas_work_j) << '\n'
           << "total_positive_aggregate_loss_work_j="
           << exact_binary64(summary.total_positive_aggregate_loss_work_j) << '\n'
           << "total_starter_work_j="
           << exact_binary64(summary.total_starter_work_j) << '\n'
           << "total_brake_work_j="
           << exact_binary64(summary.total_brake_work_j) << '\n'
           << "torque_mean_nm=" << exact_binary64(summary.torque.mean) << '\n'
           << "torque_population_rms_dispersion_nm="
           << exact_binary64(summary.torque.population_rms_dispersion) << '\n'
           << "cycle_brake_torque_nm=";
    for (std::size_t index = 0U; index < summary.cycle_brake_torque_nm.size();
         ++index) {
        if (index != 0U) {
            report << ',';
        }
        report << exact_binary64(summary.cycle_brake_torque_nm[index]);
    }
    report << '\n';
    for (const auto &pressure : summary.pressures) {
        report << "pressure_summary gas_volume_id=" << pressure.volume.id.value
               << " semantic_id=" << pressure.volume.semantic_id
               << " mean_pa_abs=" << exact_binary64(pressure.pressure.mean)
               << " population_rms_dispersion_pa="
               << exact_binary64(
                      pressure.pressure.population_rms_dispersion)
               << '\n';
    }
}

void print_comparison(std::ostringstream &report, std::string_view label,
                      const WindowComparison &comparison) {
    report << "comparison=" << label << '\n'
           << "torque_delta_nm=" << exact_binary64(comparison.torque.delta)
           << '\n'
           << "torque_envelope_nm="
           << exact_binary64(comparison.torque.envelope) << '\n'
           << "torque_inside_envelope="
           << (comparison.torque.inside ? "true" : "false") << '\n';
    for (const auto &pressure : comparison.pressures) {
        report << "pressure_comparison gas_volume_id="
               << pressure.volume.id.value
               << " semantic_id=" << pressure.volume.semantic_id
               << " delta_pa=" << exact_binary64(pressure.pressure.delta)
               << " envelope_pa=" << exact_binary64(pressure.pressure.envelope)
               << " inside_envelope="
               << (pressure.pressure.inside ? "true" : "false") << '\n';
    }
    report << "comparison_outcome="
           << (comparison.inside ? "inside" : "outside") << '\n';
}

void print_report(std::ostream &output, const std::vector<PointResult> &results) {
    const bool all_early_inside =
        std::ranges::all_of(results, [](const PointResult &result) {
            return result.early_to_later.inside;
        });
    bool all_required_extensions_inside = true;
    std::size_t extension_count = 0U;
    for (const auto &result : results) {
        if (result.early_to_later.inside) {
            if (result.extension.has_value() ||
                result.later_to_extension.has_value()) {
                fail("diagnostic extended a point whose first comparison passed");
            }
            continue;
        }
        ++extension_count;
        if (!result.extension.has_value() ||
            !result.extension_summary.has_value() ||
            !result.later_to_extension.has_value()) {
            fail("diagnostic omitted a required 25.76-second extension");
        }
        all_required_extensions_inside =
            all_required_extensions_inside &&
            result.later_to_extension->inside;
    }

    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << "BMW M52B28 cross-RPM fixed-sample diagnostic (reference-only)\n"
           << "point_count=9\n"
           << "worker_bound=9-independent-rpm-workers-sequential-horizons\n"
           << "fixed_sample_complete_cycle_count=32\n"
           << "reduction=chronological-binary64-two-pass-population-rms\n"
           << "torque_absolute_floor_nm=" << exact_binary64(kTorqueFloorNm)
           << '\n'
           << "pressure_absolute_floor_pa=" << exact_binary64(kPressureFloorPa)
           << '\n'
           << "interpretation=deterministic-engineering-diagnostic-not-a-"
              "probability-statement\n";

    for (const auto &result : results) {
        report << "\npoint_index=" << result.point_index << '\n'
               << "engine_speed_rpm=" << exact_binary64(result.rpm) << '\n';
        print_window(report, "6.44s", result.early, result.early_summary);
        print_window(report, "12.88s", result.later, result.later_summary);
        print_comparison(report, "6.44s-vs-12.88s",
                         result.early_to_later);
        if (result.extension.has_value()) {
            print_window(report, "25.76s", *result.extension,
                         *result.extension_summary);
            print_comparison(report, "12.88s-vs-25.76s",
                             *result.later_to_extension);
        }
        report << "point_outcome=";
        if (result.early_to_later.inside) {
            report << "supports-6.44s\n";
        } else if (result.later_to_extension->inside) {
            report << "supports-12.88s\n";
        } else {
            report << "inconclusive\n";
        }
    }

    report << "\nrequired_extension_count=" << extension_count << '\n';
    if (all_early_inside) {
        report << "global_outcome=use-global-6.44s-deletion-horizon\n"
               << "selected_initialization_deletion_horizon_s="
               << exact_binary64(6.44) << '\n'
               << "v2_may_be_frozen=true\n";
    } else if (all_required_extensions_inside) {
        report << "global_outcome=use-global-12.88s-deletion-horizon\n"
               << "selected_initialization_deletion_horizon_s="
               << exact_binary64(12.88) << '\n'
               << "v2_may_be_frozen=true\n";
    } else {
        report << "global_outcome=inconclusive\n"
               << "selected_initialization_deletion_horizon_s=none\n"
               << "v2_may_be_frozen=false\n";
    }
    output << report.str();
    if (!output) {
        fail("could not write BMW cross-RPM diagnostic report");
    }
}

} // namespace

void run_bmw_m52b28_cross_rpm_fixed_sample_diagnostic(std::ostream &output) {
    const auto plans = build_plans();
    std::array<std::future<PointResult>, kPointCount> futures;
    for (std::size_t index = 0U; index < futures.size(); ++index) {
        futures[index] = std::async(std::launch::async, [&plans, index] {
            return execute_point(plans[index]);
        });
    }

    std::vector<PointResult> results;
    results.reserve(kPointCount);
    for (auto &future : futures) {
        results.push_back(future.get());
    }
    print_report(output, results);
}

} // namespace engine_sim_offline::reference
