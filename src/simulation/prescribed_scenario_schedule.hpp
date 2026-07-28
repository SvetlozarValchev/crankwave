#pragma once

#include "engine_sim_offline/contract/scenario.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace engine_sim_offline::simulation {

namespace detail {
struct PrescribedScenarioScheduleFactory;
struct PrescribedScenarioScheduleStorage;
} // namespace detail

struct ScheduledScenarioStep {
    std::uint64_t sample_index = 0;
    std::uint64_t step_end_index = 0;
    double rpm = 0.0;
    double requested_throttle = 0.0;
    contract::OperatingState operating_state;

    friend bool operator==(const ScheduledScenarioStep &,
                           const ScheduledScenarioStep &) = default;
};

// A cursor has independent iteration state and shares one immutable snapshot of the
// validated RPM lane and compact control boundaries with its compiled schedule.
class PrescribedScenarioCursor final {
  public:
    PrescribedScenarioCursor(const PrescribedScenarioCursor &) = delete;
    PrescribedScenarioCursor &operator=(const PrescribedScenarioCursor &) = delete;
    PrescribedScenarioCursor(PrescribedScenarioCursor &&other) noexcept;
    PrescribedScenarioCursor &operator=(PrescribedScenarioCursor &&other) noexcept;

    // Completion is stable: once this returns nullopt, all later calls do too.
    [[nodiscard]] std::optional<ScheduledScenarioStep> next() noexcept;
    [[nodiscard]] bool completed() const noexcept;

  private:
    PrescribedScenarioCursor(
        std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage,
        std::uint64_t first_step_index) noexcept;

    std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage_;
    std::uint64_t first_step_index_ = 0;
    std::size_t next_sample_offset_ = 0;
    std::size_t next_operating_state_boundary_ = 0;
    std::size_t next_throttle_boundary_ = 0;
    contract::OperatingState operating_state_;
    double requested_throttle_ = 0.0;

    friend class PrescribedScenarioSchedule;
};

// The validated fixed-rate RPM lane is copied once into immutable session storage.
// Control times are resolved once into integer physics-step boundaries. Consequently,
// later mutation or destruction of the source request cannot alter compiled physics.
class PrescribedScenarioSchedule final {
  public:
    PrescribedScenarioSchedule(const PrescribedScenarioSchedule &) = default;
    PrescribedScenarioSchedule(PrescribedScenarioSchedule &&) noexcept = default;
    PrescribedScenarioSchedule &operator=(const PrescribedScenarioSchedule &) = default;
    PrescribedScenarioSchedule &
    operator=(PrescribedScenarioSchedule &&) noexcept = default;

    [[nodiscard]] const contract::RationalRateHz &rate() const noexcept;
    [[nodiscard]] std::uint64_t first_step_index() const noexcept;
    [[nodiscard]] contract::RpmSampleSemantics sample_semantics() const noexcept;
    [[nodiscard]] std::uint64_t sample_count() const noexcept;
    [[nodiscard]] std::span<const double> post_step_rpm() const noexcept;
    [[nodiscard]] PrescribedScenarioCursor fresh_cursor() const noexcept;

  private:
    PrescribedScenarioSchedule(
        contract::RationalRateHz rate, std::uint64_t first_step_index,
        contract::RpmSampleSemantics sample_semantics,
        std::shared_ptr<const detail::PrescribedScenarioScheduleStorage>
            storage) noexcept;

    contract::RationalRateHz rate_;
    std::uint64_t first_step_index_ = 0;
    contract::RpmSampleSemantics sample_semantics_ =
        contract::RpmSampleSemantics::post_step_rpm;
    std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage_;

    friend struct detail::PrescribedScenarioScheduleFactory;
};

using PrescribedScenarioScheduleResult =
    std::variant<PrescribedScenarioSchedule, contract::ValidationReport>;

// This is a narrow, provenance-independent admission seam for the fixed-rate
// prescribed simulator. Other scenario modes and scalar RPM trajectories are not
// silently adapted.
[[nodiscard]] PrescribedScenarioScheduleResult
compile_prescribed_scenario_schedule(const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::simulation
