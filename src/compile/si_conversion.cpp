#include "compile/si_conversion.hpp"

#include "compile/diagnostics.hpp"

#include <cmath>
#include <exception>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::compile::detail {
namespace {

struct AffineConversion {
    double scale = 1.0;
    double offset = 0.0;
};

[[nodiscard]] std::optional<AffineConversion>
conversion(authoring::QuantityDimension dimension, std::string_view unit) noexcept {
    using enum authoring::QuantityDimension;
    switch (dimension) {
    case dimensionless:
        if (unit == "1") {
            return AffineConversion{};
        }
        break;
    case angle:
        if (unit == "rad") {
            return AffineConversion{};
        }
        if (unit == "deg") {
            return AffineConversion{std::numbers::pi_v<double> / 180.0, 0.0};
        }
        break;
    case angular_speed:
        if (unit == "rad/s") {
            return AffineConversion{};
        }
        if (unit == "rpm") {
            return AffineConversion{2.0 * std::numbers::pi_v<double> / 60.0, 0.0};
        }
        break;
    case area:
        if (unit == "m2") {
            return AffineConversion{};
        }
        if (unit == "cm2") {
            return AffineConversion{1.0e-4, 0.0};
        }
        if (unit == "mm2") {
            return AffineConversion{1.0e-6, 0.0};
        }
        if (unit == "in2") {
            return AffineConversion{0.00064516, 0.0};
        }
        break;
    case density:
        if (unit == "kg/m3") {
            return AffineConversion{};
        }
        if (unit == "g/cm3") {
            return AffineConversion{1000.0, 0.0};
        }
        break;
    case duration:
        if (unit == "s") {
            return AffineConversion{};
        }
        if (unit == "ms") {
            return AffineConversion{1.0e-3, 0.0};
        }
        break;
    case energy_per_mass:
        if (unit == "J/kg") {
            return AffineConversion{};
        }
        if (unit == "kJ/kg") {
            return AffineConversion{1000.0, 0.0};
        }
        if (unit == "MJ/kg") {
            return AffineConversion{1.0e6, 0.0};
        }
        break;
    case force:
        if (unit == "N") {
            return AffineConversion{};
        }
        break;
    case frequency:
        if (unit == "Hz") {
            return AffineConversion{};
        }
        if (unit == "kHz") {
            return AffineConversion{1000.0, 0.0};
        }
        break;
    case length:
        if (unit == "m") {
            return AffineConversion{};
        }
        if (unit == "cm") {
            return AffineConversion{0.01, 0.0};
        }
        if (unit == "mm") {
            return AffineConversion{0.001, 0.0};
        }
        if (unit == "in") {
            return AffineConversion{0.0254, 0.0};
        }
        break;
    case mass:
        if (unit == "kg") {
            return AffineConversion{};
        }
        if (unit == "g") {
            return AffineConversion{0.001, 0.0};
        }
        if (unit == "lb") {
            return AffineConversion{0.45359237, 0.0};
        }
        break;
    case mass_flow_rate:
        if (unit == "kg/s") {
            return AffineConversion{};
        }
        if (unit == "g/s") {
            return AffineConversion{0.001, 0.0};
        }
        break;
    case molar_mass:
        if (unit == "kg/mol") {
            return AffineConversion{};
        }
        if (unit == "g/mol") {
            return AffineConversion{0.001, 0.0};
        }
        break;
    case moment_of_inertia:
        if (unit == "kg*m2") {
            return AffineConversion{};
        }
        break;
    case power:
        if (unit == "W") {
            return AffineConversion{};
        }
        if (unit == "kW") {
            return AffineConversion{1000.0, 0.0};
        }
        break;
    case pressure:
        if (unit == "Pa") {
            return AffineConversion{};
        }
        if (unit == "kPa") {
            return AffineConversion{1000.0, 0.0};
        }
        if (unit == "bar") {
            return AffineConversion{100000.0, 0.0};
        }
        if (unit == "atm") {
            return AffineConversion{101325.0, 0.0};
        }
        if (unit == "psi") {
            return AffineConversion{6894.757293168361, 0.0};
        }
        if (unit == "inHg") {
            return AffineConversion{3386.389, 0.0};
        }
        if (unit == "inH2O") {
            return AffineConversion{249.08891, 0.0};
        }
        break;
    case pressure_per_speed:
        if (unit == "Pa*s/m") {
            return AffineConversion{};
        }
        if (unit == "kPa*s/m") {
            return AffineConversion{1000.0, 0.0};
        }
        if (unit == "bar*s/m") {
            return AffineConversion{100000.0, 0.0};
        }
        break;
    case pressure_per_speed_squared:
        if (unit == "Pa*s2/m2") {
            return AffineConversion{};
        }
        if (unit == "kPa*s2/m2") {
            return AffineConversion{1000.0, 0.0};
        }
        if (unit == "bar*s2/m2") {
            return AffineConversion{100000.0, 0.0};
        }
        break;
    case speed:
        if (unit == "m/s") {
            return AffineConversion{};
        }
        if (unit == "km/h") {
            return AffineConversion{1.0 / 3.6, 0.0};
        }
        if (unit == "mph") {
            return AffineConversion{0.44704, 0.0};
        }
        break;
    case temperature:
        if (unit == "K") {
            return AffineConversion{};
        }
        if (unit == "degC") {
            return AffineConversion{1.0, 273.15};
        }
        break;
    case torque:
        if (unit == "N*m") {
            return AffineConversion{};
        }
        if (unit == "lb*ft") {
            return AffineConversion{1.3558179483314004, 0.0};
        }
        break;
    case volume:
        if (unit == "m3") {
            return AffineConversion{};
        }
        if (unit == "L") {
            return AffineConversion{0.001, 0.0};
        }
        if (unit == "cm3") {
            return AffineConversion{1.0e-6, 0.0};
        }
        break;
    case volume_flow_rate:
        if (unit == "m3/s") {
            return AffineConversion{};
        }
        if (unit == "L/s") {
            return AffineConversion{0.001, 0.0};
        }
        if (unit == "cfm") {
            return AffineConversion{0.0004719474432, 0.0};
        }
        break;
    }
    return std::nullopt;
}

[[nodiscard]] std::variant<FlowCalibrationStandard, authoring::DiagnosticReport>
resolve_flow_standard(const authoring::Quantity &quantity,
                      authoring::QuantityDimension dimension, std::string_view path) {
    if (!quantity.standard) {
        return FlowCalibrationStandard::none;
    }
    const auto standard_path = std::string{path} + "/standard";
    if (dimension != authoring::QuantityDimension::volume_flow_rate) {
        return diagnostic(authoring::DiagnosticCode::invalid_value, standard_path,
                          "flow calibration standard is valid only for a "
                          "volume-flow quantity");
    }
    if (*quantity.standard == "carburetor_1p5_inhg") {
        return FlowCalibrationStandard::carburetor_1p5_inhg;
    }
    if (*quantity.standard == "port_28_inh2o") {
        return FlowCalibrationStandard::port_28_inh2o;
    }
    return diagnostic(authoring::DiagnosticCode::invalid_value, standard_path,
                      "unsupported flow calibration standard '" + *quantity.standard +
                          "'");
}

} // namespace

SiQuantityResult convert_quantity_to_si(const authoring::Quantity &quantity,
                                        authoring::QuantityDimension expected_dimension,
                                        std::string_view json_pointer) noexcept {
    try {
        if (!std::isfinite(quantity.value)) {
            return diagnostic(authoring::DiagnosticCode::invalid_value,
                              std::string{json_pointer} + "/value",
                              "quantity value must be finite");
        }
        const auto affine = conversion(expected_dimension, quantity.unit);
        if (!affine) {
            return diagnostic(
                authoring::DiagnosticCode::invalid_unit,
                std::string{json_pointer} + "/unit",
                "unit '" + quantity.unit +
                    "' is unsupported for the expected quantity dimension");
        }
        const auto standard =
            resolve_flow_standard(quantity, expected_dimension, json_pointer);
        if (const auto *report = std::get_if<authoring::DiagnosticReport>(&standard)) {
            return *report;
        }
        const double value = quantity.value * affine->scale + affine->offset;
        if (!std::isfinite(value)) {
            return diagnostic(authoring::DiagnosticCode::out_of_range,
                              std::string{json_pointer} + "/value",
                              "quantity overflows canonical SI binary64 range");
        }
        if (expected_dimension == authoring::QuantityDimension::temperature &&
            value < 0.0) {
            return diagnostic(authoring::DiagnosticCode::out_of_range,
                              std::string{json_pointer} + "/value",
                              "temperature is below absolute zero");
        }
        return SiQuantity{
            value,
            expected_dimension,
            std::get<FlowCalibrationStandard>(standard),
        };
    } catch (const std::bad_alloc &) {
        return resource_failure("converting a quantity to SI");
    } catch (...) {
        return internal_failure("converting a quantity to SI");
    }
}

SiRateResult convert_rate_to_si(const authoring::RationalRate &rate,
                                std::string_view json_pointer) noexcept {
    try {
        if (rate.unit != "Hz") {
            return diagnostic(authoring::DiagnosticCode::invalid_unit,
                              std::string{json_pointer} + "/unit",
                              "rate unit must be 'Hz'");
        }
        if (rate.numerator == 0U) {
            return diagnostic(authoring::DiagnosticCode::out_of_range,
                              std::string{json_pointer} + "/numerator",
                              "rate numerator must be positive");
        }
        if (rate.denominator == 0U) {
            return diagnostic(authoring::DiagnosticCode::out_of_range,
                              std::string{json_pointer} + "/denominator",
                              "rate denominator must be positive");
        }
        const auto divisor = std::gcd(rate.numerator, rate.denominator);
        return SiRate{
            rate.numerator / divisor,
            rate.denominator / divisor,
        };
    } catch (const std::bad_alloc &) {
        return resource_failure("converting a rate to SI");
    } catch (...) {
        return internal_failure("converting a rate to SI");
    }
}

} // namespace engine_sim_offline::compile::detail
