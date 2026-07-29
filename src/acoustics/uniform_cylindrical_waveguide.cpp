#include "acoustics/uniform_cylindrical_waveguide.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::acoustics {
namespace {

void require_positive_finite(double value, const char *message) {
    if (!std::isfinite(value) || !(value > 0.0)) {
        throw std::invalid_argument{message};
    }
}

void require_finite_input(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

[[nodiscard]] std::size_t delay_history_size(double delay_frames) {
    if (!std::isfinite(delay_frames) || delay_frames < 2.0) {
        throw std::invalid_argument{
            "fractional delay must be finite and at least two frames"};
    }
    constexpr auto maximum_integer_delay =
        std::numeric_limits<std::size_t>::max() / std::size_t{2};
    if (delay_frames > static_cast<double>(maximum_integer_delay)) {
        throw std::invalid_argument{
            "fractional delay is too large for addressable history"};
    }
    return static_cast<std::size_t>(std::floor(delay_frames)) + std::size_t{2};
}

} // namespace

UniformCylindricalWaveguideProperties resolve_uniform_cylindrical_waveguide(
    const UniformCylindricalWaveguideParameters &parameters) {
    require_positive_finite(parameters.length_m,
                            "waveguide length must be positive and finite");
    require_positive_finite(parameters.inner_area_m2,
                            "waveguide inner area must be positive and finite");
    require_positive_finite(parameters.reference_static_pressure_pa_abs,
                            "waveguide reference pressure must be positive and finite");
    require_positive_finite(
        parameters.reference_static_temperature_k,
        "waveguide reference temperature must be positive and finite");
    if (!std::isfinite(parameters.propagation_loss_nepers_per_m) ||
        parameters.propagation_loss_nepers_per_m < 0.0) {
        throw std::invalid_argument{
            "waveguide propagation loss must be finite and nonnegative"};
    }
    require_positive_finite(parameters.sample_rate_hz,
                            "waveguide sample rate must be positive and finite");
    require_positive_finite(
        parameters.universal_gas_constant_j_per_mol_k,
        "waveguide universal gas constant must be positive and finite");
    require_positive_finite(parameters.gas_molar_mass_kg_per_mol,
                            "waveguide gas molar mass must be positive and finite");
    if (!std::isfinite(parameters.heat_capacity_ratio) ||
        !(parameters.heat_capacity_ratio > 1.0)) {
        throw std::invalid_argument{
            "waveguide heat-capacity ratio must be finite and greater than one"};
    }

    const double specific_gas_constant_j_kg_k =
        parameters.universal_gas_constant_j_per_mol_k /
        parameters.gas_molar_mass_kg_per_mol;
    const double density =
        parameters.reference_static_pressure_pa_abs /
        (specific_gas_constant_j_kg_k * parameters.reference_static_temperature_k);
    const double sound_speed =
        std::sqrt(parameters.heat_capacity_ratio * specific_gas_constant_j_kg_k *
                  parameters.reference_static_temperature_k);
    const double characteristic_impedance =
        density * sound_speed / parameters.inner_area_m2;
    const double delay_frames =
        parameters.length_m * parameters.sample_rate_hz / sound_speed;
    const double amplitude_survival =
        std::exp(-parameters.propagation_loss_nepers_per_m * parameters.length_m);

    if (!std::isfinite(density) || !std::isfinite(sound_speed) ||
        !std::isfinite(characteristic_impedance) || !std::isfinite(delay_frames) ||
        !std::isfinite(amplitude_survival)) {
        throw std::invalid_argument{"waveguide derived properties must be finite"};
    }
    if (!(density > 0.0) || !(sound_speed > 0.0) || !(characteristic_impedance > 0.0)) {
        throw std::invalid_argument{
            "waveguide derived gas properties must be positive"};
    }
    if (delay_frames < 2.0) {
        throw std::invalid_argument{
            "waveguide propagation delay must be at least two acoustic frames"};
    }
    if (!(amplitude_survival > 0.0) || amplitude_survival > 1.0) {
        throw std::invalid_argument{
            "waveguide one-way amplitude survival must be in (0, 1]"};
    }

    const auto integer_delay_frames = delay_history_size(delay_frames) - std::size_t{2};
    const double fractional_delay_frames =
        delay_frames - static_cast<double>(integer_delay_frames);
    if (!std::isfinite(fractional_delay_frames) || fractional_delay_frames < 0.0 ||
        fractional_delay_frames >= 1.0) {
        throw std::invalid_argument{
            "waveguide fractional delay did not resolve inside [0, 1)"};
    }

    return {
        density,
        sound_speed,
        characteristic_impedance,
        delay_frames,
        integer_delay_frames,
        fractional_delay_frames,
        amplitude_survival,
    };
}

FixedPassiveFractionalDelay::FixedPassiveFractionalDelay(double delay_frames,
                                                         double amplitude_survival)
    : delay_frames_(delay_frames),
      integer_delay_frames_(delay_history_size(delay_frames) - std::size_t{2}),
      fractional_delay_frames_(delay_frames -
                               static_cast<double>(integer_delay_frames_)),
      amplitude_survival_(amplitude_survival),
      history_(integer_delay_frames_ + std::size_t{2}, 0.0) {
    if (!std::isfinite(delay_frames_) || delay_frames_ < 2.0) {
        throw std::invalid_argument{
            "fractional delay must be finite and at least two frames"};
    }
    if (!std::isfinite(fractional_delay_frames_) || fractional_delay_frames_ < 0.0 ||
        fractional_delay_frames_ >= 1.0) {
        throw std::invalid_argument{"fractional delay remainder must be inside [0, 1)"};
    }
    if (!std::isfinite(amplitude_survival_) || !(amplitude_survival_ > 0.0) ||
        amplitude_survival_ > 1.0) {
        throw std::invalid_argument{
            "fractional-delay amplitude survival must be in (0, 1]"};
    }
}

double FixedPassiveFractionalDelay::process(double input) {
    require_finite_input(input, "fractional-delay input was non-finite");

    history_[write_index_] = input;
    const auto newer_index =
        (write_index_ + history_.size() - integer_delay_frames_) % history_.size();
    const auto older_index =
        (newer_index + history_.size() - std::size_t{1}) % history_.size();
    const double interpolated = std::lerp(history_[newer_index], history_[older_index],
                                          fractional_delay_frames_);
    const double output = amplitude_survival_ * interpolated;

    ++write_index_;
    if (write_index_ == history_.size()) {
        write_index_ = 0;
    }
    return output;
}

double FixedPassiveFractionalDelay::delay_frames() const noexcept {
    return delay_frames_;
}

std::size_t FixedPassiveFractionalDelay::integer_delay_frames() const noexcept {
    return integer_delay_frames_;
}

double FixedPassiveFractionalDelay::fractional_delay_frames() const noexcept {
    return fractional_delay_frames_;
}

double FixedPassiveFractionalDelay::amplitude_survival() const noexcept {
    return amplitude_survival_;
}

UniformCylindricalWaveguide::UniformCylindricalWaveguide(
    UniformCylindricalWaveguideParameters parameters)
    : parameters_(std::move(parameters)),
      properties_(resolve_uniform_cylindrical_waveguide(parameters_)),
      upstream_to_downstream_(properties_.delay_frames,
                              properties_.one_way_amplitude_survival),
      downstream_to_upstream_(properties_.delay_frames,
                              properties_.one_way_amplitude_survival) {}

WaveguideArrivalFrame
UniformCylindricalWaveguide::process(const WaveguideLaunchFrame &launched) {
    require_finite_input(launched.from_upstream_pa,
                         "upstream waveguide launch was non-finite");
    require_finite_input(launched.from_downstream_pa,
                         "downstream waveguide launch was non-finite");

    // Validate both inputs before either direction mutates its history.
    return {
        downstream_to_upstream_.process(launched.from_downstream_pa),
        upstream_to_downstream_.process(launched.from_upstream_pa),
    };
}

const UniformCylindricalWaveguideParameters &
UniformCylindricalWaveguide::parameters() const noexcept {
    return parameters_;
}

const UniformCylindricalWaveguideProperties &
UniformCylindricalWaveguide::properties() const noexcept {
    return properties_;
}

} // namespace engine_sim_offline::acoustics
