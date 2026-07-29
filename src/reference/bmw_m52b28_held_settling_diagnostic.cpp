#include "reference/bmw_m52b28_held_settling_diagnostic.hpp"

#include "engine_sim_offline/profiles/bmw_m52b28_full_throttle_torque_sweep_request.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
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

constexpr std::size_t kCanonicalPointIndex = 1U;
constexpr std::uint32_t kCyclesPerBlock = 16U;
constexpr std::size_t kRetainedCycleCount = 2U * kCyclesPerBlock;
constexpr contract::GasVolumeId kCylinderOneVolumeId{4U};

constexpr std::uint64_t kRecordedTorqueResidualBits =
    UINT64_C(4600432227973450752);
constexpr std::uint64_t kRecordedTorqueToleranceBits =
    UINT64_C(4604930618986332160);
constexpr std::uint64_t kRecordedPressureResidualBits =
    UINT64_C(4666284760132184832);
constexpr std::uint64_t kRecordedPressureToleranceBits =
    UINT64_C(4654311885213007872);

struct DiagnosticPlan {
    double cutoff_s = 0.0;
    double total_s = 0.0;
    std::uint64_t cutoff_frame = 0U;
    std::uint64_t total_frame = 0U;
    std::string_view scenario_id;
};

constexpr std::array<DiagnosticPlan, 3U> kPlans{{
    {
        6.44,
        6.46,
        UINT64_C(64400),
        UINT64_C(64600),
        "bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v1",
    },
    {
        12.88,
        12.90,
        UINT64_C(128800),
        UINT64_C(129000),
        "bmw-m52b28-held-2500rpm-full-throttle-settling-diagnostic-128800f-v1",
    },
    {
        25.76,
        25.78,
        UINT64_C(257600),
        UINT64_C(257800),
        "bmw-m52b28-held-2500rpm-full-throttle-settling-diagnostic-257600f-v1",
    },
}};

struct DiagnosticVolumeMean {
    contract::GasVolumeId gas_volume_id;
    std::string semantic_id;
    double block_a_pressure_pa_abs = 0.0;
    double block_b_pressure_pa_abs = 0.0;
};

struct DiagnosticCycle {
    char block = 'a';
    std::uint64_t ordinal = 0U;
    double cylinder_one_pressure_pa_abs = 0.0;
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
};

struct DiagnosticWindow {
    DiagnosticPlan plan;
    contract::Sha256Digest request_identity;
    bool settled = false;
    double torque_residual_nm = 0.0;
    double torque_tolerance_nm = 0.0;
    double pressure_residual_pa = 0.0;
    double pressure_tolerance_pa = 0.0;
    contract::GasVolumeId limiting_gas_volume_id;
    std::uint64_t block_a_first_ordinal = 0U;
    std::uint64_t block_a_last_ordinal = 0U;
    std::uint64_t block_b_first_ordinal = 0U;
    std::uint64_t block_b_last_ordinal = 0U;
    std::vector<DiagnosticVolumeMean> volume_means;
    std::vector<DiagnosticCycle> cycles;
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

[[nodiscard]] std::string_view
gas_volume_semantic_id(const contract::EngineSpec &engine,
                       contract::GasVolumeId gas_volume_id) {
    std::string_view result;
    for (const auto &volume : engine.gas_volumes) {
        if (volume.id != gas_volume_id) {
            continue;
        }
        if (!result.empty()) {
            fail("diagnostic engine has duplicate stable gas-volume identity");
        }
        result = volume.semantic_id.value;
    }
    if (result.empty()) {
        fail("diagnostic convergence evidence names an unknown gas-volume identity");
    }
    return result;
}

template <class PressureRange>
[[nodiscard]] double cylinder_one_pressure(const PressureRange &pressures) {
    std::optional<double> result;
    for (const auto &pressure : pressures) {
        if (pressure.gas_volume_id != kCylinderOneVolumeId) {
            continue;
        }
        if (result.has_value()) {
            fail("diagnostic cycle repeats cylinder.1 pressure identity");
        }
        result = pressure.pressure_pa_abs;
    }
    if (!result.has_value()) {
        fail("diagnostic cycle omits cylinder.1 pressure");
    }
    return *result;
}

void require_canonical_cycle_shape(const DiagnosticWindow &window) {
    if (window.block_a_first_ordinal == 0U ||
        window.block_a_last_ordinal - window.block_a_first_ordinal + 1U !=
            kCyclesPerBlock ||
        window.block_b_first_ordinal != window.block_a_last_ordinal + 1U ||
        window.block_b_last_ordinal - window.block_b_first_ordinal + 1U !=
            kCyclesPerBlock ||
        window.cycles.size() != kRetainedCycleCount) {
        fail("diagnostic evidence is not the latest adjacent two-by-16-cycle pair");
    }
    for (std::size_t index = 0U; index < window.cycles.size(); ++index) {
        const auto expected_ordinal = window.block_a_first_ordinal + index;
        const char expected_block = index < kCyclesPerBlock ? 'a' : 'b';
        if (window.cycles[index].ordinal != expected_ordinal ||
            window.cycles[index].block != expected_block) {
            fail("diagnostic completed-cycle evidence is not contiguous and ordered");
        }
    }
}

[[nodiscard]] DiagnosticWindow from_terminal_error(
    const DiagnosticPlan &plan, const contract::EngineSpec &engine,
    const contract::Sha256Digest &request_identity,
    const simulation::AdjacentCycleBlockConvergenceError &error) {
    if (error.code !=
            simulation::AdjacentCycleBlockConvergenceErrorCode::nonconverged ||
        error.retained_cycle_count != kRetainedCycleCount ||
        error.required_cycle_count != kRetainedCycleCount ||
        !error.evidence.has_value()) {
        fail("diagnostic terminal failure did not retain one nonconverged "
             "two-by-16-cycle result");
    }
    const auto &evidence = *error.evidence;
    if (evidence.cycles_per_block != kCyclesPerBlock || evidence.settled ||
        bits(evidence.fixed_cutoff_time_s) != bits(plan.cutoff_s)) {
        fail("diagnostic terminal evidence disagrees with its fixed cutoff");
    }

    DiagnosticWindow result{
        plan,
        request_identity,
        false,
        evidence.torque_residual_nm,
        evidence.cycle_mean_torque_tolerance_nm,
        evidence.pressure_residual_pa,
        evidence.pressure_tolerance_pa,
        evidence.limiting_gas_volume_id,
        evidence.block_a.range.first_cycle_ordinal,
        evidence.block_a.range.last_cycle_ordinal,
        evidence.block_b.range.first_cycle_ordinal,
        evidence.block_b.range.last_cycle_ordinal,
        {},
        {},
    };
    result.volume_means.reserve(evidence.pressure_means.size());
    for (const auto &mean : evidence.pressure_means) {
        result.volume_means.push_back({
            mean.gas_volume_id,
            std::string{gas_volume_semantic_id(engine, mean.gas_volume_id)},
            mean.block_a_mean_pressure_pa_abs,
            mean.block_b_mean_pressure_pa_abs,
        });
    }

    const auto append_block = [&](char block,
                                  const simulation::AdjacentCycleBlockMean &mean) {
        if (mean.completed_cycles.size() != kCyclesPerBlock) {
            fail("diagnostic terminal evidence retained a malformed cycle block");
        }
        for (const auto &cycle : mean.completed_cycles) {
            result.cycles.push_back({
                block,
                cycle.completed_cycle_ordinal,
                cylinder_one_pressure(cycle.end_boundary_pressures),
                cycle.indicated_gas_work_j,
                cycle.positive_aggregate_loss_work_j,
                cycle.starter_work_j,
                cycle.brake_work_j,
            });
        }
    };
    result.cycles.reserve(kRetainedCycleCount);
    append_block('a', evidence.block_a);
    append_block('b', evidence.block_b);
    require_canonical_cycle_shape(result);
    return result;
}

[[nodiscard]] DiagnosticWindow from_success(
    const DiagnosticPlan &plan, const contract::EngineSpec &engine,
    const contract::RenderScenario &scenario,
    const contract::Sha256Digest &request_identity,
    const contract::HeldSpeedOperatingPointResult &operating_point) {
    const auto result_report =
        contract::validate(operating_point, scenario, engine, request_identity);
    if (!result_report.ok()) {
        fail("diagnostic successful held result failed request-bound validation" +
             validation_text(result_report));
    }
    const auto &evidence = operating_point.convergence;
    if (evidence.comparison_cycle_count != kCyclesPerBlock ||
        bits(evidence.fixed_cutoff_time_s) != bits(plan.cutoff_s) ||
        evidence.last_eligible_completed_cycle_ordinal_at_fixed_cutoff !=
            evidence.block_b.cycles.last_completed_cycle_ordinal) {
        fail("diagnostic successful evidence is not the latest frozen cutoff pair");
    }
    if (evidence.block_a.mean_boundary_pressures.size() !=
        evidence.block_b.mean_boundary_pressures.size()) {
        fail("diagnostic successful pressure-mean blocks have different shapes");
    }

    DiagnosticWindow result{
        plan,
        request_identity,
        true,
        evidence.torque_residual_nm,
        evidence.torque_tolerance_nm,
        evidence.pressure_residual_pa,
        evidence.pressure_tolerance_pa,
        evidence.limiting_pressure_volume_id,
        evidence.block_a.cycles.first_completed_cycle_ordinal,
        evidence.block_a.cycles.last_completed_cycle_ordinal,
        evidence.block_b.cycles.first_completed_cycle_ordinal,
        evidence.block_b.cycles.last_completed_cycle_ordinal,
        {},
        {},
    };
    result.volume_means.reserve(evidence.block_a.mean_boundary_pressures.size());
    for (std::size_t index = 0U;
         index < evidence.block_a.mean_boundary_pressures.size(); ++index) {
        const auto &block_a = evidence.block_a.mean_boundary_pressures[index];
        const auto &block_b = evidence.block_b.mean_boundary_pressures[index];
        if (block_a.gas_volume_id != block_b.gas_volume_id) {
            fail("diagnostic successful pressure-mean identities changed by block");
        }
        result.volume_means.push_back({
            block_a.gas_volume_id,
            std::string{gas_volume_semantic_id(engine, block_a.gas_volume_id)},
            block_a.pressure_pa_abs,
            block_b.pressure_pa_abs,
        });
    }

    const auto append_block =
        [&](char block, const contract::HeldSpeedCycleBlockEvidence &mean) {
            if (mean.completed_cycles.size() != kCyclesPerBlock) {
                fail("diagnostic successful evidence retained a malformed cycle "
                     "block");
            }
            for (const auto &cycle : mean.completed_cycles) {
                result.cycles.push_back({
                    block,
                    cycle.completed_cycle_ordinal,
                    cylinder_one_pressure(cycle.end_boundary_pressures),
                    cycle.indicated_gas_work_j,
                    cycle.aggregate_loss_work_j,
                    cycle.starter_work_j,
                    cycle.brake_work_j,
                });
            }
        };
    result.cycles.reserve(kRetainedCycleCount);
    append_block('a', evidence.block_a);
    append_block('b', evidence.block_b);
    require_canonical_cycle_shape(result);
    return result;
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
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
    const DiagnosticPlan &plan) {
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
    if (preparation == nullptr ||
        preparation->comparison_cycle_count.value != kCyclesPerBlock ||
        bits(preparation->maximum_preparation_duration_s.value) !=
            bits(plan.cutoff_s) ||
        bits(request.scenario.audible_start_s.value) != bits(plan.cutoff_s) ||
        bits(request.scenario.audible_duration_s.value) != bits(0.02) ||
        bits(request.scenario.total_duration_s.value) != bits(plan.total_s) ||
        request.scenario.scenario_id != plan.scenario_id ||
        contract::resolve_frame_index(plan.cutoff_s,
                                      request.scenario.rates.physics) !=
            plan.cutoff_frame ||
        contract::resolve_frame_index(plan.total_s, request.scenario.rates.physics) !=
            plan.total_frame) {
        fail("diagnostic request disagrees with its frozen cutoff and horizon");
    }
}

void require_recorded_failure_reproduction(
    const contract::FailureContext &failure,
    const simulation::AdjacentCycleBlockConvergenceError &terminal_error);

[[nodiscard]] DiagnosticWindow execute(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request,
    const DiagnosticPlan &plan, const contract::Sha256Digest &identity,
    bool require_recorded_reproduction = false) {
    auto compiled = simulation::compile_low_order_capture_session(
        request.engine, request.scenario, identity);
    if (const auto *report = std::get_if<contract::ValidationReport>(&compiled)) {
        fail("diagnostic capture session compilation failed" +
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
        (plan.total_frame + capacity - 1U) / capacity;
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
            const auto &terminal_error =
                session.held_speed_convergence_finalization_error();
            if (!terminal_error.has_value()) {
                fail("diagnostic held failure omitted retained convergence evidence: " +
                     failure->detail_code + ": " + failure->state_summary);
            }
            if (failure->kind != contract::FailureKind::preparation_not_converged ||
                failure->detail_code != contract::kPreparationNotConvergedDetailCode) {
                fail("diagnostic terminated for a reason other than fixed-cutoff "
                     "nonconvergence: " +
                     failure->detail_code + ": " + failure->state_summary);
            }
            if (require_recorded_reproduction) {
                require_recorded_failure_reproduction(*failure, *terminal_error);
            }
            return from_terminal_error(plan, request.engine, identity,
                                       *terminal_error);
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
            fail("diagnostic completed without exactly one clean held-speed result");
        }
        if (require_recorded_reproduction) {
            fail("canonical 6.44-second diagnostic unexpectedly converged");
        }
        return from_success(plan, request.engine, request.scenario, identity,
                            *completed.held_speed_operating_point);
    }
    fail("diagnostic session exceeded its compiled block-count bound");
}

void require_recorded_failure_reproduction(
    const contract::FailureContext &failure,
    const simulation::AdjacentCycleBlockConvergenceError &terminal_error) {
    const std::string expected_summary =
        "scenario=bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v1; "
        "fixed-cutoff convergence failed; error-code=5; retained-cycle-count=32; "
        "required-cycle-count=32; block-a-first-ordinal=101; "
        "block-a-last-ordinal=116; block-b-first-ordinal=117; "
        "block-b-last-ordinal=132; torque-residual-binary64=" +
        std::to_string(kRecordedTorqueResidualBits) +
        "; torque-tolerance-binary64=" +
        std::to_string(kRecordedTorqueToleranceBits) +
        "; pressure-residual-binary64=" +
        std::to_string(kRecordedPressureResidualBits) +
        "; pressure-tolerance-binary64=" +
        std::to_string(kRecordedPressureToleranceBits) +
        "; limiting-gas-volume-id=4";
    if (failure.kind != contract::FailureKind::preparation_not_converged ||
        failure.detail_code != contract::kPreparationNotConvergedDetailCode ||
        failure.state_summary != expected_summary ||
        failure.gas_volume_id != std::optional{kCylinderOneVolumeId} ||
        failure.tolerances.size() != 2U ||
        failure.tolerances[0].quantity_id !=
            contract::kCycleMeanTorqueResidualNmQuantityId ||
        bits(failure.tolerances[0].attempted_value) !=
            kRecordedTorqueResidualBits ||
        bits(failure.tolerances[0].tolerance) != kRecordedTorqueToleranceBits ||
        failure.tolerances[1].quantity_id !=
            contract::kBoundaryPressureResidualPaQuantityId ||
        bits(failure.tolerances[1].attempted_value) !=
            kRecordedPressureResidualBits ||
        bits(failure.tolerances[1].tolerance) !=
            kRecordedPressureToleranceBits) {
        fail("canonical 6.44-second public failure did not bit-match the recorded "
             "failure");
    }
    if (terminal_error.code !=
            simulation::AdjacentCycleBlockConvergenceErrorCode::nonconverged ||
        !terminal_error.evidence.has_value()) {
        fail("canonical 6.44-second retained typed failure is missing");
    }
    const auto &evidence = *terminal_error.evidence;
    if (bits(evidence.torque_residual_nm) != kRecordedTorqueResidualBits ||
        bits(evidence.cycle_mean_torque_tolerance_nm) !=
            kRecordedTorqueToleranceBits ||
        bits(evidence.pressure_residual_pa) != kRecordedPressureResidualBits ||
        bits(evidence.pressure_tolerance_pa) !=
            kRecordedPressureToleranceBits ||
        evidence.block_a.range.first_cycle_ordinal != 101U ||
        evidence.block_a.range.last_cycle_ordinal != 116U ||
        evidence.block_b.range.first_cycle_ordinal != 117U ||
        evidence.block_b.range.last_cycle_ordinal != 132U ||
        evidence.limiting_gas_volume_id != kCylinderOneVolumeId) {
        fail("canonical 6.44-second retained evidence did not bit-match the "
             "recorded failure");
    }
}

void print_report(std::ostream &output,
                  const std::array<DiagnosticWindow, kPlans.size()> &windows) {
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << "BMW M52B28 held-settling diagnostic (reference-only)\n"
           << "canonical_torque_sweep_point_index=1\n"
           << "engine_speed_rpm=" << exact_binary64(2500.0) << '\n'
           << "comparison_cycles_per_block=16\n"
           << "interpretation=descriptive deterministic windows; no classifier or "
              "tuning\n";

    for (std::size_t window_index = 0U; window_index < windows.size();
         ++window_index) {
        const auto &window = windows[window_index];
        report << "\nwindow=" << window_index << '\n'
               << "scenario_id=" << window.plan.scenario_id << '\n'
               << "simulation_request_v2_sha256="
               << digest_hex(window.request_identity) << '\n'
               << "cutoff_s=" << exact_binary64(window.plan.cutoff_s) << '\n'
               << "total_horizon_s=" << exact_binary64(window.plan.total_s) << '\n'
               << "status=" << (window.settled ? "settled" : "nonconverged")
               << '\n'
               << "torque_residual_nm="
               << exact_binary64(window.torque_residual_nm) << '\n'
               << "torque_tolerance_nm="
               << exact_binary64(window.torque_tolerance_nm) << '\n'
               << "pressure_residual_pa="
               << exact_binary64(window.pressure_residual_pa) << '\n'
               << "pressure_tolerance_pa="
               << exact_binary64(window.pressure_tolerance_pa) << '\n'
               << "limiting_gas_volume_id="
               << window.limiting_gas_volume_id.value << '\n'
               << "block_a_cycle_range=" << window.block_a_first_ordinal << ".."
               << window.block_a_last_ordinal << '\n'
               << "block_b_cycle_range=" << window.block_b_first_ordinal << ".."
               << window.block_b_last_ordinal << '\n';

        for (const auto &mean : window.volume_means) {
            report << "volume_mean gas_volume_id=" << mean.gas_volume_id.value
                   << " semantic_id=" << mean.semantic_id
                   << " block_a_pressure_pa_abs="
                   << exact_binary64(mean.block_a_pressure_pa_abs)
                   << " block_b_pressure_pa_abs="
                   << exact_binary64(mean.block_b_pressure_pa_abs) << '\n';
        }
        for (const auto &cycle : window.cycles) {
            report << "cycle block=" << cycle.block
                   << " ordinal=" << cycle.ordinal
                   << " cylinder.1_pressure_pa_abs="
                   << exact_binary64(cycle.cylinder_one_pressure_pa_abs)
                   << " indicated_gas_work_j="
                   << exact_binary64(cycle.indicated_gas_work_j)
                   << " positive_aggregate_loss_work_j="
                   << exact_binary64(cycle.positive_aggregate_loss_work_j)
                   << " starter_work_j=" << exact_binary64(cycle.starter_work_j)
                   << " brake_work_j=" << exact_binary64(cycle.brake_work_j)
                   << '\n';
        }
    }
    output << report.str();
    if (!output) {
        fail("could not write BMW held-settling diagnostic report");
    }
}

} // namespace

void run_bmw_m52b28_held_settling_diagnostic(std::ostream &output) {
    auto request_set_result =
        profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&request_set_result)) {
        fail("canonical BMW torque-sweep request construction failed" +
             validation_text(*report));
    }
    const auto request_set =
        std::get<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(
            std::move(request_set_result));
    const auto &canonical = request_set[kCanonicalPointIndex];
    if (gas_volume_semantic_id(canonical.engine, kCylinderOneVolumeId) !=
        "cylinder.1") {
        fail("canonical gas-volume ID 4 no longer maps to cylinder.1");
    }

    std::array<profiles::BmwM52b28FullThrottleTorqueSweepRequest, kPlans.size()>
        requests{canonical, canonical, canonical};
    for (std::size_t index = 1U; index < requests.size(); ++index) {
        auto &scenario = requests[index].scenario;
        auto *preparation =
            std::get_if<contract::ConvergenceSettling>(&scenario.preparation);
        if (preparation == nullptr) {
            fail("canonical 2500-rpm request lost convergence preparation");
        }
        scenario.scenario_id = std::string{kPlans[index].scenario_id};
        preparation->maximum_preparation_duration_s.value = kPlans[index].cutoff_s;
        scenario.audible_start_s.value = kPlans[index].cutoff_s;
        scenario.total_duration_s.value = kPlans[index].total_s;
    }

    std::array<contract::Sha256Digest, kPlans.size()> identities{};
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        validate_request(requests[index], kPlans[index]);
        identities[index] = request_identity(requests[index]);
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (identities[index] == identities[prior]) {
                fail("two diagnostic cutoffs share a request-v2 identity");
            }
        }
    }

    std::array<DiagnosticWindow, kPlans.size()> windows{
        execute(requests[0], kPlans[0], identities[0], true),
        execute(requests[1], kPlans[1], identities[1]),
        execute(requests[2], kPlans[2], identities[2]),
    };
    print_report(output, windows);
}

} // namespace engine_sim_offline::reference
