#pragma once

#include "engine_sim_offline/contract.hpp"

#include <cstdint>
#include <stop_token>
#include <string>
#include <variant>

namespace engine_sim_offline::scheduling {

// Internal orchestration types. They are intentionally outside the public render
// header: callers select a resolved render request, while an admitted method owns its
// deterministic internal partition.
struct FrameRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;

    [[nodiscard]] std::uint64_t frame_count() const noexcept {
        return end - begin;
    }

    friend bool operator==(const FrameRange &, const FrameRange &) = default;
};

struct SchedulePolicy {
    std::string id;
    std::uint32_t version = 0;
    std::uint32_t capture_partition_frames = 0;
    std::uint32_t maximum_event_records_per_block = 0;
    contract::SamplePhase capture_phase = contract::SamplePhase::unspecified;
    std::uint32_t deterministic_worker_count = 0;

    friend bool operator==(const SchedulePolicy &, const SchedulePolicy &) = default;
};

struct ClockExtent {
    contract::RationalRateHz rate;
    std::uint64_t total_frames = 0;
    FrameRange audible;
    std::uint64_t maximum_frames_per_block = 0;

    friend bool operator==(const ClockExtent &, const ClockExtent &) = default;
};

class RenderSchedulePlan {
  public:
    [[nodiscard]] const SchedulePolicy &policy() const noexcept;
    [[nodiscard]] const ClockExtent &physics() const noexcept;
    [[nodiscard]] const ClockExtent &capture() const noexcept;
    [[nodiscard]] const ClockExtent &source_processing() const noexcept;
    [[nodiscard]] const ClockExtent &acoustic() const noexcept;
    [[nodiscard]] const ClockExtent &delivery() const noexcept;
    [[nodiscard]] std::uint32_t capture_capacity_frames() const noexcept;
    [[nodiscard]] std::uint32_t event_journal_capacity_records() const noexcept;
    [[nodiscard]] std::uint64_t block_count() const noexcept;

    friend bool operator==(const RenderSchedulePlan &,
                           const RenderSchedulePlan &) = default;

  private:
    RenderSchedulePlan(SchedulePolicy policy, ClockExtent physics, ClockExtent capture,
                       ClockExtent source_processing, ClockExtent acoustic,
                       ClockExtent delivery, std::uint32_t capture_capacity_frames,
                       std::uint32_t event_journal_capacity_records,
                       std::uint64_t block_count) noexcept;

    SchedulePolicy policy_;
    ClockExtent physics_;
    ClockExtent capture_;
    ClockExtent source_processing_;
    ClockExtent acoustic_;
    ClockExtent delivery_;
    std::uint32_t capture_capacity_frames_ = 0;
    std::uint32_t event_journal_capacity_records_ = 0;
    std::uint64_t block_count_ = 0;

    friend std::variant<RenderSchedulePlan, contract::ValidationReport>
    compile_render_schedule(const contract::RenderScenario &, const SchedulePolicy &);
};

using ScheduleCompileResult =
    std::variant<RenderSchedulePlan, contract::ValidationReport>;

// Compiles integer horizons and a constant-memory partition. This does not validate
// provenance or admit a renderer; callers must supply the partition fixed by the
// selected method record.
[[nodiscard]] ScheduleCompileResult
compile_render_schedule(const contract::RenderScenario &scenario,
                        const SchedulePolicy &policy);

[[nodiscard]] const SchedulePolicy &p18_reference_schedule_policy_v1() noexcept;
[[nodiscard]] ScheduleCompileResult
compile_p18_reference_schedule(const contract::RenderScenario &scenario);

struct ScheduledClockBlock {
    FrameRange frames;
    FrameRange audible;

    friend bool operator==(const ScheduledClockBlock &,
                           const ScheduledClockBlock &) = default;
};

struct ScheduledRenderBlock {
    std::uint64_t ordinal = 0;
    contract::CaptureClock capture_clock;
    ScheduledClockBlock physics;
    ScheduledClockBlock capture;
    ScheduledClockBlock source_processing;
    ScheduledClockBlock acoustic;
    ScheduledClockBlock delivery;

    friend bool operator==(const ScheduledRenderBlock &,
                           const ScheduledRenderBlock &) = default;
};

struct ScheduleProgress {
    std::uint64_t next_block_ordinal = 0;
    std::uint64_t next_physics_frame = 0;
    std::uint64_t next_capture_frame = 0;
    std::uint64_t next_source_processing_frame = 0;
    std::uint64_t next_acoustic_frame = 0;
    std::uint64_t next_delivery_frame = 0;

    friend bool operator==(const ScheduleProgress &,
                           const ScheduleProgress &) = default;
};

struct ScheduleCompleted {
    ScheduleProgress progress;

    friend bool operator==(const ScheduleCompleted &,
                           const ScheduleCompleted &) = default;
};

struct ScheduleCancelled {
    ScheduleProgress progress;

    friend bool operator==(const ScheduleCancelled &,
                           const ScheduleCancelled &) = default;
};

using ScheduleStep =
    std::variant<ScheduledRenderBlock, ScheduleCompleted, ScheduleCancelled>;

// The cursor owns only the compact plan and the next integer boundary. It never
// materializes a duration-sized block list. A caller must finish consuming the
// returned block before requesting the next one. Cancellation is observed only
// between complete method-owned blocks, including one final poll before completion.
class ScheduleCursor {
  public:
    explicit ScheduleCursor(RenderSchedulePlan plan) noexcept;

    [[nodiscard]] ScheduleStep next(std::stop_token stop_token = {}) noexcept;
    [[nodiscard]] ScheduleProgress progress() const noexcept;

  private:
    enum class TerminalState : std::uint8_t {
        active,
        completed,
        cancelled,
    };

    RenderSchedulePlan plan_;
    std::uint64_t next_block_ordinal_ = 0;
    std::uint64_t next_capture_frame_ = 0;
    TerminalState terminal_ = TerminalState::active;
};

// Admits one callback-scoped capture payload at the schedule seam. This composes the
// CaptureBlockView structural validator with exact plan/block identity so a producer
// cannot reinterpret transport capacities or publish a different clock/range.
[[nodiscard]] contract::ValidationReport
validate_scheduled_capture_block(const RenderSchedulePlan &plan,
                                 const ScheduledRenderBlock &scheduled,
                                 const contract::CaptureBlockView &block);

} // namespace engine_sim_offline::scheduling
