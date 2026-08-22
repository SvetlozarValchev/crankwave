#include "simulation/bounded_clutch_coupling.hpp"

#include <algorithm>
#include <cmath>

namespace crankwave::simulation::detail {
namespace {

[[nodiscard]] ForwardGearReductionInputError
gear_input_error(ForwardGearReductionInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] BoundedClutchCouplingInputError
clutch_input_error(BoundedClutchCouplingInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] bool negative_or_negative_zero(double value) noexcept {
    return value < 0.0 || std::signbit(value);
}

[[nodiscard]] bool valid_reduction(const ForwardGearReduction &reduction) noexcept {
    const auto &input = reduction.input;
    if (!std::isfinite(input.vehicle_mass_kg) || !(input.vehicle_mass_kg > 0.0) ||
        !std::isfinite(input.gear_ratio) || !(input.gear_ratio > 0.0) ||
        !std::isfinite(input.differential_ratio) || !(input.differential_ratio > 0.0) ||
        !std::isfinite(input.tire_radius_m) || !(input.tire_radius_m > 0.0) ||
        !std::isfinite(reduction.crank_speed_per_vehicle_speed_rad_per_m) ||
        !(reduction.crank_speed_per_vehicle_speed_rad_per_m > 0.0) ||
        !std::isfinite(reduction.crank_reflected_vehicle_inertia_kg_m2) ||
        !(reduction.crank_reflected_vehicle_inertia_kg_m2 > 0.0) ||
        !std::isfinite(reduction.wheel_force_per_crank_torque_n_per_nm) ||
        !(reduction.wheel_force_per_crank_torque_n_per_nm > 0.0)) {
        return false;
    }
    const double expected_factor =
        input.gear_ratio * input.differential_ratio / input.tire_radius_m;
    const double expected_inertia =
        input.vehicle_mass_kg / (expected_factor * expected_factor);
    return reduction.crank_speed_per_vehicle_speed_rad_per_m == expected_factor &&
           reduction.wheel_force_per_crank_torque_n_per_nm == expected_factor &&
           reduction.crank_reflected_vehicle_inertia_kg_m2 == expected_inertia;
}

} // namespace

ForwardGearReductionCalculation
calculate_forward_gear_reduction(const ForwardGearReductionInput &input) noexcept {
    using Issue = ForwardGearReductionInputIssue;
    if (!std::isfinite(input.vehicle_mass_kg)) {
        return gear_input_error(Issue::nonfinite_vehicle_mass);
    }
    if (!(input.vehicle_mass_kg > 0.0)) {
        return gear_input_error(Issue::nonpositive_vehicle_mass);
    }
    if (!std::isfinite(input.gear_ratio)) {
        return gear_input_error(Issue::nonfinite_gear_ratio);
    }
    if (!(input.gear_ratio > 0.0)) {
        return gear_input_error(Issue::nonpositive_gear_ratio);
    }
    if (!std::isfinite(input.differential_ratio)) {
        return gear_input_error(Issue::nonfinite_differential_ratio);
    }
    if (!(input.differential_ratio > 0.0)) {
        return gear_input_error(Issue::nonpositive_differential_ratio);
    }
    if (!std::isfinite(input.tire_radius_m)) {
        return gear_input_error(Issue::nonfinite_tire_radius);
    }
    if (!(input.tire_radius_m > 0.0)) {
        return gear_input_error(Issue::nonpositive_tire_radius);
    }

    const double factor =
        input.gear_ratio * input.differential_ratio / input.tire_radius_m;
    const double reflected_inertia = input.vehicle_mass_kg / (factor * factor);
    if (!std::isfinite(factor) || !(factor > 0.0) ||
        !std::isfinite(reflected_inertia) || !(reflected_inertia > 0.0)) {
        return gear_input_error(Issue::nonfinite_derived_value);
    }
    return ForwardGearReduction{input, factor, reflected_inertia, factor};
}

BoundedClutchCouplingCalculation
advance_bounded_clutch_coupling(const BoundedClutchCouplingInput &input) noexcept {
    using Issue = BoundedClutchCouplingInputIssue;
    if (!std::isfinite(input.engine_inertia_kg_m2)) {
        return clutch_input_error(Issue::nonfinite_engine_inertia);
    }
    if (!(input.engine_inertia_kg_m2 > 0.0)) {
        return clutch_input_error(Issue::nonpositive_engine_inertia);
    }
    if (!std::isfinite(input.predicted_engine_speed_rad_s)) {
        return clutch_input_error(Issue::nonfinite_predicted_engine_speed);
    }
    if (negative_or_negative_zero(input.predicted_engine_speed_rad_s)) {
        return clutch_input_error(Issue::negative_predicted_engine_speed);
    }
    if (!std::isfinite(input.predicted_vehicle_speed_m_s)) {
        return clutch_input_error(Issue::nonfinite_predicted_vehicle_speed);
    }
    if (negative_or_negative_zero(input.predicted_vehicle_speed_m_s)) {
        return clutch_input_error(Issue::negative_predicted_vehicle_speed);
    }
    if (input.selected_gear.has_value() && !valid_reduction(*input.selected_gear)) {
        return clutch_input_error(Issue::invalid_selected_gear);
    }
    if (!std::isfinite(input.maximum_clutch_torque_nm)) {
        return clutch_input_error(Issue::nonfinite_maximum_clutch_torque);
    }
    if (negative_or_negative_zero(input.maximum_clutch_torque_nm)) {
        return clutch_input_error(Issue::negative_maximum_clutch_torque);
    }
    if (!std::isfinite(input.clutch_engagement_01)) {
        return clutch_input_error(Issue::nonfinite_clutch_engagement);
    }
    if (negative_or_negative_zero(input.clutch_engagement_01) ||
        input.clutch_engagement_01 > 1.0) {
        return clutch_input_error(Issue::clutch_engagement_out_of_range);
    }
    if (!std::isfinite(input.duration_s)) {
        return clutch_input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return clutch_input_error(Issue::nonpositive_duration);
    }

    const double torque_capacity_nm =
        input.maximum_clutch_torque_nm * input.clutch_engagement_01;
    if (!std::isfinite(torque_capacity_nm)) {
        return clutch_input_error(Issue::nonfinite_derived_value);
    }
    if (!input.selected_gear.has_value()) {
        return BoundedClutchCouplingStep{
            input,
            BoundedClutchCouplingDisposition::neutral,
            std::nullopt,
            std::nullopt,
            torque_capacity_nm,
            0.0,
            0.0,
            0.0,
            input.predicted_engine_speed_rad_s,
            input.predicted_vehicle_speed_m_s,
        };
    }

    const auto &reduction = *input.selected_gear;
    const double factor = reduction.crank_speed_per_vehicle_speed_rad_per_m;
    const double reflected_inertia = reduction.crank_reflected_vehicle_inertia_kg_m2;
    const double predicted_vehicle_shaft_speed_rad_s =
        factor * input.predicted_vehicle_speed_m_s;
    const double predicted_slip_rad_s =
        input.predicted_engine_speed_rad_s - predicted_vehicle_shaft_speed_rad_s;
    const double inverse_inertia_sum =
        1.0 / input.engine_inertia_kg_m2 + 1.0 / reflected_inertia;
    const double required_torque_on_engine_nm =
        -predicted_slip_rad_s / (input.duration_s * inverse_inertia_sum);
    const double applied_torque_on_engine_nm = std::clamp(
        required_torque_on_engine_nm, -torque_capacity_nm, torque_capacity_nm);

    double final_engine_speed_rad_s = 0.0;
    double final_vehicle_speed_m_s = 0.0;
    if (applied_torque_on_engine_nm == required_torque_on_engine_nm) {
        const double common_shaft_speed_rad_s =
            (input.engine_inertia_kg_m2 * input.predicted_engine_speed_rad_s +
             reflected_inertia * predicted_vehicle_shaft_speed_rad_s) /
            (input.engine_inertia_kg_m2 + reflected_inertia);
        final_engine_speed_rad_s = common_shaft_speed_rad_s;
        final_vehicle_speed_m_s = common_shaft_speed_rad_s / factor;
    } else {
        final_engine_speed_rad_s =
            input.predicted_engine_speed_rad_s +
            applied_torque_on_engine_nm * input.duration_s / input.engine_inertia_kg_m2;
        const double wheel_force_n = -applied_torque_on_engine_nm *
                                     reduction.wheel_force_per_crank_torque_n_per_nm;
        final_vehicle_speed_m_s =
            input.predicted_vehicle_speed_m_s +
            wheel_force_n * input.duration_s / reduction.input.vehicle_mass_kg;
    }
    const double applied_wheel_force_n =
        -applied_torque_on_engine_nm * reduction.wheel_force_per_crank_torque_n_per_nm;
    const double final_slip_rad_s =
        final_engine_speed_rad_s - factor * final_vehicle_speed_m_s;

    if (!std::isfinite(predicted_vehicle_shaft_speed_rad_s) ||
        !std::isfinite(predicted_slip_rad_s) || !std::isfinite(inverse_inertia_sum) ||
        !(inverse_inertia_sum > 0.0) || !std::isfinite(required_torque_on_engine_nm) ||
        !std::isfinite(applied_torque_on_engine_nm) ||
        !std::isfinite(applied_wheel_force_n) ||
        !std::isfinite(final_engine_speed_rad_s) ||
        negative_or_negative_zero(final_engine_speed_rad_s) ||
        !std::isfinite(final_vehicle_speed_m_s) ||
        negative_or_negative_zero(final_vehicle_speed_m_s) ||
        !std::isfinite(final_slip_rad_s)) {
        return clutch_input_error(Issue::nonfinite_derived_value);
    }

    BoundedClutchCouplingDisposition disposition =
        BoundedClutchCouplingDisposition::tracking;
    if (input.clutch_engagement_01 == 0.0) {
        disposition = BoundedClutchCouplingDisposition::disengaged;
    } else if (applied_torque_on_engine_nm > required_torque_on_engine_nm) {
        disposition = BoundedClutchCouplingDisposition::engine_driving_torque_limited;
    } else if (applied_torque_on_engine_nm < required_torque_on_engine_nm) {
        disposition =
            BoundedClutchCouplingDisposition::vehicle_backdrive_torque_limited;
    }

    return BoundedClutchCouplingStep{
        input,
        disposition,
        predicted_slip_rad_s,
        final_slip_rad_s,
        torque_capacity_nm,
        required_torque_on_engine_nm,
        applied_torque_on_engine_nm,
        applied_wheel_force_n,
        final_engine_speed_rad_s,
        final_vehicle_speed_m_s,
    };
}

} // namespace crankwave::simulation::detail
