#include "acoustics/unflanged_pipe_outlet.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::acoustics {
namespace {

constexpr double kSilvaNumeratorFirstOrder = 0.167;
constexpr double kSilvaDenominatorFirstOrder = 1.393;
constexpr double kSilvaDenominatorSecondOrder = 0.457;
constexpr std::size_t kPassivityFrequencyIntervals = 65536;
constexpr double kPassivityTolerance = 1.0e-12;

void require_positive_finite(double value, const char *message) {
    if (!std::isfinite(value) || !(value > 0.0)) {
        throw std::invalid_argument{message};
    }
}

void require_finite_result(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

[[nodiscard]] std::size_t observer_history_size(double delay_frames) {
    if (!std::isfinite(delay_frames) || delay_frames < 0.0) {
        throw std::invalid_argument{
            "outlet observer delay must be finite and nonnegative"};
    }
    constexpr auto maximum_integer_delay =
        std::numeric_limits<std::size_t>::max() / std::size_t{2};
    if (delay_frames > static_cast<double>(maximum_integer_delay)) {
        throw std::invalid_argument{
            "outlet observer delay is too large for addressable history"};
    }
    return static_cast<std::size_t>(std::floor(delay_frames)) + std::size_t{2};
}

[[nodiscard]] double pole_magnitude(const std::complex<double> &pole) {
    if (!std::isfinite(pole.real()) || !std::isfinite(pole.imag())) {
        throw std::invalid_argument{"outlet reflection pole was non-finite"};
    }
    const double magnitude = std::abs(pole);
    if (!std::isfinite(magnitude) || !(magnitude < 1.0)) {
        throw std::invalid_argument{
            "outlet reflection pole was not strictly inside the unit circle"};
    }
    return magnitude;
}

[[nodiscard]] double
sampled_maximum_reflection_magnitude(const DigitalReflectionBiquad &filter) {
    double maximum_magnitude = 0.0;
    for (std::size_t index = 0; index <= kPassivityFrequencyIntervals; ++index) {
        const double omega = std::numbers::pi * static_cast<double>(index) /
                             static_cast<double>(kPassivityFrequencyIntervals);
        const std::complex<double> z_inverse = std::polar(1.0, -omega);
        const std::complex<double> z_inverse_squared = z_inverse * z_inverse;
        const std::complex<double> numerator =
            filter.b0 + filter.b1 * z_inverse + filter.b2 * z_inverse_squared;
        const std::complex<double> denominator =
            1.0 + filter.a1 * z_inverse + filter.a2 * z_inverse_squared;
        const double denominator_magnitude = std::abs(denominator);
        if (!std::isfinite(denominator_magnitude) || !(denominator_magnitude > 0.0)) {
            throw std::invalid_argument{
                "outlet reflection response had a singular digital denominator"};
        }
        const double magnitude = std::abs(numerator / denominator);
        if (!std::isfinite(magnitude)) {
            throw std::invalid_argument{
                "outlet reflection response magnitude was non-finite"};
        }
        maximum_magnitude = std::max(maximum_magnitude, magnitude);
    }
    if (maximum_magnitude > 1.0 + kPassivityTolerance) {
        throw std::invalid_argument{
            "outlet reflection response exceeded its passive magnitude bound"};
    }
    return maximum_magnitude;
}

} // namespace

UnflangedPipeOutletProperties
resolve_unflanged_pipe_outlet(const UnflangedPipeOutletParameters &parameters) {
    require_positive_finite(parameters.outlet_radius_m,
                            "outlet radius must be positive and finite");
    require_positive_finite(parameters.pipe_sound_speed_m_s,
                            "outlet pipe sound speed must be positive and finite");
    require_positive_finite(
        parameters.characteristic_impedance_pa_s_m3,
        "outlet characteristic impedance must be positive and finite");
    require_positive_finite(parameters.sample_rate_hz,
                            "outlet sample rate must be positive and finite");
    require_positive_finite(parameters.ambient_density_kg_m3,
                            "ambient density must be positive and finite");
    require_positive_finite(parameters.ambient_sound_speed_m_s,
                            "ambient sound speed must be positive and finite");
    require_positive_finite(parameters.observation_distance_m,
                            "observation distance must be positive and finite");

    const double radius_time_s =
        parameters.outlet_radius_m / parameters.pipe_sound_speed_m_s;
    const double bilinear_rate_per_s = 2.0 * parameters.sample_rate_hz;
    const double normalized_rate = radius_time_s * bilinear_rate_per_s;
    const double normalized_rate_squared = normalized_rate * normalized_rate;
    const double numerator_first_order = kSilvaNumeratorFirstOrder * normalized_rate;
    const double denominator_first_order =
        kSilvaDenominatorFirstOrder * normalized_rate;
    const double denominator_second_order =
        kSilvaDenominatorSecondOrder * normalized_rate_squared;
    const double denominator_zero =
        1.0 + denominator_first_order + denominator_second_order;

    if (!std::isfinite(radius_time_s) || !std::isfinite(normalized_rate) ||
        !std::isfinite(normalized_rate_squared) ||
        !std::isfinite(numerator_first_order) ||
        !std::isfinite(denominator_first_order) ||
        !std::isfinite(denominator_second_order) || !std::isfinite(denominator_zero) ||
        !(denominator_zero > 0.0)) {
        throw std::invalid_argument{
            "outlet reflection bilinear coefficients did not resolve finitely"};
    }

    const DigitalReflectionBiquad reflection{
        -(1.0 + numerator_first_order) / denominator_zero,
        -2.0 / denominator_zero,
        -(1.0 - numerator_first_order) / denominator_zero,
        (2.0 - 2.0 * denominator_second_order) / denominator_zero,
        (1.0 - denominator_first_order + denominator_second_order) / denominator_zero,
    };
    if (!std::isfinite(reflection.b0) || !std::isfinite(reflection.b1) ||
        !std::isfinite(reflection.b2) || !std::isfinite(reflection.a1) ||
        !std::isfinite(reflection.a2)) {
        throw std::invalid_argument{
            "outlet reflection normalized coefficients were non-finite"};
    }

    const std::complex<double> discriminant =
        std::complex<double>{reflection.a1 * reflection.a1 - 4.0 * reflection.a2, 0.0};
    const std::complex<double> square_root = std::sqrt(discriminant);
    const double first_pole_magnitude =
        pole_magnitude((-reflection.a1 + square_root) * 0.5);
    const double second_pole_magnitude =
        pole_magnitude((-reflection.a1 - square_root) * 0.5);
    const double maximum_sampled_reflection_magnitude =
        sampled_maximum_reflection_magnitude(reflection);

    const double observer_delay_frames = parameters.observation_distance_m *
                                         parameters.sample_rate_hz /
                                         parameters.ambient_sound_speed_m_s;
    static_cast<void>(observer_history_size(observer_delay_frames));
    const double monopole_far_field_scale =
        parameters.ambient_density_kg_m3 /
        (4.0 * std::numbers::pi * parameters.observation_distance_m);
    if (!std::isfinite(monopole_far_field_scale) || !(monopole_far_field_scale > 0.0)) {
        throw std::invalid_argument{
            "outlet monopole far-field scale did not resolve positively and finitely"};
    }

    return {
        radius_time_s,
        reflection,
        first_pole_magnitude,
        second_pole_magnitude,
        maximum_sampled_reflection_magnitude,
        kPassivityFrequencyIntervals + std::size_t{1},
        observer_delay_frames,
        monopole_far_field_scale,
    };
}

UnflangedPipeOutlet::UnflangedPipeOutlet(UnflangedPipeOutletParameters parameters)
    : parameters_(std::move(parameters)),
      properties_(resolve_unflanged_pipe_outlet(parameters_)),
      observer_integer_delay_frames_(
          static_cast<std::size_t>(std::floor(properties_.observer_delay_frames))),
      observer_fractional_delay_frames_(
          properties_.observer_delay_frames -
          static_cast<double>(observer_integer_delay_frames_)),
      observer_history_(observer_history_size(properties_.observer_delay_frames), 0.0) {
    if (!std::isfinite(observer_fractional_delay_frames_) ||
        observer_fractional_delay_frames_ < 0.0 ||
        observer_fractional_delay_frames_ >= 1.0) {
        throw std::invalid_argument{
            "outlet observer fractional delay did not resolve inside [0, 1)"};
    }
}

double UnflangedPipeOutlet::preview_observer_delay(double input) const {
    const auto newer_index = (observer_write_index_ + observer_history_.size() -
                              observer_integer_delay_frames_) %
                             observer_history_.size();
    const auto older_index = (newer_index + observer_history_.size() - std::size_t{1}) %
                             observer_history_.size();
    const double newer =
        observer_integer_delay_frames_ == 0U ? input : observer_history_[newer_index];
    const double output = std::lerp(newer, observer_history_[older_index],
                                    observer_fractional_delay_frames_);
    require_finite_result(output, "outlet observer delayed pressure became non-finite");
    return output;
}

void UnflangedPipeOutlet::commit_observer_delay(double input) noexcept {
    observer_history_[observer_write_index_] = input;
    ++observer_write_index_;
    if (observer_write_index_ == observer_history_.size()) {
        observer_write_index_ = 0;
    }
}

UnflangedPipeOutletFrame
UnflangedPipeOutlet::process(double incident_pressure_wave_pa) {
    if (!std::isfinite(incident_pressure_wave_pa)) {
        throw std::domain_error{"outlet incident pressure wave must be finite"};
    }

    const auto &filter = properties_.reflection;
    const double reflected_pressure_wave_pa =
        filter.b0 * incident_pressure_wave_pa + filter.b1 * incident_z1_pa_ +
        filter.b2 * incident_z2_pa_ - filter.a1 * reflected_z1_pa_ -
        filter.a2 * reflected_z2_pa_;
    require_finite_result(reflected_pressure_wave_pa,
                          "outlet reflected pressure wave became non-finite");

    const double outlet_volume_velocity_m3_s =
        (incident_pressure_wave_pa - reflected_pressure_wave_pa) /
        parameters_.characteristic_impedance_pa_s_m3;
    require_finite_result(outlet_volume_velocity_m3_s,
                          "outlet volume velocity became non-finite");

    const double volume_velocity_delta =
        outlet_volume_velocity_m3_s - previous_outlet_volume_velocity_m3_s_;
    require_finite_result(volume_velocity_delta,
                          "outlet volume-velocity difference became non-finite");
    const double volume_velocity_derivative_m3_s2 =
        parameters_.sample_rate_hz * volume_velocity_delta;
    require_finite_result(volume_velocity_derivative_m3_s2,
                          "outlet volume-velocity derivative became non-finite");
    const double undelayed_radiated_pressure_pa =
        properties_.monopole_far_field_scale_kg_per_m4 *
        volume_velocity_derivative_m3_s2;
    require_finite_result(undelayed_radiated_pressure_pa,
                          "outlet radiated pressure became non-finite");
    const double radiated_pressure_pa =
        preview_observer_delay(undelayed_radiated_pressure_pa);

    // Commit all recursive and delay state only after the complete output frame
    // has passed its finite checks.
    incident_z2_pa_ = incident_z1_pa_;
    incident_z1_pa_ = incident_pressure_wave_pa;
    reflected_z2_pa_ = reflected_z1_pa_;
    reflected_z1_pa_ = reflected_pressure_wave_pa;
    previous_outlet_volume_velocity_m3_s_ = outlet_volume_velocity_m3_s;
    commit_observer_delay(undelayed_radiated_pressure_pa);

    return {
        reflected_pressure_wave_pa,
        outlet_volume_velocity_m3_s,
        radiated_pressure_pa,
    };
}

const UnflangedPipeOutletParameters &UnflangedPipeOutlet::parameters() const noexcept {
    return parameters_;
}

const UnflangedPipeOutletProperties &UnflangedPipeOutlet::properties() const noexcept {
    return properties_;
}

} // namespace engine_sim_offline::acoustics
