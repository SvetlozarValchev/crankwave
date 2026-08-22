#include "crankwave/authoring/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace crankwave::authoring {
namespace detail {

struct JsonObjectMember {
    std::string key;
    std::size_t value_node_index = 0;
};

struct JsonNode {
    JsonKind kind = JsonKind::invalid;
    bool boolean_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<std::size_t> array_elements;
    std::vector<JsonObjectMember> object_members;
};

struct JsonDocumentStorage {
    std::vector<JsonNode> nodes;
    std::size_t root_node_index = 0;
};

} // namespace detail

namespace {

[[nodiscard]] bool is_json_whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] bool is_decimal_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_nonzero_decimal_digit(char value) noexcept {
    return value >= '1' && value <= '9';
}

[[nodiscard]] int hexadecimal_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] JsonSourceLocation location_at(std::string_view input,
                                             std::size_t requested_offset) noexcept {
    const std::size_t offset = std::min(requested_offset, input.size());
    JsonSourceLocation location{offset, 1U, 1U};

    std::size_t cursor = 0;
    while (cursor < offset) {
        const char value = input[cursor];
        if (value == '\r') {
            ++location.line;
            location.column = 1U;
            ++cursor;
            if (cursor < offset && input[cursor] == '\n') {
                ++cursor;
            }
            continue;
        }
        if (value == '\n') {
            ++location.line;
            location.column = 1U;
            ++cursor;
            continue;
        }

        ++location.column;
        ++cursor;
    }

    return location;
}

[[nodiscard]] JsonParseError make_error(std::string_view input,
                                        JsonParseErrorCode code,
                                        std::size_t offset,
                                        std::string_view message) noexcept {
    JsonParseError error;
    error.code = code;
    error.location = location_at(input, offset);

    const std::size_t copied_size =
        std::min(message.size(), JsonParseError::kMaximumMessageBytes);
    std::copy_n(message.data(), copied_size, error.message_bytes.data());
    error.message_bytes[copied_size] = '\0';
    error.message_size = static_cast<std::uint16_t>(copied_size);
    return error;
}

struct Utf8Sequence {
    bool valid = false;
    std::size_t length = 0;
    std::size_t error_offset = 0;
};

[[nodiscard]] bool is_utf8_continuation(unsigned char value) noexcept {
    return value >= 0x80U && value <= 0xbfU;
}

[[nodiscard]] Utf8Sequence validate_utf8_sequence(std::string_view input,
                                                  std::size_t offset) noexcept {
    Utf8Sequence result{false, 0U, offset};
    if (offset >= input.size()) {
        result.error_offset = input.size();
        return result;
    }

    const auto first = static_cast<unsigned char>(input[offset]);
    if (first <= 0x7fU) {
        return {true, 1U, offset};
    }

    if (first >= 0xc2U && first <= 0xdfU) {
        if (input.size() - offset < 2U) {
            result.error_offset = input.size();
            return result;
        }
        if (!is_utf8_continuation(
                static_cast<unsigned char>(input[offset + 1U]))) {
            result.error_offset = offset + 1U;
            return result;
        }
        return {true, 2U, offset};
    }

    if (first >= 0xe0U && first <= 0xefU) {
        if (input.size() - offset < 3U) {
            result.error_offset = input.size();
            return result;
        }

        const auto second = static_cast<unsigned char>(input[offset + 1U]);
        const auto third = static_cast<unsigned char>(input[offset + 2U]);
        if ((first == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
            (first == 0xedU && (second < 0x80U || second > 0x9fU)) ||
            (first != 0xe0U && first != 0xedU &&
             !is_utf8_continuation(second))) {
            result.error_offset = offset + 1U;
            return result;
        }
        if (!is_utf8_continuation(third)) {
            result.error_offset = offset + 2U;
            return result;
        }
        return {true, 3U, offset};
    }

    if (first >= 0xf0U && first <= 0xf4U) {
        if (input.size() - offset < 4U) {
            result.error_offset = input.size();
            return result;
        }

        const auto second = static_cast<unsigned char>(input[offset + 1U]);
        const auto third = static_cast<unsigned char>(input[offset + 2U]);
        const auto fourth = static_cast<unsigned char>(input[offset + 3U]);
        if ((first == 0xf0U && (second < 0x90U || second > 0xbfU)) ||
            (first == 0xf4U && (second < 0x80U || second > 0x8fU)) ||
            (first != 0xf0U && first != 0xf4U &&
             !is_utf8_continuation(second))) {
            result.error_offset = offset + 1U;
            return result;
        }
        if (!is_utf8_continuation(third)) {
            result.error_offset = offset + 2U;
            return result;
        }
        if (!is_utf8_continuation(fourth)) {
            result.error_offset = offset + 3U;
            return result;
        }
        return {true, 4U, offset};
    }

    result.error_offset = offset;
    return result;
}

void append_utf8_scalar(std::string &output, std::uint32_t scalar) {
    if (scalar <= 0x7fU) {
        output.push_back(static_cast<char>(scalar));
    } else if (scalar <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else if (scalar <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU)));
        output.push_back(
            static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    }
}

class Parser final {
  public:
    Parser(std::string_view input, JsonParseLimits limits,
           std::size_t &progress_offset)
        : input_(input), limits_(limits), progress_offset_(progress_offset),
          storage_(std::make_unique<detail::JsonDocumentStorage>()) {}

    [[nodiscard]] bool parse_document() {
        skip_whitespace();
        if (at_end()) {
            return fail(JsonParseErrorCode::empty_input, offset_,
                        "JSON input contains no value");
        }

        std::size_t root_node_index = 0;
        if (!parse_value(0U, root_node_index)) {
            return false;
        }

        skip_whitespace();
        if (!at_end()) {
            return fail(JsonParseErrorCode::trailing_content, offset_,
                        "non-whitespace content follows the root JSON value");
        }

        storage_->root_node_index = root_node_index;
        return true;
    }

    [[nodiscard]] JsonParseError error() const noexcept { return error_; }

    [[nodiscard]] std::unique_ptr<detail::JsonDocumentStorage>
    take_storage() noexcept {
        return std::move(storage_);
    }

  private:
    [[nodiscard]] bool at_end() const noexcept { return offset_ >= input_.size(); }

    [[nodiscard]] char current() const noexcept { return input_[offset_]; }

    void advance(std::size_t count = 1U) noexcept {
        offset_ += count;
        progress_offset_ = offset_;
    }

    void skip_whitespace() noexcept {
        while (!at_end() && is_json_whitespace(current())) {
            advance();
        }
    }

    [[nodiscard]] bool fail(JsonParseErrorCode code, std::size_t offset,
                            std::string_view message) noexcept {
        if (!failed_) {
            error_ = make_error(input_, code, offset, message);
            failed_ = true;
        }
        return false;
    }

    [[nodiscard]] bool can_add_node(std::size_t source_offset) {
        if (storage_->nodes.size() >= limits_.maximum_nodes) {
            return fail(JsonParseErrorCode::node_limit_exceeded, source_offset,
                        "JSON value count exceeds maximum_nodes");
        }
        return true;
    }

    [[nodiscard]] bool add_node(detail::JsonNode node,
                                std::size_t source_offset,
                                std::size_t &node_index) {
        if (!can_add_node(source_offset)) {
            return false;
        }
        node_index = storage_->nodes.size();
        storage_->nodes.push_back(std::move(node));
        return true;
    }

    [[nodiscard]] bool parse_value(std::size_t container_depth,
                                   std::size_t &node_index) {
        skip_whitespace();
        if (at_end()) {
            return fail(JsonParseErrorCode::unexpected_end, offset_,
                        "expected a JSON value before end of input");
        }
        if (!can_add_node(offset_)) {
            return false;
        }

        const char token = current();
        if (token == '{') {
            if (container_depth >= limits_.maximum_depth) {
                return fail(JsonParseErrorCode::depth_limit_exceeded, offset_,
                            "object would exceed maximum_depth");
            }
            return parse_object(container_depth + 1U, node_index);
        }
        if (token == '[') {
            if (container_depth >= limits_.maximum_depth) {
                return fail(JsonParseErrorCode::depth_limit_exceeded, offset_,
                            "array would exceed maximum_depth");
            }
            return parse_array(container_depth + 1U, node_index);
        }
        if (token == '"') {
            return parse_string_node(node_index);
        }
        if (token == 't') {
            return parse_literal("true", JsonKind::boolean, true, node_index);
        }
        if (token == 'f') {
            return parse_literal("false", JsonKind::boolean, false, node_index);
        }
        if (token == 'n') {
            return parse_literal("null", JsonKind::null_value, false, node_index);
        }
        if (token == '-' || is_decimal_digit(token)) {
            return parse_number(node_index);
        }

        if (static_cast<unsigned char>(token) >= 0x80U) {
            const Utf8Sequence sequence = validate_utf8_sequence(input_, offset_);
            if (!sequence.valid) {
                return fail(JsonParseErrorCode::invalid_utf8,
                            sequence.error_offset,
                            "invalid UTF-8 outside a JSON string");
            }
        }
        return fail(JsonParseErrorCode::unexpected_token, offset_,
                    "expected a JSON value");
    }

    [[nodiscard]] bool parse_literal(std::string_view spelling, JsonKind kind,
                                     bool boolean_value,
                                     std::size_t &node_index) {
        const std::size_t start = offset_;
        for (std::size_t index = 0; index < spelling.size(); ++index) {
            if (at_end()) {
                return fail(JsonParseErrorCode::invalid_literal, offset_,
                            "incomplete JSON literal");
            }
            if (current() != spelling[index]) {
                return fail(JsonParseErrorCode::invalid_literal, offset_,
                            "invalid JSON literal");
            }
            advance();
        }

        detail::JsonNode node;
        node.kind = kind;
        node.boolean_value = boolean_value;
        return add_node(std::move(node), start, node_index);
    }

    [[nodiscard]] bool parse_number(std::size_t &node_index) {
        const std::size_t start = offset_;
        if (current() == '-') {
            advance();
            if (at_end()) {
                return fail(JsonParseErrorCode::invalid_number, offset_,
                            "minus sign must be followed by an integer part");
            }
        }

        if (current() == '0') {
            advance();
            if (!at_end() && is_decimal_digit(current())) {
                return fail(JsonParseErrorCode::invalid_number, offset_,
                            "leading zero is not allowed in a JSON number");
            }
        } else if (is_nonzero_decimal_digit(current())) {
            do {
                advance();
            } while (!at_end() && is_decimal_digit(current()));
        } else {
            return fail(JsonParseErrorCode::invalid_number, offset_,
                        "JSON number requires an integer part");
        }

        if (!at_end() && current() == '.') {
            advance();
            if (at_end() || !is_decimal_digit(current())) {
                return fail(JsonParseErrorCode::invalid_number, offset_,
                            "decimal point must be followed by a digit");
            }
            do {
                advance();
            } while (!at_end() && is_decimal_digit(current()));
        }

        if (!at_end() && (current() == 'e' || current() == 'E')) {
            advance();
            if (!at_end() && (current() == '+' || current() == '-')) {
                advance();
            }
            if (at_end() || !is_decimal_digit(current())) {
                return fail(JsonParseErrorCode::invalid_number, offset_,
                            "exponent must contain at least one digit");
            }
            do {
                advance();
            } while (!at_end() && is_decimal_digit(current()));
        }

        const std::string_view token = input_.substr(start, offset_ - start);
        double value = 0.0;
        const auto conversion =
            std::from_chars(token.data(), token.data() + token.size(), value,
                            std::chars_format::general);
        if (conversion.ec == std::errc::result_out_of_range) {
            return fail(JsonParseErrorCode::number_out_of_range, start,
                        "JSON number is outside the supported binary64 range");
        }
        if (conversion.ec != std::errc{} ||
            conversion.ptr != token.data() + token.size()) {
            return fail(JsonParseErrorCode::invalid_number, start,
                        "JSON number could not be converted to binary64");
        }
        if (!std::isfinite(value)) {
            return fail(JsonParseErrorCode::non_finite_number, start,
                        "JSON number did not produce a finite binary64 value");
        }

        detail::JsonNode node;
        node.kind = JsonKind::number;
        node.number_value = value;
        return add_node(std::move(node), start, node_index);
    }

    [[nodiscard]] bool parse_hexadecimal_quad(std::uint16_t &value) {
        value = 0U;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (at_end()) {
                return fail(JsonParseErrorCode::invalid_unicode_escape, offset_,
                            "Unicode escape requires exactly four hexadecimal digits");
            }
            const int digit = hexadecimal_value(current());
            if (digit < 0) {
                return fail(JsonParseErrorCode::invalid_unicode_escape, offset_,
                            "Unicode escape contains a non-hexadecimal digit");
            }
            value = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(value) << 4U) |
                static_cast<std::uint32_t>(digit));
            advance();
        }
        return true;
    }

    [[nodiscard]] bool parse_unicode_escape(std::string &output,
                                            std::size_t escape_start) {
        std::uint16_t first = 0U;
        if (!parse_hexadecimal_quad(first)) {
            return false;
        }

        if (first >= 0xd800U && first <= 0xdbffU) {
            if (input_.size() - offset_ < 2U || input_[offset_] != '\\' ||
                input_[offset_ + 1U] != 'u') {
                return fail(JsonParseErrorCode::unpaired_unicode_surrogate,
                            escape_start,
                            "high Unicode surrogate is not followed by a low surrogate");
            }

            const std::size_t second_escape_start = offset_;
            advance(2U);
            std::uint16_t second = 0U;
            if (!parse_hexadecimal_quad(second)) {
                return false;
            }
            if (second < 0xdc00U || second > 0xdfffU) {
                return fail(JsonParseErrorCode::unpaired_unicode_surrogate,
                            second_escape_start,
                            "high Unicode surrogate is followed by a non-low surrogate");
            }

            const std::uint32_t scalar =
                0x10000U +
                ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                (static_cast<std::uint32_t>(second) - 0xdc00U);
            append_utf8_scalar(output, scalar);
            return true;
        }

        if (first >= 0xdc00U && first <= 0xdfffU) {
            return fail(JsonParseErrorCode::unpaired_unicode_surrogate,
                        escape_start,
                        "low Unicode surrogate has no preceding high surrogate");
        }

        append_utf8_scalar(output, first);
        return true;
    }

    [[nodiscard]] bool parse_string(std::string &output) {
        if (at_end() || current() != '"') {
            return fail(JsonParseErrorCode::unexpected_token, offset_,
                        "expected a JSON string");
        }
        advance();

        while (!at_end()) {
            const std::size_t character_offset = offset_;
            const auto value = static_cast<unsigned char>(current());
            if (value == static_cast<unsigned char>('"')) {
                advance();
                return true;
            }
            if (value < 0x20U) {
                return fail(JsonParseErrorCode::unescaped_control_character,
                            character_offset,
                            "JSON string contains an unescaped control character");
            }

            if (value == static_cast<unsigned char>('\\')) {
                const std::size_t escape_start = offset_;
                advance();
                if (at_end()) {
                    return fail(JsonParseErrorCode::unexpected_end, offset_,
                                "JSON string ends inside an escape sequence");
                }

                const char escape = current();
                advance();
                switch (escape) {
                case '"':
                    output.push_back('"');
                    break;
                case '\\':
                    output.push_back('\\');
                    break;
                case '/':
                    output.push_back('/');
                    break;
                case 'b':
                    output.push_back('\b');
                    break;
                case 'f':
                    output.push_back('\f');
                    break;
                case 'n':
                    output.push_back('\n');
                    break;
                case 'r':
                    output.push_back('\r');
                    break;
                case 't':
                    output.push_back('\t');
                    break;
                case 'u':
                    if (!parse_unicode_escape(output, escape_start)) {
                        return false;
                    }
                    break;
                default:
                    return fail(JsonParseErrorCode::invalid_escape,
                                escape_start + 1U,
                                "JSON string contains an unsupported escape");
                }
                continue;
            }

            if (value <= 0x7fU) {
                output.push_back(static_cast<char>(value));
                advance();
                continue;
            }

            const Utf8Sequence sequence =
                validate_utf8_sequence(input_, character_offset);
            if (!sequence.valid) {
                return fail(JsonParseErrorCode::invalid_utf8,
                            sequence.error_offset,
                            "JSON string contains invalid Unicode scalar UTF-8");
            }
            output.append(input_.substr(character_offset, sequence.length));
            advance(sequence.length);
        }

        return fail(JsonParseErrorCode::unexpected_end, offset_,
                    "JSON string is missing its closing quote");
    }

    [[nodiscard]] bool parse_string_node(std::size_t &node_index) {
        const std::size_t start = offset_;
        detail::JsonNode node;
        node.kind = JsonKind::string;
        if (!parse_string(node.string_value)) {
            return false;
        }
        return add_node(std::move(node), start, node_index);
    }

    [[nodiscard]] bool parse_array(std::size_t container_depth,
                                   std::size_t &node_index) {
        const std::size_t start = offset_;
        detail::JsonNode node;
        node.kind = JsonKind::array;
        if (!add_node(std::move(node), start, node_index)) {
            return false;
        }
        advance();
        skip_whitespace();
        if (!at_end() && current() == ']') {
            advance();
            return true;
        }

        while (true) {
            std::size_t child_node_index = 0;
            if (!parse_value(container_depth, child_node_index)) {
                return false;
            }
            storage_->nodes[node_index].array_elements.push_back(child_node_index);

            skip_whitespace();
            if (at_end()) {
                return fail(JsonParseErrorCode::unexpected_end, offset_,
                            "JSON array is missing its closing bracket");
            }
            if (current() == ']') {
                advance();
                return true;
            }
            if (current() != ',') {
                return fail(JsonParseErrorCode::expected_comma_or_end, offset_,
                            "expected comma or closing bracket after array element");
            }
            advance();
            skip_whitespace();
            if (!at_end() && current() == ']') {
                return fail(JsonParseErrorCode::unexpected_token, offset_,
                            "trailing comma is not allowed in a JSON array");
            }
        }
    }

    [[nodiscard]] bool parse_object(std::size_t container_depth,
                                    std::size_t &node_index) {
        const std::size_t start = offset_;
        detail::JsonNode node;
        node.kind = JsonKind::object;
        if (!add_node(std::move(node), start, node_index)) {
            return false;
        }
        advance();
        skip_whitespace();
        if (!at_end() && current() == '}') {
            advance();
            return true;
        }

        // The set is parse-only. The DOM's ordered member vector remains the source
        // of query order, while hashed membership prevents a large object from
        // forcing a quadratic duplicate-key scan.
        std::unordered_set<std::string> decoded_keys;
        while (true) {
            if (at_end()) {
                return fail(JsonParseErrorCode::unexpected_end, offset_,
                            "JSON object is missing its closing brace");
            }
            if (current() != '"') {
                return fail(JsonParseErrorCode::expected_object_key, offset_,
                            "JSON object member requires a string key");
            }

            const std::size_t key_start = offset_;
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            const auto [unused, inserted] = decoded_keys.insert(key);
            static_cast<void>(unused);
            if (!inserted) {
                return fail(JsonParseErrorCode::duplicate_object_key, key_start,
                            "duplicate decoded JSON object key");
            }

            skip_whitespace();
            if (at_end() || current() != ':') {
                return fail(JsonParseErrorCode::expected_colon, offset_,
                            "JSON object key must be followed by a colon");
            }
            advance();

            std::size_t child_node_index = 0;
            if (!parse_value(container_depth, child_node_index)) {
                return false;
            }
            storage_->nodes[node_index].object_members.push_back(
                detail::JsonObjectMember{std::move(key), child_node_index});

            skip_whitespace();
            if (at_end()) {
                return fail(JsonParseErrorCode::unexpected_end, offset_,
                            "JSON object is missing its closing brace");
            }
            if (current() == '}') {
                advance();
                return true;
            }
            if (current() != ',') {
                return fail(JsonParseErrorCode::expected_comma_or_end, offset_,
                            "expected comma or closing brace after object member");
            }
            advance();
            skip_whitespace();
            if (!at_end() && current() == '}') {
                return fail(JsonParseErrorCode::unexpected_token, offset_,
                            "trailing comma is not allowed in a JSON object");
            }
        }
    }

    std::string_view input_;
    JsonParseLimits limits_;
    std::size_t &progress_offset_;
    std::unique_ptr<detail::JsonDocumentStorage> storage_;
    std::size_t offset_ = 0;
    JsonParseError error_;
    bool failed_ = false;
};

} // namespace

JsonValue::JsonValue(const detail::JsonDocumentStorage *storage,
                     std::size_t node_index) noexcept
    : storage_(storage), node_index_(node_index) {}

bool JsonValue::valid() const noexcept {
    return storage_ != nullptr && node_index_ < storage_->nodes.size();
}

JsonKind JsonValue::kind() const noexcept {
    return valid() ? storage_->nodes[node_index_].kind : JsonKind::invalid;
}

bool JsonValue::is_null() const noexcept {
    return kind() == JsonKind::null_value;
}

std::optional<bool> JsonValue::boolean() const noexcept {
    if (kind() != JsonKind::boolean) {
        return std::nullopt;
    }
    return storage_->nodes[node_index_].boolean_value;
}

std::optional<double> JsonValue::number() const noexcept {
    if (kind() != JsonKind::number) {
        return std::nullopt;
    }
    return storage_->nodes[node_index_].number_value;
}

std::optional<std::string_view> JsonValue::string() const noexcept {
    if (kind() != JsonKind::string) {
        return std::nullopt;
    }
    return storage_->nodes[node_index_].string_value;
}

std::size_t JsonValue::size() const noexcept {
    if (!valid()) {
        return 0U;
    }
    const auto &node = storage_->nodes[node_index_];
    if (node.kind == JsonKind::array) {
        return node.array_elements.size();
    }
    if (node.kind == JsonKind::object) {
        return node.object_members.size();
    }
    return 0U;
}

JsonValue JsonValue::at(std::size_t index) const noexcept {
    if (kind() != JsonKind::array) {
        return {};
    }
    const auto &elements = storage_->nodes[node_index_].array_elements;
    if (index >= elements.size()) {
        return {};
    }
    return JsonValue{storage_, elements[index]};
}

JsonValue JsonValue::find(std::string_view key) const noexcept {
    if (kind() != JsonKind::object) {
        return {};
    }
    const auto &members = storage_->nodes[node_index_].object_members;
    const auto found = std::find_if(
        members.begin(), members.end(),
        [key](const detail::JsonObjectMember &member) {
            return member.key == key;
        });
    if (found == members.end()) {
        return {};
    }
    return JsonValue{storage_, found->value_node_index};
}

JsonMemberView JsonValue::member_at(std::size_t index) const noexcept {
    if (kind() != JsonKind::object) {
        return {};
    }
    const auto &members = storage_->nodes[node_index_].object_members;
    if (index >= members.size()) {
        return {};
    }
    return {
        members[index].key,
        JsonValue{storage_, members[index].value_node_index},
        true,
    };
}

JsonDocument::JsonDocument(
    std::unique_ptr<detail::JsonDocumentStorage> storage) noexcept
    : storage_(std::move(storage)) {}

JsonDocument::JsonDocument(JsonDocument &&) noexcept = default;

JsonDocument &JsonDocument::operator=(JsonDocument &&) noexcept = default;

JsonDocument::~JsonDocument() = default;

JsonValue JsonDocument::root() const noexcept {
    if (storage_ == nullptr || storage_->nodes.empty()) {
        return {};
    }
    return JsonValue{storage_.get(), storage_->root_node_index};
}

std::size_t JsonDocument::node_count() const noexcept {
    return storage_ == nullptr ? 0U : storage_->nodes.size();
}

JsonParseResult parse_json(std::string_view input, JsonParseLimits limits) noexcept {
    if (input.size() > limits.maximum_input_bytes) {
        return JsonParseResult{
            std::in_place_type<JsonParseError>,
            make_error(input, JsonParseErrorCode::input_limit_exceeded,
                       limits.maximum_input_bytes,
                       "JSON input exceeds maximum_input_bytes")};
    }

    std::size_t progress_offset = 0U;
    try {
        Parser parser{input, limits, progress_offset};
        if (!parser.parse_document()) {
            return JsonParseResult{std::in_place_type<JsonParseError>,
                                   parser.error()};
        }
        return JsonParseResult{
            std::in_place_type<JsonDocument>,
            JsonDocument{parser.take_storage()}};
    } catch (const std::bad_alloc &) {
        return JsonParseResult{
            std::in_place_type<JsonParseError>,
            make_error(input, JsonParseErrorCode::allocation_failure,
                       progress_offset,
                       "memory allocation failed while parsing JSON")};
    } catch (...) {
        return JsonParseResult{
            std::in_place_type<JsonParseError>,
            make_error(input, JsonParseErrorCode::internal_failure,
                       progress_offset,
                       "unexpected internal failure while parsing JSON")};
    }
}

} // namespace crankwave::authoring
