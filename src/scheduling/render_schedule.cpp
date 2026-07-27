#include "scheduling/render_schedule.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace engine_sim_offline::scheduling {
namespace {

using contract::ContractIssueCode;
using contract::RationalRateHz;
using contract::ValidationReport;

struct DivisionResult {
    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
};

struct RateRatio {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 0;
};

bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool checked_multiply(std::uint64_t lhs, std::uint64_t rhs,
                      std::uint64_t &result) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

std::optional<DivisionResult> multiply_divide(std::uint64_t lhs, std::uint64_t rhs,
                                              std::uint64_t divisor) noexcept {
    if (divisor == 0) {
        return std::nullopt;
    }

    std::uint64_t whole = 0;
    if (!checked_multiply(lhs / divisor, rhs, whole)) {
        return std::nullopt;
    }

    const auto fractional_lhs = lhs % divisor;
    std::uint64_t fractional_quotient = 0;
    std::uint64_t remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        if (fractional_quotient > std::numeric_limits<std::uint64_t>::max() / 2U) {
            return std::nullopt;
        }
        fractional_quotient *= 2U;

        if (remainder >= divisor - remainder) {
            remainder -= divisor - remainder;
            if (fractional_quotient == std::numeric_limits<std::uint64_t>::max()) {
                return std::nullopt;
            }
            ++fractional_quotient;
        } else {
            remainder += remainder;
        }

        if (((rhs >> static_cast<unsigned>(bit)) & UINT64_C(1)) == 0 ||
            fractional_lhs == 0) {
            continue;
        }
        if (remainder >= divisor - fractional_lhs) {
            remainder -= divisor - fractional_lhs;
            if (fractional_quotient == std::numeric_limits<std::uint64_t>::max()) {
                return std::nullopt;
            }
            ++fractional_quotient;
        } else {
            remainder += fractional_lhs;
        }
    }

    std::uint64_t quotient = 0;
    if (!checked_add(whole, fractional_quotient, quotient)) {
        return std::nullopt;
    }
    return DivisionResult{quotient, remainder};
}

void cancel_factor(std::uint64_t &numerator, std::uint64_t &denominator) noexcept {
    const auto factor = std::gcd(numerator, denominator);
    numerator /= factor;
    denominator /= factor;
}

std::optional<RateRatio> rate_ratio(const RationalRateHz &target,
                                    const RationalRateHz &capture) noexcept {
    if (target.numerator == 0 || target.denominator == 0 || capture.numerator == 0 ||
        capture.denominator == 0) {
        return std::nullopt;
    }

    auto numerator_0 = target.numerator;
    auto numerator_1 = capture.denominator;
    auto denominator_0 = target.denominator;
    auto denominator_1 = capture.numerator;
    cancel_factor(numerator_0, denominator_0);
    cancel_factor(numerator_0, denominator_1);
    cancel_factor(numerator_1, denominator_0);
    cancel_factor(numerator_1, denominator_1);

    RateRatio ratio;
    if (!checked_multiply(numerator_0, numerator_1, ratio.numerator) ||
        !checked_multiply(denominator_0, denominator_1, ratio.denominator) ||
        ratio.numerator == 0 || ratio.denominator == 0) {
        return std::nullopt;
    }
    return ratio;
}

std::optional<DivisionResult>
project_capture_frame(std::uint64_t capture_frame, const RationalRateHz &target_rate,
                      const RationalRateHz &capture_rate) noexcept {
    const auto ratio = rate_ratio(target_rate, capture_rate);
    if (!ratio.has_value()) {
        return std::nullopt;
    }
    return multiply_divide(capture_frame, ratio->numerator, ratio->denominator);
}

FrameRange intersection(FrameRange frames, FrameRange audible) noexcept {
    if (frames.end <= audible.begin) {
        return {frames.end, frames.end};
    }
    if (frames.begin >= audible.end) {
        return {frames.begin, frames.begin};
    }
    return {
        std::max(frames.begin, audible.begin),
        std::min(frames.end, audible.end),
    };
}

void require(ValidationReport &report, bool condition, std::string path,
             std::string message) {
    if (!condition) {
        report.add(ContractIssueCode::inconsistent_semantics, std::move(path),
                   std::move(message));
    }
}

void append_prefixed(ValidationReport &destination, ValidationReport source,
                     std::string_view prefix) {
    for (auto &issue : source.issues) {
        issue.path = issue.path.empty() ? std::string(prefix)
                                        : std::string(prefix) + "." + issue.path;
        destination.issues.push_back(std::move(issue));
    }
}

std::optional<ClockExtent> compile_clock(ValidationReport &report,
                                         std::string_view name,
                                         const RationalRateHz &rate,
                                         const RationalRateHz &capture_rate,
                                         const contract::RenderScenario &scenario,
                                         std::uint32_t capture_partition_frames) {
    const auto path = std::string{name};
    const auto total =
        contract::resolve_frame_index(scenario.total_duration_s.value, rate);
    const auto audible_begin =
        contract::resolve_frame_index(scenario.audible_start_s.value, rate);
    const auto audible_count =
        contract::resolve_frame_index(scenario.audible_duration_s.value, rate);
    require(report, total.has_value() && *total > 0, path + ".total_frames",
            "clock total duration must resolve to a positive integral frame count");
    require(report, audible_begin.has_value(), path + ".audible.begin",
            "clock audible start must resolve to an integral frame index");
    require(report, audible_count.has_value() && *audible_count > 0,
            path + ".audible.frame_count",
            "clock audible duration must resolve to a positive integral frame count");
    if (!total.has_value() || !audible_begin.has_value() ||
        !audible_count.has_value() || *total == 0 || *audible_count == 0) {
        return std::nullopt;
    }

    std::uint64_t audible_end = 0;
    const auto audible_representable =
        checked_add(*audible_begin, *audible_count, audible_end);
    require(report, audible_representable && audible_end <= *total, path + ".audible",
            "clock audible interval must fit inside its total frame horizon");
    if (!audible_representable || audible_end > *total) {
        return std::nullopt;
    }

    const auto ratio = rate_ratio(rate, capture_rate);
    require(report, ratio.has_value(), path + ".rate",
            "clock-to-capture ratio exceeds the scheduler's integer representation");
    if (!ratio.has_value()) {
        return std::nullopt;
    }
    const auto maximum_block =
        multiply_divide(capture_partition_frames, ratio->numerator, ratio->denominator);
    require(report, maximum_block.has_value(), path + ".maximum_frames_per_block",
            "clock block bound exceeds the scheduler's integer representation");
    if (!maximum_block.has_value()) {
        return std::nullopt;
    }
    auto maximum_frames = maximum_block->quotient;
    if (maximum_block->remainder != 0) {
        require(report, maximum_frames < std::numeric_limits<std::uint64_t>::max(),
                path + ".maximum_frames_per_block",
                "clock block ceiling exceeds uint64");
        if (maximum_frames == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        ++maximum_frames;
    }

    const auto capture_total =
        contract::resolve_frame_index(scenario.total_duration_s.value, capture_rate);
    require(report, capture_total.has_value() && *capture_total > 0,
            path + ".capture_horizon",
            "capture horizon must resolve before cross-clock projection");
    if (!capture_total.has_value() || *capture_total == 0) {
        return std::nullopt;
    }
    const auto projected_total =
        project_capture_frame(*capture_total, rate, capture_rate);
    require(report,
            projected_total.has_value() && projected_total->remainder == 0 &&
                projected_total->quotient == *total,
            path + ".total_frames",
            "clock horizon must map exactly from the capture horizon");
    if (!projected_total.has_value() || projected_total->remainder != 0 ||
        projected_total->quotient != *total) {
        return std::nullopt;
    }

    return ClockExtent{
        rate,
        *total,
        {*audible_begin, audible_end},
        maximum_frames,
    };
}

ScheduledClockBlock schedule_clock_block(FrameRange capture_frames,
                                         const ClockExtent &clock,
                                         const RationalRateHz &capture_rate) noexcept {
    const auto begin =
        project_capture_frame(capture_frames.begin, clock.rate, capture_rate);
    const auto end =
        project_capture_frame(capture_frames.end, clock.rate, capture_rate);
    // Plan compilation proves every projection through the total horizon is
    // representable. The fallback keeps this noexcept and fail-closed for corrupted
    // in-process state without inventing indices.
    if (!begin.has_value() || !end.has_value()) {
        return {};
    }
    const FrameRange frames{begin->quotient, end->quotient};
    return {frames, intersection(frames, clock.audible)};
}

bool exact_extent(const ClockExtent &extent, std::uint64_t total,
                  std::uint64_t audible_begin, std::uint64_t audible_end,
                  std::uint64_t maximum_block) noexcept {
    return extent.total_frames == total &&
           extent.audible == FrameRange{audible_begin, audible_end} &&
           extent.maximum_frames_per_block == maximum_block;
}

} // namespace

RenderSchedulePlan::RenderSchedulePlan(SchedulePolicy policy, ClockExtent physics,
                                       ClockExtent capture,
                                       ClockExtent source_processing,
                                       ClockExtent acoustic, ClockExtent delivery,
                                       std::uint32_t capture_capacity_frames,
                                       std::uint32_t event_journal_capacity_records,
                                       std::uint64_t block_count) noexcept
    : policy_(std::move(policy)), physics_(physics), capture_(capture),
      source_processing_(source_processing), acoustic_(acoustic), delivery_(delivery),
      capture_capacity_frames_(capture_capacity_frames),
      event_journal_capacity_records_(event_journal_capacity_records),
      block_count_(block_count) {}

const SchedulePolicy &RenderSchedulePlan::policy() const noexcept {
    return policy_;
}

const ClockExtent &RenderSchedulePlan::physics() const noexcept {
    return physics_;
}

const ClockExtent &RenderSchedulePlan::capture() const noexcept {
    return capture_;
}

const ClockExtent &RenderSchedulePlan::source_processing() const noexcept {
    return source_processing_;
}

const ClockExtent &RenderSchedulePlan::acoustic() const noexcept {
    return acoustic_;
}

const ClockExtent &RenderSchedulePlan::delivery() const noexcept {
    return delivery_;
}

std::uint32_t RenderSchedulePlan::capture_capacity_frames() const noexcept {
    return capture_capacity_frames_;
}

std::uint32_t RenderSchedulePlan::event_journal_capacity_records() const noexcept {
    return event_journal_capacity_records_;
}

std::uint64_t RenderSchedulePlan::block_count() const noexcept {
    return block_count_;
}

ScheduleCompileResult compile_render_schedule(const contract::RenderScenario &scenario,
                                              const SchedulePolicy &policy) {
    ValidationReport report;
    auto clock_grid = contract::validate_clock_grid(scenario);
    for (auto &issue : clock_grid.issues) {
        issue.path = "scenario.clock_grid." + issue.path;
        report.issues.push_back(std::move(issue));
    }
    auto rates = contract::validate(scenario.rates);
    for (auto &issue : rates.issues) {
        issue.path =
            issue.path.empty() ? "scenario.rates" : "scenario.rates." + issue.path;
        report.issues.push_back(std::move(issue));
    }

    require(report, contract::is_valid_semantic_id(policy.id), "policy.id",
            "schedule policy ID must be a canonical semantic ID");
    require(report, policy.version > 0, "policy.version",
            "schedule policy version must be positive");
    require(report, policy.capture_partition_frames > 0,
            "policy.capture_partition_frames",
            "method-owned capture partition must be positive");
    require(report, policy.maximum_event_records_per_block > 0,
            "policy.maximum_event_records_per_block",
            "method-owned event-record bound must be positive");
    require(report,
            policy.capture_phase == contract::SamplePhase::pre_step ||
                policy.capture_phase == contract::SamplePhase::post_step,
            "policy.capture_phase",
            "schedule policy must select pre-step or post-step capture");
    require(report, policy.deterministic_worker_count == 1,
            "policy.deterministic_worker_count",
            "the current deterministic scheduler admits exactly one session worker");
    require(report,
            scenario.quality.value.capture_block_capacity_frames > 0 &&
                policy.capture_partition_frames <=
                    scenario.quality.value.capture_block_capacity_frames,
            "policy.capture_partition_frames",
            "method-owned partition must fit the scenario capture-block capacity");
    require(report, scenario.quality.value.event_journal_capacity_records > 0,
            "scenario.quality.value.event_journal_capacity_records",
            "event-journal transport capacity must be positive");
    require(report,
            policy.maximum_event_records_per_block <=
                scenario.quality.value.event_journal_capacity_records,
            "policy.maximum_event_records_per_block",
            "method-owned event-record bound must fit the scenario transport "
            "capacity");
    if (!report.ok()) {
        return report;
    }

    const auto require_integral_partition_projection = [&](std::string_view name,
                                                           const RationalRateHz &rate) {
        const auto projected = project_capture_frame(policy.capture_partition_frames,
                                                     rate, scenario.rates.capture);
        require(report, projected.has_value() && projected->remainder == 0,
                "policy.capture_partition_frames." + std::string{name},
                "method partition must project to an integral boundary on every "
                "target clock");
    };
    require_integral_partition_projection("physics", scenario.rates.physics);
    require_integral_partition_projection("capture", scenario.rates.capture);
    require_integral_partition_projection("source_processing",
                                          scenario.rates.source_processing);
    require_integral_partition_projection("acoustic", scenario.rates.acoustic);
    require_integral_partition_projection("delivery", scenario.rates.delivery);
    if (!report.ok()) {
        return report;
    }

    const auto total_capture = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.capture);
    if (!total_capture.has_value() || *total_capture == 0) {
        report.add(ContractIssueCode::inconsistent_semantics,
                   "scenario.clock_grid.capture.total_duration_s",
                   "capture horizon must resolve before schedule compilation");
        return report;
    }

    const auto physics =
        compile_clock(report, "physics", scenario.rates.physics, scenario.rates.capture,
                      scenario, policy.capture_partition_frames);
    const auto capture =
        compile_clock(report, "capture", scenario.rates.capture, scenario.rates.capture,
                      scenario, policy.capture_partition_frames);
    const auto source = compile_clock(
        report, "source_processing", scenario.rates.source_processing,
        scenario.rates.capture, scenario, policy.capture_partition_frames);
    const auto acoustic = compile_clock(report, "acoustic", scenario.rates.acoustic,
                                        scenario.rates.capture, scenario,
                                        policy.capture_partition_frames);
    const auto delivery = compile_clock(report, "delivery", scenario.rates.delivery,
                                        scenario.rates.capture, scenario,
                                        policy.capture_partition_frames);
    if (!report.ok() || !physics.has_value() || !capture.has_value() ||
        !source.has_value() || !acoustic.has_value() || !delivery.has_value()) {
        return report;
    }

    const auto partition = static_cast<std::uint64_t>(policy.capture_partition_frames);
    const auto block_count =
        *total_capture / partition + (*total_capture % partition != 0 ? 1U : 0U);
    return RenderSchedulePlan{
        policy,
        *physics,
        *capture,
        *source,
        *acoustic,
        *delivery,
        scenario.quality.value.capture_block_capacity_frames,
        scenario.quality.value.event_journal_capacity_records,
        block_count,
    };
}

const SchedulePolicy &p18_reference_schedule_policy_v1() noexcept {
    static const SchedulePolicy policy{
        "p18-reference-fixed-block-schedule", 1, 200, 19U * 200U,
        contract::SamplePhase::post_step,     1,
    };
    return policy;
}

ScheduleCompileResult
compile_p18_reference_schedule(const contract::RenderScenario &scenario) {
    auto compiled =
        compile_render_schedule(scenario, p18_reference_schedule_policy_v1());
    if (const auto *failure = std::get_if<ValidationReport>(&compiled)) {
        return *failure;
    }

    const auto &plan = std::get<RenderSchedulePlan>(compiled);
    ValidationReport report;
    require(report,
            scenario.rates.physics == RationalRateHz{10000, 1} &&
                scenario.rates.capture == RationalRateHz{10000, 1} &&
                scenario.rates.source_processing == RationalRateHz{192000, 1} &&
                scenario.rates.acoustic == RationalRateHz{192000, 1} &&
                scenario.rates.delivery == RationalRateHz{192000, 1},
            "scenario.rates",
            "P1.8 schedule requires the frozen 10 kHz to 192 kHz clock plan");
    require(report, exact_extent(plan.capture(), 170000, 20000, 170000, 200), "capture",
            "P1.8 capture schedule must be 170000 frames in 200-frame blocks "
            "with [20000,170000) audible");
    require(report,
            exact_extent(plan.source_processing(), 3264000, 384000, 3264000, 3840),
            "source_processing",
            "P1.8 source schedule must be 3264000 frames in 3840-frame blocks "
            "with [384000,3264000) audible");
    require(report,
            exact_extent(plan.acoustic(), 3264000, 384000, 3264000, 3840) &&
                exact_extent(plan.delivery(), 3264000, 384000, 3264000, 3840),
            "acoustic_delivery",
            "P1.8 acoustic and delivery clocks must retain the exact source "
            "partition and crop");
    require(report, plan.block_count() == 850, "block_count",
            "P1.8 schedule requires exactly 850 method-owned blocks");
    require(report, scenario.quality.value.capture_block_capacity_frames >= 200,
            "scenario.quality.value.capture_block_capacity_frames",
            "P1.8 transport capacity must hold one 200-frame method block");
    require(report, plan.policy().maximum_event_records_per_block == 19U * 200U,
            "policy.maximum_event_records_per_block",
            "P1.8 method must reserve 19 event records per capture frame");

    const auto *fixed = std::get_if<contract::FixedSettling>(&scenario.preparation);
    const auto warm_up_frames =
        fixed == nullptr ? std::optional<std::uint64_t>{}
                         : contract::resolve_frame_index(
                               fixed->warm_up_duration_s.value, scenario.rates.capture);
    const auto settling_frames =
        fixed == nullptr
            ? std::optional<std::uint64_t>{}
            : contract::resolve_frame_index(fixed->settling_duration_s.value,
                                            scenario.rates.capture);
    require(report,
            fixed != nullptr && warm_up_frames == std::optional<std::uint64_t>{10000} &&
                settling_frames == std::optional<std::uint64_t>{10000},
            "scenario.preparation",
            "P1.8 requires 50 bootstrap blocks followed by 50 loaded pre-roll "
            "blocks before the 750 audible blocks");
    require(report, plan.source_processing().maximum_frames_per_block <= 9600,
            "source_processing.maximum_frames_per_block",
            "P1.8 source block exceeds the frozen convolution limit");
    if (!report.ok()) {
        return report;
    }
    return plan;
}

ScheduleCursor::ScheduleCursor(RenderSchedulePlan plan) noexcept
    : plan_(std::move(plan)) {}

ScheduleProgress ScheduleCursor::progress() const noexcept {
    const auto physics = project_capture_frame(
        next_capture_frame_, plan_.physics().rate, plan_.capture().rate);
    const auto source = project_capture_frame(
        next_capture_frame_, plan_.source_processing().rate, plan_.capture().rate);
    const auto acoustic = project_capture_frame(
        next_capture_frame_, plan_.acoustic().rate, plan_.capture().rate);
    const auto delivery = project_capture_frame(
        next_capture_frame_, plan_.delivery().rate, plan_.capture().rate);
    return {
        next_block_ordinal_,
        physics.has_value() ? physics->quotient : 0,
        next_capture_frame_,
        source.has_value() ? source->quotient : 0,
        acoustic.has_value() ? acoustic->quotient : 0,
        delivery.has_value() ? delivery->quotient : 0,
    };
}

ScheduleStep ScheduleCursor::next(std::stop_token stop_token) noexcept {
    if (terminal_ == TerminalState::cancelled) {
        return ScheduleCancelled{progress()};
    }
    if (terminal_ == TerminalState::completed) {
        return ScheduleCompleted{progress()};
    }
    if (stop_token.stop_requested()) {
        terminal_ = TerminalState::cancelled;
        return ScheduleCancelled{progress()};
    }
    if (next_capture_frame_ == plan_.capture().total_frames) {
        terminal_ = TerminalState::completed;
        return ScheduleCompleted{progress()};
    }

    const auto remaining = plan_.capture().total_frames - next_capture_frame_;
    const auto frame_count =
        std::min<std::uint64_t>(remaining, plan_.policy().capture_partition_frames);
    const FrameRange capture_frames{
        next_capture_frame_,
        next_capture_frame_ + frame_count,
    };
    ScheduledRenderBlock block;
    block.ordinal = next_block_ordinal_;
    block.capture_clock = {
        plan_.capture().rate,
        capture_frames.begin,
        capture_frames.begin +
            (plan_.policy().capture_phase == contract::SamplePhase::post_step ? 1U
                                                                              : 0U),
        plan_.policy().capture_phase,
    };
    block.physics =
        schedule_clock_block(capture_frames, plan_.physics(), plan_.capture().rate);
    block.capture = {
        capture_frames,
        intersection(capture_frames, plan_.capture().audible),
    };
    block.source_processing = schedule_clock_block(
        capture_frames, plan_.source_processing(), plan_.capture().rate);
    block.acoustic =
        schedule_clock_block(capture_frames, plan_.acoustic(), plan_.capture().rate);
    block.delivery =
        schedule_clock_block(capture_frames, plan_.delivery(), plan_.capture().rate);

    next_capture_frame_ = capture_frames.end;
    ++next_block_ordinal_;
    return block;
}

ValidationReport
validate_scheduled_capture_block(const RenderSchedulePlan &plan,
                                 const ScheduledRenderBlock &scheduled,
                                 const contract::CaptureBlockView &block) {
    ValidationReport report;
    append_prefixed(report, contract::validate(block), "block");

    require(report, scheduled.ordinal < plan.block_count(), "schedule.ordinal",
            "scheduled block ordinal must lie inside the compiled plan");

    FrameRange expected_frames;
    bool expected_range_representable = false;
    if (scheduled.ordinal < plan.block_count()) {
        const auto partition =
            static_cast<std::uint64_t>(plan.policy().capture_partition_frames);
        std::uint64_t expected_begin = 0;
        std::uint64_t partition_end = 0;
        expected_range_representable =
            checked_multiply(scheduled.ordinal, partition, expected_begin) &&
            checked_add(expected_begin, partition, partition_end) &&
            expected_begin < plan.capture().total_frames;
        if (expected_range_representable) {
            expected_frames = {
                expected_begin,
                std::min(partition_end, plan.capture().total_frames),
            };
        }
    }
    require(report, expected_range_representable, "schedule.capture.frames",
            "scheduled capture range must be representable inside the compiled plan");
    if (expected_range_representable) {
        require(report, scheduled.capture.frames == expected_frames,
                "schedule.capture.frames",
                "scheduled capture range must exactly match its plan ordinal");
        require(report,
                scheduled.capture.audible ==
                    intersection(expected_frames, plan.capture().audible),
                "schedule.capture.audible",
                "scheduled capture crop must exactly match the compiled plan");

        const auto phase_offset =
            plan.policy().capture_phase == contract::SamplePhase::post_step ? 1U : 0U;
        const contract::CaptureClock expected_clock{
            plan.capture().rate,
            expected_frames.begin,
            expected_frames.begin + phase_offset,
            plan.policy().capture_phase,
        };
        require(report, scheduled.capture_clock == expected_clock,
                "schedule.capture_clock",
                "scheduled capture clock must exactly match its plan range and phase");
        require(report, block.frame_count() == expected_frames.frame_count(),
                "block.frame_count",
                "capture payload frame count must exactly match the scheduled range");
    }

    require(report, block.clock() == scheduled.capture_clock, "block.clock",
            "capture payload clock must exactly match the scheduled block");
    require(report,
            block.declared_block_capacity_frames() == plan.capture_capacity_frames(),
            "block.declared_block_capacity_frames",
            "capture payload must retain the plan's transport frame capacity");
    require(report,
            block.declared_event_journal_capacity_records() ==
                plan.event_journal_capacity_records(),
            "block.declared_event_journal_capacity_records",
            "capture payload must retain the plan's event-journal capacity");
    require(report,
            block.event_journal().events().size() <=
                plan.policy().maximum_event_records_per_block,
            "block.event_journal.events",
            "capture payload event count must not exceed the method-owned "
            "per-block bound");
    return report;
}

} // namespace engine_sim_offline::scheduling
