#include "engine_sim_offline/contract/capture.hpp"

#include "capture_block_admission.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

constexpr CaptureValidityMask kKnownCaptureValidity =
    capture_validity_mask(CaptureValidity::mechanism) |
    capture_validity_mask(CaptureValidity::thermodynamic_state) |
    capture_validity_mask(CaptureValidity::composition) |
    capture_validity_mask(CaptureValidity::gas_exchange) |
    capture_validity_mask(CaptureValidity::combustion) |
    capture_validity_mask(CaptureValidity::torque);

bool has_validity(CaptureValidityMask mask, CaptureValidity flag) noexcept {
    return (mask & capture_validity_mask(flag)) != 0;
}

bool checked_add(std::size_t lhs, std::size_t rhs, std::size_t &result) noexcept {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool checked_product(std::size_t lhs, std::size_t rhs, std::size_t &result) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checked_add_u64(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool all_finite(std::initializer_list<double> values) noexcept {
    return std::ranges::all_of(values, detail::finite);
}

template <std::size_t Extent>
bool all_finite(const std::array<double, Extent> &values) noexcept {
    return std::ranges::all_of(values, detail::finite);
}

bool finite_nonnegative_composition(const MixtureFractions &composition) noexcept {
    return detail::finite_nonnegative(composition.fuel) &&
           detail::finite_nonnegative(composition.inert) &&
           detail::finite_nonnegative(composition.oxygen);
}

bool valid_composition_for_amount(const MixtureFractions &composition,
                                  double amount_mol) noexcept {
    if (!detail::finite_nonnegative(amount_mol) ||
        !finite_nonnegative_composition(composition)) {
        return false;
    }
    if (amount_mol == 0.0) {
        return composition.fuel == 0.0 && composition.inert == 0.0 &&
               composition.oxygen == 0.0;
    }
    const auto sum = composition.fuel + composition.inert + composition.oxygen;
    return detail::finite(sum) && std::abs(sum - 1.0) <= kMixtureFractionUnityTolerance;
}

bool known_mask(CaptureValidityMask mask) noexcept {
    return (mask & ~kKnownCaptureValidity) == 0;
}

bool all_torque_quantities_unavailable(const TorqueTelemetry &telemetry) noexcept {
    return telemetry.instantaneous_indicated_gas.availability ==
               Availability::unavailable &&
           telemetry.pumping_partition.availability == Availability::unavailable &&
           telemetry.friction_pump_and_accessory.availability ==
               Availability::unavailable &&
           telemetry.starter.availability == Availability::unavailable &&
           telemetry.instantaneous_net_shaft.availability ==
               Availability::unavailable &&
           telemetry.cycle_mean_net_shaft.availability == Availability::unavailable &&
           telemetry.actuator.availability == Availability::unavailable &&
           telemetry.dyno_reaction.availability == Availability::unavailable &&
           telemetry.cycle_work_j.availability == Availability::unavailable &&
           telemetry.net_bmep_pa.availability == Availability::unavailable &&
           telemetry.instantaneous_power_w.availability == Availability::unavailable &&
           telemetry.cycle_mean_power_w.availability == Availability::unavailable;
}

bool any_torque_quantity_available(const TorqueTelemetry &telemetry) noexcept {
    return telemetry.instantaneous_indicated_gas.availability ==
               Availability::available ||
           telemetry.pumping_partition.availability == Availability::available ||
           telemetry.friction_pump_and_accessory.availability ==
               Availability::available ||
           telemetry.starter.availability == Availability::available ||
           telemetry.instantaneous_net_shaft.availability == Availability::available ||
           telemetry.cycle_mean_net_shaft.availability == Availability::available ||
           telemetry.actuator.availability == Availability::available ||
           telemetry.dyno_reaction.availability == Availability::available ||
           telemetry.cycle_work_j.availability == Availability::available ||
           telemetry.net_bmep_pa.availability == Availability::available ||
           telemetry.instantaneous_power_w.availability == Availability::available ||
           telemetry.cycle_mean_power_w.availability == Availability::available;
}

bool known(Availability value) noexcept {
    return value == Availability::available || value == Availability::unavailable;
}

bool known(Completeness value) noexcept {
    return value == Completeness::complete || value == Completeness::incomplete;
}

bool known(QuantityUnavailableReason value) noexcept {
    switch (value) {
    case QuantityUnavailableReason::none:
    case QuantityUnavailableReason::scenario_not_applicable:
    case QuantityUnavailableReason::model_not_admitted:
    case QuantityUnavailableReason::equivalent_inertia_missing:
    case QuantityUnavailableReason::cycle_integration_not_admitted:
    case QuantityUnavailableReason::not_settled:
    case QuantityUnavailableReason::required_input_missing:
        return true;
    }
    return false;
}

bool valid_quantity_value(const QuantityValue &value) noexcept {
    if (!known(value.availability) || !known(value.completeness) ||
        !known(value.unavailable_reason)) {
        return false;
    }
    if (value.availability == Availability::available) {
        return detail::finite(value.value) &&
               value.unavailable_reason == QuantityUnavailableReason::none;
    }
    return value.value == 0.0 && !std::signbit(value.value) &&
           value.completeness == Completeness::incomplete &&
           value.unavailable_reason != QuantityUnavailableReason::none;
}

bool valid_torque_value(const TorqueValueNm &value) noexcept {
    if (!known(value.availability) || !known(value.completeness) ||
        !known(value.unavailable_reason) ||
        ((value.included_terms | value.omitted_terms) &
         ~known_torque_term_mask()) != 0 ||
        (value.included_terms & value.omitted_terms) != 0 ||
        (value.completeness == Completeness::complete &&
         value.omitted_terms != 0)) {
        return false;
    }
    if (value.availability == Availability::available) {
        return detail::finite(value.value_nm) &&
               value.unavailable_reason == QuantityUnavailableReason::none;
    }
    return value.value_nm == 0.0 && !std::signbit(value.value_nm) &&
           value.completeness == Completeness::incomplete &&
           value.unavailable_reason != QuantityUnavailableReason::none &&
           value.included_terms == 0 && value.omitted_terms == 0;
}

bool valid_named_torque_scope(const TorqueValueNm &value,
                              TorqueTermMask expected_terms) noexcept {
    return value.availability != Availability::available ||
           (value.included_terms | value.omitted_terms) == expected_terms;
}

bool valid_torque_telemetry(const TorqueTelemetry &telemetry) noexcept {
    if (!valid_torque_value(telemetry.instantaneous_indicated_gas) ||
        !valid_torque_value(telemetry.pumping_partition) ||
        !valid_torque_value(telemetry.friction_pump_and_accessory) ||
        !valid_torque_value(telemetry.starter) ||
        !valid_torque_value(telemetry.instantaneous_net_shaft) ||
        !valid_torque_value(telemetry.cycle_mean_net_shaft) ||
        !valid_torque_value(telemetry.actuator) ||
        !valid_torque_value(telemetry.dyno_reaction) ||
        !valid_quantity_value(telemetry.cycle_work_j) ||
        !valid_quantity_value(telemetry.net_bmep_pa) ||
        !valid_quantity_value(telemetry.instantaneous_power_w) ||
        !valid_quantity_value(telemetry.cycle_mean_power_w) ||
        !valid_named_torque_scope(telemetry.instantaneous_indicated_gas,
                                  indicated_gas_torque_term_mask()) ||
        !valid_named_torque_scope(telemetry.pumping_partition, 0) ||
        !valid_named_torque_scope(
            telemetry.friction_pump_and_accessory,
            friction_pump_and_accessory_torque_term_mask()) ||
        !valid_named_torque_scope(telemetry.starter,
                                  torque_term_mask(TorqueTerm::starter)) ||
        !valid_named_torque_scope(telemetry.instantaneous_net_shaft,
                                  known_torque_term_mask()) ||
        !valid_named_torque_scope(telemetry.cycle_mean_net_shaft,
                                  known_torque_term_mask()) ||
        !valid_named_torque_scope(telemetry.actuator, 0) ||
        !valid_named_torque_scope(telemetry.dyno_reaction, 0)) {
        return false;
    }
    return telemetry.actuator.availability != Availability::available ||
           telemetry.dyno_reaction.availability != Availability::available ||
           telemetry.dyno_reaction.value_nm == -telemetry.actuator.value_nm;
}

bool known(SamplePhase value) noexcept {
    return value == SamplePhase::pre_step || value == SamplePhase::post_step;
}

bool known(PortKind value) noexcept {
    return value == PortKind::intake || value == PortKind::exhaust;
}

bool known(GasVolumeKind value) noexcept {
    switch (value) {
    case GasVolumeKind::atmosphere:
    case GasVolumeKind::intake_plenum:
    case GasVolumeKind::intake_runner:
    case GasVolumeKind::cylinder:
    case GasVolumeKind::exhaust_primary:
    case GasVolumeKind::exhaust_collector:
        return true;
    case GasVolumeKind::unspecified:
        return false;
    }
    return false;
}

bool known_physical_source_route(SourceRouteKind value) noexcept {
    switch (value) {
    case SourceRouteKind::exhaust_outlet:
    case SourceRouteKind::intake_inlet:
    case SourceRouteKind::mechanical_engine:
    case SourceRouteKind::mechanical_starter:
        return true;
    case SourceRouteKind::unspecified:
        return false;
    }
    return false;
}

bool is_gas_source_route(SourceRouteKind value) noexcept {
    return value == SourceRouteKind::exhaust_outlet ||
           value == SourceRouteKind::intake_inlet;
}

bool is_mechanical_source_route(SourceRouteKind value) noexcept {
    return value == SourceRouteKind::mechanical_engine ||
           value == SourceRouteKind::mechanical_starter;
}

bool known(IgnitionRejection value) noexcept {
    switch (value) {
    case IgnitionRejection::active_flame:
    case IgnitionRejection::no_fuel:
    case IgnitionRejection::mixture_low:
    case IgnitionRejection::mixture_high:
        return true;
    case IgnitionRejection::unspecified:
        return false;
    }
    return false;
}

bool known(FlameExtinctionReason value) noexcept {
    return value == FlameExtinctionReason::intake_transfer ||
           value == FlameExtinctionReason::no_geometric_progress;
}

template <class Id> bool contains_id(std::span<const Id> ids, Id target) {
    return std::ranges::find(ids, target) != ids.end();
}

bool contains_cylinder(const CaptureLayoutView &layout, CylinderId cylinder_id) {
    return contains_id(layout.cylinders(), cylinder_id);
}

std::optional<std::size_t> cylinder_order(const CaptureLayoutView &layout,
                                          CylinderId cylinder_id) {
    const auto found = std::ranges::find(layout.cylinders(), cylinder_id);
    if (found == layout.cylinders().end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - layout.cylinders().begin());
}

enum class EventCategory : std::uint8_t {
    spark,
    limiter,
    ignition,
    extinction,
};

EventCategory event_category(const EngineEventPayload &payload) {
    return std::visit(
        [](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SparkCrossing>) {
                return EventCategory::spark;
            } else if constexpr (std::is_same_v<T, LimiterStateChanged>) {
                return EventCategory::limiter;
            } else if constexpr (std::is_same_v<T, IgnitionAccepted> ||
                                 std::is_same_v<T, IgnitionRejected>) {
                return EventCategory::ignition;
            } else {
                return EventCategory::extinction;
            }
        },
        payload);
}

std::optional<CylinderId> event_cylinder_id(const EngineEventPayload &payload) {
    return std::visit(
        [](const auto &value) -> std::optional<CylinderId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LimiterStateChanged>) {
                return std::nullopt;
            } else {
                return value.cylinder_id;
            }
        },
        payload);
}

void validate_event(ValidationReport &report, const EngineEvent &event,
                    const CaptureLayoutView &layout, const std::string &path) {
    using detail::require;

    if (event.payload.valueless_by_exception()) {
        report.add(ContractIssueCode::invalid_value, path + ".payload",
                   "event payload cannot be valueless");
        return;
    }

    std::visit(
        [&](const auto &payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, SparkCrossing>) {
                require(report, contains_cylinder(layout, payload.cylinder_id),
                        ContractIssueCode::dangling_reference, path + ".cylinder_id",
                        "spark event references an unknown cylinder");
                require(report,
                        all_finite({
                            payload.raw_saved_angle_rad,
                            payload.raw_current_angle_rad,
                            payload.adjusted_current_angle_rad,
                            payload.adjusted_spark_angle_rad,
                            payload.timing_advance_rad,
                        }),
                        ContractIssueCode::invalid_value, path,
                        "spark-event angles must be finite");
            } else if constexpr (std::is_same_v<T, LimiterStateChanged>) {
                require(report, payload.old_active != payload.new_active,
                        ContractIssueCode::inconsistent_semantics, path,
                        "limiter-state-changed event must change active state");
                require(report, detail::finite_nonnegative(payload.resulting_timer_s),
                        ContractIssueCode::invalid_value, path + ".resulting_timer_s",
                        "limiter timer must be finite and nonnegative");
            } else if constexpr (std::is_same_v<T, IgnitionAccepted>) {
                require(report, contains_cylinder(layout, payload.cylinder_id),
                        ContractIssueCode::dangling_reference, path + ".cylinder_id",
                        "ignition event references an unknown cylinder");
                require(report,
                        detail::unit_interval(payload.efficiency_01) &&
                            detail::finite_nonnegative(payload.flame_speed_m_s),
                        ContractIssueCode::invalid_value, path,
                        "accepted ignition has invalid efficiency or flame speed");
            } else if constexpr (std::is_same_v<T, IgnitionRejected>) {
                require(report, contains_cylinder(layout, payload.cylinder_id),
                        ContractIssueCode::dangling_reference, path + ".cylinder_id",
                        "ignition rejection references an unknown cylinder");
                require(report, known(payload.reason),
                        ContractIssueCode::unsupported_value, path + ".reason",
                        "ignition-rejection reason is not recognized");
            } else {
                require(report, contains_cylinder(layout, payload.cylinder_id),
                        ContractIssueCode::dangling_reference, path + ".cylinder_id",
                        "flame-extinction event references an unknown cylinder");
                require(report, payload.gas_substep_index <= 7,
                        ContractIssueCode::invalid_value, path + ".gas_substep_index",
                        "low-order reference-parity flame-extinction gas substep must "
                        "be in [0, 7]");
                require(report, known(payload.reason),
                        ContractIssueCode::unsupported_value, path + ".reason",
                        "flame-extinction reason is not recognized");
            }
        },
        event.payload);
}

template <class Sample>
const Sample *frame_major_at(std::span<const Sample> samples, std::size_t frame_count,
                             std::size_t entity_count, std::size_t frame_index,
                             std::size_t entity_index) noexcept {
    if (frame_index >= frame_count || entity_index >= entity_count) {
        return nullptr;
    }
    std::size_t base = 0;
    if (!checked_product(frame_index, entity_count, base) ||
        entity_index > std::numeric_limits<std::size_t>::max() - base) {
        return nullptr;
    }
    const auto index = base + entity_index;
    if (index >= samples.size()) {
        return nullptr;
    }
    return &samples[index];
}

} // namespace

namespace detail {

bool valid_capture_block_after_layout_admission(
    const CaptureBlockView &block) noexcept {
    const auto &layout = block.layout();
    const auto &clock = block.clock();
    if (clock.rate.numerator == 0 || clock.rate.denominator == 0 ||
        std::gcd(clock.rate.numerator, clock.rate.denominator) != 1 ||
        !known(clock.phase) || block.frame_count() == 0 ||
        block.declared_block_capacity_frames() == 0 ||
        block.frame_count() > block.declared_block_capacity_frames() ||
        block.declared_event_journal_capacity_records() == 0 ||
        block.event_journal().events().size() >
            block.declared_event_journal_capacity_records()) {
        return false;
    }

    std::uint64_t timestamp_end = 0;
    if (!checked_add_u64(clock.first_timestamp_tick, block.frame_count() - 1U,
                         timestamp_end)) {
        return false;
    }

    const auto frame_count = static_cast<std::size_t>(block.frame_count());
    const auto shape_matches =
        [frame_count](std::size_t actual, std::size_t entity_count) noexcept {
            std::size_t expected = 0;
            return checked_product(frame_count, entity_count, expected) &&
                   actual == expected;
        };
    if (!shape_matches(block.engine().size(), 1U) ||
        !shape_matches(block.cylinders().size(), layout.cylinders().size()) ||
        !shape_matches(block.ports().size(), layout.ports().size()) ||
        !shape_matches(block.gas_volumes().size(), layout.gas_volumes().size()) ||
        !shape_matches(block.flow_edges().size(), layout.flow_edges().size()) ||
        !shape_matches(block.source_routes().size(), layout.routes().size())) {
        return false;
    }

    for (std::size_t index = 0; index < block.engine().size(); ++index) {
        const auto &sample = block.engine()[index];
        if (!known_mask(sample.validity) ||
            !all_finite({
                sample.theta_rad,
                sample.theta_cycle_rad,
                sample.angular_speed_rad_s,
                sample.angular_acceleration_rad_s2,
                sample.engine_speed_rpm,
                sample.requested_throttle_01,
                sample.resolved_engine_throttle_01,
                sample.intake_plate_position_01,
                sample.main_flow_multiplier_01,
                sample.external_resisting_torque_nm,
            }) ||
            sample.theta_cycle_rad < 0.0 ||
            sample.theta_cycle_rad >= 4.0 * std::numbers::pi ||
            !unit_interval(sample.requested_throttle_01) ||
            !unit_interval(sample.resolved_engine_throttle_01) ||
            !unit_interval(sample.intake_plate_position_01) ||
            !unit_interval(sample.main_flow_multiplier_01) ||
            sample.external_resisting_torque_nm < 0.0 ||
            (index != 0 &&
             sample.step_end_index <= block.engine()[index - 1].step_end_index) ||
            !valid_torque_telemetry(sample.torque)) {
            return false;
        }
        if (has_validity(sample.validity, CaptureValidity::torque)) {
            if (!any_torque_quantity_available(sample.torque)) {
                return false;
            }
        } else if (!all_torque_quantities_unavailable(sample.torque)) {
            return false;
        }
    }

    for (const auto &sample : block.cylinders()) {
        if (!known_mask(sample.validity) ||
            !all_finite({
                sample.chamber_volume_m3,
                sample.chamber_dvolume_dtheta_m3_per_rad,
                sample.piston_velocity_m_s,
                sample.pressure_pa_abs,
                sample.temperature_k,
                sample.amount_mol,
                sample.combustion_heat_release_j,
                sample.flame_radius_m,
                sample.flame_axial_travel_m,
            }) ||
            !valid_composition_for_amount(sample.composition, sample.amount_mol) ||
            !valid_torque_value(sample.indicated_gas_torque)) {
            return false;
        }
        if (has_validity(sample.validity, CaptureValidity::mechanism) &&
            sample.chamber_volume_m3 <= 0.0) {
            return false;
        }
        if (has_validity(sample.validity,
                         CaptureValidity::thermodynamic_state) &&
            (sample.pressure_pa_abs <= 0.0 || sample.temperature_k <= 0.0 ||
             sample.amount_mol < 0.0)) {
            return false;
        }
        if (has_validity(sample.validity, CaptureValidity::torque)) {
            if (sample.indicated_gas_torque.availability !=
                    Availability::available ||
                sample.indicated_gas_torque.included_terms !=
                    torque_term_mask(TorqueTerm::indicated_gas) ||
                sample.indicated_gas_torque.omitted_terms != 0) {
                return false;
            }
        } else if (sample.indicated_gas_torque.availability !=
                   Availability::unavailable) {
            return false;
        }
    }

    for (const auto &sample : block.ports()) {
        if (!known_mask(sample.validity) ||
            !all_finite({
                sample.pressure_pa_abs,
                sample.temperature_k,
                sample.signed_mass_flow_kg_s,
                sample.effective_flow_area_m2,
                sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg,
                sample.valve_lift_m,
            })) {
            return false;
        }
        if (has_validity(sample.validity, CaptureValidity::gas_exchange) &&
            (sample.pressure_pa_abs <= 0.0 || sample.temperature_k <= 0.0 ||
             sample.effective_flow_area_m2 < 0.0 ||
             sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg <
                 0.0 ||
             sample.valve_lift_m < 0.0)) {
            return false;
        }
    }

    for (const auto &sample : block.gas_volumes()) {
        if (!known_mask(sample.validity) ||
            !all_finite({
                sample.volume_m3,
                sample.pressure_pa_abs,
                sample.temperature_k,
                sample.amount_mol,
                sample.thermal_energy_j,
                sample.momentum_x_kg_m_s,
                sample.momentum_y_kg_m_s,
            }) ||
            !valid_composition_for_amount(sample.composition, sample.amount_mol)) {
            return false;
        }
        if (has_validity(sample.validity,
                         CaptureValidity::thermodynamic_state) &&
            (sample.volume_m3 <= 0.0 || sample.pressure_pa_abs <= 0.0 ||
             sample.temperature_k <= 0.0 || sample.amount_mol < 0.0 ||
             sample.thermal_energy_j < 0.0)) {
            return false;
        }
    }

    for (const auto &sample : block.flow_edges()) {
        if (!known_mask(sample.validity) ||
            !finite(sample.signed_mass_flow_kg_s)) {
            return false;
        }
    }

    const auto route_count = layout.routes().size();
    for (std::size_t index = 0; index < block.source_routes().size(); ++index) {
        const auto &sample = block.source_routes()[index];
        if (sample.valueless_by_exception() || route_count == 0) {
            return false;
        }
        const auto route_kind = layout.routes()[index % route_count].kind;
        const auto *gas = std::get_if<GasSourceRouteCaptureSample>(&sample);
        const auto *mechanical =
            std::get_if<MechanicalSourceRouteCaptureSample>(&sample);
        if (!((is_gas_source_route(route_kind) && gas != nullptr) ||
              (is_mechanical_source_route(route_kind) &&
               mechanical != nullptr))) {
            return false;
        }
        if (gas != nullptr &&
            (!known_mask(gas->validity) ||
             !all_finite({
                 gas->pressure_pa_abs,
                 gas->temperature_k,
                 gas->signed_mass_flow_kg_s,
                 gas->effective_area_m2,
             }) ||
             (has_validity(gas->validity, CaptureValidity::gas_exchange) &&
              (gas->pressure_pa_abs <= 0.0 || gas->temperature_k <= 0.0 ||
               gas->effective_area_m2 < 0.0)))) {
            return false;
        }
        if (mechanical != nullptr &&
            (!known_mask(mechanical->validity) ||
             !all_finite(mechanical->force_xyz_n) ||
             !all_finite(mechanical->torque_xyz_nm))) {
            return false;
        }
    }

    std::size_t expected_offset_count = 0;
    const auto &offsets = block.event_journal().offsets();
    const auto &events = block.event_journal().events();
    if (!checked_add(frame_count, 1U, expected_offset_count) ||
        offsets.size() != expected_offset_count || offsets.front() != 0 ||
        offsets.back() != events.size()) {
        return false;
    }

    const auto valid_event = [&](const EngineEvent &event) noexcept {
        if (event.payload.valueless_by_exception()) {
            return false;
        }
        return std::visit(
            [&](const auto &payload) noexcept {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, SparkCrossing>) {
                    return contains_cylinder(layout, payload.cylinder_id) &&
                           all_finite({
                               payload.raw_saved_angle_rad,
                               payload.raw_current_angle_rad,
                               payload.adjusted_current_angle_rad,
                               payload.adjusted_spark_angle_rad,
                               payload.timing_advance_rad,
                           });
                } else if constexpr (
                    std::is_same_v<Payload, LimiterStateChanged>) {
                    return payload.old_active != payload.new_active &&
                           finite_nonnegative(payload.resulting_timer_s);
                } else if constexpr (
                    std::is_same_v<Payload, IgnitionAccepted>) {
                    return contains_cylinder(layout, payload.cylinder_id) &&
                           unit_interval(payload.efficiency_01) &&
                           finite_nonnegative(payload.flame_speed_m_s);
                } else if constexpr (
                    std::is_same_v<Payload, IgnitionRejected>) {
                    return contains_cylinder(layout, payload.cylinder_id) &&
                           known(payload.reason);
                } else {
                    return contains_cylinder(layout, payload.cylinder_id) &&
                           payload.gas_substep_index <= 7U &&
                           known(payload.reason);
                }
            },
            event.payload);
    };

    const bool has_reference_parity = block.reference_parity().has_value();
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto begin = offsets[frame];
        const auto end = offsets[frame + 1U];
        if (begin > end || end > events.size()) {
            return false;
        }

        std::array<std::size_t, 4> category_counts{};
        std::optional<EventCategory> previous_category;
        std::optional<std::size_t> previous_spark_cylinder;
        std::optional<std::size_t> previous_ignition_cylinder;
        std::optional<std::uint8_t> previous_extinction_substep;
        std::optional<std::size_t> previous_extinction_cylinder;
        std::uint8_t previous_ordinal = 0;
        for (std::uint32_t event_index = begin; event_index < end;
             ++event_index) {
            const auto &event = events[event_index];
            if (event.frame_offset != frame ||
                (event_index != begin &&
                 event.ordinal_within_step <= previous_ordinal) ||
                !valid_event(event)) {
                return false;
            }
            previous_ordinal = event.ordinal_within_step;
            if (!has_reference_parity) {
                continue;
            }

            const auto category = event_category(event.payload);
            ++category_counts[static_cast<std::size_t>(category)];
            if (previous_category.has_value() &&
                category < *previous_category) {
                return false;
            }
            previous_category = category;

            const auto cylinder_id = event_cylinder_id(event.payload);
            const auto order =
                cylinder_id.has_value()
                    ? cylinder_order(layout, *cylinder_id)
                    : std::optional<std::size_t>{};
            if (category == EventCategory::spark && order.has_value()) {
                if (previous_spark_cylinder.has_value() &&
                    *order <= *previous_spark_cylinder) {
                    return false;
                }
                previous_spark_cylinder = order;
            } else if (category == EventCategory::ignition &&
                       order.has_value()) {
                if (previous_ignition_cylinder.has_value() &&
                    *order <= *previous_ignition_cylinder) {
                    return false;
                }
                previous_ignition_cylinder = order;
            } else if (category == EventCategory::extinction &&
                       order.has_value()) {
                const auto substep =
                    std::get<FlameExtinguished>(event.payload)
                        .gas_substep_index;
                if (previous_extinction_substep.has_value() &&
                    previous_extinction_cylinder.has_value() &&
                    !(substep > *previous_extinction_substep ||
                      (substep == *previous_extinction_substep &&
                       *order > *previous_extinction_cylinder))) {
                    return false;
                }
                previous_extinction_substep = substep;
                previous_extinction_cylinder = order;
            }
        }

        if (has_reference_parity) {
            const auto cylinder_count = layout.cylinders().size();
            std::size_t cylinder_event_capacity = 0;
            std::size_t composed_event_capacity = 0;
            if (category_counts[static_cast<std::size_t>(
                    EventCategory::spark)] > cylinder_count ||
                category_counts[static_cast<std::size_t>(
                    EventCategory::limiter)] > 1U ||
                category_counts[static_cast<std::size_t>(
                    EventCategory::ignition)] > cylinder_count ||
                category_counts[static_cast<std::size_t>(
                    EventCategory::extinction)] > cylinder_count ||
                !checked_product(cylinder_count, 3U,
                                 cylinder_event_capacity) ||
                !checked_add(cylinder_event_capacity, 1U,
                             composed_event_capacity) ||
                end - begin > composed_event_capacity) {
                return false;
            }
        }
    }

    if (!has_reference_parity) {
        return true;
    }

    const auto &parity = *block.reference_parity();
    std::uint64_t expected_first_timestamp = 0;
    std::uint64_t expected_last_step = 0;
    if (clock.rate != RationalRateHz{10000U, 1U} ||
        clock.phase != SamplePhase::post_step ||
        !checked_add_u64(clock.first_sample_index, 1U,
                         expected_first_timestamp) ||
        clock.first_timestamp_tick != expected_first_timestamp ||
        !checked_add_u64(clock.first_sample_index, block.frame_count(),
                         expected_last_step) ||
        !shape_matches(parity.filtered_engine_speed_rpm().size(), 1U) ||
        !shape_matches(parity.cylinders().size(),
                       layout.cylinders().size())) {
        return false;
    }
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        std::uint64_t expected_step = 0;
        if (!checked_add_u64(clock.first_sample_index, frame + 1U,
                             expected_step) ||
            block.engine()[frame].step_end_index != expected_step ||
            !finite(parity.filtered_engine_speed_rpm()[frame])) {
            return false;
        }
    }
    for (const auto &sample : parity.cylinders()) {
        if (!all_finite({
                sample.exhaust_primary_static_pressure_pa_abs,
                sample.dynamic_pressure_forward_pa,
                sample.dynamic_pressure_reverse_pa,
            }) ||
            sample.exhaust_primary_static_pressure_pa_abs <= 0.0 ||
            sample.dynamic_pressure_forward_pa < 0.0 ||
            sample.dynamic_pressure_reverse_pa < 0.0) {
            return false;
        }
    }
    return true;
}

} // namespace detail

CaptureLayoutView::CaptureLayoutView(EngineId engine_id,
                                     std::span<const CylinderId> cylinders,
                                     std::span<const PortIdentity> ports,
                                     std::span<const GasVolumeIdentity> gas_volumes,
                                     std::span<const FlowEdgeIdentity> flow_edges,
                                     std::span<const RouteIdentity> routes) noexcept
    : engine_id_(engine_id), cylinders_(cylinders), ports_(ports),
      gas_volumes_(gas_volumes), flow_edges_(flow_edges), routes_(routes) {}

EngineId CaptureLayoutView::engine_id() const noexcept {
    return engine_id_;
}

std::span<const CylinderId> CaptureLayoutView::cylinders() const noexcept {
    return cylinders_;
}

std::span<const PortIdentity> CaptureLayoutView::ports() const noexcept {
    return ports_;
}

std::span<const GasVolumeIdentity> CaptureLayoutView::gas_volumes() const noexcept {
    return gas_volumes_;
}

std::span<const FlowEdgeIdentity> CaptureLayoutView::flow_edges() const noexcept {
    return flow_edges_;
}

std::span<const RouteIdentity> CaptureLayoutView::routes() const noexcept {
    return routes_;
}

double CaptureClock::timestamp_s(std::uint32_t frame_offset) const noexcept {
    const auto tick = static_cast<long double>(first_timestamp_tick) +
                      static_cast<long double>(frame_offset);
    return static_cast<double>(tick * static_cast<long double>(rate.denominator) /
                               static_cast<long double>(rate.numerator));
}

ReferenceParityBlockView::ReferenceParityBlockView(
    std::span<const double> filtered_engine_speed_rpm,
    std::span<const ReferenceParityCylinderSample> cylinders) noexcept
    : filtered_engine_speed_rpm_(filtered_engine_speed_rpm), cylinders_(cylinders) {}

std::span<const double>
ReferenceParityBlockView::filtered_engine_speed_rpm() const noexcept {
    return filtered_engine_speed_rpm_;
}

std::span<const ReferenceParityCylinderSample>
ReferenceParityBlockView::cylinders() const noexcept {
    return cylinders_;
}

EventJournalView::EventJournalView(std::span<const std::uint32_t> offsets,
                                   std::span<const EngineEvent> events) noexcept
    : offsets_(offsets), events_(events) {}

std::span<const std::uint32_t> EventJournalView::offsets() const noexcept {
    return offsets_;
}

std::span<const EngineEvent> EventJournalView::events() const noexcept {
    return events_;
}

CaptureBlockView::CaptureBlockView(
    CaptureLayoutView layout, CaptureClock clock, std::uint32_t frame_count,
    std::uint32_t declared_block_capacity_frames,
    std::uint32_t declared_event_journal_capacity_records,
    std::span<const EngineCaptureSample> engine,
    std::span<const CylinderCaptureSample> cylinders,
    std::span<const PortCaptureSample> ports,
    std::span<const GasVolumeCaptureSample> gas_volumes,
    std::span<const FlowEdgeCaptureSample> flow_edges,
    std::span<const SourceRouteCaptureSample> source_routes,
    EventJournalView event_journal,
    std::optional<ReferenceParityBlockView> reference_parity) noexcept
    : layout_(layout), clock_(clock), frame_count_(frame_count),
      declared_block_capacity_frames_(declared_block_capacity_frames),
      declared_event_journal_capacity_records_(declared_event_journal_capacity_records),
      engine_(engine), cylinders_(cylinders), ports_(ports), gas_volumes_(gas_volumes),
      flow_edges_(flow_edges), source_routes_(source_routes),
      event_journal_(event_journal), reference_parity_(reference_parity) {}

const CaptureLayoutView &CaptureBlockView::layout() const noexcept {
    return layout_;
}

const CaptureClock &CaptureBlockView::clock() const noexcept {
    return clock_;
}

std::uint32_t CaptureBlockView::frame_count() const noexcept {
    return frame_count_;
}

std::uint32_t CaptureBlockView::declared_block_capacity_frames() const noexcept {
    return declared_block_capacity_frames_;
}

std::uint32_t
CaptureBlockView::declared_event_journal_capacity_records() const noexcept {
    return declared_event_journal_capacity_records_;
}

std::span<const EngineCaptureSample> CaptureBlockView::engine() const noexcept {
    return engine_;
}

std::span<const CylinderCaptureSample> CaptureBlockView::cylinders() const noexcept {
    return cylinders_;
}

std::span<const PortCaptureSample> CaptureBlockView::ports() const noexcept {
    return ports_;
}

std::span<const GasVolumeCaptureSample> CaptureBlockView::gas_volumes() const noexcept {
    return gas_volumes_;
}

std::span<const FlowEdgeCaptureSample> CaptureBlockView::flow_edges() const noexcept {
    return flow_edges_;
}

std::span<const SourceRouteCaptureSample>
CaptureBlockView::source_routes() const noexcept {
    return source_routes_;
}

const EventJournalView &CaptureBlockView::event_journal() const noexcept {
    return event_journal_;
}

const std::optional<ReferenceParityBlockView> &
CaptureBlockView::reference_parity() const noexcept {
    return reference_parity_;
}

const EngineCaptureSample *
CaptureBlockView::engine_sample(std::size_t frame_index) const noexcept {
    return frame_major_at(engine_, frame_count_, 1, frame_index, 0);
}

const CylinderCaptureSample *
CaptureBlockView::cylinder_sample(std::size_t frame_index,
                                  std::size_t cylinder_index) const noexcept {
    return frame_major_at(cylinders_, frame_count_, layout_.cylinders().size(),
                          frame_index, cylinder_index);
}

const PortCaptureSample *
CaptureBlockView::port_sample(std::size_t frame_index,
                              std::size_t port_index) const noexcept {
    return frame_major_at(ports_, frame_count_, layout_.ports().size(), frame_index,
                          port_index);
}

const GasVolumeCaptureSample *
CaptureBlockView::gas_volume_sample(std::size_t frame_index,
                                    std::size_t volume_index) const noexcept {
    return frame_major_at(gas_volumes_, frame_count_, layout_.gas_volumes().size(),
                          frame_index, volume_index);
}

const FlowEdgeCaptureSample *
CaptureBlockView::flow_edge_sample(std::size_t frame_index,
                                   std::size_t edge_index) const noexcept {
    return frame_major_at(flow_edges_, frame_count_, layout_.flow_edges().size(),
                          frame_index, edge_index);
}

const SourceRouteCaptureSample *
CaptureBlockView::source_route_sample(std::size_t frame_index,
                                      std::size_t route_index) const noexcept {
    return frame_major_at(source_routes_, frame_count_, layout_.routes().size(),
                          frame_index, route_index);
}

const GasSourceRouteCaptureSample *
CaptureBlockView::gas_source_route_sample(std::size_t frame_index,
                                          std::size_t route_index) const noexcept {
    const auto *sample = source_route_sample(frame_index, route_index);
    return sample == nullptr ? nullptr
                             : std::get_if<GasSourceRouteCaptureSample>(sample);
}

const MechanicalSourceRouteCaptureSample *
CaptureBlockView::mechanical_source_route_sample(
    std::size_t frame_index, std::size_t route_index) const noexcept {
    const auto *sample = source_route_sample(frame_index, route_index);
    return sample == nullptr ? nullptr
                             : std::get_if<MechanicalSourceRouteCaptureSample>(sample);
}

ValidationReport validate(const CaptureLayoutView &layout) {
    using detail::require;

    ValidationReport report;
    require(report, layout.engine_id().valid(), ContractIssueCode::invalid_value,
            "engine_id", "capture engine ID must be nonzero");
    require(report, detail::all_unique_valid_ids(layout.cylinders()),
            ContractIssueCode::duplicate_identity, "cylinders",
            "cylinder IDs must be nonzero and unique");

    std::unordered_set<std::uint32_t> port_ids;
    for (std::size_t index = 0; index < layout.ports().size(); ++index) {
        const auto &port = layout.ports()[index];
        const auto path = "ports[" + std::to_string(index) + "]";
        require(report, port.id.valid(), ContractIssueCode::invalid_value, path + ".id",
                "port ID must be nonzero");
        if (!port_ids.insert(port.id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "port IDs must be unique");
        }
        require(report, contains_cylinder(layout, port.cylinder_id),
                ContractIssueCode::dangling_reference, path + ".cylinder_id",
                "port references an unknown cylinder");
        require(report, known(port.kind), ContractIssueCode::unsupported_value,
                path + ".kind", "port kind is not recognized");
    }

    std::unordered_set<std::uint32_t> volume_ids;
    for (std::size_t index = 0; index < layout.gas_volumes().size(); ++index) {
        const auto &volume = layout.gas_volumes()[index];
        const auto path = "gas_volumes[" + std::to_string(index) + "]";
        require(report, volume.id.valid(), ContractIssueCode::invalid_value,
                path + ".id", "gas-volume ID must be nonzero");
        if (!volume_ids.insert(volume.id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "gas-volume IDs must be unique");
        }
        require(report, known(volume.kind), ContractIssueCode::unsupported_value,
                path + ".kind", "gas-volume kind is not recognized");
    }

    std::unordered_set<std::uint32_t> edge_ids;
    for (std::size_t index = 0; index < layout.flow_edges().size(); ++index) {
        const auto &edge = layout.flow_edges()[index];
        const auto path = "flow_edges[" + std::to_string(index) + "]";
        require(report, edge.id.valid(), ContractIssueCode::invalid_value, path + ".id",
                "flow-edge ID must be nonzero");
        if (!edge_ids.insert(edge.id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "flow-edge IDs must be unique");
        }
        require(report,
                volume_ids.contains(edge.endpoint_0_volume_id.value) &&
                    volume_ids.contains(edge.endpoint_1_volume_id.value),
                ContractIssueCode::dangling_reference, path,
                "flow edge references an unknown gas volume");
        require(report, edge.endpoint_0_volume_id != edge.endpoint_1_volume_id,
                ContractIssueCode::inconsistent_semantics, path,
                "flow edge must connect two different gas volumes");
    }

    std::unordered_set<std::uint32_t> route_ids;
    std::unordered_map<std::uint32_t, std::optional<std::uint32_t>> route_parents;
    for (std::size_t index = 0; index < layout.routes().size(); ++index) {
        const auto &route = layout.routes()[index];
        const auto path = "routes[" + std::to_string(index) + "]";
        require(report, route.id.valid(), ContractIssueCode::invalid_value,
                path + ".id", "source-route ID must be nonzero");
        if (!route_ids.insert(route.id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "source-route IDs must be unique");
        }
        require(report, known_physical_source_route(route.kind),
                ContractIssueCode::unsupported_value, path + ".kind",
                "capture layout accepts physical source routes only");
        if (route.source_volume_id.has_value()) {
            require(report, volume_ids.contains(route.source_volume_id->value),
                    ContractIssueCode::dangling_reference, path + ".source_volume_id",
                    "source route references an unknown gas volume");
        }
        if (is_gas_source_route(route.kind)) {
            require(report, route.source_volume_id.has_value(),
                    ContractIssueCode::missing_value, path + ".source_volume_id",
                    "gas source route requires its physical source volume");
        }
        if (is_mechanical_source_route(route.kind)) {
            require(report, !route.source_volume_id.has_value(),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".source_volume_id",
                    "mechanical source route cannot bind a gas volume");
            require(report, route.emitter_anchor_id.has_value(),
                    ContractIssueCode::missing_value, path + ".emitter_anchor_id",
                    "mechanical source route requires its emitter anchor");
        }
        if (route.emitter_anchor_id.has_value()) {
            require(report, is_valid_semantic_id(*route.emitter_anchor_id),
                    ContractIssueCode::invalid_value, path + ".emitter_anchor_id",
                    "source-route emitter anchor must be a canonical semantic ID");
        }
        route_parents.emplace(
            route.id.value,
            route.default_parent_route_id.has_value()
                ? std::optional<std::uint32_t>{route.default_parent_route_id->value}
                : std::nullopt);
    }

    for (std::size_t index = 0; index < layout.routes().size(); ++index) {
        const auto &route = layout.routes()[index];
        if (!route.default_parent_route_id.has_value()) {
            continue;
        }
        const auto path =
            "routes[" + std::to_string(index) + "].default_parent_route_id";
        require(report, route_ids.contains(route.default_parent_route_id->value),
                ContractIssueCode::dangling_reference, path,
                "source-route parent references an unknown route");
        require(report, *route.default_parent_route_id != route.id,
                ContractIssueCode::inconsistent_semantics, path,
                "source route cannot parent itself");
    }

    bool cycle_reported = false;
    for (const auto &route : layout.routes()) {
        std::unordered_set<std::uint32_t> chain;
        auto current = route.id.value;
        while (route_parents.contains(current)) {
            if (!chain.insert(current).second) {
                if (!cycle_reported) {
                    report.add(ContractIssueCode::inconsistent_semantics, "routes",
                               "source-route parent graph must be acyclic");
                    cycle_reported = true;
                }
                break;
            }
            const auto parent = route_parents.at(current);
            if (!parent.has_value() || !route_parents.contains(*parent)) {
                break;
            }
            current = *parent;
        }
    }

    return report;
}

ValidationReport validate(const CaptureBlockView &block) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate(block.layout()), "layout");
    append_prefixed(report, validate(block.clock().rate), "clock.rate");
    require(report, known(block.clock().phase), ContractIssueCode::unsupported_value,
            "clock.phase", "capture sample phase is not recognized");
    require(report,
            block.frame_count() > 0 && block.declared_block_capacity_frames() > 0 &&
                block.frame_count() <= block.declared_block_capacity_frames(),
            ContractIssueCode::invalid_value, "frame_count",
            "frame count must be within the declared positive block capacity");
    require(report, block.declared_event_journal_capacity_records() > 0,
            ContractIssueCode::invalid_value, "declared_event_journal_capacity_records",
            "event-journal record capacity must be positive");
    require(report,
            block.event_journal().events().size() <=
                block.declared_event_journal_capacity_records(),
            ContractIssueCode::inconsistent_shape, "event_journal.events",
            "event count must not exceed the declared event-journal capacity");

    std::uint64_t timestamp_end = 0;
    require(report,
            checked_add_u64(block.clock().first_timestamp_tick,
                            block.frame_count() - (block.frame_count() > 0 ? 1 : 0),
                            timestamp_end),
            ContractIssueCode::invalid_value, "clock.first_timestamp_tick",
            "capture timestamp ticks overflow the integer clock");

    const auto frame_count = static_cast<std::size_t>(block.frame_count());
    const auto require_shape = [&](std::size_t actual, std::size_t entity_count,
                                   const std::string &path) {
        std::size_t expected = 0;
        if (!checked_product(frame_count, entity_count, expected)) {
            report.add(ContractIssueCode::invalid_value, path,
                       "capture shape overflows size_t");
            return;
        }
        require(report, actual == expected, ContractIssueCode::inconsistent_shape, path,
                "capture span length must equal frame_count times layout count");
    };
    require_shape(block.engine().size(), 1, "engine");
    require_shape(block.cylinders().size(), block.layout().cylinders().size(),
                  "cylinders");
    require_shape(block.ports().size(), block.layout().ports().size(), "ports");
    require_shape(block.gas_volumes().size(), block.layout().gas_volumes().size(),
                  "gas_volumes");
    require_shape(block.flow_edges().size(), block.layout().flow_edges().size(),
                  "flow_edges");
    require_shape(block.source_routes().size(), block.layout().routes().size(),
                  "source_routes");

    for (std::size_t index = 0; index < block.engine().size(); ++index) {
        const auto &sample = block.engine()[index];
        const auto path = "engine[" + std::to_string(index) + "]";
        require(report, known_mask(sample.validity), ContractIssueCode::invalid_value,
                path + ".validity", "capture validity mask contains unknown flags");
        require(report,
                all_finite({
                    sample.theta_rad,
                    sample.theta_cycle_rad,
                    sample.angular_speed_rad_s,
                    sample.angular_acceleration_rad_s2,
                    sample.engine_speed_rpm,
                    sample.requested_throttle_01,
                    sample.resolved_engine_throttle_01,
                    sample.intake_plate_position_01,
                    sample.main_flow_multiplier_01,
                    sample.external_resisting_torque_nm,
                }),
                ContractIssueCode::invalid_value, path,
                "engine capture values must be finite");
        require(report,
                sample.theta_cycle_rad >= 0.0 &&
                    sample.theta_cycle_rad < 4.0 * std::numbers::pi,
                ContractIssueCode::invalid_value, path + ".theta_cycle_rad",
                "wrapped four-stroke angle must be in [0, 4*pi)");
        require(report,
                detail::unit_interval(sample.requested_throttle_01) &&
                    detail::unit_interval(sample.resolved_engine_throttle_01) &&
                    detail::unit_interval(sample.intake_plate_position_01) &&
                    detail::unit_interval(sample.main_flow_multiplier_01),
                ContractIssueCode::invalid_value, path,
                "all throttle/linkage coordinates must be in [0, 1]");
        require(report, sample.external_resisting_torque_nm >= 0.0,
                ContractIssueCode::invalid_value,
                path + ".external_resisting_torque_nm",
                "external resisting torque must be nonnegative");
        if (index != 0) {
            require(report,
                    sample.step_end_index > block.engine()[index - 1].step_end_index,
                    ContractIssueCode::inconsistent_semantics, path + ".step_end_index",
                    "step-end indices must be strictly increasing");
        }
        append_prefixed(report, validate(sample.torque), path + ".torque");
        if (has_validity(sample.validity, CaptureValidity::torque)) {
            require(report, any_torque_quantity_available(sample.torque),
                    ContractIssueCode::inconsistent_semantics, path + ".torque",
                    "engine torque validity requires at least one available "
                    "torque or derived quantity");
        } else {
            require(report, all_torque_quantities_unavailable(sample.torque),
                    ContractIssueCode::inconsistent_semantics, path + ".torque",
                    "engine telemetry without torque validity must keep every "
                    "torque and derived quantity unavailable");
        }
    }

    for (std::size_t index = 0; index < block.cylinders().size(); ++index) {
        const auto &sample = block.cylinders()[index];
        const auto path = "cylinders[" + std::to_string(index) + "]";
        require(report,
                known_mask(sample.validity) &&
                    all_finite({
                        sample.chamber_volume_m3,
                        sample.chamber_dvolume_dtheta_m3_per_rad,
                        sample.piston_velocity_m_s,
                        sample.pressure_pa_abs,
                        sample.temperature_k,
                        sample.amount_mol,
                        sample.combustion_heat_release_j,
                        sample.flame_radius_m,
                        sample.flame_axial_travel_m,
                    }),
                ContractIssueCode::invalid_value, path,
                "cylinder capture contains a nonfinite or invalid value");
        require(report,
                valid_composition_for_amount(sample.composition, sample.amount_mol),
                ContractIssueCode::invalid_value, path + ".composition",
                "zero amount requires zero fractions; positive amount requires "
                "nonnegative fractions summing to one within 1e-12");
        if (has_validity(sample.validity, CaptureValidity::mechanism)) {
            require(report, sample.chamber_volume_m3 > 0.0,
                    ContractIssueCode::invalid_value, path + ".chamber_volume_m3",
                    "valid chamber volume must be positive");
        }
        if (has_validity(sample.validity, CaptureValidity::thermodynamic_state)) {
            require(report,
                    sample.pressure_pa_abs > 0.0 && sample.temperature_k > 0.0 &&
                        sample.amount_mol >= 0.0,
                    ContractIssueCode::invalid_value, path,
                    "valid thermodynamic state is outside its physical domain");
        }
        append_prefixed(report, validate(sample.indicated_gas_torque),
                        path + ".indicated_gas_torque");
        if (has_validity(sample.validity, CaptureValidity::torque)) {
            require(report,
                    sample.indicated_gas_torque.availability ==
                            Availability::available &&
                        sample.indicated_gas_torque.included_terms ==
                            torque_term_mask(TorqueTerm::indicated_gas) &&
                        sample.indicated_gas_torque.omitted_terms == 0,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".indicated_gas_torque",
                    "valid per-cylinder indicated torque must be available and "
                    "classify only the indicated-gas term");
        } else {
            require(report,
                    sample.indicated_gas_torque.availability ==
                        Availability::unavailable,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".indicated_gas_torque.availability",
                    "per-cylinder indicated torque without torque validity must "
                    "remain unavailable");
        }
    }

    for (std::size_t index = 0; index < block.ports().size(); ++index) {
        const auto &sample = block.ports()[index];
        const auto path = "ports[" + std::to_string(index) + "]";
        require(report,
                known_mask(sample.validity) &&
                    all_finite({
                        sample.pressure_pa_abs,
                        sample.temperature_k,
                        sample.signed_mass_flow_kg_s,
                        sample.effective_flow_area_m2,
                        sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg,
                        sample.valve_lift_m,
                    }),
                ContractIssueCode::invalid_value, path,
                "port capture values must be finite");
        if (has_validity(sample.validity, CaptureValidity::gas_exchange)) {
            require(report,
                    sample.pressure_pa_abs > 0.0 && sample.temperature_k > 0.0 &&
                        sample.effective_flow_area_m2 >= 0.0 &&
                        sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg >=
                            0.0 &&
                        sample.valve_lift_m >= 0.0,
                    ContractIssueCode::invalid_value, path,
                    "valid port state is outside its physical domain");
        }
    }

    for (std::size_t index = 0; index < block.gas_volumes().size(); ++index) {
        const auto &sample = block.gas_volumes()[index];
        const auto path = "gas_volumes[" + std::to_string(index) + "]";
        require(report,
                known_mask(sample.validity) && all_finite({
                                                   sample.volume_m3,
                                                   sample.pressure_pa_abs,
                                                   sample.temperature_k,
                                                   sample.amount_mol,
                                                   sample.thermal_energy_j,
                                                   sample.momentum_x_kg_m_s,
                                                   sample.momentum_y_kg_m_s,
                                               }),
                ContractIssueCode::invalid_value, path,
                "gas-volume capture contains a nonfinite or invalid value");
        require(report,
                valid_composition_for_amount(sample.composition, sample.amount_mol),
                ContractIssueCode::invalid_value, path + ".composition",
                "zero amount requires zero fractions; positive amount requires "
                "nonnegative fractions summing to one within 1e-12");
        if (has_validity(sample.validity, CaptureValidity::thermodynamic_state)) {
            require(report,
                    sample.volume_m3 > 0.0 && sample.pressure_pa_abs > 0.0 &&
                        sample.temperature_k > 0.0 && sample.amount_mol >= 0.0 &&
                        sample.thermal_energy_j >= 0.0,
                    ContractIssueCode::invalid_value, path,
                    "valid gas-volume state is outside its physical domain");
        }
    }

    for (std::size_t index = 0; index < block.flow_edges().size(); ++index) {
        const auto &sample = block.flow_edges()[index];
        require(report,
                known_mask(sample.validity) && finite(sample.signed_mass_flow_kg_s),
                ContractIssueCode::invalid_value,
                "flow_edges[" + std::to_string(index) + "]",
                "signed edge flow must be finite");
    }

    for (std::size_t index = 0; index < block.source_routes().size(); ++index) {
        const auto &sample = block.source_routes()[index];
        const auto path = "source_routes[" + std::to_string(index) + "]";
        if (sample.valueless_by_exception()) {
            report.add(ContractIssueCode::invalid_value, path,
                       "source-route sample cannot be valueless");
            continue;
        }

        const auto route_count = block.layout().routes().size();
        if (route_count == 0) {
            report.add(ContractIssueCode::inconsistent_shape, path,
                       "source-route samples require source-route identities");
            continue;
        }
        const auto route_index = index % route_count;
        const auto route_kind = block.layout().routes()[route_index].kind;
        const auto gas_sample = std::get_if<GasSourceRouteCaptureSample>(&sample);
        const auto mechanical_sample =
            std::get_if<MechanicalSourceRouteCaptureSample>(&sample);
        require(report,
                (is_gas_source_route(route_kind) && gas_sample != nullptr) ||
                    (is_mechanical_source_route(route_kind) &&
                     mechanical_sample != nullptr),
                ContractIssueCode::inconsistent_semantics, path,
                "source-route sample type must match its physical route kind");

        if (gas_sample != nullptr) {
            require(report,
                    known_mask(gas_sample->validity) &&
                        all_finite({
                            gas_sample->pressure_pa_abs,
                            gas_sample->temperature_k,
                            gas_sample->signed_mass_flow_kg_s,
                            gas_sample->effective_area_m2,
                        }),
                    ContractIssueCode::invalid_value, path,
                    "gas source-route values must be finite");
            if (has_validity(gas_sample->validity, CaptureValidity::gas_exchange)) {
                require(report,
                        gas_sample->pressure_pa_abs > 0.0 &&
                            gas_sample->temperature_k > 0.0 &&
                            gas_sample->effective_area_m2 >= 0.0,
                        ContractIssueCode::invalid_value, path,
                        "valid gas source route is outside its physical domain");
            }
        }
        if (mechanical_sample != nullptr) {
            require(report,
                    known_mask(mechanical_sample->validity) &&
                        all_finite(mechanical_sample->force_xyz_n) &&
                        all_finite(mechanical_sample->torque_xyz_nm),
                    ContractIssueCode::invalid_value, path,
                    "mechanical source-route wrench must be finite");
        }
    }

    std::size_t expected_offsets = 0;
    const auto offsets_shape_representable =
        checked_add(frame_count, 1, expected_offsets);
    require(report, offsets_shape_representable, ContractIssueCode::invalid_value,
            "event_journal.offsets", "frame_count plus one overflows size_t");
    if (offsets_shape_representable) {
        require(report, block.event_journal().offsets().size() == expected_offsets,
                ContractIssueCode::inconsistent_shape, "event_journal.offsets",
                "event offsets must contain frame_count plus one entries");
    }

    if (offsets_shape_representable &&
        block.event_journal().offsets().size() == expected_offsets) {
        require(report,
                block.event_journal().offsets().front() == 0 &&
                    block.event_journal().offsets().back() ==
                        block.event_journal().events().size(),
                ContractIssueCode::inconsistent_shape, "event_journal.offsets",
                "event offsets must cover the complete event span");
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            const auto begin = block.event_journal().offsets()[frame];
            const auto end = block.event_journal().offsets()[frame + 1];
            require(report,
                    begin <= end && end <= block.event_journal().events().size(),
                    ContractIssueCode::inconsistent_shape,
                    "event_journal.offsets[" + std::to_string(frame) + "]",
                    "event offsets must be monotonic and in range");
            if (begin > end || end > block.event_journal().events().size()) {
                continue;
            }

            std::array<std::size_t, 4> category_counts{};
            std::optional<EventCategory> previous_category;
            std::optional<std::size_t> previous_spark_cylinder;
            std::optional<std::size_t> previous_ignition_cylinder;
            std::optional<std::uint8_t> previous_extinction_substep;
            std::optional<std::size_t> previous_extinction_cylinder;
            std::uint8_t previous_ordinal = 0;
            for (std::uint32_t event_index = begin; event_index < end; ++event_index) {
                const auto &event = block.event_journal().events()[event_index];
                const auto path =
                    "event_journal.events[" + std::to_string(event_index) + "]";
                require(report, event.frame_offset == frame,
                        ContractIssueCode::inconsistent_semantics,
                        path + ".frame_offset",
                        "event must lie in the frame selected by its offsets");
                if (event_index != begin) {
                    require(report, event.ordinal_within_step > previous_ordinal,
                            ContractIssueCode::inconsistent_semantics,
                            path + ".ordinal_within_step",
                            "event ordinals must be strictly increasing within "
                            "a frame");
                }
                previous_ordinal = event.ordinal_within_step;
                validate_event(report, event, block.layout(), path);

                if (!block.reference_parity().has_value() ||
                    event.payload.valueless_by_exception()) {
                    continue;
                }
                const auto category = event_category(event.payload);
                ++category_counts[static_cast<std::size_t>(category)];
                if (previous_category.has_value()) {
                    require(report, category >= *previous_category,
                            ContractIssueCode::inconsistent_semantics,
                            path + ".payload",
                            "low-order reference-parity events must be ordered spark, "
                            "limiter, "
                            "ignition, extinction");
                }
                previous_category = category;

                const auto cylinder_id = event_cylinder_id(event.payload);
                const auto order = cylinder_id.has_value()
                                       ? cylinder_order(block.layout(), *cylinder_id)
                                       : std::nullopt;
                if (category == EventCategory::spark && order.has_value()) {
                    if (previous_spark_cylinder.has_value()) {
                        require(report, *order > *previous_spark_cylinder,
                                ContractIssueCode::inconsistent_semantics,
                                path + ".cylinder_id",
                                "low-order reference-parity spark crossings must "
                                "follow cylinder order");
                    }
                    previous_spark_cylinder = order;
                } else if (category == EventCategory::ignition && order.has_value()) {
                    if (previous_ignition_cylinder.has_value()) {
                        require(report, *order > *previous_ignition_cylinder,
                                ContractIssueCode::inconsistent_semantics,
                                path + ".cylinder_id",
                                "low-order reference-parity ignition results must "
                                "follow cylinder order");
                    }
                    previous_ignition_cylinder = order;
                } else if (category == EventCategory::extinction && order.has_value()) {
                    const auto substep =
                        std::get<FlameExtinguished>(event.payload).gas_substep_index;
                    if (previous_extinction_substep.has_value() &&
                        previous_extinction_cylinder.has_value()) {
                        require(report,
                                substep > *previous_extinction_substep ||
                                    (substep == *previous_extinction_substep &&
                                     *order > *previous_extinction_cylinder),
                                ContractIssueCode::inconsistent_semantics,
                                path + ".payload",
                                "low-order reference-parity flame extinctions must "
                                "follow gas-substep "
                                "then cylinder order");
                    }
                    previous_extinction_substep = substep;
                    previous_extinction_cylinder = order;
                }
            }

            if (block.reference_parity().has_value()) {
                const auto cylinder_count = block.layout().cylinders().size();
                require(
                    report,
                    category_counts[static_cast<std::size_t>(EventCategory::spark)] <=
                        cylinder_count,
                    ContractIssueCode::inconsistent_shape, "event_journal",
                    "low-order reference-parity permits at most one spark crossing per "
                    "cylinder per frame");
                require(
                    report,
                    category_counts[static_cast<std::size_t>(EventCategory::limiter)] <=
                        1,
                    ContractIssueCode::inconsistent_shape, "event_journal",
                    "low-order reference-parity permits at most one limiter transition "
                    "per frame");
                require(report,
                        category_counts[static_cast<std::size_t>(
                            EventCategory::ignition)] <= cylinder_count,
                        ContractIssueCode::inconsistent_shape, "event_journal",
                        "low-order reference-parity permits at most one ignition "
                        "result per cylinder per "
                        "frame");
                require(report,
                        category_counts[static_cast<std::size_t>(
                            EventCategory::extinction)] <= cylinder_count,
                        ContractIssueCode::inconsistent_shape, "event_journal",
                        "low-order reference-parity permits at most one flame "
                        "extinction per cylinder per "
                        "frame");
                std::size_t cylinder_event_capacity = 0;
                std::size_t composed_event_capacity = 0;
                const bool capacity_representable =
                    checked_product(cylinder_count, 3U, cylinder_event_capacity) &&
                    checked_add(cylinder_event_capacity, 1U, composed_event_capacity);
                require(report,
                        capacity_representable &&
                            end - begin <= composed_event_capacity,
                        ContractIssueCode::inconsistent_shape, "event_journal",
                        "low-order reference-parity frame exceeds three "
                        "cylinder-scoped event classes plus "
                        "one engine-wide limiter transition");
            }
        }
    }

    if (block.reference_parity().has_value()) {
        const auto &parity = *block.reference_parity();
        require(report,
                block.clock().rate == RationalRateHz{10000, 1} &&
                    block.clock().phase == SamplePhase::post_step,
                ContractIssueCode::inconsistent_semantics, "clock",
                "low-order reference parity requires exact 10000/1 Hz post-step "
                "capture");

        std::uint64_t expected_first_timestamp = 0;
        const auto timestamp_representable = checked_add_u64(
            block.clock().first_sample_index, 1, expected_first_timestamp);
        require(report,
                timestamp_representable &&
                    block.clock().first_timestamp_tick == expected_first_timestamp,
                ContractIssueCode::inconsistent_semantics, "clock.first_timestamp_tick",
                "low-order reference-parity post-step timestamp tick must equal first "
                "sample index "
                "plus one");

        std::uint64_t expected_last_step = 0;
        const auto step_range_representable = checked_add_u64(
            block.clock().first_sample_index, block.frame_count(), expected_last_step);
        require(report, step_range_representable, ContractIssueCode::invalid_value,
                "clock.first_sample_index",
                "low-order reference-parity sample-to-step range overflows uint64");
        if (step_range_representable) {
            const auto comparable_frames = std::min(frame_count, block.engine().size());
            for (std::size_t frame = 0; frame < comparable_frames; ++frame) {
                std::uint64_t expected_step = 0;
                const auto frame_step_representable = checked_add_u64(
                    block.clock().first_sample_index,
                    static_cast<std::uint64_t>(frame) + 1, expected_step);
                require(report,
                        frame_step_representable &&
                            block.engine()[frame].step_end_index == expected_step,
                        ContractIssueCode::inconsistent_semantics,
                        "engine[" + std::to_string(frame) + "].step_end_index",
                        "low-order reference-parity step-end index must equal sample "
                        "index plus "
                        "one");
            }
        }

        require_shape(parity.filtered_engine_speed_rpm().size(), 1,
                      "reference_parity.filtered_engine_speed_rpm");
        require_shape(parity.cylinders().size(), block.layout().cylinders().size(),
                      "reference_parity.cylinders");
        for (std::size_t index = 0; index < parity.filtered_engine_speed_rpm().size();
             ++index) {
            require(report, finite(parity.filtered_engine_speed_rpm()[index]),
                    ContractIssueCode::invalid_value,
                    "reference_parity.filtered_engine_speed_rpm[" +
                        std::to_string(index) + "]",
                    "reference-parity filtered speed must be finite");
        }
        for (std::size_t index = 0; index < parity.cylinders().size(); ++index) {
            const auto &sample = parity.cylinders()[index];
            require(report,
                    all_finite({
                        sample.exhaust_primary_static_pressure_pa_abs,
                        sample.dynamic_pressure_forward_pa,
                        sample.dynamic_pressure_reverse_pa,
                    }) &&
                        sample.exhaust_primary_static_pressure_pa_abs > 0.0 &&
                        sample.dynamic_pressure_forward_pa >= 0.0 &&
                        sample.dynamic_pressure_reverse_pa >= 0.0,
                    ContractIssueCode::invalid_value,
                    "reference_parity.cylinders[" + std::to_string(index) + "]",
                    "low-order reference-parity primary static pressure must be "
                    "positive and "
                    "directional dynamic pressures nonnegative");
        }
    }

    return report;
}

ValidationReport validate(const CaptureBlockView &block, const EngineSpec &engine,
                          const RenderScenario &scenario) {
    using detail::append_prefixed;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate(block), "block");

    const auto &layout = block.layout();
    require(report, layout.engine_id() == engine.id,
            ContractIssueCode::inconsistent_semantics, "layout.engine_id",
            "capture layout engine ID must exactly match the resolved engine");
    require(report, scenario.engine_profile_id == engine.profile_id.value,
            ContractIssueCode::inconsistent_semantics, "scenario.engine_profile_id",
            "capture scenario profile must exactly match the resolved engine "
            "profile");

    require(report, layout.cylinders().size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape, "layout.cylinders",
            "capture layout cylinder count must exactly match the resolved engine");
    const auto cylinder_count =
        std::min(layout.cylinders().size(), engine.cylinders.size());
    for (std::size_t index = 0; index < cylinder_count; ++index) {
        require(report, layout.cylinders()[index] == engine.cylinders[index].id,
                ContractIssueCode::inconsistent_semantics,
                "layout.cylinders[" + std::to_string(index) + "]",
                "capture cylinder identity and order must exactly match the "
                "resolved engine");
    }

    require(report, layout.ports().size() == engine.ports.size(),
            ContractIssueCode::inconsistent_shape, "layout.ports",
            "capture layout port count must exactly match the resolved engine");
    const auto port_count = std::min(layout.ports().size(), engine.ports.size());
    for (std::size_t index = 0; index < port_count; ++index) {
        const auto &actual = layout.ports()[index];
        const auto &expected = engine.ports[index];
        const auto path = "layout.ports[" + std::to_string(index) + "]";
        require(report, actual.id == expected.id,
                ContractIssueCode::inconsistent_semantics, path + ".id",
                "capture port identity and order must exactly match the resolved "
                "engine");
        require(report, actual.cylinder_id == expected.cylinder_id,
                ContractIssueCode::inconsistent_semantics, path + ".cylinder_id",
                "capture port cylinder must exactly match the resolved engine");
        require(report, actual.kind == expected.kind.value,
                ContractIssueCode::inconsistent_semantics, path + ".kind",
                "capture port kind must exactly match the resolved engine");
    }

    require(report, layout.gas_volumes().size() == engine.gas_volumes.size(),
            ContractIssueCode::inconsistent_shape, "layout.gas_volumes",
            "capture gas-volume count must exactly match the resolved engine");
    const auto volume_count =
        std::min(layout.gas_volumes().size(), engine.gas_volumes.size());
    for (std::size_t index = 0; index < volume_count; ++index) {
        const auto &actual = layout.gas_volumes()[index];
        const auto &expected = engine.gas_volumes[index];
        const auto path = "layout.gas_volumes[" + std::to_string(index) + "]";
        require(report, actual.id == expected.id,
                ContractIssueCode::inconsistent_semantics, path + ".id",
                "capture gas-volume identity and order must exactly match the "
                "resolved engine");
        require(report, actual.kind == expected.kind.value,
                ContractIssueCode::inconsistent_semantics, path + ".kind",
                "capture gas-volume kind must exactly match the resolved engine");
    }

    require(report, layout.flow_edges().size() == engine.flow_edges.size(),
            ContractIssueCode::inconsistent_shape, "layout.flow_edges",
            "capture flow-edge count must exactly match the resolved engine");
    const auto edge_count =
        std::min(layout.flow_edges().size(), engine.flow_edges.size());
    for (std::size_t index = 0; index < edge_count; ++index) {
        const auto &actual = layout.flow_edges()[index];
        const auto &expected = engine.flow_edges[index];
        const auto path = "layout.flow_edges[" + std::to_string(index) + "]";
        require(report, actual.id == expected.id,
                ContractIssueCode::inconsistent_semantics, path + ".id",
                "capture flow-edge identity and order must exactly match the "
                "resolved engine");
        require(report, actual.endpoint_0_volume_id == expected.endpoint_0_volume_id,
                ContractIssueCode::inconsistent_semantics,
                path + ".endpoint_0_volume_id",
                "capture flow-edge endpoint 0 must exactly match the resolved "
                "engine");
        require(report, actual.endpoint_1_volume_id == expected.endpoint_1_volume_id,
                ContractIssueCode::inconsistent_semantics,
                path + ".endpoint_1_volume_id",
                "capture flow-edge endpoint 1 must exactly match the resolved "
                "engine");
    }

    const auto expected_physical_route_count = static_cast<std::size_t>(
        std::ranges::count_if(engine.routes, [](const RouteSpec &route) {
            return known_physical_source_route(route.kind.value);
        }));
    require(report, layout.routes().size() == expected_physical_route_count,
            ContractIssueCode::inconsistent_shape, "layout.routes",
            "capture route count must exactly match the resolved engine's "
            "physical routes");
    std::size_t capture_route_index = 0;
    for (const auto &expected : engine.routes) {
        if (!known_physical_source_route(expected.kind.value)) {
            continue;
        }
        if (capture_route_index >= layout.routes().size()) {
            break;
        }
        const auto &actual = layout.routes()[capture_route_index];
        const auto path = "layout.routes[" + std::to_string(capture_route_index) + "]";
        require(report, actual.id == expected.id,
                ContractIssueCode::inconsistent_semantics, path + ".id",
                "capture route identity and physical-route order must exactly "
                "match the resolved engine");
        require(report, actual.kind == expected.kind.value,
                ContractIssueCode::inconsistent_semantics, path + ".kind",
                "capture route kind must exactly match the resolved engine");
        require(report, actual.source_volume_id == expected.source_volume_id,
                ContractIssueCode::inconsistent_semantics, path + ".source_volume_id",
                "capture route source endpoint must exactly match the resolved "
                "engine");
        require(report,
                actual.default_parent_route_id == expected.default_parent_route_id,
                ContractIssueCode::inconsistent_semantics,
                path + ".default_parent_route_id",
                "capture route parent must exactly match the resolved engine");
        const auto expected_emitter_anchor =
            expected.emitter_anchor_id.has_value()
                ? std::optional<std::string>{expected.emitter_anchor_id->value}
                : std::nullopt;
        require(report, actual.emitter_anchor_id == expected_emitter_anchor,
                ContractIssueCode::inconsistent_semantics, path + ".emitter_anchor_id",
                "capture route emitter endpoint must exactly match the resolved "
                "engine");
        ++capture_route_index;
    }

    require(report, block.clock().rate == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "clock.rate",
            "capture clock rate must exactly match the resolved scenario capture "
            "rate");
    require(report,
            block.declared_block_capacity_frames() ==
                scenario.quality.value.capture_block_capacity_frames,
            ContractIssueCode::inconsistent_semantics, "declared_block_capacity_frames",
            "capture block capacity must exactly match the resolved scenario quality");
    require(report,
            block.declared_event_journal_capacity_records() ==
                scenario.quality.value.event_journal_capacity_records,
            ContractIssueCode::inconsistent_semantics,
            "declared_event_journal_capacity_records",
            "event-journal capacity must exactly match the resolved scenario quality");

    const auto phase_offset =
        block.clock().phase == SamplePhase::post_step ? UINT64_C(1) : UINT64_C(0);
    std::uint64_t expected_first_timestamp = 0;
    const auto timestamp_origin_representable =
        (block.clock().phase == SamplePhase::pre_step ||
         block.clock().phase == SamplePhase::post_step) &&
        checked_add_u64(block.clock().first_sample_index, phase_offset,
                        expected_first_timestamp);
    require(report,
            timestamp_origin_representable &&
                block.clock().first_timestamp_tick == expected_first_timestamp,
            ContractIssueCode::inconsistent_semantics, "clock.first_timestamp_tick",
            "capture timestamp origin must match the sample index and sample phase");

    std::uint64_t sample_end = 0;
    const auto sample_interval_representable =
        block.frame_count() > 0 && checked_add_u64(block.clock().first_sample_index,
                                                   block.frame_count(), sample_end);
    require(report, sample_interval_representable, ContractIssueCode::invalid_value,
            "clock.first_sample_index",
            "capture sample interval must be representable");
    const auto scenario_capture_frames =
        resolve_frame_index(scenario.total_duration_s.value, scenario.rates.capture);
    require(report, scenario_capture_frames.has_value(),
            ContractIssueCode::inconsistent_semantics, "scenario.total_duration_s",
            "scenario duration must resolve to an integral capture-frame horizon");
    if (sample_interval_representable && scenario_capture_frames.has_value()) {
        require(report, sample_end <= *scenario_capture_frames,
                ContractIssueCode::inconsistent_semantics, "clock.first_sample_index",
                "capture block's half-open sample interval must fit inside the "
                "scenario");
    }

    return report;
}

} // namespace engine_sim_offline::contract
