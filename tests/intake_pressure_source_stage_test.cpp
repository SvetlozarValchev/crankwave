#include "presentation/intake_pressure_source_stage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

namespace allocation_probe {

bool reject_allocations = false;

} // namespace allocation_probe

void *operator new(std::size_t size) {
    if (allocation_probe::reject_allocations) {
        throw std::bad_alloc{};
    }
    if (void *allocation = std::malloc(size == 0U ? 1U : size)) {
        return allocation;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void *allocation) noexcept {
    ::operator delete(allocation);
}

void operator delete(void *allocation, std::size_t) noexcept {
    ::operator delete(allocation);
}

void operator delete[](void *allocation, std::size_t) noexcept {
    ::operator delete(allocation);
}

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;

constexpr std::array kRoutes{
    IntakePressureSourceRouteConfiguration{contract::RouteId{41}, 101325.0, 1.0},
    IntakePressureSourceRouteConfiguration{contract::RouteId{7}, 95000.0, 2.0},
};
constexpr std::array kRouteIds{kRoutes[0].id, kRoutes[1].id};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

[[nodiscard]] std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] IntakePressureSourceStageConfiguration make_configuration(
    std::span<const IntakePressureSourceRouteConfiguration> routes = kRoutes,
    contract::RationalRateHz input_rate = kIntakePressureInputRateHz,
    contract::RationalRateHz source_rate = kIntakePressureSourceRateHz,
    std::size_t input_frames_per_block = kExcitationFramesPerMethodBlock) {
    return {routes, input_rate, source_rate, input_frames_per_block};
}

template <class Values, class RouteIds>
[[nodiscard]] IntakePressureInputBlockView
make_view(std::uint64_t first_frame_index, Values &values, RouteIds &route_ids,
          std::size_t frame_count = kExcitationFramesPerMethodBlock,
          contract::RationalRateHz rate = kIntakePressureInputRateHz) {
    return IntakePressureInputBlockView::borrow_for_callback(
        first_frame_index, rate, route_ids, frame_count, values);
}

void fill_matched_pressure_block(
    std::span<double> values, std::size_t block_index,
    std::span<const IntakePressureSourceRouteConfiguration> routes = kRoutes) {
    expect(values.size() == kExcitationFramesPerMethodBlock * routes.size(),
           "intake-pressure fixture matrix has the wrong extent");
    for (std::size_t frame = 0; frame < kExcitationFramesPerMethodBlock; ++frame) {
        const auto global = block_index * kExcitationFramesPerMethodBlock + frame;
        const auto centered = static_cast<int>(global % 29U) - 14;
        const double gauge = 80.0 + static_cast<double>(centered) * 0.25;
        for (std::size_t route = 0; route < routes.size(); ++route) {
            values[frame * routes.size() + route] =
                routes[route].reference_pressure_pa + gauge;
        }
    }
}

void test_exact_pressure_pipeline_and_gain() {
    std::vector<double> input(kExcitationFramesPerMethodBlock * kRoutes.size());
    std::vector<double> actual(kSourceFramesPerMethodBlock * kRoutes.size());
    fill_matched_pressure_block(input, 0);

    IntakePressureSourceStage stage{make_configuration()};
    const auto block = stage.process(make_view(0, input, kRouteIds), actual);
    expect(block.first_input_frame_index() == 0U &&
               block.first_source_frame_index() == 0U &&
               block.sample_rate() == kIntakePressureSourceRateHz &&
               block.input_frame_count() == kExcitationFramesPerMethodBlock &&
               block.frame_count() == kSourceFramesPerMethodBlock &&
               block.pressure_pa_gauge().data() == actual.data() &&
               std::ranges::equal(block.route_ids(), kRouteIds),
           "intake-pressure source block view changed its clock or route extent");
    expect(stage.input_rate() == kIntakePressureInputRateHz &&
               stage.source_rate() == kIntakePressureSourceRateHz &&
               stage.next_input_frame_index() == kExcitationFramesPerMethodBlock &&
               stage.next_source_frame_index() == kSourceFramesPerMethodBlock &&
               !stage.terminal_failed(),
           "intake-pressure stage counters or canonical clocks changed");

    std::vector<double> gauge(input.size());
    for (std::size_t frame = 0; frame < kExcitationFramesPerMethodBlock; ++frame) {
        for (std::size_t route = 0; route < kRoutes.size(); ++route) {
            const auto index = frame * kRoutes.size() + route;
            gauge[index] = input[index] - kRoutes[route].reference_pressure_pa;
        }
    }
    CausalReconstruction reconstruction{kRoutes.size()};
    std::vector<double> reconstructed(actual.size());
    reconstruction.process(gauge, kExcitationFramesPerMethodBlock, reconstructed);
    constexpr double time_constant_s =
        1.0 / (2.0 * dsp::kSourceConditioningPi * kIntakePressureDcRemovalCutoffHz);
    std::array dc_removers{
        dsp::DcRemoval{dsp::kConditionedSourceTimeStepS, time_constant_s},
        dsp::DcRemoval{dsp::kConditionedSourceTimeStepS, time_constant_s},
    };

    bool found_nonzero = false;
    for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
        for (std::size_t route = 0; route < kRoutes.size(); ++route) {
            const auto index = frame * kRoutes.size() + route;
            const double expected = dc_removers[route].process(reconstructed[index]) *
                                    kRoutes[route].source_gain_linear;
            expect(std::isfinite(actual[index]) &&
                       bits(actual[index]) == bits(expected),
                   "intake-pressure stage changed subtraction, reconstruction, "
                   "10 Hz DC removal, or gain order");
            found_nonzero = found_nonzero || actual[index] != 0.0;
        }
        expect(bits(actual[frame * kRoutes.size() + 1U]) ==
                   bits(actual[frame * kRoutes.size()] * 2.0),
               "authored intake source gain lost positional route ownership");
    }
    expect(found_nonzero,
           "finite varying plenum pressure produced an entirely silent source");
}

void test_dc_decay_and_block_chronology() {
    constexpr std::array routes{
        IntakePressureSourceRouteConfiguration{contract::RouteId{9}, 101325.0, 1.0}};
    constexpr std::array route_ids{routes[0].id};
    IntakePressureSourceStage stage{make_configuration(routes)};
    std::vector<double> input(kExcitationFramesPerMethodBlock, 101425.0);
    std::vector<double> output(kSourceFramesPerMethodBlock);

    double first_peak = 0.0;
    double last_peak = 0.0;
    constexpr std::size_t kBlockCount = 12U;
    for (std::size_t block_index = 0; block_index < kBlockCount; ++block_index) {
        const auto view = stage.process(
            make_view(block_index * kExcitationFramesPerMethodBlock, input, route_ids),
            output);
        expect(view.first_input_frame_index() ==
                       block_index * kExcitationFramesPerMethodBlock &&
                   view.first_source_frame_index() ==
                       block_index * kSourceFramesPerMethodBlock,
               "intake-pressure block chronology became discontinuous");
        const double peak =
            std::ranges::max(output, {}, [](double value) { return std::abs(value); });
        if (block_index == 0U) {
            first_peak = peak;
        }
        if (block_index + 1U == kBlockCount) {
            last_peak = peak;
        }
    }
    expect(first_peak > 1.0 && std::isfinite(last_peak) &&
               last_peak < first_peak * 1.0e-4,
           "fixed 10 Hz DC removal did not decay a constant gauge pressure");

    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(stage.process(make_view(0, input, route_ids), output));
        },
        "intake-pressure stage accepted a repeated input chronology");
    expect(!stage.terminal_failed(),
           "chronology rejection incorrectly made the stage terminal");
}

void test_configuration_and_structural_validation() {
    constexpr std::array<IntakePressureSourceRouteConfiguration, 0> no_routes{};
    expect_throw<std::invalid_argument>(
        [&] { IntakePressureSourceStage invalid{make_configuration(no_routes)}; },
        "intake-pressure stage accepted zero routes");
    expect_throw<std::invalid_argument>(
        [&] {
            IntakePressureSourceStage invalid{
                make_configuration(kRoutes, contract::RationalRateHz{10000, 1})};
        },
        "intake-pressure stage accepted the retired 10 kHz clock");
    expect_throw<std::invalid_argument>(
        [&] {
            IntakePressureSourceStage invalid{
                make_configuration(kRoutes, kIntakePressureInputRateHz,
                                   contract::RationalRateHz{44100, 1})};
        },
        "intake-pressure stage accepted a noncanonical source clock");
    expect_throw<std::invalid_argument>(
        [&] {
            IntakePressureSourceStage invalid{make_configuration(
                kRoutes, kIntakePressureInputRateHz, kIntakePressureSourceRateHz,
                kExcitationFramesPerMethodBlock - 1U)};
        },
        "intake-pressure stage accepted a partial configured block");

    auto invalid_id = kRoutes;
    invalid_id[0].id = {};
    expect_throw<std::invalid_argument>(
        [&] { IntakePressureSourceStage invalid{make_configuration(invalid_id)}; },
        "intake-pressure stage accepted an invalid route ID");
    auto duplicate_id = kRoutes;
    duplicate_id[1].id = duplicate_id[0].id;
    expect_throw<std::invalid_argument>(
        [&] { IntakePressureSourceStage invalid{make_configuration(duplicate_id)}; },
        "intake-pressure stage accepted duplicate route IDs");
    auto invalid_reference = kRoutes;
    invalid_reference[0].reference_pressure_pa = 0.0;
    expect_throw<std::invalid_argument>(
        [&] {
            IntakePressureSourceStage invalid{make_configuration(invalid_reference)};
        },
        "intake-pressure stage accepted a zero reference pressure");
    auto invalid_gain = kRoutes;
    invalid_gain[0].source_gain_linear = -0.0;
    expect_throw<std::invalid_argument>(
        [&] { IntakePressureSourceStage invalid{make_configuration(invalid_gain)}; },
        "intake-pressure stage accepted a negative-zero source gain");

    IntakePressureSourceStage stage{make_configuration()};
    std::vector<double> valid(kExcitationFramesPerMethodBlock * kRoutes.size());
    std::vector<double> output(kSourceFramesPerMethodBlock * kRoutes.size());
    fill_matched_pressure_block(valid, 0);

    auto swapped_ids = kRouteIds;
    std::swap(swapped_ids[0], swapped_ids[1]);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(stage.process(make_view(0, valid, swapped_ids), output));
        },
        "intake-pressure stage accepted swapped route order");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(stage.process(
                make_view(0, valid, kRouteIds, kExcitationFramesPerMethodBlock,
                          contract::RationalRateHz{10000, 1}),
                output));
        },
        "intake-pressure stage accepted a retired input-view clock");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(stage.process(make_view(1, valid, kRouteIds), output));
        },
        "intake-pressure stage accepted a discontinuous first frame");

    std::vector<double> partial((kExcitationFramesPerMethodBlock - 1U) * kRoutes.size(),
                                101325.0);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(stage.process(
                make_view(0, partial, kRouteIds, kExcitationFramesPerMethodBlock - 1U),
                output));
        },
        "intake-pressure stage accepted a partial input block");
    auto incomplete = valid;
    incomplete.pop_back();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(
                stage.process(make_view(0, incomplete, kRouteIds), output));
        },
        "intake-pressure stage accepted an incomplete route matrix");
    std::vector<double> short_output(output.size() - 1U);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(
                stage.process(make_view(0, valid, kRouteIds), short_output));
        },
        "intake-pressure stage accepted a short output matrix");
    auto nonfinite = valid;
    nonfinite[17] = std::numeric_limits<double>::infinity();
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                stage.process(make_view(0, nonfinite, kRouteIds), output));
        },
        "intake-pressure stage accepted non-finite absolute pressure");
    auto negative = valid;
    negative[19] = -1.0;
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(stage.process(make_view(0, negative, kRouteIds), output));
        },
        "intake-pressure stage accepted negative absolute pressure");

    expect(stage.next_input_frame_index() == 0U &&
               stage.next_source_frame_index() == 0U && !stage.terminal_failed(),
           "structural intake-pressure rejection mutated stage state");
    static_cast<void>(stage.process(make_view(0, valid, kRouteIds), output));
}

void test_terminal_arithmetic_failure() {
    constexpr std::array routes{IntakePressureSourceRouteConfiguration{
        contract::RouteId{3}, 101325.0, std::numeric_limits<double>::max()}};
    constexpr std::array route_ids{routes[0].id};
    IntakePressureSourceStage stage{make_configuration(routes)};
    std::vector<double> input(kExcitationFramesPerMethodBlock, 101425.0);
    std::vector<double> output(kSourceFramesPerMethodBlock);

    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(stage.process(make_view(0, input, route_ids), output));
        },
        "overflowing intake-pressure source gain was not rejected");
    expect(stage.terminal_failed(),
           "partial intake-pressure arithmetic failure was not terminal");
    expect_throw<std::logic_error>(
        [&] {
            static_cast<void>(stage.process(make_view(0, input, route_ids), output));
        },
        "terminal intake-pressure source stage resumed processing");
}

class RejectAllocations final {
  public:
    RejectAllocations() {
        allocation_probe::reject_allocations = true;
    }

    ~RejectAllocations() {
        allocation_probe::reject_allocations = false;
    }

    RejectAllocations(const RejectAllocations &) = delete;
    RejectAllocations &operator=(const RejectAllocations &) = delete;
};

void test_process_is_allocation_free() {
    IntakePressureSourceStage stage{make_configuration()};
    std::vector<double> input(kExcitationFramesPerMethodBlock * kRoutes.size());
    std::vector<double> output(kSourceFramesPerMethodBlock * kRoutes.size());
    fill_matched_pressure_block(input, 0);

    const IntakePressureSourceBlockView block = [&] {
        RejectAllocations guard;
        return stage.process(make_view(0, input, kRouteIds), output);
    }();
    expect(block.frame_count() == kSourceFramesPerMethodBlock &&
               !stage.terminal_failed(),
           "allocation-free intake-pressure process did not complete");
}

void run_tests() {
    static_assert(kIntakePressureInputRateHz == contract::RationalRateHz{20000, 1});
    static_assert(kIntakePressureSourceRateHz == contract::RationalRateHz{192000, 1});
    static_assert(kIntakePressureDcRemovalCutoffHz == 10.0);
    test_exact_pressure_pipeline_and_gain();
    test_dc_decay_and_block_chronology();
    test_configuration_and_structural_validation();
    test_terminal_arithmetic_failure();
    test_process_is_allocation_free();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "intake-pressure source-stage test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
