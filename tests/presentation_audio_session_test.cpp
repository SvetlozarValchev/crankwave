#include "presentation/presentation_audio_session.hpp"

#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <array>
#include <bit>
#include <cmath>
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
constexpr std::array<double, 3> kReferenceMassFlowKgS{1.0, 1.0, 1.0};

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
            contract::SourceRouteKind::exhaust_outlet,
            kSeeds[route],
            kReferenceMassFlowKgS[route],
            kernel,
            kWetMixes[route],
            std::nullopt,
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
    contract::RationalRateHz rate = kExcitationRateHz;
    std::size_t frame_count = kExcitationFramesPerMethodBlock;
    std::vector<double> values;
    std::vector<double> absolute_exhaust_valve_mass_flow_kg_s;

    explicit Excitation(
        contract::RationalRateHz configured_rate = kExcitationRateHz,
        std::size_t configured_frame_count = kExcitationFramesPerMethodBlock)
        : rate(configured_rate), frame_count(configured_frame_count),
          values(frame_count * kRouteIds.size()),
          absolute_exhaust_valve_mass_flow_kg_s(frame_count * kRouteIds.size()) {}
};

void fill_excitation(Excitation &excitation, std::uint64_t block_ordinal) {
    for (std::size_t frame = 0; frame < excitation.frame_count; ++frame) {
        const auto global = block_ordinal * excitation.frame_count + frame;
        for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
            const auto code =
                static_cast<std::int64_t>((global + 3) * (route + 5) % 37) - 18;
            excitation.values[frame * kRouteIds.size() + route] =
                static_cast<double>(code) * 0.0625;
            excitation.absolute_exhaust_valve_mass_flow_kg_s[frame * kRouteIds.size() +
                                                             route] =
                0.05 * static_cast<double>(route + 1U) *
                static_cast<double>((global % 5U) + 1U);
        }
    }
}

[[nodiscard]] ExhaustExcitationBlockView make_view(Excitation &excitation,
                                                   std::uint64_t first_frame_index) {
    return ExhaustExcitationBlockView::borrow_for_callback(
        first_frame_index, excitation.rate, excitation.route_ids,
        excitation.frame_count, excitation.values,
        excitation.absolute_exhaust_valve_mass_flow_kg_s);
}

struct IntakePressure {
    std::vector<contract::RouteId> route_ids;
    contract::RationalRateHz rate = kIntakePressureInputRateHz;
    std::size_t frame_count = kExcitationFramesPerMethodBlock;
    std::vector<double> values;
};

[[nodiscard]] IntakePressureInputBlockView
make_intake_view(IntakePressure &intake, std::uint64_t first_frame_index) {
    return IntakePressureInputBlockView::borrow_for_callback(
        first_frame_index, intake.rate, intake.route_ids, intake.frame_count,
        intake.values);
}

[[nodiscard]] PresentationAudioBlockView
process_exhaust_only(PresentationAudioSession &session, Excitation &excitation,
                     std::uint64_t first_frame_index) {
    IntakePressure intake;
    return session.process(make_view(excitation, first_frame_index),
                           make_intake_view(intake, first_frame_index));
}

void expect_exact_float(float actual, float expected, const char *message) {
    expect(bits(actual) == bits(expected), message);
}

void test_exact_dynamic_route_pipeline() {
    const auto plan = make_plan();
    PresentationAudioSession session{plan};

    Excitation excitation;
    fill_excitation(excitation, 0);
    const auto actual = process_exhaust_only(session, excitation, 0);

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
    ExhaustSourceStage source{kRouteIds, kSeeds, kReferenceMassFlowKgS, kConditioning};
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
            actual.route_stem(route, PresentationAudioStemRole::configured_transfer);
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
        return process_exhaust_only(session, excitation, 0);
    }();

    expect(block.frame_count() == kSourceFramesPerMethodBlock &&
               session.next_input_frame_index() == kExcitationFramesPerMethodBlock &&
               session.next_source_frame_index() == kSourceFramesPerMethodBlock &&
               !session.terminal_failed(),
           "allocation-free presentation process did not complete");
}

void test_active_intake_preserves_exhaust_and_enters_master_once() {
    auto baseline_plan = make_plan();
    auto mixed_plan = make_plan();
    const contract::RouteId intake_id{99U};
    mixed_plan.routes.insert(mixed_plan.routes.begin() + 1,
                             PresentationAudioRoutePlan{
                                 intake_id,
                                 contract::SourceRouteKind::intake_inlet,
                                 std::nullopt,
                                 std::nullopt,
                                 nullptr,
                                 +0.0,
                                 IntakePressureSourceRouteConfiguration{
                                     intake_id,
                                     101325.0,
                                     1.5,
                                 },
                             });
    mixed_plan.audition_route_ids.push_back(intake_id);
    PresentationAudioSession baseline{std::move(baseline_plan)};
    PresentationAudioSession mixed{std::move(mixed_plan)};
    const std::array<std::size_t, 3> mixed_exhaust_indices{0U, 2U, 3U};
    constexpr std::array roles{
        PresentationAudioStemRole::dry,
        PresentationAudioStemRole::configured_transfer,
        PresentationAudioStemRole::selected,
    };

    Excitation excitation;
    IntakePressure empty_intake;
    IntakePressure active_intake;
    active_intake.route_ids = {intake_id};
    active_intake.values.resize(active_intake.frame_count, 101325.0);
    for (std::uint64_t block_ordinal = 0; block_ordinal < 2U; ++block_ordinal) {
        fill_excitation(excitation, block_ordinal);
        for (std::size_t frame = 0; frame < active_intake.frame_count; ++frame) {
            const auto global = block_ordinal * active_intake.frame_count + frame;
            const auto code = static_cast<std::int64_t>((global * 13U) % 101U) - 50;
            active_intake.values[frame] = 101325.0 + static_cast<double>(code) * 4.0;
        }
        const auto first =
            block_ordinal * static_cast<std::uint64_t>(excitation.frame_count);
        const auto baseline_block = baseline.process(
            make_view(excitation, first), make_intake_view(empty_intake, first));
        const auto mixed_block = mixed.process(make_view(excitation, first),
                                               make_intake_view(active_intake, first));

        expect(mixed_block.route_count() == 4U &&
                   mixed_block.route_ids()[1] == intake_id &&
                   mixed_block.audition_route_ids().back() == intake_id,
               "mixed presentation lost the interleaved intake route");
        for (std::size_t route = 0; route < kRouteIds.size(); ++route) {
            for (const auto role : roles) {
                const auto baseline_stem = baseline_block.route_stem(route, role);
                const auto mixed_stem =
                    mixed_block.route_stem(mixed_exhaust_indices[route], role);
                for (std::size_t frame = 0; frame < baseline_stem.size(); ++frame) {
                    expect(bits(mixed_stem[frame]) == bits(baseline_stem[frame]),
                           "silent intake changed an exhaust stem bit pattern");
                }
            }
        }
        const auto intake_dry =
            mixed_block.route_stem(1U, PresentationAudioStemRole::dry);
        const auto intake_transfer =
            mixed_block.route_stem(1U, PresentationAudioStemRole::configured_transfer);
        const auto intake_selected =
            mixed_block.route_stem(1U, PresentationAudioStemRole::selected);
        bool nonzero_intake = false;
        for (std::size_t frame = 0; frame < baseline_block.frame_count(); ++frame) {
            expect(bits(intake_transfer[frame]) == bits(intake_dry[frame]) &&
                       bits(intake_selected[frame]) == bits(intake_dry[frame]),
                   "intake identity transfer changed its dry sample");
            nonzero_intake = nonzero_intake || bits(intake_selected[frame]) != 0U;
            const float expected_raw =
                baseline_block.raw_master()[frame] + intake_selected[frame];
            expect_exact_float(mixed_block.raw_master()[frame], expected_raw,
                               "intake was not included exactly once in the raw "
                               "master");
            expect_exact_float(
                mixed_block.audition_master()[frame], expected_raw * kMonitoringGain,
                "intake was not included exactly once in the audition master");
        }
        expect(nonzero_intake, "active intake source produced only zero samples");
    }
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

    auto incomplete_intake = make_plan();
    incomplete_intake.routes.push_back({
        contract::RouteId{99U},
        contract::SourceRouteKind::intake_inlet,
        std::nullopt,
        std::nullopt,
        nullptr,
        +0.0,
        std::nullopt,
    });
    expect_throw<std::invalid_argument>(
        [&] { PresentationAudioSession rejected{std::move(incomplete_intake)}; },
        "presentation audio admitted an intake without an active source");

    auto wrong_canonical_extent = make_plan();
    wrong_canonical_extent.excitation_frames_per_block =
        kExcitationFramesPerMethodBlock / 2U;
    expect_throw<std::invalid_argument>(
        [&] { PresentationAudioSession rejected{std::move(wrong_canonical_extent)}; },
        "presentation audio accepted a partial canonical input block");

    auto retired_rate = make_plan();
    retired_rate.excitation_rate = {10000U, 1U};
    retired_rate.excitation_frames_per_block = 200U;
    expect_throw<std::invalid_argument>(
        [&] { PresentationAudioSession rejected{std::move(retired_rate)}; },
        "presentation audio accepted the retired 10 kHz method quantum");

    PresentationAudioSession session{make_plan()};
    Excitation excitation;
    fill_excitation(excitation, 0);
    std::swap(excitation.route_ids[0], excitation.route_ids[1]);
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(process_exhaust_only(session, excitation, 0)); },
        "presentation audio accepted reordered excitation routes");
    expect(session.next_input_frame_index() == 0 &&
               session.next_source_frame_index() == 0 && !session.terminal_failed(),
           "structural presentation rejection advanced or poisoned the session");

    std::swap(excitation.route_ids[0], excitation.route_ids[1]);
    const auto recovered = process_exhaust_only(session, excitation, 0);
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
        test_active_intake_preserves_exhaust_and_enters_master_once();
        test_validation_and_structural_rejection();
    } catch (const std::exception &error) {
        std::cerr << "presentation audio session test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
