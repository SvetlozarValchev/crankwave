#pragma once

#include "engine_sim_offline/contract/scenario.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace engine_sim_offline::simulation {

namespace detail {
struct KinematicScenarioScheduleFactory;
struct KinematicScenarioScheduleStorage;
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
// validated kinematic lane and compact control boundaries with its compiled schedule.
class KinematicScenarioCursor final {
  public:
    KinematicScenarioCursor(const KinematicScenarioCursor &) = delete;
    KinematicScenarioCursor &operator=(const KinematicScenarioCursor &) = delete;
    KinematicScenarioCursor(KinematicScenarioCursor &&other) noexcept;
    KinematicScenarioCursor &operator=(KinematicScenarioCursor &&other) noexcept;

    // Completion is stable: once this returns nullopt, all later calls do too.
    [[nodiscard]] std::optional<ScheduledScenarioStep> next() noexcept;
    [[nodiscard]] bool completed() const noexcept;

  private:
    KinematicScenarioCursor(
        std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage,
        std::uint64_t first_step_index) noexcept;

    std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage_;
    std::uint64_t first_step_index_ = 0;
    std::uint64_t next_sample_offset_ = 0;
    std::size_t next_operating_state_boundary_ = 0;
    std::size_t next_throttle_boundary_ = 0;
    contract::OperatingState operating_state_;
    double requested_throttle_ = 0.0;

    friend class KinematicScenarioSchedule;
};

// A sampled sweep owns one immutable copy of its fixed-rate RPM lane. A held-speed
// schedule stores only one RPM value regardless of horizon length. Control times are
// resolved once into compact integer physics-step boundaries. Consequently, later
// mutation or destruction of the source request cannot alter compiled physics.
class KinematicScenarioSchedule final {
  public:
    KinematicScenarioSchedule(const KinematicScenarioSchedule &) = default;
    KinematicScenarioSchedule(KinematicScenarioSchedule &&) noexcept = default;
    KinematicScenarioSchedule &operator=(const KinematicScenarioSchedule &) = default;
    KinematicScenarioSchedule &
    operator=(KinematicScenarioSchedule &&) noexcept = default;

    [[nodiscard]] const contract::RationalRateHz &rate() const noexcept;
    [[nodiscard]] std::uint64_t first_step_index() const noexcept;
    [[nodiscard]] contract::RpmSampleSemantics sample_semantics() const noexcept;
    [[nodiscard]] std::uint64_t sample_count() const noexcept;
    [[nodiscard]] double initial_theta_rad() const noexcept;
    [[nodiscard]] std::optional<double>
    rpm_at_sample_offset(std::uint64_t sample_offset) const noexcept;
    [[nodiscard]] KinematicScenarioCursor fresh_cursor() const noexcept;

  private:
    KinematicScenarioSchedule(
        contract::RationalRateHz rate, std::uint64_t first_step_index,
        contract::RpmSampleSemantics sample_semantics, std::uint64_t sample_count,
        double initial_theta_rad,
        std::shared_ptr<const detail::KinematicScenarioScheduleStorage>
            storage) noexcept;

    contract::RationalRateHz rate_;
    std::uint64_t first_step_index_ = 0;
    contract::RpmSampleSemantics sample_semantics_ =
        contract::RpmSampleSemantics::post_step_rpm;
    std::uint64_t sample_count_ = 0;
    double initial_theta_rad_ = 0.0;
    std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage_;

    friend struct detail::KinematicScenarioScheduleFactory;
};

using KinematicScenarioScheduleResult =
    std::variant<KinematicScenarioSchedule, contract::ValidationReport>;

// This is a configuration-provenance-independent admission seam for the typed
// fixed-rate method family and held speed. Profile adapters own exact configuration
// identity; dynamic and scalar-RPM modes are not silently adapted.
[[nodiscard]] KinematicScenarioScheduleResult
compile_kinematic_scenario_schedule(const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::simulation
