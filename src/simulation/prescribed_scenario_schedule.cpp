#include "prescribed_scenario_schedule.hpp"

#include <cmath>
#include <limits>
#include <string>
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

struct PrescribedScenarioScheduleStorage {
    std::vector<double> post_step_rpm;
    std::vector<OperatingStateBoundary> operating_state;
    std::vector<ThrottleBoundary> throttle;
};

struct PrescribedScenarioScheduleFactory {
    static PrescribedScenarioSchedule
    make(const contract::FixedRateRpmTrajectory &rpm,
         std::shared_ptr<const PrescribedScenarioScheduleStorage> storage) noexcept {
        return {rpm.rate, rpm.first_step_index, rpm.semantics, std::move(storage)};
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
                  "boundary must not exceed the prescribed scenario horizon");
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

} // namespace

PrescribedScenarioCursor::PrescribedScenarioCursor(
    std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage,
    std::uint64_t first_step_index) noexcept
    : storage_(std::move(storage)), first_step_index_(first_step_index) {}

PrescribedScenarioCursor::PrescribedScenarioCursor(
    PrescribedScenarioCursor &&other) noexcept
    : storage_(std::move(other.storage_)),
      first_step_index_(std::exchange(other.first_step_index_, 0)),
      next_sample_offset_(std::exchange(other.next_sample_offset_, 0)),
      next_operating_state_boundary_(
          std::exchange(other.next_operating_state_boundary_, 0)),
      next_throttle_boundary_(std::exchange(other.next_throttle_boundary_, 0)),
      operating_state_(other.operating_state_),
      requested_throttle_(other.requested_throttle_) {}

PrescribedScenarioCursor &
PrescribedScenarioCursor::operator=(PrescribedScenarioCursor &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    storage_ = std::move(other.storage_);
    first_step_index_ = std::exchange(other.first_step_index_, 0);
    next_sample_offset_ = std::exchange(other.next_sample_offset_, 0);
    next_operating_state_boundary_ =
        std::exchange(other.next_operating_state_boundary_, 0);
    next_throttle_boundary_ = std::exchange(other.next_throttle_boundary_, 0);
    operating_state_ = other.operating_state_;
    requested_throttle_ = other.requested_throttle_;
    return *this;
}

std::optional<ScheduledScenarioStep> PrescribedScenarioCursor::next() noexcept {
    if (!storage_ || completed()) {
        return std::nullopt;
    }

    const auto sample_index =
        first_step_index_ + static_cast<std::uint64_t>(next_sample_offset_);
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

    const auto step = ScheduledScenarioStep{
        sample_index,
        sample_index + 1,
        storage_->post_step_rpm[next_sample_offset_],
        requested_throttle_,
        operating_state_,
    };
    ++next_sample_offset_;
    return step;
}

bool PrescribedScenarioCursor::completed() const noexcept {
    return !storage_ || next_sample_offset_ >= storage_->post_step_rpm.size();
}

PrescribedScenarioSchedule::PrescribedScenarioSchedule(
    contract::RationalRateHz rate, std::uint64_t first_step_index,
    contract::RpmSampleSemantics sample_semantics,
    std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage) noexcept
    : rate_(rate), first_step_index_(first_step_index),
      sample_semantics_(sample_semantics), storage_(std::move(storage)) {}

const contract::RationalRateHz &PrescribedScenarioSchedule::rate() const noexcept {
    return rate_;
}

std::uint64_t PrescribedScenarioSchedule::first_step_index() const noexcept {
    return first_step_index_;
}

contract::RpmSampleSemantics
PrescribedScenarioSchedule::sample_semantics() const noexcept {
    return sample_semantics_;
}

std::uint64_t PrescribedScenarioSchedule::sample_count() const noexcept {
    return storage_ == nullptr
               ? 0
               : static_cast<std::uint64_t>(storage_->post_step_rpm.size());
}

std::span<const double> PrescribedScenarioSchedule::post_step_rpm() const noexcept {
    return storage_ == nullptr ? std::span<const double>{}
                               : std::span<const double>{storage_->post_step_rpm};
}

PrescribedScenarioCursor PrescribedScenarioSchedule::fresh_cursor() const noexcept {
    return {storage_, first_step_index_};
}

PrescribedScenarioScheduleResult
compile_prescribed_scenario_schedule(const contract::RenderScenario &scenario) {
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

    const auto *sweep = std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    if (sweep == nullptr) {
        add_issue(report, ContractIssueCode::unsupported_value, "scenario.mode",
                  "prescribed scheduling requires PrescribedKinematicSweep mode");
        return report;
    }

    const auto *rpm =
        std::get_if<contract::FixedRateRpmTrajectory>(&sweep->trajectory.rpm);
    if (rpm == nullptr) {
        add_issue(report, ContractIssueCode::unsupported_value,
                  "scenario.mode.trajectory.rpm",
                  "prescribed scheduling requires a fixed-rate RPM trajectory");
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
                  "prescribed scheduling supports only post-step RPM samples");
    }
    if (horizon.has_value() && rpm_snapshot.size() != *horizon) {
        add_issue(report, ContractIssueCode::inconsistent_shape,
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

    std::vector<detail::OperatingStateBoundary> operating_state;
    std::vector<detail::ThrottleBoundary> throttle;
    compile_operating_state_boundaries(report, scenario, horizon, operating_state);
    compile_throttle_boundaries(report, scenario, sweep->throttle_01, horizon,
                                throttle);

    if (!report.ok()) {
        return report;
    }

    auto mutable_storage =
        std::make_shared<detail::PrescribedScenarioScheduleStorage>();
    mutable_storage->post_step_rpm = std::move(rpm_snapshot);
    mutable_storage->operating_state = std::move(operating_state);
    mutable_storage->throttle = std::move(throttle);
    std::shared_ptr<const detail::PrescribedScenarioScheduleStorage> storage =
        std::move(mutable_storage);
    return detail::PrescribedScenarioScheduleFactory::make(*rpm, std::move(storage));
}

} // namespace engine_sim_offline::simulation
