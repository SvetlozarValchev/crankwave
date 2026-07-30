#include "kinematic_scenario_schedule.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine_sim_offline::simulation {

namespace detail {

struct OperatingStateBoundary {
    std::uint64_t step_index = 0;
    contract::OperatingState state;
};

struct ThrottleBoundary {
    std::uint64_t step_index = 0;
    double value = 0.0;
};

struct ExternalResistingTorqueBoundary {
    std::uint64_t step_index = 0;
    double value_nm = 0.0;
};

struct SampledRpmLane {
    std::vector<double> post_step_rpm;
};

struct ConstantRpmLane {
    double rpm = 0.0;
};

using RpmLane = std::variant<SampledRpmLane, ConstantRpmLane>;

struct ScenarioControlScheduleStorage {
    std::uint64_t sample_count = 0;
    std::vector<OperatingStateBoundary> operating_state;
    std::vector<ThrottleBoundary> throttle;
    std::vector<ExternalResistingTorqueBoundary> external_resisting_torque;
};

struct KinematicScenarioScheduleStorage {
    RpmLane rpm;
    std::shared_ptr<const ScenarioControlScheduleStorage> controls;
};

struct ScenarioControlScheduleFactory {
    static ScenarioControlSchedule
    make(contract::RationalRateHz rate, std::uint64_t first_step_index,
         std::uint64_t sample_count, double initial_theta_rad,
         std::shared_ptr<const ScenarioControlScheduleStorage> storage) noexcept {
        return {rate, first_step_index, sample_count, initial_theta_rad,
                std::move(storage)};
    }
};

struct KinematicScenarioScheduleFactory {
    static KinematicScenarioSchedule
    make(contract::RationalRateHz rate, std::uint64_t first_step_index,
         contract::RpmSampleSemantics semantics, std::uint64_t sample_count,
         double initial_theta_rad,
         std::shared_ptr<const KinematicScenarioScheduleStorage> storage) noexcept {
        return {rate,         first_step_index,  semantics,
                sample_count, initial_theta_rad, std::move(storage)};
    }
};

} // namespace detail

namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void add_issue(ValidationReport &report, ContractIssueCode code, std::string path,
               std::string message) {
    report.add(code, std::move(path), std::move(message));
}

void append_rate_report(ValidationReport &report, const contract::RationalRateHz &rate,
                        const std::string &path) {
    auto nested = contract::validate(rate);
    for (auto &issue : nested.issues) {
        issue.path = issue.path.empty() ? path : path + "." + issue.path;
        report.issues.push_back(std::move(issue));
    }
}

std::optional<std::uint64_t>
resolve_boundary(ValidationReport &report, double time_s,
                 const contract::RationalRateHz &rate,
                 const std::optional<std::uint64_t> &horizon, const std::string &path) {
    const auto step_index = contract::resolve_frame_index(time_s, rate);
    if (!step_index.has_value()) {
        add_issue(report, ContractIssueCode::inconsistent_semantics, path,
                  "boundary must be finite, nonnegative, and resolve to an integral "
                  "physics-step index");
        return std::nullopt;
    }
    if (horizon.has_value() && *step_index > *horizon) {
        add_issue(report, ContractIssueCode::inconsistent_semantics, path,
                  "boundary must not exceed the kinematic scenario horizon");
    }
    return step_index;
}

void compile_operating_state_boundaries(
    ValidationReport &report, const contract::RenderScenario &scenario,
    const std::optional<std::uint64_t> &horizon,
    std::vector<detail::OperatingStateBoundary> &output) {
    const auto &points = scenario.operating_state.value;
    if (points.empty()) {
        add_issue(report, ContractIssueCode::missing_value,
                  "scenario.operating_state.value",
                  "right-continuous operating state requires a step-zero boundary");
        return;
    }

    output.reserve(points.size());
    std::optional<std::uint64_t> previous_step;
    double previous_time_s = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto &point = points[index];
        const auto path =
            "scenario.operating_state.value[" + std::to_string(index) + "].time_s";
        const auto step = resolve_boundary(report, point.time_s, scenario.rates.physics,
                                           horizon, path);
        if (index == 0) {
            if (!step.has_value() || *step != 0) {
                add_issue(report, ContractIssueCode::inconsistent_semantics, path,
                          "right-continuous operating state must begin at physics "
                          "step zero");
            }
        } else {
            if (!std::isfinite(point.time_s) || !(point.time_s > previous_time_s)) {
                add_issue(report, ContractIssueCode::inconsistent_semantics, path,
                          "operating-state boundaries must be strictly increasing");
            }
            if (step.has_value() && previous_step.has_value() &&
                *step <= *previous_step) {
                add_issue(report, ContractIssueCode::inconsistent_semantics, path,
                          "operating-state physics-step boundaries must be strictly "
                          "increasing");
            }
        }
        if (step.has_value()) {
            output.push_back({*step, point.state});
            previous_step = step;
        } else {
            previous_step.reset();
        }
        previous_time_s = point.time_s;
    }
}

void compile_throttle_boundaries(ValidationReport &report,
                                 const contract::RenderScenario &scenario,
                                 const contract::ScalarTrajectory &throttle,
                                 const std::optional<std::uint64_t> &horizon,
                                 std::vector<detail::ThrottleBoundary> &output) {
    if (throttle.interpolation !=
        contract::TrajectoryInterpolation::right_continuous_hold) {
        add_issue(report, ContractIssueCode::unsupported_value,
                  "scenario.mode.throttle_01.interpolation",
                  "prescribed scheduling supports only right-continuous-hold "
                  "throttle interpolation");
    }
    if (throttle.points.empty()) {
        add_issue(report, ContractIssueCode::missing_value,
                  "scenario.mode.throttle_01.points",
                  "right-continuous throttle requires a step-zero boundary");
        return;
    }

    output.reserve(throttle.points.size());
    std::optional<std::uint64_t> previous_step;
    double previous_time_s = 0.0;
    for (std::size_t index = 0; index < throttle.points.size(); ++index) {
        const auto &point = throttle.points[index];
        const auto point_path =
            "scenario.mode.throttle_01.points[" + std::to_string(index) + "]";
        if (!std::isfinite(point.value)) {
            add_issue(report, ContractIssueCode::invalid_value, point_path + ".value",
                      "throttle must be finite");
        } else if (point.value < 0.0 || point.value > 1.0) {
            add_issue(report, ContractIssueCode::invalid_value, point_path + ".value",
                      "throttle must be in [0, 1]");
        }

        const auto step = resolve_boundary(report, point.time_s, scenario.rates.physics,
                                           horizon, point_path + ".time_s");
        if (index == 0) {
            if (!step.has_value() || *step != 0) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "right-continuous throttle must begin at physics step zero");
            }
        } else {
            if (!std::isfinite(point.time_s) || !(point.time_s > previous_time_s)) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "throttle boundaries must be strictly increasing");
            }
            if (step.has_value() && previous_step.has_value() &&
                *step <= *previous_step) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "throttle physics-step boundaries must be strictly "
                          "increasing");
            }
        }
        if (step.has_value()) {
            output.push_back({*step, point.value});
            previous_step = step;
        } else {
            previous_step.reset();
        }
        previous_time_s = point.time_s;
    }
}

void compile_external_resisting_torque_boundaries(
    ValidationReport &report, const contract::RenderScenario &scenario,
    const contract::ScalarTrajectory &torque,
    const std::optional<std::uint64_t> &horizon,
    std::vector<detail::ExternalResistingTorqueBoundary> &output) {
    constexpr std::string_view path = "scenario.mode.external_resisting_torque_nm";
    if (torque.interpolation !=
        contract::TrajectoryInterpolation::right_continuous_hold) {
        add_issue(report, ContractIssueCode::unsupported_value,
                  std::string{path} + ".interpolation",
                  "free-engine scheduling supports only right-continuous-hold "
                  "external resisting torque");
    }
    if (torque.points.empty()) {
        add_issue(report, ContractIssueCode::missing_value,
                  std::string{path} + ".points",
                  "right-continuous external resisting torque requires a "
                  "step-zero boundary");
        return;
    }

    output.reserve(torque.points.size());
    std::optional<std::uint64_t> previous_step;
    double previous_time_s = 0.0;
    for (std::size_t index = 0; index < torque.points.size(); ++index) {
        const auto &point = torque.points[index];
        const auto point_path =
            std::string{path} + ".points[" + std::to_string(index) + "]";
        if (!std::isfinite(point.value) || point.value < 0.0) {
            add_issue(report, ContractIssueCode::invalid_value, point_path + ".value",
                      "external resisting torque must be finite and nonnegative");
        }

        const auto step = resolve_boundary(report, point.time_s, scenario.rates.physics,
                                           horizon, point_path + ".time_s");
        if (index == 0) {
            if (!step.has_value() || *step != 0) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "right-continuous external resisting torque must begin at "
                          "physics step zero");
            }
        } else {
            if (!std::isfinite(point.time_s) || !(point.time_s > previous_time_s)) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "external resisting-torque boundaries must be strictly "
                          "increasing");
            }
            if (step.has_value() && previous_step.has_value() &&
                *step <= *previous_step) {
                add_issue(report, ContractIssueCode::inconsistent_semantics,
                          point_path + ".time_s",
                          "external resisting-torque physics-step boundaries must be "
                          "strictly increasing");
            }
            if (step.has_value() && horizon.has_value() && *step == *horizon) {
                add_issue(report, ContractIssueCode::unsupported_value,
                          point_path + ".time_s",
                          "an external resisting-torque boundary at the final "
                          "physics horizon is never observed");
            }
        }
        if (step.has_value()) {
            output.push_back({*step, point.value});
            previous_step = step;
        } else {
            previous_step.reset();
        }
        previous_time_s = point.time_s;
    }
}

} // namespace

ScenarioControlCursor::ScenarioControlCursor(
    std::shared_ptr<const detail::ScenarioControlScheduleStorage> storage,
    std::uint64_t first_step_index) noexcept
    : storage_(std::move(storage)), first_step_index_(first_step_index) {}

ScenarioControlCursor::ScenarioControlCursor(ScenarioControlCursor &&other) noexcept
    : storage_(std::move(other.storage_)),
      first_step_index_(std::exchange(other.first_step_index_, 0)),
      next_sample_offset_(std::exchange(other.next_sample_offset_, 0)),
      next_operating_state_boundary_(
          std::exchange(other.next_operating_state_boundary_, 0)),
      next_throttle_boundary_(std::exchange(other.next_throttle_boundary_, 0)),
      next_external_resisting_torque_boundary_(
          std::exchange(other.next_external_resisting_torque_boundary_, 0)),
      operating_state_(other.operating_state_),
      requested_throttle_(other.requested_throttle_),
      external_resisting_torque_nm_(other.external_resisting_torque_nm_) {}

ScenarioControlCursor &
ScenarioControlCursor::operator=(ScenarioControlCursor &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    storage_ = std::move(other.storage_);
    first_step_index_ = std::exchange(other.first_step_index_, 0);
    next_sample_offset_ = std::exchange(other.next_sample_offset_, 0);
    next_operating_state_boundary_ =
        std::exchange(other.next_operating_state_boundary_, 0);
    next_throttle_boundary_ = std::exchange(other.next_throttle_boundary_, 0);
    next_external_resisting_torque_boundary_ =
        std::exchange(other.next_external_resisting_torque_boundary_, 0);
    operating_state_ = other.operating_state_;
    requested_throttle_ = other.requested_throttle_;
    external_resisting_torque_nm_ = other.external_resisting_torque_nm_;
    return *this;
}

std::optional<ScheduledScenarioControls> ScenarioControlCursor::next() noexcept {
    if (!storage_ || completed()) {
        return std::nullopt;
    }

    const auto sample_index = first_step_index_ + next_sample_offset_;
    while (next_operating_state_boundary_ < storage_->operating_state.size() &&
           storage_->operating_state[next_operating_state_boundary_].step_index <=
               sample_index) {
        operating_state_ =
            storage_->operating_state[next_operating_state_boundary_].state;
        ++next_operating_state_boundary_;
    }
    while (next_throttle_boundary_ < storage_->throttle.size() &&
           storage_->throttle[next_throttle_boundary_].step_index <= sample_index) {
        requested_throttle_ = storage_->throttle[next_throttle_boundary_].value;
        ++next_throttle_boundary_;
    }
    while (next_external_resisting_torque_boundary_ <
               storage_->external_resisting_torque.size() &&
           storage_->external_resisting_torque[next_external_resisting_torque_boundary_]
                   .step_index <= sample_index) {
        external_resisting_torque_nm_ =
            storage_
                ->external_resisting_torque[next_external_resisting_torque_boundary_]
                .value_nm;
        ++next_external_resisting_torque_boundary_;
    }

    ++next_sample_offset_;
    return ScheduledScenarioControls{
        sample_index,        sample_index + 1U,
        requested_throttle_, external_resisting_torque_nm_,
        operating_state_,
    };
}

bool ScenarioControlCursor::completed() const noexcept {
    return !storage_ || next_sample_offset_ >= storage_->sample_count;
}

ScenarioControlSchedule::ScenarioControlSchedule(
    contract::RationalRateHz rate, std::uint64_t first_step_index,
    std::uint64_t sample_count, double initial_theta_rad,
    std::shared_ptr<const detail::ScenarioControlScheduleStorage> storage) noexcept
    : rate_(rate), first_step_index_(first_step_index), sample_count_(sample_count),
      initial_theta_rad_(initial_theta_rad), storage_(std::move(storage)) {}

const contract::RationalRateHz &ScenarioControlSchedule::rate() const noexcept {
    return rate_;
}

std::uint64_t ScenarioControlSchedule::first_step_index() const noexcept {
    return first_step_index_;
}

std::uint64_t ScenarioControlSchedule::sample_count() const noexcept {
    return storage_ == nullptr ? 0 : sample_count_;
}

double ScenarioControlSchedule::initial_theta_rad() const noexcept {
    return initial_theta_rad_;
}

ScenarioControlCursor ScenarioControlSchedule::fresh_cursor() const noexcept {
    return {storage_, first_step_index_};
}

KinematicScenarioCursor::KinematicScenarioCursor(
    std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage,
    std::uint64_t first_step_index) noexcept
    : storage_(std::move(storage)), first_step_index_(first_step_index) {}

KinematicScenarioCursor::KinematicScenarioCursor(
    KinematicScenarioCursor &&other) noexcept
    : storage_(std::move(other.storage_)),
      first_step_index_(std::exchange(other.first_step_index_, 0)),
      next_sample_offset_(std::exchange(other.next_sample_offset_, 0)),
      next_operating_state_boundary_(
          std::exchange(other.next_operating_state_boundary_, 0)),
      next_throttle_boundary_(std::exchange(other.next_throttle_boundary_, 0)),
      next_external_resisting_torque_boundary_(
          std::exchange(other.next_external_resisting_torque_boundary_, 0)),
      operating_state_(other.operating_state_),
      requested_throttle_(other.requested_throttle_),
      external_resisting_torque_nm_(other.external_resisting_torque_nm_) {}

KinematicScenarioCursor &
KinematicScenarioCursor::operator=(KinematicScenarioCursor &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    storage_ = std::move(other.storage_);
    first_step_index_ = std::exchange(other.first_step_index_, 0);
    next_sample_offset_ = std::exchange(other.next_sample_offset_, 0);
    next_operating_state_boundary_ =
        std::exchange(other.next_operating_state_boundary_, 0);
    next_throttle_boundary_ = std::exchange(other.next_throttle_boundary_, 0);
    next_external_resisting_torque_boundary_ =
        std::exchange(other.next_external_resisting_torque_boundary_, 0);
    operating_state_ = other.operating_state_;
    requested_throttle_ = other.requested_throttle_;
    external_resisting_torque_nm_ = other.external_resisting_torque_nm_;
    return *this;
}

std::optional<ScheduledScenarioStep> KinematicScenarioCursor::next() noexcept {
    if (!storage_ || completed()) {
        return std::nullopt;
    }

    const auto sample_index =
        first_step_index_ + static_cast<std::uint64_t>(next_sample_offset_);
    const auto &controls = *storage_->controls;
    while (next_operating_state_boundary_ < controls.operating_state.size() &&
           controls.operating_state[next_operating_state_boundary_].step_index <=
               sample_index) {
        operating_state_ =
            controls.operating_state[next_operating_state_boundary_].state;
        ++next_operating_state_boundary_;
    }
    while (next_throttle_boundary_ < controls.throttle.size() &&
           controls.throttle[next_throttle_boundary_].step_index <= sample_index) {
        requested_throttle_ = controls.throttle[next_throttle_boundary_].value;
        ++next_throttle_boundary_;
    }
    while (next_external_resisting_torque_boundary_ <
               controls.external_resisting_torque.size() &&
           controls.external_resisting_torque[next_external_resisting_torque_boundary_]
                   .step_index <= sample_index) {
        external_resisting_torque_nm_ =
            controls.external_resisting_torque[next_external_resisting_torque_boundary_]
                .value_nm;
        ++next_external_resisting_torque_boundary_;
    }

    const auto step = ScheduledScenarioStep{
        sample_index,
        sample_index + 1,
        std::visit(
            [this](const auto &lane) {
                using Lane = std::decay_t<decltype(lane)>;
                if constexpr (std::is_same_v<Lane, detail::SampledRpmLane>) {
                    return lane
                        .post_step_rpm[static_cast<std::size_t>(next_sample_offset_)];
                } else {
                    return lane.rpm;
                }
            },
            storage_->rpm),
        requested_throttle_,
        external_resisting_torque_nm_,
        operating_state_,
    };
    ++next_sample_offset_;
    return step;
}

bool KinematicScenarioCursor::completed() const noexcept {
    return !storage_ || !storage_->controls ||
           next_sample_offset_ >= storage_->controls->sample_count;
}

KinematicScenarioSchedule::KinematicScenarioSchedule(
    contract::RationalRateHz rate, std::uint64_t first_step_index,
    contract::RpmSampleSemantics sample_semantics, std::uint64_t sample_count,
    double initial_theta_rad,
    std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage) noexcept
    : rate_(rate), first_step_index_(first_step_index),
      sample_semantics_(sample_semantics), sample_count_(sample_count),
      initial_theta_rad_(initial_theta_rad), storage_(std::move(storage)) {}

const contract::RationalRateHz &KinematicScenarioSchedule::rate() const noexcept {
    return rate_;
}

std::uint64_t KinematicScenarioSchedule::first_step_index() const noexcept {
    return first_step_index_;
}

contract::RpmSampleSemantics
KinematicScenarioSchedule::sample_semantics() const noexcept {
    return sample_semantics_;
}

std::uint64_t KinematicScenarioSchedule::sample_count() const noexcept {
    return storage_ == nullptr ? 0 : sample_count_;
}

double KinematicScenarioSchedule::initial_theta_rad() const noexcept {
    return initial_theta_rad_;
}

std::optional<double> KinematicScenarioSchedule::rpm_at_sample_offset(
    std::uint64_t sample_offset) const noexcept {
    if (storage_ == nullptr || sample_offset >= sample_count_) {
        return std::nullopt;
    }
    return std::visit(
        [sample_offset](const auto &lane) {
            using Lane = std::decay_t<decltype(lane)>;
            if constexpr (std::is_same_v<Lane, detail::SampledRpmLane>) {
                return lane.post_step_rpm[static_cast<std::size_t>(sample_offset)];
            } else {
                return lane.rpm;
            }
        },
        storage_->rpm);
}

ScenarioControlSchedule KinematicScenarioSchedule::control_schedule() const noexcept {
    return detail::ScenarioControlScheduleFactory::make(
        rate_, first_step_index_, sample_count_, initial_theta_rad_,
        storage_ == nullptr ? nullptr : storage_->controls);
}

KinematicScenarioCursor KinematicScenarioSchedule::fresh_cursor() const noexcept {
    return {storage_, first_step_index_};
}

ScenarioControlScheduleResult
compile_scenario_control_schedule(const contract::RenderScenario &scenario) {
    ValidationReport report;
    append_rate_report(report, scenario.rates.physics, "scenario.rates.physics");

    const auto horizon = contract::resolve_frame_index(scenario.total_duration_s.value,
                                                       scenario.rates.physics);
    if (!horizon.has_value() || *horizon == 0) {
        add_issue(report, ContractIssueCode::inconsistent_semantics,
                  "scenario.total_duration_s.value",
                  "total duration must resolve to a positive integral physics-step "
                  "horizon");
    }

    double initial_theta_rad = 0.0;
    std::vector<detail::OperatingStateBoundary> operating_state;
    std::vector<detail::ThrottleBoundary> throttle;
    std::vector<detail::ExternalResistingTorqueBoundary> external_resisting_torque;
    if (const auto *held = std::get_if<contract::HeldSpeed>(&scenario.mode)) {
        initial_theta_rad = held->initial_theta_rad.value;
        if (!std::isfinite(initial_theta_rad)) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.initial_theta_rad.value",
                      "initial crank angle must be finite");
        }
        if (!std::isfinite(held->throttle_01.value) || held->throttle_01.value < 0.0 ||
            held->throttle_01.value > 1.0) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.throttle_01.value",
                      "throttle must be finite and in [0, 1]");
        }
        throttle.push_back({0, held->throttle_01.value});
    } else if (const auto *sweep =
                   std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode)) {
        initial_theta_rad = sweep->trajectory.initial_theta_rad.value;
        if (!std::isfinite(initial_theta_rad)) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.trajectory.initial_theta_rad.value",
                      "initial crank angle must be finite");
        }
        compile_throttle_boundaries(report, scenario, sweep->throttle_01, horizon,
                                    throttle);
    } else if (const auto *dyno = std::get_if<contract::InertialDyno>(&scenario.mode)) {
        initial_theta_rad = dyno->initial_theta_rad.value;
        if (!std::isfinite(initial_theta_rad)) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.initial_theta_rad.value",
                      "initial crank angle must be finite");
        }
        compile_throttle_boundaries(report, scenario, dyno->throttle_01, horizon,
                                    throttle);
    } else if (const auto *free_engine =
                   std::get_if<contract::FreeEngine>(&scenario.mode)) {
        initial_theta_rad = free_engine->initial_theta_rad.value;
        if (!std::isfinite(initial_theta_rad)) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.initial_theta_rad.value",
                      "initial crank angle must be finite");
        }
        compile_throttle_boundaries(report, scenario, free_engine->throttle_01, horizon,
                                    throttle);
        compile_external_resisting_torque_boundaries(
            report, scenario, free_engine->external_resisting_torque_nm, horizon,
            external_resisting_torque);
    } else {
        add_issue(report, ContractIssueCode::unsupported_value, "scenario.mode",
                  "control scheduling supports only HeldSpeed, "
                  "PrescribedKinematicSweep, InertialDyno, and FreeEngine modes");
        return report;
    }

    compile_operating_state_boundaries(report, scenario, horizon, operating_state);
    if (!report.ok()) {
        return report;
    }

    auto mutable_storage = std::make_shared<detail::ScenarioControlScheduleStorage>();
    mutable_storage->sample_count = *horizon;
    mutable_storage->operating_state = std::move(operating_state);
    mutable_storage->throttle = std::move(throttle);
    mutable_storage->external_resisting_torque = std::move(external_resisting_torque);
    std::shared_ptr<const detail::ScenarioControlScheduleStorage> storage =
        std::move(mutable_storage);
    return detail::ScenarioControlScheduleFactory::make(
        scenario.rates.physics, 0U, *horizon, initial_theta_rad, std::move(storage));
}

KinematicScenarioScheduleResult
compile_kinematic_scenario_schedule(const contract::RenderScenario &scenario) {
    ValidationReport report;
    append_rate_report(report, scenario.rates.physics, "scenario.rates.physics");

    const auto horizon = contract::resolve_frame_index(scenario.total_duration_s.value,
                                                       scenario.rates.physics);
    if (!horizon.has_value() || *horizon == 0) {
        add_issue(report, ContractIssueCode::inconsistent_semantics,
                  "scenario.total_duration_s.value",
                  "total duration must resolve to a positive integral physics-step "
                  "horizon");
    }

    detail::RpmLane rpm_lane;
    std::uint64_t first_step_index = 0;
    auto semantics = contract::RpmSampleSemantics::post_step_rpm;
    std::uint64_t sample_count = horizon.value_or(0);
    double initial_theta_rad = 0.0;
    std::vector<detail::OperatingStateBoundary> operating_state;
    std::vector<detail::ThrottleBoundary> throttle;

    if (const auto *held = std::get_if<contract::HeldSpeed>(&scenario.mode)) {
        if (!std::isfinite(held->engine_speed_rpm.value) ||
            held->engine_speed_rpm.value <= 0.0) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.engine_speed_rpm.value",
                      "held speed must be finite and positive");
        }
        if (!std::isfinite(held->initial_theta_rad.value)) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.initial_theta_rad.value",
                      "initial crank angle must be finite");
        }
        if (!std::isfinite(held->throttle_01.value) || held->throttle_01.value < 0.0 ||
            held->throttle_01.value > 1.0) {
            add_issue(report, ContractIssueCode::invalid_value,
                      "scenario.mode.throttle_01.value",
                      "throttle must be finite and in [0, 1]");
        }

        rpm_lane = detail::ConstantRpmLane{held->engine_speed_rpm.value};
        initial_theta_rad = held->initial_theta_rad.value;
        throttle.push_back({0, held->throttle_01.value});
    } else if (const auto *sweep =
                   std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode)) {
        if (sweep->trajectory.kinematic_resolution.value.id !=
                "fixed-rate-post-step-rpm-binary64-v1" ||
            sweep->trajectory.kinematic_resolution.value.version != 1U) {
            add_issue(report, ContractIssueCode::unsupported_value,
                      "scenario.mode.trajectory.kinematic_resolution",
                      "prescribed scheduling requires "
                      "fixed-rate-post-step-rpm-binary64-v1 version 1");
        }
        const auto *rpm =
            std::get_if<contract::FixedRateRpmTrajectory>(&sweep->trajectory.rpm);
        if (rpm == nullptr) {
            add_issue(report, ContractIssueCode::unsupported_value,
                      "scenario.mode.trajectory.rpm",
                      "kinematic scheduling requires a fixed-rate RPM trajectory");
            return report;
        }
        std::vector<double> rpm_snapshot = rpm->post_step_rpm;

        append_rate_report(report, rpm->rate, "scenario.mode.trajectory.rpm.rate");
        if (rpm->rate != scenario.rates.physics) {
            add_issue(report, ContractIssueCode::inconsistent_semantics,
                      "scenario.mode.trajectory.rpm.rate",
                      "fixed-rate RPM trajectory rate must equal the physics rate");
        }
        if (rpm->first_step_index != 0) {
            add_issue(report, ContractIssueCode::inconsistent_semantics,
                      "scenario.mode.trajectory.rpm.first_step_index",
                      "fixed-rate RPM trajectory must begin at physics step zero");
        }
        if (rpm->semantics != contract::RpmSampleSemantics::post_step_rpm) {
            add_issue(report, ContractIssueCode::unsupported_value,
                      "scenario.mode.trajectory.rpm.semantics",
                      "kinematic scheduling supports only post-step RPM samples");
        }
        if (horizon.has_value() && rpm_snapshot.size() != *horizon) {
            add_issue(
                report, ContractIssueCode::inconsistent_shape,
                "scenario.mode.trajectory.rpm.post_step_rpm",
                "fixed-rate RPM sample count must equal the physics-step horizon");
        }
        for (std::size_t index = 0; index < rpm_snapshot.size(); ++index) {
            const auto value = rpm_snapshot[index];
            if (!std::isfinite(value)) {
                add_issue(report, ContractIssueCode::invalid_value,
                          "scenario.mode.trajectory.rpm.post_step_rpm[" +
                              std::to_string(index) + "]",
                          "post-step RPM must be finite");
            } else if (value < 0.0) {
                add_issue(report, ContractIssueCode::invalid_value,
                          "scenario.mode.trajectory.rpm.post_step_rpm[" +
                              std::to_string(index) + "]",
                          "post-step RPM must be nonnegative");
            }
        }
        if (contract::canonical_binary64_le_sha256(rpm_snapshot) !=
            rpm->samples_f64le_sha256) {
            add_issue(report, ContractIssueCode::inconsistent_semantics,
                      "scenario.mode.trajectory.rpm.samples_f64le_sha256",
                      "fixed-rate RPM sample hash does not match the captured lane");
        }

        first_step_index = rpm->first_step_index;
        semantics = rpm->semantics;
        sample_count = static_cast<std::uint64_t>(rpm_snapshot.size());
        initial_theta_rad = sweep->trajectory.initial_theta_rad.value;
        rpm_lane = detail::SampledRpmLane{std::move(rpm_snapshot)};
        compile_throttle_boundaries(report, scenario, sweep->throttle_01, horizon,
                                    throttle);
    } else {
        add_issue(report, ContractIssueCode::unsupported_value, "scenario.mode",
                  "kinematic scheduling supports only HeldSpeed and "
                  "PrescribedKinematicSweep modes");
        return report;
    }

    compile_operating_state_boundaries(report, scenario, horizon, operating_state);

    if (!report.ok()) {
        return report;
    }

    auto mutable_control_storage =
        std::make_shared<detail::ScenarioControlScheduleStorage>();
    mutable_control_storage->sample_count = sample_count;
    mutable_control_storage->operating_state = std::move(operating_state);
    mutable_control_storage->throttle = std::move(throttle);
    std::shared_ptr<const detail::ScenarioControlScheduleStorage> control_storage =
        std::move(mutable_control_storage);

    auto mutable_storage = std::make_shared<detail::KinematicScenarioScheduleStorage>();
    mutable_storage->rpm = std::move(rpm_lane);
    mutable_storage->controls = std::move(control_storage);
    std::shared_ptr<const detail::KinematicScenarioScheduleStorage> storage =
        std::move(mutable_storage);
    return detail::KinematicScenarioScheduleFactory::make(
        scenario.rates.physics, first_step_index, semantics, sample_count,
        initial_theta_rad, std::move(storage));
}

} // namespace engine_sim_offline::simulation
