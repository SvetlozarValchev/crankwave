#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::identity::detail {

inline constexpr std::size_t kMaximumCanonicalDocumentBytes = 4U * 1024U * 1024U;

class CanonicalJsonWriter final {
  public:
    enum class Error {
        none,
        invalid_state,
        invalid_utf8,
        non_finite_binary64,
        unsupported_value,
        size_limit,
    };

    [[nodiscard]] bool begin_object();
    [[nodiscard]] bool end_object();
    [[nodiscard]] bool begin_array();
    [[nodiscard]] bool end_array();
    [[nodiscard]] bool key(std::string_view value);
    [[nodiscard]] bool string_value(std::string_view value);
    [[nodiscard]] bool uint32_value(std::uint32_t value);
    [[nodiscard]] bool uint64_hex_value(std::uint64_t value);
    [[nodiscard]] bool int64_string_value(std::int64_t value);
    [[nodiscard]] bool binary64_bits_value(double value);
    [[nodiscard]] bool sha256_value(const contract::Sha256Digest &value);
    [[nodiscard]] bool bool_value(bool value);
    [[nodiscard]] bool null_value();
    [[nodiscard]] bool fail(Error error, std::string message);

    [[nodiscard]] bool finish(std::vector<std::byte> &output);
    [[nodiscard]] Error error() const noexcept;
    [[nodiscard]] std::string_view error_message() const noexcept;

  private:
    enum class ContainerKind : std::uint8_t { object, array };
    struct Container {
        ContainerKind kind = ContainerKind::object;
        std::size_t value_count = 0;
        bool awaiting_member_value = false;
    };

    [[nodiscard]] bool before_value();
    [[nodiscard]] bool append(std::string_view value);
    [[nodiscard]] bool append_byte(char value);
    [[nodiscard]] bool append_escaped_string(std::string_view value, bool begin_value);

    std::vector<std::byte> bytes_;
    std::vector<Container> containers_;
    Error error_ = Error::none;
    std::string error_message_;
    bool root_written_ = false;
    bool finished_ = false;
};

} // namespace engine_sim_offline::identity::detail
