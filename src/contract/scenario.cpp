#include "engine_sim_offline/contract/scenario.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "sha256_stream.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::contract {

const MethodIdentity &fixed_horizon_cycle_sampling_method_identity() {
    static const MethodIdentity identity{
        std::string{kFixedHorizonCycleSamplingMethodId},
        kFixedHorizonCycleSamplingMethodVersion,
        kFixedHorizonCycleSamplingMethodConfigurationSha256,
    };
    return identity;
}

Sha256Digest canonical_binary64_le_sha256(std::span<const double> samples) noexcept {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    static_assert(std::numeric_limits<double>::digits == 53);

    detail::Sha256Stream hash;
    for (const auto &sample : samples) {
        const auto bits = std::bit_cast<std::uint64_t>(sample);
        std::array<std::byte, sizeof(bits)> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] =
                static_cast<std::byte>((bits >> (index * 8U)) & UINT64_C(0xff));
        }
        hash.update(bytes);
    }
    return hash.finish();
}

namespace {

bool checked_add_u64(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

void validate_resolution_id(ValidationReport &report, const std::string &resolution_id,
                            const ProvenanceLedger &provenance,
                            const std::string &field_path,
                            const std::string &parameter_path) {
    detail::require(report, is_valid_semantic_id(resolution_id),
                    ContractIssueCode::invalid_value, field_path,
                    "resolution ID must be a canonical semantic ID");
    const auto *resolution = detail::find_resolution(provenance, resolution_id);
    detail::require(report, resolution != nullptr,
                    ContractIssueCode::dangling_reference, field_path,
                    "resolution ID is not present in the provenance ledger");
    if (resolution != nullptr) {
        detail::require(report, resolution->parameter_path == parameter_path,
                        ContractIssueCode::inconsistent_semantics, field_path,
                        "resolution ID belongs to a different resolved field");
    }
}

void validate_trajectory(ValidationReport &report, const ScalarTrajectory &trajectory,
                         const ProvenanceLedger &provenance, double total_duration_s,
                         bool unit_interval_values, bool nonnegative_values,
                         const std::string &path) {
    using detail::finite;
    using detail::require;

    validate_resolution_id(report, trajectory.resolution_id, provenance,
                           path + ".resolution_id", path);
    require(report, !trajectory.points.empty(), ContractIssueCode::missing_value,
            path + ".points", "trajectory must contain at least one point");
    if (trajectory.points.empty()) {
        return;
    }
    require(report, trajectory.points.front().time_s == 0.0,
            ContractIssueCode::inconsistent_semantics, path + ".points[0].time_s",
            "right-continuous trajectory must begin at time zero");
    for (std::size_t index = 0; index < trajectory.points.size(); ++index) {
        const auto &point = trajectory.points[index];
        const auto point_path = path + ".points[" + std::to_string(index) + "]";
        require(report,
                finite(point.time_s) && point.time_s >= 0.0 &&
                    point.time_s <= total_duration_s,
                ContractIssueCode::invalid_value, point_path + ".time_s",
                "trajectory time must be finite and inside the scenario interval");
        require(report, finite(point.value), ContractIssueCode::invalid_value,
                point_path + ".value", "trajectory value must be finite");
        if (unit_interval_values) {
            require(report, detail::unit_interval(point.value),
                    ContractIssueCode::invalid_value, point_path + ".value",
                    "trajectory value must be in [0, 1]");
        }
        if (nonnegative_values) {
            require(report, point.value >= 0.0, ContractIssueCode::invalid_value,
                    point_path + ".value", "trajectory value must be nonnegative");
        }
        if (index != 0) {
            require(report, point.time_s > trajectory.points[index - 1].time_s,
                    ContractIssueCode::inconsistent_semantics, point_path + ".time_s",
                    "trajectory times must be strictly increasing");
        }
    }
    require(report,
            trajectory.interpolation ==
                    TrajectoryInterpolation::right_continuous_hold ||
                trajectory.interpolation == TrajectoryInterpolation::linear,
            ContractIssueCode::unsupported_value, path + ".interpolation",
            "trajectory interpolation is not recognized");
    if (trajectory.interpolation == TrajectoryInterpolation::linear) {
        require(report, trajectory.points.back().time_s == total_duration_s,
                ContractIssueCode::inconsistent_semantics, path + ".points",
                "linear trajectory must explicitly cover the scenario endpoint");
    }
}

void validate_fixed_rate_rpm_trajectory(ValidationReport &report,
                                        const FixedRateRpmTrajectory &trajectory,
                                        const ProvenanceLedger &provenance,
                                        const std::string &path) {
    using detail::finite;
    using detail::require;

    validate_resolution_id(report, trajectory.resolution_id, provenance,
                           path + ".resolution_id", path);
    require(report, trajectory.semantics == RpmSampleSemantics::post_step_rpm,
            ContractIssueCode::unsupported_value, path + ".semantics",
            "fixed-rate RPM trajectory must contain post-step RPM samples");
    require(report, !trajectory.post_step_rpm.empty(), ContractIssueCode::missing_value,
            path + ".post_step_rpm",
            "fixed-rate RPM trajectory must contain at least one post-step sample");
    for (std::size_t index = 0; index < trajectory.post_step_rpm.size(); ++index) {
        const auto sample_path = path + ".post_step_rpm[" + std::to_string(index) + "]";
        const auto sample = trajectory.post_step_rpm[index];
        require(report, finite(sample) && sample >= 0.0,
                ContractIssueCode::invalid_value, sample_path,
                "post-step RPM sample must be finite and nonnegative");
    }
    require(report,
            trajectory.samples_f64le_sha256 ==
                canonical_binary64_le_sha256(trajectory.post_step_rpm),
            ContractIssueCode::inconsistent_semantics, path + ".samples_f64le_sha256",
            "fixed-rate RPM sample hash does not match the owned sample vector");
}

void require_fixed_horizon_sampling_method(ValidationReport &report,
                                           const MethodIdentity &method,
                                           const std::string &path) {
    detail::require(report, method == fixed_horizon_cycle_sampling_method_identity(),
                    ContractIssueCode::unsupported_value, path,
                    "fixed-horizon preparation requires the exact "
                    "fixed-horizon-trailing-complete-cycle-sample-v1 method identity");
}

[[nodiscard]] double
sufficient_complete_cycle_horizon_s(std::uint32_t trailing_complete_cycle_count,
                                    double engine_speed_rpm) noexcept {
    const auto conservative_cycle_count = static_cast<double>(
        static_cast<std::uint64_t>(trailing_complete_cycle_count) + UINT64_C(1));
    return (conservative_cycle_count * 120.0) / engine_speed_rpm;
}

} // namespace

ValidationReport validate_clock_grid(const RenderScenario &scenario) {
    using detail::require;

    ValidationReport report;
    const std::array clocks{
        std::pair{std::string_view{"physics"}, scenario.rates.physics},
        std::pair{std::string_view{"capture"}, scenario.rates.capture},
        std::pair{std::string_view{"source_processing"},
                  scenario.rates.source_processing},
        std::pair{std::string_view{"acoustic"}, scenario.rates.acoustic},
        std::pair{std::string_view{"delivery"}, scenario.rates.delivery},
    };
    for (const auto &[name, rate] : clocks) {
        if (!validate(rate).ok()) {
            continue;
        }
        const auto total = resolve_frame_index(scenario.total_duration_s.value, rate);
        const auto audible_start =
            resolve_frame_index(scenario.audible_start_s.value, rate);
        const auto audible_count =
            resolve_frame_index(scenario.audible_duration_s.value, rate);
        const auto path = std::string{name};
        require(report, total.has_value() && *total > 0,
                ContractIssueCode::inconsistent_semantics, path + ".total_duration_s",
                "total duration must resolve to a positive integral frame count");
        require(report, audible_start.has_value(),
                ContractIssueCode::inconsistent_semantics, path + ".audible_start_s",
                "audible start must resolve to an integral frame index");
        require(report, audible_count.has_value() && *audible_count > 0,
                ContractIssueCode::inconsistent_semantics, path + ".audible_duration_s",
                "audible duration must resolve to a positive integral frame count");
        if (total.has_value() && audible_start.has_value() &&
            audible_count.has_value()) {
            std::uint64_t audible_end = 0;
            const auto audible_end_representable =
                checked_add_u64(*audible_start, *audible_count, audible_end);
            require(
                report, audible_end_representable && audible_end == *total,
                ContractIssueCode::inconsistent_semantics, path + ".audible_interval",
                "audible frame interval must end exactly at the total frame horizon");
        }
    }

    if (validate(scenario.rates.physics).ok()) {
        const auto resolve_physics_boundary =
            [&](double time_s, const std::string &path, std::string message) {
                const auto frame = resolve_frame_index(time_s, scenario.rates.physics);
                require(report, frame.has_value(),
                        ContractIssueCode::inconsistent_semantics, path,
                        std::move(message));
                return frame;
            };
        const auto audible_start =
            resolve_frame_index(scenario.audible_start_s.value, scenario.rates.physics);

        std::visit(
            [&](const auto &preparation) {
                using T = std::decay_t<decltype(preparation)>;
                if constexpr (std::is_same_v<T, FixedSettling>) {
                    const auto warm_up = resolve_physics_boundary(
                        preparation.warm_up_duration_s.value,
                        "physics.preparation.warm_up_duration_s",
                        "warm-up duration must resolve to an integral physics-frame "
                        "count");
                    const auto settling = resolve_physics_boundary(
                        preparation.settling_duration_s.value,
                        "physics.preparation.settling_duration_s",
                        "settling duration must resolve to an integral physics-frame "
                        "count");
                    if (warm_up.has_value() && settling.has_value() &&
                        audible_start.has_value()) {
                        std::uint64_t preparation_end = 0;
                        const auto preparation_end_representable =
                            checked_add_u64(*warm_up, *settling, preparation_end);
                        require(
                            report,
                            preparation_end_representable &&
                                preparation_end == *audible_start,
                            ContractIssueCode::inconsistent_semantics,
                            "physics.preparation",
                            "fixed preparation must end exactly at the audible-start "
                            "physics frame");
                    }
                } else {
                    const auto fixed_horizon = resolve_physics_boundary(
                        preparation.fixed_preparation_horizon_s.value,
                        "physics.preparation.fixed_preparation_horizon_s",
                        "fixed preparation horizon must resolve to an integral "
                        "physics-frame index");
                    if (fixed_horizon.has_value() && audible_start.has_value()) {
                        const auto *free_engine =
                            std::get_if<FreeEngine>(&scenario.mode);
                        const bool warm_free_engine =
                            free_engine != nullptr &&
                            free_engine->initial_engine_speed_rpm.value > 0.0;
                        require(
                            report,
                            warm_free_engine ? *fixed_horizon <= *audible_start
                                             : *fixed_horizon == *audible_start,
                            ContractIssueCode::inconsistent_semantics,
                            "physics.preparation.fixed_preparation_horizon_s",
                            warm_free_engine
                                ? "free-engine fixed preparation must end at or before "
                                  "the audible-start physics frame"
                                : "fixed preparation horizon must equal the "
                                  "audible-start physics frame");
                    }
                }
            },
            scenario.preparation);

        for (std::size_t index = 0; index < scenario.operating_state.value.size();
             ++index) {
            resolve_physics_boundary(
                scenario.operating_state.value[index].time_s,
                "physics.operating_state[" + std::to_string(index) + "].time_s",
                "operating-state boundary must resolve to an integral physics-frame "
                "index");
        }

        const auto validate_trajectory_grid = [&](const ScalarTrajectory &trajectory,
                                                  const std::string &path) {
            for (std::size_t index = 0; index < trajectory.points.size(); ++index) {
                resolve_physics_boundary(
                    trajectory.points[index].time_s,
                    path + ".points[" + std::to_string(index) + "].time_s",
                    "trajectory boundary must resolve to an integral physics-frame "
                    "index");
            }
        };
        std::visit(
            [&](const auto &mode) {
                using T = std::decay_t<decltype(mode)>;
                if constexpr (std::is_same_v<T, PrescribedKinematicSweep>) {
                    std::visit(
                        [&](const auto &rpm) {
                            using Rpm = std::decay_t<decltype(rpm)>;
                            if constexpr (std::is_same_v<Rpm, ScalarTrajectory>) {
                                validate_trajectory_grid(rpm,
                                                         "physics.mode.trajectory.rpm");
                            } else {
                                detail::append_prefixed(
                                    report, validate(rpm.rate),
                                    "physics.mode.trajectory.rpm.rate");
                                require(
                                    report, rpm.first_step_index == 0,
                                    ContractIssueCode::inconsistent_semantics,
                                    "physics.mode.trajectory.rpm.first_step_index",
                                    "post-step RPM trajectory must begin at physics "
                                    "step zero");
                                require(report, rpm.rate == scenario.rates.physics,
                                        ContractIssueCode::inconsistent_semantics,
                                        "physics.mode.trajectory.rpm.rate",
                                        "fixed-rate RPM trajectory rate must equal the "
                                        "physics rate");
                                const auto total =
                                    resolve_frame_index(scenario.total_duration_s.value,
                                                        scenario.rates.physics);
                                if (total.has_value()) {
                                    require(report, rpm.post_step_rpm.size() == *total,
                                            ContractIssueCode::inconsistent_shape,
                                            "physics.mode.trajectory.rpm.post_step_rpm",
                                            "post-step RPM sample count must equal the "
                                            "total physics-frame count");
                                }
                            }
                        },
                        mode.trajectory.rpm);
                    validate_trajectory_grid(mode.throttle_01,
                                             "physics.mode.throttle_01");
                } else if constexpr (std::is_same_v<T, HeldDyno>) {
                    detail::append_prefixed(
                        report, validate(mode.target_engine_speed_rpm.rate),
                        "physics.mode.target_engine_speed_rpm.rate");
                    require(report, mode.target_engine_speed_rpm.first_step_index == 0,
                            ContractIssueCode::inconsistent_semantics,
                            "physics.mode.target_engine_speed_rpm.first_step_index",
                            "held-dyno target RPM must begin at physics step zero");
                    require(report,
                            mode.target_engine_speed_rpm.rate == scenario.rates.physics,
                            ContractIssueCode::inconsistent_semantics,
                            "physics.mode.target_engine_speed_rpm.rate",
                            "held-dyno target RPM rate must equal the physics rate");
                    const auto total = resolve_frame_index(
                        scenario.total_duration_s.value, scenario.rates.physics);
                    if (total.has_value()) {
                        require(report,
                                mode.target_engine_speed_rpm.post_step_rpm.size() ==
                                    *total,
                                ContractIssueCode::inconsistent_shape,
                                "physics.mode.target_engine_speed_rpm.post_step_rpm",
                                "held-dyno target sample count must equal the total "
                                "physics-frame count");
                    }
                    validate_trajectory_grid(mode.throttle_01,
                                             "physics.mode.throttle_01");
                } else if constexpr (std::is_same_v<T, InertialDyno>) {
                    validate_trajectory_grid(mode.throttle_01,
                                             "physics.mode.throttle_01");
                } else if constexpr (std::is_same_v<T, FreeEngine>) {
                    validate_trajectory_grid(mode.throttle_01,
                                             "physics.mode.throttle_01");
                    validate_trajectory_grid(
                        mode.external_resisting_torque_nm,
                        "physics.mode.external_resisting_torque_nm");
                }
            },
            scenario.mode);
    }

    return report;
}

ValidationReport validate(const RenderScenario &scenario,
                          const ProvenanceLedger &provenance) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report = validate(provenance);
    require(report, scenario.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version", "scenario schema version must be positive");
    require(report, is_valid_semantic_id(scenario.scenario_id),
            ContractIssueCode::invalid_value, "scenario_id",
            "scenario ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(scenario.engine_profile_id),
            ContractIssueCode::invalid_value, "engine_profile_id",
            "engine profile ID must be a canonical semantic ID");
    require(report, scenario.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "scenario and provenance ledger schema IDs must match");

    validate_resolved(report, scenario.ambient.pressure_pa_abs, provenance,
                      "scenario.ambient.pressure_pa_abs");
    validate_resolved(report, scenario.ambient.temperature_k, provenance,
                      "scenario.ambient.temperature_k");
    validate_resolved(report, scenario.ambient.relative_humidity_01, provenance,
                      "scenario.ambient.relative_humidity_01");
    require(report, finite_positive(scenario.ambient.pressure_pa_abs.value),
            ContractIssueCode::invalid_value, "ambient.pressure_pa_abs.value",
            "ambient absolute pressure must be finite and positive");
    require(report, finite_positive(scenario.ambient.temperature_k.value),
            ContractIssueCode::invalid_value, "ambient.temperature_k.value",
            "ambient temperature must be finite and positive");
    require(report, detail::unit_interval(scenario.ambient.relative_humidity_01.value),
            ContractIssueCode::invalid_value, "ambient.relative_humidity_01.value",
            "relative humidity must be in [0, 1]");

    validate_resolved(report, scenario.fuel.fuel_id, provenance,
                      "scenario.fuel.fuel_id");
    validate_resolved(report, scenario.fuel.lower_heating_value_j_per_kg, provenance,
                      "scenario.fuel.lower_heating_value_j_per_kg");
    validate_resolved(report, scenario.fuel.stoichiometric_air_fuel_mass_ratio,
                      provenance, "scenario.fuel.stoichiometric_air_fuel_mass_ratio");
    require(report, is_valid_semantic_id(scenario.fuel.fuel_id.value),
            ContractIssueCode::invalid_value, "fuel.fuel_id.value",
            "fuel ID must be a canonical semantic ID");
    require(report,
            finite_positive(scenario.fuel.lower_heating_value_j_per_kg.value) &&
                finite_positive(scenario.fuel.stoichiometric_air_fuel_mass_ratio.value),
            ContractIssueCode::invalid_value, "fuel",
            "fuel heating value and stoichiometric ratio must be finite and positive");

    const auto validate_temperature = [&](const ResolvedValue<double> &value,
                                          const std::string &path) {
        validate_resolved(report, value, provenance, path);
        require(report, finite_positive(value.value), ContractIssueCode::invalid_value,
                path + ".value",
                "initial absolute temperature must be finite and positive");
    };
    validate_temperature(scenario.initial_thermal_state.gas_temperature_k,
                         "scenario.initial_thermal_state.gas_temperature_k");
    validate_temperature(scenario.initial_thermal_state.wall_temperature_k,
                         "scenario.initial_thermal_state.wall_temperature_k");
    validate_temperature(scenario.initial_thermal_state.coolant_temperature_k,
                         "scenario.initial_thermal_state.coolant_temperature_k");
    validate_temperature(scenario.initial_thermal_state.oil_temperature_k,
                         "scenario.initial_thermal_state.oil_temperature_k");
    validate_resolved(report, scenario.crankcase.pressure_pa_abs, provenance,
                      "scenario.crankcase.pressure_pa_abs");
    require(report, finite_positive(scenario.crankcase.pressure_pa_abs.value),
            ContractIssueCode::invalid_value,
            "scenario.crankcase.pressure_pa_abs.value",
            "crankcase absolute pressure must be finite and positive");
    validate_temperature(scenario.crankcase.temperature_k,
                         "scenario.crankcase.temperature_k");

    validate_resolved(report, scenario.total_duration_s, provenance,
                      "scenario.total_duration_s");
    validate_resolved(report, scenario.audible_start_s, provenance,
                      "scenario.audible_start_s");
    validate_resolved(report, scenario.audible_duration_s, provenance,
                      "scenario.audible_duration_s");
    require(report, finite_positive(scenario.total_duration_s.value),
            ContractIssueCode::invalid_value, "total_duration_s.value",
            "total duration must be finite and positive");
    require(report,
            finite_nonnegative(scenario.audible_start_s.value) &&
                finite_positive(scenario.audible_duration_s.value) &&
                scenario.audible_start_s.value + scenario.audible_duration_s.value <=
                    scenario.total_duration_s.value,
            ContractIssueCode::invalid_value, "audible_duration_s.value",
            "audible half-open interval must fit inside total duration");

    std::visit(
        [&](const auto &preparation) {
            using T = std::decay_t<decltype(preparation)>;
            if constexpr (std::is_same_v<T, FixedSettling>) {
                validate_resolved(report, preparation.warm_up_duration_s, provenance,
                                  "scenario.preparation.warm_up_duration_s");
                validate_resolved(report, preparation.settling_duration_s, provenance,
                                  "scenario.preparation.settling_duration_s");
                require(report,
                        finite_nonnegative(preparation.warm_up_duration_s.value) &&
                            finite_nonnegative(preparation.settling_duration_s.value),
                        ContractIssueCode::invalid_value, "preparation",
                        "fixed preparation durations must be finite and nonnegative");
                require(report,
                        detail::nearly_equal(preparation.warm_up_duration_s.value +
                                                 preparation.settling_duration_s.value,
                                             scenario.audible_start_s.value) &&
                            detail::nearly_equal(scenario.audible_start_s.value +
                                                     scenario.audible_duration_s.value,
                                                 scenario.total_duration_s.value),
                        ContractIssueCode::inconsistent_semantics,
                        "total_duration_s.value",
                        "fixed preparation must end at audible start, and the audible "
                        "interval must end at total duration");
            } else {
                validate_resolved(report, preparation.method, provenance,
                                  "scenario.preparation.method");
                append_prefixed(report, validate(preparation.method.value),
                                "scenario.preparation.method.value");
                require_fixed_horizon_sampling_method(
                    report, preparation.method.value,
                    "scenario.preparation.method.value");
                validate_resolved(report, preparation.fixed_preparation_horizon_s,
                                  provenance,
                                  "scenario.preparation.fixed_preparation_horizon_s");
                validate_resolved(report, preparation.trailing_complete_cycle_count,
                                  provenance,
                                  "scenario.preparation.trailing_complete_cycle_count");
                require(
                    report,
                    finite_positive(preparation.fixed_preparation_horizon_s.value) &&
                        preparation.trailing_complete_cycle_count.value > 0 &&
                        static_cast<std::uintmax_t>(
                            preparation.trailing_complete_cycle_count.value) <=
                            static_cast<std::uintmax_t>(
                                std::numeric_limits<std::size_t>::max()),
                    ContractIssueCode::invalid_value, "preparation",
                    "fixed horizon must be finite and positive, and retained "
                    "cycle capacity must be positive and representable");
                const auto *free_engine = std::get_if<FreeEngine>(&scenario.mode);
                const bool warm_free_engine =
                    free_engine != nullptr &&
                    free_engine->initial_engine_speed_rpm.value > 0.0;
                const bool preparation_boundary_valid =
                    warm_free_engine
                        ? preparation.fixed_preparation_horizon_s.value <=
                              scenario.audible_start_s.value
                        : std::bit_cast<std::uint64_t>(
                              preparation.fixed_preparation_horizon_s.value) ==
                              std::bit_cast<std::uint64_t>(
                                  scenario.audible_start_s.value);
                require(report, preparation_boundary_valid,
                        ContractIssueCode::inconsistent_semantics,
                        "preparation.fixed_preparation_horizon_s.value",
                        warm_free_engine
                            ? "free-engine fixed preparation must end at or before "
                              "audible start"
                            : "fixed preparation horizon must exactly equal audible "
                              "start");
                require(report,
                        detail::nearly_equal(scenario.audible_start_s.value +
                                                 scenario.audible_duration_s.value,
                                             scenario.total_duration_s.value),
                        ContractIssueCode::inconsistent_semantics,
                        "total_duration_s.value",
                        "audible interval must end at total duration");

                const double engine_speed_rpm = std::visit(
                    [](const auto &mode) {
                        using Mode = std::decay_t<decltype(mode)>;
                        if constexpr (std::is_same_v<Mode, HeldSpeed>) {
                            return mode.engine_speed_rpm.value;
                        } else if constexpr (std::is_same_v<Mode, HeldDyno>) {
                            return mode.initial_engine_speed_rpm.value;
                        } else if constexpr (std::is_same_v<Mode, InertialDyno>) {
                            return mode.initial_engine_speed_rpm.value;
                        } else if constexpr (std::is_same_v<Mode, FreeEngine>) {
                            return mode.initial_engine_speed_rpm.value;
                        } else {
                            return 0.0;
                        }
                    },
                    scenario.mode);
                if (finite_positive(engine_speed_rpm) &&
                    preparation.trailing_complete_cycle_count.value > 0) {
                    const double sufficient_horizon_s =
                        sufficient_complete_cycle_horizon_s(
                            preparation.trailing_complete_cycle_count.value,
                            engine_speed_rpm);
                    require(
                        report,
                        finite_positive(sufficient_horizon_s) &&
                            preparation.fixed_preparation_horizon_s.value >=
                                sufficient_horizon_s,
                        ContractIssueCode::inconsistent_semantics,
                        "scenario.preparation.fixed_preparation_horizon_s.value",
                        "fixed preparation horizon is shorter than the conservative "
                        "complete-cycle admission bound");
                }
            }
        },
        scenario.preparation);

    validate_resolved(report, scenario.operating_state, provenance,
                      "scenario.operating_state");
    require(report, !scenario.operating_state.value.empty(),
            ContractIssueCode::missing_value, "operating_state.value",
            "operating-state timeline must contain at least one point");
    std::unordered_set<std::string> event_ids;
    for (std::size_t index = 0; index < scenario.operating_state.value.size();
         ++index) {
        const auto &point = scenario.operating_state.value[index];
        const auto path = "operating_state.value[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(point.event_id),
                ContractIssueCode::invalid_value, path + ".event_id",
                "event ID must be a canonical semantic ID");
        if (!event_ids.insert(point.event_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".event_id",
                       "event IDs must be unique");
        }
        require(report,
                finite(point.time_s) && point.time_s >= 0.0 &&
                    point.time_s <= scenario.total_duration_s.value,
                ContractIssueCode::invalid_value, path + ".time_s",
                "event time must be inside the scenario interval");
        if (index == 0) {
            require(report, point.time_s == 0.0,
                    ContractIssueCode::inconsistent_semantics, path + ".time_s",
                    "right-continuous operating state must begin at time zero");
        } else {
            require(report,
                    point.time_s > scenario.operating_state.value[index - 1].time_s,
                    ContractIssueCode::inconsistent_semantics, path + ".time_s",
                    "operating-state times must be strictly increasing");
        }
    }

    append_prefixed(report, validate(scenario.rates), "rates");
    append_prefixed(report, validate_clock_grid(scenario), "clock_grid");
    validate_resolution_id(report, scenario.rates_resolution_id, provenance,
                           "rates_resolution_id", "scenario.rates");
    validate_resolved(report, scenario.quality, provenance, "scenario.quality");
    require(report, is_valid_semantic_id(scenario.quality.value.profile_id),
            ContractIssueCode::invalid_value, "quality.value.profile_id",
            "quality profile ID must be a canonical semantic ID");
    require(report,
            scenario.quality.value.version > 0 &&
                scenario.quality.value.capture_block_capacity_frames > 0 &&
                scenario.quality.value.event_journal_capacity_records > 0,
            ContractIssueCode::invalid_value, "quality.value",
            "quality version and capture transport capacities must be positive");
    validate_resolved(report, scenario.public_seed, provenance, "scenario.public_seed");
    validate_resolution_id(report, scenario.mode_resolution_id, provenance,
                           "mode_resolution_id", "scenario.mode.kind");

    std::visit(
        [&](const auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, HeldSpeed>) {
                validate_resolved(report, mode.engine_speed_rpm, provenance,
                                  "scenario.mode.engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.throttle_01, provenance,
                                  "scenario.mode.throttle_01");
                require(report, finite_positive(mode.engine_speed_rpm.value),
                        ContractIssueCode::invalid_value, "mode.engine_speed_rpm.value",
                        "held speed must be finite and positive");
                require(report, finite(mode.initial_theta_rad.value),
                        ContractIssueCode::invalid_value,
                        "mode.initial_theta_rad.value",
                        "initial crank angle must be finite");
                require(report, detail::unit_interval(mode.throttle_01.value),
                        ContractIssueCode::invalid_value, "mode.throttle_01.value",
                        "throttle must be in [0, 1]");
            } else if constexpr (std::is_same_v<T, PrescribedKinematicSweep>) {
                validate_resolved(report, mode.trajectory.initial_theta_rad, provenance,
                                  "scenario.mode.trajectory.initial_theta_rad");
                require(report, finite(mode.trajectory.initial_theta_rad.value),
                        ContractIssueCode::invalid_value,
                        "mode.trajectory.initial_theta_rad.value",
                        "initial crank angle must be finite");
                validate_resolved(report, mode.trajectory.kinematic_resolution,
                                  provenance,
                                  "scenario.mode.trajectory.kinematic_resolution");
                append_prefixed(report,
                                validate(mode.trajectory.kinematic_resolution.value),
                                "mode.trajectory.kinematic_resolution");
                std::visit(
                    [&](const auto &rpm) {
                        using Rpm = std::decay_t<decltype(rpm)>;
                        if constexpr (std::is_same_v<Rpm, ScalarTrajectory>) {
                            validate_trajectory(report, rpm, provenance,
                                                scenario.total_duration_s.value, false,
                                                true, "scenario.mode.trajectory.rpm");
                        } else {
                            validate_fixed_rate_rpm_trajectory(
                                report, rpm, provenance,
                                "scenario.mode.trajectory.rpm");
                        }
                    },
                    mode.trajectory.rpm);
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T, HeldDyno>) {
                validate_resolved(report, mode.initial_engine_speed_rpm, provenance,
                                  "scenario.mode.initial_engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.maximum_absorbing_torque_nm, provenance,
                                  "scenario.mode.maximum_absorbing_torque_nm");
                validate_resolved(report, mode.maximum_driving_torque_nm, provenance,
                                  "scenario.mode.maximum_driving_torque_nm");
                require(
                    report,
                    finite_positive(mode.initial_engine_speed_rpm.value) &&
                        finite(mode.initial_theta_rad.value) &&
                        finite_nonnegative(mode.maximum_absorbing_torque_nm.value) &&
                        finite_nonnegative(mode.maximum_driving_torque_nm.value),
                    ContractIssueCode::invalid_value, "mode",
                    "held dyno requires positive initial speed, finite crank "
                    "angle, and nonnegative actuator limits");
                validate_fixed_rate_rpm_trajectory(
                    report, mode.target_engine_speed_rpm, provenance,
                    "scenario.mode.target_engine_speed_rpm");
                require(
                    report,
                    mode.target_engine_speed_rpm.rate == scenario.rates.physics &&
                        mode.target_engine_speed_rpm.first_step_index == 0U &&
                        mode.target_engine_speed_rpm.post_step_rpm.size() ==
                            contract::resolve_frame_index(
                                scenario.total_duration_s.value, scenario.rates.physics)
                                .value_or(0U),
                    ContractIssueCode::inconsistent_shape,
                    "mode.target_engine_speed_rpm",
                    "held-dyno target lane must cover the exact physics horizon");
                for (std::size_t index = 0;
                     index < mode.target_engine_speed_rpm.post_step_rpm.size();
                     ++index) {
                    require(report,
                            finite_positive(
                                mode.target_engine_speed_rpm.post_step_rpm[index]),
                            ContractIssueCode::invalid_value,
                            "mode.target_engine_speed_rpm.post_step_rpm[" +
                                std::to_string(index) + "]",
                            "held-dyno target speed must remain finite and positive");
                }
                const auto preparation_frame_count = contract::resolve_frame_index(
                    scenario.audible_start_s.value, scenario.rates.physics);
                bool preparation_target_matches_initial =
                    preparation_frame_count.has_value() &&
                    *preparation_frame_count <=
                        mode.target_engine_speed_rpm.post_step_rpm.size();
                if (preparation_target_matches_initial) {
                    const auto initial_bits = std::bit_cast<std::uint64_t>(
                        mode.initial_engine_speed_rpm.value);
                    for (std::uint64_t index = 0; index < *preparation_frame_count;
                         ++index) {
                        if (std::bit_cast<std::uint64_t>(
                                mode.target_engine_speed_rpm
                                    .post_step_rpm[static_cast<std::size_t>(index)]) !=
                            initial_bits) {
                            preparation_target_matches_initial = false;
                            break;
                        }
                    }
                }
                require(report, preparation_target_matches_initial,
                        ContractIssueCode::inconsistent_semantics,
                        "mode.target_engine_speed_rpm",
                        "held-dyno target must remain at the exact initial speed "
                        "through fixed held preparation");
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
                validate_resolved(report, mode.constraint_method, provenance,
                                  "scenario.mode.constraint_method");
                append_prefixed(report, validate(mode.constraint_method.value),
                                "mode.constraint_method");
            } else if constexpr (std::is_same_v<T, LoadTargetHeldCapture>) {
                validate_resolved(report, mode.engine_speed_rpm, provenance,
                                  "scenario.mode.engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.target_net_bmep_pa, provenance,
                                  "scenario.mode.target_net_bmep_pa");
                validate_resolved(report, mode.target_tolerance_pa, provenance,
                                  "scenario.mode.target_tolerance_pa");
                validate_resolved(report, mode.throttle_lower_bound_01, provenance,
                                  "scenario.mode.throttle_lower_bound_01");
                validate_resolved(report, mode.throttle_upper_bound_01, provenance,
                                  "scenario.mode.throttle_upper_bound_01");
                require(report,
                        finite_positive(mode.engine_speed_rpm.value) &&
                            finite(mode.initial_theta_rad.value) &&
                            finite(mode.target_net_bmep_pa.value) &&
                            finite_positive(mode.target_tolerance_pa.value),
                        ContractIssueCode::invalid_value, "mode",
                        "speed and target tolerance must be positive; signed target "
                        "must be finite");
                require(report,
                        detail::unit_interval(mode.throttle_lower_bound_01.value) &&
                            detail::unit_interval(mode.throttle_upper_bound_01.value) &&
                            mode.throttle_lower_bound_01.value <=
                                mode.throttle_upper_bound_01.value,
                        ContractIssueCode::invalid_value, "mode",
                        "throttle bounds must form an ordered subset of [0, 1]");
                validate_resolved(report, mode.search_method, provenance,
                                  "scenario.mode.search_method");
                append_prefixed(report, validate(mode.search_method.value),
                                "mode.search_method");
            } else if constexpr (std::is_same_v<T, InertialDyno>) {
                validate_resolved(report, mode.initial_engine_speed_rpm, provenance,
                                  "scenario.mode.initial_engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.equivalent_inertia_kg_m2, provenance,
                                  "scenario.mode.equivalent_inertia_kg_m2");
                validate_resolved(report, mode.target_engine_speed_rpm, provenance,
                                  "scenario.mode.target_engine_speed_rpm");
                require(report,
                        finite_positive(mode.initial_engine_speed_rpm.value) &&
                            finite(mode.initial_theta_rad.value) &&
                            finite_positive(mode.equivalent_inertia_kg_m2.value) &&
                            finite_positive(mode.target_engine_speed_rpm.value) &&
                            mode.target_engine_speed_rpm.value >
                                mode.initial_engine_speed_rpm.value,
                        ContractIssueCode::invalid_value, "mode",
                        "inertial dyno requires a positive initial speed and inertia "
                        "plus an upward target speed");
                const auto *sampling =
                    std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
                require(report, sampling != nullptr,
                        ContractIssueCode::unsupported_value, "preparation",
                        "inertial dyno requires fixed-horizon sampling before "
                        "release");
                if (sampling != nullptr) {
                    require(report,
                            std::bit_cast<std::uint64_t>(
                                sampling->fixed_preparation_horizon_s.value) ==
                                std::bit_cast<std::uint64_t>(
                                    scenario.audible_start_s.value),
                            ContractIssueCode::inconsistent_semantics,
                            "preparation.fixed_preparation_horizon_s.value",
                            "inertial-dyno release is exactly the fixed horizon "
                            "and audible-start boundary");
                }
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
                validate_resolution_id(report, mode.brake_curve_resolution_id,
                                       provenance, "mode.brake_curve_resolution_id",
                                       "scenario.mode.brake_curve");
                require(report, !mode.brake_curve.empty(),
                        ContractIssueCode::missing_value, "mode.brake_curve",
                        "inertial dyno requires a passive brake curve");
                for (std::size_t index = 0; index < mode.brake_curve.size(); ++index) {
                    const auto &point = mode.brake_curve[index];
                    const auto path = "mode.brake_curve[" + std::to_string(index) + "]";
                    require(report,
                            finite_nonnegative(point.angular_speed_rad_s) &&
                                finite_nonnegative(point.resisting_torque_nm),
                            ContractIssueCode::invalid_value, path,
                            "passive brake speed and resisting magnitude must be "
                            "nonnegative");
                    if (index != 0) {
                        require(report,
                                point.angular_speed_rad_s >
                                    mode.brake_curve[index - 1].angular_speed_rad_s,
                                ContractIssueCode::inconsistent_semantics,
                                path + ".angular_speed_rad_s",
                                "brake-curve speeds must be strictly increasing");
                    }
                }
                if (!mode.brake_curve.empty() &&
                    finite_positive(mode.initial_engine_speed_rpm.value) &&
                    finite_positive(mode.target_engine_speed_rpm.value)) {
                    constexpr double radians_per_second_per_rpm =
                        std::numbers::pi_v<double> / 30.0;
                    const double initial_angular_speed =
                        mode.initial_engine_speed_rpm.value *
                        radians_per_second_per_rpm;
                    const double target_angular_speed =
                        mode.target_engine_speed_rpm.value * radians_per_second_per_rpm;
                    require(report,
                            mode.brake_curve.front().angular_speed_rad_s <=
                                    initial_angular_speed &&
                                mode.brake_curve.back().angular_speed_rad_s >=
                                    target_angular_speed,
                            ContractIssueCode::inconsistent_semantics,
                            "mode.brake_curve",
                            "passive brake curve must bracket the requested initial-to-"
                            "target pull without extrapolation");
                }
                validate_resolved(report, mode.crank_dynamics_method, provenance,
                                  "scenario.mode.crank_dynamics_method");
                append_prefixed(report, validate(mode.crank_dynamics_method.value),
                                "mode.crank_dynamics_method");
                validate_resolved(report, mode.brake_torque_method, provenance,
                                  "scenario.mode.brake_torque_method");
                append_prefixed(report, validate(mode.brake_torque_method.value),
                                "mode.brake_torque_method");
            } else if constexpr (std::is_same_v<T, FreeEngine>) {
                validate_resolved(report, mode.initial_engine_speed_rpm, provenance,
                                  "scenario.mode.initial_engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.engine_baseline_inertia_kg_m2,
                                  provenance,
                                  "scenario.mode.engine_baseline_inertia_kg_m2");
                validate_resolved(report, mode.attached_inertia_kg_m2, provenance,
                                  "scenario.mode.attached_inertia_kg_m2");
                validate_resolved(report, mode.total_equivalent_inertia_kg_m2,
                                  provenance,
                                  "scenario.mode.total_equivalent_inertia_kg_m2");
                const double expected_total = mode.engine_baseline_inertia_kg_m2.value +
                                              mode.attached_inertia_kg_m2.value;
                require(report,
                        finite_nonnegative(mode.initial_engine_speed_rpm.value) &&
                            !(mode.initial_engine_speed_rpm.value == 0.0 &&
                              std::signbit(mode.initial_engine_speed_rpm.value)) &&
                            finite(mode.initial_theta_rad.value) &&
                            finite_positive(mode.engine_baseline_inertia_kg_m2.value) &&
                            finite_nonnegative(mode.attached_inertia_kg_m2.value) &&
                            !std::signbit(mode.attached_inertia_kg_m2.value) &&
                            finite_positive(expected_total) &&
                            std::bit_cast<std::uint64_t>(expected_total) ==
                                std::bit_cast<std::uint64_t>(
                                    mode.total_equivalent_inertia_kg_m2.value),
                        ContractIssueCode::invalid_value, "mode",
                        "free engine requires a canonical nonnegative initial speed, "
                        "a positive engine inertia, a positive-zero-or-positive "
                        "attachment, their exact finite total, and a finite crank "
                        "angle");
                const auto *sampling =
                    std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
                const auto *settling =
                    std::get_if<FixedSettling>(&scenario.preparation);
                if (mode.initial_engine_speed_rpm.value > 0.0) {
                    require(report, sampling != nullptr,
                            ContractIssueCode::unsupported_value, "preparation",
                            "positive-speed free engine requires fixed-horizon cycle "
                            "sampling before release");
                } else {
                    require(report,
                            settling != nullptr &&
                                settling->warm_up_duration_s.value == 0.0 &&
                                !std::signbit(settling->warm_up_duration_s.value) &&
                                settling->settling_duration_s.value == 0.0 &&
                                !std::signbit(settling->settling_duration_s.value) &&
                                scenario.audible_start_s.value == 0.0 &&
                                !std::signbit(scenario.audible_start_s.value),
                            ContractIssueCode::inconsistent_semantics, "preparation",
                            "zero-speed free engine requires canonical zero-duration "
                            "fixed settling and an immediate canonical-zero audible "
                            "start");
                }
                if (sampling != nullptr) {
                    require(report,
                            sampling->fixed_preparation_horizon_s.value <=
                                scenario.audible_start_s.value,
                            ContractIssueCode::inconsistent_semantics,
                            "preparation.fixed_preparation_horizon_s.value",
                            "free-engine release is the fixed preparation horizon and "
                            "must not follow audible start");
                }
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
                validate_trajectory(report, mode.external_resisting_torque_nm,
                                    provenance, scenario.total_duration_s.value, false,
                                    true, "scenario.mode.external_resisting_torque_nm");
                validate_resolved(report, mode.crank_dynamics_method, provenance,
                                  "scenario.mode.crank_dynamics_method");
                append_prefixed(report, validate(mode.crank_dynamics_method.value),
                                "mode.crank_dynamics_method");
            }
        },
        scenario.mode);

    return report;
}

ValidationReport validate_for_engine(const RenderScenario &scenario,
                                     const EngineSpec &spec) {
    ValidationReport report;
    if (scenario.engine_profile_id != spec.profile_id.value) {
        report.add(ContractIssueCode::inconsistent_semantics, "engine_profile_id",
                   "scenario and engine profile IDs must match");
    }
    std::visit(
        [&](const auto &profile) {
            if (scenario.fuel.fuel_id.value != profile.core.fuel.fuel_id.value ||
                scenario.fuel.lower_heating_value_j_per_kg.value !=
                    profile.core.fuel.energy_density_j_per_kg.value) {
                report.add(
                    ContractIssueCode::inconsistent_semantics, "fuel",
                    "scenario fuel identity and heating value must exactly match "
                    "the executable engine fuel");
            }

            using Profile = std::decay_t<decltype(profile)>;
            if constexpr (std::is_same_v<Profile, LowOrderOperatingPointV1Profile>) {
                if (!std::holds_alternative<HeldSpeed>(scenario.mode) &&
                    !std::holds_alternative<HeldDyno>(scenario.mode) &&
                    !std::holds_alternative<InertialDyno>(scenario.mode) &&
                    !std::holds_alternative<FreeEngine>(scenario.mode)) {
                    report.add(ContractIssueCode::unsupported_value, "mode",
                               "operating-point v1 admits held-speed, held-dyno, "
                               "inertial-dyno, and free-engine modes");
                }
                const bool free_engine =
                    std::holds_alternative<FreeEngine>(scenario.mode);
                double free_engine_release_s = scenario.audible_start_s.value;
                if (free_engine) {
                    if (const auto *sampling = std::get_if<FixedHorizonCycleSampling>(
                            &scenario.preparation)) {
                        free_engine_release_s =
                            sampling->fixed_preparation_horizon_s.value;
                    } else {
                        free_engine_release_s = 0.0;
                    }
                }
                if (!free_engine) {
                    const auto *sampling =
                        std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
                    if (sampling == nullptr) {
                        report.add(
                            ContractIssueCode::unsupported_value, "preparation",
                            "held and inertial operating-point execution requires "
                            "fixed-horizon cycle sampling");
                    } else if (std::bit_cast<std::uint64_t>(
                                   sampling->fixed_preparation_horizon_s.value) !=
                               std::bit_cast<std::uint64_t>(
                                   scenario.audible_start_s.value)) {
                        report.add(
                            ContractIssueCode::inconsistent_semantics,
                            "preparation.fixed_preparation_horizon_s.value",
                            "operating-point preparation horizon must exactly equal "
                            "audible start");
                    }
                }

                if (std::bit_cast<std::uint64_t>(
                        scenario.initial_thermal_state.oil_temperature_k.value) !=
                    std::bit_cast<std::uint64_t>(
                        profile.aggregate_loss.required_oil_temperature_k.value)) {
                    report.add(ContractIssueCode::inconsistent_semantics,
                               "initial_thermal_state.oil_temperature_k.value",
                               "scenario oil temperature must exactly equal the "
                               "operating-profile applicability condition");
                }

                const bool operating_state_admitted =
                    !scenario.operating_state.value.empty() &&
                    std::ranges::all_of(
                        scenario.operating_state.value, [&](const auto &point) {
                            const auto &state = point.state;
                            if (free_engine) {
                                return !state.dyno_enabled &&
                                       (!state.starter_enabled ||
                                        (profile.starter.type.value ==
                                             StarterCapabilityType::cranking &&
                                         point.time_s >= free_engine_release_s));
                            }
                            return state.ignition_enabled && state.fuel_enabled &&
                                   !state.starter_enabled && state.dyno_enabled &&
                                   !state.limiter_enabled;
                        });
                if (!operating_state_admitted) {
                    report.add(
                        ContractIssueCode::inconsistent_semantics,
                        "operating_state.value",
                        free_engine
                            ? "free-engine operating-point v1 requires dyno disabled "
                              "and permits starter engagement only when the engine "
                              "has a compiled cranking starter and held preparation "
                              "has ended; ignition and fuel may change while the "
                              "crank remains dynamically owned"
                            : "operating-point v1 requires fired held-running state at "
                              "every journal point");
                }
            }
        },
        spec.physics_profile);

    const auto &torque_capability = spec.torque_capability.value;
    const bool needs_complete_cycle_result =
        std::holds_alternative<HeldSpeed>(scenario.mode) ||
        std::holds_alternative<LoadTargetHeldCapture>(scenario.mode);
    const bool has_complete_cycle_mean =
        torque_capability.cycle_mean_net_shaft.availability ==
            Availability::available &&
        torque_capability.cycle_mean_net_shaft.completeness == Completeness::complete;
    if (needs_complete_cycle_result && !has_complete_cycle_mean) {
        report.add(ContractIssueCode::unsupported_value, "mode",
                   "held-speed and load-target scenarios require an available, "
                   "complete cycle-mean net-shaft torque form");
    }
    const bool has_complete_instantaneous_net =
        torque_capability.instantaneous_net_shaft.availability ==
            Availability::available &&
        torque_capability.instantaneous_net_shaft.completeness ==
            Completeness::complete;
    if ((std::holds_alternative<HeldDyno>(scenario.mode) ||
         std::holds_alternative<InertialDyno>(scenario.mode) ||
         std::holds_alternative<FreeEngine>(scenario.mode)) &&
        (!has_complete_instantaneous_net ||
         !torque_capability.equivalent_inertia_available)) {
        report.add(ContractIssueCode::unsupported_value, "mode",
                   "dynamic crank motion requires an available, complete "
                   "instantaneous net-shaft torque form and equivalent inertia");
    }
    return report;
}

} // namespace engine_sim_offline::contract
