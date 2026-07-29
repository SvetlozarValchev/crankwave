#include "acoustics/ideal_compact_junction.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::acoustics {

IdealCompactFourPortJunction::IdealCompactFourPortJunction(
    CompactFourPortWaves characteristic_impedances_pa_s_m3)
    : characteristic_impedances_pa_s_m3_(std::move(characteristic_impedances_pa_s_m3)) {
    for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
        const double impedance = characteristic_impedances_pa_s_m3_[port];
        if (!std::isfinite(impedance) || !(impedance > 0.0)) {
            throw std::invalid_argument{"compact-junction characteristic impedances "
                                        "must be positive and finite"};
        }
        const double admittance = 1.0 / impedance;
        if (!std::isfinite(admittance) || !(admittance > 0.0)) {
            throw std::invalid_argument{"compact-junction characteristic admittances "
                                        "must be positive and finite"};
        }
        characteristic_admittances_m3_pa_s_[port] = admittance;
        admittance_sum_m3_pa_s_ += admittance;
    }
    if (!std::isfinite(admittance_sum_m3_pa_s_) || !(admittance_sum_m3_pa_s_ > 0.0)) {
        throw std::invalid_argument{
            "compact-junction admittance sum must be positive and finite"};
    }
}

CompactFourPortScattering IdealCompactFourPortJunction::scatter(
    const CompactFourPortWaves &arriving_pressure_waves_pa) const {
    double admittance_weighted_arrival_pa_m3_pa_s = 0.0;
    for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
        const double arrival = arriving_pressure_waves_pa[port];
        if (!std::isfinite(arrival)) {
            throw std::domain_error{
                "compact-junction arriving pressure wave was non-finite"};
        }
        admittance_weighted_arrival_pa_m3_pa_s +=
            characteristic_admittances_m3_pa_s_[port] * arrival;
    }
    if (!std::isfinite(admittance_weighted_arrival_pa_m3_pa_s)) {
        throw std::domain_error{
            "compact-junction weighted arriving pressure was non-finite"};
    }

    CompactFourPortScattering result;
    result.common_pressure_pa =
        2.0 * admittance_weighted_arrival_pa_m3_pa_s / admittance_sum_m3_pa_s_;
    if (!std::isfinite(result.common_pressure_pa)) {
        throw std::domain_error{"compact-junction common pressure was non-finite"};
    }

    for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
        result.departing_pressure_waves_pa[port] =
            result.common_pressure_pa - arriving_pressure_waves_pa[port];
        if (!std::isfinite(result.departing_pressure_waves_pa[port])) {
            throw std::domain_error{
                "compact-junction departing pressure wave was non-finite"};
        }
    }
    return result;
}

const CompactFourPortWaves &
IdealCompactFourPortJunction::characteristic_impedances_pa_s_m3() const noexcept {
    return characteristic_impedances_pa_s_m3_;
}

const CompactFourPortWaves &
IdealCompactFourPortJunction::characteristic_admittances_m3_pa_s() const noexcept {
    return characteristic_admittances_m3_pa_s_;
}

} // namespace engine_sim_offline::acoustics
