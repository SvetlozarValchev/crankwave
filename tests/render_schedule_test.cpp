#include "contract_test_support.hpp"
#include "scheduling/render_schedule.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;
using namespace engine_sim_offline::scheduling;

static_assert(sizeof(ScheduleCursor) < 1024,
              "schedule cursor must retain a compact plan, not a block list");

RenderScenario make_test_scenario() {
    InputBuilder builder;
    const auto engine = make_engine(builder);
    auto scenario = make_scenario(builder, engine);
    scenario.mode = HeldSpeed{
        {3000.0, "render-schedule-held-rpm"},
        {0.0, "render-schedule-held-angle"},
        {0.85, "render-schedule-held-throttle"},
    };
    scenario.quality.value.event_journal_capacity_records = 4096;
    return scenario;
}

void set_all_rates(RenderScenario &scenario, RationalRateHz rate) {
    scenario.rates = {rate, rate, rate, rate, rate};
}

RenderSchedulePlan require_plan(ScheduleCompileResult result, const char *message) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        auto detail = std::string{message};
        if (!report->issues.empty()) {
            detail += ": " + report->issues.front().path + ": " +
                      report->issues.front().message;
        }
        throw std::runtime_error{detail};
    }
    return std::get<RenderSchedulePlan>(std::move(result));
}

const ValidationReport &require_rejection(const ScheduleCompileResult &result,
                                          const char *message) {
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr && !report->ok(), message);
    return *report;
}

bool has_issue_path(const ValidationReport &report, std::string_view path) {
    for (const auto &issue : report.issues) {
        if (issue.path == path) {
            return true;
        }
    }
    return false;
}

FrameRange expected_intersection(FrameRange frames, FrameRange audible) {
    if (frames.end <= audible.begin) {
        return {frames.end, frames.end};
    }
    if (frames.begin >= audible.end) {
        return {frames.begin, frames.begin};
    }
    return {
        frames.begin < audible.begin ? audible.begin : frames.begin,
        frames.end < audible.end ? frames.end : audible.end,
    };
}

void expect_clock_block(const ScheduledClockBlock &block, const ClockExtent &extent,
                        std::uint64_t expected_begin, std::uint64_t &audible_frames,
                        const char *message) {
    expect(block.frames.begin == expected_begin, message);
    expect(block.frames.end > block.frames.begin, message);
    expect(block.frames.frame_count() <= extent.maximum_frames_per_block, message);
    expect(block.audible == expected_intersection(block.frames, extent.audible),
           message);
    audible_frames += block.audible.frame_count();
}

SchedulePolicy generic_policy(std::uint32_t partition_frames = 255) {
    return {
        "test-fixed-block-schedule", 1, partition_frames, 64, SamplePhase::post_step, 1,
    };
}

void test_generic_partition_and_crop_continuity() {
    auto scenario = make_test_scenario();
    const auto plan = require_plan(compile_render_schedule(scenario, generic_policy()),
                                   "valid generic schedule was rejected");

    expect(plan.capture().total_frames == 30000 &&
               plan.capture().audible == FrameRange{20000, 30000},
           "generic capture horizon or crop is wrong");
    expect(plan.source_processing().total_frames == 576000 &&
               plan.source_processing().audible == FrameRange{384000, 576000},
           "generic source horizon or crop is wrong");
    expect(plan.block_count() == 118,
           "generic non-divisible capture horizon has the wrong block count");

    ScheduleCursor cursor{plan};
    std::uint64_t ordinal = 0;
    std::uint64_t previous_physics_end = 0;
    std::uint64_t previous_capture_end = 0;
    std::uint64_t previous_source_end = 0;
    std::uint64_t previous_acoustic_end = 0;
    std::uint64_t previous_delivery_end = 0;
    std::uint64_t physics_audible = 0;
    std::uint64_t capture_audible = 0;
    std::uint64_t source_audible = 0;
    std::uint64_t acoustic_audible = 0;
    std::uint64_t delivery_audible = 0;

    for (;;) {
        const auto step = cursor.next();
        if (const auto *completed = std::get_if<ScheduleCompleted>(&step)) {
            expect(completed->progress.next_block_ordinal == plan.block_count(),
                   "completed generic schedule retained the wrong block ordinal");
            expect(completed->progress.next_capture_frame ==
                       plan.capture().total_frames,
                   "completed generic schedule retained the wrong capture horizon");
            expect(cursor.next() == step,
                   "completed schedule did not remain terminal and stable");
            break;
        }

        const auto *block = std::get_if<ScheduledRenderBlock>(&step);
        expect(block != nullptr, "generic schedule cancelled unexpectedly");
        expect(block->ordinal == ordinal,
               "generic schedule block ordinals are not contiguous");
        expect(block->capture_clock.first_sample_index == block->capture.frames.begin &&
                   block->capture_clock.first_timestamp_tick ==
                       block->capture.frames.begin + 1 &&
                   block->capture_clock.phase == SamplePhase::post_step,
               "generic block capture clock violated post-step indexing");

        expect_clock_block(block->physics, plan.physics(), previous_physics_end,
                           physics_audible,
                           "generic physics blocks are discontinuous or unbounded");
        expect_clock_block(block->capture, plan.capture(), previous_capture_end,
                           capture_audible,
                           "generic capture blocks are discontinuous or unbounded");
        expect_clock_block(
            block->source_processing, plan.source_processing(), previous_source_end,
            source_audible,
            "generic source-processing blocks are discontinuous or unbounded");
        expect_clock_block(block->acoustic, plan.acoustic(), previous_acoustic_end,
                           acoustic_audible,
                           "generic acoustic blocks are discontinuous or unbounded");
        expect_clock_block(block->delivery, plan.delivery(), previous_delivery_end,
                           delivery_audible,
                           "generic delivery blocks are discontinuous or unbounded");

        previous_physics_end = block->physics.frames.end;
        previous_capture_end = block->capture.frames.end;
        previous_source_end = block->source_processing.frames.end;
        previous_acoustic_end = block->acoustic.frames.end;
        previous_delivery_end = block->delivery.frames.end;
        ++ordinal;
    }

    expect(ordinal == plan.block_count() &&
               previous_physics_end == plan.physics().total_frames &&
               previous_capture_end == plan.capture().total_frames &&
               previous_source_end == plan.source_processing().total_frames &&
               previous_acoustic_end == plan.acoustic().total_frames &&
               previous_delivery_end == plan.delivery().total_frames,
           "generic schedule did not cover every clock horizon exactly once");
    expect(physics_audible == plan.physics().audible.frame_count() &&
               capture_audible == plan.capture().audible.frame_count() &&
               source_audible == plan.source_processing().audible.frame_count() &&
               acoustic_audible == plan.acoustic().audible.frame_count() &&
               delivery_audible == plan.delivery().audible.frame_count(),
           "generic block crop intersections did not cover the audible ranges");
}

void test_scheduled_capture_block_binding() {
    const auto plan =
        require_plan(compile_render_schedule(make_test_scenario(), generic_policy()),
                     "capture-binding schedule was rejected");
    ScheduleCursor cursor{plan};
    const auto step = cursor.next();
    const auto *scheduled = std::get_if<ScheduledRenderBlock>(&step);
    expect(scheduled != nullptr,
           "capture-binding schedule did not produce its first block");

    const std::array<CylinderId, 0> cylinders;
    const std::array<PortIdentity, 0> ports;
    const std::array<GasVolumeIdentity, 0> volumes;
    const std::array<FlowEdgeIdentity, 0> edges;
    const std::array<RouteIdentity, 0> routes;
    const auto layout = CaptureLayoutView::borrow_for_callback(
        EngineId{1}, cylinders, ports, volumes, edges, routes);

    const auto frame_count =
        static_cast<std::uint32_t>(scheduled->capture.frames.frame_count());
    std::vector<EngineCaptureSample> engine_samples(frame_count);
    for (std::uint32_t index = 0; index < frame_count; ++index) {
        engine_samples[index].step_end_index =
            scheduled->capture.frames.begin + index + 1;
    }
    std::vector<CylinderCaptureSample> cylinder_samples;
    std::vector<PortCaptureSample> port_samples;
    std::vector<GasVolumeCaptureSample> volume_samples;
    std::vector<FlowEdgeCaptureSample> edge_samples;
    std::vector<SourceRouteCaptureSample> route_samples;
    std::vector<std::uint32_t> event_offsets(frame_count + 1, 0);
    std::vector<EngineEvent> events;

    const auto make_block = [&](CaptureClock clock, std::uint32_t count,
                                std::uint32_t frame_capacity,
                                std::uint32_t event_capacity, auto &engine_range,
                                auto &offset_range, auto &event_range) {
        const auto journal =
            EventJournalView::borrow_for_callback(offset_range, event_range);
        return CaptureBlockView::borrow_for_callback(
            layout, clock, count, frame_capacity, event_capacity, engine_range,
            cylinder_samples, port_samples, volume_samples, edge_samples, route_samples,
            journal);
    };

    auto full_engine_span = std::span{engine_samples};
    auto full_offset_span = std::span{event_offsets};
    const auto valid_block = make_block(scheduled->capture_clock, frame_count,
                                        plan.capture_capacity_frames(),
                                        plan.event_journal_capacity_records(),
                                        full_engine_span, full_offset_span, events);
    expect(validate_scheduled_capture_block(plan, *scheduled, valid_block).ok(),
           "exact scheduled CaptureBlockView binding was rejected");

    auto wrong_schedule_range = *scheduled;
    --wrong_schedule_range.capture.frames.end;
    expect(has_issue_path(validate_scheduled_capture_block(plan, wrong_schedule_range,
                                                           valid_block),
                          "schedule.capture.frames"),
           "capture binding accepted a range different from the plan ordinal");

    auto wrong_schedule_clock = *scheduled;
    ++wrong_schedule_clock.capture_clock.first_sample_index;
    expect(has_issue_path(validate_scheduled_capture_block(plan, wrong_schedule_clock,
                                                           valid_block),
                          "schedule.capture_clock"),
           "capture binding accepted a clock different from the plan range");

    auto short_engine_span = std::span{engine_samples}.first(frame_count - 1);
    auto short_offset_span = std::span{event_offsets}.first(frame_count);
    const auto short_block = make_block(scheduled->capture_clock, frame_count - 1,
                                        plan.capture_capacity_frames(),
                                        plan.event_journal_capacity_records(),
                                        short_engine_span, short_offset_span, events);
    expect(
        has_issue_path(validate_scheduled_capture_block(plan, *scheduled, short_block),
                       "block.frame_count"),
        "capture binding accepted a payload shorter than its scheduled range");

    const auto wrong_frame_capacity = make_block(
        scheduled->capture_clock, frame_count, plan.capture_capacity_frames() - 1,
        plan.event_journal_capacity_records(), full_engine_span, full_offset_span,
        events);
    expect(has_issue_path(
               validate_scheduled_capture_block(plan, *scheduled, wrong_frame_capacity),
               "block.declared_block_capacity_frames"),
           "capture binding accepted a different transport frame capacity");

    const auto wrong_event_capacity = make_block(
        scheduled->capture_clock, frame_count, plan.capture_capacity_frames(),
        plan.event_journal_capacity_records() - 1, full_engine_span, full_offset_span,
        events);
    expect(has_issue_path(
               validate_scheduled_capture_block(plan, *scheduled, wrong_event_capacity),
               "block.declared_event_journal_capacity_records"),
           "capture binding accepted a different event-journal capacity");

    const auto method_event_bound = plan.policy().maximum_event_records_per_block;
    std::vector<EngineEvent> excessive_events(method_event_bound + 1);
    std::vector<std::uint32_t> excessive_offsets(frame_count + 1);
    for (std::size_t index = 0; index < excessive_events.size(); ++index) {
        excessive_events[index] = {
            static_cast<std::uint32_t>(index),
            0,
            LimiterStateChanged{false, true, false, 0.0},
        };
    }
    for (std::size_t index = 0; index < excessive_offsets.size(); ++index) {
        excessive_offsets[index] =
            std::min(static_cast<std::uint32_t>(index),
                     static_cast<std::uint32_t>(excessive_events.size()));
    }
    const auto excessive_event_block = make_block(
        scheduled->capture_clock, frame_count, plan.capture_capacity_frames(),
        plan.event_journal_capacity_records(), full_engine_span, excessive_offsets,
        excessive_events);
    expect(validate(excessive_event_block).ok(),
           "method-bound test payload violated the wider transport contract");
    expect(has_issue_path(validate_scheduled_capture_block(plan, *scheduled,
                                                           excessive_event_block),
                          "block.event_journal.events"),
           "capture binding accepted an event journal beyond the method-owned bound");
}

void test_method_bounds_are_admitted_explicitly() {
    auto scenario = make_test_scenario();

    auto zero_event_bound = generic_policy();
    zero_event_bound.maximum_event_records_per_block = 0;
    const auto zero_result = compile_render_schedule(scenario, zero_event_bound);
    const auto &zero_report =
        require_rejection(zero_result, "zero method event bound compiled successfully");
    expect(has_issue_path(zero_report, "policy.maximum_event_records_per_block"),
           "zero event-bound rejection did not identify the schedule policy");

    auto oversized_event_bound = generic_policy();
    oversized_event_bound.maximum_event_records_per_block =
        scenario.quality.value.event_journal_capacity_records + 1;
    const auto oversized_result =
        compile_render_schedule(scenario, oversized_event_bound);
    const auto &oversized_report = require_rejection(
        oversized_result,
        "method event bound beyond transport capacity compiled successfully");
    expect(has_issue_path(oversized_report, "policy.maximum_event_records_per_block"),
           "event transport rejection did not identify the method-owned bound");

    const auto fractional_result =
        compile_render_schedule(scenario, generic_policy(256));
    const auto &fractional_report = require_rejection(
        fractional_result,
        "fractional cross-clock method boundary compiled successfully");
    expect(has_issue_path(fractional_report,
                          "policy.capture_partition_frames.source_processing") &&
               has_issue_path(fractional_report,
                              "policy.capture_partition_frames.acoustic") &&
               has_issue_path(fractional_report,
                              "policy.capture_partition_frames.delivery"),
           "fractional method-boundary rejection did not identify every target clock");
}

SchedulePolicy accepted_presentation_policy() {
    return {
        "fixed-rate-exhaust-presentation",
        1,
        200,
        19U * 200U,
        SamplePhase::post_step,
        1,
    };
}

RenderScenario make_accepted_presentation_scenario() {
    auto scenario = make_test_scenario();
    scenario.total_duration_s.value = 17.0;
    scenario.audible_start_s.value = 2.0;
    scenario.audible_duration_s.value = 15.0;
    scenario.quality.value.capture_block_capacity_frames = 256;
    return scenario;
}

void test_accepted_presentation_partition_and_mapping() {
    const auto scenario = make_accepted_presentation_scenario();
    const auto plan =
        require_plan(compile_render_schedule(scenario, accepted_presentation_policy()),
                     "valid accepted presentation schedule was rejected");

    expect(plan.capture_capacity_frames() == 256 &&
               plan.event_journal_capacity_records() == 4096 &&
               plan.policy().capture_partition_frames == 200 &&
               plan.block_count() == 850 &&
               plan.capture().maximum_frames_per_block == 200 &&
               plan.source_processing().maximum_frames_per_block == 3840,
           "presentation transport capacity and method-owned bounds were conflated");
    expect(plan.capture().total_frames == 170000 &&
               plan.capture().audible == FrameRange{20000, 170000},
           "presentation capture extent is wrong");
    expect(plan.source_processing().total_frames == 3264000 &&
               plan.source_processing().audible == FrameRange{384000, 3264000},
           "presentation source extent is wrong");

    ScheduleCursor cursor{plan};
    for (std::uint64_t ordinal = 0; ordinal < 850; ++ordinal) {
        const auto step = cursor.next();
        const auto *block = std::get_if<ScheduledRenderBlock>(&step);
        expect(block != nullptr && block->ordinal == ordinal,
               "presentation cursor did not return the expected block");
        expect(block->capture.frames ==
                       FrameRange{ordinal * 200, (ordinal + 1) * 200} &&
                   block->source_processing.frames ==
                       FrameRange{ordinal * 3840, (ordinal + 1) * 3840} &&
                   block->acoustic.frames == block->source_processing.frames &&
                   block->delivery.frames == block->source_processing.frames,
               "presentation 200-to-3840 block mapping drifted");
        expect(block->capture_clock.first_timestamp_tick == ordinal * 200 + 1,
               "presentation post-step capture timestamp origin drifted");

        const auto expected_capture_audible =
            ordinal < 100 ? FrameRange{(ordinal + 1) * 200, (ordinal + 1) * 200}
                          : block->capture.frames;
        const auto expected_source_audible =
            ordinal < 100 ? FrameRange{(ordinal + 1) * 3840, (ordinal + 1) * 3840}
                          : block->source_processing.frames;
        expect(block->capture.audible == expected_capture_audible &&
                   block->source_processing.audible == expected_source_audible,
               "presentation crop did not preserve 100 complete pre-roll blocks");
    }

    const auto terminal = cursor.next();
    const auto *completed = std::get_if<ScheduleCompleted>(&terminal);
    expect(completed != nullptr && completed->progress.next_capture_frame == 170000 &&
               completed->progress.next_source_processing_frame == 3264000,
           "presentation cursor did not finish at the expected horizons");

    auto insufficient_capacity = scenario;
    insufficient_capacity.quality.value.capture_block_capacity_frames = 199;
    const auto rejected =
        compile_render_schedule(insufficient_capacity, accepted_presentation_policy());
    const auto &report = require_rejection(
        rejected, "presentation partition exceeded capacity without rejection");
    expect(has_issue_path(report, "policy.capture_partition_frames"),
           "presentation capacity rejection did not identify the method partition");
}

void test_binary64_grid_conversion_and_rejection() {
    expect(resolve_frame_index(0.1, RationalRateHz{48000, 1}) ==
               std::optional<std::uint64_t>{4800},
           "ordinary binary64 duration did not resolve to its integral frame");
    expect(resolve_frame_index(1001.0 / 30000.0, RationalRateHz{30000, 1001}) ==
               std::optional<std::uint64_t>{1},
           "rational-rate binary64 representation error was not tolerated");
    expect(!resolve_frame_index(1.5 / 48000.0, RationalRateHz{48000, 1}).has_value(),
           "genuine half-frame duration was rounded onto the clock grid");

    auto scenario = make_test_scenario();
    set_all_rates(scenario, {48000, 1});
    scenario.total_duration_s.value = 0.3;
    scenario.audible_start_s.value = 0.1;
    scenario.audible_duration_s.value = 0.2;
    scenario.quality.value.capture_block_capacity_frames = 257;
    scenario.preparation = FixedSettling{
        {0.05, ""},
        {0.05, ""},
    };
    const auto plan =
        require_plan(compile_render_schedule(scenario, generic_policy(257)),
                     "integral 48 kHz binary64 schedule was rejected");
    expect(plan.capture().total_frames == 14400 &&
               plan.capture().audible == FrameRange{4800, 14400},
           "48 kHz binary64 schedule resolved the wrong indices");

    scenario.audible_duration_s.value = 1.5 / 48000.0;
    require_rejection(compile_render_schedule(scenario, generic_policy(257)),
                      "nonintegral audible duration compiled successfully");
}

RenderSchedulePlan make_single_block_plan() {
    auto scenario = make_test_scenario();
    set_all_rates(scenario, {10, 1});
    scenario.total_duration_s.value = 1.0;
    scenario.audible_start_s.value = 0.0;
    scenario.audible_duration_s.value = 1.0;
    scenario.preparation = FixedSettling{
        {0.0, ""},
        {0.0, ""},
    };
    return require_plan(compile_render_schedule(scenario, generic_policy(16)),
                        "single-block cancellation schedule was rejected");
}

void test_cancellation_boundaries_and_final_poll() {
    const auto plan =
        require_plan(compile_render_schedule(make_test_scenario(), generic_policy()),
                     "cancellation test schedule was rejected");

    {
        ScheduleCursor cursor{plan};
        std::stop_source stop;
        stop.request_stop();
        const auto step = cursor.next(stop.get_token());
        const auto *cancelled = std::get_if<ScheduleCancelled>(&step);
        expect(cancelled != nullptr && cancelled->progress == ScheduleProgress{},
               "pre-cancelled cursor consumed schedule work");
        expect(cursor.next() == step,
               "cancelled cursor did not remain terminal and stable");
    }

    {
        ScheduleCursor cursor{plan};
        const auto first = cursor.next();
        expect(std::holds_alternative<ScheduledRenderBlock>(first),
               "active cursor did not produce its first complete block");
        std::stop_source stop;
        stop.request_stop();
        const auto step = cursor.next(stop.get_token());
        const auto *cancelled = std::get_if<ScheduleCancelled>(&step);
        expect(cancelled != nullptr && cancelled->progress.next_block_ordinal == 1 &&
                   cancelled->progress.next_capture_frame == 255,
               "between-block cancellation retained the wrong progress boundary");
    }

    {
        const auto single_block = make_single_block_plan();
        ScheduleCursor cursor{single_block};
        const auto block = cursor.next();
        expect(std::holds_alternative<ScheduledRenderBlock>(block),
               "single-block cursor did not produce its block");
        std::stop_source stop;
        stop.request_stop();
        const auto step = cursor.next(stop.get_token());
        const auto *cancelled = std::get_if<ScheduleCancelled>(&step);
        expect(cancelled != nullptr && cancelled->progress.next_capture_frame == 10,
               "final cancellation poll was skipped after the last block");

        ScheduleCursor completed_cursor{single_block};
        static_cast<void>(completed_cursor.next());
        const auto completed = completed_cursor.next();
        expect(std::holds_alternative<ScheduleCompleted>(completed) &&
                   completed_cursor.next() == completed,
               "uncancelled final poll did not complete stably");
    }
}

void test_interleaved_cursors_are_session_local() {
    const auto plan =
        require_plan(compile_render_schedule(make_test_scenario(), generic_policy()),
                     "interleaving test schedule was rejected");
    ScheduleCursor first{plan};
    ScheduleCursor second{plan};

    const auto first_0 = first.next();
    const auto first_1 = first.next();
    const auto second_0 = second.next();
    const auto second_1 = second.next();
    expect(first_0 == second_0 && first_1 == second_1,
           "independent cursors shared or leaked progress");

    for (;;) {
        const auto first_step = first.next();
        const auto second_step = second.next();
        expect(first_step == second_step && first.progress() == second.progress(),
               "interleaved cursor traces diverged");
        if (std::holds_alternative<ScheduleCompleted>(first_step)) {
            break;
        }
    }
}

void test_cursor_does_not_materialize_duration() {
    auto scenario = make_test_scenario();
    set_all_rates(scenario, {1, 1});
    scenario.total_duration_s.value = 1000000000000.0;
    scenario.audible_start_s.value = 2.0;
    scenario.audible_duration_s.value = 999999999998.0;

    const auto plan = require_plan(compile_render_schedule(scenario, generic_policy()),
                                   "large compact schedule was rejected");
    expect(plan.capture().total_frames == UINT64_C(1000000000000) &&
               plan.block_count() > UINT64_C(3000000000),
           "large compact schedule retained the wrong horizon");

    ScheduleCursor cursor{plan};
    const auto first = cursor.next();
    const auto *block = std::get_if<ScheduledRenderBlock>(&first);
    expect(block != nullptr && block->capture.frames == FrameRange{0, 255},
           "large compact cursor did not calculate its first block directly");

    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = cursor.next(stop.get_token());
    expect(std::holds_alternative<ScheduleCancelled>(cancelled),
           "large compact cursor could not cancel without traversing its horizon");
}

void run_tests() {
    test_generic_partition_and_crop_continuity();
    test_scheduled_capture_block_binding();
    test_method_bounds_are_admitted_explicitly();
    test_accepted_presentation_partition_and_mapping();
    test_binary64_grid_conversion_and_rejection();
    test_cancellation_boundaries_and_final_poll();
    test_interleaved_cursors_are_session_local();
    test_cursor_does_not_materialize_duration();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "render schedule test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
