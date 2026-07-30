#include "presentation/presentation_audio_session.hpp"

#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool reject_allocations = false;

[[gnu::noinline]] void release_allocation(void *allocation) noexcept {
    std::free(allocation);
}

} // namespace

void *operator new(std::size_t size) {
    if (reject_allocations) {
        throw std::bad_alloc{};
    }
    if (void *allocation = std::malloc(size == 0 ? 1 : size)) {
        return allocation;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *allocation) noexcept {
    release_allocation(allocation);
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

constexpr std::array<contract::RouteId, 3> kRouteIds{
    contract::RouteId{1},
    contract::RouteId{2},
    contract::RouteId{3},
};

constexpr std::array<RouteConditioningSeeds, 3> kSeeds{
    RouteConditioningSeeds{
        {UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
        {UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
    },
    RouteConditioningSeeds{
        {UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
        {UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
    },
    RouteConditioningSeeds{
        {UINT64_C(0x88f17c7c2d60e87b), UINT64_C(0x6fd4c7602345b719)},
        {UINT64_C(0xa54ff53a5f1d36f1), UINT64_C(0x3c6ef372fe94f82b)},
    },
};

constexpr RouteConditioningCalibration kConditioning{
    0.5, 10000.0, std::bit_cast<double>(UINT64_C(0x3f847ae140000000)), 1.0, 2000.0,
};

constexpr std::array<double, 3> kWetMixes{0.0, 0.375, 1.0};
constexpr std::array<contract::RouteId, 3> kAuditionOrder{
    kRouteIds[2],
    kRouteIds[0],
    kRouteIds[1],
};
constexpr float kMonitoringGain = 128.0F;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

[[nodiscard]] std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::shared_ptr<const dsp::FixedConvolutionKernel> make_kernel() {
    std::vector<double> coefficients(dsp::FixedConvolutionKernel::coefficient_count,
                                     0.0);
    coefficients[0] = 0.75;
    coefficients[1] = -0.125;
    coefficients[2] = 0.03125;
    auto fft = std::make_shared<const dsp::FixedFftPlan>();
    return std::make_shared<const dsp::FixedConvolutionKernel>(coefficients,
                                                               std::move(fft));
}

[[nodiscard]] PresentationAudioPlan make_plan() {
    const auto kernel = make_kernel();
    std::vector<PresentationAudioRoutePlan> routes;
    routes.reserve(kRouteIds.size());
    for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
        routes.push_back({
            kRouteIds[route],
            kSeeds[route],
            kernel,
            kWetMixes[route],
        });
    }
    return {
        kConditioning,
        std::move(routes),
        dsp::kSourcePublicationCalibration,
        {kAuditionOrder.begin(), kAuditionOrder.end()},
        kMonitoringGain,
    };
}

struct Excitation {
    std::array<contract::RouteId, 3> route_ids = kRouteIds;
    std::vector<double> values =
        std::vector<double>(kExcitationFramesPerMethodBlock * kRouteIds.size());
};

void fill_excitation(Excitation &excitation, std::uint64_t block_ordinal) {
    for (std::size_t frame = 0; frame < kExcitationFramesPerMethodBlock; ++frame) {
        const auto global = block_ordinal * kExcitationFramesPerMethodBlock + frame;
        for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
            const auto code =
                static_cast<std::int64_t>((global + 3) * (route + 5) % 37) - 18;
            excitation.values[frame * kRouteIds.size() + route] =
                static_cast<double>(code) * 0.0625;
        }
    }
}

[[nodiscard]] ExhaustExcitationBlockView make_view(Excitation &excitation,
                                                   std::uint64_t first_frame_index) {
    return ExhaustExcitationBlockView::borrow_for_callback(
        first_frame_index, kExcitationRateHz, excitation.route_ids,
        kExcitationFramesPerMethodBlock, excitation.values);
}

void expect_exact_float(float actual, float expected, const char *message) {
    expect(bits(actual) == bits(expected), message);
}

void test_exact_dynamic_route_pipeline() {
    const auto plan = make_plan();
    PresentationAudioSession session{plan};

    Excitation excitation;
    fill_excitation(excitation, 0);
    const auto actual = session.process(make_view(excitation, 0));

    expect(actual.first_input_frame_index() == 0 &&
               actual.first_source_frame_index() == 0 &&
               actual.input_frame_count() == kExcitationFramesPerMethodBlock &&
               actual.frame_count() == kSourceFramesPerMethodBlock &&
               actual.sample_rate() == kPresentationAudioRateHz,
           "presentation audio returned the wrong method quantum");
    expect(std::ranges::equal(actual.route_ids(), kRouteIds) &&
               std::ranges::equal(actual.audition_route_ids(), kAuditionOrder) &&
               actual.route_count() == kRouteIds.size(),
           "presentation audio lost dynamic route order");

    std::vector<double> conditioned(kSourceFramesPerMethodBlock * kRouteIds.size());
    ExhaustSourceStage source{kRouteIds, kSeeds, kConditioning};
    const auto reference_extent = source.process(make_view(excitation, 0), conditioned);
    expect(reference_extent.first_source_frame_index ==
               actual.first_source_frame_index(),
           "presentation audio source extent differs from the source stage");

    using SourceBlock = std::array<double, kSourceFramesPerMethodBlock>;
    std::vector<SourceBlock> dry(kRouteIds.size());
    std::vector<SourceBlock> configured_ir(kRouteIds.size());
    std::vector<SourceBlock> selected(kRouteIds.size());
    std::vector<std::unique_ptr<CausalOverlapSaveConvolver>> convolvers;
    convolvers.reserve(kRouteIds.size());
    for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
        convolvers.push_back(std::make_unique<CausalOverlapSaveConvolver>(
            plan.routes[route].configured_ir));
        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            dry[route][frame] = conditioned[frame * kRouteIds.size() + route];
        }
        convolvers.back()->process(dry[route], configured_ir[route]);
        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            selected[route][frame] = kWetMixes[route] * configured_ir[route][frame] +
                                     (1.0 - kWetMixes[route]) * dry[route][frame];
        }
    }

    for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
        const auto actual_dry =
            actual.route_stem(route, PresentationAudioStemRole::dry);
        const auto actual_ir =
            actual.route_stem(route, PresentationAudioStemRole::configured_ir);
        const auto actual_selected =
            actual.route_stem(route, PresentationAudioStemRole::selected);
        expect(actual_dry.size() == kSourceFramesPerMethodBlock &&
                   actual_ir.size() == kSourceFramesPerMethodBlock &&
                   actual_selected.size() == kSourceFramesPerMethodBlock,
               "presentation audio stem length changed");
        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            expect_exact_float(
                actual_dry[frame],
                dsp::publish_calibrated_float32(
                    dry[route][frame], plan.publication_calibration_gain_linear),
                "presentation audio dry stem arithmetic changed");
            expect_exact_float(
                actual_ir[frame],
                dsp::publish_calibrated_float32(
                    configured_ir[route][frame],
                    plan.publication_calibration_gain_linear),
                "presentation audio configured-IR stem arithmetic changed");
            expect_exact_float(
                actual_selected[frame],
                dsp::publish_calibrated_float32(
                    selected[route][frame], plan.publication_calibration_gain_linear),
                "presentation audio selected stem arithmetic changed");
        }
    }

    const auto raw = actual.raw_master();
    const auto audition = actual.audition_master();
    for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
        float expected_raw =
            actual.route_stem(2, PresentationAudioStemRole::selected)[frame];
        expected_raw = expected_raw +
                       actual.route_stem(0, PresentationAudioStemRole::selected)[frame];
        expected_raw = expected_raw +
                       actual.route_stem(1, PresentationAudioStemRole::selected)[frame];
        expect_exact_float(raw[frame], expected_raw,
                           "presentation audio raw reduction order changed");
        expect_exact_float(audition[frame], expected_raw * kMonitoringGain,
                           "presentation audio monitor arithmetic changed");
    }
}

class RejectAllocations final {
  public:
    RejectAllocations() {
        reject_allocations = true;
    }

    ~RejectAllocations() {
        reject_allocations = false;
    }

    RejectAllocations(const RejectAllocations &) = delete;
    RejectAllocations &operator=(const RejectAllocations &) = delete;
};

void test_process_is_allocation_free() {
    PresentationAudioSession session{make_plan()};
    Excitation excitation;
    fill_excitation(excitation, 0);

    const PresentationAudioBlockView block = [&] {
        RejectAllocations guard;
        return session.process(make_view(excitation, 0));
    }();

    expect(block.frame_count() == kSourceFramesPerMethodBlock &&
               session.next_input_frame_index() == kExcitationFramesPerMethodBlock &&
               session.next_source_frame_index() == kSourceFramesPerMethodBlock &&
               !session.terminal_failed(),
           "allocation-free presentation process did not complete");
}

void test_validation_and_structural_rejection() {
    auto empty = make_plan();
    empty.routes.clear();
    empty.audition_route_ids.clear();
    expect_throw<std::invalid_argument>(
        [&] { PresentationAudioSession rejected{std::move(empty)}; },
        "presentation audio accepted an empty route plan");

    auto duplicate_audition = make_plan();
    duplicate_audition.audition_route_ids[1] = duplicate_audition.audition_route_ids[0];
    expect_throw<std::invalid_argument>(
        [&] { PresentationAudioSession rejected{std::move(duplicate_audition)}; },
        "presentation audio accepted duplicate audition routes");

    PresentationAudioSession session{make_plan()};
    Excitation excitation;
    fill_excitation(excitation, 0);
    std::swap(excitation.route_ids[0], excitation.route_ids[1]);
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(session.process(make_view(excitation, 0))); },
        "presentation audio accepted reordered excitation routes");
    expect(session.next_input_frame_index() == 0 &&
               session.next_source_frame_index() == 0 && !session.terminal_failed(),
           "structural presentation rejection advanced or poisoned the session");

    std::swap(excitation.route_ids[0], excitation.route_ids[1]);
    const auto recovered = session.process(make_view(excitation, 0));
    expect(recovered.frame_count() == kSourceFramesPerMethodBlock &&
               !session.terminal_failed(),
           "presentation audio could not recover from structural rejection");
    expect_throw<std::out_of_range>(
        [&] {
            static_cast<void>(recovered.route_stem(recovered.route_count(),
                                                   PresentationAudioStemRole::dry));
        },
        "presentation audio accepted an out-of-range route stem");
}

} // namespace

int main() {
    try {
        test_exact_dynamic_route_pipeline();
        test_process_is_allocation_free();
        test_validation_and_structural_rejection();
    } catch (const std::exception &error) {
        std::cerr << "presentation audio session test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
