#include "simulation/legacy_fixed_crank_torque_accounting.hpp"

#include <cmath>

namespace engine_sim_offline::simulation {
namespace {

constexpr contract::Sha256Digest kLegacyLowOrderV1ConfigurationSha256{{
    0x43, 0x54, 0x41, 0x89, 0x0e, 0x0a, 0x5f, 0x8d, 0x01, 0xe8, 0x19,
    0x95, 0xf6, 0x4f, 0x33, 0xd4, 0xc5, 0x54, 0x14, 0x4f, 0x5b, 0x14,
    0x36, 0x89, 0x5e, 0x68, 0x16, 0xf6, 0xdb, 0x85, 0xe3, 0x4c,
}};

[[nodiscard]] contract::TorqueValueNm
available_torque(double value_nm, contract::Completeness completeness,
                 contract::TorqueTermMask included,
                 contract::TorqueTermMask omitted) noexcept {
    return {
        value_nm,     contract::Availability::available,
        completeness, contract::QuantityUnavailableReason::none,
        included,     omitted,
    };
}

[[nodiscard]] contract::TorqueValueNm
unavailable_torque(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
        0,
        0,
    };
}

[[nodiscard]] contract::QuantityValue
unavailable_quantity(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
    };
}

} // namespace

LegacyFixedCrankTorqueAccountingCompileResult
compile_legacy_fixed_crank_torque_accounting(
    const contract::EngineSpec &engine,
    const contract::LegacyFixedCrankLossV1 &profile) {
    contract::ValidationReport report;
    if (!std::isfinite(profile.fixed_crank_friction_magnitude_nm.value) ||
        profile.fixed_crank_friction_magnitude_nm.value < 0.0) {
        report.add(
            contract::ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank."
            "fixed_crank_friction_magnitude_nm.value",
            "legacy fixed crank-friction magnitude must be finite and nonnegative");
    }

    const auto required_included_terms =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto required_omitted_terms =
        contract::known_torque_term_mask() & ~required_included_terms;
    if (profile.included_terms.value != required_included_terms ||
        profile.omitted_terms.value != required_omitted_terms) {
        report.add(contract::ContractIssueCode::unsupported_value,
                   "engine.physics_profile.losses",
                   "legacy fixed crank accounting requires the exact M3 included "
                   "and omitted torque inventory");
    }

    const auto &method = engine.methods.losses.value;
    if (method.id != "legacy_low_order_v1" || method.version != 1U ||
        method.configuration_sha256 != kLegacyLowOrderV1ConfigurationSha256) {
        report.add(contract::ContractIssueCode::unsupported_value,
                   "engine.methods.losses",
                   "legacy fixed crank accounting requires the exact "
                   "legacy_low_order_v1 version 1 configuration");
    }

    const auto &capability = engine.torque_capability.value;
    if (capability.instantaneous_net_shaft.availability !=
            contract::Availability::available ||
        capability.instantaneous_net_shaft.completeness !=
            contract::Completeness::incomplete ||
        capability.instantaneous_net_shaft.included_terms != required_included_terms ||
        capability.instantaneous_net_shaft.omitted_terms != required_omitted_terms ||
        capability.cycle_mean_net_shaft.availability !=
            contract::Availability::unavailable ||
        capability.cycle_mean_net_shaft.completeness !=
            contract::Completeness::incomplete ||
        capability.cycle_mean_net_shaft.included_terms != 0 ||
        capability.cycle_mean_net_shaft.omitted_terms != 0 ||
        capability.equivalent_inertia_available) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "engine.torque_capability.value",
                   "legacy fixed crank accounting requires the exact incomplete "
                   "M3 instantaneous capability");
    }

    if (!report.ok()) {
        return report;
    }
    return LegacyFixedCrankTorqueAccountingPlan{
        profile.fixed_crank_friction_magnitude_nm.value,
    };
}

LegacyFixedCrankTorqueAccountingEvaluation
evaluate_legacy_fixed_crank_torque_accounting(
    const LegacyFixedCrankTorqueAccountingPlan &plan, double angular_speed_rad_s,
    double indicated_gas_torque_nm) noexcept {
    if (!std::isfinite(angular_speed_rad_s)) {
        return LegacyFixedCrankTorqueAccountingError{
            LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_angular_speed,
        };
    }
    if (!std::isfinite(indicated_gas_torque_nm)) {
        return LegacyFixedCrankTorqueAccountingError{
            LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_indicated_gas_torque,
        };
    }

    double crank_friction_torque_nm = 0.0;
    if (angular_speed_rad_s > 0.0) {
        crank_friction_torque_nm = -plan.magnitude_nm;
    } else if (angular_speed_rad_s < 0.0) {
        crank_friction_torque_nm = plan.magnitude_nm;
    }
    const double incomplete_modeled_net_torque_nm =
        indicated_gas_torque_nm + crank_friction_torque_nm;
    if (!std::isfinite(crank_friction_torque_nm) ||
        !std::isfinite(incomplete_modeled_net_torque_nm)) {
        return LegacyFixedCrankTorqueAccountingError{
            LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_torque_sum,
        };
    }

    const auto indicated =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas);
    const auto crank = contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto included = indicated | crank;
    const auto omitted = contract::known_torque_term_mask() & ~included;
    const auto friction_scope =
        contract::friction_pump_and_accessory_torque_term_mask();

    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas = available_torque(
        indicated_gas_torque_nm, contract::Completeness::complete, indicated, 0);
    result.pumping_partition = unavailable_torque(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.friction_pump_and_accessory =
        available_torque(crank_friction_torque_nm, contract::Completeness::incomplete,
                         crank, friction_scope & ~crank);
    result.starter =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.instantaneous_net_shaft =
        available_torque(incomplete_modeled_net_torque_nm,
                         contract::Completeness::incomplete, included, omitted);
    result.cycle_mean_net_shaft = unavailable_torque(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.actuator = unavailable_torque(
        contract::QuantityUnavailableReason::equivalent_inertia_missing);
    result.dyno_reaction = unavailable_torque(
        contract::QuantityUnavailableReason::equivalent_inertia_missing);
    result.cycle_work_j = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.net_bmep_pa = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.instantaneous_power_w = {
        incomplete_modeled_net_torque_nm * angular_speed_rad_s,
        contract::Availability::available,
        contract::Completeness::incomplete,
        contract::QuantityUnavailableReason::none,
    };
    result.cycle_mean_power_w = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    return result;
}

} // namespace engine_sim_offline::simulation
