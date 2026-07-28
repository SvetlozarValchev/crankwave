#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>

namespace engine_sim_offline::contract {

enum class Availability : std::uint8_t {
    available,
    unavailable,
};

enum class Completeness : std::uint8_t {
    complete,
    incomplete,
};

// Availability says whether the numeric value may be consumed. Completeness says
// whether every term required by that quantity's model is represented. An available
// value may therefore be complete or incomplete; an unavailable value is necessarily
// incomplete.
enum class QuantityUnavailableReason : std::uint8_t {
    none,
    scenario_not_applicable,
    model_not_admitted,
    equivalent_inertia_missing,
    cycle_integration_not_admitted,
    not_settled,
    required_input_missing,
};

enum class TorqueTerm : std::uint64_t {
    indicated_gas = 1ULL << 0U,
    crank_friction = 1ULL << 1U,
    piston_ring_friction = 1ULL << 2U,
    bearing_friction = 1ULL << 3U,
    valvetrain_friction = 1ULL << 4U,
    pump_and_oil = 1ULL << 5U,
    accessory = 1ULL << 6U,
    starter = 1ULL << 7U,
};

using TorqueTermMask = std::uint64_t;

[[nodiscard]] constexpr TorqueTermMask torque_term_mask(TorqueTerm term) noexcept {
    return static_cast<TorqueTermMask>(term);
}

[[nodiscard]] constexpr TorqueTermMask known_torque_term_mask() noexcept {
    return torque_term_mask(TorqueTerm::indicated_gas) |
           torque_term_mask(TorqueTerm::crank_friction) |
           torque_term_mask(TorqueTerm::piston_ring_friction) |
           torque_term_mask(TorqueTerm::bearing_friction) |
           torque_term_mask(TorqueTerm::valvetrain_friction) |
           torque_term_mask(TorqueTerm::pump_and_oil) |
           torque_term_mask(TorqueTerm::accessory) |
           torque_term_mask(TorqueTerm::starter);
}

[[nodiscard]] constexpr TorqueTermMask indicated_gas_torque_term_mask() noexcept {
    return torque_term_mask(TorqueTerm::indicated_gas);
}

[[nodiscard]] constexpr TorqueTermMask
friction_pump_and_accessory_torque_term_mask() noexcept {
    return known_torque_term_mask() &
           ~(indicated_gas_torque_term_mask() | torque_term_mask(TorqueTerm::starter));
}

struct QuantityValue {
    double value = 0.0;
    Availability availability = Availability::unavailable;
    Completeness completeness = Completeness::incomplete;
    QuantityUnavailableReason unavailable_reason =
        QuantityUnavailableReason::model_not_admitted;

    friend bool operator==(const QuantityValue &, const QuantityValue &) = default;
};

struct TorqueValueNm {
    double value_nm = 0.0;
    Availability availability = Availability::unavailable;
    Completeness completeness = Completeness::incomplete;
    QuantityUnavailableReason unavailable_reason =
        QuantityUnavailableReason::model_not_admitted;
    TorqueTermMask included_terms = 0;
    TorqueTermMask omitted_terms = 0;

    friend bool operator==(const TorqueValueNm &, const TorqueValueNm &) = default;
};

struct TorqueTelemetry {
    TorqueValueNm instantaneous_indicated_gas;
    TorqueValueNm pumping_partition;
    TorqueValueNm friction_pump_and_accessory;
    TorqueValueNm starter;
    TorqueValueNm instantaneous_net_shaft;
    TorqueValueNm cycle_mean_net_shaft;
    TorqueValueNm actuator;
    TorqueValueNm dyno_reaction;
    QuantityValue cycle_work_j;
    QuantityValue net_bmep_pa;
    QuantityValue instantaneous_power_w;
    QuantityValue cycle_mean_power_w;

    friend bool operator==(const TorqueTelemetry &, const TorqueTelemetry &) = default;
};

struct NetTorqueFormCapability {
    Availability availability = Availability::unavailable;
    Completeness completeness = Completeness::incomplete;
    TorqueTermMask included_terms = 0;
    TorqueTermMask omitted_terms = 0;

    friend bool operator==(const NetTorqueFormCapability &,
                           const NetTorqueFormCapability &) = default;
};

struct TorqueCapability {
    NetTorqueFormCapability instantaneous_net_shaft;
    NetTorqueFormCapability cycle_mean_net_shaft;
    bool equivalent_inertia_available = false;

    friend bool operator==(const TorqueCapability &,
                           const TorqueCapability &) = default;
};

[[nodiscard]] ValidationReport validate(const QuantityValue &value);
[[nodiscard]] ValidationReport validate(const TorqueValueNm &value);
[[nodiscard]] ValidationReport validate(const TorqueTelemetry &telemetry);
[[nodiscard]] ValidationReport validate(const NetTorqueFormCapability &capability);
[[nodiscard]] ValidationReport validate(const TorqueCapability &capability);

} // namespace engine_sim_offline::contract
