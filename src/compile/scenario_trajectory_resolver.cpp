#include "compile/scenario_resolver_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace crankwave::compile::detail::scenario_resolution {

contract::ScalarTrajectory
ScenarioResolver::scalar_trajectory(const authoring::ScalarTrajectory &input,
                                    std::string_view path, bool require_unit_interval,
                                    bool require_current_scheduler_hold) {
    contract::ScalarTrajectory output;
    output.interpolation = contract_interpolation(input.interpolation);
    if (require_current_scheduler_hold &&
        input.interpolation !=
            authoring::TrajectoryInterpolation::right_continuous_hold) {
        add(authoring::DiagnosticCode::unsupported_capability,
            std::string{path} + "/interpolation",
            "the current executable scheduler supports only "
            "right-continuous-hold throttle interpolation");
    }
    if (input.points.empty()) {
        add(authoring::DiagnosticCode::missing_value, std::string{path} + "/points",
            "trajectory must contain a time-zero point");
        return output;
    }

    output.points.reserve(input.points.size());
    std::optional<std::uint64_t> previous_frame;
    for (std::size_t index = 0; index < input.points.size(); ++index) {
        const auto point_path = std::string{path} + "/points/" + std::to_string(index);
        const auto time_s =
            quantity(input.points[index].time, authoring::QuantityDimension::duration,
                     point_path + "/time");
        const auto frame = physics_frame(time_s, point_path + "/time");
        const auto value = input.points[index].value;
        if (!std::isfinite(value) ||
            (require_unit_interval && (value < 0.0 || value > 1.0))) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/value",
                require_unit_interval ? "trajectory value must be finite and in [0, 1]"
                                      : "trajectory value must be finite");
        }
        if (index == 0U && (!frame.has_value() || *frame != 0U)) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "right-continuous trajectory must begin at time zero");
        }
        if (previous_frame.has_value() && frame.has_value() &&
            *frame <= *previous_frame) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "trajectory boundaries must be strictly increasing on the "
                "physics clock");
        }
        if (frame.has_value() && *frame > request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/time",
                "trajectory point occurs after the scenario horizon");
        }
        if (require_current_scheduler_hold && index != 0U && frame.has_value() &&
            *frame == request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::unsupported_capability, point_path + "/time",
                "a throttle boundary at the final physics horizon is never "
                "observed by the current scheduler");
        }
        output.points.push_back({time_s, value});
        previous_frame = frame;
    }
    if (input.interpolation == authoring::TrajectoryInterpolation::linear) {
        const auto last_frame = contract::resolve_frame_index(
            output.points.back().time_s, scenario_.rates.physics);
        if (!last_frame.has_value() ||
            *last_frame != request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::inconsistent_value,
                std::string{path} + "/points",
                "linear trajectory must explicitly reach total_duration");
        }
    }
    return output;
}

contract::ScalarTrajectory
ScenarioResolver::torque_trajectory(const authoring::QuantityTrajectory &input,
                                    std::string_view path) {
    contract::ScalarTrajectory output;
    output.interpolation = contract_interpolation(input.interpolation);
    if (input.value_dimension != authoring::QuantityDimension::torque) {
        add(authoring::DiagnosticCode::invalid_value,
            std::string{path} + "/value_dimension",
            "external resisting-torque trajectory must use torque values");
    }
    if (input.interpolation !=
        authoring::TrajectoryInterpolation::right_continuous_hold) {
        add(authoring::DiagnosticCode::unsupported_capability,
            std::string{path} + "/interpolation",
            "free-engine execution currently supports only "
            "right-continuous-hold resisting torque");
    }
    if (input.points.empty()) {
        add(authoring::DiagnosticCode::missing_value, std::string{path} + "/points",
            "resisting-torque trajectory must contain a time-zero point");
        return output;
    }

    output.points.reserve(input.points.size());
    std::optional<std::uint64_t> previous_frame;
    for (std::size_t index = 0; index < input.points.size(); ++index) {
        const auto point_path = std::string{path} + "/points/" + std::to_string(index);
        const auto time_s =
            quantity(input.points[index].time, authoring::QuantityDimension::duration,
                     point_path + "/time");
        const auto frame = physics_frame(time_s, point_path + "/time");
        const auto value =
            quantity(input.points[index].value, authoring::QuantityDimension::torque,
                     point_path + "/value");
        if (!std::isfinite(value) || value < 0.0) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/value",
                "external resisting torque must be finite and nonnegative");
        }
        if (index == 0U && (!frame.has_value() || *frame != 0U)) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "resisting-torque trajectory must begin at time zero");
        }
        if (previous_frame.has_value() && frame.has_value() &&
            *frame <= *previous_frame) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "resisting-torque boundaries must be strictly increasing on the "
                "physics clock");
        }
        if (frame.has_value() && *frame > request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/time",
                "resisting-torque point occurs after the scenario horizon");
        }
        if (index != 0U && frame.has_value() &&
            *frame == request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::unsupported_capability, point_path + "/time",
                "a resisting-torque boundary at the final physics horizon is never "
                "observed by the current scheduler");
        }
        output.points.push_back({time_s, value});
        previous_frame = frame;
    }
    return output;
}

ConvertedQuantityTrajectory
ScenarioResolver::speed_trajectory(const authoring::QuantityTrajectory &input,
                                   std::string_view path) {
    ConvertedQuantityTrajectory output;
    output.interpolation = input.interpolation;
    if (input.value_dimension != authoring::QuantityDimension::angular_speed) {
        add(authoring::DiagnosticCode::invalid_value,
            std::string{path} + "/value_dimension",
            "external engine-speed trajectory must use angular-speed values");
    }
    if (input.points.empty()) {
        add(authoring::DiagnosticCode::missing_value, std::string{path} + "/points",
            "engine-speed trajectory must contain a time-zero point");
        return output;
    }

    output.points.reserve(input.points.size());
    std::optional<std::uint64_t> previous_frame;
    for (std::size_t index = 0; index < input.points.size(); ++index) {
        const auto point_path = std::string{path} + "/points/" + std::to_string(index);
        const auto time_s =
            quantity(input.points[index].time, authoring::QuantityDimension::duration,
                     point_path + "/time");
        const auto frame = physics_frame(time_s, point_path + "/time");
        const auto rpm =
            engine_speed_rpm(input.points[index].value, point_path + "/value");
        if (!std::isfinite(rpm) || rpm < 0.0) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/value",
                "engine speed must be finite and nonnegative");
        }
        if (index == 0U && (!frame.has_value() || *frame != 0U)) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "engine-speed trajectory must begin at time zero");
        }
        if (previous_frame.has_value() && frame.has_value() &&
            *frame <= *previous_frame) {
            add(authoring::DiagnosticCode::inconsistent_value, point_path + "/time",
                "engine-speed boundaries must be strictly increasing on the "
                "physics clock");
        }
        if (frame.has_value() && *frame > request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::out_of_range, point_path + "/time",
                "engine-speed point occurs after the scenario horizon");
        }
        output.points.push_back({time_s, frame.value_or(0U), rpm});
        previous_frame = frame;
    }
    if (input.interpolation == authoring::TrajectoryInterpolation::linear &&
        output.points.back().frame != request_input_.total_physics_frames) {
        add(authoring::DiagnosticCode::inconsistent_value,
            std::string{path} + "/points",
            "linear engine-speed trajectory must explicitly reach "
            "total_duration");
    }
    return output;
}

contract::FixedRateRpmTrajectory
ScenarioResolver::materialize_rpm_lane(const ConvertedQuantityTrajectory &trajectory) {
    contract::FixedRateRpmTrajectory output;
    output.rate = scenario_.rates.physics;
    output.first_step_index = 0;
    output.semantics = contract::RpmSampleSemantics::post_step_rpm;
    if (request_input_.total_physics_frames >
        context_.limits.maximum_fixed_rate_trajectory_frames) {
        add(authoring::DiagnosticCode::resource_limit, "/mode/engine_speed",
            "external-speed trajectory exceeds the configured fixed-rate "
            "materialization limit");
        return output;
    }
    if (request_input_.total_physics_frames >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        add(authoring::DiagnosticCode::resource_limit, "/mode/engine_speed",
            "external-speed trajectory cannot be represented by this build");
        return output;
    }
    if (trajectory.points.empty()) {
        return output;
    }

    output.post_step_rpm.resize(
        static_cast<std::size_t>(request_input_.total_physics_frames));
    std::size_t left = 0U;
    for (std::uint64_t offset = 0U; offset < request_input_.total_physics_frames;
         ++offset) {
        const auto target_frame = offset + 1U;
        while (left + 1U < trajectory.points.size() &&
               trajectory.points[left + 1U].frame <= target_frame) {
            ++left;
        }

        double value = trajectory.points[left].value;
        if (trajectory.interpolation == authoring::TrajectoryInterpolation::linear &&
            left + 1U < trajectory.points.size()) {
            const auto &start = trajectory.points[left];
            const auto &end = trajectory.points[left + 1U];
            const auto numerator = static_cast<long double>(target_frame - start.frame);
            const auto denominator = static_cast<long double>(end.frame - start.frame);
            const auto alpha = numerator / denominator;
            value =
                static_cast<double>(static_cast<long double>(start.value) +
                                    alpha * (static_cast<long double>(end.value) -
                                             static_cast<long double>(start.value)));
        }
        output.post_step_rpm[static_cast<std::size_t>(offset)] = value;
    }
    output.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(output.post_step_rpm);
    return output;
}

} // namespace crankwave::compile::detail::scenario_resolution
