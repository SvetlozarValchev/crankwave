#include "authoring/document_reader.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::authoring::detail {
namespace {

[[nodiscard]] std::string escape_pointer_token(std::string_view token) {
    std::string result;
    result.reserve(token.size());
    for (const char byte : token) {
        if (byte == '~') {
            result += "~0";
        } else if (byte == '/') {
            result += "~1";
        } else {
            result.push_back(byte);
        }
    }
    return result;
}

[[nodiscard]] bool is_known_standard(std::string_view value) noexcept {
    return value == "carburetor_1p5_inhg" || value == "port_28_inh2o";
}

[[nodiscard]] bool unit_matches(QuantityDimension dimension,
                                std::string_view unit) noexcept {
    using enum QuantityDimension;
    switch (dimension) {
    case dimensionless:
        return unit == "1";
    case angle:
        return unit == "deg" || unit == "rad";
    case angular_speed:
        return unit == "rpm" || unit == "rad/s";
    case area:
        return unit == "m2" || unit == "cm2" || unit == "mm2" || unit == "in2";
    case density:
        return unit == "kg/m3" || unit == "g/cm3";
    case duration:
        return unit == "s" || unit == "ms";
    case energy_per_mass:
        return unit == "J/kg" || unit == "kJ/kg" || unit == "MJ/kg";
    case force:
        return unit == "N";
    case frequency:
        return unit == "Hz" || unit == "kHz";
    case length:
        return unit == "m" || unit == "cm" || unit == "mm" || unit == "in";
    case mass:
        return unit == "kg" || unit == "g";
    case mass_flow_rate:
        return unit == "kg/s" || unit == "g/s";
    case molar_mass:
        return unit == "kg/mol" || unit == "g/mol";
    case moment_of_inertia:
        return unit == "kg*m2";
    case power:
        return unit == "W" || unit == "kW";
    case pressure:
        return unit == "Pa" || unit == "kPa" || unit == "bar" || unit == "atm" ||
               unit == "psi" || unit == "inHg" || unit == "inH2O";
    case pressure_per_speed:
        return unit == "Pa*s/m" || unit == "kPa*s/m" || unit == "bar*s/m";
    case pressure_per_speed_squared:
        return unit == "Pa*s2/m2" || unit == "kPa*s2/m2" ||
               unit == "bar*s2/m2";
    case speed:
        return unit == "m/s" || unit == "km/h" || unit == "mph";
    case temperature:
        return unit == "K" || unit == "degC";
    case torque:
        return unit == "N*m" || unit == "lb*ft";
    case volume:
        return unit == "m3" || unit == "L" || unit == "cm3";
    case volume_flow_rate:
        return unit == "m3/s" || unit == "L/s" || unit == "cfm";
    }
    return false;
}

[[nodiscard]] std::string_view kind_name(JsonKind kind) noexcept {
    using enum JsonKind;
    switch (kind) {
    case null_value:
        return "null";
    case boolean:
        return "boolean";
    case number:
        return "number";
    case string:
        return "string";
    case array:
        return "array";
    case object:
        return "object";
    case invalid:
        return "missing value";
    }
    return "invalid value";
}

} // namespace

DocumentReader::DocumentReader(AuthoringParseLimits limits) noexcept
    : limits_(std::move(limits)) {
    if (limits_.maximum_diagnostics == 0U) {
        limits_.maximum_diagnostics = 1U;
    }
}

std::string pointer_member(std::string_view parent, std::string_view member) {
    auto result = std::string{parent};
    result.push_back('/');
    result += escape_pointer_token(member);
    return result;
}

std::string pointer_index(std::string_view parent, std::size_t index) {
    auto result = std::string{parent};
    result.push_back('/');
    result += std::to_string(index);
    return result;
}

DiagnosticReport syntax_diagnostic(const JsonParseError &error) {
    DiagnosticCode code = DiagnosticCode::malformed_document;
    if (error.code == JsonParseErrorCode::input_limit_exceeded ||
        error.code == JsonParseErrorCode::depth_limit_exceeded ||
        error.code == JsonParseErrorCode::node_limit_exceeded ||
        error.code == JsonParseErrorCode::allocation_failure) {
        code = DiagnosticCode::resource_limit;
    } else if (error.code == JsonParseErrorCode::internal_failure) {
        code = DiagnosticCode::internal_failure;
    }

    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.source_position = SourcePosition{
        error.location.byte_offset,
        static_cast<std::uint32_t>(std::min<std::size_t>(
            error.location.line, std::numeric_limits<std::uint32_t>::max())),
        static_cast<std::uint32_t>(std::min<std::size_t>(
            error.location.column, std::numeric_limits<std::uint32_t>::max())),
    };
    diagnostic.message = std::string{error.message()};
    return DiagnosticReport{{std::move(diagnostic)}};
}

DiagnosticReport internal_diagnostic(std::string message) {
    Diagnostic diagnostic;
    diagnostic.code = DiagnosticCode::internal_failure;
    diagnostic.message = std::move(message);
    return DiagnosticReport{{std::move(diagnostic)}};
}

bool DocumentReader::object(JsonValue value, std::string_view path,
                            const std::optional<DiagnosticSubject> &subject_value) {
    if (value.kind() == JsonKind::object) {
        return true;
    }
    add(DiagnosticCode::invalid_type, path,
        "expected object, found " + std::string{kind_name(value.kind())},
        subject_value);
    return false;
}

bool DocumentReader::array(JsonValue value, std::string_view path,
                           const std::optional<DiagnosticSubject> &subject_value) {
    if (value.kind() != JsonKind::array) {
        add(DiagnosticCode::invalid_type, path,
            "expected array, found " + std::string{kind_name(value.kind())},
            subject_value);
        return false;
    }
    if (value.size() > limits_.maximum_array_items) {
        add(DiagnosticCode::resource_limit, path,
            "array contains " + std::to_string(value.size()) +
                " items, exceeding the configured limit of " +
                std::to_string(limits_.maximum_array_items),
            subject_value);
        return false;
    }
    return true;
}

JsonValue
DocumentReader::required(JsonValue object_value, std::string_view key,
                         std::string_view object_path,
                         const std::optional<DiagnosticSubject> &subject_value) {
    if (object_value.kind() != JsonKind::object) {
        return {};
    }
    const auto value = object_value.find(key);
    if (!value.valid()) {
        add(DiagnosticCode::missing_value, pointer_member(object_path, key),
            "required member is missing", subject_value);
    }
    return value;
}

JsonValue DocumentReader::optional(JsonValue object_value, std::string_view key) const {
    if (object_value.kind() != JsonKind::object) {
        return {};
    }
    return object_value.find(key);
}

void DocumentReader::reject_unknown(
    JsonValue object_value, std::string_view object_path,
    std::initializer_list<std::string_view> allowed,
    const std::optional<DiagnosticSubject> &subject_value) {
    if (object_value.kind() != JsonKind::object) {
        return;
    }
    for (std::size_t index = 0; index < object_value.size(); ++index) {
        const auto member = object_value.member_at(index);
        if (!member) {
            continue;
        }
        if (std::ranges::find(allowed, member.key) == allowed.end()) {
            add(DiagnosticCode::unknown_field, pointer_member(object_path, member.key),
                "unknown member '" + std::string{member.key} + "'", subject_value);
        }
    }
}

bool DocumentReader::string(JsonValue value, std::string_view path, std::string &output,
                            const std::optional<DiagnosticSubject> &subject_value) {
    const auto text = value.string();
    if (!text) {
        if (value.valid()) {
            add(DiagnosticCode::invalid_type, path,
                "expected string, found " + std::string{kind_name(value.kind())},
                subject_value);
        }
        return false;
    }
    if (text->size() > limits_.maximum_string_bytes) {
        add(DiagnosticCode::resource_limit, path,
            "string exceeds the configured byte limit of " +
                std::to_string(limits_.maximum_string_bytes),
            subject_value);
        return false;
    }
    output.assign(text->begin(), text->end());
    return true;
}

bool DocumentReader::boolean(JsonValue value, std::string_view path, bool &output,
                             const std::optional<DiagnosticSubject> &subject_value) {
    const auto boolean_value = value.boolean();
    if (!boolean_value) {
        if (value.valid()) {
            add(DiagnosticCode::invalid_type, path,
                "expected boolean, found " + std::string{kind_name(value.kind())},
                subject_value);
        }
        return false;
    }
    output = *boolean_value;
    return true;
}

bool DocumentReader::number(JsonValue value, std::string_view path, double &output,
                            const std::optional<DiagnosticSubject> &subject_value) {
    const auto number_value = value.number();
    if (!number_value) {
        if (value.valid()) {
            add(DiagnosticCode::invalid_type, path,
                "expected number, found " + std::string{kind_name(value.kind())},
                subject_value);
        }
        return false;
    }
    if (!std::isfinite(*number_value)) {
        add(DiagnosticCode::invalid_value, path, "number must be finite",
            subject_value);
        return false;
    }
    output = *number_value;
    return true;
}

bool DocumentReader::fraction(JsonValue value, std::string_view path, double &output,
                              const std::optional<DiagnosticSubject> &subject_value) {
    if (!number(value, path, output, subject_value)) {
        return false;
    }
    if (output < 0.0 || output > 1.0) {
        add(DiagnosticCode::out_of_range, path, "value must lie in [0, 1]",
            subject_value);
        return false;
    }
    return true;
}

bool DocumentReader::nonnegative_number(
    JsonValue value, std::string_view path, double &output,
    const std::optional<DiagnosticSubject> &subject_value) {
    if (!number(value, path, output, subject_value)) {
        return false;
    }
    if (output < 0.0) {
        add(DiagnosticCode::out_of_range, path, "value must be nonnegative",
            subject_value);
        return false;
    }
    return true;
}

bool DocumentReader::uint32(JsonValue value, std::string_view path,
                            std::uint32_t &output,
                            const std::optional<DiagnosticSubject> &subject_value) {
    double number_value = 0.0;
    if (!number(value, path, number_value, subject_value)) {
        return false;
    }
    if (number_value < 0.0 ||
        number_value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::trunc(number_value) != number_value) {
        add(DiagnosticCode::out_of_range, path,
            "value must be an unsigned 32-bit integer", subject_value);
        return false;
    }
    output = static_cast<std::uint32_t>(number_value);
    return true;
}

bool DocumentReader::uint64(JsonValue value, std::string_view path,
                            std::uint64_t &output,
                            const std::optional<DiagnosticSubject> &subject_value) {
    std::string text;
    if (!string(value, path, text, subject_value)) {
        return false;
    }
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        add(DiagnosticCode::invalid_value, path,
            "unsigned 64-bit integer must be a canonical decimal string",
            subject_value);
        return false;
    }
    std::uint64_t parsed = 0;
    for (const char byte : text) {
        if (byte < '0' || byte > '9') {
            add(DiagnosticCode::invalid_value, path,
                "unsigned 64-bit integer must contain decimal digits only",
                subject_value);
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(byte - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            add(DiagnosticCode::out_of_range, path,
                "decimal string exceeds unsigned 64-bit range", subject_value);
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    output = parsed;
    return true;
}

bool DocumentReader::quantity(JsonValue value, std::string_view path,
                              QuantityDimension dimension, Quantity &output,
                              const std::optional<DiagnosticSubject> &subject_value) {
    if (!object(value, path, subject_value)) {
        return false;
    }
    reject_unknown(value, path, {"value", "unit", "standard"}, subject_value);

    const auto value_member = required(value, "value", path, subject_value);
    const auto unit_member = required(value, "unit", path, subject_value);
    bool valid = number(value_member, pointer_member(path, "value"), output.value,
                        subject_value);
    valid =
        string(unit_member, pointer_member(path, "unit"), output.unit, subject_value) &&
        valid;
    if (!output.unit.empty() && !unit_matches(dimension, output.unit)) {
        add(DiagnosticCode::invalid_unit, pointer_member(path, "unit"),
            "unit '" + output.unit + "' is not valid for this quantity dimension",
            subject_value);
        valid = false;
    }

    const auto standard_member = optional(value, "standard");
    if (standard_member.valid() && !standard_member.is_null()) {
        std::string standard_value;
        if (string(standard_member, pointer_member(path, "standard"), standard_value,
                   subject_value)) {
            if (!is_known_standard(standard_value)) {
                add(DiagnosticCode::invalid_value, pointer_member(path, "standard"),
                    "unknown calibration standard '" + standard_value + "'",
                    subject_value);
                valid = false;
            } else if (dimension != QuantityDimension::volume_flow_rate) {
                add(DiagnosticCode::invalid_value, pointer_member(path, "standard"),
                    "calibration standard is valid only for volume-flow quantities",
                    subject_value);
                valid = false;
            } else {
                output.standard = std::move(standard_value);
            }
        } else {
            valid = false;
        }
    }
    return valid;
}

bool DocumentReader::rational_rate(
    JsonValue value, std::string_view path, RationalRate &output,
    const std::optional<DiagnosticSubject> &subject_value) {
    if (!object(value, path, subject_value)) {
        return false;
    }
    reject_unknown(value, path, {"numerator", "denominator", "unit"}, subject_value);
    bool valid =
        uint64(required(value, "numerator", path, subject_value),
               pointer_member(path, "numerator"), output.numerator, subject_value);
    valid = uint64(required(value, "denominator", path, subject_value),
                   pointer_member(path, "denominator"), output.denominator,
                   subject_value) &&
            valid;
    valid = string(required(value, "unit", path, subject_value),
                   pointer_member(path, "unit"), output.unit, subject_value) &&
            valid;
    if (output.denominator == 0) {
        add(DiagnosticCode::out_of_range, pointer_member(path, "denominator"),
            "rate denominator must be positive", subject_value);
        valid = false;
    }
    if (output.numerator == 0) {
        add(DiagnosticCode::out_of_range, pointer_member(path, "numerator"),
            "rate numerator must be positive", subject_value);
        valid = false;
    }
    if (!output.unit.empty() && output.unit != "Hz") {
        add(DiagnosticCode::invalid_unit, pointer_member(path, "unit"),
            "rate unit must be 'Hz'", subject_value);
        valid = false;
    }
    return valid;
}

void DocumentReader::add(DiagnosticCode code, std::string_view path,
                         std::string message,
                         const std::optional<DiagnosticSubject> &subject_value) {
    if (report_.diagnostics.size() >= limits_.maximum_diagnostics) {
        return;
    }
    report_.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::error,
        code,
        std::string{path},
        subject_value,
        std::nullopt,
        std::move(message),
        {},
    });
}

bool DocumentReader::ok() const noexcept {
    return report_.ok();
}

const AuthoringParseLimits &DocumentReader::limits() const noexcept {
    return limits_;
}

DiagnosticReport DocumentReader::finish() && {
    return std::move(report_);
}

bool DocumentReader::valid_stable_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    const auto valid_first = [](char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    const auto valid_rest = [&](char byte) {
        return valid_first(byte) || byte == '.' || byte == '_' || byte == '-';
    };
    return valid_first(value.front()) &&
           std::ranges::all_of(value.substr(1), valid_rest);
}

std::optional<DiagnosticSubject> subject(std::string kind, std::string id) {
    if (id.empty()) {
        return std::nullopt;
    }
    return DiagnosticSubject{std::move(kind), std::move(id)};
}

} // namespace engine_sim_offline::authoring::detail
