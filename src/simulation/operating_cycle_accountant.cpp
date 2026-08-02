#include "simulation/operating_cycle_accountant.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

[[nodiscard]] bool canonical_positive_zero(double value) noexcept {
    return value == 0.0 && !std::signbit(value);
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] OperatingCycleAccountingError compile_error(
    OperatingCycleAccountingErrorCode code,
    std::size_t element_index = kNoOperatingCycleAccountingElement,
    std::optional<FourStrokeCycleIntegrationErrorCode> quadrature_error = std::nullopt,
    std::optional<ChenFlynnCycleMeanLossErrorCode> aggregate_loss_error = std::nullopt,
    std::optional<ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode>
        per_cylinder_travel_aggregate_loss_error = std::nullopt) noexcept {
    return {
        code,
        0,
        element_index,
        quadrature_error,
        aggregate_loss_error,
        per_cylinder_travel_aggregate_loss_error,
    };
}

[[nodiscard]] bool
exact_term_partition(const OperatingCycleAccountingPlan &plan) noexcept {
    const auto indicated = contract::indicated_gas_torque_term_mask();
    const auto aggregate = contract::friction_pump_and_accessory_torque_term_mask();
    const auto starter = contract::torque_term_mask(contract::TorqueTerm::starter);
    return plan.starter_mechanically_disengaged && plan.indicated_terms == indicated &&
           plan.aggregate_loss_terms == aggregate && plan.starter_terms == starter &&
           (plan.indicated_terms & plan.aggregate_loss_terms) == 0 &&
           (plan.indicated_terms & plan.starter_terms) == 0 &&
           (plan.aggregate_loss_terms & plan.starter_terms) == 0 &&
           (plan.indicated_terms | plan.aggregate_loss_terms | plan.starter_terms) ==
               contract::known_torque_term_mask();
}

} // namespace

OperatingCycleAccountant::OperatingCycleAccountant(
    OperatingCycleAccountingPlan plan, FourStrokeCycleIntegrator quadrature,
    std::vector<std::size_t> cylinder_volume_indices)
    : plan_(std::move(plan)), quadrature_(std::move(quadrature)),
      cylinder_volume_indices_(std::move(cylinder_volume_indices)),
      current_cycle_peak_pressures_pa_abs_(plan_.cylinders.size(), 0.0) {
    previous_gas_volumes_.reserve(plan_.physically_resolved_gas_volumes.size());
}

OperatingCycleAccountant::OperatingCycleAccountant(
    OperatingCycleAccountant &&other) noexcept
    : plan_(std::move(other.plan_)), quadrature_(std::move(other.quadrature_)),
      cylinder_volume_indices_(std::move(other.cylinder_volume_indices_)),
      previous_gas_volumes_(std::move(other.previous_gas_volumes_)),
      current_cycle_peak_pressures_pa_abs_(
          std::move(other.current_cycle_peak_pressures_pa_abs_)),
      completed_cycle_count_(other.completed_cycle_count_),
      terminal_error_(other.terminal_error_) {
    other.invalidate_after_move();
}

OperatingCycleAccountant &
OperatingCycleAccountant::operator=(OperatingCycleAccountant &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    plan_ = std::move(other.plan_);
    quadrature_ = std::move(other.quadrature_);
    cylinder_volume_indices_ = std::move(other.cylinder_volume_indices_);
    previous_gas_volumes_ = std::move(other.previous_gas_volumes_);
    current_cycle_peak_pressures_pa_abs_ =
        std::move(other.current_cycle_peak_pressures_pa_abs_);
    completed_cycle_count_ = other.completed_cycle_count_;
    terminal_error_ = other.terminal_error_;
    other.invalidate_after_move();
    return *this;
}

void OperatingCycleAccountant::invalidate_after_move() noexcept {
    plan_ = {};
    cylinder_volume_indices_.clear();
    previous_gas_volumes_.clear();
    current_cycle_peak_pressures_pa_abs_.clear();
    completed_cycle_count_ = 0;
    terminal_error_ = OperatingCycleAccountingError{
        OperatingCycleAccountingErrorCode::moved_from,
        0,
        kNoOperatingCycleAccountingElement,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    };
}

OperatingCycleAccountingAdvanceResult OperatingCycleAccountant::fail(
    OperatingCycleAccountingErrorCode code, std::uint64_t sample_index,
    std::size_t element_index,
    std::optional<FourStrokeCycleIntegrationErrorCode> quadrature_error,
    std::optional<ChenFlynnCycleMeanLossErrorCode> aggregate_loss_error,
    std::optional<ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode>
        per_cylinder_travel_aggregate_loss_error) {
    terminal_error_ = OperatingCycleAccountingError{
        code,
        sample_index,
        element_index,
        quadrature_error,
        aggregate_loss_error,
        per_cylinder_travel_aggregate_loss_error,
    };
    return *terminal_error_;
}

std::optional<OperatingCycleAccountingError> OperatingCycleAccountant::validate_sample(
    const OperatingCycleSample &sample) const noexcept {
    if (sample.physically_resolved_gas_volumes.size() !=
        plan_.physically_resolved_gas_volumes.size()) {
        return OperatingCycleAccountingError{
            OperatingCycleAccountingErrorCode::malformed_sample,
            sample.sample_index,
            kNoOperatingCycleAccountingElement,
            std::nullopt,
            std::nullopt,
            std::nullopt,
        };
    }

    if (!std::isfinite(sample.engine_speed_rpm) || sample.engine_speed_rpm <= 0.0 ||
        (!plan_.derive_mean_engine_speed_from_cycle_duration &&
         !same_binary64(sample.engine_speed_rpm, plan_.engine_speed_rpm))) {
        return OperatingCycleAccountingError{
            OperatingCycleAccountingErrorCode::engine_speed_mismatch,
            sample.sample_index,
            kNoOperatingCycleAccountingElement,
            std::nullopt,
            std::nullopt,
            std::nullopt,
        };
    }

    for (std::size_t index = 0; index < sample.physically_resolved_gas_volumes.size();
         ++index) {
        const auto &actual = sample.physically_resolved_gas_volumes[index];
        if (actual.gas_volume_id != plan_.physically_resolved_gas_volumes[index]) {
            return OperatingCycleAccountingError{
                OperatingCycleAccountingErrorCode::malformed_sample,
                sample.sample_index,
                index,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            };
        }
        if (!std::isfinite(actual.pressure_pa_abs)) {
            return OperatingCycleAccountingError{
                OperatingCycleAccountingErrorCode::nonfinite_pressure,
                sample.sample_index,
                index,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            };
        }
        if (actual.pressure_pa_abs <= 0.0) {
            return OperatingCycleAccountingError{
                OperatingCycleAccountingErrorCode::nonpositive_pressure,
                sample.sample_index,
                index,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            };
        }
    }
    return std::nullopt;
}

void OperatingCycleAccountant::seed_cycle_peaks(
    std::span<const OperatingGasVolumePressureSample> gas_volumes) noexcept {
    for (std::size_t index = 0; index < cylinder_volume_indices_.size(); ++index) {
        current_cycle_peak_pressures_pa_abs_[index] =
            gas_volumes[cylinder_volume_indices_[index]].pressure_pa_abs;
    }
}

void OperatingCycleAccountant::retain_cycle_peaks(
    std::span<const OperatingGasVolumePressureSample> gas_volumes) noexcept {
    for (std::size_t index = 0; index < cylinder_volume_indices_.size(); ++index) {
        current_cycle_peak_pressures_pa_abs_[index] =
            std::max(current_cycle_peak_pressures_pa_abs_[index],
                     gas_volumes[cylinder_volume_indices_[index]].pressure_pa_abs);
    }
}

OperatingCycleAccountingAdvanceResult
OperatingCycleAccountant::advance(const OperatingCycleSample &sample) {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (const auto malformed = validate_sample(sample); malformed.has_value()) {
        return fail(malformed->code, malformed->sample_index, malformed->element_index);
    }

    const auto quadrature_result = quadrature_.advance({
        sample.sample_index,
        sample.time_s,
        sample.theta_unwrapped_rad,
        sample.indicated_gas_torque_nm,
        0.0,
        0.0,
    });
    if (const auto *error =
            std::get_if<FourStrokeCycleIntegrationError>(&quadrature_result)) {
        return fail(OperatingCycleAccountingErrorCode::quadrature_failure,
                    error->sample_index, kNoOperatingCycleAccountingElement,
                    error->code);
    }

    if (previous_gas_volumes_.empty()) {
        seed_cycle_peaks(sample.physically_resolved_gas_volumes);
        previous_gas_volumes_.assign(sample.physically_resolved_gas_volumes.begin(),
                                     sample.physically_resolved_gas_volumes.end());
        return NoOperatingCycleBoundaryCrossing{};
    }

    if (std::holds_alternative<NoFourStrokeCycleBoundaryCrossing>(quadrature_result)) {
        retain_cycle_peaks(sample.physically_resolved_gas_volumes);
        previous_gas_volumes_.assign(sample.physically_resolved_gas_volumes.begin(),
                                     sample.physically_resolved_gas_volumes.end());
        return NoOperatingCycleBoundaryCrossing{};
    }

    const auto &crossing = std::get<FourStrokeCycleBoundaryCrossing>(quadrature_result);
    std::vector<OperatingGasVolumePressureSample> boundary_gas_volumes;
    boundary_gas_volumes.reserve(sample.physically_resolved_gas_volumes.size());
    for (std::size_t index = 0; index < sample.physically_resolved_gas_volumes.size();
         ++index) {
        const double pressure = interpolate_cycle_boundary_scalar(
            previous_gas_volumes_[index].pressure_pa_abs,
            sample.physically_resolved_gas_volumes[index].pressure_pa_abs,
            crossing.boundary);
        if (!std::isfinite(pressure)) {
            return fail(OperatingCycleAccountingErrorCode::nonfinite_result,
                        sample.sample_index, index);
        }
        if (pressure <= 0.0) {
            return fail(OperatingCycleAccountingErrorCode::nonpositive_pressure,
                        sample.sample_index, index);
        }
        boundary_gas_volumes.push_back({
            sample.physically_resolved_gas_volumes[index].gas_volume_id,
            pressure,
        });
    }

    retain_cycle_peaks(boundary_gas_volumes);
    std::optional<OperatingCompletedCycle> completed;
    if (crossing.completed_cycle.has_value()) {
        const auto &indicated_cycle = *crossing.completed_cycle;
        if (!canonical_positive_zero(
                indicated_cycle.friction_pump_and_accessory_work_j) ||
            !canonical_positive_zero(indicated_cycle.starter_work_j) ||
            indicated_cycle.summed_torque_work_j !=
                indicated_cycle.indicated_gas_work_j) {
            return fail(OperatingCycleAccountingErrorCode::nonzero_placeholder_work,
                        sample.sample_index);
        }

        std::vector<OperatingCylinderPeakPressure> peak_evidence;
        std::vector<ChenFlynnCylinderPeakPressureInput> loss_inputs;
        peak_evidence.reserve(plan_.cylinders.size());
        loss_inputs.reserve(plan_.cylinders.size());
        for (std::size_t index = 0; index < plan_.cylinders.size(); ++index) {
            const auto &cylinder = plan_.cylinders[index];
            const double peak_pressure = current_cycle_peak_pressures_pa_abs_[index];
            peak_evidence.push_back({
                cylinder.cylinder_id,
                cylinder.chamber_volume_id,
                cylinder.displacement_m3,
                peak_pressure,
            });
            loss_inputs.push_back({
                cylinder.cylinder_id.value,
                cylinder.displacement_m3,
                peak_pressure,
            });
        }

        const double duration_s =
            indicated_cycle.end_time_s - indicated_cycle.start_time_s;
        const double cycle_mean_engine_speed_rpm =
            plan_.derive_mean_engine_speed_from_cycle_duration ? 120.0 / duration_s
                                                               : plan_.engine_speed_rpm;
        ChenFlynnCycleMeanLossResult loss;
        if (const auto *common =
                std::get_if<CommonStrokeChenFlynnLossPlan>(&plan_.aggregate_loss)) {
            const auto loss_calculation = calculate_chen_flynn_cycle_mean_loss(
                common->coefficients, {
                                          cycle_mean_engine_speed_rpm,
                                          common->stroke_m,
                                          loss_inputs,
                                      });
            if (const auto *error =
                    std::get_if<ChenFlynnCycleMeanLossError>(&loss_calculation)) {
                return fail(OperatingCycleAccountingErrorCode::aggregate_loss_failure,
                            sample.sample_index, error->element_index, std::nullopt,
                            error->code);
            }
            loss = std::get<ChenFlynnCycleMeanLossResult>(loss_calculation);
        } else {
            const auto &per_cylinder =
                std::get<PerCylinderTravelChenFlynnLossPlan>(plan_.aggregate_loss);
            std::vector<ChenFlynnPerCylinderPistonTravelInput> per_cylinder_loss_inputs;
            per_cylinder_loss_inputs.reserve(plan_.cylinders.size());
            for (std::size_t index = 0; index < plan_.cylinders.size(); ++index) {
                per_cylinder_loss_inputs.push_back({
                    plan_.cylinders[index].cylinder_id.value,
                    plan_.cylinders[index].displacement_m3,
                    per_cylinder.cylinders[index]
                        .piston_axis_path_length_m_per_crank_revolution,
                    current_cycle_peak_pressures_pa_abs_[index],
                });
            }
            const auto loss_calculation =
                calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
                    per_cylinder.coefficients,
                    {cycle_mean_engine_speed_rpm, per_cylinder_loss_inputs});
            if (const auto *error =
                    std::get_if<ChenFlynnPerCylinderPistonTravelCycleMeanLossError>(
                        &loss_calculation)) {
                return fail(OperatingCycleAccountingErrorCode::aggregate_loss_failure,
                            sample.sample_index, error->element_index, std::nullopt,
                            std::nullopt, error->code);
            }
            const auto &per_cylinder_loss =
                std::get<ChenFlynnPerCylinderPistonTravelCycleMeanLossResult>(
                    loss_calculation);
            // OperatingCompletedCycle retains the established common result shape.
            // The radial calculator's additional weighted U^2 diagnostic is the
            // only field deliberately omitted by this normalization boundary.
            loss = {
                per_cylinder_loss.total_swept_displacement_m3,
                per_cylinder_loss.displacement_weighted_mean_piston_speed_m_s,
                per_cylinder_loss.displacement_weighted_peak_pressure_pa_abs,
                per_cylinder_loss.displacement_weighted_peak_pressure_bar_abs,
                per_cylinder_loss.friction_mean_effective_pressure_bar,
                per_cylinder_loss.positive_aggregate_loss_work_j,
                per_cylinder_loss.running_direction_cycle_mean_loss_torque_nm,
            };
        }
        if (loss.total_displacement_m3 != plan_.quadrature.total_displacement_m3) {
            return fail(
                OperatingCycleAccountingErrorCode::accounting_invariant_violation,
                sample.sample_index);
        }

        const double starter_work_j = 0.0;
        const double brake_work_j = (indicated_cycle.indicated_gas_work_j -
                                     loss.positive_aggregate_loss_work_j) +
                                    starter_work_j;
        const double mean_brake_torque_nm = brake_work_j / kFourStrokeCycleRadians;
        const double net_bmep_pa =
            brake_work_j / plan_.quadrature.total_displacement_m3;
        const double mean_brake_power_w = brake_work_j / duration_s;
        if (!std::isfinite(cycle_mean_engine_speed_rpm) ||
            !(cycle_mean_engine_speed_rpm > 0.0) || !std::isfinite(brake_work_j) ||
            !std::isfinite(mean_brake_torque_nm) || !std::isfinite(net_bmep_pa) ||
            !std::isfinite(mean_brake_power_w)) {
            return fail(OperatingCycleAccountingErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        completed = OperatingCompletedCycle{
            indicated_cycle,
            std::move(peak_evidence),
            cycle_mean_engine_speed_rpm,
            loss,
            starter_work_j,
            brake_work_j,
            mean_brake_torque_nm,
            net_bmep_pa,
            mean_brake_power_w,
        };
        if (completed_cycle_count_ == std::numeric_limits<std::uint64_t>::max()) {
            return fail(OperatingCycleAccountingErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        ++completed_cycle_count_;
    }

    seed_cycle_peaks(boundary_gas_volumes);
    if (sample.theta_unwrapped_rad > crossing.theta_rad) {
        retain_cycle_peaks(sample.physically_resolved_gas_volumes);
    }
    previous_gas_volumes_.assign(sample.physically_resolved_gas_volumes.begin(),
                                 sample.physically_resolved_gas_volumes.end());

    return OperatingCycleBoundaryCrossing{
        crossing.boundary,    crossing.theta_rad,
        crossing.time_s,      std::move(boundary_gas_volumes),
        std::move(completed),
    };
}

bool OperatingCycleAccountant::faulted() const noexcept {
    return terminal_error_.has_value();
}

std::uint64_t OperatingCycleAccountant::completed_cycle_count() const noexcept {
    return completed_cycle_count_;
}

OperatingCycleAccountantCompileResult
compile_operating_cycle_accountant(OperatingCycleAccountingPlan plan) {
    if (!exact_term_partition(plan)) {
        return compile_error(OperatingCycleAccountingErrorCode::invalid_term_partition);
    }
    if (!std::isfinite(plan.engine_speed_rpm) || !(plan.engine_speed_rpm > 0.0)) {
        return compile_error(OperatingCycleAccountingErrorCode::invalid_plan);
    }
    if (plan.cylinders.empty()) {
        return compile_error(OperatingCycleAccountingErrorCode::invalid_cylinder_plan);
    }

    double total_displacement_m3 = 0.0;
    contract::CylinderId previous_cylinder_id;
    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto &cylinder = plan.cylinders[index];
        if (!cylinder.cylinder_id.valid() ||
            (index != 0 && cylinder.cylinder_id <= previous_cylinder_id) ||
            !cylinder.chamber_volume_id.valid() ||
            !std::isfinite(cylinder.displacement_m3) ||
            !(cylinder.displacement_m3 > 0.0)) {
            return compile_error(
                OperatingCycleAccountingErrorCode::invalid_cylinder_plan, index);
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (plan.cylinders[prior].chamber_volume_id == cylinder.chamber_volume_id) {
                return compile_error(
                    OperatingCycleAccountingErrorCode::invalid_cylinder_plan, index);
            }
        }
        const double next_total = total_displacement_m3 + cylinder.displacement_m3;
        if (!std::isfinite(next_total) || !(next_total > total_displacement_m3)) {
            return compile_error(
                OperatingCycleAccountingErrorCode::invalid_cylinder_plan, index);
        }
        total_displacement_m3 = next_total;
        previous_cylinder_id = cylinder.cylinder_id;
    }
    if (total_displacement_m3 != plan.quadrature.total_displacement_m3) {
        return compile_error(OperatingCycleAccountingErrorCode::invalid_cylinder_plan);
    }

    if (plan.physically_resolved_gas_volumes.empty()) {
        return compile_error(
            OperatingCycleAccountingErrorCode::invalid_gas_volume_plan);
    }
    contract::GasVolumeId previous_volume_id;
    for (std::size_t index = 0; index < plan.physically_resolved_gas_volumes.size();
         ++index) {
        const auto volume_id = plan.physically_resolved_gas_volumes[index];
        if (!volume_id.valid() || (index != 0 && volume_id <= previous_volume_id)) {
            return compile_error(
                OperatingCycleAccountingErrorCode::invalid_gas_volume_plan, index);
        }
        previous_volume_id = volume_id;
    }
    std::vector<std::size_t> cylinder_volume_indices;
    cylinder_volume_indices.reserve(plan.cylinders.size());
    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto chamber_id = plan.cylinders[index].chamber_volume_id;
        const auto found =
            std::lower_bound(plan.physically_resolved_gas_volumes.begin(),
                             plan.physically_resolved_gas_volumes.end(), chamber_id);
        if (found == plan.physically_resolved_gas_volumes.end() ||
            *found != chamber_id) {
            return compile_error(
                OperatingCycleAccountingErrorCode::invalid_gas_volume_plan, index);
        }
        cylinder_volume_indices.push_back(static_cast<std::size_t>(
            found - plan.physically_resolved_gas_volumes.begin()));
    }

    std::vector<ChenFlynnCylinderPeakPressureInput> loss_probe;
    loss_probe.reserve(plan.cylinders.size());
    for (const auto &cylinder : plan.cylinders) {
        loss_probe.push_back({
            cylinder.cylinder_id.value,
            cylinder.displacement_m3,
            100000.0,
        });
    }
    if (const auto *common =
            std::get_if<CommonStrokeChenFlynnLossPlan>(&plan.aggregate_loss)) {
        const auto loss_probe_result = calculate_chen_flynn_cycle_mean_loss(
            common->coefficients, {
                                      plan.engine_speed_rpm,
                                      common->stroke_m,
                                      loss_probe,
                                  });
        if (const auto *error =
                std::get_if<ChenFlynnCycleMeanLossError>(&loss_probe_result)) {
            return compile_error(OperatingCycleAccountingErrorCode::invalid_plan,
                                 error->element_index, std::nullopt, error->code);
        }
    } else {
        const auto &per_cylinder =
            std::get<PerCylinderTravelChenFlynnLossPlan>(plan.aggregate_loss);
        if (per_cylinder.cylinders.size() != plan.cylinders.size()) {
            return compile_error(
                OperatingCycleAccountingErrorCode::invalid_cylinder_plan,
                std::min(per_cylinder.cylinders.size(), plan.cylinders.size()));
        }
        std::vector<ChenFlynnPerCylinderPistonTravelInput> per_cylinder_loss_probe;
        per_cylinder_loss_probe.reserve(plan.cylinders.size());
        for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
            if (per_cylinder.cylinders[index].cylinder_id !=
                plan.cylinders[index].cylinder_id) {
                return compile_error(
                    OperatingCycleAccountingErrorCode::invalid_cylinder_plan, index);
            }
            per_cylinder_loss_probe.push_back({
                plan.cylinders[index].cylinder_id.value,
                plan.cylinders[index].displacement_m3,
                per_cylinder.cylinders[index]
                    .piston_axis_path_length_m_per_crank_revolution,
                100000.0,
            });
        }
        const auto loss_probe_result =
            calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
                per_cylinder.coefficients,
                {plan.engine_speed_rpm, per_cylinder_loss_probe});
        if (const auto *error =
                std::get_if<ChenFlynnPerCylinderPistonTravelCycleMeanLossError>(
                    &loss_probe_result)) {
            return compile_error(OperatingCycleAccountingErrorCode::invalid_plan,
                                 error->element_index, std::nullopt, std::nullopt,
                                 error->code);
        }
    }

    auto quadrature = compile_four_stroke_cycle_integrator(plan.quadrature);
    if (const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&quadrature)) {
        return compile_error(OperatingCycleAccountingErrorCode::invalid_plan,
                             kNoOperatingCycleAccountingElement, error->code);
    }
    return OperatingCycleAccountant{
        std::move(plan),
        std::get<FourStrokeCycleIntegrator>(std::move(quadrature)),
        std::move(cylinder_volume_indices),
    };
}

} // namespace engine_sim_offline::simulation
