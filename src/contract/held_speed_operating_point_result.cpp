#include "engine_sim_offline/contract/result.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::contract {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;
constexpr double kFrozenLegacyRpmScale = 0.104719755;
constexpr RationalRateHz kFrozenMechanicsRate{10000, 1};

[[nodiscard]] bool same_binary64(double lhs, double rhs) noexcept {
    return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
}

[[nodiscard]] bool canonical_positive_zero(double value) noexcept {
    return value == 0.0 && !std::signbit(value);
}

[[nodiscard]] ValidationReport
validate_boundary(const OperatingPointBoundaryEvidence &boundary) {
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::require;

    ValidationReport report;
    require(report,
            boundary.left_bracket_sample_index <= boundary.right_bracket_sample_index,
            ContractIssueCode::inconsistent_shape, "right_bracket_sample_index",
            "cycle-boundary sample brackets must be ordered");
    require(report,
            finite_nonnegative(boundary.scenario_time_s) &&
                finite(boundary.theta_unwrapped_rad),
            ContractIssueCode::invalid_value, "",
            "cycle-boundary time must be nonnegative and angle must be finite");
    require(report, detail::unit_interval(boundary.fraction_from_left_01),
            ContractIssueCode::invalid_value, "fraction_from_left_01",
            "cycle-boundary interpolation fraction must be in [0, 1]");

    if (boundary.left_bracket_sample_index == boundary.right_bracket_sample_index) {
        require(report, canonical_positive_zero(boundary.fraction_from_left_01),
                ContractIssueCode::inconsistent_semantics, "fraction_from_left_01",
                "an exact-sample boundary requires canonical positive-zero "
                "interpolation fraction");
    } else {
        require(report,
                boundary.left_bracket_sample_index !=
                        std::numeric_limits<std::uint64_t>::max() &&
                    boundary.right_bracket_sample_index ==
                        boundary.left_bracket_sample_index + 1,
                ContractIssueCode::inconsistent_shape, "right_bracket_sample_index",
                "an interpolated boundary must retain adjacent post-step "
                "samples");
        require(report,
                boundary.fraction_from_left_01 > 0.0 &&
                    boundary.fraction_from_left_01 < 1.0,
                ContractIssueCode::inconsistent_semantics, "fraction_from_left_01",
                "a bracketed boundary requires a strict interior fraction");
    }
    return report;
}

[[nodiscard]] std::optional<std::int64_t>
boundary_cycle_index(double theta_unwrapped_rad,
                     double cycle_reference_theta_rad) noexcept {
    const double candidate = std::round(
        (theta_unwrapped_rad - cycle_reference_theta_rad) / kFourStrokeCycleRadians);
    constexpr double kMaximumExactInteger = 9007199254740991.0;
    if (!detail::finite(candidate) || candidate < -kMaximumExactInteger ||
        candidate > kMaximumExactInteger) {
        return std::nullopt;
    }
    const auto cycle_index = static_cast<std::int64_t>(candidate);
    const double reconstructed =
        cycle_reference_theta_rad +
        static_cast<double>(cycle_index) * kFourStrokeCycleRadians;
    if (!same_binary64(theta_unwrapped_rad, reconstructed)) {
        return std::nullopt;
    }
    return cycle_index;
}

[[nodiscard]] ValidationReport
validate_cycle_range(const CompletedCycleRangeEvidence &cycles,
                     double cycle_reference_theta_rad) {
    using detail::append_prefixed;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate_boundary(cycles.start_boundary), "start_boundary");
    append_prefixed(report, validate_boundary(cycles.end_boundary), "end_boundary");
    require(report, cycles.completed_cycle_count > 0, ContractIssueCode::invalid_value,
            "completed_cycle_count",
            "a cycle block must retain at least one complete cycle");
    require(report,
            cycles.first_completed_cycle_ordinal <= cycles.last_completed_cycle_ordinal,
            ContractIssueCode::inconsistent_shape, "last_completed_cycle_ordinal",
            "completed-cycle ordinals must form an ordered range");
    if (cycles.first_completed_cycle_ordinal <= cycles.last_completed_cycle_ordinal) {
        const auto ordinal_span =
            cycles.last_completed_cycle_ordinal - cycles.first_completed_cycle_ordinal;
        require(report,
                ordinal_span < std::numeric_limits<std::uint64_t>::max() &&
                    ordinal_span + 1 ==
                        static_cast<std::uint64_t>(cycles.completed_cycle_count),
                ContractIssueCode::inconsistent_shape, "completed_cycle_count",
                "completed-cycle count must exactly cover the ordinal range");
    }
    require(report,
            cycles.start_boundary.scenario_time_s <
                    cycles.end_boundary.scenario_time_s &&
                finite_positive(cycles.end_boundary.scenario_time_s -
                                cycles.start_boundary.scenario_time_s),
            ContractIssueCode::inconsistent_semantics, "end_boundary.scenario_time_s",
            "a completed-cycle block must have strictly positive duration");

    const auto start_cycle_index = boundary_cycle_index(
        cycles.start_boundary.theta_unwrapped_rad, cycle_reference_theta_rad);
    const auto end_cycle_index = boundary_cycle_index(
        cycles.end_boundary.theta_unwrapped_rad, cycle_reference_theta_rad);
    require(report, start_cycle_index.has_value(),
            ContractIssueCode::inconsistent_semantics,
            "start_boundary.theta_unwrapped_rad",
            "cycle boundary must lie exactly on the four-stroke reference "
            "lattice");
    require(report, end_cycle_index.has_value(),
            ContractIssueCode::inconsistent_semantics,
            "end_boundary.theta_unwrapped_rad",
            "cycle boundary must lie exactly on the four-stroke reference "
            "lattice");
    if (start_cycle_index.has_value() && end_cycle_index.has_value()) {
        require(report,
                *end_cycle_index >= *start_cycle_index &&
                    static_cast<std::uint64_t>(*end_cycle_index - *start_cycle_index) ==
                        static_cast<std::uint64_t>(cycles.completed_cycle_count),
                ContractIssueCode::inconsistent_semantics,
                "end_boundary.theta_unwrapped_rad",
                "cycle-block lattice indices must contain exactly the "
                "declared four-stroke cycles");
    }
    require(report,
            cycles.start_boundary.right_bracket_sample_index <=
                cycles.end_boundary.left_bracket_sample_index,
            ContractIssueCode::inconsistent_semantics, "end_boundary",
            "cycle-block boundary evidence must advance in sample order");
    return report;
}

void validate_complete_torque(ValidationReport &report, const TorqueValueNm &value,
                              TorqueTermMask expected_terms, std::string_view path) {
    detail::append_prefixed(report, validate(value), path);
    detail::require(
        report,
        value.availability == Availability::available &&
            value.completeness == Completeness::complete &&
            value.unavailable_reason == QuantityUnavailableReason::none &&
            value.included_terms == expected_terms && value.omitted_terms == 0,
        ContractIssueCode::inconsistent_semantics, std::string(path),
        "cycle-mean torque must be available, complete, and own exactly its "
        "declared physical term scope");
}

[[nodiscard]] ValidationReport
validate_torque_breakdown(const CycleMeanTorqueBreakdown &torque) {
    using detail::require;

    ValidationReport report;
    validate_complete_torque(report, torque.indicated_gas,
                             indicated_gas_torque_term_mask(), "indicated_gas");
    validate_complete_torque(report, torque.aggregate_loss,
                             friction_pump_and_accessory_torque_term_mask(),
                             "aggregate_loss");
    validate_complete_torque(report, torque.starter,
                             torque_term_mask(TorqueTerm::starter), "starter");
    validate_complete_torque(report, torque.net_shaft, known_torque_term_mask(),
                             "net_shaft");
    require(report, torque.aggregate_loss.value_nm < 0.0,
            ContractIssueCode::inconsistent_semantics, "aggregate_loss.value_nm",
            "positive-speed aggregate loss must be a negative torque "
            "contribution");
    require(report, canonical_positive_zero(torque.starter.value_nm),
            ContractIssueCode::inconsistent_semantics, "starter.value_nm",
            "mechanically disengaged starter torque must be canonical "
            "positive zero");
    return report;
}

[[nodiscard]] ValidationReport
validate_pressure_means(const std::vector<MeanBoundaryPressurePa> &pressures) {
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(report, !pressures.empty(), ContractIssueCode::missing_value, "",
            "a convergence block requires every physical gas-volume pressure");
    for (std::size_t index = 0; index < pressures.size(); ++index) {
        const auto &pressure = pressures[index];
        const auto path = "[" + std::to_string(index) + "]";
        require(report, pressure.gas_volume_id.valid(),
                ContractIssueCode::invalid_value, path + ".gas_volume_id",
                "boundary-pressure gas-volume ID must be nonzero");
        require(report, finite_positive(pressure.pressure_pa_abs),
                ContractIssueCode::invalid_value, path + ".pressure_pa_abs",
                "mean absolute boundary pressure must be finite and positive");
        if (index != 0) {
            require(report, pressures[index - 1].gas_volume_id < pressure.gas_volume_id,
                    ContractIssueCode::inconsistent_shape, path + ".gas_volume_id",
                    "boundary-pressure means must use strictly ascending stable "
                    "gas-volume IDs");
        }
    }
    return report;
}

[[nodiscard]] ValidationReport
validate_end_boundary_pressures(const std::vector<EndBoundaryPressurePa> &pressures) {
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(report, !pressures.empty(), ContractIssueCode::missing_value, "",
            "a completed cycle requires every physical end-boundary pressure");
    for (std::size_t index = 0; index < pressures.size(); ++index) {
        const auto &pressure = pressures[index];
        const auto path = "[" + std::to_string(index) + "]";
        require(report, pressure.gas_volume_id.valid(),
                ContractIssueCode::invalid_value, path + ".gas_volume_id",
                "end-boundary pressure gas-volume ID must be nonzero");
        require(report, finite_positive(pressure.pressure_pa_abs),
                ContractIssueCode::invalid_value, path + ".pressure_pa_abs",
                "end-boundary absolute pressure must be finite and positive");
        if (index != 0) {
            require(report, pressures[index - 1].gas_volume_id < pressure.gas_volume_id,
                    ContractIssueCode::inconsistent_shape, path + ".gas_volume_id",
                    "end-boundary pressures must use strictly ascending stable "
                    "gas-volume IDs");
        }
    }
    return report;
}

struct WorkSums {
    double indicated_gas_work_j = 0.0;
    double aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
};

[[nodiscard]] ValidationReport
validate_completed_cycle_evidence(const HeldSpeedCycleBlockEvidence &block) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(report,
            block.completed_cycles.size() ==
                static_cast<std::size_t>(block.cycles.completed_cycle_count),
            ContractIssueCode::inconsistent_shape, "",
            "per-cycle work evidence must contain exactly the declared complete "
            "cycles");

    WorkSums sums;
    std::vector<GasVolumeId> pressure_ids;
    std::vector<double> pressure_sums;
    for (std::size_t index = 0; index < block.completed_cycles.size(); ++index) {
        const auto &cycle = block.completed_cycles[index];
        const auto path = "completed_cycles[" + std::to_string(index) + "]";
        const bool ordinal_representable =
            block.cycles.first_completed_cycle_ordinal <=
            std::numeric_limits<std::uint64_t>::max() - index;
        require(report,
                ordinal_representable &&
                    cycle.completed_cycle_ordinal ==
                        block.cycles.first_completed_cycle_ordinal + index,
                ContractIssueCode::inconsistent_semantics,
                path + ".completed_cycle_ordinal",
                "per-cycle work ordinals must exactly cover the block in "
                "written order");
        require(report,
                finite(cycle.indicated_gas_work_j) &&
                    finite_positive(cycle.aggregate_loss_work_j) &&
                    canonical_positive_zero(cycle.starter_work_j) &&
                    finite(cycle.brake_work_j),
                ContractIssueCode::invalid_value, path,
                "cycle work must be finite, aggregate loss must be positive, "
                "and disengaged-starter work must be canonical positive zero");
        const double canonical_brake_work_j =
            (cycle.indicated_gas_work_j - cycle.aggregate_loss_work_j) +
            cycle.starter_work_j;
        require(report,
                finite(canonical_brake_work_j) &&
                    same_binary64(cycle.brake_work_j, canonical_brake_work_j),
                ContractIssueCode::inconsistent_semantics, path + ".brake_work_j",
                "cycle brake work must exactly reproduce (indicated minus "
                "aggregate loss) plus starter in the declared evaluation "
                "order");
        append_prefixed(report,
                        validate_end_boundary_pressures(cycle.end_boundary_pressures),
                        path + ".end_boundary_pressures");

        if (index == 0) {
            pressure_ids.reserve(cycle.end_boundary_pressures.size());
            pressure_sums.assign(cycle.end_boundary_pressures.size(), 0.0);
            for (const auto &pressure : cycle.end_boundary_pressures) {
                pressure_ids.push_back(pressure.gas_volume_id);
            }
        } else {
            require(report, cycle.end_boundary_pressures.size() == pressure_ids.size(),
                    ContractIssueCode::inconsistent_shape,
                    path + ".end_boundary_pressures",
                    "every completed cycle must retain the same physical "
                    "end-boundary pressure shape");
        }
        const auto pressure_count =
            std::min(cycle.end_boundary_pressures.size(), pressure_ids.size());
        for (std::size_t pressure_index = 0; pressure_index < pressure_count;
             ++pressure_index) {
            const auto &pressure = cycle.end_boundary_pressures[pressure_index];
            const auto pressure_path = path + ".end_boundary_pressures[" +
                                       std::to_string(pressure_index) + "]";
            require(report, pressure.gas_volume_id == pressure_ids[pressure_index],
                    ContractIssueCode::inconsistent_semantics,
                    pressure_path + ".gas_volume_id",
                    "completed-cycle pressure lanes must retain identical "
                    "ascending gas-volume identity");
            pressure_sums[pressure_index] += pressure.pressure_pa_abs;
            require(report, finite(pressure_sums[pressure_index]),
                    ContractIssueCode::invalid_value,
                    pressure_path + ".pressure_pa_abs",
                    "stable left-to-right end-boundary pressure reduction "
                    "overflowed");
        }

        sums.indicated_gas_work_j += cycle.indicated_gas_work_j;
        sums.aggregate_loss_work_j += cycle.aggregate_loss_work_j;
        sums.starter_work_j += cycle.starter_work_j;
        sums.brake_work_j += cycle.brake_work_j;
        require(report,
                finite(sums.indicated_gas_work_j) &&
                    finite(sums.aggregate_loss_work_j) && finite(sums.starter_work_j) &&
                    finite(sums.brake_work_j),
                ContractIssueCode::invalid_value, path,
                "stable left-to-right cycle-work reduction overflowed");
    }
    require(
        report,
        same_binary64(block.indicated_gas_work_j, sums.indicated_gas_work_j) &&
            same_binary64(block.aggregate_loss_work_j, sums.aggregate_loss_work_j) &&
            same_binary64(block.starter_work_j, sums.starter_work_j) &&
            same_binary64(block.brake_work_j, sums.brake_work_j),
        ContractIssueCode::inconsistent_semantics, "",
        "all four block work totals must exactly reproduce the retained "
        "per-cycle values by stable left-to-right reduction from canonical "
        "positive zero");

    require(report, block.mean_boundary_pressures.size() == pressure_ids.size(),
            ContractIssueCode::inconsistent_shape, "mean_boundary_pressures",
            "published pressure means must contain exactly the retained "
            "completed-cycle pressure lanes");
    const auto mean_count =
        std::min(block.mean_boundary_pressures.size(), pressure_ids.size());
    const double cycle_count = static_cast<double>(block.cycles.completed_cycle_count);
    for (std::size_t pressure_index = 0; pressure_index < mean_count;
         ++pressure_index) {
        const auto &published = block.mean_boundary_pressures[pressure_index];
        const auto path =
            "mean_boundary_pressures[" + std::to_string(pressure_index) + "]";
        require(report, published.gas_volume_id == pressure_ids[pressure_index],
                ContractIssueCode::inconsistent_semantics, path + ".gas_volume_id",
                "published pressure means must retain the exact completed-cycle "
                "pressure-lane identities");
        const double canonical_mean = pressure_sums[pressure_index] / cycle_count;
        require(report,
                finite(canonical_mean) &&
                    same_binary64(published.pressure_pa_abs, canonical_mean),
                ContractIssueCode::inconsistent_semantics, path + ".pressure_pa_abs",
                "published pressure mean must exactly reproduce the canonical "
                "+0-seeded left-to-right completed-cycle reduction divided by "
                "the declared binary64 cycle count");
    }
    return report;
}

[[nodiscard]] ValidationReport
validate_cycle_block(const HeldSpeedCycleBlockEvidence &block,
                     double total_displacement_m3, double cycle_reference_theta_rad) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    append_prefixed(report,
                    validate_cycle_range(block.cycles, cycle_reference_theta_rad),
                    "cycles");
    report.append(validate_completed_cycle_evidence(block));
    append_prefixed(report, validate_torque_breakdown(block.cycle_mean_torque),
                    "cycle_mean_torque");
    append_prefixed(report, validate_pressure_means(block.mean_boundary_pressures),
                    "mean_boundary_pressures");
    require(report,
            finite(block.indicated_gas_work_j) &&
                finite_positive(block.aggregate_loss_work_j) &&
                canonical_positive_zero(block.starter_work_j) &&
                finite(block.brake_work_j) && finite(block.net_bmep_pa) &&
                finite(block.mean_power_w),
            ContractIssueCode::invalid_value, "",
            "block work, BMEP, and power values must be finite; aggregate loss "
            "must be positive and starter work must be canonical positive zero");

    const double angle_range = static_cast<double>(block.cycles.completed_cycle_count) *
                               kFourStrokeCycleRadians;
    const double displacement_range =
        static_cast<double>(block.cycles.completed_cycle_count) * total_displacement_m3;
    const double duration_s = block.cycles.end_boundary.scenario_time_s -
                              block.cycles.start_boundary.scenario_time_s;
    if (finite_positive(angle_range)) {
        require(report,
                same_binary64(block.cycle_mean_torque.indicated_gas.value_nm,
                              block.indicated_gas_work_j / angle_range),
                ContractIssueCode::inconsistent_semantics,
                "cycle_mean_torque.indicated_gas.value_nm",
                "indicated mean torque must be the exact binary64 division of "
                "indicated work by block angle");
        require(report,
                same_binary64(block.cycle_mean_torque.aggregate_loss.value_nm,
                              -block.aggregate_loss_work_j / angle_range),
                ContractIssueCode::inconsistent_semantics,
                "cycle_mean_torque.aggregate_loss.value_nm",
                "aggregate-loss mean torque must be the exact binary64 "
                "division of negative loss work by block angle");
        require(report,
                same_binary64(block.cycle_mean_torque.starter.value_nm,
                              block.starter_work_j / angle_range),
                ContractIssueCode::inconsistent_semantics,
                "cycle_mean_torque.starter.value_nm",
                "starter mean torque must be the exact binary64 division of "
                "starter work by block angle");
        require(report,
                same_binary64(block.cycle_mean_torque.net_shaft.value_nm,
                              block.brake_work_j / angle_range),
                ContractIssueCode::inconsistent_semantics,
                "cycle_mean_torque.net_shaft.value_nm",
                "net mean torque must be the exact binary64 division of brake "
                "work by block angle");
    }
    if (finite_positive(displacement_range)) {
        require(
            report,
            same_binary64(block.net_bmep_pa, block.brake_work_j / displacement_range),
            ContractIssueCode::inconsistent_semantics, "net_bmep_pa",
            "net BMEP must be the exact binary64 division of total block "
            "brake work by cycle count times total displacement");
    }
    if (finite_positive(duration_s)) {
        require(report,
                same_binary64(block.mean_power_w, block.brake_work_j / duration_s),
                ContractIssueCode::inconsistent_semantics, "mean_power_w",
                "mean power must be the exact binary64 division of brake work "
                "by retained cycle duration");
    }
    return report;
}

[[nodiscard]] ValidationReport
validate_operating_conditions(const HeldSpeedOperatingPointConditions &conditions) {
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(report, is_valid_semantic_id(conditions.engine_profile_id),
            ContractIssueCode::invalid_value, "engine_profile_id",
            "operating-point engine profile ID must be canonical");
    require(report,
            finite_positive(conditions.engine_speed_rpm) &&
                finite(conditions.initial_theta_unwrapped_rad) &&
                finite(conditions.cycle_reference_theta_rad) &&
                detail::unit_interval(conditions.throttle_01),
            ContractIssueCode::invalid_value, "",
            "held-speed RPM, crank angle, and throttle are invalid");
    require(report, conditions.physics_rate_hz == kFrozenMechanicsRate,
            ContractIssueCode::unsupported_value, "physics_rate_hz",
            "operating-point evidence requires the frozen 10000 Hz mechanics "
            "sample grid");
    require(report,
            finite_positive(conditions.ambient.pressure_pa_abs) &&
                finite_positive(conditions.ambient.temperature_k) &&
                detail::unit_interval(conditions.ambient.relative_humidity_01),
            ContractIssueCode::invalid_value, "ambient",
            "held-speed ambient conditions are invalid");
    require(report,
            is_valid_semantic_id(conditions.fuel.fuel_id) &&
                finite_positive(conditions.fuel.lower_heating_value_j_per_kg) &&
                finite_positive(conditions.fuel.stoichiometric_air_fuel_mass_ratio),
            ContractIssueCode::invalid_value, "fuel",
            "held-speed fuel conditions are invalid");
    require(
        report,
        finite_positive(conditions.initial_thermal_state.gas_temperature_k) &&
            finite_positive(conditions.initial_thermal_state.wall_temperature_k) &&
            finite_positive(conditions.initial_thermal_state.coolant_temperature_k) &&
            finite_positive(conditions.initial_thermal_state.oil_temperature_k),
        ContractIssueCode::invalid_value, "initial_thermal_state",
        "held-speed thermal conditions must be finite and positive");
    require(report,
            finite_positive(conditions.crankcase.pressure_pa_abs) &&
                finite_positive(conditions.crankcase.temperature_k),
            ContractIssueCode::invalid_value, "crankcase",
            "held-speed crankcase conditions are invalid");
    require(report, finite_positive(conditions.total_displacement_m3),
            ContractIssueCode::invalid_value, "total_displacement_m3",
            "operating-point displacement must be finite and positive");
    require(report,
            is_valid_semantic_id(conditions.accessory_configuration.configuration_id) &&
                !conditions.accessory_configuration.content_sha256.is_zero(),
            ContractIssueCode::invalid_value, "accessory_configuration",
            "operating-point accessory configuration must have a canonical ID "
            "and content identity");
    require(report,
            conditions.starter_mechanically_disengaged &&
                conditions.starter_included_terms ==
                    torque_term_mask(TorqueTerm::starter),
            ContractIssueCode::inconsistent_semantics, "starter",
            "operating-point starter must be mechanically disengaged and own "
            "exactly the starter term");
    return report;
}

struct ThetaEnvelope {
    double center_rad = 0.0;
    double absolute_error_bound_rad = std::numeric_limits<double>::infinity();
};

// Mechanics advances theta by one binary64 addition per post-step sample. This
// O(1) envelope compares retained evidence with the exact-real repeated-sum model.
// The Higham gamma bound uses epsilon (rather than unit roundoff) conservatively;
// direct center construction receives a separate arithmetic guard. If n*epsilon
// is too large for that theorem, the bound becomes infinite and the evidence is
// rejected as unverifiable rather than replaying or imposing an arbitrary cap.
[[nodiscard]] ThetaEnvelope theta_envelope(std::uint64_t sample_index,
                                           double initial_theta_rad,
                                           double step_rotation_rad) noexcept {
    if (sample_index == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const auto addition_count = sample_index + 1;
    const double n = static_cast<double>(addition_count);
    const double displacement = n * step_rotation_rad;
    const double center = initial_theta_rad + displacement;
    const double epsilon = std::numeric_limits<double>::epsilon();
    const long double n_epsilon =
        static_cast<long double>(addition_count) * static_cast<long double>(epsilon);
    double gamma = std::numeric_limits<double>::infinity();
    if (n_epsilon < 0.5L) {
        gamma = static_cast<double>(n_epsilon / (1.0L - n_epsilon));
    }
    const double scale = std::abs(initial_theta_rad) + std::abs(displacement);
    const double direct_arithmetic_guard = 8.0 * epsilon * std::max(1.0, scale);
    return {
        center,
        gamma * scale + direct_arithmetic_guard,
    };
}

[[nodiscard]] double sample_time_s(std::uint64_t sample_index,
                                   RationalRateHz rate) noexcept {
    return static_cast<double>(sample_index + 1) *
           static_cast<double>(rate.denominator) / static_cast<double>(rate.numerator);
}

[[nodiscard]] bool nonnegative_lattice_distance(std::int64_t lower, std::int64_t upper,
                                                std::uint64_t &distance) noexcept {
    if (upper < lower) {
        return false;
    }
    if (lower >= 0 || upper < 0) {
        distance = static_cast<std::uint64_t>(upper - lower);
        return true;
    }
    const auto negative_magnitude = static_cast<std::uint64_t>(-(lower + 1)) + 1;
    distance = negative_magnitude + static_cast<std::uint64_t>(upper);
    return true;
}

[[nodiscard]] std::optional<std::int64_t>
first_full_cycle_start_lattice(const HeldSpeedOperatingPointConditions &conditions,
                               double step_rotation_rad) noexcept {
    const double first_post_step_theta =
        conditions.initial_theta_unwrapped_rad + step_rotation_rad;
    const double preceding_index_value =
        std::floor((first_post_step_theta - conditions.cycle_reference_theta_rad) /
                   kFourStrokeCycleRadians);
    constexpr double kMaximumExactInteger = 9007199254740991.0;
    if (!detail::finite(preceding_index_value) ||
        preceding_index_value < -kMaximumExactInteger ||
        preceding_index_value > kMaximumExactInteger - 1.0) {
        return std::nullopt;
    }
    const auto preceding_index = static_cast<std::int64_t>(preceding_index_value);
    const double preceding_boundary =
        conditions.cycle_reference_theta_rad +
        static_cast<double>(preceding_index) * kFourStrokeCycleRadians;
    return first_post_step_theta == preceding_boundary ? preceding_index
                                                       : preceding_index + 1;
}

void validate_boundary_kinematics(ValidationReport &report,
                                  const OperatingPointBoundaryEvidence &boundary,
                                  const HeldSpeedOperatingPointConditions &conditions,
                                  double step_rotation_rad,
                                  std::uint64_t last_cutoff_sample_index,
                                  std::string_view path) {
    using detail::require;

    const auto field_path = [path](std::string_view field) {
        return std::string(path) + "." + std::string(field);
    };
    require(report, boundary.right_bracket_sample_index <= last_cutoff_sample_index,
            ContractIssueCode::inconsistent_semantics,
            field_path("right_bracket_sample_index"),
            "boundary brackets must be post-step mechanics samples at or "
            "before the fixed cutoff");
    if (boundary.right_bracket_sample_index > last_cutoff_sample_index) {
        return;
    }

    const auto left =
        theta_envelope(boundary.left_bracket_sample_index,
                       conditions.initial_theta_unwrapped_rad, step_rotation_rad);
    const auto right =
        theta_envelope(boundary.right_bracket_sample_index,
                       conditions.initial_theta_unwrapped_rad, step_rotation_rad);
    const bool finite_envelopes = detail::finite(left.center_rad) &&
                                  detail::finite(left.absolute_error_bound_rad) &&
                                  detail::finite(right.center_rad) &&
                                  detail::finite(right.absolute_error_bound_rad);
    require(report, finite_envelopes, ContractIssueCode::unsupported_value,
            field_path("right_bracket_sample_index"),
            "boundary sample index is too large for the documented binary64 "
            "sequential-addition error theorem");
    if (!finite_envelopes) {
        return;
    }

    const double target = boundary.theta_unwrapped_rad;
    if (boundary.left_bracket_sample_index == boundary.right_bracket_sample_index) {
        require(report,
                std::abs(target - right.center_rad) <= right.absolute_error_bound_rad,
                ContractIssueCode::inconsistent_semantics,
                field_path("theta_unwrapped_rad"),
                "exact-sample lattice angle lies outside the documented "
                "held-RPM accumulation-error envelope");
    } else {
        const double left_low = left.center_rad - left.absolute_error_bound_rad;
        const double left_high = left.center_rad + left.absolute_error_bound_rad;
        const double right_low = right.center_rad - right.absolute_error_bound_rad;
        const double right_high = right.center_rad + right.absolute_error_bound_rad;
        const double minimum_denominator = right_low - left_high;
        const double maximum_denominator = right_high - left_low;
        const bool denominator_proven = detail::finite(minimum_denominator) &&
                                        detail::finite(maximum_denominator) &&
                                        minimum_denominator > 0.0;
        require(report, denominator_proven, ContractIssueCode::inconsistent_semantics,
                field_path("right_bracket_sample_index"),
                "held-RPM error envelopes do not prove an ordered adjacent "
                "sample bracket");
        if (denominator_proven) {
            const double fraction_lower = (target - left_high) / maximum_denominator;
            const double fraction_upper = (target - left_low) / minimum_denominator;
            const double epsilon = std::numeric_limits<double>::epsilon();
            const double guard =
                16.0 * epsilon *
                std::max({1.0, std::abs(fraction_lower), std::abs(fraction_upper)});
            require(report,
                    target > left_low && target < right_high &&
                        boundary.fraction_from_left_01 >= fraction_lower - guard &&
                        boundary.fraction_from_left_01 <= fraction_upper + guard,
                    ContractIssueCode::inconsistent_semantics,
                    field_path("fraction_from_left_01"),
                    "boundary fraction lies outside interval arithmetic over "
                    "the documented held-RPM accumulation-error envelopes");
        }
    }

    const double left_time =
        sample_time_s(boundary.left_bracket_sample_index, conditions.physics_rate_hz);
    const double right_time =
        sample_time_s(boundary.right_bracket_sample_index, conditions.physics_rate_hz);
    const double expected_time =
        boundary.left_bracket_sample_index == boundary.right_bracket_sample_index
            ? right_time
            : left_time + boundary.fraction_from_left_01 * (right_time - left_time);
    require(report, same_binary64(boundary.scenario_time_s, expected_time),
            ContractIssueCode::inconsistent_semantics, field_path("scenario_time_s"),
            "boundary time must exactly reproduce direct fixed-rate post-step "
            "sample-time interpolation");
}

void validate_block_ordinal_lattice(ValidationReport &report,
                                    const HeldSpeedCycleBlockEvidence &block,
                                    std::int64_t first_full_start_lattice,
                                    double cycle_reference_theta_rad,
                                    std::string_view path) {
    using detail::require;

    const auto start_index = boundary_cycle_index(
        block.cycles.start_boundary.theta_unwrapped_rad, cycle_reference_theta_rad);
    const auto end_index = boundary_cycle_index(
        block.cycles.end_boundary.theta_unwrapped_rad, cycle_reference_theta_rad);
    if (!start_index.has_value() || !end_index.has_value()) {
        return;
    }

    std::uint64_t start_distance = 0;
    std::uint64_t end_distance = 0;
    const bool start_ordered = nonnegative_lattice_distance(
        first_full_start_lattice, *start_index, start_distance);
    const bool end_ordered = nonnegative_lattice_distance(first_full_start_lattice,
                                                          *end_index, end_distance);
    const bool last_has_successor = block.cycles.last_completed_cycle_ordinal !=
                                    std::numeric_limits<std::uint64_t>::max();
    require(report,
            start_ordered && end_ordered && last_has_successor &&
                start_distance == block.cycles.first_completed_cycle_ordinal &&
                end_distance == block.cycles.last_completed_cycle_ordinal + 1,
            ContractIssueCode::inconsistent_semantics, std::string(path) + ".cycles",
            "completed-cycle ordinals must map exactly to the boundary lattice "
            "derived from the first post-step held sample and discarded initial "
            "partial cycle");
}

void validate_held_speed_kinematics(ValidationReport &report,
                                    const HeldSpeedOperatingPointConditions &conditions,
                                    const HeldSpeedConvergenceEvidence &convergence) {
    using detail::require;

    const auto cutoff_frame_count = resolve_frame_index(convergence.fixed_cutoff_time_s,
                                                        conditions.physics_rate_hz);
    require(report, cutoff_frame_count.has_value() && *cutoff_frame_count > 0,
            ContractIssueCode::inconsistent_semantics,
            "convergence.fixed_cutoff_time_s",
            "fixed cutoff must resolve to a positive integral mechanics-frame "
            "count");
    if (!cutoff_frame_count.has_value() || *cutoff_frame_count == 0) {
        return;
    }
    const auto last_cutoff_sample_index = *cutoff_frame_count - 1;

    const double angular_speed_rad_s =
        conditions.engine_speed_rpm * kFrozenLegacyRpmScale;
    const double step_s = static_cast<double>(conditions.physics_rate_hz.denominator) /
                          static_cast<double>(conditions.physics_rate_hz.numerator);
    const double step_rotation_rad = angular_speed_rad_s * step_s;
    require(report,
            detail::finite(angular_speed_rad_s) && angular_speed_rad_s > 0.0 &&
                detail::finite(step_rotation_rad) && step_rotation_rad > 0.0 &&
                step_rotation_rad < kFourStrokeCycleRadians,
            ContractIssueCode::invalid_value, "conditions.engine_speed_rpm",
            "held RPM must produce a finite positive sub-cycle mechanics step");
    if (!(detail::finite(step_rotation_rad) && step_rotation_rad > 0.0 &&
          step_rotation_rad < kFourStrokeCycleRadians)) {
        return;
    }

    validate_boundary_kinematics(report, convergence.block_a.cycles.start_boundary,
                                 conditions, step_rotation_rad,
                                 last_cutoff_sample_index,
                                 "convergence.block_a.cycles.start_boundary");
    validate_boundary_kinematics(
        report, convergence.block_a.cycles.end_boundary, conditions, step_rotation_rad,
        last_cutoff_sample_index, "convergence.block_a.cycles.end_boundary");
    validate_boundary_kinematics(report, convergence.block_b.cycles.start_boundary,
                                 conditions, step_rotation_rad,
                                 last_cutoff_sample_index,
                                 "convergence.block_b.cycles.start_boundary");
    validate_boundary_kinematics(
        report, convergence.block_b.cycles.end_boundary, conditions, step_rotation_rad,
        last_cutoff_sample_index, "convergence.block_b.cycles.end_boundary");
    validate_boundary_kinematics(
        report, convergence.last_eligible_cycle_end_boundary_at_fixed_cutoff,
        conditions, step_rotation_rad, last_cutoff_sample_index,
        "convergence."
        "last_eligible_cycle_end_boundary_at_fixed_cutoff");

    const auto first_full_start =
        first_full_cycle_start_lattice(conditions, step_rotation_rad);
    require(report, first_full_start.has_value(), ContractIssueCode::invalid_value,
            "conditions.initial_theta_unwrapped_rad",
            "first post-step held state cannot derive a representable "
            "four-stroke lattice origin");
    if (first_full_start.has_value()) {
        validate_block_ordinal_lattice(report, convergence.block_a, *first_full_start,
                                       conditions.cycle_reference_theta_rad,
                                       "convergence.block_a");
        validate_block_ordinal_lattice(report, convergence.block_b, *first_full_start,
                                       conditions.cycle_reference_theta_rad,
                                       "convergence.block_b");
    }

    const auto validate_duration_coherence = [&](const HeldSpeedCycleBlockEvidence
                                                     &block,
                                                 std::string_view path) {
        const double actual_duration_s = block.cycles.end_boundary.scenario_time_s -
                                         block.cycles.start_boundary.scenario_time_s;
        const double angle_range_rad = block.cycles.end_boundary.theta_unwrapped_rad -
                                       block.cycles.start_boundary.theta_unwrapped_rad;
        const double expected_duration_s = angle_range_rad / angular_speed_rad_s;
        const auto start_envelope =
            theta_envelope(block.cycles.start_boundary.right_bracket_sample_index,
                           conditions.initial_theta_unwrapped_rad, step_rotation_rad);
        const auto end_envelope =
            theta_envelope(block.cycles.end_boundary.right_bracket_sample_index,
                           conditions.initial_theta_unwrapped_rad, step_rotation_rad);
        const double arithmetic_guard =
            32.0 * std::numeric_limits<double>::epsilon() *
            std::max({1.0, std::abs(actual_duration_s), std::abs(expected_duration_s)});
        const double duration_bound = (start_envelope.absolute_error_bound_rad +
                                       end_envelope.absolute_error_bound_rad) /
                                          angular_speed_rad_s +
                                      arithmetic_guard;
        require(report,
                detail::finite(expected_duration_s) && detail::finite(duration_bound) &&
                    std::abs(actual_duration_s - expected_duration_s) <= duration_bound,
                ContractIssueCode::inconsistent_semantics,
                std::string(path) + ".cycles",
                "cycle duration and lattice angle must agree with held RPM "
                "within the documented binary64 accumulation and arithmetic "
                "bounds");
    };
    validate_duration_coherence(convergence.block_a, "convergence.block_a");
    validate_duration_coherence(convergence.block_b, "convergence.block_b");

    const auto block_b_end_index = boundary_cycle_index(
        convergence.block_b.cycles.end_boundary.theta_unwrapped_rad,
        conditions.cycle_reference_theta_rad);
    const auto cutoff_theta =
        theta_envelope(last_cutoff_sample_index, conditions.initial_theta_unwrapped_rad,
                       step_rotation_rad);
    if (block_b_end_index.has_value()) {
        require(report, *block_b_end_index != std::numeric_limits<std::int64_t>::max(),
                ContractIssueCode::invalid_value,
                "convergence.block_b.cycles.end_boundary."
                "theta_unwrapped_rad",
                "last retained boundary leaves no representable following "
                "four-stroke lattice index");
        if (*block_b_end_index != std::numeric_limits<std::int64_t>::max()) {
            const double following_boundary_theta =
                conditions.cycle_reference_theta_rad +
                static_cast<double>(*block_b_end_index + 1) * kFourStrokeCycleRadians;
            require(report,
                    detail::finite(cutoff_theta.absolute_error_bound_rad) &&
                        cutoff_theta.center_rad +
                                cutoff_theta.absolute_error_bound_rad <
                            following_boundary_theta,
                    ContractIssueCode::inconsistent_semantics,
                    "convergence.block_b.cycles.end_boundary",
                    "the documented held-RPM cutoff envelope must prove that no "
                    "following complete cycle was available at fixed cutoff");
        }
    }
}

} // namespace

ValidationReport validate(const HeldSpeedOperatingPointResult &operating_point) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(report, !operating_point.simulation_request_identity_v2_sha256.is_zero(),
            ContractIssueCode::invalid_value, "simulation_request_identity_v2_sha256",
            "held-speed result requires a nonzero canonical simulation-request "
            "identity");
    append_prefixed(report, validate_operating_conditions(operating_point.conditions),
                    "conditions");
    require(report,
            operating_point.applicability_label ==
                kGenericChenFlynnLowOrderModelPredictionApplicability,
            ContractIssueCode::inconsistent_semantics, "applicability_label",
            "held-speed result must retain the exact generic Chen-Flynn low-order "
            "model-prediction applicability label");

    const auto displacement_m3 = operating_point.conditions.total_displacement_m3;
    const auto &convergence = operating_point.convergence;
    append_prefixed(report, validate(convergence.method), "convergence.method");
    require(report,
            convergence.method ==
                adjacent_cycle_block_mean_convergence_method_identity(),
            ContractIssueCode::unsupported_value, "convergence.method",
            "operating-point convergence evidence must use the exact admitted "
            "contract method identity");
    require(report,
            convergence.comparison_cycle_count > 0 &&
                finite_nonnegative(convergence.eligibility_threshold_time_s) &&
                finite_positive(convergence.fixed_cutoff_time_s) &&
                convergence.fixed_cutoff_time_s >=
                    convergence.eligibility_threshold_time_s &&
                finite_nonnegative(convergence.torque_residual_nm) &&
                finite_positive(convergence.torque_tolerance_nm) &&
                finite_nonnegative(convergence.pressure_residual_pa) &&
                finite_positive(convergence.pressure_tolerance_pa),
            ContractIssueCode::invalid_value, "convergence",
            "convergence time bounds, cycle count, residuals, or tolerances are "
            "invalid");

    append_prefixed(
        report,
        validate_cycle_block(convergence.block_a, displacement_m3,
                             operating_point.conditions.cycle_reference_theta_rad),
        "convergence.block_a");
    append_prefixed(
        report,
        validate_cycle_block(convergence.block_b, displacement_m3,
                             operating_point.conditions.cycle_reference_theta_rad),
        "convergence.block_b");
    require(report,
            convergence.block_a.cycles.completed_cycle_count ==
                    convergence.comparison_cycle_count &&
                convergence.block_b.cycles.completed_cycle_count ==
                    convergence.comparison_cycle_count,
            ContractIssueCode::inconsistent_shape, "convergence",
            "both convergence blocks must contain exactly N complete cycles");

    const auto &a_cycles = convergence.block_a.cycles;
    const auto &b_cycles = convergence.block_b.cycles;
    require(report,
            a_cycles.last_completed_cycle_ordinal !=
                    std::numeric_limits<std::uint64_t>::max() &&
                b_cycles.first_completed_cycle_ordinal ==
                    a_cycles.last_completed_cycle_ordinal + 1 &&
                a_cycles.end_boundary == b_cycles.start_boundary,
            ContractIssueCode::inconsistent_semantics, "convergence.block_b.cycles",
            "convergence blocks A and B must be adjacent, non-overlapping cycle "
            "ranges sharing one exact boundary");
    require(report,
            a_cycles.start_boundary.scenario_time_s >=
                    convergence.eligibility_threshold_time_s &&
                b_cycles.end_boundary.scenario_time_s <=
                    convergence.fixed_cutoff_time_s,
            ContractIssueCode::inconsistent_semantics, "convergence",
            "convergence blocks must begin at or after the eligibility threshold "
            "and end at or before the fixed cutoff");
    require(report,
            convergence.last_eligible_completed_cycle_ordinal_at_fixed_cutoff ==
                    b_cycles.last_completed_cycle_ordinal &&
                convergence.last_eligible_cycle_end_boundary_at_fixed_cutoff ==
                    b_cycles.end_boundary,
            ContractIssueCode::inconsistent_semantics,
            "convergence."
            "last_eligible_cycle_end_boundary_at_fixed_cutoff",
            "block B must end at the explicitly attested last complete eligible "
            "cycle at the fixed cutoff");

    validate_held_speed_kinematics(report, operating_point.conditions, convergence);

    const double torque_denominator =
        static_cast<double>(convergence.comparison_cycle_count) *
        kFourStrokeCycleRadians;
    const double canonical_block_a_net_torque_nm =
        convergence.block_a.brake_work_j / torque_denominator;
    const double canonical_block_b_net_torque_nm =
        convergence.block_b.brake_work_j / torque_denominator;
    const double torque_residual =
        std::abs(canonical_block_b_net_torque_nm - canonical_block_a_net_torque_nm);
    require(report,
            finite(torque_residual) &&
                same_binary64(convergence.torque_residual_nm, torque_residual),
            ContractIssueCode::inconsistent_semantics, "convergence.torque_residual_nm",
            "torque residual must be the exact binary64 absolute A/B difference "
            "recomputed from retained brake work");

    const auto &a_pressures = convergence.block_a.mean_boundary_pressures;
    const auto &b_pressures = convergence.block_b.mean_boundary_pressures;
    require(report, a_pressures.size() == b_pressures.size(),
            ContractIssueCode::inconsistent_shape,
            "convergence.block_b.mean_boundary_pressures",
            "convergence blocks must retain identical physical pressure sets");
    bool comparable_pressures =
        !a_pressures.empty() && a_pressures.size() == b_pressures.size();
    double pressure_residual = 0.0;
    GasVolumeId limiting_volume;
    if (comparable_pressures) {
        for (std::size_t index = 0; index < a_pressures.size(); ++index) {
            if (a_pressures[index].gas_volume_id != b_pressures[index].gas_volume_id ||
                !detail::finite(a_pressures[index].pressure_pa_abs) ||
                !detail::finite(b_pressures[index].pressure_pa_abs)) {
                comparable_pressures = false;
                break;
            }
            const double candidate = std::abs(b_pressures[index].pressure_pa_abs -
                                              a_pressures[index].pressure_pa_abs);
            if (index == 0 || candidate > pressure_residual) {
                pressure_residual = candidate;
                limiting_volume = a_pressures[index].gas_volume_id;
            }
        }
    }
    if (comparable_pressures) {
        require(report,
                same_binary64(convergence.pressure_residual_pa, pressure_residual),
                ContractIssueCode::inconsistent_semantics,
                "convergence.pressure_residual_pa",
                "pressure residual must be the exact binary64 maximum A/B "
                "boundary-pressure difference");
        require(report, convergence.limiting_pressure_volume_id == limiting_volume,
                ContractIssueCode::inconsistent_semantics,
                "convergence.limiting_pressure_volume_id",
                "limiting pressure identity must retain the first ascending "
                "maximum-residual gas volume");
    } else {
        require(report, convergence.limiting_pressure_volume_id.valid(),
                ContractIssueCode::invalid_value,
                "convergence.limiting_pressure_volume_id",
                "limiting pressure gas-volume ID must be nonzero");
    }
    require(report,
            finite(torque_residual) &&
                torque_residual <= convergence.torque_tolerance_nm &&
                comparable_pressures &&
                pressure_residual <= convergence.pressure_tolerance_pa,
            ContractIssueCode::inconsistent_semantics, "convergence",
            "recomputed canonical residuals for a successful operating point must "
            "satisfy both inclusive convergence tolerances");
    return report;
}

ValidationReport
validate(const HeldSpeedOperatingPointResult &operating_point,
         const RenderScenario &requested_scenario, const EngineSpec &engine,
         const Sha256Digest &expected_simulation_request_identity_v2_sha256) {
    using detail::require;

    auto report = validate(operating_point);
    const auto &conditions = operating_point.conditions;
    require(report,
            !expected_simulation_request_identity_v2_sha256.is_zero() &&
                operating_point.simulation_request_identity_v2_sha256 ==
                    expected_simulation_request_identity_v2_sha256,
            ContractIssueCode::inconsistent_semantics,
            "simulation_request_identity_v2_sha256",
            "held-speed result must retain the caller-supplied canonical v2 "
            "simulation-request identity");

    const auto *requested_mode = std::get_if<HeldSpeed>(&requested_scenario.mode);
    require(report, requested_mode != nullptr,
            ContractIssueCode::inconsistent_semantics, "conditions",
            "only a requested held-speed scenario may own an operating-point "
            "result");
    if (requested_mode != nullptr) {
        require(report,
                same_binary64(conditions.engine_speed_rpm,
                              requested_mode->engine_speed_rpm.value) &&
                    same_binary64(conditions.initial_theta_unwrapped_rad,
                                  requested_mode->initial_theta_rad.value) &&
                    same_binary64(conditions.throttle_01,
                                  requested_mode->throttle_01.value),
                ContractIssueCode::inconsistent_semantics, "conditions",
                "compact operating-point RPM, initial angle, and throttle must "
                "exactly match the requested held mode");
    }

    const auto *requested_preparation =
        std::get_if<ConvergenceSettling>(&requested_scenario.preparation);
    require(report, requested_preparation != nullptr,
            ContractIssueCode::inconsistent_semantics, "convergence",
            "held-speed operating-point result requires convergence preparation");
    if (requested_preparation != nullptr) {
        const double minimum_eligible_time_s =
            requested_preparation->minimum_warm_up_duration_s.value +
            requested_preparation->minimum_settling_duration_s.value;
        require(
            report,
            operating_point.convergence.method == requested_preparation->method.value &&
                operating_point.convergence.comparison_cycle_count ==
                    requested_preparation->comparison_cycle_count.value &&
                same_binary64(operating_point.convergence.eligibility_threshold_time_s,
                              minimum_eligible_time_s) &&
                same_binary64(
                    operating_point.convergence.fixed_cutoff_time_s,
                    requested_preparation->maximum_preparation_duration_s.value) &&
                same_binary64(
                    operating_point.convergence.torque_tolerance_nm,
                    requested_preparation->cycle_mean_torque_tolerance_nm.value) &&
                same_binary64(operating_point.convergence.pressure_tolerance_pa,
                              requested_preparation->pressure_tolerance_pa.value),
            ContractIssueCode::inconsistent_semantics, "convergence",
            "convergence identity, fixed time bounds, cycle count, and "
            "tolerances must exactly match the request");
    }

    require(
        report,
        conditions.engine_profile_id == requested_scenario.engine_profile_id &&
            conditions.physics_rate_hz == requested_scenario.rates.physics &&
            same_binary64(conditions.ambient.pressure_pa_abs,
                          requested_scenario.ambient.pressure_pa_abs.value) &&
            same_binary64(conditions.ambient.temperature_k,
                          requested_scenario.ambient.temperature_k.value) &&
            same_binary64(conditions.ambient.relative_humidity_01,
                          requested_scenario.ambient.relative_humidity_01.value) &&
            conditions.fuel.fuel_id == requested_scenario.fuel.fuel_id.value &&
            same_binary64(conditions.fuel.lower_heating_value_j_per_kg,
                          requested_scenario.fuel.lower_heating_value_j_per_kg.value) &&
            same_binary64(
                conditions.fuel.stoichiometric_air_fuel_mass_ratio,
                requested_scenario.fuel.stoichiometric_air_fuel_mass_ratio.value) &&
            same_binary64(
                conditions.initial_thermal_state.gas_temperature_k,
                requested_scenario.initial_thermal_state.gas_temperature_k.value) &&
            same_binary64(
                conditions.initial_thermal_state.wall_temperature_k,
                requested_scenario.initial_thermal_state.wall_temperature_k.value) &&
            same_binary64(
                conditions.initial_thermal_state.coolant_temperature_k,
                requested_scenario.initial_thermal_state.coolant_temperature_k.value) &&
            same_binary64(
                conditions.initial_thermal_state.oil_temperature_k,
                requested_scenario.initial_thermal_state.oil_temperature_k.value) &&
            same_binary64(conditions.crankcase.pressure_pa_abs,
                          requested_scenario.crankcase.pressure_pa_abs.value) &&
            same_binary64(conditions.crankcase.temperature_k,
                          requested_scenario.crankcase.temperature_k.value),
        ContractIssueCode::inconsistent_semantics, "conditions",
        "compact ambient, thermal, fuel, crankcase, profile, and mechanics "
        "conditions must exactly match the requested scenario");

    const auto *profile =
        std::get_if<LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    require(report, profile != nullptr, ContractIssueCode::inconsistent_semantics,
            "conditions",
            "held-speed operating-point evidence requires the admitted low-order "
            "operating-point engine profile");
    require(report,
            conditions.engine_profile_id == engine.profile_id.value &&
                same_binary64(conditions.total_displacement_m3,
                              engine.total_displacement_m3.value),
            ContractIssueCode::inconsistent_semantics, "conditions",
            "operating-point profile and displacement must exactly match the "
            "executed engine");
    if (profile != nullptr) {
        require(report,
                same_binary64(
                    conditions.cycle_reference_theta_rad,
                    profile->core.mechanism.crank.crank_tdc_reference_rad.value) &&
                    conditions.accessory_configuration.configuration_id ==
                        profile->accessory_configuration.configuration_id.value &&
                    conditions.accessory_configuration.content_sha256 ==
                        profile->accessory_configuration.content_sha256.value &&
                    conditions.starter_mechanically_disengaged ==
                        profile->starter.mechanically_disengaged.value &&
                    conditions.starter_included_terms ==
                        profile->starter.included_terms.value,
                ContractIssueCode::inconsistent_semantics, "conditions",
                "operating-point cycle reference, accessory, and starter "
                "conditions must exactly match the admitted engine profile");
    }

    std::vector<GasVolumeId> expected_physical_volumes;
    expected_physical_volumes.reserve(engine.gas_volumes.size());
    for (const auto &volume : engine.gas_volumes) {
        if (volume.kind.value != GasVolumeKind::atmosphere) {
            expected_physical_volumes.push_back(volume.id);
        }
    }
    std::ranges::sort(expected_physical_volumes);
    const auto validate_pressure_coverage =
        [&](const HeldSpeedCycleBlockEvidence &block, std::string_view path) {
            require(report,
                    block.mean_boundary_pressures.size() ==
                        expected_physical_volumes.size(),
                    ContractIssueCode::inconsistent_shape, std::string(path),
                    "pressure evidence must contain every physical gas volume and "
                    "exclude the atmosphere alias");
            const auto count = std::min(block.mean_boundary_pressures.size(),
                                        expected_physical_volumes.size());
            for (std::size_t index = 0; index < count; ++index) {
                require(report,
                        block.mean_boundary_pressures[index].gas_volume_id ==
                            expected_physical_volumes[index],
                        ContractIssueCode::inconsistent_semantics,
                        std::string(path) + "[" + std::to_string(index) +
                            "].gas_volume_id",
                        "pressure evidence must follow exact ascending physical "
                        "gas-volume identity");
            }
        };
    validate_pressure_coverage(operating_point.convergence.block_a,
                               "convergence.block_a.mean_boundary_pressures");
    validate_pressure_coverage(operating_point.convergence.block_b,
                               "convergence.block_b.mean_boundary_pressures");
    return report;
}

} // namespace engine_sim_offline::contract
