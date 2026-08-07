#include "engine_sim_offline/artifacts/engine_telemetry_ndjson_encoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

using Error = EngineTelemetryNdjsonEncodingError;
using ErrorCode = EngineTelemetryNdjsonEncodingErrorCode;
using Status = EngineTelemetryNdjsonEncodingStatus;

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] Error error(ErrorCode code, std::string path, std::string message) {
    return {code, std::move(path), std::move(message)};
}

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs,
                               std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checked_multiply(std::uint64_t lhs, std::uint64_t rhs,
                                    std::uint64_t &result) noexcept {
    if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] bool equal_factor_products(std::array<std::uint64_t, 3> lhs,
                                         std::array<std::uint64_t, 3> rhs) noexcept {
    for (auto &left : lhs) {
        for (auto &right : rhs) {
            const auto divisor = std::gcd(left, right);
            left /= divisor;
            right /= divisor;
        }
    }
    std::uint64_t left_product = 1U;
    std::uint64_t right_product = 1U;
    for (const auto factor : lhs) {
        if (!checked_multiply(left_product, factor, left_product)) {
            return false;
        }
    }
    for (const auto factor : rhs) {
        if (!checked_multiply(right_product, factor, right_product)) {
            return false;
        }
    }
    return left_product == right_product;
}

[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first <= 0x7fU) {
            ++offset;
            continue;
        }
        const auto continuation = [&](std::size_t index) {
            if (index >= value.size()) {
                return false;
            }
            const auto byte = static_cast<unsigned char>(value[index]);
            return byte >= 0x80U && byte <= 0xbfU;
        };
        if (first >= 0xc2U && first <= 0xdfU) {
            if (!continuation(offset + 1U)) {
                return false;
            }
            offset += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (offset + 2U >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            if (!continuation(offset + 2U) ||
                (first == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
                (first == 0xedU && (second < 0x80U || second > 0x9fU)) ||
                ((first != 0xe0U && first != 0xedU) && !continuation(offset + 1U))) {
                return false;
            }
            offset += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (offset + 3U >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            if (!continuation(offset + 2U) || !continuation(offset + 3U) ||
                (first == 0xf0U && (second < 0x90U || second > 0xbfU)) ||
                (first == 0xf4U && (second < 0x80U || second > 0x8fU)) ||
                ((first != 0xf0U && first != 0xf4U) && !continuation(offset + 1U))) {
                return false;
            }
            offset += 4U;
            continue;
        }
        return false;
    }
    return true;
}

class JsonRecordWriter final {
  public:
    enum class Failure : std::uint8_t {
        none,
        invalid_state,
        invalid_utf8,
        non_finite,
        conversion,
    };

    [[nodiscard]] bool begin_object() {
        if (!before_value() || !append('{')) {
            return false;
        }
        containers_.push_back({ContainerKind::object, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_object() {
        if (failure_ != Failure::none || containers_.empty() ||
            containers_.back().kind != ContainerKind::object ||
            containers_.back().awaiting_value) {
            return fail(Failure::invalid_state,
                        "object ended before all members were complete");
        }
        if (!append('}')) {
            return false;
        }
        containers_.pop_back();
        return true;
    }

    [[nodiscard]] bool key(std::string_view value) {
        if (failure_ != Failure::none || containers_.empty() ||
            containers_.back().kind != ContainerKind::object ||
            containers_.back().awaiting_value || !is_valid_utf8(value)) {
            return fail(is_valid_utf8(value) ? Failure::invalid_state
                                             : Failure::invalid_utf8,
                        "invalid JSON object key");
        }
        auto &object = containers_.back();
        if ((object.value_count != 0U && !append(',')) || !escaped_string(value) ||
            !append(':')) {
            return false;
        }
        object.awaiting_value = true;
        return true;
    }

    [[nodiscard]] bool string_value(std::string_view value) {
        if (!is_valid_utf8(value)) {
            return fail(Failure::invalid_utf8,
                        "JSON string is not Unicode scalar UTF-8");
        }
        return before_value() && escaped_string(value);
    }

    [[nodiscard]] bool bool_value(bool value) {
        return before_value() && raw(value ? "true" : "false");
    }

    [[nodiscard]] bool null_value() {
        return before_value() && raw("null");
    }

    [[nodiscard]] bool uint32_value(std::uint32_t value) {
        std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1U> storage{};
        const auto converted =
            std::to_chars(storage.data(), storage.data() + storage.size(), value);
        if (converted.ec != std::errc{}) {
            return fail(Failure::conversion, "uint32 conversion failed");
        }
        return before_value() &&
               raw(std::string_view{
                   storage.data(),
                   static_cast<std::size_t>(converted.ptr - storage.data())});
    }

    [[nodiscard]] bool uint64_string_value(std::uint64_t value) {
        std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1U> storage{};
        const auto converted =
            std::to_chars(storage.data(), storage.data() + storage.size(), value);
        if (converted.ec != std::errc{}) {
            return fail(Failure::conversion, "uint64 conversion failed");
        }
        return before_value() && append('"') &&
               raw(std::string_view{
                   storage.data(),
                   static_cast<std::size_t>(converted.ptr - storage.data())}) &&
               append('"');
    }

    [[nodiscard]] bool int64_string_value(std::int64_t value) {
        std::array<char, std::numeric_limits<std::int64_t>::digits10 + 2U> storage{};
        const auto converted =
            std::to_chars(storage.data(), storage.data() + storage.size(), value);
        if (converted.ec != std::errc{}) {
            return fail(Failure::conversion, "int64 conversion failed");
        }
        return before_value() && append('"') &&
               raw(std::string_view{
                   storage.data(),
                   static_cast<std::size_t>(converted.ptr - storage.data())}) &&
               append('"');
    }

    [[nodiscard]] bool number_value(double value) {
        if (!std::isfinite(value)) {
            return fail(Failure::non_finite, "JSON binary64 value must be finite");
        }
        if (value == 0.0) {
            value = 0.0;
        }
        std::array<char, 32U> storage{};
        const auto converted =
            std::to_chars(storage.data(), storage.data() + storage.size(), value,
                          std::chars_format::general);
        if (converted.ec != std::errc{}) {
            return fail(Failure::conversion, "binary64 conversion failed");
        }
        const std::string_view source{
            storage.data(), static_cast<std::size_t>(converted.ptr - storage.data())};
        const auto exponent = source.find_first_of("eE");
        if (!before_value()) {
            return false;
        }
        if (exponent == std::string_view::npos) {
            return raw(source);
        }
        if (!raw(source.substr(0U, exponent)) || !append('e')) {
            return false;
        }
        std::size_t cursor = exponent + 1U;
        if (cursor < source.size() && source[cursor] == '-') {
            if (!append('-')) {
                return false;
            }
            ++cursor;
        } else if (cursor < source.size() && source[cursor] == '+') {
            ++cursor;
        }
        while (cursor + 1U < source.size() && source[cursor] == '0') {
            ++cursor;
        }
        return raw(source.substr(cursor));
    }

    [[nodiscard]] bool fixed_hex32_value(std::uint32_t value) {
        std::array<char, 12U> encoded{};
        encoded[0] = '"';
        encoded[1] = '0';
        encoded[2] = 'x';
        for (std::size_t index = 0; index < 8U; ++index) {
            const auto shift = static_cast<unsigned>((7U - index) * 4U);
            encoded[index + 3U] = kHexDigits[(value >> shift) & 0x0fU];
        }
        encoded.back() = '"';
        return before_value() && raw({encoded.data(), encoded.size()});
    }

    [[nodiscard]] bool fixed_hex64_value(std::uint64_t value) {
        std::array<char, 20U> encoded{};
        encoded[0] = '"';
        encoded[1] = '0';
        encoded[2] = 'x';
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto shift = static_cast<unsigned>((15U - index) * 4U);
            encoded[index + 3U] = kHexDigits[(value >> shift) & UINT64_C(0x0f)];
        }
        encoded.back() = '"';
        return before_value() && raw({encoded.data(), encoded.size()});
    }

    [[nodiscard]] bool sha256_value(const contract::Sha256Digest &value) {
        std::array<char, 66U> encoded{};
        encoded.front() = '"';
        for (std::size_t index = 0; index < value.bytes.size(); ++index) {
            encoded[1U + index * 2U] = kHexDigits[(value.bytes[index] >> 4U) & 0x0fU];
            encoded[2U + index * 2U] = kHexDigits[value.bytes[index] & 0x0fU];
        }
        encoded.back() = '"';
        return before_value() && raw({encoded.data(), encoded.size()});
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> finish() {
        if (failure_ != Failure::none || !root_written_ || !containers_.empty()) {
            static_cast<void>(
                fail(Failure::invalid_state, "record is not a complete JSON value"));
            return std::nullopt;
        }
        if (!append('\n')) {
            return std::nullopt;
        }
        return std::move(bytes_);
    }

    [[nodiscard]] Failure failure() const noexcept {
        return failure_;
    }
    [[nodiscard]] std::string_view failure_message() const noexcept {
        return failure_message_;
    }

  private:
    enum class ContainerKind : std::uint8_t { object };
    struct Container {
        ContainerKind kind = ContainerKind::object;
        std::size_t value_count = 0;
        bool awaiting_value = false;
    };

    [[nodiscard]] bool before_value() {
        if (failure_ != Failure::none) {
            return false;
        }
        if (containers_.empty()) {
            if (root_written_) {
                return fail(Failure::invalid_state,
                            "record contains more than one root value");
            }
            root_written_ = true;
            return true;
        }
        auto &container = containers_.back();
        if (!container.awaiting_value) {
            return fail(Failure::invalid_state,
                        "object member value has no preceding key");
        }
        container.awaiting_value = false;
        ++container.value_count;
        return true;
    }

    [[nodiscard]] bool append(char value) {
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        return true;
    }

    [[nodiscard]] bool raw(std::string_view value) {
        for (const char character : value) {
            bytes_.push_back(
                static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        return true;
    }

    [[nodiscard]] bool escaped_string(std::string_view value) {
        if (!append('"')) {
            return false;
        }
        for (const char character : value) {
            const auto byte = static_cast<unsigned char>(character);
            switch (character) {
            case '"':
                if (!raw("\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!raw("\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!raw("\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!raw("\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!raw("\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!raw("\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!raw("\\t")) {
                    return false;
                }
                break;
            default:
                if (byte < 0x20U) {
                    const std::array<char, 6U> escaped{'\\',
                                                       'u',
                                                       '0',
                                                       '0',
                                                       kHexDigits[byte >> 4U],
                                                       kHexDigits[byte & 0x0fU]};
                    if (!raw({escaped.data(), escaped.size()})) {
                        return false;
                    }
                } else if (!append(character)) {
                    return false;
                }
                break;
            }
        }
        return append('"');
    }

    [[nodiscard]] bool fail(Failure failure, std::string message) {
        if (failure_ == Failure::none) {
            failure_ = failure;
            failure_message_ = std::move(message);
        }
        return false;
    }

    std::vector<std::byte> bytes_;
    std::vector<Container> containers_;
    Failure failure_ = Failure::none;
    std::string failure_message_;
    bool root_written_ = false;
};

[[nodiscard]] std::optional<std::string_view>
motion_mode_name(EngineMotionMode value) noexcept {
    switch (value) {
    case EngineMotionMode::held_speed:
        return "held_speed";
    case EngineMotionMode::prescribed_kinematic_sweep:
        return "prescribed_kinematic_sweep";
    case EngineMotionMode::held_dyno:
        return "held_dyno";
    case EngineMotionMode::load_target_held_capture:
        return "load_target_held_capture";
    case EngineMotionMode::inertial_dyno:
        return "inertial_dyno";
    case EngineMotionMode::free_engine:
        return "free_engine";
    case EngineMotionMode::free_vehicle:
        return "free_vehicle";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
block_phase_name(EngineSessionBlockPhase value) noexcept {
    switch (value) {
    case EngineSessionBlockPhase::preparation:
        return "preparation";
    case EngineSessionBlockPhase::audible:
        return "audible";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
availability_name(contract::Availability value) noexcept {
    switch (value) {
    case contract::Availability::available:
        return "available";
    case contract::Availability::unavailable:
        return "unavailable";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
completeness_name(contract::Completeness value) noexcept {
    switch (value) {
    case contract::Completeness::complete:
        return "complete";
    case contract::Completeness::incomplete:
        return "incomplete";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
unavailable_reason_name(contract::QuantityUnavailableReason value) noexcept {
    switch (value) {
    case contract::QuantityUnavailableReason::none:
        return "none";
    case contract::QuantityUnavailableReason::scenario_not_applicable:
        return "scenario_not_applicable";
    case contract::QuantityUnavailableReason::model_not_admitted:
        return "model_not_admitted";
    case contract::QuantityUnavailableReason::equivalent_inertia_missing:
        return "equivalent_inertia_missing";
    case contract::QuantityUnavailableReason::cycle_integration_not_admitted:
        return "cycle_integration_not_admitted";
    case contract::QuantityUnavailableReason::not_settled:
        return "not_settled";
    case contract::QuantityUnavailableReason::required_input_missing:
        return "required_input_missing";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
held_dyno_disposition_name(EngineHeldDynoDisposition value) noexcept {
    switch (value) {
    case EngineHeldDynoDisposition::tracking:
        return "tracking";
    case EngineHeldDynoDisposition::absorbing_torque_limited:
        return "absorbing_torque_limited";
    case EngineHeldDynoDisposition::driving_torque_limited:
        return "driving_torque_limited";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
clutch_disposition_name(EngineClutchDisposition value) noexcept {
    switch (value) {
    case EngineClutchDisposition::neutral:
        return "neutral";
    case EngineClutchDisposition::disengaged:
        return "disengaged";
    case EngineClutchDisposition::engine_driving_torque_limited:
        return "engine_driving_torque_limited";
    case EngineClutchDisposition::vehicle_backdrive_torque_limited:
        return "vehicle_backdrive_torque_limited";
    case EngineClutchDisposition::tracking:
        return "tracking";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
road_load_disposition_name(EngineRoadLoadDisposition value) noexcept {
    switch (value) {
    case EngineRoadLoadDisposition::moving:
        return "moving";
    case EngineRoadLoadDisposition::stopped_within_step:
        return "stopped_within_step";
    case EngineRoadLoadDisposition::held_at_rest:
        return "held_at_rest";
    }
    return std::nullopt;
}

[[nodiscard]] Error non_finite(std::string path) {
    return error(ErrorCode::non_finite_value, std::move(path),
                 "engine telemetry NDJSON requires every binary64 value to be "
                 "finite");
}

[[nodiscard]] Status validate_finite(double value, std::string path) {
    if (!std::isfinite(value)) {
        return non_finite(std::move(path));
    }
    return std::nullopt;
}

template <class Enum, class Mapper>
[[nodiscard]] Status validate_enum(Enum value, Mapper mapper, std::string path) {
    if (!mapper(value).has_value()) {
        return error(ErrorCode::invalid_enum, std::move(path),
                     "value has no frozen snake-case representation in schema v1");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_contract_report(const contract::ValidationReport &report,
                                              const std::string &path) {
    if (report.ok()) {
        return std::nullopt;
    }
    const auto &issue = report.issues.front();
    auto issue_path = path;
    if (!issue.path.empty()) {
        issue_path += ".";
        issue_path += issue.path;
    }
    return error(ErrorCode::invalid_value, std::move(issue_path),
                 "value violates canonical torque/quantity semantics: " +
                     issue.message);
}

[[nodiscard]] Status validate_quantity(const contract::QuantityValue &value,
                                       const std::string &path) {
    if (auto issue = validate_contract_report(contract::validate(value), path)) {
        return issue;
    }
    if (auto issue = validate_finite(value.value, path + ".value")) {
        return issue;
    }
    if (auto issue = validate_enum(value.availability, availability_name,
                                   path + ".availability")) {
        return issue;
    }
    if (auto issue = validate_enum(value.completeness, completeness_name,
                                   path + ".completeness")) {
        return issue;
    }
    return validate_enum(value.unavailable_reason, unavailable_reason_name,
                         path + ".unavailable_reason");
}

[[nodiscard]] Status validate_torque_value(const contract::TorqueValueNm &value,
                                           const std::string &path) {
    if (auto issue = validate_contract_report(contract::validate(value), path)) {
        return issue;
    }
    if (auto issue = validate_finite(value.value_nm, path + ".value_nm")) {
        return issue;
    }
    if (auto issue = validate_enum(value.availability, availability_name,
                                   path + ".availability")) {
        return issue;
    }
    if (auto issue = validate_enum(value.completeness, completeness_name,
                                   path + ".completeness")) {
        return issue;
    }
    if (auto issue = validate_enum(value.unavailable_reason, unavailable_reason_name,
                                   path + ".unavailable_reason")) {
        return issue;
    }
    const auto known = contract::known_torque_term_mask();
    if (((value.included_terms | value.omitted_terms) & ~known) != 0U) {
        return error(ErrorCode::invalid_value, path + ".terms_mask",
                     "torque masks contain a term outside the frozen v1 inventory");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_torque_telemetry(const contract::TorqueTelemetry &value,
                                               const std::string &path) {
    if (auto issue = validate_contract_report(contract::validate(value), path)) {
        return issue;
    }
    const std::array torque_fields{
        std::pair{"instantaneous_indicated_gas", &value.instantaneous_indicated_gas},
        std::pair{"pumping_partition", &value.pumping_partition},
        std::pair{"friction_pump_and_accessory", &value.friction_pump_and_accessory},
        std::pair{"starter", &value.starter},
        std::pair{"instantaneous_net_shaft", &value.instantaneous_net_shaft},
        std::pair{"cycle_mean_net_shaft", &value.cycle_mean_net_shaft},
        std::pair{"actuator", &value.actuator},
        std::pair{"dyno_reaction", &value.dyno_reaction},
    };
    for (const auto &[name, field] : torque_fields) {
        if (auto issue = validate_torque_value(*field, path + "." + name)) {
            return issue;
        }
    }
    const std::array quantity_fields{
        std::pair{"cycle_work_j", &value.cycle_work_j},
        std::pair{"net_bmep_pa", &value.net_bmep_pa},
        std::pair{"instantaneous_power_w", &value.instantaneous_power_w},
        std::pair{"cycle_mean_power_w", &value.cycle_mean_power_w},
    };
    for (const auto &[name, field] : quantity_fields) {
        if (auto issue = validate_quantity(*field, path + "." + name)) {
            return issue;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_endpoint(const EngineTelemetryFrame &frame,
                                       EngineMotionMode motion_mode) {
    constexpr auto known_validity =
        contract::capture_validity_mask(contract::CaptureValidity::mechanism) |
        contract::capture_validity_mask(
            contract::CaptureValidity::thermodynamic_state) |
        contract::capture_validity_mask(contract::CaptureValidity::composition) |
        contract::capture_validity_mask(contract::CaptureValidity::gas_exchange) |
        contract::capture_validity_mask(contract::CaptureValidity::combustion) |
        contract::capture_validity_mask(contract::CaptureValidity::torque);
    if ((frame.engine.validity & ~known_validity) != 0U) {
        return error(ErrorCode::invalid_value, "block.telemetry.engine.validity_mask",
                     "capture validity mask contains a bit outside schema v1");
    }
    const std::array scalar_fields{
        std::pair{"mean_intake_manifold_pressure_pa_abs",
                  frame.mean_intake_manifold_pressure_pa_abs},
        std::pair{"engine.theta_rad", frame.engine.theta_rad},
        std::pair{"engine.theta_cycle_rad", frame.engine.theta_cycle_rad},
        std::pair{"engine.angular_speed_rad_s", frame.engine.angular_speed_rad_s},
        std::pair{"engine.angular_acceleration_rad_s2",
                  frame.engine.angular_acceleration_rad_s2},
        std::pair{"engine.engine_speed_rpm", frame.engine.engine_speed_rpm},
        std::pair{"engine.requested_throttle_01", frame.engine.requested_throttle_01},
        std::pair{"engine.resolved_engine_throttle_01",
                  frame.engine.resolved_engine_throttle_01},
        std::pair{"engine.intake_plate_position_01",
                  frame.engine.intake_plate_position_01},
        std::pair{"engine.main_flow_multiplier_01",
                  frame.engine.main_flow_multiplier_01},
        std::pair{"engine.requested_external_resisting_torque_nm",
                  frame.engine.requested_external_resisting_torque_nm},
    };
    for (const auto &[name, value] : scalar_fields) {
        if (auto issue =
                validate_finite(value, "block.telemetry." + std::string{name})) {
            return issue;
        }
    }
    if (!(frame.mean_intake_manifold_pressure_pa_abs > 0.0)) {
        return error(ErrorCode::invalid_value,
                     "block.telemetry.mean_intake_manifold_pressure_pa_abs",
                     "intake-manifold pressure must be positive");
    }
    if (frame.engine.theta_cycle_rad < 0.0 ||
        frame.engine.theta_cycle_rad >= 4.0 * std::numbers::pi) {
        return error(ErrorCode::invalid_value, "block.telemetry.engine.theta_cycle_rad",
                     "wrapped four-stroke angle must be in [0, 4*pi)");
    }
    const std::array unit_interval_fields{
        std::pair{"requested_throttle_01", frame.engine.requested_throttle_01},
        std::pair{"resolved_engine_throttle_01",
                  frame.engine.resolved_engine_throttle_01},
        std::pair{"intake_plate_position_01", frame.engine.intake_plate_position_01},
        std::pair{"main_flow_multiplier_01", frame.engine.main_flow_multiplier_01},
    };
    for (const auto &[name, value] : unit_interval_fields) {
        if (value < 0.0 || value > 1.0) {
            return error(ErrorCode::invalid_value,
                         "block.telemetry.engine." + std::string{name},
                         "engine control coordinate must be in [0, 1]");
        }
    }
    if (frame.engine.requested_external_resisting_torque_nm < 0.0) {
        return error(ErrorCode::invalid_value,
                     "block.telemetry.engine.requested_external_resisting_torque_nm",
                     "requested external resisting torque must be nonnegative");
    }
    if (auto issue = validate_torque_telemetry(frame.engine.torque,
                                               "block.telemetry.engine.torque")) {
        return issue;
    }
    const auto &torque = frame.engine.torque;
    const std::array quantity_availability{
        torque.instantaneous_indicated_gas.availability,
        torque.pumping_partition.availability,
        torque.friction_pump_and_accessory.availability,
        torque.starter.availability,
        torque.instantaneous_net_shaft.availability,
        torque.cycle_mean_net_shaft.availability,
        torque.actuator.availability,
        torque.dyno_reaction.availability,
        torque.cycle_work_j.availability,
        torque.net_bmep_pa.availability,
        torque.instantaneous_power_w.availability,
        torque.cycle_mean_power_w.availability,
    };
    const bool any_torque_quantity_available =
        std::ranges::any_of(quantity_availability, [](const auto availability) {
            return availability == contract::Availability::available;
        });
    const bool torque_capture_valid =
        (frame.engine.validity &
         contract::capture_validity_mask(contract::CaptureValidity::torque)) != 0U;
    if (torque_capture_valid != any_torque_quantity_available) {
        return error(
            ErrorCode::invalid_value, "block.telemetry.engine.torque",
            torque_capture_valid
                ? "engine torque validity requires at least one available torque or "
                  "derived quantity"
                : "engine telemetry without torque validity must keep every torque "
                  "and derived quantity unavailable");
    }
    const bool held_dyno_allowed = motion_mode == EngineMotionMode::held_dyno;
    const bool free_vehicle_allowed = motion_mode == EngineMotionMode::free_vehicle;
    if ((frame.held_dyno.has_value() && !held_dyno_allowed) ||
        (frame.free_vehicle.has_value() && !free_vehicle_allowed)) {
        return error(
            ErrorCode::invalid_value, "block.telemetry.mode_sidecars",
            "telemetry sidecar presence is inconsistent with the declared motion "
            "mode");
    }
    if (frame.held_dyno.has_value()) {
        const auto &dyno = *frame.held_dyno;
        const std::array fields{
            std::pair{"target_engine_speed_rpm", dyno.target_engine_speed_rpm},
            std::pair{"maximum_absorbing_torque_nm", dyno.maximum_absorbing_torque_nm},
            std::pair{"maximum_driving_torque_nm", dyno.maximum_driving_torque_nm},
            std::pair{"required_actuator_torque_nm", dyno.required_actuator_torque_nm},
            std::pair{"applied_actuator_torque_nm", dyno.applied_actuator_torque_nm},
        };
        for (const auto &[name, value] : fields) {
            if (auto issue = validate_finite(value, "block.telemetry.held_dyno." +
                                                        std::string{name})) {
                return issue;
            }
        }
        if (auto issue = validate_enum(dyno.disposition, held_dyno_disposition_name,
                                       "block.telemetry.held_dyno.disposition")) {
            return issue;
        }
        if (torque.actuator.availability != contract::Availability::available) {
            return error(ErrorCode::invalid_value,
                         "block.telemetry.engine.torque.actuator.availability",
                         "held-dyno telemetry requires available actuator torque");
        }
        if (torque.dyno_reaction.availability != contract::Availability::available) {
            return error(ErrorCode::invalid_value,
                         "block.telemetry.engine.torque.dyno_reaction.availability",
                         "held-dyno telemetry requires available dyno-reaction torque");
        }
        if (std::bit_cast<std::uint64_t>(torque.actuator.value_nm) !=
            std::bit_cast<std::uint64_t>(dyno.applied_actuator_torque_nm)) {
            return error(
                ErrorCode::invalid_value,
                "block.telemetry.held_dyno.applied_actuator_torque_nm",
                "held-dyno sidecar and common actuator torque must agree exactly");
        }
        if (std::bit_cast<std::uint64_t>(torque.dyno_reaction.value_nm) !=
            std::bit_cast<std::uint64_t>(-dyno.applied_actuator_torque_nm)) {
            return error(
                ErrorCode::invalid_value,
                "block.telemetry.engine.torque.dyno_reaction.value_nm",
                "held-dyno sidecar and common reaction torque must agree exactly");
        }
    }
    if (frame.free_vehicle.has_value()) {
        const auto &vehicle = *frame.free_vehicle;
        const std::array fields{
            std::pair{"vehicle_speed_m_s", vehicle.vehicle_speed_m_s},
            std::pair{"vehicle_distance_m", vehicle.vehicle_distance_m},
            std::pair{"clutch_engagement_01", vehicle.clutch_engagement_01},
            std::pair{"service_brake_application_01",
                      vehicle.service_brake_application_01},
            std::pair{"clutch_torque_capacity_nm", vehicle.clutch_torque_capacity_nm},
            std::pair{"applied_average_clutch_torque_on_engine_nm",
                      vehicle.applied_average_clutch_torque_on_engine_nm},
            std::pair{"requested_road_load_force_n",
                      vehicle.requested_road_load_force_n},
            std::pair{"applied_average_road_load_force_n",
                      vehicle.applied_average_road_load_force_n},
        };
        for (const auto &[name, value] : fields) {
            if (auto issue = validate_finite(value, "block.telemetry.free_vehicle." +
                                                        std::string{name})) {
                return issue;
            }
        }
        if (vehicle.final_clutch_slip_rad_s.has_value()) {
            if (auto issue = validate_finite(
                    *vehicle.final_clutch_slip_rad_s,
                    "block.telemetry.free_vehicle.final_clutch_slip_rad_s")) {
                return issue;
            }
        }
        if (auto issue =
                validate_enum(vehicle.clutch_disposition, clutch_disposition_name,
                              "block.telemetry.free_vehicle.clutch_disposition")) {
            return issue;
        }
        if (auto issue =
                validate_enum(vehicle.road_load_disposition, road_load_disposition_name,
                              "block.telemetry.free_vehicle.road_load_disposition")) {
            return issue;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_boundary(const EngineCycleBoundaryEvidence &boundary,
                                       const std::string &path) {
    const std::array fields{
        std::pair{"fraction_from_left_01", boundary.fraction_from_left_01},
        std::pair{"theta_unwrapped_rad", boundary.theta_unwrapped_rad},
        std::pair{"time_s", boundary.time_s},
        std::pair{"delivery_frame", boundary.delivery_frame},
    };
    for (const auto &[name, value] : fields) {
        if (auto issue = validate_finite(value, path + "." + name)) {
            return issue;
        }
    }
    if (boundary.fraction_from_left_01 < 0.0 || boundary.fraction_from_left_01 > 1.0 ||
        boundary.right_physics_frame < boundary.left_physics_frame) {
        return error(ErrorCode::invalid_value, path,
                     "cycle boundary bracket or interpolation fraction is invalid");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_control(const EngineCycleControlEvidence &control,
                                      const std::string &path) {
    const std::array fields{
        std::pair{"time_weighted_mean_01", control.time_weighted_mean_01},
        std::pair{"minimum_01", control.minimum_01},
        std::pair{"maximum_01", control.maximum_01},
    };
    for (const auto &[name, value] : fields) {
        if (auto issue = validate_finite(value, path + "." + name)) {
            return issue;
        }
    }
    // ZOH integration accumulates hundreds or thousands of binary64 intervals.
    // Preserve the exact result on the wire while admitting its established public
    // evidence tolerance at the closed [0,1] and min/mean/max boundaries.
    constexpr double tolerance_01 = 1.0e-12;
    if (control.minimum_01 < -tolerance_01 || control.maximum_01 > 1.0 + tolerance_01 ||
        control.minimum_01 > control.maximum_01 + tolerance_01 ||
        control.time_weighted_mean_01 < control.minimum_01 - tolerance_01 ||
        control.time_weighted_mean_01 > control.maximum_01 + tolerance_01) {
        return error(ErrorCode::invalid_value, path,
                     "cycle control range or mean lies outside [0,1]");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_cycle_net_shaft(const EngineCycleNetShaftEvidence &torque,
                                              const std::string &path) {
    const contract::TorqueValueNm work{
        torque.angular_work_j,     torque.availability,   torque.completeness,
        torque.unavailable_reason, torque.included_terms, torque.omitted_terms,
    };
    const contract::TorqueValueNm mean_torque{
        torque.cycle_mean_torque_nm, torque.availability,   torque.completeness,
        torque.unavailable_reason,   torque.included_terms, torque.omitted_terms,
    };
    if (auto issue = validate_contract_report(contract::validate(work),
                                              path + ".angular_work_j")) {
        return issue;
    }
    if (auto issue = validate_contract_report(contract::validate(mean_torque),
                                              path + ".cycle_mean_torque_nm")) {
        return issue;
    }
    if (auto issue = validate_finite(torque.angular_work_j, path + ".angular_work_j")) {
        return issue;
    }
    if (auto issue = validate_finite(torque.cycle_mean_torque_nm,
                                     path + ".cycle_mean_torque_nm")) {
        return issue;
    }
    if (auto issue = validate_enum(torque.availability, availability_name,
                                   path + ".availability")) {
        return issue;
    }
    if (auto issue = validate_enum(torque.completeness, completeness_name,
                                   path + ".completeness")) {
        return issue;
    }
    if (auto issue = validate_enum(torque.unavailable_reason, unavailable_reason_name,
                                   path + ".unavailable_reason")) {
        return issue;
    }
    const auto known = contract::known_torque_term_mask();
    if (((torque.included_terms | torque.omitted_terms) & ~known) != 0U) {
        return error(ErrorCode::invalid_value, path + ".terms_mask",
                     "cycle torque masks contain a term outside schema v1");
    }
    return std::nullopt;
}

[[nodiscard]] constexpr EngineCycleStateFlagMask known_cycle_state_mask() noexcept {
    return engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::starter_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_enabled) |
           engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
}

[[nodiscard]] Status validate_cycle(const EngineCompletedCycleEvidence &cycle,
                                    std::uint64_t expected_cycle_ordinal,
                                    std::uint64_t block_physics_begin,
                                    std::uint64_t block_physics_end,
                                    std::size_t block_cycle_index) {
    const auto base = "block.cycle_evidence[" + std::to_string(block_cycle_index) + "]";
    if (cycle.completed_cycle_ordinal != expected_cycle_ordinal) {
        return error(ErrorCode::cycle_order_mismatch, base + ".completed_cycle_ordinal",
                     "completed cycle ordinals must begin at zero and be contiguous");
    }
    if (auto issue =
            validate_boundary(cycle.start_boundary, base + ".start_boundary")) {
        return issue;
    }
    if (auto issue = validate_boundary(cycle.end_boundary, base + ".end_boundary")) {
        return issue;
    }
    if (cycle.start_boundary.cycle_ordinal ==
            std::numeric_limits<std::int64_t>::max() ||
        cycle.end_boundary.cycle_ordinal != cycle.start_boundary.cycle_ordinal + 1) {
        return error(ErrorCode::invalid_value, base + ".boundaries",
                     "completed cycle must span exactly one adjacent lattice interval");
    }
    if (cycle.end_boundary.right_physics_frame < block_physics_begin ||
        cycle.end_boundary.right_physics_frame >= block_physics_end) {
        return error(ErrorCode::cycle_order_mismatch,
                     base + ".end_boundary.right_physics_frame",
                     "cycle must be emitted by the block containing its end crossing");
    }
    if (auto issue = validate_finite(cycle.duration_s, base + ".duration_s")) {
        return issue;
    }
    if (auto issue = validate_finite(cycle.mean_engine_speed_rpm,
                                     base + ".mean_engine_speed_rpm")) {
        return issue;
    }
    if (!(cycle.duration_s > 0.0) || !(cycle.mean_engine_speed_rpm >= 0.0)) {
        return error(ErrorCode::invalid_value, base,
                     "completed cycle duration must be positive and RPM nonnegative");
    }
    if (auto issue =
            validate_control(cycle.requested_throttle, base + ".requested_throttle")) {
        return issue;
    }
    if (auto issue = validate_control(cycle.resolved_engine_throttle,
                                      base + ".resolved_engine_throttle")) {
        return issue;
    }
    if (auto issue = validate_control(cycle.intake_plate_position,
                                      base + ".intake_plate_position")) {
        return issue;
    }
    if (auto issue = validate_cycle_net_shaft(cycle.instantaneous_net_shaft,
                                              base + ".instantaneous_net_shaft")) {
        return issue;
    }
    const auto masks =
        cycle.start_state_flags | cycle.end_state_flags | cycle.state_transition_flags;
    if ((masks & ~known_cycle_state_mask()) != 0U) {
        return error(ErrorCode::invalid_value, base + ".state_flags",
                     "cycle state mask contains a bit outside schema v1");
    }
    return std::nullopt;
}

[[nodiscard]] Status validate_event_counters(const EngineEventCounters &counters,
                                             std::string path) {
    std::uint64_t partition = 0;
    const std::array leaf_counts{
        counters.spark_crossing_count,
        counters.limiter_transition_count,
        counters.ignition_accepted_count,
        counters.ignition_rejected_active_flame_count,
        counters.ignition_rejected_no_fuel_count,
        counters.ignition_rejected_mixture_low_count,
        counters.ignition_rejected_mixture_high_count,
        counters.flame_extinguished_intake_transfer_count,
        counters.flame_extinguished_no_geometric_progress_count,
    };
    for (const auto count : leaf_counts) {
        if (!checked_add(partition, count, partition)) {
            return error(ErrorCode::integer_overflow, path,
                         "event counter partition overflowed uint64");
        }
    }
    std::uint64_t limiter_transitions = 0;
    if (!checked_add(counters.limiter_activation_count, counters.limiter_release_count,
                     limiter_transitions) ||
        limiter_transitions != counters.limiter_transition_count ||
        counters.limiter_transition_overspeed_refreshed_count >
            counters.limiter_transition_count ||
        partition != counters.total_event_record_count) {
        return error(ErrorCode::event_count_mismatch, std::move(path),
                     "event counters do not satisfy the frozen v1 partition "
                     "invariants");
    }
    return std::nullopt;
}

[[nodiscard]] bool add_event_counters(const EngineEventCounters &lhs,
                                      const EngineEventCounters &rhs,
                                      EngineEventCounters &result) noexcept {
#define ESO_ADD_EVENT_COUNTER(member)                                                  \
    if (!checked_add(lhs.member, rhs.member, result.member)) {                         \
        return false;                                                                  \
    }
    ESO_ADD_EVENT_COUNTER(total_event_record_count)
    ESO_ADD_EVENT_COUNTER(spark_crossing_count)
    ESO_ADD_EVENT_COUNTER(limiter_transition_count)
    ESO_ADD_EVENT_COUNTER(limiter_activation_count)
    ESO_ADD_EVENT_COUNTER(limiter_release_count)
    ESO_ADD_EVENT_COUNTER(limiter_transition_overspeed_refreshed_count)
    ESO_ADD_EVENT_COUNTER(ignition_accepted_count)
    ESO_ADD_EVENT_COUNTER(ignition_rejected_active_flame_count)
    ESO_ADD_EVENT_COUNTER(ignition_rejected_no_fuel_count)
    ESO_ADD_EVENT_COUNTER(ignition_rejected_mixture_low_count)
    ESO_ADD_EVENT_COUNTER(ignition_rejected_mixture_high_count)
    ESO_ADD_EVENT_COUNTER(flame_extinguished_intake_transfer_count)
    ESO_ADD_EVENT_COUNTER(flame_extinguished_no_geometric_progress_count)
#undef ESO_ADD_EVENT_COUNTER
    return true;
}

[[nodiscard]] bool write_rate(JsonRecordWriter &writer,
                              const contract::RationalRateHz &rate) {
    return writer.begin_object() && writer.key("numerator") &&
           writer.uint64_string_value(rate.numerator) && writer.key("denominator") &&
           writer.uint64_string_value(rate.denominator) && writer.end_object();
}

[[nodiscard]] bool write_frame_range(JsonRecordWriter &writer, std::uint64_t begin,
                                     std::uint64_t end) {
    return writer.begin_object() && writer.key("begin") &&
           writer.uint64_string_value(begin) && writer.key("end") &&
           writer.uint64_string_value(end) && writer.end_object();
}

[[nodiscard]] bool write_event_counters(JsonRecordWriter &writer,
                                        const EngineEventCounters &counters) {
    return writer.begin_object() && writer.key("total_event_record_count") &&
           writer.uint64_string_value(counters.total_event_record_count) &&
           writer.key("spark_crossing_count") &&
           writer.uint64_string_value(counters.spark_crossing_count) &&
           writer.key("limiter_transition_count") &&
           writer.uint64_string_value(counters.limiter_transition_count) &&
           writer.key("limiter_activation_count") &&
           writer.uint64_string_value(counters.limiter_activation_count) &&
           writer.key("limiter_release_count") &&
           writer.uint64_string_value(counters.limiter_release_count) &&
           writer.key("limiter_transition_overspeed_refreshed_count") &&
           writer.uint64_string_value(
               counters.limiter_transition_overspeed_refreshed_count) &&
           writer.key("ignition_accepted_count") &&
           writer.uint64_string_value(counters.ignition_accepted_count) &&
           writer.key("ignition_rejected_active_flame_count") &&
           writer.uint64_string_value(counters.ignition_rejected_active_flame_count) &&
           writer.key("ignition_rejected_no_fuel_count") &&
           writer.uint64_string_value(counters.ignition_rejected_no_fuel_count) &&
           writer.key("ignition_rejected_mixture_low_count") &&
           writer.uint64_string_value(counters.ignition_rejected_mixture_low_count) &&
           writer.key("ignition_rejected_mixture_high_count") &&
           writer.uint64_string_value(counters.ignition_rejected_mixture_high_count) &&
           writer.key("flame_extinguished_intake_transfer_count") &&
           writer.uint64_string_value(
               counters.flame_extinguished_intake_transfer_count) &&
           writer.key("flame_extinguished_no_geometric_progress_count") &&
           writer.uint64_string_value(
               counters.flame_extinguished_no_geometric_progress_count) &&
           writer.end_object();
}

[[nodiscard]] bool write_quantity(JsonRecordWriter &writer,
                                  const contract::QuantityValue &value) {
    return writer.begin_object() && writer.key("value") &&
           writer.number_value(value.value) && writer.key("availability") &&
           writer.string_value(*availability_name(value.availability)) &&
           writer.key("completeness") &&
           writer.string_value(*completeness_name(value.completeness)) &&
           writer.key("unavailable_reason") &&
           writer.string_value(*unavailable_reason_name(value.unavailable_reason)) &&
           writer.end_object();
}

[[nodiscard]] bool write_torque_value(JsonRecordWriter &writer,
                                      const contract::TorqueValueNm &value) {
    return writer.begin_object() && writer.key("value_nm") &&
           writer.number_value(value.value_nm) && writer.key("availability") &&
           writer.string_value(*availability_name(value.availability)) &&
           writer.key("completeness") &&
           writer.string_value(*completeness_name(value.completeness)) &&
           writer.key("unavailable_reason") &&
           writer.string_value(*unavailable_reason_name(value.unavailable_reason)) &&
           writer.key("included_terms_mask") &&
           writer.fixed_hex64_value(value.included_terms) &&
           writer.key("omitted_terms_mask") &&
           writer.fixed_hex64_value(value.omitted_terms) && writer.end_object();
}

[[nodiscard]] bool write_torque_telemetry(JsonRecordWriter &writer,
                                          const contract::TorqueTelemetry &value) {
    return writer.begin_object() && writer.key("instantaneous_indicated_gas") &&
           write_torque_value(writer, value.instantaneous_indicated_gas) &&
           writer.key("pumping_partition") &&
           write_torque_value(writer, value.pumping_partition) &&
           writer.key("friction_pump_and_accessory") &&
           write_torque_value(writer, value.friction_pump_and_accessory) &&
           writer.key("starter") && write_torque_value(writer, value.starter) &&
           writer.key("instantaneous_net_shaft") &&
           write_torque_value(writer, value.instantaneous_net_shaft) &&
           writer.key("cycle_mean_net_shaft") &&
           write_torque_value(writer, value.cycle_mean_net_shaft) &&
           writer.key("actuator") && write_torque_value(writer, value.actuator) &&
           writer.key("dyno_reaction") &&
           write_torque_value(writer, value.dyno_reaction) &&
           writer.key("cycle_work_j") && write_quantity(writer, value.cycle_work_j) &&
           writer.key("net_bmep_pa") && write_quantity(writer, value.net_bmep_pa) &&
           writer.key("instantaneous_power_w") &&
           write_quantity(writer, value.instantaneous_power_w) &&
           writer.key("cycle_mean_power_w") &&
           write_quantity(writer, value.cycle_mean_power_w) && writer.end_object();
}

[[nodiscard]] bool write_engine_capture(JsonRecordWriter &writer,
                                        const contract::EngineCaptureSample &engine) {
    return writer.begin_object() && writer.key("step_end_index") &&
           writer.uint64_string_value(engine.step_end_index) &&
           writer.key("validity_mask") && writer.fixed_hex32_value(engine.validity) &&
           writer.key("theta_rad") && writer.number_value(engine.theta_rad) &&
           writer.key("theta_cycle_rad") &&
           writer.number_value(engine.theta_cycle_rad) &&
           writer.key("angular_speed_rad_s") &&
           writer.number_value(engine.angular_speed_rad_s) &&
           writer.key("angular_acceleration_rad_s2") &&
           writer.number_value(engine.angular_acceleration_rad_s2) &&
           writer.key("engine_speed_rpm") &&
           writer.number_value(engine.engine_speed_rpm) &&
           writer.key("requested_throttle_01") &&
           writer.number_value(engine.requested_throttle_01) &&
           writer.key("resolved_engine_throttle_01") &&
           writer.number_value(engine.resolved_engine_throttle_01) &&
           writer.key("intake_plate_position_01") &&
           writer.number_value(engine.intake_plate_position_01) &&
           writer.key("main_flow_multiplier_01") &&
           writer.number_value(engine.main_flow_multiplier_01) &&
           writer.key("ignition_enabled") &&
           writer.bool_value(engine.ignition_enabled) && writer.key("fuel_enabled") &&
           writer.bool_value(engine.fuel_enabled) && writer.key("starter_enabled") &&
           writer.bool_value(engine.starter_enabled) && writer.key("dyno_enabled") &&
           writer.bool_value(engine.dyno_enabled) && writer.key("limiter_enabled") &&
           writer.bool_value(engine.limiter_enabled) &&
           writer.key("limiter_cut_active") &&
           writer.bool_value(engine.limiter_cut_active) &&
           writer.key("requested_external_resisting_torque_nm") &&
           writer.number_value(engine.requested_external_resisting_torque_nm) &&
           writer.key("torque") && write_torque_telemetry(writer, engine.torque) &&
           writer.end_object();
}

[[nodiscard]] bool
write_held_dyno(JsonRecordWriter &writer,
                const std::optional<EngineHeldDynoTelemetry> &value) {
    if (!value.has_value()) {
        return writer.null_value();
    }
    return writer.begin_object() && writer.key("target_engine_speed_rpm") &&
           writer.number_value(value->target_engine_speed_rpm) &&
           writer.key("maximum_absorbing_torque_nm") &&
           writer.number_value(value->maximum_absorbing_torque_nm) &&
           writer.key("maximum_driving_torque_nm") &&
           writer.number_value(value->maximum_driving_torque_nm) &&
           writer.key("required_actuator_torque_nm") &&
           writer.number_value(value->required_actuator_torque_nm) &&
           writer.key("applied_actuator_torque_nm") &&
           writer.number_value(value->applied_actuator_torque_nm) &&
           writer.key("disposition") &&
           writer.string_value(*held_dyno_disposition_name(value->disposition)) &&
           writer.end_object();
}

[[nodiscard]] bool
write_free_vehicle(JsonRecordWriter &writer,
                   const std::optional<EngineFreeVehicleTelemetry> &value) {
    if (!value.has_value()) {
        return writer.null_value();
    }
    return writer.begin_object() && writer.key("vehicle_speed_m_s") &&
           writer.number_value(value->vehicle_speed_m_s) &&
           writer.key("vehicle_distance_m") &&
           writer.number_value(value->vehicle_distance_m) &&
           writer.key("selected_forward_gear_ordinal") &&
           (value->selected_forward_gear_ordinal.has_value()
                ? writer.uint32_value(*value->selected_forward_gear_ordinal)
                : writer.null_value()) &&
           writer.key("clutch_engagement_01") &&
           writer.number_value(value->clutch_engagement_01) &&
           writer.key("service_brake_application_01") &&
           writer.number_value(value->service_brake_application_01) &&
           writer.key("clutch_disposition") &&
           writer.string_value(*clutch_disposition_name(value->clutch_disposition)) &&
           writer.key("clutch_torque_capacity_nm") &&
           writer.number_value(value->clutch_torque_capacity_nm) &&
           writer.key("applied_average_clutch_torque_on_engine_nm") &&
           writer.number_value(value->applied_average_clutch_torque_on_engine_nm) &&
           writer.key("final_clutch_slip_rad_s") &&
           (value->final_clutch_slip_rad_s.has_value()
                ? writer.number_value(*value->final_clutch_slip_rad_s)
                : writer.null_value()) &&
           writer.key("road_load_disposition") &&
           writer.string_value(
               *road_load_disposition_name(value->road_load_disposition)) &&
           writer.key("requested_road_load_force_n") &&
           writer.number_value(value->requested_road_load_force_n) &&
           writer.key("applied_average_road_load_force_n") &&
           writer.number_value(value->applied_average_road_load_force_n) &&
           writer.end_object();
}

[[nodiscard]] bool write_endpoint(JsonRecordWriter &writer,
                                  const EngineTelemetryFrame &frame) {
    return writer.begin_object() && writer.key("physics_step_end") &&
           writer.uint64_string_value(frame.physics_step_end) &&
           writer.key("mean_intake_manifold_pressure_pa_abs") &&
           writer.number_value(frame.mean_intake_manifold_pressure_pa_abs) &&
           writer.key("engine") && write_engine_capture(writer, frame.engine) &&
           writer.key("held_dyno") && write_held_dyno(writer, frame.held_dyno) &&
           writer.key("free_vehicle") &&
           write_free_vehicle(writer, frame.free_vehicle) && writer.end_object();
}

[[nodiscard]] bool write_boundary(JsonRecordWriter &writer,
                                  const EngineCycleBoundaryEvidence &boundary) {
    return writer.begin_object() && writer.key("cycle_ordinal") &&
           writer.int64_string_value(boundary.cycle_ordinal) &&
           writer.key("left_physics_frame") &&
           writer.uint64_string_value(boundary.left_physics_frame) &&
           writer.key("right_physics_frame") &&
           writer.uint64_string_value(boundary.right_physics_frame) &&
           writer.key("fraction_from_left_01") &&
           writer.number_value(boundary.fraction_from_left_01) &&
           writer.key("theta_unwrapped_rad") &&
           writer.number_value(boundary.theta_unwrapped_rad) && writer.key("time_s") &&
           writer.number_value(boundary.time_s) && writer.key("delivery_frame") &&
           writer.number_value(boundary.delivery_frame) && writer.end_object();
}

[[nodiscard]] bool write_control(JsonRecordWriter &writer,
                                 const EngineCycleControlEvidence &control) {
    return writer.begin_object() && writer.key("time_weighted_mean_01") &&
           writer.number_value(control.time_weighted_mean_01) &&
           writer.key("minimum_01") && writer.number_value(control.minimum_01) &&
           writer.key("maximum_01") && writer.number_value(control.maximum_01) &&
           writer.key("change_count") && writer.uint32_value(control.change_count) &&
           writer.end_object();
}

[[nodiscard]] bool write_cycle_net_shaft(JsonRecordWriter &writer,
                                         const EngineCycleNetShaftEvidence &torque) {
    return writer.begin_object() && writer.key("angular_work_j") &&
           writer.number_value(torque.angular_work_j) &&
           writer.key("cycle_mean_torque_nm") &&
           writer.number_value(torque.cycle_mean_torque_nm) &&
           writer.key("availability") &&
           writer.string_value(*availability_name(torque.availability)) &&
           writer.key("completeness") &&
           writer.string_value(*completeness_name(torque.completeness)) &&
           writer.key("unavailable_reason") &&
           writer.string_value(*unavailable_reason_name(torque.unavailable_reason)) &&
           writer.key("included_terms_mask") &&
           writer.fixed_hex64_value(torque.included_terms) &&
           writer.key("omitted_terms_mask") &&
           writer.fixed_hex64_value(torque.omitted_terms) && writer.end_object();
}

[[nodiscard]] bool write_cycle(JsonRecordWriter &writer,
                               const EngineCompletedCycleEvidence &cycle) {
    return writer.begin_object() && writer.key("completed_cycle_ordinal") &&
           writer.uint64_string_value(cycle.completed_cycle_ordinal) &&
           writer.key("start_boundary") &&
           write_boundary(writer, cycle.start_boundary) && writer.key("end_boundary") &&
           write_boundary(writer, cycle.end_boundary) && writer.key("duration_s") &&
           writer.number_value(cycle.duration_s) &&
           writer.key("mean_engine_speed_rpm") &&
           writer.number_value(cycle.mean_engine_speed_rpm) &&
           writer.key("requested_throttle") &&
           write_control(writer, cycle.requested_throttle) &&
           writer.key("resolved_engine_throttle") &&
           write_control(writer, cycle.resolved_engine_throttle) &&
           writer.key("intake_plate_position") &&
           write_control(writer, cycle.intake_plate_position) &&
           writer.key("instantaneous_net_shaft") &&
           write_cycle_net_shaft(writer, cycle.instantaneous_net_shaft) &&
           writer.key("start_state_flags_mask") &&
           writer.fixed_hex32_value(cycle.start_state_flags) &&
           writer.key("end_state_flags_mask") &&
           writer.fixed_hex32_value(cycle.end_state_flags) &&
           writer.key("state_transition_flags_mask") &&
           writer.fixed_hex32_value(cycle.state_transition_flags) &&
           writer.end_object();
}

using RecordResult = std::variant<std::vector<std::byte>, Error>;

[[nodiscard]] RecordResult finish_record(JsonRecordWriter writer,
                                         std::string_view record_name) {
    if (auto bytes = writer.finish()) {
        return std::move(*bytes);
    }
    return error(ErrorCode::encoding_failure, std::string{record_name},
                 "canonical JSON encoding failed: " +
                     std::string{writer.failure_message()});
}

[[nodiscard]] RecordResult
make_header_record(const EngineTelemetryNdjsonStreamDescriptor &descriptor) {
    JsonRecordWriter writer;
    const auto audible_block_count =
        descriptor.total_block_count - descriptor.preparation_block_count;
    const bool encoded =
        writer.begin_object() && writer.key("record_type") &&
        writer.string_value("header") && writer.key("schema") &&
        writer.string_value(kEngineTelemetryNdjsonSchemaIdV1) &&
        writer.key("schema_version") &&
        writer.uint32_value(kEngineTelemetryNdjsonSchemaVersionV1) &&
        writer.key("simulation_request_identity_v7_sha256") &&
        writer.sha256_value(descriptor.simulation_request_identity_v7_sha256) &&
        writer.key("engine_id") && writer.string_value(descriptor.engine_id) &&
        writer.key("scenario_id") && writer.string_value(descriptor.scenario_id) &&
        writer.key("execution_kind") && writer.string_value("finite_scenario") &&
        writer.key("motion_mode") &&
        writer.string_value(*motion_mode_name(descriptor.motion_mode)) &&
        writer.key("physics_rate_hz") && write_rate(writer, descriptor.physics_rate) &&
        writer.key("delivery_rate_hz") &&
        write_rate(writer, descriptor.delivery_rate) &&
        writer.key("physics_frames_per_block") &&
        writer.uint32_value(descriptor.physics_frames_per_block) &&
        writer.key("delivery_frames_per_block") &&
        writer.uint32_value(descriptor.delivery_frames_per_block) &&
        writer.key("preparation_block_count") &&
        writer.uint64_string_value(descriptor.preparation_block_count) &&
        writer.key("audible_block_count") &&
        writer.uint64_string_value(audible_block_count) &&
        writer.key("total_block_count") &&
        writer.uint64_string_value(descriptor.total_block_count) && writer.end_object();
    if (!encoded) {
        return error(ErrorCode::encoding_failure, "header",
                     "canonical JSON header encoding failed: " +
                         std::string{writer.failure_message()});
    }
    return finish_record(std::move(writer), "header");
}

[[nodiscard]] double endpoint_time_s(std::uint64_t frame,
                                     const contract::RationalRateHz &rate) noexcept {
    return static_cast<double>(frame) * static_cast<double>(rate.denominator) /
           static_cast<double>(rate.numerator);
}

[[nodiscard]] double scenario_endpoint_time_s(
    std::uint64_t physics_frame_end,
    const EngineTelemetryNdjsonStreamDescriptor &descriptor) noexcept {
    const auto preparation_frame_end =
        descriptor.preparation_block_count * descriptor.physics_frames_per_block;
    const bool negative = physics_frame_end < preparation_frame_end;
    const auto magnitude = negative ? preparation_frame_end - physics_frame_end
                                    : physics_frame_end - preparation_frame_end;
    const auto seconds = endpoint_time_s(magnitude, descriptor.physics_rate);
    return negative ? -seconds : seconds;
}

[[nodiscard]] RecordResult
make_block_record(const EngineTelemetryNdjsonStreamDescriptor &descriptor,
                  const EngineTelemetryNdjsonBlockInput &block,
                  std::uint64_t physics_end, std::uint64_t delivery_end,
                  std::uint64_t preparation_delivery_end) {
    JsonRecordWriter writer;
    const bool audible = block.phase == EngineSessionBlockPhase::audible;
    const auto audition_begin =
        audible ? block.first_delivery_frame - preparation_delivery_end : 0U;
    const auto audition_end = audible ? delivery_end - preparation_delivery_end : 0U;
    const bool encoded =
        writer.begin_object() && writer.key("record_type") &&
        writer.string_value("block") && writer.key("block_ordinal") &&
        writer.uint64_string_value(block.block_ordinal) && writer.key("phase") &&
        writer.string_value(*block_phase_name(block.phase)) &&
        writer.key("physics_frame_range") &&
        write_frame_range(writer, block.first_physics_frame, physics_end) &&
        writer.key("delivery_frame_range") &&
        write_frame_range(writer, block.first_delivery_frame, delivery_end) &&
        writer.key("audition_frame_range") &&
        (audible ? write_frame_range(writer, audition_begin, audition_end)
                 : writer.null_value()) &&
        writer.key("endpoint_session_time_s") &&
        writer.number_value(endpoint_time_s(physics_end, descriptor.physics_rate)) &&
        writer.key("endpoint_scenario_time_s") &&
        writer.number_value(scenario_endpoint_time_s(physics_end, descriptor)) &&
        writer.key("telemetry") && write_endpoint(writer, block.telemetry.front()) &&
        writer.key("event_counters") &&
        write_event_counters(writer, block.event_counters) && writer.end_object();
    if (!encoded) {
        return error(ErrorCode::encoding_failure, "block",
                     "canonical JSON block encoding failed: " +
                         std::string{writer.failure_message()});
    }
    return finish_record(std::move(writer), "block");
}

[[nodiscard]] RecordResult
make_cycle_record(std::uint64_t block_ordinal,
                  const EngineCompletedCycleEvidence &cycle) {
    JsonRecordWriter writer;
    const bool encoded =
        writer.begin_object() && writer.key("record_type") &&
        writer.string_value("cycle") && writer.key("emitting_block_ordinal") &&
        writer.uint64_string_value(block_ordinal) && writer.key("cycle") &&
        write_cycle(writer, cycle) && writer.end_object();
    if (!encoded) {
        return error(ErrorCode::encoding_failure, "cycle",
                     "canonical JSON cycle encoding failed: " +
                         std::string{writer.failure_message()});
    }
    return finish_record(std::move(writer), "cycle");
}

[[nodiscard]] RecordResult
make_footer_record(const EngineTelemetryNdjsonStreamDescriptor &descriptor,
                   std::uint64_t block_count, std::uint64_t cycle_count,
                   const EngineEventCounters &event_totals) {
    const auto audible_block_count =
        descriptor.total_block_count - descriptor.preparation_block_count;
    const auto final_physics_frame_end =
        descriptor.total_block_count * descriptor.physics_frames_per_block;
    const auto final_delivery_frame_end =
        descriptor.total_block_count * descriptor.delivery_frames_per_block;
    const auto final_audition_frame_end =
        audible_block_count * descriptor.delivery_frames_per_block;
    JsonRecordWriter writer;
    const bool encoded =
        writer.begin_object() && writer.key("record_type") &&
        writer.string_value("footer") && writer.key("block_count") &&
        writer.uint64_string_value(block_count) &&
        writer.key("preparation_block_count") &&
        writer.uint64_string_value(descriptor.preparation_block_count) &&
        writer.key("audible_block_count") &&
        writer.uint64_string_value(audible_block_count) && writer.key("cycle_count") &&
        writer.uint64_string_value(cycle_count) && writer.key("event_totals") &&
        write_event_counters(writer, event_totals) &&
        writer.key("final_physics_frame_end") &&
        writer.uint64_string_value(final_physics_frame_end) &&
        writer.key("final_delivery_frame_end") &&
        writer.uint64_string_value(final_delivery_frame_end) &&
        writer.key("final_audition_frame_end") &&
        writer.uint64_string_value(final_audition_frame_end) && writer.end_object();
    if (!encoded) {
        return error(ErrorCode::encoding_failure, "footer",
                     "canonical JSON footer encoding failed: " +
                         std::string{writer.failure_message()});
    }
    return finish_record(std::move(writer), "footer");
}

[[nodiscard]] Status emit_record(std::span<const std::byte> record,
                                 std::size_t maximum_chunk_bytes,
                                 std::uint64_t &byte_offset,
                                 const EngineTelemetryNdjsonChunkConsumer &consumer) {
    if (record.empty() || maximum_chunk_bytes == 0U) {
        return error(ErrorCode::encoding_failure, "record",
                     "canonical NDJSON record or chunk size is empty");
    }
    std::size_t consumed = 0;
    while (consumed < record.size()) {
        const auto count = std::min(maximum_chunk_bytes, record.size() - consumed);
        if (count > std::numeric_limits<std::uint64_t>::max() - byte_offset) {
            return error(ErrorCode::size_overflow, "byte_offset",
                         "telemetry artifact byte offset overflowed uint64");
        }
        bool accepted = false;
        try {
            accepted = consumer(byte_offset, record.subspan(consumed, count));
        } catch (...) {
            accepted = false;
        }
        if (!accepted) {
            return error(ErrorCode::callback_rejected, "consumer",
                         "artifact consumer rejected engine telemetry NDJSON bytes");
        }
        consumed += count;
        byte_offset += count;
    }
    return std::nullopt;
}

[[nodiscard]] Status
validate_descriptor(const EngineTelemetryNdjsonStreamDescriptor &descriptor) {
    if (descriptor.simulation_request_identity_v7_sha256.is_zero()) {
        return error(ErrorCode::invalid_descriptor,
                     "simulation_request_identity_v7_sha256",
                     "finite telemetry requires the exact nonzero request identity");
    }
    if (!contract::is_valid_semantic_id(descriptor.engine_id)) {
        return error(ErrorCode::invalid_descriptor, "engine_id",
                     "engine identifier is not a valid semantic id");
    }
    if (!contract::is_valid_semantic_id(descriptor.scenario_id)) {
        return error(ErrorCode::invalid_descriptor, "scenario_id",
                     "scenario identifier is not a valid semantic id");
    }
    if (descriptor.execution_kind != EngineSessionExecutionKind::finite_scenario) {
        return error(ErrorCode::invalid_descriptor, "execution_kind",
                     "schema v1 admits finite native sessions only");
    }
    if (!motion_mode_name(descriptor.motion_mode).has_value()) {
        return error(ErrorCode::invalid_enum, "motion_mode",
                     "motion mode has no frozen schema-v1 representation");
    }
    if (descriptor.physics_rate.numerator == 0U ||
        descriptor.physics_rate.denominator == 0U ||
        descriptor.delivery_rate.numerator == 0U ||
        descriptor.delivery_rate.denominator == 0U) {
        return error(ErrorCode::invalid_descriptor, "rates",
                     "physics and delivery rates must be positive rationals");
    }
    if (descriptor.physics_frames_per_block == 0U ||
        descriptor.delivery_frames_per_block == 0U ||
        descriptor.total_block_count == 0U ||
        descriptor.preparation_block_count >= descriptor.total_block_count) {
        return error(ErrorCode::invalid_descriptor, "block_horizon",
                     "finite telemetry requires nonempty fixed quanta and at least "
                     "one audible block");
    }
    if (!equal_factor_products(
            {descriptor.physics_frames_per_block, descriptor.physics_rate.denominator,
             descriptor.delivery_rate.numerator},
            {descriptor.delivery_frames_per_block, descriptor.delivery_rate.denominator,
             descriptor.physics_rate.numerator})) {
        return error(ErrorCode::invalid_descriptor, "block_duration",
                     "physics and delivery block durations differ");
    }
    if (descriptor.maximum_chunk_bytes == 0U ||
        descriptor.maximum_chunk_bytes > kMaximumEngineTelemetryNdjsonChunkBytes) {
        return error(ErrorCode::invalid_descriptor, "maximum_chunk_bytes",
                     "chunk bound must be in [1,65536]");
    }
    std::uint64_t unused = 0;
    if (!checked_multiply(descriptor.total_block_count,
                          descriptor.physics_frames_per_block, unused) ||
        !checked_multiply(descriptor.total_block_count,
                          descriptor.delivery_frames_per_block, unused) ||
        !checked_multiply(descriptor.preparation_block_count,
                          descriptor.physics_frames_per_block, unused) ||
        !checked_multiply(descriptor.preparation_block_count,
                          descriptor.delivery_frames_per_block, unused)) {
        return error(ErrorCode::integer_overflow, "block_horizon",
                     "declared frame horizons overflow uint64");
    }
    return std::nullopt;
}

} // namespace

EngineTelemetryNdjsonBlockInput
borrow_engine_telemetry_ndjson_block(const EngineSessionBlockView &block) noexcept {
    return {
        block.block_ordinal(),
        block.phase(),
        block.first_physics_frame(),
        block.physics_frame_count(),
        block.first_delivery_frame(),
        block.delivery_frame_count(),
        block.telemetry(),
        block.cycle_evidence(),
        block.event_counters(),
    };
}

EngineTelemetryNdjsonEncoder::EngineTelemetryNdjsonEncoder(
    EngineTelemetryNdjsonStreamDescriptor descriptor) noexcept
    : descriptor_(std::move(descriptor)) {}

EngineTelemetryNdjsonEncodingStatus
EngineTelemetryNdjsonEncoder::fail(EngineTelemetryNdjsonEncodingError value) noexcept {
    state_ = State::failed;
    return value;
}

EngineTelemetryNdjsonEncodingStatus EngineTelemetryNdjsonEncoder::begin(
    const EngineTelemetryNdjsonChunkConsumer &consumer) {
    if (state_ != State::ready) {
        return fail(error(ErrorCode::invalid_state, "state",
                          "telemetry header may be emitted exactly once"));
    }
    try {
        auto record = make_header_record(descriptor_);
        if (const auto *failure = std::get_if<Error>(&record)) {
            return fail(*failure);
        }
        if (auto issue = emit_record(std::get<std::vector<std::byte>>(record),
                                     descriptor_.maximum_chunk_bytes, bytes_emitted_,
                                     consumer)) {
            return fail(std::move(*issue));
        }
        state_ = State::begun;
        return std::nullopt;
    } catch (const std::bad_alloc &) {
        return fail(error(ErrorCode::allocation_failure, "header",
                          "telemetry header encoding exhausted memory"));
    } catch (const std::exception &caught) {
        return fail(
            error(ErrorCode::encoding_failure, "header",
                  "telemetry header encoding threw: " + std::string{caught.what()}));
    } catch (...) {
        return fail(error(ErrorCode::encoding_failure, "header",
                          "telemetry header encoding threw a non-standard exception"));
    }
}

EngineTelemetryNdjsonEncodingStatus EngineTelemetryNdjsonEncoder::write_block(
    const EngineSessionBlockView &block,
    const EngineTelemetryNdjsonChunkConsumer &consumer) {
    return write_block(borrow_engine_telemetry_ndjson_block(block), consumer);
}

EngineTelemetryNdjsonEncodingStatus EngineTelemetryNdjsonEncoder::write_block(
    const EngineTelemetryNdjsonBlockInput &block,
    const EngineTelemetryNdjsonChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(ErrorCode::invalid_state, "state",
                          "telemetry blocks require an emitted header"));
    }
    try {
        if (blocks_written_ >= descriptor_.total_block_count ||
            block.block_ordinal != blocks_written_) {
            return fail(error(ErrorCode::unexpected_block, "block.block_ordinal",
                              "blocks must be written exactly once in ordinal order"));
        }
        const auto expected_phase =
            blocks_written_ < descriptor_.preparation_block_count
                ? EngineSessionBlockPhase::preparation
                : EngineSessionBlockPhase::audible;
        if (!block_phase_name(block.phase).has_value() ||
            block.phase != expected_phase) {
            return fail(
                error(ErrorCode::unexpected_block, "block.phase",
                      "block phase differs from the declared preparation horizon"));
        }
        std::uint64_t expected_physics_begin = 0;
        std::uint64_t expected_delivery_begin = 0;
        std::uint64_t physics_end = 0;
        std::uint64_t delivery_end = 0;
        std::uint64_t preparation_delivery_end = 0;
        if (!checked_multiply(blocks_written_, descriptor_.physics_frames_per_block,
                              expected_physics_begin) ||
            !checked_multiply(blocks_written_, descriptor_.delivery_frames_per_block,
                              expected_delivery_begin) ||
            !checked_add(expected_physics_begin, descriptor_.physics_frames_per_block,
                         physics_end) ||
            !checked_add(expected_delivery_begin, descriptor_.delivery_frames_per_block,
                         delivery_end) ||
            !checked_multiply(descriptor_.preparation_block_count,
                              descriptor_.delivery_frames_per_block,
                              preparation_delivery_end)) {
            return fail(error(ErrorCode::integer_overflow, "block.frame_ranges",
                              "expected block frame range overflowed uint64"));
        }
        if (block.first_physics_frame != expected_physics_begin ||
            block.physics_frame_count != descriptor_.physics_frames_per_block ||
            block.first_delivery_frame != expected_delivery_begin ||
            block.delivery_frame_count != descriptor_.delivery_frames_per_block) {
            return fail(error(ErrorCode::discontinuous_range, "block.frame_ranges",
                              "block is not the next exact physics/delivery quantum"));
        }
        if (block.telemetry.size() != 1U) {
            return fail(
                error(ErrorCode::endpoint_count_mismatch, "block.telemetry",
                      "schema v1 requires exactly one endpoint per native block"));
        }
        if (block.telemetry.front().physics_step_end != physics_end ||
            block.telemetry.front().engine.step_end_index != physics_end) {
            return fail(error(ErrorCode::endpoint_count_mismatch,
                              "block.telemetry.physics_step_end",
                              "endpoint step indices must equal the block's exclusive "
                              "physics-frame end"));
        }
        if (auto issue =
                validate_endpoint(block.telemetry.front(), descriptor_.motion_mode)) {
            return fail(std::move(*issue));
        }
        if (auto issue =
                validate_event_counters(block.event_counters, "block.event_counters")) {
            return fail(std::move(*issue));
        }

        if (block.cycle_evidence.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
            return fail(error(ErrorCode::integer_overflow, "block.cycle_evidence",
                              "cycle count is not representable as uint64"));
        }
        const auto block_cycle_count =
            static_cast<std::uint64_t>(block.cycle_evidence.size());
        std::uint64_t next_cycle_count = 0;
        if (!checked_add(cycles_written_, block_cycle_count, next_cycle_count)) {
            return fail(error(ErrorCode::integer_overflow, "cycle_count",
                              "telemetry cycle count overflowed uint64"));
        }
        for (std::size_t index = 0; index < block.cycle_evidence.size(); ++index) {
            std::uint64_t expected_cycle = 0;
            if (!checked_add(cycles_written_, static_cast<std::uint64_t>(index),
                             expected_cycle)) {
                return fail(error(ErrorCode::integer_overflow, "cycle_count",
                                  "expected cycle ordinal overflowed uint64"));
            }
            if (auto issue =
                    validate_cycle(block.cycle_evidence[index], expected_cycle,
                                   expected_physics_begin, physics_end, index)) {
                return fail(std::move(*issue));
            }
        }
        EngineEventCounters next_event_totals;
        if (!add_event_counters(event_totals_, block.event_counters,
                                next_event_totals)) {
            return fail(error(ErrorCode::integer_overflow, "event_totals",
                              "aggregate event totals overflowed uint64"));
        }

        try {
            std::vector<std::vector<std::byte>> records;
            records.reserve(block.cycle_evidence.size() + 1U);
            auto block_record =
                make_block_record(descriptor_, block, physics_end, delivery_end,
                                  preparation_delivery_end);
            if (const auto *failure = std::get_if<Error>(&block_record)) {
                return fail(*failure);
            }
            records.push_back(
                std::move(std::get<std::vector<std::byte>>(block_record)));
            for (const auto &cycle : block.cycle_evidence) {
                auto cycle_record = make_cycle_record(block.block_ordinal, cycle);
                if (const auto *failure = std::get_if<Error>(&cycle_record)) {
                    return fail(*failure);
                }
                records.push_back(
                    std::move(std::get<std::vector<std::byte>>(cycle_record)));
            }
            for (const auto &record : records) {
                if (auto issue = emit_record(record, descriptor_.maximum_chunk_bytes,
                                             bytes_emitted_, consumer)) {
                    return fail(std::move(*issue));
                }
            }
            ++blocks_written_;
            cycles_written_ = next_cycle_count;
            event_totals_ = next_event_totals;
            return std::nullopt;
        } catch (const std::bad_alloc &) {
            return fail(error(ErrorCode::allocation_failure, "block",
                              "telemetry block encoding exhausted memory"));
        } catch (const std::exception &caught) {
            return fail(
                error(ErrorCode::encoding_failure, "block",
                      "telemetry block encoding threw: " + std::string{caught.what()}));
        } catch (...) {
            return fail(
                error(ErrorCode::encoding_failure, "block",
                      "telemetry block encoding threw a non-standard exception"));
        }
    } catch (const std::bad_alloc &) {
        return fail(error(ErrorCode::allocation_failure, "block.validation",
                          "telemetry block validation exhausted memory"));
    } catch (const std::exception &caught) {
        return fail(
            error(ErrorCode::encoding_failure, "block.validation",
                  "telemetry block validation threw: " + std::string{caught.what()}));
    } catch (...) {
        return fail(error(ErrorCode::encoding_failure, "block.validation",
                          "telemetry block validation threw a non-standard exception"));
    }
}

EngineTelemetryNdjsonEncodingStatus EngineTelemetryNdjsonEncoder::finish(
    const EngineTelemetryNdjsonChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(ErrorCode::invalid_state, "state",
                          "telemetry footer requires an active stream"));
    }
    try {
        if (blocks_written_ != descriptor_.total_block_count) {
            return fail(error(ErrorCode::unexpected_block, "block_count",
                              "telemetry cannot finish before every declared block"));
        }
        if (auto issue = validate_event_counters(event_totals_, "event_totals")) {
            return fail(std::move(*issue));
        }
        try {
            auto record = make_footer_record(descriptor_, blocks_written_,
                                             cycles_written_, event_totals_);
            if (const auto *failure = std::get_if<Error>(&record)) {
                return fail(*failure);
            }
            if (auto issue = emit_record(std::get<std::vector<std::byte>>(record),
                                         descriptor_.maximum_chunk_bytes,
                                         bytes_emitted_, consumer)) {
                return fail(std::move(*issue));
            }
            state_ = State::finished;
            return std::nullopt;
        } catch (const std::bad_alloc &) {
            return fail(error(ErrorCode::allocation_failure, "footer",
                              "telemetry footer encoding exhausted memory"));
        } catch (const std::exception &caught) {
            return fail(error(ErrorCode::encoding_failure, "footer",
                              "telemetry footer encoding threw: " +
                                  std::string{caught.what()}));
        } catch (...) {
            return fail(
                error(ErrorCode::encoding_failure, "footer",
                      "telemetry footer encoding threw a non-standard exception"));
        }
    } catch (const std::bad_alloc &) {
        return fail(error(ErrorCode::allocation_failure, "footer.validation",
                          "telemetry footer validation exhausted memory"));
    } catch (const std::exception &caught) {
        return fail(
            error(ErrorCode::encoding_failure, "footer.validation",
                  "telemetry footer validation threw: " + std::string{caught.what()}));
    } catch (...) {
        return fail(
            error(ErrorCode::encoding_failure, "footer.validation",
                  "telemetry footer validation threw a non-standard exception"));
    }
}

const EngineTelemetryNdjsonStreamDescriptor &
EngineTelemetryNdjsonEncoder::descriptor() const noexcept {
    return descriptor_;
}

std::uint64_t EngineTelemetryNdjsonEncoder::blocks_written() const noexcept {
    return blocks_written_;
}

std::uint64_t EngineTelemetryNdjsonEncoder::cycles_written() const noexcept {
    return cycles_written_;
}

std::uint64_t EngineTelemetryNdjsonEncoder::bytes_emitted() const noexcept {
    return bytes_emitted_;
}

const EngineEventCounters &EngineTelemetryNdjsonEncoder::event_totals() const noexcept {
    return event_totals_;
}

bool EngineTelemetryNdjsonEncoder::failed() const noexcept {
    return state_ == State::failed;
}

bool EngineTelemetryNdjsonEncoder::finished() const noexcept {
    return state_ == State::finished;
}

EngineTelemetryNdjsonEncoderResult
make_engine_telemetry_ndjson_encoder(EngineTelemetryNdjsonStreamDescriptor descriptor) {
    try {
        if (auto issue = validate_descriptor(descriptor)) {
            return std::move(*issue);
        }
        return EngineTelemetryNdjsonEncoder{std::move(descriptor)};
    } catch (const std::bad_alloc &) {
        return error(ErrorCode::allocation_failure, "descriptor",
                     "telemetry encoder construction exhausted memory");
    } catch (const std::exception &caught) {
        return error(ErrorCode::encoding_failure, "descriptor",
                     "telemetry encoder construction threw: " +
                         std::string{caught.what()});
    } catch (...) {
        return error(ErrorCode::encoding_failure, "descriptor",
                     "telemetry encoder construction threw a non-standard exception");
    }
}

EngineTelemetryNdjsonEncoderResult make_engine_telemetry_ndjson_encoder(
    const EngineSessionDescriptor &session,
    const contract::Sha256Digest &simulation_request_identity_v7_sha256,
    std::size_t maximum_chunk_bytes) {
    try {
        return make_engine_telemetry_ndjson_encoder({
            simulation_request_identity_v7_sha256,
            std::string{session.engine_id},
            std::string{session.scenario_id},
            session.execution_kind,
            session.motion_mode,
            session.physics_rate,
            session.delivery_rate,
            session.physics_frames_per_block,
            session.delivery_frames_per_block,
            session.preparation_block_count,
            session.total_block_count,
            maximum_chunk_bytes,
        });
    } catch (const std::bad_alloc &) {
        return error(ErrorCode::allocation_failure, "descriptor",
                     "telemetry descriptor copy exhausted memory");
    } catch (const std::exception &caught) {
        return error(ErrorCode::encoding_failure, "descriptor",
                     "telemetry descriptor copy threw: " + std::string{caught.what()});
    } catch (...) {
        return error(ErrorCode::encoding_failure, "descriptor",
                     "telemetry descriptor copy threw a non-standard exception");
    }
}

} // namespace engine_sim_offline::artifacts
