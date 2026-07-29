#include "identity/canonical_json_writer.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

namespace engine_sim_offline::identity::detail {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] bool is_continuation(unsigned char value) noexcept {
    return value >= 0x80U && value <= 0xbfU;
}

[[nodiscard]] bool is_valid_unicode_scalar_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first <= 0x7fU) {
            ++offset;
            continue;
        }

        if (first >= 0xc2U && first <= 0xdfU) {
            if (value.size() - offset < 2U ||
                !is_continuation(static_cast<unsigned char>(value[offset + 1U]))) {
                return false;
            }
            offset += 2U;
            continue;
        }

        if (first >= 0xe0U && first <= 0xefU) {
            if (value.size() - offset < 3U) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            if (!is_continuation(third) ||
                (first == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
                (first == 0xedU && (second < 0x80U || second > 0x9fU)) ||
                ((first != 0xe0U && first != 0xedU) && !is_continuation(second))) {
                return false;
            }
            offset += 3U;
            continue;
        }

        if (first >= 0xf0U && first <= 0xf4U) {
            if (value.size() - offset < 4U) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            const auto fourth = static_cast<unsigned char>(value[offset + 3U]);
            if (!is_continuation(third) || !is_continuation(fourth) ||
                (first == 0xf0U && (second < 0x90U || second > 0xbfU)) ||
                (first == 0xf4U && (second < 0x80U || second > 0x8fU)) ||
                ((first != 0xf0U && first != 0xf4U) && !is_continuation(second))) {
                return false;
            }
            offset += 4U;
            continue;
        }

        return false;
    }
    return true;
}

[[nodiscard]] std::array<char, 20> quoted_fixed_hex(std::uint64_t value) noexcept {
    std::array<char, 20> encoded{};
    encoded[0] = '"';
    encoded[1] = '0';
    encoded[2] = 'x';
    for (std::size_t index = 0; index < 16U; ++index) {
        const auto shift = static_cast<unsigned>((15U - index) * 4U);
        encoded[index + 3U] = kHexDigits[(value >> shift) & UINT64_C(0x0f)];
    }
    encoded[19] = '"';
    return encoded;
}

} // namespace

bool CanonicalJsonWriter::begin_object() {
    if (!before_value() || !append_byte('{')) {
        return false;
    }
    containers_.push_back(Container{ContainerKind::object, 0, false});
    return true;
}

bool CanonicalJsonWriter::end_object() {
    if (error_ != Error::none) {
        return false;
    }
    if (finished_ || containers_.empty() ||
        containers_.back().kind != ContainerKind::object ||
        containers_.back().awaiting_member_value) {
        return fail(Error::invalid_state,
                    "object may end only after all member values are complete");
    }
    if (!append_byte('}')) {
        return false;
    }
    containers_.pop_back();
    return true;
}

bool CanonicalJsonWriter::begin_array() {
    if (!before_value() || !append_byte('[')) {
        return false;
    }
    containers_.push_back(Container{ContainerKind::array, 0, false});
    return true;
}

bool CanonicalJsonWriter::end_array() {
    if (error_ != Error::none) {
        return false;
    }
    if (finished_ || containers_.empty() ||
        containers_.back().kind != ContainerKind::array) {
        return fail(Error::invalid_state, "array may end only while an array is open");
    }
    if (!append_byte(']')) {
        return false;
    }
    containers_.pop_back();
    return true;
}

bool CanonicalJsonWriter::key(std::string_view value) {
    if (error_ != Error::none) {
        return false;
    }
    if (finished_ || containers_.empty() ||
        containers_.back().kind != ContainerKind::object ||
        containers_.back().awaiting_member_value) {
        return fail(Error::invalid_state,
                    "a key requires an object that is ready for its next member");
    }
    if (!is_valid_unicode_scalar_utf8(value)) {
        return fail(Error::invalid_utf8,
                    "JSON object key is not valid Unicode scalar UTF-8");
    }

    auto &object = containers_.back();
    if ((object.value_count != 0U && !append_byte(',')) ||
        !append_escaped_string(value, false) || !append_byte(':')) {
        return false;
    }
    object.awaiting_member_value = true;
    return true;
}

bool CanonicalJsonWriter::string_value(std::string_view value) {
    return append_escaped_string(value, true);
}

bool CanonicalJsonWriter::uint32_value(std::uint32_t value) {
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1U> encoded{};
    const auto [end, conversion_error] =
        std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
    if (conversion_error != std::errc{}) {
        return fail(Error::unsupported_value, "could not encode uint32 value");
    }
    return before_value() &&
           append(std::string_view{encoded.data(),
                                   static_cast<std::size_t>(end - encoded.data())});
}

bool CanonicalJsonWriter::uint64_hex_value(std::uint64_t value) {
    const auto encoded = quoted_fixed_hex(value);
    return before_value() && append(std::string_view{encoded.data(), encoded.size()});
}

bool CanonicalJsonWriter::int64_string_value(std::int64_t value) {
    std::array<char, std::numeric_limits<std::int64_t>::digits10 + 2U> digits{};
    const auto [end, conversion_error] =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (conversion_error != std::errc{}) {
        return fail(Error::unsupported_value, "could not encode int64 value");
    }

    if (!before_value() || !append_byte('"') ||
        !append(std::string_view{digits.data(),
                                 static_cast<std::size_t>(end - digits.data())}) ||
        !append_byte('"')) {
        return false;
    }
    return true;
}

bool CanonicalJsonWriter::binary64_bits_value(double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);

    auto bits = std::bit_cast<std::uint64_t>(value);
    constexpr auto exponent_mask = UINT64_C(0x7ff0000000000000);
    constexpr auto magnitude_mask = UINT64_C(0x7fffffffffffffff);
    if ((bits & exponent_mask) == exponent_mask) {
        return fail(Error::non_finite_binary64,
                    "binary64 manifest values must be finite");
    }
    if ((bits & magnitude_mask) == 0U) {
        bits = 0;
    }
    return uint64_hex_value(bits);
}

bool CanonicalJsonWriter::sha256_value(const contract::Sha256Digest &value) {
    std::array<char, 66> encoded{};
    encoded.front() = '"';
    for (std::size_t index = 0; index < value.bytes.size(); ++index) {
        const auto byte = value.bytes[index];
        encoded[1U + index * 2U] = kHexDigits[(byte >> 4U) & 0x0fU];
        encoded[2U + index * 2U] = kHexDigits[byte & 0x0fU];
    }
    encoded.back() = '"';
    return before_value() && append(std::string_view{encoded.data(), encoded.size()});
}

bool CanonicalJsonWriter::bool_value(bool value) {
    return before_value() && append(value ? "true" : "false");
}

bool CanonicalJsonWriter::null_value() {
    return before_value() && append("null");
}

bool CanonicalJsonWriter::fail(Error error, std::string message) {
    if (error_ != Error::none) {
        return false;
    }
    if (error == Error::none) {
        error = Error::invalid_state;
        message = "writer failure requires a non-none error code";
    }
    error_ = error;
    error_message_ = std::move(message);
    return false;
}

bool CanonicalJsonWriter::finish(std::vector<std::byte> &output) {
    if (error_ != Error::none) {
        return false;
    }
    if (finished_ || !root_written_ || !containers_.empty()) {
        return fail(Error::invalid_state,
                    "a complete root value may be finished exactly once");
    }
    if (!append_byte('\n')) {
        return false;
    }
    output = std::move(bytes_);
    finished_ = true;
    return true;
}

CanonicalJsonWriter::Error CanonicalJsonWriter::error() const noexcept {
    return error_;
}

std::string_view CanonicalJsonWriter::error_message() const noexcept {
    return error_message_;
}

bool CanonicalJsonWriter::before_value() {
    if (error_ != Error::none) {
        return false;
    }
    if (finished_) {
        return fail(Error::invalid_state,
                    "no JSON value may be written after the document is finished");
    }
    if (containers_.empty()) {
        if (root_written_) {
            return fail(Error::invalid_state,
                        "a canonical JSON document contains exactly one root value");
        }
        root_written_ = true;
        return true;
    }

    auto &container = containers_.back();
    if (container.kind == ContainerKind::object) {
        if (!container.awaiting_member_value) {
            return fail(Error::invalid_state,
                        "an object value must follow a member key");
        }
        container.awaiting_member_value = false;
        ++container.value_count;
        return true;
    }

    if (container.value_count != 0U && !append_byte(',')) {
        return false;
    }
    ++container.value_count;
    return true;
}

bool CanonicalJsonWriter::append(std::string_view value) {
    if (error_ != Error::none) {
        return false;
    }
    if (value.size() > kMaximumCanonicalDocumentBytes - bytes_.size()) {
        return fail(Error::size_limit,
                    "canonical JSON document exceeds the 4 MiB limit");
    }
    for (const char character : value) {
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return true;
}

bool CanonicalJsonWriter::append_byte(char value) {
    if (error_ != Error::none) {
        return false;
    }
    if (bytes_.size() == kMaximumCanonicalDocumentBytes) {
        return fail(Error::size_limit,
                    "canonical JSON document exceeds the 4 MiB limit");
    }
    bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return true;
}

bool CanonicalJsonWriter::append_escaped_string(std::string_view value,
                                                bool begin_value) {
    if (error_ != Error::none) {
        return false;
    }
    if (!is_valid_unicode_scalar_utf8(value)) {
        return fail(Error::invalid_utf8,
                    "JSON string is not valid Unicode scalar UTF-8");
    }
    if ((begin_value && !before_value()) || !append_byte('"')) {
        return false;
    }

    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            if (!append("\\\"")) {
                return false;
            }
            break;
        case '\\':
            if (!append("\\\\")) {
                return false;
            }
            break;
        case '\b':
            if (!append("\\b")) {
                return false;
            }
            break;
        case '\t':
            if (!append("\\t")) {
                return false;
            }
            break;
        case '\n':
            if (!append("\\n")) {
                return false;
            }
            break;
        case '\f':
            if (!append("\\f")) {
                return false;
            }
            break;
        case '\r':
            if (!append("\\r")) {
                return false;
            }
            break;
        default:
            if (byte < 0x20U) {
                const std::array<char, 6> escape{'\\',
                                                 'u',
                                                 '0',
                                                 '0',
                                                 kHexDigits[byte >> 4U],
                                                 kHexDigits[byte & 0x0fU]};
                if (!append(std::string_view{escape.data(), escape.size()})) {
                    return false;
                }
            } else if (!append_byte(character)) {
                return false;
            }
            break;
        }
    }
    return append_byte('"');
}

} // namespace engine_sim_offline::identity::detail
