#include "session/event_counters.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace crankwave;
namespace contract = crankwave::contract;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] contract::EngineEvent event(const std::uint8_t ordinal,
                                          contract::EngineEventPayload payload) {
    return {0U, ordinal, std::move(payload)};
}

void test_every_admitted_event_and_reason_is_counted() {
    const std::array events{
        event(0U, contract::SparkCrossing{contract::CylinderId{1U}, 0.0, 0.1, 0.1, 0.2,
                                          0.1}),
        event(1U, contract::LimiterStateChanged{false, true, true, 0.5}),
        event(2U, contract::LimiterStateChanged{true, false, false, 0.0}),
        event(3U, contract::IgnitionAccepted{contract::CylinderId{1U}, 0.8, 1.2}),
        event(4U,
              contract::IgnitionRejected{contract::CylinderId{1U},
                                         contract::IgnitionRejection::active_flame}),
        event(5U, contract::IgnitionRejected{contract::CylinderId{1U},
                                             contract::IgnitionRejection::no_fuel}),
        event(6U, contract::IgnitionRejected{contract::CylinderId{1U},
                                             contract::IgnitionRejection::mixture_low}),
        event(7U,
              contract::IgnitionRejected{contract::CylinderId{1U},
                                         contract::IgnitionRejection::mixture_high}),
        event(8U,
              contract::FlameExtinguished{
                  contract::CylinderId{1U}, 0U,
                  contract::FlameExtinctionReason::intake_transfer}),
        event(9U,
              contract::FlameExtinguished{
                  contract::CylinderId{1U}, 1U,
                  contract::FlameExtinctionReason::no_geometric_progress}),
    };

    static_assert(noexcept(
        session::reduce_event_counters(std::span<const contract::EngineEvent>{},
                                       std::declval<EngineEventCounters &>())));

    EngineEventCounters first;
    EngineEventCounters repeated;
    expect(session::reduce_event_counters(events, first),
           "the complete admitted event inventory was rejected");
    expect(session::reduce_event_counters(events, repeated) && repeated == first,
           "identical event inventories produced different counters");

    const EngineEventCounters expected{
        10U, 1U, 2U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U,
    };
    expect(first == expected,
           "the fixed v1 counters lost an admitted event variant or reason");

    EngineEventCounters empty{.total_event_record_count = 99U};
    expect(session::reduce_event_counters({}, empty) && empty == EngineEventCounters{},
           "an empty journal did not deterministically produce zero counters");
}

void expect_rejected_without_partial_output(const contract::EngineEvent &invalid,
                                            const std::string_view context) {
    const std::array events{
        event(0U, contract::SparkCrossing{contract::CylinderId{1U}, 0.0, 0.1, 0.1, 0.2,
                                          0.1}),
        invalid,
    };
    EngineEventCounters output{
        .total_event_record_count = 41U,
        .spark_crossing_count = 17U,
    };
    const auto sentinel = output;
    expect(!session::reduce_event_counters(events, output) && output == sentinel,
           context);
}

void test_invalid_sentinels_and_non_transitions_fail_closed() {
    expect_rejected_without_partial_output(
        event(1U, contract::IgnitionRejected{contract::CylinderId{1U},
                                             contract::IgnitionRejection::unspecified}),
        "an unspecified ignition rejection was admitted or partially published");
    expect_rejected_without_partial_output(
        event(
            1U,
            contract::FlameExtinguished{contract::CylinderId{1U}, 0U,
                                        contract::FlameExtinctionReason::unspecified}),
        "an unspecified flame extinction was admitted or partially published");
    expect_rejected_without_partial_output(
        event(1U, contract::LimiterStateChanged{false, false, false, 0.0}),
        "an inactive-to-inactive limiter event was admitted or partially published");
    expect_rejected_without_partial_output(
        event(1U, contract::LimiterStateChanged{true, true, true, 0.5}),
        "an active-to-active limiter event was admitted or partially published");
}

void run_tests() {
    test_every_admitted_event_and_reason_is_counted();
    test_invalid_sentinels_and_non_transitions_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Session event-counter test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
