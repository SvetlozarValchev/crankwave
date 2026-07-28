#include "engine_sim_offline/contract/torque.hpp"

#include "validation_support.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace engine_sim_offline::contract {
namespace {

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

void validate_availability(ValidationReport &report, double value,
                           Availability availability, QuantityUnavailableReason reason,
                           std::string_view path) {
    using detail::finite;
    using detail::require;

    const auto field_path = [path](std::string_view field) {
        if (path.empty()) {
            return std::string(field);
        }
        return std::string(path) + "." + std::string(field);
    };

    require(report, known(availability), ContractIssueCode::unsupported_value,
            field_path("availability"), "quantity availability is not recognized");
    require(report, known(reason), ContractIssueCode::unsupported_value,
            field_path("unavailable_reason"),
            "quantity-unavailable reason is not recognized");
    if (!known(availability) || !known(reason)) {
        return;
    }

    if (availability == Availability::available) {
        require(report, finite(value), ContractIssueCode::invalid_value,
                field_path("value"), "available quantity must be finite");
        require(report, reason == QuantityUnavailableReason::none,
                ContractIssueCode::inconsistent_semantics,
                field_path("unavailable_reason"),
                "available quantity must use the none reason");
    } else {
        require(
            report, value == 0.0 && !std::signbit(value),
            ContractIssueCode::inconsistent_semantics, field_path("value"),
            "unavailable quantity uses canonical positive zero and is never consumed");
        require(report, reason != QuantityUnavailableReason::none,
                ContractIssueCode::inconsistent_semantics,
                field_path("unavailable_reason"),
                "unavailable quantity must state why");
    }
}

void validate_completeness(ValidationReport &report, Availability availability,
                           Completeness completeness, std::string_view path) {
    using detail::require;

    require(report, known(completeness), ContractIssueCode::unsupported_value,
            std::string(path), "quantity completeness is not recognized");
    if (known(availability) && known(completeness) &&
        availability == Availability::unavailable) {
        require(report, completeness == Completeness::incomplete,
                ContractIssueCode::inconsistent_semantics, std::string(path),
                "unavailable quantity must be incomplete");
    }
}

void validate_named_term_scope(ValidationReport &report, const TorqueValueNm &value,
                               TorqueTermMask expected_terms, std::string_view path) {
    using detail::require;

    if (value.availability != Availability::available) {
        return;
    }
    require(report, (value.included_terms | value.omitted_terms) == expected_terms,
            ContractIssueCode::inconsistent_semantics,
            std::string(path) + ".included_terms",
            "available named torque must classify exactly its physical term scope");
}

} // namespace

ValidationReport validate(const QuantityValue &value) {
    ValidationReport report;
    validate_availability(report, value.value, value.availability,
                          value.unavailable_reason, "");
    validate_completeness(report, value.availability, value.completeness,
                          "completeness");
    return report;
}

ValidationReport validate(const TorqueValueNm &value) {
    using detail::require;

    ValidationReport report;
    validate_availability(report, value.value_nm, value.availability,
                          value.unavailable_reason, "");
    validate_completeness(report, value.availability, value.completeness,
                          "completeness");
    require(
        report,
        ((value.included_terms | value.omitted_terms) & ~known_torque_term_mask()) == 0,
        ContractIssueCode::unsupported_value, "included_terms",
        "torque masks contain unknown terms");
    if ((value.included_terms & value.omitted_terms) != 0) {
        report.add(ContractIssueCode::inconsistent_semantics, "included_terms",
                   "a torque term cannot be both included and omitted");
    }
    if (value.completeness == Completeness::complete && value.omitted_terms != 0) {
        report.add(ContractIssueCode::inconsistent_semantics, "completeness",
                   "complete torque cannot declare omitted terms");
    }
    if (value.availability == Availability::unavailable) {
        require(report, value.included_terms == 0 && value.omitted_terms == 0,
                ContractIssueCode::inconsistent_semantics, "included_terms",
                "unavailable torque uses canonical empty term masks");
    }
    return report;
}

ValidationReport validate(const TorqueTelemetry &telemetry) {
    using detail::append_prefixed;

    ValidationReport report;
    append_prefixed(report, validate(telemetry.instantaneous_indicated_gas),
                    "instantaneous_indicated_gas");
    append_prefixed(report, validate(telemetry.pumping_partition), "pumping_partition");
    append_prefixed(report, validate(telemetry.friction_pump_and_accessory),
                    "friction_pump_and_accessory");
    append_prefixed(report, validate(telemetry.starter), "starter");
    append_prefixed(report, validate(telemetry.instantaneous_net_shaft),
                    "instantaneous_net_shaft");
    append_prefixed(report, validate(telemetry.cycle_mean_net_shaft),
                    "cycle_mean_net_shaft");
    append_prefixed(report, validate(telemetry.actuator), "actuator");
    append_prefixed(report, validate(telemetry.dyno_reaction), "dyno_reaction");
    append_prefixed(report, validate(telemetry.cycle_work_j), "cycle_work_j");
    append_prefixed(report, validate(telemetry.net_bmep_pa), "net_bmep_pa");
    append_prefixed(report, validate(telemetry.instantaneous_power_w),
                    "instantaneous_power_w");
    append_prefixed(report, validate(telemetry.cycle_mean_power_w),
                    "cycle_mean_power_w");

    validate_named_term_scope(report, telemetry.instantaneous_indicated_gas,
                              indicated_gas_torque_term_mask(),
                              "instantaneous_indicated_gas");
    // Pumping is a diagnostic partition of indicated gas work, not another
    // additive physical net-torque term.
    validate_named_term_scope(report, telemetry.pumping_partition, 0,
                              "pumping_partition");
    validate_named_term_scope(report, telemetry.friction_pump_and_accessory,
                              friction_pump_and_accessory_torque_term_mask(),
                              "friction_pump_and_accessory");
    validate_named_term_scope(report, telemetry.starter,
                              torque_term_mask(TorqueTerm::starter), "starter");
    validate_named_term_scope(report, telemetry.instantaneous_net_shaft,
                              known_torque_term_mask(), "instantaneous_net_shaft");
    validate_named_term_scope(report, telemetry.cycle_mean_net_shaft,
                              known_torque_term_mask(), "cycle_mean_net_shaft");
    // Test-cell actuator and reaction torque are outside engine net-torque
    // term ownership.
    validate_named_term_scope(report, telemetry.actuator, 0, "actuator");
    validate_named_term_scope(report, telemetry.dyno_reaction, 0, "dyno_reaction");

    if (telemetry.actuator.availability == Availability::available &&
        telemetry.dyno_reaction.availability == Availability::available &&
        telemetry.dyno_reaction.value_nm != -telemetry.actuator.value_nm) {
        report.add(ContractIssueCode::inconsistent_semantics, "dyno_reaction.value_nm",
                   "dyno reaction must be exactly the negative of actuator torque");
    }

    return report;
}

ValidationReport validate(const NetTorqueFormCapability &capability) {
    using detail::require;

    ValidationReport report;
    const auto classified = capability.included_terms | capability.omitted_terms;
    require(report, known(capability.availability),
            ContractIssueCode::unsupported_value, "availability",
            "net-torque form availability is not recognized");
    require(report, known(capability.completeness),
            ContractIssueCode::unsupported_value, "completeness",
            "net-torque form completeness is not recognized");
    require(report, (classified & ~known_torque_term_mask()) == 0,
            ContractIssueCode::unsupported_value, "included_terms",
            "net-torque form capability contains unknown terms");
    require(report, (capability.included_terms & capability.omitted_terms) == 0,
            ContractIssueCode::inconsistent_semantics, "included_terms",
            "a net-torque form cannot both include and omit the same term");

    if (capability.availability == Availability::available) {
        require(report, classified == known_torque_term_mask(),
                ContractIssueCode::inconsistent_semantics, "included_terms",
                "an available net-torque form must classify every known physical term");
        require(report,
                capability.completeness == (capability.omitted_terms == 0
                                                ? Completeness::complete
                                                : Completeness::incomplete),
                ContractIssueCode::inconsistent_semantics, "completeness",
                "an available net-torque form is complete exactly when it omits "
                "no terms");
    } else if (capability.availability == Availability::unavailable) {
        require(report, capability.completeness == Completeness::incomplete,
                ContractIssueCode::inconsistent_semantics, "completeness",
                "an unavailable net-torque form must be incomplete");
        require(report, capability.included_terms == 0 && capability.omitted_terms == 0,
                ContractIssueCode::inconsistent_semantics, "included_terms",
                "an unavailable net-torque form uses canonical empty term masks");
    }
    return report;
}

ValidationReport validate(const TorqueCapability &capability) {
    using detail::append_prefixed;

    ValidationReport report;
    append_prefixed(report, validate(capability.instantaneous_net_shaft),
                    "instantaneous_net_shaft");
    append_prefixed(report, validate(capability.cycle_mean_net_shaft),
                    "cycle_mean_net_shaft");
    return report;
}

} // namespace engine_sim_offline::contract
