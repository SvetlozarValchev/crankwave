#include "package/uniform_cycle_bank.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::package_detail {
namespace {

constexpr auto kNormalRunningState =
    engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled);
constexpr double kGridClosureTolerance = 1.0e-9;

struct Candidate {
    const EngineCompletedCycleEvidence *evidence = nullptr;
    contract::AudioPackageSourceBoundary start;
    contract::AudioPackageSourceBoundary end;
};

[[nodiscard]] UniformCycleBankError error(const UniformCycleBankErrorCode code,
                                          std::string path, std::string detail) {
    return {code, std::move(path), std::move(detail)};
}

[[nodiscard]] std::string cycle_path(const std::size_t index,
                                     const std::string_view member = {}) {
    auto path = "lane.cycles[" + std::to_string(index) + "]";
    if (!member.empty()) {
        path += '.';
        path += member;
    }
    return path;
}

[[nodiscard]] bool valid_control(const EngineCycleControlEvidence &control) noexcept {
    return std::isfinite(control.time_weighted_mean_01) &&
           std::isfinite(control.minimum_01) && std::isfinite(control.maximum_01) &&
           control.time_weighted_mean_01 >= 0.0 &&
           control.time_weighted_mean_01 <= 1.0 && control.minimum_01 >= 0.0 &&
           control.maximum_01 <= 1.0 && control.minimum_01 <= control.maximum_01;
}

[[nodiscard]] double canonical_zero(const double value) noexcept {
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] std::optional<UniformCycleBankError>
validate_evidence(const EngineCompletedCycleEvidence &cycle, const std::size_t index) {
    const auto valid_boundary = [](const EngineCycleBoundaryEvidence &boundary) {
        return boundary.left_physics_frame <= boundary.right_physics_frame &&
               boundary.right_physics_frame - boundary.left_physics_frame <= 1U &&
               std::isfinite(boundary.fraction_from_left_01) &&
               boundary.fraction_from_left_01 >= 0.0 &&
               boundary.fraction_from_left_01 <= 1.0 &&
               std::isfinite(boundary.theta_unwrapped_rad) &&
               std::isfinite(boundary.time_s) && std::isfinite(boundary.delivery_frame);
    };
    if (!valid_boundary(cycle.start_boundary) || !valid_boundary(cycle.end_boundary)) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(index, "boundary"),
                     "cycle contains a nonfinite or malformed physical boundary");
    }
    if (cycle.start_boundary.cycle_ordinal ==
            std::numeric_limits<std::int64_t>::max() ||
        cycle.end_boundary.cycle_ordinal != cycle.start_boundary.cycle_ordinal + 1 ||
        !(cycle.end_boundary.theta_unwrapped_rad >
          cycle.start_boundary.theta_unwrapped_rad) ||
        !(cycle.end_boundary.time_s > cycle.start_boundary.time_s) ||
        !(cycle.end_boundary.delivery_frame > cycle.start_boundary.delivery_frame) ||
        !std::isfinite(cycle.duration_s) || !(cycle.duration_s > 0.0) ||
        !std::isfinite(cycle.mean_engine_speed_rpm) ||
        !(cycle.mean_engine_speed_rpm > 0.0)) {
        return error(UniformCycleBankErrorCode::malformed_evidence, cycle_path(index),
                     "cycle extent, duration, or measured RPM is malformed");
    }
    if (!valid_control(cycle.requested_throttle) ||
        !valid_control(cycle.resolved_engine_throttle) ||
        !valid_control(cycle.intake_plate_position)) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(index, "controls"),
                     "cycle contains nonfinite or out-of-range control evidence");
    }
    if (!std::isfinite(cycle.instantaneous_net_shaft.angular_work_j) ||
        !std::isfinite(cycle.instantaneous_net_shaft.cycle_mean_torque_nm)) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(index, "instantaneous_net_shaft"),
                     "cycle contains nonfinite net-shaft evidence");
    }
    const auto &torque = cycle.instantaneous_net_shaft;
    const bool available_metadata_valid =
        torque.availability != contract::Availability::available ||
        ((torque.completeness == contract::Completeness::complete ||
          torque.completeness == contract::Completeness::incomplete) &&
         torque.included_terms != 0U &&
         (torque.included_terms & torque.omitted_terms) == 0U &&
         (torque.included_terms | torque.omitted_terms) ==
             contract::known_torque_term_mask() &&
         ((torque.completeness == contract::Completeness::complete &&
           torque.omitted_terms == 0U) ||
          (torque.completeness == contract::Completeness::incomplete &&
           torque.omitted_terms != 0U)));
    if (!available_metadata_valid) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(index, "instantaneous_net_shaft"),
                     "cycle modeled net-shaft metadata does not partition known torque terms");
    }
    if (cycle.completed_cycle_ordinal > contract::kMaximumResolvedFrameIndex) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(index, "completed_cycle_ordinal"),
                     "cycle ordinal is not exactly representable by the runtime");
    }
    return std::nullopt;
}

[[nodiscard]] std::variant<contract::AudioPackageSourceBoundary, UniformCycleBankError>
aligned_boundary(const EngineCycleBoundaryEvidence &boundary,
                 const UniformCycleLaneView &lane,
                 const PackageBakeMethodGeometry &geometry,
                 const std::size_t cycle_index, const std::string_view member) {
    const long double coordinate =
        static_cast<long double>(boundary.delivery_frame) +
        static_cast<long double>(geometry.cycle_signal_alignment_frames) -
        static_cast<long double>(lane.first_global_delivery_frame);
    if (!std::isfinite(coordinate) || coordinate < 0.0L ||
        coordinate > static_cast<long double>(contract::kMaximumResolvedFrameIndex)) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, member),
                     "signal-aligned boundary lies outside the representable tape");
    }

    const auto lower = std::floor(coordinate);
    const auto left = static_cast<std::uint64_t>(lower);
    if (coordinate == lower) {
        return contract::AudioPackageSourceBoundary{left, left, 0.0};
    }
    if (left >= contract::kMaximumResolvedFrameIndex) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, member),
                     "fractional signal boundary has no representable upper bracket");
    }
    const double fraction = static_cast<double>(coordinate - lower);
    if (!(fraction > 0.0) || !(fraction < 1.0) || !std::isfinite(fraction)) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(cycle_index, member),
                     "fractional signal boundary could not be normalized exactly");
    }
    return contract::AudioPackageSourceBoundary{left, left + 1U, fraction};
}

[[nodiscard]] bool
boundary_less(const contract::AudioPackageSourceBoundary &left,
              const contract::AudioPackageSourceBoundary &right) noexcept {
    return left.left_frame < right.left_frame ||
           (left.left_frame == right.left_frame &&
            left.fraction_from_left_01 < right.fraction_from_left_01);
}

[[nodiscard]] std::optional<UniformCycleBankError>
edge_rejection(const contract::AudioPackageSourceBoundary &boundary,
               const UniformCycleLaneView &lane,
               const PackageBakeMethodGeometry &geometry, const std::size_t cycle_index,
               const std::string_view member) {
    if (boundary.right_frame >= lane.pcm_frame_count) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, member),
                     "signal-aligned boundary lies outside the PCM tape");
    }
    if (boundary.left_frame < geometry.edge_guard_frames) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, member),
                     "signal-aligned boundary lacks left-edge PCM context");
    }
    const auto right_context = lane.pcm_frame_count - boundary.right_frame - 1U;
    if (right_context < geometry.edge_guard_frames) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, member),
                     "signal-aligned boundary lacks right-edge PCM context");
    }
    return std::nullopt;
}

[[nodiscard]] std::variant<Candidate, UniformCycleBankError>
candidate(const EngineCompletedCycleEvidence &cycle, const UniformCycleLaneView &lane,
          const PackageBakeMethodGeometry &geometry, const std::size_t cycle_index,
          const bool require_available_torque) {
    if (cycle.state_transition_flags != 0U) {
        return error(
            UniformCycleBankErrorCode::cycle_rejected,
            cycle_path(cycle_index, "state_transition_flags"),
            "normal-running package cycles may not contain a state transition");
    }
    if (cycle.start_state_flags != kNormalRunningState ||
        cycle.end_state_flags != kNormalRunningState) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, "state_flags"),
                     "cycle is not stable ignition-on, fuel-on, dyno-on running");
    }
    if (require_available_torque &&
        cycle.instantaneous_net_shaft.availability !=
            contract::Availability::available) {
        return error(UniformCycleBankErrorCode::cycle_rejected,
                     cycle_path(cycle_index, "instantaneous_net_shaft"),
                     "cycle has no available modeled net-shaft torque summary");
    }

    auto start_result = aligned_boundary(cycle.start_boundary, lane, geometry,
                                         cycle_index, "start_boundary.delivery_frame");
    if (auto *failure = std::get_if<UniformCycleBankError>(&start_result)) {
        return std::move(*failure);
    }
    auto end_result = aligned_boundary(cycle.end_boundary, lane, geometry, cycle_index,
                                       "end_boundary.delivery_frame");
    if (auto *failure = std::get_if<UniformCycleBankError>(&end_result)) {
        return std::move(*failure);
    }
    const auto start = std::get<contract::AudioPackageSourceBoundary>(start_result);
    const auto end = std::get<contract::AudioPackageSourceBoundary>(end_result);
    if (!boundary_less(start, end)) {
        return error(UniformCycleBankErrorCode::malformed_evidence,
                     cycle_path(cycle_index, "end_boundary"),
                     "signal-aligned cycle has no positive tape extent");
    }
    if (auto failure = edge_rejection(start, lane, geometry, cycle_index,
                                      "start_boundary.delivery_frame")) {
        return std::move(*failure);
    }
    if (auto failure = edge_rejection(end, lane, geometry, cycle_index,
                                      "end_boundary.delivery_frame")) {
        return std::move(*failure);
    }
    return Candidate{&cycle, start, end};
}

[[nodiscard]] std::optional<UniformCycleBankError>
validate_request(const UniformCycleLaneView &lane,
                 const PackageBakeMethodGeometry &geometry,
                 const double load_coordinate) {
    if (lane.cycles.empty()) {
        return error(UniformCycleBankErrorCode::invalid_request, "lane.cycles",
                     "cycle lane is empty");
    }
    if (lane.pcm_frame_count == 0U) {
        return error(UniformCycleBankErrorCode::invalid_request, "lane.pcm_frame_count",
                     "PCM tape is empty");
    }
    if (!std::isfinite(geometry.rpm_grid_spacing) ||
        !(geometry.rpm_grid_spacing > 0.0) ||
        !std::isfinite(geometry.cycle_signal_alignment_frames) ||
        geometry.cycle_signal_alignment_frames < 0.0 ||
        !std::isfinite(geometry.maximum_assignment_error_rpm) ||
        geometry.maximum_assignment_error_rpm < 0.0 ||
        geometry.maximum_assignment_error_rpm > geometry.rpm_grid_spacing * 0.5 ||
        geometry.edge_guard_frames == 0U) {
        return error(UniformCycleBankErrorCode::invalid_request, "geometry",
                     "uniform-cycle geometry is invalid");
    }
    if (!std::isfinite(load_coordinate) || load_coordinate < -1.0 ||
        load_coordinate > 1.0) {
        return error(UniformCycleBankErrorCode::invalid_request, "load_coordinate",
                     "authored load coordinate must be finite and in [-1, 1]");
    }
    for (std::size_t index = 0; index < lane.cycles.size(); ++index) {
        if (auto failure = validate_evidence(lane.cycles[index], index)) {
            return failure;
        }
        if (index != 0U) {
            const auto &previous = lane.cycles[index - 1U];
            const auto &current = lane.cycles[index];
            if (previous.completed_cycle_ordinal >= current.completed_cycle_ordinal ||
                previous.end_boundary.delivery_frame >
                    current.start_boundary.delivery_frame) {
                return error(
                    UniformCycleBankErrorCode::malformed_evidence, cycle_path(index),
                    "cycle evidence is not in increasing nonoverlapping source order");
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] contract::AudioPackageCycleUnit unit(const Candidate &candidate,
                                                   const double canonical_rpm,
                                                   const double load_coordinate) {
    const auto &cycle = *candidate.evidence;
    return {
        cycle.completed_cycle_ordinal,
        candidate.start,
        candidate.end,
        canonical_rpm,
        cycle.mean_engine_speed_rpm,
        canonical_zero(load_coordinate),
        cycle.instantaneous_net_shaft.availability ==
                contract::Availability::available
            ? std::optional<double>{canonical_zero(
                  cycle.instantaneous_net_shaft.cycle_mean_torque_nm)}
            : std::nullopt,
        canonical_zero(cycle.requested_throttle.time_weighted_mean_01),
        canonical_zero(cycle.resolved_engine_throttle.time_weighted_mean_01),
        cycle.end_state_flags,
        cycle.state_transition_flags,
    };
}

[[nodiscard]] std::variant<std::vector<Candidate>, UniformCycleBankError>
safe_candidates(const UniformCycleLaneView &lane,
                const PackageBakeMethodGeometry &geometry,
                std::uint64_t &rejected_count,
                const bool require_available_torque) {
    std::vector<Candidate> candidates;
    candidates.reserve(lane.cycles.size());
    std::optional<UniformCycleBankError> first_rejection;
    for (std::size_t index = 0; index < lane.cycles.size(); ++index) {
        auto result = candidate(lane.cycles[index], lane, geometry, index,
                                require_available_torque);
        if (auto *failure = std::get_if<UniformCycleBankError>(&result)) {
            if (failure->code == UniformCycleBankErrorCode::malformed_evidence) {
                return std::move(*failure);
            }
            ++rejected_count;
            if (!first_rejection.has_value()) {
                first_rejection = std::move(*failure);
            }
            continue;
        }
        candidates.push_back(std::get<Candidate>(result));
    }
    if (candidates.empty()) {
        return first_rejection.value_or(
            error(UniformCycleBankErrorCode::cycle_rejected, "lane.cycles",
                  "lane contains no safe complete normal-running cycles"));
    }
    return candidates;
}

} // namespace

UniformCycleBankResult assign_uniform_running_cycle_bank(
    const UniformRunningCycleBankRequest &request) noexcept {
    try {
        if (auto failure = validate_request(request.lane, request.geometry,
                                            request.load_coordinate)) {
            return *failure;
        }
        if (!std::isfinite(request.padded_minimum_rpm) ||
            !std::isfinite(request.padded_maximum_rpm) ||
            !(request.padded_minimum_rpm > 0.0) ||
            !(request.padded_maximum_rpm > request.padded_minimum_rpm)) {
            return error(UniformCycleBankErrorCode::invalid_request, "rpm_range",
                         "padded RPM range must be finite, positive, and ascending");
        }
        if (request.direction != authoring::PackageBakeRunningDirection::rising &&
            request.direction != authoring::PackageBakeRunningDirection::falling) {
            return error(UniformCycleBankErrorCode::invalid_request, "direction",
                         "running direction is not admitted");
        }
        const double interval_count =
            (request.padded_maximum_rpm - request.padded_minimum_rpm) /
            request.geometry.rpm_grid_spacing;
        const double rounded_intervals = std::round(interval_count);
        if (!std::isfinite(interval_count) ||
            std::abs(interval_count - rounded_intervals) > kGridClosureTolerance ||
            rounded_intervals < 1.0 ||
            rounded_intervals >=
                static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            return error(UniformCycleBankErrorCode::invalid_request, "rpm_range",
                         "padded RPM bounds do not close on the uniform grid");
        }
        const auto row_count = static_cast<std::size_t>(rounded_intervals) + 1U;

        std::uint64_t rejected_count = 0U;
        auto candidate_result = safe_candidates(request.lane, request.geometry,
                                                rejected_count, true);
        if (auto *failure = std::get_if<UniformCycleBankError>(&candidate_result)) {
            return std::move(*failure);
        }
        auto candidates = std::get<std::vector<Candidate>>(std::move(candidate_result));
        if (request.direction == authoring::PackageBakeRunningDirection::falling) {
            std::reverse(candidates.begin(), candidates.end());
        }
        if (candidates.size() < row_count) {
            return error(
                UniformCycleBankErrorCode::impossible_coverage, "lane.cycles",
                "fewer safe unique source cycles exist than canonical RPM rows");
        }
        if (row_count + 1U >
            std::numeric_limits<std::size_t>::max() / (candidates.size() + 1U)) {
            return error(UniformCycleBankErrorCode::resource_limit, "lane.cycles",
                         "cycle-assignment decision matrix exceeds addressable memory");
        }

        constexpr double infinity = std::numeric_limits<double>::infinity();
        std::vector<double> previous(candidates.size() + 1U, 0.0);
        std::vector<double> current(candidates.size() + 1U, infinity);
        std::vector<std::uint8_t> take((row_count + 1U) * (candidates.size() + 1U), 0U);
        for (std::size_t row = 1U; row <= row_count; ++row) {
            std::fill(current.begin(), current.end(), infinity);
            const double canonical_rpm =
                request.padded_minimum_rpm +
                request.geometry.rpm_grid_spacing * static_cast<double>(row - 1U);
            for (std::size_t count = 1U; count <= candidates.size(); ++count) {
                current[count] = current[count - 1U];
                const double difference =
                    candidates[count - 1U].evidence->mean_engine_speed_rpm -
                    canonical_rpm;
                if (std::abs(difference) <=
                        request.geometry.maximum_assignment_error_rpm &&
                    std::isfinite(previous[count - 1U])) {
                    const double assigned =
                        previous[count - 1U] + difference * difference;
                    // Strict comparison retains the earliest source choice on a
                    // mathematically equal total-error tie.
                    if (assigned < current[count]) {
                        current[count] = assigned;
                        take[row * (candidates.size() + 1U) + count] = 1U;
                    }
                }
            }
            std::swap(previous, current);
        }
        if (!std::isfinite(previous.back())) {
            return error(UniformCycleBankErrorCode::impossible_coverage, "lane.cycles",
                         "no order-preserving one-to-one cycle assignment covers every "
                         "RPM row within the maximum error");
        }

        std::vector<std::size_t> selected(row_count);
        std::size_t row = row_count;
        std::size_t count = candidates.size();
        while (row != 0U) {
            if (count == 0U) {
                return error(UniformCycleBankErrorCode::impossible_coverage,
                             "lane.cycles",
                             "cycle assignment backtracking exhausted its source pool");
            }
            if (take[row * (candidates.size() + 1U) + count] != 0U) {
                selected[row - 1U] = count - 1U;
                --row;
            }
            --count;
        }

        UniformCycleBank bank;
        const auto &first_torque =
            candidates[selected.front()].evidence->instantaneous_net_shaft;
        bank.load_calibration = {
            first_torque.completeness,
            first_torque.included_terms,
            first_torque.omitted_terms,
        };
        bank.total_squared_rpm_error = previous.back();
        bank.rejected_cycle_count = rejected_count;
        bank.units.reserve(row_count);
        for (std::size_t index = 0; index < row_count; ++index) {
            const auto &torque =
                candidates[selected[index]].evidence->instantaneous_net_shaft;
            if (contract::AudioPackageLoadCalibration{
                    torque.completeness,
                    torque.included_terms,
                    torque.omitted_terms,
                } != bank.load_calibration) {
                return error(
                    UniformCycleBankErrorCode::impossible_coverage,
                    "lane.cycles",
                    "selected RPM rows do not share one modeled net-torque term partition");
            }
            const double canonical_rpm =
                request.padded_minimum_rpm +
                request.geometry.rpm_grid_spacing * static_cast<double>(index);
            bank.units.push_back(unit(candidates[selected[index]], canonical_rpm,
                                      request.load_coordinate));
        }
        return bank;
    } catch (const std::bad_alloc &) {
        return error(UniformCycleBankErrorCode::resource_limit, "",
                     "allocation failed while assigning the uniform cycle bank");
    } catch (...) {
        return error(UniformCycleBankErrorCode::internal_failure, "",
                     "unexpected failure while assigning the uniform cycle bank");
    }
}

UniformCycleBankResult
retain_uniform_idle_cycle_pool(const UniformIdleCyclePoolRequest &request) noexcept {
    try {
        if (auto failure = validate_request(request.lane, request.geometry,
                                            request.load_coordinate)) {
            return *failure;
        }
        if (!std::isfinite(request.playback_idle_rpm) ||
            !(request.playback_idle_rpm > 0.0)) {
            return error(UniformCycleBankErrorCode::invalid_request,
                         "playback_idle_rpm",
                         "idle target RPM must be finite and positive");
        }
        std::uint64_t rejected_count = 0U;
        auto candidate_result = safe_candidates(request.lane, request.geometry,
                                                rejected_count, false);
        if (auto *failure = std::get_if<UniformCycleBankError>(&candidate_result)) {
            return std::move(*failure);
        }
        const auto &candidates = std::get<std::vector<Candidate>>(candidate_result);
        UniformCycleBank bank;
        bank.rejected_cycle_count = rejected_count;
        bank.units.reserve(candidates.size());
        for (const auto &candidate : candidates) {
            // Idle is not projected onto the directional grid. Retaining each
            // unit's measured pitch keeps the dense pool selectable without
            // inventing identical coordinates for distinct source cycles.
            bank.units.push_back(unit(candidate,
                                      candidate.evidence->mean_engine_speed_rpm,
                                      request.load_coordinate));
        }
        return bank;
    } catch (const std::bad_alloc &) {
        return error(UniformCycleBankErrorCode::resource_limit, "",
                     "allocation failed while retaining the idle cycle pool");
    } catch (...) {
        return error(UniformCycleBankErrorCode::internal_failure, "",
                     "unexpected failure while retaining the idle cycle pool");
    }
}

} // namespace engine_sim_offline::package_detail
