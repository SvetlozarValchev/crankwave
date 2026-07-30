#include "engine_sim_offline/authoring/json.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline::authoring;

static_assert(noexcept(parse_json(std::string_view{})));
static_assert(std::is_nothrow_move_constructible_v<JsonDocument>);
static_assert(std::is_nothrow_move_assignable_v<JsonDocument>);
static_assert(!std::is_copy_constructible_v<JsonDocument>);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] JsonDocument require_document(std::string_view input,
                                            JsonParseLimits limits = {}) {
    JsonParseResult result = parse_json(input, limits);
    if (const auto *error = std::get_if<JsonParseError>(&result)) {
        throw std::runtime_error{
            "valid JSON was rejected at byte " +
            std::to_string(error->location.byte_offset) + ": " +
            std::string{error->message()}};
    }
    return std::get<JsonDocument>(std::move(result));
}

[[nodiscard]] JsonParseError require_error(std::string_view input,
                                           JsonParseLimits limits = {}) {
    JsonParseResult result = parse_json(input, limits);
    if (std::holds_alternative<JsonDocument>(result)) {
        throw std::runtime_error{"invalid JSON was accepted"};
    }
    return std::get<JsonParseError>(result);
}

void expect_error(std::string_view input, JsonParseErrorCode code,
                  std::size_t byte_offset, std::size_t line,
                  std::size_t column, std::string_view message,
                  JsonParseLimits limits = {}) {
    const JsonParseError error = require_error(input, limits);
    expect(error.code == code, std::string{message} + ": wrong error code");
    expect(error.location ==
               JsonSourceLocation{byte_offset, line, column},
           std::string{message} + ": wrong source location");
    expect(!error.message().empty(),
           std::string{message} + ": missing error message");
}

void test_complete_dom_and_read_only_queries() {
    constexpr std::string_view input = R"json({
  "nothing": null,
  "enabled": true,
  "gain": -12.5e+2,
  "name": "sample-engine",
  "items": [false, 0, {"port": "rear"}]
})json";

    JsonDocument document = require_document(input);
    const JsonValue root = document.root();
    expect(root.valid() && root.kind() == JsonKind::object,
           "root object query failed");
    expect(root.size() == 5U, "root member count changed");
    expect(document.node_count() == 10U,
           "object keys were incorrectly counted as value nodes");

    expect(root.find("nothing").is_null(), "null query failed");
    expect(root.find("enabled").boolean() == true, "boolean query failed");
    expect(root.find("gain").number() == -1250.0, "number query failed");
    expect(root.find("name").string() == std::string_view{"sample-engine"},
           "string query failed");

    const JsonValue items = root.find("items");
    expect(items.kind() == JsonKind::array && items.size() == 3U,
           "array query failed");
    expect(items.at(0U).boolean() == false, "array boolean query failed");
    expect(items.at(1U).number() == 0.0, "array number query failed");
    expect(items.at(2U).find("port").string() == std::string_view{"rear"},
           "nested object query failed");

    const JsonMemberView first = root.member_at(0U);
    expect(first && first.key == "nothing" && first.value.is_null(),
           "source object member order was not preserved");
    expect(!root.member_at(root.size()), "out-of-range member view was valid");
    expect(!root.find("missing").valid(), "missing object key was valid");
    expect(!root.at(0U).valid(), "object was queried as an array");
    expect(!items.find("name").valid(), "array was queried as an object");
    expect(!items.string().has_value(), "array was queried as a string");
}

void test_document_move_preserves_existing_views() {
    JsonDocument first = require_document(R"json({"value":[7]})json");
    const JsonValue root_before_move = first.root();
    const JsonValue value_before_move = root_before_move.find("value").at(0U);

    JsonDocument second = std::move(first);
    expect(value_before_move.number() == 7.0,
           "document move invalidated an existing value view");
    expect(second.root().find("value").at(0U).number() == 7.0,
           "moved document lost its DOM");
    expect(!first.root().valid(), "moved-from document retained a root view");
}

void test_strings_escapes_unicode_and_embedded_nul() {
    constexpr std::string_view input =
        R"json({"escaped":"\"\\\/\b\f\n\r\t","unicode":"A\u20ac\uD83D\uDE00","raw":"é😀","nul":"\u0000x"})json";
    JsonDocument document = require_document(input);
    const JsonValue root = document.root();

    const std::string expected_escaped{"\"\\/\b\f\n\r\t"};
    expect(root.find("escaped").string() ==
               std::string_view{expected_escaped.data(), expected_escaped.size()},
           "simple JSON escape decoding changed");
    expect(root.find("unicode").string() ==
               std::string_view{"A\xE2\x82\xAC\xF0\x9F\x98\x80"},
           "Unicode escape or surrogate-pair decoding changed");
    expect(root.find("raw").string() == std::string_view{"é😀"},
           "valid raw UTF-8 was not preserved");

    const auto nul = root.find("nul").string();
    expect(nul.has_value() && nul->size() == 2U && (*nul)[0] == '\0' &&
               (*nul)[1] == 'x',
           "escaped U+0000 was not retained in the DOM string");
}

void test_duplicate_decoded_keys_are_rejected() {
    constexpr std::string_view duplicate = R"json({"a":1,"\u0061":2})json";
    expect_error(duplicate, JsonParseErrorCode::duplicate_object_key, 7U, 1U,
                 8U, "escaped duplicate object key");

    constexpr std::string_view nested =
        R"json({"outer":{"x":1,"x":2},"x":3})json";
    const std::size_t second_x = nested.find("\"x\"", nested.find("\"x\"") + 1U);
    expect_error(nested, JsonParseErrorCode::duplicate_object_key, second_x, 1U,
                 second_x + 1U, "nested duplicate object key");

    JsonDocument distinct =
        require_document(R"json({"A":1,"a":2,"a\u0000":3})json");
    expect(distinct.root().size() == 3U,
           "distinct case-sensitive or embedded-NUL keys collided");
}

void test_large_object_membership_and_order() {
    constexpr std::size_t member_count = 4096U;
    std::string input;
    input.reserve(member_count * 16U);
    input.push_back('{');
    for (std::size_t index = 0; index < member_count; ++index) {
        if (index != 0U) {
            input.push_back(',');
        }
        input += "\"key";
        input += std::to_string(index);
        input += "\":";
        input += std::to_string(index);
    }
    input.push_back('}');

    JsonDocument document = require_document(input);
    const JsonValue root = document.root();
    expect(root.size() == member_count,
           "large object did not retain every unique member");
    expect(root.find("key0").number() == 0.0,
           "large object lost its first member");
    expect(root.find("key4095").number() == 4095.0,
           "large object lost its final member");
    const JsonMemberView last = root.member_at(member_count - 1U);
    expect(last && last.key == "key4095",
           "large object source order changed");
}

void test_strict_number_grammar_and_finite_binary64() {
    constexpr std::string_view input =
        R"json([0,-0,1,-1,1.25,-0.25,1e2,1E-2,6.022e+23,1e308])json";
    JsonDocument document = require_document(input);
    const JsonValue values = document.root();
    expect(values.size() == 10U, "valid number set was not fully parsed");
    expect(values.at(0U).number() == 0.0, "zero number changed");
    expect(values.at(1U).number().has_value() &&
               std::signbit(*values.at(1U).number()),
           "negative zero sign was lost");
    expect(values.at(6U).number() == 100.0, "positive exponent changed");
    expect(values.at(7U).number() == 0.01, "negative exponent changed");
    expect(values.at(9U).number().has_value() &&
               std::isfinite(*values.at(9U).number()),
           "large finite binary64 value was rejected");

    const std::string_view malformed_numbers[] = {
        "01", "-01", "1.", "1e", "1e+", "-", "--1",
    };
    for (const std::string_view malformed : malformed_numbers) {
        const JsonParseError error = require_error(malformed);
        expect(error.code == JsonParseErrorCode::invalid_number,
               "malformed numeric grammar returned the wrong error");
    }

    expect(require_error("+1").code == JsonParseErrorCode::unexpected_token,
           "leading plus sign was accepted as a JSON number");
    expect(require_error(".1").code == JsonParseErrorCode::unexpected_token,
           "missing integer part was accepted");
    expect(require_error("NaN").code == JsonParseErrorCode::unexpected_token,
           "NaN was accepted");
    expect(require_error("Infinity").code ==
               JsonParseErrorCode::unexpected_token,
           "Infinity was accepted");
    expect_error("1e309", JsonParseErrorCode::number_out_of_range, 0U, 1U,
                 1U, "overflowing binary64 number");
    expect_error("1e-9999", JsonParseErrorCode::number_out_of_range, 0U, 1U,
                 1U, "underflowing binary64 number");
}

void test_unicode_and_utf8_rejection() {
    expect_error(R"json("\uD800")json",
                 JsonParseErrorCode::unpaired_unicode_surrogate, 1U, 1U, 2U,
                 "unpaired high surrogate");
    expect_error(R"json("\uDC00")json",
                 JsonParseErrorCode::unpaired_unicode_surrogate, 1U, 1U, 2U,
                 "unpaired low surrogate");
    expect_error(R"json("\uD800\u0041")json",
                 JsonParseErrorCode::unpaired_unicode_surrogate, 7U, 1U, 8U,
                 "high surrogate followed by non-low surrogate");
    expect_error(R"json("\u12x4")json",
                 JsonParseErrorCode::invalid_unicode_escape, 5U, 1U, 6U,
                 "non-hexadecimal Unicode escape");
    expect_error(R"json("\q")json", JsonParseErrorCode::invalid_escape, 2U, 1U,
                 3U, "unsupported string escape");

    std::string invalid_lead{"\""};
    invalid_lead.push_back(static_cast<char>(0xc0U));
    invalid_lead.push_back('"');
    expect_error(invalid_lead, JsonParseErrorCode::invalid_utf8, 1U, 1U, 2U,
                 "overlong UTF-8 lead byte");

    std::string bad_continuation{"\""};
    bad_continuation.push_back(static_cast<char>(0xe2U));
    bad_continuation.push_back('A');
    bad_continuation.push_back(static_cast<char>(0xacU));
    bad_continuation.push_back('"');
    expect_error(bad_continuation, JsonParseErrorCode::invalid_utf8, 2U, 1U,
                 3U, "invalid UTF-8 continuation byte");

    std::string encoded_surrogate{"\""};
    encoded_surrogate.push_back(static_cast<char>(0xedU));
    encoded_surrogate.push_back(static_cast<char>(0xa0U));
    encoded_surrogate.push_back(static_cast<char>(0x80U));
    encoded_surrogate.push_back('"');
    expect_error(encoded_surrogate, JsonParseErrorCode::invalid_utf8, 2U, 1U,
                 3U, "UTF-8 encoded surrogate scalar");

    const std::string control{"\"a\nb\""};
    expect_error(control, JsonParseErrorCode::unescaped_control_character, 2U,
                 1U, 3U, "unescaped control character");
}

void test_grammar_and_exact_multiline_locations() {
    expect_error("", JsonParseErrorCode::empty_input, 0U, 1U, 1U,
                 "empty document");
    expect_error(" \n\t", JsonParseErrorCode::empty_input, 3U, 2U, 2U,
                 "whitespace-only document");
    expect_error("true false", JsonParseErrorCode::trailing_content, 5U, 1U,
                 6U, "trailing root content");
    expect_error("tru", JsonParseErrorCode::invalid_literal, 3U, 1U, 4U,
                 "incomplete literal");
    expect_error("\"abc", JsonParseErrorCode::unexpected_end, 4U, 1U, 5U,
                 "unterminated string");
    expect_error("[1,]", JsonParseErrorCode::unexpected_token, 3U, 1U, 4U,
                 "array trailing comma");
    expect_error(R"json({"a":1,})json", JsonParseErrorCode::unexpected_token,
                 7U, 1U, 8U, "object trailing comma");
    expect_error("{a:1}", JsonParseErrorCode::expected_object_key, 1U, 1U,
                 2U, "non-string object key");
    expect_error(R"json({"a" 1})json", JsonParseErrorCode::expected_colon, 5U,
                 1U, 6U, "missing object colon");
    expect_error("[1 2]", JsonParseErrorCode::expected_comma_or_end, 3U, 1U,
                 4U, "missing array comma");

    constexpr std::string_view multiline =
        "{\r\n  \"a\": 1,\n  \"b\" 2\n}";
    const std::size_t fault = multiline.find('2');
    expect_error(multiline, JsonParseErrorCode::expected_colon, fault, 3U, 7U,
                 "CRLF/LF multiline location");
}

void test_input_depth_and_node_limits() {
    JsonParseLimits input_limit;
    input_limit.maximum_input_bytes = 4U;
    expect_error("null ", JsonParseErrorCode::input_limit_exceeded, 4U, 1U,
                 5U, "input byte limit", input_limit);
    expect(require_document("null", input_limit).root().is_null(),
           "exact input byte limit was rejected");

    JsonParseLimits depth_zero;
    depth_zero.maximum_depth = 0U;
    expect(require_document("7", depth_zero).root().number() == 7.0,
           "scalar root incorrectly consumed container depth");
    expect_error("[]", JsonParseErrorCode::depth_limit_exceeded, 0U, 1U, 1U,
                 "zero container depth", depth_zero);

    JsonParseLimits depth_one;
    depth_one.maximum_depth = 1U;
    expect(require_document(R"json({"x":1})json", depth_one).root().size() ==
               1U,
           "root container at exact depth limit was rejected");
    expect_error(R"json({"x":[]})json",
                 JsonParseErrorCode::depth_limit_exceeded, 5U, 1U, 6U,
                 "nested container depth limit", depth_one);

    JsonParseLimits two_nodes;
    two_nodes.maximum_nodes = 2U;
    expect_error("[null,true]", JsonParseErrorCode::node_limit_exceeded, 6U, 1U,
                 7U, "value-node count limit", two_nodes);

    JsonParseLimits three_nodes;
    three_nodes.maximum_nodes = 3U;
    JsonDocument exact_nodes = require_document("[null,true]", three_nodes);
    expect(exact_nodes.node_count() == 3U,
           "exact value-node count limit was rejected");

    JsonParseLimits zero_nodes;
    zero_nodes.maximum_nodes = 0U;
    expect_error("null", JsonParseErrorCode::node_limit_exceeded, 0U, 1U, 1U,
                 "zero node limit", zero_nodes);
}

void run_tests() {
    test_complete_dom_and_read_only_queries();
    test_document_move_preserves_existing_views();
    test_strings_escapes_unicode_and_embedded_nul();
    test_duplicate_decoded_keys_are_rejected();
    test_large_object_membership_and_order();
    test_strict_number_grammar_and_finite_binary64();
    test_unicode_and_utf8_rejection();
    test_grammar_and_exact_multiline_locations();
    test_input_depth_and_node_limits();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "JSON parser test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
