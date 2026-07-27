#include "contract_test_support.hpp"

#include <type_traits>

namespace engine_sim_offline::contract::test {

void run_primitives_contract_tests() {
    static_assert(!std::is_convertible_v<CylinderId, PortId>);
    static_assert(!std::is_convertible_v<RouteId, FlowEdgeId>);

    expect(validate(RationalRateHz{10000, 1}).ok(), "valid reduced rate rejected");
    expect(!validate(RationalRateHz{20000, 2}).ok(), "unreduced rate accepted");

    TorqueValueNm unavailable;
    unavailable.unavailable_reason =
        QuantityUnavailableReason::equivalent_inertia_missing;
    expect(validate(unavailable).ok(), "explicit unavailable torque should be valid");
    unavailable.value_nm = 1.0;
    expect(!validate(unavailable).ok(),
           "unavailable torque was mistaken for a numeric torque");

    QuantityValue unavailable_quantity;
    unavailable_quantity.unavailable_reason = QuantityUnavailableReason::not_settled;
    expect(validate(unavailable_quantity).ok(),
           "explicit unavailable scalar quantity should be valid");
    unavailable_quantity.completeness = Completeness::complete;
    expect(!validate(unavailable_quantity).ok(),
           "unavailable scalar quantity was allowed to claim completeness");
    unavailable_quantity.completeness = static_cast<Completeness>(255);
    expect(!validate(unavailable_quantity).ok(),
           "unknown scalar completeness value was accepted");

    const QuantityValue available_partial{
        1.0,
        Availability::available,
        Completeness::incomplete,
        QuantityUnavailableReason::none,
    };
    expect(validate(available_partial).ok(),
           "available incomplete quantity was rejected as unavailable");

    const auto available_torque = [](double value_nm, TorqueTermMask terms) {
        return TorqueValueNm{
            value_nm,
            Availability::available,
            Completeness::complete,
            QuantityUnavailableReason::none,
            terms,
            0,
        };
    };
    TorqueTelemetry typed_telemetry;
    typed_telemetry.instantaneous_indicated_gas =
        available_torque(10.0, indicated_gas_torque_term_mask());
    typed_telemetry.pumping_partition = available_torque(-1.0, 0);
    typed_telemetry.friction_pump_and_accessory =
        available_torque(-2.0, friction_pump_and_accessory_torque_term_mask());
    typed_telemetry.starter =
        available_torque(0.0, torque_term_mask(TorqueTerm::starter));
    typed_telemetry.instantaneous_net_shaft =
        available_torque(8.0, known_torque_term_mask());
    typed_telemetry.cycle_mean_net_shaft =
        available_torque(7.0, known_torque_term_mask());
    typed_telemetry.actuator = available_torque(-8.0, 0);
    typed_telemetry.dyno_reaction = available_torque(8.0, 0);
    expect(validate(typed_telemetry).ok(),
           "correctly classified named torque telemetry was rejected");

    auto underclassified_net = typed_telemetry;
    underclassified_net.instantaneous_net_shaft.included_terms =
        indicated_gas_torque_term_mask();
    expect(!validate(underclassified_net).ok(),
           "complete net torque omitted physical terms without classifying them");

    auto mislabeled_partition = typed_telemetry;
    mislabeled_partition.pumping_partition.included_terms =
        indicated_gas_torque_term_mask();
    expect(!validate(mislabeled_partition).ok(),
           "diagnostic pumping partition claimed an additive engine torque term");

    auto mislabeled_actuator = typed_telemetry;
    mislabeled_actuator.actuator.included_terms =
        torque_term_mask(TorqueTerm::accessory);
    expect(!validate(mislabeled_actuator).ok(),
           "test-cell actuator claimed an engine torque term");

    auto classified_incomplete_net = typed_telemetry;
    classified_incomplete_net.instantaneous_net_shaft.completeness =
        Completeness::incomplete;
    classified_incomplete_net.instantaneous_net_shaft.included_terms =
        indicated_gas_torque_term_mask();
    classified_incomplete_net.instantaneous_net_shaft.omitted_terms =
        friction_pump_and_accessory_torque_term_mask() |
        torque_term_mask(TorqueTerm::starter);
    expect(validate(classified_incomplete_net).ok(),
           "incomplete net torque with an exhaustive term classification was "
           "rejected");

    const TorqueCapability complete_capability{
        true, true, true, known_torque_term_mask(), 0,
    };
    expect(validate(complete_capability).ok(),
           "fully classified physical net torque capability was rejected");
    auto falsely_complete = complete_capability;
    falsely_complete.omitted_terms = torque_term_mask(TorqueTerm::accessory);
    falsely_complete.included_terms &= ~falsely_complete.omitted_terms;
    expect(!validate(falsely_complete).ok(),
           "torque capability concealed an omitted term behind a complete claim");

    InputBuilder crosswired_builder;
    const auto first_leaf = crosswired_builder.resolved(1.0, "crosswire.first");
    auto crosswired_leaf = crosswired_builder.resolved(2.0, "crosswire.second");
    crosswired_leaf.resolution_id = first_leaf.resolution_id;
    EngineSpec crosswired_engine = make_engine(crosswired_builder);
    crosswired_engine.total_displacement_m3.resolution_id =
        crosswired_leaf.resolution_id;
    expect(!validate(crosswired_engine, crosswired_builder.provenance).ok(),
           "resolved value accepted provenance belonging to another parameter path");

    const ReachabilityCandidate incumbent{2, 0.8, 12.0, 90.0, true};
    const ReachabilityCandidate lower_actuator{3, 0.9, 8.0, 110.0, true};
    const ReachabilityCandidate lower_throttle{1, 0.7, 8.0, 90.0, true};
    expect(is_better_nearest_candidate(lower_actuator, incumbent, 100.0),
           "equal-error reachability tie did not prefer lower actuator magnitude");
    expect(is_better_nearest_candidate(lower_throttle, lower_actuator, 100.0),
           "reachability tie did not prefer lower throttle");

    InputBuilder builder;
    builder.provenance.resolutions.push_back({
        "cycle-a",
        "cycle.a",
        ResolutionMode::derived,
        "derived-claim",
        method("derive-a", 20),
        {"cycle.b"},
    });
    builder.provenance.resolutions.push_back({
        "cycle-b",
        "cycle.b",
        ResolutionMode::derived,
        "derived-claim",
        method("derive-b", 21),
        {"cycle.a"},
    });
    expect(!validate(builder.provenance).ok(),
           "cyclic resolution provenance was accepted");
}

} // namespace engine_sim_offline::contract::test
