#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

namespace crankwave::authoring {

enum class JsonKind : std::uint8_t {
    invalid,
    null_value,
    boolean,
    number,
    string,
    array,
    object,
};

enum class JsonParseErrorCode : std::uint8_t {
    none,
    empty_input,
    input_limit_exceeded,
    depth_limit_exceeded,
    node_limit_exceeded,
    unexpected_end,
    unexpected_token,
    trailing_content,
    expected_object_key,
    expected_colon,
    expected_comma_or_end,
    duplicate_object_key,
    invalid_literal,
    invalid_number,
    number_out_of_range,
    non_finite_number,
    unescaped_control_character,
    invalid_escape,
    invalid_unicode_escape,
    unpaired_unicode_surrogate,
    invalid_utf8,
    allocation_failure,
    internal_failure,
};

struct JsonSourceLocation {
    // Byte offsets are zero-based. Lines and byte-columns are one-based. CRLF is one
    // line break; bare CR and LF are also line breaks.
    std::size_t byte_offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    friend bool operator==(const JsonSourceLocation &,
                           const JsonSourceLocation &) = default;
};

struct JsonParseLimits {
    std::size_t maximum_input_bytes = 4U * 1024U * 1024U;
    // Depth counts open array/object containers. A scalar root has depth zero.
    std::size_t maximum_depth = 128U;
    // Every JSON value counts once. Object member names do not count as values.
    std::size_t maximum_nodes = 262144U;

    friend bool operator==(const JsonParseLimits &,
                           const JsonParseLimits &) = default;
};

struct JsonParseError {
    static constexpr std::size_t kMaximumMessageBytes = 191U;

    JsonParseErrorCode code = JsonParseErrorCode::none;
    JsonSourceLocation location;
    std::array<char, kMaximumMessageBytes + 1U> message_bytes{};
    std::uint16_t message_size = 0;

    [[nodiscard]] std::string_view message() const noexcept {
        return {message_bytes.data(), message_size};
    }

    friend bool operator==(const JsonParseError &,
                           const JsonParseError &) = default;
};

namespace detail {
struct JsonDocumentStorage;
}

struct JsonMemberView;

// A cheap read-only view into a JsonDocument. Views survive moves of their owning
// document but become invalid when that document is destroyed or move-assigned.
class JsonValue final {
  public:
    JsonValue() noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] JsonKind kind() const noexcept;

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] std::optional<bool> boolean() const noexcept;
    [[nodiscard]] std::optional<double> number() const noexcept;
    [[nodiscard]] std::optional<std::string_view> string() const noexcept;

    // Array element count or object member count; zero for all other kinds.
    [[nodiscard]] std::size_t size() const noexcept;
    // Returns an invalid view when this is not an array or index is out of range.
    [[nodiscard]] JsonValue at(std::size_t index) const noexcept;
    // Returns an invalid view when this is not an object or the key is absent.
    [[nodiscard]] JsonValue find(std::string_view key) const noexcept;
    // Preserves source member order. Returns an invalid member view on mismatch.
    [[nodiscard]] JsonMemberView member_at(std::size_t index) const noexcept;

  private:
    JsonValue(const detail::JsonDocumentStorage *storage,
              std::size_t node_index) noexcept;

    const detail::JsonDocumentStorage *storage_ = nullptr;
    std::size_t node_index_ = 0;

    friend class JsonDocument;
    friend struct JsonMemberView;
};

struct JsonMemberView {
    std::string_view key;
    JsonValue value;
    bool present = false;

    [[nodiscard]] explicit operator bool() const noexcept { return present; }
};

class JsonDocument final {
  public:
    JsonDocument(JsonDocument &&) noexcept;
    JsonDocument &operator=(JsonDocument &&) noexcept;
    ~JsonDocument();

    JsonDocument(const JsonDocument &) = delete;
    JsonDocument &operator=(const JsonDocument &) = delete;

    [[nodiscard]] JsonValue root() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;

  private:
    explicit JsonDocument(std::unique_ptr<detail::JsonDocumentStorage> storage) noexcept;

    std::unique_ptr<detail::JsonDocumentStorage> storage_;

    friend std::variant<JsonDocument, JsonParseError>
    parse_json(std::string_view, JsonParseLimits) noexcept;
};

using JsonParseResult = std::variant<JsonDocument, JsonParseError>;

// Parses exactly one UTF-8 JSON value. The function is a no-throw public boundary:
// allocation and unexpected internal failures are returned as typed errors.
[[nodiscard]] JsonParseResult
parse_json(std::string_view input, JsonParseLimits limits = {}) noexcept;

} // namespace crankwave::authoring
